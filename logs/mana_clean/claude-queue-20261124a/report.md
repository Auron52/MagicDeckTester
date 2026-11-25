# Mana-clean gate `claude-queue-20261124a`

Mana-clean gate run: 1 decks, games (seed 30000000 + game index) hinata 10000-14999; src `7e8f979563a5`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | ORACLE-GAP | UNMODELED | CAPPED | CAPPED-DECISION | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hinata | 5000 | issues | 44395 | 134 | 1235 | 81 | 218 | 13 | 38176 | 840 | 3698 | 12497 | 3804 | 20125 | 20967 | 22 |

**MISS = 1668, ORACLE-GAP = 13.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=210 plans=1759 skipped=0 applies=26132256 frontiers=21400166 pruned=8862665 options=26945615 engine_drops=122 gate_rejects=1422(checked 1400, skipped 0) MISS=0 ORACLE-GAP=0 UNMODELED=109 CAPPED=39 ms=5333913 ===
=== MANA ORACLE classes: PHANTOM=29 DOMINATED-LAND=128 CONTINUATION-DIFFERS=17 DROP-COVERED=52 UNMODELED=109 CAPPED=39 CAPPED-DECISION=51 ===
=== MANA ORACLE enum-gap rejections: noop=0 hidden=0 host_blind=0 cont_land=0 ===
=== MANA ORACLE hit-only cuts: 23533 unpayable-payment subtrees not built ===
=== MANA ORACLE chained feeds: 47305733 fed-tap reorders tried, 5896218 paid ===
=== MANA ORACLE hit-only cuts: 39048 frontiers below a dropped action not built ===
=== MANA ORACLE decision budget: 5 decisions spent it, 45 plans not audited ===
=== MANA ORACLE board memory: 1 searches capped by MTG_MANA_ORACLE_BOARD_MB, 20049637 pruned frontier boards and 49323 hit-only outcome boards given back early ===
=== MANA ORACLE shared boards: 222974 outcome boards held for 4643719 outcomes (4420745 shared); 0 share checks ===
=== MANA ORACLE capped-plan escalation: 232 retried, 188 finished ===
=== MANA ORACLE unmodeled: 98 x engine tapped a mana source outside any payment ===
=== MANA ORACLE unmodeled: 7166 x floating pool too large to enumerate set-asides ===
=== MANA ORACLE unmodeled: 386 x payment enumeration capped ===
=== MANA ORACLE unmodeled: 2108 x tap-ahead enumeration capped ===
=== MANA ORACLE unmodeled: 12 x untap effect chose among different lands ===
=== MANA ORACLE rollback census: lost=0 depth0=0 depth1=0 depth2=0 depth3+=0 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=0 off-menu=0 need_in_plan=0 need_later=0 pay_rollback_rescued=0/0 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | ORACLE-GAP | UNMODELED | CAPPED | CAPPED-DECISION |
|---|---|---|---|---|---|---|---|---|---|---|
| hinata | 51100 | 496497 | 134 | 1235 | 81 | 218 | 13 | 18682 | 840 | 3698 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth1 | depth2 | depth3+ | capped | unfaithful | reorder | off-menu | rescued | need-later |
|---|---|---|---|---|---|---|---|---|---|
| hinata | 683 | 200 | 258 | 92 | 2 | 81 | 134 | 3 | 479 |

## ENUM-GAP (134 flags, 133 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **hinata** x2 -- T4 m1 `{land=Mountain + cast:Hinata, Dawn-Crowned + cast:Magma Opus}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Ornithopter of Paradise#36:W Izzet Boilerworks#24:U Mountain#29:R] ; pay2[Mystic Monastery#32:U Ornithopter of Paradise#37:R]  
  board: board=[Mystic Monastery#32, Ornithopter of Paradise#36, Ornithopter of Paradise#37, Izzet Boilerworks#24] hand=[Remand, Preordain, Hinata, Dawn-Crowned, Magma Opus, Mountain, Crackle with Power] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014620 --game-index 14620`
- **hinata** x1 -- T4 m1 `{land=Island + cast:Hinata, Dawn-Crowned + cast:Magma Opus}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Sol Ring#56:C Ornithopter of Paradise#38:W Island#21:U Mystic Monastery#35:U Cascade Bluffs#2:UR] ; pay2[Reflecting Pool#51:R]  
  board: board=[Mystic Monastery#35, Reflecting Pool#51, Cascade Bluffs#2, Sol Ring#56, Ornithopter of Paradise#38] hand=[Hinata, Dawn-Crowned, Magma Opus, Island, Soulfire Eruption, Crackle with Power] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010033 --game-index 10033`
- **hinata** x1 -- T5 m1 `{land=Reflecting Pool + cast:Hinata, Dawn-Crowned + cast:Magma Opus}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forbidden Orchard#10:W Mystic Monastery#32:U Ornithopter of Paradise#36:W Reflecting Pool#51:R] ; pay2[Mountain#29:R Forbidden Orchard#11:U]  
  board: board=[Forbidden Orchard#10, Mountain#29, Forbidden Orchard#11, Mystic Monastery#32, Ornithopter of Paradise#36] hand=[Hinata, Dawn-Crowned, Reality Spasm, Magma Opus, Crackle with Power, Reality Spasm, Soulfire Eruption, Reflecting Pool] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010069 --game-index 10069`
- **hinata** x1 -- T6 m1 `{no land + cast:Soulfire Eruption + cast:Hinata, Dawn-Crowned}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Island#23:U Ornithopter of Paradise#38:W Forbidden Orchard#10:R Sol Ring#56:C Mystic Monastery#32:R] ; pay2[Ornithopter of Paradise#36:R Mystic Monastery#33:R]  
  board: board=[Island#23, Mystic Monastery#32, Ornithopter of Paradise#38, Forbidden Orchard#10, Ornithopter of Paradise#36, Mystic Monastery#33, Sol Ring#56] hand=[Soulfire Eruption, Reality Spasm, Reality Spasm, Memory Lapse, Hinata, Dawn-Crowned] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010231 --game-index 10231`
- **hinata** x1 -- T8 m1 `{no land + cast:Crackle with Power(x2) + cast:Hinata, Dawn-Crowned}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Forbidden Orchard#11:W Sol Ring#56:C Ornithopter of Paradise#38:U Cascade Bluffs#2:UR] ; pay2[Mystic Monastery#32:U Mystic Monastery#33:U Izzet Boilerworks#24:U Forbidden Orchard#10:R]  
  board: board=[Mystic Monastery#32, Izzet Boilerworks#24, Mystic Monastery#33, Forbidden Orchard#11, Cascade Bluffs#2, Sol Ring#56, Ornithopter of Paradise#38, Forbidden Orchard#10] hand=[Soulfire Eruption, Crackle with Power, Reflecting Pool, Hinata, Dawn-Crowned, Preordain] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010163 --game-index 10163`
- **hinata** x1 -- T5 m1 `{land=Mountain + cast:Reality Spasm(x4) + cast:Crackle with Power(x4) + cast:Sol Ring}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Mountain#30:R] ; pay2[(float only)] ; pay3[Izzet Boilerworks#25:U Forbidden Orchard#10:U Sol Ring#56:C] ; pay4[Izzet Boilerworks#25:U Forbidden Orchard#10:W Forbidden Orchard#11:W Mountain#30:R Sol Ring#56:C]  
  board: board=[Izzet Boilerworks#25, Forbidden Orchard#10, Forbidden Orchard#11, Hinata, Dawn-Crowned#15] hand=[Mountain, Reality Spasm, Crackle with Power, Irencrag Feat, Remand, Sol Ring] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010339 --game-index 10339`
- **hinata** x1 -- T4 m1 `{land=Forbidden Orchard + cast:Hinata, Dawn-Crowned + cast:Magma Opus}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Island#22:U Sol Ring#56:C Ornithopter of Paradise#38:W Forbidden Orchard#13:R] ; pay2[Island#23:U Ornithopter of Paradise#37:R]  
  board: board=[Island#22, Sol Ring#56, Ornithopter of Paradise#38, Island#23, Ornithopter of Paradise#37] hand=[Hinata, Dawn-Crowned, Magma Opus, Forbidden Orchard] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010408 --game-index 10408`
- **hinata** x1 -- T4 m1 `{land=Forbidden Orchard + cast:Hinata, Dawn-Crowned + cast:Magma Opus}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Ornithopter of Paradise#38:W Forbidden Orchard#12:U Sol Ring#56:C Forbidden Orchard#11:R] ; pay2[Island#23:U Reflecting Pool#52:R]  
  board: board=[Island#23, Reflecting Pool#52, Ornithopter of Paradise#38, Forbidden Orchard#12, Sol Ring#56] hand=[Forbidden Orchard, Hinata, Dawn-Crowned, Irencrag Feat, Magma Opus] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010713 --game-index 10713`
- **hinata** x1 -- T6 m1 `{land=Cascade Bluffs + cast:Magma Opus + cast:Hinata, Dawn-Crowned}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Izzet Boilerworks#24:U Mystic Monastery#32:W Cascade Bluffs#1:C] ; pay2[Forbidden Orchard#10:R Island#21:U]  
  board: board=[Izzet Boilerworks#24, Forbidden Orchard#10, Island#21, Mystic Monastery#32] hand=[Crackle with Power, Reality Spasm, Magma Opus, Soulfire Eruption, Cascade Bluffs, Reflecting Pool, Island, Hinata, Dawn-Crowned] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010822 --game-index 10822`
- **hinata** x1 -- T6 m1 `{land=Reflecting Pool + cast:Magma Opus + cast:Hinata, Dawn-Crowned}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Izzet Boilerworks#24:U Mystic Monastery#32:U Reflecting Pool#51:W] ; pay2[Forbidden Orchard#10:R Island#21:U]  
  board: board=[Izzet Boilerworks#24, Forbidden Orchard#10, Island#21, Mystic Monastery#32] hand=[Crackle with Power, Reality Spasm, Magma Opus, Soulfire Eruption, Cascade Bluffs, Reflecting Pool, Island, Hinata, Dawn-Crowned] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010822 --game-index 10822`
- **hinata** x1 -- T6 m1 `{land=Island + cast:Magma Opus + cast:Hinata, Dawn-Crowned}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Izzet Boilerworks#24:U Mystic Monastery#32:W Island#22:U] ; pay2[Forbidden Orchard#10:R Island#21:U]  
  board: board=[Izzet Boilerworks#24, Forbidden Orchard#10, Island#21, Mystic Monastery#32] hand=[Crackle with Power, Reality Spasm, Magma Opus, Soulfire Eruption, Cascade Bluffs, Reflecting Pool, Island, Hinata, Dawn-Crowned] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010822 --game-index 10822`
- **hinata** x1 -- T7 m1 `{land=Mystic Monastery + cast:Soulfire Eruption + cast:Hinata, Dawn-Crowned}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Sol Ring#56:C Mystic Monastery#32:W Island#21:U Forbidden Orchard#10:R Cascade Bluffs#1:UR] ; pay2[Mountain#29:R Forbidden Orchard#11:R]  
  board: board=[Forbidden Orchard#10, Sol Ring#56, Mystic Monastery#32, Mountain#29, Cascade Bluffs#1, Forbidden Orchard#11, Island#21] hand=[Soulfire Eruption, Soulfire Eruption, Memory Lapse, Distorting Wake, Hinata, Dawn-Crowned, Reality Spasm, Mystic Monastery] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010787 --game-index 10787`
- ... 121 more signatures in flags.jsonl

## APPLY-DROP (1235 flags, 542 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **hinata** x43 -- T4 m1 `{no land [ordered] + cast:Reality Spasm(x4) + cast:Gamble + cast:Ponder + cast:Soulfire Eruption}`  
  engine dropped [cast Soulfire Eruption]; every action pays with pay1[(float only)] ; pay2[Mystic Monastery#32:U Reflecting Pool#51:W Ornithopter of Paradise#36:U Cascade Bluffs#1:UU] ; pay3[Cascade Bluffs#1:UR] ; pay4[(float only)] ; pay5[Mystic Monastery#32:R Reflecting Pool#51:R Ornithopter of Paradise#36:R] ; pay6[Forbidden Orchard#12:W], reaching a board the menu's other order of these actions does not (4/4 Creature#500000 only in oracle; life engine 18 oracle 11; hand cards differ; graveyard cards differ (engine vs oracle: +Island -Hinata, Dawn-Crowned); float engine - oracle -) || ROLLBACK depth1: keep the engine's first 3 mana decisions, re-make the rest (decision #5 is the payment th  
  board: board=[Mystic Monastery#32, Reflecting Pool#51, Ornithopter of Paradise#36, Cascade Bluffs#1, Hinata, Dawn-Crowned#15] hand=[Reality Spasm, Remand, Ponder, Soulfire Eruption, Hinata, Dawn-Crowned, Gamble] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30011667 --game-index 11667`
- **hinata** x36 -- T6 m1 `{no land [ordered] + cast:Ponder + cast:Gamble}`  
  engine dropped [cast Preordain]; every action pays with pay1[Ornithopter of Paradise#36:U] ; pay2[Izzet Boilerworks#24:U] ; pay3[Mystic Monastery#32:U Cascade Bluffs#1:UR] ; pay4[(float only)], reaching a board the menu's other order of these actions does not (Ornithopter of Paradise#36 tapped by oracle only; Sol Ring#56 only in engine; Ornithopter of Paradise#37 only in engine; Cascade Bluffs#1 only in oracle; hand size differs; graveyard size differs; library size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 3 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2  
  board: board=[Izzet Boilerworks#24, Mystic Monastery#32, Ornithopter of Paradise#36, Hinata, Dawn-Crowned#15] hand=[Ornithopter of Paradise, Preordain, Crackle with Power, Remand, Soulfire Eruption, Gamble, Crackle with Power, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010618 --game-index 10618`
- **hinata** x18 -- T5 m1 `{no land + cast:Reality Spasm(x4)}`  
  engine dropped [cast Reality Spasm]; every action pays with pay1[(float only)] ; pay2[Mystic Monastery#32:U Reflecting Pool#51:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[(float only)] ; pay2[Mystic Monastery#32:U Reflecting Pool#51:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mystic Monastery#32, Reflecting Pool#51, Mountain#29, Mountain#30, Hinata, Dawn-Crowned#15] hand=[Crackle with Power, Soulfire Eruption, Reality Spasm, Crackle with Power, Island, Forbidden Orchard] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010332 --game-index 10332`
- **hinata** x18 -- T6 m1 `{no land [ordered] + cast:Gamble + cast:Ponder}`  
  engine dropped [cast Preordain]; every action pays with pay1[Izzet Boilerworks#24:U] ; pay2[Ornithopter of Paradise#36:U] ; pay3[Mystic Monastery#32:U Cascade Bluffs#1:UR] ; pay4[(float only)], reaching a board the menu's other order of these actions does not (Ornithopter of Paradise#36 tapped by oracle only; Sol Ring#56 only in engine; Ornithopter of Paradise#37 only in engine; Cascade Bluffs#1 only in oracle; hand size differs; graveyard size differs; library size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 3 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2  
  board: board=[Izzet Boilerworks#24, Mystic Monastery#32, Ornithopter of Paradise#36, Hinata, Dawn-Crowned#15] hand=[Ornithopter of Paradise, Preordain, Crackle with Power, Remand, Soulfire Eruption, Gamble, Crackle with Power, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010618 --game-index 10618`
- **hinata** x14 -- T4 m1 `{no land + cast:Reality Spasm(x4) + cast:Ponder}`  
  engine dropped [cast Reality Spasm]; every action pays with pay1[(float only)] ; pay2[Sol Ring#56:C Mystic Monastery#35:U Reflecting Pool#51:U Mountain#30:R] ; pay3[Reflecting Pool#51:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[(float only)] ; pay2[Sol Ring#56:C Mystic Monastery#35:U Reflecting Pool#51:U Mountain#30:R] ; pay3[Reflecting Pool#51:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mountain#30, Sol Ring#56, Mystic Monastery#35, Reflecting Pool#51, Hinata, Dawn-Crowned#17] hand=[Ponder, Icy Blast, Reality Spasm] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014619 --game-index 14619`
- **hinata** x12 -- T5 m1 `{no land + cast:Ponder + cast:Soulfire Eruption + cast:Ornithopter of Paradise}`  
  engine dropped [cast Soulfire Eruption]; every action pays with pay1[Reflecting Pool#51:U] ; pay2[Sol Ring#56:C] ; pay3[Forbidden Orchard#10:U Forbidden Orchard#11:R Cascade Bluffs#1:RR] ; pay4[Forbidden Orchard#12:U] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Reflecting Pool#51:U] ; pay2[Sol Ring#56:C] ; pay3[Forbidden Orchard#10:U Forbidden Orchard#11:R Cascade Bluffs#1:RR] ; pay4[Forbidden Orchard#12:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forbidden Orchard#10, Sol Ring#56, Forbidden Orchard#11, Cascade Bluffs#1, Hinata, Dawn-Crowned#15, Reflecting Pool#51] hand=[Remand, Memory Lapse, Memory Lapse, Soulfire Eruption, Ornithopter of Paradise, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30013686 --game-index 13686`
- **hinata** x9 -- T6 m1 `{no land [ordered] + cast:Gamble + cast:Preordain}`  
  engine dropped [cast Ponder]; every action pays with pay1[Izzet Boilerworks#24:U] ; pay2[Ornithopter of Paradise#36:U] ; pay3[Mystic Monastery#32:U Cascade Bluffs#1:UR] ; pay4[(float only)], reaching a board the menu's other order of these actions does not (Izzet Boilerworks#25 only in engine; Sol Ring#56 only in engine; Ornithopter of Paradise#37 only in engine; Mystic Monastery#32 only in oracle; Cascade Bluffs#1 only in oracle; hand size differs; graveyard size differs; library size differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 3 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[  
  board: board=[Izzet Boilerworks#24, Mystic Monastery#32, Ornithopter of Paradise#36, Hinata, Dawn-Crowned#15] hand=[Ornithopter of Paradise, Preordain, Crackle with Power, Remand, Soulfire Eruption, Gamble, Crackle with Power, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010618 --game-index 10618`
- **hinata** x9 -- T4 m1 `{no land + cast:Soulfire Eruption + cast:Reality Spasm(x4) + cast:Gamble + cast:Reality Spasm(x4) + cast:Reality Spasm(x4)}`  
  engine dropped [cast Soulfire Eruption]; every action pays with pay1[(float only)] ; pay2[Reflecting Pool#51:U Sol Ring#56:C Ornithopter of Paradise#36:U Mystic Monastery#32:U] ; pay3[(float only)] ; pay4[Reflecting Pool#51:W Sol Ring#56:C Ornithopter of Paradise#36:U Mystic Monastery#32:U] ; pay5[(float only)] ; pay6[Reflecting Pool#51:W Sol Ring#56:C Ornithopter of Paradise#36:U Mystic Monastery#32:R] ; pay7[(float only)] ; pay8[Mystic Monastery#32:R Reflecting Pool#51:R Ornithopter of Paradise#36:R] || ROLLBACK depth3+: keep the engine's first 4 mana decisions, re-make the rest (decision #8 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's ow  
  board: board=[Mystic Monastery#32, Reflecting Pool#51, Sol Ring#56, Ornithopter of Paradise#36, Hinata, Dawn-Crowned#15] hand=[Soulfire Eruption, Reality Spasm, Gamble, Reality Spasm, Reality Spasm, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30011283 --game-index 11283`
- **hinata** x9 -- T4 m1 `{land=Izzet Boilerworks + cast:Ponder + cast:Soulfire Eruption}`  
  engine dropped [cast Soulfire Eruption]; every action pays with pay1[Ornithopter of Paradise#36:U] ; pay2[Forbidden Orchard#10:U Mountain#29:R Cascade Bluffs#1:RR] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Ornithopter of Paradise#36:U] ; pay2[Forbidden Orchard#10:U Mountain#29:R Cascade Bluffs#1:RR]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forbidden Orchard#10, Mountain#29, Ornithopter of Paradise#36, Cascade Bluffs#1, Hinata, Dawn-Crowned#15] hand=[Izzet Boilerworks, Ponder, Mountain, Soulfire Eruption, Crackle with Power] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014006 --game-index 14006`
- **hinata** x8 -- T4 m1 `{land=Izzet Boilerworks + cast:Ponder + cast:Soulfire Eruption}`  
  engine dropped [cast Soulfire Eruption]; every action pays with pay1[Forbidden Orchard#11:U] ; pay2[Forbidden Orchard#10:U Ornithopter of Paradise#36:R Cascade Bluffs#1:RR] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Forbidden Orchard#11:U] ; pay2[Forbidden Orchard#10:U Ornithopter of Paradise#36:R Cascade Bluffs#1:RR]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forbidden Orchard#10, Cascade Bluffs#1, Ornithopter of Paradise#36, Forbidden Orchard#11, Hinata, Dawn-Crowned#15] hand=[Izzet Boilerworks, Ponder, Mountain, Soulfire Eruption, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30012981 --game-index 12981`
- **hinata** x8 -- T5 m1 `{no land + cast:Ponder + cast:Soulfire Eruption}`  
  engine dropped [cast Soulfire Eruption]; every action pays with pay1[Reflecting Pool#51:U] ; pay2[Forbidden Orchard#10:U Forbidden Orchard#11:R Cascade Bluffs#1:RR] ; pay3[Forbidden Orchard#12:U] ; pay4[Sol Ring#56:C] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Reflecting Pool#51:U] ; pay2[Forbidden Orchard#10:U Forbidden Orchard#11:R Cascade Bluffs#1:RR] ; pay3[Forbidden Orchard#12:U] ; pay4[Sol Ring#56:C]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forbidden Orchard#10, Sol Ring#56, Forbidden Orchard#11, Cascade Bluffs#1, Hinata, Dawn-Crowned#15, Reflecting Pool#51] hand=[Remand, Memory Lapse, Memory Lapse, Soulfire Eruption, Ornithopter of Paradise, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30013686 --game-index 13686`
- **hinata** x7 -- T4 m1 `{land=Cascade Bluffs + cast:Ponder + cast:Soulfire Eruption}`  
  engine dropped [cast Soulfire Eruption]; every action pays with pay1[Ornithopter of Paradise#36:U] ; pay2[Mountain#29:R Mystic Monastery#32:U Sol Ring#56:C Cascade Bluffs#2:RR] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Ornithopter of Paradise#36:U] ; pay2[Mountain#29:R Mystic Monastery#32:U Sol Ring#56:C Cascade Bluffs#2:RR]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mountain#29, Mystic Monastery#32, Sol Ring#56, Ornithopter of Paradise#36, Hinata, Dawn-Crowned#15] hand=[Memory Lapse, Ponder, Soulfire Eruption, Cascade Bluffs] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010164 --game-index 10164`
- ... 530 more signatures in flags.jsonl

## APPLY-DROP-ORDER (81 flags, 29 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **hinata** x7 -- T5 m1 `{no land + cast:Irencrag Feat + cast:Soulfire Eruption + cast:Reality Spasm(x5) + cast:Magma Opus}`  
  engine dropped [cast Magma Opus]; the order Reality Spasm > Magma Opus > Irencrag Feat > Soulfire Eruption pays every action with pay1[(float only)] ; pay2[Forbidden Orchard#10:W Reflecting Pool#51:U Sol Ring#56:C Ornithopter of Paradise#37:U Forbidden Orchard#12:U] ; pay3[Ornithopter of Paradise#37:R] ; pay4[Forbidden Orchard#10:R Forbidden Orchard#12:R Reflecting Pool#51:R Sol Ring#56:C] ; pay5[(float only)]  
  board: board=[Forbidden Orchard#10, Reflecting Pool#51, Sol Ring#56, Ornithopter of Paradise#37, Forbidden Orchard#12, Hinata, Dawn-Crowned#15] hand=[Irencrag Feat, Soulfire Eruption, Reality Spasm, Magma Opus] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30012958 --game-index 12958`
- **hinata** x7 -- T5 m1 `{land=Cascade Bluffs + cast:Irencrag Feat + cast:Magma Opus + cast:Reality Spasm(x5) + cast:Soulfire Eruption}`  
  engine dropped [cast Magma Opus]; the order Reality Spasm > Magma Opus > Irencrag Feat > Soulfire Eruption pays every action with pay1[(float only)] ; pay2[Island#21:U Ornithopter of Paradise#36:W Sol Ring#56:C Mountain#29:R Cascade Bluffs#1:UU] ; pay3[Sol Ring#56:C Island#21:U Cascade Bluffs#1:UR] ; pay4[Mountain#29:R Ornithopter of Paradise#36:R] ; pay5[(float only)]  
  board: board=[Mountain#29, Island#21, Ornithopter of Paradise#36, Sol Ring#56, Hinata, Dawn-Crowned#15] hand=[Crackle with Power, Irencrag Feat, Memory Lapse, Magma Opus, Reality Spasm, Soulfire Eruption, Cascade Bluffs] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30013733 --game-index 13733`
- **hinata** x7 -- T5 m1 `{land=Forbidden Orchard + cast:Reality Spasm(x5) + cast:Soulfire Eruption + cast:Magma Opus}`  
  engine dropped [cast Magma Opus]; the order Reality Spasm > Magma Opus > Soulfire Eruption pays every action with pay1[(float only)] ; pay2[Mystic Monastery#32:U Reflecting Pool#52:W Island#22:U Forbidden Orchard#13:U Cascade Bluffs#2:RR] ; pay3[Forbidden Orchard#13:U] ; pay4[Mystic Monastery#32:U Reflecting Pool#52:R Island#22:U Cascade Bluffs#2:RR]  
  board: board=[Mystic Monastery#32, Cascade Bluffs#2, Reflecting Pool#52, Island#22, Hinata, Dawn-Crowned#16] hand=[Reality Spasm, Reality Spasm, Soulfire Eruption, Mountain, Magma Opus, Forbidden Orchard] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014065 --game-index 14065`
- **hinata** x7 -- T5 m1 `{no land + cast:Soulfire Eruption + cast:Reality Spasm(x5) + cast:Magma Opus}`  
  engine dropped [cast Magma Opus]; the order Reality Spasm > Magma Opus > Soulfire Eruption pays every action with pay1[(float only)] ; pay2[Cascade Bluffs#2:C Mystic Monastery#35:U Sol Ring#56:C Reflecting Pool#52:U Island#22:U] ; pay3[Reflecting Pool#52:R] ; pay4[Island#22:U Mystic Monastery#35:R Sol Ring#56:C Cascade Bluffs#2:RR]  
  board: board=[Island#22, Cascade Bluffs#2, Mystic Monastery#35, Sol Ring#56, Reflecting Pool#52, Hinata, Dawn-Crowned#18] hand=[Soulfire Eruption, Crackle with Power, Reality Spasm, Magma Opus] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014945 --game-index 14945`
- **hinata** x6 -- T4 m1 `{land=Mountain + cast:Soulfire Eruption + cast:Magma Opus}`  
  engine dropped [cast Magma Opus]; the order Magma Opus > Soulfire Eruption pays every action with pay1[Island#21:U Mountain#30:R] ; pay2[Mountain#29:R Ornithopter of Paradise#36:R Forbidden Orchard#10:R]  
  board: board=[Island#21, Mountain#29, Ornithopter of Paradise#36, Forbidden Orchard#10, Hinata, Dawn-Crowned#15] hand=[Crackle with Power, Ponder, Crackle with Power, Mountain, Soulfire Eruption, Magma Opus] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30012851 --game-index 12851`
- **hinata** x5 -- T4 m1 `{no land [ordered] + cast:Reality Spasm(x4) + cast:Expressive Iteration + cast:Gamble}`  
  engine dropped [cast Gamble]; the order Gamble > Reality Spasm > Expressive Iteration pays every action with pay1[Reflecting Pool#51:R] ; pay2[Forbidden Orchard#10:U Mystic Monastery#32:U Sol Ring#56:C] ; pay3[(float only)] ; pay4[Mystic Monastery#32:U Sol Ring#56:C Reflecting Pool#51:R Forbidden Orchard#10:W]  
  board: board=[Forbidden Orchard#10, Mystic Monastery#32, Sol Ring#56, Reflecting Pool#51, Hinata, Dawn-Crowned#15] hand=[Distorting Wake, Ponder, Expressive Iteration, Reality Spasm, Gamble, Crackle with Power] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010371 --game-index 10371`
- **hinata** x3 -- T5 m1 `{land=Mountain + cast:Expressive Iteration + cast:Soulfire Eruption}`  
  engine dropped [cast Soulfire Eruption]; the order Soulfire Eruption > Expressive Iteration pays every action with pay1[Mountain#30:R Forbidden Orchard#10:R Mountain#29:R] ; pay2[Mystic Monastery#32:U Cascade Bluffs#2:UR]  
  board: board=[Mystic Monastery#32, Cascade Bluffs#2, Mountain#30, Forbidden Orchard#10, Hinata, Dawn-Crowned#17] hand=[Expressive Iteration, Ornithopter of Paradise, Mountain, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010382 --game-index 10382`
- **hinata** x3 -- T5 m1 `{land=Island + cast:Expressive Iteration + cast:Soulfire Eruption}`  
  engine dropped [cast Soulfire Eruption]; the order Soulfire Eruption > Expressive Iteration pays every action with pay1[Reflecting Pool#52:R Ornithopter of Paradise#36:R Mystic Monastery#33:R] ; pay2[Forbidden Orchard#10:W Mystic Monastery#32:R Island#21:U]  
  board: board=[Forbidden Orchard#10, Mystic Monastery#32, Reflecting Pool#52, Ornithopter of Paradise#36, Mystic Monastery#33, Hinata, Dawn-Crowned#15] hand=[Expressive Iteration, Irencrag Feat, Mystic Monastery, Soulfire Eruption, Island, Izzet Boilerworks] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30012731 --game-index 12731`
- **hinata** x3 -- T5 m1 `{land=Reflecting Pool + cast:Soulfire Eruption + cast:Expressive Iteration + cast:Reality Spasm(x5) + cast:Soulfire Eruption}`  
  engine dropped [cast Soulfire Eruption]; the order Reality Spasm > Soulfire Eruption > Soulfire Eruption > Expressive Iteration pays every action with pay1[(float only)] ; pay2[Forbidden Orchard#10:R Forbidden Orchard#13:R Ornithopter of Paradise#36:R Island#21:U Reflecting Pool#52:U] ; pay3[(float only)] ; pay4[Forbidden Orchard#10:R Ornithopter of Paradise#36:R Reflecting Pool#52:R] ; pay5[Forbidden Orchard#13:R Island#21:U]  
  board: board=[Forbidden Orchard#10, Forbidden Orchard#13, Ornithopter of Paradise#36, Island#21, Hinata, Dawn-Crowned#15] hand=[Soulfire Eruption, Expressive Iteration, Reality Spasm, Distorting Wake, Soulfire Eruption, Reflecting Pool] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014449 --game-index 14449`
- **hinata** x3 -- T5 m2 `{no land + cast:Soulfire Eruption + cast:Expressive Iteration + cast:Reality Spasm(x5) + cast:Soulfire Eruption}`  
  engine dropped [cast Soulfire Eruption]; the order Reality Spasm > Soulfire Eruption > Soulfire Eruption > Expressive Iteration pays every action with pay1[(float only)] ; pay2[Forbidden Orchard#10:R Forbidden Orchard#13:R Ornithopter of Paradise#36:R Island#21:U Reflecting Pool#52:U] ; pay3[(float only)] ; pay4[Forbidden Orchard#10:R Ornithopter of Paradise#36:R Reflecting Pool#52:R] ; pay5[Forbidden Orchard#13:R Island#21:U]  
  board: board=[Forbidden Orchard#10, Forbidden Orchard#13, Ornithopter of Paradise#36, Island#21, Hinata, Dawn-Crowned#15(T), Reflecting Pool#52(new)] hand=[Soulfire Eruption, Expressive Iteration, Reality Spasm, Distorting Wake, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014449 --game-index 14449`
- **hinata** x3 -- T5 m1 `{no land + cast:Magma Opus + cast:Soulfire Eruption + cast:Expressive Iteration + cast:Reality Spasm(x5)}`  
  engine dropped [cast Magma Opus]; the order Expressive Iteration > Magma Opus > Reality Spasm > Soulfire Eruption pays every action with pay1[Mystic Monastery#32:U Ornithopter of Paradise#36:R] ; pay2[Island#21:U] ; pay3[Reflecting Pool#51:R Island#22:U] ; pay4[Ornithopter of Paradise#36:U] ; pay5[Mystic Monastery#32:U] ; pay6[Island#21:U] ; pay7[Reflecting Pool#51:R Island#22:U] ; pay8[Reflecting Pool#51:W Mystic Monastery#32:R Ornithopter of Paradise#36:R] ; pay9[Island#21:U] ; pay10[Island#22:U Reflecting Pool#52:U keep-floating {U}] ; pay11[(float only)]  
  board: board=[Island#21, Reflecting Pool#51, Mystic Monastery#32, Ornithopter of Paradise#36, Island#22, Hinata, Dawn-Crowned#15] hand=[Magma Opus, Soulfire Eruption, Expressive Iteration, Soulfire Eruption, Irencrag Feat, Reality Spasm] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014487 --game-index 14487`
- **hinata** x3 -- T5 m1 `{land=Mountain + cast:Reality Spasm(x5) + cast:Magma Opus + cast:Reality Spasm(x5) + cast:Reality Spasm(x5) + cast:Expressive Iteration}`  
  engine dropped [cast Magma Opus]; the order Reality Spasm > Reality Spasm > Magma Opus > Reality Spasm > Expressive Iteration pays every action with pay1[Forbidden Orchard#12:W Reflecting Pool#52:W Mountain#30:R Mountain#29:R Cascade Bluffs#2:UU] ; pay2[(float only)] ; pay3[Forbidden Orchard#12:W Reflecting Pool#52:W Mountain#30:R Mountain#29:R Cascade Bluffs#2:UU] ; pay4[(float only)] ; pay5[Reflecting Pool#52:U] ; pay6[Forbidden Orchard#12:W Mountain#30:R Mountain#29:R Cascade Bluffs#2:UU] ; pay7[(float only)] ; pay8[Forbidden Orchard#12:W Reflecting Pool#52:W Mountain#29:R Cascade Bluffs#2:UU keep-floating {R}]  
  board: board=[Forbidden Orchard#12, Cascade Bluffs#2, Reflecting Pool#52, Mountain#30, Hinata, Dawn-Crowned#16] hand=[Reality Spasm, Magma Opus, Mountain, Reality Spasm, Reality Spasm, Expressive Iteration] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014791 --game-index 14791`
- ... 17 more signatures in flags.jsonl

## DOMINATED-CREATURE (218 flags, 110 signatures)

_the engine's payment taps a creature that could attack, where another payment keeps it untapped and loses nothing._

- **hinata** x8 -- T4 m1 `{no land + cast:Ponder}`  
  keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Izzet Boilerworks#25:U Cascade Bluffs#2:UU] ; pay2[(float only)]; the engine ended board=[Cascade Bluffs#2(T), Ornithopter of Paradise#37(T), Izzet Boilerworks#25(T), Sol Ring#56, Cascade Bluffs#1(new)] hand=[Soulfire Eruption, Memory Lapse, Hinata, Dawn-Crowned] float={U}{R} life=20  
  board: board=[Cascade Bluffs#2, Ornithopter of Paradise#37, Izzet Boilerworks#25, Sol Ring#56] hand=[Soulfire Eruption, Ponder, Cascade Bluffs, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30011126 --game-index 11126`
- **hinata** x7 -- T5 m1 `{land=Cascade Bluffs + cast:Irencrag Feat + cast:Magma Opus}`  
  keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Izzet Boilerworks#25:U Izzet Boilerworks#24:U Cascade Bluffs#1:UR] ; pay2[(float only)]; the engine ended board=[Izzet Boilerworks#25(T), Ornithopter of Paradise#37(T), Sol Ring#56, Izzet Boilerworks#24(T), Cascade Bluffs#1(T)(new)] hand=[Crackle with Power, Remand, Reality Spasm, Forbidden Orchard, Island, Ornithopter of Paradise] float={R} life=20  
  board: board=[Izzet Boilerworks#25, Ornithopter of Paradise#37, Sol Ring#56, Izzet Boilerworks#24] hand=[Crackle with Power, Remand, Reality Spasm, Irencrag Feat, Magma Opus, Forbidden Orchard, Cascade Bluffs] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010661 --game-index 10661`
- **hinata** x7 -- T3 m1 `{land=Izzet Boilerworks + cast:Ponder + cast:Expressive Iteration}`  
  keeps Forbidden Orchard untapped; keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Island#23:U]; the engine ended board=[Forbidden Orchard#13(T), Ornithopter of Paradise#37(T), Izzet Boilerworks#25(T)(new)] hand=[Expressive Iteration, Reality Spasm, Mountain, Island] float={U}{R} life=20  
  board: board=[Forbidden Orchard#13, Island#23, Ornithopter of Paradise#37] hand=[Izzet Boilerworks, Expressive Iteration, Reality Spasm, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010862 --game-index 10862`
- **hinata** x7 -- T4 m1 `{land=Izzet Boilerworks + cast:Expressive Iteration}`  
  keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Island#21:U Cascade Bluffs#1:UR]; the engine ended board=[Cascade Bluffs#1(T), Sol Ring#56, Ornithopter of Paradise#36(T), Cascade Bluffs#2, Hinata, Dawn-Crowned#15, Ornithopter of Paradise#37, Izzet Boilerworks#24(T)(new)] hand=[Crackle with Power, Reflecting Pool, Crackle with Power, Island] float={R} life=20  
  board: board=[Island#21, Cascade Bluffs#1, Sol Ring#56, Ornithopter of Paradise#36, Cascade Bluffs#2, Hinata, Dawn-Crowned#15, Ornithopter of Paradise#37] hand=[Izzet Boilerworks, Expressive Iteration, Crackle with Power] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30013592 --game-index 13592`
- **hinata** x7 -- T4 m1 `{land=Cascade Bluffs + cast:Ponder + cast:Preordain}`  
  keeps Mystic Monastery untapped; keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Izzet Boilerworks#24:U] ; pay2[Cascade Bluffs#1:UU] ; pay3[(float only)]; the engine ended board=[Mystic Monastery#33(T), Ornithopter of Paradise#37(T), Izzet Boilerworks#24(T), Cascade Bluffs#1(T)(new)] hand=[Icy Blast, Crackle with Power, Remand, Hinata, Dawn-Crowned, Soulfire Eruption, Island] float={U}{R} life=20  
  board: board=[Mystic Monastery#33, Ornithopter of Paradise#37, Izzet Boilerworks#24] hand=[Icy Blast, Crackle with Power, Remand, Ponder, Cascade Bluffs, Hinata, Dawn-Crowned, Preordain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014085 --game-index 14085`
- **hinata** x6 -- T4 m1 `{land=Cascade Bluffs + cast:Ponder}`  
  keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Izzet Boilerworks#25:U Cascade Bluffs#2:UU] ; pay2[(float only)]; the engine ended board=[Cascade Bluffs#2(T), Ornithopter of Paradise#37(T), Izzet Boilerworks#25(T), Sol Ring#56, Cascade Bluffs#1(new)] hand=[Soulfire Eruption, Memory Lapse, Hinata, Dawn-Crowned] float={U}{R} life=20  
  board: board=[Cascade Bluffs#2, Ornithopter of Paradise#37, Izzet Boilerworks#25, Sol Ring#56] hand=[Soulfire Eruption, Ponder, Cascade Bluffs, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30011126 --game-index 11126`
- **hinata** x6 -- T3 m1 `{land=Izzet Boilerworks + cast:Ponder + cast:Expressive Iteration}`  
  keeps Forbidden Orchard untapped; keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Forbidden Orchard#10:U]; the engine ended board=[Forbidden Orchard#11(T), Ornithopter of Paradise#36(T), Izzet Boilerworks#24(T)(new)] hand=[Crackle with Power, Hinata, Dawn-Crowned, Island, Mountain, Expressive Iteration, Forbidden Orchard, Forbidden Orchard] float={U}{R} life=20  
  board: board=[Forbidden Orchard#10, Forbidden Orchard#11, Ornithopter of Paradise#36] hand=[Crackle with Power, Hinata, Dawn-Crowned, Island, Mountain, Ponder, Izzet Boilerworks, Expressive Iteration] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30011759 --game-index 11759`
- **hinata** x5 -- T3 m1 `{land=Forbidden Orchard + cast:Ponder + cast:Preordain + cast:Expressive Iteration}`  
  keeps Mountain untapped; keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Mystic Monastery#32:U] ; pay2[Forbidden Orchard#10:U]; the engine ended board=[Mountain#29(T), Sol Ring#56, Ornithopter of Paradise#36(T), Mystic Monastery#32(T), Forbidden Orchard#10(T)(new)] hand=[Ponder, Expressive Iteration, Mystic Monastery, Preordain, Island] float={U}{R} life=20  
  board: board=[Mountain#29, Sol Ring#56, Ornithopter of Paradise#36, Mystic Monastery#32] hand=[Ponder, Preordain, Ponder, Expressive Iteration, Mystic Monastery, Forbidden Orchard] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010447 --game-index 10447`
- **hinata** x5 -- T3 m1 `{land=Forbidden Orchard + cast:Ponder + cast:Ponder + cast:Expressive Iteration}`  
  keeps Mountain untapped; keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Mystic Monastery#32:U] ; pay2[Forbidden Orchard#10:U]; the engine ended board=[Mountain#29(T), Sol Ring#56, Ornithopter of Paradise#36(T), Mystic Monastery#32(T), Forbidden Orchard#10(T)(new)] hand=[Ponder, Expressive Iteration, Mystic Monastery, Island, Ponder] float={U}{R} life=20  
  board: board=[Mountain#29, Sol Ring#56, Ornithopter of Paradise#36, Mystic Monastery#32] hand=[Ponder, Preordain, Ponder, Expressive Iteration, Mystic Monastery, Forbidden Orchard] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010447 --game-index 10447`
- **hinata** x5 -- T4 m1 `{land=Forbidden Orchard + cast:Preordain + cast:Ponder + cast:Preordain + cast:Expressive Iteration}`  
  keeps Mystic Monastery untapped; keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Izzet Boilerworks#24:U Forbidden Orchard#10:U] ; pay2[(float only)]; the engine ended board=[Mystic Monastery#32(T), Ornithopter of Paradise#36(T), Izzet Boilerworks#24(T), Forbidden Orchard#10(T)(new)] hand=[Irencrag Feat, Remand, Ponder, Expressive Iteration, Mountain, Distorting Wake, Ponder] float={U}{U}{R} life=20  
  board: board=[Mystic Monastery#32, Ornithopter of Paradise#36, Izzet Boilerworks#24] hand=[Irencrag Feat, Remand, Preordain, Ponder, Expressive Iteration, Preordain, Forbidden Orchard, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30012061 --game-index 12061`
- **hinata** x5 -- T7 m1 `{land=Reflecting Pool + cast:Reality Spasm(x5) + cast:Reality Spasm(x5) + cast:Expressive Iteration}`  
  keeps Mountain untapped; keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[(float only)] ; pay2[Mountain#30:R Sol Ring#56:C Mystic Monastery#35:U Reflecting Pool#51:U Ornithopter of Paradise#36:U] ; pay3[(float only)] ; pay4[Mountain#30:R Sol Ring#56:C Mystic Monastery#35:U Reflecting Pool#51:U Ornithopter of Paradise#36:U keep-floating {U}] ; pay5[(float only)] ; pay6[(float only)] ; pay7[Mystic Monastery#35:R Reflecting Pool#51:R]; the engine ended board=[Mountain#30(T), Sol Ring#56, Ornithopter of Paradise#36(T), Mystic Monastery#35(T), Hinata, Dawn-Crowned#16, Reflecting Pool#51(T)(new)] hand=[Icy Blast, Memory Lapse, Island, Cascade Bluffs, Cascade Bluffs, Irencrag Fea  
  board: board=[Mountain#30, Sol Ring#56, Ornithopter of Paradise#36, Mystic Monastery#35, Hinata, Dawn-Crowned#16] hand=[Reality Spasm, Icy Blast, Reality Spasm, Expressive Iteration, Memory Lapse, Reflecting Pool] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30013617 --game-index 13617`
- **hinata** x5 -- T5 m1 `{land=Mystic Monastery + cast:Ponder + cast:Ornithopter of Paradise}`  
  keeps Ornithopter of Paradise untapped (attacker) -- by paying pay1[Izzet Boilerworks#25:U] ; pay2[Izzet Boilerworks#24:U] ; pay3[(float only)]; the engine ended board=[Izzet Boilerworks#25(T), Ornithopter of Paradise#37(T), Hinata, Dawn-Crowned#17, Izzet Boilerworks#24(T), Mystic Monastery#34(T)(new), Ornithopter of Paradise#36(new)] hand=[Distorting Wake, Forbidden Orchard, Hinata, Dawn-Crowned, Crackle with Power] float={R} life=20  
  board: board=[Izzet Boilerworks#25, Ornithopter of Paradise#37, Hinata, Dawn-Crowned#17, Izzet Boilerworks#24] hand=[Distorting Wake, Mystic Monastery, Ponder, Ornithopter of Paradise, Forbidden Orchard, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014575 --game-index 14575`
- ... 98 more signatures in flags.jsonl

## ORACLE-GAP (13 flags, 5 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **hinata** x7 -- T7 m1 `{no land + cast:Ponder}`  
  3590 oracle outcomes, none covers the engine's -- nearest pay1[Reflecting Pool#51:U Ornithopter of Paradise#38:U] ; pay2[Island#22:U]: Izzet Boilerworks#24 tapped by engine only; Cascade Bluffs#2 tapped by engine only; Reflecting Pool#51 tapped by oracle only; Ornithopter of Paradise#38 tapped by oracle only; Island#22 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Izzet Boilerworks#24(T), Cascade Bluffs#2(T), Forbidden Orchard#13, Reflecting Pool#51, Ornithopter of Paradise#38, Mountain#29, Hinata, Dawn-Crowned#16, Island#22(new)] hand=[Reality Spasm, Distorting Wake, Crackle with Power] float=- life=20  
  board: board=[Izzet Boilerworks#24, Cascade Bluffs#2, Forbidden Orchard#13, Reflecting Pool#51, Ornithopter of Paradise#38, Mountain#29, Hinata, Dawn-Crowned#16] hand=[Reality Spasm, Island, Ponder, Remand] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014984 --game-index 14984`
- **hinata** x2 -- T5 m1 `{no land + cast:Ponder + cast:Magma Opus}`  
  20 oracle outcomes, none covers the engine's -- nearest pay1[Izzet Boilerworks#25:U Cascade Bluffs#1:UU] ; pay2[Island#21:U]: Island#21 tapped by oracle only; same core; float engine - oracle {R}; engine ended board=[Forbidden Orchard#12, Izzet Boilerworks#25(T), Cascade Bluffs#1(T), Hinata, Dawn-Crowned#16, Island#21(new)] hand=[Crackle with Power, Distorting Wake, Soulfire Eruption, Magma Opus, Ponder, Mountain] float=- life=20  
  board: board=[Forbidden Orchard#12, Izzet Boilerworks#25, Cascade Bluffs#1, Hinata, Dawn-Crowned#16] hand=[Crackle with Power, Distorting Wake, Soulfire Eruption, Island, Magma Opus, Ponder, Remand] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30013249 --game-index 13249`
- **hinata** x2 -- T3 m1 `{land=Forbidden Orchard [ordered] + cast:Expressive Iteration + cast:Gamble}`  
  11 oracle outcomes, none covers the engine's -- nearest pay1[Island#22:U Sol Ring#56:C Forbidden Orchard#12:R Mystic Monastery#33:R] ; pay2[(float only)]: Sol Ring#56 tapped by oracle only; same core; float engine - oracle {C}{C}; engine ended board=[Island#22(T), Mystic Monastery#33(T), Sol Ring#56, Forbidden Orchard#12(T)(new)] hand=[Mystic Monastery, Preordain, Mountain, Hinata, Dawn-Crowned] float=- life=20  
  board: board=[Island#22, Mystic Monastery#33, Sol Ring#56] hand=[Expressive Iteration, Mystic Monastery, Preordain, Forbidden Orchard, Gamble] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30014814 --game-index 14814`
- **hinata** x1 -- T5 m1 `{no land + cast:Preordain}`  
  54 oracle outcomes, none covers the engine's -- nearest pay1[Mystic Monastery#32:U Cascade Bluffs#1:UU] ; pay2[Island#22:U]: Izzet Boilerworks#24 tapped by engine only; Mystic Monastery#32 tapped by oracle only; Island#22 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Sol Ring#56, Izzet Boilerworks#24(T), Mystic Monastery#32, Cascade Bluffs#1(T), Hinata, Dawn-Crowned#15, Island#22(new)] hand=[Hinata, Dawn-Crowned, Soulfire Eruption, Island, Island, Soulfire Eruption, Preordain] float=- life=20  
  board: board=[Sol Ring#56, Izzet Boilerworks#24, Mystic Monastery#32, Cascade Bluffs#1, Hinata, Dawn-Crowned#15] hand=[Remand, Hinata, Dawn-Crowned, Soulfire Eruption, Island, Island, Island, Preordain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010547 --game-index 10547`
- **hinata** x1 -- T5 m1 `{no land + cast:Preordain}`  
  73 oracle outcomes, none covers the engine's -- nearest pay1[Forbidden Orchard#10:U Cascade Bluffs#1:UU] ; pay2[Forbidden Orchard#11:U]: Izzet Boilerworks#24 tapped by engine only; Forbidden Orchard#10 tapped by oracle only; Forbidden Orchard#11 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Sol Ring#56, Izzet Boilerworks#24(T), Cascade Bluffs#1(T), Forbidden Orchard#10, Hinata, Dawn-Crowned#15, Forbidden Orchard#11(new)] hand=[Irencrag Feat, Memory Lapse, Crackle with Power, Soulfire Eruption, Distorting Wake, Ornithopter of Paradise] float=- life=20  
  board: board=[Sol Ring#56, Izzet Boilerworks#24, Cascade Bluffs#1, Forbidden Orchard#10, Hinata, Dawn-Crowned#15] hand=[Remand, Irencrag Feat, Memory Lapse, Preordain, Forbidden Orchard, Crackle with Power, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30011891 --game-index 11891`

## UNMODELED (18682 flags, 1663 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **hinata** x1131 -- T5 m1 `{no land + cast:Ponder}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Izzet Boilerworks#24, Mystic Monastery#34, Mountain#29, Hinata, Dawn-Crowned#16] hand=[Soulfire Eruption, Crackle with Power, Hinata, Dawn-Crowned, Reality Spasm, Ponder, Reflecting Pool] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010034 --game-index 10034`
- **hinata** x781 -- T4 m1 `{no land + cast:Reality Spasm(x3) + cast:Ponder}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Sol Ring#56, Izzet Boilerworks#25, Forbidden Orchard#12, Hinata, Dawn-Crowned#18] hand=[Izzet Boilerworks, Soulfire Eruption, Ponder, Reality Spasm, Irencrag Feat, Cascade Bluffs] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010023 --game-index 10023`
- **hinata** x326 -- T5 m1 `{no land + cast:Reality Spasm(x4) + cast:Soulfire Eruption}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Forbidden Orchard#10, Mystic Monastery#32, Mountain#29, Mountain#30, Hinata, Dawn-Crowned#15] hand=[Hinata, Dawn-Crowned, Remand, Hinata, Dawn-Crowned, Reality Spasm, Soulfire Eruption, Gamble, Reality Spasm] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010007 --game-index 10007`
- **hinata** x321 -- T5 m1 `{no land + cast:Soulfire Eruption}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Forbidden Orchard#10, Reflecting Pool#51, Ornithopter of Paradise#36, Island#21, Mystic Monastery#32, Hinata, Dawn-Crowned#15] hand=[Soulfire Eruption, Remand, Soulfire Eruption, Crackle with Power, Distorting Wake] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010014 --game-index 10014`
- **hinata** x260 -- T5 m1 `{no land + cast:Ponder + cast:Soulfire Eruption}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Mystic Monastery#32, Mystic Monastery#33, Mystic Monastery#34, Mountain#29, Hinata, Dawn-Crowned#15] hand=[Crackle with Power, Reality Spasm, Reality Spasm, Soulfire Eruption, Crackle with Power, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010072 --game-index 10072`
- **hinata** x258 -- T5 m1 `{no land + cast:Gamble}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Forbidden Orchard#10, Mystic Monastery#32, Mountain#29, Mountain#30, Hinata, Dawn-Crowned#15] hand=[Hinata, Dawn-Crowned, Remand, Hinata, Dawn-Crowned, Reality Spasm, Soulfire Eruption, Gamble, Reality Spasm] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010007 --game-index 10007`
- **hinata** x223 -- T5 m1 `{no land + cast:Expressive Iteration}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Forbidden Orchard#11, Mountain#30, Mystic Monastery#32, Island#23, Hinata, Dawn-Crowned#18] hand=[Soulfire Eruption, Reality Spasm, Crackle with Power, Irencrag Feat, Expressive Iteration, Izzet Boilerworks] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010051 --game-index 10051`
- **hinata** x206 -- T4 m1 `{no land + cast:Reality Spasm(x3) + cast:Ponder + cast:Soulfire Eruption}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Sol Ring#56, Izzet Boilerworks#25, Forbidden Orchard#12, Hinata, Dawn-Crowned#18] hand=[Izzet Boilerworks, Soulfire Eruption, Ponder, Reality Spasm, Irencrag Feat, Cascade Bluffs] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010023 --game-index 10023`
- **hinata** x199 -- T5 m1 `{no land + cast:Preordain}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Reflecting Pool#51, Mountain#29, Ornithopter of Paradise#36, Forbidden Orchard#10, Hinata, Dawn-Crowned#15] hand=[Reality Spasm, Distorting Wake, Distorting Wake, Icy Blast, Ponder, Preordain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010001 --game-index 10001`
- **hinata** x193 -- T4 m1 `{no land [ordered] + cast:Gamble + cast:Ponder}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Cascade Bluffs#2, Sol Ring#56, Ornithopter of Paradise#37, Mystic Monastery#35, Ornithopter of Paradise#38, Reflecting Pool#51, Hinata, Dawn-Crowned#17] hand=[Gamble, Ponder, Icy Blast] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010181 --game-index 10181`
- **hinata** x181 -- T5 m1 `{land=Mountain + cast:Ponder}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Izzet Boilerworks#25, Mystic Monastery#33, Forbidden Orchard#12, Hinata, Dawn-Crowned#15] hand=[Mountain, Remand, Soulfire Eruption, Reality Spasm, Reality Spasm, Ponder, Island] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010101 --game-index 10101`
- **hinata** x167 -- T4 m1 `{no land [ordered] + cast:Ponder + cast:Gamble}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Cascade Bluffs#2, Sol Ring#56, Ornithopter of Paradise#37, Mystic Monastery#35, Ornithopter of Paradise#38, Reflecting Pool#51, Hinata, Dawn-Crowned#17] hand=[Gamble, Ponder, Icy Blast] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010181 --game-index 10181`
- ... 1651 more signatures in flags.jsonl

## CAPPED (840 flags, 307 signatures)

_the payment search hit MTG_MANA_ORACLE_NODES and then the _RETRY escalation (400000); the complete side is partial._

- **hinata** x48 -- T5 m1 `{no land + cast:Soulfire Eruption}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forbidden Orchard#11, Island#22, Ornithopter of Paradise#38, Mystic Monastery#32, Island#21, Hinata, Dawn-Crowned#17] hand=[Reality Spasm, Soulfire Eruption, Reality Spasm, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010508 --game-index 10508`
- **hinata** x43 -- T6 m1 `{no land + cast:Reality Spasm(x5) + cast:Soulfire Eruption}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Izzet Boilerworks#24, Mystic Monastery#32, Forbidden Orchard#13, Hinata, Dawn-Crowned#15, Mystic Monastery#33, Ornithopter of Paradise#37] hand=[Reality Spasm, Mystic Monastery, Island, Soulfire Eruption, Reality Spasm, Distorting Wake] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010111 --game-index 10111`
- **hinata** x27 -- T7 m2 `{no land + cast:Soulfire Eruption + cast:Reality Spasm(x8)}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Mystic Monastery#32(T), Reflecting Pool#51, Ornithopter of Paradise#36, Forbidden Orchard#10, Forbidden Orchard#11, Mountain#29(T), Island#21(T), Island#22(T)(new), Hinata, Dawn-Crowned#15(new)] hand=[Soulfire Eruption, Reality Spasm, Reality Spasm, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010192 --game-index 10192`
- **hinata** x20 -- T6 m2 `{no land + cast:Soulfire Eruption + cast:Reality Spasm(x7) + cast:Reality Spasm(x7)}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Island#23(T), Mystic Monastery#32(T), Ornithopter of Paradise#38, Forbidden Orchard#10, Ornithopter of Paradise#36, Mystic Monastery#33(T), Sol Ring#56(T), Hinata, Dawn-Crowned#17(new)] hand=[Soulfire Eruption, Reality Spasm, Reality Spasm, Memory Lapse] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010231 --game-index 10231`
- **hinata** x20 -- T5 m1 `{no land + cast:Soulfire Eruption}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Forbidden Orchard#10, Ornithopter of Paradise#36, Izzet Boilerworks#24, Mystic Monastery#32, Hinata, Dawn-Crowned#15] hand=[Reality Spasm, Reality Spasm, Hinata, Dawn-Crowned, Soulfire Eruption, Island, Reflecting Pool, Remand] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010599 --game-index 10599`
- **hinata** x18 -- T5 m1 `{no land + cast:Magma Opus + cast:Soulfire Eruption}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forbidden Orchard#10, Cascade Bluffs#1, Ornithopter of Paradise#36, Island#21, Island#23, Hinata, Dawn-Crowned#16] hand=[Memory Lapse, Icy Blast, Magma Opus, Soulfire Eruption, Hinata, Dawn-Crowned] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010482 --game-index 10482`
- **hinata** x13 -- T6 m1 `{no land + cast:Reality Spasm(x5) + cast:Soulfire Eruption + cast:Reality Spasm(x5)}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Izzet Boilerworks#24, Mystic Monastery#32, Forbidden Orchard#13, Hinata, Dawn-Crowned#15, Mystic Monastery#33, Ornithopter of Paradise#37] hand=[Reality Spasm, Mystic Monastery, Island, Soulfire Eruption, Reality Spasm, Distorting Wake] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010111 --game-index 10111`
- **hinata** x13 -- T5 m1 `{no land + cast:Reality Spasm(x6) + cast:Ponder}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forbidden Orchard#12, Forbidden Orchard#13, Ornithopter of Paradise#38, Mystic Monastery#33, Ornithopter of Paradise#37, Reflecting Pool#52, Hinata, Dawn-Crowned#16] hand=[Crackle with Power, Reality Spasm, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30011358 --game-index 11358`
- **hinata** x13 -- T5 m1 `{no land + cast:Soulfire Eruption + cast:Magma Opus}`  
  payment search hit the oracle's board memory budget (MTG_MANA_ORACLE_BOARD_MB=768); the complete side is partial for this plan  
  board: board=[Forbidden Orchard#11, Mountain#29, Ornithopter of Paradise#38, Cascade Bluffs#2, Cascade Bluffs#1, Hinata, Dawn-Crowned#18] hand=[Soulfire Eruption, Magma Opus, Crackle with Power, Island] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010046 --game-index 10046`
- **hinata** x12 -- T8 m1 `{no land + cast:Reality Spasm(x6) + cast:Soulfire Eruption + cast:Reality Spasm(x6) + cast:Expressive Iteration}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Izzet Boilerworks#25, Cascade Bluffs#1, Island#23, Mystic Monastery#35, Mountain#31, Cascade Bluffs#2, Hinata, Dawn-Crowned#16] hand=[Reality Spasm, Soulfire Eruption, Hinata, Dawn-Crowned, Reality Spasm, Expressive Iteration] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30013684 --game-index 13684`
- **hinata** x10 -- T5 m2 `{no land + cast:Reality Spasm(x6) + cast:Reality Spasm(x6) + cast:Soulfire Eruption}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forbidden Orchard#10(T), Mountain#29(T), Forbidden Orchard#11(T), Mystic Monastery#32(T), Ornithopter of Paradise#36, Reflecting Pool#51(new), Hinata, Dawn-Crowned#15(new)] hand=[Reality Spasm, Magma Opus, Crackle with Power, Reality Spasm, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010069 --game-index 10069`
- **hinata** x10 -- T6 m1 `{land=Island + cast:Reality Spasm(x6) + cast:Soulfire Eruption}`  
  payment search hit 400000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Izzet Boilerworks#24, Mystic Monastery#32, Forbidden Orchard#13, Hinata, Dawn-Crowned#15, Mystic Monastery#33, Ornithopter of Paradise#37] hand=[Reality Spasm, Mystic Monastery, Island, Soulfire Eruption, Reality Spasm, Distorting Wake] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010111 --game-index 10111`
- ... 295 more signatures in flags.jsonl

## CAPPED-DECISION (3698 flags, 866 signatures)

_the decision spent its priced work budget (MTG_MANA_ORACLE_DECISION_S, default 1300 priced s) before this plan's audit finished, so it has no verdict._

- **hinata** x81 -- T6 m1 `{no land + cast:Magma Opus}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Cascade Bluffs#1, Sol Ring#56, Mystic Monastery#32, Cascade Bluffs#2, Mystic Monastery#33, Mystic Monastery#34, Hinata, Dawn-Crowned#15] hand=[Magma Opus, Reality Spasm, Soulfire Eruption, Remand, Forbidden Orchard] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010102 --game-index 10102`
- **hinata** x60 -- T5 m1 `{no land + cast:Ponder}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Forbidden Orchard#11, Ornithopter of Paradise#36, Izzet Boilerworks#24, Forbidden Orchard#10, Hinata, Dawn-Crowned#15] hand=[Soulfire Eruption, Distorting Wake, Island, Reality Spasm, Hinata, Dawn-Crowned, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010627 --game-index 10627`
- **hinata** x50 -- T5 m1 `{no land + cast:Reality Spasm(x4) + cast:Magma Opus}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Forbidden Orchard#10, Forbidden Orchard#11, Ornithopter of Paradise#36, Hinata, Dawn-Crowned#15, Izzet Boilerworks#25] hand=[Crackle with Power, Mountain, Reality Spasm, Icy Blast, Magma Opus, Cascade Bluffs, Reality Spasm] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30011059 --game-index 11059`
- **hinata** x45 -- T5 m1 `{no land + cast:Reality Spasm(x4) + cast:Ponder}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Forbidden Orchard#11, Ornithopter of Paradise#36, Izzet Boilerworks#24, Forbidden Orchard#10, Hinata, Dawn-Crowned#15] hand=[Soulfire Eruption, Distorting Wake, Island, Reality Spasm, Hinata, Dawn-Crowned, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010627 --game-index 10627`
- **hinata** x43 -- T5 m1 `{no land + cast:Ponder + cast:Soulfire Eruption}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Forbidden Orchard#11, Ornithopter of Paradise#36, Izzet Boilerworks#24, Forbidden Orchard#10, Hinata, Dawn-Crowned#15] hand=[Soulfire Eruption, Distorting Wake, Island, Reality Spasm, Hinata, Dawn-Crowned, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010627 --game-index 10627`
- **hinata** x42 -- T5 m1 `{no land + cast:Reality Spasm(x4) + cast:Ponder + cast:Soulfire Eruption}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Forbidden Orchard#11, Ornithopter of Paradise#36, Izzet Boilerworks#24, Forbidden Orchard#10, Hinata, Dawn-Crowned#15] hand=[Soulfire Eruption, Distorting Wake, Island, Reality Spasm, Hinata, Dawn-Crowned, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010627 --game-index 10627`
- **hinata** x38 -- T5 m1 `{no land + cast:Magma Opus}`  
  oracle audit backstop (1300 thread-CPU s) spent before this plan was audited  
  board: board=[Forbidden Orchard#10, Mystic Monastery#32, Cascade Bluffs#1, Ornithopter of Paradise#36, Island#21, Hinata, Dawn-Crowned#15] hand=[Crackle with Power, Magma Opus, Reality Spasm, Reality Spasm, Island, Reality Spasm] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010085 --game-index 10085`
- **hinata** x38 -- T5 m1 `{no land + cast:Reality Spasm(x5) + cast:Reality Spasm(x5) + cast:Ponder}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Cascade Bluffs#1, Reflecting Pool#51, Ornithopter of Paradise#36, Mystic Monastery#32, Mountain#29, Hinata, Dawn-Crowned#15] hand=[Memory Lapse, Crackle with Power, Magma Opus, Reality Spasm, Reality Spasm, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010019 --game-index 10019`
- **hinata** x37 -- T5 m1 `{no land + cast:Reality Spasm(x5) + cast:Reality Spasm(x5) + cast:Ponder}`  
  oracle audit backstop (1300 thread-CPU s) spent before this plan was audited  
  board: board=[Forbidden Orchard#10, Cascade Bluffs#1, Mountain#29, Ornithopter of Paradise#38, Island#21, Hinata, Dawn-Crowned#18] hand=[Irencrag Feat, Soulfire Eruption, Reality Spasm, Reality Spasm, Crackle with Power, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30011287 --game-index 11287`
- **hinata** x36 -- T5 m1 `{no land + cast:Ponder}`  
  oracle audit backstop (1300 thread-CPU s) spent before this plan was audited  
  board: board=[Forbidden Orchard#10, Cascade Bluffs#1, Ornithopter of Paradise#36, Mountain#29, Hinata, Dawn-Crowned#15] hand=[Reality Spasm, Crackle with Power, Soulfire Eruption, Ponder, Remand, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010302 --game-index 10302`
- **hinata** x36 -- T5 m1 `{no land + cast:Reality Spasm(x4) + cast:Ponder}`  
  oracle audit backstop (1300 thread-CPU s) spent before this plan was audited  
  board: board=[Forbidden Orchard#10, Cascade Bluffs#1, Ornithopter of Paradise#36, Mountain#29, Hinata, Dawn-Crowned#15] hand=[Reality Spasm, Crackle with Power, Soulfire Eruption, Ponder, Remand, Soulfire Eruption] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30010302 --game-index 10302`
- **hinata** x32 -- T5 m1 `{no land + cast:Reality Spasm(x6) + cast:Reality Spasm(x6) + cast:Ponder + cast:Soulfire Eruption}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=1300 priced s) spent before this plan was audited  
  board: board=[Mystic Monastery#32, Cascade Bluffs#1, Reflecting Pool#51, Sol Ring#56, Ornithopter of Paradise#36, Mystic Monastery#33, Hinata, Dawn-Crowned#15] hand=[Reality Spasm, Reality Spasm, Irencrag Feat, Soulfire Eruption, Ponder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Hinata/Hinata.cod' --profile 'decks/Hinata/Hinata.profile.json' --games 1 --seed 30011507 --game-index 11507`
- ... 854 more signatures in flags.jsonl

