# Mana-clean gate `lastweek-20261110-jumbo-float`

Mana-clean gate run: 1 decks, games (seed 30000000 + game index) jumbo_cactuar 20000-24999; src `21c2fa3f65e9`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | ORACLE-GAP | UNMODELED | CAPPED-DECISION | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| jumbo_cactuar | 5000 | issues | 37918 | 560 | 879 | 572 | 666 | 175 | 682 | 34384 | 16231 | 4975 | 17 | 152177 | 8 |

**MISS = 2677, ORACLE-GAP = 175.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=35815 plans=755561 skipped=0 applies=553891989 frontiers=453294647 pruned=115280578 options=288184633 engine_drops=22230 gate_rejects=26590039(checked 4716329, skipped 21843277) MISS=2677 ORACLE-GAP=175 UNMODELED=682 CAPPED=0 ms=12693262 ===
=== MANA ORACLE classes: ORACLE-GAP=175 ENUM-GAP=560 APPLY-DROP=879 APPLY-DROP-ORDER=572 PHANTOM=16231 DOMINATED-CREATURE=666 DOMINATED-LAND=152177 CONTINUATION-DIFFERS=4975 DROP-COVERED=17 UNMODELED=682 HIDDEN-INFO=8 CAPPED-DECISION=34384 ===
=== MANA ORACLE enum-gap rejections: noop=0 hidden=151 host_blind=8 cont_land=118 ===
=== MANA ORACLE decision budget: 2 decisions spent it, 34382 plans not audited ===
=== MANA ORACLE capped-plan escalation: 638 retried, 638 finished ===
=== MANA ORACLE unmodeled: 12 x floating pool too large to enumerate set-asides ===
=== MANA ORACLE unmodeled: 2205877 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 682 x too many actions to try every order ===
=== MANA ORACLE rollback census: lost=2011 depth0=0 depth1=624 depth2=100 depth3+=155 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=572 off-menu=560 need_in_plan=714 need_later=165 pay_rollback_rescued=0/879 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | ORACLE-GAP | UNMODELED | CAPPED-DECISION |
|---|---|---|---|---|---|---|---|---|---|
| jumbo_cactuar | 35815 | 759510 | 560 | 879 | 572 | 666 | 175 | 682 | 5000 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth1 | depth2 | depth3+ | reorder | off-menu | need-later |
|---|---|---|---|---|---|---|
| jumbo_cactuar | 624 | 100 | 155 | 572 | 560 | 165 |

## ENUM-GAP (560 flags, 161 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **jumbo_cactuar** x29 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Chariot of Victory + cast:Overgrowth@17 + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G] ; pay2[Forest#15:G] ; pay3[Forest#17:G] ; pay4[(float only)] ; pay5[Voyaging Satyr#55:G]  
  board: board=[Forest#17, Utopia Sprawl#50@17, Forest#15, Overgrowth#42@17, Arbor Elf#1, Chariot of Victory#8] hand=[Tooth and Nail, Chariot of Victory, Overgrowth, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021031 --game-index 21031`
- **jumbo_cactuar** x20 -- T3 m1 `{land=Forest + cast:Chariot of Victory + cast:Overgrowth@10 + cast:Tooth and Nail + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#2:G Forest#12:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#11:G] ; pay4[Forest#10:G] ; pay5[Voyaging Satyr#54:G]  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Arbor Elf#2, Arbor Elf#1] hand=[Forest, Chariot of Victory, Overgrowth, Tooth and Nail, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021183 --game-index 21183`
- **jumbo_cactuar** x20 -- T3 m1 `{land=Forest + cast:Chariot of Victory + cast:Tooth and Nail + cast:Voyaging Satyr + cast:Wild Growth@10 + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#12:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#11:G] ; pay4[Forest#10:G] ; pay5[Voyaging Satyr#53:G]  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Overgrowth#39@10] hand=[Chariot of Victory, Tooth and Nail, Forest, Voyaging Satyr, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022184 --game-index 22184`
- **jumbo_cactuar** x20 -- T3 m1 `{no land + cast:Overgrowth@10 + cast:Tooth and Nail + cast:Voyaging Satyr + cast:Lightning Greaves + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Forest#11:G Voyaging Satyr#54:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Wild Growth#57@10, Voyaging Satyr#53] hand=[Overgrowth, Tooth and Nail, Voyaging Satyr, Lightning Greaves] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022448 --game-index 22448`
- **jumbo_cactuar** x20 -- T3 m1 `{no land + cast:Overgrowth@18 + cast:Voyaging Satyr + cast:Tooth and Nail + cast:Lightning Greaves + cast:Arbor Elf + equip:Lightning Greaves + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#25:G Voyaging Satyr#53:G] ; pay2[Forest#18:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Arbor Elf#2:G] ; pay6[(float only)] ; pay7[Voyaging Satyr#56:G] ; pay8[(float only)] ; pay9[(float only)]  
  board: board=[Forest#18, Utopia Sprawl#51@18, Forest#25, Voyaging Satyr#53] hand=[Overgrowth, Voyaging Satyr, Tooth and Nail, Lightning Greaves, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30023079 --game-index 23079`
- **jumbo_cactuar** x20 -- T3 m1 `{land=Forest + cast:Arbor Elf + cast:Overgrowth@10 + cast:Tooth and Nail + cast:Lightning Greaves + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Voyaging Satyr#53:G Forest#11:G] ; pay2[Arbor Elf#1:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Forest#10:G Arbor Elf#2:G]  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53] hand=[Arbor Elf, Overgrowth, Green Sun's Zenith, Tooth and Nail, Lightning Greaves, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30023931 --game-index 23931`
- **jumbo_cactuar** x20 -- T4 m1 `{land=Forest + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Tooth and Nail + cast:Chariot of Victory + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G] ; pay3[(float only)] ; pay4[Forest#27:G] ; pay5[Voyaging Satyr#54:G] ; pay6[Forest#10:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Utopia Sprawl#50@10, Arbor Elf#1] hand=[Overgrowth, Chariot of Victory, Tooth and Nail, Chariot of Victory, Voyaging Satyr, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30024691 --game-index 24691`
- **jumbo_cactuar** x20 -- T4 m1 `{land=Forest + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Tooth and Nail + cast:Chariot of Victory + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G] ; pay3[(float only)] ; pay4[Forest#10:G] ; pay5[(float only)] ; pay6[Forest#27:G Voyaging Satyr#54:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Utopia Sprawl#50@10, Arbor Elf#1] hand=[Overgrowth, Chariot of Victory, Tooth and Nail, Chariot of Victory, Voyaging Satyr, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30024691 --game-index 24691`
- **jumbo_cactuar** x19 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Arbor Elf + cast:Overgrowth@10}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[Arbor Elf#2:G]  
  board: board=[Forest#10, Arbor Elf#1, Wild Growth#57@10, Voyaging Satyr#53, Surrak and Goreclaw#43] hand=[Tooth and Nail, Overgrowth, Jumbo Cactuar, Jumbo Cactuar, Arbor Elf, Overgrowth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021761 --game-index 21761`
- **jumbo_cactuar** x19 -- T4 m1 `{land=Forest + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Tooth and Nail + cast:Arbor Elf + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Voyaging Satyr#53:G Forest#11:G] ; pay2[Arbor Elf#1:G] ; pay3[(float only)] ; pay4[Forest#10:G Arbor Elf#2:G] ; pay5[(float only)]  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Surrak and Goreclaw#43] hand=[Overgrowth, Chariot of Victory, Tooth and Nail, Arbor Elf, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30023248 --game-index 23248`
- **jumbo_cactuar** x17 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but casting Chariot of Victory > Overgrowth > Voyaging Satyr > Tooth and Nail pays every action with pay1[Voyaging Satyr#53:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#10:G] ; pay4[(float only)] ; pay5[Voyaging Satyr#54:G] ; pay6[Forest#11:G]  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Chariot of Victory#5] hand=[Green Sun's Zenith, Tooth and Nail, Overgrowth, Chariot of Victory, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020323 --game-index 20323`
- **jumbo_cactuar** x14 -- T3 m1 `{no land + cast:Lightning Greaves + cast:Voyaging Satyr + cast:Tooth and Nail + cast:Overgrowth@10 + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G Forest#10:G keep-floating {W}{W}{G}] ; pay2[Arbor Elf#2:G keep-floating {W}{W}] ; pay3[keep-floating {W}{W}{W}] ; pay4[(float only)] ; pay5[Voyaging Satyr#53:G keep-floating {W}{W}]  
  board: board=[Forest#10, Arbor Elf#1, Wild Growth#57@10, Utopia Sprawl#49@10, Arbor Elf#2] hand=[Lightning Greaves, Voyaging Satyr, Tooth and Nail, Green Sun's Zenith, Overgrowth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020205 --game-index 20205`
- ... 149 more signatures in flags.jsonl

## APPLY-DROP (879 flags, 90 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **jumbo_cactuar** x160 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#11:G] ; pay2[Arbor Elf#1:G Arbor Elf#2:G Forest#26:G Voyaging Satyr#53:G Forest#10:G] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Arbor Elf#1:G Arbor Elf#2:G Forest#26:G Voyaging Satyr#53:G Forest#10:G] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Arbor Elf#2, Forest#26, Voyaging Satyr#53, Chariot of Victory#5] hand=[Tooth and Nail, Finale of Devastation, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021744 --game-index 21744`
- **jumbo_cactuar** x160 -- T4 m1 `{no land + cast:Finale of Devastation + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@17}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#23:G] ; pay2[Voyaging Satyr#54:G Forest#26:G Arbor Elf#4:G Arbor Elf#2:G Forest#17:G] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Voyaging Satyr#54:G Forest#26:G Arbor Elf#4:G Arbor Elf#2:G Forest#17:G] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#17, Forest#23, Voyaging Satyr#54, Forest#26, Arbor Elf#4, Arbor Elf#2] hand=[Finale of Devastation, Tooth and Nail, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022268 --game-index 22268`
- **jumbo_cactuar** x40 -- T4 m1 `{no land + cast:Finale of Devastation(x1) + cast:Tooth and Nail}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#10:G Arbor Elf#1:G] ; pay2[Forest#11:G Forest#12:G] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Forest#10:G Arbor Elf#1:G] ; pay2[Forest#11:G Forest#12:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Utopia Sprawl#50@10, Forest#12, Utopia Sprawl#51@10, Arbor Elf#1] hand=[Finale of Devastation, Tooth and Nail, Jumbo Cactuar] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020730 --game-index 20730`
- **jumbo_cactuar** x30 -- T3 m1 `{no land + cast:Green Sun's Zenith(x6) + cast:Finale of Devastation}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Arbor Elf#1:G Arbor Elf#2:G Forest#11:G Voyaging Satyr#53:G Forest#10:G] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Arbor Elf#1:G Arbor Elf#2:G Forest#11:G Voyaging Satyr#53:G Forest#10:G] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Arbor Elf#2, Voyaging Satyr#53] hand=[Green Sun's Zenith, Finale of Devastation, Forest, Green Sun's Zenith] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30023449 --game-index 23449`
- **jumbo_cactuar** x25 -- T3 m1 `{land=Forest + cast:Overgrowth@11 + cast:Green Sun's Zenith(x1) + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Green Sun's Zenith]; every action pays with pay1[Forest#12:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G] ; pay4[(float only)] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Forest#12:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Chariot of Victory#5] hand=[Overgrowth, Forest, Forest, Green Sun's Zenith, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020626 --game-index 20626`
- **jumbo_cactuar** x25 -- T3 m1 `{no land + cast:Green Sun's Zenith(x1) + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Chariot of Victory]; every action pays with pay1[Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Arbor Elf#1:G] || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Arbor Elf#1:G]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Voyaging Satyr#53] hand=[Surrak and Goreclaw, Green Sun's Zenith, Chariot of Victory, Jumbo Cactuar, Lightning Greaves, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021335 --game-index 21335`
- **jumbo_cactuar** x25 -- T3 m1 `{no land + cast:Green Sun's Zenith(x1) + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Chariot of Victory]; every action pays with pay1[Voyaging Satyr#53:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#10:G] ; pay4[(float only)] ; pay5[Forest#11:G Arbor Elf#2:G] || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Voyaging Satyr#53:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#10:G] ; pay4[(float only)] ; pay5[Forest#11:G Arbor Elf#2:G]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53] hand=[Chariot of Victory, Green Sun's Zenith, Jumbo Cactuar, Lightning Greaves, Surrak and Goreclaw, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30024537 --game-index 24537`
- **jumbo_cactuar** x25 -- T3 m1 `{land=Forest + cast:Green Sun's Zenith(x1) + cast:Overgrowth@11 + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Green Sun's Zenith]; every action pays with pay1[Forest#12:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G] ; pay4[(float only)] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Forest#12:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Chariot of Victory#5] hand=[Jumbo Cactuar, Green Sun's Zenith, Forest, Forest, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30024615 --game-index 24615`
- **jumbo_cactuar** x24 -- T2 m1 `{land=Forest + cast:Utopia Sprawl(x1)@10 + cast:Lightning Greaves + cast:Finale of Devastation}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#11:G] ; pay2[Arbor Elf#1:G Forest#10:G] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Arbor Elf#1:G Forest#10:G] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1] hand=[Utopia Sprawl, Lightning Greaves, Finale of Devastation, Voyaging Satyr, Wild Growth, Chariot of Victory, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020087 --game-index 20087`
- **jumbo_cactuar** x20 -- T3 m1 `{land=Forest + cast:Finale of Devastation + cast:Green Sun's Zenith(x1) + cast:Green Sun's Zenith(x1)}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G Forest#12:G] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Voyaging Satyr#53:G] ; pay3[Forest#11:G Forest#12:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53] hand=[Finale of Devastation, Forest, Tooth and Nail, Green Sun's Zenith, Overgrowth, Green Sun's Zenith] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020067 --game-index 20067`
- **jumbo_cactuar** x16 -- T2 m1 `{land=Forest + cast:Lightning Greaves + cast:Utopia Sprawl(x1)@10 + cast:Finale of Devastation}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#11:G] ; pay2[Arbor Elf#1:G Forest#10:G] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Arbor Elf#1:G Forest#10:G] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1] hand=[Lightning Greaves, Utopia Sprawl, Finale of Devastation, Chariot of Victory, Tooth and Nail, Forest, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020327 --game-index 20327`
- **jumbo_cactuar** x16 -- T3 m1 `{land=Forest + cast:Finale of Devastation + cast:Utopia Sprawl(x1)@10 + cast:Lightning Greaves}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#11:G] ; pay2[Arbor Elf#1:G Forest#10:G] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Arbor Elf#1:G Forest#10:G] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1] hand=[Finale of Devastation, Forest, Chariot of Victory, Forest, Voyaging Satyr, Utopia Sprawl, Lightning Greaves] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020488 --game-index 20488`
- ... 78 more signatures in flags.jsonl

## APPLY-DROP-ORDER (572 flags, 171 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **jumbo_cactuar** x25 -- T3 m1 `{no land + cast:Overgrowth@11 + cast:Utopia Sprawl(x1)@10 + cast:Green Sun's Zenith(x1)}`  
  engine dropped [cast Green Sun's Zenith]; the order Overgrowth > Green Sun's Zenith > Utopia Sprawl pays every action with pay1[Forest#10:G] ; pay2[Forest#11:G] ; pay3[(float only)]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Wild Growth#57@10, Chariot of Victory#5] hand=[Overgrowth, Utopia Sprawl, Green Sun's Zenith, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022280 --game-index 22280`
- **jumbo_cactuar** x15 -- T3 m1 `{no land + cast:Green Sun's Zenith(x1) + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Chariot of Victory > Arbor Elf > Green Sun's Zenith > equip:Chariot of Victory@5 pays every action with pay1[Forest#11:G Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[Arbor Elf#1:G] ; pay5[Forest#12:G]  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Voyaging Satyr#53] hand=[Green Sun's Zenith, Jumbo Cactuar, Forest, Chariot of Victory, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020186 --game-index 20186`
- **jumbo_cactuar** x15 -- T3 m1 `{no land + cast:Chariot of Victory + cast:Green Sun's Zenith(x1) + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Chariot of Victory > Voyaging Satyr > Green Sun's Zenith > equip:Chariot of Victory@5 pays every action with pay1[Arbor Elf#1:G Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[Voyaging Satyr#54:G]  
  board: board=[Forest#10, Arbor Elf#1, Wild Growth#57@10, Voyaging Satyr#53] hand=[Chariot of Victory, Green Sun's Zenith, Jumbo Cactuar, Voyaging Satyr, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020354 --game-index 20354`
- **jumbo_cactuar** x15 -- T3 m1 `{land=Forest + cast:Green Sun's Zenith(x1) + cast:Chariot of Victory + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Chariot of Victory > Voyaging Satyr > Green Sun's Zenith > equip:Chariot of Victory@5 pays every action with pay1[Forest#10:G] ; pay2[Forest#12:G] ; pay3[Forest#11:G] ; pay4[Voyaging Satyr#53:G] ; pay5[(float only)] ; pay6[Arbor Elf#1:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Overgrowth#39@10] hand=[Lightning Greaves, Tooth and Nail, Green Sun's Zenith, Chariot of Victory, Forest, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022743 --game-index 22743`
- **jumbo_cactuar** x10 -- T3 m1 `{no land + cast:Chariot of Victory + cast:Finale of Devastation + cast:Green Sun's Zenith(x1) + cast:Arbor Elf + equip:Chariot of Victory}`  
  engine dropped [cast Finale of Devastation]; the order Chariot of Victory > Arbor Elf > Finale of Devastation > Green Sun's Zenith > equip:Chariot of Victory@5 pays every action with pay1[Arbor Elf#1:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[Arbor Elf#2:G] ; pay5[(float only)]  
  board: board=[Forest#10, Wild Growth#57@10, Utopia Sprawl#49@10, Arbor Elf#1] hand=[Chariot of Victory, Finale of Devastation, Voyaging Satyr, Green Sun's Zenith, Voyaging Satyr, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020183 --game-index 20183`
- **jumbo_cactuar** x10 -- T3 m1 `{no land + cast:Chariot of Victory + cast:Green Sun's Zenith(x1) + cast:Arbor Elf + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Chariot of Victory > Arbor Elf > Green Sun's Zenith > equip:Chariot of Victory@8 pays every action with pay1[Forest#21:G Voyaging Satyr#54:G] ; pay2[Forest#20:G] ; pay3[(float only)] ; pay4[Arbor Elf#4:G]  
  board: board=[Forest#20, Wild Growth#60@20, Forest#21, Voyaging Satyr#54] hand=[Overgrowth, Chariot of Victory, Green Sun's Zenith, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020348 --game-index 20348`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Voyaging Satyr + cast:Chariot of Victory + cast:Arbor Elf + cast:Green Sun's Zenith(x1) + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Voyaging Satyr > Chariot of Victory > Arbor Elf > equip:Chariot of Victory@5 > Green Sun's Zenith pays every action with pay1[Voyaging Satyr#55:G] ; pay2[Arbor Elf#1:G Forest#23:G] ; pay3[Forest#12:G] ; pay4[(float only)] ; pay5[Voyaging Satyr#56:G]  
  board: board=[Forest#12, Arbor Elf#1, Utopia Sprawl#52@12, Voyaging Satyr#55] hand=[Voyaging Satyr, Chariot of Victory, Arbor Elf, Green Sun's Zenith, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020417 --game-index 20417`
- **jumbo_cactuar** x10 -- T3 m1 `{no land + cast:Green Sun's Zenith(x1) + cast:Chariot of Victory + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Chariot of Victory > Voyaging Satyr > Green Sun's Zenith > equip:Chariot of Victory@5 pays every action with pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Forest#11:G] ; pay4[Voyaging Satyr#53:G] ; pay5[(float only)] ; pay6[Arbor Elf#1:G]  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Wild Growth#58@10, Overgrowth#39@10] hand=[Tooth and Nail, Green Sun's Zenith, Chariot of Victory, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020584 --game-index 20584`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Voyaging Satyr + cast:Finale of Devastation + cast:Green Sun's Zenith(x1) + cast:Chariot of Victory + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Voyaging Satyr > Chariot of Victory > Finale of Devastation > Green Sun's Zenith > equip:Chariot of Victory@5 pays every action with pay1[Forest#11:G Forest#12:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[Voyaging Satyr#53:G] ; pay5[(float only)]  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Overgrowth#39@10] hand=[Voyaging Satyr, Finale of Devastation, Green Sun's Zenith, Forest, Lightning Greaves, Chariot of Victory] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020901 --game-index 20901`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Voyaging Satyr + cast:Finale of Devastation(x1) + cast:Green Sun's Zenith(x1) + cast:Lightning Greaves + equip:Lightning Greaves}`  
  engine dropped [cast Green Sun's Zenith, cast Voyaging Satyr]; the order Voyaging Satyr > Green Sun's Zenith > Lightning Greaves > Finale of Devastation > equip:Lightning Greaves@37 pays every action with pay1[Forest#11:G Forest#12:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Voyaging Satyr#53:G] ; pay6[(float only)] ; pay7[Arbor Elf#1:G]  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Overgrowth#39@10] hand=[Voyaging Satyr, Finale of Devastation, Green Sun's Zenith, Forest, Lightning Greaves, Chariot of Victory] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020901 --game-index 20901`
- **jumbo_cactuar** x10 -- T3 m1 `{no land + cast:Chariot of Victory + cast:Green Sun's Zenith(x1) + cast:Arbor Elf + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Chariot of Victory > Arbor Elf > Green Sun's Zenith > equip:Chariot of Victory@5 pays every action with pay1[Forest#11:G Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[Arbor Elf#1:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53] hand=[Tooth and Nail, Chariot of Victory, Green Sun's Zenith, Green Sun's Zenith, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021236 --game-index 21236`
- **jumbo_cactuar** x10 -- T3 m1 `{no land + cast:Finale of Devastation + cast:Chariot of Victory + cast:Green Sun's Zenith(x1) + cast:Arbor Elf + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Chariot of Victory > Arbor Elf > Finale of Devastation > Green Sun's Zenith > equip:Chariot of Victory@5 pays every action with pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Forest#11:G] ; pay4[Arbor Elf#1:G] ; pay5[(float only)]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Overgrowth#39@10] hand=[Finale of Devastation, Chariot of Victory, Green Sun's Zenith, Green Sun's Zenith, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021344 --game-index 21344`
- ... 159 more signatures in flags.jsonl

## DOMINATED-CREATURE (666 flags, 207 signatures)

_the engine's payment taps a creature that could attack, where another payment keeps it untapped and loses nothing._

- **jumbo_cactuar** x30 -- T3 m1 `{land=Forest + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Arbor Elf#1:G Forest#11:G Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Arbor Elf#2(T), Voyaging Satyr#53(T), Forest#12(T)(new), Utopia Sprawl#51(new)@10] hand=[] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Arbor Elf#2, Voyaging Satyr#53] hand=[Tooth and Nail, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021838 --game-index 21838`
- **jumbo_cactuar** x15 -- T3 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker); keeps Forest untapped -- by paying pay1[Arbor Elf#1:G] ; pay2[Arbor Elf#4:G Arbor Elf#2:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Arbor Elf#4(T), Arbor Elf#2(T), Arbor Elf#3(T), Utopia Sprawl#50(new)@10] hand=[Jumbo Cactuar] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Arbor Elf#4, Arbor Elf#2, Arbor Elf#3] hand=[Jumbo Cactuar, Tooth and Nail, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020239 --game-index 20239`
- **jumbo_cactuar** x15 -- T3 m1 `{no land + cast:Tooth and Nail(x2) + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker); keeps Forest untapped -- by paying pay1[Forest#11:G] ; pay2[Arbor Elf#1:G Arbor Elf#4:G Arbor Elf#2:G]; the engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Arbor Elf#4(T), Arbor Elf#2(T), Arbor Elf#3(T), Utopia Sprawl#50(new)@10, Jumbo Cactuar#33(new), Surrak and Goreclaw#43(new)] hand=[Jumbo Cactuar] float={W}{G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Arbor Elf#4, Arbor Elf#2, Arbor Elf#3] hand=[Jumbo Cactuar, Tooth and Nail, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020239 --game-index 20239`
- **jumbo_cactuar** x15 -- T3 m1 `{land=Forest + cast:Chariot of Victory + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#11:G] ; pay2[Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Utopia Sprawl#49@10, Voyaging Satyr#53(T), Forest#11(T)(new), Utopia Sprawl#50(new)@10, Chariot of Victory#5(new)] hand=[Overgrowth, Green Sun's Zenith, Lightning Greaves] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53] hand=[Overgrowth, Green Sun's Zenith, Chariot of Victory, Lightning Greaves, Utopia Sprawl, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020375 --game-index 20375`
- **jumbo_cactuar** x15 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@13}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Arbor Elf#2:G] ; pay2[Voyaging Satyr#55:G Voyaging Satyr#56:G]; the engine ended board=[Forest#13, Arbor Elf#2(T), Wild Growth#57@13, Voyaging Satyr#55(T), Arbor Elf#3(T), Voyaging Satyr#56(T), Utopia Sprawl#51(new)@13] hand=[Jumbo Cactuar] float=- life=20  
  board: board=[Forest#13, Arbor Elf#2, Wild Growth#57@13, Voyaging Satyr#55, Arbor Elf#3, Voyaging Satyr#56] hand=[Jumbo Cactuar, Tooth and Nail, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021291 --game-index 21291`
- **jumbo_cactuar** x15 -- T3 m1 `{no land + cast:Overgrowth@10 + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#11:G] ; pay2[Arbor Elf#1:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Arbor Elf#2(T), Voyaging Satyr#53, Utopia Sprawl#51(new)@10, Overgrowth#39(new)@10] hand=[Green Sun's Zenith, Green Sun's Zenith] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Arbor Elf#2, Voyaging Satyr#53] hand=[Green Sun's Zenith, Overgrowth, Green Sun's Zenith, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021461 --game-index 21461`
- **jumbo_cactuar** x15 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@11}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Arbor Elf#2:G] ; pay2[Voyaging Satyr#54:G Voyaging Satyr#55:G]; the engine ended board=[Forest#11, Arbor Elf#2(T), Wild Growth#59@11, Voyaging Satyr#54(T), Arbor Elf#3(T), Voyaging Satyr#55(T), Utopia Sprawl#51(new)@11] hand=[Tooth and Nail] float=- life=20  
  board: board=[Forest#11, Arbor Elf#2, Wild Growth#59@11, Voyaging Satyr#54, Arbor Elf#3, Voyaging Satyr#55] hand=[Tooth and Nail, Tooth and Nail, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022029 --game-index 22029`
- **jumbo_cactuar** x15 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#11:G Forest#12:G] ; pay2[Arbor Elf#1:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Arbor Elf#2(T), Forest#12(T), Overgrowth#39@10, Overgrowth#40@10, Utopia Sprawl#49(new)@10] hand=[Jumbo Cactuar] float={G}{G}{G}{G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Arbor Elf#2, Forest#12, Overgrowth#39@10, Overgrowth#40@10] hand=[Tooth and Nail, Jumbo Cactuar, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022682 --game-index 22682`
- **jumbo_cactuar** x15 -- T3 m1 `{land=Forest + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Forest#11:G Voyaging Satyr#53:G Voyaging Satyr#54:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Voyaging Satyr#53(T), Voyaging Satyr#54(T), Forest#12(T)(new), Utopia Sprawl#50(new)@10] hand=[Arbor Elf] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Voyaging Satyr#53, Voyaging Satyr#54] hand=[Tooth and Nail, Forest, Arbor Elf, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30024455 --game-index 24455`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Overgrowth@10 + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Forest#11:G Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Voyaging Satyr#53(T), Forest#12(T)(new), Utopia Sprawl#49(new)@10, Overgrowth#39(new)@10] hand=[Jumbo Cactuar, Chariot of Victory] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53] hand=[Jumbo Cactuar, Overgrowth, Chariot of Victory, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020012 --game-index 20012`
- **jumbo_cactuar** x10 -- T3 m1 `{no land + cast:Finale of Devastation + cast:Wild Growth@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#11:G] ; pay2[Arbor Elf#1:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Arbor Elf#2(T), Arbor Elf#3, Wild Growth#59(new)@10] hand=[Chariot of Victory, Forest] float={W}{W} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Arbor Elf#2, Arbor Elf#3] hand=[Chariot of Victory, Finale of Devastation, Forest, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020181 --game-index 20181`
- **jumbo_cactuar** x10 -- T3 m1 `{no land + cast:Tooth and Nail(x1) + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker); keeps Forest untapped -- by paying pay1[Arbor Elf#1:G] ; pay2[Arbor Elf#4:G Arbor Elf#2:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Arbor Elf#4(T), Arbor Elf#2(T), Arbor Elf#3(T), Utopia Sprawl#50(new)@10] hand=[Jumbo Cactuar] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Arbor Elf#4, Arbor Elf#2, Arbor Elf#3] hand=[Jumbo Cactuar, Tooth and Nail, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020239 --game-index 20239`
- ... 195 more signatures in flags.jsonl

## ORACLE-GAP (175 flags, 66 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **jumbo_cactuar** x12 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Wild Growth@11 + act:Nylea, God of the Hunt(x4)}`  
  48 oracle outcomes, none covers the engine's -- nearest pay1[Arbor Elf#3:G] ; pay2[Voyaging Satyr#53:G] ; pay3[Arbor Elf#2:G] ; pay4[Forest#11:G Forest#19:G]: Arbor Elf#2 pump differs; float engine {G}{G}{G} oracle -; engine ended board=[Forest#11(T), Arbor Elf#2(T), Utopia Sprawl#52@11, Voyaging Satyr#53(T), Forest#19(T), Overgrowth#42@11, Arbor Elf#3(T), Nylea, God of the Hunt#38, Wild Growth#60(new)@11] hand=[] float={G}{G}{G} life=20  
  board: board=[Forest#11, Arbor Elf#2, Utopia Sprawl#52@11, Voyaging Satyr#53, Forest#19, Overgrowth#42@11, Arbor Elf#3, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021881 --game-index 21881`
- **jumbo_cactuar** x10 -- T4 m1 `{land=Forest + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  10 oracle outcomes, none covers the engine's -- nearest pay1[Forest#13:G] ; pay2[Arbor Elf#1:G Voyaging Satyr#53:G] ; pay3[Forest#11:G] ; pay4[Forest#10:G] ; pay5[(float only)]: Forest#12 tapped by engine only; Chariot of Victory#5 on #0 by engine, #54 by oracle; float engine - oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Voyaging Satyr#53(T), Forest#12(T), Overgrowth#39@10, Nylea, God of the Hunt#38, Forest#13(T)(new), Utopia Sprawl#49(new)@10, Voyaging Satyr#54(new), Chariot of Victory#5(new)] hand=[Jumbo Cactuar] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53, Forest#12, Overgrowth#39@10, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Forest, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020347 --game-index 20347`
- **jumbo_cactuar** x10 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  10 oracle outcomes, none covers the engine's -- nearest pay1[Forest#11:G] ; pay2[Arbor Elf#1:G Voyaging Satyr#53:G] ; pay3[Forest#13:G] ; pay4[Forest#10:G] ; pay5[(float only)]: Forest#12 tapped by engine only; Chariot of Victory#5 on #0 by engine, #54 by oracle; float engine - oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Voyaging Satyr#53(T), Forest#12(T), Overgrowth#39@10, Nylea, God of the Hunt#38, Utopia Sprawl#49(new)@10, Forest#13(T)(new), Voyaging Satyr#54(new), Chariot of Victory#5(new)] hand=[Jumbo Cactuar] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53, Forest#12, Overgrowth#39@10, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Forest, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020347 --game-index 20347`
- **jumbo_cactuar** x10 -- T4 m1 `{no land + cast:Overgrowth@10 + cast:Finale of Devastation + cast:Green Sun's Zenith(x6)}`  
  9 oracle outcomes, none covers the engine's -- nearest pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G] ; pay3[Forest#10:G]: Arbor Elf#1 tapped by oracle only; same core; float engine - oracle {W}{W}{G}{G}{G}{G}{G}; engine ended board=[Forest#10(T), Utopia Sprawl#49@10, Voyaging Satyr#53(T), Wild Growth#57@10, Overgrowth#39@10, Arbor Elf#1, Overgrowth#40(new)@10, Surrak and Goreclaw#43(new)] hand=[Green Sun's Zenith, Forest] float=- life=20  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Wild Growth#57@10, Overgrowth#39@10, Arbor Elf#1] hand=[Overgrowth, Finale of Devastation, Green Sun's Zenith, Green Sun's Zenith, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021771 --game-index 21771`
- **jumbo_cactuar** x10 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  8 oracle outcomes, none covers the engine's -- nearest pay1[Forest#11:G] ; pay2[Voyaging Satyr#53:G Arbor Elf#1:G] ; pay3[(float only)] ; pay4[Forest#10:G] ; pay5[(float only)]: Forest#12 tapped by engine only; Chariot of Victory#5 on #0 by engine, #3 by oracle; float engine - oracle -; engine ended board=[Forest#10(T), Forest#11(T), Voyaging Satyr#53(T), Forest#12(T), Overgrowth#39@10, Arbor Elf#1(T), Utopia Sprawl#52(new)@10, Arbor Elf#3(new), Chariot of Victory#5(new)] hand=[Tooth and Nail, Jumbo Cactuar] float=- life=20  
  board: board=[Forest#10, Forest#11, Voyaging Satyr#53, Forest#12, Overgrowth#39@10, Arbor Elf#1] hand=[Tooth and Nail, Tooth and Nail, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021810 --game-index 21810`
- **jumbo_cactuar** x6 -- T8 m1 `{no land + cast:Tooth and Nail}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Forest#28:G Forest#18:G Arbor Elf#4:G] ; pay2[Forest#22:G]: Forest#22 tapped by oracle only; same core; float engine - oracle {W}; engine ended board=[Forest#28(T), Forest#18(T), Arbor Elf#4(T), Utopia Sprawl#52@18, Overgrowth#42@28, Forest#22(new), Arbor Elf#3(new)] hand=[Jumbo Cactuar, Overgrowth, Green Sun's Zenith, Voyaging Satyr, Voyaging Satyr] float=- life=20  
  board: board=[Forest#28, Forest#18, Arbor Elf#4, Utopia Sprawl#52@18, Overgrowth#42@28] hand=[Tooth and Nail, Jumbo Cactuar, Overgrowth, Green Sun's Zenith, Voyaging Satyr, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020872 --game-index 20872`
- **jumbo_cactuar** x6 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Wild Growth@11 + act:Nylea, God of the Hunt(x4)}`  
  39 oracle outcomes, none covers the engine's -- nearest pay1[Voyaging Satyr#53:G] ; pay2[Arbor Elf#2:G] ; pay3[Arbor Elf#3:G] ; pay4[Forest#11:G Forest#19:G]: Voyaging Satyr#53 pump differs; float engine {G}{G}{G} oracle -; engine ended board=[Forest#11(T), Arbor Elf#2(T), Utopia Sprawl#52@11, Voyaging Satyr#53(T), Forest#19(T), Overgrowth#42@11, Arbor Elf#3(T), Nylea, God of the Hunt#38, Wild Growth#60(new)@11] hand=[] float={G}{G}{G} life=20  
  board: board=[Forest#11, Arbor Elf#2, Utopia Sprawl#52@11, Voyaging Satyr#53, Forest#19, Overgrowth#42@11, Arbor Elf#3, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021881 --game-index 21881`
- **jumbo_cactuar** x6 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Wild Growth@11 + act:Nylea, God of the Hunt(x4)}`  
  39 oracle outcomes, none covers the engine's -- nearest pay1[Voyaging Satyr#53:G] ; pay2[Arbor Elf#2:G] ; pay3[Arbor Elf#3:G] ; pay4[Forest#11:G Forest#19:G]: Nylea, God of the Hunt#38 pump differs; float engine {G}{G}{G} oracle -; engine ended board=[Forest#11(T), Arbor Elf#2(T), Utopia Sprawl#52@11, Voyaging Satyr#53(T), Forest#19(T), Overgrowth#42@11, Arbor Elf#3(T), Nylea, God of the Hunt#38, Wild Growth#60(new)@11] hand=[] float={G}{G}{G} life=20  
  board: board=[Forest#11, Arbor Elf#2, Utopia Sprawl#52@11, Voyaging Satyr#53, Forest#19, Overgrowth#42@11, Arbor Elf#3, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021881 --game-index 21881`
- **jumbo_cactuar** x6 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Wild Growth@11 + act:Nylea, God of the Hunt(x3)}`  
  47 oracle outcomes, none covers the engine's -- nearest pay1[Forest#19:G] ; pay2[Arbor Elf#2:G Voyaging Satyr#53:G] ; pay3[Arbor Elf#3:G] ; pay4[Forest#11:G]: Voyaging Satyr#53 pump differs; float engine {G}{G}{G} oracle {G}; engine ended board=[Forest#11(T), Arbor Elf#2(T), Utopia Sprawl#52@11, Voyaging Satyr#53(T), Forest#19(T), Overgrowth#42@11, Arbor Elf#3(T), Nylea, God of the Hunt#38, Wild Growth#60(new)@11] hand=[] float={G}{G}{G} life=20  
  board: board=[Forest#11, Arbor Elf#2, Utopia Sprawl#52@11, Voyaging Satyr#53, Forest#19, Overgrowth#42@11, Arbor Elf#3, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021881 --game-index 21881`
- **jumbo_cactuar** x6 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Wild Growth@11 + act:Nylea, God of the Hunt(x3)}`  
  47 oracle outcomes, none covers the engine's -- nearest pay1[Forest#19:G] ; pay2[Arbor Elf#2:G Voyaging Satyr#53:G] ; pay3[Arbor Elf#3:G] ; pay4[Forest#11:G]: Nylea, God of the Hunt#38 pump differs; float engine {G}{G}{G} oracle {G}; engine ended board=[Forest#11(T), Arbor Elf#2(T), Utopia Sprawl#52@11, Voyaging Satyr#53(T), Forest#19(T), Overgrowth#42@11, Arbor Elf#3(T), Nylea, God of the Hunt#38, Wild Growth#60(new)@11] hand=[] float={G}{G}{G} life=20  
  board: board=[Forest#11, Arbor Elf#2, Utopia Sprawl#52@11, Voyaging Satyr#53, Forest#19, Overgrowth#42@11, Arbor Elf#3, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021881 --game-index 21881`
- **jumbo_cactuar** x5 -- T4 m1 `{land=Forest + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  10 oracle outcomes, none covers the engine's -- nearest pay1[Forest#13:G] ; pay2[Arbor Elf#1:G Voyaging Satyr#53:G] ; pay3[(float only)] ; pay4[Forest#11:G] ; pay5[Forest#10:G] ; pay6[(float only)]: Forest#12 tapped by engine only; Chariot of Victory#5 on #0 by engine, #2 by oracle; float engine - oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Voyaging Satyr#53(T), Forest#12(T), Overgrowth#39@10, Nylea, God of the Hunt#38, Forest#13(T)(new), Utopia Sprawl#49(new)@10, Arbor Elf#2(new), Arbor Elf#3(new), Chariot of Victory#5(new)] hand=[] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53, Forest#12, Overgrowth#39@10, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Forest, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020347 --game-index 20347`
- **jumbo_cactuar** x5 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  10 oracle outcomes, none covers the engine's -- nearest pay1[Forest#11:G] ; pay2[Arbor Elf#1:G Voyaging Satyr#53:G] ; pay3[(float only)] ; pay4[Forest#13:G] ; pay5[Forest#10:G] ; pay6[(float only)]: Forest#12 tapped by engine only; Chariot of Victory#5 on #0 by engine, #2 by oracle; float engine - oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Voyaging Satyr#53(T), Forest#12(T), Overgrowth#39@10, Nylea, God of the Hunt#38, Utopia Sprawl#49(new)@10, Forest#13(T)(new), Arbor Elf#2(new), Arbor Elf#3(new), Chariot of Victory#5(new)] hand=[] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53, Forest#12, Overgrowth#39@10, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Forest, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30020347 --game-index 20347`
- ... 54 more signatures in flags.jsonl

## UNMODELED (682 flags, 30 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **jumbo_cactuar** x210 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Tooth and Nail + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory + equip:Chariot of Victory}`  
  engine dropped [cast Tooth and Nail]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#10, Arbor Elf#1, Wild Growth#57@10, Utopia Sprawl#49@10, Overgrowth#39@10, Chariot of Victory#5] hand=[Tooth and Nail, Tooth and Nail, Jumbo Cactuar, Chariot of Victory, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30023981 --game-index 23981`
- **jumbo_cactuar** x80 -- T5 m1 `{land=Forest + cast:Tooth and Nail + cast:Finale of Devastation(x1) + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory + equip:Lightning Greaves}`  
  engine dropped [cast Finale of Devastation]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#12, Utopia Sprawl#50@12, Utopia Sprawl#51@12, Lightning Greaves#37, Overgrowth#39@12] hand=[Tooth and Nail, Tooth and Nail, Finale of Devastation, Chariot of Victory, Arbor Elf, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021711 --game-index 21711`
- **jumbo_cactuar** x80 -- T4 m1 `{no land + cast:Tooth and Nail(x1) + cast:Tooth and Nail + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory + equip:Chariot of Victory}`  
  engine dropped [cast Tooth and Nail]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#10, Arbor Elf#1, Wild Growth#57@10, Utopia Sprawl#49@10, Overgrowth#39@10, Chariot of Victory#5] hand=[Tooth and Nail, Tooth and Nail, Jumbo Cactuar, Chariot of Victory, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30023981 --game-index 23981`
- **jumbo_cactuar** x60 -- T5 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation(x1) + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory + equip:Lightning Greaves}`  
  engine dropped [cast Tooth and Nail]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#12, Utopia Sprawl#50@12, Utopia Sprawl#51@12, Lightning Greaves#37, Overgrowth#39@12] hand=[Tooth and Nail, Tooth and Nail, Finale of Devastation, Chariot of Victory, Arbor Elf, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021711 --game-index 21711`
- **jumbo_cactuar** x40 -- T5 m1 `{land=Forest + cast:Tooth and Nail + cast:Finale of Devastation + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory + equip:Lightning Greaves}`  
  engine dropped [cast Finale of Devastation]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#12, Utopia Sprawl#50@12, Utopia Sprawl#51@12, Lightning Greaves#37, Overgrowth#39@12] hand=[Tooth and Nail, Tooth and Nail, Finale of Devastation, Chariot of Victory, Arbor Elf, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021711 --game-index 21711`
- **jumbo_cactuar** x40 -- T5 m1 `{land=Forest + cast:Tooth and Nail + cast:Finale of Devastation(x1) + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory + equip:Lightning Greaves}`  
  engine dropped [cast Tooth and Nail]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#12, Utopia Sprawl#50@12, Utopia Sprawl#51@12, Lightning Greaves#37, Overgrowth#39@12] hand=[Tooth and Nail, Tooth and Nail, Finale of Devastation, Chariot of Victory, Arbor Elf, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021711 --game-index 21711`
- **jumbo_cactuar** x40 -- T5 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory + equip:Lightning Greaves}`  
  engine dropped [cast Tooth and Nail]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#12, Utopia Sprawl#50@12, Utopia Sprawl#51@12, Lightning Greaves#37, Overgrowth#39@12] hand=[Tooth and Nail, Tooth and Nail, Finale of Devastation, Chariot of Victory, Arbor Elf, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021711 --game-index 21711`
- **jumbo_cactuar** x20 -- T3 m1 `{land=Forest + cast:Chariot of Victory + cast:Tooth and Nail + cast:Lightning Greaves + cast:Arbor Elf + equip:Chariot of Victory + equip:Lightning Greaves}`  
  engine dropped [cast Tooth and Nail]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#14, Arbor Elf#4, Forest#10, Overgrowth#39@10] hand=[Chariot of Victory, Tooth and Nail, Forest, Lightning Greaves, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022771 --game-index 22771`
- **jumbo_cactuar** x20 -- T2 m1 `{land=Forest + cast:Wild Growth@10 + cast:Wild Growth@10 + cast:Finale of Devastation + cast:Utopia Sprawl(x1)@10 + cast:Arbor Elf + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Finale of Devastation]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#10, Arbor Elf#1] hand=[Wild Growth, Wild Growth, Finale of Devastation, Utopia Sprawl, Arbor Elf, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30023341 --game-index 23341`
- **jumbo_cactuar** x20 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Jumbo Cactuar + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory + equip:Chariot of Victory}`  
  engine dropped [cast Jumbo Cactuar]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#10, Arbor Elf#1, Wild Growth#57@10, Utopia Sprawl#49@10, Overgrowth#39@10, Chariot of Victory#5] hand=[Tooth and Nail, Tooth and Nail, Jumbo Cactuar, Chariot of Victory, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30023981 --game-index 23981`
- **jumbo_cactuar** x10 -- T4 m1 `{no land + cast:Tooth and Nail(x1) + cast:Tooth and Nail(x1) + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory + equip:Chariot of Victory}`  
  engine dropped [cast Tooth and Nail]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#10, Arbor Elf#1, Wild Growth#57@10, Utopia Sprawl#49@10, Overgrowth#39@10, Chariot of Victory#5] hand=[Tooth and Nail, Tooth and Nail, Jumbo Cactuar, Chariot of Victory, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30023981 --game-index 23981`
- **jumbo_cactuar** x8 -- T5 m1 `{land=Forest + cast:Tooth and Nail(x1) + cast:Finale of Devastation(x1) + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory + equip:Lightning Greaves}`  
  engine dropped [cast Finale of Devastation]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Forest#12, Utopia Sprawl#50@12, Utopia Sprawl#51@12, Lightning Greaves#37, Overgrowth#39@12] hand=[Tooth and Nail, Tooth and Nail, Finale of Devastation, Chariot of Victory, Arbor Elf, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30021711 --game-index 21711`
- ... 18 more signatures in flags.jsonl

## CAPPED-DECISION (5000 flags, 26 signatures)

_the decision spent its priced work budget (MTG_MANA_ORACLE_DECISION_S, default 300 priced s) before this plan's audit finished, so it has no verdict._

- **jumbo_cactuar** x1088 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation(x9) + cast:Utopia Sprawl(x1)@10}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Arbor Elf#2, Chariot of Victory#5] hand=[Tooth and Nail, Jumbo Cactuar, Finale of Devastation, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30024581 --game-index 24581`
- **jumbo_cactuar** x950 -- T5 m1 `{no land + cast:Tooth and Nail + cast:Surrak and Goreclaw + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Overgrowth#40@10] hand=[Tooth and Nail, Surrak and Goreclaw, Tooth and Nail, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022536 --game-index 22536`
- **jumbo_cactuar** x760 -- T5 m1 `{no land + cast:Tooth and Nail(x1) + cast:Surrak and Goreclaw + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Overgrowth#40@10] hand=[Tooth and Nail, Surrak and Goreclaw, Tooth and Nail, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022536 --game-index 22536`
- **jumbo_cactuar** x570 -- T5 m1 `{no land + cast:Tooth and Nail(x2) + cast:Surrak and Goreclaw + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Utopia Sprawl(x1)@10}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Overgrowth#40@10] hand=[Tooth and Nail, Surrak and Goreclaw, Tooth and Nail, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022536 --game-index 22536`
- **jumbo_cactuar** x312 -- T4 m1 `{no land + cast:Tooth and Nail(x1) + cast:Finale of Devastation(x9) + cast:Utopia Sprawl(x1)@10}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Arbor Elf#2, Chariot of Victory#5] hand=[Tooth and Nail, Jumbo Cactuar, Finale of Devastation, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30024581 --game-index 24581`
- **jumbo_cactuar** x190 -- T5 m1 `{no land + cast:Tooth and Nail + cast:Surrak and Goreclaw + cast:Tooth and Nail + cast:Overgrowth@10}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Overgrowth#40@10] hand=[Tooth and Nail, Surrak and Goreclaw, Tooth and Nail, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022536 --game-index 22536`
- **jumbo_cactuar** x190 -- T5 m1 `{no land + cast:Tooth and Nail + cast:Surrak and Goreclaw + cast:Tooth and Nail}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Overgrowth#40@10] hand=[Tooth and Nail, Surrak and Goreclaw, Tooth and Nail, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022536 --game-index 22536`
- **jumbo_cactuar** x180 -- T5 m1 `{no land + cast:Tooth and Nail(x1) + cast:Surrak and Goreclaw + cast:Tooth and Nail(x1) + cast:Utopia Sprawl(x1)@10}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Overgrowth#40@10] hand=[Tooth and Nail, Surrak and Goreclaw, Tooth and Nail, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022536 --game-index 22536`
- **jumbo_cactuar** x165 -- T5 m1 `{no land + cast:Tooth and Nail(x2) + cast:Surrak and Goreclaw + cast:Tooth and Nail(x1) + cast:Overgrowth@10 + cast:Utopia Sprawl(x1)@10}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Overgrowth#40@10] hand=[Tooth and Nail, Surrak and Goreclaw, Tooth and Nail, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022536 --game-index 22536`
- **jumbo_cactuar** x152 -- T5 m1 `{no land + cast:Tooth and Nail(x1) + cast:Surrak and Goreclaw + cast:Tooth and Nail + cast:Overgrowth@10}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Overgrowth#40@10] hand=[Tooth and Nail, Surrak and Goreclaw, Tooth and Nail, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022536 --game-index 22536`
- **jumbo_cactuar** x152 -- T5 m1 `{no land + cast:Tooth and Nail(x1) + cast:Surrak and Goreclaw + cast:Tooth and Nail}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Overgrowth#40@10] hand=[Tooth and Nail, Surrak and Goreclaw, Tooth and Nail, Overgrowth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30022536 --game-index 22536`
- **jumbo_cactuar** x120 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation + cast:Utopia Sprawl(x2)@10}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#10, Arbor Elf#1, Utopia Sprawl#49@10, Voyaging Satyr#53, Overgrowth#39@10, Arbor Elf#2, Chariot of Victory#5] hand=[Tooth and Nail, Jumbo Cactuar, Finale of Devastation, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30024581 --game-index 24581`
- ... 14 more signatures in flags.jsonl

