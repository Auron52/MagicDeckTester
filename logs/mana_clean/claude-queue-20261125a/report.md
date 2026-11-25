# Mana-clean gate `claude-queue-20261125a`

Mana-clean gate run: 10 decks, games (seed 30000000 + game index) hydras 15000-43999, shadowborn_apostle 120000-146999, sea_creatures 10000-44999, mill 25000-47999, prevent_damage 122000-129999, stuffy_doll 10000-23999, dragonstorm 87000-158999, treasure_hunt 100000-199999, dragons 96000-195999, worldfire 0-22999; src `773d4e440d5c`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-LIFE | ORACLE-GAP | CAPPED | CAPPED-DECISION | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hydras | 29000 | issues | 174 | 18 | 0 | 91 | 0 | 3 | 0 | 62 | 20375 | 1 | 1307 | 460 | 58 |
| shadowborn_apostle | 27000 | issues | 296 | 0 | 58 | 238 | 0 | 0 | 0 | 0 | 90276 | 176 | 4135 | 58271 | 113 |
| sea_creatures | 35000 | issues | 243 | 61 | 94 | 83 | 1 | 5 | 0 | 0 | 8196 | 14 | 3 | 15178 | 5 |
| mill | 23000 | issues | 341 | 341 | 0 | 0 | 0 | 0 | 0 | 0 | 723 | 695 | 1658401 | 140753 | 3 |
| prevent_damage | 8000 | issues | 26 | 0 | 0 | 0 | 26807 | 26 | 0 | 0 | 137 | 185 | 18 | 16121 | 0 |
| stuffy_doll | 14000 | issues | 235 | 0 | 24 | 206 | 0 | 5 | 0 | 0 | 19262 | 458 | 700 | 10584 | 0 |
| dragonstorm | 72000 | issues | 1137 | 33 | 728 | 68 | 0 | 5 | 27 | 276 | 153960 | 577 | 43693 | 224536 | 1338 |
| treasure_hunt | 100000 | issues | 5047 | 365 | 4440 | 242 | 6662 | 0 | 0 | 0 | 59384 | 2069 | 618367 | 323727 | 43 |
| dragons | 100000 | issues | 4874 | 0 | 1127 | 0 | 0 | 3747 | 0 | 0 | 99380 | 468 | 3858 | 158007 | 0 |
| worldfire | 23000 | issues | 7392 | 0 | 6938 | 451 | 0 | 3 | 0 | 0 | 620104 | 17374 | 26699 | 41616 | 17 |

**MISS = 13668, ORACLE-GAP = 3794.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=359679 plans=3114850 skipped=0 applies=273976975 frontiers=228481635 pruned=74614547 options=220425645 engine_drops=688096 gate_rejects=5426312(checked 5415348, skipped 10964) MISS=7538 ORACLE-GAP=8 UNMODELED=0 CAPPED=0 ms=11597241 ===
=== MANA ORACLE classes: ORACLE-GAP=8 APPLY-DROP=6958 APPLY-DROP-ORDER=580 PHANTOM=635609 DOMINATED-LAND=49981 CONTINUATION-DIFFERS=17713 DROP-COVERED=27225 HIDDEN-INFO=17 ===
=== MANA ORACLE enum-gap rejections: noop=0 hidden=0 host_blind=0 cont_land=0 ===
=== MANA ORACLE hit-only cuts: 80621099 unpayable-payment subtrees not built ===
=== MANA ORACLE order fold: 923745 order variants are an earlier order's apply (trailing activations) ===
=== MANA ORACLE hit-only cuts: 695121 frontiers below a dropped action not built ===
=== MANA ORACLE goal fold: 643741 plans, 49 frontiers not expanded, 0 plans realised again in full, 0 checked ===
=== MANA ORACLE board memory: 0 searches capped by MTG_MANA_ORACLE_BOARD_MB, 77566215 pruned frontier boards and 2611608 hit-only outcome boards given back early ===
=== MANA ORACLE shared boards: 14287236 outcome boards held for 42188611 outcomes (27901375 shared); 0 share checks ===
=== MANA ORACLE capped-plan escalation: 3384 retried, 3384 finished ===
=== MANA ORACLE unmodeled: 34226 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE rollback census: lost=7538 depth0=0 depth1=4884 depth2=1783 depth3+=282 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=9 unmodeled=0 reorder=580 off-menu=0 need_in_plan=5856 need_later=1102 pay_rollback_rescued=4/6958 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP | CAPPED | CAPPED-DECISION |
|---|---|---|---|---|---|---|---|---|
| hydras | 284868 | 1833045 | 18 | 0 | 91 | 3 | 0 | 62 |
| shadowborn_apostle | 232076 | 2081745 | 0 | 58 | 238 | 0 | 0 | 0 |
| sea_creatures | 366772 | 1096619 | 61 | 94 | 83 | 5 | 0 | 0 |
| mill | 207066 | 20808704 | 341 | 0 | 0 | 0 | 0 | 0 |
| prevent_damage | 79508 | 1274466 | 0 | 0 | 0 | 26 | 0 | 0 |
| stuffy_doll | 117040 | 827458 | 0 | 24 | 206 | 5 | 0 | 0 |
| dragonstorm | 554643 | 2722533 | 33 | 728 | 68 | 5 | 27 | 276 |
| treasure_hunt | 709709 | 14909364 | 365 | 4440 | 242 | 0 | 0 | 0 |
| dragons | 1008545 | 7174498 | 0 | 1127 | 0 | 3747 | 0 | 0 |
| worldfire | 266025 | 2454592 | 0 | 5000 | 451 | 3 | 0 | 0 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth0 | depth1 | depth2 | depth3+ | capped | unfaithful | no-failed-payment | reorder | off-menu | rescued | need-later |
|---|---|---|---|---|---|---|---|---|---|---|---|
| hydras | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 91 | 18 | 0 | 0 |
| shadowborn_apostle | 0 | 15 | 9 | 34 | 0 | 0 | 0 | 238 | 0 | 0 | 31 |
| sea_creatures | 8 | 40 | 41 | 5 | 0 | 0 | 0 | 83 | 61 | 0 | 0 |
| mill | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 341 | 0 | 0 |
| stuffy_doll | 0 | 11 | 9 | 4 | 0 | 0 | 0 | 206 | 0 | 4 | 11 |
| dragonstorm | 0 | 220 | 135 | 265 | 85 | 23 | 0 | 68 | 33 | 0 | 299 |
| treasure_hunt | 0 | 4143 | 118 | 179 | 0 | 0 | 0 | 242 | 365 | 20 | 1210 |
| dragons | 0 | 667 | 442 | 18 | 0 | 0 | 0 | 0 | 0 | 38 | 686 |
| worldfire | 0 | 3543 | 1261 | 190 | 0 | 0 | 6 | 451 | 0 | 0 | 767 |

## ENUM-GAP (818 flags, 329 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **mill** x32 -- T2 m1 `{land=Darkslick Shores + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Darkslick Shores#10:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Watery Grave|L||0,0|card 1>0)  
  board: board=[Watery Grave#57, Ruin Crab#39] hand=[Visions of Beyond, Darkslick Shores, Surgical Extraction, Counterspell] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30025292 --game-index 25292`
- **mill** x30 -- T3 m1 `{land=Island + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Island#32:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Watery Grave|L||0,0|card 1>0)  
  board: board=[Watery Grave#57, Ruin Crab#39, Ruin Crab#40] hand=[Tasha's Hideous Laughter, Glimpse the Unthinkable, Counterspell, Visions of Beyond, Surgical Extraction, Island] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30025496 --game-index 25496`
- **mill** x23 -- T2 m1 `{land=Drowned Catacomb + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Drowned Catacomb#17:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Watery Grave|L||0,0|card 1>0)  
  board: board=[Watery Grave#59, Hedron Crab#27] hand=[Surgical Extraction, Visions of Beyond, Drowned Catacomb, Counterspell, Watery Grave, Watery Grave, Archive Trap] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30030221 --game-index 30221`
- **mill** x22 -- T4 m1 `{land=Watery Grave + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Watery Grave#57:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Darkslick Shores|L||0,0|card 1>0)  
  board: board=[Darkslick Shores#9, Hedron Crab#27, Hedron Crab#28] hand=[Surgical Extraction, Tasha's Hideous Laughter, Fatal Push, Drown in the Loch, Visions of Beyond, Counterspell, Watery Grave] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30026944 --game-index 26944`
- **sea_creatures** x14 -- T2 m2 `{land=Forest + cast:Overgrowth@7 + cast:Overgrowth@7}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Sol Ring#36:C Forest#9:G] ; pay2[Flooded Grove#7:C]  
  board: board=[Flooded Grove#7, Sol Ring#36] hand=[Hedge Maze, Overgrowth, Forest, Breeding Pool, Forest, Misty Rainforest, Overgrowth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Sea Creatures/Sea Creatures.cod' --profile 'decks/Sea Creatures/Sea Creatures.profile.json' --games 1 --seed 30014121 --game-index 14121`
- **mill** x13 -- T2 m1 `{land=Drowned Catacomb + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Drowned Catacomb#17:U Watery Grave#57:B] ; pay2[(float only)]  
  board: board=[Watery Grave#57, Ruin Crab#39] hand=[Ruin Crab, Fractured Sanity, Visions of Beyond, Drown in the Loch, Polluted Delta, Drowned Catacomb, Surgical Extraction] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30027157 --game-index 27157`
- **mill** x13 -- T2 m1 `{land=Island + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Island#31:U Watery Grave#57:B] ; pay2[(float only)]  
  board: board=[Watery Grave#57, Hedron Crab#27] hand=[Tasha's Hideous Laughter, Island, Visions of Beyond, Surgical Extraction, Counterspell, Tasha's Hideous Laughter, Hedron Crab] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30027403 --game-index 27403`
- **mill** x13 -- T3 m1 `{land=Watery Grave + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Watery Grave#60:U Watery Grave#59:B] ; pay2[(float only)]  
  board: board=[Scalding Tarn#43, Watery Grave#59, Hedron Crab#28] hand=[Drowned Catacomb, Surgical Extraction, Watery Grave, Visions of Beyond, Counterspell] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30028611 --game-index 28611`
- **mill** x13 -- T2 m1 `{land=Darkslick Shores + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Darkslick Shores#9:U Watery Grave#58:B] ; pay2[(float only)]  
  board: board=[Watery Grave#58, Ruin Crab#39] hand=[Watery Grave, Darkslick Shores, Visions of Beyond, Fractured Sanity, Surgical Extraction, Drown in the Loch, Visions of Beyond] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30031811 --game-index 31811`
- **treasure_hunt** x12 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Thundering Falls#53:U Sandstone Needle#38:R] ; pay2[Ferrous Lake#8:UR]  
  board: board=[Thundering Falls#53, Sandstone Needle#38] hand=[Forgotten Cave, Lonely Sandbar, Treasure Hunt, Forgotten Cave, Treasure Hunt, Ferrous Lake] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30104998 --game-index 104998`
- **mill** x11 -- T3 m1 `{land=Island + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Island#31:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Darkslick Shores|L||0,0|card 1>0)  
  board: board=[Darkslick Shores#9, Hedron Crab#30, Polluted Delta#37] hand=[Visions of Beyond, Island, Island, Fractured Sanity, Counterspell, Surgical Extraction] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30026451 --game-index 26451`
- **treasure_hunt** x10 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Temple of Epiphany#48:U Sandstone Needle#37:R] ; pay2[Cascade Bluffs#1:UU]  
  board: board=[Temple of Epiphany#48, Sandstone Needle#37] hand=[Treasure Hunt, Treasure Hunt, Cascade Bluffs, Frostboil Snarl, Ferrous Lake] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30103698 --game-index 103698`
- ... 317 more signatures in flags.jsonl

## APPLY-DROP (11471 flags, 3898 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **dragons** x253 -- T3 m1 `{land=Gruul Turf}`  
  engine dropped [cast Lightning Bolt]; every action pays with pay1[Haven of the Spirit Dragon#16:C keep-floating {R}] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Haven of the Spirit Dragon#16 tapped by oracle only; Mind Stone#29 only in engine; hand cards differ; graveyard size differs; library size differs; opponent life engine 19 oracle 16; spells cast differs; float engine {R} oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Haven of the Spirit Dragon#16:C keep-floating {R}] ; pay2[(float only)]; a lost action was not declared in the plan  
  board: board=[Haven of the Spirit Dragon#16, Mountain#31, Mind Stone#29] hand=[Lightning Bolt, Atsushi, the Blazing Sky, Gruul Turf, Scourge of Valkas, Inferno of the Star Mounts, Gruul Turf, Haven of the Spirit Dragon] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30096713 --game-index 96713`
- **worldfire** x120 -- T3 m1 `{no land + cast:Ponder + suspend:Keldon Halberdier}`  
  engine dropped [cast Ponder]; every action pays with pay1[Mercadian Bazaar#20:R] ; pay2[Steam Vents#54:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mercadian Bazaar#20:R] ; pay2[Steam Vents#54:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mercadian Bazaar#20, Steam Vents#54] hand=[Worldfire, Mistrise Village, Irencrag Feat, Flooded Strand, Keldon Halberdier, Ponder, Personal Tutor] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30000447 --game-index 447`
- **treasure_hunt** x104 -- T4 m1 `{land=Cascade Bluffs + cast:Land's Edge + cast:Treasure Hunt}`  
  engine dropped [cast Treasure Hunt]; every action pays with pay1[Sandstone Needle#36:R Cascade Bluffs#2:C] ; pay2[Temple of Epiphany#48:U Cascade Bluffs#1:C] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Sandstone Needle#36:R Cascade Bluffs#2:C] ; pay2[Temple of Epiphany#48:U Cascade Bluffs#1:C]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Temple of Epiphany#48, Cascade Bluffs#1, Sandstone Needle#36] hand=[Land's Edge, Cascade Bluffs, Fiery Islet, Reliquary Tower, Reliquary Tower, Reliquary Tower, Treasure Hunt, Lonely Sandbar] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30101992 --game-index 101992`
- **treasure_hunt** x72 -- T4 m1 `{no land + cast:Treasure Hunt}`  
  engine dropped [cast Land's Edge]; every action pays with pay1[Saprazzan Skerry#40:U Ferrous Lake#5:UR] ; pay2[Reliquary Tower#31:C Cascade Bluffs#1:RR] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Saprazzan Skerry#40:U Ferrous Lake#5:UR] ; pay2[Reliquary Tower#31:C Cascade Bluffs#1:RR]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Reliquary Tower#31, Ferrous Lake#5, Saprazzan Skerry#40] hand=[Forgotten Cave, Steam Vents, Frostboil Snarl, Reliquary Tower, Frostboil Snarl, Fiery Islet, Treasure Hunt, Island, Cascade Bluffs, Sandstone Needle, Ferrous Lake, Cascade Bluffs, Forgotten Cave, Steam Vents, Island, Temple of Epiphany, Sandstone Needle, Forgotten Cave, Lonely Sandbar, Forgotten Cave, Steam Vents, Lonely Sandbar  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30106563 --game-index 106563`
- **treasure_hunt** x72 -- T4 m1 `{land=Cascade Bluffs + cast:Land's Edge + cast:Treasure Hunt}`  
  engine dropped [cast Treasure Hunt]; every action pays with pay1[Sandstone Needle#39:R Cascade Bluffs#1:C] ; pay2[Thundering Falls#53:U Cascade Bluffs#4:C] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Sandstone Needle#39:R Cascade Bluffs#1:C] ; pay2[Thundering Falls#53:U Cascade Bluffs#4:C]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Thundering Falls#53, Cascade Bluffs#4, Sandstone Needle#39] hand=[Land's Edge, Saprazzan Skerry, Reliquary Tower, Sandstone Needle, Fiery Islet, Cascade Bluffs, Treasure Hunt, Frostboil Snarl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30110763 --game-index 110763`
- **treasure_hunt** x64 -- T5 m1 `{land=Cascade Bluffs + cast:Land's Edge + cast:Treasure Hunt}`  
  engine dropped [cast Treasure Hunt]; every action pays with pay1[Sandstone Needle#38:R Cascade Bluffs#4:C Cascade Bluffs#1:UR] ; pay2[Reliquary Tower#33:C] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Sandstone Needle#38:R Cascade Bluffs#4:C Cascade Bluffs#1:UR] ; pay2[Reliquary Tower#33:C]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Cascade Bluffs#1, Reliquary Tower#33, Land's Edge#26, Sandstone Needle#38] hand=[Land's Edge, Cascade Bluffs, Treasure Hunt, Treasure Hunt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30105794 --game-index 105794`
- **worldfire** x53 -- T4 m1 `{land=Sandstone Needle + cast:Ponder + suspend:Keldon Halberdier}`  
  engine dropped [cast Ponder]; every action pays with pay1[Mercadian Bazaar#20:R] ; pay2[Steam Vents#54:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mercadian Bazaar#20:R] ; pay2[Steam Vents#54:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Steam Vents#54, Mercadian Bazaar#20, Mercadian Bazaar#21] hand=[Sandstone Needle, Keldon Halberdier, Ponder, Seething Song, Rift Bolt, Worldfire] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30000377 --game-index 377`
- **treasure_hunt** x48 -- T4 m1 `{land=Cascade Bluffs + cast:Land's Edge + cast:Treasure Hunt}`  
  engine dropped [cast Treasure Hunt]; every action pays with pay1[Sandstone Needle#36:R Cascade Bluffs#2:C] ; pay2[Cascade Bluffs#1:C Temple of Epiphany#48:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Sandstone Needle#36:R Cascade Bluffs#2:C] ; pay2[Cascade Bluffs#1:C Temple of Epiphany#48:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sandstone Needle#36, Cascade Bluffs#1, Temple of Epiphany#48] hand=[Ferrous Lake, Lonely Sandbar, Cascade Bluffs, Sandstone Needle, Fiery Islet, Forgotten Cave, Land's Edge, Treasure Hunt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30125627 --game-index 125627`
- **worldfire** x46 -- T3 m1 `{no land + suspend:Keldon Halberdier + cast:Personal Tutor}`  
  engine dropped [cast Personal Tutor]; every action pays with pay1[Mercadian Bazaar#20:R] ; pay2[Steam Vents#54:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mercadian Bazaar#20:R] ; pay2[Steam Vents#54:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mercadian Bazaar#20, Steam Vents#54] hand=[Worldfire, Mistrise Village, Irencrag Feat, Flooded Strand, Keldon Halberdier, Ponder, Personal Tutor] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30000447 --game-index 447`
- **treasure_hunt** x40 -- T8 m1 `{land=Cascade Bluffs + cast:Land's Edge + cast:Treasure Hunt}`  
  engine dropped [cast Treasure Hunt]; every action pays with pay1[Temple of Epiphany#49:U Reliquary Tower#33:C Cascade Bluffs#4:RR] ; pay2[Thundering Falls#55:U Reliquary Tower#32:C] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Temple of Epiphany#49:U Reliquary Tower#33:C Cascade Bluffs#4:RR] ; pay2[Thundering Falls#55:U Reliquary Tower#32:C]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Thundering Falls#55, Cascade Bluffs#3, Temple of Epiphany#49, Reliquary Tower#33, Reliquary Tower#32] hand=[Frostboil Snarl, Frostboil Snarl, Ferrous Lake, Sandstone Needle, Temple of Epiphany, Steam Vents, Ferrous Lake, Island, Lonely Sandbar, Forgotten Cave, Frostboil Snarl, Cascade Bluffs, Sandstone Needle, Island, Steam Vents, Forgotten Cave, Cascade Bluffs, Reliquary Tower, Steam Vents  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30133719 --game-index 133719`
- **worldfire** x39 -- T3 m1 `{land=Saprazzan Skerry + cast:Ponder + suspend:Keldon Halberdier}`  
  engine dropped [cast Ponder]; every action pays with pay1[Mercadian Bazaar#20:R] ; pay2[Steam Vents#54:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mercadian Bazaar#20:R] ; pay2[Steam Vents#54:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mercadian Bazaar#20, Steam Vents#54] hand=[Keldon Halberdier, Saprazzan Skerry, Brainstorm, Ponder, Worldfire] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30001572 --game-index 1572`
- **treasure_hunt** x35 -- T4 m1 `{no land + cast:Treasure Hunt}`  
  engine dropped [cast Land's Edge]; every action pays with pay1[Reliquary Tower#33:C Ferrous Lake#5:UR] ; pay2[Saprazzan Skerry#43:U Cascade Bluffs#1:RR] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Reliquary Tower#33:C Ferrous Lake#5:UR] ; pay2[Saprazzan Skerry#43:U Cascade Bluffs#1:RR]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Saprazzan Skerry#43, Reliquary Tower#33, Ferrous Lake#5] hand=[Lonely Sandbar, Island, Island, Land's Edge, Ferrous Lake, Remote Isle, Forgotten Cave, Saprazzan Skerry, Island, Saprazzan Skerry, Frostboil Snarl, Temple of Epiphany, Island, Fiery Islet, Steam Vents, Steam Vents, Steam Vents, Fiery Islet, Thundering Falls, Fiery Islet, Frostboil Snarl, Temple of Epiphany, Cascade Bluffs, Land  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30104173 --game-index 104173`
- ... 3886 more signatures in flags.jsonl

## APPLY-DROP-ORDER (1379 flags, 596 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **hydras** x23 -- T6 m1 `{no land + cast:Vernal Bloom + cast:Vernal Bloom + cast:Nature's Lore}`  
  engine dropped [cast Nature's Lore]; the order Vernal Bloom > Nature's Lore > Vernal Bloom pays every action with pay1[Forest#1:G Forest#2:G Forest#3:G Forest#22:G] ; pay2[Forest#8:G] ; pay3[Forest#19:G Forest#7:G]  
  board: board=[Forest#1, Forest#2, Forest#3, Forest#22, Voracious Hydra#60, Forest#8, Forest#19, Neverwinter Hydra#38, Primordial Hydra#45] hand=[Vernal Bloom, Vernal Bloom, Kalonian Hydra, Nature's Lore] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30015049 --game-index 15049`
- **shadowborn_apostle** x20 -- T4 m2 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Dark Ritual + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; the order act:Shadowborn Apostle(x1)>Abhorrent Overlord@21 > Shadowborn Apostle > Culling the Weak > Dark Ritual > Diabolic Intent pays every action with pay1[Swamp#42:B] ; pay2[Lake of the Dead#14:B] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Swamp#42(new)] hand=[Diabolic Intent, Culling the Weak, Dark Ritual, Jetmir, Nexus of Revels, Swamp, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30125823 --game-index 125823`
- **shadowborn_apostle** x16 -- T4 m2 `{no land + cast:Culling the Weak + cast:Diabolic Intent + cast:Dark Ritual + cast:Diabolic Intent + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent, cast Diabolic Intent]; the order act:Shadowborn Apostle(x1)>Abhorrent Overlord@21 > Shadowborn Apostle > Culling the Weak > Dark Ritual > Diabolic Intent > Diabolic Intent pays every action with pay1[Swamp#42:B] ; pay2[Lake of the Dead#14:B] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Swamp#42(new)] hand=[Sire of Insanity, Culling the Weak, Diabolic Intent, Dark Ritual, Diabolic Intent, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30129163 --game-index 129163`
- **shadowborn_apostle** x16 -- T4 m2 `{no land + cast:Culling the Weak + cast:Diabolic Intent + cast:Dark Ritual + cast:Diabolic Intent + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent, cast Diabolic Intent]; the order act:Shadowborn Apostle(x1)>Jetmir, Nexus of Revels@21 > Shadowborn Apostle > Culling the Weak > Dark Ritual > Diabolic Intent > Diabolic Intent pays every action with pay1[Swamp#42:B] ; pay2[Lake of the Dead#14:B] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Swamp#42(new)] hand=[Sire of Insanity, Culling the Weak, Diabolic Intent, Dark Ritual, Diabolic Intent, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30129163 --game-index 129163`
- **shadowborn_apostle** x16 -- T4 m2 `{no land + cast:Culling the Weak + cast:Diabolic Intent + cast:Dark Ritual + cast:Diabolic Intent + cast:Shadowborn Apostle}`  
  engine dropped [cast Diabolic Intent, cast Diabolic Intent]; the order Shadowborn Apostle > Culling the Weak > Dark Ritual > Diabolic Intent > Diabolic Intent pays every action with pay1[Swamp#42:B] ; pay2[Lake of the Dead#14:B] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Swamp#42(new)] hand=[Sire of Insanity, Culling the Weak, Diabolic Intent, Dark Ritual, Diabolic Intent, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30129163 --game-index 129163`
- **shadowborn_apostle** x16 -- T4 m2 `{no land + cast:Culling the Weak + cast:Diabolic Intent + cast:Dark Ritual + cast:Shadowborn Apostle}`  
  engine dropped [cast Diabolic Intent]; the order Shadowborn Apostle > Culling the Weak > Dark Ritual > Diabolic Intent pays every action with pay1[Swamp#42:B] ; pay2[Lake of the Dead#14:B] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Swamp#42(new)] hand=[Sire of Insanity, Culling the Weak, Diabolic Intent, Dark Ritual, Diabolic Intent, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30129163 --game-index 129163`
- **worldfire** x13 -- T3 m1 `{land=Island + cast:Brainstorm + cast:Ponder}`  
  engine dropped [cast Ponder]; the order Ponder > Brainstorm pays every action with pay1[Island#9:U] ; pay2[Steam Vents#54:U]  
  board: board=[Mountain#24, Steam Vents#54] hand=[Worldfire, Mistrise Village, Saprazzan Skerry, Personal Tutor, Island, Brainstorm, Ponder] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30005239 --game-index 5239`
- **hydras** x12 -- T4 m1 `{land=Forest + cast:Vernal Bloom + cast:Vernal Bloom + cast:Nature's Lore}`  
  engine dropped [cast Nature's Lore]; the order Vernal Bloom > Nature's Lore > Vernal Bloom pays every action with pay1[Forest#1:G Forest#2:G Forest#4:G Forest#19:G] ; pay2[Forest#11:G] ; pay3[Forest#3:G Forest#22:G]  
  board: board=[Forest#1, Forest#2, Forest#4, Forest#11, Forest#3, Primordial Hydra#42] hand=[Vernal Bloom, Vernal Bloom, Gauntlet of Power, Forest, Nature's Lore] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30020765 --game-index 20765`
- **shadowborn_apostle** x12 -- T4 m2 `{no land + cast:Diabolic Intent + cast:Diabolic Intent + cast:Dark Ritual + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; the order act:Shadowborn Apostle(x1)>Abhorrent Overlord@23 > Shadowborn Apostle > Dark Ritual > Diabolic Intent > Diabolic Intent pays every action with pay1[Swamp#42:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Swamp#42(new)] hand=[Diabolic Intent, Diabolic Intent, Swamp, Dark Ritual, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30146037 --game-index 146037`
- **shadowborn_apostle** x12 -- T4 m2 `{no land + cast:Diabolic Intent + cast:Diabolic Intent + cast:Dark Ritual + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; the order act:Shadowborn Apostle(x1)>Jetmir, Nexus of Revels@23 > Shadowborn Apostle > Dark Ritual > Diabolic Intent > Diabolic Intent pays every action with pay1[Swamp#42:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Swamp#42(new)] hand=[Diabolic Intent, Diabolic Intent, Swamp, Dark Ritual, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30146037 --game-index 146037`
- **shadowborn_apostle** x12 -- T4 m2 `{no land + cast:Diabolic Intent + cast:Diabolic Intent + cast:Dark Ritual + cast:Shadowborn Apostle}`  
  engine dropped [cast Diabolic Intent]; the order Shadowborn Apostle > Dark Ritual > Diabolic Intent > Diabolic Intent pays every action with pay1[Swamp#42:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Swamp#42(new)] hand=[Diabolic Intent, Diabolic Intent, Swamp, Dark Ritual, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30146037 --game-index 146037`
- **worldfire** x11 -- T2 m1 `{land=Steam Vents + cast:Brainstorm + cast:Ponder}`  
  engine dropped [cast Ponder]; the order Ponder > Brainstorm pays every action with pay1[Steam Vents#54:U] ; pay2[Steam Vents#55:U]  
  board: board=[Steam Vents#55] hand=[Rift Bolt, Brainstorm, Seething Song, Ponder, Steam Vents, Keldon Halberdier, Irencrag Feat] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30004279 --game-index 4279`
- ... 584 more signatures in flags.jsonl

## ORACLE-GAP (3794 flags, 688 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **dragons** x109 -- T8 m1 `{no land + gy-return:Haven of the Spirit Dragon}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Haven of the Spirit Dragon#16:C Mountain#42:R]: Haven of the Spirit Dragon#16 tapped by oracle only; Mountain#42 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Haven of the Spirit Dragon#16, Mountain#42] hand=[Inferno of the Star Mounts, Urza's Incubator, Dragonspeaker Shaman, Atsushi, the Blazing Sky, Inferno of the Star Mounts, Glorybringer, Dragonspeaker Shaman, Urza's Incubator] float=- life=20  
  board: board=[Haven of the Spirit Dragon#16, Mountain#42] hand=[Inferno of the Star Mounts, Urza's Incubator, Dragonspeaker Shaman, Atsushi, the Blazing Sky, Inferno of the Star Mounts, Glorybringer, Dragonspeaker Shaman, Urza's Incubator] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30097740 --game-index 97740`
- **dragons** x99 -- T3 m1 `{land=Haven of the Spirit Dragon + cast:Lightning Bolt + gy-return:Haven of the Spirit Dragon}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Gruul Turf#13:R] ; pay2[Haven of the Spirit Dragon#16:C]: Haven of the Spirit Dragon#16 tapped by oracle only; same core; float engine {G} oracle -; engine ended board=[Gruul Turf#13(T), Haven of the Spirit Dragon#16(new)] hand=[Lathliss, Dragon Queen, Fire Diamond, Dragonspeaker Shaman, Scourge of Valkas, Inferno of the Star Mounts, Gruul Turf] float={G} life=20  
  board: board=[Gruul Turf#13] hand=[Lathliss, Dragon Queen, Fire Diamond, Lightning Bolt, Dragonspeaker Shaman, Scourge of Valkas, Inferno of the Star Mounts, Haven of the Spirit Dragon, Gruul Turf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30097175 --game-index 97175`
- **dragons** x91 -- T5 m1 `{no land + cast:Lightning Bolt + gy-return:Haven of the Spirit Dragon}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#31:R] ; pay2[Haven of the Spirit Dragon#16:C Mind Stone#29:C]: Haven of the Spirit Dragon#16 tapped by oracle only; Mind Stone#29 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Haven of the Spirit Dragon#16, Mountain#31(T), Mind Stone#29] hand=[Atsushi, the Blazing Sky, Dragonspeaker Shaman, Lightning Bolt, Scourge of Valkas, Dragon Tempest, Glorybringer] float=- life=20  
  board: board=[Haven of the Spirit Dragon#16, Mountain#31, Mind Stone#29] hand=[Atsushi, the Blazing Sky, Lightning Bolt, Dragonspeaker Shaman, Lightning Bolt, Scourge of Valkas, Dragon Tempest, Glorybringer] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30104131 --game-index 104131`
- **dragons** x88 -- T4 m1 `{no land + cast:Lightning Bolt + gy-return:Haven of the Spirit Dragon}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Gruul Turf#13:R] ; pay2[Haven of the Spirit Dragon#16:C]: Haven of the Spirit Dragon#16 tapped by oracle only; same core; float engine {G} oracle -; engine ended board=[Gruul Turf#13(T), Haven of the Spirit Dragon#16, Dragonspeaker Shaman#8] hand=[Lathliss, Dragon Queen, Fire Diamond, Scourge of Valkas, Inferno of the Star Mounts, Gruul Turf, Mountain] float={G} life=20  
  board: board=[Gruul Turf#13, Haven of the Spirit Dragon#16, Dragonspeaker Shaman#8] hand=[Lathliss, Dragon Queen, Fire Diamond, Lightning Bolt, Scourge of Valkas, Inferno of the Star Mounts, Gruul Turf, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30097175 --game-index 97175`
- **dragons** x83 -- T5 m1 `{land=Mountain + cast:Scourge of Valkas + gy-return:Haven of the Spirit Dragon}`  
  7 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#31:R Mountain#32:R Haven of the Spirit Dragon#16:R(creature-only)] ; pay2[Gruul Turf#13:R]: Haven of the Spirit Dragon#16 tapped by oracle only; same core; float engine {G} oracle -; engine ended board=[Gruul Turf#13(T), Haven of the Spirit Dragon#16, Dragonspeaker Shaman#8, Mountain#31(T), Inferno of the Star Mounts#18, Mountain#32(T)(new), Scourge of Valkas#49(new)] hand=[Lathliss, Dragon Queen, Fire Diamond, Lightning Bolt, Gruul Turf] float={G} life=20  
  board: board=[Gruul Turf#13, Haven of the Spirit Dragon#16, Dragonspeaker Shaman#8, Mountain#31, Inferno of the Star Mounts#18] hand=[Lathliss, Dragon Queen, Fire Diamond, Lightning Bolt, Scourge of Valkas, Gruul Turf, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30097175 --game-index 97175`
- **dragons** x75 -- T4 m1 `{land=Mountain + cast:Atsushi, the Blazing Sky + gy-return:Haven of the Spirit Dragon}`  
  6 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#31:R Haven of the Spirit Dragon#16:R(creature-only)] ; pay2[Gruul Turf#13:R]: Haven of the Spirit Dragon#16 tapped by oracle only; same core; float engine {G} oracle -; engine ended board=[Gruul Turf#13(T), Haven of the Spirit Dragon#16, Dragonspeaker Shaman#8, Mountain#31(T)(new), Atsushi, the Blazing Sky#1(new)] hand=[Scourge of Valkas, Dragon Tempest, Inferno of the Star Mounts, Dragon Tempest, Dragon Tempest] float={G} life=20  
  board: board=[Gruul Turf#13, Haven of the Spirit Dragon#16, Dragonspeaker Shaman#8] hand=[Atsushi, the Blazing Sky, Scourge of Valkas, Mountain, Dragon Tempest, Inferno of the Star Mounts, Dragon Tempest, Dragon Tempest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30096139 --game-index 96139`
- **dragons** x73 -- T5 m1 `{no land + cast:Atsushi, the Blazing Sky + gy-return:Haven of the Spirit Dragon}`  
  6 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#31:R Haven of the Spirit Dragon#16:R(creature-only)] ; pay2[Gruul Turf#13:R]: Haven of the Spirit Dragon#16 tapped by oracle only; same core; float engine {G} oracle -; engine ended board=[Gruul Turf#13(T), Haven of the Spirit Dragon#16, Dragonspeaker Shaman#8, Mountain#31(T), Inferno of the Star Mounts#18, Atsushi, the Blazing Sky#1(new)] hand=[Scourge of Valkas, Dragon Tempest, Dragon Tempest, Dragon Tempest, Lightning Bolt] float={G} life=20  
  board: board=[Gruul Turf#13, Haven of the Spirit Dragon#16, Dragonspeaker Shaman#8, Mountain#31, Inferno of the Star Mounts#18] hand=[Atsushi, the Blazing Sky, Scourge of Valkas, Dragon Tempest, Dragon Tempest, Dragon Tempest, Lightning Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30096139 --game-index 96139`
- **dragons** x68 -- T5 m1 `{no land + cast:Dragon Tempest + gy-return:Haven of the Spirit Dragon}`  
  6 oracle outcomes, none covers the engine's -- nearest pay1[Haven of the Spirit Dragon#16:C Mountain#31:R] ; pay2[Gruul Turf#13:R]: Haven of the Spirit Dragon#16 tapped by oracle only; Mountain#31 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Gruul Turf#13(T), Haven of the Spirit Dragon#16, Urza's Incubator#54, Mountain#31, Lathliss, Dragon Queen#21, Dragon Tempest#4(new)] hand=[Lathliss, Dragon Queen, Mountain, Mind Stone, Glorybringer, Dragonspeaker Shaman] float=- life=20  
  board: board=[Gruul Turf#13, Haven of the Spirit Dragon#16, Urza's Incubator#54, Mountain#31, Lathliss, Dragon Queen#21] hand=[Dragon Tempest, Lathliss, Dragon Queen, Mountain, Mind Stone, Glorybringer, Dragonspeaker Shaman] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30096043 --game-index 96043`
- **dragons** x66 -- T6 m1 `{land=Mountain + cast:Lathliss, Dragon Queen + gy-return:Haven of the Spirit Dragon}`  
  12 oracle outcomes, none covers the engine's -- nearest pay1[Haven of the Spirit Dragon#16:C Mountain#42:R Mountain#43:R Mountain#44:R] ; pay2[Gruul Turf#15:R]: Haven of the Spirit Dragon#16 tapped by oracle only; Mountain#44 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Gruul Turf#15(T), Haven of the Spirit Dragon#16, Urza's Incubator#55, Mountain#42(T), Scourge of Valkas#51, Mountain#43(T), Atsushi, the Blazing Sky#3, Mountain#44(new), Lathliss, Dragon Queen#22(new)] hand=[] float=- life=20  
  board: board=[Gruul Turf#15, Haven of the Spirit Dragon#16, Urza's Incubator#55, Mountain#42, Scourge of Valkas#51, Mountain#43, Atsushi, the Blazing Sky#3] hand=[Lathliss, Dragon Queen, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30097494 --game-index 97494`
- **dragons** x65 -- T5 m1 `{land=Mountain + cast:Urza's Incubator + gy-return:Haven of the Spirit Dragon}`  
  8 oracle outcomes, none covers the engine's -- nearest pay1[Haven of the Spirit Dragon#16:C Mountain#31:R Mountain#32:R] ; pay2[Gruul Turf#13:R]: Haven of the Spirit Dragon#16 tapped by oracle only; Mountain#32 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Gruul Turf#13(T), Haven of the Spirit Dragon#16, Dragonspeaker Shaman#8, Mountain#31(T), Glorybringer#11, Mountain#32(new), Urza's Incubator#54(new)] hand=[Mountain, Scourge of Valkas, Urza's Incubator, Lightning Bolt] float=- life=20  
  board: board=[Gruul Turf#13, Haven of the Spirit Dragon#16, Dragonspeaker Shaman#8, Mountain#31, Glorybringer#11] hand=[Mountain, Urza's Incubator, Mountain, Scourge of Valkas, Urza's Incubator, Lightning Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30099135 --game-index 99135`
- **dragons** x62 -- T4 m1 `{land=Mountain + cast:Dragon Tempest + gy-return:Haven of the Spirit Dragon}`  
  6 oracle outcomes, none covers the engine's -- nearest pay1[Haven of the Spirit Dragon#16:C Mountain#31:R] ; pay2[Gruul Turf#13:R]: Haven of the Spirit Dragon#16 tapped by oracle only; Mountain#31 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Gruul Turf#13(T), Haven of the Spirit Dragon#16, Urza's Incubator#54, Mountain#31(new), Dragon Tempest#4(new)] hand=[Lathliss, Dragon Queen, Lathliss, Dragon Queen, Mountain, Mind Stone, Glorybringer] float=- life=20  
  board: board=[Gruul Turf#13, Haven of the Spirit Dragon#16, Urza's Incubator#54] hand=[Dragon Tempest, Lathliss, Dragon Queen, Lathliss, Dragon Queen, Mountain, Mountain, Mind Stone, Glorybringer] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30096043 --game-index 96043`
- **dragons** x53 -- T6 m1 `{no land + cast:Scourge of Valkas + gy-return:Haven of the Spirit Dragon}`  
  6 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#32:R Mountain#33:R Haven of the Spirit Dragon#16:R(creature-only)] ; pay2[Gruul Turf#13:R]: Haven of the Spirit Dragon#16 tapped by oracle only; same core; float engine {G} oracle -; engine ended board=[Gruul Turf#13(T), Haven of the Spirit Dragon#16, Urza's Incubator#54, Mountain#32(T), Lathliss, Dragon Queen#21, Mountain#33(T), 5/5 Dragon Token#1000, Scourge of Valkas#49(new), 5/5 Dragon Token#1001(new)] hand=[Utvara Hellkite, Mountain, Mountain, Mountain] float={G} life=20  
  board: board=[Gruul Turf#13, Haven of the Spirit Dragon#16, Urza's Incubator#54, Mountain#32, Lathliss, Dragon Queen#21, Mountain#33, 5/5 Dragon Token#1000] hand=[Scourge of Valkas, Utvara Hellkite, Mountain, Mountain, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragons/Dragons.cod' --profile 'decks/Dragons/Dragons.profile.json' --games 1 --seed 30098660 --game-index 98660`
- ... 676 more signatures in flags.jsonl

## CAPPED (27 flags, 8 signatures)

_the payment search hit MTG_MANA_ORACLE_NODES and then the _RETRY escalation (400000); the complete side is partial._

- **dragonstorm** x7 -- T8 m1 `{no land [ordered] + cast:Rite of Flame + cast:Desperate Ritual + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power + cast:Apex of Power + cast:Irencrag Feat}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Sandstone Needle#47, Unclaimed Territory#57, Mountain#28, Mountain#31, Lotus Bloom#19(new), Lotus Bloom#21(new), Lotus Bloom#18(new)] hand=[Apex of Power, Desperate Ritual, Rite of Flame, Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30121383 --game-index 121383`
- **dragonstorm** x7 -- T8 m1 `{no land [ordered] + cast:Rite of Flame + cast:Desperate Ritual + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power + cast:Apex of Power}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Sandstone Needle#47, Unclaimed Territory#57, Mountain#28, Mountain#31, Lotus Bloom#19(new), Lotus Bloom#21(new), Lotus Bloom#18(new)] hand=[Apex of Power, Desperate Ritual, Rite of Flame, Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30121383 --game-index 121383`
- **dragonstorm** x7 -- T8 m1 `{no land [ordered] + cast:Rite of Flame + cast:Desperate Ritual + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power + cast:Irencrag Feat}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Sandstone Needle#47, Unclaimed Territory#57, Mountain#28, Mountain#31, Lotus Bloom#19(new), Lotus Bloom#21(new), Lotus Bloom#18(new)] hand=[Apex of Power, Desperate Ritual, Rite of Flame, Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30121383 --game-index 121383`
- **dragonstorm** x2 -- T7 m1 `{land=Mountain [ordered] + cast:Rite of Flame + cast:Pyretic Ritual + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power + cast:Apex of Power + cast:Irencrag Feat}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Mercadian Bazaar#22, Mercadian Bazaar#23, Sandstone Needle#46, Mountain#26, Lotus Bloom#18(new), Lotus Bloom#19(new)] hand=[Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song, Pyretic Ritual, Rite of Flame, Apex of Power, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30137529 --game-index 137529`
- **dragonstorm** x1 -- T8 m1 `{no land [ordered] + cast:Rite of Flame + cast:Desperate Ritual + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Sandstone Needle#47, Unclaimed Territory#57, Mountain#28, Mountain#31, Lotus Bloom#19(new), Lotus Bloom#21(new), Lotus Bloom#18(new)] hand=[Apex of Power, Desperate Ritual, Rite of Flame, Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30121383 --game-index 121383`
- **dragonstorm** x1 -- T7 m1 `{land=Mountain [ordered] + cast:Rite of Flame + cast:Pyretic Ritual + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power + cast:Irencrag Feat}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Mercadian Bazaar#22, Mercadian Bazaar#23, Sandstone Needle#46, Mountain#26, Lotus Bloom#18(new), Lotus Bloom#19(new)] hand=[Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song, Pyretic Ritual, Rite of Flame, Apex of Power, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30137529 --game-index 137529`
- **dragonstorm** x1 -- T7 m1 `{land=Mountain [ordered] + cast:Rite of Flame + cast:Pyretic Ritual + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power + cast:Apex of Power}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Mercadian Bazaar#22, Mercadian Bazaar#23, Sandstone Needle#46, Mountain#26, Lotus Bloom#18(new), Lotus Bloom#19(new)] hand=[Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song, Pyretic Ritual, Rite of Flame, Apex of Power, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30137529 --game-index 137529`
- **dragonstorm** x1 -- T7 m1 `{land=Mountain [ordered] + cast:Rite of Flame + cast:Pyretic Ritual + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Mercadian Bazaar#22, Mercadian Bazaar#23, Sandstone Needle#46, Mountain#26, Lotus Bloom#18(new), Lotus Bloom#19(new)] hand=[Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song, Pyretic Ritual, Rite of Flame, Apex of Power, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30137529 --game-index 137529`

## CAPPED-DECISION (338 flags, 64 signatures)

_the decision spent its priced work budget (MTG_MANA_ORACLE_DECISION_S, default 1300 priced s) before this plan's audit finished, so it has no verdict._

- **dragonstorm** x12 -- T6 m1 `{no land + cast:Apex of Power}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Mountain#26, Sandstone Needle#48, Ruby Medallion#44, Sandstone Needle#49, Lotus Bloom#21(new), Lotus Bloom#18(new), Lotus Bloom#20(new)] hand=[Apex of Power, Pyretic Ritual, Desperate Ritual, Apex of Power, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30112005 --game-index 112005`
- **dragonstorm** x12 -- T8 m1 `{no land [ordered] + cast:Rite of Flame + cast:Desperate Ritual + cast:Apex of Power + cast:Apex of Power}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Sandstone Needle#47, Unclaimed Territory#57, Mountain#28, Mountain#31, Lotus Bloom#19(new), Lotus Bloom#21(new), Lotus Bloom#18(new)] hand=[Apex of Power, Desperate Ritual, Rite of Flame, Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30121383 --game-index 121383`
- **hydras** x10 -- T5 m1 `{no land + cast:Kalonian Hydra + cast:Garruk, Primal Hunter + cast:Primordial Hydra(x16) + loyalty:Nissa, Who Shakes the World@2}`  
  oracle audit backstop (1300 thread-CPU s) spent before this gate-rejected plan was audited  
  board: board=[Forest#1, Forest#2, Forest#5, Forest#3, Vernal Bloom#53, Forest#4, Nissa, Who Shakes the World#41, Forest#11, Voracious Hydra#60] hand=[Kalonian Hydra, Garruk, Primal Hunter, Primordial Hydra] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30015605 --game-index 15605`
- **hydras** x10 -- T5 m1 `{no land + cast:Kalonian Hydra + cast:Garruk, Primal Hunter + cast:Primordial Hydra(x16) + loyalty:Nissa, Who Shakes the World}`  
  oracle audit backstop (1300 thread-CPU s) spent before this gate-rejected plan was audited  
  board: board=[Forest#1, Forest#2, Forest#5, Forest#3, Vernal Bloom#53, Forest#4, Nissa, Who Shakes the World#41, Forest#11, Voracious Hydra#60] hand=[Kalonian Hydra, Garruk, Primal Hunter, Primordial Hydra] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30015605 --game-index 15605`
- **hydras** x10 -- T5 m1 `{no land + cast:Kalonian Hydra + cast:Garruk, Primal Hunter + cast:Primordial Hydra(x16)}`  
  oracle audit backstop (1300 thread-CPU s) spent before this gate-rejected plan was audited  
  board: board=[Forest#1, Forest#2, Forest#5, Forest#3, Vernal Bloom#53, Forest#4, Nissa, Who Shakes the World#41, Forest#11, Voracious Hydra#60] hand=[Kalonian Hydra, Garruk, Primal Hunter, Primordial Hydra] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30015605 --game-index 15605`
- **dragonstorm** x6 -- T6 m1 `{no land [ordered] + cast:Pyretic Ritual + cast:Desperate Ritual + cast:Apex of Power}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Mountain#26, Sandstone Needle#48, Ruby Medallion#44, Sandstone Needle#49, Lotus Bloom#21(new), Lotus Bloom#18(new), Lotus Bloom#20(new)] hand=[Apex of Power, Pyretic Ritual, Desperate Ritual, Apex of Power, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30112005 --game-index 112005`
- **dragonstorm** x6 -- T6 m1 `{no land [ordered] + cast:Pyretic Ritual + cast:Apex of Power + cast:Apex of Power}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Mountain#26, Sandstone Needle#48, Ruby Medallion#44, Sandstone Needle#49, Lotus Bloom#21(new), Lotus Bloom#18(new), Lotus Bloom#20(new)] hand=[Apex of Power, Pyretic Ritual, Desperate Ritual, Apex of Power, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30112005 --game-index 112005`
- **dragonstorm** x6 -- T6 m1 `{land=Mountain [ordered] + cast:Pyretic Ritual + cast:Apex of Power}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Mountain#26, Sandstone Needle#48, Ruby Medallion#44, Sandstone Needle#49, Lotus Bloom#21(new), Lotus Bloom#18(new), Lotus Bloom#20(new)] hand=[Apex of Power, Pyretic Ritual, Desperate Ritual, Apex of Power, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30112005 --game-index 112005`
- **dragonstorm** x6 -- T6 m1 `{no land [ordered] + cast:Pyretic Ritual + cast:Apex of Power}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Mountain#26, Sandstone Needle#48, Ruby Medallion#44, Sandstone Needle#49, Lotus Bloom#21(new), Lotus Bloom#18(new), Lotus Bloom#20(new)] hand=[Apex of Power, Pyretic Ritual, Desperate Ritual, Apex of Power, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30112005 --game-index 112005`
- **dragonstorm** x6 -- T6 m1 `{land=Mountain + cast:Apex of Power}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Mountain#26, Sandstone Needle#48, Ruby Medallion#44, Sandstone Needle#49, Lotus Bloom#21(new), Lotus Bloom#18(new), Lotus Bloom#20(new)] hand=[Apex of Power, Pyretic Ritual, Desperate Ritual, Apex of Power, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30112005 --game-index 112005`
- **dragonstorm** x6 -- T8 m1 `{no land [ordered] + cast:Rite of Flame + cast:Desperate Ritual + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Sandstone Needle#47, Unclaimed Territory#57, Mountain#28, Mountain#31, Lotus Bloom#19(new), Lotus Bloom#21(new), Lotus Bloom#18(new)] hand=[Apex of Power, Desperate Ritual, Rite of Flame, Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30121383 --game-index 121383`
- **dragonstorm** x6 -- T8 m1 `{no land [ordered] + cast:Rite of Flame + cast:Desperate Ritual + cast:Desperate Ritual + cast:Apex of Power}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Sandstone Needle#47, Unclaimed Territory#57, Mountain#28, Mountain#31, Lotus Bloom#19(new), Lotus Bloom#21(new), Lotus Bloom#18(new)] hand=[Apex of Power, Desperate Ritual, Rite of Flame, Apex of Power, Desperate Ritual, Irencrag Feat, Seething Song] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30121383 --game-index 121383`
- ... 52 more signatures in flags.jsonl

