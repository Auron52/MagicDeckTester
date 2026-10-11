## The d0-runner stall (2HG gi 254; d0 gi 195, 865, 1773) — root cause and fix

These four games stalled only once the lone Crackle was offered (lever B, or the hold disabled
entirely with `MTG_NO_HINATA_HOLD_CRACKLE=1`). The hold had been masking two modelling defects. The
executor's cast traces (`MTG_USER_ORDER_TRACE=1`) show both.

1. **Irencrag Feat's own cost "paid" by its own float (2HG gi 254, d0 gi 195).** The d0 runner commits
   {Irencrag Feat, Crackle} turn after turn. On T3–T6 of gi 254 the trace reads Irencrag `paid=0`, then
   Crackle `paid=0`, so nothing is cast. The board is Sol Ring + Izzet Boilerworks + Forbidden Orchard:
   five mana, but only two can be red. Both subset walks credit a ritual's float to the whole subset,
   its own cost included, in the flat test and in the colour-exact gate (`PoolCredit(pool, eff)`). The
   single-accelerant check in `SequencedRitualCredit` tests the flat pool, where a bounce land is two
   wild. So {R}{R}{R} "paid" from Irencrag's own seven red.
2. **The restrictor counted in rank order, the subset payable only in another order (d0 gi 865,
   1773).** On four mana the runner commits {Irencrag, Ornithopter of Paradise, Crackle}. The legality
   test counts the casts after Irencrag by `CastOrderRank`. Ornithopter ranks before him, so it counts
   one follower and calls the plan legal. But Ornithopter can only be paid from Irencrag's float. The
   executor's payable-order fallback therefore casts Irencrag first, the Ornithopter takes his one
   remaining cast, and the Crackle is refused (`paid=0`).

Neither is a USER-owned decision. They are payability and legality modelling, not cast order or policy.

### `MTG_RESTRICTOR_ORDER_EXACT` (heurarm `RESTRICTOR_ORDER_EXACT`), DEFAULT ON (=0 → the rank-only count)

`RestrictorPrePayable` (TurnSolver.cpp) runs inside both walks' Irencrag loop, just before
`after > max_casts_after`. It asks whether the casts ranked at or before the restrictor, plus the
restrictor itself, can be paid **without the restrictor's own float**. If not, every payable order
casts the restrictor ahead of one of them, so one more cast follows it than the rank count says
(`++after`). It never rejects a payable line, because both sides err generous:

* **Bill:** the subset's bill with every subset-level credit already folded in, minus the cost of
  every action outside the pre-set.
* **Supply:** the board plus the mana of every pre-ranked or pre-pass producer (other rituals, rocks,
  sac-for-mana), regardless of order.
* **Colours:** the walks' own colour-exact gate (`ColorFeasibility`), run on the pre-set alone. It
  knows a bounce land makes one of each colour.

It is scoped like the Irencrag waste gate: not for decks that search their cast order (Dragonstorm,
`OrderingSearchEnabled`). Anything it cannot model (hybrid, twobrid or Phyrexian pips) is left
unchanged.

### `MTG_HINATA_CASTABLE_MANA` (heurarm `HINATA_CASTABLE_MANA`), DEFAULT ON (=0 → the source count)

`HinataCastableFromHand` asked whether the SOURCE COUNT reached her mana value. That under-counts every
multi-mana source and ignores colours: gi 254's three sources make five mana and pay {1}{U}{R}{W}, yet
read "not castable". It now asks whether `AvailableManaPool` (the accounting pool every payment gate
uses) pays her printed cost with the pool's own colour-aware `CanPay`. The check feeds three things:
Reality Spasm's emission under the same-subset credit, Crackle's X sizing when she is castable this
turn, and lever B's scope gate.

### Recovery (single games, turns / failed casts in the executor trace)

| game | condition | 3d64d9a6 | both levers on | RESTRICTOR off | CASTABLE_MANA off |
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
itself defect, not touched here; see "Still open".)

* **2HG gi 254:** either lever alone removes the stall. With the hold disabled, `RESTRICTOR_ORDER_EXACT`
  stops the unrealisable {Irencrag, Crackle} line. Under lever B, `HINATA_CASTABLE_MANA` reads her as
  castable, so the hold stays on and that line never forms.
* **d0 gi 195:** `RESTRICTOR_ORDER_EXACT` recovers it (8 → 6, five failed casts → 0).
* **d0 gi 865, 1773:** the failed Crackle cast is gone (Irencrag now funds the Crackle it was cast
  for). The games stay unwon under lever B and with the hold off; that slowness is lever B's own chip
  mechanism (above). With the shipped hold, all arms win on T6.

### Held-out (seeds 8300001+, one pooled batch, paired by gi, unwon = 9)

One pooled batch (`mtg --batch`, 6 threads, `lowprio.sh`), arms paired by gi. Cells: shipped =
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
  has none. Shipped gi 4 (6 → 5) is the same class under search: the off arm commits {Irencrag,
  Crackle} on T3, and both fail at payment in main 1 and again in main 2.
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
burn / dragonstorm / stompy (d0 100 + d3 b10 30 each) gave 0 mismatches. Unit suite: 932 / 932.
