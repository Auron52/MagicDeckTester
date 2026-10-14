# Hydras: cast-order draft (2026-10-14)

**This deck is not ready for play.** Of its 14 nonland names, only Lightning Greaves is in cards.json; Forest is the only land. The other 36 nonland cards sit in hand as dead cards. No implemented card can deal damage, so no game can be won: probe s1001 gi0 made four land drops, cast nothing, and went unwon (avg 9). Everything below is a **provisional order, written from Scryfall oracle text** (cards.json has no entry for these cards). It is meant to steer the implementation, and it gets re-checked against the real params once the cards exist.

## 1. Status: **(D)**, generic tiers only
* Provider: **Generic**. There is no Hydras override, no flag, and no Hydras section in cast-order-rankings.md or any docs/design file.
* **Not in `test/regression_cases.sh`**: no Hydras case and no ground truth, so nothing tracks this deck's play.
* **Routing is deliberate, but only as a default.** The USER said on 2026-10-08 *"We can commit Hydras as-is for now"*. The pin `{"Hydras", "Generic"}` (test_pirates_provider.cpp:158, commit 8c2a7aac) was *recorded from a measurement*: no card trips an archetype signature, so the deck falls through to Generic. It therefore inherits no other deck's order.
* **Hazard:** implementing Nature's Lore, Three Visits or Search for Tomorrow with `tutor_to_hand` or `fetch_land_types` sets the `anti` signature (DecisionProviders.cpp:18522) and silently moves the deck to **AntiLifegainProvider**. The safe param is `tutor_land_to_battlefield` (Crop Rotation's), which no signature reads. Better: create `HydrasProvider : DeckProvider` first, as analyze-deck Stage 4 requires.

## 2. What plays today
| pos | card(s) | main | rule |
|---|---|---|---|
| 0 | land drop | m1 | first (the deck has no cantrip); at depth > 0 the land is searched as part of the plan, and the executor plays it before the casts (TurnSolver.cpp:39661) |
| 1 | Lightning Greaves | m1 | rank 20, other noncreature |
| - | the other 13 nonland names | - | not in cards.json |

If those 13 were implemented with no new rank params, generic would put every Hydra and Gargos at 10 (**TIE**), and Vernal Bloom, Gauntlet, Nissa, Garruk, the three fetch spells and Greaves at 20 (**TIE**). Plan (hand) order would settle both ties. The deck has a single main phase (`uses_second_main=no`), so every cast is effectively m1.

## 3. Proposed order
| pos | card(s) | main | range | why |
|---|---|---|---|---|
| 0 | land drop | m1 | - | First. The deck has no draw spell; Garruk's -3 is an activation, not a cast. |
| 1 | Vernal Bloom | m1 | - | "Whenever a Forest is tapped for mana, its controller adds an additional {G}". It is a mana doubler and the cheapest one (4), so it goes before everything it can help pay for, including the 5-mana doublers. |
| 2= | Gauntlet of Power (choose green), Nissa, Who Shakes the World | m1 | - | Both are 5-mana doublers, so both go before the fetches and the creatures. **The tie is safe:** each adds exactly one {G} per Forest tap (every land here is a basic Forest) and neither reads the other. Nissa's +1 (untap a land) is an activation that trails the casts (breakpoint site 9), so it feeds only the continuation. |
| 3= | Nature's Lore, Three Visits, Search for Tomorrow (hard-cast) | m1 | - | Each puts an UNTAPPED basic Forest onto the battlefield. They go after the doublers, so the new Forest taps for more, and before every creature, because that Forest helps pay for it. **The tie is safe:** all three have the same effect, the same total is paid in any order, and mana per tap is fixed once the doublers are down. Search's suspend {G} is a special action, not a cast. |
| 4 | Gargos, Vicious Watcher | m1 | - | "Hydra spells you cast cost {4} less", so it goes before every Hydra, Kalonian included (principle 2). It is a 6-drop, so the fetches come first. |
| 5 | Lightning Greaves | m1 | - | A fixed-cost cast placed ahead of the X sinks. Its equip {0} is a separate action that trails every cast, so moving the cast earlier changes nothing else. |
| 6 | Garruk, Primal Hunter | m1 | - | A fixed cost ahead of the X sinks. His -3 ("draw cards equal to the greatest power") is a loyalty activation that trails the plan's casts (site 9), so it still sees the Hydras cast after him. |
| 7 | Kalonian Hydra | m1 | - | A fixed {3}{G}{G}, so it goes before the X sinks. Its attack trigger "double[s] the number of +1/+1 counters on each creature you control", so Hydras cast pre-combat this turn get doubled; m1 is the only main anyway. |
| 8= | Voracious Hydra, Primordial Hydra, Neverwinter Hydra, Steelbane Hydra | m1 | - | X sinks go last, taking whatever mana is left. **The tie is safe:** each one's counters depend only on its own X, and none reads another's ETB or cast (Voracious doubles only itself; Primordial doubles only at upkeep). |

## 4. What changes and why it matters
All of these are **predicted, not measured**: the cards do not exist yet.
1. **Doublers move from 20 to before every creature.** T6: six Forests, hand Vernal Bloom + Primordial Hydra. Bloom first leaves two Forests making {G}{G} each, so Primordial is cast at X=2 (4/4 at the next upkeep). Generic casts the creature (10) first, which leaves the pair {Bloom, Primordial X>=1} unpayable, so one of them is lost.
2. **Fetch spells move from 20 to before every creature.** T4: four Forests, hand Nature's Lore + Voracious Hydra. Lore first leaves three untapped Forests, so Voracious is cast at X=1 (doubled to 2/3 trample) and the deck gains a permanent fifth land. Generic order makes that pair unpayable (Hydra 3, then 1 left for {1}{G}). Fetch first always weakly dominates: the same total is paid, and the fetched land adds one.
3. **Gargos goes before every Hydra.** T8: eight Forests, hand Gargos + Kalonian. Gargos (6), then Kalonian at {G}{G}: both are cast. Kalonian first (5) leaves 3, so Gargos cannot be cast. Under a rank-10 tie, hand order decides which happens.
4. **Fixed-cost casts (Greaves, Garruk, Kalonian) go before the X-Hydras.** See section 7: the X is sized against the whole pool, so this matters only once that limitation is fixed.

## 5. Questions for the USER
1. Should the mana doublers (Vernal Bloom, then Gauntlet / Nissa) go **first**, ahead of the fetch spells and every creature? Default: **yes**.
2. Should the fetch spells (Nature's Lore, Three Visits, Search for Tomorrow) go after the doublers and before every creature? Default: **yes**.
3. Should Gargos go before every Hydra, including Kalonian, implemented on the existing `reduces_spell_subtype` param (generic rank 8)? Default: **yes**.
4. Should the X-Hydras be offered **every X**, not just the maximum, so one can share a turn with a fetch spell, Greaves or Garruk? Default: **yes** (this widens the options, it does not prune them).
5. Should the deck get its own provider (`HydrasProvider : DeckProvider`) before the fetch spells are implemented? Default: **yes**.

## 6. Generic-order findings
* **G1. A mana doubler that is not a rock falls to rank 20, behind every creature.** Vernal Bloom (enchantment), Gauntlet (artifact, no tap ability) and Nissa (planeswalker) are all in this class. Wild Growth had the same hole until it was promoted to 5 (DecisionProviders.cpp:3143-3151). Fix: a "land tapped for mana adds extra" param ranks 5, gated, as the land-Aura one is, on the enumerator crediting the same-turn extra mana.
* **G2. A spell that puts a land onto the battlefield untapped is a same-turn accelerant**, but it ranks 20. CreatureGivingProvider ranks Crop Rotation (`tutor_land_to_battlefield`) at 5 for exactly this reason (DecisionProviders.cpp:14911). That belongs in Generic.
* **G3. No X-sink rule.** An {X} spell should be the last cast in its phase. Today there is no tier for it (see section 7).
* **G4. `CastCheapestFirstWithinTier` covers only rituals.** Two doublers at one rank fall back to hand order.

## 7. Implementation notes
* Params, by slot:
  * Slot 1-2: a new "Forest / basic tapped -> extra mana" static, carried by Bloom, Nissa and Gauntlet (Gauntlet also needs a colour choice, always green here). Rank 4 for MV 4, 5 for MV 5, because non-ritual ties have no cheapest-first key.
  * Slot 3: `tutor_land_to_battlefield`, rank 6. Search for Tomorrow can reuse the existing `suspend_time_counters` for its suspend.
  * Slot 4: `reduces_spell_subtype: "Hydra"`, `reduces_spell_subtype_amount: 4`, rank 8 (already generic).
  * Slot 5: Greaves exists. It needs rank 9 in the provider.
  * Slot 6: planeswalker, rank 9.
  * Slot 7: rank 10.
  * Slot 8: X creatures, rank 12.
* **ENGINE LIMITATION (blocks slots 1-3 and 5-7 for every X-Hydra).** The X of an {X} cast is fixed at enumeration and sized against the **whole available pool**: `max_x = (P - bmv) / pips` with `P = AvailableManaPool(state).Total()` (TurnSolver.cpp:23217; the trick path at ~24170 does the same). `GenericProvider::XCandidates` then offers **only** that maximum (DecisionProviders.cpp:3232). So an X-Hydra built on this path can never share a plan with another mana-costing cast, and its X can never include mana from a doubler or fetch spell cast earlier the same turn. The fix: a Hydras `XCandidates` returns the whole 1..max range (sized on the post-ramp pool), or the X is re-sized at cast time.
* `AvailableManaPool` must count an on-board doubler's extra {G} (a new read in the same place the land-Aura credit lives).
* Kalonian's "double counters on each creature you control" is a new attack trigger. `doubles_counters` (Doubling Season) is a replacement and does not fit.
