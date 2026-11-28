# Mana-clean gate `claude-queue-20261128a`

Mana-clean gate run: 5 decks, games (seed 30000000 + game index) treasure_hunt 282000-381999, worldfire 35000-60999, shadowborn_apostle 172000-192999, 1_drop_burn 100000-199999, creature_burn 100000-192999; src `a45c79657903`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-LIFE | ORACLE-GAP | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| treasure_hunt | 100000 | issues | 81 | 10 | 71 | 0 | 6909 | 0 | 55105 | 1734 | 623162 | 328725 | 38 |
| worldfire | 26000 | issues | 1151 | 0 | 787 | 363 | 0 | 1 | 733470 | 18506 | 28573 | 47459 | 24 |
| shadowborn_apostle | 21000 | issues | 50 | 0 | 25 | 8 | 0 | 17 | 75963 | 529 | 3598 | 41235 | 233 |
| 1_drop_burn | 100000 | issues | 396 | 0 | 227 | 169 | 0 | 0 | 1153093 | 0 | 2606 | 2 | 0 |
| creature_burn | 93000 | clean | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 14276 | 0 |

**MISS = 1660, ORACLE-GAP = 18.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE pay-table memo: lookups=479140503 hits=291879600 kept=79146022 budget_full=295015 checked=0 mismatch=0 ===
=== MANA ORACLE: decisions=1711311 plans=18645336 skipped=0 applies=603480960 frontiers=539014933 pruned=59869524 options=320039813 engine_drops=1340688 gate_rejects=33949257(checked 33787700, skipped 161557) MISS=1457 ORACLE-GAP=17 UNMODELED=0 CAPPED=0 ms=16598946 ===
=== MANA ORACLE classes: ORACLE-GAP=17 ENUM-GAP=1 APPLY-DROP=257 APPLY-DROP-ORDER=177 PHANTOM=1237731 DOMINATED-LIFE=1022 DOMINATED-LAND=106662 CONTINUATION-DIFFERS=873 DROP-COVERED=101492 HIDDEN-INFO=243 ===
=== MANA ORACLE enum-gap rejections: noop=16 hidden=139 host_blind=0 cont_land=0 provider=96 ===
=== MANA ORACLE hit-only cuts: 280768311 unpayable-payment subtrees not built ===
=== MANA ORACLE order fold: 6441740 order variants are an earlier order's apply (trailing activations) ===
=== MANA ORACLE chained feeds: 461129 fed-tap reorders tried, 105321 paid ===
=== MANA ORACLE order fold: 112 plans past the order bound searched by fold class ===
=== MANA ORACLE hit-only cuts: 7270689 frontiers below a dropped action not built ===
=== MANA ORACLE goal fold: 8637083 plans, 1375 frontiers not expanded, 0 plans realised again in full, 0 checked ===
=== MANA ORACLE board memory: 0 searches capped by MTG_MANA_ORACLE_BOARD_MB, 59872455 pruned frontier boards and 1555902 hit-only outcome boards given back early ===
=== MANA ORACLE shared boards: 36186832 outcome boards held for 55637990 outcomes (19451158 shared); 0 share checks ===
=== MANA ORACLE capped-plan escalation: 711 retried, 711 finished ===
=== MANA ORACLE unmodeled: 5125069 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE rollback census: lost=435 depth0=0 depth1=6 depth2=93 depth3+=44 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=114 unmodeled=0 reorder=177 off-menu=1 need_in_plan=236 need_later=21 pay_rollback_rescued=0/257 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP |
|---|---|---|---|---|---|---|
| treasure_hunt | 709788 | 14946102 | 10 | 71 | 0 | 0 |
| worldfire | 296763 | 2669903 | 0 | 787 | 363 | 1 |
| shadowborn_apostle | 180052 | 1615273 | 0 | 25 | 8 | 17 |
| 1_drop_burn | 633808 | 9793178 | 0 | 227 | 169 | 0 |
| creature_burn | 788719 | 4949321 | 0 | 0 | 0 | 0 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth1 | depth2 | depth3+ | no-failed-payment | reorder | off-menu | need-later |
|---|---|---|---|---|---|---|---|
| treasure_hunt | 44 | 27 | 0 | 0 | 0 | 10 | 44 |
| worldfire | 435 | 234 | 113 | 5 | 363 | 0 | 351 |
| shadowborn_apostle | 5 | 0 | 20 | 0 | 8 | 0 | 20 |
| 1_drop_burn | 0 | 89 | 24 | 114 | 169 | 0 | 0 |

## ENUM-GAP (10 flags, 6 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **treasure_hunt** x4 -- T5 m1 `{no land + cast:Treasure Hunt + cast:Land's Edge}`  
  a mana gate kept this plan off the menu, but casting Land's Edge > Treasure Hunt pays every action with pay1[Saprazzan Skerry#43:U Reliquary Tower#34:C Cascade Bluffs#1:RR] ; pay2[Reliquary Tower#31:C]  
  board: board=[Saprazzan Skerry#43, Reliquary Tower#34, Reliquary Tower#31, Cascade Bluffs#1] hand=[Island, Forgotten Cave, Treasure Hunt, Lonely Sandbar, Island, Land's Edge] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30294900 --game-index 294900`
- **treasure_hunt** x2 -- T5 m1 `{land=Cascade Bluffs + cast:Land's Edge + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Reliquary Tower#33:C Ferrous Lake#6:UR Cascade Bluffs#1:UR Cascade Bluffs#2:UU] ; pay2[Reliquary Tower#31:C]  
  board: board=[Reliquary Tower#33, Ferrous Lake#6, Reliquary Tower#31, Cascade Bluffs#2] hand=[Lonely Sandbar, Forgotten Cave, Throes of Chaos, Steam Vents, Lonely Sandbar, Land's Edge, Saprazzan Skerry, Forgotten Cave, Island, Fiery Islet, Cascade Bluffs, Temple of Epiphany, Lonely Sandbar, Ferrous Lake, Sandstone Needle, Ferrous Lake, Thundering Falls, Thundering Falls, Frostboil Snarl, Saprazzan Skerry  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30306953 --game-index 306953`
- **treasure_hunt** x1 -- T5 m1 `{land=Island + cast:Throes of Chaos + cast:Land's Edge}`  
  a mana gate kept this plan off the menu, but casting Land's Edge > Throes of Chaos pays every action with pay1[Saprazzan Skerry#42:U Cascade Bluffs#2:RR] ; pay2[Fiery Islet#12:R Saprazzan Skerry#43:U Island#24:U]  
  board: board=[Fiery Islet#12, Saprazzan Skerry#43, Saprazzan Skerry#42, Cascade Bluffs#2, Land's Edge#25] hand=[Lonely Sandbar, Throes of Chaos, Land's Edge, Island, Forgotten Cave] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30328114 --game-index 328114`
- **treasure_hunt** x1 -- T5 m1 `{land=Cascade Bluffs + cast:Treasure Hunt + cast:Land's Edge}`  
  a mana gate kept this plan off the menu, but casting Land's Edge > Treasure Hunt pays every action with pay1[Reliquary Tower#31:C Ferrous Lake#7:UR Cascade Bluffs#3:UR Cascade Bluffs#4:UU Cascade Bluffs#2:UU] ; pay2[(float only)]  
  board: board=[Cascade Bluffs#4, Ferrous Lake#7, Reliquary Tower#31, Cascade Bluffs#2] hand=[Throes of Chaos, Lonely Sandbar, Frostboil Snarl, Thundering Falls, Sandstone Needle, Steam Vents, Temple of Epiphany, Island, Ferrous Lake, Saprazzan Skerry, Cascade Bluffs, Frostboil Snarl, Sandstone Needle, Saprazzan Skerry, Fiery Islet, Saprazzan Skerry, Thundering Falls, Forgotten Cave, Remote Isle, Sandstone  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30329678 --game-index 329678`
- **treasure_hunt** x1 -- T5 m1 `{land=Reliquary Tower + cast:Land's Edge + cast:Land's Edge}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Saprazzan Skerry#41:U Reliquary Tower#31:C Cascade Bluffs#3:RR] ; pay2[Reliquary Tower#33:C Cascade Bluffs#1:RR]  
  board: board=[Reliquary Tower#31, Saprazzan Skerry#41, Cascade Bluffs#3, Cascade Bluffs#1] hand=[Island, Fiery Islet, Land's Edge, Temple of Epiphany, Frostboil Snarl, Ferrous Lake, Ferrous Lake, Land's Edge, Reliquary Tower, Frostboil Snarl, Fiery Islet, Saprazzan Skerry, Reliquary Tower, Steam Vents, Lonely Sandbar, Forgotten Cave, Thundering Falls, Lonely Sandbar, Reliquary Tower, Fiery Islet, Sandsto  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30348427 --game-index 348427`
- **treasure_hunt** x1 -- T7 m1 `{land=Fiery Islet + cast:Throes of Chaos + cast:Land's Edge}`  
  a mana gate kept this plan off the menu, but casting Land's Edge > Throes of Chaos pays every action with pay1[Saprazzan Skerry#40:U Cascade Bluffs#4:RR] ; pay2[Saprazzan Skerry#42:U Saprazzan Skerry#41:U Fiery Islet#12:R]  
  board: board=[Saprazzan Skerry#40, Reliquary Tower#31, Reliquary Tower#32, Saprazzan Skerry#42, Saprazzan Skerry#41, Cascade Bluffs#4] hand=[Throes of Chaos, Forgotten Cave, Temple of Epiphany, Temple of Epiphany, Remote Isle, Thundering Falls, Island, Ferrous Lake, Ferrous Lake, Steam Vents, Saprazzan Skerry, Frostboil Snarl, Reliquary Tower, Island, Steam Vents, Island, Fiery Islet, Thundering Falls, C  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30354428 --game-index 354428`

## APPLY-DROP (1110 flags, 602 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **1_drop_burn** x20 -- T3 m1 `{no land + cast:Lightning Bolt}`  
  engine dropped [act Grim Lavamancer unpaid(pip stranded)]; every action pays with pay1[Sunbaked Canyon#56:W Mountain#43:R] ; pay2[Mountain#33:R], reaching a board the menu's other order of these actions does not (Sunbaked Canyon#56 tapped by oracle only; Grim Lavamancer#16 tapped by oracle only; Mountain#33 only in oracle; life engine 18 oracle 17; hand size differs; graveyard size differs; opponent life engine 13 oracle 11; float engine - oracle {W}) || ROLLBACK no-failed-payment: no engine payment failed -- the action dropped for another reason; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sunbaked Canyon#56, Grim Lavamancer#16, Mountain#43, Grim Lavamancer#18] hand=[Lightning Bolt, Rift Bolt, Mountain] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30111228 --game-index 111228`
- **1_drop_burn** x16 -- T3 m1 `{land=Mountain + cast:Lightning Bolt}`  
  engine dropped [act Grim Lavamancer unpaid(pip stranded)]; every action pays with pay1[Sunbaked Canyon#56:W Mountain#37:R] ; pay2[Mountain#31:R], reaching a board the menu's other order of these actions does not (Grim Lavamancer#17 tapped by oracle only; Sunbaked Canyon#56 only in oracle; life engine 18 oracle 17; hand size differs; graveyard size differs; library size differs; opponent life engine 13 oracle 11; float engine - oracle {W}) || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Sunbaked Canyon#56:W Mountain#37:R] ; pay2[Mountain#31:R]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sunbaked Canyon#56, Mountain#31, Grim Lavamancer#17, Grim Lavamancer#16] hand=[Lightning Bolt, Mountain] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30131264 --game-index 131264`
- **worldfire** x15 -- T4 m1 `{no land + cast:Seething Song + cast:Seething Song + cast:Ponder + cast:Keldon Halberdier}`  
  engine dropped [cast Ponder]; every action pays with pay1[Mercadian Bazaar#22:R Saprazzan Skerry#43:U] ; pay2[(float only)] ; pay3[Steam Vents#55:U] ; pay4[(float only)] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Mercadian Bazaar#22:R Saprazzan Skerry#43:U] ; pay2[(float only)] ; pay3[Steam Vents#55:U] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Steam Vents#55, Mercadian Bazaar#22, Saprazzan Skerry#43] hand=[Seething Song, Personal Tutor, Irencrag Feat, Keldon Halberdier, Seething Song, Ponder] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30035707 --game-index 35707`
- **worldfire** x13 -- T4 m1 `{no land + cast:Seething Song + cast:Seething Song + cast:Irencrag Feat + cast:Ponder}`  
  engine dropped [cast Ponder]; every action pays with pay1[Mercadian Bazaar#20:R Saprazzan Skerry#43:U] ; pay2[Steam Vents#54:U] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mercadian Bazaar#20:R Saprazzan Skerry#43:U] ; pay2[Steam Vents#54:U] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Steam Vents#54, Mercadian Bazaar#20, Saprazzan Skerry#43] hand=[Rift Bolt, Seething Song, Ponder, Seething Song, Irencrag Feat, Worldfire] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30035046 --game-index 35046`
- **1_drop_burn** x12 -- T4 m1 `{no land + cast:Lightning Bolt}`  
  engine dropped [act Grim Lavamancer unpaid(pip stranded)]; every action pays with pay1[Sunbaked Canyon#56:W Mountain#37:R] ; pay2[Mountain#43:R], reaching a board the menu's other order of these actions does not (Grim Lavamancer#18 tapped by oracle only; Sunbaked Canyon#56 only in oracle; life engine 17 oracle 16; hand size differs; graveyard size differs; library size differs; opponent life engine 5 oracle 3; float engine - oracle {W}) || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Sunbaked Canyon#56:W Mountain#37:R] ; pay2[Mountain#43:R]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sunbaked Canyon#56, Grim Lavamancer#18, Mountain#37, Mountain#43, Grim Lavamancer#17] hand=[Lightning Bolt] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30100472 --game-index 100472`
- **1_drop_burn** x12 -- T4 m1 `{no land + cast:Chain Lightning}`  
  engine dropped [act Grim Lavamancer unpaid(pip stranded)]; every action pays with pay1[Sunbaked Canyon#56:W Mountain#43:R] ; pay2[Mountain#33:R], reaching a board the menu's other order of these actions does not (Grim Lavamancer#16 tapped by oracle only; Sunbaked Canyon#56 only in oracle; life engine 18 oracle 17; hand size differs; graveyard size differs; library size differs; opponent life engine 5 oracle 3; float engine - oracle {W}) || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Sunbaked Canyon#56:W Mountain#43:R] ; pay2[Mountain#33:R]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sunbaked Canyon#56, Grim Lavamancer#16, Mountain#43, Grim Lavamancer#18, Mountain#33] hand=[Chain Lightning] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30111228 --game-index 111228`
- **1_drop_burn** x12 -- T4 m1 `{no land + cast:Boltwave}`  
  engine dropped [act Grim Lavamancer unpaid(pip stranded)]; every action pays with pay1[Sunbaked Canyon#56:W Mountain#41:R] ; pay2[Mountain#38:R], reaching a board the menu's other order of these actions does not (Sunbaked Canyon#56 tapped by oracle only; Grim Lavamancer#16 tapped by oracle only; Mountain#38 tapped by oracle only; life engine 17 oracle 16; graveyard size differs; opponent life engine 8 oracle 6; float engine - oracle {W}) || ROLLBACK no-failed-payment: no engine payment failed -- the action dropped for another reason; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sunbaked Canyon#56, Grim Lavamancer#16, Mountain#41, Grim Lavamancer#17, Mountain#38] hand=[Rift Bolt, Boltwave] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30122154 --game-index 122154`
- **treasure_hunt** x10 -- T4 m1 `{no land + cast:Treasure Hunt}`  
  engine dropped [cast Land's Edge]; every action pays with pay1[Reliquary Tower#31:C Ferrous Lake#5:UR] ; pay2[Saprazzan Skerry#40:U Cascade Bluffs#1:RR], reaching a board the menu's other order of these actions does not (Reliquary Tower#31 tapped by oracle only; Ferrous Lake#5 tapped by oracle only; Cascade Bluffs#1 only in oracle; Land's Edge#25 only in oracle; hand size differs; graveyard size differs; opponent life engine 20 oracle -24; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Reliquary Tower#31:C Ferrous Lake#5:UR] ; pay2[Saprazzan Skerr  
  board: board=[Saprazzan Skerry#40, Reliquary Tower#31, Ferrous Lake#5] hand=[Treasure Hunt, Forgotten Cave, Forgotten Cave, Thundering Falls, Island, Temple of Epiphany, Temple of Epiphany, Temple of Epiphany, Island, Land's Edge, Thundering Falls, Island] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Treasure Hunt/Treasure Hunt.txt' --profile 'decks/Treasure Hunt/Treasure Hunt.profile.json' --games 1 --seed 30283625 --game-index 283625`
- **1_drop_burn** x10 -- T3 m1 `{no land + cast:Chain Lightning}`  
  engine dropped [act Grim Lavamancer unpaid(pip stranded)]; every action pays with pay1[Sunbaked Canyon#56:W Mountain#36:R] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Sunbaked Canyon#56 tapped by oracle only; Grim Lavamancer#16 tapped by oracle only; Mountain#36 only in engine; life engine 18 oracle 17; hand size differs; graveyard size differs; opponent life engine 12 oracle 6; spells cast differs; float engine - oracle {W}) || ROLLBACK no-failed-payment: no engine payment failed -- the action dropped for another reason; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sunbaked Canyon#56, Grim Lavamancer#16, Mountain#36, Grim Lavamancer#18] hand=[Chain Lightning, Mountain, Fireblast] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30100902 --game-index 100902`
- **1_drop_burn** x8 -- T3 m1 `{land=Mountain + cast:Chain Lightning}`  
  engine dropped [act Grim Lavamancer unpaid(pip stranded)]; every action pays with pay1[Sunbaked Canyon#56:W Mountain#39:R] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Grim Lavamancer#16 tapped by oracle only; Mountain#36 only in engine; Mountain#39 only in engine; Sunbaked Canyon#56 only in oracle; life engine 18 oracle 17; hand size differs; graveyard cards differ (engine vs oracle: +Chain Lightning +Lava Spike +Sunbaked Canyon -Fireblast -Mountain); library size differs; opponent life engine 12 oracle 6; spells cast differs; float engine - oracle {W}) || ROLLBACK no-failed-payment: no engine payment failed -- the action dropped for another reaso  
  board: board=[Sunbaked Canyon#56, Grim Lavamancer#16, Mountain#36, Grim Lavamancer#18] hand=[Chain Lightning, Mountain, Fireblast] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30100902 --game-index 100902`
- **1_drop_burn** x8 -- T3 m1 `{land=Mountain + cast:Lightning Bolt + cast:Fireblast}`  
  engine dropped [act Grim Lavamancer unpaid(pip stranded)]; every action pays with pay1[Sunbaked Canyon#56:W Mountain#31:R] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Sunbaked Canyon#56 tapped by oracle only; Grim Lavamancer#18 tapped by oracle only; life engine 18 oracle 17; graveyard size differs; opponent life engine 9 oracle 7; float engine {R} oracle {W}) || ROLLBACK no-failed-payment: no engine payment failed -- the action dropped for another reason; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sunbaked Canyon#56, Grim Lavamancer#18, Mountain#37, Grim Lavamancer#16] hand=[Lightning Bolt, Mountain, Fireblast] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30122898 --game-index 122898`
- **1_drop_burn** x8 -- T3 m1 `{land=Mountain + cast:Fireblast + cast:Chain Lightning}`  
  engine dropped [act Grim Lavamancer unpaid(pip stranded)]; every action pays with pay1[Sunbaked Canyon#55:W Mountain#32:R] ; pay2[(float only)], reaching a board the menu's other order of these actions does not (Sunbaked Canyon#55 tapped by oracle only; Grim Lavamancer#16 tapped by oracle only; life engine 18 oracle 17; graveyard size differs; opponent life engine 10 oracle 8; float engine {R} oracle {W}) || ROLLBACK no-failed-payment: no engine payment failed -- the action dropped for another reason; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sunbaked Canyon#55, Mountain#31, Grim Lavamancer#16, Grim Lavamancer#17] hand=[Mountain, Fireblast, Chain Lightning, Mountain] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30126576 --game-index 126576`
- ... 590 more signatures in flags.jsonl

## APPLY-DROP-ORDER (540 flags, 246 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **1_drop_burn** x59 -- T4 m1 `{land=Mountain + cast:Needle Drop + cast:Skewer the Critics}`  
  engine dropped [cast Needle Drop]; the order Skewer the Critics > Needle Drop pays every action with pay1[Mountain#31:R Mountain#32:R Mountain#34:R] ; pay2[Mountain#33:R]  
  board: board=[Mountain#31, Mountain#32, Goblin Guide#12, Grim Lavamancer#16, Mountain#33] hand=[Needle Drop, Mountain, Skewer the Critics] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30107690 --game-index 107690`
- **1_drop_burn** x18 -- T4 m1 `{land=Mountain + cast:Needle Drop + cast:Skewer the Critics}`  
  engine dropped [cast Needle Drop]; the order Skewer the Critics > Needle Drop pays every action with pay1[Mountain#31:R Mountain#32:R Mountain#33:R] ; pay2[Sunbaked Canyon#55:R]  
  board: board=[Sunbaked Canyon#55, Goblin Guide#12, Mountain#31, Mountain#32] hand=[Needle Drop, Mountain, Skewer the Critics] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30182170 --game-index 182170`
- **1_drop_burn** x14 -- T3 m1 `{land=Mountain + cast:Monastery Swiftspear + cast:Vexing Devil + cast:Needle Drop}`  
  engine dropped [cast Needle Drop]; the order Vexing Devil > Monastery Swiftspear > Needle Drop pays every action with pay1[Mountain#33:R] ; pay2[Mountain#31:R] ; pay3[Mountain#32:R]  
  board: board=[Mountain#31, Mountain#32, Goblin Guide#12] hand=[Monastery Swiftspear, Lava Spike, Vexing Devil, Needle Drop, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30152259 --game-index 152259`
- **worldfire** x10 -- T2 m1 `{land=Mistrise Village + cast:Brainstorm + cast:Personal Tutor}`  
  engine dropped [cast Personal Tutor]; the order Personal Tutor > Brainstorm pays every action with pay1[Mistrise Village#23:U] ; pay2[Steam Vents#54:U]  
  board: board=[Steam Vents#54] hand=[Sandstone Needle, Irencrag Feat, Keldon Halberdier, Brainstorm, Mistrise Village, Worldfire, Seething Song, Personal Tutor] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30042703 --game-index 42703`
- **1_drop_burn** x8 -- T3 m1 `{land=Mountain + cast:Goblin Guide + cast:Needle Drop + cast:Vexing Devil}`  
  engine dropped [cast Needle Drop]; the order Vexing Devil > Goblin Guide > Needle Drop pays every action with pay1[Mountain#32:R] ; pay2[Mountain#31:R] ; pay3[Sunbaked Canyon#55:R]  
  board: board=[Sunbaked Canyon#55, Mountain#31, Goblin Guide#12] hand=[Goblin Guide, Boltwave, Needle Drop, Vexing Devil, Mountain] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30100319 --game-index 100319`
- **1_drop_burn** x8 -- T4 m1 `{land=Sunbaked Canyon + cast:Needle Drop + cast:Skewer the Critics}`  
  engine dropped [cast Needle Drop]; the order Skewer the Critics > Needle Drop pays every action with pay1[Mountain#36:R Mountain#34:R Sunbaked Canyon#56:R] ; pay2[Sunbaked Canyon#55:R]  
  board: board=[Sunbaked Canyon#55, Goblin Guide#14, Mountain#36, Mountain#34] hand=[Needle Drop, Skewer the Critics, Sunbaked Canyon] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30131505 --game-index 131505`
- **1_drop_burn** x8 -- T4 m1 `{land=Mountain + cast:Needle Drop + cast:Skewer the Critics}`  
  engine dropped [cast Needle Drop]; the order Skewer the Critics > Needle Drop pays every action with pay1[Sunbaked Canyon#56:R Mountain#39:R Mountain#35:R] ; pay2[Sunbaked Canyon#55:R]  
  board: board=[Sunbaked Canyon#56, Sunbaked Canyon#55, Mountain#39] hand=[Needle Drop, Fireblast, Mountain, Skewer the Critics] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30134309 --game-index 134309`
- **1_drop_burn** x8 -- T3 m1 `{land=Mountain + cast:Needle Drop + cast:Vexing Devil + cast:Goblin Guide}`  
  engine dropped [cast Needle Drop]; the order Vexing Devil > Goblin Guide > Needle Drop pays every action with pay1[Mountain#32:R] ; pay2[Mountain#31:R] ; pay3[Sunbaked Canyon#55:R]  
  board: board=[Sunbaked Canyon#55, Mountain#31, Monastery Swiftspear#27] hand=[Needle Drop, Vexing Devil, Goblin Guide, Mountain] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30189802 --game-index 189802`
- **worldfire** x7 -- T2 m1 `{land=Sandstone Needle + cast:Brainstorm + cast:Ponder}`  
  engine dropped [cast Ponder]; the order Ponder > Brainstorm pays every action with pay1[Saprazzan Skerry#45:U] ; pay2[(float only)]  
  board: board=[Saprazzan Skerry#45] hand=[Brainstorm, Personal Tutor, Brainstorm, Seething Song, Keldon Halberdier, Ponder, Sandstone Needle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30044001 --game-index 44001`
- **worldfire** x7 -- T3 m1 `{land=Mercadian Bazaar + cast:Brainstorm + cast:Ponder}`  
  engine dropped [cast Ponder]; the order Ponder > Brainstorm pays every action with pay1[Saprazzan Skerry#43:U] ; pay2[(float only)]  
  board: board=[Sandstone Needle#38, Saprazzan Skerry#43] hand=[Brainstorm, Ponder, Rift Bolt, Personal Tutor, Mystical Tutor, Mercadian Bazaar] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30045885 --game-index 45885`
- **worldfire** x7 -- T3 m1 `{land=Mountain + cast:Brainstorm + cast:Personal Tutor + suspend:Keldon Halberdier}`  
  engine dropped [cast Personal Tutor]; the order Personal Tutor > Brainstorm > suspend:Keldon Halberdier pays every action with pay1[Mountain#24:R] ; pay2[Island#9:U] ; pay3[Steam Vents#54:U]  
  board: board=[Steam Vents#54, Island#9] hand=[Personal Tutor, Keldon Halberdier, Irencrag Feat, Mountain, Worldfire, Seething Song, Brainstorm] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30057224 --game-index 57224`
- **1_drop_burn** x7 -- T3 m1 `{land=Mountain + cast:Needle Drop + cast:Goblin Guide + cast:Vexing Devil}`  
  engine dropped [cast Needle Drop]; the order Vexing Devil > Goblin Guide > Needle Drop pays every action with pay1[Mountain#33:R] ; pay2[Mountain#31:R] ; pay3[Mountain#32:R]  
  board: board=[Mountain#31, Mountain#32, Goblin Guide#12] hand=[Needle Drop, Mountain, Goblin Guide, Vexing Devil] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop Burn.profile.json' --games 1 --seed 30115018 --game-index 115018`
- ... 234 more signatures in flags.jsonl

## ORACLE-GAP (18 flags, 2 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **shadowborn_apostle** x17 -- T8 m1 `{no land + cast:Shadowborn Apostle}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#47:B]: Swamp#47 tapped by oracle only; same core; float engine - oracle {B}; engine ended board=[Lake of the Dead#15, Thrumming Stone#57, Swamp#52, Reaper from the Abyss#20, Swamp#47, Shadowborn Apostle#31(new), Shadowborn Apostle#35(new), Shadowborn Apostle#38(new), Shadowborn Apostle#37(new), Shadowborn Apostle#32(new), Shadowborn Apostle#30(new), Shadowborn Apostle#29(new), Shadowborn Apostle#21(new), Shadowborn Apostle#26(new), Shadowborn Apostle#36(new), Shadowborn Apostle#33(new), Shadowborn Apostle#27(new), Shadowborn Apostle#24(new), Shadowborn Apostle#23(new), Shadowborn Apostle#22(new), Shadowborn Apostle#34(new), Sh  
  board: board=[Lake of the Dead#15, Thrumming Stone#57, Swamp#44, Swamp#52, Reaper from the Abyss#20, Swamp#47] hand=[Lake of the Dead, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30174168 --game-index 174168`
- **worldfire** x1 -- T7 m2 `{no land + cast:Keldon Halberdier}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Mercadian Bazaar#21:R]: Sandstone Needle#39 only in oracle; Saprazzan Skerry#44 only in oracle; graveyard size differs; float engine {R}{R}{R}{R}{R}{R} oracle {R}{R}; engine ended board=[Mercadian Bazaar#21(T), Mountain#24(T), Keldon Halberdier#11(new)] hand=[Scalding Tarn, Scalding Tarn, Scalding Tarn] float={R}{R}{R}{R}{R}{R} life=1  
  board: board=[Mercadian Bazaar#21, Sandstone Needle#39, Saprazzan Skerry#44, Mountain#24(T)] hand=[Scalding Tarn, Scalding Tarn, Scalding Tarn, Keldon Halberdier] float=- life=1  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Worldfire/Worldfire.cod' --profile 'decks/Worldfire/Worldfire.profile.json' --games 1 --seed 30045666 --game-index 45666`

