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
Orders are taken over CLASSES of indistinguishable casts (same definition, kind, cost and cost mode --
`UnlockOrderSameCast`), so two Voyaging Satyrs are one order, not two: an identity fold (the walk cannot tell them
apart; Jumbo digests were byte-identical with and without it over 1000 games) that removes ~10% of the search's cost.

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

## Measurement (2026-11-19, worktree `wip/jumbo-order`, Release)

Unit tests: `mtg-test -tc="*Jumbo*"` 20/20. The s30115214 board: `ApplyPayableCastOrder` moves GSZ ahead of Wild
Growth with `PO_UNLOCK_ORDER` on, not off. The s30119376 board: with both levers off no plan holds
{Tooth and Nail, Surrak, Satyr, Satyr} plus an equip; `SEQ_TRAILING_EQUIP` alone offers Chariot onto Surrak (#30);
`PO_UNLOCK_ORDER` alone offers Chariot onto Satyr #54 (an unlock, in an order that fires it) but not onto Surrak.

### Repros (oracle level 2, one pooled batch, arms off / u = unlock only / on = both)

| seed, turn | off | u | on |
|---|---|---|---|
| 30115140 T3 | APPLY-DROP-ORDER 1 | ORACLE-GAP 1 (*) | ORACLE-GAP 1 (*) |
| 30115214 T3 | APPLY-DROP-ORDER 5 | -- | -- |
| 30115251 T3 | APPLY-DROP-ORDER 10 | -- | -- |
| 30115377 T3 | APPLY-DROP-ORDER 6 | -- | -- |
| 30115733 T3 | APPLY-DROP-ORDER 6 | -- | -- |
| 30116038 T4 | APPLY-DROP-ORDER 5 | -- | -- |
| 30116848 T3 | APPLY-DROP-ORDER 1 | -- | -- |
| 30115578 T4 | APPLY-DROP-ORDER 1 | APPLY-DROP-ORDER 1 | APPLY-DROP-ORDER 1 (residual) |
| 30119376 T5 | ENUM-GAP 19 | -- | -- |

(*) Analyzer side: the engine now realises {Overgrowth, Arbor Elf, equip Chariot -> Elf} with the Elf hasted and
re-tapping the Forest ({G} left floating); the oracle has no outcome with the haste re-tap, so it reports ORACLE-GAP.
Residual s30115578 T4 ({Tooth and Nail x2, Chariot, Satyr, equip Chariot}; the oracle pays it in the order
T&N > Satyr > Chariot > equip): unchanged in every arm, a different mechanism, not chased here.

### Slice (gi 115000-116999, both levers 0 vs 1; flags / decisions / games)

| class | levers off | levers on |
|---|---|---|
| ENUM-GAP | 43 / 42 / 26 | 28 / 27 / 18 |
| APPLY-DROP | 7 / 3 / 3 | 2 / 2 / 2 |
| APPLY-DROP-ORDER | 92 / 32 / 24 | 1 / 1 / 1 |
| ORACLE-GAP | 42 / 15 / 11 | 42 / 15 / 12 |
| PHANTOM | 4681 / 851 / 365 | 3452 / 632 / 291 |

### Held-out A/B (one pooled `mtg --batch`, seed = 30000000+gi, game_index = gi, play settings, gi from 650000)

Arms: off, u = `MTG_PO_UNLOCK_ORDER` only, ut = both. Jumbo 5000 pairs (all three arms); Angels, Bruna, Colossus
Hammer, Kitty Equipment, Five Colour 1000 pairs, Martyr Lifegain and Boars 500 pairs (off vs ut). Slower/faster
count games whose win turn moved (a non-win scored 9).

| deck | comparison | faster | slower | net | digest-only | mean turn d | units | CPU |
|---|---|---|---|---|---|---|---|---|
| Jumbo Cactuar | u vs off (unlock lever) | 2 | 0 | -2 | 104 | -0.0004 | x0.994 | **x1.445** |
| Jumbo Cactuar | ut vs u (trailing lever) | 0 | 0 | 0 | 38 | 0 | x1.002 | x0.989 |
| Angels | ut vs off | 0 | 0 | 0 | 0 | 0 | x1.000 | x0.987 |
| Boars | ut vs off | 0 | 0 | 0 | 1 | 0 | x1.000 | x1.028 |
| Bruna | ut vs off | 0 | 0 | 0 | 13 | 0 | x1.004 | x1.016 |
| Colossus Hammer | ut vs off | 0 | 0 | 0 | 0 | 0 | x1.000 | x0.982 |
| Five Colour | ut vs off | 0 | 0 | 0 | 13 | 0 | x1.000 | x1.023 |
| Kitty Equipment | ut vs off | 0 | 0 | 0 | 0 | 0 | x1.000 | x1.004 |
| Martyr Lifegain | ut vs off | 0 | 0 | 0 | 0 | 0 | x1.000 | x0.994 |

Faster Jumbo games: gi 653146 and 653783 (T5 -> T4). No slower game in any deck, so no d8 b0 recovery run was owed.
Every deck's net is <= 0. The class fold is an identity: all 290 jobs had the same digests as the pre-fold binary.

Lever-off identity: `MTG_PO_UNLOCK_ORDER=0 MTG_SEQ_TRAILING_EQUIP=0 bash test/regression.sh --smoke` ALL PASS
(231 passed, 0 play changes vs committed GT). Smoke/regression GT with the levers ON is not accepted yet (Jumbo rows
move: digest-only and two faster games).

### CPU of the unlock-order search (open)

The search's walks are not charged to the budget (units x0.994), so its cost is pure extra CPU: Jumbo x1.445 at the
same play (x1.64 before the class fold). Counted over 1000 Jumbo games: 517k searches, 5.4M walks, 36k orders found
(7%); by order length n = 2..8: 167k / 176k / 101k / 55k / 16k / 2.1k / 50, so n = 5..7 carry most of the walks.
Tried and dropped: an order-free necessary bound (SpwfProvablySupplyShort on a board with every line haste
pre-granted). The generous version was sound (0 violations in verify mode) but caught only 16% of searches (-3% CPU);
a tighter one (only real haste grants) caught 18% and was UNSOUND (40 violations), so neither ships.
Options for the USER: (a) keep as is (x1.45 CPU on Jumbo only; other decks within noise); (b) re-price the walks
against the decision budget (`TurnSolver::DecisionBudget()`, as the equip route does -- inert at unlimited budget,
but it moves budgeted play and needs its own A/B); (c) a resumable prefix-sharing walk (DFS over orders with the
scratch board copied per depth) to cut the per-walk cost.

