# V4 full refresh protocol (prepared, not launched)

This authorizes helper preparation only until the source repair is committed,
`src/` is clean, and fresh PGO is ready. Do not generate, seal or launch V4
inputs during the source repair. Preserve every earlier generation, including
the unmodified A7/V3 helpers and source guards. V3 stopped after ordinary FD
131117 across all six raw cast arms: its partial outcomes are unadoptable and
cannot supply V4 quality data. The repair can change actual play and mulligan
trials, so all measured V4 quality and memory observations must be fresh.

## Counts and process boundaries

| Process | Workers | Games |
|---|---:|---:|
| Primary quality, same 1,490 semantic jobs | 24 | 13,142 |
| Normal core reference | 1 | 168 |
| Normal both reference sets | 6 | 468 |
| Normal both reference sets, independent t12 proof | 12 | 468 |
| Tiny shared-plan pool | 12 | 168 |
| Unbounded shared-plan pool | 6 | 168 |
| Quiet Cyclone/Hinata cost | stored plan | 535 |
| VERIFY | stored plan | 24 |
| iso92 two process modes | explicit plan | 80 |

The five extra memory processes total 1,440 games. Fixed planned work totals
15,221 games before data-dependent blind/recovery work. The additional 468
normal12 games are a genuine process-static proof, not another quality wave.
Normal6 and normal12 use the identical `memory-six-reference.json`: 168 core
plus 300 shapes, both unchunked. Primary normal24 includes those 468 games in
chunks. Reports require complete normal12 matching and explicit shape
comparisons normal6-vs-normal12, normal6-vs-normal24, normal12-vs-normal24.
Do not separately run shape-only plans already covered by those processes.

Normal1/6 interim preflight still compares 168 core games and inventories the
300 additional normal6 shapes; it proves neither t12/t24 nor tiny pressure.
The full memory report additionally requires tiny12 positive refused stores
at 20480KB and clean ordinary fidelity diagnostics for every process. All
memory plans retain FD_ORACLE/NONCONV and actual default environment policy;
no per-job default cache pins, quotas or environment changes are introduced.

QC3's 480 games and the six original raw-cast chunks containing
seed 131117/global GI 117 have highest queue priority. Existing shape seed 107008
remains in the earliest ordinary-priority chunk. Only `weight` changes;
profiles, arms, flags, seeds, global indices, depths, budgets and samples stay
identical. These priorities do not request repeated games or quality waves.

## Future generation commands (do not execute before the new freeze)

Run from the primary checkout. Use a new empty V4 output root and the repaired
Ready checkout. The original 600 QC1 setup traces may be reused only when their
recorded original shuffle/deck/cards hashes still match; they are setup hand
identity evidence, never reused policy outcomes. Generation checks all 600
traces and preserves the 97-hand selected cohort when inputs match. Never
invent or copy sampler provenance onto unknown traces.

```bash
WORKTREE=/workspaces/MagicDeckTester/.claude/worktrees/cyclone-ready
HELPERS="$WORKTREE/logs/cyclone-completion/prepared"
V4=/workspaces/MagicDeckTester/build/scratch/cyclone-finish/v4
INPUTS="$V4/final-pgo-inputs"
PRUNES="$V4/final-prunes"
BINARY="$WORKTREE/build/PGO/mtg"
python3 "$HELPERS/reconstruct_prunes.py" --worktree "$WORKTREE" --bin "$BINARY" \
  --output "$PRUNES" --threads 24 phasec
python3 "$HELPERS/reconstruct_prunes.py" --worktree "$WORKTREE" --bin "$BINARY" \
  --output "$PRUNES" --threads 24 iso92
python3 "$HELPERS/make_validation_manifests.py" \
  --worktree "$WORKTREE" --bin "$BINARY" --build-mode pgo --threads 24 --out "$INPUTS" \
  --experiment cost --experiment memory --experiment qc1 --experiment qc3 \
  --experiment nested --experiment cast --experiment payment --experiment verify \
  --qc1-traces /workspaces/MagicDeckTester/build/scratch/cyclone-finish/qc1-sampling/traces
python3 "$HELPERS/compose_final_quality.py" --inputs "$INPUTS" \
  --prunes "$PRUNES/phasec/manifest.json" --worktree "$WORKTREE" --bin "$BINARY" \
  --threads 24 --seal
```

Audit source/binary/profile/input hashes, both reference manifests, exactly
1,490 jobs/13,142 games, all six raw/blind cast arms including DRAW, live
PBCYC-vs-CYCLER control, unchanged paired seed/globalGI identity, no forced
mulligans or quotas, FD_ORACLE/NONCONV on memory, intended env after cleanup,
and 97 original QC1 physical hands. Confirm only the known scheduling weights
change relative to the previous semantic protocol. Store the newly frozen
source and PGO hashes in V4 metadata; A7 hashes cannot authorize V4.

## Future launch and report commands

Root owns scheduling and all process lifecycle decisions. Every stage must
use the explicit V4 paths below. The launcher resolves the primary count from
`--threads` or stored generation/seal metadata and rejects inconsistency; it
never silently seals a 24-worker input as 12. The thread argument always names
the primary count; independent memory stages use their own stored 1/6/12 count.

```bash
python3 "$HELPERS/run_prepared_validation.py" memory6 --worktree "$WORKTREE" \
  --inputs "$INPUTS" --prunes "$PRUNES/phasec/manifest.json" --bin "$BINARY" --threads 24
python3 "$HELPERS/run_prepared_validation.py" memory1 --worktree "$WORKTREE" \
  --inputs "$INPUTS" --prunes "$PRUNES/phasec/manifest.json" --bin "$BINARY" --threads 24
python3 "$HELPERS/report_final_results.py" memory-preflight --inputs "$INPUTS"
```

After the independent preflight passes, root may launch `primary`, `memory12`,
`memory-tiny12`, `memory-unbounded6`, `cost` and `verify` using the same explicit
launcher arguments. Keep the 535-game cost pool quiet and separate. Do not
rerun memory6/1 merely because primary finishes. Keep process logs/status and
raw diagnostics immutable; ordinary simulated Cyclone FD or nonconvergence
requires review, distinct from limited-information blind/leaf warnings.

```bash
python3 "$HELPERS/report_final_results.py" quality --inputs "$INPUTS"
python3 "$HELPERS/report_final_results.py" memory --inputs "$INPUTS"
python3 "$HELPERS/reconstruct_prunes.py" --worktree "$WORKTREE" --output "$PRUNES" report phasec
python3 "$HELPERS/prunes_proofs.py" --root "$PRUNES" movers \
  --traces "$INPUTS/runs/primary-quality/traces"
python3 "$HELPERS/reconstruct_prunes.py" --worktree "$WORKTREE" --bin "$BINARY" \
  --output "$PRUNES" --threads 24 blind --salts 4
```

The last two commands prepare data-dependent inputs only after every Phase C
chunk completes. Their movers, deduplicated controls and four-salt blind
cohort are conditional; never present them as deckwide measurements. Select
40-salt counterfactual expansions only after observable-state eligibility
review. iso92 requires two independent process modes and same-hand/correct
Emrakul-slot eligibility; follow [prunes-PROOF-REPORTS.md](prunes-PROOF-REPORTS.md).
A7-only scratch follow-up runners must remain guarded and must not launch V4.

Helper-only verification: `python3 -B "$HELPERS/test_report_final_results.py"`.
It launches no engine. Before/after hashes and semantic/diff evidence belong
under `build/scratch/cyclone-finish/v4-protocol-audit`, outside runtime inputs.
