# Hinata d4 vs d5 diagnosis -- handoff (2026-11-21, agent at ~265K context)

Branch `wip/hinata-d4-d5`, worktree `/workspaces/MagicDeckTester/.claude/worktrees/hinata-d4-d5`, based on
origin/phase-1-2-deck-analyzer `906d3c0a4` (src tree `94df5afc7`). No source changes. The only commit is this
handoff file. Binary: `<worktree>/build/Release/mtg` (Release, built 2026-11-21 05:36 UTC). Scratch:
`/workspaces/MagicDeckTester/build/scratch/claude_hinata_d4d5/` (called `$D` below).

## USER ruling (verbatim, 2026-11-21)

"This doesn't make much sense. We shouldn't be getting better results with Depth 4 than Depth 5 unless perhaps this
is just variance. If they are similarily constrained below 4 turns lookahead they should act the same and going
further should not make the results worse. We need to dig into this rather than just wave it off."

Standing rules: Hinata play depth must be >= 5 (do not propose d4 as the answer). Any settings change is the USER's
decision: report it, do not adopt it. Do not modify `decks/Hinata/*` in a way that ships. Subagents do not push the
shared branch.

## Arms

`$D/arms/Hinata.value.{d4E,d4N,d5E,d5N}.json` are copies of the CURRENT `decks/Hinata/Hinata.value.json` (the eval
model, crossover table and leaf table are byte-identical to the old sidecar) with only the `value_play` changed:
target_depth 4 or 5; E = `ladder: "emulated"` + old `emul_r [1096.28, 256.6, 100.58, 105.9]`; N = no ladder (the
hybrid, i.e. what ships now). d5N == shipped. d4E == the 2026-11-11 E.7 block. `$D/arms/old.value.json` = the sidecar
at `da17c0bbf^`. Per-job `value_profile` and an explicit `budget_ms` (source `value_play(depth)+cli(budget)`).
Controls: d5N s1001 b30 reproduces the current GT `hinata_smoke_d5_s1001=5.5000/cf869f7cf1dec8c5` exactly; d4E s1001
gives 5.4300, the old GT average.

Held-out seeds 61001..64004 (not in the suite). Pairing: same seed and game_index. A loss counts as 9. Delta = second
arm minus first; positive = the second arm is slower.

## Results

### Q1 + Q2: b30, n=2400 paired (4 seeds x 600)
| arm | d4E | d4N | d5E | d5N (shipped) |
|---|---|---|---|---|
| avg | 5.4863 | 5.5267 | 5.4996 | 5.5592 |

- d5N - d4E: **+0.0729, se 0.0084, t +8.7, 95% CI [+0.057, +0.089], 207 slower / 70 faster** (the real gap; not variance)
- shape at d5: d5N - d5E +0.0596 (t +7.0, 199/88); shape at d4: d4N - d4E +0.0404 (t +5.1, 126/70)
- depth with emulated ladder: d5E - d4E +0.0133 (t +4.45, only 43 games move: 36 slower / 7 faster)
- depth with no ladder: d5N - d4N +0.0325 (t +3.7, 154/79)
- d5E - d4N -0.027 (t -3.3)
- 2HG b30 n=400 (seed 61001): d5N - d4E +0.055 (t +2.8); d5E - d4E 0.000; d5N - d5E +0.055; d4N - d4E -0.0025.

### Q3: budget
b120, n=800 (2 seeds x 400): d4E 5.4638, d4N 5.4600, d5E 5.4813, d5N 5.4900.
- shape effect gone: d4N-d4E -0.004 (t -0.5); d5N-d5E +0.009 (t +0.65)
- depth effect remains: d5E-d4E +0.0175 (t +2.3, 12/3); d5N-d4N +0.030 (t +3.0, 32/14)

b480, n=199 (seed 61001 gi 0..199 minus gi33; `$D/b480/`): d4E 5.3618, d4N 5.3518, d5E 5.3668, d5N 5.3668.
- every pair is within noise: d5E-d4E +0.005 (t +0.45); d5N-d4N +0.015 (t +1.3); d5N-d5E 0.000.

b0, 34-39 games (seed 61001 gi 0..39; 20M-unit abandon): d5E == d4E on all 34 paired; d5N == d4N net (1/1);
d5N-d4E +0.026 (1 game). Too small to conclude alone. The sidecar's own unbounded matrix
(`value_leaf_table`) has heuristic_lp d4 5.3399 vs d5 5.338 and value_lp d4 5.487 vs d5 5.369: d5 >= d4 at unlimited.

Reading: at b30 the gap is mostly SHAPE (the hybrid vs the emulated ladder), and that part disappears by b120. A
smaller DEPTH residual exists at b30/b120 and fades by b480/b0, i.e. it is a finite-budget spending effect.

### Cost (mean / median units per game, gi33 excluded; `$D/main/wins/*.units`, `$D/b480/wins/*.units`)
| budget | d4E | d4N | d5E | d5N |
|---|---|---|---|---|
| b30 | 63,834 / 40,467 | 96,887 / 74,995 | 130,118 / 74,318 | 78,758 / 55,682 |
| b120 | 184,079 / 118,414 | 236,981 / 159,277 | 359,199 / 233,717 | 178,585 / 124,478 |
| b480 | 401,797 | 471,976 | 871,395 | 336,832 |
d5E is about 1.65x d5N units at b30, and about 2x d4E at every budget. The CPU figures from batch.out (thread cpu per
job) are polluted by gi33 and a contended box. **A clean CPU figure is still owed** (MTG_PGO=0 on both arms, gi33
excluded, uncontended or in the lane at fixed threads).

### Mechanism evidence so far
1. **Hybrid (N) at b30** (`$D/st1/{d4N,d5N}.err`, MTG_HYBRID_STATS, 150 games s61001): the value-leaf probe takes the
   budget, and the heuristic escalation mostly runs from a starved remainder. d5N: 436 decisions, 273 redos,
   escalations taken 256 / discarded 17. Probe-budget leftover at escalation is under 10% of the decision budget in
   **152 of 272** escalations. Escalation achieved heuristic depth h0:11 h1:64 h2:133 h3:40 h4:10 h5:15. d4N: leftover
   <10% in 131 of 297, achieved h1:114 h2:74 h3:38 h4:43. The emulated ladder instead always commits a heuristic-rollout
   line (d4E committed-depth histogram d1 189, d2 185, d3 101, d4 82 of 557).
2. **Depth residual inside the emulated ladder** (`$D/st1/{d4E,d5E}.err`): d5E commits SHALLOWER: d1 290, d2 251,
   d3 61, d4 23, d5 18 of 643. Its discarded ("other") heuristic units are 5.67M vs d4E 0.48M. At the FIRST decision of
   146 of 150 games, the same heuristic pass at the same depth on the same state costs more in d5E (e.g. 61001 T1
   X2 5904 -> 6722 units, 61003 T1 X2 7584 -> 31386, 61011 T4 X1 5230 -> 579017). The start gate then refuses the
   next pass, so d5E commits a shallower line. Cross-decision reuse also drops: game-line-cache nowin_hits d4E 9508 vs
   d5E 3215.
3. **Leading hypothesis for (2), and part of the d5N-d4N residual:** the escalation beam is switched on by search
   depth == 5 exactly (`src/ai/TurnSolver.cpp` ~125071-125110, "DEPTH-ADAPTIVE BEAM"): at depth 5 the value-ranked
   beam W3/ld2 plus `g_probe_val_recording = true`; at depth 4 the beam is OFF; at depth 3 static W20. So d4 vs d5
   differs in more than depth: beam plus value recording. That is lossy for the hybrid's escalation (value-ranked
   pruning) and changes pass cost. UNCONFIRMED. The probe for it is running (below).
4. Example first-divergence traces (b30, smoke seed 1001; `$D/tr1/`, `tools/tdiff.py`): d5E reproduces d4E on all 10
   of the GT-slower smoke games (gi 0 19 22 27 34 38 55 60 86 95); d5N is slower on 9 of 10. The first diverging
   choices are cantrip/scry resolutions or the cantrip choice: gi0 T2 Gamble (d5N) vs Preordain; gi19 T2 Ponder keep vs
   play; gi22 T1 Ponder keep (d5N) vs shuffle. In gi22 T1 the hybrid's probe ran value passes H1..H4 (72K units, 2.7x
   the 27K limit) and committed a value-leaf line, while the emulated ladder committed a heuristic X1 line (7K units).
5. **s5005 gi384 / s6006 gi156** (d8 b0, `$D/tr3/`): gi384: new binary + OLD sidecar (d4E shape) = **T4**; new binary
   + current sidecar (d5N) = **T5**. So gi384 belongs to the SHAPE (the hybrid's value-leaf line choice), and it
   persists at d8 b0, where budget plays no part. gi156: T5 with both sidecars on the new binary, so it is an
   engine/binary change, not the setting. (The overnight report's "old arm" was old binary + old sidecar.) The case
   setting (b30) traces for these two are queued (`$D/tr2/case_*`).

### gi33 (seed 61034, game_index 33) -- a separate PERFORMANCE DEFECT
In every arm, budgets 30/120/480: ~20K-90K units but 1-4+ hours of CPU (d5E_b480 over 8 h); loss (wt 8). The plan
cache is near full (plan=3189M/3200M) and RSS goes past 5 GB (it killed a 4 GB-capped run, exit 137). Units underprice
its work by orders of magnitude. It is not d4/d5 related. Repro job: `$D/gi33/gi33.json` (d5N sidecar, b30, P16 trace,
game trace dir `$D/gi33/gi33.trace`). Not yet diagnosed.

## Jobs (all launched with nohup through `scripts/box_lane.sh`; ALL are reparented to init, PPID 1, so they are
detached and survive this agent stopping)
| job | lane PID | output | DONE marker |
|---|---|---|---|
| gi33 repro | 220201 (child 347040 mtg, running) | `$D/gi33/gi33.{out,err}`, `gi33.wins`, `gi33.trace` | `$D/gi33/gi33.DONE` |
| Q4 traces d4E (8 d5E-slower games) | one of 347195-347198 | `$D/tr2/d4E.*` | `$D/tr2/d4E.DONE` |
| Q4 traces d5E | one of 347195-347198 | `$D/tr2/d5E.*` | `$D/tr2/d5E.DONE` |
| case b30 gi384/gi156 old sidecar | one of 347195-347198 | `$D/tr2/case_d4E.*` | `$D/tr2/case_d4E.DONE` |
| case b30 gi384/gi156 current sidecar | one of 347195-347198 | `$D/tr2/case_d5N.*` | `$D/tr2/case_d5N.DONE` |
| beam probe d5E, MTG_ESC_BEAM=0, 30 games s61001 (gi33 skipped) | 349092 or 349093 | `$D/nb/d5E.*` | `$D/nb/d5E.DONE` |
| beam probe d5N, MTG_ESC_BEAM=0 | 349092 or 349093 | `$D/nb/d5N.*` | `$D/nb/d5N.DONE` |
Finished: main batch (stopped at the b480 gi33 tail on the coordinator's instruction; `$D/main/batch.out.final`,
`$D/main/wins/`), b480 rerun (`$D/b480/`), st1 stats (`$D/st1/`, all four arms incl. d5E rerun), tr1, tr3.
`$D/nb/d4E.json` exists for a no-beam d4E control; it is not launched (d4E has no beam anyway).

## Tools
`$D/tools/pair.py <winsdir> A B <budget> [--2hg] [--list]` (held-out seeds 6xxxx only), `tools/tdiff.py A.json B.json`
(first diverging action of two game traces), `tools/tr.sh <dir> <arm>` / `st.sh` / `nb.sh` (1-thread lane runs with
P16 / hybrid / rollout stats), `tools/mkman.py` (main manifest), `tools/run_big.sh` (pooled batch under
`claude_box.lock`). P16 decision line: `[p16] s=<game seed> T<turn> used limit cd n | <kind><depth>:units/leaf/status/dvs/win`;
W = value warm-up, X = heuristic (emulated) pass, H = ladder pass (value leaf in the hybrid probe).

## Remaining steps, in order
1. Beam probe (`$D/nb`): compare each first decision's passes in `nb/d5E.err` against `st1/d4E.err` (same s61001
   games). If MTG_ESC_BEAM=0 makes d5E's pass costs equal d4E's, the depth-5-only beam (plus value recording) is the
   depth residual. Also check `nb/d5N` vs `st1/d4N`. Note MTG_ESC_BEAM is process-wide.
2. If confirmed, measure the candidate(s) on the b30 held-out sample (same seeds 61001-64004 x 600, one pooled batch on
   the box lock, MTG_PGO=0, gi33 skipped): d5E-nobeam, d5N-nobeam (an env lever; per-job needs a manifest key or
   `flags`, so check `heurarm`/ValueArm for a per-job beam override before using a process-wide env), plus d5E and d4E
   as references. Report quality, units and clean CPU.
3. Q4 write-up: read `$D/tr2` (8 d5E-slower games + case gi384/gi156) with tdiff + P16. For each, name the first
   diverging decision and why (a pass refused by the gate after a costlier heuristic pass; a shallower commit; a
   value-leaf line kept).
4. gi33: read `$D/gi33` P16 / trace for the turn that burns CPU (plan cache saturation), and report it as a perf item.
5. Write `docs/design/hinata-d4-vs-d5-2026-11-21.md` on this branch with Q1-Q5, the mechanism and the proposals with
   quality and CPU; commit; push `wip/hinata-d4-d5` (never the shared branch).
6. Proposals to report to the USER (none adopted): (a) d5 + emulated ladder (b30: -0.060 vs shipped d5N, t -7; +0.013
   vs d4E; 1.65x d5N units; clean CPU owed); (b) if the beam probe confirms, the depth-5-only beam/value-recording as
   the depth-residual cause, and a fix that keeps depth 5 (beam off, or the beam applied by a depth-independent rule).
   The adoption bar applies: a held-out A/B on the decks that move.
