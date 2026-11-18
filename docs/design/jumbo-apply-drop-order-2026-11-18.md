# Jumbo Cactuar: unlock-order domain and trailing equips in the walk (`MTG_PO_UNLOCK_ORDER`, `MTG_SEQ_TRAILING_EQUIP`), 2026-11-18

Status: IN PROGRESS (measurements below are filled in as they land).

Follows `docs/design/jumbo-tooth-and-nail-enum-2026-11-18.md` ("Not fixed" items 1 and 2).

## Family 1: APPLY-DROP-ORDER (gate 291; slice gi 115000-116999: 92 flags / 32 decisions / 24 games)

### Mechanism (verified)

Both apply worlds realise a plan's casts in the order `ApplyPayableCastOrder` returns: the given order (rank sort,
the deck's arrangement, the range ladder) unless the payable-order decider `PayableCastOrderCoreImpl`
(ManaPayment.cpp) finds it unpayable and finds a payable permutation. The decider

* admits an order only inside a producer domain (Treasure makers, rituals, domain wideners, land Auras, sac fodder);
* walks an aggregate pool (`FirstUnpayablePosImpl`) that knows nothing about a haste-unlock equip; and
* fails fast on an order-free bound (`bound.CanPay(total)`) that credits no unlock either.

A Jumbo line whose middle cast is paid by a mana creature that a same-line haste unlocks (Lightning Greaves / Chariot
of Victory equipped onto a fresh Arbor Elf / Voyaging Satyr, fired by `TurnSolver::ApplyManaUnlockEquips` the moment
both pieces are on the battlefield) is therefore either outside every domain, or inside the land-Aura domain and
refused by the bound or the walk. The decider returns false, the rank order stands, and the cast the unlock should
have paid is dropped. Examples (slice, `MTG_MANA_ORACLE=2`):

* s30115214 T3: board Forest#10 + Wild Growth, Lightning Greaves; plan {GSZ(X=1) -> Arbor Elf, Wild Growth -> #10,
  equip Greaves -> the fetched Elf}. The rank order casts Wild Growth first and GSZ is one short; GSZ first pays off
  the Forest's {G}{G}, the Elf enters, Greaves hastes it, and its re-tap pays Wild Growth.
* s30115140 T3: {Overgrowth -> #10, Arbor Elf, equip Chariot}: Overgrowth first takes all three lands and the Elf is
  dropped; Elf first, Chariot equip, the hasted Elf re-taps the Wild Growth Forest for Overgrowth.

The enumeration's sequential walk (`SeqWalkFixedOrder`) already models the unlock (it fires the same
`ApplyManaUnlockEquips` at the apply's points; `MTG_SEQ_UNLOCK_EQUIP`), which is why the plan reached the menu at all.
The hypothesis in the earlier doc (an unlock-piece producer domain plus `ApplyManaUnlockEquips` semantics in the
order walk) is right in substance; the change below reuses the enumeration's own walk rather than teaching the
aggregate walk a second model of the unlock.

### Change: `MTG_PO_UNLOCK_ORDER` (heurarm `PO_UNLOCK_ORDER`, default ON)

The UNLOCK-ORDER domain: a line holding a same-line haste for a mana creature -- a plan unlock equip
(`SeqWalkUnlockEquip`), or an enters-haste watcher cast (Surrak and Goreclaw) beside a mana-dork cast -- with only hand
casts in its order (no Vial put, no reveal surcharge). In that domain orders are priced by `SeqWalkFixedOrder` with the
plan's unlock (and trailing) equips in its `sel`: the given order when it pays, else every permutation nearest-first
(Kendall distance to the given order; all permutations for n <= 7, single moves beyond), with the walk's prefix rule
skipping every permutation that shares a failed prefix.

* Apply site: `TurnSolver::UnlockPayableOrder`, called by `PayableCastOrderCore` only when the decider found nothing
  (so every order the decider settles is byte-identical).
* Enumeration site: `SubsetPayableSequentialTail`, after the rank and decider orders failed, so the menu admits the
  subsets the apply then realises.

A realisation of the plan the search chose: its casts and equips are the plan's; only the order moves, and only when
the given order does not pay. The USER's Jumbo cast-order rules (`JumboCactuarProvider::ArrangeCastOrder`) stand
whenever their order pays.

## Family 2: `ef-walk-fail` Surrak (s30119376 T5 x19)

### Mechanism (verified -- NOT an order problem)

`MTG_DBG_SEQ=1 MTG_DBG_MULTI=5` on s30119376: the subset {Tooth and Nail, Surrak, Satyr, Satyr} IS walked and
offered (Surrak first by `JumboSurrakEarlyIfPaid`; both Satyrs enter hasted and re-tap Forest#18). The flagged subset
adds `equip Chariot of Victory` ({1}, onto a creature whose mana it does not unlock), and for it the walk never
starts: `SubsetPayableSequentialImpl` refuses any equip that is neither an unlock nor a free attach as an "unmodelled
kind". Both apply worlds pay such an equip in the trailing equip pass, after the casts, off the mana they left (here
the {1} left after Tooth and Nail).

### Change: `MTG_SEQ_TRAILING_EQUIP` (heurarm `SEQ_TRAILING_EQUIP`, default ON)

`SeqWalkTrailingEquip`: a paid equip (no X) that is not an unlock is skipped by the three lockstep loops
(`SubsetHasUnmodelledSeqKind`, `SubsetPayableSequentialImpl`, `OrderPaysSequentiallyImpl`) and paid at the end of
`SeqWalkFixedOrder` at `EquipActionCostNow`, as the trailing pass pays it. A planned unlock that never fired because
its host no longer needed it (hasted by Surrak, or already tapped) joins the same end payment instead of refusing the
subset; one still locked is refused as before. Moving a haste Equipment off a fresh creature whose {T} it enables
stays refused (the free-attach carve-out). Such subsets are kept out of the cast-keyed memos.

Reach: decks with a PAID equip -- Jumbo (Chariot), Boars (Loxodon Warhammer), Colossus Hammer, Kitty Equipment,
Martyr Lifegain (Jitte). `MTG_PO_UNLOCK_ORDER` reaches decks with a haste Equipment and a mana creature: Jumbo,
Angels (Giada), Boars, Bruna, Five Colour.

## Measurement

(pending)
