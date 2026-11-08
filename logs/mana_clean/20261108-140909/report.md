# Mana-clean gate `20261108-140909`

Mana-clean gate run: 4 decks, games (seed 30000000 + game index) goblins 105000-109999, merfolk 105000-109999, pirates 44000-48999, treasure_hunt 55000-59999; src `3bda5c647e7c`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | DOMINATED-LIFE | ORACLE-GAP | UNMODELED | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| goblins | 5000 | issues | 724 | 6 | 35 | 0 | 0 | 683 | 0 | 777 | 29149 | 680 | 105 | 0 |
| merfolk | 5000 | issues | 349 | 0 | 349 | 0 | 44 | 0 | 0 | 2931 | 4 | 69 | 90 | 0 |
| pirates | 5000 | issues | 469 | 0 | 223 | 4 | 3 | 49 | 193 | 1885 | 17 | 140 | 322 | 0 |
| treasure_hunt | 5000 | issues | 608 | 18 | 501 | 5 | 350 | 83 | 1 | 3874 | 293 | 18449 | 18187 | 2 |

**MISS = 1141, ORACLE-GAP = 815.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=144394 plans=2068028 skipped=0 applies=138930166 frontiers=104909815 pruned=3383539 options=33145161 engine_drops=30130 gate_rejects=2611140(checked 2606077, skipped 5063) MISS=1538 ORACLE-GAP=815 UNMODELED=194 CAPPED=0 ms=2636624 ===
=== MANA ORACLE classes: ORACLE-GAP=815 ENUM-GAP=24 APPLY-DROP=1108 APPLY-DROP-ORDER=9 PHANTOM=9467 DOMINATED-LIFE=397 DOMINATED-LAND=18704 CONTINUATION-DIFFERS=29463 DROP-COVERED=19338 UNMODELED=194 HIDDEN-INFO=2 ===
=== MANA ORACLE enum-gap rejections: noop=102 hidden=4 host_blind=0 cont_land=0 ===
=== MANA ORACLE capped-plan escalation: 213 retried, 213 finished ===
=== MANA ORACLE unmodeled: 193 x engine tapped a mana source outside any payment ===
=== MANA ORACLE unmodeled: 144456 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 54 x payment enumeration capped ===
=== MANA ORACLE rollback census: lost=1141 depth0=0 depth1=1012 depth2=89 depth3+=7 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=9 off-menu=24 need_in_plan=789 need_later=319 pay_rollback_rescued=0/1108 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP | UNMODELED |
|---|---|---|---|---|---|---|---|
| goblins | 33018 | 308376 | 6 | 35 | 0 | 683 | 0 |
| merfolk | 37028 | 563818 | 0 | 349 | 0 | 0 | 0 |
| pirates | 38662 | 446284 | 0 | 223 | 4 | 49 | 193 |
| treasure_hunt | 35686 | 749550 | 18 | 501 | 5 | 83 | 1 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth1 | depth2 | depth3+ | reorder | off-menu | need-later |
|---|---|---|---|---|---|---|
| goblins | 35 | 0 | 0 | 0 | 6 | 9 |
| merfolk | 347 | 2 | 0 | 0 | 0 | 25 |
| pirates | 198 | 25 | 0 | 4 | 0 | 180 |
| treasure_hunt | 432 | 62 | 7 | 5 | 18 | 105 |

## ENUM-GAP (24 flags, 22 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **treasure_hunt** x2 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Saprazzan Skerry#40:U] ; pay2[Thundering Falls#53:U Ferrous Lake#5:UR]  
  board: board=[Thundering Falls#53, Saprazzan Skerry#40] hand=[Ferrous Lake, Treasure Hunt, Land's Edge, Ferrous Lake, Fiery Islet, Treasure Hunt, Cascade Bluffs] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30055296 --game-index 55296`
- **treasure_hunt** x2 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Saprazzan Skerry#40:U] ; pay2[Thundering Falls#53:U Frostboil Snarl#17:U]  
  board: board=[Thundering Falls#53, Saprazzan Skerry#40] hand=[Island, Treasure Hunt, Land's Edge, Treasure Hunt, Treasure Hunt, Frostboil Snarl, Lonely Sandbar] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30056812 --game-index 56812`
- **goblins** x1 -- T7 m1 `{land=Mountain + cast:Goblin Chainwhirler + cast:Goblin Lackey}`  
  a mana gate kept this plan off the menu, but casting Goblin Lackey > Goblin Chainwhirler pays every action with pay1[Mountain#33:R] ; pay2[Mountain#30:R Mountain#27:R Three Tree City#59:R]  
  board: board=[Mountain#30, Mountain#27, Three Tree City#59, Goblin Matron#14, Goblin Chieftain#6] hand=[Goblin Chainwhirler, Goblin Lackey, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=7 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30105530 --game-index 105530`
- **goblins** x1 -- T3 m1 `{no land + cast:Rundvelt Hordemaster + cast:Goblin Piledriver + sac:Skirk Prospector}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Mountain#24:R Three Tree City#59:R] ; pay2[(float only)]  
  board: board=[Mountain#24, Goblin Lackey#9, Three Tree City#59, Muxus, Goblin Grandee#45, Goblin Matron#13, Skirk Prospector#56, Twinshot Sniper#60, Goblin Chieftain#5] hand=[Rundvelt Hordemaster, Goblin Piledriver, Mountain, Cavern of Souls] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30105660 --game-index 105660`
- **goblins** x1 -- T6 m1 `{land=Three Tree City + cast:Krenko, Mob Boss + cast:Goblin Lackey}`  
  a mana gate kept this plan off the menu, but casting Goblin Lackey > Krenko, Mob Boss pays every action with pay1[Mountain#34:R] ; pay2[Cavern of Souls#3:C Mountain#28:R Three Tree City#59:R]  
  board: board=[Cavern of Souls#3, Mountain#34, Goblin Piledriver#18, Aether Vial#2, Mountain#28, Goblin Matron#14, Goblin Chieftain#5] hand=[Krenko, Mob Boss, Goblin Lackey, Three Tree City] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30105874 --game-index 105874`
- **goblins** x1 -- T3 m2 `{no land + cast:Goblin King + cast:Pashalik Mons + sac:Skirk Prospector}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Mountain#30:R Three Tree City#59:R] ; pay2[(float only)]  
  board: board=[Mountain#30, Goblin Lackey#11(T), Three Tree City#59, Skirk Prospector#57(T), Goblin Chieftain#5(T), Muxus, Goblin Grandee#45(new), Siege-Gang Commander#53(new), 1/1 Goblin Token#1000(new), 1/1 Goblin Token#1001(new), 1/1 Goblin Token#1002(new), Rundvelt Hordemaster#50(new), Rundvelt Hordemaster#51(new), Goblin Chieftain#6(new)] hand=[Goblin King, Pashalik Mons] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30108363 --game-index 108363`
- **goblins** x1 -- T5 m1 `{land=Mountain + cast:Siege-Gang Commander + cast:Goblin Piledriver}`  
  a mana gate kept this plan off the menu, but casting Goblin Piledriver > Siege-Gang Commander pays every action with pay1[Mountain#26:R] ; pay2[Mountain#31:R Mountain#38:R Three Tree City#59:R]  
  board: board=[Mountain#31, Three Tree City#59, Goblin Warchief#19, Goblin Matron#14, Mountain#38, Goblin Chieftain#5] hand=[Siege-Gang Commander, Goblin Piledriver, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30108456 --game-index 108456`
- **goblins** x1 -- T5 m1 `{no land + vial:Goblin Chieftain + sacoutlet:Pashalik Mons}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Cavern of Souls#3:C Mountain#37:R Three Tree City#59:R]  
  board: board=[Cavern of Souls#3, Aether Vial#2, Three Tree City#59, Goblin Warchief#19, Mountain#37, Pashalik Mons#48, Goblin Lackey#10] hand=[Goblin Chieftain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30109144 --game-index 109144`
- **treasure_hunt** x1 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Saprazzan Skerry#43:U] ; pay2[Temple of Epiphany#49:U Steam Vents#45:U]  
  board: board=[Temple of Epiphany#49, Saprazzan Skerry#43] hand=[Steam Vents, Fiery Islet, Fiery Islet, Treasure Hunt, Treasure Hunt, Treasure Hunt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30056164 --game-index 56164`
- **treasure_hunt** x1 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Saprazzan Skerry#41:U] ; pay2[Cascade Bluffs#4:C Frostboil Snarl#20:U]  
  board: board=[Cascade Bluffs#4, Saprazzan Skerry#41] hand=[Treasure Hunt, Forgotten Cave, Treasure Hunt, Frostboil Snarl, Steam Vents] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30056267 --game-index 56267`
- **treasure_hunt** x1 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Saprazzan Skerry#41:U] ; pay2[Thundering Falls#53:U Fiery Islet#11:U]  
  board: board=[Saprazzan Skerry#41, Thundering Falls#53] hand=[Forgotten Cave, Treasure Hunt, Fiery Islet, Lonely Sandbar, Lonely Sandbar, Cascade Bluffs, Treasure Hunt, Throes of Chaos] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30056259 --game-index 56259`
- **treasure_hunt** x1 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Fiery Islet#12:U Sandstone Needle#36:R] ; pay2[Ferrous Lake#6:UR]  
  board: board=[Fiery Islet#12, Sandstone Needle#36] hand=[Treasure Hunt, Ferrous Lake, Treasure Hunt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30057519 --game-index 57519`
- ... 10 more signatures in flags.jsonl

## APPLY-DROP (1108 flags, 308 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **treasure_hunt** x129 -- T3 m1 `{no land + cast:Treasure Hunt + cast:Treasure Hunt}`  
  engine dropped [cast Treasure Hunt]; every action pays with pay1[Saprazzan Skerry#40:U Sandstone Needle#36:R keep-floating {U}{R}] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Saprazzan Skerry#40:U Sandstone Needle#36:R keep-floating {U}{R}] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Saprazzan Skerry#40, Sandstone Needle#36] hand=[Fiery Islet, Treasure Hunt, Treasure Hunt, Saprazzan Skerry, Cascade Bluffs, Forgotten Cave, Reliquary Tower, Island] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30055097 --game-index 55097`
- **merfolk** x21 -- T3 m1 `{land=Island + cast:Spreading Seas@600000 + cast:Tide Shaper}`  
  engine dropped [cast Spreading Seas]; every action pays with pay1[Secluded Courtyard#35:U(creature-only)] ; pay2[Secluded Courtyard#36:C Island#10:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Secluded Courtyard#35:U(creature-only)] ; pay2[Secluded Courtyard#36:C Island#10:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Secluded Courtyard#35, Tide Shaper#47, Secluded Courtyard#36, Lord of Atlantis#19] hand=[Island, Spreading Seas, Tide Shaper, Island, Aether Vial] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Merfolk/Merfolk.cod' --profile 'decks/Merfolk/Merfolk.profile.json' --games 1 --seed 30105430 --game-index 105430`
- **merfolk** x21 -- T4 m1 `{land=Sink into Stupor + cast:Spreading Seas@600000 + cast:Merfolk Mistbinder}`  
  engine dropped [cast Spreading Seas]; every action pays with pay1[Secluded Courtyard#35:U(creature-only) Unclaimed Territory#51:G(creature-only)] ; pay2[Secluded Courtyard#36:C Soporific Springs#43:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Secluded Courtyard#35:U(creature-only) Unclaimed Territory#51:G(creature-only)] ; pay2[Secluded Courtyard#36:C Soporific Springs#43:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Secluded Courtyard#35, Cosi's Trickster#9, Unclaimed Territory#51, Master of the Pearl Trident#23, Secluded Courtyard#36, Tide Shaper#47, Tide Shaper#48] hand=[Spreading Seas, Sink into Stupor, Merfolk Mistbinder] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Merfolk/Merfolk.cod' --profile 'decks/Merfolk/Merfolk.profile.json' --games 1 --seed 30105516 --game-index 105516`
- **merfolk** x14 -- T3 m1 `{land=Island + cast:Tide Shaper + cast:Spreading Seas@600000}`  
  engine dropped [cast Spreading Seas]; every action pays with pay1[Secluded Courtyard#35:U(creature-only)] ; pay2[Secluded Courtyard#36:C Island#10:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Secluded Courtyard#35:U(creature-only)] ; pay2[Secluded Courtyard#36:C Island#10:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Secluded Courtyard#35, Cosi's Trickster#9, Secluded Courtyard#36, Merfolk Mistbinder#27] hand=[Tide Shaper, Island, Silvergill Adept, Spreading Seas, Botanical Sanctum] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Merfolk/Merfolk.cod' --profile 'decks/Merfolk/Merfolk.profile.json' --games 1 --seed 30105140 --game-index 105140`
- **merfolk** x14 -- T3 m1 `{land=Sink into Stupor + cast:Spreading Seas@600000 + cast:Tide Shaper}`  
  engine dropped [cast Spreading Seas]; every action pays with pay1[Unclaimed Territory#51:U(creature-only)] ; pay2[Secluded Courtyard#35:C Soporific Springs#43:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Unclaimed Territory#51:U(creature-only)] ; pay2[Secluded Courtyard#35:C Soporific Springs#43:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Secluded Courtyard#35, Cosi's Trickster#9, Unclaimed Territory#51, Master of the Pearl Trident#23] hand=[Spreading Seas, Tide Shaper, Sink into Stupor, Tide Shaper, Secluded Courtyard] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Merfolk/Merfolk.cod' --profile 'decks/Merfolk/Merfolk.profile.json' --games 1 --seed 30105516 --game-index 105516`
- **merfolk** x14 -- T3 m1 `{land=Island + cast:Tide Shaper + cast:Spreading Seas@600000}`  
  engine dropped [cast Spreading Seas]; every action pays with pay1[Unclaimed Territory#52:U(creature-only)] ; pay2[Secluded Courtyard#38:C Island#12:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Unclaimed Territory#52:U(creature-only)] ; pay2[Secluded Courtyard#38:C Island#12:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Secluded Courtyard#38, Kumena's Speaker#18, Unclaimed Territory#52, Lord of Atlantis#20] hand=[Tide Shaper, Phantasmal Image, Unclaimed Territory, Island, Spreading Seas] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Merfolk/Merfolk.cod' --profile 'decks/Merfolk/Merfolk.profile.json' --games 1 --seed 30107109 --game-index 107109`
- **merfolk** x14 -- T4 m1 `{no land + cast:Spreading Seas@600000 + cast:Tide Shaper}`  
  engine dropped [cast Spreading Seas]; every action pays with pay1[Secluded Courtyard#35:U(creature-only)] ; pay2[Secluded Courtyard#36:C Island#10:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Secluded Courtyard#35:U(creature-only)] ; pay2[Secluded Courtyard#36:C Island#10:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Secluded Courtyard#35, Tide Shaper#47, Secluded Courtyard#36, Merfolk Mistbinder#27, Island#10, Lord of Atlantis#19, Aether Vial#1] hand=[Spreading Seas, Silvergill Adept, Phantasmal Image, Tide Shaper] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Merfolk/Merfolk.cod' --profile 'decks/Merfolk/Merfolk.profile.json' --games 1 --seed 30107921 --game-index 107921`
- **merfolk** x14 -- T3 m1 `{land=Island + cast:Spreading Seas@600000 + cast:Tide Shaper}`  
  engine dropped [cast Spreading Seas]; every action pays with pay1[Unclaimed Territory#51:U(creature-only)] ; pay2[Secluded Courtyard#35:C Island#10:U] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Unclaimed Territory#51:U(creature-only)] ; pay2[Secluded Courtyard#35:C Island#10:U]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Secluded Courtyard#35, Tide Shaper#49, Unclaimed Territory#51, Lord of Atlantis#21] hand=[Vodalian Hexcatcher, Spreading Seas, Tide Shaper, Master of the Pearl Trident, Island] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Merfolk/Merfolk.cod' --profile 'decks/Merfolk/Merfolk.profile.json' --games 1 --seed 30108443 --game-index 108443`
- **pirates** x14 -- T3 m1 `{land=Unclaimed Territory + cast:Staunch Crewmate}`  
  engine dropped [cast Lightning Bolt]; every action pays with pay1[Secluded Courtyard#43:C Unclaimed Territory#57:U(creature-only)] ; pay2[Spirebluff Canal#49:R], reaching a board the menu's other order of these actions does not (Secluded Courtyard#43 tapped by oracle only; hand size differs; graveyard size differs; opponent life differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Secluded Courtyard#43:C Unclaimed Territory#57:U(creature-only)] ; pay2[Spirebluff Canal#49:R]; a lost action was not declared in the plan; MTG_PAY_ROLLBACK (forced o  
  board: board=[Secluded Courtyard#43, Daring Buccaneer#14, Spirebluff Canal#49, Aether Vial#5, Aether Vial#6] hand=[Unclaimed Territory, Goblin Tomb Raider, Lightning Bolt, Staunch Crewmate, Metallic Mimic] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30044861 --game-index 44861`
- **goblins** x10 -- T3 m1 `{land=Mountain + cast:Goblin Warchief + cast:Krenko, Mob Boss}`  
  engine dropped [cast Krenko, Mob Boss]; every action pays with pay1[Mountain#24:R Mountain#25:R Three Tree City#59:R] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Mountain#24:R Mountain#25:R Three Tree City#59:R] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Mountain#24, Goblin Lackey#9, Three Tree City#59, Goblin Lackey#10, Muxus, Goblin Grandee#45, 1/1 Goblin Token#1000, Twinshot Sniper#60, 1/1 Goblin Token#1001(new)] hand=[Goblin Warchief, Goblin Piledriver, Krenko, Mob Boss, Mountain, Goblin Chieftain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30108713 --game-index 108713`
- **goblins** x9 -- T4 m1 `{no land + cast:Goblin Matron + sac:Skirk Prospector}`  
  engine dropped [cast Lightning Bolt]; every action pays with pay1[(float only)] ; pay2[(float only)] ; pay3[Cavern of Souls#3:R(creature-only) keep-floating {R}] ; pay4[(float only)], reaching a board the menu's other order of these actions does not (Skirk Prospector#56 only in engine; Goblin Warchief#19 only in engine; Goblin Chieftain#6 only in engine; Goblin Matron#13 only in oracle; Goblin Lackey#11 only in oracle; Goblin Lackey#9 only in oracle; hand size differs; graveyard size differs; library size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every acti  
  board: board=[Cavern of Souls#3, Goblin Lackey#12, Skirk Prospector#56, Muxus, Goblin Grandee#45, Goblin Warchief#19, Rundvelt Hordemaster#50] hand=[Goblin Matron, Muxus, Goblin Grandee, Lightning Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30107086 --game-index 107086`
- **treasure_hunt** x8 -- T4 m1 `{land=Cascade Bluffs + cast:Land's Edge + cast:Treasure Hunt}`  
  engine dropped [cast Treasure Hunt]; every action pays with pay1[Sandstone Needle#39:R Cascade Bluffs#1:C] ; pay2[Temple of Epiphany#51:U Cascade Bluffs#4:C] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Sandstone Needle#39:R Cascade Bluffs#1:C] ; pay2[Temple of Epiphany#51:U Cascade Bluffs#4:C]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Temple of Epiphany#51, Cascade Bluffs#4, Sandstone Needle#39] hand=[Land's Edge, Treasure Hunt, Treasure Hunt, Fiery Islet, Sandstone Needle, Reliquary Tower, Cascade Bluffs, Sandstone Needle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30055377 --game-index 55377`
- ... 296 more signatures in flags.jsonl

## APPLY-DROP-ORDER (9 flags, 3 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **treasure_hunt** x5 -- T5 m1 `{land=Fiery Islet + cast:Treasure Hunt + cast:Land's Edge}`  
  engine dropped [cast Land's Edge]; the order Land's Edge > Treasure Hunt pays every action with pay1[Reliquary Tower#34:C Forgotten Cave#16:R Fiery Islet#11:R Cascade Bluffs#2:UR] ; pay2[Reliquary Tower#31:C]  
  board: board=[Reliquary Tower#34, Reliquary Tower#31, Forgotten Cave#16, Cascade Bluffs#2] hand=[Throes of Chaos, Treasure Hunt, Fiery Islet, Temple of Epiphany, Steam Vents, Forgotten Cave, Thundering Falls, Frostboil Snarl, Lonely Sandbar, Temple of Epiphany, Forgotten Cave, Island, Sandstone Needle, Island, Temple of Epiphany, Ferrous Lake, Forgotten Cave, Sandstone Needle, Steam Vents, Ferrous Lake,   
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30059018 --game-index 59018`
- **pirates** x2 -- T4 m1 `{no land + cast:Metallic Mimic + vial:Corsair Captain + cast:Daring Buccaneer}`  
  engine dropped [cast Daring Buccaneer]; the order Daring Buccaneer > vial:Corsair Captain > Metallic Mimic pays every action with pay1[Fiery Islet#22:R] ; pay2[Spirebluff Canal#49:U Treasure Token#1000:W]  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Fiery Islet#22, Staunch Crewmate#53, Daring Buccaneer#14, Malcolm, the Eyes#33, Staunch Crewmate#54] hand=[Adaptive Automaton, Metallic Mimic, Corsair Captain, Daring Buccaneer, Mountain] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30048392 --game-index 48392`
- **pirates** x2 -- T5 m1 `{no land + vial:Corsair Captain + cast:Metallic Mimic + cast:Daring Buccaneer}`  
  engine dropped [cast Daring Buccaneer]; the order Daring Buccaneer > vial:Corsair Captain > Metallic Mimic pays every action with pay1[Mountain#38:R] ; pay2[Fiery Islet#22:U Treasure Token#1000:W]  
  board: board=[Fiery Islet#22, Daring Buccaneer#14, Aether Vial#5, Aether Vial#6, Mountain#38, Staunch Crewmate#53, Dire Fleet Captain#18] hand=[Adaptive Automaton, Corsair Captain, Adaptive Automaton, Metallic Mimic, Daring Buccaneer] float=- life=16  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30048510 --game-index 48510`

## ORACLE-GAP (815 flags, 225 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **goblins** x159 -- T2 m2 `{no land + sacoutlet:Siege-Gang Commander}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[(no payment exists)]: 1/1 Goblin Token#1000 only in oracle; 1/1 Goblin Token#1001 only in oracle; 1/1 Goblin Token#1002 only in oracle; graveyard size differs; opponent life differs; float engine - oracle -; engine ended board=[Mountain#35(T), Goblin Lackey#10(T), Skirk Prospector#56(new), Siege-Gang Commander#52(new)] hand=[] float=- life=20  
  board: board=[Mountain#35(T), Goblin Lackey#10(T), Skirk Prospector#56(new), Siege-Gang Commander#52(new), 1/1 Goblin Token#1000(new), 1/1 Goblin Token#1001(new), 1/1 Goblin Token#1002(new)] hand=[] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30105032 --game-index 105032`
- **goblins** x77 -- T2 m2 `{no land + sacoutlet:Siege-Gang Commander}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[(no payment exists)]: Mountain#43 tapped by engine only; 1/1 Goblin Token#1000 only in oracle; 1/1 Goblin Token#1001 only in oracle; graveyard size differs; opponent life differs; float engine - oracle -; engine ended board=[Cavern of Souls#3(T), Goblin Lackey#12(T), Mountain#43(T)(new), Skirk Prospector#57(new), Siege-Gang Commander#54(new), 1/1 Goblin Token#1002(new)] hand=[Goblin Chainwhirler, Mountain, Mountain] float=- life=20  
  board: board=[Cavern of Souls#3(T), Goblin Lackey#12(T), Mountain#43(new), Skirk Prospector#57(new), Siege-Gang Commander#54(new), 1/1 Goblin Token#1000(new), 1/1 Goblin Token#1001(new), 1/1 Goblin Token#1002(new)] hand=[Goblin Chainwhirler, Mountain, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30105063 --game-index 105063`
- **treasure_hunt** x27 -- T4 m1 `{land=Ferrous Lake + cast:Land's Edge}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Temple of Epiphany#48:U Cascade Bluffs#1:UR Ferrous Lake#5:UR]: Temple of Epiphany#48 tapped by oracle only; Reliquary Tower#31 tapped by engine only; same core; float engine - oracle -; engine ended board=[Temple of Epiphany#48, Reliquary Tower#31(T), Cascade Bluffs#1(T), Ferrous Lake#5(T)(new), Land's Edge#25(new)] hand=[Land's Edge, Treasure Hunt] float=- life=20  
  board: board=[Temple of Epiphany#48, Reliquary Tower#31, Cascade Bluffs#1] hand=[Land's Edge, Cascade Bluffs, Forgotten Cave, Frostboil Snarl, Forgotten Cave, Lonely Sandbar, Land's Edge, Ferrous Lake, Cascade Bluffs, Thundering Falls, Fiery Islet, Lonely Sandbar, Steam Vents, Steam Vents, Saprazzan Skerry, Temple of Epiphany, Sandstone Needle, Thundering Falls, Island, Ferrous Lake, Ferrous Lake, Thunde  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30056899 --game-index 56899`
- **pirates** x26 -- T4 m1 `{land=Fiery Islet + cast:Kitesail Larcenist(x10) + vial:Forerunner of the Coalition}`  
  15 oracle outcomes, none covers the engine's -- nearest pay1[Spirebluff Canal#49:U Treasure Token#1000:W Fiery Islet#23:U] ; pay2[Treasure Token#10:W]: Spirebluff Canal#50 tapped by engine only; Fiery Islet#22 only in engine; Treasure Token#1000 only in engine; Forerunner of the Coalition#24 only in oracle; float engine - oracle -; engine ended board=[Spirebluff Canal#49(T), Aether Vial#5(T), Spirebluff Canal#50(T), Metallic Mimic#34, Fiery Islet#22(T), Treasure Token#1000, Dire Fleet Captain#18, Fiery Islet#23(T)(new), Kitesail Larcenist#30(new)] hand=[Secluded Courtyard, Dire Fleet Captain] float=- life=18  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Spirebluff Canal#50, Metallic Mimic#34, Fiery Islet#22, Corsair Captain#10, Treasure Token#1000, Dire Fleet Captain#18] hand=[Kitesail Larcenist, Fiery Islet, Forerunner of the Coalition, Secluded Courtyard] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30047253 --game-index 47253`
- **goblins** x20 -- T2 m2 `{no land + sacoutlet:Siege-Gang Commander}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[(no payment exists)]: Goblin Lackey#12 only in oracle; 1/1 Goblin Token#1000 only in oracle; 1/1 Goblin Token#1001 only in oracle; graveyard size differs; opponent life differs; float engine - oracle -; engine ended board=[Cavern of Souls#3(T), Mountain#24(T)(new), Stingscourger#58(new), Muxus, Goblin Grandee#45(new), Goblin King#8(new), Siege-Gang Commander#53(new), 1/1 Goblin Token#1002(new), Goblin Chieftain#5(new), Skirk Prospector#56(new)] hand=[Mountain, Goblin Lackey, Goblin Piledriver] float=- life=20  
  board: board=[Cavern of Souls#3(T), Goblin Lackey#12(T), Mountain#24(T)(new), Stingscourger#58(new), Muxus, Goblin Grandee#45(new), Goblin King#8(new), Siege-Gang Commander#53(new), 1/1 Goblin Token#1000(new), 1/1 Goblin Token#1001(new), 1/1 Goblin Token#1002(new), Goblin Chieftain#5(new), Skirk Prospector#56(new)] hand=[Mountain, Goblin Lackey, Goblin Piledriver] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30105455 --game-index 105455`
- **goblins** x16 -- T3 m2 `{no land + sacoutlet:Siege-Gang Commander}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[(no payment exists)]: Mountain#40 tapped by engine only; Goblin Lackey#11 only in oracle; 1/1 Goblin Token#1002 only in oracle; graveyard size differs; opponent life differs; float engine - oracle -; engine ended board=[Mountain#42(T), Mountain#40(T), Skirk Prospector#56(T), Siege-Gang Commander#52(T), Goblin Warchief#19(T), Stingscourger#58(T)(new)] hand=[] float=- life=20  
  board: board=[Mountain#42(T), Goblin Lackey#11(T), Mountain#40, Skirk Prospector#56(T), Siege-Gang Commander#52(T), 1/1 Goblin Token#1002(T), Goblin Warchief#19(T), Stingscourger#58(T)(new)] hand=[] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30105437 --game-index 105437`
- **pirates** x15 -- T4 m1 `{land=Fiery Islet + cast:Kitesail Larcenist + vial:Forerunner of the Coalition}`  
  10 oracle outcomes, none covers the engine's -- nearest pay1[Spirebluff Canal#49:U Treasure Token#1000:W Fiery Islet#23:U] ; pay2[Spirebluff Canal#50:U]: Fiery Islet#22 only in engine; Forerunner of the Coalition#24 only in oracle; float engine - oracle -; engine ended board=[Spirebluff Canal#49(T), Aether Vial#5(T), Spirebluff Canal#50(T), Metallic Mimic#34, Fiery Islet#22(T), Corsair Captain#10, Dire Fleet Captain#18, Fiery Islet#23(T)(new), Kitesail Larcenist#30(new)] hand=[Secluded Courtyard, Dire Fleet Captain] float=- life=18  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Spirebluff Canal#50, Metallic Mimic#34, Fiery Islet#22, Corsair Captain#10, Treasure Token#1000, Dire Fleet Captain#18] hand=[Kitesail Larcenist, Fiery Islet, Forerunner of the Coalition, Secluded Courtyard] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30047253 --game-index 47253`
- **goblins** x12 -- T6 m2 `{no land + cast:Goblin Matron + sacoutlet:Siege-Gang Commander}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[Mountain#26:R Mountain#37:R Three Tree City#59:R] ; pay2[(float only)]: 1/1 Goblin Token#1000 only in engine; graveyard size differs; opponent life differs; float engine - oracle -; engine ended board=[Three Tree City#59(T), Mountain#26(T), Goblin Lackey#9(T), Mountain#37(T)(new), Siege-Gang Commander#52(new), 1/1 Goblin Token#1000(new), 1/1 Goblin Token#1001(new), 1/1 Goblin Token#1002(new), Goblin Matron#14(new)] hand=[Twinshot Sniper, Goblin Piledriver, Siege-Gang Commander, Mogg War Marshal, Goblin Chieftain] float=- life=20  
  board: board=[Three Tree City#59, Mountain#26, Goblin Lackey#9(T), Mountain#37(new), Siege-Gang Commander#52(new), 1/1 Goblin Token#1000(new), 1/1 Goblin Token#1001(new), 1/1 Goblin Token#1002(new)] hand=[Twinshot Sniper, Goblin Piledriver, Siege-Gang Commander, Goblin Matron, Mogg War Marshal] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30106155 --game-index 106155`
- **goblins** x12 -- T3 m2 `{no land + sacoutlet:Siege-Gang Commander + sacoutlet:Siege-Gang Commander}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[(no payment exists)] ; pay2[(no payment exists)]: Goblin Lackey#9 only in oracle; Skirk Prospector#56 only in oracle; Siege-Gang Commander#53 only in oracle; 1/1 Goblin Token#1006 only in oracle; 1/1 Goblin Token#1007 only in oracle; 1/1 Goblin Token#1008 only in oracle; graveyard size differs; opponent life differs; float engine - oracle -; engine ended board=[Mountain#24(T), Mountain#25(T), Siege-Gang Commander#52(T), Goblin Chieftain#5(T)(new), Siege-Gang Commander#54(new)] hand=[] float=- life=20  
  board: board=[Mountain#24(T), Goblin Lackey#9(T), Mountain#25(T), Skirk Prospector#56(T), Siege-Gang Commander#52(T), Siege-Gang Commander#53(T)(new), Goblin Chieftain#5(T)(new), Siege-Gang Commander#54(new), 1/1 Goblin Token#1006(new), 1/1 Goblin Token#1007(new), 1/1 Goblin Token#1008(new)] hand=[] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30108516 --game-index 108516`
- **goblins** x9 -- T3 m2 `{no land + sacoutlet:Siege-Gang Commander + sacoutlet:Siege-Gang Commander}`  
  1 oracle outcomes, none covers the engine's -- nearest pay1[(no payment exists)] ; pay2[(no payment exists)]: 1/1 Goblin Token#1000 only in oracle; 1/1 Goblin Token#1001 only in oracle; 1/1 Goblin Token#1002 only in oracle; 1/1 Goblin Token#1003 only in oracle; 1/1 Goblin Token#1004 only in oracle; 1/1 Goblin Token#1005 only in oracle; graveyard size differs; opponent life differs; float engine - oracle -; engine ended board=[Cavern of Souls#3(T), Mountain#32(T), Goblin Lackey#11(T), Mountain#31(T)(new), Goblin Matron#14(new), Muxus, Goblin Grandee#47(new), Siege-Gang Commander#55(new), Siege-Gang Commander#52(new), Skirk Prospector#56(new)] hand=[Mountain] float=- life=20  
  board: board=[Cavern of Souls#3(T), Mountain#32(T), Goblin Lackey#11(T), Mountain#31(T)(new), Goblin Matron#14(new), Muxus, Goblin Grandee#47(new), Siege-Gang Commander#55(new), 1/1 Goblin Token#1000(new), 1/1 Goblin Token#1001(new), 1/1 Goblin Token#1002(new), Siege-Gang Commander#52(new), 1/1 Goblin Token#1003(new), 1/1 Goblin Token#1004(new), 1/1 Goblin Token#1005(new), Skirk Prospector#56(new)] hand=  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30106182 --game-index 106182`
- **goblins** x9 -- T4 m1 `{no land + cast:Goblin Matron + sac:Skirk Prospector}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[(float only)] ; pay2[(float only)] ; pay3[Cavern of Souls#3:R(creature-only) keep-floating {R}] ; pay4[(float only)]: Cavern of Souls#3 tapped by oracle only; Goblin Matron#13 only in oracle; graveyard size differs; float engine - oracle -; engine ended board=[Cavern of Souls#3, Goblin Lackey#11(new), Skirk Prospector#57(new)] hand=[Muxus, Goblin Grandee, Stingscourger] float=- life=20  
  board: board=[Cavern of Souls#3, Goblin Lackey#12, Skirk Prospector#56, Muxus, Goblin Grandee#45, Goblin Warchief#19, Rundvelt Hordemaster#50] hand=[Goblin Matron, Muxus, Goblin Grandee, Lightning Bolt] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30107086 --game-index 107086`
- **treasure_hunt** x9 -- T4 m1 `{land=Ferrous Lake + cast:Land's Edge}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Thundering Falls#53:U Cascade Bluffs#1:UR Ferrous Lake#5:UR]: Thundering Falls#53 tapped by oracle only; Reliquary Tower#31 tapped by engine only; same core; float engine - oracle -; engine ended board=[Thundering Falls#53, Reliquary Tower#31(T), Cascade Bluffs#1(T), Ferrous Lake#5(T)(new), Land's Edge#25(new)] hand=[Land's Edge] float=- life=20  
  board: board=[Thundering Falls#53, Reliquary Tower#31, Cascade Bluffs#1] hand=[Forgotten Cave, Ferrous Lake, Fiery Islet, Lonely Sandbar, Forgotten Cave, Land's Edge, Island, Ferrous Lake, Sandstone Needle, Island, Cascade Bluffs, Steam Vents, Island, Frostboil Snarl, Ferrous Lake, Sandstone Needle, Saprazzan Skerry, Thundering Falls, Reliquary Tower, Saprazzan Skerry, Land's Edge, Fiery Islet] float=- l  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/treasure_hunt/treasure_hunt.txt' --profile 'decks/treasure_hunt/treasure_hunt.profile.json' --games 1 --seed 30057626 --game-index 57626`
- ... 213 more signatures in flags.jsonl

## UNMODELED (194 flags, 26 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **pirates** x30 -- T4 m1 `{land=Fiery Islet + cast:Kitesail Larcenist(x5) + vial:Forerunner of the Coalition}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Spirebluff Canal#50, Metallic Mimic#34, Fiery Islet#22, Corsair Captain#10, Treasure Token#1000, Dire Fleet Captain#18] hand=[Kitesail Larcenist, Fiery Islet, Forerunner of the Coalition, Secluded Courtyard] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30047253 --game-index 47253`
- **pirates** x21 -- T5 m1 `{no land + cast:Kitesail Larcenist(x15)}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Spirebluff Canal#49, Daring Buccaneer#15, Secluded Courtyard#45, Dire Fleet Captain#18, Unclaimed Territory#58, Metallic Mimic#37, Fiery Islet#22, Staunch Crewmate#56, Metallic Mimic#35] hand=[Secluded Courtyard, Kitesail Larcenist] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30045123 --game-index 45123`
- **pirates** x20 -- T4 m1 `{land=Secluded Courtyard + cast:Kitesail Larcenist(x5) + vial:Forerunner of the Coalition}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Spirebluff Canal#50, Metallic Mimic#34, Fiery Islet#22, Corsair Captain#10, Treasure Token#1000, Dire Fleet Captain#18] hand=[Kitesail Larcenist, Fiery Islet, Forerunner of the Coalition, Secluded Courtyard] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30047253 --game-index 47253`
- **pirates** x19 -- T4 m1 `{land=Mountain + cast:Kitesail Larcenist(x26)}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Blackcleave Cliffs#9, Goblin Tomb Raider#26, Fiery Islet#22, Dire Fleet Captain#18, Secluded Courtyard#43, Aether Vial#5, Siren Stormtamer#47] hand=[Kitesail Larcenist, Mountain] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30044134 --game-index 44134`
- **pirates** x12 -- T4 m1 `{land=Fiery Islet + cast:Kitesail Larcenist(x47)}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Spirebluff Canal#49, Siren Stormtamer#47, Unclaimed Territory#57, Dire Fleet Captain#18, Fiery Islet#22, Dire Fleet Captain#19] hand=[Fiery Islet, Kitesail Larcenist, Unclaimed Territory] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30044946 --game-index 44946`
- **pirates** x11 -- T4 m1 `{land=Unclaimed Territory + cast:Kitesail Larcenist(x5)}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Secluded Courtyard#43, Metallic Mimic#34, Goblin Tomb Raider#26, Fiery Islet#22, Corsair Captain#10, Dire Fleet Captain#18, Aether Vial#6] hand=[Kitesail Larcenist, Unclaimed Territory] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30044159 --game-index 44159`
- **pirates** x10 -- T5 m1 `{land=Mountain + cast:Forerunner of the Coalition}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Secluded Courtyard#43, Siren Stormtamer#47, Unclaimed Territory#57, Corsair Captain#10, Treasure Token#1000, Fiery Islet#22, Adaptive Automaton#1, Goblin Tomb Raider#26] hand=[Mountain, Forerunner of the Coalition] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30045617 --game-index 45617`
- **pirates** x10 -- T5 m1 `{land=Mountain + vial:Forerunner of the Coalition}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Secluded Courtyard#43, Siren Stormtamer#47, Unclaimed Territory#57, Corsair Captain#10, Treasure Token#1000, Fiery Islet#22, Adaptive Automaton#1, Goblin Tomb Raider#26] hand=[Mountain, Forerunner of the Coalition] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30045617 --game-index 45617`
- **pirates** x10 -- T5 m1 `{no land + cast:Forerunner of the Coalition}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Secluded Courtyard#43, Siren Stormtamer#47, Unclaimed Territory#57, Corsair Captain#10, Treasure Token#1000, Fiery Islet#22, Adaptive Automaton#1, Goblin Tomb Raider#26] hand=[Mountain, Forerunner of the Coalition] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30045617 --game-index 45617`
- **pirates** x10 -- T5 m1 `{no land + vial:Forerunner of the Coalition}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Secluded Courtyard#43, Siren Stormtamer#47, Unclaimed Territory#57, Corsair Captain#10, Treasure Token#1000, Fiery Islet#22, Adaptive Automaton#1, Goblin Tomb Raider#26] hand=[Mountain, Forerunner of the Coalition] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30045617 --game-index 45617`
- **pirates** x10 -- T4 m1 `{no land + cast:Kitesail Larcenist(x34) + vial:Forerunner of the Coalition}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Spirebluff Canal#49, Aether Vial#5, Spirebluff Canal#50, Metallic Mimic#34, Fiery Islet#22, Corsair Captain#10, Treasure Token#1000, Dire Fleet Captain#18] hand=[Kitesail Larcenist, Fiery Islet, Forerunner of the Coalition, Secluded Courtyard] float=- life=19  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30047253 --game-index 47253`
- **pirates** x6 -- T5 m1 `{land=Mountain + cast:Kitesail Larcenist(x16) + vial:Corsair Captain}`  
  a mana choice the oracle does not drive: Fiery Islet  
  board: board=[Secluded Courtyard#44, Daring Buccaneer#16, Aether Vial#7, Fiery Islet#23, Staunch Crewmate#54, Siren Stormtamer#47, Daring Buccaneer#15, Dire Fleet Captain#19] hand=[Kitesail Larcenist, Corsair Captain, Mountain] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Pirates/Pirates.cod' --profile 'decks/Pirates/Pirates.profile.json' --games 1 --seed 30044963 --game-index 44963`
- ... 14 more signatures in flags.jsonl

