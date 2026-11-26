# maze2hg-perf handoff

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze2hg-perf`, branch `wip/maze2hg-perf`, off origin
phase-1-2-deck-analyzer e430cc585. Scratch is `/workspaces/MagicDeckTester/build/scratch/maze2hg-perf/` (written $S
below). Binaries: `$S/bin_before/mtg` = e430cc585 Release; `$S/bin_after/mtg` (and `mtg-test`) = the tip Release.

## Done
- **c5c8877d9** (pushed to origin `wip/maze2hg-perf`). Byte-identical per-node cuts, the dead-crossing price, and
  unit tests:
  - src/ai/DecisionProviders.cpp:
    - `MazesEndProvider::EtbTapSubtypeOrder`: decorate-sort (each key computed once).
    - `GenericProvider` ManaSourceRank: `ScaledManaFeederMana` is read lazily, behind the devotion-land tests.
    - `MazesEndProvider::LandEntryPinCandidates`: the spend scan exits once `base`'s colours are covered.
  - `workctr::kDeadCross` ("dead_x", `MTG_DEAD_CROSS_PRICE`, default ON, price 21,426 ns = the turn-step price), in
    src/core/WorkCounters.h. The flag is defined in src/core/SpellEffects.cpp. In src/ai/TurnSolver.cpp,
    `BillDeadCrossing` / `BillFailedCrossing` run at all 4 FSLineTail BoundaryFan sites, and there is a census column
    `w_dead_x`.
  - scripts/census_priced.py knows dead_x.
  - test/unit/test_mazes_end_i3.cpp: 2 new tests.
- The tree is clean: everything is committed. The report could NOT be written as a file because the harness blocked
  it. Its content is in the final message to the coordinator (it should go to `logs/maze2hg-perf/report.md`).

## Root cause
- **g1647** is the g803 defect and is already fixed on e430cc585.
  - The hold-up A/B binary predated the g803 fixes. It gave digest 0bf2d11a41c16f26, wt 7, 59,868 / 60,097 units,
    1528 / 1961 s.
  - e430: digest 6b119926d7b287cb, wt 7, 63,114 units, 9.5 s wall / 9.5 s CPU (quiet-ish box).
  - e430 with `MTG_DEAD_TAIL_FAN_SKIP=0 MTG_BURGEONING_IDLE_FOLD=0`: the T6 decision ran > 370 s with 12.0M fan
    crossings (killed).
- **g1380** (e430): digest 833668b2aae5ae1f, wt 6, 172,253 units, 990 s wall, 962 s CPU. Four decisions hold 97%:

  | decision | units | wall | fan crossings | payments |
  |---|---|---|---|---|
  | T5 | 224 | 305 s | 8.20M | 18.28M |
  | T3 | 238 | 153 s | 8.80M | 17.85M |
  | T4 | 628 | 179 s | 6.13M | 17.89M |
  | T5 | 154 | 347 s | 9.93M | 17.80M |

  A TMPDIAG (removed) found every big fan at FSLineTail's single-main site (site 4), at T6, d0, with `cutoff = 6 =
  turn`. Each fan had 65k-284k crossings, every child was refused at FSLineWin entry, and no crossing won. That is a
  `turn == cutoff` sibling of g803's `turn > cutoff`: a boundary win is still adoptable there, so a skip is not sound.
  No repeats: 50 distinct pre-crossing keys out of 50 fans. The priced ceiling (X=100, 45 s priced) DOES bind:
  about 18M payments x 2,561 ns. But a crossing was priced only through its ~5.6 us of payments, against 20-60 us of
  real work.

## Measurements
- **Identity batch** (`$S/ident.json` = g803's ident2, 712 games, 2 threads): `id_before` vs `id_after` (price ON).
  Every job's digest, every per-game `.wins` line and every unit count is IDENTICAL. CPU went 7131 -> 6794 s (-4.7%,
  contended). Per job (units, CPU s before / after):

  | job | units | CPU s before / after |
  |---|---|---|
  | bloodsun std | 11,814,755 | 1142 / 1065 |
  | bloodsun 2HG | 12,714,798 | 1300 / 1298 |
  | maze s1001 | 12,713,131 | 725 / 722 |
  | maze s2002 | 19,913,878 | 1190 / 1062 |
  | maze s3003 | 20,591,120 | 1157 / 1104 |
  | maze2hg s1001 | 2,860,168 | 160 / 154 |
  | maze2hg s2002 | 12,068,838 | 726 / 685 |
  | maze2hg s3003 | 12,851,592 | 732 / 703 |

  Compare with `python3 $S/cmp.py id_before id_after`. The price-off arm (`id_after_p0`) was killed as redundant.
- **g1380 on the tip**: digest 833668b2aae5ae1f (same), wt 6, units 171,522 (-731, so a play change by the GT
  definition). The first T5 decision made 1.48M crossings (was 8.2M) with 94 units (was 224).
  - Run t_after, concurrent with the trio below: 410 s wall, 356 s CPU.
  - Each slow decision took about 100 s: T5 100, T3 101, T4 99, T5 98.
- **Concurrent trio, g1380, load ~36**: t_before T5 544 s, T3 392 s, T4 557 s (still running). t_p0 (the cuts alone,
  `MTG_DEAD_CROSS_PRICE=0`): T5 384 s, T3 382 s, T4 363 s, same units 224 / 238 / 628 (still running). t_after: done,
  as above.
- **g1647 pair, concurrent and loaded**: before 27.5 s wall / 23.8 s CPU; after 25.0 s / 21.7 s. Same digest and units.
- **Units**: `-tc="Maze's End*"` all pass. Full suite: 4195 cases, 2 failures, both in Windswept Heath card-data tests
  (`test_bloodsun_suppression` hand_fixing_produces; `test_boars_cards` Heath produces). Neither file is touched here;
  they come from origin's c1206ef6b, so they fail on origin too.

## Running jobs (do NOT kill; let them finish)
- PIDs 3699395 / 3700914 (per the coordinator) = the `t_before` and `t_p0` g1380 runs.
- Output: `$S/t_before/{out,err,census.tsv}` and `$S/t_p0/{out,err,census.tsv}`. Each is done when `$S/<tag>/DONE`
  exists. Read `out` (digest / units / ms / cpu) and the `err` "User time" / "Elapsed" lines.
- Expected: t_before digest 833668b2aae5ae1f with 172,253 units. t_p0 should be the SAME digest and SAME 172,253
  units: the cuts are byte-identical.

## Remaining, in order
1. When t_before and t_p0 finish, record their wall / CPU against t_after (410 s / 356 s) in report.md, and check
   that t_p0's units are 172,253.
2. Commit `logs/maze2hg-perf/report.md` and this handoff with `git add -f`, then `git push origin wip/maze2hg-perf`.
3. Parent decisions:
   - GT: smoke / regression should be byte-identical unless a row is ceiling-bound.
   - Follow-up: charge dead crossings in UNITS (a play change; needs a held-out A/B), or a deck vouch that no
     boundary win is possible (for a sound `turn == cutoff` skip).

## Brief rules (verbatim from the parent's brief)
- "Preferred: a sound collapse of wasted work (default ON). Otherwise: a price, so the work is charged to the budget
  and stays inert at unlimited budget (the CLAUDE.md "a budget may re-price work" rule). Never a lossy gate or a
  greedy pick. Add a unit test."
- "Check identity: run a pooled before/after batch of a few hundred Maze's End + Blood Sun standard + 2HG games (like
  g803's identity batch). Report whether digests and units are identical. Anything that moves play is a play change;
  then also list slower games and check each at `--depth 8 --budget-ms 0`."
- "Single games, perf runs and units go through `bash scripts/box_lane.sh --slots N -- <cmd>` with N <= 2."
- "Any batch over ~4 threads runs under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with
  `MTG_RSS_CAP_GB=40` and `MTG_PGO=0`."
- "Builds: `flock /workspaces/MagicDeckTester/build/scratch/claude_build.lock ./build.sh`, about 200 s after a src
  edit. Never raw cmake."
- "Never use `timeout`. Wait with `run_in_background` loops on a PID or a DONE marker; never `pgrep -f` or `pkill -f`."
- "Push only `wip/maze2hg-perf`. NEVER push `phase-1-2-deck-analyzer`. Never touch `references/`."
