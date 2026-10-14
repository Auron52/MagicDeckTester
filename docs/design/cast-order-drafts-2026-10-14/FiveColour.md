# FiveColour: cast-order draft (verification of the USER order, 2026-10-14)

## 1. Status: **(A)**, a USER-agreed order shipped default ON, with one USER sub-rule that never runs

- `MTG_5C_ORDER` and `MTG_5C_PHASE` are both `EnvOn(..., true)` (DecisionProviders.cpp:15123 and :15132). They
  were adopted in 6311a8c8 on 2026-08-19. The USER's prototype and both review rounds are quoted in
  cast-order-rankings.md:314-381. `MTG_5C_CONDEMN` is also default ON (:15630).
- **The USER's second-round range rule for the scaling dorks has never run.** The rule (2026-08-19): with Greaves live
  and fewer than 5 colours, *"try 2 options in this order: after all spells that add a new colour ... and in front.
  Whether it works is a matter of feasibility"*.
  - It is encoded as `CastOrderFallbackRanks` rungs {ideal, 9} (:15556).
  - Those rungs are read only inside `ApplyCastOrderRangeLadder`. That function returns at once unless
    `MTG_ORDER_RANGE` is set (default OFF) or the provider overrides `OrderOpaqueCastsByRank()`
    (ManaPayment.cpp:3943). FiveColourProvider does neither.
  - The gate (5896d20b, 08-18) is older than the rule (a1706ccd, 08-19). So the "in front" rung has been dead since the
    day it was written, and the adoption measurement ran without it.
  - Probe in section 4, item 1.
- Nothing else in the USER order is OFF.

## 2. What plays today

The report (`reports/FiveColour.txt`) renders an empty board, so it shows only the no-Greaves branch. The ranks that
depend on board state are added from the code (DecisionProviders.cpp:15508-15554).

| rank | card(s) | main | rule |
|---|---|---|---|
| land | land drop | m1 | first at depth 0; at depth > 0 it is folded into the plan |
| 4 | Mana Cannons x3 (copies, so the tie is moot) | m1 if payable without tapping an attacker, or with the hasty-payoff exception; else both | `multicolor_cast_damage_per_color` |
| 5 | Ancient Cornucopia (+ Garth's Black Lotus copy) **tie** | m1 | `mana_rock` / `sac_for_mana_amount` |
| 6 | Lightning Greaves | m1 | haste equipment |
| 7 | Unite the Coalition | m1 iff payable without tapping an attack-capable creature, else m2 | `modal_choose_n` |
| 8 | Birds, Deathrite **tie** (only when Greaves is on the battlefield or in hand) | both (haste access) | plain dork, Greaves live |
| 9 | Bloom Tender, Faeburrow (Greaves live and domain = 5) | m1 | scaling dork, taps for 5 |
| 1x3 / 200 | Bloom Tender, Faeburrow (Greaves live, domain < 5) | m1 | 3 after the cheapest single completer in hand, else 200. **The rung to 9 never fires.** |
| 150 | Cosmic Spider-Man, Garth One-Eye, Maelstrom Archangel **tie** | m1 (haste) / m2 / m2; Garth and Archangel are "both" with Greaves | bodies cheapest-first |
| 160 | Two-Headed Hellkite (+ Garth's Shivan copy) | m1 (haste) | body, mv 6 |
| 210 | Oko, Jared Carthalion, Nicol Bolas (+ Garth's Braingeyser / Regrowth copies) **tie** | m1 / m1 / m2 | walkers and other noncreatures |
| 220 | Progenitus | m2 | `protection_from_everything` |
| 250 | Birds, Deathrite, Bloom Tender, Faeburrow **tie** (no Greaves) | m2 | dorks last |

Checked:
- **Order-opaque:** no FiveColour card is `OrderingOpaque`. Unite's draw is `modal_draw_per_choice`, and Garth's
  Braingeyser copy is `draw_x`, not `draw`. The rank sort therefore governs every cast set.
- **Unite's draws:** they arm the hand-entry breakpoint (TurnSolver.cpp:50904, `MTG_BP_PUT_IN_HAND` default ON), so the
  rest of the turn is re-planned after them.
- **Loyalty:** abilities fire in the trailing pass, after every cast of the main (TurnSolver.cpp:43905). So walker
  position never changes which creatures Jared's -3 can see.

## 3. Proposed order: the USER's order, unchanged, with the ladder wired

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| land | land drop | m1 | - | The only draw (Unite) costs 7 and needs the land's mana. Fetches also fuel Deathrite. |
| 4 | Mana Cannons | as today | - | USER: before every multicolored cast; each one pings for its colour count. |
| 5 | Ancient Cornucopia, Lotus copy | m1 | - | Online for the rest of the line. The tie is order-free: both pay the same total. Cornucopia is a credited producer, so `MTG_PAYABLE_ORDER` repairs any colour miss. |
| 6 | Lightning Greaves | m1 | - | Must be on the battlefield before the unlock hoist can haste a dork (TurnSolver.cpp:11377). |
| 7 | Unite the Coalition | as today | - | USER placement. Draws first, then the breakpoint re-plans the turn. |
| 8 | Birds, Deathrite (Greaves live) | both | - | USER. The tie is order-free: one equip per plan, chosen by the search. **Deathrite's slot is cosmetic today** (section 4, item 2). |
| 9 | Bloom Tender, Faeburrow (Greaves live, 5 colours) | m1 | - | USER: tap for 5 at once. |
| 1x3 or 200 | Bloom Tender, Faeburrow (Greaves live, < 5 colours) | m1 | **[ideal .. 9], wired** | USER's second round: right after the completing permanent, else in front, by feasibility. |
| 150 | Spider-Man, Garth, Archangel | as today | - | Order-free: identical WUBRG cost, each completes the domain, no ETB or cast trigger. |
| 160 | Two-Headed Hellkite | m1 | - | USER: after the mv-5 bodies. It draws on attack, not on cast. |
| 210 | Oko, Jared, Bolas | m1 / m1 / m2 | - | Order-free: loyalty fires in the trailing pass and Cannons pings by colour count. Only Cornucopia's once-a-turn lifegain sees the order. |
| 220 | Progenitus | m2 | - | USER: very last. It cannot wear Greaves, and staying in hand keeps it the Archangel free-cast target. |
| 250 | all four dorks (no Greaves) | m2 | - | USER: last. Order-free: summoning-sick, so no mana this turn. |

## 4. What changes and why it matters

1. **Wire the "in front" rung. This line is unfindable today, at any budget.**
   - Board, T4: Godless Shrine, Breeding Pool, Stomping Ground, Overgrown Tomb, Mana Cannons, Deathrite, and an
     unattached Greaves. Domain is {B,G,R}.
   - Hand: Spider-Man, then Bloom Tender. Opponent at 10.
   - The line: Bloom (2 lands), Greaves onto Bloom, Bloom taps B,G,R. With W and U from the other two lands that is
     Spider-Man, which Cannons pings for 5. Spider-Man swings for 5 and Deathrite for 1: 11.
   - Today Bloom sits at its ideal 153, behind Spider-Man (150). Spider-Man cannot be paid first, and nothing rescues
     the plan: `MTG_PAYABLE_ORDER` admits only rituals, rocks, Treasures and reveals.
   - Defaults: no `[order-range]` line at all, and **no T4 win at d3 b200 or at d8 b0** (opponent left on 4 and 9).
   - `MTG_ORDER_RANGE=1`: `demote=Bloom Tender 612->36`, and T4 is won at d3 b200 and at d8 b0.
   - FiveColour has no other ranged card (its rank ignores the promotion tiers), so `MTG_ORDER_RANGE=1` on this deck
     is exactly the USER's rule.
   - Files: `probe_FiveColour/ladder_{default,range1}_{d3,d8b0}.{json,log.json}`.
2. **A Greaves-hasted Deathrite never funds the rest of its plan. This is an engine defect, not a rank defect.**
   - `HasteUnlockedManaOf` returns nothing for `gy_land_exile_mana` ("conservative", TurnSolver.cpp:11137). Both the
     enumerator credit and the unlock hoist go through it.
   - Probe `greaves_birds_fix`: five lands with no white source plus Greaves; hand Spider-Man and Birds. Birds, equip,
     W, Spider-Man: won T5.
   - Probe `greaves_drs_fix`: the same board with Deathrite and two fetches in the graveyard. **No win at d3 b200 or
     at d8 b0.** The engine casts Deathrite, equips it, and stops.
   - So the USER's "Birds, Deathrite early iff Greaves" slot works for Birds only.
3. **Checked, no change: Cannons (4) and Unite (7) ahead of a mana-positive Greaves dork.**
   - In one main, a dork that Greaves unlocks can fund neither of them. The USER saw this (*"almost a search problem"*)
     and chose the prototype.
   - The main split recovers it. Unite and Cannons stay castable in main 2, where an untapped hasted dork (or a
     vigilant Faeburrow that has already attacked) pays for them.
   - Probe `cannons_vs_unlock` (seed-3 T3 analog: Bloom + Greaves + Cannons on 5 mana plus Bloom's 2): the search took
     Faeburrow's 3-damage attack line (Cannons m1, Bloom m2) over deploying all three. That is a value choice, not a
     line the rank removed.
4. **Checked, no change: every tie** (5, 8, 150, 210, 250) is order-free, for the reasons given in section 3.

Not measured as a whole. Item 1 changes play and needs the usual held-out A/B on FiveColour, with the value leaf and
keep table in place.

## 5. Questions for the USER

1. Wire your 08-19 scaling-dork rule (ideal slot, else in front) as written? It has never run. Default: **yes**,
   default ON, with a FiveColour held-out A/B reported back.
2. Should "in front" stay rank 9 (first of the dorks, still after Cannons, Cornucopia, Greaves and Unite)? The
   alternative is ahead of Cannons. Default: **9**. The main split covers the rest (section 4, item 3).
3. Fix the engine so a Greaves-hasted Deathrite's mana counts in the same plan when a land is in the graveyard?
   Default: **yes**, as a separately measured engine fix.
4. Your 08-19 follow-up on Cornucopia trigger timing ("trigger on the most multi-color spell") is still unbuilt. It
   moves only lifegain. Build it now? Default: **no, keep it parked**.
5. Your 08-19 follow-up on in-between placements for cumulative completers ("rare") is still unbuilt. Build it now?
   Default: **no, keep it parked**.

## 6. Generic-order findings

- **G1. Ladder gate trap.** A provider's `CastOrderFallbackRanks` is silently inert unless it also overrides
  `OrderOpaqueCastsByRank()` or `MTG_ORDER_RANGE` is set (ManaPayment.cpp:3943). The base-class comment
  (DecisionProvider.h:1202) does not mention this.
  - Fix: enter the ladder whenever any cast carries a non-empty funding ladder.
  - This is byte-identical for Mirrorwing, whose ladder and opt-in share `MTG_MW_ORDERED`.
- **G2. Cast-trigger watchers.** A cast watcher (`multicolor_cast_damage_per_color`) gets the generic 20, after the
  creatures (10) it watches. The pre-review table shows Cannons missing every same-main body. Watchers should go before
  what they watch, the same principle as Giants' enter-watchers.
- **G3. Haste equipment at 20 sits after the dorks at 10.** So the unlock cannot fund the bodies. This corroborates
  Bruna G2/G3. FiveColour's order (Greaves 6, dorks 8, bodies 100+) is the working template.
- **G4. `--cast-order-report` shows one board state.** Conditional ranks never appear: here, every Greaves-live
  position and the ladder. A reviewer cannot see what plays. Print the conditional branches.
- **G5. `HasteUnlockedManaOf` excludes `gy_land_exile_mana`.** That hits any Greaves + Deathrite deck (section 4,
  item 2).

## 7. Implementation notes

- **Slots are already param-keyed** (:15516-15551):
  - `multicolor_cast_damage_per_color` -> 4; `mana_rock` / `sac_for_mana_amount` -> 5;
    `is_equipment && equip_grants_haste` -> 6; `modal_choose_n` -> 7.
  - creature with `produces`: `domain_mana` -> 9 / ideal / 250, otherwise 8 / 250.
  - `protection_from_everything` -> 220; creature -> 100 + 10*mv; else 210.
  - Exception: `MainPhaseOverride` keys Bolas by NAME (:15696). A param candidate is the `face_damage` / `steal_creature`
    loyalty set.
- **Q1 is a one-liner:** `bool FiveColourProvider::OrderOpaqueCastsByRank() const override { return Fc5OrderEnabled(); }`.
  - Its only effect here is the ladder, because FiveColour has no opaque card. G1 is the cleaner generic route.
  - Re-run `probe_FiveColour/ladder_default_d3.json` after the change; it should then win T4.
- **Q3:** credit and hoist a hasted Deathrite when the graveyard holds a land the shared fuel does not already back.
  That needs a per-plan fuel count, which is why the code calls the exclusion "conservative".
- **Stale comments to fix:**
  - DecisionProviders.h:1545 says "rank 10 + mv"; the code is 100 + 10*mv.
  - DecisionProviders.h:1559 says `MTG_5C_CONDEMN` is "default OFF pending"; it is ON.
  - DecisionProviders.cpp:15115-15116 says "Both DEFAULT OFF pending measurement"; both are ON.
  - DecisionProviders.cpp:15159 says "Hellkite pinned 195"; the pin was removed in a1706ccd.
  - The 08-19 status block in cast-order-ideal-with-ranges.md quotes the pre-widening scale (25, 10+mv, Hellkite 19).
- **Harmless quirk:** `Fc5ScalingIdealRank` counts the dork itself as the completer when its own colours finish the
  domain, so a Faeburrow lands at 133. Nothing ranks between 9 and 150, so play is unchanged.
- **No scalar-rank limitation blocks the order.** The remaining approximations are ones the USER acknowledged:
  - Greaves "live" means on the battlefield or in hand.
  - Cumulative completers are caught only by position.
  - Garth cast, equip and tap in one plan is still a recorded follow-up.
