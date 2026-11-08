# Mana-clean gate `20261108-124250`

Mana-clean gate run: 2 decks, games (seed 30000000 + game index) minotaur 172000-271999, kittyequipment 57000-156999; src `3bda5c647e7c`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | APPLY-DROP | ORACLE-GAP | CAPPED-DECISION | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND |
|---|---|---|---|---|---|---|---|---|---|---|
| minotaur | 100000 | issues | 43 | 23 | 20 | 0 | 2681 | 216 | 224 | 28417 |
| kittyequipment | 100000 | issues | 3560 | 0 | 0 | 3560 | 0 | 0 | 0 | 110106 |

**MISS = 23, ORACLE-GAP = 20.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=1533563 plans=12151300 skipped=0 applies=1841016196 frontiers=1514940297 pruned=85169673 options=631497322 engine_drops=2928 gate_rejects=32498738(checked 31450048, skipped 1045130) MISS=23 ORACLE-GAP=20 UNMODELED=0 CAPPED=0 ms=67894078 ===
=== MANA ORACLE classes: ORACLE-GAP=20 APPLY-DROP=23 PHANTOM=2681 DOMINATED-LAND=138523 CONTINUATION-DIFFERS=216 DROP-COVERED=224 CAPPED-DECISION=3560 ===
=== MANA ORACLE enum-gap rejections: noop=22506 hidden=0 host_blind=0 cont_land=0 ===
=== MANA ORACLE combo routes: 135078 equip-route macros audited, 471505 links, 214522 ms ===
=== MANA ORACLE decision budget: 4 decisions spent it, 3560 plans not audited ===
=== MANA ORACLE unmodeled: 3773831 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 66 x payment enumeration capped ===
=== MANA ORACLE rollback census: lost=23 depth0=0 depth1=19 depth2=0 depth3+=4 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=0 off-menu=0 need_in_plan=9 need_later=14 pay_rollback_rescued=0/23 ===
```

## Per deck

| deck | decisions | plans | APPLY-DROP | ORACLE-GAP | CAPPED-DECISION |
|---|---|---|---|---|---|
| minotaur | 872111 | 5468110 | 23 | 20 | 0 |
| kittyequipment | 661452 | 6211685 | 0 | 0 | 3560 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth1 | depth3+ | need-later |
|---|---|---|---|
| minotaur | 19 | 4 | 14 |

## APPLY-DROP (23 flags, 10 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **minotaur** x5 -- T4 m1 `{no land + vial:Ragemonger + cast:Neheb, the Worthy + cast:Boros Reckoner}`  
  engine dropped [cast Boros Reckoner]; every action pays with pay1[Rakdos Carnarium#45:B] ; pay2[Mountain#30:R] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Rakdos Carnarium#45:B] ; pay2[Mountain#30:R]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Aether Vial#1, Mountain#30, Gnarled Scarhide#21, Burning-Fist Minotaur#9, Aether Vial#2, Rakdos Carnarium#45] hand=[Ragemonger, Neheb, the Worthy, Mountain, Boros Reckoner] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30202734 --game-index 202734`
- **minotaur** x3 -- T4 m1 `{no land + cast:Neheb, the Worthy + vial:Ragemonger + cast:Boros Reckoner}`  
  engine dropped [cast Boros Reckoner]; every action pays with pay1[Rakdos Carnarium#45:B] ; pay2[Mountain#29:R] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Rakdos Carnarium#45:B] ; pay2[Mountain#29:R]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Aether Vial#1, Mountain#29, Slaughter-Priest of Mogis#53, Deathbellow Raider#14, Burning-Fist Minotaur#9, Rakdos Carnarium#45] hand=[Neheb, the Worthy, Ragemonger, Blood Crypt, Boros Reckoner] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30187316 --game-index 187316`
- **minotaur** x2 -- T5 m1 `{land=Rakdos Carnarium + cast:Slaughter-Priest of Mogis + pump:Burning-Fist Minotaur(x1)}`  
  engine dropped [cast Ragemonger]; every action pays with pay1[Rakdos Carnarium#45:B Unclaimed Territory#58:R(creature-only)] ; pay2[Unclaimed Territory#57:C], reaching a board the menu's other order of these actions does not (Burning-Fist Minotaur#9 pump differs; Unclaimed Territory#57 tapped by oracle only; hand size differs; graveyard size differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Rakdos Carnarium#45:B Unclaimed Territory#58:R(creature-only)] ; pay2[Unclaimed Territory#57:C]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does  
  board: board=[Aether Vial#1, Unclaimed Territory#58, Burning-Fist Minotaur#9, Slaughter-Priest of Mogis#53, Rakdos Carnarium#45, Unclaimed Territory#57, Fanatic of Mogis#18] hand=[Kragma Warcaller, Rakdos Carnarium, Mountain, Slaughter-Priest of Mogis, Ragemonger] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30179639 --game-index 179639`
- **minotaur** x2 -- T5 m1 `{no land + cast:Slaughter-Priest of Mogis}`  
  engine dropped [cast Burning-Fist Minotaur]; every action pays with pay1[Rakdos Carnarium#45:B Unclaimed Territory#58:C] ; pay2[Unclaimed Territory#57:R(creature-only)], reaching a board the menu's other order of these actions does not (Unclaimed Territory#58 only in engine; Unclaimed Territory#57 tapped by oracle only; Rakdos Carnarium#46 only in oracle; Burning-Fist Minotaur#10 only in oracle; hand size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Rakdos Carnarium#45:B Unclaimed Territory#58:C] ; pay2[Unclaimed Territory#57:R(creature  
  board: board=[Unclaimed Territory#58, Slaughter-Priest of Mogis#53, Burning-Fist Minotaur#9, Rakdos Carnarium#45, Unclaimed Territory#57, Fanatic of Mogis#18] hand=[Rageblood Shaman, Burning-Fist Minotaur, Sethron, Hurloon General, Rakdos Carnarium, Slaughter-Priest of Mogis] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30205034 --game-index 205034`
- **minotaur** x2 -- T4 m1 `{no land + cast:Burning-Fist Minotaur + cast:Slaughter-Priest of Mogis}`  
  engine dropped [cast Rageblood Shaman]; every action pays with pay1[Secluded Courtyard#49:C] ; pay2[(float only)] ; pay3[Rakdos Carnarium#45:B], reaching a board the menu's other order of these actions does not (Secluded Courtyard#49 only in engine; Rakdos Carnarium#46 only in oracle; Rageblood Shaman#39 only in oracle; hand size differs; spells cast differs; float engine {R} oracle -) || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Secluded Courtyard#49:C] ; pay2[(float only)] ; pay3[Rakdos Carnarium#45:B]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not rec  
  board: board=[Rakdos Carnarium#45, Secluded Courtyard#49, Ragemonger#43, Slaughter-Priest of Mogis#54] hand=[Burning-Fist Minotaur, Rakdos Carnarium, Mountain, Rageblood Shaman, Kragma Warcaller, Slaughter-Priest of Mogis] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30225991 --game-index 225991`
- **minotaur** x2 -- T5 m1 `{no land + cast:Slaughter-Priest of Mogis}`  
  engine dropped [cast Burning-Fist Minotaur]; every action pays with pay1[Rakdos Carnarium#45:B Unclaimed Territory#59:C] ; pay2[Secluded Courtyard#50:R(creature-only)], reaching a board the menu's other order of these actions does not (Unclaimed Territory#59 only in engine; Secluded Courtyard#50 tapped by oracle only; Rakdos Carnarium#46 only in oracle; Burning-Fist Minotaur#10 only in oracle; hand size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Rakdos Carnarium#45:B Unclaimed Territory#59:C] ; pay2[Secluded Courtyard#50:R(creature-on  
  board: board=[Unclaimed Territory#59, Burning-Fist Minotaur#11, Burning-Fist Minotaur#9, Rakdos Carnarium#45, Secluded Courtyard#50, Fanatic of Mogis#20] hand=[Kragma Warcaller, Mountain, Rakdos Carnarium, Slaughter-Priest of Mogis, Burning-Fist Minotaur] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30232695 --game-index 232695`
- **minotaur** x2 -- T5 m1 `{no land + cast:Neheb, the Worthy + cast:Slaughter-Priest of Mogis}`  
  engine dropped [cast Rageblood Shaman]; every action pays with pay1[Secluded Courtyard#47:C] ; pay2[(float only)] ; pay3[Rakdos Carnarium#46:B], reaching a board the menu's other order of these actions does not (Secluded Courtyard#47 only in engine; Rakdos Carnarium#45 only in oracle; Rageblood Shaman#37 only in oracle; hand size differs; spells cast differs; float engine {R} oracle -) || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Secluded Courtyard#47:C] ; pay2[(float only)] ; pay3[Rakdos Carnarium#46:B]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not rec  
  board: board=[Deathbellow Raider#14, Rakdos Carnarium#46, Secluded Courtyard#47, Ragemonger#41, Slaughter-Priest of Mogis#53] hand=[Rageblood Shaman, Kragma Warcaller, Neheb, the Worthy, Mountain, Rakdos Carnarium, Slaughter-Priest of Mogis] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30244570 --game-index 244570`
- **minotaur** x2 -- T4 m1 `{land=Rakdos Carnarium}`  
  engine dropped [cast Boros Reckoner]; every action pays with pay1[Rakdos Carnarium#46:B keep-floating {R}] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Rakdos Carnarium#46:B keep-floating {R}] ; pay2[(float only)]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Aether Vial#1, Rakdos Carnarium#46, Mountain#30, Ragemonger#43, Slaughter-Priest of Mogis#55, Slaughter-Priest of Mogis#54] hand=[Neheb, the Worthy, Neheb, the Worthy, Rakdos Carnarium, Boros Reckoner] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30254165 --game-index 254165`
- **minotaur** x2 -- T5 m1 `{no land + cast:Slaughter-Priest of Mogis + sacoutlet:Slaughter-Priest of Mogis}`  
  engine dropped [cast Boros Reckoner]; every action pays with pay1[(float only)] ; pay2[Rakdos Carnarium#45:B Secluded Courtyard#47:C] ; pay3[Unclaimed Territory#57:W(creature-only)], reaching a board the menu's other order of these actions does not (Secluded Courtyard#47 only in engine; Unclaimed Territory#57 tapped by oracle only; Rakdos Carnarium#46 only in oracle; Boros Reckoner#7 only in oracle; hand size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Rakdos Carnarium#45:B Secluded Courtyard#47:C] ; pay3[Uncl  
  board: board=[Secluded Courtyard#47, Gnarled Scarhide#21, Slaughter-Priest of Mogis#53, Gnarled Scarhide#22, Rakdos Carnarium#45, Unclaimed Territory#57, Ragemonger#41, Neheb, the Worthy#34] hand=[Rageblood Shaman, Boros Reckoner, Rakdos Carnarium, Slaughter-Priest of Mogis] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30257679 --game-index 257679`
- **minotaur** x1 -- T4 m1 `{land=Rakdos Carnarium + cast:Neheb, the Worthy + vial:Ragemonger + cast:Boros Reckoner}`  
  engine dropped [cast Boros Reckoner]; every action pays with pay1[Rakdos Carnarium#45:B] ; pay2[Mountain#29:R] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Rakdos Carnarium#45:B] ; pay2[Mountain#29:R]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mountain#29, Aether Vial#1, Slaughter-Priest of Mogis#53, Slaughter-Priest of Mogis#54, Gnarled Scarhide#22, Rakdos Carnarium#45] hand=[Neheb, the Worthy, Ragemonger, Rakdos Carnarium, Secluded Courtyard, Boros Reckoner] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30269855 --game-index 269855`

## ORACLE-GAP (20 flags, 9 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **minotaur** x4 -- T5 m1 `{no land + cast:Slaughter-Priest of Mogis + sacoutlet:Slaughter-Priest of Mogis}`  
  11 oracle outcomes, none covers the engine's -- nearest pay1[(float only)] ; pay2[Secluded Courtyard#50:C Secluded Courtyard#49:C] ; pay3[Rakdos Carnarium#45:B]: Slaughter-Priest of Mogis#55 pump differs; Secluded Courtyard#49 tapped by oracle only; 2/3 Minotaur Token#1001 only in engine; Slaughter-Priest of Mogis#56 pump differs; graveyard size differs; float engine - oracle -; engine ended board=[Aether Vial#1, Rakdos Carnarium#45(T), Ragemonger#44, Slaughter-Priest of Mogis#55, Secluded Courtyard#49, Sethron, Hurloon General#51, Rageblood Shaman#40, 2/3 Minotaur Token#1001, Slaughter-Priest of Mogis#56(new), 2/3 Minotaur Token#1002(new), Rakdos Carnarium#46(T)(new)] hand=[Secluded Courtya  
  board: board=[Aether Vial#1, Rakdos Carnarium#45, Secluded Courtyard#50, Ragemonger#44, Slaughter-Priest of Mogis#55, Secluded Courtyard#49, Sethron, Hurloon General#51, 2/3 Minotaur Token#1000, Rageblood Shaman#40, 2/3 Minotaur Token#1001] hand=[Rakdos Carnarium, Slaughter-Priest of Mogis] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30172680 --game-index 172680`
- **minotaur** x2 -- T5 m1 `{no land + cast:Slaughter-Priest of Mogis}`  
  9 oracle outcomes, none covers the engine's -- nearest pay1[Unclaimed Territory#60:B(creature-only) Unclaimed Territory#59:R(creature-only)] ; pay2[Rakdos Carnarium#45:B]: Unclaimed Territory#59 tapped by oracle only; Boros Reckoner#7 only in engine; Slaughter-Priest of Mogis#55 pump differs; graveyard size differs; float engine - oracle -; engine ended board=[Rakdos Carnarium#45(T), Unclaimed Territory#59, Boros Reckoner#7, Aether Vial#1, Slaughter-Priest of Mogis#55(new), Rakdos Carnarium#46(T)(new)] hand=[Kragma Warcaller, Blood Crypt, Cavern of Souls, Unclaimed Territory] float=- life=18  
  board: board=[Rakdos Carnarium#45, Unclaimed Territory#60, Unclaimed Territory#59, Boros Reckoner#7, Aether Vial#1] hand=[Kragma Warcaller, Rakdos Carnarium, Blood Crypt, Cavern of Souls, Slaughter-Priest of Mogis] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30179487 --game-index 179487`
- **minotaur** x2 -- T5 m1 `{no land + cast:Slaughter-Priest of Mogis + sacoutlet:Slaughter-Priest of Mogis}`  
  21 oracle outcomes, none covers the engine's -- nearest pay1[(float only)] ; pay2[Rakdos Carnarium#45:B Secluded Courtyard#49:C] ; pay3[Unclaimed Territory#57:C]: Slaughter-Priest of Mogis#53 pump differs; Burning-Fist Minotaur#11 only in engine; Unclaimed Territory#57 tapped by oracle only; Slaughter-Priest of Mogis#56 pump differs; graveyard size differs; float engine - oracle -; engine ended board=[Slaughter-Priest of Mogis#53, Burning-Fist Minotaur#11, Rakdos Carnarium#45(T), Unclaimed Territory#57, Ragemonger#43, Aether Vial#2, Slaughter-Priest of Mogis#56(new), Rakdos Carnarium#46(T)(new)] hand=[Secluded Courtyard] float=- life=20  
  board: board=[Gnarled Scarhide#24, Secluded Courtyard#49, Slaughter-Priest of Mogis#53, Burning-Fist Minotaur#11, Rakdos Carnarium#45, Unclaimed Territory#57, Ragemonger#43, Aether Vial#2] hand=[Rakdos Carnarium, Slaughter-Priest of Mogis] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30190170 --game-index 190170`
- **minotaur** x2 -- T5 m1 `{no land + cast:Slaughter-Priest of Mogis}`  
  9 oracle outcomes, none covers the engine's -- nearest pay1[Secluded Courtyard#47:B(creature-only) Cavern of Souls#13:R(creature-only)] ; pay2[Rakdos Carnarium#45:B]: Gnarled Scarhide#21 only in engine; Cavern of Souls#13 tapped by oracle only; Slaughter-Priest of Mogis#53 pump differs; graveyard size differs; float engine - oracle -; engine ended board=[Gnarled Scarhide#21, Rakdos Carnarium#45(T), Rageblood Shaman#37, Cavern of Souls#13, Fanatic of Mogis#18, Slaughter-Priest of Mogis#53(new), Rakdos Carnarium#46(T)(new)] hand=[Bloodstained Mire, Sethron, Hurloon General, Ragemonger, Secluded Courtyard] float=- life=20  
  board: board=[Gnarled Scarhide#21, Rakdos Carnarium#45, Secluded Courtyard#47, Rageblood Shaman#37, Cavern of Souls#13, Fanatic of Mogis#18] hand=[Bloodstained Mire, Sethron, Hurloon General, Rakdos Carnarium, Slaughter-Priest of Mogis, Ragemonger] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30194060 --game-index 194060`
- **minotaur** x2 -- T4 m1 `{no land + cast:Neheb, the Worthy + vial:Ragemonger + cast:Slaughter-Priest of Mogis}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Rakdos Carnarium#46:B Secluded Courtyard#49:C] ; pay2[(float only)] ; pay3[(float only)]: Deathbellow Raider#14 only in engine; Slaughter-Priest of Mogis#55 pump differs; graveyard size differs; float engine {R} oracle -; engine ended board=[Aether Vial#2(T), Deathbellow Raider#14, Rakdos Carnarium#46(T), Ragemonger#44(new), Neheb, the Worthy#35(new), Slaughter-Priest of Mogis#55(new), Rakdos Carnarium#45(T)(new)] hand=[Blood Crypt, Secluded Courtyard] float={R} life=17  
  board: board=[Aether Vial#2, Secluded Courtyard#49, Deathbellow Raider#14, Rakdos Carnarium#46] hand=[Neheb, the Worthy, Ragemonger, Rakdos Carnarium, Blood Crypt, Slaughter-Priest of Mogis] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30198859 --game-index 198859`
- **minotaur** x2 -- T5 m1 `{no land + cast:Slaughter-Priest of Mogis}`  
  9 oracle outcomes, none covers the engine's -- nearest pay1[Unclaimed Territory#58:B(creature-only) Unclaimed Territory#60:R(creature-only)] ; pay2[Rakdos Carnarium#45:B]: Gnarled Scarhide#22 only in engine; Unclaimed Territory#60 tapped by oracle only; Slaughter-Priest of Mogis#53 pump differs; graveyard size differs; float engine - oracle -; engine ended board=[Gnarled Scarhide#22, Rakdos Carnarium#45(T), Rageblood Shaman#37, Unclaimed Territory#60, Fanatic of Mogis#20, Slaughter-Priest of Mogis#53(new), Rakdos Carnarium#46(T)(new)] hand=[Blood Crypt, Unclaimed Territory] float=- life=20  
  board: board=[Gnarled Scarhide#22, Rakdos Carnarium#45, Unclaimed Territory#58, Rageblood Shaman#37, Unclaimed Territory#60, Fanatic of Mogis#20] hand=[Rakdos Carnarium, Slaughter-Priest of Mogis, Blood Crypt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30199414 --game-index 199414`
- **minotaur** x2 -- T4 m1 `{no land + cast:Neheb, the Worthy + cast:Slaughter-Priest of Mogis}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Unclaimed Territory#57:C] ; pay2[(float only)] ; pay3[Rakdos Carnarium#45:B]: Ragemonger#41 only in engine; Slaughter-Priest of Mogis#53 pump differs; Slaughter-Priest of Mogis#54 pump differs; graveyard size differs; float engine {R} oracle -; engine ended board=[Rakdos Carnarium#45(T), Ragemonger#41, Slaughter-Priest of Mogis#53, Neheb, the Worthy#34(new), Slaughter-Priest of Mogis#54(new), Rakdos Carnarium#46(T)(new)] hand=[Fanatic of Mogis, Sethron, Hurloon General, Kragma Warcaller, Unclaimed Territory] float={R} life=20  
  board: board=[Rakdos Carnarium#45, Unclaimed Territory#57, Ragemonger#41, Slaughter-Priest of Mogis#53] hand=[Rakdos Carnarium, Fanatic of Mogis, Sethron, Hurloon General, Kragma Warcaller, Neheb, the Worthy, Slaughter-Priest of Mogis] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30200106 --game-index 200106`
- **minotaur** x2 -- T5 m1 `{no land + cast:Slaughter-Priest of Mogis}`  
  17 oracle outcomes, none covers the engine's -- nearest pay1[Rakdos Carnarium#45:B Secluded Courtyard#47:C] ; pay2[Secluded Courtyard#48:C]: Deathbellow Raider#14 only in engine; Secluded Courtyard#48 tapped by oracle only; Slaughter-Priest of Mogis#56 pump differs; graveyard size differs; float engine - oracle -; engine ended board=[Deathbellow Raider#14, Secluded Courtyard#48, Neheb, the Worthy#34, Gnarled Scarhide#23, Rakdos Carnarium#45(T), Slaughter-Priest of Mogis#56(new), Rakdos Carnarium#46(T)(new)] hand=[Kragma Warcaller, Blood Crypt, Secluded Courtyard] float=- life=17  
  board: board=[Secluded Courtyard#47, Deathbellow Raider#14, Secluded Courtyard#48, Neheb, the Worthy#34, Gnarled Scarhide#23, Rakdos Carnarium#45] hand=[Kragma Warcaller, Rakdos Carnarium, Blood Crypt, Slaughter-Priest of Mogis] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30242178 --game-index 242178`
- **minotaur** x2 -- T4 m1 `{no land + cast:Neheb, the Worthy + cast:Slaughter-Priest of Mogis}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Unclaimed Territory#59:C] ; pay2[(float only)] ; pay3[Rakdos Carnarium#45:B]: Gnarled Scarhide#21 only in engine; Slaughter-Priest of Mogis#55 pump differs; graveyard size differs; float engine {R} oracle -; engine ended board=[Gnarled Scarhide#21, Rakdos Carnarium#45(T), Ragemonger#41, Neheb, the Worthy#36(new), Slaughter-Priest of Mogis#55(new), Rakdos Carnarium#46(T)(new)] hand=[Fanatic of Mogis, Unclaimed Territory] float={R} life=20  
  board: board=[Gnarled Scarhide#21, Rakdos Carnarium#45, Unclaimed Territory#59, Ragemonger#41] hand=[Neheb, the Worthy, Rakdos Carnarium, Fanatic of Mogis, Slaughter-Priest of Mogis] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Minotaur/Minotaur.cod' --profile 'decks/Minotaur/Minotaur.profile.json' --games 1 --seed 30250386 --game-index 250386`

## CAPPED-DECISION (3560 flags, 3321 signatures)

_the decision spent its priced work budget (MTG_MANA_ORACLE_DECISION_S, default 300 priced s) before this plan's audit finished, so it has no verdict._

- **kittyequipment** x3 -- T3 m1 `{no land + cast:Skateboard + cast:Golem-Skin Gauntlets + cast:Colossus Hammer + equip:Golem-Skin Gauntlets}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Remote Farm#45, Sram, Senior Edificer#57, Cathar's Shield#14, Spidersilk Net#53, Remote Farm#46, Accorder's Shield#1] hand=[Skateboard, Golem-Skin Gauntlets, Dragonfire Blade, Shadowspear, Colossus Hammer, Ancient Den] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30119192 --game-index 119192`
- **kittyequipment** x3 -- T3 m1 `{land=Plains + cast:Bone Saw + cast:Accorder's Shield + cast:Golem-Skin Gauntlets + cast:Colossus Hammer + equip:Bone Saw + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Remote Farm#45, Ancient Den#6, Sram, Senior Edificer#57, Bone Saw#10, Cathar's Shield#14, Cathar's Shield#15, Golem-Skin Gauntlets#25, Bone Saw#11, Kite Shield#29, Kite Shield#30] hand=[Golem-Skin Gauntlets, Colossus Hammer, Shadowspear, Bone Saw, Plains, Accorder's Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30128380 --game-index 128380`
- **kittyequipment** x3 -- T3 m1 `{land=Plains + cast:Bone Saw + cast:Accorder's Shield + cast:Golem-Skin Gauntlets + cast:Shadowspear + equip:Bone Saw + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Remote Farm#45, Ancient Den#6, Sram, Senior Edificer#57, Bone Saw#10, Cathar's Shield#14, Cathar's Shield#15, Golem-Skin Gauntlets#25, Bone Saw#11, Kite Shield#29, Kite Shield#30] hand=[Golem-Skin Gauntlets, Colossus Hammer, Shadowspear, Bone Saw, Plains, Accorder's Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30128380 --game-index 128380`
- **kittyequipment** x3 -- T3 m1 `{no land + cast:Bone Saw + cast:Accorder's Shield + cast:Golem-Skin Gauntlets + cast:Colossus Hammer + equip:Bone Saw + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Remote Farm#45, Ancient Den#6, Sram, Senior Edificer#57, Bone Saw#10, Cathar's Shield#14, Cathar's Shield#15, Golem-Skin Gauntlets#25, Bone Saw#11, Kite Shield#29, Kite Shield#30] hand=[Golem-Skin Gauntlets, Colossus Hammer, Shadowspear, Bone Saw, Plains, Accorder's Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30128380 --game-index 128380`
- **kittyequipment** x3 -- T3 m1 `{no land + cast:Bone Saw + cast:Accorder's Shield + cast:Golem-Skin Gauntlets + cast:Shadowspear + equip:Bone Saw + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Remote Farm#45, Ancient Den#6, Sram, Senior Edificer#57, Bone Saw#10, Cathar's Shield#14, Cathar's Shield#15, Golem-Skin Gauntlets#25, Bone Saw#11, Kite Shield#29, Kite Shield#30] hand=[Golem-Skin Gauntlets, Colossus Hammer, Shadowspear, Bone Saw, Plains, Accorder's Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30128380 --game-index 128380`
- **kittyequipment** x3 -- T3 m1 `{land=Plains + cast:Bone Saw + cast:Accorder's Shield + cast:Golem-Skin Gauntlets + equip:Bone Saw + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Remote Farm#45, Ancient Den#6, Sram, Senior Edificer#57, Bone Saw#10, Cathar's Shield#14, Cathar's Shield#15, Golem-Skin Gauntlets#25, Bone Saw#11, Kite Shield#29, Kite Shield#30] hand=[Golem-Skin Gauntlets, Colossus Hammer, Shadowspear, Bone Saw, Plains, Accorder's Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30128380 --game-index 128380`
- **kittyequipment** x3 -- T3 m1 `{no land + cast:Bone Saw + cast:Accorder's Shield + cast:Golem-Skin Gauntlets + equip:Bone Saw + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Remote Farm#45, Ancient Den#6, Sram, Senior Edificer#57, Bone Saw#10, Cathar's Shield#14, Cathar's Shield#15, Golem-Skin Gauntlets#25, Bone Saw#11, Kite Shield#29, Kite Shield#30] hand=[Golem-Skin Gauntlets, Colossus Hammer, Shadowspear, Bone Saw, Plains, Accorder's Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30128380 --game-index 128380`
- **kittyequipment** x3 -- T4 m1 `{land=Plains + cast:Bone Saw + cast:Kite Shield + cast:Shadowspear + cast:Deconstruction Hammer + cast:Golem-Skin Gauntlets + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Ancient Den#6, Plains#33, Sram, Senior Edificer#57, Cathar's Shield#14, Cathar's Shield#15, Kite Shield#29, Ancient Den#7, Colossus Hammer#18, Colossus Hammer#19, Golem-Skin Gauntlets#25, Bone Saw#10] hand=[Plains, Shadowspear, Deconstruction Hammer, Golem-Skin Gauntlets, Bone Saw, Kite Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30143280 --game-index 143280`
- **kittyequipment** x3 -- T4 m1 `{no land + cast:Bone Saw + cast:Kite Shield + cast:Shadowspear + cast:Deconstruction Hammer + cast:Golem-Skin Gauntlets + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Ancient Den#6, Plains#33, Sram, Senior Edificer#57, Cathar's Shield#14, Cathar's Shield#15, Kite Shield#29, Ancient Den#7, Colossus Hammer#18, Colossus Hammer#19, Golem-Skin Gauntlets#25, Bone Saw#10] hand=[Plains, Shadowspear, Deconstruction Hammer, Golem-Skin Gauntlets, Bone Saw, Kite Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30143280 --game-index 143280`
- **kittyequipment** x3 -- T4 m1 `{land=Plains + cast:Bone Saw + cast:Kite Shield + cast:Shadowspear + cast:Golem-Skin Gauntlets + equip:Colossus Hammer + equip:Colossus Hammer + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Ancient Den#6, Plains#33, Sram, Senior Edificer#57, Cathar's Shield#14, Cathar's Shield#15, Kite Shield#29, Ancient Den#7, Colossus Hammer#18, Colossus Hammer#19, Golem-Skin Gauntlets#25, Bone Saw#10] hand=[Plains, Shadowspear, Deconstruction Hammer, Golem-Skin Gauntlets, Bone Saw, Kite Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30143280 --game-index 143280`
- **kittyequipment** x3 -- T4 m1 `{land=Plains + cast:Bone Saw + cast:Kite Shield + cast:Deconstruction Hammer + cast:Golem-Skin Gauntlets + equip:Colossus Hammer + equip:Colossus Hammer + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Ancient Den#6, Plains#33, Sram, Senior Edificer#57, Cathar's Shield#14, Cathar's Shield#15, Kite Shield#29, Ancient Den#7, Colossus Hammer#18, Colossus Hammer#19, Golem-Skin Gauntlets#25, Bone Saw#10] hand=[Plains, Shadowspear, Deconstruction Hammer, Golem-Skin Gauntlets, Bone Saw, Kite Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30143280 --game-index 143280`
- **kittyequipment** x3 -- T4 m1 `{no land + cast:Bone Saw + cast:Kite Shield + cast:Shadowspear + cast:Golem-Skin Gauntlets + equip:Colossus Hammer + equip:Colossus Hammer + equip:Golem-Skin Gauntlets + equip:Bone Saw}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this gate-rejected plan was audited  
  board: board=[Ancient Den#6, Plains#33, Sram, Senior Edificer#57, Cathar's Shield#14, Cathar's Shield#15, Kite Shield#29, Ancient Den#7, Colossus Hammer#18, Colossus Hammer#19, Golem-Skin Gauntlets#25, Bone Saw#10] hand=[Plains, Shadowspear, Deconstruction Hammer, Golem-Skin Gauntlets, Bone Saw, Kite Shield] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/KittyEquipment/KittyEquipment.cod' --profile 'decks/KittyEquipment/KittyEquipment.profile.json' --games 1 --seed 30143280 --game-index 143280`
- ... 3309 more signatures in flags.jsonl

