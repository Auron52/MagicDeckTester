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
profile is present. Bottoming is governed by the profile's `bottoming_enabled` flag** (below).

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
| **2. Low-R exhaustive, keep-only** | deck-aware keep, **bottoming OFF** (lookahead) | minutes | once you want the deck playing *sensibly* while polishing. Doubles as a pipeline smoke. |
| **3. High-R exhaustive** | accurate keep **+ bottoming** | hours–days (secondary machine) | after play is frozen — the definitive, adopt-able profile. |

Note **static is skipped** — the deck runs on defaults, then a low-R exhaustive keep, then the high-R
profile. Keep is robust at low R (it beats the *trained* static baseline by ~0.03t in-game at R=20). Bottoming
is noisier at low R (its argmin over sub-comps is more R-sensitive), but the confound correction below
shows blind bottoming is the correct blind-to-shuffle policy at any R — validate each profile with the
confounded A/B rather than assuming low-R must ship off.

## Feasibility pre-check (do this first)

Distinct-hand count ≈ `C(K+6,7)` where K is the effective bucket count. **1-ofs are the killers** (each
is a fresh dimension); 4-ofs self-collapse. Cheap check without a full run:

```bash
# ~1 min: prints the merged buckets so you can read K.
MTG_EQUIV_DISCOVER=1 MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 \
  ./build/Release/mtg-analyze decks/<name>.txt --cards-json src/cards/data/cards.json --max-turns 8
```

Then `hands ≈ C(K+6,7)`, and runtime ≈ `hands × 2(pd) × R / (~110 rollouts/s/core × cores)`. Rough
guide: K=10 → 7.8k hands (Slivers), K=15 → 114k, K=24 → ~1M, K=60 (all 1-ofs) → infeasible. If K is
too large: lower R (keep tolerates it), or fall back to static/defaults for that deck.

## Generating a profile

**Pin every discovery parameter** — the buckets (and thus the poolable `bucket_fp`) depend on all of
them, so they must match across any machines you intend to pool.

```bash
HASH=$(git rev-parse --short HEAD)          # play-logic identity, stamped into the sidecar
MTG_KEEP_EXHAUSTIVE=1 \
  MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 MTG_EQUIV_DEPTH=5 \  # bucketing (pinned)
  MTG_EQUIV_SEED=20260701 \                                          # FIXED bucket seed (all machines)
  MTG_KEEP_ROLLOUTS=<R> MTG_KEEP_MAXMULL=3 \                         # R = label precision; depth of mull
  MTG_COMMIT="$HASH" \
  MTG_KEEP_BOTTOMING=0 \                                             # bottoming_enabled (see below)
  ./build/Release/mtg-analyze decks/<name>.txt --cards-json src/cards/data/cards.json \
    --max-turns 8 --seed <SEED_BASE>                                 # SEED_BASE = the rollout run id
```

The report prints per-size hand counts, the optimal-vs-static policy gap, the disagreement
decomposition, the label-noise diagnostic, and a **projected-regret-vs-R curve** (extrapolated) — read
it to pick the minimum viable R for the deck.

### R selection

- Keep is robust: **R=20 already captures ~94% of the keep gap** on Slivers (residual label-noise
  regret ~0.004t). R=100 is comfortably definitive.
- Bottoming is **not** robust at low R (next section) — it needs the high-R run.

## Bottoming: ships OFF until a validated high-R profile

The blind exhaustive bottoming (`bottom_keep` = the argmin `(7-m)`-subcomposition) is jointly optimal
*in expectation*, but at low R the argmin mis-ranks near-tie subhands. Measured on Slivers R=20: of 50
d3 games where exhaustive lost to lookahead bottoming, re-scoring the kept subhands at R=400 attributed
**28 to R=20 label noise** (exhaustive kept a genuinely blind-worse hand, mean −0.11t), only 18 to
clairvoyance, 4 ties — i.e. the R=20 "loss" was *mostly fixable R-noise, not clairvoyance*. The
confound correction (see the `bottoming_enabled` note below) later showed that even the residual
"clairvoyance loss" is a **test artifact** (lookahead scored on its peeked library); under the
confounded A/B blind bottoming wins. So the fix is more R (and the confounded A/B as the gate), **not**
shipping bottoming off.

`bottoming_enabled` is **baked into the profile** (generation/merge default is now **ON**; set
`MTG_KEEP_BOTTOMING=0` to ship a profile with it off). At runtime `BottomCards` follows the profile flag;
`MTG_EXHAUSTIVE_BOTTOM` is a **3-state A/B override**: unset → follow the flag, `0` → force off, `1` →
force on. Keep is independent of this. **Decks with no exhaustive table fall back to lookahead bottoming**
(byte-identical).

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
real defect. **Adoption gate for each new profile = run the CONFOUNDED A/B** (`MTG_CONFOUND_BOTTOM`) and
confirm blind ≥ lookahead; if a profile fails it, ship that one with `MTG_KEEP_BOTTOMING=0`. (We are still
validating the next couple profiles this way before treating default-on as fully settled.) Note the GT
tradeoff: because the *standard* (unconfounded) goldfish metric still rewards the peek, enabling blind
bottoming shows a small win-turn *increase* on mulligan games — a deliberate, honest shift to accept via
per-game audit, not a regression.

## Multi-machine handoff (pooling with the secondary machine)

Pooling sums raw sidecars element-wise. It is valid **only** when the runs agree on play-logic, buckets
and deck, with **disjoint** rollout streams — the merge tool enforces this via fingerprints.

**Parity checklist (what makes sidecars poolable):**
1. **Same build/commit** → identical play logic. Secondary must `git checkout <same commit>` and build;
   `MTG_COMMIT=<hash>` must match (merge rejects mismatch).
2. **Same `decks/<name>.txt` + `cards.json`** → matching `deck_fp`.
3. **Same buckets** → matching `bucket_fp`. Requires the **same committed base `.profile.json`** and
   the **same pinned discovery params** (`MTG_EQUIV_PROBES/THRESHOLD/DEPTH/BUDGET`, `MTG_EQUIV_SEED`,
   `--max-turns`).
4. **Distinct `--seed` (seed_base) per machine** → disjoint continuations. The merge rejects overlap.

**Determinism handshake (do this ONCE before trusting a pool):** both machines run a tiny *identical*
config (same `--seed`, e.g. `MTG_KEEP_ROLLOUTS=2 MTG_KEEP_MAXMULL=1`) and you confirm the raw sidecars
have **identical `bucket_fp` and `deck_fp`** and **byte-identical V** on that shared seed. Only if they
match is cross-machine rollout determinism holding (the shuffle is portable across Linux/Windows, but
verify per pair). Then discard those and run **distinct-seed** production runs.

**Seed allocation:** give each machine a distinct seed prefix (e.g. primary `1xxxxxxx`, secondary
`2xxxxxxx`) and keep a per-deck ledger of used `seed_base`s; the merge's overlap-guard is the backstop,
not the plan.

**Transfer + merge:** the secondary sends back its `<deck>.keepmodel.exhaustive.raw.json`; then:

```bash
MTG_KEEP_MERGE=1 \
  MTG_MERGE_INPUTS="decks/<name>.keepmodel.exhaustive.raw.json,/path/to/secondary.raw.json" \
  MTG_KEEP_BOTTOMING=<0|1> \        # bake bottoming_enabled based on the POOLED R + attribution
  ./build/Release/mtg-analyze decks/<name>.txt --cards-json src/cards/data/cards.json
```

The merge rebuilds the policy at the pooled R via the same code the in-run path uses (identical output)
and re-emits a merged sidecar listing all `pooled_seed_bases`, so merges chain.

**Work allocation:** the slow box adds effective-R with no coordination (contribution ~ speed × time;
`1/√R` diminishing). It is usually worth more pointed at a **different / harder deck** than piling R on
one already resolved.

## Validating a profile (A/B)

```bash
# KEEP: exhaustive keep vs static; bottoming held to lookahead both sides (isolates the keep decision).
KM_DECK=decks/<name>.txt KM_MODE=keep   bash test/keepmodel_exhaustive_ab.sh
# BOTTOM: exhaustive vs lookahead bottoming; keep held to exhaustive both sides (isolates bottoming).
KM_DECK=decks/<name>.txt KM_MODE=bottom bash test/keepmodel_exhaustive_ab.sh
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
- **Bottoming:** set `bottoming_enabled=true` (regenerate/merge with `MTG_KEEP_BOTTOMING=1`) **only**
  after a high-R run whose A/B + attribution clears it (ties/beats lookahead, or loses only to
  clairvoyance).

## Artifacts

- **Commit** the definitive high-R `.profile.json` (and its `.raw.json` for reproducibility / further
  pooling).
- **Do NOT commit** throwaway low-R profiles/sidecars — regenerate at the target R.
- Profiles live under `decks/` (per repo convention); rollout logs/scratch under `logs/`.
