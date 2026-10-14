# Anti-Lifegain: cast-order draft (2026-10-14)

Provider `AntiLifegainProvider`. Report: `build/scratch/castorder/reports/Anti-Lifegain.txt`.
Probes: `build/scratch/castorder/probe_Anti-Lifegain/` (scenarios `t5_et_first.json` / `t5_it_first.json`, and two single games).

## 1. Status: (A), with two items still open from the 2026-08-18 review

**(A) Your order is shipped and ON by default.** Two review rounds on 2026-08-18
(`docs/design/cast-order-ideal-with-ranges.md` "USER REVIEW: Anti-Lifegain";
`docs/design/cast-order-rankings.md` "SECOND REVIEW ROUND ADOPTED").
* `MTG_AL_ORDER` is ON (`src/ai/DecisionProviders.cpp:4999`). It sets Swords to 21 (:5019) and Skyshroud Cutter to 24 (:5020), and puts Reverent Silence in m2 (:5129).
* `MTG_ORDER_RECHECK` is ON (`src/ai/ManaPayment.cpp:3461`). This is the Remedy/Silence alternation at the tail.
* The m1/m2 labels are now **enforced**, no longer advisory: `MTG_AL_PHASE` is ON (:5058, adopted 2026-08-22), with `MTG_AL_DORK_M1` (:5138) and `MTG_AL_CONDEMN` (:5091).

**Still open from your 2026-08-18 review:**
* **Instruction 1, "m1 cards should be first in the order if there is no reason to do it otherwise".** It was built as `MTG_ORDER_M1_FIRST` and has been **OFF since 2026-08-18** (`ManaPayment.cpp:2666`). It measured neutral, and turning it on changes 7 decks' digests. Leaving it off was an agent's call to "schedule deliberately". You never made that decision. For this deck, the enforced split (adopted 08-22) already does what you asked; see Q3.
* **"Land drops should be ordered for decks. In this case, it should be put at the start, since there is no draw."** Play already does this; see §2. What is missing is the declaration. A per-deck hook, `LandDropCastOrderRank`, was added later (2026-08-27; `DecisionProvider.h:1214`). Hinata, Mirrorwing, Snow and Kitty use it to declare "start". Anti-Lifegain never adopted it and still returns -1, meaning "no declared slot". See Q2.

**Stale docs (cosmetic):**
* The Anti-Lifegain table in `cast-order-rankings.md` still shows Birds as m2 and Fiery Justice as m2. Both are now different: Birds is m1 and Fiery Justice is both.
* "The m1/m2 column is advisory today" in the ideal-with-ranges doc predates `MTG_AL_PHASE`.

## 2. What plays today

Every cast set in this deck takes the CLEAN route. No card in the list is `OrderingOpaque` (`ManaPayment.cpp:2722`), and the trace confirms `route=clean`. That means the rank sort governs every set. A tie keeps **plan (hand) order**: the cheapest-first tiebreak applies only to mana rituals (`ManaPayment.cpp:2660`), and this deck has none.

| pos | card(s) | main | rule that put it there |
|---|---|---|---|
| land | the land drop | m1 (a searched m2 drop is also possible) | Depth 0: played before every cast. Depth > 0: the folded land is played at plan start, before the cast loop (`AIEngine.cpp:3809`). The m2 drop is always offered (`EngineFlags.h:25`). |
| 0 | Tainted Remedy **=** Plague Drone (TIE) | m1 | generic ENABLER tier, `lifegain_to_loss` (`DecisionProviders.cpp:3081`) |
| 10 | Birds of Paradise **=** Ignoble Hierarch (TIE) | m1 | generic creature tier; `MTG_AL_DORK_M1` |
| 19 | Aria of Flame | m1 | generic CAST PAYOFF, `verse_damage` (:3095) |
| 20 | Enlightened Tutor **=** Idyllic Tutor **=** Invigorate (hard cast) **=** Fiery Justice (TIE) | m1 (Justice: both) | generic "other noncreature"; Justice "both" via `MTG_PHASE_DAMAGE_BOTH` |
| 21 | Swords to Plowshares | m1 | USER instr. 2, `controller_lifegain_equals_power` |
| 24 | Skyshroud Cutter (when the search puts it in a plan) | m2 | USER instr. 3 |
| 30 | Reverent Silence | m2 | generic `destroy_all_enchantments` (:3101) + USER instr. 4. Instr. 5: the recheck moves the extra enablers and wipes to the tail, alternating (`ManaPayment.cpp:3465`). |
| after the plan, each main | auto-fired free alt casts: Invigorate (when a ready attacker exists) and Skyshroud Cutter. Silence auto-fires only when it is lethal. | whichever main is running | `FireSafeAltPayloads` (`AIEngine.cpp:6601`). These are **not ranked**: they fire in HAND order once a Remedy is live. Seed 1004 shows it: T3 main 1 ran Remedy (plan), then Cutter (auto-fired). |

The card Idyllic Tutor fetches is cast **after the whole plan**, in the deferred acquisition re-solve (`AIEngine.cpp:4427`; at depth 0 it is a second pass). So the tutor's slot inside the plan never affects what gets cast with the fetched card.

## 3. Proposed order

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| land | land drop: **START** (declare `LandDropCastOrderRank` 0) | m1 (an m2 drop stays a searched choice) | - | Your ruling. Nothing in the deck draws. Both tutors find only enchantments, so nothing could find a better land. The alt costs (Invigorate, Cutter, Silence) need a Forest. Fetch cracks shuffle, so they must come before Enlightened Tutor. |
| 1 (0) | Tainted Remedy = Plague Drone | m1 | - | Every lifegain payload (Aria's ETB 10, the gifts, Justice's rider, Swords' rider) must resolve with the flip live. The tie cannot matter: both are passive replacements with no ETB, and whichever is cast second gets the Grove drip either way. |
| 2 (10) | Birds of Paradise = Ignoble Hierarch | m1 | - | Same cost {G}, no ETB, both summoning sick, so the tie cannot matter. Casting them after the enabler lets a Grove-paid {G} drip for 1. |
| 3 (19) | Aria of Flame | m1 | - | "Whenever you cast an instant or sorcery": it must precede every instant and sorcery so each one adds a verse counter. It must follow the enabler, or its ETB gives the opponent 10 life. |
| 4 (20) | Idyllic Tutor = Invigorate (hard cast) = Fiery Justice | m1 / m1 / both | - | The tie cannot matter. No card here reads another's result. Verse damage totals the same in any order (1+2+...). Idyllic's fetch is cast after the plan anyway. Idyllic's shuffle no longer hits anything once Enlightened Tutor leaves this tier. Payment is joint (`MTG_PAYABLE_ORDER`). |
| 5 (21) | Swords to Plowshares | m1 | - | Your instruction 2: after Invigorate. It was measured: 21 = GT, 17 = +0.013. |
| 6 (**22, NEW**) | **Enlightened Tutor** | m1 | - | "...then shuffle and put that card on top." Idyllic Tutor's search shuffles the library (CR 701.19; `SpellEffects.cpp:353-374`) and destroys a card already put on top. Your Creature Giving rule: *"Enlightened Tutor should be cast after all other tutors to avoid the library shuffle impacting it."* It is an instant, so after Aria it still adds a verse counter. |
| 7 (24) | Skyshroud Cutter | m2 | - | Your instruction 3. It is a sick 2/2 whose only job is the gift, so it comes before Silence while the Remedy is still live. In practice it is usually auto-fired right after the main-1 plan, with the same outcome. |
| 8 (30) | Reverent Silence | m2, LAST | - | Your instruction 4. It wipes your own Remedy and Aria. The recheck tail alternates extra Remedy / Silence (instruction 5). |

No ranges. Nothing in the list changes same-turn affordability: the dorks are summoning sick, and the deck has no rituals, rocks, reducers or draws.

## 4. What changes and why it matters

1. **Enlightened Tutor 20 -> 22, after Idyllic Tutor. This is the only change that affects play, and the tie is a real defect.**
   * Probe (`t5_*.json`, depth 3, `--budget-ms 0`). Board: T3, Remedy live, 4 lands plus an unsick Birds, Hierarch sick, the T3 draw pinned to a dead Swords. Hand: both tutors. Library: 2 Aria. The only T4 kill: T3 Idyllic (Aria to hand), **then** Enlightened (Aria to top); T4 Aria + Aria for 20.
   * The two runs use the same board and differ only in the order the hand is listed:
     * Idyllic listed first: it plays Idyllic then Enlightened, and **wins T4**.
     * Enlightened listed first: the plan {E-Tutor, Idyllic} plays E-Tutor first. Idyllic then takes the top Aria and shuffles, and the game is **won T5**, even at unlimited budget.
   * Search cannot route around it under the shipped settings. Idyllic in main 1 plus Enlightened in main 2 is condemned: Enlightened was "declined" at main 1, but only because the forced order made it worthless there. With `MTG_AL_CONDEMN=0` the search finds that split and wins T4. So a hand-order tie is deleting a line, and it is also feeding condemnation a decline that was never really a choice.
   * The order also improves target choice. With Idyllic first, Idyllic takes Remedy and Enlightened then takes Aria. With Enlightened first and no enabler live, both tutors chase Remedy, and Idyllic takes the one Enlightened just put on top.
2. **Land slot declared "start".** Play does not change today: the only reader is `BpLandDropSlotPassed` (`TurnSolver.cpp:4861`), and it is gated on `MTG_BP_CONDEMN_LAND`, which defaults OFF. This closes your 2026-08-18 instruction for this deck as a reviewed declaration rather than an accident of the default.
3. **Instruction 1 (m1 first): nothing to change for this deck.** m2 casts already happen after combat. The only remaining tie is Fiery Justice ("both") against the rank-20 m1 spells, and order cannot matter there (row 4 above).

## 5. Questions for the USER

1. **Enlightened Tutor after Idyllic Tutor (rank 22)?** This is your Creature Giving rule applied to this deck. *Default: yes.*
2. **Declare this deck's land slot as START (`LandDropCastOrderRank` 0), as Hinata, Mirrorwing, Snow and Kitty do?** Nothing changes in play until `MTG_BP_CONDEMN_LAND` is adopted. *Default: yes.*
3. **Treat instruction 1 ("m1 cards first") as delivered for this deck by the enforced split, and leave the global `MTG_ORDER_M1_FIRST` (OFF since 08-18, never your decision) to the generic review?** *Default: yes.*

## 6. Generic-order findings

* **A to-top tutor ties at 20 with the same-turn library searches that destroy its placement.** Only two decks hold a to-top tutor plus a same-turn search:
  * Anti-Lifegain: Enlightened Tutor plus Idyllic Tutor and 12 fetches.
  * Creature Giving: Enlightened Tutor plus Sylvan Scrying and fetches. Creature Giving hand-codes 22 (`DecisionProviders.cpp:14917`).

  A generic rule would cover both: `tutor_to_top` ranks after every `tutor_to_hand` / library search.
* **Ties at rank 20 fall back to hand order, and the probe above shows it costing a turn at unlimited budget.** That confirms the brief's pitfall with a concrete case.
* **The report hard-codes the LAND row (`main.cpp:7935`) and ignores `LandDropCastOrderRank`.** It cannot show a deck's declared land slot, or the lack of one.
* **The report does not show auto-fired free alt casts.** Invigorate and Skyshroud Cutter usually play through `FireSafeAltPayloads`: after the plan, in hand order, in the current main, ignoring rank and m1/m2. The reviewed table should mark them "auto-fired after the plan". Order cannot matter for them in this deck: fixed gifts, Cutter is sick, and Silence still comes later.

## 7. Implementation notes

* **Enlightened Tutor:** in `AntiLifegainProvider::CastOrderRank` (`DecisionProviders.cpp:5003`), add `if (p.tutor_to_top) { return 22; }` before the Generic fallback. This is a param test, not a name, and mirrors Creature Giving at :14917. Put it behind its own `EnvOn` lever (default OFF until you adopt it) per `.claude/skills/coding-conventions.md`, or fold it into `MTG_AL_ORDER` once adopted. The generic tier table has no label for 22, so add a `CastOrderTierName` override.
* **Land:** `int AntiLifegainProvider::LandDropCastOrderRank() const { return AlOrderReviewEnabled() ? 0 : -1; }`. The deck meets both conditions in `DecisionProvider.h:1233-1248`:
  * Holding the drop never pays. No card finds a land, and the alt costs want the Forest early.
  * Nothing forces the defer branch. The deck has no flood engine or land-first suppression.
* **No engine limitation blocks the proposed order.** It is a scalar rank, and every set is clean. The auto-fire path stays outside the rank, and order cannot matter for it here.
* **Not proposed: `MTG_AL_RED_ENABLER`.** It demotes a redundant second enabler. It measured inert and is OFF (:5033). It is an agent lever, not your instruction, and the case it targets is about which spells get cast, not their order.
