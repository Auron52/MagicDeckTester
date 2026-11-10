# Mana-clean gate `lastweek-20261110-fast`

Mana-clean gate run: 4 decks, games (seed 30000000 + game index) kithkin 230000-234999, merfolk 110000-114999, jumbo_cactuar 10000-14999, shadowborn_apostle 10000-14999; src `8ec9533150e1`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | DOMINATED-LIFE | ORACLE-GAP | UNMODELED | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| kithkin | 5000 | clean | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3294 | 224 | 0 | 0 |
| merfolk | 5000 | issues | 15 | 0 | 15 | 0 | 0 | 57 | 0 | 0 | 2758 | 0 | 96 | 69 | 0 |
| jumbo_cactuar | 5000 | issues | 2948 | 529 | 421 | 992 | 613 | 0 | 291 | 102 | 17465 | 4159 | 19 | 155307 | 15 |
| shadowborn_apostle | 5000 | issues | 27015 | 3 | 7 | 108 | 0 | 0 | 23183 | 3714 | 23314 | 116521 | 2488 | 7255 | 0 |

**MISS = 2688, ORACLE-GAP = 5291.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=156181 plans=2011766 skipped=0 applies=665163841 frontiers=540362974 pruned=119524706 options=317071478 engine_drops=58299 gate_rejects=26912595(checked 6774661, skipped 20137934) MISS=2745 ORACLE-GAP=23474 UNMODELED=3816 CAPPED=0 ms=10899959 ===
=== MANA ORACLE classes: ORACLE-GAP=23474 ENUM-GAP=532 APPLY-DROP=443 APPLY-DROP-ORDER=1100 PHANTOM=43537 DOMINATED-CREATURE=613 DOMINATED-LIFE=57 DOMINATED-LAND=162631 CONTINUATION-DIFFERS=123974 DROP-COVERED=2827 UNMODELED=3816 HIDDEN-INFO=15 ===
=== MANA ORACLE enum-gap rejections: noop=556 hidden=95 host_blind=250 cont_land=18 ===
=== MANA ORACLE capped-plan escalation: 462 retried, 462 finished ===
=== MANA ORACLE unmodeled: 225 x floating pool too large to enumerate set-asides ===
=== MANA ORACLE unmodeled: 2493138 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 3816 x too many actions to try every order ===
=== MANA ORACLE rollback census: lost=2075 depth0=0 depth1=308 depth2=85 depth3+=50 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=1100 off-menu=532 need_in_plan=360 need_later=83 pay_rollback_rescued=0/443 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | ORACLE-GAP | UNMODELED |
|---|---|---|---|---|---|---|---|---|
| kithkin | 40191 | 290267 | 0 | 0 | 0 | 0 | 0 | 0 |
| merfolk | 36987 | 571323 | 0 | 15 | 0 | 0 | 0 | 0 |
| jumbo_cactuar | 35968 | 746549 | 529 | 421 | 992 | 613 | 291 | 102 |
| shadowborn_apostle | 43035 | 403627 | 3 | 7 | 108 | 0 | 5000 | 3714 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth1 | depth2 | depth3+ | reorder | off-menu | need-later |
|---|---|---|---|---|---|---|
| merfolk | 15 | 0 | 0 | 0 | 0 | 7 |
| jumbo_cactuar | 289 | 85 | 47 | 992 | 529 | 76 |
| shadowborn_apostle | 4 | 0 | 3 | 108 | 3 | 0 |

## ENUM-GAP (532 flags, 191 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **jumbo_cactuar** x38 -- T3 m1 `{land=Forest + cast:Tooth and Nail + cast:Surrak and Goreclaw + cast:Arbor Elf}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#4:G Forest#10:G] ; pay2[(float only)] ; pay3[Forest#27:G Forest#17:G Arbor Elf#2:G]  
  board: board=[Forest#27, Arbor Elf#4, Forest#10, Utopia Sprawl#52@10, Overgrowth#40@10] hand=[Tooth and Nail, Surrak and Goreclaw, Forest, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013505 --game-index 13505`
- **jumbo_cactuar** x26 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Arbor Elf}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Forest#11:G Arbor Elf#1:G]  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Overgrowth#39@10, Forest#12, Surrak and Goreclaw#43] hand=[Tooth and Nail, Overgrowth, Chariot of Victory, Voyaging Satyr, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013857 --game-index 13857`
- **jumbo_cactuar** x20 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Voyaging Satyr + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Wild Growth@10 + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G] ; pay3[(float only)] ; pay4[Forest#10:G] ; pay5[(float only)] ; pay6[Voyaging Satyr#54:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Utopia Sprawl#50@10, Arbor Elf#1] hand=[Tooth and Nail, Voyaging Satyr, Overgrowth, Chariot of Victory, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010012 --game-index 10012`
- **jumbo_cactuar** x20 -- T4 m1 `{no land + cast:Overgrowth@10 + cast:Tooth and Nail + cast:Lightning Greaves + cast:Voyaging Satyr + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Forest#11:G Voyaging Satyr#53:G]  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Utopia Sprawl#49@10, Arbor Elf#1, Jumbo Cactuar#33] hand=[Overgrowth, Tooth and Nail, Lightning Greaves, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010110 --game-index 10110`
- **jumbo_cactuar** x20 -- T3 m1 `{no land + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Tooth and Nail + cast:Arbor Elf + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#11:G Voyaging Satyr#53:G] ; pay2[Arbor Elf#1:G] ; pay3[(float only)] ; pay4[Forest#10:G] ; pay5[Arbor Elf#2:G]  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Arbor Elf#1, Voyaging Satyr#53] hand=[Overgrowth, Chariot of Victory, Tooth and Nail, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30012692 --game-index 12692`
- **jumbo_cactuar** x20 -- T3 m1 `{land=Forest + cast:Tooth and Nail + cast:Chariot of Victory + cast:Voyaging Satyr + cast:Wild Growth@10 + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#12:G] ; pay2[Voyaging Satyr#53:G] ; pay3[Forest#11:G] ; pay4[Forest#10:G] ; pay5[Voyaging Satyr#54:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Utopia Sprawl#50@10, Voyaging Satyr#53] hand=[Tooth and Nail, Forest, Chariot of Victory, Voyaging Satyr, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30014059 --game-index 14059`
- **jumbo_cactuar** x19 -- T4 m1 `{no land + cast:Surrak and Goreclaw + cast:Tooth and Nail + cast:Voyaging Satyr}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#11:G Arbor Elf#1:G] ; pay2[Forest#10:G] ; pay3[Voyaging Satyr#54:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Overgrowth#39@10, Wild Growth#57@10, Arbor Elf#1, Chariot of Victory#5] hand=[Surrak and Goreclaw, Tooth and Nail, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30011872 --game-index 11872`
- **jumbo_cactuar** x14 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Voyaging Satyr + cast:Chariot of Victory + cast:Wild Growth@10 + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Forest#10:G Voyaging Satyr#54:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Utopia Sprawl#50@10, Arbor Elf#1] hand=[Tooth and Nail, Voyaging Satyr, Overgrowth, Chariot of Victory, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010012 --game-index 10012`
- **jumbo_cactuar** x14 -- T4 m1 `{land=Forest + cast:Chariot of Victory + cast:Tooth and Nail + cast:Overgrowth@14 + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#2:G] ; pay2[Forest#21:G] ; pay3[Forest#14:G] ; pay4[(float only)] ; pay5[Voyaging Satyr#55:G]  
  board: board=[Forest#14, Wild Growth#59@14, Arbor Elf#2, Overgrowth#39@14] hand=[Chariot of Victory, Tooth and Nail, Overgrowth, Jumbo Cactuar, Voyaging Satyr, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010681 --game-index 10681`
- **jumbo_cactuar** x14 -- T4 m1 `{no land + cast:Chariot of Victory + cast:Tooth and Nail + cast:Overgrowth@14 + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#2:G] ; pay2[Forest#14:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Voyaging Satyr#55:G]  
  board: board=[Forest#14, Wild Growth#59@14, Arbor Elf#2, Overgrowth#39@14] hand=[Chariot of Victory, Tooth and Nail, Overgrowth, Jumbo Cactuar, Voyaging Satyr, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010681 --game-index 10681`
- **jumbo_cactuar** x13 -- T3 m1 `{no land + cast:Tooth and Nail + cast:Surrak and Goreclaw + cast:Voyaging Satyr + cast:Arbor Elf}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#3:G Forest#10:G] ; pay2[(float only)] ; pay3[Arbor Elf#1:G] ; pay4[Voyaging Satyr#54:G]  
  board: board=[Forest#28, Arbor Elf#3, Forest#10, Wild Growth#58@10, Overgrowth#42@10] hand=[Tooth and Nail, Surrak and Goreclaw, Voyaging Satyr, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30014251 --game-index 14251`
- **jumbo_cactuar** x11 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Voyaging Satyr + cast:Overgrowth@10 + cast:Chariot of Victory + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G] ; pay3[(float only)] ; pay4[Forest#10:G] ; pay5[Voyaging Satyr#54:G keep-floating {W}{W}]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Utopia Sprawl#50@10, Arbor Elf#1] hand=[Tooth and Nail, Voyaging Satyr, Overgrowth, Chariot of Victory, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010012 --game-index 10012`
- ... 179 more signatures in flags.jsonl

## APPLY-DROP (443 flags, 84 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **jumbo_cactuar** x40 -- T3 m1 `{land=Forest + cast:Green Sun's Zenith(x6) + cast:Finale of Devastation + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#11:G] ; pay2[Forest#10:G Arbor Elf#1:G Voyaging Satyr#53:G] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Forest#10:G Arbor Elf#1:G Voyaging Satyr#53:G] ; pay3[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53] hand=[Green Sun's Zenith, Finale of Devastation, Wild Growth, Utopia Sprawl, Jumbo Cactuar, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010653 --game-index 10653`
- **jumbo_cactuar** x40 -- T3 m1 `{no land + cast:Finale of Devastation(x1) + cast:Tooth and Nail}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Arbor Elf#1:G Forest#11:G Voyaging Satyr#53:G Forest#10:G] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Arbor Elf#1:G Forest#11:G Voyaging Satyr#53:G Forest#10:G] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Utopia Sprawl#50@10, Voyaging Satyr#53] hand=[Finale of Devastation, Tooth and Nail, Chariot of Victory, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013727 --game-index 13727`
- **jumbo_cactuar** x20 -- T3 m1 `{land=Forest + cast:Green Sun's Zenith(x1) + cast:Green Sun's Zenith(x1) + cast:Finale of Devastation}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G Forest#12:G] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Voyaging Satyr#53:G] ; pay3[Forest#11:G Forest#12:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53] hand=[Green Sun's Zenith, Green Sun's Zenith, Jumbo Cactuar, Surrak and Goreclaw, Finale of Devastation, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010761 --game-index 10761`
- **jumbo_cactuar** x15 -- T4 m1 `{no land + cast:Finale of Devastation(x1) + cast:Overgrowth@15 + cast:Utopia Sprawl(x1)@17}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#17:G] ; pay2[(float only)] ; pay3[Forest#15:G] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Forest#17:G] ; pay2[(float only)] ; pay3[Forest#15:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#17, Utopia Sprawl#51@17, Forest#15, Overgrowth#42@17] hand=[Tooth and Nail, Finale of Devastation, Overgrowth, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30011271 --game-index 11271`
- **jumbo_cactuar** x15 -- T3 m1 `{land=Forest + cast:Overgrowth@11 + cast:Finale of Devastation(x2) + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#12:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Forest#12:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Overgrowth#39@10] hand=[Overgrowth, Finale of Devastation, Jumbo Cactuar, Forest, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30012125 --game-index 12125`
- **jumbo_cactuar** x15 -- T3 m1 `{no land + cast:Overgrowth@11 + cast:Finale of Devastation(x1) + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Forest#11:G] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Forest#11:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Overgrowth#39@10] hand=[Overgrowth, Finale of Devastation, Jumbo Cactuar, Forest, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30012125 --game-index 12125`
- **jumbo_cactuar** x12 -- T4 m1 `{land=Forest + cast:Tooth and Nail(x2) + cast:Finale of Devastation}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Arbor Elf#1:G Arbor Elf#2:G Voyaging Satyr#53:G Voyaging Satyr#54:G Forest#11:G Forest#10:G] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Voyaging Satyr#54:G keep-floating {G}] ; pay3[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Arbor Elf#2, Voyaging Satyr#54] hand=[Jumbo Cactuar, Tooth and Nail, Finale of Devastation, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30011450 --game-index 11450`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Finale of Devastation + cast:Green Sun's Zenith(x1) + cast:Voyaging Satyr}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Voyaging Satyr#54:G] ; pay2[Forest#20:G] ; pay3[Forest#19:G Forest#23:G] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Voyaging Satyr#54:G] ; pay3[Forest#19:G Forest#23:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Utopia Sprawl#49@20, Forest#19, Voyaging Satyr#54] hand=[Finale of Devastation, Forest, Green Sun's Zenith, Voyaging Satyr, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30011779 --game-index 11779`
- **jumbo_cactuar** x10 -- T4 m1 `{no land + cast:Green Sun's Zenith(x6) + cast:Finale of Devastation}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Voyaging Satyr#54:G Voyaging Satyr#53:G Voyaging Satyr#56:G Forest#16:G Forest#26:G] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Voyaging Satyr#54:G Voyaging Satyr#53:G Voyaging Satyr#56:G Forest#16:G Forest#26:G] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#26, Utopia Sprawl#51@26, Voyaging Satyr#54, Forest#16, Voyaging Satyr#53, Voyaging Satyr#56] hand=[Green Sun's Zenith, Finale of Devastation, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30012202 --game-index 12202`
- **jumbo_cactuar** x10 -- T4 m1 `{no land + cast:Arbor Elf + cast:Finale of Devastation + cast:Green Sun's Zenith(x6)}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#21:G] ; pay2[Forest#10:G Arbor Elf#2:G Arbor Elf#1:G] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Arbor Elf#2:G Arbor Elf#1:G] ; pay3[Forest#21:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Arbor Elf#2, Forest#21, Utopia Sprawl#50@10, Surrak and Goreclaw#43] hand=[Arbor Elf, Chariot of Victory, Finale of Devastation, Green Sun's Zenith, Overgrowth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30012343 --game-index 12343`
- **jumbo_cactuar** x10 -- T4 m1 `{land=Forest + cast:Green Sun's Zenith(x6) + cast:Finale of Devastation}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Arbor Elf#1:G Voyaging Satyr#53:G Voyaging Satyr#54:G Forest#11:G Forest#10:G] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Arbor Elf#1:G Voyaging Satyr#53:G Voyaging Satyr#54:G Forest#11:G Forest#10:G] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Voyaging Satyr#54, Chariot of Victory#5] hand=[Green Sun's Zenith, Overgrowth, Finale of Devastation, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30012638 --game-index 12638`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Green Sun's Zenith(x1) + cast:Finale of Devastation + cast:Green Sun's Zenith(x1)}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G Forest#12:G] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Voyaging Satyr#53:G] ; pay3[Forest#11:G Forest#12:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53] hand=[Green Sun's Zenith, Finale of Devastation, Jumbo Cactuar, Green Sun's Zenith, Green Sun's Zenith, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30014985 --game-index 14985`
- ... 72 more signatures in flags.jsonl

## APPLY-DROP-ORDER (1100 flags, 156 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **jumbo_cactuar** x160 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Finale of Devastation + cast:Overgrowth@10 + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Finale of Devastation]; the order Overgrowth > Overgrowth > Utopia Sprawl > Tooth and Nail > Finale of Devastation pays every action with pay1[Voyaging Satyr#53:G] ; pay2[Voyaging Satyr#54:G] ; pay3[(float only)] ; pay4[Forest#10:G] ; pay5[(float only)]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Wild Growth#57@10, Voyaging Satyr#54] hand=[Tooth and Nail, Overgrowth, Finale of Devastation, Overgrowth, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013421 --game-index 13421`
- **jumbo_cactuar** x120 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Finale of Devastation(x1) + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Finale of Devastation]; the order Overgrowth > Finale of Devastation > Utopia Sprawl > Tooth and Nail pays every action with pay1[Voyaging Satyr#53:G] ; pay2[Voyaging Satyr#54:G] ; pay3[(float only)] ; pay4[Forest#10:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Wild Growth#57@10, Voyaging Satyr#54] hand=[Tooth and Nail, Overgrowth, Finale of Devastation, Overgrowth, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013421 --game-index 13421`
- **jumbo_cactuar** x100 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Tooth and Nail]; the order Overgrowth > Chariot of Victory > Utopia Sprawl > Tooth and Nail pays every action with pay1[Voyaging Satyr#53:G] ; pay2[Voyaging Satyr#54:G] ; pay3[(float only)] ; pay4[Forest#10:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Wild Growth#57@10, Voyaging Satyr#54] hand=[Tooth and Nail, Overgrowth, Finale of Devastation, Overgrowth, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013421 --game-index 13421`
- **jumbo_cactuar** x40 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Finale of Devastation + cast:Overgrowth@10 + cast:Utopia Sprawl(x5)@10}`  
  engine dropped [cast Finale of Devastation]; the order Overgrowth > Overgrowth > Utopia Sprawl > Tooth and Nail > Finale of Devastation pays every action with pay1[Voyaging Satyr#53:G] ; pay2[Voyaging Satyr#54:G keep-floating {W}{G}] ; pay3[(float only)] ; pay4[Forest#10:G] ; pay5[(float only)]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Wild Growth#57@10, Voyaging Satyr#54] hand=[Tooth and Nail, Overgrowth, Finale of Devastation, Overgrowth, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013421 --game-index 13421`
- **jumbo_cactuar** x40 -- T4 m1 `{no land + cast:Green Sun's Zenith(x1) + cast:Utopia Sprawl(x2)@10 + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Chariot of Victory]; the order Green Sun's Zenith > Utopia Sprawl > Utopia Sprawl pays every action with pay1[Voyaging Satyr#53:G] ; pay2[(float only)] ; pay3[Forest#10:G] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[Arbor Elf#2:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Wild Growth#57@10, Nylea, God of the Hunt#38] hand=[Arbor Elf, Chariot of Victory, Green Sun's Zenith, Lightning Greaves, Utopia Sprawl, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30014045 --game-index 14045`
- **shadowborn_apostle** x36 -- T5 m2 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Thrumming Stone + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; the order Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent > act:Shadowborn Apostle(x1)>Abhorrent Overlord@28 pays every action with pay1[Swamp#50:B Swamp#42:B Lotus Bloom#17:W] ; pay2[Swamp#41:B] ; pay3[Swamp#43:B] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#50, Swamp#42, Swamp#41, Swamp#43, Lotus Bloom#17(new)] hand=[Diabolic Intent, Culling the Weak, Thrumming Stone, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30013493 --game-index 13493`
- **shadowborn_apostle** x36 -- T5 m2 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Thrumming Stone + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; the order Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent > act:Shadowborn Apostle(x1)>Jetmir, Nexus of Revels@28 pays every action with pay1[Swamp#50:B Swamp#42:B Lotus Bloom#17:W] ; pay2[Swamp#41:B] ; pay3[Swamp#43:B] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#50, Swamp#42, Swamp#41, Swamp#43, Lotus Bloom#17(new)] hand=[Diabolic Intent, Culling the Weak, Thrumming Stone, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30013493 --game-index 13493`
- **shadowborn_apostle** x24 -- T5 m2 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Thrumming Stone + cast:Shadowborn Apostle}`  
  engine dropped [cast Diabolic Intent]; the order Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Swamp#50:B Swamp#42:B Lotus Bloom#17:W] ; pay2[Swamp#41:B] ; pay3[Swamp#43:B] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#50, Swamp#42, Swamp#41, Swamp#43, Lotus Bloom#17(new)] hand=[Diabolic Intent, Culling the Weak, Thrumming Stone, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30013493 --game-index 13493`
- **jumbo_cactuar** x20 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Wild Growth@10}`  
  engine dropped [cast Tooth and Nail]; the order Overgrowth > Chariot of Victory > Wild Growth > Tooth and Nail pays every action with pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G] ; pay3[(float only)] ; pay4[Forest#10:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Utopia Sprawl#50@10, Arbor Elf#1] hand=[Tooth and Nail, Voyaging Satyr, Overgrowth, Chariot of Victory, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010012 --game-index 10012`
- **jumbo_cactuar** x20 -- T3 m1 `{no land + cast:Utopia Sprawl(x1)@10 + cast:Overgrowth@10 + cast:Green Sun's Zenith(x1)}`  
  engine dropped [cast Green Sun's Zenith]; the order Overgrowth > Utopia Sprawl > Green Sun's Zenith pays every action with pay1[Arbor Elf#1:G] ; pay2[Forest#10:G] ; pay3[(float only)]  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Utopia Sprawl#50@10] hand=[Utopia Sprawl, Overgrowth, Green Sun's Zenith, Jumbo Cactuar, Chariot of Victory] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30012632 --game-index 12632`
- **jumbo_cactuar** x20 -- T4 m1 `{no land + cast:Green Sun's Zenith(x1) + cast:Utopia Sprawl(x1)@10 + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Chariot of Victory]; the order Utopia Sprawl > Green Sun's Zenith > Utopia Sprawl pays every action with pay1[Voyaging Satyr#53:G] ; pay2[(float only)] ; pay3[Forest#10:G] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[Arbor Elf#2:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Wild Growth#57@10, Nylea, God of the Hunt#38] hand=[Arbor Elf, Chariot of Victory, Green Sun's Zenith, Lightning Greaves, Utopia Sprawl, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30014045 --game-index 14045`
- **jumbo_cactuar** x19 -- T2 m1 `{land=Forest + cast:Lightning Greaves + cast:Green Sun's Zenith(x1) + cast:Arbor Elf + equip:Lightning Greaves}`  
  engine dropped [cast Green Sun's Zenith]; the order Lightning Greaves > Arbor Elf > Green Sun's Zenith > equip:Lightning Greaves@37 pays every action with pay1[Forest#17:G] ; pay2[Forest#24:G] ; pay3[(float only)] ; pay4[Arbor Elf#1:G] ; pay5[(float only)]  
  board: board=[Forest#17, Utopia Sprawl#51@17] hand=[Lightning Greaves, Green Sun's Zenith, Green Sun's Zenith, Forest, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010127 --game-index 10127`
- ... 144 more signatures in flags.jsonl

## DOMINATED-CREATURE (613 flags, 189 signatures)

_the engine's payment taps a creature that could attack, where another payment keeps it untapped and loses nothing._

- **jumbo_cactuar** x30 -- T3 m1 `{land=Forest + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Arbor Elf#1:G Forest#11:G Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Arbor Elf#2(T), Voyaging Satyr#53(T), Forest#12(T)(new), Utopia Sprawl#49(new)@10] hand=[Forest] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Arbor Elf#2, Voyaging Satyr#53] hand=[Tooth and Nail, Forest, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010093 --game-index 10093`
- **jumbo_cactuar** x25 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G Voyaging Satyr#54:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Voyaging Satyr#53(T), Utopia Sprawl#49@10, Arbor Elf#2(T), Voyaging Satyr#54(T), Utopia Sprawl#52(new)@10] hand=[Jumbo Cactuar, Surrak and Goreclaw] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Voyaging Satyr#53, Utopia Sprawl#49@10, Arbor Elf#2, Voyaging Satyr#54] hand=[Tooth and Nail, Jumbo Cactuar, Surrak and Goreclaw, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013388 --game-index 13388`
- **jumbo_cactuar** x20 -- T4 m1 `{no land + cast:Tooth and Nail(x1) + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G Voyaging Satyr#54:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Voyaging Satyr#53(T), Utopia Sprawl#49@10, Arbor Elf#2(T), Voyaging Satyr#54(T), Utopia Sprawl#52(new)@10] hand=[Jumbo Cactuar, Surrak and Goreclaw] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Voyaging Satyr#53, Utopia Sprawl#49@10, Arbor Elf#2, Voyaging Satyr#54] hand=[Tooth and Nail, Jumbo Cactuar, Surrak and Goreclaw, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013388 --game-index 13388`
- **jumbo_cactuar** x15 -- T3 m1 `{no land + cast:Chariot of Victory + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#11:G] ; pay2[Arbor Elf#1:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Arbor Elf#2(T), Utopia Sprawl#49(new)@10, Chariot of Victory#5(new)] hand=[Tooth and Nail, Forest, Forest] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Arbor Elf#2] hand=[Tooth and Nail, Chariot of Victory, Forest, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010071 --game-index 10071`
- **jumbo_cactuar** x15 -- T3 m1 `{land=Forest + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Arbor Elf#1:G Arbor Elf#3:G Forest#11:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Arbor Elf#3(T), Arbor Elf#2(T), Forest#12(T)(new), Utopia Sprawl#51(new)@10] hand=[] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Arbor Elf#3, Arbor Elf#2] hand=[Tooth and Nail, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010796 --game-index 10796`
- **jumbo_cactuar** x15 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@14}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Arbor Elf#1:G Forest#17:G] ; pay2[Arbor Elf#2:G]; the engine ended board=[Forest#14, Arbor Elf#1(T), Forest#17(T), Arbor Elf#2(T), Arbor Elf#4, Overgrowth#41@14, Arbor Elf#3(T), Utopia Sprawl#49(new)@14] hand=[Jumbo Cactuar] float={G}{G} life=20  
  board: board=[Forest#14, Arbor Elf#1, Forest#17, Arbor Elf#2, Arbor Elf#4, Overgrowth#41@14, Arbor Elf#3] hand=[Jumbo Cactuar, Tooth and Nail, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30011541 --game-index 11541`
- **jumbo_cactuar** x15 -- T3 m1 `{no land + cast:Overgrowth@10 + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#11:G] ; pay2[Arbor Elf#1:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Arbor Elf#2(T), Voyaging Satyr#53, Utopia Sprawl#50(new)@10, Overgrowth#39(new)@10] hand=[Jumbo Cactuar, Chariot of Victory] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Arbor Elf#2, Voyaging Satyr#53] hand=[Jumbo Cactuar, Overgrowth, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013307 --game-index 13307`
- **jumbo_cactuar** x15 -- T4 m1 `{no land + cast:Tooth and Nail(x2) + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Arbor Elf#1:G Forest#10:G] ; pay2[Voyaging Satyr#53:G Voyaging Satyr#54:G]; the engine ended board=[Forest#10(T), Arbor Elf#1(T), Voyaging Satyr#53(T), Utopia Sprawl#49@10, Arbor Elf#2(T), Voyaging Satyr#54(T), Utopia Sprawl#52(new)@10, Jumbo Cactuar#33(new), Surrak and Goreclaw#43(new)] hand=[Nylea, God of the Hunt, Surrak and Goreclaw] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Voyaging Satyr#53, Utopia Sprawl#49@10, Arbor Elf#2, Voyaging Satyr#54] hand=[Tooth and Nail, Jumbo Cactuar, Surrak and Goreclaw, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30013388 --game-index 13388`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Tooth and Nail(x1) + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Arbor Elf#1:G Forest#11:G Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Arbor Elf#2(T), Voyaging Satyr#53(T), Forest#12(T)(new), Utopia Sprawl#49(new)@10] hand=[Forest] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Arbor Elf#2, Voyaging Satyr#53] hand=[Tooth and Nail, Forest, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010093 --game-index 10093`
- **jumbo_cactuar** x10 -- T5 m1 `{no land + cast:Chariot of Victory + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Arbor Elf#1:G] ; pay2[Forest#11:G Arbor Elf#2:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Arbor Elf#2(T), Voyaging Satyr#53, Voyaging Satyr#54, Arbor Elf#3(T), Chariot of Victory#5, Utopia Sprawl#50(new)@10, Chariot of Victory#6(new)] hand=[Tooth and Nail, Tooth and Nail] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Arbor Elf#2, Voyaging Satyr#53, Voyaging Satyr#54, Arbor Elf#3, Chariot of Victory#5] hand=[Tooth and Nail, Tooth and Nail, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010245 --game-index 10245`
- **jumbo_cactuar** x10 -- T3 m1 `{no land + cast:Chariot of Victory + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Arbor Elf#1:G] ; pay2[Forest#11:G Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Arbor Elf#2(T), Voyaging Satyr#53(T), Utopia Sprawl#49(new)@10, Chariot of Victory#5(new)] hand=[Tooth and Nail, Forest, Overgrowth] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Arbor Elf#2, Voyaging Satyr#53] hand=[Chariot of Victory, Tooth and Nail, Forest, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010459 --game-index 10459`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Utopia Sprawl(x1)@10 + cast:Chariot of Victory}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#15:G] ; pay2[Arbor Elf#3:G]; the engine ended board=[Forest#10, Arbor Elf#3(T), Utopia Sprawl#51@10, Arbor Elf#2(T), Forest#15(T)(new), Utopia Sprawl#49(new)@10, Chariot of Victory#8(new)] hand=[Wild Growth, Tooth and Nail] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#3, Utopia Sprawl#51@10, Arbor Elf#2] hand=[Wild Growth, Tooth and Nail, Utopia Sprawl, Chariot of Victory, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30010685 --game-index 10685`
- ... 177 more signatures in flags.jsonl

## ORACLE-GAP (5291 flags, 449 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **shadowborn_apostle** x497 -- T4 m1 `{land=Swamp + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#42:B] ; pay2[Lake of the Dead#14:B]: Swamp#42 only in oracle; graveyard size differs; float engine {B}{B}{B} oracle -; engine ended board=[Shadowborn Apostle#21, Shadowborn Apostle#22, Lake of the Dead#14(T), Thrumming Stone#57, Shadowborn Apostle#37(new), Shadowborn Apostle#32(new), Shadowborn Apostle#34(new), Shadowborn Apostle#36(new), Shadowborn Apostle#35(new), Shadowborn Apostle#31(new), Shadowborn Apostle#33(new), Shadowborn Apostle#30(new), Shadowborn Apostle#38(new), Shadowborn Apostle#29(new), Abhorrent Overlord#1(new), 1/1 Harpy Token#1000(new), 1/1 Harpy Token#1001(new), 1/1 Harpy Token#1002(new), 1/1 Harpy Token#1  
  board: board=[Shadowborn Apostle#21, Shadowborn Apostle#22, Lake of the Dead#14, Thrumming Stone#57] hand=[Lake of the Dead, Swamp, Shadowborn Apostle, Demonic Tutor] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010001 --game-index 10001`
- **shadowborn_apostle** x327 -- T4 m1 `{land=Swamp + cast:Shadowborn Apostle}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#42:B]: Lake of the Dead#14 tapped by engine only; Swamp#42 only in oracle; graveyard size differs; float engine {B}{B}{B}{B} oracle -; engine ended board=[Shadowborn Apostle#21, Shadowborn Apostle#22, Lake of the Dead#14(T), Thrumming Stone#57, Shadowborn Apostle#37(new), Shadowborn Apostle#32(new), Shadowborn Apostle#24(new), Shadowborn Apostle#34(new), Shadowborn Apostle#23(new), Shadowborn Apostle#36(new), Shadowborn Apostle#27(new), Shadowborn Apostle#35(new), Shadowborn Apostle#31(new), Shadowborn Apostle#33(new), Shadowborn Apostle#30(new), Shadowborn Apostle#38(new), Shadowborn Apostle#28(new), Shadowborn Apostle#26(new  
  board: board=[Shadowborn Apostle#21, Shadowborn Apostle#22, Lake of the Dead#14, Thrumming Stone#57] hand=[Lake of the Dead, Swamp, Shadowborn Apostle, Demonic Tutor] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010001 --game-index 10001`
- **shadowborn_apostle** x248 -- T4 m2 `{no land + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#44:B] ; pay2[Lake of the Dead#16:B]: Swamp#44 only in oracle; graveyard size differs; float engine {B}{B}{B} oracle -; engine ended board=[Lake of the Dead#16(T), Thrumming Stone#59, Shadowborn Apostle#33(T), Shadowborn Apostle#31(T), Shadowborn Apostle#30(T), Shadowborn Apostle#37(T), Shadowborn Apostle#35(T), Shadowborn Apostle#27(T), Shadowborn Apostle#29(T), Shadowborn Apostle#32(T), Shadowborn Apostle#36(T), Shadowborn Apostle#28(T), Shadowborn Apostle#34(new), Abhorrent Overlord#1(new), 1/1 Harpy Token#1000(new), 1/1 Harpy Token#1001(new), 1/1 Harpy Token#1002(new), 1/1 Harpy Token#1003(new), 1/1 Harpy Token#1004(new), 1  
  board: board=[Lake of the Dead#16, Thrumming Stone#59, Shadowborn Apostle#33(T), Shadowborn Apostle#31(T), Shadowborn Apostle#30(T), Shadowborn Apostle#37(T), Shadowborn Apostle#23(T), Shadowborn Apostle#35(T), Shadowborn Apostle#27(T), Shadowborn Apostle#29(T), Shadowborn Apostle#21(T), Shadowborn Apostle#25(T), Shadowborn Apostle#24(T), Shadowborn Apostle#32(T), Shadowborn Apostle#22(T), Shadowborn Apo  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010005 --game-index 10005`
- **shadowborn_apostle** x233 -- T4 m2 `{no land + cast:Shadowborn Apostle}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#44:B]: Lake of the Dead#16 tapped by engine only; Swamp#44 only in oracle; graveyard size differs; float engine {B}{B}{B}{B} oracle -; engine ended board=[Lake of the Dead#16(T), Thrumming Stone#59, Shadowborn Apostle#33(T), Shadowborn Apostle#31(T), Shadowborn Apostle#30(T), Shadowborn Apostle#37(T), Shadowborn Apostle#23(T), Shadowborn Apostle#35(T), Shadowborn Apostle#27(T), Shadowborn Apostle#29(T), Shadowborn Apostle#21(T), Shadowborn Apostle#25(T), Shadowborn Apostle#24(T), Shadowborn Apostle#32(T), Shadowborn Apostle#22(T), Shadowborn Apostle#36(T), Shadowborn Apostle#28(T), Shadowborn Apostle#26(T), Shadowborn Apostle#  
  board: board=[Lake of the Dead#16, Thrumming Stone#59, Shadowborn Apostle#33(T), Shadowborn Apostle#31(T), Shadowborn Apostle#30(T), Shadowborn Apostle#37(T), Shadowborn Apostle#23(T), Shadowborn Apostle#35(T), Shadowborn Apostle#27(T), Shadowborn Apostle#29(T), Shadowborn Apostle#21(T), Shadowborn Apostle#25(T), Shadowborn Apostle#24(T), Shadowborn Apostle#32(T), Shadowborn Apostle#22(T), Shadowborn Apo  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010005 --game-index 10005`
- **shadowborn_apostle** x169 -- T5 m1 `{land=Swamp + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#44:B]: Lake of the Dead#16 tapped by engine only; Swamp#44 only in oracle; graveyard size differs; float engine {B}{B}{B}{B} oracle -; engine ended board=[Blood Crypt#2, Lake of the Dead#16(T), Thrumming Stone#58, Shadowborn Apostle#29, Shadowborn Apostle#27, Shadowborn Apostle#34, Shadowborn Apostle#35(new)] hand=[Shadowborn Apostle] float={B}{B}{B}{B} life=18  
  board: board=[Blood Crypt#2, Lake of the Dead#16, Thrumming Stone#58, Shadowborn Apostle#29, Shadowborn Apostle#27, Shadowborn Apostle#34] hand=[Shadowborn Apostle, Shadowborn Apostle, Swamp] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010113 --game-index 10113`
- **shadowborn_apostle** x107 -- T4 m1 `{land=Swamp + cast:Shadowborn Apostle + cast:Shadowborn Apostle}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#41:B] ; pay2[Lake of the Dead#14:B]: Swamp#41 only in oracle; graveyard size differs; float engine {B}{B}{B} oracle -; engine ended board=[Lake of the Dead#14(T), Thrumming Stone#57, Shadowborn Apostle#27, Shadowborn Apostle#22, Shadowborn Apostle#36(new), Shadowborn Apostle#29(new), Shadowborn Apostle#30(new), Shadowborn Apostle#35(new), Shadowborn Apostle#32(new), Shadowborn Apostle#33(new), Shadowborn Apostle#26(new), Shadowborn Apostle#25(new), Shadowborn Apostle#28(new), Shadowborn Apostle#24(new), Shadowborn Apostle#34(new), Shadowborn Apostle#23(new), Shadowborn Apostle#38(new), Shadowborn Apostle#37(new), Shadowborn Ap  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Shadowborn Apostle#27, Shadowborn Apostle#22] hand=[Profane Tutor, Swamp, Shadowborn Apostle, Shadowborn Apostle] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010008 --game-index 10008`
- **shadowborn_apostle** x106 -- T5 m1 `{no land + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#16:B] ; pay2[Swamp#41:B]: Swamp#41 only in oracle; graveyard size differs; float engine {B}{B}{B} oracle -; engine ended board=[Shadowborn Apostle#36, Lake of the Dead#16(T), Thrumming Stone#57, Shadowborn Apostle#32(new), Shadowborn Apostle#28(new), Shadowborn Apostle#30(new), Shadowborn Apostle#29(new), Shadowborn Apostle#33(new), Shadowborn Apostle#34(new), Shadowborn Apostle#35(new), Shadowborn Apostle#37(new), Shadowborn Apostle#27(new), Shadowborn Apostle#38(new), Abhorrent Overlord#1(new), 1/1 Harpy Token#1000(new), 1/1 Harpy Token#1001(new), 1/1 Harpy Token#1002(new), 1/1 Harpy Token#1003(new), 1/1 Harpy Tok  
  board: board=[Shadowborn Apostle#36, Swamp#41, Lake of the Dead#16, Thrumming Stone#57] hand=[Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010109 --game-index 10109`
- **shadowborn_apostle** x105 -- T5 m1 `{land=Swamp + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#49:B] ; pay2[Swamp#42:B]: Lake of the Dead#14 tapped by engine only; Swamp#49 tapped by oracle only; Swamp#42 only in oracle; graveyard size differs; float engine {B}{B}{B} oracle -; engine ended board=[Lake of the Dead#14(T), Thrumming Stone#57, Shadowborn Apostle#31, Shadowborn Apostle#24, Shadowborn Apostle#22, Swamp#49(new), Shadowborn Apostle#34(new), Shadowborn Apostle#35(new), Shadowborn Apostle#36(new), Shadowborn Apostle#37(new), Shadowborn Apostle#38(new), Shadowborn Apostle#32(new), Shadowborn Apostle#33(new), Shadowborn Apostle#30(new), Abhorrent Overlord#1(new), 1/1 Harpy Token#1000(new), 1/1 Harpy Token#1001(new)  
  board: board=[Lake of the Dead#14, Swamp#42, Thrumming Stone#57, Shadowborn Apostle#31, Shadowborn Apostle#24, Shadowborn Apostle#22] hand=[Reaper from the Abyss, Profane Tutor, Shadowborn Apostle, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010160 --game-index 10160`
- **shadowborn_apostle** x99 -- T4 m2 `{no land + cast:Shadowborn Apostle + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#41:B] ; pay2[Lake of the Dead#14:B]: Swamp#41 only in oracle; graveyard size differs; float engine {B}{B}{B} oracle -; engine ended board=[Lake of the Dead#14(T), Shadowborn Apostle#21(new), Shadowborn Apostle#22(new)] hand=[Sire of Insanity, Dark Ritual, Thrumming Stone, Profane Tutor, Profane Tutor] float={B}{B}{B} life=20  
  board: board=[Lake of the Dead#14, Swamp#41(new)] hand=[Shadowborn Apostle, Sire of Insanity, Dark Ritual, Thrumming Stone, Profane Tutor, Profane Tutor, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010014 --game-index 10014`
- **shadowborn_apostle** x96 -- T4 m2 `{no land + cast:Thrumming Stone + cast:Shadowborn Apostle + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  11 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#50:B] ; pay2[Lotus Bloom#17:W] ; pay3[(float only)] ; pay4[Lake of the Dead#14:B]: Lotus Bloom#17 only in engine; Swamp#50 only in oracle; float engine - oracle -; engine ended board=[Lake of the Dead#14(T), Lotus Bloom#17(new), Thrumming Stone#57(new), Shadowborn Apostle#38(new), Shadowborn Apostle#30(new), Shadowborn Apostle#29(new), Shadowborn Apostle#33(new), Shadowborn Apostle#35(new), Shadowborn Apostle#37(new), Shadowborn Apostle#31(new), Shadowborn Apostle#36(new), Shadowborn Apostle#32(new), Shadowborn Apostle#34(new), Abhorrent Overlord#1(new), 1/1 Harpy Token#1000(new), 1/1 Harpy Token#1001(new), 1/1 Harpy Token#10  
  board: board=[Lake of the Dead#14, Lotus Bloom#17(new), Swamp#50(new)] hand=[Thrumming Stone, Thrumming Stone, Shadowborn Apostle, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010331 --game-index 10331`
- **shadowborn_apostle** x87 -- T5 m2 `{no land + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#42:B]: Lake of the Dead#14 tapped by engine only; Swamp#42 only in oracle; graveyard size differs; float engine {B}{B}{B}{B} oracle -; engine ended board=[Blood Crypt#2, Lake of the Dead#14(T), Thrumming Stone#57, Shadowborn Apostle#22(new)] hand=[Swamp, Shadowborn Apostle, Lotus Bloom] float={B}{B}{B}{B} life=18  
  board: board=[Blood Crypt#2, Lake of the Dead#14, Thrumming Stone#57, Swamp#42(new)] hand=[Swamp, Shadowborn Apostle, Shadowborn Apostle, Lotus Bloom] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010170 --game-index 10170`
- **shadowborn_apostle** x78 -- T3 m1 `{land=Lake of the Dead + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Thrumming Stone + act:Shadowborn Apostle(x1)}`  
  3 oracle outcomes, none covers the engine's -- nearest pay1[(float only)] ; pay2[Lake of the Dead#15:B] ; pay3[Swamp#49:B]: Swamp#49 only in oracle; graveyard size differs; float engine {B}{B}{B} oracle -; engine ended board=[Lake of the Dead#15(T)(new), Thrumming Stone#59(new)] hand=[Abhorrent Overlord, Diabolic Intent, Swamp] float={B}{B}{B} life=20  
  board: board=[Swamp#50, Swamp#49] hand=[Culling the Weak, Lake of the Dead, Abhorrent Overlord, Shadowborn Apostle, Diabolic Intent, Thrumming Stone, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010233 --game-index 10233`
- ... 437 more signatures in flags.jsonl

## UNMODELED (3816 flags, 213 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **shadowborn_apostle** x128 -- T5 m2 `{no land + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + cast:Culling the Weak + cast:Demonic Tutor + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Demonic Tutor]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#44(new)] hand=[Shadowborn Apostle, Shadowborn Apostle, Thrumming Stone, Dark Ritual, Culling the Weak, Shadowborn Apostle, Demonic Tutor] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30012345 --game-index 12345`
- **shadowborn_apostle** x84 -- T4 m2 `{no land + cast:Reaper from the Abyss + cast:Shadowborn Apostle + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Reaper from the Abyss, cast Shadowborn Apostle (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Shadowborn Apostle#21(T), Lake of the Dead#14, Swamp#46(new)] hand=[Reaper from the Abyss, Shadowborn Apostle, Shadowborn Apostle, Thrumming Stone, Lake of the Dead, Dark Ritual, Jetmir, Nexus of Revels] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30011605 --game-index 11605`
- **shadowborn_apostle** x68 -- T5 m2 `{no land + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + cast:Culling the Weak + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Shadowborn Apostle]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#44(new)] hand=[Shadowborn Apostle, Shadowborn Apostle, Thrumming Stone, Dark Ritual, Culling the Weak, Shadowborn Apostle, Demonic Tutor] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30012345 --game-index 12345`
- **shadowborn_apostle** x58 -- T1 m2 `{no land + cast:Dark Ritual + cast:Dark Ritual + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Shadowborn Apostle, cast Shadowborn Apostle]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40(new)] hand=[Dark Ritual, Dark Ritual, Shadowborn Apostle, Shadowborn Apostle, Thrumming Stone, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30011251 --game-index 11251`
- **shadowborn_apostle** x56 -- T3 m1 `{land=Lake of the Dead + cast:Diabolic Intent + cast:Dark Ritual + cast:Thrumming Stone + cast:Culling the Weak + cast:Diabolic Intent + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent, cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Swamp#41] hand=[Diabolic Intent, Dark Ritual, Thrumming Stone, Lake of the Dead, Culling the Weak, Diabolic Intent, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010186 --game-index 10186`
- **shadowborn_apostle** x56 -- T3 m1 `{land=Lake of the Dead + cast:Diabolic Intent + cast:Dark Ritual + cast:Thrumming Stone + cast:Culling the Weak + cast:Diabolic Intent + cast:Shadowborn Apostle}`  
  engine dropped [cast Diabolic Intent, cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Swamp#41] hand=[Diabolic Intent, Dark Ritual, Thrumming Stone, Lake of the Dead, Culling the Weak, Diabolic Intent, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010186 --game-index 10186`
- **shadowborn_apostle** x56 -- T3 m1 `{land=Lake of the Dead + cast:Diabolic Intent + cast:Dark Ritual + cast:Thrumming Stone + cast:Culling the Weak + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Swamp#41] hand=[Diabolic Intent, Dark Ritual, Thrumming Stone, Lake of the Dead, Culling the Weak, Diabolic Intent, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010186 --game-index 10186`
- **shadowborn_apostle** x56 -- T1 m2 `{no land + cast:Culling the Weak + cast:Diabolic Intent + cast:Thrumming Stone + cast:Dark Ritual + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40(new)] hand=[Culling the Weak, Diabolic Intent, Thrumming Stone, Demonic Tutor, Dark Ritual, Swamp, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010391 --game-index 10391`
- **shadowborn_apostle** x56 -- T1 m2 `{no land + cast:Culling the Weak + cast:Thrumming Stone + cast:Demonic Tutor + cast:Dark Ritual + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Demonic Tutor]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40(new)] hand=[Culling the Weak, Diabolic Intent, Thrumming Stone, Demonic Tutor, Dark Ritual, Swamp, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30010391 --game-index 10391`
- **shadowborn_apostle** x56 -- T2 m1 `{no land + cast:Culling the Weak + cast:Diabolic Intent + cast:Thrumming Stone + cast:Shadowborn Apostle + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent, cast Shadowborn Apostle (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Shadowborn Apostle#21] hand=[Culling the Weak, Diabolic Intent, Profane Tutor, Thrumming Stone, Shadowborn Apostle, Shadowborn Apostle, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30011519 --game-index 11519`
- **shadowborn_apostle** x56 -- T4 m1 `{land=Lake of the Dead + cast:Culling the Weak + cast:Thrumming Stone + cast:Dark Ritual + cast:Diabolic Intent + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Swamp#41] hand=[Culling the Weak, Thrumming Stone, Dark Ritual, Diabolic Intent, Lake of the Dead, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30012546 --game-index 12546`
- **shadowborn_apostle** x56 -- T1 m2 `{no land + cast:Dark Ritual + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Culling the Weak + cast:Thrumming Stone + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40(new)] hand=[Dark Ritual, Diabolic Intent, Diabolic Intent, Shadowborn Apostle, Culling the Weak, Thrumming Stone, Sire of Insanity] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30013259 --game-index 13259`
- ... 201 more signatures in flags.jsonl

