# Kill-only fleet VERIFY + round-stop identity batch: results (2026-11-26)

Binaries: `mtg-kv` / `mtg-id` built on wip/mill-k 402b1682c (+ f5ecf5a16). Scratch: `build/scratch/mill-k/{kverify,ident}`.

## Fleet VERIFY (kverify): PASS
- 31 decks x std/2HG, 40 games each, arm KF (`MTG_LEAF_KILL_ONLY_NOM2=1 MTG_KO_BOUNDARY_SIM=1`), `MTG_LEAF_KILL_ONLY_VERIFY=1`.
- 53 jobs reported `[ko-verify]` lines: **forks=77,826, verified=77,826, MISMATCHES=0, lost=0, bound_diff=0.**
- Heartbeat 32/32 workers busy (fork cost did not starve the pool). Wall 09:26 -> ~10:05 UTC.
- Largest samples: Maze's End 2HG 20,693 forks, Maze's End 17,070, Minotaur 2HG 4,238, Mill 4,075, Mill 2HG 3,269.

## Round-stop identity (ident): PASS
- Mill and Mill 2HG, 15 seeds x 50 games each; arms S (KF + `MTG_KO_CROSS_ROUND_STOP=1`), N (KF, stop off), K (K only, stop on).
- **S vs N: 30/30 pairs identical in digest and units.** Whole-game CPU S/N = x0.988.
- Totals (turns over 750 games): Mill N 1491 / S 1491 / K 1491; Mill 2HG N 1396 / S 1396 / K 1395.

## Verdict
VERIFY is sound on the fleet (0 mismatches) and inert; `MTG_KO_CROSS_ROUND_STOP` is byte-identical and cheaper. Both land in land-1126b.
