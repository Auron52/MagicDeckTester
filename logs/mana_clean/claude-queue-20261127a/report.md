# Mana-clean gate `claude-queue-20261127a`

Mana-clean gate run: 16 decks, games (seed 30000000 + game index) angels 235000-334999, prevent_damage 130000-138999, hydras 44000-72999, stuffy_doll 24000-40999, creature_giving 75000-137999, sea_creatures 45000-72999, shadowborn_apostle 147000-171999, mill 48000-66999, dragonstorm 159000-208999, treasure_hunt 200000-281999, dragons 196000-295999, worldfire 23000-34999, 1_drop_burn 0-99999, beasts 0-11999, blood_sun 0-826, creature_burn 0-99999; src `80cb93166cfb`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | DOMINATED-LIFE | ORACLE-GAP | UNMODELED | CAPPED | CAPPED-DECISION | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| angels | 100000 | issues | 288 | 0 | 280 | 0 | 8 | 0 | 0 | 0 | 0 | 0 | 20483 | 16 | 344 | 48695 | 0 |
| prevent_damage | 9000 | clean | 0 | 0 | 0 | 0 | 0 | 55278 | 0 | 0 | 0 | 0 | 82 | 141 | 14 | 18881 | 0 |
| hydras | 29000 | issues | 200723 | 7 | 42 | 636 | 0 | 0 | 0 | 0 | 0 | 200038 | 55117 | 21 | 1867 | 652 | 275 |
| stuffy_doll | 17000 | issues | 326 | 0 | 21 | 305 | 0 | 0 | 0 | 0 | 0 | 0 | 23071 | 641 | 761 | 14426 | 0 |
| creature_giving | 63000 | issues | 10489 | 501 | 119 | 0 | 9727 | 1735 | 142 | 0 | 0 | 0 | 0 | 764 | 39 | 53975 | 46 |
| sea_creatures | 28000 | issues | 181 | 30 | 77 | 65 | 0 | 0 | 9 | 0 | 0 | 0 | 6634 | 13 | 6 | 11983 | 2 |
| shadowborn_apostle | 25000 | issues | 80 | 0 | 50 | 14 | 0 | 0 | 16 | 0 | 0 | 0 | 92237 | 713 | 3998 | 53002 | 97 |
| mill | 19000 | issues | 336 | 336 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 256 | 225 | 1358715 | 124095 | 0 |
| dragonstorm | 50000 | issues | 537 | 36 | 462 | 16 | 0 | 0 | 1 | 0 | 13 | 9 | 138327 | 618 | 26453 | 230656 | 814 |
| treasure_hunt | 82000 | issues | 1644 | 276 | 1279 | 89 | 0 | 5661 | 0 | 0 | 0 | 0 | 45228 | 1648 | 506043 | 270913 | 31 |
| dragons | 100000 | issues | 617 | 0 | 617 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 99718 | 113 | 3896 | 155589 | 0 |
| worldfire | 12000 | issues | 936 | 0 | 761 | 175 | 0 | 0 | 0 | 0 | 0 | 0 | 332642 | 8545 | 12637 | 22015 | 2 |
| 1_drop_burn | 100000 | issues | 16394 | 0 | 300 | 241 | 0 | 0 | 15853 | 0 | 0 | 0 | 1159821 | 0 | 2257 | 4 | 0 |
| beasts | 12000 | issues | 31796 | 877 | 6713 | 10098 | 0 | 0 | 14108 | 0 | 0 | 0 | 268682 | 19204 | 9631 | 146626 | 135 |
| blood_sun | 827 | issues | 335828 | 2 | 1086 | 0 | 0 | 1143 | 239 | 7388 | 12236 | 314877 | 130 | 1138 | 5946 | 14029 | 0 |
| creature_burn | 100000 | issues | 1427 | 0 | 0 | 0 | 0 | 0 | 1427 | 0 | 0 | 0 | 0 | 0 | 0 | 15767 | 0 |

**MISS = 26912, ORACLE-GAP = 14486.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=2339464 plans=31507968 skipped=0 applies=1618665813 frontiers=1462861471 pruned=495627843 options=1195979324 engine_drops=1711404 gate_rejects=46016300(checked 43576647, skipped 2292234) MISS=2097 ORACLE-GAP=1453 UNMODELED=0 CAPPED=13 ms=90218017 ===
=== MANA ORACLE classes: ORACLE-GAP=1453 ENUM-GAP=409 APPLY-DROP=652 APPLY-DROP-ORDER=1036 PHANTOM=315642 DOMINATED-LAND=450581 CONTINUATION-DIFFERS=2231 DROP-COVERED=1391800 CAPPED=13 HIDDEN-INFO=1188 CAPPED-DECISION=200047 ===
=== MANA ORACLE enum-gap rejections: noop=18739 hidden=5003 host_blind=245 cont_land=266 ===
=== MANA ORACLE hit-only cuts: 526716117 unpayable-payment subtrees not built ===
=== MANA ORACLE order fold: 10715264 order variants are an earlier order's apply (trailing activations) ===
=== MANA ORACLE order fold: 1195 plans past the order bound searched by fold class ===
=== MANA ORACLE hit-only cuts: 987264 frontiers below a dropped action not built ===
=== MANA ORACLE goal fold: 20308590 plans, 42931 frontiers not expanded, 96 plans realised again in full, 0 checked ===
=== MANA ORACLE decision budget: 7 decisions spent it, 200041 plans not audited ===
=== MANA ORACLE board memory: 0 searches capped by MTG_MANA_ORACLE_BOARD_MB, 514599539 pruned frontier boards and 3028 hit-only outcome boards given back early ===
=== MANA ORACLE shared boards: 77153651 outcome boards held for 154601374 outcomes (77447723 shared); 0 share checks ===
=== MANA ORACLE capped-plan escalation: 16727 retried, 16713 finished ===
=== MANA ORACLE unmodeled: 1 x another order's search was partial ===
=== MANA ORACLE unmodeled: 22249 x floating pool too large to enumerate set-asides ===
=== MANA ORACLE unmodeled: 7103118 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE rollback census: lost=2097 depth0=11 depth1=134 depth2=125 depth3+=279 inconsistent=0 capped=103 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=1036 off-menu=409 need_in_plan=295 need_later=357 pay_rollback_rescued=0/652 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | ORACLE-GAP | UNMODELED | CAPPED | CAPPED-DECISION |
|---|---|---|---|---|---|---|---|---|---|---|
| angels | 824252 | 3302924 | 0 | 280 | 0 | 8 | 0 | 0 | 0 | 0 |
| prevent_damage | 89394 | 1851570 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| hydras | 284701 | 3031048 | 7 | 42 | 636 | 0 | 0 | 0 | 0 | 5000 |
| stuffy_doll | 142119 | 1018879 | 0 | 21 | 305 | 0 | 0 | 0 | 0 | 0 |
| creature_giving | 517036 | 4117103 | 501 | 119 | 0 | 5000 | 142 | 0 | 0 | 0 |
| sea_creatures | 293147 | 867653 | 30 | 77 | 65 | 0 | 9 | 0 | 0 | 0 |
| shadowborn_apostle | 214560 | 1934008 | 0 | 50 | 14 | 0 | 16 | 0 | 0 | 0 |
| mill | 171369 | 17034849 | 336 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| dragonstorm | 385020 | 2360871 | 36 | 462 | 16 | 0 | 1 | 0 | 13 | 9 |
| treasure_hunt | 582232 | 12228113 | 276 | 1279 | 89 | 0 | 0 | 0 | 0 | 0 |
| dragons | 1007395 | 7120447 | 0 | 617 | 0 | 0 | 0 | 0 | 0 | 0 |
| worldfire | 136435 | 1217182 | 0 | 761 | 175 | 0 | 0 | 0 | 0 | 0 |
| 1_drop_burn | 633234 | 9797744 | 0 | 300 | 241 | 0 | 5000 | 0 | 0 | 0 |
| beasts | 108398 | 2484916 | 877 | 6520 | 6684 | 0 | 7652 | 0 | 0 | 0 |
| blood_sun | 13142 | 554087 | 2 | 1086 | 0 | 0 | 239 | 7388 | 9403 | 10165 |
| creature_burn | 848548 | 5313282 | 0 | 0 | 0 | 0 | 1427 | 0 | 0 | 0 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth0 | depth1 | depth2 | depth3+ | capped | no-failed-payment | reorder | off-menu | rescued | need-later |
|---|---|---|---|---|---|---|---|---|---|---|
| angels | 0 | 32 | 248 | 0 | 0 | 0 | 0 | 0 | 0 | 6 |
| hydras | 0 | 6 | 0 | 36 | 0 | 0 | 636 | 7 | 0 | 42 |
| stuffy_doll | 0 | 1 | 17 | 3 | 0 | 0 | 305 | 0 | 0 | 0 |
| creature_giving | 0 | 101 | 9 | 9 | 0 | 0 | 0 | 501 | 0 | 101 |
| sea_creatures | 11 | 44 | 21 | 1 | 0 | 0 | 65 | 30 | 0 | 0 |
| shadowborn_apostle | 0 | 7 | 6 | 37 | 0 | 0 | 14 | 0 | 0 | 34 |
| mill | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 336 | 0 | 0 |
| dragonstorm | 0 | 76 | 81 | 202 | 103 | 0 | 16 | 36 | 0 | 281 |
| treasure_hunt | 0 | 1024 | 123 | 132 | 0 | 0 | 89 | 276 | 6 | 1015 |
| dragons | 0 | 320 | 278 | 13 | 0 | 6 | 0 | 0 | 24 | 307 |
| worldfire | 0 | 445 | 246 | 56 | 0 | 14 | 175 | 0 | 0 | 311 |
| 1_drop_burn | 0 | 0 | 100 | 33 | 0 | 167 | 241 | 0 | 0 | 0 |
| beasts | 4 | 3857 | 1960 | 699 | 0 | 0 | 6684 | 877 | 69 | 2790 |
| blood_sun | 0 | 200 | 9 | 13 | 864 | 0 | 0 | 2 | 0 | 111 |

## ENUM-GAP (2065 flags, 1542 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **mill** x32 -- T2 m1 `{land=Darkslick Shores + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Darkslick Shores#9:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Watery Grave|L||0,0|card 1>0)  
  board: board=[Watery Grave#57, Hedron Crab#27] hand=[Glimpse the Unthinkable, Surgical Extraction, Island, Drown in the Loch, Visions of Beyond, Darkslick Shores, Drown in the Loch] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30048461 --game-index 48461`
- **mill** x25 -- T2 m1 `{land=Watery Grave + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Watery Grave#57:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Darkslick Shores|L||0,0|card 1>0)  
  board: board=[Darkslick Shores#11, Hedron Crab#27] hand=[Watery Grave, Fractured Sanity, Tasha's Hideous Laughter, Visions of Beyond, Counterspell, Surgical Extraction] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30048241 --game-index 48241`
- **mill** x19 -- T2 m1 `{land=Island + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Island#31:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Watery Grave|L||0,0|card 1>0)  
  board: board=[Watery Grave#57, Hedron Crab#27] hand=[Glimpse the Unthinkable, Surgical Extraction, Island, Drown in the Loch, Visions of Beyond, Darkslick Shores, Drown in the Loch] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30048461 --game-index 48461`
- **mill** x16 -- T2 m1 `{land=Drowned Catacomb + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Drowned Catacomb#17:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Watery Grave|L||0,0|card 1>0)  
  board: board=[Watery Grave#57, Ruin Crab#39] hand=[Drowned Catacomb, Scalding Tarn, Visions of Beyond, Drown in the Loch, Fractured Sanity, Drown in the Loch, Surgical Extraction] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30048539 --game-index 48539`
- **mill** x15 -- T2 m1 `{land=Darkslick Shores + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Darkslick Shores#10:U Watery Grave#58:B] ; pay2[(float only)]  
  board: board=[Watery Grave#58, Hedron Crab#28] hand=[Visions of Beyond, Darkslick Shores, Visions of Beyond, Surgical Extraction, Drown in the Loch, Archive Trap] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30049369 --game-index 49369`
- **mill** x15 -- T2 m1 `{land=Drowned Catacomb + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Drowned Catacomb#18:U Watery Grave#57:B] ; pay2[(float only)]  
  board: board=[Watery Grave#57, Ruin Crab#39] hand=[Glimpse the Unthinkable, Surgical Extraction, Darkslick Shores, Visions of Beyond, Drown in the Loch, Ruin Crab, Drowned Catacomb] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30049711 --game-index 49711`
- **mill** x14 -- T2 m1 `{land=Island + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Island#32:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Darkslick Shores|L||0,0|card 1>0)  
  board: board=[Darkslick Shores#9, Hedron Crab#27] hand=[Counterspell, Fractured Sanity, Island, Visions of Beyond, Drowned Catacomb, Surgical Extraction] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30048379 --game-index 48379`
- **mill** x13 -- T2 m1 `{land=Oboro, Palace in the Clouds + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Oboro, Palace in the Clouds#34:U] ; pay2[(float only)] (the menu reaches this board only with fewer resources: U:Watery Grave|L||0,0|card 1>0)  
  board: board=[Watery Grave#57, Ruin Crab#39] hand=[Drown in the Loch, Visions of Beyond, Oboro, Palace in the Clouds, Tasha's Hideous Laughter, Tasha's Hideous Laughter, Surgical Extraction] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30048266 --game-index 48266`
- **mill** x13 -- T2 m1 `{land=Watery Grave + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Watery Grave#57:U Watery Grave#58:B] ; pay2[(float only)]  
  board: board=[Watery Grave#58, Ruin Crab#39] hand=[Oboro, Palace in the Clouds, Surgical Extraction, Scalding Tarn, Visions of Beyond, Swamp, Watery Grave, Counterspell] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30049203 --game-index 49203`
- **treasure_hunt** x12 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Thundering Falls#53:U Sandstone Needle#38:R] ; pay2[Ferrous Lake#5:UR]  
  board: board=[Thundering Falls#53, Sandstone Needle#38] hand=[Lonely Sandbar, Treasure Hunt, Ferrous Lake, Treasure Hunt, Lonely Sandbar] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30201302 --game-index 201302`
- **treasure_hunt** x10 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Thundering Falls#55:U Sandstone Needle#38:R] ; pay2[Steam Vents#44:U]  
  board: board=[Thundering Falls#55, Sandstone Needle#38] hand=[Steam Vents, Treasure Hunt, Ferrous Lake, Treasure Hunt, Forgotten Cave] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30203638 --game-index 203638`
- **mill** x10 -- T3 m1 `{land=Island + cast:Visions of Beyond + cast:Surgical Extraction}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Island#31:U Darkslick Shores#9:B] ; pay2[(float only)]  
  board: board=[Darkslick Shores#9, Hedron Crab#27, Ruin Crab#39] hand=[Island, Visions of Beyond, Tasha's Hideous Laughter, Counterspell, Darkslick Shores, Surgical Extraction] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Mill/Mill.cod' --profile 'decks/Mill/Mill.profile.json' --games 1 --seed 30050104 --game-index 50104`
- ... 1530 more signatures in flags.jsonl

## APPLY-DROP (11614 flags, 3067 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **beasts** x86 -- T3 m1 `{land=Forest + kind?:Kaheera, the Orphanguard}`  
  engine dropped [cast Utopia Sprawl]; every action pays with pay1[Forest#13:G Forest#12:G Cavern of Souls#2:C] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Cavern of Souls#2 tapped by oracle only; Utopia Sprawl#46 only in oracle; hand size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Forest#13:G Forest#12:G Cavern of Souls#2:C] ; pay2[(float only)]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#13, Utopia Sprawl#45@13, Cavern of Souls#2, Kaheera, the Orphanguard#27] hand=[Garruk's Packleader, Wooded Foothills, Thragtusk, Forest, Utopia Sprawl] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000750 --game-index 750`
- **beasts** x84 -- T3 m1 `{land=Forest + kind?:Kaheera, the Orphanguard}`  
  engine dropped [cast Wild Growth]; every action pays with pay1[Forest#13:G Forest#12:G Cavern of Souls#3:C] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Cavern of Souls#3 tapped by oracle only; Wild Growth#52 only in oracle; hand size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Forest#13:G Forest#12:G Cavern of Souls#3:C] ; pay2[(float only)]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#13, Utopia Sprawl#45@13, Cavern of Souls#3, Garruk's Harbinger#18] hand=[Krosan Warchief, Forest, Elder Gargaroth, Nykthos, Shrine to Nyx, Garruk's Packleader, Wild Growth] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000007 --game-index 7`
- **beasts** x76 -- T3 m1 `{land=Contested Cliffs + kind?:Kaheera, the Orphanguard}`  
  engine dropped [cast Utopia Sprawl]; every action pays with pay1[Forest#12:G Contested Cliffs#4:C Cavern of Souls#2:C] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Cavern of Souls#2 tapped by oracle only; Utopia Sprawl#45 only in oracle; hand size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Forest#12:G Contested Cliffs#4:C Cavern of Souls#2:C] ; pay2[(float only)]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#12, Wild Growth#49@12, Cavern of Souls#2, Garruk's Harbinger#18] hand=[Mountain, Utopia Sprawl, Gruul Turf, Contested Cliffs, Migloz, Maze Crusher] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000584 --game-index 584`
- **beasts** x72 -- T3 m1 `{land=Forest + kind?:Kaheera, the Orphanguard}`  
  engine dropped [cast Utopia Sprawl]; every action pays with pay1[Stomping Ground#40:R Forest#14:G Cavern of Souls#3:C] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Cavern of Souls#3 tapped by oracle only; Utopia Sprawl#46 only in oracle; hand size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Stomping Ground#40:R Forest#14:G Cavern of Souls#3:C] ; pay2[(float only)]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Stomping Ground#40, Utopia Sprawl#45@40, Cavern of Souls#3, Migloz, Maze Crusher#33] hand=[Utopia Sprawl, Elder Gargaroth, Garruk, Primal Hunter, Forest] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000302 --game-index 302`
- **beasts** x60 -- T3 m1 `{land=Wooded Foothills + kind?:Kaheera, the Orphanguard}`  
  engine dropped [cast Utopia Sprawl]; every action pays with pay1[Forest#13:G Forest#15:G Cavern of Souls#2:C] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Cavern of Souls#2 tapped by oracle only; Utopia Sprawl#46 only in oracle; hand size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Forest#13:G Forest#15:G Cavern of Souls#2:C] ; pay2[(float only)]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#13, Utopia Sprawl#45@13, Cavern of Souls#2, Kaheera, the Orphanguard#27] hand=[Garruk's Packleader, Wooded Foothills, Thragtusk, Forest, Utopia Sprawl] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000750 --game-index 750`
- **beasts** x60 -- T5 m1 `{no land + cast:Utopia Sprawl(x5)@12 + cast:Woodland Bellower}`  
  engine dropped [cast Woodland Bellower]; every action pays with pay1[Forest#12:G Nykthos, Shrine to Nyx#35:G] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Forest#12:G Nykthos, Shrine to Nyx#35:G] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#12, Utopia Sprawl#45@12, Nykthos, Shrine to Nyx#35, Garruk's Harbinger#18, Wild Growth#49@35, Elder Gargaroth#8] hand=[Utopia Sprawl, Herald of Ilharg, Krosan Warchief, Herald of Ilharg, Kaheera, the Orphanguard, Woodland Bellower, Utopia Sprawl, Krosan Warchief] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30007087 --game-index 7087`
- **treasure_hunt** x56 -- T4 m1 `{no land + cast:Treasure Hunt}`  
  engine dropped [cast Land's Edge]; every action pays with pay1[Saprazzan Skerry#40:U Ferrous Lake#6:UR] ; pay2[Reliquary Tower#31:C Cascade Bluffs#2:RR] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Saprazzan Skerry#40:U Ferrous Lake#6:UR] ; pay2[Reliquary Tower#31:C Cascade Bluffs#2:RR]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Reliquary Tower#31, Ferrous Lake#6, Saprazzan Skerry#40] hand=[Cascade Bluffs, Remote Isle, Treasure Hunt, Island, Temple of Epiphany, Fiery Islet, Reliquary Tower, Fiery Islet, Fiery Islet, Ferrous Lake, Island, Forgotten Cave, Forgotten Cave, Forgotten Cave, Sandstone Needle, Thundering Falls, Saprazzan Skerry, Ferrous Lake, Cascade Bluffs, Frostboil Snarl, Steam Vents, Thundering Falls,   
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30202069 --game-index 202069`
- **blood_sun** x50 -- T6 m1 `{land=Rakdos Carnarium + cast:Diabolic Revelation(x2) + cast:Ulamog, the Ceaseless Hunger}`  
  engine dropped [cast Diabolic Revelation]; every action pays with pay1[Overgrown Tomb#49:B Lotus Vale#45:W Scorched Ruins#54:C Rakdos Carnarium#50:B City of Brass#11:W] ; pay2[Golgari Rot Farm#32:B Scorched Ruins#56:C], reaching a board the menu's other order of these actions does not (Rakdos Carnarium#50 only in engine; Horizon Canopy#100043 only in oracle; float engine - oracle -) || ROLLBACK capped: the payment search hit its cap before a prefix was found; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[City of Brass#11, Burgeoning#9, Exploration#27, Golgari Rot Farm#32, Overgrown Tomb#49, Blood Sun#1, Lotus Vale#45, Scorched Ruins#54, Scorched Ruins#56] hand=[Time Warp, Diabolic Revelation, Diabolic Revelation, Ulamog, the Ceaseless Hunger, Rakdos Carnarium] float=- life=8  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000203 --game-index 203`
- **treasure_hunt** x48 -- T4 m1 `{no land + cast:Treasure Hunt}`  
  engine dropped [cast Land's Edge]; every action pays with pay1[Saprazzan Skerry#43:U] ; pay2[Reliquary Tower#32:C Ferrous Lake#5:UR Cascade Bluffs#3:UR], reaching a board the menu's other order of these actions does not (Cascade Bluffs#4 only in engine; Reliquary Tower#32 only in oracle; graveyard cards differ (engine vs oracle: +Reliquary Tower -Cascade Bluffs); float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Saprazzan Skerry#43:U] ; pay2[Reliquary Tower#32:C Ferrous Lake#5:UR Cascade Bluffs#3:UR]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced o  
  board: board=[Cascade Bluffs#3, Ferrous Lake#5, Saprazzan Skerry#43] hand=[Land's Edge, Island, Sandstone Needle, Reliquary Tower, Reliquary Tower, Cascade Bluffs, Reliquary Tower, Treasure Hunt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30200622 --game-index 200622`
- **beasts** x48 -- T3 m1 `{land=Contested Cliffs + kind?:Kaheera, the Orphanguard}`  
  engine dropped [cast Utopia Sprawl]; every action pays with pay1[Stomping Ground#38:R Contested Cliffs#4:C Cavern of Souls#2:C] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Cavern of Souls#2 tapped by oracle only; Utopia Sprawl#46 only in oracle; hand size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Stomping Ground#38:R Contested Cliffs#4:C Cavern of Souls#2:C] ; pay2[(float only)]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Stomping Ground#38, Utopia Sprawl#45@38, Cavern of Souls#2, Kaheera, the Orphanguard#27] hand=[Urza's Incubator, Mountain, Utopia Sprawl, Gruul Turf, Contested Cliffs] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000124 --game-index 124`
- **blood_sun** x48 -- T6 m1 `{land=Rakdos Carnarium + cast:Diabolic Revelation(x2) + cast:Ulamog, the Ceaseless Hunger}`  
  engine dropped [cast Diabolic Revelation]; every action pays with pay1[Overgrown Tomb#49:B Lotus Vale#45:W Scorched Ruins#54:C Rakdos Carnarium#50:B] ; pay2[City of Brass#11:B Golgari Rot Farm#32:B Scorched Ruins#56:C], reaching a board the menu's other order of these actions does not (Rakdos Carnarium#50 only in engine; Plains#109000 only in oracle; float engine - oracle -) || ROLLBACK capped: the payment search hit its cap before a prefix was found; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[City of Brass#11, Burgeoning#9, Exploration#27, Golgari Rot Farm#32, Overgrown Tomb#49, Blood Sun#1, Lotus Vale#45, Scorched Ruins#54, Scorched Ruins#56] hand=[Time Warp, Diabolic Revelation, Diabolic Revelation, Ulamog, the Ceaseless Hunger, Rakdos Carnarium] float=- life=8  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000203 --game-index 203`
- **treasure_hunt** x46 -- T4 m1 `{no land + cast:Treasure Hunt}`  
  engine dropped [cast Land's Edge]; every action pays with pay1[Reliquary Tower#34:C Ferrous Lake#5:UR] ; pay2[Saprazzan Skerry#40:U Cascade Bluffs#4:RR], reaching a board the menu's other order of these actions does not (Reliquary Tower#34 tapped by oracle only; Ferrous Lake#5 tapped by oracle only; Cascade Bluffs#4 only in oracle; Land's Edge#26 only in oracle; hand size differs; graveyard size differs; opponent life engine 20 oracle -6; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Reliquary Tower#34:C Ferrous Lake#5:UR] ; pay2[Saprazzan Skerry  
  board: board=[Saprazzan Skerry#40, Reliquary Tower#34, Ferrous Lake#5] hand=[Temple of Epiphany, Lonely Sandbar, Land's Edge, Island, Cascade Bluffs, Cascade Bluffs, Lonely Sandbar, Temple of Epiphany, Thundering Falls, Treasure Hunt, Frostboil Snarl, Temple of Epiphany] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30205623 --game-index 205623`
- ... 3055 more signatures in flags.jsonl

## APPLY-DROP-ORDER (8225 flags, 1918 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **hydras** x166 -- T5 m1 `{land=Forest + cast:Vernal Bloom + cast:Vernal Bloom + cast:Nature's Lore}`  
  engine dropped [cast Nature's Lore]; the order Vernal Bloom > Nature's Lore > Vernal Bloom pays every action with pay1[Forest#1:G Forest#2:G Forest#4:G Forest#15:G] ; pay2[Forest#3:G] ; pay3[Forest#18:G Forest#23:G]  
  board: board=[Forest#1, Forest#2, Lightning Greaves#33, Forest#4, Forest#3, Voracious Hydra#57, Forest#18, Voracious Hydra#58] hand=[Vernal Bloom, Vernal Bloom, Nature's Lore, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30045511 --game-index 45511`
- **hydras** x123 -- T4 m1 `{no land + cast:Vernal Bloom + cast:Vernal Bloom + cast:Nature's Lore}`  
  engine dropped [cast Nature's Lore]; the order Vernal Bloom > Nature's Lore > Vernal Bloom pays every action with pay1[Forest#1:G Forest#2:G Forest#5:G Forest#6:G] ; pay2[Forest#3:G] ; pay3[Forest#9:G Forest#23:G]  
  board: board=[Forest#1, Forest#2, Forest#5, Forest#6, Forest#3, Forest#9, Neverwinter Hydra#39] hand=[Vernal Bloom, Vernal Bloom, Forest, Nature's Lore] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30045985 --game-index 45985`
- **hydras** x101 -- T6 m1 `{no land + cast:Vernal Bloom + cast:Vernal Bloom + cast:Three Visits}`  
  engine dropped [cast Three Visits]; the order Vernal Bloom > Three Visits > Vernal Bloom pays every action with pay1[Forest#1:G Forest#2:G Forest#4:G Forest#3:G] ; pay2[Forest#6:G] ; pay3[Forest#18:G Forest#17:G]  
  board: board=[Forest#1, Forest#2, Forest#4, Forest#3, Primordial Hydra#42, Steelbane Hydra#50, Forest#6, Forest#18] hand=[Vernal Bloom, Vernal Bloom, Three Visits] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30044100 --game-index 44100`
- **beasts** x86 -- T3 m1 `{no land + cast:Krosan Warchief + cast:Urza's Incubator}`  
  engine dropped [cast Urza's Incubator]; the order Urza's Incubator > Krosan Warchief pays every action with pay1[Forest#14:G] ; pay2[Forest#17:G]  
  board: board=[Forest#14, Utopia Sprawl#47@14, Forest#17, Wild Growth#50@14] hand=[Krosan Warchief, Forest, Kaheera, the Orphanguard, Urza's Incubator] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000034 --game-index 34`
- **beasts** x73 -- T4 m1 `{no land + cast:Krosan Warchief + cast:Urza's Incubator}`  
  engine dropped [cast Urza's Incubator]; the order Urza's Incubator > Krosan Warchief pays every action with pay1[Stomping Ground#38:R] ; pay2[Forest#12:G]  
  board: board=[Stomping Ground#38, Wild Growth#49@38, Contested Cliffs#4, Forest#12, Utopia Sprawl#47@38, Herald of Ilharg#23] hand=[Krosan Warchief, Urza's Incubator, Gruul Turf, Kaheera, the Orphanguard, Questing Beast] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000942 --game-index 942`
- **beasts** x70 -- T2 m1 `{land=Forest + cast:Utopia Sprawl(x5)@12 + cast:Wild Growth@2}`  
  engine dropped [cast Wild Growth]; the order Wild Growth > Utopia Sprawl pays every action with pay1[Forest#12:G] ; pay2[Cavern of Souls#2:C]  
  board: board=[Cavern of Souls#2] hand=[Thragtusk, Herald of Ilharg, Utopia Sprawl, Wild Growth, Contested Cliffs, Urza's Incubator, Forest, Migloz, Maze Crusher] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000353 --game-index 353`
- **1_drop_burn** x66 -- T4 m1 `{land=Mountain + cast:Needle Drop + cast:Skewer the Critics}`  
  engine dropped [cast Needle Drop]; the order Skewer the Critics > Needle Drop pays every action with pay1[Mountain#31:R Mountain#32:R Mountain#34:R] ; pay2[Mountain#33:R]  
  board: board=[Mountain#31, Goblin Guide#12, Mountain#32, Grim Lavamancer#16, Mountain#33] hand=[Needle Drop, Mountain, Skewer the Critics] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30007936 --game-index 7936`
- **hydras** x56 -- T5 m1 `{land=Forest + cast:Nature's Lore + cast:Vernal Bloom + cast:Vernal Bloom}`  
  engine dropped [cast Nature's Lore]; the order Vernal Bloom > Nature's Lore > Vernal Bloom pays every action with pay1[Forest#10:G Forest#21:G Forest#3:G Forest#14:G] ; pay2[Forest#7:G] ; pay3[Forest#13:G Forest#16:G]  
  board: board=[Forest#10, Forest#21, Forest#3, Forest#7, Steelbane Hydra#50, Forest#13, Garruk, Primal Hunter#26, 3/3 Beast Token#1000] hand=[Nature's Lore, Vernal Bloom, Forest, Vernal Bloom] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30047259 --game-index 47259`
- **beasts** x37 -- T4 m1 `{no land + cast:Kaheera, the Orphanguard + cast:Urza's Incubator}`  
  engine dropped [cast Urza's Incubator]; the order Urza's Incubator > Kaheera, the Orphanguard pays every action with pay1[Stomping Ground#38:R] ; pay2[Forest#13:G Forest#17:G]  
  board: board=[Stomping Ground#38, Utopia Sprawl#45@38, Forest#13, Wild Growth#49@38, Forest#17, Elder Gargaroth#8] hand=[Garruk, Primal Hunter, Forest, Kaheera, the Orphanguard, Woodland Bellower, Urza's Incubator] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000032 --game-index 32`
- **1_drop_burn** x36 -- T4 m1 `{land=Mountain + cast:Needle Drop + cast:Skewer the Critics}`  
  engine dropped [cast Needle Drop]; the order Skewer the Critics > Needle Drop pays every action with pay1[Mountain#31:R Sunbaked Canyon#55:R Mountain#33:R] ; pay2[Mountain#32:R]  
  board: board=[Mountain#31, Sunbaked Canyon#55, Grim Lavamancer#16, Mountain#32] hand=[Needle Drop, Mountain, Skewer the Critics] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30022117 --game-index 22117`
- **beasts** x36 -- T5 m1 `{no land + cast:Garruk's Packleader + cast:Urza's Incubator}`  
  engine dropped [cast Urza's Incubator]; the order Urza's Incubator > Garruk's Packleader pays every action with pay1[Forest#13:G] ; pay2[Forest#17:G Forest#12:G]  
  board: board=[Forest#13, Utopia Sprawl#45@13, Forest#17, Utopia Sprawl#46@13, Forest#12, Utopia Sprawl#47@13, Elder Gargaroth#8, Thragtusk#41, 3/3 Beast Token#1000] hand=[Garruk's Packleader, Kaheera, the Orphanguard, Garruk's Packleader, Urza's Incubator] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000022 --game-index 22`
- **beasts** x36 -- T3 m1 `{land=Forest + cast:Krosan Warchief + cast:Urza's Incubator}`  
  engine dropped [cast Urza's Incubator]; the order Urza's Incubator > Krosan Warchief pays every action with pay1[Stomping Ground#38:R Forest#12:G] ; pay2[Forest#13:G]  
  board: board=[Stomping Ground#38, Utopia Sprawl#45@38, Forest#13, Migloz, Maze Crusher#32] hand=[Krosan Warchief, Herald of Ilharg, Thragtusk, Forest, Garruk's Packleader, Urza's Incubator] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30000303 --game-index 303`
- ... 1906 more signatures in flags.jsonl

## DOMINATED-CREATURE (5008 flags, 2041 signatures)

_the engine's payment taps a creature that could attack, where another payment keeps it untapped and loses nothing._

- **creature_giving** x104 -- T2 m1 `{land=Windswept Heath + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Forbidden Orchard#26:G] ; pay2[Breeding Pool#6:G Forbidden Orchard#27:W]; the engine ended board=[Birds of Paradise#3(T), Breeding Pool#6(T)(new), Forbidden Orchard#27(T)(new), Suture Priest#44(new)] hand=[Defense of the Heart, Sylvan Scrying] float=- life=17  
  board: board=[Forbidden Orchard#26, Birds of Paradise#3] hand=[Windswept Heath, Defense of the Heart, Suture Priest, Sylvan Scrying, Crop Rotation] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30075072 --game-index 75072`
- **creature_giving** x76 -- T2 m1 `{land=Windswept Heath + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Forbidden Orchard#26:G] ; pay2[Breeding Pool#6:G Forbidden Orchard#25:R]; the engine ended board=[Birds of Paradise#5(T), Breeding Pool#6(T)(new), Forbidden Orchard#25(T)(new), Varchild's War-Riders#55(new)] hand=[Sylvan Scrying, Defense of the Heart] float=- life=17  
  board: board=[Forbidden Orchard#26, Birds of Paradise#5] hand=[Sylvan Scrying, Windswept Heath, Crop Rotation, Defense of the Heart, Varchild's War-Riders] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30075038 --game-index 75038`
- **creature_giving** x49 -- T3 m1 `{land=Forbidden Orchard + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker); keeps Forbidden Orchard untapped -- by paying pay1[Azorius Chancery#1:W Forbidden Orchard#24:G] ; pay2[(float only)]; the engine ended board=[Birds of Paradise#5(T), Varchild's War-Riders#56, Forbidden Orchard#24(T)(new), Forbidden Orchard#26(T)(new), Suture Priest#43(new)] hand=[Forbidden Orchard] float=- life=20  
  board: board=[Birds of Paradise#5, Varchild's War-Riders#56, Azorius Chancery#1] hand=[Forbidden Orchard, Suture Priest, Forbidden Orchard, Crop Rotation] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30075370 --game-index 75370`
- **creature_giving** x39 -- T2 m1 `{land=Windswept Heath + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Stomping Ground#42:R Breeding Pool#6:G] ; pay2[Forbidden Orchard#27:W]; the engine ended board=[Stomping Ground#42(T), Birds of Paradise#2(T), Forbidden Orchard#27(T)(new), Varchild's War-Riders#54(new)] hand=[Overgrown Tomb, Defense of the Heart, Windswept Heath] float=- life=15  
  board: board=[Stomping Ground#42, Birds of Paradise#2] hand=[Overgrown Tomb, Windswept Heath, Varchild's War-Riders, Crop Rotation, Defense of the Heart, Windswept Heath] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30075539 --game-index 75539`
- **creature_giving** x37 -- T3 m1 `{land=Tree of Tales + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Forbidden Orchard#25:G] ; pay2[Tree of Tales#52:G Forbidden Orchard#24:W]; the engine ended board=[Birds of Paradise#3(T), Birds of Paradise#5, Tree of Tales#52(T)(new), Forbidden Orchard#24(T)(new), Suture Priest#46(new)] hand=[] float=- life=20  
  board: board=[Forbidden Orchard#25, Birds of Paradise#3, Birds of Paradise#5] hand=[Suture Priest, Crop Rotation, Tree of Tales] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30076052 --game-index 76052`
- **creature_giving** x36 -- T3 m1 `{land=Forbidden Orchard + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Azorius Chancery#1:W Forbidden Orchard#26:G] ; pay2[Forbidden Orchard#24:G keep-floating {W}]; the engine ended board=[Birds of Paradise#5(T), Forbidden Orchard#26(T)(new), Forbidden Orchard#24(T)(new)] hand=[Windswept Heath, Forbidden Orchard] float=- life=20  
  board: board=[Birds of Paradise#5, Azorius Chancery#1] hand=[Crop Rotation, Sylvan Scrying, Forbidden Orchard, Windswept Heath] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30076466 --game-index 76466`
- **creature_giving** x30 -- T2 m1 `{land=Forest + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Forbidden Orchard#24:G] ; pay2[Forest#28:G Forbidden Orchard#25:W]; the engine ended board=[Birds of Paradise#2(T), Forest#28(T)(new), Forbidden Orchard#25(T)(new), Suture Priest#43(new)] hand=[Defense of the Heart, Windswept Heath, City of Brass, Massacre Wurm] float=- life=20  
  board: board=[Forbidden Orchard#24, Birds of Paradise#2] hand=[Suture Priest, Defense of the Heart, Windswept Heath, Crop Rotation, Forest, City of Brass, Massacre Wurm] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30075131 --game-index 75131`
- **creature_giving** x29 -- T2 m1 `{land=Windswept Heath + cast:Crop Rotation + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Breeding Pool#6:G Forbidden Orchard#24:G] ; pay2[Forbidden Orchard#27:G keep-floating {G}] ; pay3[Forbidden Orchard#26:W]; the engine ended board=[Birds of Paradise#2(T), Forbidden Orchard#27(T)(new), Forbidden Orchard#26(T)(new), Suture Priest#43(new)] hand=[Massacre Wurm, Defense of the Heart, Forbidden Orchard] float=- life=17  
  board: board=[Forbidden Orchard#24, Birds of Paradise#2] hand=[Crop Rotation, Suture Priest, Crop Rotation, Massacre Wurm, Defense of the Heart, Forbidden Orchard, Windswept Heath] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30075297 --game-index 75297`
- **creature_giving** x28 -- T4 m1 `{no land + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Forbidden Orchard#27:G Forbidden Orchard#24:G] ; pay2[Breeding Pool#6:U Forbidden Orchard#26:U]; the engine ended board=[Birds of Paradise#4(T), Breeding Pool#6(T), Essence Warden#20, Forbidden Orchard#24(T), Massacre Wurm#34(new), Massacre Wurm#35(new), Forbidden Orchard#26(T)(new), Hunted Phantasm#31(new)] hand=[] float=- life=30  
  board: board=[Forbidden Orchard#27, Birds of Paradise#4, Breeding Pool#6, Essence Warden#20, Forbidden Orchard#24, Massacre Wurm#34(new), Massacre Wurm#35(new)] hand=[Hunted Phantasm, Crop Rotation] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30075700 --game-index 75700`
- **creature_giving** x26 -- T2 m1 `{land=Temple Garden + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Forbidden Orchard#24:G] ; pay2[Temple Garden#51:G Forbidden Orchard#25:W]; the engine ended board=[Birds of Paradise#3(T), Temple Garden#51(T)(new), Forbidden Orchard#25(T)(new)] hand=[City of Brass, Breeding Pool, Forbidden Orchard] float=- life=18  
  board: board=[Forbidden Orchard#24, Birds of Paradise#3] hand=[Sylvan Scrying, City of Brass, Crop Rotation, Breeding Pool, Temple Garden] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30076090 --game-index 76090`
- **creature_giving** x25 -- T2 m1 `{land=Breeding Pool + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Forbidden Orchard#24:G] ; pay2[Breeding Pool#6:G Forbidden Orchard#25:W]; the engine ended board=[Birds of Paradise#3(T), Breeding Pool#6(T)(new), Forbidden Orchard#25(T)(new)] hand=[City of Brass, Temple Garden, Forbidden Orchard] float=- life=18  
  board: board=[Forbidden Orchard#24, Birds of Paradise#3] hand=[Sylvan Scrying, City of Brass, Crop Rotation, Breeding Pool, Temple Garden] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30076090 --game-index 76090`
- **creature_giving** x24 -- T3 m1 `{no land + cast:Crop Rotation}`  
  keeps Birds of Paradise untapped (attacker) -- by paying pay1[Forbidden Orchard#27:G] ; pay2[Breeding Pool#6:G Forbidden Orchard#26:U Forbidden Orchard#25:U]; the engine ended board=[Birds of Paradise#4(T), Breeding Pool#6(T), Forbidden Orchard#26(T)(new), Forbidden Orchard#25(T)(new), Hunted Phantasm#31(new)] hand=[Defense of the Heart] float=- life=17  
  board: board=[Forbidden Orchard#27, Birds of Paradise#4, Breeding Pool#6] hand=[Crop Rotation, Hunted Phantasm, Forbidden Orchard, Defense of the Heart] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Creature Giving/Creature Giving.cod' --profile 'decks/Creature Giving/Creature Giving.profile.json' --games 1 --seed 30076123 --game-index 76123`
- ... 2029 more signatures in flags.jsonl

## ORACLE-GAP (14486 flags, 1601 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **1_drop_burn** x298 -- T3 m1 `{land=Mountain + cast:Fireblast + cast:Lava Spike + gy-exile:Grim Lavamancer}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#33:R] ; pay2[Mountain#31:R]: Mountain#33 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Grim Lavamancer#16(T), Mountain#33(new)] hand=[Mountain] float=- life=20  
  board: board=[Mountain#31, Grim Lavamancer#16, Mountain#32] hand=[Mountain, Fireblast, Mountain, Lava Spike] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30000228 --game-index 228`
- **1_drop_burn** x290 -- T3 m1 `{land=Mountain + cast:Fireblast + cast:Chain Lightning + gy-exile:Grim Lavamancer}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#41:R] ; pay2[Mountain#44:R]: Mountain#41 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Grim Lavamancer#16(T), Mountain#41(new)] hand=[Rift Bolt] float=- life=20  
  board: board=[Mountain#44, Grim Lavamancer#16, Mountain#33] hand=[Fireblast, Rift Bolt, Mountain, Chain Lightning] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30000473 --game-index 473`
- **1_drop_burn** x277 -- T3 m1 `{land=Mountain + cast:Fireblast + cast:Boltwave + gy-exile:Grim Lavamancer}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#33:R] ; pay2[Mountain#31:R]: Mountain#33 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Grim Lavamancer#16(T), Mountain#33(new)] hand=[Skewer the Critics, Needle Drop] float=- life=20  
  board: board=[Mountain#31, Mountain#32, Grim Lavamancer#16] hand=[Skewer the Critics, Fireblast, Mountain, Boltwave, Needle Drop] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30000825 --game-index 825`
- **1_drop_burn** x234 -- T4 m1 `{land=Mountain + cast:Fireblast + cast:Lightning Bolt + gy-exile:Grim Lavamancer}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#33:R] ; pay2[Mountain#31:R]: Mountain#33 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Grim Lavamancer#16(T), Mountain#33(new)] hand=[Lightning Bolt, Mountain] float=- life=20  
  board: board=[Mountain#31, Grim Lavamancer#16, Mountain#32] hand=[Fireblast, Lightning Bolt, Mountain, Lightning Bolt, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30000826 --game-index 826`
- **1_drop_burn** x176 -- T4 m1 `{land=Mountain + cast:Fireblast + cast:Vexing Devil + gy-exile:Grim Lavamancer}`  
  24 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#35:R] ; pay2[Mountain#32:R]: Mountain#35 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Sunbaked Canyon#55, Grim Lavamancer#16(T), Mountain#35(new)] hand=[Lightning Bolt] float=- life=18  
  board: board=[Sunbaked Canyon#55, Mountain#32, Mountain#33, Grim Lavamancer#16] hand=[Fireblast, Mountain, Vexing Devil, Lightning Bolt] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30001665 --game-index 1665`
- **1_drop_burn** x143 -- T3 m1 `{land=Mountain + cast:Fireblast + suspend:Rift Bolt + gy-exile:Grim Lavamancer}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#41:R] ; pay2[Mountain#44:R]: Mountain#41 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Grim Lavamancer#16(T), Mountain#41(new)] hand=[Chain Lightning] float=- life=20  
  board: board=[Mountain#44, Grim Lavamancer#16, Mountain#33] hand=[Fireblast, Rift Bolt, Mountain, Chain Lightning] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30000473 --game-index 473`
- **1_drop_burn** x120 -- T3 m1 `{land=Mountain + cast:Chain Lightning + cast:Fireblast + gy-exile:Grim Lavamancer}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#42:R] ; pay2[Mountain#41:R]: Mountain#42 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Grim Lavamancer#17(T), Mountain#42(new)] hand=[] float=- life=20  
  board: board=[Mountain#41, Mountain#35, Grim Lavamancer#17] hand=[Chain Lightning, Mountain, Fireblast] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30002000 --game-index 2000`
- **1_drop_burn** x108 -- T3 m1 `{land=Mountain + suspend:Rift Bolt + cast:Fireblast + gy-exile:Grim Lavamancer}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#31:R] ; pay2[Mountain#37:R]: Mountain#31 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Grim Lavamancer#18(T), Mountain#31(new)] hand=[] float=- life=20  
  board: board=[Mountain#37, Grim Lavamancer#18, Mountain#36] hand=[Rift Bolt, Fireblast, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30000412 --game-index 412`
- **1_drop_burn** x101 -- T3 m1 `{land=Mountain + cast:Boltwave + cast:Fireblast + gy-exile:Grim Lavamancer}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#41:R] ; pay2[Mountain#35:R]: Mountain#41 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Grim Lavamancer#17(T), Mountain#41(new)] hand=[Lightning Bolt, Monastery Swiftspear] float=- life=20  
  board: board=[Mountain#35, Grim Lavamancer#17, Mountain#44] hand=[Lightning Bolt, Mountain, Boltwave, Fireblast, Monastery Swiftspear] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30000893 --game-index 893`
- **creature_burn** x99 -- T4 m1 `{land=Mountain + cast:Fireblast + suspend:Rift Bolt + cast:Goblin Fireslinger}`  
  9 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#34:R] ; pay2[Mountain#31:R]: Mountain#34 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Figure of Destiny#8, Mountain#33, Mountain#34(new), Goblin Fireslinger#14(new)] hand=[Mountain] float=- life=20  
  board: board=[Mountain#31, Figure of Destiny#8, Mountain#32, Mountain#33] hand=[Fireblast, Rift Bolt, Mountain, Mountain, Goblin Fireslinger] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Creature Burn/Creature Burn.cod' --profile 'decks/Creature Burn/Creature Burn.profile.json' --games 1 --seed 30001127 --game-index 1127`
- **beasts** x96 -- T5 m1 `{land=Contested Cliffs + act:Contested Cliffs@500000 + kind?:Kaheera, the Orphanguard + pump:Migloz, Maze Crusher(x2)}`  
  7 oracle outcomes, none covers the engine's -- nearest pay1[Stomping Ground#38:R] ; pay2[Cavern of Souls#3:C Nykthos, Shrine to Nyx#35:G] ; pay3[(float only)]: Forest#12 tapped by engine only; Migloz, Maze Crusher#32 pump differs; Cavern of Souls#3 tapped by oracle only; float engine - oracle {G}; engine ended board=[Stomping Ground#38(T), Wild Growth#49@38, Forest#12(T), Utopia Sprawl#46@38, Migloz, Maze Crusher#32, Nykthos, Shrine to Nyx#35(T), Garruk's Packleader#19, Cavern of Souls#3, Woodland Bellower#60, Garruk's Harbinger#18, Contested Cliffs#5(T)(new)] hand=[Forest, Krosan Warchief, Wooded Foothills, Kaheera, the Orphanguard] float=- life=17  
  board: board=[Stomping Ground#38, Wild Growth#49@38, Forest#12, Utopia Sprawl#46@38, Migloz, Maze Crusher#32, Nykthos, Shrine to Nyx#35, Garruk's Packleader#19, Cavern of Souls#3, Woodland Bellower#60, Garruk's Harbinger#18] hand=[Forest, Contested Cliffs, Krosan Warchief, Wooded Foothills] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Beasts/Beasts.cod' --profile 'decks/Beasts/Beasts.profile.json' --games 1 --seed 30002632 --game-index 2632`
- **1_drop_burn** x94 -- T3 m1 `{land=Mountain + cast:Lightning Bolt + cast:Fireblast + gy-exile:Grim Lavamancer}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#41:R] ; pay2[Mountain#35:R]: Mountain#41 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Grim Lavamancer#17(T), Mountain#41(new)] hand=[Boltwave, Monastery Swiftspear] float=- life=20  
  board: board=[Mountain#35, Grim Lavamancer#17, Mountain#44] hand=[Lightning Bolt, Mountain, Boltwave, Fireblast, Monastery Swiftspear] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30000893 --game-index 893`
- ... 1589 more signatures in flags.jsonl

## UNMODELED (7388 flags, 113 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **blood_sun** x1948 -- T7 m1 `{no land + cast:Ulamog, the Ceaseless Hunger}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Exploration#29, Golgari Rot Farm#34, Blooming Marsh#7, Copperline Gorge#18, City of Brass#13, Exploration#28, City of Brass#14, Blood Sun#3, Scorched Ruins#55, Lotus Field#43, Scorched Ruins#54, Overgrown Tomb#49, Scorched Ruins#57, Lotus Field#42, Fastbond#31, Lotus Field#44, Lotus Vale#47, Lotus Vale#45, Lotus Vale#46, Lotus Vale#48, Scorched Ruins#56] hand=[Ulamog, the Ceaseless Hunger,   
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000046 --game-index 46`
- **blood_sun** x679 -- T8 m1 `{land=Blooming Marsh + cast:Ulamog, the Ceaseless Hunger}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Exploration#27, Burgeoning#9, Golgari Rot Farm#32, Graven Cairns#36, Blood Sun#1, Blooming Marsh#6, Scorched Ruins#54, Copperline Gorge#15, Scorched Ruins#55, Scorched Ruins#56, Lotus Field#44, Blood Sun#2, Lotus Field#43, Fastbond#31, Lotus Vale#48, Lotus Vale#45, City of Brass#14, Lotus Vale#46, Lotus Vale#47, Scorched Ruins#57, Copperline Gorge#16, Emrakul, the Aeons Torn#26] hand=[Ulamo  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000116 --game-index 116`
- **blood_sun** x501 -- T5 m1 `{land=Rakdos Carnarium + cast:Ulamog, the Ceaseless Hunger}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Fastbond#31, Burgeoning#9, Golgari Rot Farm#32, Rakdos Carnarium#50, Blooming Marsh#5, Overgrown Tomb#49, Blooming Marsh#6, Exploration#27, Blood Sun#1, Scorched Ruins#54, Lotus Vale#45, Graven Cairns#37, Emrakul, the Aeons Torn#25] hand=[Ulamog, the Ceaseless Hunger, Rakdos Carnarium] float=- life=8  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000363 --game-index 363`
- **blood_sun** x434 -- T6 m1 `{land=Scorched Ruins + cast:Ulamog, the Ceaseless Hunger}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Golgari Rot Farm#32, Graven Cairns#36, Blood Sun#1, Lotus Field#44, Exploration#29, Blood Sun#3, Rakdos Carnarium#53, Blooming Marsh#7, Lotus Field#43, Scorched Ruins#57] hand=[Emrakul, the Aeons Torn, Emrakul, the Aeons Torn, Ulamog, the Ceaseless Hunger, Scorched Ruins, Fastbond] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000232 --game-index 232`
- **blood_sun** x326 -- T5 m1 `{no land + cast:Emrakul, the Aeons Torn}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Exploration#27, Golgari Rot Farm#32, Rakdos Carnarium#50, Copperline Gorge#15, City of Brass#11, Blood Sun#1, Scorched Ruins#54, Rakdos Carnarium#53, Scorched Ruins#57, Lotus Field#42, Fastbond#31, Lotus Field#44, Lotus Field#43, Scorched Ruins#56, Lotus Vale#47, Graven Cairns#36, City of Brass#14, Lotus Vale#46, Lotus Vale#48, Lotus Vale#45, Scorched Ruins#55, Emrakul, the Aeons Torn#26] h  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000012 --game-index 12`
- **blood_sun** x323 -- T8 m1 `{land=Copperline Gorge + cast:Ulamog, the Ceaseless Hunger}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Exploration#27, Burgeoning#9, Golgari Rot Farm#32, Graven Cairns#36, Blood Sun#1, Blooming Marsh#6, Scorched Ruins#54, Copperline Gorge#15, Scorched Ruins#55, Scorched Ruins#56, Lotus Field#44, Blood Sun#2, Lotus Field#43, Fastbond#31, Lotus Vale#48, Lotus Vale#45, City of Brass#14, Lotus Vale#46, Lotus Vale#47, Scorched Ruins#57] hand=[Ulamog, the Ceaseless Hunger, Diabolic Revelation, Dia  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000116 --game-index 116`
- **blood_sun** x259 -- T6 m1 `{no land + cast:Ulamog, the Ceaseless Hunger + cast:Exploration}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Golgari Rot Farm#32, Graven Cairns#36, Blood Sun#1, Lotus Field#44, Exploration#29, Blood Sun#3, Rakdos Carnarium#53, Blooming Marsh#7, Lotus Field#43, Scorched Ruins#57, Scorched Ruins#54, Emrakul, the Aeons Torn#26, Fastbond#31] hand=[Emrakul, the Aeons Torn, Ulamog, the Ceaseless Hunger, Exploration] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000232 --game-index 232`
- **blood_sun** x223 -- T6 m1 `{land=Lotus Field + cast:Ulamog, the Ceaseless Hunger}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Exploration#27, Rakdos Carnarium#51, Blooming Marsh#5, Graven Cairns#37, Rakdos Carnarium#50, Blooming Marsh#7, Blood Sun#3, Copperline Gorge#18, Lotus Vale#48] hand=[Ulamog, the Ceaseless Hunger, Lotus Field, Blood Sun] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000538 --game-index 538`
- **blood_sun** x214 -- T7 m1 `{land=Graven Cairns + cast:Ulamog, the Ceaseless Hunger}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Exploration#29, Golgari Rot Farm#34, Blooming Marsh#7, Copperline Gorge#18, City of Brass#13, Exploration#28, City of Brass#14, Blood Sun#3, Scorched Ruins#55, Lotus Field#43, Scorched Ruins#54, Overgrown Tomb#49, Scorched Ruins#57, Lotus Field#42, Fastbond#31, Lotus Field#44, Lotus Vale#47, Lotus Vale#45, Lotus Vale#46, Lotus Vale#48, Scorched Ruins#56] hand=[Ulamog, the Ceaseless Hunger,   
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000046 --game-index 46`
- **blood_sun** x128 -- T3 m1 `{land=Rakdos Carnarium}`  
  a mana choice the oracle does not drive: Blooming Marsh  
  board: board=[Exploration#27, Golgari Rot Farm#32, Blooming Marsh#5, Graven Cairns#36] hand=[Diabolic Revelation, Lotus Field, Golgari Rot Farm, Time Warp, Rakdos Carnarium] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000008 --game-index 8`
- **blood_sun** x121 -- T7 m2 `{no land + cast:Ulamog, the Ceaseless Hunger + cast:Emrakul, the Aeons Torn}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Fastbond#31, Golgari Rot Farm#32(T), Exploration#27, Rakdos Carnarium#50(T), Golgari Rot Farm#33, Blooming Marsh#5, Blooming Marsh#6, City of Brass#12, Graven Cairns#37, Blood Sun#4, Blooming Marsh#7, Lotus Vale#48(T), Scorched Ruins#54(T), Scorched Ruins#56(T), Scorched Ruins#57(T), Scorched Ruins#55(T), Lotus Field#43(new), Lotus Field#42(new), Lotus Field#44(new), Lotus Vale#47(new), Lot  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000568 --game-index 568`
- **blood_sun** x118 -- T4 m1 `{no land + cast:Diabolic Revelation(x1)}`  
  the engine reached a board the oracle cannot (payment enumeration capped)  
  board: board=[Exploration#27, Golgari Rot Farm#32, Graven Cairns#36, Blood Sun#1, Rakdos Carnarium#50, Lotus Field#42, Blooming Marsh#5, Scorched Ruins#54, Lotus Field#43, Blood Sun#4, Fastbond#31, Copperline Gorge#15, Lotus Vale#47] hand=[Emrakul, the Aeons Torn, Diabolic Revelation] float=- life=12  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000011 --game-index 11`
- ... 101 more signatures in flags.jsonl

## CAPPED (9416 flags, 510 signatures)

_the payment search hit MTG_MANA_ORACLE_NODES and then the _RETRY escalation (400000); the complete side is partial._

- **blood_sun** x1216 -- T4 m1 `{no land + cast:Diabolic Revelation(x2)}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Exploration#27, Rakdos Carnarium#50, Copperline Gorge#15, Exploration#28, Golgari Rot Farm#32, Graven Cairns#36, Blood Sun#1, Blood Sun#3, Lotus Vale#45, Blood Sun#4, Rakdos Carnarium#52, Graven Cairns#37, Lotus Field#44, Fastbond#31, Lotus Field#42, Scorched Ruins#56, Lotus Field#43, Lotus Vale#48, Lotus Vale#47, Scorched Ruins#55, Scorched Ruins#54, Emrakul, the Aeons Torn#25, Scorched Ru  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000007 --game-index 7`
- **blood_sun** x911 -- T8 m1 `{no land + cast:Diabolic Revelation(x4) + cast:Ulamog, the Ceaseless Hunger}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Graven Cairns#36, Burgeoning#9, Golgari Rot Farm#32, Copperline Gorge#15, Exploration#27, Exploration#28, Copperline Gorge#16, Copperline Gorge#17, Copperline Gorge#18, Blood Sun#1, Scorched Ruins#54, Blooming Marsh#5, Lotus Field#42, Lotus Vale#45, Exploration#30, Lotus Field#43, Fastbond#31, Lotus Field#44, Lotus Vale#48, Lotus Vale#46, Lotus Vale#47, Scorched Ruins#57, Scorched Ruins#55,  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000084 --game-index 84`
- **blood_sun** x395 -- T4 m1 `{no land + cast:Ulamog, the Ceaseless Hunger}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Exploration#27, Exploration#28, Rakdos Carnarium#51, Blooming Marsh#5, Blood Sun#1, Golgari Rot Farm#32, Rakdos Carnarium#50, City of Brass#11, City of Brass#13, Golgari Rot Farm#33, Lotus Vale#47, Lotus Field#42, Scorched Ruins#54, Scorched Ruins#56, Scorched Ruins#55, Emrakul, the Aeons Torn#26, Fastbond#31, Lotus Field#43, Lotus Field#44, Lotus Vale#46, Lotus Vale#48, Lotus Vale#45, Scor  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000107 --game-index 107`
- **blood_sun** x369 -- T3 m1 `{no land + cast:Diabolic Revelation(x4) + cast:Diabolic Revelation(x4)}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Exploration#28, Lotus Vale#48, Blood Sun#3, Lotus Field#43, Scorched Ruins#55, Lotus Field#42, Golgari Rot Farm#34, Lotus Field#44, Fastbond#31, Lotus Vale#46, Lotus Vale#47, Lotus Vale#45, Scorched Ruins#57, Scorched Ruins#56, Scorched Ruins#54, Ulamog, the Ceaseless Hunger#60, Copperline Gorge#17, Emrakul, the Aeons Torn#26] hand=[Diabolic Revelation, Diabolic Revelation] float=- life=14  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000032 --game-index 32`
- **blood_sun** x288 -- T5 m1 `{no land + cast:Blood Sun}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Exploration#27, Rakdos Carnarium#50, Copperline Gorge#15, Blood Sun#1, Lotus Field#42, Rakdos Carnarium#51, Lotus Vale#45, Scorched Ruins#54, Exploration#28, Golgari Rot Farm#32, Fastbond#31, Lotus Vale#46, Lotus Field#43, Lotus Field#44, Lotus Vale#47, Lotus Vale#48, Scorched Ruins#55, Scorched Ruins#56, Scorched Ruins#57] hand=[Emrakul, the Aeons Torn, Emrakul, the Aeons Torn, Diabolic Re  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000005 --game-index 5`
- **blood_sun** x269 -- T6 m1 `{no land + cast:Ulamog, the Ceaseless Hunger + cast:Emrakul, the Aeons Torn}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Exploration#27, Golgari Rot Farm#32, City of Brass#11, Blood Sun#1, Lotus Vale#45, Scorched Ruins#54, Lotus Field#43, Lotus Field#44, Lotus Field#42, Fastbond#31, Lotus Vale#47, Lotus Vale#46, Lotus Vale#48, Scorched Ruins#55, Scorched Ruins#56, Scorched Ruins#57] hand=[Ulamog, the Ceaseless Hunger, Diabolic Revelation, Emrakul, the Aeons Torn, Emrakul, the Aeons Torn, Diabolic Revelation]   
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000040 --game-index 40`
- **blood_sun** x239 -- T4 m1 `{no land + cast:Diabolic Revelation(x5)}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Exploration#27, Golgari Rot Farm#32, Graven Cairns#36, Blood Sun#1, Rakdos Carnarium#50, Lotus Field#42, Blooming Marsh#5, Scorched Ruins#54, Lotus Field#43, Blood Sun#4, Fastbond#31, Copperline Gorge#15, Lotus Vale#47] hand=[Emrakul, the Aeons Torn, Diabolic Revelation] float=- life=12  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000011 --game-index 11`
- **blood_sun** x211 -- T4 m1 `{no land + cast:Diabolic Revelation(x2) + cast:Grim Tutor}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Exploration#27, Rakdos Carnarium#50, Copperline Gorge#15, Exploration#28, Golgari Rot Farm#32, Graven Cairns#36, Blood Sun#1, Blood Sun#3, Lotus Vale#45, Blood Sun#4, Rakdos Carnarium#52, Graven Cairns#37, Lotus Field#44, Fastbond#31, Lotus Field#42, Scorched Ruins#56, Lotus Field#43, Lotus Vale#48, Lotus Vale#47, Scorched Ruins#55, Scorched Ruins#54, Emrakul, the Aeons Torn#25, Scorched Ru  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000007 --game-index 7`
- **blood_sun** x184 -- T5 m1 `{no land + cast:Emrakul, the Aeons Torn + cast:Blood Sun}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Exploration#27, Rakdos Carnarium#50, Copperline Gorge#15, Blood Sun#1, Lotus Field#42, Rakdos Carnarium#51, Lotus Vale#45, Scorched Ruins#54, Exploration#28, Golgari Rot Farm#32, Fastbond#31, Lotus Vale#46, Lotus Field#43, Lotus Field#44, Lotus Vale#47, Lotus Vale#48, Scorched Ruins#55, Scorched Ruins#56, Scorched Ruins#57] hand=[Emrakul, the Aeons Torn, Emrakul, the Aeons Torn, Diabolic Re  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000005 --game-index 5`
- **blood_sun** x176 -- T6 m1 `{land=City of Brass + cast:Ulamog, the Ceaseless Hunger}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Exploration#27, Exploration#28, City of Brass#11, Golgari Rot Farm#32, Overgrown Tomb#49, Blood Sun#1, Blooming Marsh#6, Lotus Vale#45, Lotus Field#42, Scorched Ruins#56, Lotus Vale#48, Exploration#29, Fastbond#31, Lotus Field#44, Lotus Field#43, Lotus Vale#47, Lotus Vale#46, Scorched Ruins#54, Scorched Ruins#57, Scorched Ruins#55, Copperline Gorge#15, Emrakul, the Aeons Torn#25] hand=[Ulam  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000625 --game-index 625`
- **blood_sun** x175 -- T8 m1 `{land=Blooming Marsh + cast:Ulamog, the Ceaseless Hunger + cast:Emrakul, the Aeons Torn}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Exploration#27, Burgeoning#9, Golgari Rot Farm#32, Graven Cairns#36, Blood Sun#1, Blooming Marsh#6, Scorched Ruins#54, Copperline Gorge#15, Scorched Ruins#55, Scorched Ruins#56, Lotus Field#44, Blood Sun#2, Lotus Field#43, Fastbond#31, Lotus Vale#48, Lotus Vale#45, City of Brass#14, Lotus Vale#46, Lotus Vale#47, Scorched Ruins#57, Copperline Gorge#16, Emrakul, the Aeons Torn#26] hand=[Ulamo  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000116 --game-index 116`
- **blood_sun** x169 -- T3 m1 `{no land + cast:Emrakul, the Aeons Torn + cast:Emrakul, the Aeons Torn}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Exploration#28, Lotus Vale#48, Blood Sun#3, Lotus Field#43, Scorched Ruins#55, Lotus Field#42, Golgari Rot Farm#34, Lotus Field#44, Fastbond#31, Lotus Vale#46, Lotus Vale#47, Lotus Vale#45, Scorched Ruins#57, Scorched Ruins#56, Scorched Ruins#54, Ulamog, the Ceaseless Hunger#60] hand=[Emrakul, the Aeons Torn, Diabolic Revelation, Emrakul, the Aeons Torn, Copperline Gorge] float=- life=14  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000032 --game-index 32`
- ... 498 more signatures in flags.jsonl

## CAPPED-DECISION (15174 flags, 246 signatures)

_the decision spent its priced work budget (MTG_MANA_ORACLE_DECISION_S, default 1300 priced s) before this plan's audit finished, so it has no verdict._

- **blood_sun** x4651 -- T4 m1 `{land=Scorched Ruins + cast:Ulamog, the Ceaseless Hunger + cast:Diabolic Revelation(x1)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Exploration#27, Rakdos Carnarium#50, Rakdos Carnarium#51, Copperline Gorge#15, Blood Sun#1, Lotus Field#42, Golgari Rot Farm#33, Lotus Field#44] hand=[Ulamog, the Ceaseless Hunger, Blooming Marsh, City of Brass, Diabolic Revelation, Scorched Ruins, Burgeoning] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000041 --game-index 41`
- **blood_sun** x1172 -- T7 m1 `{land=Golgari Rot Farm + cast:Ulamog, the Ceaseless Hunger + cast:Diabolic Revelation(x8)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Exploration#27, Copperline Gorge#16, Burgeoning#9, Rakdos Carnarium#50, Copperline Gorge#15, Overgrown Tomb#49, Exploration#29, Blood Sun#1, City of Brass#14, Lotus Vale#47, Scorched Ruins#57, Scorched Ruins#55, Scorched Ruins#54] hand=[Ulamog, the Ceaseless Hunger, Diabolic Revelation, Scorched Ruins, Golgari Rot Farm] float=- life=15  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000312 --game-index 312`
- **blood_sun** x1146 -- T4 m1 `{land=Scorched Ruins + cast:Diabolic Revelation(x3) + cast:Ulamog, the Ceaseless Hunger}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Copperline Gorge#15, Exploration#27, Copperline Gorge#16, City of Brass#11, Blood Sun#1, Golgari Rot Farm#32, Exploration#28, Rakdos Carnarium#50, Rakdos Carnarium#51, Graven Cairns#36, Scorched Ruins#54] hand=[Diabolic Revelation, Scorched Ruins, Scorched Ruins, Ulamog, the Ceaseless Hunger] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000375 --game-index 375`
- **blood_sun** x873 -- T7 m1 `{land=Copperline Gorge + cast:Ulamog, the Ceaseless Hunger + cast:Diabolic Revelation(x3)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Copperline Gorge#15, Fastbond#31, Blooming Marsh#7, Burgeoning#10, City of Brass#13, Blooming Marsh#8, Burgeoning#9, Blood Sun#1, Lotus Field#43, Rakdos Carnarium#51, Scorched Ruins#56, Scorched Ruins#57] hand=[Ulamog, the Ceaseless Hunger, Diabolic Revelation, Scorched Ruins, Copperline Gorge] float=- life=14  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000055 --game-index 55`
- **blood_sun** x312 -- T6 m1 `{land=Lotus Field + cast:Ulamog, the Ceaseless Hunger + cast:Diabolic Revelation(x1)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Exploration#27, Golgari Rot Farm#32, City of Brass#11, Blood Sun#1, Lotus Vale#45, Scorched Ruins#54, Lotus Field#43] hand=[Ulamog, the Ceaseless Hunger, Diabolic Revelation, Scorched Ruins, Scorched Ruins, Lotus Field, Scorched Ruins, Lotus Field] float=- life=15  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000040 --game-index 40`
- **blood_sun** x243 -- T7 m1 `{no land + cast:Ulamog, the Ceaseless Hunger}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Fastbond#31, Golgari Rot Farm#32, Golgari Rot Farm#34, Blooming Marsh#7, City of Brass#13, Blooming Marsh#5, Blood Sun#2, Scorched Ruins#54, Lotus Field#43, Lotus Field#44, Scorched Ruins#56, Scorched Ruins#57] hand=[Emrakul, the Aeons Torn, Ulamog, the Ceaseless Hunger, Lotus Vale] float=- life=3  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000017 --game-index 17`
- **blood_sun** x229 -- T4 m1 `{land=Scorched Ruins + cast:Ulamog, the Ceaseless Hunger}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Exploration#27, Rakdos Carnarium#50, Rakdos Carnarium#51, Copperline Gorge#15, Blood Sun#1, Lotus Field#42, Golgari Rot Farm#33, Lotus Field#44] hand=[Ulamog, the Ceaseless Hunger, Blooming Marsh, City of Brass, Diabolic Revelation, Scorched Ruins, Burgeoning] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000041 --game-index 41`
- **blood_sun** x194 -- T7 m1 `{land=Scorched Ruins + cast:Diabolic Revelation(x5)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Copperline Gorge#15, Fastbond#31, Blooming Marsh#7, Burgeoning#10, City of Brass#13, Blooming Marsh#8, Burgeoning#9, Blood Sun#1, Lotus Field#43, Rakdos Carnarium#51, Scorched Ruins#56, Scorched Ruins#57] hand=[Ulamog, the Ceaseless Hunger, Diabolic Revelation, Scorched Ruins, Copperline Gorge] float=- life=14  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000055 --game-index 55`
- **blood_sun** x193 -- T7 m1 `{no land + cast:Ulamog, the Ceaseless Hunger + cast:Diabolic Revelation(x2)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Copperline Gorge#15, Fastbond#31, Blooming Marsh#7, Burgeoning#10, City of Brass#13, Blooming Marsh#8, Burgeoning#9, Blood Sun#1, Lotus Field#43, Rakdos Carnarium#51, Scorched Ruins#56, Scorched Ruins#57] hand=[Ulamog, the Ceaseless Hunger, Diabolic Revelation, Scorched Ruins, Copperline Gorge] float=- life=14  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Blood Sun/Blood Sun.cod' --profile 'decks/Blood Sun/Blood Sun.profile.json' --games 1 --seed 30000055 --game-index 55`
- **hydras** x138 -- T7 m1 `{land=Forest + cast:Gauntlet of Power(x5) + cast:Nissa, Who Shakes the World + cast:Voracious Hydra(x4) + cast:Lightning Greaves + cast:Primordial Hydra(x1) + equip:Lightning Greaves}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#9, Forest#17, Forest#6, Forest#5, Forest#10, Forest#8, Voracious Hydra#57, Forest#13, Forest#3, Forest#23] hand=[Gauntlet of Power, Nissa, Who Shakes the World, Voracious Hydra, Vernal Bloom, Forest, Vernal Bloom, Lightning Greaves, Primordial Hydra] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30046293 --game-index 46293`
- **hydras** x138 -- T7 m1 `{land=Forest + cast:Gauntlet of Power(x5) + cast:Nissa, Who Shakes the World + cast:Voracious Hydra(x4) + cast:Lightning Greaves + cast:Primordial Hydra(x1)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#9, Forest#17, Forest#6, Forest#5, Forest#10, Forest#8, Voracious Hydra#57, Forest#13, Forest#3, Forest#23] hand=[Gauntlet of Power, Nissa, Who Shakes the World, Voracious Hydra, Vernal Bloom, Forest, Vernal Bloom, Lightning Greaves, Primordial Hydra] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30046293 --game-index 46293`
- **hydras** x138 -- T7 m1 `{land=Forest + cast:Gauntlet of Power(x5) + cast:Voracious Hydra(x4) + cast:Lightning Greaves + cast:Primordial Hydra(x1) + equip:Lightning Greaves}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#9, Forest#17, Forest#6, Forest#5, Forest#10, Forest#8, Voracious Hydra#57, Forest#13, Forest#3, Forest#23] hand=[Gauntlet of Power, Nissa, Who Shakes the World, Voracious Hydra, Vernal Bloom, Forest, Vernal Bloom, Lightning Greaves, Primordial Hydra] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Hydras/Hydras.cod' --profile 'decks/Hydras/Hydras.profile.json' --games 1 --seed 30046293 --game-index 46293`
- ... 234 more signatures in flags.jsonl

