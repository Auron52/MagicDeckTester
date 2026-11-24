# g993 handoff 2 (agent 2): second cause found and fixed; g993 wins T4 under the default

Branch wip/maze-applydrop, worktree /workspaces/MagicDeckTester/.claude/worktrees/maze-applydrop.
Scratch: /workspaces/MagicDeckTester/build/scratch/claude_maze_applydrop/g993y/ (cp/ = claude-play hand-off probes).

## Second cause (evidence)
* g993 is not re-searched at T4: the engine commits the T3 line, so the T4 miss is reproduced by a claude-play
  hand-off at the T4 frame: `cp/ta.sh <tag> '1,0,0,0,0,1,0,0,0,4,0,2,0,0,0,0,0,-1' [ENV=..]` (then-auto, d8 b0).
  Rank off -> T4, default -> T5. After plan 67 (land Planar Nexus, Exploration, ME#43 -> Baldur's, ME#44 drops), the
  hand-off wins T4 in BOTH arms, so the base line's root plan is fine; the miss is inside its breakpoint fan.
* `MTG_FS_ROOT_DUMP=4 MTG_BP_CONT_TRACE=4`: rank off, the base plan's continuation list has n=25 and the T4 win is
  bp=18 (a Sylvan Scrying tutor_choice=1 variant of #0); default, n=18 -- every Scrying index-1 variant is missing.
* Mechanism: TurnSolver's tutor-index axis is SIZED on the breakpoint state (Gond Gate still in hand) and READ at
  Scrying's resolution (Gond Gate down). Under MTG_MAZE_RANK_SPENDABLE The Black Gate ranks 5 ({B} spends on
  nothing in this G/U/R list), so MazePickNewNameGate picked Talon Gates as the new name, and the P3 amendment's own
  Talon slot de-duplicated: sizing menu {Talon} (one name, no variant). At resolution (temp `[tutdbg]` trace) the
  menu was [Gateway Plaza, Talon] and pick=-1 took Gateway Plaza 85+ times; the Talon find that feeds the end-step
  Maze's End {3} (the T4 win) was never emitted. The engine assumes the resolution list can only SHRINK (it clamps);
  this one GREW. `MTG_MAZE_LAND_TARGETS=0` also recovers T4 (consistent).

## Fix (uncommitted at time of writing -> see commit below)
MazeRestrictLandTargets: when the Talon slot will append Talon, the new-name pick skips it (MazePickNewNameGate
`skip`), so the menu is {new name, Talon} at both states. Provider restriction only, no pick. Gated inside
MAZE_RANK_SPENDABLE's scope (MAZE_RANK_SPENDABLE && RANK_OBJECT_COLORS), so RANK_OBJECT_COLORS=0 is the committed
tree's play. MTG_MAZE_RANK_SPENDABLE STAYS: with it off (rank on) g993 is T5 again (cause 1, the dual tie).
Unit test added: "Maze's End list colours: the new-name slot skips Talon Gates, so Sylvan Scrying's menu stays two names".

## Results (build/Release built WITH two temp diagnostics, since removed from source: MTG_TMP_TALON_SLOT (=0 disables
the fix) and MTG_TUTOR_DBG_TMP; both inert by default, so default play = the committed source)
* g993 d8 b0 full game: new (default) T4; base (RANK_OBJECT_COLORS=0) T4; rank (MAZE_RANK_SPENDABLE=0) T5;
  fix disabled T5. Outputs g993y/fin_*.out.
* UNIT TESTS NOT RUN this session (mtg-test not rebuilt). OWED.

## Remaining, in order
1. `./build.sh` (rebuild without the temp diagnostics; DecisionProviders + SpellEffects ~8 min), then
   `build/Release/mtg-test -tc="*Maze*"` and the mana-payment set (`-tc="*payment*,*pay*,*prepay*"`;
   files test_mana_payment.cpp, test_payable_order.cpp, test_prepay_*.cpp, ...). The new test must pass; if its
   base-arm expectation ({The Black Gate, Talon}) fails, read the actual menu and correct the expectation only.
2. Re-confirm g993 per arm on that clean build (commands in g993-handoff.md; seconds each).
3. Coordinator: held-out A/B manifest /workspaces/MagicDeckTester/build/scratch/claude_maze_applydrop/ab_new.json,
   25,500 jobs (generator logs/maze-applydrop/mk_ab_spend.py: arms base / rank (d4891c6e4 play) / new; fresh seeds
   25500000 / 26500000 / 27500000; Maze's End only -- Snow cannot move between rank and new). Also the d8 b0 replay
   of the 25 previously recovered slower games under the new default.
4. Residual (report, not fixed): the engine's tutor-index sizing still assumes the resolution menu only shrinks.
   Any provider menu that can GROW mid-plan (e.g. Maze's new-name slot opening when the plan's own fetch takes
   Baldur's Gate) can still lose an index variant. A general fix (size on an upper bound) is an engine change.
