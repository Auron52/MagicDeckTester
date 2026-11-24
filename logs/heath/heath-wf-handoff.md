# heath-wf handoff (2026-11-24) -- Worldfire cost of the heath fetch-hint fix

Branch `wip/heath-wf` (from origin/wip/heath-derived a6fe07f04), worktree `.claude/worktrees/heath-wf`.
Read `docs/design/heath-produces-2026-11-22.md` Round 6 first.

## Done
* c13167c19 `MTG_ROLLOUT_FETCH_STATIC_HINT` (default OFF, heurarm slot ROLLOUT_FETCH_STATIC_HINT): the playout's legacy
  land drop (TurnSolver.cpp SimulateLandPlayOne, via PlayoutLandColours in LandPlay.cpp) reads a fetchland's static
  `produces`. Units 4128/4128. Identity checks (lever off == derived, lever on == diag bit 4) on 5 games.
* Root cause (diag build, not committed): of the 20 largest r9 slower Worldfire games, reverting ONLY the playout reader
  returns 17 to base's turn (+1 partial); the greedy ranker owns the other 2; 4 of 6 faster stay faster.
* A/B generator + runner + report: `logs/heath/wf/` (copy and written manifests in `build/scratch/heath/rwf/`).

## Remaining, in order
1. COORDINATOR: `bash /workspaces/MagicDeckTester/build/scratch/heath/rwf/run.sh` (box lock inside; base batch 240
   jobs / 24000 games, fix batch 480 jobs / 48000 games; ~95k core-s). Markers rwf/DONE_base, DONE_fix, DONE; report
   rwf/report.txt; slower list rwf/slower.txt.
2. Read play rows: base>derived (the fix), base>wf (fix + lever), derived>wf (the lever). Worldfire normal + decoupled
   first, then the nine other decks. Replay new slower games of the arm that would ship (MTG_NC_SEARCH=1 on both arms
   for the clairvoyance check, as in Round 5).
3. Bring the lever default to the USER with numbers + recommendation (do not flip silently). If ON: smoke + regression
   on the new default, audit slower searched games at d8 b0, accept GT (Round 5 step 4).
4. Clean up: worktree heath-base2 etc. only via scripts/safe_worktree_remove.sh after check_references_safe.py.

## USER rulings (verbatim, 2026-11-24)
"I think I would be willing to accept something that helps a lot of decks, but doesn't help a few deck and then look
for ways to mitigate the cost on the other decks without blocking the adoption. Naturally, if the only reason it is
worse on those other decks are the specific seeds we can just move on. d0 is not very important at all as I said
previously. The primary thing that matters is play settings, though we should also check whether any worse case is a
result of clairvoyant play. We can ignore those." / "(until we can show it is clairvoyance, variance or fix it)" / "If
play settings are super marginally better, but other settings are much worse it might be worth holding back as well,
but usually play settings being better is enough."

## Running jobs
None of mine.
