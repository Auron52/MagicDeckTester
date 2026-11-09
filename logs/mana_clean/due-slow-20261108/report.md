# Mana-clean gate `due-slow-20261108`

Mana-clean gate run: 2 decks, games (seed 30000000 + game index) accomplishedalchemist 5000-9999, eldrazidisplacerflicker 5000-9999; src `3bda5c647e7c`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | DOMINATED-LIFE | DOMINATED-OTHER | ORACLE-GAP | UNMODELED | CAPPED | CAPPED-DECISION | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| accomplishedalchemist | 5000 | issues | 262032 | 26479 | 8231 | 1721 | 8933 | 143 | 0 | 1549 | 12395 | 21413 | 181311 | 29606 | 6431 | 5468 | 126497 | 1715 |
| eldrazidisplacerflicker | 5000 | issues | 670720 | 1867 | 2260 | 364 | 0 | 70092 | 6675 | 46 | 356311 | 73961 | 229236 | 42398 | 2154 | 18405 | 43046 | 0 |

**MISS = 26212, ORACLE-GAP = 1595.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=79708 plans=1843523 skipped=0 applies=3810033133 frontiers=3412288826 pruned=1723324774 options=3573138492 engine_drops=158183 gate_rejects=2961866(checked 2693324, skipped 95369) MISS=126765 ORACLE-GAP=1595 UNMODELED=368706 CAPPED=95374 ms=1679179650 ===
=== MANA ORACLE classes: ORACLE-GAP=1595 ENUM-GAP=28346 APPLY-DROP=10491 APPLY-DROP-ORDER=2085 PHANTOM=72004 DOMINATED-CREATURE=8933 DOMINATED-LIFE=70235 DOMINATED-OTHER=6675 DOMINATED-LAND=169543 CONTINUATION-DIFFERS=8585 DROP-COVERED=23873 UNMODELED=368706 CAPPED=95374 HIDDEN-INFO=1715 CAPPED-DECISION=410547 ===
=== MANA ORACLE enum-gap rejections: noop=19916 hidden=23575 host_blind=1095 cont_land=16274 ===
=== MANA ORACLE decision budget: 2110 decisions spent it, 407531 plans not audited ===
=== MANA ORACLE capped-plan escalation: 188349 retried, 91231 finished ===
=== MANA ORACLE unmodeled: 13537 x combo-route macro ===
=== MANA ORACLE unmodeled: 225683 x engine tapped a mana source outside any payment ===
=== MANA ORACLE unmodeled: 26945930 x floating pool too large to enumerate set-asides ===
=== MANA ORACLE unmodeled: 1176653 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 90631 x more than two any-colour land Auras on Adarkar Wastes ===
=== MANA ORACLE unmodeled: 149875 x more than two any-colour land Auras on Aether Hub ===
=== MANA ORACLE unmodeled: 115847 x more than two any-colour land Auras on Brushland ===
=== MANA ORACLE unmodeled: 62595 x more than two any-colour land Auras on Conservatory ===
=== MANA ORACLE unmodeled: 18386 x more than two any-colour land Auras on Kitchen ===
=== MANA ORACLE unmodeled: 45762 x more than two any-colour land Auras on Mariposa Military Base ===
=== MANA ORACLE unmodeled: 40645 x more than two any-colour land Auras on Yavimaya Coast ===
=== MANA ORACLE unmodeled: 1091046 x payment enumeration capped ===
=== MANA ORACLE unmodeled: 15 x scripted payment refused: 'Aether Hub' cannot produce {B} (it makes {C}) ===
=== MANA ORACLE unmodeled: 5 x scripted payment refused: 'Aether Hub' cannot produce {G} (it makes {C}) ===
=== MANA ORACLE unmodeled: 15 x scripted payment refused: 'Aether Hub' cannot produce {R} (it makes {C}) ===
=== MANA ORACLE unmodeled: 12 x scripted payment refused: 'Aether Hub' cannot produce {U} (it makes {C}) ===
=== MANA ORACLE unmodeled: 25 x scripted payment refused: 'Aether Hub' cannot produce {W} (it makes {C}) ===
=== MANA ORACLE unmodeled: 117 x scripted payment refused: 'Conservatory' is already tapped ===
=== MANA ORACLE unmodeled: 2202 x scripted payment refused: 'Kitchen' is already tapped ===
=== MANA ORACLE unmodeled: 3936 x scripted payment refused: 'Mariposa Military Base' is already tapped ===
=== MANA ORACLE unmodeled: 12 x scripted payment refused: 'Yavimaya Coast' is already tapped ===
=== MANA ORACLE unmodeled: 1 x scripted payment refused: scripted payment did not pay {2}{G} ===
=== MANA ORACLE unmodeled: 7889 x scripted payment refused: scripted payment did not pay {2}{W}{W} ===
=== MANA ORACLE unmodeled: 45 x scripted payment refused: scripted payment did not pay {C} ===
=== MANA ORACLE unmodeled: 3 x scripted payment refused: scripted payment did not pay {G} ===
=== MANA ORACLE unmodeled: 1 x scripted payment refused: scripted payment did not pay {R}{G} ===
=== MANA ORACLE unmodeled: 11149 x scripted payment refused: scripted payment did not pay {W}{G} ===
=== MANA ORACLE unmodeled: 274 x scripted payment refused: set-aside exceeds the floating pool ===
=== MANA ORACLE unmodeled: 21630 x tap-ahead enumeration capped ===
=== MANA ORACLE unmodeled: 167 x too many actions to try every order ===
=== MANA ORACLE unmodeled: 128078 x untap effect chose among different lands ===
=== MANA ORACLE rollback census: lost=40940 depth0=136 depth1=6775 depth2=2171 depth3+=1017 inconsistent=0 capped=134 unfaithful=40 no-failed-payment=0 unmodeled=236 reorder=2085 off-menu=28346 need_in_plan=6616 need_later=3893 pay_rollback_rescued=678/10509 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-CREATURE | DOMINATED-OTHER | ORACLE-GAP | UNMODELED | CAPPED | CAPPED-DECISION |
|---|---|---|---|---|---|---|---|---|---|---|---|
| accomplishedalchemist | 44214 | 1141058 | 5000 | 5000 | 1721 | 5000 | 0 | 1549 | 5000 | 5000 | 5000 |
| eldrazidisplacerflicker | 35494 | 936823 | 1867 | 2260 | 364 | 0 | 5000 | 46 | 5000 | 5000 | 5000 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth0 | depth1 | depth2 | depth3+ | capped | unfaithful | unmodeled | reorder | off-menu | rescued | need-later |
|---|---|---|---|---|---|---|---|---|---|---|---|
| accomplishedalchemist | 0 | 3393 | 1022 | 523 | 51 | 11 | 0 | 1721 | 5000 | 370 | 1887 |
| eldrazidisplacerflicker | 136 | 1347 | 388 | 102 | 51 | 0 | 236 | 364 | 1867 | 14 | 645 |

## ENUM-GAP (6867 flags, 5462 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **accomplishedalchemist** x15 -- T4 m1 `{no land + cast:Stroke of Genius(x1) + cast:Feed the Clan}`  
  a mana gate kept this plan off the menu, but casting Feed the Clan > Stroke of Genius pays every action with pay1[Forest#21:G Forest#22:G] ; pay2[Accomplished Alchemist#1:U]  
  board: board=[Forest#20, Utopia Sprawl#55@20, Forest#21, Forest#22, Accomplished Alchemist#1] hand=[Feed the Clan, Stroke of Genius, Elvish Harbinger, Forest] float=- life=25  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005012 --game-index 5012`
- **eldrazidisplacerflicker** x15 -- T4 m1 `{land=Aether Hub + cast:Cloud of Faeries + cast:Emiel the Blessed}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[(float only)] ; pay2[Brushland#6:W Aether Hub#3:U keep-floating {W}] ; pay3[Brushland#6:W Aether Hub#3:C]  
  board: board=[Brushland#6, Wild Growth#53@6, Mariposa Military Base#35, Eldrazi Displacer#18, Eldrazi Displacer#19] hand=[Wild Growth, Cloud of Faeries, Cloud of Faeries, Emiel the Blessed, Aether Hub] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005560 --game-index 5560`
- **accomplishedalchemist** x14 -- T4 m1 `{no land + cast:Stroke of Genius(x1) + cast:Nourish}`  
  a mana gate kept this plan off the menu, but casting Nourish > Stroke of Genius pays every action with pay1[Forest#21:G Forest#22:G] ; pay2[Accomplished Alchemist#200000:U]  
  board: board=[Forest#20, Wild Growth#60@20, Forest#21, Forest#22, Accomplished Alchemist#200000] hand=[Elvish Harbinger, Stroke of Genius, Nourish, Weather the Storm, Nourish, Living Wish] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005035 --game-index 5035`
- **eldrazidisplacerflicker** x14 -- T2 m1 `{land=Yavimaya Coast + cast:Cloud of Faeries + cast:Emiel the Blessed}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[(float only)] ; pay2[Brushland#6:W Yavimaya Coast#57:U keep-floating {W}] ; pay3[Brushland#6:W Yavimaya Coast#57:G]  
  board: board=[Brushland#6, Wild Growth#53@6] hand=[Cloud of Faeries, Brushland, Wild Growth, Fertile Ground, Living Wish, Emiel the Blessed, Yavimaya Coast] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30006653 --game-index 6653`
- **eldrazidisplacerflicker** x13 -- T4 m1 `{no land + cast:Emiel the Blessed + cast:Cloud of Faeries}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[(float only)] ; pay2[Yavimaya Coast#59:U Conservatory#16:W keep-floating {W}] ; pay3[Yavimaya Coast#59:G Conservatory#16:W]  
  board: board=[Yavimaya Coast#59, Wild Growth#56@59, Aether Hub#4, Cloud of Faeries#11, Conservatory#16] hand=[Peregrine Drake, Emiel the Blessed, Cloud of Faeries, Adarkar Wastes, Trace of Abundance] float=- life=17  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005567 --game-index 5567`
- **accomplishedalchemist** x12 -- T4 m1 `{no land + cast:Stroke of Genius(x2) + cast:Weather the Storm}`  
  a mana gate kept this plan off the menu, but casting Weather the Storm > Stroke of Genius pays every action with pay1[Forest#21:G Forest#22:G] ; pay2[Forest#20:G Accomplished Alchemist#200000:U]  
  board: board=[Forest#20, Wild Growth#60@20, Forest#21, Forest#22, Accomplished Alchemist#200000] hand=[Elvish Harbinger, Stroke of Genius, Nourish, Weather the Storm, Nourish, Living Wish] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005035 --game-index 5035`
- **accomplishedalchemist** x11 -- T5 m1 `{no land + cast:Stroke of Genius(x2) + cast:Feed the Clan}`  
  a mana gate kept this plan off the menu, but casting Feed the Clan > Stroke of Genius pays every action with pay1[Forest#26:G Elvish Harbinger#13:W] ; pay2[Accomplished Alchemist#2:U]  
  board: board=[Forest#36, Seedcradle Witch#46, Forest#26, Utopia Sprawl#58@36, Elvish Harbinger#13, Accomplished Alchemist#2] hand=[Stroke of Genius, Staff of Domination, Feed the Clan, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005084 --game-index 5084`
- **eldrazidisplacerflicker** x11 -- T2 m1 `{land=Yavimaya Coast + cast:Emiel the Blessed + cast:Cloud of Faeries}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[(float only)] ; pay2[Brushland#6:W Yavimaya Coast#57:U keep-floating {W}] ; pay3[Brushland#6:W Yavimaya Coast#57:G]  
  board: board=[Brushland#6, Wild Growth#53@6] hand=[Kitchen, Emiel the Blessed, Emiel the Blessed, Overgrowth, Cloud of Faeries, Yavimaya Coast, Wild Growth] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005923 --game-index 5923`
- **eldrazidisplacerflicker** x10 -- T3 m1 `{no land + cast:Emiel the Blessed + cast:Cloud of Faeries}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[(float only)] ; pay2[Conservatory#14:W Yavimaya Coast#59:U keep-floating {W}] ; pay3[Conservatory#14:W Yavimaya Coast#59:G]  
  board: board=[Conservatory#14, Yavimaya Coast#59, Wild Growth#53@14] hand=[Emiel the Blessed, Mariposa Military Base, Overgrowth, Peregrine Drake, Cloud of Faeries, Conservatory] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005125 --game-index 5125`
- **eldrazidisplacerflicker** x10 -- T3 m1 `{no land + cast:Emiel the Blessed + cast:Cloud of Faeries}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[(float only)] ; pay2[Conservatory#13:G+U Yavimaya Coast#57:U keep-floating {U}] ; pay3[Conservatory#13:W+W Yavimaya Coast#57:G]  
  board: board=[Conservatory#13, Yavimaya Coast#57, Cloud of Faeries#10, Trace of Abundance#47@13] hand=[Emiel the Blessed, Overgrowth, Trace of Abundance, Peregrine Drake, Cloud of Faeries] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005180 --game-index 5180`
- **accomplishedalchemist** x9 -- T4 m1 `{no land + cast:Stroke of Genius(x2) + cast:Feed the Clan}`  
  a mana gate kept this plan off the menu, but casting Feed the Clan > Stroke of Genius pays every action with pay1[Forest#21:G Forest#22:G] ; pay2[Accomplished Alchemist#1:U] ; pay3[Forest#20:G]  
  board: board=[Forest#20, Utopia Sprawl#55@20, Forest#21, Forest#22, Accomplished Alchemist#1] hand=[Feed the Clan, Stroke of Genius, Elvish Harbinger, Forest] float=- life=25  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005012 --game-index 5012`
- **eldrazidisplacerflicker** x9 -- T2 m1 `{land=Aether Hub + cast:Emiel the Blessed + cast:Cloud of Faeries}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[(float only)] ; pay2[Brushland#6:W Aether Hub#3:U keep-floating {W}] ; pay3[Brushland#6:W Aether Hub#3:C]  
  board: board=[Brushland#6, Wild Growth#53@6] hand=[Emiel the Blessed, Aether Hub, Shivan Gorge, Cloud of Faeries, Overgrowth, Eldrazi Displacer] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005928 --game-index 5928`
- ... 5450 more signatures in flags.jsonl

## APPLY-DROP (7260 flags, 2596 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **accomplishedalchemist** x90 -- T4 m1 `{no land + cast:Elvish Harbinger + cast:Feed the Clan + cast:Galvanic Alchemist(x4) + cast:Utopia Sprawl(x5)@20}`  
  engine dropped [cast Galvanic Alchemist]; every action pays with pay1[Arbor Elf#4:G] ; pay2[Arbor Elf#5:G] ; pay3[Forest#20:G Accomplished Alchemist#1:U] ; pay4[(float only)] || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Arbor Elf#4:G] ; pay2[Arbor Elf#5:G] ; pay3[Forest#20:G Accomplished Alchemist#1:U] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4, Utopia Sprawl#55@20, Arbor Elf#5, Accomplished Alchemist#1] hand=[Elvish Harbinger, Feed the Clan, Weather the Storm, Stroke of Genius, Galvanic Alchemist, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005361 --game-index 5361`
- **accomplishedalchemist** x90 -- T4 m1 `{no land + cast:Elvish Harbinger + cast:Weather the Storm + cast:Galvanic Alchemist(x4) + cast:Utopia Sprawl(x5)@20}`  
  engine dropped [cast Galvanic Alchemist]; every action pays with pay1[Arbor Elf#4:G] ; pay2[Arbor Elf#5:G] ; pay3[Forest#20:G Accomplished Alchemist#1:U] ; pay4[(float only)] || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[Arbor Elf#4:G] ; pay2[Arbor Elf#5:G] ; pay3[Forest#20:G Accomplished Alchemist#1:U] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4, Utopia Sprawl#55@20, Arbor Elf#5, Accomplished Alchemist#1] hand=[Elvish Harbinger, Feed the Clan, Weather the Storm, Stroke of Genius, Galvanic Alchemist, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005361 --game-index 5361`
- **accomplishedalchemist** x83 -- T2 m1 `{land=Forest + cast:Crab Umbra@4 + cast:Elvish Harbinger + cast:Utopia Sprawl(x2)@20}`  
  engine dropped [cast Crab Umbra]; every action pays with pay1[Forest#21:G] ; pay2[Forest#20:G Arbor Elf#4:G keep-floating {U}] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Forest#20:G Arbor Elf#4:G keep-floating {U}] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4] hand=[Staff of Domination, Crab Umbra, Forest, Staff of Domination, Elvish Harbinger, Utopia Sprawl, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005327 --game-index 5327`
- **accomplishedalchemist** x60 -- T2 m1 `{land=Forest + cast:Elvish Harbinger + cast:Utopia Sprawl(x2)@20 + cast:Crab Umbra@4}`  
  engine dropped [cast Crab Umbra]; every action pays with pay1[Forest#21:G] ; pay2[Forest#20:G Arbor Elf#4:G keep-floating {U}] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Forest#20:G Arbor Elf#4:G keep-floating {U}] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4] hand=[Elvish Harbinger, Wild Growth, Forest, Feed the Clan, Utopia Sprawl, Crab Umbra, Crab Umbra] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005025 --game-index 5025`
- **accomplishedalchemist** x60 -- T4 m1 `{no land + cast:Elvish Harbinger + cast:Galvanic Alchemist(x4) + cast:Utopia Sprawl(x5)@20}`  
  engine dropped [cast Galvanic Alchemist]; every action pays with pay1[Arbor Elf#4:G] ; pay2[Arbor Elf#5:G] ; pay3[Forest#20:G Accomplished Alchemist#1:U] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Arbor Elf#4:G] ; pay2[Arbor Elf#5:G] ; pay3[Forest#20:G Accomplished Alchemist#1:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4, Utopia Sprawl#55@20, Arbor Elf#5, Accomplished Alchemist#1] hand=[Elvish Harbinger, Feed the Clan, Weather the Storm, Stroke of Genius, Galvanic Alchemist, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005361 --game-index 5361`
- **accomplishedalchemist** x56 -- T4 m1 `{no land + cast:Nourish + cast:Galvanic Alchemist(x4) + cast:Arbor Elf + cast:Utopia Sprawl(x1)@20}`  
  engine dropped [cast Nourish]; every action pays with pay1[Forest#21:G] ; pay2[Arbor Elf#4:G Accomplished Alchemist#1:U Forest#20:G] ; pay3[(float only)] ; pay4[Forest#22:G] || ROLLBACK depth2: keep the engine's first 2 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Arbor Elf#4:G Accomplished Alchemist#1:U Forest#20:G] ; pay4[(float only)] ; pay5[Forest#22:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Forest#22, Accomplished Alchemist#1] hand=[Stroke of Genius, Nourish, Galvanic Alchemist, Arbor Elf, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30006076 --game-index 6076`
- **accomplishedalchemist** x42 -- T4 m1 `{no land + cast:Nourish + cast:Accomplished Alchemist + cast:Utopia Sprawl(x2)@20}`  
  engine dropped [cast Nourish]; every action pays with pay1[Accomplished Alchemist#1:G] ; pay2[Arbor Elf#4:G Forest#21:G Forest#22:G Forest#20:G] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Arbor Elf#4:G Forest#22:G Accomplished Alchemist#1:W Forest#20:G] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Forest#22, Accomplished Alchemist#1] hand=[Feed the Clan, Stroke of Genius, Nourish, Galvanic Alchemist, Accomplished Alchemist, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005049 --game-index 5049`
- **accomplishedalchemist** x42 -- T6 m1 `{no land + cast:Crab Umbra@15 + cast:Living Wish + cast:Nourish + cast:Utopia Sprawl(x5)@26 + cast:Utopia Sprawl(x5)@26 + cast:Living Wish}`  
  engine dropped [cast Crab Umbra]; every action pays with pay1[Arbor Elf#5:G] ; pay2[(float only)] ; pay3[Accomplished Alchemist#200000:U] ; pay4[Forest#26:G] ; pay5[(float only)] ; pay6[(float only)] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Arbor Elf#5:G] ; pay2[(float only)] ; pay3[Accomplished Alchemist#200000:U] ; pay4[Forest#26:G] ; pay5[(float only)] ; pay6[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#26, Utopia Sprawl#55@26, Wild Growth#60@26, Arbor Elf#5, Ezuri, Renegade Leader#15, Accomplished Alchemist#200000] hand=[Crab Umbra, Living Wish, Nourish, Utopia Sprawl, Utopia Sprawl, Living Wish, Feed the Clan, Stroke of Genius] float=- life=25  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30007091 --game-index 7091`
- **accomplishedalchemist** x36 -- T3 m1 `{land=Forest + cast:Elvish Harbinger + cast:Crab Umbra@4 + cast:Elvish Harbinger + cast:Utopia Sprawl(x5)@20}`  
  engine dropped [cast Crab Umbra]; every action pays with pay1[Forest#22:G] ; pay2[Arbor Elf#4:G] ; pay3[Forest#20:G] ; pay4[Accomplished Alchemist#1:U] || ROLLBACK depth1: keep the engine's first 3 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[(the engine's own)] ; pay4[Arbor Elf#4:G] ; pay5[Accomplished Alchemist#1:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Utopia Sprawl#55@20, Accomplished Alchemist#1] hand=[Elvish Harbinger, Forest, Crab Umbra, Elvish Harbinger, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005293 --game-index 5293`
- **accomplishedalchemist** x36 -- T2 m1 `{land=Forest + cast:Utopia Sprawl(x2)@20 + cast:Elvish Harbinger + cast:Crab Umbra@4}`  
  engine dropped [cast Crab Umbra]; every action pays with pay1[Forest#21:G] ; pay2[Forest#20:G Arbor Elf#4:G keep-floating {U}] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Forest#20:G Arbor Elf#4:G keep-floating {U}] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4] hand=[Staff of Domination, Staff of Domination, Utopia Sprawl, Elvish Harbinger, Forest, Forest, Crab Umbra] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005395 --game-index 5395`
- **accomplishedalchemist** x30 -- T5 m1 `{land=Forest + cast:Elvish Harbinger + cast:Accomplished Alchemist + cast:Crab Umbra@4 + cast:Utopia Sprawl(x5)@20 + cast:Weather the Storm}`  
  engine dropped [cast Crab Umbra]; every action pays with pay1[Forest#24:G] ; pay2[Forest#21:G Forest#22:G Forest#23:G] ; pay3[Arbor Elf#4:G Accomplished Alchemist#200000:U Forest#20:G keep-floating {U}{G}{G}] ; pay4[(float only)] ; pay5[(float only)] || ROLLBACK depth1: keep the engine's first 3 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[(the engine's own)] ; pay4[Arbor Elf#4:G Forest#24:G] ; pay5[Accomplished Alchemist#200000:U] ; pay6[Forest#22:G Forest#23:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Utopia Sprawl#55@20, Forest#22, Accomplished Alchemist#200000, Forest#23] hand=[Elvish Harbinger, Accomplished Alchemist, Forest, Staff of Domination, Crab Umbra, Utopia Sprawl, Weather the Storm] float=- life=31  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30007765 --game-index 7765`
- **accomplishedalchemist** x28 -- T4 m1 `{no land + cast:Nourish + cast:Galvanic Alchemist(x4) + cast:Arbor Elf + cast:Utopia Sprawl(x2)@20}`  
  engine dropped [cast Nourish]; every action pays with pay1[Accomplished Alchemist#1:G] ; pay2[Arbor Elf#4:G Forest#21:G Forest#20:G] ; pay3[(float only)] ; pay4[Forest#22:G] || ROLLBACK depth2: keep the engine's first 2 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Arbor Elf#4:G Accomplished Alchemist#1:W Forest#20:G] ; pay4[(float only)] ; pay5[Forest#22:G]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Forest#22, Accomplished Alchemist#1] hand=[Stroke of Genius, Nourish, Galvanic Alchemist, Arbor Elf, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30006076 --game-index 6076`
- ... 2584 more signatures in flags.jsonl

## APPLY-DROP-ORDER (2085 flags, 407 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **accomplishedalchemist** x122 -- T2 m1 `{no land + cast:Crab Umbra@4 + cast:Utopia Sprawl(x1)@20 + cast:Utopia Sprawl(x2)@20}`  
  engine dropped [cast Crab Umbra]; the order Utopia Sprawl > Crab Umbra > Utopia Sprawl pays every action with pay1[Arbor Elf#4:G] ; pay2[Forest#20:G] ; pay3[(float only)]  
  board: board=[Forest#20, Arbor Elf#4] hand=[Elvish Harbinger, Crab Umbra, Accomplished Alchemist, Utopia Sprawl, Utopia Sprawl, Stroke of Genius, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005963 --game-index 5963`
- **accomplishedalchemist** x69 -- T2 m1 `{no land + cast:Utopia Sprawl(x2)@20 + cast:Utopia Sprawl(x5)@20 + cast:Seedcradle Witch}`  
  engine dropped [cast Seedcradle Witch]; the order Utopia Sprawl > Utopia Sprawl > Seedcradle Witch pays every action with pay1[Arbor Elf#4:G] ; pay2[Forest#20:G] ; pay3[(float only)]  
  board: board=[Forest#20, Arbor Elf#4] hand=[Utopia Sprawl, Feed the Clan, Utopia Sprawl, Staff of Domination, Seedcradle Witch, Accomplished Alchemist] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005158 --game-index 5158`
- **accomplishedalchemist** x48 -- T2 m1 `{no land + cast:Utopia Sprawl(x1)@20 + cast:Utopia Sprawl(x2)@20 + cast:Crab Umbra@4}`  
  engine dropped [cast Crab Umbra]; the order Utopia Sprawl > Utopia Sprawl > Crab Umbra pays every action with pay1[Arbor Elf#4:G] ; pay2[Forest#20:G] ; pay3[(float only)]  
  board: board=[Forest#20, Arbor Elf#4] hand=[Utopia Sprawl, Accomplished Alchemist, Utopia Sprawl, Staff of Domination, Crab Umbra, Elvish Harbinger] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005054 --game-index 5054`
- **accomplishedalchemist** x48 -- T2 m1 `{land=Forest + cast:Utopia Sprawl(x2)@36 + cast:Utopia Sprawl(x5)@36 + cast:Seedcradle Witch + cast:Elvish Harbinger + cast:Utopia Sprawl(x5)@36}`  
  engine dropped [cast Elvish Harbinger]; the order Utopia Sprawl > Utopia Sprawl > Utopia Sprawl > Seedcradle Witch > Elvish Harbinger pays every action with pay1[Forest#29:G] ; pay2[Arbor Elf#5:G] ; pay3[(float only)] ; pay4[Forest#36:G] ; pay5[(float only)]  
  board: board=[Forest#36, Arbor Elf#5] hand=[Utopia Sprawl, Utopia Sprawl, Seedcradle Witch, Elvish Harbinger, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30006191 --game-index 6191`
- **accomplishedalchemist** x42 -- T2 m1 `{land=Forest + cast:Staff of Domination + cast:Utopia Sprawl(x2)@20 + cast:Arbor Elf + cast:Wild Growth@20 + cast:Utopia Sprawl(x2)@20}`  
  engine dropped [cast Staff of Domination]; the order Wild Growth > Utopia Sprawl > Utopia Sprawl > Staff of Domination > Arbor Elf pays every action with pay1[Forest#21:G] ; pay2[Arbor Elf#4:G] ; pay3[(float only)] ; pay4[Forest#20:G] ; pay5[(float only)]  
  board: board=[Forest#20, Arbor Elf#4] hand=[Staff of Domination, Utopia Sprawl, Forest, Arbor Elf, Wild Growth, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30009808 --game-index 9808`
- **accomplishedalchemist** x36 -- T2 m1 `{no land + cast:Utopia Sprawl(x2)@20 + cast:Utopia Sprawl(x5)@20 + cast:Utopia Sprawl(x5)@20}`  
  engine dropped [cast Utopia Sprawl]; the order Utopia Sprawl > Utopia Sprawl > Utopia Sprawl pays every action with pay1[Arbor Elf#4:G] ; pay2[Forest#20:G] ; pay3[(float only)]  
  board: board=[Forest#20, Arbor Elf#4] hand=[Utopia Sprawl, Stroke of Genius, Utopia Sprawl, Staff of Domination, Elvish Harbinger, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005538 --game-index 5538`
- **accomplishedalchemist** x33 -- T2 m1 `{no land + cast:Seedcradle Witch + cast:Utopia Sprawl(x2)@20 + cast:Utopia Sprawl(x5)@20}`  
  engine dropped [cast Seedcradle Witch]; the order Utopia Sprawl > Seedcradle Witch > Utopia Sprawl pays every action with pay1[Arbor Elf#4:G] ; pay2[Forest#20:G] ; pay3[(float only)]  
  board: board=[Forest#20, Arbor Elf#4] hand=[Seedcradle Witch, Utopia Sprawl, Forest, Forest, Elvish Harbinger, Utopia Sprawl, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005275 --game-index 5275`
- **accomplishedalchemist** x24 -- T2 m1 `{land=Forest + cast:Seedcradle Witch + cast:Utopia Sprawl(x2)@20 + cast:Elvish Harbinger + cast:Utopia Sprawl(x5)@20 + cast:Wild Growth@20}`  
  engine dropped [cast Elvish Harbinger]; the order Utopia Sprawl > Utopia Sprawl > Wild Growth > Seedcradle Witch > Elvish Harbinger pays every action with pay1[Forest#21:G] ; pay2[Arbor Elf#4:G] ; pay3[(float only)] ; pay4[Forest#20:G] ; pay5[(float only)]  
  board: board=[Forest#20, Arbor Elf#4] hand=[Seedcradle Witch, Utopia Sprawl, Forest, Forest, Elvish Harbinger, Utopia Sprawl, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005275 --game-index 5275`
- **accomplishedalchemist** x24 -- T2 m1 `{land=Forest + cast:Seedcradle Witch + cast:Utopia Sprawl(x2)@20 + cast:Elvish Harbinger + cast:Utopia Sprawl(x2)@20 + cast:Wild Growth@20}`  
  engine dropped [cast Seedcradle Witch, cast Elvish Harbinger]; the order Wild Growth > Utopia Sprawl > Utopia Sprawl > Seedcradle Witch > Elvish Harbinger pays every action with pay1[Forest#21:G] ; pay2[Arbor Elf#4:G] ; pay3[(float only)] ; pay4[Forest#20:G] ; pay5[(float only)]  
  board: board=[Forest#20, Arbor Elf#4] hand=[Seedcradle Witch, Utopia Sprawl, Forest, Forest, Elvish Harbinger, Utopia Sprawl, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005275 --game-index 5275`
- **accomplishedalchemist** x24 -- T2 m1 `{land=Forest + cast:Seedcradle Witch + cast:Utopia Sprawl(x1)@20 + cast:Elvish Harbinger + cast:Utopia Sprawl(x2)@20 + cast:Wild Growth@20}`  
  engine dropped [cast Elvish Harbinger]; the order Wild Growth > Utopia Sprawl > Utopia Sprawl > Seedcradle Witch > Elvish Harbinger pays every action with pay1[Forest#21:G] ; pay2[Arbor Elf#4:G] ; pay3[(float only)] ; pay4[Forest#20:G] ; pay5[(float only)]  
  board: board=[Forest#20, Arbor Elf#4] hand=[Seedcradle Witch, Utopia Sprawl, Forest, Forest, Elvish Harbinger, Utopia Sprawl, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005275 --game-index 5275`
- **accomplishedalchemist** x24 -- T2 m1 `{land=Forest + cast:Utopia Sprawl(x2)@36 + cast:Utopia Sprawl(x2)@36 + cast:Seedcradle Witch + cast:Elvish Harbinger + cast:Utopia Sprawl(x5)@36}`  
  engine dropped [cast Seedcradle Witch, cast Elvish Harbinger]; the order Utopia Sprawl > Utopia Sprawl > Utopia Sprawl > Seedcradle Witch > Elvish Harbinger pays every action with pay1[Forest#29:G] ; pay2[Arbor Elf#5:G] ; pay3[(float only)] ; pay4[Forest#36:G] ; pay5[(float only)]  
  board: board=[Forest#36, Arbor Elf#5] hand=[Utopia Sprawl, Utopia Sprawl, Seedcradle Witch, Elvish Harbinger, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30006191 --game-index 6191`
- **accomplishedalchemist** x24 -- T5 m1 `{no land + cast:Feed the Clan + cast:Living Wish + cast:Galvanic Alchemist(x4) + cast:Utopia Sprawl(x5)@20}`  
  engine dropped [cast Galvanic Alchemist]; the order Feed the Clan > Galvanic Alchemist > Utopia Sprawl > Living Wish pays every action with pay1[Arbor Elf#4:G Arbor Elf#5:G] ; pay2[Accomplished Alchemist#1:U] ; pay3[Arbor Elf#6:G] ; pay4[Forest#20:G] ; pay5[(float only)]  
  board: board=[Forest#20, Arbor Elf#4, Arbor Elf#5, Arbor Elf#6, Accomplished Alchemist#1, Ezuri, Renegade Leader#15] hand=[Feed the Clan, Living Wish, Feed the Clan, Stroke of Genius, Galvanic Alchemist, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30008611 --game-index 8611`
- ... 395 more signatures in flags.jsonl

## DOMINATED-CREATURE (5000 flags, 2165 signatures)

_the engine's payment taps a creature that could attack, where another payment keeps it untapped and loses nothing._

- **accomplishedalchemist** x59 -- T5 m1 `{no land + cast:Nourish + untap:Seedcradle Witch(x1)}`  
  keeps Elvish Harbinger untapped (attacker); keeps Forest untapped -- by paying pay1[Forest#32:G] ; pay2[Accomplished Alchemist#200000:W]; the engine ended board=[Forest#32(T), Utopia Sprawl#56@32, Forest#21(T), Utopia Sprawl#55@32, Elvish Harbinger#11(T), Accomplished Alchemist#200000(T), Seedcradle Witch#46] hand=[Feed the Clan, Feed the Clan] float=- life=26  
  board: board=[Forest#32, Utopia Sprawl#56@32, Forest#21, Utopia Sprawl#55@32, Elvish Harbinger#11, Accomplished Alchemist#200000, Seedcradle Witch#46] hand=[Feed the Clan, Feed the Clan, Nourish] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005116 --game-index 5116`
- **accomplishedalchemist** x47 -- T5 m1 `{no land + cast:Nourish + untap:Crab Umbra(x2)}`  
  keeps Elvish Harbinger untapped (attacker); keeps Forest untapped -- by paying pay1[Arbor Elf#200009:G] ; pay2[Accomplished Alchemist#2:U] ; pay3[keep-floating {U}{U}]; the engine ended board=[Forest#24(T), Utopia Sprawl#57@24, Forest#35, Arbor Elf#200009, Accomplished Alchemist#2(T), Elvish Harbinger#12(T), Crab Umbra#8@200009] hand=[Seedcradle Witch] float={U}{U}{U}{U}{U} life=35  
  board: board=[Forest#24, Utopia Sprawl#57@24, Forest#35, Arbor Elf#200009, Accomplished Alchemist#2, Elvish Harbinger#12, Crab Umbra#8@200009] hand=[Nourish, Seedcradle Witch] float=- life=29  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005082 --game-index 5082`
- **accomplishedalchemist** x28 -- T4 m1 `{no land + cast:Feed the Clan + cast:Staff of Domination}`  
  keeps Accomplished Alchemist untapped (attacker); keeps Forest untapped -- by paying pay1[Forest#20:G] ; pay2[Accomplished Alchemist#200000:W] ; pay3[Forest#21:G] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)] ; pay9[(float only)] ; pay10[(float only)] ; pay11[(float only)] ; pay12[(float only)] ; pay13[(float only)] ; pay14[(float only)] ; pay15[(float only)] ; pay16[(float only)] ; pay17[(float only)] ; pay18[(float only)] ; pay19[(float only)] ; pay20[(float only)] ; pay21[(float only)] ; pay22[(float only)] ; pay23[(float only)] ; pay24[(float only)] ; pay25[(float only)] ; pay26[(float only)] ; pay27[(float only)] ; pay28[(float   
  board: board=[Forest#20, Utopia Sprawl#55@20, Forest#21, Forest#22, Accomplished Alchemist#200000] hand=[Feed the Clan, Feed the Clan, Stroke of Genius, Staff of Domination, Stroke of Genius] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005382 --game-index 5382`
- **accomplishedalchemist** x24 -- T3 m1 `{no land + cast:Feed the Clan + cast:Utopia Sprawl(x5)@20}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#21:G] ; pay2[Arbor Elf#4:G]; the engine ended board=[Forest#20, Arbor Elf#4(T), Forest#21(T), Arbor Elf#5(T), Utopia Sprawl#55(new)@20] hand=[Crab Umbra, Staff of Domination, Forest] float=- life=25  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Arbor Elf#5] hand=[Feed the Clan, Crab Umbra, Staff of Domination, Forest, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005292 --game-index 5292`
- **accomplishedalchemist** x23 -- T5 m1 `{no land + cast:Feed the Clan + untap:Seedcradle Witch(x1)}`  
  keeps Elvish Harbinger untapped (attacker); keeps Forest untapped -- by paying pay1[Forest#32:G] ; pay2[Accomplished Alchemist#200000:W]; the engine ended board=[Forest#32(T), Utopia Sprawl#56@32, Forest#21(T), Utopia Sprawl#55@32, Elvish Harbinger#11(T), Accomplished Alchemist#200000(T), Seedcradle Witch#46] hand=[Feed the Clan, Nourish] float=- life=25  
  board: board=[Forest#32, Utopia Sprawl#56@32, Forest#21, Utopia Sprawl#55@32, Elvish Harbinger#11, Accomplished Alchemist#200000, Seedcradle Witch#46] hand=[Feed the Clan, Feed the Clan, Nourish] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005116 --game-index 5116`
- **accomplishedalchemist** x21 -- T4 m1 `{land=Forest + cast:Stroke of Genius(x1) + cast:Galvanic Alchemist(x4)}`  
  keeps Accomplished Alchemist untapped (attacker) -- by paying pay1[Forest#22:G Forest#21:G] ; pay2[Arbor Elf#4:G]; the engine ended board=[Forest#22(T), Arbor Elf#4(T), Utopia Sprawl#57@22, Utopia Sprawl#55@22, Accomplished Alchemist#1(T), Forest#21(T)(new), Galvanic Alchemist#200001(new)] hand=[Nourish, Stroke of Genius, Elvish Harbinger] float={G} life=20  
  board: board=[Forest#22, Arbor Elf#4, Utopia Sprawl#57@22, Utopia Sprawl#55@22, Accomplished Alchemist#1] hand=[Stroke of Genius, Nourish, Galvanic Alchemist, Stroke of Genius, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30007111 --game-index 7111`
- **accomplishedalchemist** x19 -- T5 m1 `{no land + cast:Nourish + untap:Seedcradle Witch(x1)}`  
  keeps Elvish Harbinger untapped (attacker); keeps Forest untapped -- by paying pay1[Forest#32:G] ; pay2[(float only)]; the engine ended board=[Forest#32(T), Utopia Sprawl#56@32, Forest#21(T), Utopia Sprawl#55@32, Elvish Harbinger#11(T), Accomplished Alchemist#200000, Seedcradle Witch#46] hand=[Feed the Clan, Feed the Clan] float=- life=26  
  board: board=[Forest#32, Utopia Sprawl#56@32, Forest#21, Utopia Sprawl#55@32, Elvish Harbinger#11, Accomplished Alchemist#200000, Seedcradle Witch#46] hand=[Feed the Clan, Feed the Clan, Nourish] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005116 --game-index 5116`
- **accomplishedalchemist** x16 -- T8 m1 `{land=Forest + cast:Staff of Domination}`  
  keeps Accomplished Alchemist untapped (attacker) -- by paying pay1[Forest#33:G Arbor Elf#200009:G] ; pay2[Forest#21:G]; the engine ended board=[Forest#33(T), Utopia Sprawl#57@33, Arbor Elf#200009(T), Accomplished Alchemist#2(T), Forest#21(T)(new), Staff of Domination#47(new)] hand=[Staff of Domination, Living Wish, Elvish Harbinger, Stroke of Genius, Brindle Boar] float={G} life=20  
  board: board=[Forest#33, Utopia Sprawl#57@33, Arbor Elf#200009, Accomplished Alchemist#2] hand=[Living Wish, Staff of Domination, Staff of Domination, Living Wish, Elvish Harbinger, Stroke of Genius, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005028 --game-index 5028`
- **accomplishedalchemist** x16 -- T8 m1 `{land=Forest + cast:Staff of Domination + untap:Crab Umbra(x3) + untap:Staff of Domination(x2)}`  
  keeps Arbor Elf untapped (attacker); keeps Forest untapped -- by paying pay1[Arbor Elf#4:G Arbor Elf#5:G Forest#20:G] ; pay2[(no payment exists)] ; pay3[Forest#21:G Forest#22:G keep-floating {G}] ; pay4[(float only)]; the engine ended board=[Forest#20(T), Arbor Elf#4(T), Forest#21(T), Arbor Elf#5(T), Utopia Sprawl#55@20, Crab Umbra#7@4, Crab Umbra#8@4, Forest#22(T), Staff of Domination#47(T), Forest#23(T)(new), Staff of Domination#48(new)] hand=[Forest, Forest] float={U}{G} life=25  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Arbor Elf#5, Utopia Sprawl#55@20, Crab Umbra#7@4, Crab Umbra#8@4, Forest#22, Staff of Domination#47] hand=[Staff of Domination, Forest, Forest, Forest] float=- life=25  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005292 --game-index 5292`
- **accomplishedalchemist** x16 -- T4 m1 `{no land + cast:Elvish Harbinger + cast:Utopia Sprawl(x5)@20}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#21:G] ; pay2[Arbor Elf#4:G Accomplished Alchemist#1:W]; the engine ended board=[Forest#20, Arbor Elf#4(T), Arbor Elf#5(T), Forest#21(T), Accomplished Alchemist#1(T), Utopia Sprawl#55(new)@20, Elvish Harbinger#11(new)] hand=[Feed the Clan, Stroke of Genius, Staff of Domination] float=- life=20  
  board: board=[Forest#20, Arbor Elf#4, Arbor Elf#5, Forest#21, Accomplished Alchemist#1] hand=[Feed the Clan, Elvish Harbinger, Stroke of Genius, Staff of Domination, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005780 --game-index 5780`
- **accomplishedalchemist** x16 -- T5 m1 `{no land + cast:Staff of Domination + cast:Ezuri, Renegade Leader + cast:Utopia Sprawl(x5)@20}`  
  keeps Accomplished Alchemist untapped (attacker) -- by paying pay1[Elvish Harbinger#11:G] ; pay2[Forest#20:G] ; pay3[Arbor Elf#4:G]; the engine ended board=[Forest#20(T), Arbor Elf#4(T), Wild Growth#60@20, Accomplished Alchemist#1(T), Elvish Harbinger#11(T), Utopia Sprawl#55(new)@20, Ezuri, Renegade Leader#15(new), Staff of Domination#47(new)] hand=[Stroke of Genius, Brindle Boar, Seedcradle Witch] float={G} life=20  
  board: board=[Forest#20, Arbor Elf#4, Wild Growth#60@20, Accomplished Alchemist#1, Elvish Harbinger#11] hand=[Stroke of Genius, Staff of Domination, Ezuri, Renegade Leader, Utopia Sprawl, Brindle Boar, Seedcradle Witch] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30006550 --game-index 6550`
- **accomplishedalchemist** x16 -- T5 m1 `{no land + cast:Utopia Sprawl(x5)@20 + cast:Elvish Harbinger}`  
  keeps Arbor Elf untapped (attacker) -- by paying pay1[Forest#21:G] ; pay2[Arbor Elf#4:G Accomplished Alchemist#1:W]; the engine ended board=[Forest#20, Arbor Elf#4(T), Forest#21(T), Arbor Elf#200009(T), Accomplished Alchemist#1(T), Utopia Sprawl#55(new)@20, Elvish Harbinger#11(new)] hand=[Nourish, Stroke of Genius, Crab Umbra, Living Wish, Stroke of Genius, Living Wish] float=- life=20  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Arbor Elf#200009, Accomplished Alchemist#1] hand=[Nourish, Stroke of Genius, Crab Umbra, Living Wish, Stroke of Genius, Utopia Sprawl, Living Wish, Elvish Harbinger] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30007373 --game-index 7373`
- ... 2153 more signatures in flags.jsonl

## DOMINATED-OTHER (5000 flags, 2113 signatures)

_another payment keeps a resource the engine's spends (energy, counters)._

- **eldrazidisplacerflicker** x29 -- T2 m1 `{land=Aether Hub + cast:Trace of Abundance@6}`  
  +1 energy; keeps Aether Hub untapped -- by paying pay1[Brushland#6:W]; the engine ended board=[Brushland#6(T), Wild Growth#53@6, Aether Hub#3(T)(new), Trace of Abundance#47(new)@6] hand=[Eldrazi Displacer, Eladamri's Call, Kitchen, Brushland, Trace of Abundance] float={G} life=18  
  board: board=[Brushland#6, Wild Growth#53@6] hand=[Eldrazi Displacer, Eladamri's Call, Trace of Abundance, Kitchen, Brushland, Aether Hub, Trace of Abundance] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005109 --game-index 5109`
- **eldrazidisplacerflicker** x24 -- T3 m1 `{land=Aether Hub + cast:Emiel the Blessed + cast:Cloud of Faeries}`  
  +1 energy; keeps Kitchen untapped -- by paying pay1[(float only)] ; pay2[Kitchen#29:U Brushland#6:C+W Aether Hub#3:C] ; pay3[Brushland#6:C+W]; the engine ended board=[Kitchen#29(T), Brushland#6(T), Fertile Ground#25@6, Aether Hub#3(T)(new), Cloud of Faeries#10(new), Emiel the Blessed#21(new)] hand=[Overgrowth, Living Wish, Fertile Ground] float={G} life=20  
  board: board=[Kitchen#29, Brushland#6, Fertile Ground#25@6] hand=[Emiel the Blessed, Overgrowth, Cloud of Faeries, Living Wish, Fertile Ground, Aether Hub] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30006476 --game-index 6476`
- **eldrazidisplacerflicker** x22 -- T4 m1 `{land=Aether Hub + cast:Eladamri's Call + act:Mariposa Military Base(x1)}`  
  +1 energy -- by paying pay1[Aether Hub#3:C+W Aether Hub#4:C] ; pay2[Mariposa Military Base#36:C]; the engine ended board=[Aether Hub#3(T), Mariposa Military Base#35(T), Fertile Ground#27@3, Mariposa Military Base#36(T), Overgrowth#39@3, Aether Hub#4(T)(new)] hand=[Cloud of Faeries, Peregrine Drake, Eldrazi Displacer, Peregrine Drake] float=- life=19  
  board: board=[Aether Hub#3, Mariposa Military Base#35, Fertile Ground#27@3, Mariposa Military Base#36, Overgrowth#39@3] hand=[Cloud of Faeries, Peregrine Drake, Aether Hub, Eladamri's Call] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30007086 --game-index 7086`
- **eldrazidisplacerflicker** x16 -- T4 m1 `{no land + cast:Emiel the Blessed + cast:Cloud of Faeries}`  
  +1 energy -- by paying pay1[(float only)] ; pay2[Kitchen#30:U Aether Hub#3:C+W] ; pay3[Kitchen#30:G Aether Hub#3:C+W]; the engine ended board=[Kitchen#30(T), Aether Hub#3(T), Fertile Ground#26@3, Cloud of Faeries#200002(new), Emiel the Blessed#21(new)] hand=[Emiel the Blessed, Wild Growth] float=- life=20  
  board: board=[Kitchen#30, Aether Hub#3, Fertile Ground#26@3] hand=[Emiel the Blessed, Emiel the Blessed, Cloud of Faeries, Wild Growth] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30007584 --game-index 7584`
- **eldrazidisplacerflicker** x15 -- T4 m1 `{no land + cast:Eldrazi Displacer + cast:Wild Growth@3}`  
  +1 energy -- by paying pay1[Aether Hub#3:C Brushland#9:W] ; pay2[(float only)]; the engine ended board=[Aether Hub#3(T), Brushland#9(T), Mariposa Military Base#200004, Overgrowth#41@3, Wild Growth#55(new)@3, Eldrazi Displacer#18(new)] hand=[Peregrine Drake, Eldrazi Displacer, Training Grounds] float={G} life=17  
  board: board=[Aether Hub#3, Brushland#9, Mariposa Military Base#200004, Overgrowth#41@3] hand=[Peregrine Drake, Eldrazi Displacer, Eldrazi Displacer, Training Grounds, Wild Growth] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005850 --game-index 5850`
- **eldrazidisplacerflicker** x14 -- T3 m1 `{no land + cast:Eldrazi Displacer}`  
  +1 energy -- by paying pay1[Aether Hub#3:C Aether Hub#4:C+W keep-floating {C}]; the engine ended board=[Aether Hub#3(T), Wild Growth#53@3, Aether Hub#4(T), Fertile Ground#25@4, Eldrazi Displacer#200003(new)] hand=[Training Grounds, Shivan Gorge, Mariposa Military Base, Peregrine Drake] float={*x1} life=20  
  board: board=[Aether Hub#3, Wild Growth#53@3, Aether Hub#4, Fertile Ground#25@4] hand=[Training Grounds, Shivan Gorge, Mariposa Military Base, Eldrazi Displacer, Peregrine Drake] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30006108 --game-index 6108`
- **eldrazidisplacerflicker** x14 -- T4 m1 `{land=Mariposa Military Base + cast:Living Wish + cast:Trace of Abundance@29}`  
  +1 energy -- by paying pay1[Kitchen#29:G+W Aether Hub#3:C] ; pay2[Yavimaya Coast#57:G]; the engine ended board=[Kitchen#29(T), Yavimaya Coast#57(T), Fertile Ground#25@29, Aether Hub#3(T), Eldrazi Displacer#18, Mariposa Military Base#35(T)(new), Trace of Abundance#47(new)@29] hand=[Emiel the Blessed, Mariposa Military Base, Cloud of Faeries] float={*x1} life=19  
  board: board=[Kitchen#29, Yavimaya Coast#57, Fertile Ground#25@29, Aether Hub#3, Eldrazi Displacer#18] hand=[Living Wish, Emiel the Blessed, Mariposa Military Base, Mariposa Military Base, Trace of Abundance] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30006748 --game-index 6748`
- **eldrazidisplacerflicker** x14 -- T4 m1 `{no land + cast:Emiel the Blessed + cast:Wild Growth@5 + cast:Cloud of Faeries + cast:Living Wish}`  
  +1 energy -- by paying pay1[Conservatory#14:G] ; pay2[(float only)] ; pay3[Aether Hub#5:C+U] ; pay4[Conservatory#14:W Aether Hub#5:C+W] ; pay5[Shivan Gorge#46:C] ; pay6[(no payment exists)]; the engine ended board=[Shivan Gorge#46(T), Conservatory#14(T), Aether Hub#5(T), Fertile Ground#28@5, Wild Growth#55(new)@5, Cloud of Faeries#200002(new), Emiel the Blessed#21(new)] hand=[Dimensional Infiltrator] float=- life=20  
  board: board=[Shivan Gorge#46, Conservatory#14, Aether Hub#5, Fertile Ground#28@5] hand=[Emiel the Blessed, Wild Growth, Cloud of Faeries, Living Wish] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30007240 --game-index 7240`
- **eldrazidisplacerflicker** x14 -- T4 m1 `{land=Aether Hub + cast:Eldrazi Displacer + cast:Living Wish + cast:Trace of Abundance@5}`  
  +1 energy -- by paying pay1[Brushland#6:C+W] ; pay2[Aether Hub#5:C+W keep-floating {G}{C}] ; pay3[(float only)]; the engine ended board=[Brushland#6(T), Wild Growth#53@6, Fertile Ground#25@6, Overgrowth#40@6, Aether Hub#5(T)(new), Trace of Abundance#50(new)@5, Eldrazi Displacer#20(new)] hand=[Cloud of Faeries] float=- life=19  
  board: board=[Brushland#6, Wild Growth#53@6, Fertile Ground#25@6, Overgrowth#40@6] hand=[Eldrazi Displacer, Living Wish, Trace of Abundance, Aether Hub] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30007364 --game-index 7364`
- **eldrazidisplacerflicker** x14 -- T4 m1 `{land=Aether Hub + cast:Emiel the Blessed + cast:Living Wish + cast:Trace of Abundance@6}`  
  +1 energy -- by paying pay1[Adarkar Wastes#1:W+G] ; pay2[Conservatory#13:W Brushland#6:C+W] ; pay3[Aether Hub#3:C] ; pay4[(no payment exists)]; the engine ended board=[Conservatory#13(T), Adarkar Wastes#1(T), Brushland#6(T), Trace of Abundance#47@1, Cloud of Faeries#10, Overgrowth#38@6, Aether Hub#3(T)(new), Trace of Abundance#50(new)@6, Emiel the Blessed#21(new)] hand=[Emiel the Blessed, Dimensional Infiltrator] float=- life=17  
  board: board=[Conservatory#13, Adarkar Wastes#1, Brushland#6, Trace of Abundance#47@1, Cloud of Faeries#10, Overgrowth#38@6] hand=[Aether Hub, Emiel the Blessed, Emiel the Blessed, Living Wish, Trace of Abundance] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30007903 --game-index 7903`
- **eldrazidisplacerflicker** x14 -- T4 m1 `{land=Adarkar Wastes + cast:Emiel the Blessed + cast:Living Wish}`  
  +1 energy -- by paying pay1[Conservatory#13:W Aether Hub#3:C Adarkar Wastes#1:W Mariposa Military Base#35:C keep-floating {G}{C}] ; pay2[(float only)]; the engine ended board=[Conservatory#13(T), Mariposa Military Base#35(T), Aether Hub#3(T), Overgrowth#38@3, Adarkar Wastes#1(T)(new), Emiel the Blessed#21(new)] hand=[Yavimaya Coast, Overgrowth, Emiel the Blessed, Cloud of Faeries] float=- life=17  
  board: board=[Conservatory#13, Mariposa Military Base#35, Aether Hub#3, Overgrowth#38@3] hand=[Emiel the Blessed, Yavimaya Coast, Adarkar Wastes, Overgrowth, Emiel the Blessed, Living Wish] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30008054 --game-index 8054`
- **eldrazidisplacerflicker** x14 -- T4 m1 `{land=Brushland + cast:Eldrazi Displacer + cast:Trace of Abundance@3 + cast:Living Wish}`  
  +1 energy -- by paying pay1[Conservatory#13:G Brushland#6:W] ; pay2[Aether Hub#3:C+W Mariposa Military Base#35:C keep-floating {G}{C}] ; pay3[(float only)]; the engine ended board=[Conservatory#13(T), Mariposa Military Base#35(T), Aether Hub#3(T), Overgrowth#38@3, Brushland#6(T)(new), Trace of Abundance#47(new)@3, Eldrazi Displacer#18(new)] hand=[Conservatory, Trace of Abundance, Cloud of Faeries] float=- life=17  
  board: board=[Conservatory#13, Mariposa Military Base#35, Aether Hub#3, Overgrowth#38@3] hand=[Eldrazi Displacer, Brushland, Trace of Abundance, Conservatory, Trace of Abundance, Living Wish] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30008070 --game-index 8070`
- ... 2101 more signatures in flags.jsonl

## ORACLE-GAP (1595 flags, 580 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **accomplishedalchemist** x60 -- T4 m1 `{no land + cast:Elvish Harbinger + cast:Utopia Sprawl(x5)@20 + untap:Seedcradle Witch(x1)}`  
  58 oracle outcomes, none covers the engine's -- nearest pay1[Accomplished Alchemist#1:G] ; pay2[Forest#21:G Forest#22:G Elvish Harbinger#11:W] ; pay3[(no payment exists)]: Arbor Elf#4 tapped by engine only; Accomplished Alchemist#1 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Forest#20, Arbor Elf#4(T), Forest#21(T), Elvish Harbinger#11(T), Forest#22(T), Seedcradle Witch#46, Accomplished Alchemist#1, Utopia Sprawl#55(new)@20, Elvish Harbinger#12(new)] hand=[Staff of Domination, Feed the Clan] float=- life=20  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Elvish Harbinger#11, Forest#22, Seedcradle Witch#46, Accomplished Alchemist#1] hand=[Staff of Domination, Feed the Clan, Elvish Harbinger, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30007787 --game-index 7787`
- **accomplishedalchemist** x30 -- T4 m1 `{no land + cast:Elvish Harbinger + cast:Utopia Sprawl(x5)@20 + untap:Seedcradle Witch(x1)}`  
  52 oracle outcomes, none covers the engine's -- nearest pay1[Accomplished Alchemist#1:G] ; pay2[Forest#21:G Forest#22:G Elvish Harbinger#11:W] ; pay3[(no payment exists)]: Accomplished Alchemist#1 tapped by oracle only; same core; float engine - oracle {G}{G}; engine ended board=[Forest#20, Arbor Elf#4(T), Forest#21(T), Elvish Harbinger#11(T), Forest#22(T), Seedcradle Witch#46, Accomplished Alchemist#1, Utopia Sprawl#55(new)@20, Elvish Harbinger#12(new)] hand=[Staff of Domination, Feed the Clan] float=- life=20  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Elvish Harbinger#11, Forest#22, Seedcradle Witch#46, Accomplished Alchemist#1] hand=[Staff of Domination, Feed the Clan, Elvish Harbinger, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30007787 --game-index 7787`
- **accomplishedalchemist** x27 -- T5 m1 `{no land + cast:Elvish Harbinger + cast:Utopia Sprawl(x5)@26 + untap:Seedcradle Witch(x1)}`  
  55 oracle outcomes, none covers the engine's -- nearest pay1[Accomplished Alchemist#3:G] ; pay2[Arbor Elf#200009:G Arbor Elf#6:G] ; pay3[(no payment exists)]: Forest#20 tapped by engine only; Forest#22 tapped by engine only; Accomplished Alchemist#3 tapped by oracle only; same core; float engine - oracle {G}; engine ended board=[Forest#26, Seedcradle Witch#46, Forest#20(T), Forest#22(T), Arbor Elf#200009(T), Arbor Elf#6(T), Accomplished Alchemist#3, Utopia Sprawl#57(new)@26, Elvish Harbinger#11(new)] hand=[Nourish] float=- life=20  
  board: board=[Forest#26, Seedcradle Witch#46, Forest#20, Forest#22, Arbor Elf#200009, Arbor Elf#6, Accomplished Alchemist#3] hand=[Nourish, Elvish Harbinger, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005118 --game-index 5118`
- **accomplishedalchemist** x16 -- T5 m1 `{no land + cast:Brindle Boar + cast:Utopia Sprawl(x5)@35 + act:Staff of Domination(x3)}`  
  11 oracle outcomes, none covers the engine's -- nearest pay1[Accomplished Alchemist#3:G] ; pay2[Arbor Elf#6:G] ; pay3[Forest#26:G] ; pay4[Forest#35:G] ; pay5[(no payment exists)]: Accomplished Alchemist#3 tapped by oracle only; Forest#24 tapped by engine only; same core; float engine - oracle {G}; engine ended board=[Forest#35(T), Arbor Elf#6(T), Forest#26(T), Utopia Sprawl#56@35, Accomplished Alchemist#3, Wild Growth#60@35, Staff of Domination#48(T), Forest#24(T), Utopia Sprawl#57(new)@35, Brindle Boar#200010(new)] hand=[] float=- life=22  
  board: board=[Forest#35, Arbor Elf#6, Forest#26, Utopia Sprawl#56@35, Accomplished Alchemist#3, Wild Growth#60@35, Staff of Domination#48, Forest#24] hand=[Brindle Boar, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005844 --game-index 5844`
- **accomplishedalchemist** x16 -- T5 m1 `{no land + cast:Brindle Boar + cast:Utopia Sprawl(x5)@21 + act:Staff of Domination(x2)}`  
  13 oracle outcomes, none covers the engine's -- nearest pay1[Accomplished Alchemist#1:G] ; pay2[Arbor Elf#5:G] ; pay3[Forest#22:G Forest#26:G] ; pay4[Forest#21:G]: Accomplished Alchemist#1 tapped by oracle only; life engine 21 oracle 22; float engine {G} oracle -; engine ended board=[Forest#21(T), Arbor Elf#5(T), Forest#22(T), Utopia Sprawl#57@21, Accomplished Alchemist#1, Forest#26(T), Staff of Domination#47(T), Utopia Sprawl#56(new)@21, Brindle Boar#200010(new)] hand=[] float={G} life=21  
  board: board=[Forest#21, Arbor Elf#5, Forest#22, Utopia Sprawl#57@21, Accomplished Alchemist#1, Forest#26, Staff of Domination#47] hand=[Brindle Boar, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30006222 --game-index 6222`
- **accomplishedalchemist** x16 -- T5 m1 `{no land + cast:Ezuri, Renegade Leader + cast:Utopia Sprawl(x1)@20 + untap:Staff of Domination(x2) + cast:Crab Umbra@15}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Forest#29:G] ; pay2[Arbor Elf#4:G Forest#25:G] ; pay3[Accomplished Alchemist#2:U] ; pay4[Forest#20:G Forest#23:G] ; pay5[(no payment exists)]: Forest#20 tapped by oracle only; Staff of Domination#49 tapped by oracle only; same core; float engine - oracle {W}{G}; engine ended board=[Forest#20, Arbor Elf#4(T), Forest#29(T), Staff of Domination#49, Forest#25(T), Accomplished Alchemist#2(T), Forest#23(T), Utopia Sprawl#57(new)@20, Ezuri, Renegade Leader#15(new), Crab Umbra#9(new)@15] hand=[Forest, Forest] float=- life=20  
  board: board=[Forest#20, Arbor Elf#4, Forest#29, Staff of Domination#49, Forest#25, Accomplished Alchemist#2, Forest#23] hand=[Crab Umbra, Forest, Forest, Ezuri, Renegade Leader, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30009974 --game-index 9974`
- **accomplishedalchemist** x14 -- T4 m1 `{no land + cast:Feed the Clan + cast:Crab Umbra@2 + act:Staff of Domination(x2)}`  
  19 oracle outcomes, none covers the engine's -- nearest pay1[Forest#24:G Forest#37:G] ; pay2[Accomplished Alchemist#2:U] ; pay3[(float only)] ; pay4[Forest#31:G]: life engine 26 oracle 27; float engine - oracle {G}; engine ended board=[Forest#31(T), Wild Growth#60@31, Forest#24(T), Staff of Domination#48(T), Forest#37(T), Accomplished Alchemist#2(T), Crab Umbra#10(new)@2] hand=[Living Wish] float=- life=26  
  board: board=[Forest#31, Wild Growth#60@31, Forest#24, Staff of Domination#48, Forest#37, Accomplished Alchemist#2] hand=[Living Wish, Feed the Clan, Crab Umbra] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005770 --game-index 5770`
- **accomplishedalchemist** x14 -- T6 m1 `{no land + cast:Wild Growth@22 + act:Staff of Domination(x3) + untap:Crab Umbra(x2)}`  
  33 oracle outcomes, none covers the engine's -- nearest pay1[Elvish Harbinger#11:G] ; pay2[Arbor Elf#6:G] ; pay3[Forest#34:G Forest#26:G Elvish Harbinger#12:W] ; pay4[Arbor Elf#4:G Accomplished Alchemist#3:W] ; pay5[(no payment exists)]: Forest#22 tapped by engine only; Elvish Harbinger#11 tapped by oracle only; Elvish Harbinger#12 tapped by oracle only; same core; float engine - oracle {G}; engine ended board=[Forest#22(T), Arbor Elf#4(T), Forest#34(T), Arbor Elf#6(T), Elvish Harbinger#11, Forest#26(T), Elvish Harbinger#12, Staff of Domination#47(T), Accomplished Alchemist#3(T), Crab Umbra#7@4, Wild Growth#60(new)@22] hand=[] float=- life=24  
  board: board=[Forest#22, Arbor Elf#4, Forest#34, Arbor Elf#6, Elvish Harbinger#11, Forest#26, Elvish Harbinger#12, Staff of Domination#47, Accomplished Alchemist#3, Crab Umbra#7@4] hand=[Wild Growth] float=- life=21  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30007813 --game-index 7813`
- **accomplishedalchemist** x12 -- T6 m1 `{no land + cast:Staff of Domination + cast:Utopia Sprawl(x5)@23}`  
  47 oracle outcomes, none covers the engine's -- nearest pay1[Accomplished Alchemist#3:G] ; pay2[Accomplished Alchemist#2:W Arbor Elf#4:G] ; pay3[Forest#36:G Forest#26:G] ; pay4[Forest#23:G Forest#24:G]: Accomplished Alchemist#3 tapped by oracle only; Accomplished Alchemist#2 tapped by oracle only; life engine 20 oracle 22; float engine - oracle -; engine ended board=[Forest#23(T), Forest#36(T), Forest#26(T), Forest#24(T), Accomplished Alchemist#3, Accomplished Alchemist#2, Arbor Elf#4(T), Utopia Sprawl#55(new)@23, Staff of Domination#47(T)(new)] hand=[] float=- life=20  
  board: board=[Forest#23, Forest#36, Forest#26, Forest#24, Accomplished Alchemist#3, Accomplished Alchemist#2, Arbor Elf#4] hand=[Staff of Domination, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005188 --game-index 5188`
- **accomplishedalchemist** x12 -- T5 m1 `{no land + cast:Nourish + cast:Utopia Sprawl(x1)@20 + cast:Elvish Harbinger + untap:Seedcradle Witch(x1048576)}`  
  94 oracle outcomes, none covers the engine's -- nearest pay1[Accomplished Alchemist#200000:G] ; pay2[Arbor Elf#4:G Forest#20:G] ; pay3[(float only)] ; pay4[(no payment exists)]: Forest#20 tapped by oracle only; Forest#21 tapped by engine only; Forest#22 tapped by engine only; Forest#23 tapped by engine only; same core; float engine - oracle {G}; engine ended board=[Forest#20, Arbor Elf#4(T), Forest#21(T), Wild Growth#60@20, Seedcradle Witch#46, Forest#22(T), Accomplished Alchemist#200000(T), Forest#23(T), Utopia Sprawl#55(new)@20, Elvish Harbinger#11(new)] hand=[Staff of Domination, Staff of Domination, Elvish Harbinger, Arbor Elf] float=- life=26  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Wild Growth#60@20, Seedcradle Witch#46, Forest#22, Accomplished Alchemist#200000, Forest#23] hand=[Staff of Domination, Staff of Domination, Nourish, Utopia Sprawl, Elvish Harbinger, Elvish Harbinger, Arbor Elf] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005792 --game-index 5792`
- **accomplishedalchemist** x12 -- T4 m1 `{no land + cast:Staff of Domination + cast:Utopia Sprawl(x5)@22}`  
  7 oracle outcomes, none covers the engine's -- nearest pay1[Accomplished Alchemist#2:G] ; pay2[Arbor Elf#4:G Forest#30:G] ; pay3[Arbor Elf#200009:G] ; pay4[Forest#22:G] ; pay5[(no payment exists)]: Forest#32 tapped by engine only; Accomplished Alchemist#2 tapped by oracle only; Staff of Domination#50 tapped by oracle only; life engine 20 oracle 21; float engine - oracle -; engine ended board=[Forest#22(T), Arbor Elf#4(T), Forest#30(T), Arbor Elf#200009(T), Forest#32(T), Accomplished Alchemist#2, Utopia Sprawl#56(new)@22, Staff of Domination#50(new)] hand=[Brindle Boar] float=- life=20  
  board: board=[Forest#22, Arbor Elf#4, Forest#30, Arbor Elf#200009, Forest#32, Accomplished Alchemist#2] hand=[Living Wish, Staff of Domination, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30007414 --game-index 7414`
- **accomplishedalchemist** x12 -- T7 m1 `{no land + cast:Elvish Harbinger + cast:Utopia Sprawl(x5)@29 + untap:Staff of Domination(x1)}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Forest#33:G] ; pay2[Forest#35:G Arbor Elf#4:G] ; pay3[Forest#29:G Forest#25:G]: Forest#29 tapped by oracle only; Staff of Domination#47 tapped by oracle only; Arbor Elf#4 tapped by engine only; same core; float engine - oracle -; engine ended board=[Forest#29, Forest#33(T), Forest#35(T), Ezuri, Renegade Leader#15, Forest#25(T), Staff of Domination#47, Arbor Elf#4(T), Utopia Sprawl#57(new)@29, Elvish Harbinger#11(new)] hand=[Crab Umbra, Accomplished Alchemist] float=- life=20  
  board: board=[Forest#29, Forest#33, Forest#35, Ezuri, Renegade Leader#15, Forest#25, Staff of Domination#47, Arbor Elf#4] hand=[Crab Umbra, Accomplished Alchemist, Elvish Harbinger, Utopia Sprawl] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30007860 --game-index 7860`
- ... 568 more signatures in flags.jsonl

## UNMODELED (10000 flags, 1614 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **accomplishedalchemist** x1358 -- T4 m1 `{no land + comboroute:Combo Off(x999)}`  
  a combo-route macro (a loop with its own payment and tap-ahead machinery) -- not driven by the oracle  
  board: board=[Forest#20, Arbor Elf#4, Forest#27, Ezuri, Renegade Leader#15, Forest#29, Accomplished Alchemist#3] hand=[Feed the Clan, Nourish, Staff of Domination] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005022 --game-index 5022`
- **accomplishedalchemist** x505 -- T5 m1 `{land=Forest + comboroute:Combo Off(x999)}`  
  a combo-route macro (a loop with its own payment and tap-ahead machinery) -- not driven by the oracle  
  board: board=[Forest#20, Utopia Sprawl#55@20, Forest#21, Forest#22, Accomplished Alchemist#1, Forest#23] hand=[Feed the Clan, Elvish Harbinger, Forest, Feed the Clan, Crab Umbra, Living Wish] float=- life=25  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005012 --game-index 5012`
- **accomplishedalchemist** x271 -- T5 m1 `{no land + cast:Seedcradle Witch}`  
  a mana choice the oracle does not drive: Accomplished Alchemist  
  board: board=[Forest#20, Arbor Elf#4, Forest#200006, Elvish Harbinger#11, Accomplished Alchemist#1] hand=[Staff of Domination, Feed the Clan, Stroke of Genius, Stroke of Genius, Nourish, Seedcradle Witch] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005026 --game-index 5026`
- **eldrazidisplacerflicker** x173 -- T3 m1 `{no land + cast:Living Wish}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Conservatory#13, Aether Hub#5, Cloud of Faeries#12, Fertile Ground#28@5] hand=[Eldrazi Displacer, Overgrowth, Living Wish, Emiel the Blessed, Brushland] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005001 --game-index 5001`
- **accomplishedalchemist** x122 -- T4 m1 `{no land + cast:Seedcradle Witch}`  
  a mana choice the oracle does not drive: Arbor Elf  
  board: board=[Forest#23, Arbor Elf#6, Forest#37, Utopia Sprawl#57@23, Accomplished Alchemist#1, Forest#21, Ezuri, Renegade Leader#15, Elvish Harbinger#13] hand=[Staff of Domination, Seedcradle Witch] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005041 --game-index 5041`
- **eldrazidisplacerflicker** x102 -- T3 m1 `{land=Brushland + cast:Living Wish}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Conservatory#13, Aether Hub#5, Cloud of Faeries#12, Fertile Ground#28@5] hand=[Eldrazi Displacer, Overgrowth, Living Wish, Emiel the Blessed, Brushland] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005001 --game-index 5001`
- **eldrazidisplacerflicker** x87 -- T2 m1 `{land=Yavimaya Coast + cast:Living Wish}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Brushland#6] hand=[Emiel the Blessed, Overgrowth, Yavimaya Coast, Mariposa Military Base, Overgrowth, Living Wish, Adarkar Wastes, Fertile Ground] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005013 --game-index 5013`
- **accomplishedalchemist** x85 -- T5 m1 `{land=Forest + cast:Seedcradle Witch}`  
  a mana choice the oracle does not drive: Accomplished Alchemist  
  board: board=[Forest#33, Forest#23, Forest#21, Elvish Harbinger#13, Accomplished Alchemist#1] hand=[Nourish, Arbor Elf, Stroke of Genius, Seedcradle Witch, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005073 --game-index 5073`
- **accomplishedalchemist** x82 -- T4 m1 `{no land + cast:Feed the Clan}`  
  a mana choice the oracle does not drive: Forest  
  board: board=[Forest#20, Arbor Elf#4, Utopia Sprawl#55@20, Accomplished Alchemist#200000] hand=[Staff of Domination, Elvish Harbinger, Crab Umbra, Stroke of Genius, Nourish, Feed the Clan] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005006 --game-index 5006`
- **eldrazidisplacerflicker** x73 -- T3 m1 `{land=Mariposa Military Base + cast:Living Wish}`  
  a mana choice the oracle does not drive: (an engine tap-ahead before an untap effect)  
  board: board=[Conservatory#13, Yavimaya Coast#57, Cloud of Faeries#10, Fertile Ground#25@13] hand=[Mariposa Military Base, Living Wish, Overgrowth, Brushland, Adarkar Wastes] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005014 --game-index 5014`
- **eldrazidisplacerflicker** x61 -- T4 m1 `{no land + comboroute:Combo Off(x999)}`  
  a combo-route macro (a loop with its own payment and tap-ahead machinery) -- not driven by the oracle  
  board: board=[Conservatory#13, Aether Hub#5, Cloud of Faeries#12, Fertile Ground#28@5, Brushland#8, Overgrowth#41@8, Eldrazi Displacer#20] hand=[Living Wish, Emiel the Blessed, Kitchen] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005001 --game-index 5001`
- **accomplishedalchemist** x58 -- T5 m1 `{no land + cast:Stroke of Genius(x2) + cast:Seedcradle Witch}`  
  a mana choice the oracle does not drive: Accomplished Alchemist  
  board: board=[Forest#27, Utopia Sprawl#58@27, Arbor Elf#6, Utopia Sprawl#55@27, Accomplished Alchemist#1, Elvish Harbinger#13] hand=[Stroke of Genius, Weather the Storm, Seedcradle Witch] float=- life=25  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005298 --game-index 5298`
- ... 1602 more signatures in flags.jsonl

## CAPPED (10000 flags, 1635 signatures)

_the payment search hit MTG_MANA_ORACLE_NODES and then the _RETRY escalation (20000); the complete side is partial._

- **accomplishedalchemist** x217 -- T6 m1 `{no land + cast:Nourish}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#25, Arbor Elf#6, Forest#29, Forest#21, Accomplished Alchemist#1, Forest#31, Forest#30] hand=[Forest, Nourish, Staff of Domination] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005062 --game-index 5062`
- **accomplishedalchemist** x186 -- T5 m1 `{no land + cast:Feed the Clan}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#22, Arbor Elf#5, Forest#31, Elvish Harbinger#11, Accomplished Alchemist#1] hand=[Staff of Domination, Staff of Domination, Feed the Clan, Forest] float=- life=25  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005042 --game-index 5042`
- **accomplishedalchemist** x149 -- T6 m1 `{no land + cast:Feed the Clan + untap:Staff of Domination(x1048576)}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#22, Forest#20, Arbor Elf#6, Forest#30, Elvish Harbinger#13, Accomplished Alchemist#2, Forest#34, Staff of Domination#50] hand=[Feed the Clan] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005008 --game-index 5008`
- **accomplishedalchemist** x137 -- T4 m1 `{no land + cast:Nourish + cast:Staff of Domination}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Elvish Harbinger#11, Forest#22, Accomplished Alchemist#1] hand=[Nourish, Staff of Domination, Weather the Storm, Living Wish, Nourish] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005019 --game-index 5019`
- **accomplishedalchemist** x118 -- T5 m1 `{no land + cast:Nourish + untap:Seedcradle Witch(x1048576)}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#20, Seedcradle Witch#46, Forest#21, Forest#22, Elvish Harbinger#11, Forest#23, Accomplished Alchemist#1, Crab Umbra#7@46] hand=[Nourish, Accomplished Alchemist, Ezuri, Renegade Leader, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005021 --game-index 5021`
- **accomplishedalchemist** x117 -- T4 m1 `{no land + cast:Feed the Clan + untap:Seedcradle Witch(x1048576)}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#37, Arbor Elf#4, Forest#35, Utopia Sprawl#57@37, Elvish Harbinger#12, Seedcradle Witch#46, Accomplished Alchemist#1] hand=[Feed the Clan, Stroke of Genius] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005010 --game-index 5010`
- **accomplishedalchemist** x109 -- T4 m1 `{no land + cast:Nourish + untap:Staff of Domination(x1048576)}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#20, Utopia Sprawl#55@20, Forest#21, Wild Growth#60@20, Staff of Domination#47, Forest#22, Accomplished Alchemist#1] hand=[Nourish, Feed the Clan, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005024 --game-index 5024`
- **accomplishedalchemist** x107 -- T4 m1 `{land=Forest + cast:Feed the Clan + untap:Staff of Domination(x1048576)}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#24, Arbor Elf#5, Forest#37, Utopia Sprawl#57@24, Elvish Harbinger#12, Forest#22, Accomplished Alchemist#3, Staff of Domination#50] hand=[Feed the Clan, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005043 --game-index 5043`
- **accomplishedalchemist** x93 -- T4 m1 `{no land + cast:Feed the Clan + cast:Staff of Domination}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#29, Utopia Sprawl#55@29, Forest#21, Elvish Harbinger#13, Accomplished Alchemist#2] hand=[Feed the Clan, Staff of Domination, Nourish, Elvish Harbinger, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005033 --game-index 5033`
- **accomplishedalchemist** x93 -- T4 m1 `{land=Forest + cast:Nourish}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#29, Utopia Sprawl#55@29, Forest#21, Elvish Harbinger#13, Accomplished Alchemist#2] hand=[Feed the Clan, Staff of Domination, Nourish, Elvish Harbinger, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005033 --game-index 5033`
- **accomplishedalchemist** x90 -- T5 m1 `{no land + cast:Feed the Clan + untap:Crab Umbra(x3) + untap:Seedcradle Witch(x1048576)}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#20, Utopia Sprawl#55@20, Forest#21, Utopia Sprawl#56@21, Wild Growth#60@21, Elvish Harbinger#11, Forest#22, Accomplished Alchemist#1, Crab Umbra#7@11, Elvish Harbinger#13, Forest#34, Seedcradle Witch#46] hand=[Feed the Clan] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005385 --game-index 5385`
- **accomplishedalchemist** x84 -- T5 m1 `{land=Forest + cast:Nourish + cast:Staff of Domination}`  
  payment search hit 20000 applies (MTG_MANA_ORACLE_NODES, then _RETRY); the complete side is partial for this plan  
  board: board=[Forest#28, Forest#26, Forest#37, Elvish Harbinger#11, Accomplished Alchemist#2] hand=[Elvish Harbinger, Nourish, Staff of Domination, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005016 --game-index 5016`
- ... 1623 more signatures in flags.jsonl

## CAPPED-DECISION (10000 flags, 3214 signatures)

_the decision spent its priced work budget (MTG_MANA_ORACLE_DECISION_S, default 300 priced s) before this plan's audit finished, so it has no verdict._

- **accomplishedalchemist** x644 -- T7 m1 `{no land + cast:Utopia Sprawl(x3)@20 + untap:Staff of Domination(x1048576) + untap:Crab Umbra(x2) + untap:Crab Umbra(x1) + untap:Crab Umbra(x2)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Utopia Sprawl#55@20, Arbor Elf#5, Staff of Domination#47, Forest#22, Forest#23, Crab Umbra#7@4, Crab Umbra#8@4, Crab Umbra#9@4, Forest#24, Elvish Harbinger#11, Forest#27, Accomplished Alchemist#1] hand=[Utopia Sprawl] float=- life=36  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005007 --game-index 5007`
- **accomplishedalchemist** x220 -- T7 m1 `{no land + cast:Utopia Sprawl(x5)@20 + act:Staff of Domination(x3) + untap:Crab Umbra(x3) + untap:Crab Umbra(x3) + untap:Crab Umbra(x3)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Utopia Sprawl#55@20, Arbor Elf#5, Staff of Domination#47, Forest#22, Forest#23, Crab Umbra#7@4, Crab Umbra#8@4, Crab Umbra#9@4, Forest#24, Elvish Harbinger#11, Forest#27, Accomplished Alchemist#1] hand=[Utopia Sprawl] float=- life=36  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005007 --game-index 5007`
- **accomplishedalchemist** x196 -- T7 m1 `{no land + cast:Utopia Sprawl(x5)@20 + untap:Staff of Domination(x1048576) + untap:Crab Umbra(x3) + untap:Crab Umbra(x2)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Utopia Sprawl#55@20, Arbor Elf#5, Staff of Domination#47, Forest#22, Forest#23, Crab Umbra#7@4, Crab Umbra#8@4, Crab Umbra#9@4, Forest#24, Elvish Harbinger#11, Forest#27, Accomplished Alchemist#1] hand=[Utopia Sprawl] float=- life=36  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005007 --game-index 5007`
- **accomplishedalchemist** x79 -- T5 m1 `{no land + cast:Staff of Domination}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#31, Forest#32, Forest#25, Arbor Elf#200009, Accomplished Alchemist#200000] hand=[Elvish Harbinger, Feed the Clan, Staff of Domination, Seedcradle Witch, Feed the Clan] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005018 --game-index 5018`
- **accomplishedalchemist** x69 -- T4 m1 `{no land + cast:Brindle Boar + cast:Weather the Storm + untap:Staff of Domination(x3)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Utopia Sprawl#55@20, Arbor Elf#5, Staff of Domination#47, Forest#22, Accomplished Alchemist#1] hand=[Brindle Boar, Weather the Storm] float=- life=21  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005044 --game-index 5044`
- **accomplishedalchemist** x68 -- T4 m1 `{no land + untap:Staff of Domination(x1048576)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Utopia Sprawl#55@20, Arbor Elf#5, Staff of Domination#47, Forest#22, Accomplished Alchemist#1] hand=[Brindle Boar, Weather the Storm] float=- life=21  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005044 --game-index 5044`
- **accomplishedalchemist** x60 -- T4 m1 `{no land + cast:Feed the Clan + untap:Staff of Domination(x2)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#20, Utopia Sprawl#55@20, Forest#21, Wild Growth#60@20, Staff of Domination#47, Forest#22, Accomplished Alchemist#1] hand=[Nourish, Feed the Clan, Forest] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005024 --game-index 5024`
- **accomplishedalchemist** x60 -- T5 m1 `{land=Forest + cast:Crab Umbra@4 + cast:Living Wish + cast:Utopia Sprawl(x5)@20 + cast:Living Wish + untap:Crab Umbra(x3)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Wild Growth#60@20, Elvish Harbinger#11, Accomplished Alchemist#1, Crab Umbra#7@4, Forest#35, Arbor Elf#5] hand=[Crab Umbra, Living Wish, Utopia Sprawl, Forest, Living Wish] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005066 --game-index 5066`
- **accomplishedalchemist** x60 -- T5 m1 `{no land + cast:Crab Umbra@4 + cast:Living Wish + cast:Utopia Sprawl(x5)@21 + cast:Living Wish + untap:Crab Umbra(x3)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Wild Growth#60@20, Elvish Harbinger#11, Accomplished Alchemist#1, Crab Umbra#7@4, Forest#35, Arbor Elf#5] hand=[Crab Umbra, Living Wish, Utopia Sprawl, Forest, Living Wish] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005066 --game-index 5066`
- **accomplishedalchemist** x51 -- T4 m1 `{no land + cast:Nourish}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#20, Arbor Elf#4, Forest#21, Elvish Harbinger#11, Accomplished Alchemist#1] hand=[Nourish, Living Wish, Stroke of Genius, Seedcradle Witch, Staff of Domination] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005002 --game-index 5002`
- **eldrazidisplacerflicker** x51 -- T3 m1 `{no land + cast:Living Wish}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Brushland#6, Aether Hub#3, Fertile Ground#25@3] hand=[Emiel the Blessed, Living Wish, Cloud of Faeries, Brushland, Trace of Abundance, Cloud of Faeries] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod' --profile 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json' --games 1 --seed 30005018 --game-index 5018`
- **accomplishedalchemist** x48 -- T5 m1 `{no land + cast:Feed the Clan + cast:Crab Umbra@4 + untap:Staff of Domination(x1048576) + untap:Crab Umbra(x3)}`  
  decision work budget (MTG_MANA_ORACLE_DECISION_S=300 priced s) spent before this plan was audited  
  board: board=[Forest#20, Arbor Elf#4, Utopia Sprawl#55@20, Forest#21, Wild Growth#60@20, Elvish Harbinger#11, Staff of Domination#47, Accomplished Alchemist#1, Crab Umbra#7@4] hand=[Feed the Clan, Crab Umbra, Accomplished Alchemist] float=- life=21  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/AccomplishedAlchemist/AccomplishedAlchemist.cod' --profile 'decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json' --games 1 --seed 30005025 --game-index 5025`
- ... 3202 more signatures in flags.jsonl

