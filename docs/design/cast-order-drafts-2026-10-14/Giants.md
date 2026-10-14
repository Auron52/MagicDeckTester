# Giants: cast-order draft (agent proposal, 2026-10-14; for USER review)

## 1. Status: **(D) generic tiers only**

- `GiantsProvider` (src/ai/DecisionProviders.h:373-402) overrides only `CleanupDiscardCandidates`,
  `CleanupDiscardShedStable` and `TutorSearchWidth`. It has no `CastOrderRank`, no `MainPhaseOverride`
  and no `ClassifiesMainPhases`, so the order is `GenericProvider::CastOrderRank`
  (DecisionProviders.cpp:3013).
- No order flag exists. The only Giants flags are `MTG_GIANTS_BUCKET_DISCARD`, `_ROCK_AFTER_LANDS` and
  `_TUTOR_WIDTH`. `cast-order-rankings.md` has no Giants section, and `analysis-Giants.md` has no
  cast-order ruling. **No USER order exists, so none is OFF.**
- The USER's views on the cards (giants-discard-policy-proposal.md, USER-revised 2026-10-09: "Flinger
  higher ... very good for throwing something like a behemoth", Sovereign overrated, Titan "12 damage +
  firebreathing") rank cards for discard. They do not change cast order. Flinging a Behemoth reads its
  power live at combat, and that power is the same whatever order the Giants were cast in.

**Rank 8 "(unclassified)" is the SUBTYPE COST-REDUCER tier.** `if (!def.params.reduces_spell_subtype.empty())
{ return 8; }` (DecisionProviders.cpp:3157) is the Goblin Warchief tier ("cast it just BEFORE other
creatures"). Stinkdrinker Daredevil lands there through `reduces_spell_subtype: "Giant"`. The rank is
correct and it matters: `SameTurnReducerGenericCredit` (TurnSolver.cpp:6424) assumes the reducer resolves
first. The label is missing only because the report's `tier_name` switch (main.cpp:7885-7907) has no
`case 8` (or 9). Goblins' Warchief shows the same "(unclassified)" (cast-order-rankings.md:440).

## 2. What plays today

| pos | card(s) | main | rule |
|---|---|---|---|
| land | land drop | m1 | no cantrip in the deck, so it goes first (depth 0); at depth > 0 the land drop is folded into the plan |
| 5 | Sol Ring, Fire Diamond **(TIE)** | m1 | rock tier "online for the rest of the line". This is false for Fire Diamond, which enters tapped |
| 8 | Stinkdrinker Daredevil | m2 | subtype reducer tier (label missing) |
| 10 | Tectonic Giant, Giant Harbinger, Surtland Flinger, Inferno Titan, Sunrise Sovereign, Borderland Behemoth, Hamletback Goliath **(7-way TIE)** | m2/m1/m2/m1/m1/m2/m1 | "creature". Ties fall to plan (hand) order, so nobody chose it |
| 20 | Lightning Bolt, Lightning Greaves, Pyroclasm **(TIE)** | both/m1/m1 | other noncreature |

The **main column is advisory in Giants play**. Two things would make it count, and neither is on:
- `ClassifiesMainPhases` is not overridden, so `MainPhaseFilterActive` returns false
  (TurnSolver.cpp:20396).
- `MTG_ORDER_M1_FIRST` is off, so the phase never breaks a rank tie (ManaPayment.cpp:2664-2681).

As a result, every cast is offered in both mains.

## 3. Proposed order

| pos | card(s) | main | range | why (card text, then the deck's plan) |
|---|---|---|---|---|
| land | land drop | m1 | - | The deck has no draw, so nothing wants to come before the land. At depth > 0 the search still owns deferring it past a Tectonic mode-B attack, which can expose a Mountain. |
| 1 | Sol Ring | m1 | - | "{T}: Add {C}{C}". It taps the turn it lands, so it funds everything after it. |
| 2 | Pyroclasm | both | - | "2 damage to each creature" hits our side too. Cast it before this main's own bodies: two copies kill a fresh Daredevil (1/3) or a 3/4 Harbinger/Tectonic. It does nothing against the goldfish clock. |
| 3 | Stinkdrinker Daredevil (x2: identical, tie is moot) | m1 | - | "Giant spells you cast cost {2} less". It goes before every Giant (principle 2), the order the same-turn reducer credit already assumes. |
| 4 | Hamletback Goliath | m1 | - | "Whenever another creature enters, put X +1/+1 counters, X = that creature's power". A watcher goes before everything it watches (principle 3). Its counters also feed Flinger's doubled fling. |
| 5 | Sunrise Sovereign | m1 | - | "Other Giants get +2/+2". It enters worth 5 counters to a Goliath, and every Giant cast after it enters 2 power bigger, which is 2 more counters each. |
| 6 | Inferno Titan, Giant Harbinger, Tectonic Giant, Surtland Flinger **(tie)** | m2 by default; Harbinger m1 | - | Provably order-free. None of them reads another's entry, and none has power that depends on the others. The ETB (Titan's 3, Harbinger's fetch to top) and attack triggers do not care about sequence. Harbinger stays m1 so a mode-B Tectonic attack can exile the Giant it fetched. |
| 7 | Borderland Behemoth | m2 by default | - | "+4/+4 for each other Giant you control". As the last Giant it enters at its largest, so a Goliath counts 4 + 4 x (Giants already down). |
| 8 | Lightning Greaves, Lightning Bolt **(tie)** | m1 / both | - | Provably order-free. Neither makes mana this turn, the deck has no cast or artifact watcher, and equips happen after every cast anyway (cast-order-rankings.md "Equip timing"). |
| 9 | Fire Diamond | both | - | "This artifact enters tapped", so it makes no mana this turn. Last, so an overcommitted line drops the rock and not a Giant. |

"m2 by default" for Giant bodies with no haste means m1 when Greaves can haste one, or when a Goliath or
Behemoth already on board attacks this turn (casting a Giant pre-combat pumps it). Ranges: none. With no
draws in the deck there is no affordability trade-off, except the one Daredevil-vs-Goliath edge in Q2.

## 4. What changes and why it matters

1. **Goliath first: this line is unreachable today, at any budget (probed).** Turn 5: six Mountains, two
   Daredevils in play, an unattached Greaves, hand = {Borderland Behemoth, Hamletback Goliath}.
   - Goliath then Behemoth: Goliath takes 8 counters (Behemoth enters as 4 + 4 x 1). Greaves goes on
     Goliath, which swings for 14, plus 2 from the Daredevils = 16 = **lethal on T5**.
   - With Behemoth listed first in hand, today's tie casts Behemoth first. Goliath gets 0 counters and
     the opponent is left on 8. It is not found at d3 b20 or at **d8 unlimited**.
   - The cards are the same; only the hand order differs. Probes: `build/scratch/castorder/probe_Giants/haste_goliath_{second,first}.json`
     and `haste_goliath_second_d8b0.json`.
   - Without haste the search sometimes hides the loss by moving the second cast to m2
     (`goliath_second`). That cannot help when the counters must be on before combat.
2. **Sovereign before the other Giants, after Goliath.** Two Daredevils, 7 mana, Goliath + Sovereign +
   Titan: Goliath first takes 5 + 8 = **13 counters** (21 power with Sovereign, 42 if Flinger throws it).
   A hand-ordered Titan, Sovereign, Goliath gives it **0**.
3. **Behemoth last.** Goliath + Titan + Behemoth (8 mana with two Daredevils): Behemoth after Titan
   enters as 12, so Goliath takes 18 counters. Behemoth before Titan gives 14. That is 4 counters
   decided by hand order.
4. **Pyroclasm before our creatures.** Weakly dominant, and it only matters on a double-Pyroclasm main
   (it kills a fresh Daredevil if cast after it). Low stakes.
5. **Fire Diamond leaves the rock tier: a label fix, no play change today.** Probe `rocks_fd_first`: with
   Fire Diamond first in hand, T1 still went Mountain, Sol Ring, Fire Diamond. `MTG_PAYABLE_ORDER`
   (ManaPayment.cpp:3610, default ON) reorders when the given order cannot pay. The same mechanism
   explains why items 1-3 are not rescued: both orders pay, and only the value differs.
6. **A recorded claim is wrong.** giants-discard-policy-proposal.md says Goliath's "counters depend on
   cast ORDER, which is the search's". They do not:
   - the creature tier ties and stays stable (`CastOrderLessRanked` breaks ties cheapest-first only
     between rituals);
   - `MTG_SEARCH_ORDER` is off;
   - payable-order reorders only for payment.

   Hand order decides.

## 5. Questions for the USER

1. **Creature order Goliath > Sovereign > {Titan, Harbinger, Tectonic, Flinger} > Behemoth, after the
   Daredevils: approve?** Default: **yes**. Every step is a dominance argument, and none can lose a line.
2. **Daredevil before Goliath even when both are affordable at full price?** That gives up 1 counter (a
   Daredevil cast after Goliath is worth 1) to save 2 mana. Default: **yes**. No range; the edge needs
   10+ spare mana (8 with one Daredevil already down).
3. **Pyroclasm before our own creatures?** Default: **yes**.
4. **Ship as `MTG_GIANTS_ORDER` default ON (=0 restores generic), confirmed on Giants' suite cases?**
   Default: **yes**.
5. **Opt Giants into the m1/m2 filter (`ClassifiesMainPhases`)?** Default: **no**. Keep the column
   advisory: two classifier misses (§6) would mislabel Giants, and the filter needs its own measurement.

## 6. Generic-order findings

- **Report labels:** `tier_name` (main.cpp:7885) lacks `case 8` (subtype cost reducer, both the
  Warchief and Ragemonger forms) and `case 9` (dork-growth / enter-watcher). Both print "(unclassified)".
- **Enter-watchers tie with the creatures they watch.** `any_creature_enters_self_counters_power`
  (Hamletback Goliath) should join the rank-9 watcher class that `MTG_WATCHER_ORDER` (default OFF) opens
  for `dragon_ping_on_enter` / `haste_on_flying_enter` (DecisionProviders.cpp:3180). Same principle:
  the watcher goes before the triggerer.
- **An enters-tapped rock gets rank 5** ("online for the rest of the line"), although it is excluded
  from same-turn ramp everywhere else (TurnSolver.cpp:7160, 24668). It should take the plain noncreature
  tier.
- **A symmetric creature sweeper (`damage_all_creatures`) ranks 20, after our own creatures.** It should
  go before them.
- **Phase classifier** (advisory here, but it bites any deck that opts in):
  - `BoardHasScalingAttacker` (TurnSolver.cpp:20336) misses `static_self_pump_per_other_subtype`
    (Behemoth) and `any_creature_enters_self_counters_power` (Goliath). A pre-combat cast pumps either.
  - A `reduces_spell_subtype` creature with no haste falls to Main2 (the VanillaCreature branch) while
    the Giants it discounts are Main1. Under the filter that would delete every discounted m1 line.
  - The `firebreathing_cost` Main1 pull fires for a summoning-sick caster (Titan), which feeds no combat.

## 7. Implementation notes

`GiantsProvider::CastOrderRank`, gated on `EnvOn("MTG_GIANTS_ORDER", true)` with a heurarm slot; off
falls through to `DeckProvider::CastOrderRank`. Rank by params, never by names:

| rank | param test | card |
|---|---|---|
| 5 | `mana_rock && !enters_tapped && !IsCreature()` (generic, unchanged) | Sol Ring |
| 6 | `damage_all_creatures > 0` | Pyroclasm |
| 8 | `!reduces_spell_subtype.empty()` (generic, unchanged) | Daredevil |
| 9 | `any_creature_enters_self_counters_power` | Goliath |
| 10 | creature && `tmpl == LordEffect` && `power_bonus > 0` | Sovereign |
| 11 | other creature | Titan, Harbinger, Tectonic, Flinger |
| 12 | `!static_self_pump_per_other_subtype.empty()` | Behemoth |
| 20 | other noncreature (generic) | Greaves, Bolt |
| 21 | `mana_rock && enters_tapped` | Fire Diamond |

Notes:
- Add `CastOrderTierName` for 6/9/10/11/12/21 so the report labels them.
- Every consumer compares ranks relatively (ManaPayment.cpp:2705, TurnSolver.cpp:6283, 35621, 48211,
  53746), so renumbering the creature band is safe.
- **No engine limitation blocks this.** Every position is unconditional (weakly dominant on any board),
  so a scalar rank expresses it. The one conditional (Q2) would need a range, and it is not proposed.
- Ages the value leaf and keep table only through play, like any order change: measure with the
  sidecars in place.
