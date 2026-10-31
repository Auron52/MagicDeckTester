> Historical V2 protocol and evidence. These dated commands are not the next launch.
> Use [V4-PROTOCOL.md](V4-PROTOCOL.md) for the authorized 24-worker full refresh;
> normal12 is now a separate both-reference-set process. Preserve V2/V3 outputs.

# Frozen-source v2 rerun readiness

Use these authoritative helpers in this directory, not older scratch copies.
The v1 engine outcomes do not satisfy the rerun after the FD correction. Keep
v1 measurements and their provenance intact; every measured v2 control is run
again on the new frozen source and fresh PGO binary.

The original QC1 setup-only sample can be reused: all four recorded hashes
(GameSetup.h, Library.h, cards.json, deck) still match. Read-only revalidation
found exactly 600 original-seven traces; 97 qualify for the historical no-cheap
cohort, producing 194 measured policy games. This reuses setup hands, not v1
autoplay outcomes. The launcher rechecks those hashes; changed setup requires
new sampling and a justified cohort, not new provenance pasted onto old traces.

The primary pool remains 13,142 games. PB adoption uses **PBCYC versus current
CYCLER**, separately at d3/d5, with net <= 0 at both depths and a better aggregate
required; historical PB/base comparisons do not replace this control. All six
cast arms, including DRAW, are in both raw/blind pools (1,800 games each). The
reporter compares DRAW to base and CYC1. Myriad-only contrasts remain available.

Memory jobs have no per-job proposal pins: they exercise live compiled defaults.
The explicit exact-key, derivation-isolation and hit-charge values are all ON,
matching their current source defaults. Shared RAM budgets remain shared; tiny
sets only the shared plan pool to 20,480KB, and the so-called unbounded variant
sets only the plan-pool limit to zero. It does not remove the FSL pool limit.

Standalone memory plans now enable **MTG_FD_ORACLE=1 and MTG_FLAG_NONCONV=1**.
Both interim preflight and final memory reporting require those recorded flags
and clean fidelity inventories. Any standalone FD/nonconv record blocks signoff,
including leaf-estimate warnings, even if wins, digests and units match. Mixed
primary diagnostics retain family attribution: memory-attributed/unattributed
records block memory proof; other-family blind forecasts and leaf estimates
remain separately classified in the quality report. FSL pressure is still a
separate limitation and interim review gate.

## Generate and seal after the new freeze and fresh PGO

These commands prepare files and print plans; they launch no engine. Run from
the primary checkout. The example new v2 directory must not already exist.

```bash
set -eu
WORKTREE=/workspaces/MagicDeckTester/.claude/worktrees/cyclone-finish
HELPERS="$WORKTREE/logs/cyclone-completion/prepared"
V2=/workspaces/MagicDeckTester/build/scratch/cyclone-finish/v2
INPUTS="$V2/final-pgo-inputs"
PRUNES="$V2/final-prunes"
test ! -e "$V2"
python3 -B "$HELPERS/reconstruct_prunes.py" \
  --worktree "$WORKTREE" --output "$PRUNES" --threads 12 phasec
python3 -B "$HELPERS/reconstruct_prunes.py" \
  --worktree "$WORKTREE" --output "$PRUNES" --threads 12 iso92
python3 -B "$HELPERS/make_validation_manifests.py" \
  --worktree "$WORKTREE" --bin "$WORKTREE/build/PGO/mtg" --build-mode pgo \
  --threads 12 --out "$INPUTS" \
  --experiment cost --experiment memory --experiment qc1 --experiment qc3 \
  --experiment nested --experiment cast --experiment payment --experiment verify \
  --qc1-cohort no_cheap \
  --qc1-traces /workspaces/MagicDeckTester/build/scratch/cyclone-finish/qc1-sampling/traces
python3 -B "$HELPERS/run_prepared_validation.py" primary \
  --worktree "$WORKTREE" --inputs "$INPUTS" \
  --prunes "$PRUNES/phasec/manifest.json" --bin "$WORKTREE/build/PGO/mtg" \
  --dry-run
python3 -B "$HELPERS/run_prepared_validation.py" memory1 \
  --worktree "$WORKTREE" --inputs "$INPUTS" \
  --prunes "$PRUNES/phasec/manifest.json" --bin "$WORKTREE/build/PGO/mtg" \
  --dry-run
```

The launcher composes and seals all inputs, requires clean source, fresh matching
PGO SRC_TREE, current pruning content/profile hashes, and unchanged QC1 setup
hashes. Confirm primary count 13,142, no generation warnings, and both diagnostic
flags in the memory plan. Avoid `--experiment all`: old standalone PB/KEEP pools
are redundant; composition already imports the full twelve-arm pruning pool.

The environment-cleanup gap is resolved. Generated cleanup lists clear inherited
MTG_FSL_POOL, MTG_FSL_CAP, MTG_MEM_BUDGET_MB, MTG_PLAN_CACHE_EXACT_KEY,
MTG_BP_DERIVE_ISOLATE and MTG_CACHE_HIT_CHARGE, alongside the existing
plan-pool/quota controls. The launcher clears them before applying each plan's
intentional settings, preserving the explicit memory pins and tiny/unbounded
plan-pool overrides. Root's inspected MTG environment was already empty; this
also makes the same commands reproducible in a previously configured shell.

## Reports after the applicable v2 runs complete

Normal memory1 and combined memory6 run before primary. Inspect their preflight
first; no incomplete or fidelity-failing preflight is a launch signoff. The
reporter's unspecified output/result paths now derive from `--inputs`; explicit
paths below make the intended generation evident.

```bash
python3 -B "$HELPERS/report_final_results.py" memory-preflight \
  --inputs "$INPUTS" --output "$INPUTS/reports"
python3 -B "$HELPERS/report_final_results.py" quality \
  --inputs "$INPUTS" --wins "$INPUTS/runs/primary-quality/wins" \
  --traces "$INPUTS/runs/primary-quality/traces" \
  --stderr "$INPUTS/runs/primary-quality/batch.err" --output "$INPUTS/reports"
python3 -B "$HELPERS/reconstruct_prunes.py" \
  --worktree "$WORKTREE" --output "$PRUNES" report phasec
python3 -B "$HELPERS/report_final_results.py" memory \
  --inputs "$INPUTS" --primary-wins "$INPUTS/runs/primary-quality/wins" \
  --primary-stderr "$INPUTS/runs/primary-quality/batch.err" --output "$INPUTS/reports"
```

The primary launcher creates pruning output aliases inside the new v2 tree;
never redirect old v1 aliases onto v2. Conditional blind/recovery manifests must
be generated from completed, reviewed v2 data. Iso92 still requires two separate
process modes using the same freshly stamped manifest; commands are printed by
its generator and must use separate v2 normal/reversed destinations.

Validation: 23 synthetic helper tests pass, covering six cast arms, selected-v2
path wiring/explicit overrides, outcome/units completeness, cache pressure,
ordinary FD/nonconv gates, missing diagnostic flags and mixed-primary blind
classification and hostile inherited cache settings versus intentional pins.
No build or simulation was run during this review.
