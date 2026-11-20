# Merfolk Turns: the first complete mana-clean gate (2026-11-20)

Gate `claude-queue-20261119i` (5,000 games, gi 0-4999, seed 30000000 + gi, src `8deb2c9336d1`, PGO,
`MTG_MANA_ORACLE=2`; report `logs/mana_clean/claude-queue-20261119i/report.md` in the primary checkout). Merfolk Turns
counts: APPLY-DROP 6,952, ORACLE-GAP 7,903, ENUM-GAP 937, APPLY-DROP-ORDER 161; oracle-off play byte-identical.
Branch `wip/merfolk-gate`. Repros were traced with the gate's own binary on the gate commit's deck files (the keep
table landed after the gate's commit, so the current tree plays different hands on the same seeds).

## 1. APPLY-DROP: Three Tree City's {C} spent on a generic pip (PLAY defect)

Mechanism (s30002188 T5, traced; s30000073 T7 has the same shape). Lands Island, Island, Three Tree City, six Merfolk;
plan {Sol Ring, Stonybrook Banneret, Wanderwine Prophets, Greaves equip}. The engine paid Sol Ring's {1} with City's
plain {C} (engine-ended board: City tapped, Island #11 untapped, `{C}` floating, Prophets DROPPED). Prophets then met
Island + Sol Ring's {C}{C}. The line pays as Island for the Ring, then the Ring's {C}{C} into City for six {U}. No
line-aware rung reached it: the ramp route is for ramp filters, the any-colour route for any-colour filters, the
line-keep rung only for reducer lines (and it did not credit the rock being cast, which is City's feed).

Fix: `MTG_SCALED_LAND_ROUTE` (DEFAULT ON), `ScaledLandRouteHold` in `src/ai/ManaPayment.cpp`, the any-colour route's
hold-out shape. While a scaled land of ours is untapped and the line still owes mana, the payment is tried on a
scratch board; only when the trial strands the rest of the line (hold net of this payment and of the line's reducer
credit, judged by the complete backtracker with an untapped rock being cast credited) while the board pays this bill
and the rest together, is one of the trial's sources held out, the first that keeps the rest payable. Shared payer,
so executor, rollout and payability walks move together. Unit: `Sol Ring's {1} keeps Three Tree City for Wanderwine
Prophets (MTG_SCALED_LAND_ROUTE)`.

## 2. ORACLE-GAP: "1/1 Merfolk Token only in oracle" (PLAY defect, same root)

The oracle is right. Emperor Mihail II (cards.json: "Whenever you cast a Merfolk spell, you may pay {1}. If you do,
create a 1/1 blue Merfolk creature token.") is a plan axis (`MihailPayChoices`); on the pay variant the engine's
Prophets payment had already tapped every source, so `TapForCostDirect` for the {1} failed and no token was made.
s30001289 T5 (traced): Prophets' {3}{U}{U} (Banneret's discount) off four Islands + City's {C}, exactly, nothing
floating. City fed by two Islands makes {U}{U}{U}{U} (four Merfolk); with one more Island that pays the bill and gives
an Island back for Mihail's {1}. The backtracker tries City's plain {C} first and keeps the first covering set, and
`MTG_PAY_REFINE` -- whose dominance test is exactly "a subset of these taps, at least the engine's float" -- never
looked, because the payment neither cost life/energy nor floated a surplus.

Fix: `MTG_PAY_REFINE_SCALED` (DEFAULT ON): the refine also runs after an exact payment of four or more taps that tapped
a scaled land whose fed mode makes at least {feeder} + 2 (`PayRefineAfter`). Sound dominance (the refine adopts only a
strict subset of the engine's own taps), no new decision. Unit: `Three Tree City's fed mode refines an exact payment
that tapped its plain {C}`.

## 3. ENUM-GAP: {Seahunter + Rishadan Dockhand} kept off the menu

s30000762 T4: Islands x2, City, three Merfolk; hand Seahunter, Dockhand, Island x2. Dockhand first makes four Merfolk
and two Islands into City pay Seahunter. NOT FIXED (a cast-order question for the USER; see section 5).

Mechanism (traced with `MTG_DBG_MULTI=4` on a unit-test board, the Island already played). The plan never reaches a
per-subset gate: EnumeratePlans' odometer prunes on a scalar mana bound, `[dbgmulti-bound] t4 bound=4 pool=4
cands=[Seahunter(4), Rishadan Dockhand(1), Tidal Warrior(0)]`, and {Seahunter, Dockhand} = 5 > 4. The bound already
carries a same-turn GROWTH addend for scaled DORKS (`growth_bound`, Priest of Titania) but none for a scaled LAND, so
City's yield is priced at its current three Merfolk. The per-subset rescue (`SubsetPayableWithFilters`) would admit it:
its `scaled_land_growth` order puts Dockhand first, and the same two payments made by hand on that board both pay.

Why it is not fixed here: admitting the subset (a land-growth addend on the bound -- an upper bound, so it can only
prune less) moves the hole from the menu to the APPLY. Both apply worlds order the casts by `CastOrderLess`: Seahunter
and Dockhand tie (both creature rank 10, no cheapest-first tie-break outside rituals), so plan order stands, Seahunter
is paid first (two Islands into City for three {U} plus the third Island) and Dockhand finds nothing: an APPLY-DROP.
The payable-order decider (`PayableCastOrderCoreImpl`) re-orders only producers, reveal casts and domain wideners. The
complete fix is the bound addend plus EITHER a decider domain for "a creature that grows an untapped scaled land"
(walk the growers first when the given order does not pay) OR a Merfolk Turns cast-order rule (cheaper City-growing
Merfolk first). The second is a deck cast-order policy, which is the USER's call; the first is the established
"only when the given order does not pay" shape but changes the realised order of real games.

## 4. Measurements

### 4.1 The gate's repro games (oracle level 2, gate deck files, new binary, both levers on vs both off)

| seed | turn | class (off -> on) |
|---|---|---|
| 30002188 | T5 | APPLY-DROP 1,206 -> 0 |
| 30000073 | T7 | APPLY-DROP 3,028 -> 0 (DOMINATED-LAND 758 -> 578) |
| 30001289 | T5 | ORACLE-GAP 60 -> 4 |
| 30002784 | T6 | ORACLE-GAP 452 -> 0 |
| 30000557 | T6 | ORACLE-GAP 564 -> 452 |
| 30000762 | T4 | ENUM-GAP 4 -> 4 (not addressed by these levers) |


## 5. Open items

(pending)
