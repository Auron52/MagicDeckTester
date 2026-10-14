# StompySurprise: cast-order draft (2026-10-14)

Provider `StompyProvider` (src/ai/DecisionProviders.cpp:25818). Single-main deck (`uses_second_main=no`;
`MainPhaseFilterActive` returns false at TurnSolver.cpp:20388), so **every cast is main 1**. The report's
`m2` labels on the elves, Elderscale and Apex come from the baseline classifier and do nothing on this deck.
No card in the list is ORDER-OPAQUE (Vaultborn's `own_creature_enters_draw` and Worldly Tutor's
`tutor_to_top` are not in `OrderingOpaque`), so the rank sort really does decide what plays.

## 1. Status: (B) a USER order exists and is OFF

* **Flag:** `MTG_STOMPY_ORDER`, default OFF (DecisionProviders.cpp:25784). It has been OFF since it was
  implemented on 2026-08-21. The USER's verbatim order and rulings are in docs/design/cast-order-rankings.md
  §StompySurprise (line 869); its adoption measurement is in the same file at line 1100, plus
  docs/design/stompy-order-and-top-resolve.md.
* **It never measured worse in aggregate.** Every recorded run has it better:
  * 08-21: the 8-cell grid, with the order bundled with TOP_RESOLVE and TAP_BIG_DORKS_FIRST, was better or
    equal in all 8 cells.
  * 08-25: on its own, 15,000 paired games gave −37 turns (79 better : 43 worse, +3.26σ), and it was 2.2% faster.
  * 08-25: combined with `MTG_TOP_RESOLVE`, 59 better : 31 worse (t −2.43 / −1.90).
  * 08-28: the suite read −0.0064 (t −2.83) on hold and −0.0084 (t −3.21) on train.
* **Why it is still OFF:**
  1. **Gate 2.** 37 of the 43 regressed games never recover, even at depth = target win turn with budget 2M.
     The rank is a hard PRUNE of the cast sequence. `MTG_UNPRUNE=searchorder` recovered 8 of 8.
  2. That verdict was then **voided**: it was measured without `MTG_TOP_RESOLVE`, the reset the order was built
     around. It was never re-derived.
  3. Nothing has been re-measured on the value leaf (analysis-StompySurprise.md:551) or on the 09-17 list. That
     list adds Ghalta, World War Hulk, Apex Altisaur and Fyndhorn Elves, and drops Arbor Elf and Mirri's Guile.
* **Related USER intent that is also OFF:**
  * `MTG_TOP_RESOLVE`, the USER's "Worldly Tutor resets Call/Turntimber" loop (EngineFlags.h:979).
  * `MTG_UPKEEP_CALL`, the upkeep Call of the Wild activation. It measured −0.0019 (39 better : 0 worse) and is
    held back only because the viewer has no chooser for it (stompy-top-of-library-consumers.md).

## 2. What plays today (generic tiers inside StompyProvider)

| pos | card(s) | main | rule |
|---|---|---|---|
| 0 | land drop | m1 | the deck has no cantrip, so the land goes first |
| 5 | Sol Ring | m1 | mana rock |
| 9/10 | Elvish Mystic, Fyndhorn, Llanowar, Priest, Archdruid | m1 | creature. Rank 9 (dork growth) applies while an untapped Priest or Archdruid can tap. **TIE**: hand order |
| 10 | Vaultborn, Ghalta, Elderscale, Hornet Queen, Terastodon x2, Apex, Worldspine, **Craterhoof x2** | m1 | creature. **TIE** (hand order): Craterhoof can land before the bodies it counts |
| 20 | Worldly Tutor, Call of the Wild, Natural Order, World War Hulk, Turntimber | m1 | other noncreature. **TIE** (hand order): a shuffle can follow a tutor; Turntimber can come before the tutor |
| trail | Call of the Wild activations | m1 | engine: after every cast |

With the flag ON on today's list, two cards land in the wrong tier. World War Hulk falls through to generic
rank 20 and **ties with Craterhoof**; the report even labels it "CRATERHOOF". Ghalta sits in the "order
doesn't matter" fatty tier at 18.

## 3. Proposed order: the USER's tiers, amended

These are the USER's tier numbers (engine rank in parentheses). "2-way" means the alternative slot is
**searched**: the USER's slot is tried first and the other is a branch (§7).

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| 0 | land drop | m1 | – | The only draw is Vaultborn (7 mana), so nothing cheap wants to precede the land, and every line needs its mana. Turntimber's land face is played here when it is the drop. |
| [1] (3) | Elvish Mystic, Fyndhorn Elves, Llanowar Elves | m1 | – | USER: "Sol Ring can't fund these". They cost {G} only, and each one already down adds 1 to a later Priest or Archdruid tap. The tie is safe: their text is identical. |
| [2] (5) | Sol Ring | m1 | – | USER: "funds almost everything else". Its {C}{C} pays the generic part of every later cost. |
| [3] (6) | Priest of Titania | m1 | – | USER: the cheapest scaler goes first, so the later elves still add to its count. |
| [4] (7) | Elvish Archdruid | m1 | – | USER [4]. It is also a lord. |
| [5] (8) | Call of the Wild (the cast) | m1 | – | USER [5]. Its activations trail every cast, so they see whatever a tutor stacked. |
| [7] (12 / 15) | Turntimber Symbiosis | m1 | 2-way | USER [7]. It moves to 15, after the tutor, when the PLAN casts a mid tutor onto an unstacked top. Rank 15 measured +2.85σ, so it stays. The 2-way branch keeps "Turntimber first on the unstacked top" findable. |
| [8] (13) | Natural Order, early copy | m1 | 2-way | USER: early "only... to avoid random shuffles before effects that deal with the top". It fires only when the plan casts a mid tutor. |
| [9] (14) | Worldly Tutor, mid | m1 | 2-way | Only when the plan casts a **Turntimber** to consume the stacked card. A Call activation is served by the late tutor (§4.5). Vaultborn is not a consumer (measured 2:0). |
| [10] (16) | Vaultborn Tyrant | m1 | – | USER [10]. It is an enters-watcher: it draws on its own entry and on every later power-4+ entry (Ghalta, the fatties, Craterhoof). |
| NEW (17) | Ghalta, Stampede Tyrant | m1 | – | Its ETB puts every creature card in hand onto the battlefield. Anything hard-cast before it spent mana Ghalta would have saved. A Craterhoof it deploys enters with the team and counts all of it. It goes after Vaultborn, which draws off its entries. |
| NEW (17) | World War Hulk, early | m1 | 2-way | Used when neither Craterhoof nor Ghalta is in hand. Chapter I then frees the biggest hand body, so Hulk goes before the hard-cast fatties (it frees the dearest one) and before a late Natural Order or a Call/Natural Order Craterhoof (which count that body). It never sorts together with Ghalta, because a Ghalta in hand makes Hulk late. |
| [11] (18) | Elderscale Wurm, Hornet Queen, Terastodon, Apex Altisaur, Worldspine Wurm | m1 | – | USER: "order doesn't matter". That holds here. None of them reads another's entry: Terastodon destroys noncreature permanents, and the Insect and Elephant tokens have power below 4, so Vaultborn draws nothing off them. Every card that reads them comes after: the late Natural Order (fodder and census), the late Hulk, and Craterhoof's X. |
| [12] (19) | Natural Order, late (DEFAULT) | m1 | 2-way | USER: "avoid us sacrificing creatures early... drop a powerful Craterhoof with it". It sees the full board, and its fetch can be Craterhoof or Ghalta. |
| NEW (20) | World War Hulk, late | m1 | 2-way | Used when a Craterhoof or Ghalta is in hand: chapter I frees that card, so Hulk takes the pump's slot. It goes after every body, including the late Natural Order's fetch, and before a hard-cast Craterhoof, so "free it instead" stays open. |
| [14] (21; was 20) | Craterhoof Behemoth | m1 | – | USER: "wants to be last for maximum pump". |
| [14'] (24) | Worldly Tutor, late (DEFAULT) | m1 | 2-way | USER: "for next turn draw + upkeep (can choose to cast either location)". It comes after the late Natural Order's shuffle, and the trailing Call activation still sees the stacked card. That is the USER's [13] "tutor for Craterhoof" line. |
| trail | Call of the Wild activations | m1 | – | engine trailing pass |

Mirri's Guile is no longer in the list. Its tier (22) is inert, so it can stay. **No affordability ranges**: none
of the dual positions is an "is it payable" question. They are top-of-library and census questions, and a range
ladder cannot choose between those because it walks only when a line cannot be paid.

## 4. What changes and why it matters

1. **Craterhoof goes last.** Today it ties every creature at 10, and every library deployer (Natural Order,
   Turntimber, Hulk) is at 20, behind it. Example on T5 (Ghalta already gone): the board is Priest, Archdruid,
   Mystic and Llanowar, with 5 Forests; the hand is Craterhoof + Natural Order; 13 mana.
   * Today: Craterhoof resolves first with X = 5, so Mystic 7 + Llanowar 7 + Hoof 10 = **24**. Natural Order then
     eats the tapped Priest, and the Hornet Queen it fetches is uncounted.
   * Proposed: Natural Order (19) fetches Hornet Queen first (+5 bodies), then Craterhoof has X = 9: 11 + 11 + 14
     = **36**.
   * So today misses the 30-life (2HG) kill.
2. **Turntimber goes after the tutor that stacks for it, and this needs TOP_RESOLVE.** The USER's headline combo
   is "Worldly Tutor puts Craterhoof on top, Turntimber puts it in play" (T4: 8 mana).
   * Without the reset, Turntimber's put choices are collected before the tutor resolves, so the combo cannot be
     found at any budget.
   * With the reset, generic still leaves the tutor-versus-Turntimber order to hand order.
3. **Natural Order's shuffle never lands on a stacked top.** Today the tutor and Natural Order tie at 20.
   Proposed: late Natural Order (19) before the late tutor (24), and early Natural Order (13) before the mid tutor
   (14).
4. **Hulk frees the Craterhoof after the bodies, not before.** Commit fd901a03 records that "a same-plan Natural
   Order resolves AFTER the Saga". With hand {Hulk, Natural Order, Craterhoof}, Hulk-first frees a Craterhoof
   whose X misses Natural Order's body, and Natural Order's sacrifice then eats a pumped attacker. Proposed:
   Natural Order (19) → Hulk (20) frees Craterhoof.
5. **Worldly Tutor: a Call activation takes the late tutor (an amendment to [9]).** Activations trail every cast,
   so a tutor at 24 feeds the Call exactly as a tutor at 14 does. The mid slot is worse in two ways:
   * It forces Natural Order early (13). That is the measured "census myopia" mechanism (d0 gi106 27→17, gi445
     30→10).
   * Any power-4+ entry between the tutor and the activation, with Vaultborn in play, draws the stacked card
     into hand.

   Mid is needed only when a Turntimber cast will consume the card.

**Amending the USER order: the recorded root cause and how the amendment answers it.**

* **(a) Gate 2: the rank prunes.** The losses sit on the dual-position cards, where a state test picked one slot
  and the other became unreachable. Recorded cases (DecisionProviders.cpp:25797/25809):
  * s1500000g137 T4: {tutor, Vaultborn}. The rank casts the tutor first and wins T6; Vaultborn first wins T5.
  * s1000000g444 T3: {tutor, Turntimber}. The rank casts the tutor first and wins T5; Turntimber first, on the
    unstacked top 7 of a creature-dense library, wins T4.
  * The USER's own text says "can choose to cast either location".

  **Answer:** the alternative slot becomes a searched 2-way branch, with the USER's slot tried first. The tests
  read the PLAN instead of the hand, which removes the documented over-trigger "an UNCAST hand tutor still forces
  NO early".
* **(b) Half the design was measured.** **Answer:** ship it with TOP_RESOLVE for this deck.
* **(c) One agent-added clause was a strict loss.** Counting Vaultborn as a tutor consumer measured 2 better : 0
  worse to remove. **Answer:** drop it, which is exactly the USER's written [9]. Keep the agent's Turntimber 15,
  which measured 29:11 in its favour.
* **(d) Stale list.** **Answer:** place Ghalta and Hulk. Then re-measure on the shipped config (value leaf, keep
  table, 09-17 list) before anything flips.

Of the 26 classified gate-2 games (logs/ord26, `scripts/stompy_order_counterexamples.py`):
* 22 diverge on the PLAN, not the order. The order changes which plan the rollouts value, so the re-measure is
  the real test.
* 4 of those 22 share one T2 shape: generic casts {1-drop, Priest} where the order picks {Archdruid}. See §7,
  note 9.

## 5. Questions for the USER (default in brackets)

1. **Re-measure your order as amended here** (with TOP_RESOLVE, on the value leaf and today's list), then make it
   Stompy's default ON if it clears the bar? [yes]
2. **Worldly Tutor goes mid ONLY when a Turntimber will consume it.** A Call activation takes the late tutor, since
   activations trail every cast anyway. Vaultborn does not count as a consumer. [yes]
3. **Your "either location" cards become a searched 2-way choice**, with your slot tried first: Worldly Tutor
   mid/late, Turntimber before/after the tutor, Natural Order early/late, Hulk early/late. [yes]
4. **World War Hulk has two slots:** just before Craterhoof when a Craterhoof or Ghalta is in hand (it frees
   that card); otherwise before the fatties (it frees the biggest body for the later count). [yes]
5. **New cards:** Ghalta first of the fatty group (after Vaultborn), Apex Altisaur with the fatties, Fyndhorn
   with the 1-drops? [yes]

## 6. Generic-order findings

* **A team pump (`etb_team_pump_per_creature`) should be the LAST body producer.** Generic ties it at 10 with
  every creature (hand order) and puts every noncreature deployer (`tutor_to_battlefield_single`,
  `look_top_put_creature_count`, `saga_ch1_free_cast_creature_colors`) at 20, after it.
* **Library writers and readers are lumped together at 20.** The general rule should be: shuffler
  (`tutor_shuffle_after`) → `tutor_to_top` → top-consumers (`look_top_put_creature_count`; activations already
  trail).
* **Enters-watchers should precede the creatures they watch.** `own_creature_enters_draw` ties with the creatures
  it watches. `MTG_WATCHER_ORDER` (rank 9, OFF) covers only the dragon params; extend it to the enters-draw and
  enters-lifegain params.
* **A hand-deployer (`etb_put_creature_cards_from_hand`) should precede other hand creature casts.**
* **Report and authoring:**
  * An authored tier number that collides with a generic fall-through number silently creates a tie (Hulk with
    Craterhoof at 20). `--cast-order-report` should flag an authored provider that leaves a listed card on a
    fall-through rank.
  * The report should print `m1` on a single-main deck.

## 7. Implementation notes

**Slots by param** (house style):
* 3: `ManaDork` without `mana_per_creature_subtype`
* 5: `mana_rock` and not a creature
* 6 / 7: `ManaDork` with `mana_per_creature_subtype`, mana value ≤ 2 / 3
* 8: `activated_reveal_top_cost`
* 12 / 15: `look_top_put_creature_count`
* 13 / 19: `tutor_to_battlefield_single`
* 14 / 24: `tutor_to_top`
* 16: `own_creature_enters_draw` and a creature
* 17: `etb_put_creature_cards_from_hand` (NEW, checked before the creature fallback)
* 17 / 20: `saga_ch1_free_cast_creature_colors` (NEW). It goes to 20 when the hand holds an
  `etb_team_pump_per_creature` or `etb_put_creature_cards_from_hand` card, else 17.
* 21: `etb_team_pump_per_creature` (was 20)
* 18: other creatures
* 22: `upkeep_reorder` (inert)

**Changes:**
1. **Make the WT_LITERAL behaviour the default.** Drop `own_creature_enters_draw` from both censuses (the tutor's
   and Natural Order's lockstep copy). Retire `MTG_STOMPY_WT_LITERAL`. Keep TT 15.
2. **Make the dual-position tests read the PLAN, not the hand.** Use `ArrangeCastOrder`, which sees the set:
   * mid tutor only if the plan casts a Turntimber;
   * early Natural Order only if the plan casts a mid tutor;
   * Turntimber at 15 only if the plan casts the tutor.
3. **Search the 2-way slots.** Set `WantsCastOrderingSearch()` true, and make `CastOrderingCandidates` return the
   USER order plus one variant per dual-position card present, with that card at its other slot (at most 2^k
   orderings, k ≤ 4, usually 1–2, deduplicated by end state). Rollouts beyond the horizon keep the canonical rank.
   * For calibration: Dragonstorm's full k! search cost +47% makespan, and this set is far narrower. Measure wall
     time and work units.
   * The completeness rule (10-06) would also want the remaining permutations admitted in a later budget wave.
     The 08-25 `searchorder` run recovered 8 of 8.
4. **Renumber Craterhoof 20 → 21 and update `CastOrderTierName`.** Add 17 (Ghalta / Hulk early) and 20 (Hulk late).
5. **Adoption switches.**
   * `MTG_STOMPY_ORDER` becomes `EnvOn(..., true)`; keep the heurarm slot for A/Bs.
   * **TOP_RESOLVE is a global flag.** Other `tutor_to_top` cards (Enlightened Tutor, Giant Harbinger, Forerunner
     of the Coalition) may also arm it; check which cards actually trigger it. Adopt it through a per-provider
     opt-in, not by flipping the global default.
6. **Engine limitation: activation position.** Activations always trail every cast, so the USER's [6] "activate
   on the unknown top BEFORE the tutor" cannot be expressed (an open item in the rankings doc). The late tutor
   plus the trailing activation is the closest expressible line.
7. **Known residual.** A Turntimber whose put IS Craterhoof still fires at 12/15, before Ghalta and the fatties,
   so their bodies miss X. Moving it later would put Vaultborn's draws and Natural Order's shuffle in front of the
   top 7 it was planned against, so I left it at the USER's [7].
8. **Verify first: BP_NEW_ONLY versus the reset.** `MTG_BP_NEW_ONLY` (default ON since 09-22, after every Stompy
   measurement) drops continuations that reuse only OLD cards. The reset's "Tutor unlocks Turntimber again" is
   exactly such a continuation unless `BpPlanCasts` keeps it.
9. **Hypothesis to check (4 of the 26 counterexamples: generic casts {1-drop, Priest} at T2 where the order picks
   {Archdruid}).** The dork-growth credit in EnumeratePlans models the generic executor's selection-order prefix,
   but the USER order re-sorts the feeders 3/6/7. If the credit and the executed order disagree, the projected
   mana is not what the executor gets, and the search would avoid those lines.
10. **TOP_RESOLVE's own regressions** have one recorded shape: an extra speculative tutor that taps an attacker.
    11 of them survive d8/b100k, which makes it a valuation problem, not a budget one. Re-check it on the value
    leaf.
