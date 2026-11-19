# Maze's End S6 -- reference s6 at the shipped budget (results, 2026-11-19)

Branch `wip/maze-s6` (from `wip/maze-shared` 7626b6105, `HEAD:src` 1baf8fba5c9c). No engine or provider change was
needed or made. Scratch: `build/scratch/maze/s6/`. Binary: `mtg.base` = the maze-shared worktree's Release build of
7626b6105 (clean src).

## Verdict: s6 already plays T6 at d5 b20 on the integration tip

R1's T7 was measured on `mtg.e682` (e682d7942). On wip/maze-shared 7626b6105 the shipped search wins s6 on **T6**:

| s6 from T1, d5 | b12 | b14 | b16 | b18 | b19 | b20 | b21 | b22 | b24 | b26 | b30 | b40 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| e682 (R1) | 7 | 7 | 7 | 7 | 7 | **7** | 7 | 6 | 6 | 6 | 6 | 6 |
| 7626b6105 (tip) | 6 | 6 | 6 | 6 | 6 | **6** | 6 | 6 | 6 | 6 | 6 | 6 |

The T1 land no longer matters at the tip: forced T1 Sea Gate / Gond Gate / Maze's End, then b20 search -> T6 / T6
(Gond) / **T6** (Maze's End; e682: T7). Only "no land" loses (T7). So the tip fixed the LATER turns that turned
R1's T1 Maze's End into a T7, not the T1 ranking.

Bench (stateless T1 hand-off at shipped settings, `build/scratch/maze/r1/handoff2.py`, which R1 showed equals the
pooled `ref_bench.py` rows), all 11 references, tip binary: **11/11 equal to the USER** -- s12345681 5, s1 unwon,
s2 5, s3 6, s4 5, s5 5, s6 **6**, s7 5, s8 6, s9 6, s10 6. The pooled `ref_bench.py` run was not made (heavy lock
queue ~15 deep); it is the cheap confirmation still owed.

Attribution is not single-commit: s6 is a knife-edge game. Branch binaries off the same era flip T6/T7 (p6, cleanup
T7; p5c, p5d, p7, f7, tie, fix T6), and at the tip `MTG_MAZE_FIND_NOTHING=0` alone flips it back to T7. Levers that
do NOT move it at the tip: FETCH_EVAL_V5, BP_DEV_SEQ, OPP_LANDS_FRONT, PLAZA_PAY, HEAP_TREASURE, LAND_TARGETS,
LAND/CAST_DOMINANCE (each =0). The candidates between e682 and the tip are P5c (defer deletion), P5c2 (whose commit
message names exactly this mechanism: "a T1 Maze's End vs a Gate ... the b20 tails stop reaching the
play-the-Gate-first lines"), P5c3 and P7.

## Where b20 goes at T1 (tip; `MTG_P16_TRACE`, `MTG_TURN_CENSUS`, `MTG_FS_ROOT_DUMP=1 MTG_FS_ROOT_DUMP_SIM=1`)

- The T1 decision commits **pass 1 only (depth 1)**: 3,011 of 18,000 units, **3,008 of them horizon rollouts** (the
  tree itself is 3 units: 3 root land plans). Pass 2 costs 16,085 (b25+ runs it) but the start gate refuses it at
  b20: estimate 3,011 x kDefaultGrowth 6.0 = 18,066 > 1.1 x 14,989. Pass 1 would have to cost <= 2,789 (-7.4%) for
  b20 to run pass 2 (which then completes: overrun ceiling is 2 x limit). T2 (9,014) and T3 (3,619) are depth 1 too.
- 208 rollouts, only **95 distinct boards** (sorted battlefield + hand). 42 are order-only repeats inside one root
  (e.g. T2 "Gond, Exploration, Exploration, Maze's End" vs "Maze's End, Exploration, Gond, Exploration" -- same tapped
  board, same library), the rest repeats across roots (Sea-then-Gond == Gond-then-Sea). The engine keeps battlefield
  order in rollout keys on purpose (`CanonSimKeyOn`'s order signature: greedy rollouts read vector order, so a
  permuted sibling can return a different win turn), so folding them is an ENGINE question with a play change, not a
  provider prune. P5b's `MTG_FOLD_LAND_ORDER` (wip/maze-p5b, not on maze-shared) is the related land-first fold,
  shipped DEFAULT OFF after a net +3 A/B.
- The root tails "Sea 7 / Gond 9 / Maze 9" are not evaluations: a 9 is the branch-and-bound no-win marker. Sea Gate
  and Gond Gate roots reach the SAME T3 board and play the same greedy line; the Gond rollout ran under cutoff 6 (it
  had to beat Sea's 7) and stopped at T6. The T1 choice among the Gates is a tie the root order breaks; R1's "Sea 8,
  Gond 9, Maze 7" at e682 read the same way.
- The beyond-horizon greedy from the USER's own T3 board wins T7, not T6; no depth-1 T1 pass can see the T6 line.

## Not done (no need)

No prune was built: nothing is left to recover on s6 or on any reference at b20. The A/B and d8 b0 steps of the brief
had nothing to measure. If the margin matters (FIND_NOTHING=0 flips it), the measured lever is the 7.4% of pass-1
rollout cost at T1 that would let b20 run pass 2; the order-only repeats (20% of T1's rollouts) are where it is.
