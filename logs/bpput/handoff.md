# bpput-cost handoff (site-10 battlefield-put cost; written by the first agent, 2026-11-26)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/bpput-cost`, branch `wip/bpput-cost` (off
origin/phase-1-2-deck-analyzer 3f91b7d4a). Design + proof: `docs/design/bpput-inert-land-fold.md`.

## Done
- Census instrument `MTG_BPPUT_CENSUS` (TurnSolver.cpp `bpputcensus`, print-only). Base census (held-out seed 9700001,
  cg 100 / cg2hg 50 / melira 100 / landfall 100 / hydras 100 games): `logs/bpput/census_base.txt`.
- Inert-land fold: `CardDefinition::bpput_land_reader` / `bpput_mana_reader` (CardDatabase.cpp
  `BpPutLandReaderFromJson`), `TurnSolver::PutLandFoldCandidate` / `LandPutReaderLive`, `CastPutACardPermanent(...,
  land_put_credited)` (rollout rider `cast_put_credit`, executor `rdb_bf_credited`), mark half
  `PlanLandPutFoldsInert` in PlanOpensBreakpoint's land clause. Unit tests at the end of test/unit/test_land_search.cpp.

## Running / scratch (build/scratch/bpput/)
- `mtg.base` = census-only build of origin play (diagnostic off == origin 3f91b7d4a play). Run with cwd = worktree.
- `chain1.sh` (census, then fold build -> build2.log, chain1.DONE), then `chain2.sh` (units -> unit_plain.log;
  A/B units ab_base.out / ab_new.out on ab.json seed 9800001; census_new.err; smoke.log; chain2.status, chain2.DONE).

## Remaining, in order
1. Check build2.log rc=0 and unit_plain.log (all pass).
2. `python3 logs/bpput/units_report.py $S/ab_base.out $S/ab_new.out` -> units/game table.
3. smoke.log: if keys moved, list them; classify every slower searched game at `--depth 8 --budget-ms 0` (new vs
   mtg.base, box_lane --slots 2).
4. Subset under MTG_UNPRUNED=1 / MTG_HUMAN_PLAY=1: `build/Release/mtg-test -tc='*Inert-land*,*Land search*'` new vs
   base worktree (new failures only).
5. Commit, push wip/bpput-cost only, write logs/bpput/report.md.

## Rulings (verbatim from the brief)
"collapse wasted search unconditionally" but only as a SOUND identity fold; a budget may re-price work but never make a
line unfindable at unlimited budget; no lossy restriction without an A/B and the USER's adoption; never accept GT;
never push phase-1-2-deck-analyzer; never touch references/; MTG_RSS_CAP_GB=20; heavy work under
`flock /workspaces/MagicDeckTester/build/heavy.lock`; MTG_BUILD_JOBS=14.
