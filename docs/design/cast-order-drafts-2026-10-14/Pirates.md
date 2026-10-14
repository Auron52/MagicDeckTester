# Pirates: cast-order draft (agent review of the USER's order, 2026-10-14; for USER review)

## 1. Status: **(A) USER-agreed order, shipped default ON**

- **The order:** `PiratesProvider::CastOrderRank` (src/ai/DecisionProviders.cpp:22541-22582) is gated by
  `EnvOn("MTG_PIRATES_CAST_ORDER", true)` (:22543) and heurarm `PIRATES_CAST_ORDER` (HeuristicArm.h:263).
  It was adopted default ON in `d38c52d8` (2026-09-27). Its range companion is `MTG_PAYABLE_ORDER`
  (ManaPayment.cpp:3577-3614, default ON, same commit). The ledger is docs/design/analysis-Pirates.md:223-247.
- **The USER review** (session b67fd547, 2026-09-27 UTC):
  - *"Mimic should be before Buccaneer."*
  - The Malcolm rule: *"malcolm, the eyes should also do the same things as Corsair Captain. We should give up on
    the extra clue if the Daring Buccaneer needs to reveal Malcolm."* The walk-front-to-back correction
    followed.
  - On the full 15-row table: *"Otherwise the order looks good."*
  - On the fallback: *"both Buccaneer and Corsair Captains orders are only about affordability."*
- **No USER order is OFF.**
- **Three things the USER never reviewed:**
  1. **The m1/m2 column.**
  2. **Ranks 8-14.** An agent filled these *"for the sake of it"*. They were accepted inside "looks good",
     never discussed one by one.
  3. **Aether Vial puts.** Since `7173178f` (2026-10-06, a USER rule for all decks: *"Aether vial deployments
     should be done in the same order as casting"*), a put takes its creature's rank. The Pirates slots were
     written for CASTS (see change 2).
- **The "(unclassified)" labels are a report artefact.** `PiratesProvider` has no `CastOrderTierName`, so
  `--cast-order-report` (main.cpp:7885-7906) shows the generic name or nothing:
  - Malcolm's rank 2 prints as "DRAW".
  - Larcenist's rank 10 prints as "CREATURE".
  - Every other rank prints as "(unclassified)".

  The report also scores ranks on an empty hand, so the reveal-dependent slots 4 and 5 never show (Buccaneer
  reads 12).

## 2. What plays today

| pos | card(s) | main* | rule that put it there |
|---|---|---|---|
| land | land drop | m1 | No cast in the deck draws. Crewmate digs only artifacts/Pirates, and Forerunner tutors to the top. So the land goes first (depth 0; folded into the search at depth > 0). |
| 1 | Metallic Mimic | m2 | `other_chosen_subtype_enters_counters`: its counter goes on every Pirate that enters after it (USER). |
| 2 | Malcolm, the Eyes | m1 | `nth_spell_investigate`. Placed here while a Buccaneer in hand keeps a reveal without him, or no Buccaneer is in hand (USER). |
| 3 | Corsair Captain | m1 | `etb_creates_treasures`. Same reveal test, walked after Malcolm (USER). |
| 4 | Daring Buccaneer, reveal live | m2 | `reveal_or_pay_*` with `CanRevealForAdditionalCost` (USER). |
| 5 | Malcolm or Corsair, when it is the Buccaneer's last reveal | m1 | Never both: Malcolm waits only when no Corsair is in hand (:22559-22563). |
| 6 | Forerunner of the Coalition | m1 | `own_creature_enters_opp_life_loss` |
| 7 | Staunch Crewmate | m2 | `etb_dig_count`: *"after all of those that care about the order"* (USER). |
| 8-11 | Adaptive Automaton 8, Dire Fleet Captain 9, Kitesail Larcenist 10, Aether Vial 11 | m1/m2/m2/m1 | Agent-filled "full order". |
| 12 | Daring Buccaneer, no reveal | m2 | |
| 13 | Goblin Tomb Raider | m1 | `static_artifact_threshold` ("after the Vial so it enters hasty") |
| 14 | Siren Stormtamer | m2 | Any other creature. |
| 20 | Lightning Bolt | both | `damage` |

\*The main column is **inert**. The deck flags read `uses_second_main=no`. PiratesProvider has no
`ClassifiesMainPhases` (DecisionProvider.h:1977 defaults false), and `MTG_ORDER_M1_FIRST` is off
(ManaPayment.cpp:2666). So there is no second main, and every cast is pre-combat.

Other facts about what plays:
- **No harmful ties.** Each rank holds one card name, and copies of a card are interchangeable.
- **Nothing in the deck is ORDER-OPAQUE.** No card carries a param that `OrderingOpaque` lists
  (ManaPayment.cpp:2722-2738), so the rank sort always applies.
- **Vial puts are sorted together with the casts** at their creature's rank.
- **The "range" is `MTG_PAYABLE_ORDER`.** The order stands while it can pay. When it cannot, the fallback
  tries the Corsair hoist first, then the nearest permutation that pays. `MTG_ORDER_RANGE` is off.

## 3. Proposed order (USER order, two amendments marked NEW)

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| land | land drop | m1 | - | As today. |
| 1 | Metallic Mimic | m1 | - | USER. "Each other creature ... of the chosen type enters with an additional +1/+1 counter", and it beats the 1 drain Forerunner-first would get off Mimic's own entry: the counter is permanent and Forerunner attacks with it. |
| 2 | **NEW: Forerunner of the Coalition**, when a Buccaneer in hand would still keep a reveal without it | m1 | payable | "Whenever another Pirate you control enters, each opponent loses 1 life". Every Pirate before it drains nothing. The USER: Corsair's and Buccaneer's slots *"are only about affordability"*, and the payable fallback now covers affordability. |
| 3 | Malcolm, the Eyes. Before Forerunner (above it) only when he is CAST as the turn's first spell, so Forerunner's cast is the one that gives him his Clue | m1 | payable | "Whenever you cast your second spell each turn, investigate". He needs to be on the battlefield when spell 2 is cast. The USER's Buccaneer rule is unchanged. |
| 4 | Corsair Captain | m1 | payable (maker hoist) | "Other Pirates get +1/+1" is static and works in any order. The Treasure matters only when the line needs it, and then the hoist moves Corsair first. USER Buccaneer rule unchanged. |
| 5 | Daring Buccaneer, reveal live | m1 | payable | "reveal a Pirate card from your hand or pay {2}". It goes before its reveal fodder leaves the hand (USER). |
| 6 | Whichever of Forerunner / Malcolm / Corsair was the Buccaneer's last reveal | m1 | payable | The USER's walk, front to back, now with Forerunner first. |
| 7 | Staunch Crewmate | m1 | - | USER. Its dig of the top 4 finds the Pirate that Forerunner's ETB just tutored to the top. Its dig can also give a slot-12 Buccaneer its reveal. |
| 8 | Adaptive Automaton | m1 | - | A static lord, so position doesn't matter for the lord. Late so it gets Mimic's counter and drains: as it enters it "is the chosen type". |
| 9 | Dire Fleet Captain | m1 | - | Its pump counts attackers at declare-attackers, so position doesn't matter. Late for Mimic's counter and the drain. |
| 10 | Kitesail Larcenist | m1 | payable (Treasure) | It treasurifies one own permanent, which can pay at once. Late, so it sees the fullest board. |
| 11 | Aether Vial | m1 | - | It enters with 0 counters and deploys nothing this turn. It counts as a cast for Malcolm's Clue only. |
| 12 | Daring Buccaneer, no reveal; **NEW: also every Vial-PUT Buccaneer** | m1 | payable | A put is not a cast and pays no additional cost, so the reveal reason for slot 5 does not apply to it. |
| 13 | Goblin Tomb Raider | m1 | - | Haste and +1/+0 are read live at declare-attackers (`CanAttackFull`), so its order relative to an artifact doesn't matter. Late for the counter and the drain. |
| 14 | Siren Stormtamer | m1 | - | A vanilla 1/1 Pirate flier, since its ability is unmodelled. Late for the counter and the drain. |
| 20 | Lightning Bolt | m1 | - | Last. Probed: the payer keeps the one Bolt-legal {R} for it (see §4). |

- **Ties:** none between different cards.
- **Main phase:** every row is m1. That is what plays already, and in a goldfish it is weakly dominant:
  - lords and Mimic must come before the hasty Malcolm and Tomb Raider;
  - Bolt has nothing to learn from combat.

## 4. What changes and why it matters

Single-turn probes. Each is a hand-built board, `resume_at main1`, d3 at unlimited budget, opponent at 20.
Files are in build/scratch/castorder/probe_Pirates/. Each one-life loss below is invisible to the search: Pirates
does not search orderings, so the rank is a prune.

1. **A Vial-put Buccaneer is ranked as if it paid a reveal** (`vialput_bucc`). T3: 3 lands plus Vial at 1, hand
   Forerunner + Buccaneer.
   - Today the Vial puts the Buccaneer (rank 4, because Forerunner is reveal fodder) and then casts Forerunner:
     opponent 20 -> 20.
   - With the lever off (plan order), Forerunner comes first: 20 -> 19.
   - So the USER's order is a point worse than generic here, purely because of how puts are ranked.
2. **Corsair ahead of Forerunner when the Treasure isn't needed** (`corsair_forerunner_vial3`). T4: Vial at 3 plus
   3 lands, hand Corsair + Forerunner. Corsair (put) resolves first, then Forerunner: 0 drain. Forerunner first
   would give 1, and both are deployed either way.
3. **Buccaneer ahead of Forerunner when another reveal exists** (`bucc_forerunner_raider`). T5, 5 lands, hand
   Buccaneer + Forerunner + Tomb Raider.
   - Today: Buccaneer, Forerunner, Raider, 1 drain (20 -> 19).
   - Proposed: Forerunner, then Buccaneer (revealing Raider, still {R}), then Raider: 2 drains, for the same mana.
4. **A Vial-put Malcolm ahead of Forerunner** (`vialput_malcolm`). T4: Vial at 2 plus 3 lands. Malcolm (put) goes
   in, then Forerunner is cast: 0 drain. No Clue is possible either way (one cast), so Forerunner first would
   be a free point.
5. **What does not change:**
   - Malcolm keeps his Clue on the turns he can actually get it (Q2).
   - Mimic stays first.
   - The Buccaneer never pays {2} because of the new rank. The enumerator debits the LARGER of the rank-order
     and plan-order reveal cost (TurnSolver.cpp:6880-6889), so an unconditional Forerunner-first could hide a
     line. That is why the amendment is conditional.
   - Bolt-last is safe. In `bolt_payer` (T4: Territory, Courtyard, Territory, Mountain, a non-sick Dire Fleet
     Captain, opponent at 6), Corsair + Bolt wins on T4 with the Mountain kept for Bolt.

Size of the gain: 1 life per Pirate that enters after a misordered Forerunner (a 2-of). That buys a turn only when
lethal is short by exactly that much, so expect a small held-out gain, measured per CLAUDE.md's bar.

## 5. Questions for the USER

1. **Forerunner second** (after Mimic, ahead of Corsair, Buccaneer, and a Malcolm who can't get his Clue), only
   while the Buccaneer keeps a reveal without it? *Default: yes. Build it behind a default-OFF lever, measure, and
   bring it back.*
2. **On the one turn shape where it is Clue vs drain** (Malcolm cast as the turn's first spell, Forerunner
   second), which comes first? *Default: Malcolm, keeping the Clue.*
3. **Should a Vial-PUT Buccaneer rank with the reveal-free bodies (slot 12)** instead of slot 4/5, since a put pays
   no reveal? *Default: yes.*
4. **Should everything be main 1?** This confirms what plays now; the deck has no second main. *Default: yes.*

## 6. Generic-order findings

- **Generic tiers tie all 12 creatures at 10**, so with the lever off, plan (hand) order decides. Two
  "enters" watchers need a generic tier ahead of plain creatures (principle 3), the same as Giants' Goliath:
  - the as-enters counter grant (`other_chosen_subtype_enters_counters`);
  - the enter-drain (`own_creature_enters_opp_life_loss`).
- **Vial puts inherited cast ranks (2026-10-06), but a rank built around a CAST cost is wrong for a put.**
  Examples are a reveal-or-pay additional cost, an alt cost, or a reducer's beneficiary. A default
  `CastOrderActionRank` for `ActivateVial` could drop cost-only reasons in every Vial deck (Knights, Soldiers,
  Slivers, Goblins).
- **`--cast-order-report` has two gaps:**
  - It labels provider-defined ranks with generic tier names when the numbers happen to collide.
  - It evaluates state-dependent ranks on an empty hand, so conditional slots like 4/5 never print. It could
    print every value a conditional rank can take.

## 7. Implementation notes

- **Params per slot** (house style: params, never names):
  - Mimic `other_chosen_subtype_enters_counters`
  - Forerunner `own_creature_enters_opp_life_loss`
  - Malcolm `nth_spell_investigate`
  - Corsair `etb_creates_treasures`
  - Buccaneer `reveal_or_pay_subtype` + `reveal_or_pay_cost`
  - Crewmate `etb_dig_count`
  - Automaton: creature with `power_bonus`
  - Dire Fleet Captain `attack_pump_power_per_other_matching`
  - Larcenist `etb_treasurify_each_player`
  - Vial `upkeep_adds_charge`
  - Tomb Raider `static_artifact_threshold`
  - Stormtamer: the creature fallthrough
  - Bolt `damage`
- **Change 1 (Q1): Forerunner second.**
  - Add a `drain` count to `PiratesRevealFodder` (:22514-22538).
  - Walk the early candidates in the order Forerunner, Malcolm, Corsair, with the USER's
    keeps-a-reveal-after-me test applied to each.
  - Renumber 2-6.
- **Q2 (the Malcolm-first exception) needs the plan.** Whether Mimic is cast this turn is not visible to a
  per-card rank, so override `ArrangeCastOrder` (DecisionProvider.h:940-946). It sees the set:
  - Malcolm is the set's first CAST and `s.spells_cast_this_turn == 0` -> Malcolm goes ahead of Forerunner;
  - otherwise Forerunner goes first.
- **Change 2 (Q3): Vial-put Buccaneer.** Override `CastOrderActionRank` (DecisionProvider.h:937). For
  `a.kind == ActivateVial` on a reveal-or-pay creature, return 12. The put card also stays in hand longer as
  fodder for a cast Buccaneer.
- **Check before measuring: the maker hoist must hold in the enumerator.** The ETB Treasure is credited as
  aggregate `rock_mana.wild` (TurnSolver.cpp:24670-24681), and both apply worlds hoist it
  (`ApplyEtbTreasureFundingOrder`). That should keep a 5-land Corsair + Forerunner turn findable with Corsair
  ranked after Forerunner. Confirm it with a scenario before the A/B.
- **Lever:** something like `MTG_PIRATES_FORERUNNER_EARLY`, default OFF until the USER answers. The adoption
  bar is: held-out, every slower game recovers at d8/b0, and the deck aggregate is <= 0.
- **Stale text to fix when this lands:**
  - DecisionProviders.h:2803-2804 says CastOrderRank is *"deliberately NOT overridden"*. That contradicts
    :2859-2879.
  - analysis-Pirates.md still says "NOTHING ADOPTED" (:273-297) and "root order in force" (:512).
  - The Tomb Raider comment, "after the Vial so it enters hasty", is not the mechanism: haste is checked live
    at declare-attackers. The slot itself is fine.
  - Add `PiratesProvider::CastOrderTierName`.
