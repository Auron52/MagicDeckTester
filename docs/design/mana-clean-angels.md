# Angels mana-clean gate: pass 1 findings and fixes (2026-11-03)

Gate run `pass1-angels` (`logs/mana_clean/pass1-angels/report.md`): 100,000 games, 2,305 ISSUE flags --
APPLY-DROP 1,906 (123 signatures), ORACLE-GAP 352 (105), DOMINATED-CREATURE 32 (14), ENUM-GAP 15 (13).
The gate rule and the oracle's classes: `docs/design/mana-clean-gate.md`, `docs/design/mana-completeness-oracle.md`.

## Fixed (branch `wip/mc-angels`)

### 1. The {C} Sol Ring left over beside Giada's Angel-only {W} was booked Angel-only (engine)

**Flags:** APPLY-DROP x1,794 (every "engine dropped [cast Serra the Benevolent]" signature, top x208), and the bulk
of the ORACLE-GAPs (the `... + pump:Resplendent Angel(x1)` families: the engine silently lost the pump, the oracle
could not reproduce a board it had no reason to reach).

**Mechanism.** `TapForCostSharedOnce`'s `commit_leftover` (ManaPayment.cpp, the SUBTYPE-RESTRICTED block) booked
`min(subtype units tapped, leftover)` of a creature payment's leftover into the restricted reserve -- the
"conservative" attribution. Seed 30010687 T3: a second Giada's {1}{W} paid by Giada #7's Angel-only {W} + Sol Ring's
{C}{C}; the {C} left over went into the Angel-only reserve (a unit Giada cannot even make), so Serra the Benevolent
(a planeswalker) had Remote Farm {W}{W} + Plains {W} = 3 of 4. `MTG_PAY_ROLLBACK` cannot rescue it (it declines
with restricted mana floating).

**Fix (`MTG_SUBTYPE_LEFTOVER_EXACT`, DEFAULT ON; =0 the legacy booking).** Which units a payment spent is the
player's choice, and inside an eligible spell's payment a restricted unit can pay every generic pip (less {S}) and
every pip of a colour it makes. By the exchange argument the fewest restricted units that MUST be left over is
`sub_units - those pips`; booking only that many leaves general units where the legacy booking left restricted
ones, each paying everything the restricted one could -- a sound dominance, no preference. The colour set is the
intersection over the tapped subtype sources (fewer coverable pips -> more booked restricted, the safe direction),
and booked units come from those colours first. Any-combination sources (Smokebraider) keep their demand-led rule.

### 2. The line-float rung held too few generic pips (engine)

**Flags:** APPLY-DROP x112 ("engine dropped [cast Lightstall Inquisitor]"), and most DOMINATED-CREATURE flags of
the Sol Ring-paid-by-Remote-Farm shape.

**Mechanism.** `LineFloatTapFirstPlan` (MTG_LINE_FLOAT_TAP_FIRST) held `owed_spend` generic pips off the reserve
drain -- correct only when the drain reaches the owed units LAST. Seed 30001531 T2: Remote Farm paid Sol Ring and
floated {W}; Lightning Greaves' {2} held one pip, the drain spent the {W} on the other (it had run dry before the
second), Sol Ring paid the held pip, and Lightstall Inquisitor's {W} had only a {C}.

**Fix (`MTG_LINE_FLOAT_HOLD_EXACT`, DEFAULT ON; =0 the count alone).** The drain the payer will run (the shared
`SpendFloatingTowardCost`, on the reserve after this payment's own pips) is replayed and the hold grows until every
owed unit stays, capped at what the spare sources can pay. The old count is kept wherever it already kept the owed
units, so the rung changes only where it failed its stated purpose.

### 3. The oracle never offered a multi-unit extra tap (oracle, I1)

**Flags:** the remaining ORACLE-GAPs ("Youthful Valkyrie + Lightstall Inquisitor", "Giada + Giada": the engine paid
a {1}{W} off Remote Farm's {W}{W} AND Sol Ring's {C}{C}, floating {W}{C}).

**Fix (`MTG_ORACLE_SURPLUS_MULTI`, DEFAULT ON).** `PayEnum::ExtendSurplusNow` (ManaOracle.cpp) also tries a
multi-unit face as the one extra tap, in the audit's table only. The refine (`whole_float`) keeps the one-unit rule,
so play does not move.

## Validation

* Unit: `test_mana_oracle.cpp` -- "a {C} left over beside Giada's Angel-only {W} is Sol Ring's", "Angels s30010687
  T3: Serra ... and a second Giada both pay" (both orders), "Angels s30001531 T2: Lightning Greaves' {2} off Sol
  Ring keeps Remote Farm's floating {W}". Each fails with its own flag at 0. Full suite 2,432 / 2,432.
* Scenario `test/scenarios/angels_giada_serra_leftover_general.json` (the x208 board; T4 on both bookings -- the
  search finds another T4 line -- so it guards the board, the unit tests pin the mechanism).
* Fresh slice, gi 100,000-104,999 (seed 30,000,000 + gi): fixes 1+2 -> 16 ISSUE flags (APPLY-DROP 7 in one
  position, ORACLE-GAP 4, DOMINATED-CREATURE 4, ENUM-GAP 1) against a pass-1 rate of ~115 per 5,000.
* Held-out A/B, Angels 3,000 games (seed 53,000,000 + gi), origin vs fixes 1+2: 2 game digests changed, 0 slower,
  0 faster; units 3,337,277 -> 3,338,714.

## Remaining (not fixed here)

1. **Float drained onto a coloured pip a restricted source could pay** (APPLY-DROP, 1 position per 5,000 games):
   {Seraph Sanctuary, Serra, Lightstall, Lightstall} off two Remote Farms + Giada. The second Lightstall's {W} takes
   the floating {W} (the general float drains before any tap), Giada stays up, and Serra is a {W} short; paying
   that Lightstall with Giada's Angel-only {W} pays the line. The rung in fix 2 covers generic pips only, and the
   restricted-first rank never sees a pip the drain already paid. Not a pure dominance (tapping Giada costs her
   attack), so the shape is line-aware: hold a coloured pip off the drain when the line owes that colour later and
   an eligible restricted source can pay it (cf. `RestrictedMayHoldForAttack`).
2. **{C} left floating where a {W} could be** (DOMINATED-CREATURE, the `{land=Plains + cast:Serra}` shape): Serra's
   {2}{W}{W} off Plains + Remote Farm + Sol Ring spends a {W} on a generic pip and floats Sol Ring's second {C}; the
   continuation's Lightstall then taps Giada. Floating the {W} instead is a dominance only where nothing owes a {C}
   pip; the greedy assigns units in tap order, so the fix is a post-payment re-attribution of the leftover's colour.
3. **ENUM-GAP: Giada hasted by a 0-cost Lightning Greaves equip** (T2, {Sol Ring, Greaves, Giada, Lightstall,
   equip}): the flat mana gate never credits a creature mana source cast this turn that the plan's own equip
   hastes. `MTG_HASTE_DORK_CAST_CREDIT` covers keyword and lord haste only; the equip case also needs the order
   (cast Greaves and Giada, equip, then the Angel the restricted {W} pays).
