# Prevent Damage: cast-order draft (2026-10-14)

Probe fixtures and logs: `build/scratch/castorder/probe_PreventDamage/` (hand-built single-turn boards,
`mtg --scenario`, current Release binary).

## 1. Status

**(A) USER-agreed order, shipped default ON.** Evidence: `MTG_PD_CAST_ORDER` is read as
`EnvOn("MTG_PD_CAST_ORDER", true)` in `PreventDamageProvider::CastOrderRank` (DecisionProviders.cpp:17149-17167),
and the heurarm slot is only overridden inside A/B arms. It was adopted on 2026-10-09 (`114d1651`; GT `487a99cf`).
The ledger section is `docs/design/analysis-Prevent Damage.md` "## Cast order (USER, 2026-10-09)". The
`--cast-order-report` ranks match the USER's table row for row. No card in the list is ORDER-OPAQUE (no
draw, stage, cascade or impulse param), so the rank really is what plays.

**The MAIN-PHASE half is a USER rule that is OFF (status B).** USER 2026-09-29: *"do everything in the second
main with this deck so creatures can attack even if we choose to sacrifice them"*. This became
`MTG_PD_ALL_M2` (commit `06e75449`), a "measurement lever" that defaults OFF (HeuristicArm.h:305,
DecisionProviders.cpp:16454). When it was measured, it lost **+0.37 t/game**. The cause was that every
projected turn ran greedy instead of searched, which is the defect the 2026-09-30 no-greedy purge
removed (`docs/design/no-greedy-in-search-window.md`, "How it surfaced"). With both fixes the loss was
+0.07. The residual was "tutored land in m2" (`MTG_MAIN2_DROP`), which the purge made unconditional in
step 2c. **That measurement exists only in an agent's private memory note. The deck ledger has no
record of it, and it has not been re-measured since all three causes were deleted.** The related
`MTG_PD_SECOND_MAIN` (where sweepers open a searched m2) was flipped OFF by an agent on 09-27
(`ef26b03b`). It is still listed as ledger decision D2, "awaiting the user".

## 2. What plays today

| pos | card(s) | main | rule that put it there |
|---|---|---|---|
| land | the land drop | m1 | first (no cantrip in the list); searched (folded into the plan) at depth > 0 |
| 10 | Tamanoa, Green Sun's Zenith **(TIE)**; Purity (sb) | m1 | USER: the gain engine and GSZ, "in the Tamanoa group" |
| 12 | Vito, Dina **(TIE)**; the sb bodies | m1 | USER: "the other creatures" |
| 13 | Rhox Faithmender; Bilbo (sb) **(TIE)** | m1 | USER: amplifiers after the creatures |
| 15 | Spellshock | m1 | after the creatures and before the spells it triggers on (the generic order put it last, at 30) |
| 20 | Living Wish, Manabarbs, Beseech the Queen **(TIE)** | m1 | other noncreature spells |
| 25 | Rolling Earthquake | m1 | finisher, cast with the whole engine out |
| 26 | Pyrohemia | m1 | finisher; its pings follow every cast anyway |

All casts are m1 because `MTG_PD_SECOND_MAIN` and `MTG_PD_ALL_M2` are both OFF (the report says
`uses_second_main=no`). **A tie is decided by HAND order**, not by the search. The probes confirm this:
swapping the order of two hand cards changes the line. The code comment's "within a rank the search's
plan order stands" is wrong.

## 3. Proposed order (the USER's order with two ties resolved)

| pos | card(s) | main | range | why (card text / plan) |
|---|---|---|---|---|
| land | land drop | m2* | - | Before every cast. No draws in the list, and Beseech reads "MV <= lands you control" at resolution. On a turn with no land in hand where the Wish fetches a land, the post-Wish re-solve plays that land, which is already expressible. |
| 10 | Tamanoa (Purity) | m2* | - | "Whenever a noncreature source you control deals damage, you gain that much life": every payment and trigger after it becomes a gain. |
| **11** | **Green Sun's Zenith -> Tamanoa (X=3)** | m2* | - | USER: *"GSZ should go right after Tamanoa"*. With Tamanoa first, GSZ's own taps and its Spellshock trigger are gains. |
| 12 | Vito, Dina, **GSZ -> Dina (X=2)**: **pair ORDER searched** | m2* | - | Drains go before amplifiers. The right order depends on the board: Dina drains 1 per event, Vito drains the amount. Probes P1 vs P1-Rhox reverse. |
| 13 | Rhox Faithmender (Bilbo) | m2* | - | USER: "doesn't do anything on its own". A drain that is out first drains Rhox's own payment. |
| 15 | Spellshock | m2* | - | "Whenever a player casts a spell ... 2 damage": each later cast becomes a gain event, then a drain. |
| 20 | Living Wish, Manabarbs, Beseech, **GSZ fail-to-find (X=0)** | m2* | - | The tie is safe for Manabarbs (see §4.4). Wish with Beseech in one main is at least 5 mana of tutors (not seen). GSZ X=0 is cast only for its Spellshock trigger, so it belongs after the drains. |
| 25 | Rolling Earthquake | m2* | - | "X damage to each creature ... and each player" is one event. Cast last, so a same-turn Vito or Dina drains it. |
| 26 | Pyrohemia | m2* | - | Its {R} pings follow the casts. Nothing cast after it reads it. |
| - | Dimir House Guard, Shriekmaw, Vexing Shusher, Timeless Witness, Acidic Slime (sb) | - | - | Rank 12 by the creature rule. They are never cast autonomously: `MTG_PD_WISH_USEFUL` offers the Wish only engine creatures and lands, and GSZ and Beseech cannot reach the sideboard. The same holds for Purity and Bilbo, so the 10 and 13 ties never occur in autonomous play. |

*m2 is the USER's 2026-09-29 rule. Today every cast is m1. The rule needs one engine fix first; see §4.3 and Q3.
No ranges: no card here trades its position for affordability.

## 4. What changes and why it matters

1. **GSZ after Tamanoa (rank 11), the USER's literal words.** Probe P2 (T5; Manabarbs and Vito out; six
   basics; Dina in the library; Tamanoa and GSZ in hand). With hand order [GSZ, Tamanoa], the tie casts
   GSZ first. Every tap then happens with no gain engine out, so the line is so bad the search drops a
   card. At d3 it casts only GSZ: opponent 20 after T5, **win T7**. With hand order [Tamanoa, GSZ] it
   casts both: GSZ's 3 Manabarbs taps become 3 gains and 3 Vito drains, opponent 17, **win T6**. Hand
   order cost a full turn.
2. **Vito/Dina order searched.** Probe P1 (Manabarbs and Tamanoa out; five painless lands; both drains
   in hand):
   - Dina then Vito: Vito's 3 taps give 3 events, so Dina drains 3 (opponent **17**).
   - Vito then Dina: Dina's 2 taps give 2 events, so Vito drains 2 (**18**).
   - With Rhox out the result **reverses**: Vito first gives **16** (doubled amounts), Dina first gives
     **17**.

   At d1 the search even leaves the "wrong-order" drain stranded in hand. No static rank is right on
   both boards. Spellshock and painland boards tilt it again (count of events vs amount).
3. **ALL_M2 needs an end-of-main-2 pain sweep before it can work (root cause found).**
   `TapPainSourcesIfUseful` runs only at the END OF MAIN 1 (SpellEffects.h:30193; AIEngine.cpp:3112/6831;
   TurnSolver.cpp:44679). The pain deferral also requires that sweep to come after the payment. So
   under ALL_M2 a same-turn Rhox, Vito or Dina can no longer amplify the sweep. Probe: the shipped
   fixture `pd_rhox_keeps_citadel_for_sweep` with `MTG_PD_ALL_M2=1` ends **opponent 9, us 16**,
   against **6 / 19** shipped. Proposed amendment, keeping the USER's intent: run the pain sweep (and
   the deferral's "the sweep follows" gate) at the end of the LAST main that casts. Then re-measure
   ALL_M2.
4. **The Manabarbs tie is moot today, but only because of a payment-timing gap.** `PdBatchTimingDominated`
   (TurnSolver.cpp:37531) refuses to decline the whole-plan prepay when the plan casts Manabarbs, so
   every land is tapped before Manabarbs resolves. Probe P3 (Tamanoa and Vito out; Living Wish and
   Manabarbs, six basics): opponent 20 in both orders. Paying sequentially with Manabarbs first would
   have made the Wish's 2 taps 2 drains. This is a payment question, not an order question (Q4).
5. GSZ also ranks by what it fetches (via `CastOrderActionRank`): GSZ for Dina joins the drain pair,
   and GSZ X=0 goes to 20. This is a small edge with no probe. It falls out of the same hook as #1.

## 5. Questions for the USER

1. **GSZ at 11, strictly after Tamanoa** (ranked by its target: Dina joins the drains, X=0 goes to 20)? *Default: yes.*
2. **Search the Vito/Dina order** when both are cast in one main (both orders tried, best one kept)?
   The cheaper alternative is a static rule: Dina first unless Rhox or Bilbo is out. *Default: search it.*
3. **ALL_M2 (your 09-29 rule):** build the end-of-main-2 pain sweep, re-measure (held-out, PD only), and
   ship it ON if the net is <= 0? This also settles D2 (`MTG_PD_SECOND_MAIN` OFF). *Default: yes.*
4. **Manabarbs payment timing:** measure a sequential (non-batch) payment for plans that cast
   Manabarbs, when a gain engine and a drain are out and life allows? *Default: yes, measure only.*
5. The value leaf (10-11) and the keep table were fitted on today's play. Regenerate them after
   adopting 1-3, or keep them? *Default: keep them; regenerate only if the re-measure moves play much.*

## 6. Generic-order findings

- Generic puts `on_cast_trigger_damage` (Spellshock) at 30, in the "self-damage last" tier. That is
  backwards whenever `SelfDamageUseful` holds: there the damage source is the payoff and wants to come
  before the spells it triggers on.
- A put-tutor (`tutor_to_battlefield_single`) takes creature tier 10 whatever it fetches. Ranking it as
  its target (`CastOrderActionRank` on `tutor_target`) would be the generic shape.
- Creature/creature ties fall to hand order (`CastOrderLessRanked` breaks ties cheapest-first only
  among mana rituals). This deck shows two such ties that cost value. A tie should be either provably
  harmless or searched.
- `--cast-order-report` labels provider ranks with the GENERIC tier names. Spellshock's 15 reads
  "[15] RITUAL", and 12/13/25/26 read "(unclassified)". That is misleading in a review.

## 7. Implementation notes

- Slots, all by params (house style). Gain: `noncreature_damage_lifegain` / `prevent_noncombat_to_self_gain`
  -> 10. GSZ: `tutor_to_battlefield_single` -> **11** (CastOrderRank). New `CastOrderActionRank`
  override: when `a.tutor_target` is a drain (`PdEngineRole` kPdVito/kPdDina) -> 12, and when it is
  `kTutorDeclineTarget` -> 20. Drains: `lifegain_target_opp_loses_that_much` / `lifegain_each_opp_loses`
  -> 12. Amps: `lifegain_multiplier` / `lifegain_plus` -> 13. `on_cast_trigger_damage` -> 15. Other
  noncreatures -> 20. `x_damage_each_creature_and_player` -> 25. `ping_all_cost` -> 26.
- **Engine limitation for Q2:** `WantsCastOrderingSearch` applies every ordering of EVERY plan with two
  or more casts on a `GameState` copy (TurnSolver.cpp:~50190). A `CastOrderingCandidates` that returns
  false falls back to `DragonstormCastOrderings`. So "search only the drain pair" needs a narrow
  opt-in (the provider names the plans to expand), or the cost of one canonical apply per multi-cast
  plan has to be measured. If the cost is not acceptable, the static rule in Q2 is a pure CastOrderRank
  change (it reads the board).
- **Engine change for Q3:** the pain sweep runs only at the end of main 1 (see the sites in §4.3).
- The rank also drives the greedy playout past the horizon. Even #1 changes valuation, so adoption is
  a scoped held-out A/B (PD only) plus the two-stage recovery check on slower games.
- Stale comments: DecisionProviders.h:471 still says "the cast-order DRAFT (MTG_PD_CAST_ORDER, default
  OFF): tutors, gain, drains, ...". The .cpp says "within a rank the search's plan order stands". It is
  hand order.
