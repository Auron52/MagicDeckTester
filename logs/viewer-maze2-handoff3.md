# viewer-maze2 handoff 3 (2026-11-19)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/viewer-maze2`, branch `wip/viewer-maze2`. Push wip/* only, never
the shared branch. References read-only. Never restart the viewer (primary, port 8080). ONE heavy process: wrap
builds/tiers/batches in `flock /workspaces/MagicDeckTester/build/heavy.lock`, `export MTG_RSS_CAP_GB=20`. No timeouts,
no pgrep -f. Earlier handoffs: `logs/viewer-maze2-handoff.md` (root causes of items 1-3 below = USER items 1-3 there).

## Commits (oldest first)
- `0b974260a` JS half of the first three viewer fixes (pushed, tested).
- `cf9c9b1ec` engine half (s5 off-main win dialog, dig `role`) -- BUILT + TESTED this session (see item 3).
- `a3df399a6` handoff-only.
- `41cd55dc8` **WIP, NOT YET BUILT as a whole** (each TU passed `-fsyntax-only`): items 1, 2, 4, 5 below.
  One later uncommitted-at-write edit was folded in before push (see `git log`).

## USER words (verbatim, via coordinator)
- s7: "Here is another mana situation: logs/play/rejections/Maze_s_End_cod_s7_gi6_t2.json"
- F6 (verbatim from F6's report): "CheckLine's table of board-activation costs has no row for Maze's End's ability, so its {3}
  is priced at 0. Even Scrying + Charm + two activations, 11 mana from 7 lands, comes back `legal_not_enumerated`. ... Just
  adding the cost would wrongly grade the legal reordered line Illegal, because CheckLine runs activations after casts and does
  not model the fetched Gate." (case logs/play/rejections/Maze_s_End_cod_s6_gi5_t5.json; F6-2 owns MazesEndProvider::ManaSourceRank -- don't touch)
- s5: "The reference there didn't give me a choice on whether to activate Maze's End at the end." "(I mean it was the right play, but it should be a dialog)"
- s8: "The viewer prevents me from activating multiple maze's end at once in Seed 8"
- "Another annoying thing is that playing exploration + two lands is not allowed without multiple steps."
- (older, item list in handoff 1: ref 4 second target; Manabond message.)

## Item status
1. **Mid-line land (s7 Amulet -> Simic Guildgate -> Burgeoning)**: root cause = CheckLine pays a `landat=k` line on the
   LAND-FIRST board (Gate tapped). Fix (JS): `LB.landAfterCastRoute` in tools/play/linebuild.js + wiring in index.html
   `handleVerdict` (after fetchAutoRoute): on "can't pay" (not "before the land") with the failing step after the first
   segment's single land, the land becomes `defer` -> segments `cast=Amulet of Vigor` | `land=Simic Guildgate;cast=Burgeoning`.
   VERIFIED with the binary: both halves `accept` (plan 6, then plan 2 at choices `1,0,4,-1,-1,6`). JS test
   `checkLandAfterCastRoute` in test/viewer_linebuild_check.js passes (all 0 FAIL).
2. **CheckLine Maze's End** (src/ai/TurnSolver.cpp CheckLineExact): TapTutorPut row in the board-activation modes table
   ({3} priced); declared `act=` tokens take the board path even with a same-name card in hand; repeated activations bind
   DISTINCT sources (first unused copy; fallback first copy); `click_seq` -> `real_pays_line` tries pending order then the
   human's click order (3 call sites); `real_pays` resolves the put on its copy (`resolve_tutor_put`: each distinct library
   candidate tried, keep max AvailableManaPool -> Gond Gate untapped entry + Baldur's count are the board's own) and
   RESERVES every activation source's {T} up front. Unit test appended to test/unit/test_mazes_end_f1.cpp ("CheckLine prices
   the {3} ...") -- expectations NOT yet observed; if a CHECK fails, inspect the reason before changing it. Also re-validate
   the s6 rejection: note line in that JSON; queued order (Scrying first) should now be Illegal, act,act,Scrying legal_not_enumerated.
3. **s5 dialog + dig role** (cf9c9b1ec): built (build1, exit 0), units 3710/3710 PASS (incl. new F1 test). Smoke + Maze
   replay NOT yet run (cancelled my queued runs to re-run on the final binary).
4. **Valve drops 2nd Maze's End (seed 8 gi7 T5)**: reconstructed. Choices
   `0,1,2,2,-1,-1,3,2,4,1,-1,-1,24,4,0,-1,-1,190,1,-1,-1,0` WITH `--cast-order "4:**|cast=Guild Summit|land=Cliffgate"`
   (saved in build/scratch/viewer-maze2/s8/choices.txt; helpers build/scratch/viewer-maze2/{step.py,walk.py,val.sh}).
   Old binary @65536 + MTG_VIEWER_VALVE_DIAG=1: 87872 -> 24200, KEEP Scrying(21) Maze(21) Exploration(1) Charm(24),
   DROP Maze's End(21). Click cost (whole replay; prefix alone 0.10 s): 4.06 s / 0.54 GB at 65536, 8.77 s / 1.28 GB at
   131072 (full). Fix (TurnSolver.cpp valve, after the rank-fill): a dropped repeat group of a return-self TapTutorPut
   source whose first group is kept comes back NARROWED (members ordered: not-controlled names by eval desc, then
   controlled names + decline last; widest prefix that fits), counted as pooled, skipped when pinned. TO MEASURE: rerun the
   T5 frame at 65536 with DIAG -- expect "[valve] narrowed repeat Maze's End ...", dropped_groups 0, and the time.
5. **Exploration + two lands**: main.cpp hand JSON `grants_land_drops` (static_extra_land_drops + grants_extra_land_drop,
   absent when 0); linebuild.js `queuedLandGrants` raises queueCard's land cap; segmentParts already splits at a 2nd land
   and commitLine auto-commits segments. JS test added (0 FAIL).

## Running job
- Build of the CURRENT tree queued: subshell PID **1191950** (flock 1191951), log
  `/workspaces/MagicDeckTester/build/scratch/viewer-maze2/build2.log`, DONE marker = line `BUILD EXIT <rc>`. Queue is long
  (flock is not FIFO). It compiles whatever is on disk when it starts (= the pushed tree unless edited).

## Remaining, in order
1. Wait for build2 (`until grep -q "BUILD EXIT" <log>; do sleep 30; done`); fix compile errors (all TUs passed syntax-only).
2. `./build/Release/mtg-test -tc="*Maze*"` then full units; fix/amend.
3. Validate: s7 split halves (`bash build/scratch/viewer-maze2/val.sh 7 6 ...`), s6 rejection lines (item 2), seed-8 T5 frame (item 4).
4. One lock acquisition: `flock .../heavy.lock env MTG_RSS_CAP_GB=20 MTG_PGO=0 bash test/regression.sh smoke` in the
   worktree -- NO GT key may move (all viewer/human-play-only; CheckLine is --validate-line only; valve HumanPlayActive only;
   `grants_land_drops` JSON only). If one moves, find why, don't accept.
5. Maze replay from the PRIMARY cwd: `MTG_BIN=<wt>/build/Release/mtg python3 <wt>/test/viewer_protocol_check.py --only Maze_s_End --threads 7`
   (baseline build/scratch/viewer-maze2/vpc_base2.log: 4 ok, 2 repaired, 1 ENUM-GAP s12345681 = R1's, pre-existing).
   Expect s5_gi4 "repaired" same T5 win; new ref claude_s7_gi6 now exists (committed f5e05fb17 by parent) -- check it.
6. Squash/clean commit message, push wip/viewer-maze2, `gh run watch <id> --interval 60 --exit-status` in background, report.

## UPDATE: build2 landed BUILD EXIT 0 (whole tree incl. 41cd55dc8 + 4d0b3d090 compiled, no errors). Start at step 2 (units).
