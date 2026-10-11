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

RECOVERY_TABLE

* **2HG gi 254:** either lever alone removes the stall. With the hold disabled, `RESTRICTOR_ORDER_EXACT`
  stops the unrealisable {Irencrag, Crackle} line. Under lever B, `HINATA_CASTABLE_MANA` reads her as
  castable, so the hold stays on and that line never forms.
* **d0 gi 195:** `RESTRICTOR_ORDER_EXACT` recovers it (8 → 6, five failed casts → 0).
* **d0 gi 865, 1773:** the failed Crackle cast is gone (Irencrag now funds the Crackle it was cast
  for). The games stay unwon under lever B and with the hold off; that slowness is lever B's own chip
  mechanism (above). With the shipped hold, all arms win on T6.

### Held-out (seeds 8300001+, one pooled batch, paired by gi, unwon = 9)

HELDOUT_TABLE
