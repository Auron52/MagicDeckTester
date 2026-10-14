# Unpredictable Cyclone: cast-order draft (2026-10-14)

**Not ready for play.** Only 3 of 13 nonland names are in cards.json (Ponder, Remand, Seething Song), plus 16 of 21 lands (Watery Grave, Myriad Landscape x3, Xander's Lounge missing). The combo itself -- Unpredictable Cyclone, Street Wraith, Curator of Mysteries, the Eldrazi -- does not exist, and nothing implemented deals damage, so no game can be won (probe s1001 gi0: Island + Ponder T1, unwon, avg 9). The order below is **provisional, from Scryfall oracle text**, to steer the implementation.

## 1. Status: **(D)** generic tiers only
* Provider: **AntiLifegain**; cast-order-rankings.md:991-1009 holds the same three-row report, no USER ruling, no flag, no ledger. (accelerant-ordering-and-self-funding.md:32 cites this deck's Seething Song as the reason the ritual tier moved INTO GenericProvider -- it calls it "the generic provider"; AL's CastOrderRank falls through to Generic for every card here, so that holds for the order.)
* **Not in `test/regression_cases.sh`** -- no case, no GT. (filter-feed-strict.md:46 notes it plays Cascade Bluffs "but is not in the regression suite".)
* **Routing is ACCIDENTAL**: 4x Scalding Tarn carry `fetch_land_types`, which sets `anti` on its own (DecisionProviders.cpp:18522); pinned "from a direct --batch probe" (test_pirates_provider.cpp:121), a measurement, not a decision -- same misroute class as FiveColour / Pirates. **Inherits from AL today:** order unchanged (AL's Swords/alt-lifegain overrides match nothing); LIVE: the single-pick AL-tuned colour-coverage `FetchCandidates` (:4537) choosing every Tarn target, the USER's AL cleanup-discard buckets (:5411); dormant: `ClassifiesMainPhases()=true` (inert only while `uses_second_main=no`).

## 2. What plays today
| pos | card(s) | main | rule |
|---|---|---|---|
| 0 | land drop | m1 | first; a searched plan plays it before the casts (TurnSolver.cpp:39661) |
| 1 | Seething Song | m2* | rank 15, ritual (generic accelerant tier) |
| 2= | Ponder, Remand | m2* | rank 20; Ponder `[2..20]` (ladder `MTG_ORDER_RANGE` off -> inert); Ponder is ORDER-OPAQUE (`draw`), so any set holding it keeps **plan (hand) order** -- the rank does not bind. Remand is `goldfish_inert`, never cast |
| - | the other 10 names | - | not in cards.json |

m2* = report label; `uses_second_main=no`, so the phase filter never runs (TurnSolver.cpp:20388) -- every cast is m1 in practice.

## 3. Proposed order (single main: all m1; nothing here attacks the turn it lands)
| pos | card(s) | main | range | why |
|---|---|---|---|---|
| 1 | Long-Term Plans | m1 | - | "Search ... then **shuffle** and put that card third from the top": its own shuffle wipes any earlier top-setting, and the looks below reach its card (Ponder sees the top 3, Brainstorm draws it). Before every look. |
| 2 | Ponder | m1 | [2..20] | principle 1 (before land drop and ritual); before the put-back draws because its optional shuffle undoes a put-back |
| 3 | Brainstorm | m1 | - | draw 3, put 2 back: sets the top card the Cyclone exile will hit (an Eldrazi). After Ponder. |
| 4 | Brainsurge | m1 | - | draw 4, put 2 back; the LAST put-back owns the top, cheapest first otherwise |
| - | land drop | m1 | - | after the looks (they may find it). A FETCH shuffles, so it goes BEFORE the top-setters on a turn that will cycle into Cyclone (see Q3) |
| 5 | Seething Song | m1 | - | ritual: after the looks (principle 1), before Cyclone -- {R}{R}{R}{R}{R} pays {3}{R}{R} exactly. Red float only, so the looks must be paid from U lands |
| 6= | Unpredictable Cyclone, Curator of Mysteries | m1 | - | the enablers: both must be on the battlefield before any cycling activation (Curator: "whenever you cycle or discard another card, scry 1" -- it can bottom a Wraith/Curator sitting above the Eldrazi). **Tie safe:** neither reads the other at cast or ETB |
| - | cycling (Street Wraith "pay 2 life", Curator {U}, Cyclone {2}) | - | - | activations, not casts: after slot 6. With Cyclone out, cycling a creature exiles to the next creature and casts it free (Emrakul -> extra turn) |
| 7 | Street Wraith (hard-cast) | m1 | - | {3}{B}{B} 3/4: dominated by cycling it; last real cast |
| 8 | Ulamog, the Ceaseless Hunger {10}, Ulamog, the Infinite Gyre {11}, Emrakul, the Aeons Torn {15} (hard-cast) | m1 | - | biggest sinks last; the Cyclone free cast is not a plan cast |
| - | Remand, Toxic Deluge | - | - | never cast vs the passive opponent (no spell to counter; Deluge kills only our own bodies) -- rank cannot matter |

## 4. What changes and why it matters (predicted; the combo cards do not exist)
1. **Long-Term Plans before Brainstorm** (today a hand-order tie at 20). T4, Cyclone on board, 4 lands, hand LTP + Brainstorm + Street Wraith: LTP (Emrakul third) -> Brainstorm draws it, puts it back on TOP -> cycle Wraith -> Emrakul free, every time. Brainstorm first -> LTP's shuffle discards the set-up, Emrakul sits under two random cards, and the cycle hits a Wraith/Curator instead whenever one of them is a creature (~30% with ~7 of ~45 left).
2. **Ponder before the put-backs** (same tie, opaque). Ponder after Brainstorm can shuffle away the Eldrazi Brainstorm just put on top (`ReorderTopOrShuffle` shuffles when none of the three is wanted).
3. **Song after the looks** (today 15 < 20, but opaque -> hand order decides). Song first commits 3 lands to red before the looks may find the cycler that makes the Song turn worth it.
4. **Fetch vs top-setting.** Land-first (today's only shape) cracks a Tarn before any top-setting, which is the safe side by construction -- but the "Brainstorm, then crack Tarn to shuffle away the two put-back cards" line is inexpressible.

## 5. Questions for the USER
1. Long-Term Plans **before** every look (its own shuffle would wipe theirs)? Default: **yes**.
2. Ponder -> Brainstorm -> Brainsurge (the last put-back sets the top for the Cyclone exile)? Default: **yes**.
3. On a turn that cycles into Cyclone, crack a fetch **before** the top-setters (else after Brainstorm, to shuffle the put-backs away)? Default: **before**.
4. Own `CycloneProvider : DeckProvider`, routed above `anti` by Cyclone-only params (the Pirates precedent)? Default: **yes**.

## 6. Generic-order findings
* **G1. A self-shuffling tutor must precede every top-setter** (LTP; any "search, shuffle, put on top" card). Generic ties it with the looks at 20. (Creature Giving's Enlightened Tutor at 22 "after every same-turn shuffle" is the mirror case -- a pure top-setter after the shufflers.)
* **G2. Within the draw tier, a look-with-optional-shuffle (Ponder) must precede the put-backs (Brainstorm)**; the ideal-order cantrip tier gives both rank 2 -> hand order.
* **G3. Stale card note:** Scalding Tarn's oracle note says "Library order/thinning past the fetched card not modelled", but `PerformFetch` shuffles by default (`ShuffleAfterSearch`, SpellEffects.h:28580). It matters for this deck.

## 7. Implementation notes
* Ranks: LTP 1, Ponder 2, Brainstorm 3, Brainsurge 4 (distinct -- no within-tier key outside rituals), Song 15 (generic), Cyclone 17 = Curator 17, Street Wraith 25, Ulamog CH 26 / IG 27 / Emrakul 28. All four looks are ORDER-OPAQUE (`draw > 0`): the provider must set `OrderOpaqueCastsByRank() = true` or none of slots 1-4 binds.
* New params: the Cyclone cycling-draw replacement (exile until shared type, free cast, rest to bottom); Street Wraith's life-cost cycling; Curator's cycle/discard scry; draw-N-put-back-2; LTP tutor -> shuffle -> third from top (do not use `tutor_to_top`, which sets `anti`); Emrakul's cast trigger "take an extra turn" (**no extra-turn support exists** -- no such param in cards.json), annihilator, the Ulamogs' cast triggers; Myriad Landscape's sac-search-two-basics (shuffles).
* **ENGINE LIMITATION:** cycling is a `DigDraw`-class activation (TurnSolver.h:64), outside the cast rank; it must be sequenced after Cyclone resolves, and the exile + free cast is new information (a breakpoint). The land-first rule (TurnSolver.cpp:39661) blocks the conditional fetch position in Q3.
