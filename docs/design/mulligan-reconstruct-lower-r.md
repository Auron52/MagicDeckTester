# Reconstructing cheaper mulligan profiles from ONE full-R raw (no re-rollout)

Status: **shipped** (offline reconstruction in `RunKeepMerge`; in-game A/B in `test/keep_reconstruct_ab.sh`).

## The question this answers

For an expensive deck (e.g. Dragonstorm) we want to know, *without paying the generation cost again*:

1. **How much R does this deck actually need?** (Would R=10 or R=20 have been as good as R=40 in play?)
2. **Is adaptive bottoming sufficient, or do we need full bottoming?** (The adaptive-bottom generation
   samples the bottoming sub-tables only to the floor; full bottoming refines them.)

Re-running generation to answer either is wasteful: the full-R, full-bottoming run does a **strict
superset** of the work of every cheaper variant. We should generate the full profile **once** and
reconstruct the cheaper variants from its raw sidecar.

## Why one raw is enough

The raw sidecar stores, for **every** sub-composition and both pd (play/draw), the triple
`{count, sum, sumsq}` — i.e. the per-cell **mean and variance** of the win-turn estimate, not just the
point value. That is exactly the information needed to model what a *lower-R* estimator would have
decided: the mean of `k` rollouts is distributed `Normal(mean, var/k)`. (`RunKeepMerge` already tracked
`have_sumsq` for precisely this — "Enables synthetic R-sweeps.")

So from the full raw we can **resample** each cell at any target `k ≤ achieved R`, rebuild the policy
through the *same* `BuildPolicyFromTables` the real run uses, and get a genuine, playable profile that
makes the keep/bottom decisions a fresh R=k run would have made — with no rollouts.

## The reconstruction knobs (offline, in the merge path)

Run the normal merge (`MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS=<raw>`) with any of:

| env | effect |
|---|---|
| `MTG_KEEP_SYNTH_R=k`        | resample **all** tables to R=k → a lower-R profile |
| `MTG_KEEP_SYNTH_BOTTOM_R=j` | resample only the bottoming **sub-tables** (hand size < 7) to R=j; leave the size-7 keep decision at full R. `j` = the floor → **emulates adaptive bottoming** (keep full, bottom coarse) |
| `MTG_KEEP_SYNTH_SEED=…`     | RNG seed (default fixed) → reconstruction is **deterministic** and reproducible |

Each resampled V is `V̂ ~ Normal(mean, vs/k)`, where `vs` is the per-rollout variance
(`sumsq/cnt − mean²`) **shrunk** toward the size's pooled per-sample variance with `se_prior=8` (guards a
low-count cell's fake-tight variance — the same shrink the PRUNE-EMIT gate uses), and `V̂` is clamped to
that size/pd's observed cell-mean envelope (a k-sample mean is bounded by it). Resampling touches only the
in-memory `t.V` used to *build* the policy; the re-emitted raw is written from the untouched pooled sums,
so a reconstruction can **never corrupt a poolable sidecar**. With both knobs unset the merge is
byte-identical to before.

### ⚠️ Do NOT compare the reconstructions by their reported `D_opt`

A reconstruction prints a `D_opt` (expected win-turn under its own policy) that **drops** as R falls —
e.g. on the Dragonstorm R≈2.3 perf raw: full 3.876 → SYNTH_R=5 3.601 → SYNTH_R=2 3.262 → bottom-R=1
2.864. That is **winner's-curse optimism bias**, not real improvement: a noisier table lets the
argmin/threshold pick randomly-favorable cells, so the policy's self-reported EV is optimistic *precisely
where it is least trustworthy*. **`D_opt` is not a cross-R quality metric.** Judge quality in play.

## The uniform SYNTH_BOTTOM_R knob does NOT model adaptive bottoming

`MTG_KEEP_SYNTH_BOTTOM_R=j` resamples **every** bottoming sub-cell to the same R=j. That is NOT what
adaptive bottoming does, and it badly over-states the cost. Bottoming is an `argmin` over a hand's many
sub-compositions; downsampling *every* cell to one R corrupts the true-argmin cell with noise, and the
argmin then systematically selects the most-*underestimated* cell — a severe winner's curse that grows
as j shrinks. Measured on slivers (real adaptive cost **+0.0057t** in game): `SYNTH_BOTTOM_R` at
`j=2 → +0.236`, `8 → +0.055`, `16 → +0.032`, `40 → +0.015` — none match, all 2.6×+ too high, because a
uniform level cannot reproduce what adaptive bottoming actually does: **REFINE the decision-relevant
argmin sub-cells toward the cap** (measured mean R 16–28, 60–75 % of cells above the floor) while leaving
clearly-worse cells at the floor. So do **not** use `SYNTH_BOTTOM_R` to estimate adaptive bottoming.
(The `SYNTH_R` keep-only R-sweep above IS trustworthy — a keep decision is one symmetric threshold flip,
no argmin — Dragonstorm knee **~R=30**, R=20 fallback for hard decks.)

## Adaptive bottoming: the offline REGRET SIMULATOR (`MTG_KEEP_SIM_ADAPTIVE_BOTTOM`)

Adaptive bottoming *can* be evaluated from ONE full-bottom raw — not by downsampling, but by **replaying
the gen's variance-driven refinement offline** and scoring the result on the true means. This is the
cheap, trustworthy answer:

```bash
MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS=<full-bottom.raw.json> \
  MTG_KEEP_SIM_ADAPTIVE_BOTTOM=1 MTG_KEEP_SIM_FLOOR=2 MTG_KEEP_SIM_TRIALS=128 \
  MTG_MERGE_OUT_PROFILE=/tmp/ignore.json MTG_MERGE_OUT_RAW=/tmp/ignore.raw.json \
  ./build/Release/mtg-analyze <deck> --cards-json src/cards/data/cards.json
# prints:  regret = +Xt  (win-turns adaptive bottoming costs vs full bottoming, per pd + a 50/50 blend)
```

What it does, per Monte-Carlo floor-noise realization (default 128, `MTG_KEEP_SIM_TRIALS`):

1. **Floor the sub-tables** to R=`MTG_KEEP_SIM_FLOOR` (default 2 = the project's adaptive_bottom floor):
   each sub-cell's estimate becomes `μ + Normal(0, vs/floor)` (`vs` = the stored per-rollout variance,
   shrunk toward the table's pooled variance with `se_prior=8` — the same shrink the gen's stop gate
   uses). Size-7 (the keep table) stays at its true mean — both adaptive and full bottoming share it, so
   it cancels.
2. **Run the gen's exact refine waves** (`MTG_KEEP_SIM_FLIP_EPS`, default 0.02): mark each hand's current
   `argmin` sub-cell and drive it to the cap (its true mean) unless the hand is a confident mulligan —
   with Gaussian draws standing in for rollouts. Cells that are never any hand's argmin stay at the floor.
3. **Build the converged policy** exactly as the gen does — keep flag from the est `keep_val` vs the est
   `D_opt`, bottom target from the **refined-only** `argmin` (the `bottom_floor` filter that excludes
   never-refined floor cells).
4. **Score it on the TRUE means**: `regret = D(adaptive policy) − D(full policy) ≥ 0`.

The structural cost this captures: a genuinely-best sub-cell whose *unlucky-high* floor draw keeps it
from ever being the current argmin is never refined, so the `bottom_floor` filter excludes it and a
slightly-worse refined cell is kept — a winner's-curse-of-*omission* that only the floor-noise
realization drives (hence the trials). Refined cells are credited their true mean (the full raw's mean IS
the best estimate of a refined cell's value), which isolates this structural regret from the second-order
R-cap resampling noise the two independent gens would also differ by.

**Validation.** On the slivers full-bottom raw, `FLOOR=2` reproduces the in-game A/B almost exactly:
regret **+0.0058 ± 0.0001t** (draw +0.0058, play +0.0057) vs the measured **+0.0057t**. Floor
monotonicity is physical (finer floor → more noise → more regret): slivers `FLOOR 1/2/4 → +0.009/+0.006/
+0.003`. **Dragonstorm** (188 k size-7 hands, ~30 s/64-trials): `FLOOR 1/2/3/4 → +0.022/+0.014/+0.010/
+0.008t`; at the standard floor=2, adaptive bottoming costs **+0.0138t** — small (≈ the keep-side R=40→30
cost), but ~2.4× slivers.

**Lower the whole gen's R too (`MTG_KEEP_SIM_CAP=k`).** For a hard deck you cheapen *both* levers: a lower
cap R *and* adaptive bottoming. `SIM_CAP=k` models the sub-tables (full baseline **and** adaptive refined
cells, sharing draws so the cap-R noise cancels) reaching only R=k, so you read the adaptive-bottom regret
**at** R=20/30. It *falls* as R drops — full bottoming's own argmin gets noisier at low R, shrinking the
gap adaptive has to beat:

| adaptive-bottom regret (blend) | R=40 | R=30 | R=20 |
|---|---|---|---|
| slivers | +0.0057 | +0.0037 | +0.0031 |
| Dragonstorm | +0.0138 | +0.0090 | +0.0073 |

(The keep-table R cost is *separate* and grows the other way — SYNTH_R sweep, Dragonstorm R30 ≈ +0.01t,
R20 ≈ +0.03t. A full cheap-gen cost = keep-R cost + this bottoming regret.)

**Performance impact.** The sim also prints the gen-rollout saving as a bracket — refined cells driven all
the way to the cap (conservative, least saving) vs stopped at their modelled flip_eps-confident R
(optimistic, most saving); reality lands mid-bracket (real slivers **~52 % of sub-table rollouts / ~33 %
of total gen**, in-bracket). On **Dragonstorm** the sub-tables are **~81 % of the gen** (its keep table is
already adaptive-cheap at mean R≈4.2), so adaptive bottoming saves **~27–49 % off the total gen** at R=40
(a ~6–10 h cut on the 21 h run) for the +0.0138t cost. Use this per deck to decide adaptive vs full
bottoming from ONE generation; a shipped profile still defaults to full bottoming (gated by the confounded
A/B) — this quantifies what that default buys and what dropping it (plus lowering R) would cost.

## The compare step (in-game — the ground truth)

`test/keep_reconstruct_ab.sh` plays two exhaustive profiles identically and reports the per-depth
win-turn delta. It attaches each profile via `MTG_EXHAUSTIVE_PROFILE=<path>` (no `decks/` or GT churn)
and forces blind exhaustive bottoming on both arms so the stored bottom targets are exercised.

```bash
A_PROF=<full.profile.json> A_TAG=full \
B_PROF=<reconstructed.profile.json> B_TAG=r10 \
  bash test/keep_reconstruct_ab.sh
# negative overall delta  => candidate wins earlier (cheaper AND >= as good)
# ~0 within noise          => the cheaper reconstruction is sufficient for this deck
```

Seeds default to a held-out band (4004..19019) disjoint from the regression train/overnight seeds.

## The locked recipe

1. **Generate the full profile once**, on a frozen commit, at the definitive R (see the
   `mulligan-profile` skill / `dragonstorm-mulligan-tractability.md`). Keep its `.raw.json`.
2. **Ship** the full profile: a plain merge of the raw (`MTG_KEEP_MERGE`, no synth) writes it.
3. **Reconstruct** the variants you want to test from that raw, to distinct files:
   ```bash
   for k in 10 20; do
     MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS=<full.raw.json> MTG_KEEP_SYNTH_R=$k \
       MTG_MERGE_OUT_PROFILE=<deck>.synthR$k.profile.json MTG_MERGE_OUT_RAW=/tmp/ignore.raw.json \
       ./build/Release/mtg-analyze <deck> --cards-json src/cards/data/cards.json
   done
   # adaptive bottoming (keep full, bottom sub-tables at the generation floor, e.g. 1):
   MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS=<full.raw.json> MTG_KEEP_SYNTH_BOTTOM_R=1 \
     MTG_MERGE_OUT_PROFILE=<deck>.adaptivebottom.profile.json MTG_MERGE_OUT_RAW=/tmp/ignore.raw.json \
     ./build/Release/mtg-analyze <deck> --cards-json src/cards/data/cards.json
   ```
4. **A/B each reconstruction vs the full profile** with `test/keep_reconstruct_ab.sh`.
5. **Pick the knee**: the smallest R whose in-game delta is within the noise floor; decide adaptive vs
   full bottoming from its delta. This becomes the per-deck / future-deck R and bottoming choice — decided
   from ONE generation.

## Dragonstorm R=40 run (this instance)

- Launch: `logs/Dragonstorm_gen/launch_full_R40_e566eda.cmd` (cap R=40, floor 1, disjoint seed 20000001,
  poolable with the R≈2.3 perf raw `perf_R5_e566eda.raw.json`, 30-min checkpoints, resumable).
- Outputs: `logs/Dragonstorm_gen/full_R40_e566eda.{raw,profile}.json`, log `full_R40_e566eda.log`.
- When done, reconstruct SYNTH_R ∈ {10,20} and SYNTH_BOTTOM_R=1 and A/B them against the plain full
  profile per the recipe above to settle Dragonstorm's required-R and adaptive-vs-full bottoming.
