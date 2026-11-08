# Mana-clean gate `20261108-142402`

Mana-clean gate run: 3 decks, games (seed 30000000 + game index) angels 110000-209999, slivers_vial 110000-209999, vampires 110000-209999; src `3bda5c647e7c`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | DOMINATED-CREATURE | DOMINATED-LIFE | ORACLE-GAP | UNMODELED | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| angels | 100000 | issues | 103 | 13 | 50 | 40 | 0 | 0 | 0 | 20354 | 0 | 248 | 43279 |
| slivers_vial | 100000 | issues | 505 | 0 | 440 | 0 | 0 | 65 | 0 | 0 | 0 | 0 | 12179 |
| vampires | 100000 | issues | 71 | 0 | 0 | 0 | 41616 | 42 | 29 | 2992 | 12936 | 13400 | 3283 |

**MISS = 543, ORACLE-GAP = 107.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=2320006 plans=13918316 skipped=0 applies=2044548086 frontiers=1592148281 pruned=50635874 options=463694141 engine_drops=50153 gate_rejects=49578563(checked 49455541, skipped 123022) MISS=42159 ORACLE-GAP=107 UNMODELED=29 CAPPED=0 ms=33279742 ===
=== MANA ORACLE classes: ORACLE-GAP=107 ENUM-GAP=13 APPLY-DROP=490 PHANTOM=23346 DOMINATED-CREATURE=40 DOMINATED-LIFE=41616 DOMINATED-LAND=58741 CONTINUATION-DIFFERS=12936 DROP-COVERED=13648 UNMODELED=29 ===
=== MANA ORACLE enum-gap rejections: noop=1132849 hidden=0 host_blind=0 cont_land=0 ===
=== MANA ORACLE capped-plan escalation: 2 retried, 2 finished ===
=== MANA ORACLE unmodeled: 5498919 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 166 x payment enumeration capped ===
=== MANA ORACLE unmodeled: 29 x too many actions to try every order ===
=== MANA ORACLE rollback census: lost=503 depth0=0 depth1=127 depth2=69 depth3+=294 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=0 off-menu=13 need_in_plan=490 need_later=0 pay_rollback_rescued=1/490 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | DOMINATED-CREATURE | ORACLE-GAP | UNMODELED |
|---|---|---|---|---|---|---|---|
| angels | 826767 | 3199204 | 13 | 50 | 40 | 0 | 0 |
| slivers_vial | 742493 | 4071770 | 0 | 440 | 0 | 65 | 0 |
| vampires | 750746 | 6647342 | 0 | 0 | 0 | 42 | 29 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth1 | depth2 | depth3+ | off-menu | rescued |
|---|---|---|---|---|---|
| angels | 50 | 0 | 0 | 13 | 0 |
| slivers_vial | 77 | 69 | 294 | 0 | 1 |

## ENUM-GAP (13 flags, 11 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **angels** x2 -- T2 m1 `{land=Plains + cast:Lightning Greaves + cast:Lightstall Inquisitor + cast:Giada, Font of Hope + cast:Sol Ring + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Plains#21:W] ; pay2[Sol Ring#54:C] ; pay3[Remote Farm#36:W] ; pay4[(float only)] ; pay5[Giada, Font of Hope#7:W(Angel-only)]  
  board: board=[Remote Farm#36] hand=[Plains, Bishop of Wings, Righteous Valkyrie, Plains, Lightning Greaves, Lightstall Inquisitor, Giada, Font of Hope, Sol Ring] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30122695 --game-index 122695`
- **angels** x2 -- T2 m1 `{land=Seraph Sanctuary + cast:Lightning Greaves + cast:Lightstall Inquisitor + cast:Giada, Font of Hope + cast:Sol Ring + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Seraph Sanctuary#48:C] ; pay2[Sol Ring#54:C] ; pay3[Remote Farm#38:W] ; pay4[(float only)] ; pay5[Giada, Font of Hope#8:W(Angel-only)]  
  board: board=[Remote Farm#38] hand=[Resplendent Angel, Seraph Sanctuary, Lightning Greaves, Lightstall Inquisitor, Giada, Font of Hope, Sol Ring, Unexpectedly Absent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30178273 --game-index 178273`
- **angels** x1 -- T2 m1 `{land=Plains + cast:Sol Ring + cast:Giada, Font of Hope + cast:Lightstall Inquisitor + cast:Lightning Greaves + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Plains#25:W] ; pay2[Sol Ring#54:C] ; pay3[Remote Farm#36:W] ; pay4[(float only)] ; pay5[Giada, Font of Hope#8:W(Angel-only)]  
  board: board=[Remote Farm#36] hand=[Sol Ring, Archangel of Thune, Giada, Font of Hope, Lightstall Inquisitor, Lyra, Archangel of Dawn, Lightning Greaves, Plains] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30128441 --game-index 128441`
- **angels** x1 -- T2 m1 `{land=Plains + cast:Giada, Font of Hope + cast:Sol Ring + cast:Lightstall Inquisitor + cast:Lightning Greaves + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Plains#35:W] ; pay2[Sol Ring#54:C] ; pay3[Remote Farm#39:W] ; pay4[(float only)] ; pay5[Giada, Font of Hope#7:W(Angel-only)]  
  board: board=[Remote Farm#39] hand=[Giada, Font of Hope, Plains, Sol Ring, Lightstall Inquisitor, Lightning Greaves] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30135490 --game-index 135490`
- **angels** x1 -- T3 m1 `{no land + cast:Lightstall Inquisitor + cast:Giada, Font of Hope + cast:Lightning Greaves + cast:Sol Ring + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Remote Farm#36:W] ; pay2[Sol Ring#54:C] ; pay3[Seraph Sanctuary#48:C] ; pay4[(float only)] ; pay5[Giada, Font of Hope#7:W(Angel-only)]  
  board: board=[Seraph Sanctuary#48, Remote Farm#36] hand=[Bishop of Wings, Lightstall Inquisitor, Giada, Font of Hope, Righteous Valkyrie, Lightning Greaves, Righteous Valkyrie, Seraph Sanctuary, Sol Ring] float=- life=21  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30142083 --game-index 142083`
- **angels** x1 -- T2 m1 `{land=Seraph Sanctuary + cast:Giada, Font of Hope + cast:Lightstall Inquisitor + cast:Lightning Greaves + cast:Sol Ring + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Seraph Sanctuary#48:C] ; pay2[Sol Ring#54:C] ; pay3[Remote Farm#36:W] ; pay4[(float only)] ; pay5[Giada, Font of Hope#7:W(Angel-only)]  
  board: board=[Remote Farm#36] hand=[Giada, Font of Hope, Righteous Valkyrie, Seraph Sanctuary, Lightstall Inquisitor, Lightning Greaves, Unexpectedly Absent, Sol Ring, Swords to Plowshares] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30144953 --game-index 144953`
- **angels** x1 -- T2 m1 `{land=Plains + cast:Giada, Font of Hope + cast:Lightning Greaves + cast:Lightstall Inquisitor + cast:Sol Ring + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Plains#26:W] ; pay2[Sol Ring#54:C] ; pay3[Remote Farm#37:W] ; pay4[(float only)] ; pay5[Giada, Font of Hope#10:W(Angel-only)]  
  board: board=[Remote Farm#37] hand=[Giada, Font of Hope, Lightning Greaves, Lightstall Inquisitor, Lightstall Inquisitor, Sol Ring, Lightstall Inquisitor, Plains] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30157789 --game-index 157789`
- **angels** x1 -- T2 m1 `{land=Seraph Sanctuary + cast:Lightning Greaves + cast:Sol Ring + cast:Giada, Font of Hope + cast:Lightstall Inquisitor + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Seraph Sanctuary#48:C] ; pay2[Sol Ring#54:C] ; pay3[Remote Farm#36:W] ; pay4[(float only)] ; pay5[Giada, Font of Hope#7:W(Angel-only)]  
  board: board=[Remote Farm#36] hand=[Lightning Greaves, Seraph Sanctuary, Sol Ring, Resplendent Angel, Bishop of Wings, Righteous Valkyrie, Giada, Font of Hope, Lightstall Inquisitor] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30189827 --game-index 189827`
- **angels** x1 -- T2 m1 `{land=Plains + cast:Lightning Greaves + cast:Giada, Font of Hope + cast:Lightstall Inquisitor + cast:Sol Ring + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Plains#21:W] ; pay2[Sol Ring#54:C] ; pay3[Remote Farm#36:W] ; pay4[(float only)] ; pay5[Giada, Font of Hope#7:W(Angel-only)]  
  board: board=[Remote Farm#36] hand=[Lightning Greaves, Giada, Font of Hope, Lyra, Archangel of Dawn, Archangel of Thune, Lightstall Inquisitor, Sol Ring, Remote Farm, Plains] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30194119 --game-index 194119`
- **angels** x1 -- T2 m1 `{land=Plains + cast:Sol Ring + cast:Lightstall Inquisitor + cast:Giada, Font of Hope + cast:Lightning Greaves + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Plains#21:W] ; pay2[Sol Ring#54:C] ; pay3[Remote Farm#36:W] ; pay4[(float only)] ; pay5[Giada, Font of Hope#7:W(Angel-only)]  
  board: board=[Remote Farm#36] hand=[Sol Ring, Resplendent Angel, Lightstall Inquisitor, Plains, Giada, Font of Hope, Giada, Font of Hope, Lightning Greaves] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30199786 --game-index 199786`
- **angels** x1 -- T2 m1 `{land=Plains + cast:Lightstall Inquisitor + cast:Sol Ring + cast:Lightning Greaves + cast:Giada, Font of Hope + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Plains#21:W] ; pay2[Sol Ring#54:C] ; pay3[Remote Farm#36:W] ; pay4[(float only)] ; pay5[Giada, Font of Hope#7:W(Angel-only)]  
  board: board=[Remote Farm#36] hand=[Lightstall Inquisitor, Archangel of Thune, Sol Ring, Lyra Dawnbringer, Lightning Greaves, Giada, Font of Hope, Plains] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30207736 --game-index 207736`

## APPLY-DROP (490 flags, 324 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **angels** x14 -- T4 m1 `{land=Plains + cast:Serra the Benevolent + cast:Giada, Font of Hope}`  
  engine dropped [cast Serra the Benevolent]; every action pays with pay1[Seraph Sanctuary#48:C Giada, Font of Hope#7:W(Angel-only)] ; pay2[Plains#21:W Sol Ring#54:C Plains#22:W] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Seraph Sanctuary#48:C Giada, Font of Hope#7:W(Angel-only)] ; pay2[Plains#21:W Sol Ring#54:C Plains#22:W]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Plains#21, Sol Ring#54, Seraph Sanctuary#48, Giada, Font of Hope#7, Seraph Sanctuary#49, Resplendent Angel#40] hand=[Serra the Benevolent, Giada, Font of Hope, Unexpectedly Absent, Plains] float=- life=25  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30121072 --game-index 121072`
- **angels** x14 -- T3 m1 `{land=Seraph Sanctuary + cast:Lightstall Inquisitor + cast:Lightstall Inquisitor + cast:Serra the Benevolent}`  
  engine dropped [cast Serra the Benevolent]; every action pays with pay1[Giada, Font of Hope#7:W(Angel-only)] ; pay2[Remote Farm#36:W] ; pay3[Plains#21:W Sol Ring#54:C] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Giada, Font of Hope#7:W(Angel-only)] ; pay3[Sol Ring#54:C Remote Farm#36:W]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Plains#21, Sol Ring#54, Remote Farm#36, Giada, Font of Hope#7] hand=[Lightstall Inquisitor, Seraph Sanctuary, Lightstall Inquisitor, Serra the Benevolent, Remote Farm] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30192512 --game-index 192512`
- **angels** x8 -- T4 m1 `{land=Plains + cast:Bishop of Wings + cast:Serra the Benevolent + cast:Lightstall Inquisitor}`  
  engine dropped [cast Serra the Benevolent]; every action pays with pay1[Remote Farm#36:W] ; pay2[Giada, Font of Hope#7:W(Angel-only)] ; pay3[Plains#21:W Sol Ring#54:C Plains#22:W] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Giada, Font of Hope#7:W(Angel-only)] ; pay3[Sol Ring#54:C Remote Farm#36:W]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Plains#21, Sol Ring#54, Remote Farm#36, Lyra, Archangel of Dawn#17, Seraph Sanctuary#48, Giada, Font of Hope#7, Resplendent Angel#40] hand=[Bishop of Wings, Serra the Benevolent, Lightstall Inquisitor, Plains] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30145441 --game-index 145441`
- **angels** x7 -- T3 m1 `{land=Seraph Sanctuary + cast:Lightstall Inquisitor + cast:Serra the Benevolent + cast:Lightstall Inquisitor}`  
  engine dropped [cast Serra the Benevolent]; every action pays with pay1[Giada, Font of Hope#7:W(Angel-only)] ; pay2[Remote Farm#38:W] ; pay3[Remote Farm#36:W Seraph Sanctuary#48:C] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Giada, Font of Hope#7:W(Angel-only) keep-floating {W}] ; pay3[Remote Farm#36:W Seraph Sanctuary#48:C]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Remote Farm#36, Remote Farm#38, Giada, Font of Hope#7] hand=[Seraph Sanctuary, Lyra Dawnbringer, Lightstall Inquisitor, Serra the Benevolent, Lightstall Inquisitor] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30146164 --game-index 146164`
- **slivers_vial** x6 -- T3 m1 `{land=Cavern of Souls + cast:Striking Sliver + cast:Aether Vial}`  
  engine dropped [cast Aether Vial]; every action pays with pay1[Unclaimed Territory#57:C Ancient Ziggurat#5:W(creature-only) Cavern of Souls#8:R(creature-only)] ; pay2[(float only)] || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Unclaimed Territory#57:C Ancient Ziggurat#5:W(creature-only) Cavern of Souls#8:R(creature-only)] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Ancient Ziggurat#5, Galerider Sliver#19, Unclaimed Territory#57, Hatchery Sliver#22] hand=[Cavern of Souls, Striking Sliver, Sinew Sliver, Aether Vial] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/slivers_vial/slivers_vial.txt' --profile 'decks/slivers_vial/slivers_vial.profile.json' --games 1 --seed 30121204 --game-index 121204`
- **angels** x5 -- T3 m1 `{land=Plains + cast:Lightstall Inquisitor + cast:Serra the Benevolent + cast:Giada, Font of Hope + equip:Lightning Greaves}`  
  engine dropped [cast Serra the Benevolent]; every action pays with pay1[Remote Farm#36:W Seraph Sanctuary#48:C] ; pay2[(float only)] ; pay3[Giada, Font of Hope#7:W(Angel-only) keep-floating {W}] ; pay4[Sol Ring#54:C Plains#21:W] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Giada, Font of Hope#7:W(Angel-only)] ; pay4[Sol Ring#54:C Remote Farm#36:W]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Seraph Sanctuary#48, Sol Ring#54, Lightning Greaves#11, Remote Farm#36] hand=[Archangel of Thune, Unexpectedly Absent, Lightstall Inquisitor, Serra the Benevolent, Giada, Font of Hope, Plains] float=- life=21  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30142243 --game-index 142243`
- **slivers_vial** x5 -- T3 m1 `{land=Ancient Ziggurat + cast:Galerider Sliver + cast:Aether Vial}`  
  engine dropped [cast Aether Vial]; every action pays with pay1[Sliver Hive#53:C Sliver Hive#52:C Ancient Ziggurat#6:U(creature-only)] ; pay2[(float only)] || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Sliver Hive#53:C Sliver Hive#52:C Ancient Ziggurat#6:U(creature-only)] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sliver Hive#53, Plated Sliver#35, Sliver Hive#52, Hatchery Sliver#23] hand=[Cloudshredder Sliver, Galerider Sliver, Ancient Ziggurat, Aether Vial] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/slivers_vial/slivers_vial.txt' --profile 'decks/slivers_vial/slivers_vial.profile.json' --games 1 --seed 30110598 --game-index 110598`
- **slivers_vial** x5 -- T5 m1 `{land=Ancient Ziggurat + cast:Plated Sliver + cast:Aether Vial}`  
  engine dropped [cast Aether Vial]; every action pays with pay1[Sliver Hive#50:C Unclaimed Territory#57:C Ancient Ziggurat#5:W(creature-only)] ; pay2[(float only)] || ROLLBACK depth2: keep the engine's first 1 mana decision, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[keep-floating {W}] ; pay3[Ancient Ziggurat#5:W(creature-only) keep-floating {W}] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sliver Hive#50, Galerider Sliver#17, Unclaimed Territory#57, Hibernation Sliver#25, Hatchery Sliver#21, Galerider Sliver#0, Galerider Sliver#18] hand=[Predatory Sliver, Sinew Sliver, Galerider Sliver, Plated Sliver, Aether Vial, Ancient Ziggurat] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/slivers_vial/slivers_vial.txt' --profile 'decks/slivers_vial/slivers_vial.profile.json' --games 1 --seed 30111337 --game-index 111337`
- **slivers_vial** x5 -- T3 m1 `{land=Ancient Ziggurat + cast:Striking Sliver + cast:Aether Vial}`  
  engine dropped [cast Aether Vial]; every action pays with pay1[Sliver Hive#50:C Unclaimed Territory#57:C Ancient Ziggurat#5:R(creature-only)] ; pay2[(float only)] || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Sliver Hive#50:C Unclaimed Territory#57:C Ancient Ziggurat#5:R(creature-only)] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sliver Hive#50, Plated Sliver#34, Unclaimed Territory#57, Hatchery Sliver#21] hand=[Muscle Sliver, Predatory Sliver, Cavern of Souls, Striking Sliver, Ancient Ziggurat, Aether Vial] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/slivers_vial/slivers_vial.txt' --profile 'decks/slivers_vial/slivers_vial.profile.json' --games 1 --seed 30121583 --game-index 121583`
- **slivers_vial** x5 -- T3 m1 `{land=Secluded Courtyard + cast:Galerider Sliver + cast:Aether Vial}`  
  engine dropped [cast Aether Vial]; every action pays with pay1[Sliver Hive#50:C Ancient Ziggurat#5:W(creature-only) Secluded Courtyard#42:U(creature-only)] ; pay2[(float only)] || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Sliver Hive#50:C Ancient Ziggurat#5:W(creature-only) Secluded Courtyard#42:U(creature-only)] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sliver Hive#50, Plated Sliver#34, Ancient Ziggurat#5, Hatchery Sliver#21] hand=[Predatory Sliver, Galerider Sliver, Muscle Sliver, Galerider Sliver, Secluded Courtyard, Aether Vial] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/slivers_vial/slivers_vial.txt' --profile 'decks/slivers_vial/slivers_vial.profile.json' --games 1 --seed 30140635 --game-index 140635`
- **slivers_vial** x4 -- T3 m1 `{land=Ancient Ziggurat + cast:Aether Vial + cast:Plated Sliver}`  
  engine dropped [cast Aether Vial]; every action pays with pay1[Secluded Courtyard#42:C Cavern of Souls#9:C Ancient Ziggurat#5:W(creature-only)] ; pay2[(float only)] || ROLLBACK depth2: keep the engine's first 1 mana decision, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[keep-floating {W}] ; pay3[Ancient Ziggurat#5:W(creature-only) keep-floating {W}] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Secluded Courtyard#42, Plated Sliver#36, Cavern of Souls#9, Hatchery Sliver#23] hand=[Aether Vial, Ancient Ziggurat, Sinew Sliver, Mutavault, Plated Sliver] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/slivers_vial/slivers_vial.txt' --profile 'decks/slivers_vial/slivers_vial.profile.json' --games 1 --seed 30110277 --game-index 110277`
- **slivers_vial** x4 -- T5 m1 `{land=Ancient Ziggurat + cast:Galerider Sliver + cast:Aether Vial}`  
  engine dropped [cast Aether Vial]; every action pays with pay1[Sliver Hive#50:C Unclaimed Territory#57:C Ancient Ziggurat#5:U(creature-only)] ; pay2[(float only)] || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Sliver Hive#50:C Unclaimed Territory#57:C Ancient Ziggurat#5:U(creature-only)] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sliver Hive#50, Galerider Sliver#17, Unclaimed Territory#57, Hibernation Sliver#25, Hatchery Sliver#21, Galerider Sliver#0, Galerider Sliver#18] hand=[Predatory Sliver, Sinew Sliver, Galerider Sliver, Plated Sliver, Aether Vial, Ancient Ziggurat] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/slivers_vial/slivers_vial.txt' --profile 'decks/slivers_vial/slivers_vial.profile.json' --games 1 --seed 30111337 --game-index 111337`
- ... 312 more signatures in flags.jsonl

## DOMINATED-CREATURE (40 flags, 13 signatures)

_the engine's payment taps a creature that could attack, where another payment keeps it untapped and loses nothing._

- **angels** x14 -- T3 m1 `{land=Plains + cast:Serra the Benevolent}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Remote Farm#39:W Sol Ring#54:C Plains#23:W] ; pay2[(float only)]; the engine ended board=[Sol Ring#54(T), Giada, Font of Hope#8(T), Plains#23(T)(new), Serra the Benevolent#53(new), Lightstall Inquisitor#14(new), 4/4 Angel Token#1000(new)] hand=[Youthful Valkyrie, Lyra Dawnbringer] float={C} life=20  
  board: board=[Remote Farm#39, Sol Ring#54, Giada, Font of Hope#8] hand=[Serra the Benevolent, Youthful Valkyrie, Lightstall Inquisitor, Lyra Dawnbringer, Plains] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30118339 --game-index 118339`
- **angels** x8 -- T4 m1 `{no land + cast:Serra the Benevolent}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Remote Farm#36:W Sol Ring#54:C Plains#32:W] ; pay2[(float only)]; the engine ended board=[Sol Ring#54(T), Giada, Font of Hope#8(T), Plains#32(T), Righteous Valkyrie#45, Lyra Dawnbringer#16, Serra the Benevolent#53(new), Lightstall Inquisitor#12(new), 4/4 Angel Token#1000(new)] hand=[Righteous Valkyrie] float={C} life=45  
  board: board=[Remote Farm#36, Sol Ring#54, Giada, Font of Hope#8, Plains#32, Righteous Valkyrie#45, Lyra Dawnbringer#16] hand=[Serra the Benevolent, Lightstall Inquisitor, Righteous Valkyrie] float=- life=27  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30189205 --game-index 189205`
- **angels** x4 -- T4 m1 `{land=Remote Farm + cast:Serra the Benevolent}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Remote Farm#37:W Sol Ring#54:C Plains#21:W] ; pay2[(float only)]; the engine ended board=[Sol Ring#54(T), Giada, Font of Hope#7(T), Plains#21(T), Lyra, Archangel of Dawn#17, Lyra Dawnbringer#16, Remote Farm#38(T)(new), Serra the Benevolent#52(new), Lightstall Inquisitor#12(new), 4/4 Angel Token#1000(new)] hand=[Bishop of Wings] float={C} life=20  
  board: board=[Remote Farm#37, Sol Ring#54, Giada, Font of Hope#7, Plains#21, Lyra, Archangel of Dawn#17, Lyra Dawnbringer#16] hand=[Lightstall Inquisitor, Remote Farm, Bishop of Wings, Serra the Benevolent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30203925 --game-index 203925`
- **angels** x3 -- T3 m1 `{no land + cast:Righteous Valkyrie + cast:Lightstall Inquisitor + cast:Sol Ring}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Remote Farm#36:W] ; pay2[Sol Ring#54:C] ; pay3[Plains#25:W]; the engine ended board=[Plains#25(T), Giada, Font of Hope#8(T), Lightstall Inquisitor#14, Sol Ring#54(T)(new), Righteous Valkyrie#45(new), Lightstall Inquisitor#13(new)] hand=[Lyra, Archangel of Dawn] float={C} life=24  
  board: board=[Remote Farm#36, Plains#25, Giada, Font of Hope#8, Lightstall Inquisitor#14] hand=[Righteous Valkyrie, Lightstall Inquisitor, Lyra, Archangel of Dawn, Sol Ring] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30157823 --game-index 157823`
- **angels** x2 -- T3 m1 `{no land + cast:Resplendent Angel + cast:Giada, Font of Hope + cast:Sol Ring}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Remote Farm#37:W] ; pay2[Remote Farm#36:W Sol Ring#54:C] ; pay3[(float only)]; the engine ended board=[Remote Farm#37(T), Giada, Font of Hope#7(T), Sol Ring#54(T)(new), Resplendent Angel#40(new)] hand=[Serra the Benevolent, Seraph Sanctuary, Archangel of Thune, Righteous Valkyrie] float={C} life=20  
  board: board=[Remote Farm#36, Remote Farm#37, Giada, Font of Hope#7] hand=[Serra the Benevolent, Seraph Sanctuary, Archangel of Thune, Righteous Valkyrie, Resplendent Angel, Giada, Font of Hope, Sol Ring] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30110363 --game-index 110363`
- **angels** x2 -- T3 m1 `{land=Plains + cast:Serra the Benevolent}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Remote Farm#37:W Sol Ring#54:C] ; pay2[Remote Farm#36:W Plains#21:W] ; pay3[(float only)]; the engine ended board=[Remote Farm#37(T), Sol Ring#54(T), Giada, Font of Hope#7(T), Plains#21(T)(new), Serra the Benevolent#52(new), Bishop of Wings#3(new), Lightstall Inquisitor#12(new), 4/4 Angel Token#1000(new)] hand=[Giada, Font of Hope] float={C} life=28  
  board: board=[Remote Farm#36, Remote Farm#37, Sol Ring#54, Giada, Font of Hope#7] hand=[Lightstall Inquisitor, Bishop of Wings, Giada, Font of Hope, Plains, Serra the Benevolent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30204530 --game-index 204530`
- **angels** x1 -- T4 m1 `{no land + cast:Giada, Font of Hope + cast:Giada, Font of Hope + cast:Sol Ring}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Remote Farm#37:W] ; pay2[Sol Ring#54:C] ; pay3[Plains#21:W]; the engine ended board=[Giada, Font of Hope#7(T), Plains#21(T), Righteous Valkyrie#44, Righteous Valkyrie#45, Sol Ring#54(T)(new)] hand=[Archangel of Thune] float={C} life=58  
  board: board=[Remote Farm#37, Giada, Font of Hope#7, Plains#21, Righteous Valkyrie#44, Righteous Valkyrie#45] hand=[Giada, Font of Hope, Archangel of Thune, Giada, Font of Hope, Sol Ring] float=- life=26  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30116390 --game-index 116390`
- **angels** x1 -- T3 m1 `{no land + cast:Lightstall Inquisitor + cast:Lyra, Archangel of Dawn + cast:Sol Ring}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Remote Farm#36:W] ; pay2[Sol Ring#54:C] ; pay3[Plains#25:W]; the engine ended board=[Plains#25(T), Giada, Font of Hope#8(T), Lightstall Inquisitor#14, Sol Ring#54(T)(new), Lyra, Archangel of Dawn#17(new), Lightstall Inquisitor#13(new)] hand=[Righteous Valkyrie] float={C} life=20  
  board: board=[Remote Farm#36, Plains#25, Giada, Font of Hope#8, Lightstall Inquisitor#14] hand=[Righteous Valkyrie, Lightstall Inquisitor, Lyra, Archangel of Dawn, Sol Ring] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30157823 --game-index 157823`
- **angels** x1 -- T3 m1 `{no land + cast:Righteous Valkyrie + cast:Lightstall Inquisitor + cast:Sol Ring}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Seraph Sanctuary#48:C] ; pay2[Remote Farm#36:W Sol Ring#54:C] ; pay3[(float only)]; the engine ended board=[Seraph Sanctuary#48(T), Giada, Font of Hope#7(T), Lightstall Inquisitor#12, Sol Ring#54(T)(new), Righteous Valkyrie#44(new), Lightstall Inquisitor#13(new)] hand=[Bishop of Wings, Resplendent Angel, Seraph Sanctuary] float={C} life=29  
  board: board=[Remote Farm#36, Seraph Sanctuary#48, Giada, Font of Hope#7, Lightstall Inquisitor#12] hand=[Bishop of Wings, Resplendent Angel, Righteous Valkyrie, Seraph Sanctuary, Lightstall Inquisitor, Sol Ring] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30160759 --game-index 160759`
- **angels** x1 -- T3 m1 `{no land + cast:Youthful Valkyrie + cast:Giada, Font of Hope + cast:Sol Ring}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Seraph Sanctuary#48:C] ; pay2[Remote Farm#36:W Sol Ring#54:C] ; pay3[(float only)]; the engine ended board=[Seraph Sanctuary#48(T), Giada, Font of Hope#7(T), Lightstall Inquisitor#12, Sol Ring#54(T)(new), Youthful Valkyrie#60(new)] hand=[Archangel of Thune, Righteous Valkyrie, Serra the Benevolent] float={C} life=25  
  board: board=[Remote Farm#36, Seraph Sanctuary#48, Giada, Font of Hope#7, Lightstall Inquisitor#12] hand=[Youthful Valkyrie, Archangel of Thune, Righteous Valkyrie, Serra the Benevolent, Giada, Font of Hope, Sol Ring] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30166881 --game-index 166881`
- **angels** x1 -- T3 m1 `{no land + cast:Giada, Font of Hope + cast:Resplendent Angel + cast:Sol Ring}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Remote Farm#37:W] ; pay2[Remote Farm#39:W Sol Ring#54:C] ; pay3[(float only)]; the engine ended board=[Remote Farm#37(T), Giada, Font of Hope#7(T), Sol Ring#54(T)(new), Resplendent Angel#41(new)] hand=[Lightstall Inquisitor, Unexpectedly Absent] float={C} life=20  
  board: board=[Remote Farm#39, Remote Farm#37, Giada, Font of Hope#7] hand=[Lightstall Inquisitor, Giada, Font of Hope, Resplendent Angel, Unexpectedly Absent, Sol Ring] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30172439 --game-index 172439`
- **angels** x1 -- T3 m1 `{no land + cast:Lightstall Inquisitor + cast:Lyra, Archangel of Dawn + cast:Sol Ring}`  
  keeps Giada, Font of Hope untapped (attacker) -- by paying pay1[Seraph Sanctuary#49:C] ; pay2[Remote Farm#37:W Sol Ring#54:C] ; pay3[(float only)]; the engine ended board=[Seraph Sanctuary#49(T), Giada, Font of Hope#7(T), Lightstall Inquisitor#13, Sol Ring#54(T)(new), Lyra, Archangel of Dawn#19(new), Lightstall Inquisitor#14(new)] hand=[Resplendent Angel, Bishop of Wings] float={C} life=25  
  board: board=[Remote Farm#37, Seraph Sanctuary#49, Giada, Font of Hope#7, Lightstall Inquisitor#13] hand=[Lightstall Inquisitor, Resplendent Angel, Lyra, Archangel of Dawn, Bishop of Wings, Sol Ring] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile 'decks/Angels/Angels.profile.json' --games 1 --seed 30184847 --game-index 184847`
- ... 1 more signatures in flags.jsonl

## ORACLE-GAP (107 flags, 64 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **vampires** x8 -- T4 m1 `{land=Voldaren Estate + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1)}`  
  27 oracle outcomes, none covers the engine's -- nearest pay1[Voldaren Estate#60:R(creature-only)] ; pay2[(float only)]: Voldaren Estate#58 tapped by engine only; Blood Token#1001 only in engine; float engine - oracle -; engine ended board=[Secluded Courtyard#34, Falkenrath Gorger#16, Voldaren Estate#58(T), Falkenrath Pit Fighter#22, Markov Baron#30, Voldaren Estate#57(T), Stromkirk Condemned#40, Voldaren Estate#60(T)(new), Falkenrath Pit Fighter#21(new), Blood Token#1000(new), Blood Token#1001(new)] hand=[] float=- life=18  
  board: board=[Secluded Courtyard#34, Falkenrath Gorger#16, Voldaren Estate#58, Falkenrath Pit Fighter#22, Markov Baron#30, Voldaren Estate#57, Stromkirk Condemned#40] hand=[Voldaren Estate, Falkenrath Pit Fighter] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30135521 --game-index 135521`
- **vampires** x8 -- T4 m1 `{land=Voldaren Estate + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Voldaren Estate(x1)}`  
  17 oracle outcomes, none covers the engine's -- nearest pay1[Blackcleave Cliffs#7:R] ; pay2[(float only)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)]: Blackcleave Cliffs#7 tapped by oracle only; Blood Token#1002 only in oracle; life engine 16 oracle 17; float engine - oracle -; engine ended board=[Blackcleave Cliffs#7, Falkenrath Gorger#16, Voldaren Estate#57(T), Legion Lieutenant#24, Voldaren Estate#58(T), Stromkirk Condemned#40, Falkenrath Pit Fighter#20, Voldaren Estate#59(T)(new), Falkenrath Pit Fighter#21(new), Blood Token#1000(new), Blood Token#1001(new)] hand=[Vampire Socialite] float=- life=16  
  board: board=[Blackcleave Cliffs#7, Falkenrath Gorger#16, Voldaren Estate#57, Legion Lieutenant#24, Voldaren Estate#58, Stromkirk Condemned#40, Falkenrath Pit Fighter#20] hand=[Swamp, Vampire Socialite, Voldaren Estate, Falkenrath Pit Fighter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30208219 --game-index 208219`
- **vampires** x6 -- T4 m1 `{land=Voldaren Estate + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Voldaren Estate(x1)}`  
  24 oracle outcomes, none covers the engine's -- nearest pay1[Voldaren Estate#58:C Secluded Courtyard#32:R(creature-only)] ; pay2[(float only)] ; pay3[(float only)] ; pay4[(float only)]: Secluded Courtyard#32 tapped by oracle only; life engine 15 oracle 16; same core; float engine - oracle {C}; engine ended board=[Voldaren Estate#57(T), Falkenrath Gorger#16, Voldaren Estate#58(T), Asylum Visitor#5, Secluded Courtyard#32, Stromkirk Condemned#40, Falkenrath Pit Fighter#20, Voldaren Estate#59(T)(new), Falkenrath Pit Fighter#21(new), Blood Token#1000(new), Blood Token#1001(new)] hand=[Vampire Socialite] float=- life=15  
  board: board=[Voldaren Estate#57, Falkenrath Gorger#16, Voldaren Estate#58, Asylum Visitor#5, Secluded Courtyard#32, Stromkirk Condemned#40, Falkenrath Pit Fighter#20] hand=[Swamp, Voldaren Estate, Falkenrath Pit Fighter, Vampire Socialite] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30192123 --game-index 192123`
- **vampires** x6 -- T4 m1 `{land=Voldaren Estate + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Voldaren Estate(x1)}`  
  24 oracle outcomes, none covers the engine's -- nearest pay1[Secluded Courtyard#32:R(creature-only)] ; pay2[(float only)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)]: Secluded Courtyard#32 tapped by oracle only; Blood Token#1002 only in oracle; life engine 15 oracle 16; float engine - oracle -; engine ended board=[Voldaren Estate#57(T), Falkenrath Gorger#16, Voldaren Estate#58(T), Asylum Visitor#5, Secluded Courtyard#32, Stromkirk Condemned#40, Falkenrath Pit Fighter#20, Voldaren Estate#59(T)(new), Falkenrath Pit Fighter#21(new), Blood Token#1000(new), Blood Token#1001(new)] hand=[Vampire Socialite] float=- life=15  
  board: board=[Voldaren Estate#57, Falkenrath Gorger#16, Voldaren Estate#58, Asylum Visitor#5, Secluded Courtyard#32, Stromkirk Condemned#40, Falkenrath Pit Fighter#20] hand=[Swamp, Voldaren Estate, Falkenrath Pit Fighter, Vampire Socialite] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30192123 --game-index 192123`
- **vampires** x4 -- T5 m1 `{no land + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Voldaren Estate(x1)}`  
  17 oracle outcomes, none covers the engine's -- nearest pay1[Blackcleave Cliffs#7:R] ; pay2[(float only)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)]: Blackcleave Cliffs#7 tapped by oracle only; Blood Token#1003 only in oracle; life engine 16 oracle 17; float engine - oracle -; engine ended board=[Blackcleave Cliffs#7, Falkenrath Pit Fighter#20, Voldaren Estate#57(T), Stromkirk Condemned#40, Voldaren Estate#58(T), Drana, Liberator of Malakir#15, Voldaren Estate#59(T), Falkenrath Pit Fighter#21, Falkenrath Pit Fighter#22(new), Blood Token#1001(new), Blood Token#1002(new)] hand=[] float=- life=16  
  board: board=[Blackcleave Cliffs#7, Falkenrath Pit Fighter#20, Voldaren Estate#57, Stromkirk Condemned#40, Voldaren Estate#58, Drana, Liberator of Malakir#15, Voldaren Estate#59, Falkenrath Pit Fighter#21] hand=[Stromkirk Captain, Falkenrath Pit Fighter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30148585 --game-index 148585`
- **slivers_vial** x3 -- T4 m1 `{no land + cast:Thrumming Hivepool + cast:Plated Sliver + animate:Mutavault}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Sliver Hive#50:C Secluded Courtyard#42:W(creature-only)] ; pay2[Mutavault#32:C keep-floating {C}]: core keys differ in a field not listed here; float engine {C} oracle {C}; engine ended board=[Sliver Hive#50(T), Plated Sliver#34, Secluded Courtyard#42(T), Hatchery Sliver#21, Mutavault#32(T), Plated Sliver#0, Plated Sliver#35, Plated Sliver#36(new), Plated Sliver#0(new), Thrumming Hivepool#56(new)] hand=[Sinew Sliver, Muscle Sliver] float={C} life=20  
  board: board=[Sliver Hive#50, Plated Sliver#34, Secluded Courtyard#42, Hatchery Sliver#21, Mutavault#32, Plated Sliver#0, Plated Sliver#35] hand=[Thrumming Hivepool, Sinew Sliver, Plated Sliver, Muscle Sliver] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/slivers_vial/slivers_vial.txt' --profile 'decks/slivers_vial/slivers_vial.profile.json' --games 1 --seed 30121662 --game-index 121662`
- **slivers_vial** x3 -- T4 m1 `{no land + cast:Thrumming Hivepool + cast:Plated Sliver + animate:Mutavault}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Sliver Hive#50:C Cavern of Souls#7:W(creature-only)] ; pay2[Mutavault#32:C keep-floating {C}]: core keys differ in a field not listed here; float engine {C} oracle {C}; engine ended board=[Sliver Hive#50(T), Plated Sliver#34, Cavern of Souls#7(T), Hatchery Sliver#21, Mutavault#32(T), Striking Sliver#0, Striking Sliver#54, Plated Sliver#35(new), Plated Sliver#0(new), Thrumming Hivepool#56(new)] hand=[Leeching Sliver, Aether Vial] float={C} life=20  
  board: board=[Sliver Hive#50, Plated Sliver#34, Cavern of Souls#7, Hatchery Sliver#21, Mutavault#32, Striking Sliver#0, Striking Sliver#54] hand=[Thrumming Hivepool, Leeching Sliver, Plated Sliver, Aether Vial] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/slivers_vial/slivers_vial.txt' --profile 'decks/slivers_vial/slivers_vial.profile.json' --games 1 --seed 30136844 --game-index 136844`
- **slivers_vial** x3 -- T4 m1 `{no land + cast:Thrumming Hivepool + cast:Plated Sliver + animate:Mutavault}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Secluded Courtyard#42:C Unclaimed Territory#58:W(creature-only)] ; pay2[Mutavault#33:C keep-floating {C}]: core keys differ in a field not listed here; float engine {C} oracle {C}; engine ended board=[Secluded Courtyard#42(T), Galerider Sliver#17, Unclaimed Territory#58(T), Hatchery Sliver#24, Mutavault#33(T), Galerider Sliver#0, Galerider Sliver#20, Plated Sliver#37(new), Plated Sliver#0(new), Thrumming Hivepool#56(new)] hand=[Muscle Sliver, Striking Sliver] float={C} life=20  
  board: board=[Secluded Courtyard#42, Galerider Sliver#17, Unclaimed Territory#58, Hatchery Sliver#24, Mutavault#33, Galerider Sliver#0, Galerider Sliver#20] hand=[Muscle Sliver, Thrumming Hivepool, Striking Sliver, Plated Sliver] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/slivers_vial/slivers_vial.txt' --profile 'decks/slivers_vial/slivers_vial.profile.json' --games 1 --seed 30153079 --game-index 153079`
- **vampires** x2 -- T5 m1 `{land=Voldaren Estate + vial:Bloodline Keeper + cast:Falkenrath Pit Fighter + act:Bloodline Keeper(x1) + act:Voldaren Estate(x1)}`  
  22 oracle outcomes, none covers the engine's -- nearest pay1[Voldaren Estate#59:R(creature-only)] ; pay2[(float only)] ; pay3[Swamp#43:B]: Voldaren Estate#57 tapped by engine only; Blood Token#1002 only in engine; float engine - oracle -; engine ended board=[Voldaren Estate#57(T), Aether Vial#1(T), Voldaren Estate#58(T), Legion Lieutenant#24, Vampire of the Dire Moon#54, Legion Lieutenant#25, Swamp#43(T), Captivating Vampire#13, Voldaren Estate#59(T)(new), Lord of Lineage#12(new), Falkenrath Pit Fighter#20(new), Blood Token#1001(new), Blood Token#1002(new)] hand=[] float=- life=22  
  board: board=[Voldaren Estate#57, Aether Vial#1, Voldaren Estate#58, Legion Lieutenant#24, Vampire of the Dire Moon#54, Legion Lieutenant#25, Swamp#43, Captivating Vampire#13] hand=[Bloodline Keeper, Falkenrath Pit Fighter, Voldaren Estate] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30137766 --game-index 137766`
- **vampires** x2 -- T5 m1 `{land=Voldaren Estate + vial:Bloodline Keeper + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1)}`  
  17 oracle outcomes, none covers the engine's -- nearest pay1[Voldaren Estate#59:R(creature-only)] ; pay2[(float only)]: Voldaren Estate#57 tapped by engine only; Swamp#43 tapped by engine only; Blood Token#1002 only in engine; float engine - oracle -; engine ended board=[Voldaren Estate#57(T), Aether Vial#1(T), Voldaren Estate#58(T), Legion Lieutenant#24, Vampire of the Dire Moon#54, Legion Lieutenant#25, Swamp#43(T), Captivating Vampire#13, Voldaren Estate#59(T)(new), Lord of Lineage#12(new), Falkenrath Pit Fighter#20(new), Blood Token#1001(new), Blood Token#1002(new)] hand=[] float=- life=22  
  board: board=[Voldaren Estate#57, Aether Vial#1, Voldaren Estate#58, Legion Lieutenant#24, Vampire of the Dire Moon#54, Legion Lieutenant#25, Swamp#43, Captivating Vampire#13] hand=[Bloodline Keeper, Falkenrath Pit Fighter, Voldaren Estate] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30137766 --game-index 137766`
- **vampires** x2 -- T5 m1 `{land=Voldaren Estate + vial:Bloodline Keeper + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1)}`  
  19 oracle outcomes, none covers the engine's -- nearest pay1[Voldaren Estate#59:R(creature-only)] ; pay2[(float only)]: Voldaren Estate#57 tapped by engine only; Swamp#43 tapped by engine only; Voldaren Estate#59 tapped by oracle only; float engine - oracle -; engine ended board=[Voldaren Estate#57(T), Aether Vial#1(T), Voldaren Estate#58(T), Legion Lieutenant#24, Vampire of the Dire Moon#54, Legion Lieutenant#25, Swamp#43(T), Captivating Vampire#13, Voldaren Estate#59(new), Lord of Lineage#12(new), Falkenrath Pit Fighter#20(new), Blood Token#1001(new)] hand=[] float=- life=22  
  board: board=[Voldaren Estate#57, Aether Vial#1, Voldaren Estate#58, Legion Lieutenant#24, Vampire of the Dire Moon#54, Legion Lieutenant#25, Swamp#43, Captivating Vampire#13] hand=[Bloodline Keeper, Falkenrath Pit Fighter, Voldaren Estate] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30137766 --game-index 137766`
- **vampires** x2 -- T5 m1 `{no land + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Blood Token(x1)}`  
  12 oracle outcomes, none covers the engine's -- nearest pay1[Blackcleave Cliffs#8:R] ; pay2[(float only)] ; pay3[Voldaren Estate#57:C] ; pay4[(float only)]: Blackcleave Cliffs#8 tapped by oracle only; Swamp#47 tapped by engine only; life engine 16 oracle 17; same core; float engine - oracle -; engine ended board=[Blackcleave Cliffs#8, Falkenrath Pit Fighter#21, Voldaren Estate#57(T), Asylum Visitor#5, Voldaren Estate#58(T), Captivating Vampire#13, Swamp#47(T), Vampire of the Dire Moon#54, Stromkirk Condemned#41, Falkenrath Pit Fighter#22(new), Blood Token#1001(new)] hand=[] float=- life=16  
  board: board=[Blackcleave Cliffs#8, Falkenrath Pit Fighter#21, Voldaren Estate#57, Asylum Visitor#5, Voldaren Estate#58, Captivating Vampire#13, Swamp#47, Vampire of the Dire Moon#54, Stromkirk Condemned#41, Blood Token#1000] hand=[Swamp, Falkenrath Pit Fighter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30166044 --game-index 166044`
- ... 52 more signatures in flags.jsonl

## UNMODELED (29 flags, 19 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **vampires** x7 -- T5 m1 `{land=Voldaren Estate + vial:Stromkirk Condemned + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + act:Voldaren Estate(x1)}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Gorger#16, Voldaren Estate#58, Legion Lieutenant#24, Unclaimed Territory#49, Aether Vial#1, Unclaimed Territory#50, Bloodline Keeper#11] hand=[Voldaren Estate, Stromkirk Condemned, Falkenrath Pit Fighter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30111212 --game-index 111212`
- **vampires** x3 -- T5 m1 `{no land + cast:Markov Baron + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Voldaren Estate (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#59, Aether Vial#3, Voldaren Estate#57, Stromkirk Condemned#42, Unclaimed Territory#51, Legion Lieutenant#25, Voldaren Estate#60, Bloodline Keeper#12] hand=[Markov Baron] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30147824 --game-index 147824`
- **vampires** x2 -- T5 m1 `{land=Voldaren Estate + vial:Stromkirk Condemned + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + act:Voldaren Estate(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Gorger#16, Voldaren Estate#58, Legion Lieutenant#24, Unclaimed Territory#49, Aether Vial#1, Unclaimed Territory#50, Bloodline Keeper#11] hand=[Voldaren Estate, Stromkirk Condemned, Falkenrath Pit Fighter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30111212 --game-index 111212`
- **vampires** x2 -- T5 m1 `{land=Voldaren Estate + cast:Aether Vial + cast:Falkenrath Gorger + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + act:Voldaren Estate(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Pit Fighter#20, Voldaren Estate#58, Markov Baron#28, Unclaimed Territory#49, Stromkirk Captain#36, Unclaimed Territory#50, Bloodline Keeper#11] hand=[Aether Vial, Swamp, Falkenrath Gorger, Voldaren Estate] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30133363 --game-index 133363`
- **vampires** x1 -- T5 m1 `{land=Voldaren Estate + vial:Stromkirk Condemned + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + act:Voldaren Estate(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Gorger#16, Voldaren Estate#58, Legion Lieutenant#24, Unclaimed Territory#49, Aether Vial#1, Unclaimed Territory#50, Bloodline Keeper#11] hand=[Voldaren Estate, Stromkirk Condemned, Falkenrath Pit Fighter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30111212 --game-index 111212`
- **vampires** x1 -- T5 m1 `{land=Voldaren Estate + vial:Stromkirk Condemned + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Gorger#16, Voldaren Estate#58, Legion Lieutenant#24, Unclaimed Territory#49, Aether Vial#1, Unclaimed Territory#50, Bloodline Keeper#11] hand=[Voldaren Estate, Stromkirk Condemned, Falkenrath Pit Fighter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30111212 --game-index 111212`
- **vampires** x1 -- T5 m1 `{no land + vial:Stromkirk Condemned + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Gorger#16, Voldaren Estate#58, Legion Lieutenant#24, Unclaimed Territory#49, Aether Vial#1, Unclaimed Territory#50, Bloodline Keeper#11] hand=[Voldaren Estate, Stromkirk Condemned, Falkenrath Pit Fighter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30111212 --game-index 111212`
- **vampires** x1 -- T5 m1 `{land=Voldaren Estate + vial:Stromkirk Condemned + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + act:Voldaren Estate(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Gorger#16, Voldaren Estate#58, Legion Lieutenant#24, Unclaimed Territory#49, Aether Vial#1, Unclaimed Territory#50, Bloodline Keeper#11] hand=[Voldaren Estate, Stromkirk Condemned, Falkenrath Pit Fighter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30111212 --game-index 111212`
- **vampires** x1 -- T5 m1 `{land=Voldaren Estate + cast:Falkenrath Pit Fighter + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + act:Voldaren Estate(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Gorger#16, Voldaren Estate#58, Legion Lieutenant#24, Unclaimed Territory#49, Aether Vial#1, Unclaimed Territory#50, Bloodline Keeper#11] hand=[Voldaren Estate, Stromkirk Condemned, Falkenrath Pit Fighter] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30111212 --game-index 111212`
- **vampires** x1 -- T5 m1 `{no land + vial:Legion Lieutenant + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Gorger#16, Voldaren Estate#58, Vampire of the Dire Moon#54, Markov Baron#28, Unclaimed Territory#49, Aether Vial#1, Voldaren Estate#59, Bloodline Keeper#11] hand=[Swamp, Swamp, Legion Lieutenant] float=- life=25  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30117085 --game-index 117085`
- **vampires** x1 -- T5 m1 `{land=Voldaren Estate + cast:Aether Vial + cast:Falkenrath Gorger + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Pit Fighter#20, Voldaren Estate#58, Markov Baron#28, Unclaimed Territory#49, Stromkirk Captain#36, Unclaimed Territory#50, Bloodline Keeper#11] hand=[Aether Vial, Swamp, Falkenrath Gorger, Voldaren Estate] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30133363 --game-index 133363`
- **vampires** x1 -- T5 m1 `{land=Voldaren Estate + cast:Falkenrath Gorger + act:Voldaren Estate(x1) + act:Voldaren Estate(x1) + act:Bloodline Keeper(x1) + act:Voldaren Estate(x1) + tapfortokenpay:Bloodline Keeper}`  
  engine dropped [cast Bloodline Keeper (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Voldaren Estate#57, Falkenrath Pit Fighter#20, Voldaren Estate#58, Markov Baron#28, Unclaimed Territory#49, Stromkirk Captain#36, Unclaimed Territory#50, Bloodline Keeper#11] hand=[Aether Vial, Swamp, Falkenrath Gorger, Voldaren Estate] float=- life=23  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Vampires/Vampire.cod' --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30133363 --game-index 133363`
- ... 7 more signatures in flags.jsonl

