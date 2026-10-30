# Cyclone final validation reproducers

These helpers prepare, run and report the outstanding cost, memory, QC1,
QC3, nested-exhaustion, cast and payment experiments. `reconstruct_prunes.py`
and `prunes_proofs.py` beside them own the pruning work; see
`prunes-PROOF-REPORTS.md`. Nothing was simulated while this copy was prepared.
Engine source is unchanged by these helpers.

Run commands from the primary checkout. Helpers may remain here; runtime
manifests, candidate profiles, traces and logs belong under `build/scratch`,
not beside decks or in this persistent directory. Launcher and reporter
resolve that primary checkout even when invoked from this linked worktree.

## Prepare after a source freeze

The preparation used clean source tree
`ce3431d90d7f7ee88b47a791ce61595740f907f0`. Preparation is not a PGO freshness
claim: final launch requires a freshly trained PGO binary whose `SRC_TREE`
stamp matches the current source. Refresh source/data provenance before the
first launch, never after different code has already produced observations.
Read-only Git calls disable optional locks.

```bash
HELPERS=.claude/worktrees/cyclone-finish/logs/cyclone-completion/prepared
WORKTREE=.claude/worktrees/cyclone-finish
INPUTS=build/scratch/cyclone-finish/final-pgo-inputs
PRUNES=build/scratch/cyclone-finish/final-prunes
python3 "$HELPERS/make_validation_manifests.py" \
  --worktree "$WORKTREE" --bin "$WORKTREE/build/PGO/mtg" --build-mode pgo \
  --threads 12 --out "$INPUTS" \
  --experiment cost --experiment memory --experiment qc1 --experiment qc3 \
  --experiment nested --experiment cast --experiment payment --experiment verify \
  --qc1-traces build/scratch/cyclone-finish/qc1-sampling/traces
python3 "$HELPERS/compose_final_quality.py" \
  --inputs "$INPUTS" --prunes "$PRUNES/phasec/manifest.json" \
  --worktree "$WORKTREE" --bin "$WORKTREE/build/PGO/mtg" --threads 12
```

Do not regenerate over measurement inputs in use. On complete runtime-volume
loss, first reconstruct pruning Phase C using its helper, then reconstruct
the QC1 sample below, then run the commands above. The default twelve-arm
Phase C contains shared controls, Ponder and PBCYC+Ponder; do not add the old
separate PB/KEEP pools again. The composed primary has 12,542 games with the
original 97-hand QC1 cohort. A changed selected cohort must be explained,
not forced to have the old count.

## Preserve QC1 sample identity

The original 600 forced-seven sampling traces are under
`build/scratch/cyclone-finish/qc1-sampling/traces`, with adjacent `meta.json`.
The exact original source/deck input hashes are retained here as
`qc1-original-sampling-provenance.json`. Sampling uses d0, max_turns1,
`force_mulligan:"0:"`, seeds109000/110000, 300 hands each. Its autoplay
outcomes are not measurements. All policy comparisons remove force_mulligan
and replay the original seed and global game index under normal policies.

If those runtime traces are lost, generate only `--experiment qc1` without
`--qc1-traces` into a fresh runtime input directory first. Its `run-plan.json`
contains one `qc1_sample` plan with exact argv/cwd/env and 10-game chunks.
The following explicitly launches that sample; use it only after current PGO
is ready and a simulation launch is intended. It refuses changed original
shuffle inputs or pre-existing logs. Replace SAMPLE_INPUTS below with that
fresh directory. It writes new actual-binary provenance before launching.

```bash
python3 - "$HELPERS" SAMPLE_INPUTS <<'PY_SAMPLE'
import hashlib,json,os,subprocess,sys
from pathlib import Path
helpers,inputs=map(Path,sys.argv[1:])
record=json.loads((helpers/'qc1-original-sampling-provenance.json').read_text())
plan=json.loads((inputs/'run-plan.json').read_text())
p,= [p for p in plan['plans'] if p['name']=='qc1_sample']
root=Path(p['cwd']); binary=Path(p['argv'][0])
for name,expected in record['inputs'].items():
    assert hashlib.sha256((root/name).read_bytes()).hexdigest()==expected,name
stamp=binary.parent/'SRC_TREE'
assert stamp.read_text().strip()==plan['src_tree'],'stale PGO stamp'
run=Path(p['stdout']).parent; run.mkdir(parents=True,exist_ok=True)
for key in ('--game-log-dir','--game-trace-dir'):
    Path(p['argv'][p['argv'].index(key)+1]).mkdir(parents=True,exist_ok=True)
record['binary_sha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
with (run/'meta.json').open('x') as f: json.dump(record,f,indent=2)
env=dict(os.environ)
for key in plan['unset_before_run']: env.pop(key,None)
env.update(p['env']); env['GIT_OPTIONAL_LOCKS']='0'
with Path(p['stdout']).open('x') as out,Path(p['stderr']).open('x') as err:
    subprocess.run(p['argv'],cwd=root,env=env,stdout=out,stderr=err,check=True)
PY_SAMPLE
```

Pass the resulting `runs/qc1_sample/traces` path to final generation. Selection
requires all600 exact identities and forced-seven traces. The default cohort
fails TwoDrop through no cheap nonland while passing the other static filters;
one-land-only failures are counted separately. Measured reports verify both
initial physical hands and the first keep decision (shipped rejects; curve
none keeps), then report zero versus at-least-one creature-cycler strata.
Never copy provenance onto older traces whose origin is unknown.

## Launch and report

After fresh PGO, the single primary launch command is:

```bash
python3 "$HELPERS/run_prepared_validation.py" primary
```

It seals source, profile, manifest and binary provenance, validates the original
QC1 sampler inputs, links pruning result paths to the shared pool, refuses
existing results and records PID/status/DONE or STOPPED. It never uses timeout,
restarts a run, or treats an engine exit as a quality verdict. The caller owns
progress monitoring. `--dry-run` still seals/checks; it cannot bypass stale PGO.

All stages use this same launcher with stage names `memory1`, `memory6`,
`memory-tiny12`, `memory-unbounded6`, `cost`, `verify`. Primary uses12 workers.
Normal12 memory references share primary; other process-static memory
configurations run separately. No per-thread cache quota is used. Tiny uses a
shared20MiB plan pool; unbounded uses the existing shared-pool control.

Run the separate535-game current Cyclone+Hinata cost batch on a quiet host
after other pools have finished. Hinata bottoming remains ON through the
normal depth-based behavior. It omits full traces. Do not interpret timing
ratios from mixed schedules. The separate24-game VERIFY batch combines junk
memo, cantrip rank-cache and kill-only checks; automatic checking requires
actual positive hits/exercise and rejects mismatches.

```bash
python3 "$HELPERS/report_final_results.py" quality
python3 "$HELPERS/report_final_results.py" memory
python3 "$HELPERS/reconstruct_prunes.py" --worktree "$WORKTREE" \
  --output "$PRUNES" report phasec
```

Quality aggregates the chunk map into logical cells and exact seed/globalGI
pairs, scoring unwon as9. It reports paired deltas/SE/faster/slower and units,
QC1 keep handshake/strata, QC3, payment and raw/blind cast arms. It inventories
FD/nonconv by family and preserves `leaf_est`. Blind-unknown warnings require
ordinary-world controls before a rollout/executor conclusion; ordinary raw
Cyclone true simulated FD divergence remains blocking. Hinata/Kitty leaf
estimate warnings are classified separately. Missing/incomplete data exits2;
unresolved fidelity review exits1. `--allow-partial` labels intersections but
still fails missing data. It does not duplicate pruning reports.

Memory compares exact per-game wins, digests and units from primary chunks
against t1/t6/tiny/unbounded runs, reporting every difference. Every run must
have its process-exit plan-pool summary. The tiny12 proof additionally requires
budget20480KB and positive refused stores; equality without actual pressure
is inconclusive and exits1. Missing files or exit summaries exit2; drift
exits1; complete exact equality with proven tiny12 pressure exits0. Per-run
FSL refused-store counts and warning lines are retained separately: binding
FSL still has the existing hit-discount/interleaving limitation, even if this
measured comparison happens to agree. Use `--primary-stderr FILE` and repeat
`--stderr PLAN_NAME=FILE` when overriding log locations. Synthetic-only
regressions are reproducible with `python3 "$HELPERS/test_report_final_results.py"`. Repro manifests emitted
by reporting (ordinary controls, d8/b0 and expanded budgets) are inputs only;
no reporter launches those jobs or supplies invented results. See pruning
proof documentation for separate iso92 and data-dependent blind workloads.
