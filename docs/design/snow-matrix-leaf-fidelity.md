# Snow's heavy matrix games are the beyond-horizon LEAF, not the search — measured 2026-10-13

Self-contained. Written while the Snow value-leaf run (queue `logs/vlq_snow`, frozen at `1eb4ad7f`)
was STOPPED in phase C at the user's direction:

> *"If we are skipping that many games I think we may need to stop and optimize."*
> *"The cap is not intended to get a deck this slow through the value-leaf. It is intended to take out
> a few outliers."*

At the stop, 110 of ~600 game indices (18%) were on the skip list (abandoned at the 40M-unit
ceiling), projecting to ~290 of 1,600 — about twice the 10% skip cap. Almost all abandoned in the
**H4/H5** cells (43 at H4+H5 only, 21 at H5 only, 19 at H3–H5; one game, s11011 gi66, in every H cell
and V4–V8).

## 1. Where the units go

An H cell is the HEURISTIC reference: `value_model` off, budget 0, the ladder's warm-up passes on the
value leaf, and the committed pass scored by the rollout leaf. That leaf is `SimulateToEnd` at
`EffectiveFdLeafDepth()` — by default **1**, a 1-ply lookahead on EVERY simulated turn of EVERY leaf
(`TurnSolver.cpp`, `s_fd_leaf_depth`). Each of those plies enumerates the turn's candidates and, on
Snow, opens the tap-draw breakpoints (site 8) inside it; every candidate then rolls out greedily.

Same binary, same games, one pooled batch, per-game units (`MTG_DUMP_UNITS=1`), H-cell fields copied
from the run's own manifest. `leaf0` = the deck profile with `search_leaf_depth: 0` (a greedy
playout beyond the horizon; Melira Pod ships this), siblings symlinked so the keep table and sidecar
are the shipped ones.

| game (cell seed, gi) | cell | 1-ply leaf (units) | greedy leaf (units) | ratio | win turn |
|---|---|---|---|---|---|
| 8008, 107 | H3 | 6,190,266 | 13,421 | 461x | 6 / 6 |
| 8008, 32 | H3 | 15,122,689 | 229,040 | 66x | 6 / 6 |
| 10010, 4 | H3 | 8,262,732 | 87,870 | 94x | 6 / 6 |
| 11011, 13 | H3 | 2,833,833 | 61,384 | 46x | 7 / 7 |
| 9009, 41 | H3 | 35,239,416 | 699,028 | 50x | 7 / 7 |

Every one of these abandoned at 40M in H4 and H5 under the 1-ply leaf. Under the greedy leaf, H4/H5
on the same games (s8008 gi107/gi32, s9009 gi41/gi5, s10010 gi4/gi37, s11011 gi13/gi43):
**0.11M–14.3M units, all inside the ceiling** (worst: s10010 gi37 H5 14.28M, s9009 gi41 H5 12.99M).

Ordinary games, paired, H3 (4 seeds x 25, 10M ceiling): **83 paired, 0 better / 0 worse**, units
133.2M -> 3.77M (**0.028x**).

V cells do not move: V4, 25 games, byte-identical digests AND identical units — the value leaf
replaces the rollout, so the rollout's fidelity is never read there.

## 2. Why it is NOT a profile change

The same `search_leaf_depth: 0` in SHIPPED (budgeted) play is worse, on held-out seeds 830000+1000·i:

| cell | paired | Δ turns/game | better / worse | units ratio |
|---|---|---|---|---|
| d3 / b10 | 480 | +0.0208 | 7 / 17 | 0.38 |
| d5 / b20 | 240 | +0.0208 | 2 / 7 | 0.47 |

A budgeted search finishes its configured depth early on the cheap leaf and leaves budget unspent,
while each leaf is a weaker estimate. `search_leaf_first_turn_depth: 1` (one searched ply at the
horizon, greedy after) was worse still (+0.042 / +0.033) AND 1.3x the cost, so it is dead.

## 2b. But NOT at shallow depth -- the reason the override carries a threshold

Paired ordinary games, greedy leaf vs the 1-ply one, same pooled batch:

| cell | paired | better / worse | Δ turns/game | units ratio |
|---|---|---|---|---|
| H1 | 50 | 0 / 11 | +0.22 | 0.052 |
| H2 | 25 | 0 / 2 | +0.08 | 0.034 |
| H3 | 83 | 0 / 0 | 0 | 0.028 |

A shallow search leans on its leaf; a deep one barely reads it (at d>=3 from turn 3 onward the window
already spans most of an 8-turn goldfish, and the lazy leaf proves many decisions leafless). So a
blanket matrix override would WEAKEN the H1/H2 reference, and a weaker H makes the value leaf look
better than it is -- the bias runs toward adopting the leaf, the same direction as the lazy-leaf H5
finding. H1/H2 are cheap at the 1-ply leaf anyway (~0.1M / ~0.4M units per ordinary game), so there
is nothing to buy there.

## 3. The matrix-only override, and the route bug it exposed

`scripts/attic/valueleaf_depth_matrix.py` already had the right knob — `MTG_MATRIX_FD_LEAF_DEPTH`,
a per-RUN override scoped to the matrix (phase A's label games and phase E never see it), used by
EDF on 2026-09-09. **It was a no-op on the pooled route**: only `run_batch` (the retired per-cell
path) translated it. Rebuilt on branch `snow-opt` (`4a60ec66`) as a PER-JOB override with a depth
threshold: `MTG_MATRIX_FD_LEAF_DEPTH=0 MTG_MATRIX_FD_LEAF_FROM=3` makes H cells of depth >= 3 read a
variant profile carrying `search_leaf_depth: 0` (siblings symlinked, valueleaf.sh's
`make_variant_deck` pattern) -- one pool cannot vary an env var per cell. H1/H2, V cells, phase A and
phase E are untouched; an exported `MTG_FD_LEAF_DEPTH` is refused (it would outrank every cell's
profile). The setting is stamped beside the state (`<out>.fdleaf`) and a resume across a change
REFUSES, because neither the src nor the smoke-digest play fingerprint can see it. Unset, the
manifest is byte-identical to the previous driver's (checked with a stub engine).

## 4. What is the user's decision

The H cells are the reference every V cell is judged against, and H5 is the escalation cap. Changing
their leaf is therefore a methodology choice, not an optimization to slip in:

* **Evidence it is harmless to the reference:** H3 0/0 over 83 paired ordinary games, every heavy game
  the same win turn, V cells untouched. (H1/H2 in §2b, H4/H5 in §5.)
* **Evidence it is not free elsewhere:** shipped budgeted play loses 0.02 t/game, so it must stay a
  MATRIX setting, never a profile one.
* **Cost of using it:** the banked phase-C chunks (~5,900 games at the 1-ply leaf) cannot be mixed in;
  phase C restarts fresh. Phases A (rows) and B (model) are unaffected and are kept.

## 5. The deep cells, paired on ordinary games (seeds 601000-604000, 631000-633000; 10M ceiling)

| cell | paired | better / worse | digests moved | units ratio |
|---|---|---|---|---|
| H3 | 83 | 0 / 0 | 6 | 0.028 |
| H5 | 41 | 0 / 0 | **0** | 0.070 |
| H4 | 62 | 0 / 0 | 3 | 0.049 |

So at depth >= 3 the greedy leaf gives the SAME win turn on every paired game measured (186 of 186),
and at H5 the same game on every one. The one game that still abandons under it is s11011 gi66 (H4
and H5 both at 40M) -- the kind of outlier the ceiling exists for.

## 6. The 1-ply leaf cannot be made cheap without paying the same quality (branch `snow-rollout`)

A separate investigation (docs/design/snow-leaf-lookahead-cost.md on branch `snow-rollout`) found
each simulated turn's 1-ply lookahead scores ~116 candidates on Snow (~45 base plans + ~70 tap-draw
breakpoint variants), each with a greedy rollout. Two leaf-only, default-OFF levers
(`MTG_LEAF_NO_BP`, `MTG_LEAF_CAND_CAP` with K) cut the heavy games 9-38x, but on 75 paired H2 games
every arm was worse than base (+0.027 .. +0.067 t/game, 0 better), approaching leaf0's own +0.067 as
the cut deepens. No lossless lever was found. That is the evidence for keeping H1/H2 on the 1-ply
leaf and changing only H>=3, where the leaf measurably does not decide anything.

## 7. In-window cuts are percent-sized next to this (branch `snow-window`)

`MTG_SNOW_LOOK_CAP1` (at most one look per card name offered per plan list; a hit still opens the
breakpoint and the re-plan re-offers the untapped copies) is the best of three default-OFF provider
restrictions: 0.876x units over five heavy H3 games (0.21x on gi107, **1.000x on gi41**, the heaviest)
and 70/70 paired H2 games at the same win turn. It completes s10010 gi37 at H3 (36M) where base
abandoned. Real, but it does not move the tail that drove the stop -- the leaf does. Full record:
docs/design/snow-look-cap-and-defer-levers.md on that branch.

## 8. The resume this enables (the USER's call -- not started)

```
cd .claude/worktrees/snow-vl
git cherry-pick 4a60ec66        # scripts only: src unchanged -> check_freeze is a no-op, no rebuild
mkdir -p logs/vlq_snow/phaseC_leaf1_aside
mv logs/vlq_snow/matrix.txt* logs/vlq_snow/wins logs/vlq_snow/matrix.log logs/vlq_snow/heartbeat.txt \
   logs/vlq_snow/slow_games.log logs/vlq_snow/phaseC_leaf1_aside/
MTG_MATRIX_FD_LEAF_DEPTH=0 MTG_MATRIX_FD_LEAF_FROM=3 \
  setsid nohup bash scripts/valueleaf.sh run decks/Snow > logs/snow-vl-run2.out 2>&1 &
```

Phase A (14,347 rows) and B (RMSE 0.4802) are kept; phase C starts fresh (the stamp guard refuses to
mix it with the banked 1-ply chunks). From the stopped run's own records the COMPLETED games were
cheap everywhere (mean 7.5 s/game at H4, 6.9 s at H5); the ~390 core-h it still projected was the
skip-listed games burning to 40M in cell after cell. Run it with the box to itself: the matrix's
condemnation rule is wall-clock based.
