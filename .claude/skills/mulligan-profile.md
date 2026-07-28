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

Then `hands ≈ C(K+6,7)`, and runtime ≈ `hands × 2(pd) × R / (~110 rollouts/s/core × cores)`. Rough
guide: K=10 → 7.8k hands (Slivers), K=15 → 114k, K=24 → ~1M, K=60 (all 1-ofs) → infeasible. If K is
too large: lower R (keep tolerates it), or fall back to static/defaults for that deck.

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
- **Every run reports its settings and its slowest cells** — always on, ~free (one clock read + an atomic
  compare per rollout). `MTG_KEEP_SLOW_MS=<ms>` additionally *streams* live over-threshold rollouts.

### Manual / advanced path (pin every param — for chunking & pooling)

When you need the individual knobs — chunked / multi-machine runs, a specific R, a custom bucketing —
drive the gen directly. **Pin every discovery parameter**: the buckets (and thus the poolable
`bucket_fp`) depend on all of them, so they must match across any machines you intend to pool.

```bash
HASH=$(git rev-parse --short HEAD)          # play-logic identity, stamped into the sidecar
MTG_KEEP_EXHAUSTIVE=1 \
  MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 MTG_EQUIV_DEPTH=5 \  # bucketing (pinned)
  MTG_EQUIV_SEED=20260701 \                                          # FIXED bucket seed (all machines)
  MTG_KEEP_ROLLOUTS=<R> \                                            # R = label precision (per-cell rollouts)
  MTG_COMMIT="$HASH" \              # (bottoming always on; max_mull fixed at 6 = down to keep-1 -- no knob)
  ./build/Release/mtg-analyze decks/<name>/<name>.txt --cards-json src/cards/data/cards.json \
    --max-turns 8 --seed <SEED_BASE>                                 # SEED_BASE = the rollout run id
```

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

## Resuming an interrupted run & chunking (the "it didn't finish" path)

The one-flag `--gen-mulligan` run is **single-shot** — great for the normal case, but it does not
auto-resume. When a run is long enough that an interruption is likely (power failure, machine restart,
manual shutdown) or must be **split across machines**, drive it through the resumable/poolable chunk
driver instead. This is the separate path the simple flag deliberately leaves alone.

**Resumable single-machine run (stop and restart at will).** `test/exhaustive_chunked_gen.sh` journals
per-cell and continues from where it stopped — re-run the **identical** command to resume (it skips
already-completed cells). Nothing is lost but the few in-flight cells.

```bash
# CONTINUOUS pool: one adaptive floor->cap queue; stop (Ctrl-C / power loss / reboot) and RE-RUN to resume.
KM_CONTINUOUS=1 KM_DECK=decks/<name>/<name>.cod KM_TARGET_R=40 KM_FLOOR_R=2 \
  bash test/exhaustive_chunked_gen.sh
#   <-- interrupted? run the EXACT same line again; the <deck>.keepmodel.exhaustive.raw.json.journal
#       carries the completed cells. Cores stay full to the last live cell (no per-chunk barrier).
```

Round mode (`KM_TARGET_R`/`KM_ROUND_R`, prune-set carry) is the alternative: each round adds `KM_ROUND_R`
to the still-live frontier and freezes confident cells, so each round is cheaper and independently
resumable/poolable. Use it when you want explicit stop points at intermediate R.

**Manual carry (advanced).** A finished raw at a lower R (including a completed `--gen-mulligan recommend`
R=1 probe) can seed a higher-R run via `MTG_KEEP_PRIOR_RAW=<that.raw.json>` on an **adaptive**
(`0 < R_FLOOR < ROLLOUTS`) run — but it is **fingerprint-gated**: the carry is refused unless
`bucket_fp`/`deck_fp`/`equiv_seed`/`K` match, and it re-rolls any cells a changed `play_digest` touched.
That gate is the "don't assume it's safe" guard — a play-logic change since the prior invalidates it and
the affected cells are recomputed. (Getting the seed/accumulate semantics right is exactly why chunk
accumulation lives in the driver script, not the one-shot flag.)

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
