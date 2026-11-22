# Worldfire mulligan generation setting

Measured 2026-11-22 before the first exhaustive profile. The deck has 21
equivalence buckets: Scalding Tarn and Flooded Strand merge; the other card
names remain distinct. `value_play.expected_buckets` records that discovery
result. With the deck's copy limits, the size-seven table has 562,383 bucket
compositions per play/draw side.

The default 48-hand, R24 setting screen chose depth 1 / 1 ms, but its reference
noise ceiling was 0.9202. All candidates saturated the normalized fidelity
score, so that screen could not distinguish them. I repeated the screen with
96 hands and R64, then measured intermediate budgets against the same play
reference (depth 5 / 20 ms). Cost is search units per rollout; fidelity is
Spearman rank correlation divided by the reference's noise ceiling.

| Seed | Setting | Fidelity | Units | Relative cost |
| --- | --- | ---: | ---: | ---: |
| 20261123 | d5 / 20 ms reference | 1.0000 | 26,772 | 1.000 |
| 20261123 | d2 / 7 ms | 0.9876 | 15,546 | 0.581 |
| 20261123 | **d2 / 8 ms** | **0.9911** | **16,219** | **0.606** |
| 20261123 | d2 / 9 ms | 0.9951 | 17,322 | 0.647 |
| 20261123 | d2 / 10 ms | 0.9988 | 18,191 | 0.679 |
| 20261124 | d5 / 20 ms reference | 1.0000 | 29,678 | 1.000 |
| 20261124 | **d2 / 8 ms** | **1.0000** | **17,824** | **0.601** |

Depth 2 / 8 ms is the cheapest tested setting that met the 0.99 fidelity gate
on both independent samples. Its measured search work is about 40% lower than
play settings. This is a generation label setting; the deck's play settings
remain unchanged.

The score caches and full logs are in the isolated worktree's ignored
`build/scratch/worldfire-mullperf/` and `logs/worldfire-mullperf-*.log`.

The suite PGO training manifest initially read generation settings only from
`.value.json`. Worldfire has no value sidecar, so it trained at the default
depth 5 / 20 ms despite the new contract. `pgo_train_manifest.py` now reads
the profile first, then lets a value sidecar override it. The regenerated
manifest reports `gen_worldfire` at depth 2 / 8 ms.

On the same 1,000 uniformly sampled size-seven rollouts, Release used 497 ms
of thread CPU per rollout and PGO+LTO used 346 ms (1.44x throughput). Both
reported 18,680 search units per rollout and digest `5b57e1a7c0074163`.
The PGO sample's slowest 1% accounted for 16.1% of time; its maximum was
15.4 s. At 32 fully occupied cores, one rollout per side across all 562,383
size-seven compositions alone projects to about 3.4 hours. A full profile
adds the floor's second rollout, refinement, and smaller-hand tables, so this
sample does not estimate its total time.
