# Mill / K (MTG_LEAF_KILL_ONLY_NOM2) mitigation: handoff (2026-11-26 ~04:20 UTC)

Branch `wip/mill-k` (worktree `/workspaces/MagicDeckTester/.claude/worktrees/mill-k`, based on origin
phase-1-2-deck-analyzer 9ca7deb5e). Nothing has been pushed yet. No default was changed.

## Mechanism (FOUND, confirmed)

K's kill-only turn skips every candidate that has not killed by the end of combat (no m2 on Mill). It assumes that a
candidate which has not killed by then cannot end the game this round. `KillOnlyBoundaryEndsGame` covers the
exception where the game ends at the turn BOUNDARY (credited to this turn), but it is gated on
`deck_has_crossing_land_put` (Maze's End only). Its comment says "a deck-out is flagged the moment the library
empties", and that is true of every mill INSIDE our turn. The passive opponent's own turn also runs in the boundary
(`SimulateEndAndStartNextTurn` -> `opponent_turn()` -> `ResolvePassiveOpponentLandTurn`, SpellEffects.h ~3662).
When the opponent cracks a fetchland there, every Archive Trap in our hand is cast for {0} and mills 13
(`MillToGraveyard`). That is a deck-out at the boundary, credited to this turn by the `opponent_decked` return
before `AdvanceTurnCounter`, on a board no main phase saw. So under K the killer "hold Archive Trap" gets skipped,
and the solve returns a non-killer. That makes the earlier root read a winning line as no-win.

Worked example, Mill seed 58077083 gi83 (std, 3 -> 4 under K). The T1 root (FSLineWin, `MTG_FS_ROOT_DUMP=1`
`MTG_FS_ROOT_DUMP_SIM=1`) scores "Oboro + Hedron Crab" as win 3 today and win 9 under K. Today's T3 killer inside
that tail is "Polluted Delta + Fractured Sanity" holding Archive Trap. The rollout after it, `rollout from t3 d0
cutoff=3`, returns 3 immediately: the Trap left the hand in the opponent's turn. Under K the T3 rollout picks
"Delta + Surgical Extraction + Fractured Sanity" instead. Surgical exiles the opponent's fetch, so there is no Trap
window and the line scores 9. The root then casts Surgical Extraction on T1 (val=400 line, win 4), and the game
ends on T4.

The first decision that differs, from an action diff of the game logs (`build/scratch/mill-k/tools/acts.py`): T1
main 1, K adds a Surgical Extraction cast.

## Fix (committed 3fc84b086; a verify-tool fix is in the tree, see below)

- `MTG_KO_BOUNDARY_SIM` (heurarm slot `KO_BOUNDARY_SIM`, per-job armable, **DEFAULT OFF**). For a
  non-crossing-land deck, `KillOnlyBoundaryEndsGame` crosses the boundary on a copy
  (`SimulateEndAndStartNextTurn(ce) && OpponentHasLost(ce) && ce.turn_number == t`), the same as its existing
  no-fan branch. Crossing-land decks keep their fan path unchanged. It is general (no list of mechanisms) and
  charges no units: SimulateEndAndStartNextTurn calls no `ConsumeAt`. So KF == K byte-for-byte wherever no boundary
  win exists. Recommendation: fold it into K unconditionally when K is adopted, as a soundness fix with no lever.
  It is gated OFF only so that no default moves.
- Unit test `test/unit/test_leaf_kill_only.cpp`, "a deck-out in the opponent's turn (Archive Trap after their
  fetch) is found at the cutoff". It passes (9/9 "Leaf kill-only*" cases). It is NOT yet checked that it FAILS with
  the arm off.
- Committed with this handoff (rebuilt; unit tests 9/9 pass on it): the
  `MTG_LEAF_KILL_ONLY_VERIFY` reference run now gets a fresh `TranspositionTable`. On the shared table, its first
  lookup is the entry the kill-only run had just stored for the same state, so the reference echoed it back. On the
  58077083 game, verify reports `verified=8 MISMATCHES=0` at the play setting and `verified=6 MISMATCHES=0` at d5 b0
  with the old binary: it was blind to this defect. This is diagnostic only, inert unless VERIFY is set.

## Measurements so far

- The original 20 Mill K-slower games (18 std + 2 2HG, seeds from logs/k-fleet slower_ab*.json; there are 20, not
  22) were replayed on the new binary (`build/scratch/mill-k/old22/`). Sum of turns: T 84, K 104, KF 85. **19 of 20
  are fixed by KF** and all reproduce under K. The one left is 58085000 gi59 (std): T 6, K 7, KF 7.
- 58085059 gi59's residual is budget re-spend, not a defect. `MTG_P16_TRACE=1` at the T4 decision:
  - Today: H1 9u -> 7, H2 6157u -> 7, then the dev-seq second ladder H2 659u -> 6. Committed depth 2, win 6 (Glimpse).
  - KF: H2 costs only 9u (K), so the ladder goes to depth 3. H3 6393u -> 7 (non-monotone vs depth 2's 6). The second
    ladder's H3 overruns (11882u, used 18293 of 18000). Committed depth 3, win 7 (Fractured Sanity).
  - Depth-k-worse-than-k-1 also happens without K (today T3 at b100: H3 -> 6, H4 -> 7), so it is general.
  - The fleet verdict for this game: recovers at d8 b0 (6).

## Running jobs

- **Pooled A/B: PREPARED, NOT RUN** (was queued as run.sh pid 3884618, flock 3884622, parent
  shell 3884616). Manifest `build/scratch/mill-k/ab/ab.json`: 648 jobs, 81,000 games, est ~99k core-s (~52 min at
  32 threads). Mill T/K/KF at 8000 std + 4000 2HG (seeds 59.0M+). The other 30 armed decks K/KF at 500 + 250 (seeds
  59.5M+). Binary snapshot `mill-k/build/Release/mtg-ab` (= 3fc84b086 code, verify fix not included; that is
  irrelevant, since VERIFY is off). Markers: `build/scratch/mill-k/ab/DONE`, `rc.txt`; output `ab/batch.out`,
  `ab/wins/`. A stale DONE waiter (pid 3883179, my shell loop) also watches that marker. The coordinator said not
  to start the A/B myself, so this agent KILLED pids 3884616/3884618/3884622/3883179 before it began (nothing ran;
  no DONE marker exists). Relaunch with `logs/mill-k/run.sh /workspaces/MagicDeckTester/build/scratch/mill-k/ab`.
- No jobs running. build/Release/mtg now includes the verify fix; build/Release/mtg-ab is the 3fc84b086 snapshot.

## Remaining steps, in order

1. (build2 DONE, unit tests 9/9 pass.) Still to do: confirm the new verify catches the defect on the old arm ---
   K only,
   `MTG_LEAF_KILL_ONLY_VERIFY=1`, seed 58077083 gi83, should print MISMATCH. With `MTG_KO_BOUNDARY_SIM=1` it should
   print 0. (The verify fix itself is committed.) Optionally check that the new unit test fails with the arm forced to 0.
2. Run the pooled A/B (above) under the box lock, `MTG_RSS_CAP_GB=40 MTG_PGO=0`, one batch.
3. `python3 logs/mill-k/summarize.py build/scratch/mill-k/ab`. It writes tables for K vs T, KF vs T and KF vs K
   (per-row net, faster/slower, t, units, cpu, and identical-jobs count for KF vs K), plus `ab/slower.json`.
4. Slower games: `python3 logs/mill-k/slower.py mk build/scratch/mill-k/ab` (KFvT and KFvK). Run `ab/sl/d8.json`
   as a batch into `ab/sl/w_d8` (small: through `scripts/box_lane.sh --slots 2`, `--threads 2`). Then `slower.py
   mknc`, run `ab/sl/nc.json` with `MTG_NC_SEARCH=1` into `ab/sl/w_nc`, then `slower.py verdict`.
5. Commit the results under logs/mill-k (`git add -f`, since logs/ is gitignored) and push ONLY `wip/mill-k`.
6. Report the mechanism, the fix, per-deck nets (Mill std/2HG and KF==K identity elsewhere), unit ratios and
   slower-game verdicts. Change no default; the parent puts it to the USER with the K decision.

## USER rulings from the brief (verbatim)

- The USER's adoption bar: "adopt if many decks gain at play settings; mitigate the few worse after; ignore seed
  noise and clairvoyant losses; d0 barely counts".
- "If it is a defect in K (for example, K does not treat "opponent draws from an empty library" as a kill), fix it in
  the general K code, not with a Mill-specific setting. The USER has a slight preference for no per-deck settings."
- "Do not change any default. The parent will put the result to the USER together with the K decision."
- Box: ONE big engine process at a time under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with
  MTG_RSS_CAP_GB=40 MTG_PGO=0; small jobs via `scripts/box_lane.sh --slots N` (N <= 2); pool into ONE `mtg
  --batch`; builds under `claude_build.lock`, wait ~200 s after editing src; never `timeout`, `pgrep -f`, `pkill -f`.
- Git: push only wip/mill-k; never the shared branch; rebase, never merge.
