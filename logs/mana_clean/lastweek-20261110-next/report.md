# Mana-clean gate `lastweek-20261110-next`

Mana-clean gate run: 3 decks, games (seed 30000000 + game index) merfolk 115000-119999, jumbo_cactuar 15000-19999, shadowborn_apostle 15000-19999; src `740823c3f482`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | DOMINATED-LIFE | ORACLE-GAP | UNMODELED | CAPPED-DECISION | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| merfolk | 5000 | clean | 0 | 0 | 0 | 0 | 0 | 32 | 0 | 0 | 0 | 2795 | 2 | 37 | 68 | 0 |
| jumbo_cactuar | 5000 | issues | 6947 | 504 | 607 | 638 | 574 | 0 | 75 | 149 | 4400 | 17541 | 4491 | 92 | 154660 | 3 |
| shadowborn_apostle | 5000 | issues | 6846 | 11 | 83 | 550 | 0 | 0 | 35 | 6167 | 0 | 22689 | 150 | 807 | 10455 | 0 |

**MISS = 2967, ORACLE-GAP = 110.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=115836 plans=1713235 skipped=0 applies=666651464 frontiers=543935941 pruned=127198942 options=331859761 engine_drops=55945 gate_rejects=26672469(checked 6154308, skipped 20513761) MISS=2999 ORACLE-GAP=110 UNMODELED=6316 CAPPED=0 ms=13948600 ===
=== MANA ORACLE classes: ORACLE-GAP=110 ENUM-GAP=515 APPLY-DROP=690 APPLY-DROP-ORDER=1188 PHANTOM=43025 DOMINATED-CREATURE=574 DOMINATED-LIFE=32 DOMINATED-LAND=165183 CONTINUATION-DIFFERS=4643 DROP-COVERED=936 UNMODELED=6316 HIDDEN-INFO=3 CAPPED-DECISION=4400 ===
=== MANA ORACLE enum-gap rejections: noop=4 hidden=22 host_blind=15 cont_land=17 ===
=== MANA ORACLE decision budget: 1 decisions spent it, 4400 plans not audited ===
=== MANA ORACLE capped-plan escalation: 665 retried, 665 finished ===
=== MANA ORACLE unmodeled: 930 x floating pool too large to enumerate set-asides ===
=== MANA ORACLE unmodeled: 2461457 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 6316 x too many actions to try every order ===
=== MANA ORACLE rollback census: lost=2393 depth0=53 depth1=420 depth2=73 depth3+=144 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=1188 off-menu=515 need_in_plan=557 need_later=133 pay_rollback_rescued=0/690 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | ORACLE-GAP | UNMODELED | CAPPED-DECISION |
|---|---|---|---|---|---|---|---|---|---|
| merfolk | 37004 | 566021 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| jumbo_cactuar | 35913 | 737011 | 504 | 607 | 638 | 574 | 75 | 149 | 4400 |
| shadowborn_apostle | 42919 | 410203 | 11 | 83 | 550 | 0 | 35 | 5000 | 0 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth0 | depth1 | depth2 | depth3+ | reorder | off-menu | need-later |
|---|---|---|---|---|---|---|---|
| jumbo_cactuar | 0 | 400 | 69 | 138 | 638 | 504 | 124 |
| shadowborn_apostle | 53 | 20 | 4 | 6 | 550 | 11 | 9 |

## ENUM-GAP (515 flags, 205 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **jumbo_cactuar** x20 -- T3 m1 `{land=Forest + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Voyaging Satyr#53:G] ; pay2[Forest#12:G] ; pay3[Forest#10:G] ; pay4[(float only)] ; pay5[Forest#11:G Arbor Elf#1:G]  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Utopia Sprawl#49@10, Voyaging Satyr#53] hand=[Tooth and Nail, Overgrowth, Forest, Chariot of Victory, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015811 --game-index 15811`
- **jumbo_cactuar** x20 -- T3 m1 `{land=Forest + cast:Chariot of Victory + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Voyaging Satyr#53:G Forest#12:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#11:G] ; pay4[Forest#10:G] ; pay5[Voyaging Satyr#54:G]  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Voyaging Satyr#53] hand=[Chariot of Victory, Tooth and Nail, Forest, Overgrowth, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016859 --game-index 16859`
- **jumbo_cactuar** x20 -- T3 m1 `{no land + cast:Wild Growth@10 + cast:Lightning Greaves + cast:Wild Growth@10 + cast:Voyaging Satyr + cast:Arbor Elf + cast:Tooth and Nail + equip:Lightning Greaves + equip:Lightning Greaves}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#2:G] ; pay2[(float only)] ; pay3[Forest#10:G] ; pay4[(float only)] ; pay5[(float only)] ; pay6[Arbor Elf#1:G] ; pay7[(float only)] ; pay8[Voyaging Satyr#53:G] ; pay9[(float only)] ; pay10[(float only)]  
  board: board=[Forest#10, Wild Growth#57@10, Arbor Elf#2] hand=[Overgrowth, Wild Growth, Lightning Greaves, Wild Growth, Voyaging Satyr, Arbor Elf, Tooth and Nail] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017261 --game-index 17261`
- **jumbo_cactuar** x19 -- T5 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@25 + cast:Finale of Devastation(x1)}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#10:G Forest#17:G Forest#13:G] ; pay2[Forest#25:G] ; pay3[Arbor Elf#4:G]  
  board: board=[Forest#25, Wild Growth#58@25, Forest#10, Utopia Sprawl#49@25, Forest#17, Chariot of Victory#5, Forest#13, Surrak and Goreclaw#44] hand=[Tooth and Nail, Overgrowth, Finale of Devastation] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017121 --game-index 17121`
- **jumbo_cactuar** x19 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G] ; pay3[(float only)] ; pay4[Forest#10:G] ; pay5[Voyaging Satyr#54:G keep-floating {W}{W}]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Utopia Sprawl#50@10, Arbor Elf#1] hand=[Tooth and Nail, Overgrowth, Chariot of Victory, Voyaging Satyr, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30019718 --game-index 19718`
- **jumbo_cactuar** x19 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Forest#10:G Arbor Elf#2:G keep-floating {W}{W}{W}]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Voyaging Satyr#53, Utopia Sprawl#50@10, Arbor Elf#1] hand=[Tooth and Nail, Overgrowth, Chariot of Victory, Voyaging Satyr, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30019718 --game-index 19718`
- **jumbo_cactuar** x17 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation(x2) + cast:Arbor Elf}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Arbor Elf#2:G Arbor Elf#1:G Forest#11:G keep-floating {W}{W}{W}{W}]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Utopia Sprawl#50@10, Overgrowth#39@10, Surrak and Goreclaw#43] hand=[Tooth and Nail, Finale of Devastation, Arbor Elf, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015200 --game-index 15200`
- **jumbo_cactuar** x17 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation(x1) + cast:Voyaging Satyr}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Arbor Elf#2:G Voyaging Satyr#53:G Forest#11:G keep-floating {W}{W}{W}{W}]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Utopia Sprawl#50@10, Overgrowth#39@10, Surrak and Goreclaw#43] hand=[Tooth and Nail, Finale of Devastation, Arbor Elf, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015200 --game-index 15200`
- **jumbo_cactuar** x17 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation(x2) + cast:Voyaging Satyr}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#10:G] ; pay2[Arbor Elf#2:G] ; pay3[Voyaging Satyr#53:G Forest#11:G keep-floating {W}{W}{W}]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Utopia Sprawl#50@10, Overgrowth#39@10, Surrak and Goreclaw#43] hand=[Tooth and Nail, Finale of Devastation, Arbor Elf, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015200 --game-index 15200`
- **jumbo_cactuar** x14 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation(x3) + cast:Arbor Elf}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#10:G] ; pay2[Arbor Elf#2:G] ; pay3[Arbor Elf#1:G Forest#11:G keep-floating {W}{W}{W}]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Utopia Sprawl#50@10, Overgrowth#39@10, Surrak and Goreclaw#43] hand=[Tooth and Nail, Finale of Devastation, Arbor Elf, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015200 --game-index 15200`
- **jumbo_cactuar** x14 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation(x4) + cast:Arbor Elf}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forest#10:G Forest#11:G] ; pay2[Arbor Elf#2:G] ; pay3[Arbor Elf#1:G keep-floating {W}{W}]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Utopia Sprawl#50@10, Overgrowth#39@10, Surrak and Goreclaw#43] hand=[Tooth and Nail, Finale of Devastation, Arbor Elf, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015200 --game-index 15200`
- **jumbo_cactuar** x11 -- T4 m1 `{no land + cast:Chariot of Victory + cast:Surrak and Goreclaw + cast:Tooth and Nail + cast:Overgrowth@10 + cast:Arbor Elf + equip:Chariot of Victory}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Arbor Elf#1:G] ; pay2[Voyaging Satyr#53:G Voyaging Satyr#54:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Forest#10:G Arbor Elf#2:G] ; pay6[keep-floating {W}{W}]  
  board: board=[Forest#10, Wild Growth#57@10, Voyaging Satyr#53, Utopia Sprawl#49@10, Arbor Elf#1, Voyaging Satyr#54] hand=[Chariot of Victory, Surrak and Goreclaw, Tooth and Nail, Overgrowth, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30019153 --game-index 19153`
- ... 193 more signatures in flags.jsonl

## APPLY-DROP (690 flags, 118 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **jumbo_cactuar** x40 -- T4 m1 `{no land + cast:Finale of Devastation(x1) + cast:Tooth and Nail}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#10:G Voyaging Satyr#53:G] ; pay2[Forest#11:G Forest#12:G] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Forest#10:G Voyaging Satyr#53:G] ; pay2[Forest#11:G Forest#12:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Utopia Sprawl#50@10, Forest#12, Utopia Sprawl#51@10, Voyaging Satyr#53] hand=[Jumbo Cactuar, Finale of Devastation, Tooth and Nail] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30018840 --game-index 18840`
- **jumbo_cactuar** x40 -- T4 m1 `{land=Forest + cast:Finale of Devastation(x1) + cast:Tooth and Nail}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Voyaging Satyr#53:G Arbor Elf#1:G Forest#22:G Forest#26:G] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Voyaging Satyr#53:G Arbor Elf#1:G Forest#22:G Forest#26:G] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#26, Utopia Sprawl#49@26, Voyaging Satyr#53, Utopia Sprawl#51@26, Arbor Elf#1] hand=[Finale of Devastation, Voyaging Satyr, Tooth and Nail, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30019478 --game-index 19478`
- **jumbo_cactuar** x38 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Finale of Devastation(x1)}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#16:G Voyaging Satyr#54:G Arbor Elf#4:G Forest#27:G] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Forest#16:G Voyaging Satyr#54:G Arbor Elf#4:G Forest#27:G] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#27, Utopia Sprawl#49@27, Forest#16, Voyaging Satyr#54, Utopia Sprawl#52@27, Arbor Elf#4] hand=[Tooth and Nail, Finale of Devastation, Surrak and Goreclaw, Overgrowth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015283 --game-index 15283`
- **shadowborn_apostle** x37 -- T4 m1 `{land=Lake of the Dead + cast:Sire of Insanity}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)] || ROLLBACK depth0: keep the engine's first 0 mana decisions, re-make the rest (decision #1 is the payment that failed) and every action pays: pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Swamp#40, Shadowborn Apostle#21, Blood Crypt#2] hand=[Lotus Bloom, Sire of Insanity, Thrumming Stone, Reaper from the Abyss, Shadowborn Apostle, Lake of the Dead, Swamp] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30015225 --game-index 15225`
- **jumbo_cactuar** x25 -- T3 m1 `{no land + cast:Green Sun's Zenith(x1) + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Chariot of Victory]; every action pays with pay1[Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Arbor Elf#1:G] || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Arbor Elf#1:G]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53] hand=[Jumbo Cactuar, Green Sun's Zenith, Chariot of Victory, Tooth and Nail, Lightning Greaves, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016775 --game-index 16775`
- **jumbo_cactuar** x20 -- T6 m1 `{no land + cast:Tooth and Nail + cast:Chariot of Victory + cast:Voyaging Satyr + cast:Arbor Elf + equip:Chariot of Victory + equip:Lightning Greaves + equip:Lightning Greaves}`  
  engine dropped [cast Tooth and Nail]; every action pays with pay1[Voyaging Satyr#55:G] ; pay2[(float only)] ; pay3[Arbor Elf#3:G keep-floating {G}] ; pay4[(float only)] ; pay5[(float only)] ; pay6[Forest#21:G Forest#19:G Voyaging Satyr#54:G] ; pay7[Forest#25:G] ; pay8[(float only)] ; pay9[(float only)], reaching a board the menu's other order of these actions does not (Chariot of Victory#6 on #54 by engine, #3 by oracle; float engine - oracle -) || ROLLBACK depth3+: keep the engine's first 5 mana decisions, re-make the rest (decision #9 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[(the engine's own)] ; pay4[(the engine's own)]   
  board: board=[Forest#21, Forest#19, Voyaging Satyr#55, Forest#25, Overgrowth#39@21, Lightning Greaves#37] hand=[Tooth and Nail, Chariot of Victory, Voyaging Satyr, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015456 --game-index 15456`
- **jumbo_cactuar** x20 -- T6 m1 `{no land + cast:Tooth and Nail + cast:Chariot of Victory + cast:Voyaging Satyr + cast:Arbor Elf + equip:Lightning Greaves + equip:Lightning Greaves}`  
  engine dropped [cast Tooth and Nail]; every action pays with pay1[Voyaging Satyr#55:G] ; pay2[(float only)] ; pay3[Arbor Elf#3:G keep-floating {G}] ; pay4[(float only)] ; pay5[(float only)] ; pay6[Forest#21:G Forest#19:G Voyaging Satyr#54:G] ; pay7[(float only)] ; pay8[(float only)] || ROLLBACK depth3+: keep the engine's first 5 mana decisions, re-make the rest (decision #9 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[(the engine's own)] ; pay4[(the engine's own)] ; pay5[(the engine's own)] ; pay6[Arbor Elf#3:G] ; pay7[(float only)] ; pay8[Forest#19:G Voyaging Satyr#55:G Voyaging Satyr#54:G] ; pay9[(float only)] ; pay10[(float   
  board: board=[Forest#21, Forest#19, Voyaging Satyr#55, Forest#25, Overgrowth#39@21, Lightning Greaves#37] hand=[Tooth and Nail, Chariot of Victory, Voyaging Satyr, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015456 --game-index 15456`
- **jumbo_cactuar** x20 -- T3 m1 `{land=Forest + cast:Finale of Devastation + cast:Green Sun's Zenith(x1) + cast:Voyaging Satyr}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G Forest#12:G] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Voyaging Satyr#53:G] ; pay3[Forest#11:G Forest#12:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53] hand=[Chariot of Victory, Jumbo Cactuar, Forest, Finale of Devastation, Green Sun's Zenith, Voyaging Satyr] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015925 --game-index 15925`
- **jumbo_cactuar** x16 -- T2 m1 `{land=Forest + cast:Lightning Greaves + cast:Utopia Sprawl(x1)@10 + cast:Finale of Devastation}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#11:G] ; pay2[Arbor Elf#1:G Forest#10:G] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Arbor Elf#1:G Forest#10:G] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Arbor Elf#1] hand=[Forest, Jumbo Cactuar, Lightning Greaves, Utopia Sprawl, Forest, Finale of Devastation] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017286 --game-index 17286`
- **jumbo_cactuar** x15 -- T3 m1 `{no land + cast:Finale of Devastation(x1) + cast:Overgrowth@11 + cast:Utopia Sprawl(x1)@10}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Forest#11:G] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Forest#11:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Overgrowth#39@10] hand=[Finale of Devastation, Chariot of Victory, Overgrowth, Green Sun's Zenith, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016472 --game-index 16472`
- **jumbo_cactuar** x12 -- T4 m1 `{no land + cast:Tooth and Nail(x2) + cast:Finale of Devastation(x1)}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Arbor Elf#4:G Arbor Elf#1:G Arbor Elf#2:G Forest#27:G] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Arbor Elf#4:G Arbor Elf#1:G Arbor Elf#2:G Forest#27:G] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#27, Arbor Elf#4, Utopia Sprawl#49@27, Utopia Sprawl#52@27, Arbor Elf#1, Arbor Elf#2] hand=[Jumbo Cactuar, Tooth and Nail, Finale of Devastation] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016583 --game-index 16583`
- **jumbo_cactuar** x12 -- T4 m1 `{no land + cast:Finale of Devastation(x1) + cast:Tooth and Nail}`  
  engine dropped [cast Finale of Devastation]; every action pays with pay1[Forest#14:G Arbor Elf#1:G Arbor Elf#2:G Forest#15:G] ; pay2[(float only)] ; pay3[Forest#25:G] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Forest#14:G Arbor Elf#1:G Arbor Elf#2:G Forest#15:G] ; pay2[(float only)] ; pay3[Forest#25:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#15, Utopia Sprawl#49@15, Forest#14, Utopia Sprawl#51@15, Arbor Elf#1, Arbor Elf#2] hand=[Finale of Devastation, Jumbo Cactuar, Tooth and Nail, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016621 --game-index 16621`
- ... 106 more signatures in flags.jsonl

## APPLY-DROP-ORDER (1188 flags, 176 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **shadowborn_apostle** x112 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual}`  
  engine dropped [cast Diabolic Intent]; the order Dark Ritual > Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Swamp#41:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Lake of the Dead#14, Swamp#41(new)] hand=[Diabolic Intent, Culling the Weak, Shadowborn Apostle, Swamp, Thrumming Stone, Swamp, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30017425 --game-index 17425`
- **shadowborn_apostle** x112 -- T4 m2 `{no land + cast:Dark Ritual + cast:Shadowborn Apostle + cast:Culling the Weak + cast:Diabolic Intent + cast:Thrumming Stone}`  
  engine dropped [cast Diabolic Intent]; the order Dark Ritual > Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Blood Crypt#2:B] ; pay2[Lake of the Dead#16:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Lake of the Dead#16, Blood Crypt#2(new)] hand=[Dark Ritual, Shadowborn Apostle, Culling the Weak, Abhorrent Overlord, Diabolic Intent, Thrumming Stone] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30019054 --game-index 19054`
- **shadowborn_apostle** x112 -- T3 m2 `{no land + cast:Thrumming Stone + cast:Shadowborn Apostle + cast:Dark Ritual + cast:Culling the Weak + cast:Diabolic Intent}`  
  engine dropped [cast Diabolic Intent]; the order Dark Ritual > Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Swamp#41:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Lake of the Dead#14, Swamp#41(new)] hand=[Thrumming Stone, Shadowborn Apostle, Dark Ritual, Culling the Weak, Diabolic Intent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30019814 --game-index 19814`
- **shadowborn_apostle** x96 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + cast:Culling the Weak}`  
  engine dropped [cast Diabolic Intent]; the order Dark Ritual > Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Swamp#41:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Lake of the Dead#14, Swamp#41(new)] hand=[Diabolic Intent, Shadowborn Apostle, Thrumming Stone, Dark Ritual, Culling the Weak] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30017830 --game-index 17830`
- **shadowborn_apostle** x32 -- T4 m1 `{land=Lake of the Dead + cast:Thrumming Stone + cast:Culling the Weak + cast:Diabolic Intent + cast:Diabolic Intent + cast:Shadowborn Apostle}`  
  engine dropped [cast Diabolic Intent, cast Diabolic Intent]; the order Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent > Diabolic Intent pays every action with pay1[Lake of the Dead#14:B(crack)] ; pay2[(float only)] ; pay3[Swamp#42:B] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#40, Swamp#41, Swamp#42] hand=[Thrumming Stone, Culling the Weak, Diabolic Intent, Reaper from the Abyss, Diabolic Intent, Lake of the Dead, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30015355 --game-index 15355`
- **shadowborn_apostle** x32 -- T4 m1 `{land=Lake of the Dead + cast:Thrumming Stone + cast:Culling the Weak + cast:Diabolic Intent + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; the order Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent > act:Shadowborn Apostle(x1)>Jetmir, Nexus of Revels@37 pays every action with pay1[Lake of the Dead#14:B(crack)] ; pay2[(float only)] ; pay3[Swamp#42:B] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)]  
  board: board=[Swamp#40, Swamp#41, Swamp#42] hand=[Thrumming Stone, Culling the Weak, Diabolic Intent, Reaper from the Abyss, Diabolic Intent, Lake of the Dead, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30015355 --game-index 15355`
- **shadowborn_apostle** x32 -- T4 m1 `{land=Lake of the Dead + cast:Thrumming Stone + cast:Culling the Weak + cast:Diabolic Intent + cast:Shadowborn Apostle}`  
  engine dropped [cast Diabolic Intent]; the order Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Lake of the Dead#14:B(crack)] ; pay2[(float only)] ; pay3[Swamp#42:B] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Swamp#40, Swamp#41, Swamp#42] hand=[Thrumming Stone, Culling the Weak, Diabolic Intent, Reaper from the Abyss, Diabolic Intent, Lake of the Dead, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30015355 --game-index 15355`
- **jumbo_cactuar** x30 -- T3 m1 `{no land + cast:Green Sun's Zenith(x1) + cast:Chariot of Victory + cast:Arbor Elf + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Chariot of Victory > Arbor Elf > Green Sun's Zenith > equip:Chariot of Victory@5 pays every action with pay1[Forest#11:G Voyaging Satyr#53:G] ; pay2[Forest#10:G] ; pay3[(float only)] ; pay4[Arbor Elf#1:G] ; pay5[Forest#12:G]  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53] hand=[Green Sun's Zenith, Chariot of Victory, Forest, Overgrowth, Tooth and Nail, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015333 --game-index 15333`
- **jumbo_cactuar** x25 -- T3 m1 `{no land + cast:Overgrowth@27 + cast:Green Sun's Zenith(x1) + cast:Utopia Sprawl(x1)@26}`  
  engine dropped [cast Green Sun's Zenith]; the order Overgrowth > Utopia Sprawl > Green Sun's Zenith pays every action with pay1[Forest#26:G] ; pay2[Forest#27:G] ; pay3[(float only)]  
  board: board=[Forest#26, Forest#27, Wild Growth#57@26, Utopia Sprawl#50@26] hand=[Overgrowth, Tooth and Nail, Green Sun's Zenith, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015295 --game-index 15295`
- **jumbo_cactuar** x25 -- T3 m1 `{land=Forest + cast:Chariot of Victory + cast:Green Sun's Zenith(x1) + cast:Voyaging Satyr + equip:Chariot of Victory}`  
  engine dropped [cast Chariot of Victory]; the order Chariot of Victory > Voyaging Satyr > Green Sun's Zenith > equip:Chariot of Victory@5 pays every action with pay1[Voyaging Satyr#53:G Forest#12:G] ; pay2[Forest#10:G] ; pay3[Forest#11:G] ; pay4[Voyaging Satyr#54:G]  
  board: board=[Forest#10, Wild Growth#57@10, Forest#11, Voyaging Satyr#53] hand=[Chariot of Victory, Green Sun's Zenith, Voyaging Satyr, Forest, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015325 --game-index 15325`
- **jumbo_cactuar** x25 -- T2 m1 `{land=Forest + cast:Utopia Sprawl(x5)@11 + cast:Utopia Sprawl(x1)@11 + cast:Green Sun's Zenith(x1) + cast:Wild Growth@10}`  
  engine dropped [cast Green Sun's Zenith]; the order Utopia Sprawl > Utopia Sprawl > Wild Growth > Green Sun's Zenith pays every action with pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Forest#11:G] ; pay4[(float only)]  
  board: board=[Forest#10, Wild Growth#57@10] hand=[Utopia Sprawl, Forest, Utopia Sprawl, Green Sun's Zenith, Forest, Wild Growth, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30019899 --game-index 19899`
- **jumbo_cactuar** x25 -- T2 m1 `{land=Forest + cast:Utopia Sprawl(x1)@11 + cast:Green Sun's Zenith(x1) + cast:Wild Growth@11 + cast:Wild Growth@10}`  
  engine dropped [cast Green Sun's Zenith]; the order Utopia Sprawl > Wild Growth > Green Sun's Zenith > Wild Growth pays every action with pay1[Forest#10:G] ; pay2[(float only)] ; pay3[Forest#11:G] ; pay4[(float only)]  
  board: board=[Forest#10, Wild Growth#57@10] hand=[Utopia Sprawl, Forest, Utopia Sprawl, Green Sun's Zenith, Forest, Wild Growth, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30019899 --game-index 19899`
- ... 164 more signatures in flags.jsonl

## DOMINATED-CREATURE (574 flags, 200 signatures)

_the engine's payment taps a creature that could attack, where another payment keeps it untapped and loses nothing._

- **jumbo_cactuar** x20 -- T3 m1 `{no land + cast:Overgrowth@10 + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#11:G] ; pay2[Arbor Elf#1:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Arbor Elf#2(T), Voyaging Satyr#53, Utopia Sprawl#52(new)@10, Overgrowth#39(new)@10] hand=[Jumbo Cactuar, Finale of Devastation] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Arbor Elf#2, Voyaging Satyr#53] hand=[Jumbo Cactuar, Overgrowth, Finale of Devastation, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016365 --game-index 16365`
- **jumbo_cactuar** x15 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@26}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Arbor Elf#2:G] ; pay2[Voyaging Satyr#56:G Arbor Elf#3:G]; the engine ended board=[Forest#26, Arbor Elf#2(T), Utopia Sprawl#51@26, Voyaging Satyr#56(T), Arbor Elf#3(T), Arbor Elf#1(T), Utopia Sprawl#50(new)@26] hand=[Chariot of Victory] float=- life=20  
  board: board=[Forest#26, Arbor Elf#2, Utopia Sprawl#51@26, Voyaging Satyr#56, Arbor Elf#3, Arbor Elf#1] hand=[Tooth and Nail, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015824 --game-index 15824`
- **jumbo_cactuar** x15 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@17}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Arbor Elf#2:G] ; pay2[Voyaging Satyr#54:G Voyaging Satyr#56:G]; the engine ended board=[Forest#17, Arbor Elf#2(T), Utopia Sprawl#49@17, Voyaging Satyr#54(T), Arbor Elf#3(T), Voyaging Satyr#56(T), Utopia Sprawl#52(new)@17] hand=[Chariot of Victory] float=- life=20  
  board: board=[Forest#17, Arbor Elf#2, Utopia Sprawl#49@17, Voyaging Satyr#54, Arbor Elf#3, Voyaging Satyr#56] hand=[Tooth and Nail, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016155 --game-index 16155`
- **jumbo_cactuar** x15 -- T3 m1 `{land=Forest + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Arbor Elf#1:G Arbor Elf#3:G Forest#11:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Arbor Elf#3(T), Arbor Elf#2(T), Forest#12(T)(new), Utopia Sprawl#50(new)@10] hand=[] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Arbor Elf#3, Arbor Elf#2] hand=[Tooth and Nail, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016402 --game-index 16402`
- **jumbo_cactuar** x15 -- T3 m1 `{land=Forest + cast:Wild Growth@10 + cast:Utopia Sprawl(x1)@10 + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#20:G] ; pay2[Voyaging Satyr#55:G] ; pay3[(float only)]; the engine ended board=[Forest#10, Arbor Elf#2(T), Utopia Sprawl#52@10, Voyaging Satyr#55(T), Forest#20(T)(new), Wild Growth#58(new)@10, Utopia Sprawl#51(new)@10, Utopia Sprawl#50(new)@10] hand=[Tooth and Nail] float={W}{W} life=20  
  board: board=[Forest#10, Arbor Elf#2, Utopia Sprawl#52@10, Voyaging Satyr#55] hand=[Tooth and Nail, Wild Growth, Utopia Sprawl, Utopia Sprawl, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017339 --game-index 17339`
- **jumbo_cactuar** x15 -- T3 m1 `{land=Forest + cast:Tooth and Nail + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Arbor Elf#1:G Forest#11:G Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Arbor Elf#2(T), Voyaging Satyr#53(T), Forest#12(T)(new), Utopia Sprawl#51(new)@10] hand=[Jumbo Cactuar] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Arbor Elf#2, Voyaging Satyr#53] hand=[Tooth and Nail, Jumbo Cactuar, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017519 --game-index 17519`
- **jumbo_cactuar** x15 -- T3 m1 `{land=Forest + cast:Overgrowth@10 + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Forest#11:G Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Voyaging Satyr#53(T), Forest#12(T)(new), Utopia Sprawl#49(new)@10, Overgrowth#39(new)@10] hand=[Green Sun's Zenith, Surrak and Goreclaw, Jumbo Cactuar] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53] hand=[Green Sun's Zenith, Overgrowth, Surrak and Goreclaw, Forest, Jumbo Cactuar, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017949 --game-index 17949`
- **jumbo_cactuar** x15 -- T3 m1 `{land=Forest + cast:Overgrowth@10 + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#11:G] ; pay2[Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Wild Growth#57@10, Voyaging Satyr#53(T), Forest#11(T)(new), Utopia Sprawl#49(new)@10, Overgrowth#39(new)@10] hand=[Jumbo Cactuar, Finale of Devastation, Wild Growth] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Wild Growth#57@10, Voyaging Satyr#53] hand=[Overgrowth, Jumbo Cactuar, Finale of Devastation, Wild Growth, Utopia Sprawl, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30018751 --game-index 18751`
- **jumbo_cactuar** x10 -- T3 m1 `{no land + cast:Chariot of Victory + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#11:G] ; pay2[Arbor Elf#1:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Arbor Elf#2(T), Arbor Elf#3, Utopia Sprawl#49(new)@10, Chariot of Victory#5(new)] hand=[Tooth and Nail] float={G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Arbor Elf#2, Arbor Elf#3] hand=[Chariot of Victory, Tooth and Nail, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016024 --game-index 16024`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Jumbo Cactuar + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Arbor Elf#1:G Forest#11:G Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Arbor Elf#2(T), Voyaging Satyr#53(T), Forest#12(T)(new), Utopia Sprawl#49(new)@10, Jumbo Cactuar#33(new)] hand=[] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Arbor Elf#2, Voyaging Satyr#53] hand=[Jumbo Cactuar, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016456 --game-index 16456`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Chariot of Victory + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Arbor Elf#1:G Forest#11:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Arbor Elf#2(T), Forest#12(T)(new), Utopia Sprawl#49(new)@10, Chariot of Victory#5(new)] hand=[Jumbo Cactuar] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Arbor Elf#2] hand=[Forest, Jumbo Cactuar, Chariot of Victory, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30016582 --game-index 16582`
- **jumbo_cactuar** x10 -- T3 m1 `{land=Forest + cast:Tooth and Nail(x1) + cast:Utopia Sprawl(x1)@10}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#12:G] ; pay2[Arbor Elf#1:G Forest#11:G Voyaging Satyr#53:G]; the engine ended board=[Forest#10, Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Arbor Elf#2(T), Voyaging Satyr#53(T), Forest#12(T)(new), Utopia Sprawl#51(new)@10] hand=[Jumbo Cactuar] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Arbor Elf#2, Voyaging Satyr#53] hand=[Tooth and Nail, Jumbo Cactuar, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017519 --game-index 17519`
- ... 188 more signatures in flags.jsonl

## ORACLE-GAP (110 flags, 65 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **jumbo_cactuar** x5 -- T4 m1 `{no land + cast:Tooth and Nail(x2) + cast:Green Sun's Zenith(x7)}`  
  6 oracle outcomes, none covers the engine's -- nearest pay1[Arbor Elf#4:G Arbor Elf#3:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#22:G]: Arbor Elf#1 tapped by oracle only; same core; float engine - oracle {W}{W}{G}{G}{G}{G}; engine ended board=[Forest#22(T), Arbor Elf#4(T), Utopia Sprawl#52@22, Arbor Elf#3(T), Overgrowth#39@22, Overgrowth#42@22, Arbor Elf#1, Jumbo Cactuar#36(new), Surrak and Goreclaw#43(new), Jumbo Cactuar#33(new), Arbor Elf#2(new)] hand=[] float=- life=20  
  board: board=[Forest#22, Arbor Elf#4, Utopia Sprawl#52@22, Arbor Elf#3, Overgrowth#39@22, Overgrowth#42@22, Arbor Elf#1] hand=[Surrak and Goreclaw, Tooth and Nail, Green Sun's Zenith] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30018841 --game-index 18841`
- **jumbo_cactuar** x4 -- T4 m1 `{no land + cast:Jumbo Cactuar + cast:Overgrowth@10 + equip:Lightning Greaves + act:Nylea, God of the Hunt(x3)}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Voyaging Satyr#53:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#10:G] ; pay4[Forest#11:G Forest#12:G] ; pay5[(float only)]: Arbor Elf#1 pump differs; float engine {G}{G} oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Nylea, God of the Hunt#38, Forest#12(T), Overgrowth#39@10, Lightning Greaves#37, Voyaging Satyr#53(T), Overgrowth#40(new)@10, Jumbo Cactuar#33(new)] hand=[] float={G}{G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Nylea, God of the Hunt#38, Forest#12, Overgrowth#39@10, Lightning Greaves#37, Voyaging Satyr#53] hand=[Jumbo Cactuar, Overgrowth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015273 --game-index 15273`
- **jumbo_cactuar** x4 -- T4 m1 `{no land + cast:Jumbo Cactuar + cast:Overgrowth@10 + equip:Lightning Greaves + act:Nylea, God of the Hunt(x3)}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Voyaging Satyr#53:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#10:G] ; pay4[Forest#11:G Forest#12:G] ; pay5[(float only)]: Nylea, God of the Hunt#38 pump differs; float engine {G}{G} oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Nylea, God of the Hunt#38, Forest#12(T), Overgrowth#39@10, Lightning Greaves#37, Voyaging Satyr#53(T), Overgrowth#40(new)@10, Jumbo Cactuar#33(new)] hand=[] float={G}{G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Nylea, God of the Hunt#38, Forest#12, Overgrowth#39@10, Lightning Greaves#37, Voyaging Satyr#53] hand=[Jumbo Cactuar, Overgrowth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015273 --game-index 15273`
- **jumbo_cactuar** x4 -- T4 m1 `{no land + cast:Green Sun's Zenith(x7)}`  
  14 oracle outcomes, none covers the engine's -- nearest pay1[Forest#10:G Arbor Elf#1:G] ; pay2[(float only)]: Forest#11 tapped by engine only; Forest#12 tapped by engine only; Wild Growth#58 on #10 by engine, #11 by oracle; float engine {G}{G}{G} oracle {G}; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Wild Growth#57@10, Utopia Sprawl#49@10, Nylea, God of the Hunt#38, Forest#12(T), Overgrowth#39@10, Lightning Greaves#37, Jumbo Cactuar#33(new), Wild Growth#58(new)@10] hand=[] float={G}{G}{G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Wild Growth#57@10, Utopia Sprawl#49@10, Nylea, God of the Hunt#38, Forest#12, Overgrowth#39@10, Lightning Greaves#37] hand=[Green Sun's Zenith, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30018415 --game-index 18415`
- **shadowborn_apostle** x3 -- T4 m2 `{no land + cast:Sire of Insanity}`  
  3 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:R]: core keys differ in a field not listed here; float engine {R}{R} oracle {R}{R}; engine ended board=[Lake of the Dead#14(T), Thrumming Stone#57, Sire of Insanity#39(new)] hand=[Swamp, Shadowborn Apostle, Diabolic Intent] float={R}{R} life=18  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Lotus Bloom#17(new), Swamp#41(new)] hand=[Sire of Insanity, Swamp, Shadowborn Apostle, Diabolic Intent] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30017302 --game-index 17302`
- **jumbo_cactuar** x2 -- T4 m1 `{no land + cast:Overgrowth@26 + cast:Finale of Devastation + cast:Chariot of Victory}`  
  6 oracle outcomes, none covers the engine's -- nearest pay1[Voyaging Satyr#56:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#26:G]: Voyaging Satyr#56 tapped by oracle only; same core; float engine - oracle {W}{W}{G}{G}{G}; engine ended board=[Forest#26(T), Arbor Elf#1(T), Utopia Sprawl#50@26, Wild Growth#58@26, Voyaging Satyr#56, Overgrowth#40(new)@26, Chariot of Victory#8(new)] hand=[Jumbo Cactuar, Forest] float=- life=20  
  board: board=[Forest#26, Arbor Elf#1, Utopia Sprawl#50@26, Wild Growth#58@26, Voyaging Satyr#56] hand=[Overgrowth, Jumbo Cactuar, Finale of Devastation, Chariot of Victory, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015119 --game-index 15119`
- **jumbo_cactuar** x2 -- T4 m1 `{no land + cast:Jumbo Cactuar + cast:Overgrowth@10 + act:Nylea, God of the Hunt(x3)}`  
  11 oracle outcomes, none covers the engine's -- nearest pay1[Voyaging Satyr#53:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#10:G] ; pay4[Forest#11:G Forest#12:G]: Arbor Elf#1 pump differs; float engine {G}{G} oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Nylea, God of the Hunt#38, Forest#12(T), Overgrowth#39@10, Lightning Greaves#37, Voyaging Satyr#53(T), Overgrowth#40(new)@10, Jumbo Cactuar#33(new)] hand=[] float={G}{G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Nylea, God of the Hunt#38, Forest#12, Overgrowth#39@10, Lightning Greaves#37, Voyaging Satyr#53] hand=[Jumbo Cactuar, Overgrowth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015273 --game-index 15273`
- **jumbo_cactuar** x2 -- T4 m1 `{no land + cast:Jumbo Cactuar + cast:Overgrowth@10 + act:Nylea, God of the Hunt(x3)}`  
  11 oracle outcomes, none covers the engine's -- nearest pay1[Voyaging Satyr#53:G] ; pay2[Arbor Elf#1:G] ; pay3[Forest#10:G] ; pay4[Forest#11:G Forest#12:G]: Nylea, God of the Hunt#38 pump differs; float engine {G}{G} oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Utopia Sprawl#49@10, Nylea, God of the Hunt#38, Forest#12(T), Overgrowth#39@10, Lightning Greaves#37, Voyaging Satyr#53(T), Overgrowth#40(new)@10, Jumbo Cactuar#33(new)] hand=[] float={G}{G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Utopia Sprawl#49@10, Nylea, God of the Hunt#38, Forest#12, Overgrowth#39@10, Lightning Greaves#37, Voyaging Satyr#53] hand=[Jumbo Cactuar, Overgrowth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015273 --game-index 15273`
- **jumbo_cactuar** x2 -- T4 m1 `{no land + cast:Tooth and Nail(x2)}`  
  6 oracle outcomes, none covers the engine's -- nearest pay1[Arbor Elf#1:G Voyaging Satyr#53:G] ; pay2[Forest#10:G]: Voyaging Satyr#53 tapped by oracle only; same core; float engine - oracle {W}{W}{G}{G}{G}; engine ended board=[Forest#10(T), Arbor Elf#1(T), Wild Growth#57@10, Utopia Sprawl#49@10, Overgrowth#39@10, Voyaging Satyr#53, Chariot of Victory#5, Jumbo Cactuar#33(new), Surrak and Goreclaw#43(new), Utopia Sprawl#50(new)@10] hand=[Jumbo Cactuar] float=- life=20  
  board: board=[Forest#10, Arbor Elf#1, Wild Growth#57@10, Utopia Sprawl#49@10, Overgrowth#39@10, Voyaging Satyr#53, Chariot of Victory#5] hand=[Tooth and Nail, Jumbo Cactuar, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015482 --game-index 15482`
- **jumbo_cactuar** x2 -- T4 m1 `{no land + cast:Overgrowth@10 + cast:Finale of Devastation + cast:Wild Growth@10 + act:Nylea, God of the Hunt(x3)}`  
  12 oracle outcomes, none covers the engine's -- nearest pay1[Voyaging Satyr#53:G] ; pay2[(float only)] ; pay3[Arbor Elf#1:G] ; pay4[(float only)] ; pay5[Forest#10:G]: Arbor Elf#1 pump differs; Forest#11 tapped by engine only; float engine {G}{G} oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Voyaging Satyr#53(T), Wild Growth#57@10, Overgrowth#39@10, Nylea, God of the Hunt#38, Wild Growth#58(new)@10, Overgrowth#40(new)@10] hand=[Tooth and Nail] float={G}{G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53, Wild Growth#57@10, Overgrowth#39@10, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Overgrowth, Finale of Devastation, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015491 --game-index 15491`
- **jumbo_cactuar** x2 -- T4 m1 `{no land + cast:Overgrowth@10 + cast:Finale of Devastation + cast:Wild Growth@10 + act:Nylea, God of the Hunt(x3)}`  
  12 oracle outcomes, none covers the engine's -- nearest pay1[Voyaging Satyr#53:G] ; pay2[(float only)] ; pay3[Arbor Elf#1:G] ; pay4[(float only)] ; pay5[Forest#10:G]: Forest#11 tapped by engine only; Voyaging Satyr#53 pump differs; float engine {G}{G} oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Voyaging Satyr#53(T), Wild Growth#57@10, Overgrowth#39@10, Nylea, God of the Hunt#38, Wild Growth#58(new)@10, Overgrowth#40(new)@10] hand=[Tooth and Nail] float={G}{G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53, Wild Growth#57@10, Overgrowth#39@10, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Overgrowth, Finale of Devastation, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015491 --game-index 15491`
- **jumbo_cactuar** x2 -- T4 m1 `{no land + cast:Overgrowth@10 + cast:Finale of Devastation + cast:Wild Growth@10 + act:Nylea, God of the Hunt(x3)}`  
  12 oracle outcomes, none covers the engine's -- nearest pay1[Voyaging Satyr#53:G] ; pay2[(float only)] ; pay3[Arbor Elf#1:G] ; pay4[(float only)] ; pay5[Forest#10:G]: Forest#11 tapped by engine only; Nylea, God of the Hunt#38 pump differs; float engine {G}{G} oracle -; engine ended board=[Forest#10(T), Arbor Elf#1(T), Forest#11(T), Voyaging Satyr#53(T), Wild Growth#57@10, Overgrowth#39@10, Nylea, God of the Hunt#38, Wild Growth#58(new)@10, Overgrowth#40(new)@10] hand=[Tooth and Nail] float={G}{G} life=20  
  board: board=[Forest#10, Arbor Elf#1, Forest#11, Voyaging Satyr#53, Wild Growth#57@10, Overgrowth#39@10, Nylea, God of the Hunt#38] hand=[Tooth and Nail, Overgrowth, Finale of Devastation, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30015491 --game-index 15491`
- ... 53 more signatures in flags.jsonl

## UNMODELED (5149 flags, 214 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **shadowborn_apostle** x416 -- T1 m2 `{no land + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + cast:Culling the Weak + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#52(new)] hand=[Diabolic Intent, Shadowborn Apostle, Thrumming Stone, Swamp, Dark Ritual, Culling the Weak] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30015427 --game-index 15427`
- **shadowborn_apostle** x224 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Lake of the Dead#14, Swamp#41(new)] hand=[Diabolic Intent, Culling the Weak, Shadowborn Apostle, Swamp, Thrumming Stone, Swamp, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30017425 --game-index 17425`
- **shadowborn_apostle** x168 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + cast:Culling the Weak + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#52, Swamp#45] hand=[Diabolic Intent, Shadowborn Apostle, Thrumming Stone, Dark Ritual, Culling the Weak, Sire of Insanity, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30015427 --game-index 15427`
- **shadowborn_apostle** x84 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + cast:Culling the Weak + cast:Dark Ritual}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#52, Swamp#45] hand=[Diabolic Intent, Shadowborn Apostle, Thrumming Stone, Dark Ritual, Culling the Weak, Sire of Insanity, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30015427 --game-index 15427`
- **shadowborn_apostle** x84 -- T4 m2 `{no land + cast:Shadowborn Apostle + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + suspend:Profane Tutor + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Shadowborn Apostle, cast Shadowborn Apostle]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Lake of the Dead#14, Swamp#41] hand=[Profane Tutor, Shadowborn Apostle, Shadowborn Apostle, Thrumming Stone, Dark Ritual, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30016364 --game-index 16364`
- **shadowborn_apostle** x84 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Thrumming Stone + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Diabolic Intent + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Shadowborn Apostle, cast Diabolic Intent, cast Diabolic Intent, cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Swamp#41, Blood Crypt#2(new)] hand=[Diabolic Intent, Thrumming Stone, Thrumming Stone, Diabolic Intent, Shadowborn Apostle, Diabolic Intent, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30016465 --game-index 16465`
- **shadowborn_apostle** x84 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Thrumming Stone + cast:Thrumming Stone + cast:Shadowborn Apostle + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Thrumming Stone, cast Shadowborn Apostle, cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Swamp#41, Blood Crypt#2(new)] hand=[Diabolic Intent, Thrumming Stone, Thrumming Stone, Diabolic Intent, Shadowborn Apostle, Diabolic Intent, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30016465 --game-index 16465`
- **shadowborn_apostle** x84 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Thrumming Stone + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Shadowborn Apostle, cast Diabolic Intent, cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Swamp#41, Blood Crypt#2(new)] hand=[Diabolic Intent, Thrumming Stone, Thrumming Stone, Diabolic Intent, Shadowborn Apostle, Diabolic Intent, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30016465 --game-index 16465`
- **shadowborn_apostle** x72 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Dark Ritual + cast:Thrumming Stone + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent, cast Shadowborn Apostle (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Swamp#41] hand=[Diabolic Intent, Culling the Weak, Shadowborn Apostle, Dark Ritual, Thrumming Stone, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30015900 --game-index 15900`
- **shadowborn_apostle** x72 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Dark Ritual + cast:Thrumming Stone + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Swamp#41] hand=[Diabolic Intent, Culling the Weak, Shadowborn Apostle, Dark Ritual, Thrumming Stone, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30015900 --game-index 15900`
- **shadowborn_apostle** x64 -- T1 m2 `{no land + cast:Shadowborn Apostle + cast:Dark Ritual + cast:Thrumming Stone + cast:Culling the Weak + cast:Diabolic Intent + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#42(new)] hand=[Shadowborn Apostle, Shadowborn Apostle, Dark Ritual, Thrumming Stone, Culling the Weak, Diabolic Intent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30016675 --game-index 16675`
- **shadowborn_apostle** x62 -- T3 m2 `{no land + cast:Dark Ritual + cast:Shadowborn Apostle + cast:Shadowborn Apostle + cast:Dark Ritual + cast:Thrumming Stone + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Shadowborn Apostle, cast Shadowborn Apostle]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Lake of the Dead#14] hand=[Reaper from the Abyss, Lake of the Dead, Dark Ritual, Shadowborn Apostle, Shadowborn Apostle, Dark Ritual, Thrumming Stone] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30015115 --game-index 15115`
- ... 202 more signatures in flags.jsonl

## CAPPED-DECISION (4400 flags, 39 signatures)

_the decision spent its priced work budget (MTG_MANA_ORACLE_DECISION_S, default 300 priced s) before this plan's audit finished, so it has no verdict._

- **jumbo_cactuar** x210 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Chariot of Victory + cast:Tooth and Nail + cast:Jumbo Cactuar + cast:Arbor Elf}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x210 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Tooth and Nail + cast:Jumbo Cactuar + cast:Arbor Elf}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x210 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Chariot of Victory + cast:Tooth and Nail + cast:Arbor Elf + equip:Chariot of Victory}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x210 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Chariot of Victory + cast:Tooth and Nail + cast:Jumbo Cactuar}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x210 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Chariot of Victory + cast:Tooth and Nail + cast:Arbor Elf}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x210 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Tooth and Nail + cast:Jumbo Cactuar}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x210 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Tooth and Nail + cast:Arbor Elf}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x210 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Chariot of Victory + cast:Tooth and Nail}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x210 -- T4 m1 `{no land + cast:Tooth and Nail + cast:Tooth and Nail}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x200 -- T4 m1 `{no land + cast:Tooth and Nail(x1) + cast:Chariot of Victory + cast:Tooth and Nail + cast:Jumbo Cactuar + cast:Arbor Elf}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x200 -- T4 m1 `{no land + cast:Tooth and Nail(x1) + cast:Tooth and Nail + cast:Jumbo Cactuar + cast:Arbor Elf}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- **jumbo_cactuar** x200 -- T4 m1 `{no land + cast:Tooth and Nail(x1) + cast:Chariot of Victory + cast:Tooth and Nail + cast:Arbor Elf + equip:Chariot of Victory}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Forest#10, Utopia Sprawl#49@10, Forest#11, Voyaging Satyr#53, Overgrowth#39@10, Voyaging Satyr#54] hand=[Tooth and Nail, Chariot of Victory, Tooth and Nail, Jumbo Cactuar, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile 'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30017067 --game-index 17067`
- ... 27 more signatures in flags.jsonl

