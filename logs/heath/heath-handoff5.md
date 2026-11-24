# heath handoff 5 (2026-11-24) -- for a fresh agent with NO context

Read CLAUDE.md first (box lock `/workspaces/MagicDeckTester/build/scratch/claude_box.lock` for ONE big process; small
jobs via `scripts/box_lane.sh --slots 2`; never `timeout`; never push phase-1-2-deck-analyzer -- the parent lands it;
never scripts/push_queue.py; references commit-only). Full measurements: `docs/design/heath-produces-2026-11-22.md`
("Round 5" is this session). Previous handoff: `logs/heath/heath-handoff4.md`.

## Tree / branch
`.claude/worktrees/heath-derived` = branch `wip/heath-derived` on 249db2aa7 (origin has moved on; the parent rebases
at landing). This session:
* a0773a63d `MTG_FETCH_AFTER_DUAL` (heurarm slot FETCH_AFTER_DUAL, reader `FetchAfterDualEnabled` in EngineFlags.h,
  **default OFF**): the greedy ranker's multi pass (LandPlay.cpp GreedyLandRank) and its rollout mirror (TurnSolver.cpp
  SimulateLandPlayOne) try non-fetch multi-colour lands before a fetchland. Unit test in test_boars_cards.cpp.
* docs commit (Round 5).
Binaries (build volume, `build/scratch/heath/`): `mtg.fad` = a0773a63d; `mtg.base2` = 249db2aa7 (worktree
`.claude/worktrees/heath-base2`, detached; remove later with scripts/safe_worktree_remove.sh); r4 pair `mtg.base` /
`mtg.derived` (worktrees heath-base / heath-produces).

## USER rulings (verbatim)
* "I don't know what this is for, but W/U is understating the power of fetchlands. They can get any shock in the deck, so
  they can theoritically produce any colour those can get."
* "Yes, for 1 we should count shocks. I'm less certain about triomes because of the come-into-play-tapped, but usually
  shocks cover the colours we need. 2. Orchard first makes sense for the decks you mentioned."
* "I'm more concerned about areas that help searched."
* 2026-11-24: "I think I would be willing to accept something that helps a lot of decks, but doesn't help a few deck and
  then look for ways to mitigate the cost on the other decks without blocking the adoption. Naturally, if the only reason
  it is worse on those other decks are the specific seeds we can just move on. d0 is not very important at all as I said
  previously. The primary thing that matters is play settings, though we should also check whether any worse case is a
  result of clairvoyant play. We can ignore those." / "Play settings are the most important, so if the change was
  specific to Maze's end we shouldn't adopt." / "(until we can show it is clairvoyance, variance or fix it)" / "If play
  settings are super marginally better, but other settings are much worse it might be worth holding back as well, but
  usually play settings being better is enough."

## Findings this session (details + tables in the doc, Round 5)
* ONE mechanism on Burn / Morph / Five Colour: the deck-derived hint makes a fetch in hand multi-colour, so it ties a
  real dual in the greedy ranker's multi pass; hand order decides; the search's last-resort land tie-break names that
  pick; the fetch's reshuffle then deals a different game. Honest value of the choice ~0; under the lockstep search a
  held fetch is a clairvoyant reshuffle-timing option.
* Classification of r4 play slower games (MTG_NC_SEARCH=1 on both arms): Burn 16 = 13 clairvoyance / 1 recovers / 2
  flip-variance (6410, 8192); Morph 6 = 4 / 1 / 1 (2276); Five Colour 4 = 4 / 0 / 0; Worldfire = diffuse variance (73
  changed, 34 fast / 36 slow, scattered first divergences), 103 / 1363 / 1684 / 205 UNRESOLVED (no NC run).
* Lever on, single games: Burn 15/16 slower recover (4/5 faster revert), Morph 5/6 (3/3), Five Colour 4/4.

## Remaining, in order
1. COORDINATOR runs the re-measure: `bash /workspaces/MagicDeckTester/build/scratch/heath/r8/run.sh` (needs mtg.base2 and
   mtg.fad; takes the box lock itself; markers r8/DONE_base, r8/DONE_fix, r8/DONE; report r8/report.txt, slower list
   r8/slower.txt). 600 base jobs / 60000 games + 1200 fix jobs / 120000 games, ~145k core-s. Generator:
   `r8/gen_manifest.py` (copy in logs/heath/r8/).
2. Read the report, play rows first: base>derived (the fix as r4 measured it, fresh seeds), base>fad (fix + lever),
   derived>fad (the lever). Decoupled rows = honest-reshuffle check for Burn / Morph. Replay any new slower play game of
   the arm that will ship at the r4 way (r6/arms.sh + MTG_NC_SEARCH=1) before calling it clairvoyance.
3. Lever default: if fad is <= derived on the play rows and keeps the fix's gains, propose it default ON to the USER
   (it is an ORDER heuristic at greedy sites, measured; the default is the USER's call -- state numbers + recommendation,
   do not decide silently).
4. GT: NOT accepted. If the lever stays OFF: src differs from the tier run (500bcdfc8) only by the lever, whose off path
   reproduces derived move for move (21 Burn games + 5 identity checks); re-run smoke to confirm the digests, then
   `bash test/regression.sh --both --accept-with-regressions="<note: heath deck-derived fetch hint; r4 + r8 A/B; tier
   slower all recover d8 b0; Boars 5351 clairvoyance; WF 103/1363/1684/205 open>"` from the r5 run if smoke matches,
   else re-run both tiers. If the lever goes default ON: re-run smoke + regression on the new tree, audit its slower
   searched games at d8 b0, then accept. Then `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`,
   commit, push wip/heath-derived only.
5. Owed / say to the USER: ~60 direct `params.produces` readers still see the static hint; `LandAbilityClasses.h` still
   lists the deleted `hand_fixing_produces` key (harmless); WF 103 / 1363 / 1684 / 205 unresolved.

## Running jobs (mine)
`build/scratch/heath/r7/build2.sh` (lane): heath-derived full build + units (r7/unit.log, r7/DONE_unit), then the
base2 build (r7/build_base2.log, r7/DONE_base2 -> mtg.base2). Progress in r7/progress.
