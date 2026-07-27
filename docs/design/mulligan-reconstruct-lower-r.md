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
