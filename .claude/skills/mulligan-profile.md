# Mulligan Profile Generation Skill

Authoritative guide for generating **exhaustive bucketed mulligan profiles** (the keep + bottom
policy) and pooling that (expensive) work across machines. This is a **separate, late stage** from
`analyze-deck` (which implements cards, checks coverage, and validates play) — read it whenever the
user asks to **generate / regenerate / pool / A-B / adopt a mulligan (keep or bottom) profile**, or to
**hand profile generation to the secondary machine**.

Full design + validation history: [docs/design/exhaustive-keep-policy.md](../../docs/design/exhaustive-keep-policy.md).

## What this produces

For a compressible deck (equivalent cards merge into few buckets), the mulligan decision is a pure
function of the hand's **bucket composition**, so every distinct hand is *evaluated* rather than a
model *fit*:
- `<deck>.keepmodel.exhaustive.profile.json` — the runtime policy (keep flags + bottoming targets).
- `<deck>.keepmodel.exhaustive.raw.json` — the poolable raw sidecar (per-comp rollout `sum`+`count` +
  fingerprints), the unit of cross-machine merging.

At runtime `AIEngine::KeepHand` consults the policy first (presence-gated: a hand it can't resolve
falls through, so decks without a profile are byte-identical). **Keep is default-on whenever the
profile is present, and every generated profile now bakes bottoming ON** — there is no
generation-time off switch (see "Bottoming" below). The `bottoming_enabled` flag still exists in the
format (legacy profiles / runtime), but generation always sets it true.

## Rule 0 — generate LATE, on a FROZEN commit

Exhaustive generation is **expensive *and* commit-bound**: the raw sidecar stamps a `commit`
fingerprint, and the merge tool refuses to pool sidecars from different play-logic. So a later card/rule
fix changes the commit and **invalidates every prior sidecar** for that deck. Therefore:

> **Only generate an exhaustive profile once the deck's cards are implemented, reviewed, and play is
> validated (claude-play sweep / regression clean), and you don't expect further commits that change
> rollout results.** Never spend the rollout hours while play may still have bugs.

Corollary: on the exhaustive route we **skip `analyze-deck`'s static-profile grid entirely** (it is its
own expensive optimisation; don't pay for a baseline you're about to replace).

### Rule 0 binds GENERATION, not a profile's lifetime — do NOT regenerate for every later commit

Distinguish two things agents keep conflating (and getting stuck on the wrong one):

- **Mid-generation invalidation (real).** A play-logic change *during* a gen — or across machines you
  pool — is genuine invalidation: cells rolled before vs after the change use different play, so the table
  ends up **skewed / unbalanced across entries** (some entries evaluated on old play, some on new). That
  is the whole reason to freeze on ONE commit, and why the merge refuses to pool sidecars from different
  commits.
- **After-generation "invalidation" (a non-problem).** A finished, validated profile does **not** expire
  when a *later, unrelated* commit moves its `play_digest`. The whole table was generated under one
  consistent play logic, so it is internally balanced; a moved digest only means "these rollouts would
  differ slightly if re-rolled today", not "this profile is now wrong". **You cannot — and must not —
  regenerate every deck's profile for every engine commit.**

The `play_digest`/`commit` fingerprints gate **pooling and resume** (the mid-gen concerns), NOT runtime
*use*: at play time the profile is presence-gated and applied regardless of digest. So:

> A profile stays adopted until it demonstrably **underperforms on the current engine**. The bar for
> keeping/adopting is **net benefit under the CONFOUNDED bottoming A/B + no major regression in the
> rebaseline on today's engine** — NOT a matching `play_digest`. Regenerate only when a change plausibly
> and materially moves *this deck's* play (or the deck's list changes), confirmed by a **regression**,
> not by a digest diff.

## The three mulligan tiers over a deck's life

| tier | policy | cost | when |
|---|---|---|---|
| **1. Defaults** (`MulliganProfile::DefaultProfile`) | generic, deck-agnostic | free | earliest card/rules bug-shaking — mulligan quality doesn't affect rules-correctness. May be bad for an atypical deck. |
| **2. Low-R exhaustive** | deck-aware keep **+ bottoming** (both on) | minutes | once you want the deck playing *sensibly* while polishing. Doubles as a pipeline smoke. |
| **3. High-R exhaustive** | more-precise keep + bottoming | hours–days (secondary machine) | after play is frozen — the definitive, adopt-able profile. |

Note **static is skipped** — the deck runs on defaults, then a low-R exhaustive, then the high-R
profile. Both keep AND bottoming are ON at every tier (there is no bottoming-off generation option). Keep
is robust at low R (it beats the *trained* static baseline by ~0.03t in-game at R=20). Bottoming's argmin
over sub-comps is more R-sensitive, so higher R sharpens it — but blind bottoming is the correct
blind-to-shuffle policy at *any* R (confound correction below), so you never ship it off; you just gain
precision with R. Confirm each profile with the confounded A/B.

## Feasibility pre-check (do this first)

Distinct-hand count ≈ `C(K+6,7)` where K is the effective bucket count. **1-ofs are the killers** (each
is a fresh dimension); 4-ofs self-collapse. Cheap check without a full run:

```bash
# ~1 min: prints the merged buckets so you can read K.
MTG_EQUIV_DISCOVER=1 MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 \
  ./build/Release/mtg-analyze decks/<name>/<name>.txt --cards-json src/cards/data/cards.json --max-turns 8
```

**Fetchlands ALWAYS merge, as a class, regardless of what the threshold says** (user, 2026-08-14).
This is now BY CONSTRUCTION in `DiscoverEquivalence` — no flag, nothing to remember — alongside the
goldfish-inert merge. It exists because on FiveColour the documented `0.01` threshold merged NOTHING
(K=31, every card its own dimension): the five fetchlands measure 0.0125–0.035 apart, just above it.
Merging that one cycle is worth **2.86x** (5,655,953 size-7 hands → 1,977,898, measured). Leaving a
known equivalence to a distance threshold makes a 2.9x cost difference a deck-by-deck coin flip. The
rule stops at fetchlands: duals measure ~5x further (0.07+) and that is real signal on any deck whose
mana creatures scale with colours. See `docs/design/fivecolour-mulligan-and-slow-atom.md`.

**A discovery cache from before a discovery change is stale.** The cache's fingerprint covers
discovery's INPUTS; `kDiscoveryVersion` (in `ExhaustiveKeep.cpp`) covers what discovery DOES with
them. Bump it whenever the clustering can move for unchanged inputs, or every deck with an existing
cache silently keeps its old buckets.

Then `hands ≈ C(K+6,7)`, and runtime ≈ `hands × 2(pd) × R / (~110 rollouts/s/core × cores)`. Rough
guide: K=10 → 7.8k hands (Slivers), K=15 → 114k, K=24 → ~1M, K=60 (all 1-ofs) → infeasible. If K is
too large: lower R (keep tolerates it), or fall back to static/defaults for that deck.

## NEVER generate a table for a union/superset deck

Standing user directive (2026-08-18, restated absolutely 2026-08-19: *"I never want a union deck!"*,
*"Absolutely never."*). The banned object is a DECKLIST nobody would play. A union TABLE covering
the plausible 60-card combinations you are actually testing is fine and is the right way to share
an apparatus — *"A union table based on a number of different plausible combinations that we want
to test is different"* (user). Test: **is every cell reachable by an arm you will actually run?**
Scope coverage to the arms of the CURRENT test, never widen it to serve hypothetical later tests.
Pool per-arm tables with `scripts/keepstore.py` where they already exist. A superset raises K (cells go as `C(K+6,7)`) and most of its cells are
unreachable by any arm — measured at 31.7% — so the hours buy apparatus nothing can query. One
violation cost 5+ hours on a 74-card / K=20 / 1,167,340-cell table. If a comparison seems to need
one, ASK. Full rationale: `.claude/skills/deck-screening.md` Rule 0a.

## Generating a profile

### The normal path — one flag (`--gen-mulligan`)

For almost every deck, don't hand-set the `MTG_KEEP_*`/`MTG_EQUIV_*` knobs — use the recipe flag. It
takes **no parameters but the recipe**, pulls the rollout depth/budget from the deck's **play profile**
(`value_play`), and prints its effective settings so a run is self-documenting.

```bash
# 1) SCOUT first (cheap, bounded): one rollout of every cell -> a wall-clock estimate + the slowest cells.
./build/Release/mtg-analyze decks/<name>/<name>.cod --cards-json src/cards/data/cards.json \
    --gen-mulligan recommend
#    -> GEN-TIME PROJECTION: complete ~Xh / fast ~Yh vs the ~8h overnight window (MTG_KEEP_OVERNIGHT_H),
#       and the top-12 slowest rollouts (watch for a degenerate combo cell BEFORE committing hours).

# 2) Then generate with the recipe you chose from the estimate:
./build/Release/mtg-analyze decks/<name>/<name>.cod --cards-json src/cards/data/cards.json \
    --gen-mulligan complete      # full bottoming, R40 -- the definitive profile (default when it fits)
#   --gen-mulligan fast          # adaptive bottoming, R30 -- ~a third off gen for slow/large-K decks
```

- **complete** = full bottoming + cap R40; **fast** = adaptive bottoming + cap R30; keep is always
  adaptive (floor 2). See the recipe study (quality cost ≤ ~0.02t; fast saves ~22–38% of gen).
- **Depth/budget** come from `value_play` (`target_depth`/`budget_ms`); set the optional
  `value_play.mull_gen_depth` / `mull_gen_budget_ms` in `<deck>.value.json` when mulligan gen should run at
  a **cheaper depth than shipped play** for cost (0/unset → inherit the play depth → built-in default).

### Three properties every run has, that nothing can switch off

`docs/design/keepgen-no-off-switches.md`. There is **one execution path** and it is always:

- **Continuous / batched.** A single pooled queue covers discovery, the play-digest battery, the
  sub-tables and the size-7 floor+refine. No phase joins before another starts, so cores go idle only on
  the final live cell. There is no schedule to select (the old `MTG_KEEP_R_FLOOR` didn't tune the
  schedule, it *selected* a uniform one — and was the env route's **default**). The floor is derived;
  **a cap R below 2 is rejected**, not silently downgraded.
- **Slow-game reporting.** Every rollout in every phase is timed. Anything ≥ 30 s streams live to stderr
  *and* to `<raw>.slow.log` with the seed that reproduces it; the top-12 print at each heartbeat, at
  floor-complete and at end-of-run. `MTG_KEEP_SLOW_MS` may only **lower** the stream threshold — `=0` no
  longer disables it (that is how the Creature Giving scout ended up with no slow list at all).
- **Incremental.** Every completed cell is journalled as it commits, so **re-running the identical
  command resumes** — there is no resume flag and no driver script. Buckets are cached next to the deck
  (`<stem>.keepmodel.gencache.json`), a `recommend` probe is reused as the real gen's r=0 slice
  automatically, and an interrupted run's `.journal` can be pooled directly (below), so nothing is repaid
  and nothing is stranded.

### Manual / advanced path (pin every param — for multi-machine pooling)

Same engine, same single path — this route exists only to pin the *bucketing* and the seed across
machines. **Pin every discovery parameter**: the buckets (and thus the poolable `bucket_fp`) depend on
all of them, so they must match across any machines you intend to pool. There is no floor/schedule knob;
`MTG_KEEP_ROLLOUTS` is the cap R and must be ≥ 2.

```bash
MTG_KEEP_EXHAUSTIVE=1 \
  MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 MTG_EQUIV_DEPTH=5 \  # bucketing (pinned)
  MTG_EQUIV_SEED=20260701 \                                          # FIXED bucket seed (all machines)
  MTG_KEEP_ROLLOUTS=<R> \                                            # cap R (>=2); floor is derived
  ./build/Release/mtg-analyze decks/<name>/<name>.txt --cards-json src/cards/data/cards.json \
    --max-turns 8 --seed <SEED_BASE>                                 # SEED_BASE = the rollout run id
```

(`MTG_COMMIT` is optional: the recipe path auto-stamps the sidecar from `git rev-parse --short HEAD`,
`+dirty` on an unclean tree. Bottoming is always on; `max_mull` is fixed at 6 — no knobs.)

**Prefer the recipe even here.** For a second machine, the modern form of "chunking" is simply: same
commit, same recipe, **a different `--seed`**, then pool with `MTG_KEEP_MERGE`. Low-R uniform chunks are
no longer generatable.

> **⚠ A second chunk on the SAME MACHINE overwrites the first.** The recipe writes to a fixed path per
> deck (`<stem>.keepmodel.exhaustive.{profile,raw}.json`), so running chunk 2 for the same deck
> silently destroys chunk 1's raw and profile on disk — the merge then has only one input to pool.
> **Commit chunk 1 (as `.gz`) or copy its raw aside BEFORE starting chunk 2.** Recovered once via
> `git show HEAD:<path>.gz | gunzip > /tmp/chunk1.raw.json` only because it had already been
> committed. This does not arise in the cross-machine flow (separate filesystems), which is why the
> handoff section above never mentions it.

**Pooling gates on the PLAY DIGEST, not the commit string** (verified 2026-08-24, Mirrorwing). Two
chunks whose `commit` stamps differed (11 commits apart) pooled fine because `play_digest`,
`bucket_fp`, `deck_fp` and `K` all matched — the merge printed `pooled 2 file(s); 2 distinct
seed_base(s); effective R=60`. So the parity checklist's "same commit" is a *sufficient* condition,
not a necessary one: what must actually match is the play logic those fingerprints capture. Confirm
it the Rule 0 way (the deck's GT keys unmoved by the intervening commits), not by assuming.

**Pooling is the cheap route to `complete`-grade R when `complete` does not fit the window.** Two
`fast` chunks (R30 each, ~10 h + ~11 h) pool to **R=60** — above `complete`'s R40 — for less wall
clock than one `complete` run (36.4 h projected on that deck), and neither chunk ever occupies the
box for longer than a single overnight. Validate the pooled table with the in-game A/B, never the
merge's own `D_opt` (winner's-curse optimistic).

The report prints per-size hand counts, the optimal-vs-static policy gap, the disagreement
decomposition, the label-noise diagnostic, and a **projected-regret-vs-R curve** (extrapolated) — read
it to pick the minimum viable R for the deck.

### R selection

- Keep is robust: **R=20 already captures ~94% of the keep gap** on Slivers (residual label-noise
  regret ~0.004t). R=100 is comfortably definitive.
- Bottoming is **not** robust at low R (next section) — it needs the high-R run.
- **Hard floor: R < 10 cannot produce a runtime profile.** A profile below R=10 is too noisy to be
  usable, so it is *structurally* unshippable — a sub-10 single run (or a partial merge whose pooled
  per-cell R < 10) writes **only a poolable raw chunk**, never a `.profile.json`, and says so loudly. To
  get a profile from low-R rollouts you must **merge chunks up to R ≥ 10** (that is the only path). The
  floor is a hard constant (not env-overridable — an override would just re-arm the footgun). It changes
  no real recipe: `complete` (R40) and `fast` (R30) clear it by a wide margin. The one exemption is the
  offline reconstruction probe (`MTG_KEEP_SYNTH_R`, next-but-one section), a deliberate in-game-A/B
  experiment that is allowed to build a low-R profile.

## Bottoming: always ON (no generation off switch)

Blind exhaustive bottoming is the table's **second purpose** and is baked ON in every generated profile.
There is deliberately **no** generation-time off switch (a bottoming-off profile is a footgun no agent
should be able to ship). What follows is *why* that's safe, and how to confirm it per profile.

The blind exhaustive bottoming (`bottom_keep` = the argmin `(7-m)`-subcomposition) is jointly optimal
*in expectation*, but at low R the argmin mis-ranks near-tie subhands. Measured on Slivers R=20: of 50
d3 games where exhaustive lost to lookahead bottoming, re-scoring the kept subhands at R=400 attributed
**28 to R=20 label noise** (exhaustive kept a genuinely blind-worse hand, mean −0.11t), only 18 to
clairvoyance, 4 ties — i.e. the R=20 "loss" was *mostly fixable R-noise, not clairvoyance*. The
confound correction (see the `bottoming_enabled` note below) later showed that even the residual
"clairvoyance loss" is a **test artifact** (lookahead scored on its peeked library); under the
confounded A/B blind bottoming wins. So the fix is more R (and the confounded A/B as the gate), **not**
shipping bottoming off.

`bottoming_enabled` is **baked into the profile and generation always sets it true** — `MTG_KEEP_BOTTOMING`
is no longer read (the off switch was removed, commit ea2d530). At runtime `BottomCards` follows the profile
flag; `MTG_EXHAUSTIVE_BOTTOM` remains a **3-state A/B override** for *testing only* (unset → follow the flag,
`0` → force off, `1` → force on) — it changes play ephemerally and writes no profile, so it's the safe way to
isolate bottoming in an A/B. Keep is independent of this. **Decks with no exhaustive table fall back to
lookahead bottoming** (byte-identical).

**Why default-on (the confound correction).** Blind exhaustive bottoming is the *theoretically correct*
policy when you are blind to the shuffle — a real player can't peek at the library the way the clairvoyant
lookahead bottomer does (it rolls out the game's actual library and picks the removal best FOR that
library). The naive in-game bottoming A/B is **confounded**: it scores lookahead on the very library it
peeked at, so lookahead "wins" for free. **Remove the peek and the table wins.** Two proofs:
- **Confounded in-game A/B** (`MTG_CONFOUND_BOTTOM=1`, reshuffles the library *after* the bottoming
  decision so the playout draws are decorrelated from the peek; `test/keepmodel_burn_confound.sh`): burn
  d5 flipped from blind **+0.076t (naive "loss")** to blind **−0.0098t (win), 11/16 seeds**.
- **R=400 blind-EV probe** (`MTG_SCORE_COMPS`): the table's stored bottom pick IS the blind-argmin over
  fresh shuffles; lookahead's library-specific deviations are blind-*worse* by 0.5–1.9t.
So a low-R attribution "loss" to lookahead is mostly the peek confound plus fixable table R-noise, NOT a
real defect. **Confirm each new profile with the CONFOUNDED A/B** (`MTG_CONFOUND_BOTTOM`) and check that
blind ≥ lookahead — this has held on every profile so far. There is no ship-off escape hatch, so a
profile that ever *failed* the confounded A/B would be a signal to fix the bottoming heuristic or raise R
(a modeling/quality problem), NOT to disable bottoming. Note the GT tradeoff: because the *standard* (unconfounded) goldfish metric still rewards the peek, enabling blind
bottoming shows a small win-turn *increase* on mulligan games — a deliberate, honest shift to accept via
per-game audit, not a regression.

## Post-hoc reconstruction: test lower R / adaptive bottoming without re-rollout

Generate the full profile **once**, then reconstruct cheaper variants from its raw sidecar (which stores
per-cell `mean+variance+count`) with **no rollouts** — the full run is a strict superset of every cheaper
variant, so never re-generate a subset. In the merge path (`MTG_KEEP_MERGE`):

| env | effect |
|---|---|
| `MTG_KEEP_SYNTH_R=k`        | resample all tables to R=k → a lower-R profile (the **required-R / keep** question) |
| `MTG_KEEP_SYNTH_SEED=…`     | deterministic RNG seed |

Then A/B each `SYNTH_R` reconstruction against the full profile **in game** with
`test/keep_reconstruct_ab.sh` (attaches via `MTG_EXHAUSTIVE_PROFILE`, no `decks/`/GT churn) to find the
required-R knee. **The reconstruction's reported `D_opt` is winner's-curse optimistic — do NOT rank
variants by it; use the in-game delta.**

**Adaptive vs full BOTTOMING is a different question — do NOT use `SYNTH_BOTTOM_R` for it.** A uniform
resample of every sub-cell to one R corrupts the bottoming `argmin` with noise and over-states the cost
2.6×+ (slivers: real +0.0057 vs `SYNTH_BOTTOM_R=2` +0.236). Use the **offline regret simulator** instead
— it replays the gen's variance-driven refinement (floor → refine argmin cells → refined-only argmin) and
scores on the true means, reproducing the in-game A/B for ~free:

```bash
MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS=<full-bottom.raw.json> \
  MTG_KEEP_SIM_ADAPTIVE_BOTTOM=1 MTG_KEEP_SIM_FLOOR=2 MTG_KEEP_SIM_TRIALS=128 \
  MTG_MERGE_OUT_PROFILE=/tmp/ig.json MTG_MERGE_OUT_RAW=/tmp/ig.raw.json \
  ./build/Release/mtg-analyze <deck> --cards-json src/cards/data/cards.json   # prints regret=+Xt
```

Validated to ±0.0001t on slivers (+0.0058 sim vs +0.0057 in-game). Full recipe + rationale:
[docs/design/mulligan-reconstruct-lower-r.md](../../docs/design/mulligan-reconstruct-lower-r.md).

## Resuming an interrupted run (the "it didn't finish" path)

**Run the identical command again. That is the whole procedure.** There is no resume flag, no driver
script and nothing to remember: the recipe pins the seed, every completed cell is journalled to
`<raw>.journal` as it commits, and startup replays it and skips those cells. A crash/reboot/Ctrl-C loses
only the handful of in-flight cells. Bucketing is not re-derived either (it is cached next to the deck).

```bash
# start ... and resume: the SAME line, whatever happened in between.
./build/Release/mtg-analyze decks/<name>/<name>.cod --cards-json src/cards/data/cards.json \
    --gen-mulligan complete
```

(There used to be a `test/exhaustive_chunked_gen.sh` driver for this. It is **deleted**: its round mode
ran the uniform, non-journalled, barriered path — strictly worse than re-running the recipe.)

**Pooling an interrupted run.** A killed run's journal is itself a valid merge input, so its rollouts are
never stranded — you can ship or pool them without finishing the run:

```bash
MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="decks/<n>/<n>.keepmodel.exhaustive.raw.json.journal" \
  ./build/Release/mtg-analyze decks/<n>/<n>.txt --cards-json src/cards/data/cards.json
```

**Manual carry (advanced).** A finished raw at a lower R can seed a higher-R run via
`MTG_KEEP_PRIOR_RAW=<that.raw.json>` — **fingerprint-gated**: the carry is refused unless
`bucket_fp`/`deck_fp`/`equiv_seed`/`K` match, and it re-rolls any cells a changed `play_digest` touched.
That gate is the "don't assume it's safe" guard — a play-logic change since the prior invalidates it and
the affected cells are recomputed. (A `recommend` probe needs no flag at all: it is picked up
automatically as the next gen's r=0 slice.)

## Multi-machine handoff (pooling with the secondary machine)

Pooling sums raw sidecars element-wise. It is valid **only** when the runs agree on play-logic, buckets
and deck, with **disjoint** rollout streams — the merge tool enforces this via fingerprints.

**Parity checklist (what makes sidecars poolable):**
1. **Same build/commit** → identical play logic. Secondary must `git checkout <same commit>` and build;
   `MTG_COMMIT=<hash>` must match (merge rejects mismatch).
2. **Same `decks/<name>/<name>.txt` + `cards.json`** → matching `deck_fp`.
3. **Same buckets** → matching `bucket_fp`. Requires the **same committed base `.profile.json`** and
   the **same pinned discovery params** (`MTG_EQUIV_PROBES/THRESHOLD/DEPTH/BUDGET`, `MTG_EQUIV_SEED`,
   `--max-turns`).
4. **Distinct `--seed` (seed_base) per machine** → disjoint continuations. The merge rejects overlap.

**Determinism handshake (do this ONCE before trusting a pool):** both machines run a tiny *identical*
config (same `--seed`, e.g. `MTG_KEEP_ROLLOUTS=2`) and you confirm the raw sidecars
have **identical `bucket_fp` and `deck_fp`** and **byte-identical V** on that shared seed. Only if they
match is cross-machine rollout determinism holding (the shuffle is portable across Linux/Windows, but
verify per pair). Then discard those and run **distinct-seed** production runs.

> **Pin `MTG_KEEP_REFS_OFFSET=0` for the handshake.** At the shipped default (2) the freeze shrink
> target is re-derived on a *timing-triggered* schedule, so a cell sitting on the freeze threshold can
> stop at a different R between two runs of **the same binary and seed** (measured on burn R=10: one
> size-7 cell in 330, ~1 run in 3). That is accepted in production but would make the handshake fail for
> a reason that has nothing to do with cross-machine parity. offset=0 is deterministic by construction.

**Seed allocation:** give each machine a distinct seed prefix (e.g. primary `1xxxxxxx`, secondary
`2xxxxxxx`) and keep a per-deck ledger of used `seed_base`s; the merge's overlap-guard is the backstop,
not the plan.

**Transfer + merge:** the secondary sends back its `<deck>.keepmodel.exhaustive.raw.json`; then:

```bash
MTG_KEEP_MERGE=1 \
  MTG_MERGE_INPUTS="decks/<name>/<name>.keepmodel.exhaustive.raw.json,/path/to/secondary.raw.json" \
  ./build/Release/mtg-analyze decks/<name>/<name>.txt --cards-json src/cards/data/cards.json
  # (bottoming is always baked on -- no flag)
```

The merge rebuilds the policy at the pooled R via the same code the in-run path uses (identical output)
and re-emits a merged sidecar listing all `pooled_seed_bases`, so merges chain.

**Work allocation:** the slow box adds effective-R with no coordination (contribution ~ speed × time;
`1/√R` diminishing). It is usually worth more pointed at a **different / harder deck** than piling R on
one already resolved.

## Validating a profile (A/B)

```bash
# KEEP: exhaustive keep vs static; bottoming held to lookahead both sides (isolates the keep decision).
KM_DECK=decks/<name>/<name>.txt KM_MODE=keep   bash test/keepmodel_exhaustive_ab.sh
# BOTTOM: exhaustive vs lookahead bottoming; keep held to exhaustive both sides (isolates bottoming).
KM_DECK=decks/<name>/<name>.txt KM_MODE=bottom bash test/keepmodel_exhaustive_ab.sh
```

Reports per-depth win-turn deltas over 16–24 seeds × depths 0/3/5. Negative = exhaustive wins.
(`MTG_DUMP_WINS` writes `[win] gi=N wt=M` to **stderr** — the harness parses the `err_` files.)

**Diagnosing a bottoming loss (clairvoyance vs R-noise):** identify losing `(seed, gi)` from the two
runs' win dumps (game index is stable — same library both sides), log both with `--log-dir` to read the
kept subhands, then re-score each kept subhand at high R with `MTG_SCORE_COMPS` (parallel scorer;
`MTG_SCORE_FILE` = lines `H:c0,...,cK-1`, `MTG_SCORE_R`). If exhaustive's kept hand is blind-*better* at
high R → clairvoyance (acceptable); if blind-*worse* → R-noise (raise R).

## Adoption

- **Keep:** presence-gated — it takes effect the moment the profile ships. No flag.
- **Bottoming:** always on (generation bakes `bottoming_enabled=true`; no off switch). It ships with the
  profile; confirm it with the confounded A/B (`MTG_CONFOUND_BOTTOM`, `KM_MODE=bottom`) as a sanity check,
  not a gate. A failure means fix the heuristic / raise R, not disable bottoming.

## Artifacts

- **Commit** the definitive high-R `.profile.json` (and its `.raw.json` for reproducibility / further
  pooling).
- **Do NOT commit** throwaway low-R profiles/sidecars — regenerate at the target R.
- Profiles live in the deck's folder `decks/<name>/` (per repo convention — the decklist,
  profile, and all sibling models share one folder); rollout logs/scratch under `logs/`.
  Commit the gzipped `<name>.keepmodel.exhaustive.raw.json.gz`; the uncompressed raw is gitignored.
