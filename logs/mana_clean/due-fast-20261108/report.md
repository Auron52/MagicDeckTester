# Mana-clean gate `due-fast-20261108`

Mana-clean gate run: 11 decks, games (seed 30000000 + game index) anti_lifegain 85000-89999, bruna 41000-45999, creature_giving 55000-59999, dragons 71000-75999, dragonstorm 22000-26999, fungus 45000-49999, galadriel 71000-75999, midrange_burn 100000-104999, selesnyalifegain 97000-101999, shadowborn_apostle 5000-9999, stompysurprise 31000-35999; src `3bda5c647e7c`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | DOMINATED-LIFE | ORACLE-GAP | UNMODELED | CAPPED | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| anti_lifegain | 5000 | issues | 4007 | 0 | 0 | 0 | 0 | 0 | 4005 | 2 | 0 | 0 | 523 | 0 | 2795 | 0 |
| bruna | 5000 | issues | 3524 | 55 | 1797 | 613 | 777 | 0 | 87 | 177 | 18 | 8144 | 486 | 2670 | 7468 | 0 |
| creature_giving | 5000 | issues | 284 | 253 | 7 | 0 | 19 | 24 | 4 | 1 | 0 | 0 | 0 | 0 | 6686 | 28 |
| dragons | 5000 | issues | 1189 | 0 | 958 | 0 | 0 | 0 | 231 | 0 | 0 | 5124 | 34 | 220 | 10954 | 0 |
| dragonstorm | 5000 | issues | 2781 | 246 | 677 | 4 | 0 | 0 | 22 | 1808 | 24 | 7474 | 66 | 1161 | 15991 | 0 |
| fungus | 5000 | issues | 1571 | 206 | 1 | 1 | 924 | 964 | 82 | 17 | 340 | 1779 | 9176 | 252 | 3756 | 0 |
| galadriel | 5000 | issues | 26372 | 2 | 21 | 0 | 0 | 0 | 0 | 25464 | 885 | 843 | 59 | 59 | 354 | 0 |
| midrange_burn | 5000 | issues | 10503 | 501 | 1020 | 8275 | 0 | 220 | 707 | 0 | 0 | 6555 | 174 | 1413 | 1304 | 65 |
| selesnyalifegain | 5000 | issues | 9425 | 3 | 1565 | 190 | 3558 | 5960 | 663 | 1007 | 2439 | 1659 | 897 | 145 | 29249 | 620 |
| shadowborn_apostle | 5000 | issues | 11991 | 19 | 971 | 2949 | 0 | 0 | 0 | 8052 | 0 | 20723 | 894 | 902 | 1007 | 0 |
| stompysurprise | 5000 | issues | 6010 | 43 | 1740 | 173 | 4053 | 0 | 1 | 0 | 0 | 7737 | 897 | 206 | 67899 | 31 |

**MISS = 28346, ORACLE-GAP = 5802.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=454190 plans=3421993 skipped=0 applies=860702502 frontiers=579948598 pruned=116867045 options=497178484 engine_drops=98789 gate_rejects=7137516(checked 7079807, skipped 57709) MISS=38789 ORACLE-GAP=5802 UNMODELED=36528 CAPPED=3706 ms=37332886 ===
=== MANA ORACLE classes: ORACLE-GAP=5802 ENUM-GAP=1328 APPLY-DROP=8757 APPLY-DROP-ORDER=12205 PHANTOM=60038 DOMINATED-CREATURE=9331 DOMINATED-LIFE=7168 DOMINATED-LAND=147463 CONTINUATION-DIFFERS=13206 DROP-COVERED=7028 UNMODELED=36528 CAPPED=3706 HIDDEN-INFO=744 ===
=== MANA ORACLE enum-gap rejections: noop=3436 hidden=13745 host_blind=71 cont_land=36 ===
=== MANA ORACLE capped-plan escalation: 13837 retried, 10131 finished ===
=== MANA ORACLE unmodeled: 1524 x engine tapped a mana source outside any payment ===
=== MANA ORACLE unmodeled: 382 x floating pool too large to enumerate set-asides ===
=== MANA ORACLE unmodeled: 376448 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 137497 x payment enumeration capped ===
=== MANA ORACLE unmodeled: 79647 x scripted payment refused: scripted payment did not pay {1}{U}{G} ===
=== MANA ORACLE unmodeled: 48 x scripted payment refused: scripted payment did not pay {2}{G} ===
=== MANA ORACLE unmodeled: 123 x scripted payment refused: scripted payment did not pay {3}{W}{W}{U} ===
=== MANA ORACLE unmodeled: 26 x scripted payment refused: scripted payment did not pay {G} ===
=== MANA ORACLE unmodeled: 24 x scripted payment refused: scripted payment did not pay {R}{R} ===
=== MANA ORACLE unmodeled: 50332 x scripted payment refused: set-aside exceeds the floating pool ===
=== MANA ORACLE unmodeled: 9032 x too many actions to try every order ===
=== MANA ORACLE rollback census: lost=22290 depth0=876 depth1=6333 depth2=1333 depth3+=192 inconsistent=0 capped=20 unfaithful=0 no-failed-payment=0 unmodeled=3 reorder=12205 off-menu=1328 need_in_plan=8041 need_later=716 pay_rollback_rescued=573/8757 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | ORACLE-GAP | UNMODELED | CAPPED |
|---|---|---|---|---|---|---|---|---|---|
| anti_lifegain | 37238 | 140605 | 0 | 0 | 0 | 0 | 4005 | 2 | 0 |
| bruna | 43245 | 376770 | 55 | 1797 | 613 | 777 | 87 | 177 | 18 |
| creature_giving | 41206 | 210742 | 253 | 7 | 0 | 19 | 4 | 1 | 0 |
| dragons | 50428 | 360292 | 0 | 958 | 0 | 0 | 231 | 0 | 0 |
| dragonstorm | 38718 | 190189 | 246 | 677 | 4 | 0 | 22 | 1808 | 24 |
| fungus | 40530 | 229211 | 206 | 1 | 1 | 924 | 82 | 17 | 340 |
| galadriel | 44423 | 186817 | 2 | 21 | 0 | 0 | 0 | 5000 | 885 |
| midrange_burn | 38919 | 460219 | 501 | 1020 | 5000 | 0 | 707 | 0 | 0 |
| selesnyalifegain | 40856 | 325181 | 3 | 1565 | 190 | 3558 | 663 | 1007 | 2439 |
| shadowborn_apostle | 43033 | 395619 | 19 | 971 | 2949 | 0 | 0 | 5000 | 0 |
| stompysurprise | 35594 | 546348 | 43 | 1740 | 173 | 4053 | 1 | 0 | 0 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth0 | depth1 | depth2 | depth3+ | capped | unmodeled | reorder | off-menu | rescued | need-later |
|---|---|---|---|---|---|---|---|---|---|---|
| bruna | 0 | 1498 | 200 | 99 | 0 | 0 | 613 | 55 | 445 | 368 |
| creature_giving | 0 | 0 | 0 | 7 | 0 | 0 | 0 | 253 | 0 | 0 |
| dragons | 0 | 791 | 157 | 10 | 0 | 0 | 0 | 0 | 72 | 55 |
| dragonstorm | 550 | 72 | 38 | 17 | 0 | 0 | 4 | 246 | 0 | 21 |
| fungus | 0 | 1 | 0 | 0 | 0 | 0 | 1 | 206 | 0 | 0 |
| galadriel | 0 | 21 | 0 | 0 | 0 | 0 | 0 | 2 | 0 | 0 |
| midrange_burn | 0 | 962 | 58 | 0 | 0 | 0 | 5000 | 501 | 0 | 101 |
| selesnyalifegain | 0 | 1090 | 418 | 37 | 20 | 0 | 190 | 3 | 56 | 53 |
| shadowborn_apostle | 326 | 447 | 187 | 8 | 0 | 3 | 2949 | 19 | 0 | 78 |
| stompysurprise | 0 | 1451 | 275 | 14 | 0 | 0 | 173 | 43 | 0 | 40 |

## ENUM-GAP (1328 flags, 1062 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **midrange_burn** x15 -- T4 m1 `{no land + cast:Chandra's Incinerator + cast:Fireblast + cast:Lightning Bolt}`  
  a mana gate kept this plan off the menu, but casting Lightning Bolt > Fireblast > Chandra's Incinerator pays every action with pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)]  
  board: board=[Mountain#29, Grim Lavamancer#16, Mountain#30] hand=[Price of Progress, Chandra's Incinerator, Fireblast, Lightning Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100052 --game-index 100052`
- **creature_giving** x10 -- T3 m2 `{no land + cast:Crop Rotation + cast:Crop Rotation}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forbidden Orchard#27:G] ; pay2[Forbidden Orchard#26:G]  
  board: board=[Forbidden Orchard#24(T), Soul Warden#39(T), Varchild's War-Riders#53(T), Forest#28(T)(new), Varchild's War-Riders#55(new), Forbidden Orchard#27(new)] hand=[Crop Rotation, Crop Rotation] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30055370 --game-index 55370`
- **midrange_burn** x10 -- T3 m1 `{land=Mountain + cast:Chandra's Incinerator + cast:Chain Lightning + cast:Skewer the Critics}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Mountain#31:R] ; pay2[Mountain#29:R] ; pay3[Mountain#30:R]  
  board: board=[Mountain#29, Monastery Swiftspear#25, Mountain#30] hand=[Chandra's Incinerator, Chandra's Incinerator, Chain Lightning, Mountain, Skewer the Critics] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100034 --game-index 100034`
- **midrange_burn** x9 -- T3 m1 `{no land + cast:Fireblast + cast:Chandra's Incinerator + cast:Lightning Bolt}`  
  a mana gate kept this plan off the menu, but casting Lightning Bolt > Fireblast > Chandra's Incinerator pays every action with pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)]  
  board: board=[Mountain#29, Monastery Swiftspear#25, Mountain#30] hand=[Chandra, Torch of Defiance, Rift Bolt, Fireblast, Chandra's Incinerator, Rift Bolt, Lightning Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100185 --game-index 100185`
- **midrange_burn** x9 -- T3 m1 `{no land + cast:Chandra's Incinerator + cast:Fireblast + cast:Chain Lightning}`  
  a mana gate kept this plan off the menu, but casting Chain Lightning > Fireblast > Chandra's Incinerator pays every action with pay1[Mountain#31:R Mountain#44:R] ; pay2[(float only)]  
  board: board=[Mountain#31, Mountain#44] hand=[Chandra's Incinerator, Fireblast, Skewer the Critics, Chain Lightning] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100505 --game-index 100505`
- **midrange_burn** x9 -- T3 m1 `{land=Mountain + cast:Lightning Bolt + cast:Chandra's Incinerator + cast:Skewer the Critics}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Mountain#40:R] ; pay2[Mountain#29:R] ; pay3[Mountain#46:R]  
  board: board=[Mountain#29, Mountain#46] hand=[Lightning Bolt, Light Up the Stage, Chandra's Incinerator, Mountain, Skewer the Critics] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100864 --game-index 100864`
- **midrange_burn** x8 -- T3 m1 `{land=Mountain + cast:Chandra's Incinerator + cast:Fireblast + cast:Searing Blaze}`  
  a mana gate kept this plan off the menu, but casting Searing Blaze > Fireblast > Chandra's Incinerator pays every action with pay1[Mountain#39:R Mountain#44:R] ; pay2[Fiery Islet#11:R]  
  board: board=[Fiery Islet#11, Mountain#39] hand=[Chandra's Incinerator, Fireblast, Mountain, Chandra's Incinerator, Searing Blaze] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100004 --game-index 100004`
- **midrange_burn** x8 -- T4 m1 `{no land + cast:Chandra's Incinerator + cast:Chain Lightning + cast:Fireblast}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)]  
  board: board=[Mountain#29, Monastery Swiftspear#25, Grim Lavamancer#16, Mountain#30] hand=[Chandra, Torch of Defiance, Chandra, Torch of Defiance, Chandra's Incinerator, Chain Lightning, Fireblast] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100817 --game-index 100817`
- **midrange_burn** x8 -- T3 m1 `{no land + cast:Fireblast + cast:Chandra's Incinerator + cast:Skewer the Critics}`  
  a mana gate kept this plan off the menu, but casting Skewer the Critics > Fireblast > Chandra's Incinerator pays every action with pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)]  
  board: board=[Mountain#29, Mountain#30] hand=[Fireblast, Chandra's Incinerator, Mountain, Skewer the Critics, Grim Lavamancer] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30101470 --game-index 101470`
- **midrange_burn** x7 -- T3 m1 `{no land + cast:Fireblast + cast:Chandra's Incinerator + cast:Chain Lightning}`  
  a mana gate kept this plan off the menu, but casting Chain Lightning > Fireblast > Chandra's Incinerator pays every action with pay1[Mountain#40:R Mountain#46:R] ; pay2[(float only)]  
  board: board=[Mountain#40, Mountain#46] hand=[Fireblast, Chandra's Incinerator, Chain Lightning, Skewer the Critics, Mountain, Skewer the Critics] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30101157 --game-index 101157`
- **midrange_burn** x6 -- T3 m1 `{no land + cast:Chain Lightning + cast:Fireblast + cast:Chandra's Incinerator}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)]  
  board: board=[Mountain#29, Monastery Swiftspear#25, Mountain#30, Monastery Swiftspear#26] hand=[Chain Lightning, Fireblast, Chandra, Torch of Defiance, Chandra's Incinerator, Chain Lightning] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100103 --game-index 100103`
- **midrange_burn** x6 -- T3 m1 `{land=Mountain + cast:Skewer the Critics + cast:Chandra's Incinerator + cast:Chain Lightning}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Mountain#31:R] ; pay2[Mountain#29:R] ; pay3[Mountain#30:R]  
  board: board=[Mountain#29, Mountain#30] hand=[Skewer the Critics, Chandra's Incinerator, Chain Lightning, Mountain, Mountain, Lightning Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100367 --game-index 100367`
- ... 1050 more signatures in flags.jsonl

## APPLY-DROP (8757 flags, 2668 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **stompysurprise** x54 -- T5 m1 `{land=Forest + cast:Natural Order + cast:Natural Order + cast:Elvish Archdruid}`  
  engine dropped [cast Natural Order]; every action pays with pay1[Forest#17:G Elvish Mystic#13:G Forest#18:G] ; pay2[Elvish Archdruid#10:G Priest of Titania#43:G] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Elvish Archdruid#10:G Priest of Titania#43:G] ; pay3[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#17, Elvish Mystic#13, Priest of Titania#43, Elvish Archdruid#10] hand=[Terastodon, Natural Order, Natural Order, Elvish Archdruid, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30033224 --game-index 33224`
- **shadowborn_apostle** x48 -- T4 m1 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; every action pays with pay1[Swamp#40:B] ; pay2[(float only)] ; pay3[Lotus Bloom#17:W keep-floating {W}{B}{B}{B}] ; pay4[(float only)] ; pay5[keep-floating {W}] || ROLLBACK depth0: keep the engine's first 4 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[(the engine's own)] ; pay4[(the engine's own)] ; pay5[Lotus Bloom#17:B] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Swamp#40, Shadowborn Apostle#21, Lotus Bloom#17(new)] hand=[Diabolic Intent, Culling the Weak, Shadowborn Apostle, Thrumming Stone, Diabolic Intent, Shadowborn Apostle, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30009550 --game-index 9550`
- **midrange_burn** x46 -- T3 m1 `{no land + cast:Fireblast + cast:Light Up the Stage + suspend:Rift Bolt}`  
  engine dropped [cast Light Up the Stage]; every action pays with pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mountain#29, Monastery Swiftspear#25, Mountain#30] hand=[Fireblast, Skewer the Critics, Light Up the Stage, Rift Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30102106 --game-index 102106`
- **midrange_burn** x42 -- T3 m1 `{no land + cast:Light Up the Stage + suspend:Rift Bolt + cast:Fireblast}`  
  engine dropped [cast Light Up the Stage]; every action pays with pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mountain#29, Grim Lavamancer#16, Mountain#30] hand=[Chandra, Torch of Defiance, Chandra's Incinerator, Light Up the Stage, Rift Bolt, Fireblast] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100066 --game-index 100066`
- **stompysurprise** x39 -- T4 m1 `{no land [ordered] + cast:Elvish Mystic + cast:Natural Order + cast:Worldspine Wurm}`  
  engine dropped [cast Worldspine Wurm]; every action pays with pay1[Llanowar Elves#34:G] ; pay2[Elvish Archdruid#7:G Priest of Titania#42:G] ; pay3[Turntimber, Serpentine Wood#49:G Fyndhorn Elves#28:G Elvish Mystic#11:G], reaching a board the menu's other order of these actions does not (Priest of Titania#42 only in engine; Elvish Mystic#11 pump differs; Llanowar Elves#34 pump differs; Elvish Archdruid#7 pump differs; Elvish Mystic#12 pump differs; Worldspine Wurm#60 pump differs; Craterhoof Behemoth#4 pump differs; Fyndhorn Elves#28 only in oracle; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) a  
  board: board=[Turntimber, Serpentine Wood#49, Fyndhorn Elves#28, Priest of Titania#42, Elvish Mystic#11, Llanowar Elves#34, Elvish Archdruid#7] hand=[World War Hulk, Ghalta, Stampede Tyrant, Natural Order, Worldspine Wurm, Elvish Mystic] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30035209 --game-index 35209`
- **stompysurprise** x39 -- T4 m1 `{no land [ordered] + cast:Elvish Archdruid + cast:Natural Order + cast:World War Hulk}`  
  engine dropped [cast World War Hulk]; every action pays with pay1[Forest#25:G Forest#15:G Elvish Mystic#11:G] ; pay2[Elvish Archdruid#8:G Priest of Titania#44:G] ; pay3[Forest#20:G], reaching a board the menu's other order of these actions does not (Elvish Mystic#11 pump differs; Elvish Archdruid#8 pump differs; Elvish Archdruid#10 pump differs; Vaultborn Tyrant#53 pump differs; Craterhoof Behemoth#5 pump differs; life engine 26 oracle 23; hand size differs; library size differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Elvish Archdruid  
  board: board=[Forest#25, Forest#15, Priest of Titania#44, Forest#20, Elvish Mystic#11, Elvish Archdruid#8] hand=[Natural Order, Elvish Archdruid, World War Hulk, Vaultborn Tyrant] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30035641 --game-index 35641`
- **stompysurprise** x38 -- T4 m1 `{no land [ordered] + cast:Elvish Archdruid + cast:Natural Order + cast:Ghalta, Stampede Tyrant + cast:Worldly Tutor}`  
  engine dropped [cast Worldly Tutor]; every action pays with pay1[Elvish Mystic#11:G Elvish Mystic#12:G Fyndhorn Elves#28:G] ; pay2[Priest of Titania#42:G Priest of Titania#43:G] ; pay3[(float only)] ; pay4[Turntimber, Serpentine Wood#49:G], reaching a board the menu's other order of these actions does not (Elvish Mystic#12 pump differs; Fyndhorn Elves#28 pump differs; Priest of Titania#42 pump differs; Priest of Titania#43 pump differs; Elvish Archdruid#7 pump differs; Ghalta, Stampede Tyrant#32 pump differs; Craterhoof Behemoth#4 pump differs; float engine - oracle -) || ROLLBACK depth2: keep the engine's first 1 mana decision, re-make the rest (decision #4 is the payment that failed) and e  
  board: board=[Turntimber, Serpentine Wood#49, Elvish Mystic#11, Elvish Mystic#12, Fyndhorn Elves#28, Priest of Titania#42, Priest of Titania#43] hand=[Natural Order, Ghalta, Stampede Tyrant, World War Hulk, Elvish Archdruid, Worldly Tutor] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30032025 --game-index 32025`
- **midrange_burn** x33 -- T3 m1 `{no land + cast:Light Up the Stage + cast:Fireblast + suspend:Rift Bolt}`  
  engine dropped [cast Light Up the Stage]; every action pays with pay1[Mountain#45:R Mountain#44:R] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mountain#45:R Mountain#44:R] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mountain#45, Monastery Swiftspear#26, Mountain#44] hand=[Light Up the Stage, Chain Lightning, Fireblast, Rift Bolt, Chandra, Torch of Defiance] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100839 --game-index 100839`
- **bruna** x32 -- T2 m1 `{no land + cast:Arcanum Wings@3 + cast:Sol Ring}`  
  engine dropped [cast Arcanum Wings]; every action pays with pay1[Avacyn's Pilgrim#3:W] ; pay2[Botanical Sanctum#15:U Sol Ring#55:C] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Avacyn's Pilgrim#3:W] ; pay2[Botanical Sanctum#15:U Sol Ring#55:C]; MTG_PAY_ROLLBACK (forced on) recovers it  
  board: board=[Botanical Sanctum#15, Avacyn's Pilgrim#3] hand=[Mythic Proportions, Arcanum Wings, Eldrazi Conscription, Botanical Sanctum, Sol Ring] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Bruna/Bruna.cod' --profile 'decks/Bruna/Bruna.profile.json' --games 1 --seed 30041632 --game-index 41632`
- **midrange_burn** x32 -- T4 m1 `{no land + cast:Chain Lightning + cast:Skewer the Critics + cast:Fireblast}`  
  engine dropped [cast Skewer the Critics]; every action pays with pay1[Mountain#41:R Mountain#34:R] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mountain#41:R Mountain#34:R] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mountain#41, Mountain#34, Chandra's Incinerator#6] hand=[Chain Lightning, Skewer the Critics, Fireblast] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100672 --game-index 100672`
- **shadowborn_apostle** x32 -- T4 m1 `{land=Swamp + cast:Culling the Weak + cast:Thrumming Stone + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1) + sac:Lotus Bloom}`  
  engine dropped [cast Shadowborn Apostle]; every action pays with pay1[Swamp#49:B] ; pay2[keep-floating {B}] ; pay3[(float only)] ; pay4[(float only)] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[keep-floating {B}] ; pay3[(float only)] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Swamp#43, Shadowborn Apostle#29, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Shadowborn Apostle, Diabolic Intent, Thrumming Stone, Shadowborn Apostle, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30009193 --game-index 9193`
- **midrange_burn** x31 -- T3 m1 `{no land + cast:Fireblast + cast:Light Up the Stage + suspend:Rift Bolt}`  
  engine dropped [cast Light Up the Stage]; every action pays with pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)] ; pay3[Mountain#31:R] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)] ; pay3[Mountain#31:R]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mountain#29, Monastery Swiftspear#25, Mountain#30] hand=[Fireblast, Skewer the Critics, Light Up the Stage, Rift Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30102106 --game-index 102106`
- ... 2656 more signatures in flags.jsonl

## APPLY-DROP-ORDER (8930 flags, 1298 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **midrange_burn** x186 -- T3 m1 `{land=Mountain + cast:Fireblast + cast:Searing Blaze}`  
  engine dropped [cast Searing Blaze]; the order Searing Blaze > Fireblast pays every action with pay1[Mountain#39:R Mountain#44:R]  
  board: board=[Fiery Islet#11, Mountain#39] hand=[Chandra's Incinerator, Fireblast, Mountain, Chandra's Incinerator, Searing Blaze] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100004 --game-index 100004`
- **midrange_burn** x186 -- T3 m1 `{no land + cast:Fireblast + cast:Searing Blaze}`  
  engine dropped [cast Searing Blaze]; the order Searing Blaze > Fireblast pays every action with pay1[Mountain#29:R Mountain#30:R]  
  board: board=[Mountain#29, Grim Lavamancer#16, Mountain#30] hand=[Rift Bolt, Fireblast, Searing Blaze, Chandra's Incinerator, Rift Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100065 --game-index 100065`
- **shadowborn_apostle** x166 -- T4 m1 `{land=Swamp + cast:Shadowborn Apostle + suspend:Profane Tutor + sac:Lake of the Dead}`  
  engine dropped [cast Shadowborn Apostle]; the order Shadowborn Apostle > sac:Lake of the Dead@14 > suspend:Profane Tutor pays every action with pay1[(float only)] ; pay2[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#58, Shadowborn Apostle#25, Shadowborn Apostle#24, Shadowborn Apostle#33, Shadowborn Apostle#27, Shadowborn Apostle#37, Shadowborn Apostle#32, Shadowborn Apostle#35, Shadowborn Apostle#31, Shadowborn Apostle#22] hand=[Profane Tutor, Shadowborn Apostle, Swamp, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30005048 --game-index 5048`
- **shadowborn_apostle** x160 -- T3 m2 `{no land + cast:Shadowborn Apostle + suspend:Profane Tutor + sac:Lake of the Dead}`  
  engine dropped [cast Shadowborn Apostle]; the order Shadowborn Apostle > sac:Lake of the Dead@14 > suspend:Profane Tutor pays every action with pay1[(float only)] ; pay2[(float only)]  
  board: board=[Lake of the Dead#14, Swamp#41(new)] hand=[Thrumming Stone, Shadowborn Apostle, Sire of Insanity, Thrumming Stone, Dark Ritual, Profane Tutor] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30005017 --game-index 5017`
- **midrange_burn** x141 -- T3 m1 `{no land + cast:Fireblast + cast:Lightning Bolt}`  
  engine dropped [cast Lightning Bolt]; the order Lightning Bolt > Fireblast pays every action with pay1[Mountain#29:R]  
  board: board=[Mountain#29, Monastery Swiftspear#25, Mountain#30, Monastery Swiftspear#26] hand=[Skewer the Critics, Fireblast, Skewer the Critics, Light Up the Stage, Lightning Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100015 --game-index 100015`
- **midrange_burn** x128 -- T2 m1 `{land=Mountain + cast:Fireblast + cast:Magma Jet}`  
  engine dropped [cast Magma Jet]; the order Magma Jet > Fireblast pays every action with pay1[Mountain#29:R Mountain#30:R]  
  board: board=[Mountain#29, Monastery Swiftspear#25] hand=[Fireblast, Magma Jet, Chain Lightning, Light Up the Stage, Skewer the Critics, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100020 --game-index 100020`
- **midrange_burn** x126 -- T2 m1 `{land=Mountain + cast:Fireblast + cast:Lightning Bolt}`  
  engine dropped [cast Lightning Bolt]; the order Lightning Bolt > Fireblast pays every action with pay1[Mountain#30:R]  
  board: board=[Mountain#29, Monastery Swiftspear#25] hand=[Fireblast, Mountain, Mountain, Searing Blaze, Lightning Bolt, Skewer the Critics, Fiery Islet] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100073 --game-index 100073`
- **midrange_burn** x120 -- T2 m1 `{land=Mountain + cast:Fireblast + cast:Chain Lightning}`  
  engine dropped [cast Chain Lightning]; the order Chain Lightning > Fireblast pays every action with pay1[Mountain#30:R]  
  board: board=[Mountain#29, Monastery Swiftspear#25] hand=[Fireblast, Magma Jet, Chain Lightning, Light Up the Stage, Skewer the Critics, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100020 --game-index 100020`
- **midrange_burn** x113 -- T3 m1 `{land=Mountain + cast:Fireblast + cast:Price of Progress}`  
  engine dropped [cast Price of Progress]; the order Price of Progress > Fireblast pays every action with pay1[Mountain#29:R Mountain#30:R]  
  board: board=[Fiery Islet#10, Mountain#29] hand=[Fireblast, Price of Progress, Skewer the Critics, Mountain] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100016 --game-index 100016`
- **midrange_burn** x112 -- T3 m1 `{no land + cast:Fireblast + cast:Chain Lightning}`  
  engine dropped [cast Chain Lightning]; the order Chain Lightning > Fireblast pays every action with pay1[Mountain#29:R]  
  board: board=[Mountain#29, Monastery Swiftspear#25, Mountain#30] hand=[Fireblast, Chandra, Torch of Defiance, Searing Blaze, Lightning Bolt, Chain Lightning] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100143 --game-index 100143`
- **midrange_burn** x102 -- T3 m1 `{no land + cast:Fireblast + cast:Price of Progress}`  
  engine dropped [cast Price of Progress]; the order Price of Progress > Fireblast pays every action with pay1[Mountain#29:R Mountain#30:R]  
  board: board=[Mountain#29, Monastery Swiftspear#25, Mountain#30] hand=[Searing Blaze, Chain Lightning, Fireblast, Price of Progress] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100028 --game-index 100028`
- **midrange_burn** x101 -- T3 m1 `{no land + cast:Fireblast + cast:Magma Jet}`  
  engine dropped [cast Magma Jet]; the order Magma Jet > Fireblast pays every action with pay1[Mountain#29:R Mountain#30:R]  
  board: board=[Mountain#29, Monastery Swiftspear#25, Mountain#30] hand=[Fireblast, Magma Jet, Light Up the Stage, Fiery Islet] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100020 --game-index 100020`
- ... 1286 more signatures in flags.jsonl

## DOMINATED-CREATURE (9331 flags, 3755 signatures)

_the engine's payment taps a creature that could attack, where another payment keeps it untapped and loses nothing._

- **stompysurprise** x51 -- T4 m1 `{no land + cast:World War Hulk + cast:Craterhoof Behemoth}`  
  keeps Elvish Archdruid untapped (attacker); keeps Forest untapped x3 -- by paying pay1[Elvish Archdruid#7:G Priest of Titania#42:G]; the engine ended board=[Forest#15(T), Fyndhorn Elves#28, Forest#16(T), Elvish Archdruid#7(T), Forest#17(T), Priest of Titania#42(T), Elvish Archdruid#8(T), World War Hulk#54(new), Ghalta, Stampede Tyrant#32(new), Craterhoof Behemoth#4(new), Terastodon#47(new)] hand=[] float={G}{G}{G}{G}{G}{*x5} life=20  
  board: board=[Forest#15, Fyndhorn Elves#28, Forest#16, Elvish Archdruid#7, Forest#17, Priest of Titania#42, Elvish Archdruid#8] hand=[World War Hulk, Ghalta, Stampede Tyrant, Terastodon, Craterhoof Behemoth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30032945 --game-index 32945`
- **stompysurprise** x43 -- T4 m1 `{no land [ordered] + cast:Llanowar Elves + cast:Call of the Wild + cast:World War Hulk + cast:Natural Order}`  
  keeps Elvish Mystic untapped (attacker); keeps Turntimber, Serpentine Wood untapped -- by paying pay1[Elvish Mystic#11:G] ; pay2[Elvish Archdruid#7:G] ; pay3[Priest of Titania#42:G] ; pay4[Fyndhorn Elves#28:G]; the engine ended board=[Turntimber, Serpentine Wood#49(T), Priest of Titania#42(T), Elvish Mystic#12(T), Fyndhorn Elves#28(T), Elvish Archdruid#7(T), Llanowar Elves#34(new), Call of the Wild#2(new), World War Hulk#54(new), Craterhoof Behemoth#4(new), Craterhoof Behemoth#5(new)] hand=[] float={G}{G} life=17  
  board: board=[Turntimber, Serpentine Wood#49, Elvish Mystic#11, Priest of Titania#42, Elvish Mystic#12, Fyndhorn Elves#28, Elvish Archdruid#7] hand=[World War Hulk, Craterhoof Behemoth, Natural Order, Call of the Wild, Llanowar Elves] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30031403 --game-index 31403`
- **stompysurprise** x38 -- T4 m1 `{no land [ordered] + cast:Elvish Archdruid + cast:Terastodon + cast:Natural Order}`  
  keeps Forest untapped; keeps Llanowar Elves untapped (attacker) -- by paying pay1[Forest#15:G Llanowar Elves#34:G Fyndhorn Elves#28:G] ; pay2[Priest of Titania#42:G Priest of Titania#43:G] ; pay3[(float only)]; the engine ended board=[Forest#15(T), Fyndhorn Elves#28(T), Llanowar Elves#35(T), Priest of Titania#42(T), Priest of Titania#43(T), Elvish Archdruid#7(new), Terastodon#47(new), 3/3 Elephant Token#1000(new), Craterhoof Behemoth#4(new)] hand=[World War Hulk] float={G}{G} life=20  
  board: board=[Forest#15, Llanowar Elves#34, Forest#16, Fyndhorn Elves#28, Llanowar Elves#35, Priest of Titania#42, Priest of Titania#43] hand=[Terastodon, World War Hulk, Natural Order, Elvish Archdruid] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30032509 --game-index 32509`
- **stompysurprise** x37 -- T4 m1 `{no land + cast:Craterhoof Behemoth + cast:Natural Order + cast:Llanowar Elves}`  
  keeps Elvish Mystic untapped (attacker) x2; keeps Turntimber, Serpentine Wood untapped -- by paying pay1[Fyndhorn Elves#28:G] ; pay2[Elvish Archdruid#7:G Priest of Titania#42:G] ; pay3[(float only)]; the engine ended board=[Turntimber, Serpentine Wood#49(T), Elvish Mystic#11(T), Elvish Mystic#12(T), Fyndhorn Elves#28(T), Elvish Archdruid#7(T), Llanowar Elves#34(new), Craterhoof Behemoth#5(new), Craterhoof Behemoth#4(new)] hand=[World War Hulk, Call of the Wild] float={G}{G} life=17  
  board: board=[Turntimber, Serpentine Wood#49, Elvish Mystic#11, Priest of Titania#42, Elvish Mystic#12, Fyndhorn Elves#28, Elvish Archdruid#7] hand=[World War Hulk, Craterhoof Behemoth, Natural Order, Call of the Wild, Llanowar Elves] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30031403 --game-index 31403`
- **stompysurprise** x37 -- T4 m1 `{no land + cast:Natural Order + cast:Priest of Titania + cast:Elvish Archdruid}`  
  keeps Fyndhorn Elves untapped (attacker); keeps Turntimber, Serpentine Wood untapped -- by paying pay1[Elvish Mystic#11:G Elvish Mystic#12:G] ; pay2[Elvish Archdruid#7:G] ; pay3[Llanowar Elves#34:G]; the engine ended board=[Turntimber, Serpentine Wood#49(T), Fyndhorn Elves#28(T), Llanowar Elves#34(T), Elvish Mystic#12(T), Elvish Archdruid#7(T), Priest of Titania#42(new), Elvish Archdruid#8(new), Craterhoof Behemoth#4(new)] hand=[World War Hulk, Call of the Wild] float={G}{G}{G} life=17  
  board: board=[Turntimber, Serpentine Wood#49, Fyndhorn Elves#28, Elvish Mystic#11, Llanowar Elves#34, Elvish Mystic#12, Elvish Archdruid#7] hand=[Natural Order, World War Hulk, Priest of Titania, Call of the Wild, Elvish Archdruid] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30033135 --game-index 33135`
- **stompysurprise** x37 -- T4 m1 `{no land + cast:Priest of Titania + cast:Natural Order + cast:Elvish Archdruid}`  
  keeps Forest untapped; keeps Fyndhorn Elves untapped (attacker) -- by paying pay1[Fyndhorn Elves#28:G Fyndhorn Elves#29:G] ; pay2[Elvish Archdruid#7:G] ; pay3[Elvish Mystic#11:G]; the engine ended board=[Forest#15(T), Elvish Mystic#11(T), Fyndhorn Elves#29(T), Fyndhorn Elves#30(T), Elvish Archdruid#7(T), Priest of Titania#42(new), Elvish Archdruid#8(new), Craterhoof Behemoth#4(new)] hand=[World War Hulk, Worldly Tutor] float={G}{G}{G} life=20  
  board: board=[Forest#15, Fyndhorn Elves#28, Elvish Mystic#11, Fyndhorn Elves#29, Fyndhorn Elves#30, Elvish Archdruid#7] hand=[World War Hulk, Worldly Tutor, Priest of Titania, Natural Order, Elvish Archdruid] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30033395 --game-index 33395`
- **stompysurprise** x35 -- T4 m1 `{no land + cast:Turntimber Symbiosis + cast:Llanowar Elves + revealtop:Call of the Wild(x1)}`  
  keeps Elvish Mystic untapped (attacker); keeps Turntimber, Serpentine Wood untapped -- by paying pay1[Forest#26:G] ; pay2[Priest of Titania#44:G Priest of Titania#45:G] ; pay3[Fyndhorn Elves#31:G]; the engine ended board=[Turntimber, Serpentine Wood#51(T), Elvish Mystic#11(T), Forest#26(T), Fyndhorn Elves#31(T), Priest of Titania#44(T), Priest of Titania#45(T), Call of the Wild#2, Llanowar Elves#36(new), Llanowar Elves#35(new)] hand=[] float=- life=17  
  board: board=[Turntimber, Serpentine Wood#51, Elvish Mystic#11, Forest#26, Fyndhorn Elves#31, Priest of Titania#44, Priest of Titania#45, Call of the Wild#2] hand=[Turntimber Symbiosis, Llanowar Elves] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30034873 --game-index 34873`
- **stompysurprise** x35 -- T4 m1 `{no land + cast:Elvish Archdruid + cast:Turntimber Symbiosis}`  
  keeps Elvish Mystic untapped (attacker); keeps Forest untapped -- by paying pay1[Priest of Titania#42:G Fyndhorn Elves#29:G] ; pay2[Priest of Titania#44:G]; the engine ended board=[Forest#16(T), Elvish Mystic#12(T), Priest of Titania#42(T), Fyndhorn Elves#29(T), Priest of Titania#44(T), Elvish Archdruid#9(new), Craterhoof Behemoth#5(new)] hand=[Call of the Wild, Worldly Tutor] float={G}{G}{G} life=20  
  board: board=[Forest#16, Elvish Mystic#12, Priest of Titania#42, Fyndhorn Elves#29, Priest of Titania#44] hand=[Call of the Wild, Worldly Tutor, Elvish Archdruid, Turntimber Symbiosis] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30034878 --game-index 34878`
- **stompysurprise** x34 -- T4 m1 `{no land [ordered] + cast:Llanowar Elves + cast:Call of the Wild + cast:World War Hulk + cast:Natural Order}`  
  keeps Fyndhorn Elves untapped (attacker); keeps Turntimber, Serpentine Wood untapped -- by paying pay1[Elvish Mystic#11:G] ; pay2[Elvish Archdruid#7:G] ; pay3[Priest of Titania#42:G Elvish Mystic#12:G] ; pay4[(float only)]; the engine ended board=[Turntimber, Serpentine Wood#49(T), Fyndhorn Elves#28(T), Elvish Mystic#11(T), Elvish Mystic#12(T), Elvish Archdruid#7(T), Llanowar Elves#34(new), Call of the Wild#2(new), World War Hulk#54(new), Craterhoof Behemoth#4(new)] hand=[] float={G}{G} life=17  
  board: board=[Turntimber, Serpentine Wood#49, Fyndhorn Elves#28, Priest of Titania#42, Elvish Mystic#11, Elvish Mystic#12, Elvish Archdruid#7] hand=[World War Hulk, Call of the Wild, Natural Order, Llanowar Elves] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30033180 --game-index 33180`
- **stompysurprise** x29 -- T4 m1 `{no land + cast:Natural Order + cast:Elvish Archdruid + cast:Priest of Titania}`  
  keeps Elvish Mystic untapped (attacker); keeps Forest untapped -- by paying pay1[Elvish Mystic#14:G Llanowar Elves#36:G] ; pay2[Elvish Archdruid#10:G] ; pay3[Fyndhorn Elves#31:G]; the engine ended board=[Forest#22(T), Elvish Mystic#12(T), Llanowar Elves#36(T), Fyndhorn Elves#31(T), Elvish Archdruid#10(T), Priest of Titania#42(new), Elvish Archdruid#8(new), Craterhoof Behemoth#4(new)] hand=[Vaultborn Tyrant] float={G}{G}{G} life=20  
  board: board=[Forest#22, Elvish Mystic#14, Elvish Mystic#12, Llanowar Elves#36, Fyndhorn Elves#31, Elvish Archdruid#10] hand=[Natural Order, Elvish Archdruid, Vaultborn Tyrant, Priest of Titania] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30032971 --game-index 32971`
- **stompysurprise** x28 -- T4 m1 `{no land + cast:Priest of Titania + cast:Craterhoof Behemoth + cast:Natural Order}`  
  keeps Forest untapped x2; keeps Fyndhorn Elves untapped (attacker) -- by paying pay1[Fyndhorn Elves#28:G Elvish Mystic#11:G] ; pay2[Elvish Archdruid#7:G Priest of Titania#42:G] ; pay3[(float only)]; the engine ended board=[Forest#15(T), Forest#16(T), Elvish Mystic#11(T), Fyndhorn Elves#29(T), Priest of Titania#42(T), Elvish Archdruid#7(T), Priest of Titania#43(new), Craterhoof Behemoth#5(new), Craterhoof Behemoth#4(new)] hand=[] float={G} life=20  
  board: board=[Forest#15, Fyndhorn Elves#28, Forest#16, Elvish Mystic#11, Fyndhorn Elves#29, Priest of Titania#42, Elvish Archdruid#7] hand=[Priest of Titania, Craterhoof Behemoth, Natural Order] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30031603 --game-index 31603`
- **stompysurprise** x28 -- T4 m1 `{no land + cast:Elvish Archdruid + cast:Turntimber Symbiosis}`  
  keeps Elvish Mystic untapped (attacker); keeps Forest untapped -- by paying pay1[Priest of Titania#44:G Elvish Mystic#11:G] ; pay2[Priest of Titania#43:G]; the engine ended board=[Forest#20(T), Elvish Mystic#11(T), Priest of Titania#44(T), Elvish Mystic#13(T), Priest of Titania#43(T), Elvish Archdruid#10(new), Llanowar Elves#36(new)] hand=[Apex Altisaur, Worldly Tutor] float={G}{G}{G} life=20  
  board: board=[Forest#20, Elvish Mystic#11, Priest of Titania#44, Elvish Mystic#13, Priest of Titania#43] hand=[Elvish Archdruid, Turntimber Symbiosis, Apex Altisaur, Worldly Tutor] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/StompySurprise/StompySurprise.cod' --profile 'decks/StompySurprise/StompySurprise.profile.json' --games 1 --seed 30031993 --game-index 31993`
- ... 3743 more signatures in flags.jsonl

## ORACLE-GAP (5802 flags, 2885 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **anti_lifegain** x80 -- T2 m1 `{land=Grove of the Burnwillows + cast:Tainted Remedy}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Overgrown Tomb#32:B Ignoble Hierarch#22:B Grove of the Burnwillows#18:R]: opponent life differs; float engine - oracle -; engine ended board=[Overgrown Tomb#32(T), Ignoble Hierarch#22(T), Grove of the Burnwillows#18(T)(new), Tainted Remedy#49(new)] hand=[Godless Shrine, Reverent Silence, Bloodstained Mire, Grove of the Burnwillows, Fiery Justice] float=- life=17  
  board: board=[Overgrown Tomb#32, Ignoble Hierarch#22] hand=[Tainted Remedy, Godless Shrine, Reverent Silence, Bloodstained Mire, Grove of the Burnwillows, Grove of the Burnwillows, Fiery Justice] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Anti-Lifegain/Anti-Lifegain.cod' --profile 'decks/Anti-Lifegain/Anti-Lifegain.profile.json' --games 1 --seed 30085001 --game-index 85001`
- **midrange_burn** x56 -- T3 m1 `{no land + cast:Fireblast + suspend:Rift Bolt + gy-exile:Grim Lavamancer}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#42:R Mountain#32:R] ; pay2[(float only)]: Grim Lavamancer#16 tapped by oracle only; graveyard size differs; opponent life differs; float engine - oracle -; engine ended board=[Grim Lavamancer#16] hand=[Mountain, Price of Progress] float=- life=20  
  board: board=[Mountain#42, Grim Lavamancer#16, Mountain#32] hand=[Fireblast, Mountain, Rift Bolt, Price of Progress] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30101665 --game-index 101665`
- **anti_lifegain** x54 -- T2 m1 `{land=Grove of the Burnwillows + cast:Tainted Remedy}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Overgrown Tomb#32:B Birds of Paradise#5:W Grove of the Burnwillows#18:R]: opponent life differs; float engine - oracle -; engine ended board=[Overgrown Tomb#32(T), Birds of Paradise#5(T), Grove of the Burnwillows#18(T)(new), Tainted Remedy#49(new), Skyshroud Cutter#41(new)] hand=[Reverent Silence, Aria of Flame, Invigorate, Godless Shrine] float=- life=17  
  board: board=[Overgrown Tomb#32, Birds of Paradise#5] hand=[Reverent Silence, Skyshroud Cutter, Aria of Flame, Grove of the Burnwillows, Invigorate, Tainted Remedy, Godless Shrine] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Anti-Lifegain/Anti-Lifegain.cod' --profile 'decks/Anti-Lifegain/Anti-Lifegain.profile.json' --games 1 --seed 30085005 --game-index 85005`
- **midrange_burn** x50 -- T3 m1 `{no land + suspend:Rift Bolt + cast:Fireblast + gy-exile:Grim Lavamancer}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)]: Grim Lavamancer#16 tapped by oracle only; graveyard size differs; opponent life differs; float engine - oracle -; engine ended board=[Grim Lavamancer#16] hand=[Searing Blaze, Chandra's Incinerator, Rift Bolt] float=- life=20  
  board: board=[Mountain#29, Grim Lavamancer#16, Mountain#30] hand=[Rift Bolt, Fireblast, Searing Blaze, Chandra's Incinerator, Rift Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100065 --game-index 100065`
- **anti_lifegain** x47 -- T2 m1 `{land=Grove of the Burnwillows + cast:Tainted Remedy}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Temple Garden#53:G Birds of Paradise#8:B Grove of the Burnwillows#19:R]: opponent life differs; float engine - oracle -; engine ended board=[Temple Garden#53(T), Birds of Paradise#8(T), Grove of the Burnwillows#19(T)(new), Tainted Remedy#49(new)] hand=[Birds of Paradise, Aria of Flame] float=- life=18  
  board: board=[Temple Garden#53, Birds of Paradise#8] hand=[Birds of Paradise, Tainted Remedy, Aria of Flame, Grove of the Burnwillows] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Anti-Lifegain/Anti-Lifegain.cod' --profile 'decks/Anti-Lifegain/Anti-Lifegain.profile.json' --games 1 --seed 30085014 --game-index 85014`
- **midrange_burn** x44 -- T3 m1 `{no land + suspend:Rift Bolt + cast:Fireblast + gy-exile:Grim Lavamancer}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)]: Grim Lavamancer#16 tapped by oracle only; life engine 20 oracle 18; graveyard size differs; float engine - oracle -; engine ended board=[Grim Lavamancer#16] hand=[Searing Blaze, Chandra's Incinerator, Rift Bolt] float=- life=20  
  board: board=[Mountain#29, Grim Lavamancer#16, Mountain#30] hand=[Rift Bolt, Fireblast, Searing Blaze, Chandra's Incinerator, Rift Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100065 --game-index 100065`
- **anti_lifegain** x39 -- T2 m1 `{land=Grove of the Burnwillows + cast:Idyllic Tutor}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Temple Garden#53:G Birds of Paradise#5:W Grove of the Burnwillows#18:R]: opponent life differs; float engine - oracle -; engine ended board=[Temple Garden#53(T), Birds of Paradise#5(T), Grove of the Burnwillows#18(T)(new)] hand=[Tainted Remedy, Bloodstained Mire, Skyshroud Cutter, Skyshroud Cutter, Aria of Flame] float=- life=17  
  board: board=[Temple Garden#53, Birds of Paradise#5] hand=[Tainted Remedy, Bloodstained Mire, Idyllic Tutor, Skyshroud Cutter, Grove of the Burnwillows, Skyshroud Cutter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Anti-Lifegain/Anti-Lifegain.cod' --profile 'decks/Anti-Lifegain/Anti-Lifegain.profile.json' --games 1 --seed 30085244 --game-index 85244`
- **midrange_burn** x38 -- T3 m1 `{no land + suspend:Rift Bolt + cast:Fireblast + gy-exile:Grim Lavamancer@16}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#29:R Mountain#30:R] ; pay2[(float only)]: Grim Lavamancer#16 only in engine; graveyard size differs; float engine - oracle -; engine ended board=[Grim Lavamancer#16] hand=[Searing Blaze, Chandra's Incinerator, Rift Bolt] float=- life=20  
  board: board=[Mountain#29, Grim Lavamancer#16, Mountain#30] hand=[Rift Bolt, Fireblast, Searing Blaze, Chandra's Incinerator, Rift Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30100065 --game-index 100065`
- **anti_lifegain** x35 -- T2 m1 `{land=Grove of the Burnwillows + cast:Tainted Remedy}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Temple Garden#53:G Ignoble Hierarch#22:B Grove of the Burnwillows#19:R]: opponent life differs; float engine - oracle -; engine ended board=[Temple Garden#53(T), Ignoble Hierarch#22(T), Grove of the Burnwillows#19(T)(new), Tainted Remedy#49(new), Skyshroud Cutter#41(new)] hand=[Swords to Plowshares, Invigorate, Blood Crypt, Birds of Paradise] float=- life=17  
  board: board=[Temple Garden#53, Ignoble Hierarch#22] hand=[Swords to Plowshares, Skyshroud Cutter, Invigorate, Blood Crypt, Tainted Remedy, Birds of Paradise, Grove of the Burnwillows] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Anti-Lifegain/Anti-Lifegain.cod' --profile 'decks/Anti-Lifegain/Anti-Lifegain.profile.json' --games 1 --seed 30085027 --game-index 85027`
- **midrange_burn** x32 -- T3 m1 `{no land + cast:Fireblast + suspend:Rift Bolt + gy-exile:Grim Lavamancer}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#42:R Mountain#32:R] ; pay2[(float only)]: Grim Lavamancer#16 tapped by oracle only; life engine 20 oracle 18; graveyard size differs; float engine - oracle -; engine ended board=[Grim Lavamancer#16] hand=[Mountain, Price of Progress] float=- life=20  
  board: board=[Mountain#42, Grim Lavamancer#16, Mountain#32] hand=[Fireblast, Mountain, Rift Bolt, Price of Progress] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30101665 --game-index 101665`
- **midrange_burn** x32 -- T3 m1 `{no land + cast:Fireblast + suspend:Rift Bolt + gy-exile:Grim Lavamancer@16}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#42:R Mountain#32:R] ; pay2[(float only)]: Grim Lavamancer#16 only in engine; graveyard size differs; float engine - oracle -; engine ended board=[Grim Lavamancer#16] hand=[Mountain, Price of Progress] float=- life=20  
  board: board=[Mountain#42, Grim Lavamancer#16, Mountain#32] hand=[Fireblast, Mountain, Rift Bolt, Price of Progress] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Midrange Burn/Midrange Burn.cod' --profile 'decks/Midrange Burn/Midrange Burn.profile.json' --games 1 --seed 30101665 --game-index 101665`
- **anti_lifegain** x30 -- T2 m1 `{land=Windswept Heath + cast:Idyllic Tutor}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Grove of the Burnwillows#18:R Ignoble Hierarch#22:B Temple Garden#53:W]: opponent life differs; float engine - oracle -; engine ended board=[Grove of the Burnwillows#18(T), Ignoble Hierarch#22(T), Temple Garden#53(T)(new)] hand=[Aria of Flame, Invigorate, Tainted Remedy, Tainted Remedy, Skyshroud Cutter, Aria of Flame] float=- life=17  
  board: board=[Grove of the Burnwillows#18, Ignoble Hierarch#22] hand=[Aria of Flame, Invigorate, Idyllic Tutor, Tainted Remedy, Windswept Heath, Tainted Remedy, Skyshroud Cutter] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Anti-Lifegain/Anti-Lifegain.cod' --profile 'decks/Anti-Lifegain/Anti-Lifegain.profile.json' --games 1 --seed 30085237 --game-index 85237`
- ... 2873 more signatures in flags.jsonl

## UNMODELED (13012 flags, 995 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **galadriel** x532 -- T4 m1 `{no land + cast:Crackle with Power(x16)}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Temple of Malady#39, Saprazzan Skerry#16, Sol Ring#19, Galadriel of Lothlórien#10, Temple of Malady#38, Temple of Malady#37, Temple of Enlightenment#31, The Grey Havens#55, Temple of Deceit#26, Rumble Arena#12, Temple of Mystery#45, Temple of Deceit#25, Crystal Grotto#5, Temple of Epiphany#34, Crystal Grotto#4, Crystal Grotto#6, Temple of Triumph#51, Temple of Abandon#22, Crystal Grotto#3,   
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071001 --game-index 71001`
- **galadriel** x352 -- T4 m1 `{no land + cast:Galadriel of Lothlórien}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Temple of Malady#39, Saprazzan Skerry#16, Sol Ring#19, Galadriel of Lothlórien#10, Temple of Malady#38, Temple of Malady#37, Temple of Enlightenment#31, The Grey Havens#55, Temple of Deceit#26, Rumble Arena#12, Temple of Mystery#45, Temple of Deceit#25, Crystal Grotto#5, Temple of Epiphany#34, Crystal Grotto#4, Crystal Grotto#6, Temple of Triumph#51, Temple of Abandon#22, Crystal Grotto#3,   
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071001 --game-index 71001`
- **galadriel** x237 -- T5 m1 `{land=Saprazzan Skerry + cast:Galadriel of Lothlórien}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Temple of Mystery#43, Rumble Arena#11, Temple of Malady#38, Galadriel of Lothlórien#7, Temple of Malice#41, Zhalfirin Void#60, Temple of Enlightenment#30, Zhalfirin Void#59, Temple of Plenty#47, Temple of Triumph#53, Temple of Mystery#44, Temple of Mystery#45, Temple of Deceit#27, Temple of Triumph#52, Temple of Enlightenment#29, Temple of Epiphany#32, Temple of Deceit#26, Crystal Grotto#3,  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071019 --game-index 71019`
- **galadriel** x225 -- T5 m1 `{land=Zhalfirin Void + cast:Galadriel of Lothlórien}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Temple of Malady#38, Temple of Deceit#26, Crystal Grotto#3, Galadriel of Lothlórien#8, Temple of Triumph#51, Temple of Malice#40, Temple of Plenty#47, Rumble Arena#14, Temple of Malice#42, Temple of Plenty#49, The Grey Havens#55, Temple of Triumph#54, Temple of Epiphany#32] hand=[Zhalfirin Void, Zhalfirin Void, Crackle with Power, Galadriel of Lothlórien] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071018 --game-index 71018`
- **galadriel** x187 -- T5 m1 `{land=Temple of Deceit + cast:Galadriel of Lothlórien}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Temple of Mystery#43, Rumble Arena#11, Temple of Malady#38, Galadriel of Lothlórien#7, Temple of Malice#41, Zhalfirin Void#60, Temple of Enlightenment#30, Zhalfirin Void#59, Temple of Plenty#47, Temple of Triumph#53, Temple of Mystery#44, Temple of Mystery#45, Temple of Deceit#27, Temple of Triumph#52, Temple of Enlightenment#29, Temple of Epiphany#32, Temple of Deceit#26, Crystal Grotto#3,  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071019 --game-index 71019`
- **galadriel** x181 -- T5 m1 `{land=Rumble Arena + cast:Galadriel of Lothlórien}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Temple of Epiphany#34, Temple of Plenty#48, Rumble Arena#12, Galadriel of Lothlórien#8, Temple of Deceit#25, Rumble Arena#13, Temple of Triumph#53, Temple of Triumph#51, The Grey Havens#55, Crystal Grotto#5, Temple of Abandon#20, Temple of Epiphany#35, Temple of Deceit#26, Crystal Grotto#3, Temple of Triumph#52, Temple of Plenty#50, Temple of Mystery#44, Temple of Mystery#43, Temple of Mala  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071000 --game-index 71000`
- **galadriel** x171 -- T5 m1 `{land=Temple of Malady + cast:Galadriel of Lothlórien}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Temple of Epiphany#34, Temple of Plenty#48, Rumble Arena#12, Galadriel of Lothlórien#8, Temple of Deceit#25, Rumble Arena#13, Temple of Triumph#53, Temple of Triumph#51, The Grey Havens#55, Crystal Grotto#5, Temple of Abandon#20, Temple of Epiphany#35, Temple of Deceit#26, Crystal Grotto#3, Temple of Triumph#52, Temple of Plenty#50, Temple of Mystery#44, Temple of Mystery#43, Temple of Mala  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071000 --game-index 71000`
- **galadriel** x169 -- T5 m1 `{land=Crystal Grotto + cast:Galadriel of Lothlórien}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Temple of Enlightenment#30, Temple of Malady#37, Rumble Arena#12, Galadriel of Lothlórien#10, Temple of Triumph#54, Crystal Grotto#5, Temple of Epiphany#33, Temple of Mystery#44, Temple of Enlightenment#29] hand=[Crystal Grotto, Temple of Enlightenment, Crackle with Power, Temple of Plenty, Galadriel of Lothlórien] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071031 --game-index 71031`
- **galadriel** x168 -- T4 m1 `{land=Temple of Triumph + cast:Galadriel of Lothlórien}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Saprazzan Skerry#15, Temple of Malady#36, Galadriel of Lothlórien#7, Temple of Triumph#51, Temple of Triumph#53, Temple of Abandon#20, Temple of Triumph#54, Temple of Enlightenment#28, Temple of Deceit#25, Temple of Malady#38, Temple of Epiphany#32, Temple of Abandon#21, Rumble Arena#11, Rumble Arena#12, Temple of Plenty#47, Zhalfirin Void#57, Temple of Plenty#48, Temple of Plenty#49, The G  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071021 --game-index 71021`
- **galadriel** x165 -- T4 m1 `{land=Temple of Mystery + cast:Galadriel of Lothlórien}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Saprazzan Skerry#15, Temple of Malady#36, Galadriel of Lothlórien#7, Temple of Triumph#51, Temple of Triumph#53, Temple of Abandon#20, Temple of Triumph#54, Temple of Enlightenment#28, Temple of Deceit#25, Temple of Malady#38, Temple of Epiphany#32, Temple of Abandon#21, Rumble Arena#11, Rumble Arena#12, Temple of Plenty#47, Zhalfirin Void#57, Temple of Plenty#48, Temple of Plenty#49, The G  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071021 --game-index 71021`
- **galadriel** x159 -- T4 m1 `{land=Temple of Enlightenment + cast:Galadriel of Lothlórien}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Temple of Mystery#46, Saprazzan Skerry#17, Galadriel of Lothlórien#9, Rumble Arena#13, Temple of Abandon#23, Temple of Malice#41, Temple of Mystery#44, Zhalfirin Void#57, Crystal Grotto#4, Temple of Deceit#26, The Grey Havens#55, Temple of Mystery#43, Zhalfirin Void#60, Temple of Epiphany#35, Temple of Abandon#22, Rumble Arena#11, Temple of Triumph#51, Temple of Malady#39, Temple of Epiphan  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071008 --game-index 71008`
- **selesnyalifegain** x152 -- T5 m1 `{no land}`  
  a mana choice the oracle does not drive: Wirewood Lodge  
  board: board=[Horizon Canopy#42, Elvish Mystic#27, Brushland#20, Fyndhorn Elves#36, Llanowar Elves#44, Wirewood Lodge#60, Ageless Entity#2, Priest of Titania#55] hand=[Ageless Entity] float=- life=15  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/SelesnyaLifegain/SelesnyaLifegain.cod' --profile 'decks/SelesnyaLifegain/SelesnyaLifegain.profile.json' --games 1 --seed 30097076 --game-index 97076`
- ... 983 more signatures in flags.jsonl

## CAPPED (3706 flags, 831 signatures)

_the payment search hit MTG_MANA_ORACLE_NODES and then the _RETRY escalation (20000); the complete side is partial._

- **galadriel** x104 -- T4 m1 `{no land + cast:Galadriel of Lothlórien + cast:Galadriel of Lothlórien}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Saprazzan Skerry#17, Temple of Malady#36, Galadriel of Lothlórien#10, The Grey Havens#55, Temple of Mystery#46, Zhalfirin Void#59, Temple of Abandon#21, Temple of Plenty#48, Zhalfirin Void#58, Temple of Mystery#43, Temple of Abandon#20] hand=[Galadriel of Lothlórien, Galadriel of Lothlórien] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071041 --game-index 71041`
- **galadriel** x78 -- T4 m1 `{land=Saprazzan Skerry + cast:Galadriel of Lothlórien + cast:Galadriel of Lothlórien}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Temple of Abandon#22, Saprazzan Skerry#18, Galadriel of Lothlórien#8, Temple of Abandon#21, Temple of Mystery#44, Temple of Plenty#49, Zhalfirin Void#57, Temple of Epiphany#32, Temple of Mystery#43, Temple of Enlightenment#28, Crystal Grotto#6, Crystal Grotto#5, Temple of Malady#39, The Grey Havens#55, Temple of Deceit#24, Temple of Plenty#50, Rumble Arena#12, Temple of Deceit#27, Temple of  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071011 --game-index 71011`
- **galadriel** x75 -- T4 m1 `{land=Zhalfirin Void + cast:Galadriel of Lothlórien + cast:Galadriel of Lothlórien}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Temple of Abandon#22, Saprazzan Skerry#18, Galadriel of Lothlórien#8, Temple of Abandon#21, Temple of Mystery#44, Temple of Plenty#49, Zhalfirin Void#57, Temple of Epiphany#32, Temple of Mystery#43, Temple of Enlightenment#28, Crystal Grotto#6, Crystal Grotto#5, Temple of Malady#39, The Grey Havens#55, Temple of Deceit#24, Temple of Plenty#50, Rumble Arena#12, Temple of Deceit#27, Temple of  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071011 --game-index 71011`
- **selesnyalifegain** x68 -- T5 m1 `{no land + act:Blighted Steppe(x1) + act:Blighted Steppe(x1)}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Razorverge Thicket#56, Elvish Mystic#29, Brushland#17, Elvish Mystic#27, Priest of Titania#53, Razorverge Thicket#59, Elvish Archdruid#24, Branchloft Pathway#16, Forest#34, Fyndhorn Elves#37, Elvish Archdruid#23, Priest of Titania#55, Blighted Steppe#5, Blighted Steppe#7, Priest of Titania#54, Razorverge Thicket#57, Llanowar Elves#44] hand=[Brushland] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/SelesnyaLifegain/SelesnyaLifegain.cod' --profile 'decks/SelesnyaLifegain/SelesnyaLifegain.profile.json' --games 1 --seed 30097629 --game-index 97629`
- **selesnyalifegain** x59 -- T5 m1 `{no land + cast:Ageless Entity + act:Blighted Steppe(x1)}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Razorverge Thicket#56, Elvish Mystic#27, Brushland#20, Elvish Mystic#28, Llanowar Elves#47, Blighted Steppe#5, Priest of Titania#52, Priest of Titania#55, Blighted Steppe#7, Elvish Archdruid#25] hand=[Ageless Entity] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/SelesnyaLifegain/SelesnyaLifegain.cod' --profile 'decks/SelesnyaLifegain/SelesnyaLifegain.profile.json' --games 1 --seed 30097193 --game-index 97193`
- **galadriel** x54 -- T5 m1 `{land=Temple of Deceit + cast:Galadriel of Lothlórien + cast:Galadriel of Lothlórien}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Temple of Plenty#47, Galadriel of Lothlórien#7, Crystal Grotto#3, Temple of Enlightenment#28, Temple of Malady#36, Crystal Grotto#4, Saprazzan Skerry#16, Zhalfirin Void#57, Temple of Triumph#51, Temple of Abandon#20, Temple of Epiphany#32, The Grey Havens#55, Temple of Deceit#26, Zhalfirin Void#58, Temple of Mystery#43, Temple of Enlightenment#29, Temple of Abandon#21, Temple of Malady#37,   
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071252 --game-index 71252`
- **galadriel** x54 -- T4 m1 `{land=Crystal Grotto + cast:Galadriel of Lothlórien + cast:Galadriel of Lothlórien}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Saprazzan Skerry#18, Temple of Mystery#43, Galadriel of Lothlórien#9, Temple of Deceit#25, Temple of Abandon#22, Temple of Epiphany#35, Temple of Plenty#50, Rumble Arena#13, Temple of Plenty#49, Temple of Triumph#52, Temple of Mystery#45, Temple of Enlightenment#31, Crystal Grotto#3, Temple of Triumph#53, Temple of Deceit#26, Crystal Grotto#5, Temple of Malady#37, Temple of Abandon#23, Temp  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071296 --game-index 71296`
- **galadriel** x48 -- T4 m1 `{land=Temple of Enlightenment + cast:Galadriel of Lothlórien + cast:Galadriel of Lothlórien}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Saprazzan Skerry#15, Temple of Plenty#47, Galadriel of Lothlórien#7, Temple of Epiphany#32, Temple of Enlightenment#29, Temple of Deceit#24, Temple of Plenty#48, Rumble Arena#11, Temple of Abandon#20, Temple of Enlightenment#30, Temple of Deceit#25, Zhalfirin Void#57, Temple of Triumph#51, Rumble Arena#12, Temple of Malice#40, Temple of Abandon#21, Temple of Enlightenment#31, Rumble Arena#1  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071397 --game-index 71397`
- **galadriel** x45 -- T4 m1 `{land=Rumble Arena + cast:Galadriel of Lothlórien + cast:Galadriel of Lothlórien}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Temple of Plenty#48, Saprazzan Skerry#17, Galadriel of Lothlórien#8, Temple of Epiphany#32, Temple of Malice#42, Temple of Mystery#45, Temple of Abandon#22, Zhalfirin Void#59, Temple of Abandon#23, Temple of Deceit#24, Temple of Malady#39, Temple of Abandon#20] hand=[Galadriel of Lothlórien, Rumble Arena, Galadriel of Lothlórien, Crackle with Power, Sol Ring] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071417 --game-index 71417`
- **galadriel** x42 -- T5 m1 `{land=Temple of Abandon + cast:Galadriel of Lothlórien + cast:Galadriel of Lothlórien}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Temple of Enlightenment#28, Temple of Malady#36, Temple of Triumph#51, Galadriel of Lothlórien#7, Temple of Triumph#52, The Grey Havens#55, Crystal Grotto#3, Temple of Mystery#43, Temple of Epiphany#33, Temple of Epiphany#34, Zhalfirin Void#57, Rumble Arena#12, Crystal Grotto#4, Temple of Enlightenment#29, Zhalfirin Void#58, Temple of Abandon#21, Temple of Enlightenment#30, Crystal Grotto#5  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071012 --game-index 71012`
- **galadriel** x42 -- T4 m1 `{land=Temple of Malady + cast:Galadriel of Lothlórien + cast:Galadriel of Lothlórien}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Temple of Abandon#22, Saprazzan Skerry#18, Galadriel of Lothlórien#8, Temple of Abandon#21, Temple of Mystery#44, Temple of Plenty#49, Zhalfirin Void#57, Temple of Epiphany#32, Temple of Mystery#43, Temple of Enlightenment#28, Crystal Grotto#6, Crystal Grotto#5, Temple of Malady#39, The Grey Havens#55, Temple of Deceit#24, Temple of Plenty#50, Rumble Arena#12, Temple of Deceit#27, Temple of  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071011 --game-index 71011`
- **galadriel** x42 -- T5 m1 `{land=Temple of Mystery + cast:Galadriel of Lothlórien + cast:Galadriel of Lothlórien}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Temple of Triumph#51, Temple of Mystery#43, Temple of Enlightenment#28, Galadriel of Lothlórien#7, Temple of Malice#40, Temple of Enlightenment#29, Temple of Malice#41, Temple of Abandon#20, Temple of Triumph#52, Temple of Epiphany#32, Temple of Deceit#24, Rumble Arena#12, Rumble Arena#13, Temple of Enlightenment#30, Zhalfirin Void#58, Temple of Mystery#45, Temple of Abandon#21, Temple of E  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Galadriel/Galadriel.cod' --profile 'decks/Galadriel/Galadriel.profile.json' --games 1 --seed 30071259 --game-index 71259`
- ... 819 more signatures in flags.jsonl

