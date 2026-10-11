#!/usr/bin/env python3
"""Fill doc_stall.md's tables and write the final section text (doc_stall_final.md)."""
R = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/logs/rs_crackle/'
s = open(R + 'doc_stall.md').read()
rec = '''| game | condition | 3d64d9a6 | both levers on | RESTRICTOR off | CASTABLE_MANA off |
|---|---|---|---|---|---|
| 2HG gi 254 | shipped hold | 4 / 1 | 4 / 1 | 4 / 1 | 4 / 1 |
| 2HG gi 254 | lever B on | 9 / 17 | 4 / 1 | 4 / 1 | 4 / 1 |
| 2HG gi 254 | hold disabled | 9 / 17 | 4 / 1 | 9 / 17 | 4 / 1 |
| d0 gi 195 | shipped hold | 7 / 0 | 7 / 0 | 7 / 0 | 7 / 0 |
| d0 gi 195 | lever B on | 8 / 5 | 6 / 0 | 8 / 5 | 6 / 0 |
| d0 gi 195 | hold disabled | 8 / 5 | 6 / 0 | 8 / 5 | 6 / 0 |
| d0 gi 865 | shipped hold | 6 / 0 | 6 / 0 | 6 / 0 | 6 / 0 |
| d0 gi 865 | lever B on / hold disabled | 9 / 1 | 9 / 0 | 9 / 1 | 9 / 0 |
| d0 gi 1773 | shipped hold | 6 / 0 | 6 / 0 | 6 / 0 | 6 / 0 |
| d0 gi 1773 | lever B on / hold disabled | 9 / 1 | 9 / 0 | 9 / 1 | 9 / 0 |

(The one failed cast that remains in 2HG gi 254 under every arm is T1 "Sol Ring, Ponder" off a single
land: Ponder's {U} is "paid" by Sol Ring's own {C}{C} -- the same-turn ROCK form of the producer-pays-
itself defect, not touched here; see "Still open".)'''
held = '''One pooled batch (`mtg --batch`, 6 threads, `lowprio.sh`), arms paired by gi. Cells: shipped =
profile depth, budget 20 ms; d3 b10; 2HG d0 (life 30, two heads); d0. **The shipped and d3 cells are
budget-ms cells measured under contention from another container**; their per-job digests reproduced
across two separate runs (and single-game repros), so the comparison is paired, but treat the wall
numbers as contended.

**`MTG_RESTRICTOR_ORDER_EXACT` on vs off** (both other levers at defaults):

| cell | n | turns on | turns off | Δ | faster | slower | search units on/off |
|---|---|---|---|---|---|---|---|
| shipped b20 (contended) | 200 | 1129 | 1129 | 0 | 1 | 1 | 1.013 |
| d3 b10 (contended) | 200 | 1116 | 1116 | 0 | 0 | 0 | 0.997 |
| 2HG d0 | 400 | 2643 | 2645 | −2 | 2 | 0 | n/a |
| d0 | 2000 | 13499 | 13502 | −3 | 3 | 0 | n/a |
| **Hinata2 net** | 2800 | | | **−5** | **6** | **1** | |

* Faster: every depth-0 one (2HG gi 13, 257; d0 gi 286, 1487, 1703) is a game whose off arm commits an
  Irencrag line the executor cannot realise. Failed casts in the trace are Soulfire Eruption after the
  cap, or d0 gi 1487's Irencrag + Soulfire committed and failed **24 times** across T4–T6. The on arm
  has none. Shipped gi 4 (6 → 5) is the same class under search.
* **The one slower game, shipped gi 68 (5 → 6), reproduced twice.** The first divergence is the T3 land
  drop (Island vs Mountain), chosen by the depth-5 search. The lever rejected unrealisable
  restrictor subsets in that search's futures, and the two arms then walk different boards. On T5 the
  on arm commits {Irencrag Feat, Soulfire Eruption}. It is rank-legal (Soulfire ranks before Irencrag),
  but Soulfire is payable only from Irencrag's float. So Irencrag is cast first, Soulfire takes the
  one remaining cast, and the cards Soulfire staged (Sol Ring, Preordain) are refused (`paid=0`). The
  off arm, with Island kept, wins T5 through Soulfire → Reality Spasm ×2 → … → Irencrag → Crackle. The
  lever did not reject either T5 line; the stranded continuation is a residual of the same rank-vs-
  realised mismatch, in its after = 0 form (see "Still open").

**`MTG_HINATA_CASTABLE_MANA` on vs off:** **byte-identical in shipped play**. Every job digest matched
(2HG d0 400, d0 2000; 0 turn changes). Its consumers sit behind two default-OFF levers:
`MTG_HINATA_SUBSET_CREDIT` (Reality Spasm emission and Crackle X sizing) and lever B. The controls show
it functions where it is consumed:

* Under `MTG_HINATA_SUBSET_CREDIT=1` at d3 b10 (150 games, the search, where that credit lives), 2 of 6
  job digests move, with 0 turn changes and units 0.984. At d0 there is no change: the d0 runner never
  applies the credit.
* Under lever B (d0, 1000 games): +1 turn (1 faster, 2 slower). Both slower games (gi 322: 4 → 5;
  gi 779: 8 → 9) are boards where Hinata really is not castable this turn. For example, gi 322 T2 has
  Ornithopter as the only {U}/{W} source and needs both, while the source count said "castable". So
  lever B releases the lone Crackle as a chip a turn before she lands. That is lever B's own scope
  trade-off (default OFF, the USER's call), now judged on accurate castability.

Byte-identity: with both levers `=0`, against the 3d64d9a6 binary, Hinata (32 jobs, 1500 games: d0
1000, 2HG 400, d3 50, shipped 50) gave 0 digest mismatches. With both levers ON, against 3d64d9a6,
burn / dragonstorm / stompy (d0 100 + d3 b10 30 each) gave 0 mismatches. Unit suite: 932 / 932.'''
s = s.replace('RECOVERY_TABLE', rec).replace('HELDOUT_TABLE', held)
open(R + 'doc_stall_final.md', 'w').write(s)
print('ok')
