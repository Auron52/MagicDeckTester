# Mana-clean gate `due-medium-20261108`

Mana-clean gate run: 10 decks, games (seed 30000000 + game index) fivecolour 12000-16999, hinata2 5000-9999, landfall 21000-25999, landfallelementals 12000-16999, melira_pod 5000-9999, mirrorwing_dragon 14000-18999, nivmizzetcombo 5000-9999, prevent_damage 12000-16999, snow 7000-11999, unpredictable_cyclone 5000-9999; src `3bda5c647e7c`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | DOMINATED-ONESHOT | DOMINATED-HASTE | DOMINATED-LIFE | ORACLE-GAP | UNMODELED | CAPPED | CAPPED-DECISION | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| fivecolour | 5000 | issues | 35012 | 38 | 1709 | 743 | 5367 | 0 | 0 | 0 | 344 | 19700 | 1999 | 5112 | 6922 | 721 | 404 | 31131 | 0 |
| hinata2 | 5000 | issues | 58243 | 113 | 2160 | 266 | 248 | 0 | 0 | 0 | 13 | 37216 | 9930 | 8297 | 9853 | 3254 | 11428 | 25852 | 7 |
| landfall | 5000 | issues | 81161 | 0 | 42 | 36 | 0 | 0 | 0 | 10 | 260 | 4 | 2043 | 78776 | 1283 | 528 | 12117 | 7665 | 256 |
| landfallelementals | 5000 | issues | 94137 | 0 | 2958 | 409 | 0 | 0 | 0 | 7882 | 1029 | 179 | 1250 | 88312 | 123 | 2835 | 8635 | 73509 | 0 |
| melira_pod | 5000 | issues | 299501 | 0 | 2338 | 0 | 1198 | 0 | 0 | 139400 | 5610 | 319 | 122 | 289914 | 405 | 12020 | 985 | 83936 | 0 |
| mirrorwing_dragon | 5000 | issues | 12549 | 7128 | 1301 | 183 | 198 | 66 | 0 | 0 | 3082 | 0 | 591 | 0 | 72 | 933 | 901 | 5233 | 125 |
| nivmizzetcombo | 5000 | issues | 333811 | 111 | 11368 | 299 | 0 | 0 | 5407 | 482 | 445 | 467 | 1273 | 314441 | 16957 | 7675 | 18226 | 50742 | 6 |
| prevent_damage | 5000 | issues | 3775 | 0 | 39 | 18 | 0 | 0 | 0 | 13010 | 250 | 3400 | 68 | 0 | 130 | 200 | 47 | 8707 | 0 |
| snow | 5000 | issues | 44095 | 439 | 461 | 0 | 21 | 0 | 0 | 0 | 1420 | 1986 | 10604 | 29164 | 80720 | 1895 | 37531 | 22166 | 6 |
| unpredictable_cyclone | 5000 | issues | 13103 | 591 | 6898 | 2342 | 0 | 0 | 0 | 0 | 1143 | 184 | 302 | 1643 | 419726 | 48024 | 185018 | 72496 | 162 |

**MISS = 43327, ORACLE-GAP = 12986.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=468743 plans=12615001 skipped=0 applies=5060281432 frontiers=3798030357 pruned=1514692893 options=4313728432 engine_drops=921493 gate_rejects=12168040(checked 11112773, skipped 903006) MISS=215279 ORACLE-GAP=13596 UNMODELED=63455 CAPPED=28182 ms=471953207 ===
=== MANA ORACLE classes: ORACLE-GAP=13596 ENUM-GAP=8420 APPLY-DROP=29274 APPLY-DROP-ORDER=4296 PHANTOM=536191 DOMINATED-CREATURE=7032 DOMINATED-ONESHOT=66 DOMINATED-HASTE=5407 DOMINATED-LIFE=160784 DOMINATED-LAND=381437 CONTINUATION-DIFFERS=78085 DROP-COVERED=275292 UNMODELED=63455 CAPPED=28182 HIDDEN-INFO=562 CAPPED-DECISION=815659 ===
=== MANA ORACLE enum-gap rejections: noop=23847 hidden=2809 host_blind=0 cont_land=1387 ===
=== MANA ORACLE decision budget: 340 decisions spent it, 815450 plans not audited ===
=== MANA ORACLE capped-plan escalation: 214315 retried, 186005 finished ===
=== MANA ORACLE unmodeled: 35051 x engine tapped a mana source outside any payment ===
=== MANA ORACLE unmodeled: 71510 x floating pool too large to enumerate set-asides ===
=== MANA ORACLE unmodeled: 1726134 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 1827923 x payment enumeration capped ===
=== MANA ORACLE unmodeled: 1 x scripted payment refused: scripted payment did not pay {1} ===
=== MANA ORACLE unmodeled: 52 x scripted payment refused: scripted payment did not pay {1}{B}{B} ===
=== MANA ORACLE unmodeled: 2 x scripted payment refused: scripted payment did not pay {1}{G} ===
=== MANA ORACLE unmodeled: 165 x scripted payment refused: scripted payment did not pay {1}{R} ===
=== MANA ORACLE unmodeled: 26215 x scripted payment refused: scripted payment did not pay {1}{U} ===
=== MANA ORACLE unmodeled: 2 x scripted payment refused: scripted payment did not pay {1}{U}{G} ===
=== MANA ORACLE unmodeled: 150 x scripted payment refused: scripted payment did not pay {1}{U}{U}{R}{R} ===
=== MANA ORACLE unmodeled: 25 x scripted payment refused: scripted payment did not pay {2} ===
=== MANA ORACLE unmodeled: 6178 x scripted payment refused: scripted payment did not pay {2}{G} ===
=== MANA ORACLE unmodeled: 21901 x scripted payment refused: scripted payment did not pay {2}{U}{G} ===
=== MANA ORACLE unmodeled: 26 x scripted payment refused: scripted payment did not pay {2}{U}{U}{R}{R} ===
=== MANA ORACLE unmodeled: 1 x scripted payment refused: scripted payment did not pay {2}{U}{U}{U}{R}{R} ===
=== MANA ORACLE unmodeled: 113867 x scripted payment refused: scripted payment did not pay {G} ===
=== MANA ORACLE unmodeled: 267 x scripted payment refused: scripted payment did not pay {R} ===
=== MANA ORACLE unmodeled: 32 x scripted payment refused: scripted payment did not pay {R}{G} ===
=== MANA ORACLE unmodeled: 43999 x scripted payment refused: scripted payment did not pay {U} ===
=== MANA ORACLE unmodeled: 286682 x scripted payment refused: scripted payment did not pay {U}{G} ===
=== MANA ORACLE unmodeled: 166 x scripted payment refused: scripted payment did not pay {U}{U}{U}{R}{R}{R} ===
=== MANA ORACLE unmodeled: 453656 x scripted payment refused: set-aside exceeds the floating pool ===
=== MANA ORACLE unmodeled: 20883 x tap-ahead enumeration capped ===
=== MANA ORACLE unmodeled: 643 x too many actions to try every order ===
=== MANA ORACLE unmodeled: 1949 x untap effect chose among different lands ===
=== MANA ORACLE rollback census: lost=41993 depth0=2638 depth1=18821 depth2=5422 depth3+=2133 inconsistent=0 capped=257 unfaithful=6 no-failed-payment=0 unmodeled=0 reorder=4296 off-menu=8420 need_in_plan=18806 need_later=10471 pay_rollback_rescued=962/29277 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | DOMINATED-ONESHOT | DOMINATED-HASTE | ORACLE-GAP | UNMODELED | CAPPED | CAPPED-DECISION |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| fivecolour | 45129 | 356594 | 38 | 1709 | 743 | 5000 | 0 | 0 | 344 | 5000 | 1999 | 5000 |
| hinata2 | 53277 | 481863 | 113 | 2160 | 266 | 248 | 0 | 0 | 13 | 5000 | 5000 | 5000 |
| landfall | 51832 | 989415 | 0 | 42 | 36 | 0 | 0 | 0 | 260 | 4 | 2043 | 5000 |
| landfallelementals | 38378 | 1366192 | 0 | 2958 | 409 | 0 | 0 | 0 | 1029 | 179 | 1250 | 5000 |
| melira_pod | 42716 | 2549185 | 0 | 2338 | 0 | 1198 | 0 | 0 | 5000 | 319 | 122 | 5000 |
| mirrorwing_dragon | 36379 | 593387 | 5000 | 1301 | 183 | 198 | 66 | 0 | 3082 | 0 | 591 | 0 |
| nivmizzetcombo | 40476 | 748762 | 111 | 5000 | 299 | 0 | 0 | 5000 | 445 | 467 | 1273 | 5000 |
| prevent_damage | 49621 | 753631 | 0 | 39 | 18 | 0 | 0 | 0 | 250 | 3400 | 68 | 0 |
| snow | 51665 | 1182236 | 439 | 461 | 0 | 21 | 0 | 0 | 1420 | 1986 | 5000 | 5000 |
| unpredictable_cyclone | 59270 | 4256925 | 591 | 5000 | 2342 | 0 | 0 | 0 | 1143 | 184 | 302 | 1643 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth0 | depth1 | depth2 | depth3+ | capped | unfaithful | reorder | off-menu | rescued | need-later |
|---|---|---|---|---|---|---|---|---|---|---|
| fivecolour | 210 | 1039 | 265 | 52 | 143 | 0 | 743 | 38 | 23 | 164 |
| hinata2 | 0 | 1408 | 259 | 404 | 88 | 1 | 266 | 113 | 0 | 586 |
| landfall | 0 | 42 | 0 | 0 | 0 | 0 | 36 | 0 | 20 | 4 |
| landfallelementals | 0 | 2178 | 724 | 54 | 0 | 2 | 409 | 0 | 76 | 89 |
| melira_pod | 0 | 1935 | 372 | 31 | 0 | 0 | 0 | 0 | 20 | 80 |
| mirrorwing_dragon | 19 | 1041 | 230 | 11 | 0 | 0 | 183 | 5000 | 622 | 521 |
| nivmizzetcombo | 953 | 2861 | 751 | 426 | 9 | 0 | 299 | 111 | 0 | 1853 |
| prevent_damage | 0 | 39 | 0 | 0 | 0 | 0 | 18 | 0 | 0 | 7 |
| snow | 155 | 289 | 15 | 2 | 0 | 0 | 0 | 439 | 201 | 269 |
| unpredictable_cyclone | 0 | 3361 | 1313 | 326 | 0 | 0 | 2342 | 591 | 0 | 3697 |

## ENUM-GAP (6292 flags, 3723 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **unpredictable_cyclone** x48 -- T2 m1 `{no land + cast:Ponder + dig:Curator of Mysteries}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Steam Vents#45:U] ; pay2[Steam Vents#46:U]  
  board: board=[Steam Vents#45] hand=[Scalding Tarn, Ponder, Mountain, Unpredictable Cyclone, Curator of Mysteries, Brainsurge] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005026 --game-index 5026`
- **mirrorwing_dragon** x45 -- T3 m2 `{no land + cast:Fortifying Draught@5}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Treasure Token#1000:G]  
  board: board=[Game Trail#29(T), Elvish Mystic#5(T), Ignoble Hierarch#39(T), Ignoble Hierarch#40(new), Treasure Token#1000(new), Gruul Turf#37(T)(new)] hand=[Fortifying Draught, Gold Rush, Oracle's Restoration, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30014035 --game-index 14035`
- **mirrorwing_dragon** x36 -- T2 m2 `{no land + cast:Fortifying Draught}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Treasure Token#1000:G]  
  board: board=[Game Trail#29(T), Game Trail#30(T)(new), Treasure Token#1000(new)] hand=[Fists of Flame, Frontline Heroism, Mountain, Fists of Flame, Fortifying Draught] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30014104 --game-index 14104`
- **unpredictable_cyclone** x36 -- T2 m1 `{no land + cast:Ponder + dig:Curator of Mysteries}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Island#19:U] ; pay2[Steam Vents#46:U]  
  board: board=[Island#19] hand=[Street Wraith, Curator of Mysteries, Blood Crypt, Unpredictable Cyclone, Ponder, Scalding Tarn, Curator of Mysteries] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005067 --game-index 5067`
- **mirrorwing_dragon** x32 -- T2 m1 `{land=Forest + cast:Gold Rush + cast:Gold Rush}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Sandstone Needle#54:R Forest#13:G] ; pay2[Treasure Token#1000:G]  
  board: board=[Sandstone Needle#54] hand=[Fortifying Draught, Gold Rush, Gold Rush, Forest, Frontline Heroism, Mirrorwing Dragon, Ancestral Anger] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30014032 --game-index 14032`
- **mirrorwing_dragon** x31 -- T2 m2 `{no land + cast:Fortifying Draught@39}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Treasure Token#1000:G]  
  board: board=[Ignoble Hierarch#39(T), Treasure Token#1000(new), Gruul Turf#38(T)(new)] hand=[Zada, Hedron Grinder, Fortifying Draught, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30014069 --game-index 14069`
- **mirrorwing_dragon** x30 -- T2 m1 `{no land + cast:Gold Rush + cast:Fortifying Draught}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#13:G Ignoble Hierarch#39:B] ; pay2[Treasure Token#1000:G]  
  board: board=[Forest#13, Ignoble Hierarch#39] hand=[Oracle's Restoration, Gold Rush, Rootbound Crag, Forest, Frontline Heroism, Fortifying Draught, Fists of Flame] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30014001 --game-index 14001`
- **mirrorwing_dragon** x30 -- T3 m2 `{no land + cast:Ancestral Anger@39}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Treasure Token#1000:R]  
  board: board=[Rootbound Crag#52(T), Game Trail#30(T), Ignoble Hierarch#39(T), Ignoble Hierarch#42(new), Treasure Token#1000(new)] hand=[Frontline Heroism, Ancestral Anger, Frontline Heroism] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30014367 --game-index 14367`
- **mirrorwing_dragon** x27 -- T2 m1 `{no land + cast:Fortifying Draught@39 + cast:Gold Rush@39}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#13:G Ignoble Hierarch#39:B] ; pay2[Treasure Token#1000:G]  
  board: board=[Forest#13, Ignoble Hierarch#39] hand=[Ancestral Anger, Fortifying Draught, Mirrorwing Dragon, Sandstone Needle, Zada, Hedron Grinder, Gold Rush, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30014011 --game-index 14011`
- **mirrorwing_dragon** x25 -- T2 m1 `{no land + cast:Fortifying Draught + cast:Gold Rush}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#13:G Ignoble Hierarch#39:B] ; pay2[Treasure Token#1000:G]  
  board: board=[Forest#13, Ignoble Hierarch#39] hand=[Fortifying Draught, Rootbound Crag, Frontline Heroism, Mirrorwing Dragon, Gold Rush, Forest, Ancestral Anger] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30014187 --game-index 14187`
- **unpredictable_cyclone** x25 -- T2 m1 `{no land + cast:Brainstorm + dig:Curator of Mysteries}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Island#23:U] ; pay2[Steam Vents#46:U]  
  board: board=[Island#23] hand=[Curator of Mysteries, Remand, Cascade Bluffs, Brainstorm, Remand, Unpredictable Cyclone, Myriad Landscape] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005271 --game-index 5271`
- **mirrorwing_dragon** x24 -- T2 m2 `{no land + cast:Ancestral Anger@5}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Treasure Token#1000:R]  
  board: board=[Forest#13(T), Elvish Mystic#5(T), Sandstone Needle#54(T)(new), Treasure Token#1000(new)] hand=[Rootbound Crag, Ancestral Anger, Zada, Hedron Grinder, Forest, Frontline Heroism] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30014029 --game-index 14029`
- ... 3711 more signatures in flags.jsonl

## APPLY-DROP (21008 flags, 6442 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **unpredictable_cyclone** x138 -- T5 m1 `{land=Myriad Landscape + dig:Curator of Mysteries}`  
  engine dropped [cast Ponder]; every action pays with pay1[Island#21:U] ; pay2[Steam Vents#45:U Watery Grave#59:U Cascade Bluffs#10:C] ; pay3[Steam Vents#46:U] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Steam Vents#45:U Watery Grave#59:U Cascade Bluffs#10:C] ; pay3[Steam Vents#46:U]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Steam Vents#45, Steam Vents#46, Watery Grave#59, Island#21, Cascade Bluffs#10, Unpredictable Cyclone#56, Street Wraith#47, Emrakul, the Aeons Torn#15] hand=[Curator of Mysteries, Myriad Landscape, Brainsurge, Mountain, Ponder] float=- life=8  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005542 --game-index 5542`
- **unpredictable_cyclone** x125 -- T5 m1 `{land=Myriad Landscape + cast:Brainsurge + dig:Curator of Mysteries}`  
  engine dropped [cast Curator of Mysteries]; every action pays with pay1[Mountain#26:R Mountain#25:R Steam Vents#46:U] ; pay2[Watery Grave#59:U] ; pay3[(float only)] ; pay4[Island#19:U Myriad Landscape#29:C], reaching a board the menu's other order of these actions does not (Watery Grave#59 tapped by oracle only; Mountain#25 tapped by oracle only; Ulamog, the Ceaseless Hunger#53 only in oracle; Unpredictable Cyclone#56 only in oracle; life engine 15 oracle 13; hand size differs; graveyard size differs; library size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and e  
  board: board=[Island#19, Watery Grave#59, Myriad Landscape#29, Mountain#26, Mountain#25, Steam Vents#46, Unpredictable Cyclone#57, Emrakul, the Aeons Torn#18] hand=[Brainsurge, Myriad Landscape, Emrakul, the Aeons Torn, Curator of Mysteries, Island] float=- life=15  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30007244 --game-index 7244`
- **unpredictable_cyclone** x116 -- T5 m1 `{land=Xander's Lounge + dig:Curator of Mysteries}`  
  engine dropped [cast Brainstorm]; every action pays with pay1[Watery Grave#59:U] ; pay2[Steam Vents#45:U Island#19:U Cascade Bluffs#10:C] ; pay3[Steam Vents#46:U] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Steam Vents#45:U Cascade Bluffs#10:C Watery Grave#59:U] ; pay3[Steam Vents#46:U]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Steam Vents#45, Island#19, Steam Vents#46, Cascade Bluffs#10, Watery Grave#59, Unpredictable Cyclone#56, Emrakul, the Aeons Torn#16] hand=[Brainsurge, Curator of Mysteries, Curator of Mysteries, Brainstorm, Xander's Lounge, Toxic Deluge] float=- life=8  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30006689 --game-index 6689`
- **unpredictable_cyclone** x114 -- T4 m1 `{land=Island + dig:Curator of Mysteries}`  
  engine dropped [cast Ponder]; every action pays with pay1[Island#21:U] ; pay2[Cascade Bluffs#10:C Mountain#25:R Steam Vents#45:U] ; pay3[Watery Grave#59:U], reaching a board the menu's other order of these actions does not (Cascade Bluffs#10 tapped by oracle only; hand cards differ; graveyard size differs; library size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Cascade Bluffs#10:C Mountain#25:R Steam Vents#45:U] ; pay3[Watery Grave#59:U]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on  
  board: board=[Watery Grave#59, Cascade Bluffs#10, Mountain#25, Steam Vents#45, Unpredictable Cyclone#56, Emrakul, the Aeons Torn#18] hand=[Brainsurge, Island, Curator of Mysteries, Toxic Deluge, Ponder] float=- life=15  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005009 --game-index 5009`
- **unpredictable_cyclone** x114 -- T4 m1 `{land=Scalding Tarn + dig:Curator of Mysteries}`  
  engine dropped [cast Brainstorm]; every action pays with pay1[Steam Vents#45:U] ; pay2[Myriad Landscape#28:C Island#19:U Cascade Bluffs#10:C] ; pay3[Island#20:U], reaching a board the menu's other order of these actions does not (Xander's Lounge#60 only in engine; Steam Vents#45 only in oracle; life engine 17 oracle 15; hand cards differ; graveyard size differs; library size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Myriad Landscape#28:C Island#20:U Steam Vents#45:U Cascade Bluffs#10:UU] ; pay3[(float only)]  
  board: board=[Myriad Landscape#28, Island#19, Cascade Bluffs#10, Island#20, Unpredictable Cyclone#55, Emrakul, the Aeons Torn#17] hand=[Brainsurge, Scalding Tarn, Brainstorm, Curator of Mysteries, Scalding Tarn] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005378 --game-index 5378`
- **unpredictable_cyclone** x112 -- T4 m1 `{land=Scalding Tarn + dig:Curator of Mysteries}`  
  engine dropped [cast Ponder]; every action pays with pay1[Steam Vents#46:U] ; pay2[Cascade Bluffs#10:C Steam Vents#45:U Mountain#25:R] ; pay3[Island#19:U], reaching a board the menu's other order of these actions does not (Xander's Lounge#60 only in engine; Steam Vents#46 only in oracle; life engine 12 oracle 10; hand cards differ; graveyard size differs; library size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Cascade Bluffs#10:C Mountain#25:R Steam Vents#46:U] ; pay3[Steam Vents#45:U]; a lost action was not   
  board: board=[Cascade Bluffs#10, Island#19, Steam Vents#45, Mountain#25, Unpredictable Cyclone#56, Emrakul, the Aeons Torn#15] hand=[Brainsurge, Myriad Landscape, Ponder, Scalding Tarn, Curator of Mysteries] float=- life=13  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005414 --game-index 5414`
- **unpredictable_cyclone** x100 -- T5 m1 `{no land + cast:Seething Song + cast:Ponder + cast:Unpredictable Cyclone + dig:Street Wraith}`  
  engine dropped [cast Ponder]; every action pays with pay1[Mountain#25:R Blood Crypt#1:B Myriad Landscape#28:C] ; pay2[Island#22:U] ; pay3[(float only)] ; pay4[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mountain#25:R Blood Crypt#1:B Myriad Landscape#28:C] ; pay2[Island#22:U] ; pay3[(float only)] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Island#22, Mountain#25, Blood Crypt#1, Myriad Landscape#28] hand=[Seething Song, Ponder, Remand, Unpredictable Cyclone, Remand, Curator of Mysteries, Street Wraith, Ulamog, the Infinite Gyre] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30006628 --game-index 6628`
- **unpredictable_cyclone** x96 -- T5 m1 `{land=Scalding Tarn + cast:Brainsurge + dig:Curator of Mysteries}`  
  engine dropped [cast Ponder, cast Curator of Mysteries]; every action pays with pay1[Steam Vents#46:U Steam Vents#45:U Xander's Lounge#60:U] ; pay2[Watery Grave#59:U] ; pay3[Island#23:U] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Steam Vents#46:U Steam Vents#45:U Xander's Lounge#60:U] ; pay2[Watery Grave#59:U] ; pay3[Island#23:U]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Steam Vents#46, Island#23, Xander's Lounge#60, Myriad Landscape#29, Steam Vents#45, Unpredictable Cyclone#55, Emrakul, the Aeons Torn#17, Curator of Mysteries#14] hand=[Brainsurge, Ponder, Curator of Mysteries, Scalding Tarn] float=- life=11  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30008290 --game-index 8290`
- **unpredictable_cyclone** x94 -- T4 m1 `{land=Mountain + dig:Curator of Mysteries}`  
  engine dropped [cast Ponder]; every action pays with pay1[Mountain#27:R Cascade Bluffs#10:UU] ; pay2[Steam Vents#46:U Myriad Landscape#28:C] ; pay3[Steam Vents#45:U] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Myriad Landscape#28:C Mountain#27:R Cascade Bluffs#10:UU] ; pay3[Steam Vents#45:U]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Steam Vents#46, Myriad Landscape#28, Steam Vents#45, Cascade Bluffs#10, Unpredictable Cyclone#58, Emrakul, the Aeons Torn#18] hand=[Mountain, Brainsurge, Curator of Mysteries, Ponder] float=- life=12  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30006274 --game-index 6274`
- **unpredictable_cyclone** x92 -- T4 m1 `{land=Mountain + dig:Curator of Mysteries}`  
  engine dropped [cast Brainstorm]; every action pays with pay1[Island#19:U] ; pay2[Myriad Landscape#29:C Mountain#26:R Cascade Bluffs#10:UU] ; pay3[Steam Vents#45:U], reaching a board the menu's other order of these actions does not (Curator of Mysteries#13 only in oracle; hand cards differ; library size differs; float engine {U} oracle -) || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Myriad Landscape#29:C Mountain#26:R Cascade Bluffs#10:UU] ; pay3[Steam Vents#45:U]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Steam Vents#45, Myriad Landscape#29, Cascade Bluffs#10, Island#19, Unpredictable Cyclone#56, Emrakul, the Aeons Torn#18] hand=[Curator of Mysteries, Brainsurge, Mountain, Remand, Brainstorm] float=- life=15  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30007545 --game-index 7545`
- **unpredictable_cyclone** x86 -- T6 m1 `{land=Blood Crypt + cast:Seething Song + cast:Seething Song + cast:Brainsurge + cast:Unpredictable Cyclone + dig:Street Wraith}`  
  engine dropped [cast Brainsurge]; every action pays with pay1[Steam Vents#45:U Island#22:U Blood Crypt#1:R] ; pay2[(float only)] ; pay3[Steam Vents#46:U] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Steam Vents#45:U Island#22:U Blood Crypt#1:R] ; pay2[(float only)] ; pay3[Steam Vents#46:U] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Steam Vents#45, Island#22, Steam Vents#46] hand=[Brainsurge, Seething Song, Emrakul, the Aeons Torn, Unpredictable Cyclone, Street Wraith, Remand, Seething Song, Blood Crypt] float=- life=14  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30006878 --game-index 6878`
- **unpredictable_cyclone** x84 -- T5 m1 `{no land + cast:Brainsurge + dig:Curator of Mysteries}`  
  engine dropped [cast Curator of Mysteries]; every action pays with pay1[Mountain#26:R Mountain#25:R Island#22:U] ; pay2[Island#20:U], reaching a board the menu's other order of these actions does not (Watery Grave#59 tapped by engine only; Mountain#25 tapped by oracle only; Myriad Landscape#30 only in oracle; hand size differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mountain#26:R Mountain#25:R Island#22:U] ; pay2[Island#20:U]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Watery Grave#59, Mountain#26, Mountain#25, Island#22, Island#20, Unpredictable Cyclone#56, Emrakul, the Aeons Torn#18] hand=[Curator of Mysteries, Emrakul, the Aeons Torn, Brainsurge, Unpredictable Cyclone, Myriad Landscape] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005017 --game-index 5017`
- ... 6430 more signatures in flags.jsonl

## APPLY-DROP-ORDER (4296 flags, 1341 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **landfallelementals** x62 -- T4 m1 `{land=Terramorphic Expanse + cast:Risen Reef + cast:Akoum Hellhound + cast:Flamekin Harbinger}`  
  engine dropped [cast Flamekin Harbinger]; the order Akoum Hellhound > Risen Reef > Flamekin Harbinger pays every action with pay1[Smokebraider#50:*(Elemental-only)] ; pay2[Forest#17:G Myriad Landscape#36:C] ; pay3[Mountain#32:R]  
  board: board=[Mountain#32, Forest#17, Brushfire Elemental#8, Myriad Landscape#36, Smokebraider#50] hand=[Risen Reef, Akoum Hellhound, Flamekin Harbinger, Myriad Landscape, Terramorphic Expanse, Brushfire Elemental] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30016173 --game-index 16173`
- **landfallelementals** x54 -- T4 m1 `{land=Myriad Landscape + cast:Risen Reef + cast:Akoum Hellhound + cast:Flamekin Harbinger}`  
  engine dropped [cast Flamekin Harbinger]; the order Akoum Hellhound > Risen Reef > Flamekin Harbinger pays every action with pay1[Smokebraider#50:*(Elemental-only)] ; pay2[Forest#17:G Myriad Landscape#36:C] ; pay3[Mountain#32:R]  
  board: board=[Mountain#32, Forest#17, Brushfire Elemental#8, Myriad Landscape#36, Smokebraider#50] hand=[Risen Reef, Akoum Hellhound, Flamekin Harbinger, Myriad Landscape, Terramorphic Expanse, Brushfire Elemental] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30016173 --game-index 16173`
- **unpredictable_cyclone** x43 -- T4 m1 `{land=Island + cast:Curator of Mysteries + cast:Seething Song + dig:Unpredictable Cyclone}`  
  engine dropped [cast Seething Song]; the order Seething Song > Curator of Mysteries > dig:Unpredictable Cyclone pays every action with pay1[Steam Vents#45:U Steam Vents#46:R Island#21:U] ; pay2[Cascade Bluffs#10:UU] ; pay3[(float only)]  
  board: board=[Steam Vents#45, Cascade Bluffs#10, Steam Vents#46] hand=[Mountain, Remand, Street Wraith, Unpredictable Cyclone, Curator of Mysteries, Seething Song, Curator of Mysteries, Island] float=- life=12  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005656 --game-index 5656`
- **unpredictable_cyclone** x39 -- T4 m1 `{land=Mountain + cast:Curator of Mysteries + cast:Seething Song + dig:Unpredictable Cyclone}`  
  engine dropped [cast Seething Song]; the order Seething Song > Curator of Mysteries > dig:Unpredictable Cyclone pays every action with pay1[Steam Vents#45:U Steam Vents#46:U Mountain#25:R] ; pay2[Cascade Bluffs#10:UU] ; pay3[(float only)]  
  board: board=[Steam Vents#45, Cascade Bluffs#10, Steam Vents#46] hand=[Mountain, Remand, Street Wraith, Unpredictable Cyclone, Curator of Mysteries, Seething Song, Curator of Mysteries, Island] float=- life=12  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005656 --game-index 5656`
- **unpredictable_cyclone** x35 -- T4 m1 `{land=Island + cast:Seething Song + cast:Curator of Mysteries + dig:Unpredictable Cyclone}`  
  engine dropped [cast Seething Song]; the order Seething Song > dig:Unpredictable Cyclone > Curator of Mysteries pays every action with pay1[Steam Vents#46:U Xander's Lounge#60:R Island#23:U] ; pay2[Cascade Bluffs#10:UU] ; pay3[(float only)]  
  board: board=[Steam Vents#46, Cascade Bluffs#10, Xander's Lounge#60] hand=[Mountain, Seething Song, Unpredictable Cyclone, Curator of Mysteries, Island] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30005208 --game-index 5208`
- **landfallelementals** x33 -- T4 m1 `{land=Terramorphic Expanse + cast:Risen Reef + cast:Akoum Hellhound + cast:Flamekin Harbinger + act:Terramorphic Expanse(x1)}`  
  engine dropped [cast Flamekin Harbinger]; the order Akoum Hellhound > Risen Reef > Flamekin Harbinger > act:Terramorphic Expanse(x1)@54 pays every action with pay1[Smokebraider#50:*(Elemental-only)] ; pay2[Forest#17:G Myriad Landscape#36:C] ; pay3[Mountain#32:R] ; pay4[(float only)]  
  board: board=[Mountain#32, Forest#17, Brushfire Elemental#8, Myriad Landscape#36, Smokebraider#50] hand=[Risen Reef, Akoum Hellhound, Flamekin Harbinger, Myriad Landscape, Terramorphic Expanse, Brushfire Elemental] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30016173 --game-index 16173`
- **landfallelementals** x33 -- T4 m1 `{land=Forest + cast:Risen Reef + cast:Akoum Hellhound + cast:Flamekin Harbinger}`  
  engine dropped [cast Flamekin Harbinger]; the order Akoum Hellhound > Risen Reef > Flamekin Harbinger pays every action with pay1[Smokebraider#48:*(Elemental-only)] ; pay2[Forest#17:G Forest#18:G] ; pay3[Mountain#34:R]  
  board: board=[Forest#17, Aether Vial#1, Flamekin Harbinger#13, Mountain#34, Smokebraider#48, Metallic Mimic#27] hand=[Risen Reef, Akoum Hellhound, Flamekin Harbinger, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30016368 --game-index 16368`
- **unpredictable_cyclone** x32 -- T4 m1 `{land=Island + cast:Seething Song + cast:Curator of Mysteries + dig:Unpredictable Cyclone}`  
  engine dropped [cast Seething Song]; the order Seething Song > dig:Unpredictable Cyclone > Curator of Mysteries pays every action with pay1[Steam Vents#45:U Steam Vents#46:R Island#22:U] ; pay2[Cascade Bluffs#10:UU] ; pay3[(float only)]  
  board: board=[Steam Vents#45, Steam Vents#46, Cascade Bluffs#10] hand=[Long-Term Plans, Seething Song, Curator of Mysteries, Unpredictable Cyclone, Scalding Tarn, Island, Remand, Toxic Deluge] float=- life=15  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30006104 --game-index 6104`
- **landfallelementals** x31 -- T4 m1 `{no land + cast:Risen Reef + cast:Akoum Hellhound + cast:Flamekin Harbinger}`  
  engine dropped [cast Flamekin Harbinger]; the order Akoum Hellhound > Risen Reef > Flamekin Harbinger pays every action with pay1[Smokebraider#48:*(Elemental-only)] ; pay2[Waterlogged Grove#60:G Myriad Landscape#36:C] ; pay3[Mountain#32:R]  
  board: board=[Mountain#32, Waterlogged Grove#60, Smokebraider#48, Akoum Hellhound#4, Mossborn Hydra#28, Myriad Landscape#36] hand=[Forest, Risen Reef, Mossborn Hydra, Akoum Hellhound, Flamekin Harbinger] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30014187 --game-index 14187`
- **landfallelementals** x31 -- T4 m1 `{no land + vial:Risen Reef + cast:Mossborn Hydra + cast:Flamekin Harbinger}`  
  engine dropped [cast Flamekin Harbinger]; the order vial:Risen Reef > Flamekin Harbinger > Mossborn Hydra pays every action with pay1[Smokebraider#48:*(Elemental-only)] ; pay2[Forest#19:G Waterlogged Grove#60:G]  
  board: board=[Forest#19, Aether Vial#3, Waterlogged Grove#60, Flamekin Harbinger#14, Smokebraider#48] hand=[Risen Reef, Mossborn Hydra, Flamekin Harbinger] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30015826 --game-index 15826`
- **landfallelementals** x26 -- T4 m1 `{land=Terramorphic Expanse + cast:Risen Reef + cast:Akoum Hellhound + cast:Flamekin Harbinger + act:Terramorphic Expanse(x1)}`  
  engine dropped [cast Flamekin Harbinger]; the order Akoum Hellhound > Risen Reef > Flamekin Harbinger > act:Terramorphic Expanse(x1)@54 pays every action with pay1[(float only)] ; pay2[Smokebraider#50:*(Elemental-only)] ; pay3[Forest#17:G Myriad Landscape#36:C] ; pay4[Mountain#32:R]  
  board: board=[Mountain#32, Forest#17, Brushfire Elemental#8, Myriad Landscape#36, Smokebraider#50] hand=[Risen Reef, Akoum Hellhound, Flamekin Harbinger, Myriad Landscape, Terramorphic Expanse, Brushfire Elemental] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30016173 --game-index 16173`
- **fivecolour** x21 -- T5 m1 `{land=Overgrown Tomb + cast:Unite the Coalition(x5) + cast:Faeburrow Elder + equip:Lightning Greaves}`  
  engine dropped [cast Faeburrow Elder]; the order Faeburrow Elder > Unite the Coalition > equip:Lightning Greaves@26 pays every action with pay1[Jetmir's Garden#25:W Breeding Pool#9:G Overgrown Tomb#40:B] ; pay2[(float only)] ; pay3[Godless Shrine#21:W Stomping Ground#46:R Birds of Paradise#2:U Birds of Paradise#5:B Faeburrow Elder#19:W]  
  board: board=[Godless Shrine#21, Stomping Ground#46, Birds of Paradise#2, Jetmir's Garden#25, Lightning Greaves#26, Birds of Paradise#5, Breeding Pool#9, Mana Cannons#31] hand=[Unite the Coalition, Overgrown Tomb, Jared Carthalion, Faeburrow Elder] float=- life=12  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30013563 --game-index 13563`
- ... 1329 more signatures in flags.jsonl

## DOMINATED-CREATURE (6665 flags, 2454 signatures)

_the engine's payment taps a creature that could attack, where another payment keeps it untapped and loses nothing._

- **melira_pod** x120 -- T5 m1 `{no land + cast:Chord of Calling(x2) + pod:Birthing Pod}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Orzhov Basilica#47:W Blooming Marsh#8:G Razorverge Thicket#50:G keep-floating {W}] ; pay2[Ignoble Hierarch#32:G]; the engine ended board=[Birthing Pod#3(T), Birds of Paradise#1(T), Orzhov Basilica#47(T), Blooming Marsh#8(T), Felidar Guardian#28(T), Razorverge Thicket#50(T), Kitchen Finks#36(T), Melira, Sylvok Outcast#44(new), Melira, Sylvok Outcast#43(new)] hand=[Birthing Pod, Severance Priest] float=- life=16  
  board: board=[Birthing Pod#3, Birds of Paradise#1, Orzhov Basilica#47, Blooming Marsh#8, Felidar Guardian#28, Razorverge Thicket#50, Ignoble Hierarch#32, Kitchen Finks#36] hand=[Chord of Calling, Birthing Pod, Severance Priest] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30008756 --game-index 8756`
- **fivecolour** x42 -- T4 m1 `{no land + cast:Jared Carthalion + garth:Garth One-Eye}`  
  keeps Birds of Paradise untapped (attacker); keeps Breeding Pool untapped; keeps Jetmir's Garden untapped; keeps Overgrown Tomb untapped -- by paying pay1[(float only)] ; pay2[Faeburrow Elder#18:W]; the engine ended board=[Breeding Pool#9(T), Birds of Paradise#3(T), Overgrown Tomb#40(T), Faeburrow Elder#18(T), Jetmir's Garden#25(T), Deathrite Shaman#15, Garth One-Eye#20(T), Black Lotus#1000(new), Jared Carthalion#24(new)] hand=[Faeburrow Elder, Progenitus] float={W}{B}{R}{G} life=13  
  board: board=[Breeding Pool#9, Birds of Paradise#3, Overgrown Tomb#40, Faeburrow Elder#18, Jetmir's Garden#25, Deathrite Shaman#15, Garth One-Eye#20] hand=[Jared Carthalion, Faeburrow Elder, Progenitus] float=- life=13  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30013607 --game-index 13607`
- **fivecolour** x42 -- T6 m1 `{no land + cast:Jared Carthalion + garth:Garth One-Eye}`  
  keeps Birds of Paradise untapped (attacker); keeps Breeding Pool untapped; keeps Steam Vents untapped -- by paying pay1[(float only)] ; pay2[Faeburrow Elder#18:W]; the engine ended board=[Breeding Pool#9(T), Birds of Paradise#5(T), Steam Vents#45(T), Faeburrow Elder#18(T), Deathrite Shaman#15, Cosmic Spider-Man#10, Garth One-Eye#20(T), Jared Carthalion#23(new)] hand=[Unite the Coalition] float={W}{U}{G}{G}{G}{G} life=25  
  board: board=[Breeding Pool#9, Birds of Paradise#5, Steam Vents#45, Faeburrow Elder#18, Deathrite Shaman#15, Cosmic Spider-Man#10, Garth One-Eye#20] hand=[Jared Carthalion, Unite the Coalition] float=- life=25  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30015485 --game-index 15485`
- **melira_pod** x39 -- T5 m1 `{land=Llanowar Wastes + cast:Birthing Pod + pod:Birthing Pod + pod:Birthing Pod}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Blooming Marsh#8:B Ignoble Hierarch#30:B Llanowar Wastes#41:C] ; pay2[Orzhov Basilica#47:W] ; pay3[Razorverge Thicket#50:G] ; pay4[(no payment exists)]; the engine ended board=[Birds of Paradise#1(T), Orzhov Basilica#47(T), Razorverge Thicket#50(T), Ravenous Chupacabra#49, Blooming Marsh#8(T), Birthing Pod#3(T), Llanowar Wastes#41(T)(new), Birthing Pod#4(T)(new), Melira, Sylvok Outcast#44(new), Kitchen Finks#34(new), Murderous Redcap#46(new)] hand=[Branchloft Pathway, Blooming Marsh] float=- life=16  
  board: board=[Birds of Paradise#1, Orzhov Basilica#47, Razorverge Thicket#50, Ravenous Chupacabra#49, Blooming Marsh#8, Ignoble Hierarch#30, Birthing Pod#3, Kitchen Finks#34] hand=[Birthing Pod, Branchloft Pathway, Blooming Marsh, Llanowar Wastes] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30007597 --game-index 7597`
- **melira_pod** x39 -- T5 m1 `{land=Branchloft Pathway + cast:Birthing Pod + pod:Birthing Pod + pod:Birthing Pod}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Blooming Marsh#8:B Ignoble Hierarch#30:B Branchloft Pathway#12:G] ; pay2[Orzhov Basilica#47:W] ; pay3[Razorverge Thicket#50:G] ; pay4[(no payment exists)]; the engine ended board=[Birds of Paradise#1(T), Orzhov Basilica#47(T), Razorverge Thicket#50(T), Ravenous Chupacabra#49, Blooming Marsh#8(T), Birthing Pod#3(T), Branchloft Pathway#12(T)(new), Birthing Pod#4(T)(new), Melira, Sylvok Outcast#44(new), Kitchen Finks#34(new), Murderous Redcap#46(new)] hand=[Blooming Marsh, Llanowar Wastes] float=- life=16  
  board: board=[Birds of Paradise#1, Orzhov Basilica#47, Razorverge Thicket#50, Ravenous Chupacabra#49, Blooming Marsh#8, Ignoble Hierarch#30, Birthing Pod#3, Kitchen Finks#34] hand=[Birthing Pod, Branchloft Pathway, Blooming Marsh, Llanowar Wastes] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30007597 --game-index 7597`
- **melira_pod** x39 -- T5 m1 `{land=Branchloft Pathway + cast:Birthing Pod + pod:Birthing Pod + pod:Birthing Pod}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Blooming Marsh#8:B Ignoble Hierarch#30:B Boulderloft Pathway#12:W] ; pay2[Orzhov Basilica#47:W] ; pay3[Razorverge Thicket#50:G] ; pay4[(no payment exists)]; the engine ended board=[Birds of Paradise#1(T), Orzhov Basilica#47(T), Razorverge Thicket#50(T), Ravenous Chupacabra#49, Blooming Marsh#8(T), Birthing Pod#3(T), Boulderloft Pathway#12(T)(new), Birthing Pod#4(T)(new), Melira, Sylvok Outcast#44(new), Kitchen Finks#34(new), Murderous Redcap#46(new)] hand=[Blooming Marsh, Llanowar Wastes] float=- life=16  
  board: board=[Birds of Paradise#1, Orzhov Basilica#47, Razorverge Thicket#50, Ravenous Chupacabra#49, Blooming Marsh#8, Ignoble Hierarch#30, Birthing Pod#3, Kitchen Finks#34] hand=[Birthing Pod, Branchloft Pathway, Blooming Marsh, Llanowar Wastes] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30007597 --game-index 7597`
- **fivecolour** x36 -- T6 m1 `{land=Mountain + cast:Jared Carthalion + cast:Jared Carthalion + garth:Garth One-Eye}`  
  keeps Birds of Paradise untapped (attacker); keeps Mountain untapped; keeps Overgrown Tomb untapped; keeps Zagoth Triome untapped -- by paying pay1[(float only)] ; pay2[Bloom Tender#7:W] ; pay3[Faeburrow Elder#19:W]; the engine ended board=[Overgrown Tomb#40(T), Birds of Paradise#2(T), Deathrite Shaman#12, Deathrite Shaman#13, Bloom Tender#7(T), Faeburrow Elder#19(T), Garth One-Eye#20(T), Two-Headed Hellkite#47, Zagoth Triome#60(T), Mountain#36(T)(new), Jared Carthalion#23(new)] hand=[Nicol Bolas, Planeswalker, Nicol Bolas, Planeswalker, Faeburrow Elder] float={W}{B}{B}{R}{G}{G}{G} life=17  
  board: board=[Overgrown Tomb#40, Birds of Paradise#2, Deathrite Shaman#12, Deathrite Shaman#13, Bloom Tender#7, Faeburrow Elder#19, Garth One-Eye#20, Two-Headed Hellkite#47, Zagoth Triome#60] hand=[Jared Carthalion, Nicol Bolas, Planeswalker, Nicol Bolas, Planeswalker, Faeburrow Elder, Jared Carthalion, Mountain] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30015089 --game-index 15089`
- **fivecolour** x36 -- T6 m1 `{no land + cast:Jared Carthalion + cast:Jared Carthalion + garth:Garth One-Eye}`  
  keeps Birds of Paradise untapped (attacker); keeps Overgrown Tomb untapped; keeps Zagoth Triome untapped -- by paying pay1[(float only)] ; pay2[Bloom Tender#7:W] ; pay3[Faeburrow Elder#19:W]; the engine ended board=[Overgrown Tomb#40(T), Birds of Paradise#2(T), Deathrite Shaman#12, Deathrite Shaman#13, Bloom Tender#7(T), Faeburrow Elder#19(T), Garth One-Eye#20(T), Two-Headed Hellkite#47, Zagoth Triome#60(T), Jared Carthalion#23(new)] hand=[Nicol Bolas, Planeswalker, Nicol Bolas, Planeswalker, Faeburrow Elder, Mountain] float={W}{B}{B}{G}{G}{G} life=17  
  board: board=[Overgrown Tomb#40, Birds of Paradise#2, Deathrite Shaman#12, Deathrite Shaman#13, Bloom Tender#7, Faeburrow Elder#19, Garth One-Eye#20, Two-Headed Hellkite#47, Zagoth Triome#60] hand=[Jared Carthalion, Nicol Bolas, Planeswalker, Nicol Bolas, Planeswalker, Faeburrow Elder, Jared Carthalion, Mountain] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30015089 --game-index 15089`
- **fivecolour** x35 -- T4 m1 `{land=Scalding Tarn + cast:Jared Carthalion + cast:Birds of Paradise + garth:Garth One-Eye}`  
  keeps Birds of Paradise untapped (attacker); keeps Godless Shrine untapped; keeps Stomping Ground untapped -- by paying pay1[(float only)] ; pay2[Faeburrow Elder#16:W Breeding Pool#9:G] ; pay3[(float only)]; the engine ended board=[Stomping Ground#46(T), Birds of Paradise#2(T), Godless Shrine#21(T), Faeburrow Elder#16(T), Garth One-Eye#20(T), Breeding Pool#9(T)(new), Black Lotus#1000(new), Jared Carthalion#23(new), Birds of Paradise#3(new)] hand=[Wooded Foothills, Unite the Coalition, Faeburrow Elder] float={W}{W}{R} life=12  
  board: board=[Stomping Ground#46, Birds of Paradise#2, Godless Shrine#21, Faeburrow Elder#16, Garth One-Eye#20] hand=[Jared Carthalion, Wooded Foothills, Birds of Paradise, Unite the Coalition, Scalding Tarn, Faeburrow Elder] float=- life=15  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30016467 --game-index 16467`
- **melira_pod** x32 -- T3 m1 `{land=Razorverge Thicket + cast:Birthing Pod + pod:Birthing Pod}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Ignoble Hierarch#32:B Orzhov Basilica#47:W] ; pay2[Razorverge Thicket#51:G]; the engine ended board=[Birds of Paradise#2(T), Orzhov Basilica#47(T), Razorverge Thicket#51(T)(new), Birthing Pod#5(T)(new), Melira, Sylvok Outcast#43(new)] hand=[Murderous Redcap, Chord of Calling, Kitchen Finks, Recruiter of the Guard] float=- life=16  
  board: board=[Birds of Paradise#2, Ignoble Hierarch#32, Orzhov Basilica#47] hand=[Murderous Redcap, Chord of Calling, Kitchen Finks, Recruiter of the Guard, Razorverge Thicket, Birthing Pod] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30005917 --game-index 5917`
- **melira_pod** x30 -- T5 m1 `{no land + cast:Chord of Calling(x1) + pod:Birthing Pod}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Orzhov Basilica#47:W Blooming Marsh#8:G Razorverge Thicket#50:G] ; pay2[Ignoble Hierarch#32:G keep-floating {W}]; the engine ended board=[Birthing Pod#3(T), Birds of Paradise#1(T), Orzhov Basilica#47(T), Blooming Marsh#8(T), Felidar Guardian#28(T), Razorverge Thicket#50(T), Kitchen Finks#36(T), Carrion Feeder#17(new), Melira, Sylvok Outcast#43(new)] hand=[Birthing Pod, Severance Priest] float={B} life=16  
  board: board=[Birthing Pod#3, Birds of Paradise#1, Orzhov Basilica#47, Blooming Marsh#8, Felidar Guardian#28, Razorverge Thicket#50, Ignoble Hierarch#32, Kitchen Finks#36] hand=[Chord of Calling, Birthing Pod, Severance Priest] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30008756 --game-index 8756`
- **fivecolour** x22 -- T4 m1 `{no land + cast:Mana Cannons + garth:Garth One-Eye}`  
  keeps Birds of Paradise untapped (attacker); keeps Blood Crypt untapped; keeps Breeding Pool untapped -- by paying pay1[Faeburrow Elder#16:W keep-floating {W}{G}] ; pay2[(float only)]; the engine ended board=[Breeding Pool#9(T), Birds of Paradise#2(T), Blood Crypt#6(T), Faeburrow Elder#16(T), Garth One-Eye#20(T), Mana Cannons#31(new), Jetmir's Garden#25(T)(new)] hand=[Jared Carthalion, Unite the Coalition, Jared Carthalion, Two-Headed Hellkite, Misty Rainforest] float={U}{B}{G} life=13  
  board: board=[Breeding Pool#9, Birds of Paradise#2, Blood Crypt#6, Faeburrow Elder#16, Garth One-Eye#20] hand=[Mana Cannons, Jared Carthalion, Unite the Coalition, Jared Carthalion, Two-Headed Hellkite, Misty Rainforest] float=- life=14  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30012835 --game-index 12835`
- ... 2442 more signatures in flags.jsonl

## DOMINATED-ONESHOT (66 flags, 26 signatures)

_the engine spends a one-shot source (Treasure, Spawn) where another payment keeps it._

- **mirrorwing_dragon** x7 -- T4 m1 `{land=Forest + cast:Zada, Hedron Grinder + cast:Gold Rush@43 + cast:Fortifying Draught@43 + cast:Oracle's Restoration@43}`  
  keeps Treasure Token -- by paying pay1[Sandstone Needle#54:R Sandstone Needle#55:R] ; pay2[Forest#14:G] ; pay3[Forest#13:G Elvish Mystic#5:G] ; pay4[Treasure Token#1001:G]; the engine ended board=[Forest#13(T), Elvish Mystic#5(T), Sandstone Needle#55(T), Mirrorwing Dragon#43, Forest#14(T)(new), Zada, Hedron Grinder#57(new), Treasure Token#1003(new)] hand=[Gruul Turf, Frontline Heroism, Frontline Heroism] float={R} life=29  
  board: board=[Forest#13, Elvish Mystic#5, Sandstone Needle#54, Sandstone Needle#55, Mirrorwing Dragon#43] hand=[Zada, Hedron Grinder, Gold Rush, Fortifying Draught, Oracle's Restoration, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30016009 --game-index 16009`
- **mirrorwing_dragon** x5 -- T4 m1 `{no land + cast:Fists of Flame@43 + cast:Fortifying Draught@43 + cast:Ignoble Hierarch}`  
  keeps Treasure Token -- by paying pay1[Game Trail#29:G] ; pay2[Sandstone Needle#54:R] ; pay3[Forest#13:G]; the engine ended board=[Ignoble Hierarch#39, Forest#13(T), Mirrorwing Dragon#43, Ignoble Hierarch#40(new), Gruul Turf#37(T)(new)] hand=[Forest, Forest, Mirrorwing Dragon, Gruul Turf, Game Trail] float={R} life=26  
  board: board=[Game Trail#29, Ignoble Hierarch#39, Sandstone Needle#54, Treasure Token#1000, Forest#13, Mirrorwing Dragon#43] hand=[Fists of Flame, Forest, Forest, Fortifying Draught, Ignoble Hierarch] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30016917 --game-index 16917`
- **mirrorwing_dragon** x4 -- T4 m1 `{land=Forest + cast:Zada, Hedron Grinder + cast:Gold Rush@43 + cast:Oracle's Restoration@43}`  
  keeps Treasure Token -- by paying pay1[Elvish Mystic#5:G Sandstone Needle#55:R Forest#14:G Sandstone Needle#54:R keep-floating {R}{G}] ; pay2[Forest#13:G keep-floating {G}] ; pay3[(float only)]; the engine ended board=[Forest#13(T), Elvish Mystic#5(T), Sandstone Needle#55(T), Mirrorwing Dragon#43, Forest#14(T)(new), Zada, Hedron Grinder#57(new), Treasure Token#1002(new), Treasure Token#1003(new)] hand=[Fortifying Draught, Gruul Turf, Frontline Heroism, Frontline Heroism] float={R} life=23  
  board: board=[Forest#13, Elvish Mystic#5, Sandstone Needle#54, Sandstone Needle#55, Mirrorwing Dragon#43] hand=[Zada, Hedron Grinder, Gold Rush, Fortifying Draught, Oracle's Restoration, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30016009 --game-index 16009`
- **mirrorwing_dragon** x4 -- T4 m1 `{land=Game Trail + cast:Oracle's Restoration@57 + cast:Gold Rush@57}`  
  keeps Treasure Token -- by paying pay1[Game Trail#31:G] ; pay2[Game Trail#29:G Sandstone Needle#54:R Game Trail#30:G] ; pay3[Treasure Token#1000:G keep-floating {G}] ; pay4[(float only)]; the engine ended board=[Game Trail#29(T), Game Trail#30(T), Zada, Hedron Grinder#57, Game Trail#31(T)(new)] hand=[Frontline Heroism, Forest, Mountain] float={R} life=22  
  board: board=[Game Trail#29, Sandstone Needle#54, Game Trail#30, Zada, Hedron Grinder#57] hand=[Frontline Heroism, Oracle's Restoration, Forest, Oracle's Restoration, Gold Rush, Game Trail] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30016256 --game-index 16256`
- **mirrorwing_dragon** x4 -- T4 m1 `{land=Forest + cast:Oracle's Restoration@57 + cast:Gold Rush@57}`  
  keeps Treasure Token -- by paying pay1[Game Trail#29:G] ; pay2[Sandstone Needle#54:R Game Trail#30:G] ; pay3[Treasure Token#1000:G] ; pay4[Forest#13:G]; the engine ended board=[Game Trail#29(T), Game Trail#30(T), Zada, Hedron Grinder#57, Forest#13(T)(new)] hand=[Frontline Heroism, Game Trail, Mountain] float={R} life=22  
  board: board=[Game Trail#29, Sandstone Needle#54, Game Trail#30, Zada, Hedron Grinder#57] hand=[Frontline Heroism, Oracle's Restoration, Forest, Oracle's Restoration, Gold Rush, Game Trail] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30016256 --game-index 16256`
- **mirrorwing_dragon** x4 -- T4 m1 `{land=Game Trail + cast:Oracle's Restoration@1001 + cast:Elvish Mystic}`  
  keeps Treasure Token -- by paying pay1[Gruul Turf#37:R Game Trail#32:G] ; pay2[(float only)]; the engine ended board=[Ignoble Hierarch#41, Rootbound Crag#53, Frontline Heroism#26, 1/1 Soldier Token#1001, Gruul Turf#37(T), Game Trail#32(T)(new), Elvish Mystic#7(new), 1/1 Soldier Token#1002(new)] hand=[Forest, Forest, Gold Rush] float={R}{C} life=22  
  board: board=[Ignoble Hierarch#41, Rootbound Crag#53, Treasure Token#1000, Frontline Heroism#26, 1/1 Soldier Token#1001, Gruul Turf#37] hand=[Oracle's Restoration, Elvish Mystic, Forest, Game Trail] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30018332 --game-index 18332`
- **mirrorwing_dragon** x4 -- T4 m1 `{land=Forest + cast:Oracle's Restoration@1001 + cast:Elvish Mystic}`  
  keeps Treasure Token -- by paying pay1[Gruul Turf#37:R Forest#17:G] ; pay2[(float only)]; the engine ended board=[Ignoble Hierarch#41, Rootbound Crag#53, Frontline Heroism#26, 1/1 Soldier Token#1001, Gruul Turf#37(T), Forest#17(T)(new), Elvish Mystic#7(new), 1/1 Soldier Token#1002(new)] hand=[Game Trail, Forest, Gold Rush] float={R}{C} life=22  
  board: board=[Ignoble Hierarch#41, Rootbound Crag#53, Treasure Token#1000, Frontline Heroism#26, 1/1 Soldier Token#1001, Gruul Turf#37] hand=[Oracle's Restoration, Elvish Mystic, Forest, Game Trail] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30018332 --game-index 18332`
- **mirrorwing_dragon** x3 -- T5 m1 `{no land + cast:Fortifying Draught@43 + cast:Gold Rush@43 + cast:Gold Rush@43 + cast:Ancestral Anger@43}`  
  keeps Treasure Token -- by paying pay1[Sandstone Needle#54:R] ; pay2[Gruul Turf#38:R] ; pay3[Treasure Token#1000:G] ; pay4[Forest#18:G]; the engine ended board=[Gruul Turf#38(T), Forest#18(T), Mirrorwing Dragon#43] hand=[Ancestral Anger] float={R} life=22  
  board: board=[Gruul Turf#38, Sandstone Needle#54, Forest#18, Mirrorwing Dragon#43] hand=[Fortifying Draught, Gold Rush, Gold Rush, Ancestral Anger] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30016508 --game-index 16508`
- **mirrorwing_dragon** x2 -- T5 m1 `{land=Forest + cast:Fists of Flame@46 + cast:Elvish Mystic}`  
  keeps Treasure Token -- by paying pay1[Gruul Turf#37:R] ; pay2[Sandstone Needle#54:R] ; pay3[Ignoble Hierarch#40:G] ; pay4[Forest#14:G]; the engine ended board=[Gruul Turf#37(T), Sandstone Needle#56, Ignoble Hierarch#40(T), Sandstone Needle#54(T), Mirrorwing Dragon#46, Forest#14(T)(new), Elvish Mystic#5(new), Treasure Token#1001(new), Treasure Token#1002(new)] hand=[Zada, Hedron Grinder, Elvish Mystic, Oracle's Restoration, Ignoble Hierarch, Forest] float={R} life=23  
  board: board=[Gruul Turf#37, Sandstone Needle#56, Ignoble Hierarch#40, Sandstone Needle#54, Mirrorwing Dragon#46] hand=[Fists of Flame, Elvish Mystic, Gold Rush, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30015486 --game-index 15486`
- **mirrorwing_dragon** x2 -- T4 m1 `{land=Game Trail + cast:Ancestral Anger@43 + cast:Ignoble Hierarch}`  
  keeps Treasure Token -- by paying pay1[Ignoble Hierarch#41:G] ; pay2[Sandstone Needle#55:R] ; pay3[Forest#14:G] ; pay4[Rootbound Crag#52:G]; the engine ended board=[Forest#14(T), Ignoble Hierarch#41(T), Rootbound Crag#52(T), Mirrorwing Dragon#43, Game Trail#32(T)(new), Ignoble Hierarch#39(new), Treasure Token#1001(new), Treasure Token#1002(new)] hand=[Sandstone Needle, Frontline Heroism, Frontline Heroism, Mountain, Forest] float={R} life=23  
  board: board=[Forest#14, Ignoble Hierarch#41, Sandstone Needle#55, Rootbound Crag#52, Mirrorwing Dragon#43] hand=[Ancestral Anger, Oracle's Restoration, Game Trail, Ignoble Hierarch] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30015872 --game-index 15872`
- **mirrorwing_dragon** x2 -- T5 m1 `{no land + cast:Gold Rush@43 + cast:Ancestral Anger@43}`  
  keeps Treasure Token -- by paying pay1[Sandstone Needle#54:R] ; pay2[Gruul Turf#38:R] ; pay3[Treasure Token#1000:G] ; pay4[Forest#18:G]; the engine ended board=[Gruul Turf#38(T), Forest#18(T), Mirrorwing Dragon#43] hand=[Ancestral Anger] float={R} life=22  
  board: board=[Gruul Turf#38, Sandstone Needle#54, Forest#18, Mirrorwing Dragon#43] hand=[Fortifying Draught, Gold Rush, Gold Rush, Ancestral Anger] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30016508 --game-index 16508`
- **mirrorwing_dragon** x2 -- T4 m1 `{land=Game Trail + cast:Fists of Flame@57}`  
  keeps Treasure Token -- by paying pay1[Sandstone Needle#54:R] ; pay2[Ignoble Hierarch#39:G] ; pay3[Gruul Turf#37:R]; the engine ended board=[Ignoble Hierarch#39(T), Zada, Hedron Grinder#57, Gruul Turf#37(T), Game Trail#29(T)(new), Treasure Token#1001(new)] hand=[Elvish Mystic, Rootbound Crag, Frontline Heroism, Frontline Heroism, Oracle's Restoration, Zada, Hedron Grinder] float={R} life=22  
  board: board=[Sandstone Needle#54, Ignoble Hierarch#39, Zada, Hedron Grinder#57, Gruul Turf#37] hand=[Fists of Flame, Oracle's Restoration, Elvish Mystic, Game Trail, Rootbound Crag, Frontline Heroism] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30016586 --game-index 16586`
- ... 14 more signatures in flags.jsonl

## DOMINATED-HASTE (5000 flags, 2584 signatures)

_another payment of the same plan gives a creature haste (Generator Servant's {C} spent on it)._

- **nivmizzetcombo** x14 -- T3 m1 `{no land + cast:Tandem Lookout + cast:Tandem Lookout + sac:Generator Servant}`  
  haste on Tandem Lookout -- by paying pay1[Saprazzan Skerry#49:U Spirebluff Canal#53:U keep-floating {C}(haste 1)] ; pay2[Izzet Signet#25:UR]; the engine ended board=[Spirebluff Canal#53(T), Izzet Signet#25(T), Tandem Lookout#60(new), Tandem Lookout#59(new)] hand=[Generator Servant, Spirebluff Canal] float=- life=20  
  board: board=[Saprazzan Skerry#49, Spirebluff Canal#53, Izzet Signet#25, Generator Servant#12] hand=[Generator Servant, Tandem Lookout, Tandem Lookout, Spirebluff Canal] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30005214 --game-index 5214`
- **nivmizzetcombo** x14 -- T3 m1 `{no land + cast:Ponder}`  
  haste on Tandem Lookout -- by paying pay1[Island#18:U] ; pay2[Cascade Bluffs#3:C Island#21:U keep-floating {C}{C}(haste 2)] ; pay3[Izzet Signet#26:UR]; the engine ended board=[Island#18(T), Cascade Bluffs#3(T), Island#21(T)(new), Izzet Signet#26(T)(new), Tandem Lookout#57(new)] hand=[Saprazzan Skerry, Niv-Mizzet, the Firemind, Preordain] float=- life=18  
  board: board=[Island#18, Cascade Bluffs#3, Generator Servant#12] hand=[Tandem Lookout, Ponder, Saprazzan Skerry, Izzet Signet, Niv-Mizzet, the Firemind, Island] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30005636 --game-index 5636`
- **nivmizzetcombo** x13 -- T3 m1 `{land=Island + cast:Ponder}`  
  haste on Tandem Lookout -- by paying pay1[Island#21:U] ; pay2[Island#18:U Cascade Bluffs#3:C keep-floating {C}{C}(haste 2)] ; pay3[Izzet Signet#26:UR]; the engine ended board=[Island#18(T), Cascade Bluffs#3(T), Island#21(T)(new), Izzet Signet#26(T)(new), Tandem Lookout#57(new)] hand=[Saprazzan Skerry, Niv-Mizzet, the Firemind, Preordain] float=- life=18  
  board: board=[Island#18, Cascade Bluffs#3, Generator Servant#12] hand=[Tandem Lookout, Ponder, Saprazzan Skerry, Izzet Signet, Niv-Mizzet, the Firemind, Island] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30005636 --game-index 5636`
- **nivmizzetcombo** x12 -- T3 m1 `{no land + cast:Tandem Lookout + cast:Tandem Lookout + sac:Generator Servant}`  
  haste on Tandem Lookout -- by paying pay1[Saprazzan Skerry#49:U Island#18:U keep-floating {C}(haste 1)] ; pay2[Izzet Signet#25:UR]; the engine ended board=[Island#18(T), Izzet Signet#25(T), Tandem Lookout#57(new), Tandem Lookout#60(new)] hand=[Niv-Mizzet, the Firemind, Izzet Signet] float=- life=18  
  board: board=[Saprazzan Skerry#49, Island#18, Izzet Signet#25, Generator Servant#12] hand=[Niv-Mizzet, the Firemind, Tandem Lookout, Tandem Lookout, Izzet Signet] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30005278 --game-index 5278`
- **nivmizzetcombo** x12 -- T4 m1 `{land=Island + cast:Niv-Mizzet, the Firemind + sac:Generator Servant + cast:Curiosity@32}`  
  haste on Niv-Mizzet, the Firemind -- by paying pay1[Spirebluff Canal#52:R Island#18:U Island#20:U Izzet Signet#27:UR Ferrous Lake#11:UR] ; pay2[(float only)]; the engine ended board=[Spirebluff Canal#52(T), Ferrous Lake#11(T), Island#18(T), Izzet Signet#27(T), Island#20(T)(new), Niv-Mizzet, the Firemind#32(new), Curiosity#7(new)@32] hand=[Sulfur Falls, Curiosity] float=- life=20  
  board: board=[Spirebluff Canal#52, Ferrous Lake#11, Generator Servant#14, Island#18, Izzet Signet#27] hand=[Sulfur Falls, Curiosity, Island, Niv-Mizzet, the Firemind, Curiosity] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30006405 --game-index 6405`
- **nivmizzetcombo** x12 -- T4 m1 `{land=Ferrous Lake + cast:Niv-Mizzet, the Firemind + sac:Generator Servant + cast:Ponder}`  
  haste on Niv-Mizzet, the Firemind -- by paying pay1[Saprazzan Skerry#49:U Spirebluff Canal#52:U Izzet Signet#26:UR Ferrous Lake#11:UR] ; pay2[(float only)]; the engine ended board=[Spirebluff Canal#52(T), Izzet Signet#26(T), Ferrous Lake#11(T)(new), Niv-Mizzet, the Firemind#32(new)] hand=[Tandem Lookout, Niv-Mizzet, the Firemind, Preordain, Generator Servant] float=- life=20  
  board: board=[Spirebluff Canal#52, Saprazzan Skerry#49, Izzet Signet#26, Generator Servant#12] hand=[Tandem Lookout, Niv-Mizzet, the Firemind, Niv-Mizzet, the Firemind, Preordain, Ponder, Ferrous Lake] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30006977 --game-index 6977`
- **nivmizzetcombo** x9 -- T3 m1 `{no land + cast:Izzet Signet + sac:Generator Servant + cast:Ponder}`  
  haste on Generator Servant -- by paying pay1[Spirebluff Canal#52:U keep-floating {C}(haste 1)] ; pay2[Cascade Bluffs#3:C Izzet Signet#24:UR] ; pay3[(float only)]; the engine ended board=[Spirebluff Canal#52(T), Cascade Bluffs#3(T), Izzet Signet#24(T)(new), Saprazzan Skerry#48(T)(new), Generator Servant#13(new)] hand=[Niv-Mizzet, the Firemind, Niv-Mizzet, Parun, Island] float=- life=20  
  board: board=[Spirebluff Canal#52, Cascade Bluffs#3, Generator Servant#12] hand=[Izzet Signet, Niv-Mizzet, the Firemind, Niv-Mizzet, Parun, Island, Ponder, Saprazzan Skerry] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30005134 --game-index 5134`
- **nivmizzetcombo** x9 -- T4 m1 `{land=Cascade Bluffs + cast:Niv-Mizzet, the Firemind + sac:Generator Servant}`  
  haste on Tandem Lookout -- by paying pay1[Saprazzan Skerry#48:U Sandstone Needle#46:R Cascade Bluffs#3:C] ; pay2[Spirebluff Canal#52:U Izzet Signet#24:UR] ; pay3[(float only)] ; pay4[(float only)]; the engine ended board=[Spirebluff Canal#52(T), Sandstone Needle#46(T), Izzet Signet#24(T), Cascade Bluffs#3(T)(new), Niv-Mizzet, the Firemind#30(T)(new), Tandem Lookout#57(new)] hand=[Remand, Spirebluff Canal, Tandem Lookout, Generator Servant, Sandstone Needle, Curiosity, Ferrous Lake, Island, Preordain, Izzet Signet, Ponder, Niv-Mizzet, the Firemind, Saprazzan Skerry, Izzet Signet, Island, Cascade Bluffs, Spirebluff Canal, Curiosity, Preordain, Island, Gitaxian Probe, Tandem Lookout] float=- li  
  board: board=[Spirebluff Canal#52, Saprazzan Skerry#48, Sandstone Needle#46, Izzet Signet#24, Generator Servant#12] hand=[Remand, Cascade Bluffs, Niv-Mizzet, the Firemind, Tandem Lookout, Spirebluff Canal] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30005218 --game-index 5218`
- **nivmizzetcombo** x8 -- T4 m1 `{no land + cast:Niv-Mizzet, the Firemind + sac:Generator Servant}`  
  haste on Tandem Lookout -- by paying pay1[Spirebluff Canal#52:U Saprazzan Skerry#48:U Sandstone Needle#46:R keep-floating {C}(haste 1)] ; pay2[Cascade Bluffs#3:C Izzet Signet#24:UR] ; pay3[(float only)] ; pay4[(float only)]; the engine ended board=[Spirebluff Canal#52(T), Sandstone Needle#46(T), Izzet Signet#24(T), Niv-Mizzet, the Firemind#30(T)(new), Cascade Bluffs#3(T)(new), Tandem Lookout#57(new)] hand=[Remand, Spirebluff Canal, Tandem Lookout, Generator Servant, Sandstone Needle, Curiosity, Ferrous Lake, Island, Preordain, Izzet Signet, Ponder, Niv-Mizzet, the Firemind, Saprazzan Skerry, Izzet Signet, Island, Cascade Bluffs, Spirebluff Canal, Curiosity, Preordain, Island, Gitaxian Probe,  
  board: board=[Spirebluff Canal#52, Saprazzan Skerry#48, Sandstone Needle#46, Izzet Signet#24, Generator Servant#12] hand=[Remand, Cascade Bluffs, Niv-Mizzet, the Firemind, Tandem Lookout, Spirebluff Canal] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30005218 --game-index 5218`
- **nivmizzetcombo** x8 -- T4 m1 `{no land + cast:Niv-Mizzet, the Firemind + sac:Generator Servant + cast:Curiosity@33}`  
  haste on Niv-Mizzet, the Firemind -- by paying pay1[Saprazzan Skerry#48:U Island#21:U Ferrous Lake#11:UR Izzet Signet#26:UR] ; pay2[(float only)]; the engine ended board=[Island#21(T), Ferrous Lake#11(T), Izzet Signet#26(T), Niv-Mizzet, the Firemind#33(new), Curiosity#8(new)@33] hand=[Tandem Lookout, Preordain] float=- life=20  
  board: board=[Island#21, Saprazzan Skerry#48, Ferrous Lake#11, Izzet Signet#26, Generator Servant#15] hand=[Curiosity, Niv-Mizzet, the Firemind, Tandem Lookout, Preordain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30005290 --game-index 5290`
- **nivmizzetcombo** x8 -- T3 m1 `{no land + cast:Izzet Signet + sac:Generator Servant + cast:Ponder}`  
  haste on Generator Servant -- by paying pay1[Island#18:U keep-floating {C}(haste 1)] ; pay2[Cascade Bluffs#6:C Izzet Signet#26:UR] ; pay3[(float only)]; the engine ended board=[Island#18(T), Cascade Bluffs#6(T), Izzet Signet#26(T)(new), Sandstone Needle#47(T)(new), Generator Servant#12(new)] hand=[Tandem Lookout, Niv-Mizzet, the Firemind, Ponder] float=- life=20  
  board: board=[Island#18, Cascade Bluffs#6, Generator Servant#15] hand=[Tandem Lookout, Sandstone Needle, Niv-Mizzet, the Firemind, Ponder, Ponder, Izzet Signet] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30005707 --game-index 5707`
- **nivmizzetcombo** x8 -- T5 m1 `{land=Island + cast:Niv-Mizzet, the Firemind + cast:Tandem Lookout + sac:Generator Servant}`  
  haste on Tandem Lookout -- by paying pay1[Spirebluff Canal#54:R Cascade Bluffs#4:C Island#23:U Saprazzan Skerry#48:U Island#19:U Izzet Signet#27:UR keep-floating {C}(haste 1)] ; pay2[(float only)]; the engine ended board=[Spirebluff Canal#54(T), Cascade Bluffs#4(T), Island#23(T), Tandem Lookout#57, Saprazzan Skerry#48(T), Izzet Signet#27(T), Island#19(T)(new), Niv-Mizzet, the Firemind#30(new), Tandem Lookout#58(new)] hand=[Niv-Mizzet, the Firemind, Curiosity, Remand] float=- life=18  
  board: board=[Spirebluff Canal#54, Cascade Bluffs#4, Island#23, Tandem Lookout#57, Generator Servant#13, Saprazzan Skerry#48, Izzet Signet#27] hand=[Niv-Mizzet, the Firemind, Niv-Mizzet, the Firemind, Curiosity, Island, Tandem Lookout, Remand] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/NivMizzetCombo/NivMizzetCombo.cod' --profile 'decks/NivMizzetCombo/NivMizzetCombo.profile.json' --games 1 --seed 30006748 --game-index 6748`
- ... 2572 more signatures in flags.jsonl

## ORACLE-GAP (12986 flags, 4142 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **snow** x175 -- T3 m1 `{no land + cast:Ice-Fang Coatl}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Snow-Covered Island#50:U Boreal Druid#9:C Arcum's Astrolabe#5:G]: Snow-Covered Mountain#58 tapped by engine only; Boreal Druid#9 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Snow-Covered Island#50(T), Arcum's Astrolabe#5(T), Snow-Covered Mountain#58(T), Boreal Druid#9, Ice-Fang Coatl#22(new), Scrying Sheets#36(new)] hand=[Abominable Treefolk, Rimescale Dragon, Snow-Covered Island, Snow-Covered Island, Snow-Covered Forest] float=- life=20  
  board: board=[Snow-Covered Island#50, Arcum's Astrolabe#5, Snow-Covered Mountain#58, Boreal Druid#9] hand=[Abominable Treefolk, Ice-Fang Coatl, Rimescale Dragon, Snow-Covered Island, Snow-Covered Island, Snow-Covered Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Snow/Snow.cod' --profile 'decks/Snow/Snow.profile.json' --games 1 --seed 30007190 --game-index 7190`
- **melira_pod** x100 -- T4 m1 `{land=Darkbore Pathway + cast:Recruiter of the Guard + pod:Birthing Pod + pod:Birthing Pod}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Blooming Marsh#8:B Birds of Paradise#1:U Razorverge Thicket#50:G Razorverge Thicket#51:W] ; pay2[Darkbore Pathway#26:B keep-floating {G}] ; pay3[(float only)]: Birthing Pod#4 tapped by oracle only; life engine 14 oracle 12; same core; float engine - oracle -; engine ended board=[Blooming Marsh#8(T), Razorverge Thicket#50(T), Birthing Pod#3(T), Razorverge Thicket#51(T), Birthing Pod#4, Darkbore Pathway#26(T)(new), Recruiter of the Guard#55(new), Melira, Sylvok Outcast#44(new)] hand=[Orzhov Basilica, Chord of Calling, Melira, Sylvok Outcast] float=- life=14  
  board: board=[Blooming Marsh#8, Birds of Paradise#1, Razorverge Thicket#50, Birthing Pod#3, Razorverge Thicket#51, Birthing Pod#4] hand=[Darkbore Pathway, Orzhov Basilica, Chord of Calling, Recruiter of the Guard] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30005788 --game-index 5788`
- **melira_pod** x80 -- T4 m1 `{land=Darkbore Pathway + cast:Recruiter of the Guard + pod:Birthing Pod + pod:Birthing Pod}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Blooming Marsh#8:B Birds of Paradise#1:U Razorverge Thicket#50:G Razorverge Thicket#51:W] ; pay2[Slitherbore Pathway#26:G keep-floating {G}] ; pay3[(float only)]: Birthing Pod#4 tapped by oracle only; life engine 14 oracle 12; same core; float engine - oracle -; engine ended board=[Blooming Marsh#8(T), Razorverge Thicket#50(T), Birthing Pod#3(T), Razorverge Thicket#51(T), Birthing Pod#4, Slitherbore Pathway#26(T)(new), Recruiter of the Guard#55(new), Melira, Sylvok Outcast#44(new)] hand=[Orzhov Basilica, Chord of Calling, Melira, Sylvok Outcast] float=- life=14  
  board: board=[Blooming Marsh#8, Birds of Paradise#1, Razorverge Thicket#50, Birthing Pod#3, Razorverge Thicket#51, Birthing Pod#4] hand=[Darkbore Pathway, Orzhov Basilica, Chord of Calling, Recruiter of the Guard] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30005788 --game-index 5788`
- **snow** x75 -- T3 m1 `{land=Snow-Covered Island + cast:Ice-Fang Coatl}`  
  6 oracle outcomes, none covers the engine's -- nearest pay1[Boreal Druid#9:C Snow-Covered Island#51:U Arcum's Astrolabe#5:G]: Snow-Covered Mountain#58 tapped by engine only; Boreal Druid#9 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Snow-Covered Island#50, Arcum's Astrolabe#5(T), Snow-Covered Mountain#58(T), Boreal Druid#9, Snow-Covered Island#51(T)(new), Ice-Fang Coatl#22(new)] hand=[Abominable Treefolk, Rimescale Dragon, Snow-Covered Island, Snow-Covered Forest, Scrying Sheets] float=- life=20  
  board: board=[Snow-Covered Island#50, Arcum's Astrolabe#5, Snow-Covered Mountain#58, Boreal Druid#9] hand=[Abominable Treefolk, Ice-Fang Coatl, Rimescale Dragon, Snow-Covered Island, Snow-Covered Island, Snow-Covered Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Snow/Snow.cod' --profile 'decks/Snow/Snow.profile.json' --games 1 --seed 30007190 --game-index 7190`
- **snow** x58 -- T3 m1 `{land=Snow-Covered Island + cast:Ice-Fang Coatl}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Snow-Covered Island#50:U Snow-Covered Island#51:U Arcum's Astrolabe#5:G]: Snow-Covered Island#50 tapped by oracle only; Snow-Covered Mountain#58 tapped by engine only; same core; float engine - oracle -; engine ended board=[Snow-Covered Island#50, Frost Augur#17, Snow-Covered Mountain#58(T), Arcum's Astrolabe#5(T), Snow-Covered Island#51(T)(new), Ice-Fang Coatl#22(new)] hand=[Abominable Treefolk, Skred, Skred, Ice-Fang Coatl, Marit Lage's Slumber, Boreal Druid] float=- life=20  
  board: board=[Snow-Covered Island#50, Frost Augur#17, Snow-Covered Mountain#58, Arcum's Astrolabe#5] hand=[Abominable Treefolk, Ice-Fang Coatl, Skred, Skred, Ice-Fang Coatl, Snow-Covered Island, Marit Lage's Slumber] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Snow/Snow.cod' --profile 'decks/Snow/Snow.profile.json' --games 1 --seed 30007858 --game-index 7858`
- **snow** x55 -- T3 m1 `{land=Scrying Sheets + cast:Ice-Fang Coatl}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Snow-Covered Island#55:U Scrying Sheets#36:C Arcum's Astrolabe#8:G]: Snow-Covered Mountain#59 tapped by engine only; Scrying Sheets#36 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Snow-Covered Mountain#59(T), Arcum's Astrolabe#8(T), Snow-Covered Island#55(T), Boreal Druid#9, Scrying Sheets#36(new), Ice-Fang Coatl#23(new)] hand=[Abominable Treefolk, Skred, Snow-Covered Forest, Snow-Covered Forest] float=- life=20  
  board: board=[Snow-Covered Mountain#59, Arcum's Astrolabe#8, Snow-Covered Island#55, Boreal Druid#9] hand=[Scrying Sheets, Abominable Treefolk, Skred, Ice-Fang Coatl, Snow-Covered Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Snow/Snow.cod' --profile 'decks/Snow/Snow.profile.json' --games 1 --seed 30007314 --game-index 7314`
- **snow** x51 -- T3 m1 `{land=Snow-Covered Island + cast:Ice-Fang Coatl}`  
  6 oracle outcomes, none covers the engine's -- nearest pay1[Boreal Druid#11:C Snow-Covered Island#53:U Arcum's Astrolabe#5:G]: Snow-Covered Mountain#58 tapped by engine only; Snow-Covered Island#51 tapped by engine only; Boreal Druid#11 tapped by oracle only; Snow-Covered Island#53 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Snow-Covered Mountain#58(T), Arcum's Astrolabe#5(T), Snow-Covered Island#51(T), Boreal Druid#11, Snow-Covered Island#53(new), Ice-Fang Coatl#24(new)] hand=[Marit Lage's Slumber, Snow-Covered Forest, Marit Lage's Slumber, Arcum's Astrolabe] float=- life=20  
  board: board=[Snow-Covered Mountain#58, Arcum's Astrolabe#5, Snow-Covered Island#51, Boreal Druid#11] hand=[Snow-Covered Island, Marit Lage's Slumber, Ice-Fang Coatl, Snow-Covered Forest, Marit Lage's Slumber] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Snow/Snow.cod' --profile 'decks/Snow/Snow.profile.json' --games 1 --seed 30008124 --game-index 8124`
- **melira_pod** x48 -- T4 m1 `{land=Caves of Koilos + cast:Birthing Pod + pod:Birthing Pod + pod:Birthing Pod}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Ignoble Hierarch#32:B Blooming Marsh#10:B Caves of Koilos#20:C] ; pay2[Razorverge Thicket#50:G Razorverge Thicket#52:G] ; pay3[(float only)]: Birthing Pod#4 tapped by oracle only; life engine 14 oracle 12; same core; float engine - oracle -; engine ended board=[Razorverge Thicket#50(T), Razorverge Thicket#52(T), Birthing Pod#5(T), Blooming Marsh#10(T), Murderous Redcap#46, Caves of Koilos#20(T)(new), Birthing Pod#4(new), Bloodthrone Vampire#7(new)] hand=[Branchloft Pathway] float=- life=14  
  board: board=[Razorverge Thicket#50, Ignoble Hierarch#32, Razorverge Thicket#52, Birthing Pod#5, Blooming Marsh#10, Murderous Redcap#46] hand=[Caves of Koilos, Branchloft Pathway, Birthing Pod] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30006254 --game-index 6254`
- **mirrorwing_dragon** x45 -- T3 m1 `{no land + cast:Fists of Flame}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Ignoble Hierarch#39:B Ignoble Hierarch#42:R]: Forest#14 tapped by engine only; Ignoble Hierarch#39 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Forest#14(T), Ignoble Hierarch#39, Forest#15, Ignoble Hierarch#42(T)] hand=[Zada, Hedron Grinder, Frontline Heroism, Fists of Flame, Rootbound Crag] float=- life=20  
  board: board=[Forest#14, Ignoble Hierarch#39, Forest#15, Ignoble Hierarch#42] hand=[Fists of Flame, Zada, Hedron Grinder, Frontline Heroism, Fists of Flame] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Mirrorwing Dragon/Mirrorwing Dragon.cod' --profile 'decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json' --games 1 --seed 30014106 --game-index 14106`
- **melira_pod** x44 -- T5 m1 `{no land + cast:Birthing Pod + pod:Birthing Pod + pod:Birthing Pod}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Ignoble Hierarch#30:B Forest#29:G Caves of Koilos#20:C] ; pay2[Blooming Marsh#9:B Blooming Marsh#8:G] ; pay3[(float only)]: Birthing Pod#4 tapped by oracle only; life engine 13 oracle 11; same core; float engine - oracle -; engine ended board=[Forest#29(T), Birthing Pod#3(T), Blooming Marsh#9(T), Ravenous Chupacabra#49, Caves of Koilos#20(T), Felidar Guardian#28, Blooming Marsh#8(T), Carrion Feeder#16, Birthing Pod#4(new), Melira, Sylvok Outcast#42(new)] hand=[Ranger of Eos, Chord of Calling] float=- life=13  
  board: board=[Ignoble Hierarch#30, Forest#29, Birthing Pod#3, Blooming Marsh#9, Ravenous Chupacabra#49, Caves of Koilos#20, Felidar Guardian#28, Blooming Marsh#8, Carrion Feeder#16] hand=[Birthing Pod, Ranger of Eos, Chord of Calling] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30008071 --game-index 8071`
- **snow** x42 -- T4 m1 `{no land + cast:Ice-Fang Coatl}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Snow-Covered Island#54:U Scrying Sheets#37:C Arcum's Astrolabe#7:G]: Snow-Covered Mountain#60 tapped by engine only; Scrying Sheets#37 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Snow-Covered Mountain#60(T), Arcum's Astrolabe#7(T), Snow-Covered Island#54(T), Ice-Fang Coatl#23, Scrying Sheets#37, Jorn, God of Winter#26, Ice-Fang Coatl#24(new)] hand=[Coldsteel Heart, Abominable Treefolk, Snow-Covered Forest, Abominable Treefolk, Skred, Coldsteel Heart] float=- life=20  
  board: board=[Snow-Covered Mountain#60, Arcum's Astrolabe#7, Snow-Covered Island#54, Ice-Fang Coatl#23, Scrying Sheets#37, Jorn, God of Winter#26] hand=[Coldsteel Heart, Abominable Treefolk, Snow-Covered Forest, Abominable Treefolk, Skred, Ice-Fang Coatl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Snow/Snow.cod' --profile 'decks/Snow/Snow.profile.json' --games 1 --seed 30007247 --game-index 7247`
- **melira_pod** x40 -- T4 m1 `{land=Darkbore Pathway + cast:Recruiter of the Guard + pod:Birthing Pod + pod:Birthing Pod}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Razorverge Thicket#50:G Razorverge Thicket#51:W Darkbore Pathway#26:B] ; pay2[Birds of Paradise#1:W] ; pay3[Blooming Marsh#8:B]: Birthing Pod#4 tapped by oracle only; life engine 14 oracle 12; same core; float engine - oracle -; engine ended board=[Blooming Marsh#8(T), Razorverge Thicket#50(T), Birthing Pod#3(T), Razorverge Thicket#51(T), Birthing Pod#4, Darkbore Pathway#26(T)(new), Recruiter of the Guard#55(new)] hand=[Orzhov Basilica, Chord of Calling, Melira, Sylvok Outcast] float=- life=14  
  board: board=[Blooming Marsh#8, Birds of Paradise#1, Razorverge Thicket#50, Birthing Pod#3, Razorverge Thicket#51, Birthing Pod#4] hand=[Darkbore Pathway, Orzhov Basilica, Chord of Calling, Recruiter of the Guard] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30005788 --game-index 5788`
- ... 4130 more signatures in flags.jsonl

## UNMODELED (16539 flags, 2430 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **hinata2** x344 -- T5 m1 `{no land + cast:Ponder}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Forbidden Orchard#13, Island#22, Mountain#30, Mountain#29, Hinata, Dawn-Crowned#18] hand=[Reality Spasm, Soulfire Eruption, Ponder, Hinata, Dawn-Crowned, Mystic Monastery] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005019 --game-index 5019`
- **hinata2** x309 -- T4 m1 `{no land + cast:Reality Spasm(x4) + cast:Ponder}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Sol Ring#56, Izzet Boilerworks#25, Ornithopter of Paradise#38, Forbidden Orchard#13, Hinata, Dawn-Crowned#15] hand=[Reality Spasm, Crackle with Power, Ponder, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005008 --game-index 5008`
- **fivecolour** x286 -- T4 m2 `{no land + cast:Oko, Thief of Crowns}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Breeding Pool#9, Birds of Paradise#2, Blood Crypt#6, Bloom Tender#7, Deathrite Shaman#14, Two-Headed Hellkite#47(T), Jetmir's Garden#25, Zagoth Triome#60(T)(new)] hand=[Garth One-Eye, Cosmic Spider-Man, Unite the Coalition, Maelstrom Archangel, Nicol Bolas, Planeswalker, Oko, Thief of Crowns] float=- life=12  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30012084 --game-index 12084`
- **fivecolour** x254 -- T5 m2 `{no land + cast:Unite the Coalition(x5)}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Jetmir's Garden#25, Overgrown Tomb#40, Deathrite Shaman#15, Zagoth Triome#60, Bloom Tender#8, Steam Vents#45, Maelstrom Archangel#28(T), Maelstrom Archangel#29(T), Breeding Pool#9(new)] hand=[Unite the Coalition, Nicol Bolas, Planeswalker] float=- life=11  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30012025 --game-index 12025`
- **prevent_damage** x225 -- T6 m1 `{land=Ancient Tomb + cast:Beseech the Queen}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Grand Coliseum#22, Tarnished Citadel#57, Battlefield Forge#5, Vito, Thorn of the Dusk Rose#60, Tarnished Citadel#55, Rhox Faithmender#42, Ancient Tomb#2] hand=[Ancient Tomb, Beseech the Queen] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Prevent Damage/Prevent Damage.cod' --profile 'decks/Prevent Damage/Prevent Damage.profile.json' --games 1 --seed 30012051 --game-index 12051`
- **fivecolour** x204 -- T4 m1 `{no land + cast:Mana Cannons}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Breeding Pool#9, Birds of Paradise#5, Blood Crypt#6, Faeburrow Elder#17, Deathrite Shaman#14, Maelstrom Archangel#30] hand=[Two-Headed Hellkite, Unite the Coalition, Mana Cannons] float=- life=14  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30012008 --game-index 12008`
- **fivecolour** x192 -- T5 m2 `{no land + cast:Jared Carthalion}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Jetmir's Garden#25, Zagoth Triome#60, Steam Vents#45, Faeburrow Elder#17, Blood Crypt#6, Jared Carthalion#24, Kavu Token#1000(T), Island#22(new)] hand=[Unite the Coalition, Faeburrow Elder, Jared Carthalion] float=- life=13  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30012038 --game-index 12038`
- **prevent_damage** x189 -- T5 m1 `{land=Ancient Tomb + cast:Green Sun's Zenith(x3)}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Grand Coliseum#23, Tarnished Citadel#56, Reflecting Pool#41, Tamanoa#53, Battlefield Forge#6] hand=[Rolling Earthquake, Pyrohemia, Beseech the Queen, Green Sun's Zenith, Vito, Thorn of the Dusk Rose, Ancient Tomb] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Prevent Damage/Prevent Damage.cod' --profile 'decks/Prevent Damage/Prevent Damage.profile.json' --games 1 --seed 30012097 --game-index 12097`
- **snow** x132 -- T5 m1 `{no land + act:Scrying Sheets(x1)}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Snow-Covered Mountain#60, Arcum's Astrolabe#5, Scrying Sheets#38, Snow-Covered Island#53, Arcum's Astrolabe#8, Coldsteel Heart#16, Scrying Sheets#37, Arcum's Astrolabe#7, Boreal Druid#9, Coldsteel Heart#14, 20/20 Avatar Token#1000(new)] hand=[Snow-Covered Island, Abominable Treefolk, Boreal Druid] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Snow/Snow.cod' --profile 'decks/Snow/Snow.profile.json' --games 1 --seed 30007007 --game-index 7007`
- **fivecolour** x124 -- T4 m1 `{no land + garth:Garth One-Eye}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Stomping Ground#46, Birds of Paradise#5, Zagoth Triome#60, Bloom Tender#8, Blood Crypt#6, Garth One-Eye#20] hand=[Faeburrow Elder, Unite the Coalition, Jetmir's Garden] float=- life=14  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30012019 --game-index 12019`
- **prevent_damage** x112 -- T6 m1 `{land=Ancient Tomb + cast:Manabarbs}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Grand Coliseum#20, Battlefield Forge#6, Tarnished Citadel#54, Spellshock#48, Ancient Tomb#2, Vito, Thorn of the Dusk Rose#60, Grand Coliseum#23] hand=[Manabarbs, Ancient Tomb, Brushland] float=- life=11  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Prevent Damage/Prevent Damage.cod' --profile 'decks/Prevent Damage/Prevent Damage.profile.json' --games 1 --seed 30012296 --game-index 12296`
- **prevent_damage** x110 -- T6 m1 `{land=Reflecting Pool + cast:Beseech the Queen}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[City of Brass#17, Tarnished Citadel#55, Dina, Soul Steeper#18, Ancient Tomb#4, Tamanoa#53, Grand Coliseum#23, Reflecting Pool#41] hand=[Reflecting Pool, Battlefield Forge, Beseech the Queen] float=- life=13  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Prevent Damage/Prevent Damage.cod' --profile 'decks/Prevent Damage/Prevent Damage.profile.json' --games 1 --seed 30012201 --game-index 12201`
- ... 2418 more signatures in flags.jsonl

## CAPPED (17648 flags, 4096 signatures)

_the payment search hit MTG_MANA_ORACLE_NODES and then the _RETRY escalation (20000); the complete side is partial._

- **hinata2** x221 -- T5 m1 `{no land + cast:Soulfire Eruption}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forbidden Orchard#10, Forbidden Orchard#11, Ornithopter of Paradise#37, Cascade Bluffs#1, Forbidden Orchard#12, Hinata, Dawn-Crowned#15] hand=[Remand, Reality Spasm, Remand, Crackle with Power, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005029 --game-index 5029`
- **hinata2** x136 -- T8 m1 `{no land + cast:Reality Spasm(x5) + cast:Soulfire Eruption}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forbidden Orchard#10, Island#21, Mountain#31, Sol Ring#56, Forbidden Orchard#11, Hinata, Dawn-Crowned#16] hand=[Reality Spasm, Distorting Wake, Soulfire Eruption, Reality Spasm, Irencrag Feat] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005014 --game-index 5014`
- **unpredictable_cyclone** x130 -- T6 m1 `{land=Cascade Bluffs + cast:Ponder + dig:Curator of Mysteries}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Watery Grave#59, Steam Vents#45, Mountain#27, Steam Vents#46, Blood Crypt#1, Unpredictable Cyclone#55, Emrakul, the Aeons Torn#18, Island#21, Island#22, Island#19] hand=[Remand, Xander's Lounge, Ponder, Cascade Bluffs, Island, Curator of Mysteries] float=- life=5  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30007810 --game-index 7810`
- **hinata2** x128 -- T6 m1 `{no land + cast:Reality Spasm(x5) + cast:Ponder}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forbidden Orchard#12, Cascade Bluffs#1, Mystic Monastery#32, Mountain#31, Hinata, Dawn-Crowned#16, Forbidden Orchard#11] hand=[Ponder, Expressive Iteration, Icy Blast, Reality Spasm, Ornithopter of Paradise] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005247 --game-index 5247`
- **unpredictable_cyclone** x126 -- T5 m1 `{land=Scalding Tarn + cast:Brainstorm + cast:Brainstorm + dig:Curator of Mysteries}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Steam Vents#45, Cascade Bluffs#10, Watery Grave#59, Blood Crypt#1, Unpredictable Cyclone#55, Emrakul, the Aeons Torn#16, Street Wraith#49, Xander's Lounge#60, Island#22, Island#21] hand=[Scalding Tarn, Brainstorm, Brainstorm, Curator of Mysteries] float=- life=9  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30009299 --game-index 9299`
- **hinata2** x115 -- T6 m1 `{no land + cast:Soulfire Eruption + cast:Reality Spasm(x5)}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Island#23, Forbidden Orchard#12, Mystic Monastery#33, Forbidden Orchard#10, Hinata, Dawn-Crowned#17, Forbidden Orchard#13] hand=[Soulfire Eruption, Reality Spasm, Hinata, Dawn-Crowned, Hinata, Dawn-Crowned, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005129 --game-index 5129`
- **hinata2** x107 -- T6 m1 `{no land + cast:Reality Spasm(x5) + cast:Ponder + cast:Soulfire Eruption}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Island#21, Forbidden Orchard#11, Ornithopter of Paradise#36, Cascade Bluffs#2, Ornithopter of Paradise#38, Hinata, Dawn-Crowned#18] hand=[Soulfire Eruption, Remand, Crackle with Power, Reality Spasm, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005543 --game-index 5543`
- **fivecolour** x103 -- T5 m1 `{no land + garth:Garth One-Eye}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Stomping Ground#46, Birds of Paradise#5, Zagoth Triome#60, Bloom Tender#8, Blood Crypt#6, Garth One-Eye#20, Jetmir's Garden#25, Shivan Dragon#1000, Faeburrow Elder#16] hand=[Unite the Coalition, Verdant Catacombs] float=- life=14  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/FiveColour/FiveColour.cod' --profile 'decks/FiveColour/FiveColour.profile.json' --games 1 --seed 30012019 --game-index 12019`
- **hinata2** x89 -- T6 m1 `{no land + cast:Ponder}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Cascade Bluffs#1, Reflecting Pool#52, Mystic Monastery#33, Ornithopter of Paradise#36, Hinata, Dawn-Crowned#16] hand=[Crackle with Power, Hinata, Dawn-Crowned, Reality Spasm, Ponder, Reality Spasm] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005096 --game-index 5096`
- **hinata2** x72 -- T6 m1 `{no land + cast:Reality Spasm(x4) + cast:Reality Spasm(x4) + cast:Ponder}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Cascade Bluffs#1, Reflecting Pool#52, Mystic Monastery#33, Ornithopter of Paradise#36, Hinata, Dawn-Crowned#16] hand=[Crackle with Power, Hinata, Dawn-Crowned, Reality Spasm, Ponder, Reality Spasm] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005096 --game-index 5096`
- **landfallelementals** x66 -- T5 m1 `{land=Secluded Courtyard + cast:Mossborn Hydra + cast:Brushfire Elemental + cast:Flamekin Harbinger}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Flamekin Harbinger#13, Forest#17, Smokebraider#48, Mountain#32, Risen Reef#40, Smokebraider#51, Gruul Turf#24, Unclaimed Territory#56, Risen Reef#41, Mountain#33, Mossborn Hydra#28, Unclaimed Territory#59, Mountain#35, Mountain#34] hand=[Mossborn Hydra, Brushfire Elemental, Flamekin Harbinger, Secluded Courtyard] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30016670 --game-index 16670`
- **landfall** x64 -- T6 m1 `{land=Forest [ordered] + cast:Khalni Heart Expedition + cast:Cultivate + act:Myriad Landscape(x1) + act:Myriad Landscape(x1) + act:Myriad Landscape(x1)}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#14, Forest#12, Mountain#33, Forest#22, Mountain#34, Primeval Titan#44, Primeval Titan#45, Myriad Landscape#37, Myriad Landscape#36, Forest#15, Forest#24, Gruul Turf#27, Myriad Landscape#38, Myriad Landscape#35] hand=[Primeval Titan, Khalni Heart Expedition, Cultivate, Forest, Omnath, Locus of Rage] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Landfall/Landfall.cod' --profile 'decks/Landfall/Landfall.profile.json' --games 1 --seed 30023272 --game-index 23272`
- ... 4084 more signatures in flags.jsonl

## CAPPED-DECISION (36643 flags, 9253 signatures)

_the decision spent its priced work budget (MTG_MANA_ORACLE_DECISION_S, default 300 priced s) before this plan's audit finished, so it has no verdict._

- **melira_pod** x1649 -- T5 m1 `{land=Darkbore Pathway + cast:Birthing Pod + pod:Birthing Pod + pod:Birthing Pod + sacoutlet:Carrion Feeder}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Blooming Marsh#8, Ignoble Hierarch#30, Llanowar Wastes#38, Melira, Sylvok Outcast#42, Caves of Koilos#20, Birds of Paradise#1, Forest#29, Carrion Feeder#16, Birthing Pod#3, 0/0 Elemental Token#1000, Kitchen Finks#37] hand=[Darkbore Pathway, Birthing Pod] float=- life=50  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30007253 --game-index 7253`
- **melira_pod** x913 -- T5 m1 `{land=Darkbore Pathway + cast:Birthing Pod + pod:Birthing Pod + pod:Birthing Pod}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Blooming Marsh#8, Ignoble Hierarch#30, Llanowar Wastes#38, Melira, Sylvok Outcast#42, Caves of Koilos#20, Birds of Paradise#1, Forest#29, Carrion Feeder#16, Birthing Pod#3, 0/0 Elemental Token#1000, Kitchen Finks#37] hand=[Darkbore Pathway, Birthing Pod] float=- life=50  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30007253 --game-index 7253`
- **melira_pod** x736 -- T5 m1 `{land=Darkbore Pathway + cast:Birthing Pod + pod:Birthing Pod + pod:Birthing Pod + sacoutlet:Carrion Feeder + sacoutlet:Carrion Feeder}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Blooming Marsh#8, Ignoble Hierarch#30, Llanowar Wastes#38, Melira, Sylvok Outcast#42, Caves of Koilos#20, Birds of Paradise#1, Forest#29, Carrion Feeder#16, Birthing Pod#3, 0/0 Elemental Token#1000, Kitchen Finks#37] hand=[Darkbore Pathway, Birthing Pod] float=- life=50  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30007253 --game-index 7253`
- **landfallelementals** x400 -- T5 m1 `{land=Mountain + cast:Akoum Hellhound + cast:Akoum Hellhound + cast:Metallic Mimic + cast:Brushfire Elemental + act:Terramorphic Expanse(x1)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#18, Unclaimed Territory#59, Smokebraider#49, Risen Reef#40, Mountain#34, Fiery Islet#12, Risen Reef#41, Terramorphic Expanse#52, Fulminator Mage#21, Terramorphic Expanse#53] hand=[Mountain, Akoum Hellhound, Forest, Akoum Hellhound, Metallic Mimic, Brushfire Elemental] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30013031 --game-index 13031`
- **melira_pod** x316 -- T5 m1 `{no land + cast:Birthing Pod + pod:Birthing Pod + pod:Birthing Pod + sacoutlet:Carrion Feeder}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Blooming Marsh#8, Ignoble Hierarch#30, Llanowar Wastes#38, Melira, Sylvok Outcast#42, Caves of Koilos#20, Birds of Paradise#1, Forest#29, Carrion Feeder#16, Birthing Pod#3, 0/0 Elemental Token#1000, Kitchen Finks#37] hand=[Darkbore Pathway, Birthing Pod] float=- life=50  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Melira Pod/Melira Pod.cod' --profile 'decks/Melira Pod/Melira Pod.profile.json' --games 1 --seed 30007253 --game-index 7253`
- **unpredictable_cyclone** x294 -- T5 m1 `{land=Scalding Tarn + dig:Curator of Mysteries}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Steam Vents#45, Cascade Bluffs#10, Watery Grave#59, Blood Crypt#1, Unpredictable Cyclone#55, Emrakul, the Aeons Torn#16, Street Wraith#49, Xander's Lounge#60, Island#22, Island#21] hand=[Scalding Tarn, Brainstorm, Brainstorm, Curator of Mysteries] float=- life=9  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Unpredictable Cyclone/Unpredictable Cyclone.cod' --profile 'decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json' --games 1 --seed 30009299 --game-index 9299`
- **hinata2** x276 -- T5 m1 `{no land [ordered] + cast:Reality Spasm(x4) + cast:Ponder + cast:Gamble}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Island#21, Island#23, Mystic Monastery#34, Ornithopter of Paradise#37, Hinata, Dawn-Crowned#17] hand=[Gamble, Soulfire Eruption, Reality Spasm, Ponder, Reality Spasm, Cascade Bluffs] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005115 --game-index 5115`
- **hinata2** x271 -- T5 m1 `{no land [ordered] + cast:Ponder + cast:Gamble}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Island#21, Island#23, Mystic Monastery#34, Ornithopter of Paradise#37, Hinata, Dawn-Crowned#17] hand=[Gamble, Soulfire Eruption, Reality Spasm, Ponder, Reality Spasm, Cascade Bluffs] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005115 --game-index 5115`
- **landfallelementals** x270 -- T5 m1 `{land=Mountain + cast:Metallic Mimic + cast:Brushfire Elemental + act:Terramorphic Expanse(x1)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#18, Unclaimed Territory#59, Smokebraider#49, Risen Reef#40, Mountain#34, Fiery Islet#12, Risen Reef#41, Terramorphic Expanse#52, Fulminator Mage#21, Terramorphic Expanse#53] hand=[Mountain, Akoum Hellhound, Forest, Akoum Hellhound, Metallic Mimic, Brushfire Elemental] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30013031 --game-index 13031`
- **hinata2** x266 -- T5 m1 `{no land [ordered] + cast:Gamble + cast:Ponder}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Island#21, Island#23, Mystic Monastery#34, Ornithopter of Paradise#37, Hinata, Dawn-Crowned#17] hand=[Gamble, Soulfire Eruption, Reality Spasm, Ponder, Reality Spasm, Cascade Bluffs] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005115 --game-index 5115`
- **hinata2** x266 -- T5 m1 `{no land [ordered] + cast:Reality Spasm(x4) + cast:Gamble + cast:Ponder}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Island#21, Island#23, Mystic Monastery#34, Ornithopter of Paradise#37, Hinata, Dawn-Crowned#17] hand=[Gamble, Soulfire Eruption, Reality Spasm, Ponder, Reality Spasm, Cascade Bluffs] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata2/Hinata2.cod' --profile 'decks/Hinata2/Hinata2.profile.json' --games 1 --seed 30005115 --game-index 5115`
- **landfallelementals** x252 -- T5 m1 `{land=Forest + cast:Akoum Hellhound + cast:Akoum Hellhound + cast:Metallic Mimic + cast:Brushfire Elemental + act:Terramorphic Expanse(x1)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#18, Unclaimed Territory#59, Smokebraider#49, Risen Reef#40, Mountain#34, Fiery Islet#12, Risen Reef#41, Terramorphic Expanse#52, Fulminator Mage#21, Terramorphic Expanse#53] hand=[Mountain, Akoum Hellhound, Forest, Akoum Hellhound, Metallic Mimic, Brushfire Elemental] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/LandfallElementals/LandfallElementals.cod' --profile 'decks/LandfallElementals/LandfallElementals.profile.json' --games 1 --seed 30013031 --game-index 13031`
- ... 9241 more signatures in flags.jsonl

