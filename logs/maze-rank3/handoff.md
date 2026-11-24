# maze-rank3 handoff (three Maze's End d8 b0 slower games of the rank fix)

Worktree /workspaces/MagicDeckTester/.claude/worktrees/maze-rank3, branch wip/maze-rank3 (from origin/wip/maze-applydrop 56f23f14c).
Scratch: /workspaces/MagicDeckTester/build/scratch/claude_maze_rank3/ (run1.sh single game, bat.sh/mk.py batch,
cp/step.py + cp/ta.sh claude-play stepping / then-auto hand-off at d8 b0; frozen binary = claude_maze_applydrop/newbin/mtg).

## Verdicts (all three: PAYMENT-ORDER DEFECT of MTG_MAZE_RANK_SPENDABLE's rank half; not clairvoyance)
Rank-only arm (RANK_OBJECT_COLORS=1, SPENDABLE=0) recovers every game at d8 b0 (g2030 T5, g1770 T6, g1149 T5,
digests = base). Hand-offs reproduce each miss at the divergence frame; the draws are identical, only a payment differs.
* g2030 (T5 -> T6), T4 frame (choices `1,0,0,-1,0,4,0,-1,0,0,0`): Citadel Gate (W, chosen G) and Boros (R/W) both rank
  10 (mono-spendable); Gateway Plaza's {1} takes Citadel's {G} by battlefield order, so the root plan
  "Gateway Plaza[pay]: Sylvan Scrying + Burgeoning" is not enumerated (base has it; bp-cont after Plaza n=2 vs n=1).
  Forcing base's plan in the new arm then hand-off -> T5. (Base worked because Citadel misranked rainbow 50.)
* g1770 (T6 -> T7), T4: Guild Summit {2}{U}: base pays Manor + Maze's End {C}; new pays Manor + Orzhov (W/B dead -> 5,
  ties Maze's End's {C} 5, battlefield order). Summit's ETB then has no untapped Gate to tap -> one fewer draw.
* g1149 (T5 -> T6), T5 frame (choices `1,0,0,-1,0,0,0,-1,5,2,-1,-1,14,1,-1,-1`): first Maze's End {3}: base =
  Basilisk + Black Gate + Baldur's; new = Basilisk + Orzhov + Black Gate (dead Gates tie the {C} tier at 5);
  the later Summit / second activation then come up short.

## Fix (UNCOMMITTED at writing; source edited, build1 queued on the shared build lock)
MazesEndProvider::ManaSourceRank (SPENDABLE scope only): dead source (no list colour) -> 5 + its colour count (above the
{C} tier, historical mono<dual order); within a rung +1 if the source makes the list's MAJOR colour
(new deck constant GameState::deck_spell_major_color, stamped in StampDeckTraits: colour on strictly most nonland
mainboard copies; Maze = G; 0xFF none/Garth; in the existing hole, sizeof static_assert guards). Unit tests updated
(Orzhov 7, Selesnya 11) + 2 new cases. TEMP diagnostics in source, MUST be removed before commit:
MTG_TMP_NO_TIEBREAK (old ranks) and MTG_TMP_NO_TALON_SKIP (disables the 50f807e9c Talon skip) -- grep TMPDIAG.

## Remaining, in order
1. build1 (log scratch/claude_maze_rank3/build1.log); run fix3.json (`bat.sh fix3 $S/fix3.json 2`, BIN=<worktree>/build/Release/mtg)
   + same with MTG_TMP_NO_TIEBREAK=1 (should equal newbin: T6/T7/T6) + MTG_TMP_NO_TALON_SKIP=1 (Talon isolation).
2. g993 2HG d8 b0 must stay T4 (MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 seed 26200993 gi993, see maze-applydrop g993-handoff.md).
3. Remove TMPDIAG lines, rebuild, `build/Release/mtg-test -tc="*Maze*"`, commit + push wip/maze-rank3 (never the shared branch).
4. NC batch (scratch nc/, MTG_NC_SEARCH=1, abandon 30M) results -> record in the verdicts (expected: not needed, the
   misses are payment-order and reproduce under identical draws).

## Status at stop (13:49 UTC, agent context limit)
* Source committed as WIP (this commit; includes the TMPDIAG toggles; UNBUILT, UNTESTED). Pushed to origin/wip/maze-rank3.
* build1 still QUEUED on /workspaces/MagicDeckTester/build/scratch/claude_build.lock (3 builds ahead at 13:48), waiter
  shell PID 2140187; it builds this worktree's tree when it gets the lock -> scratch/claude_maze_rank3/build1.log (rc= line at end).
  If source changes before then, the build uses whatever is on disk.
* NC batch (MTG_NC_SEARCH=1, d8 b0, abandon_units 30M, 2 threads) still running, mtg PID 2137307, started 13:14;
  no job finished after 34 min. Output scratch/claude_maze_rank3/nc/out, DONE marker nc/DONE. My own experiment:
  kill it if the box needs the cores.

## Update after stop
* build1 finished rc=0 (this WIP source incl. TMPDIAG): `mtg-test -tc="*Maze*"` 124/124 cases, 5173 assertions pass
  (the two new tie-break cases included). Game confirmations (step 1-2 above) NOT run yet.
* NC batch ended rc=143 (SIGTERM, 16:30:45 UTC) with NO job finished -- NC check UN-RUN. Redo with fewer/cheaper jobs
  (e.g. the three base+new arms one at a time) if the coordinator wants it.
