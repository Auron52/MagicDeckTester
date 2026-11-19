# F7 handoff: Maze's End s3 completeness hole (second land drop after a mid-turn breakpoint)

Branch `wip/maze-f7`, worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-f7`, scratch
`/workspaces/MagicDeckTester/build/scratch/maze/f7/` (F below).

## Mechanism (confirmed in code)
A breakpoint continuation is a Plan, and a Plan carries ONE land drop (`bp_play_searched_land`). The site-10
extra-drop route (`TurnSolver::LandDropNewlyOpen`) arms only at the section window in `ApplyPlanDirect`, ONCE,
before the deferred re-solve loop (`while (deferred_cantrip_resolve && (top_first_pass || deferred_top_rearm ||
partition_loop) ...)`), and every armer inside a continuation is `sink_stack.empty()`-gated. So after a tutor's
site-10 continuation played Maze's End, the drop Exploration still held open was never re-offered: The Black Gate
as the second drop was unreachable at any budget. (The first continuation never offers The Black Gate directly
because MTG_BP_NEW_ONLY drops an old-card land while no land has been played this turn; with NEW_ONLY=0 it
appears, but its own follow-up drop (Maze's End) was dropped by the same missing re-arm, so it was T6 either way.)
Not a provider prune; not `lands_played_this_turn` vs `LandDropsAvailable()` (those are right).

## Done
- Commit `c7af5242a` (rebased onto origin/phase-1-2-deck-analyzer 6bbfe4df1; pre-rebase fcb3f6f67):
  - rollout: at the end of each deferred pass, `LandDropNewlyOpen(drops_at_section, ...)` re-arms site 10 for
    another pass (`deferred_drop_rearm`, like the tutor-to-top re-arm; not sink-gated).
  - executor: `exec_drop_rearm_loop` after the two no-commit end-of-main `resolve_draw_breakpoint(0)` calls.
    Committed lines need nothing (each pass is its own rec_bp_ord segment; the catch-all replays every ordinal).
  - hatch `MTG_BP_DROP_REARM` (heurarm slot BP_DROP_REARM, shared reader in EngineFlags.h), DEFAULT ON, =0 reverts.
    Its output with =0 was byte-identical to the parent binary on the repro.
  - Unit test "Maze's End F7: two land drops AFTER a mid-turn tutor breakpoint" in test/unit/test_mazes_end_i3.cpp:
    FAILS on the parent build (6 != 5) and passes with the fix.
  - Deck-gated by deck_has_static_land_drops: only Exploration has the param, and only Maze's End plays it, so no
    other deck can move. (Smoke/regression in the chain should confirm that.)
  - The nested occurrence is BRANCHED, not a front() pick: FSW trace shows child 0 cont=[Maze's End, The Black Gate]
    and the wave walker's nested slot (EMPTY@1 -> cont=[Maze's End]).

## Measurements
s3 T4 frame-0 repro (`build/scratch/maze/r1/handoff2.py references/Maze_s_End/claude_s3_gi2.json 4 -- ...`):
parent T6 at d2 b0, d5 b20/b100/b500/b2000, d8 b0. Fix: T5 at d2 b0, d5 b100/b500/b2000, d8 b0; d5 b20 still
T6 (budget only, the USER's settings question).

Held-out A/B (one pooled batch, arms innermost, `F/ab1/`, seeds 211001.., mtg.f7 with flag off vs on; loss=9):
| row | games | off | f7 | better/worse | net | units/game off -> f7 | ms/game |
|---|---|---|---|---|---|---|---|
| d5 b20 | 1500 | 5.6620 | 5.5967 | 105/9 | -98 | 135,740 -> 132,902 | 3,910 -> 3,864 |
| 2HG d5 b20 | 400 | 5.5425 | 5.4900 | 22/1 | -21 | 114,163 -> 114,058 | 3,781 -> 4,104 |
| d3 b10 | 400 | 5.6550 | 5.6125 | 17/0 | -17 | 76,319 -> 74,863 | 2,233 -> 2,077 |
| d0 | 500 | 6.9840 | 6.9840 | 0/0 | 0 | 0 | 1 |
Net <= 0 on every row; units flat to slightly lower (no extra cost to fold away, so no P5b proposal needed).
Slower games (10): d5 g11, g506, g645, g673, g675, g889, g1026, g1244, g1430; 2HG g41 (seed = base + gi).

## Queued chain (do NOT kill it)
`flock /workspaces/MagicDeckTester/build/heavy.lock F/chain2.sh`, flock PID 1334989 (parent shell 1334988).
Stages (F/chain2.stage): rebuild the rebased tree -> `F/mtg.f7r` (log F/build_r.log) -> d8 b0 recovery batch
`F/d8/manifest.json` (20 jobs: each slower game, both arms, abandon_units 50M; output F/d8/run.out, DONE F/d8/DONE)
-> full unit suite (F/unit.log, F/unit.DONE) -> `MTG_PGO=0 bash test/regression.sh --both` in the worktree
(F/tiers.log, F/tiers.DONE). Final marker: F/chain2.DONE ("OK" or "BUILDFAIL").

## Remaining, in order
1. d8 recovery: `python3 F/anab.py F/d8/run.out off` (names D8_<tag>_g<gi>_<arm>). Bar: every f7 game reaches a
   win turn <= its off twin at d8 b0, none abandoned (check run.err for abandon). Any that does not: root-cause it
   (CLAUDE.md: "draws diverge" is not an explanation).
2. Unit suite: F/unit.log must show 0 failed.
3. Tiers: only mazesend / mazesend2hg keys may FAIL; any other deck moving is a defect (the change is deck-gated).
   Then `bash test/regression.sh --both --accept --deck=mazesend,mazesend2hg` with a ONE-LINE ACCEPT_ACK note
   (the A/B above), diff test/regression_gt.txt, `python3 test/check_gt_logs.py`, commit, push wip/maze-f7 ONLY.
   Overnight GT is left to the periodic overnight run (say so in the commit).
4. Write logs/maze/F7-results.md and report to the parent: mechanism, commit, A/B table, d8 recovery, GT, head.

## USER rulings in my brief (verbatim as given by the parent)
- "a budget may re-price work, it may not make a line unfindable" (CLAUDE.md completeness invariant).
- Fix "as a searched capability at every budget (no default-off lever; no greedy pick in the search window)".
- "P5b's fold is a USER decision, so propose, don't ship".
- Box rules (USER, mandatory): ONE heavy process at a time via flock build/heavy.lock; MTG_BUILD_JOBS=12;
  batches --threads $(nproc), default cache budget, MTG_RSS_CAP_GB=20; no timeouts; waiters on PID/DONE markers,
  never pgrep -f/pkill -f; references/ read-only; don't touch the running viewer.
- Push wip/maze-f7 only, never the shared branch; rebase, never merge.

## USER decision to surface
- s3 still ends T6 at the shipped d5 b20 (T5 from b100 up): a budget/settings question for the USER, as R1 said of s6.
- MTG_BP_DROP_REARM is default ON (a completeness fix, clean win: better and not more expensive). The hatch can be
  deleted once accepted.
