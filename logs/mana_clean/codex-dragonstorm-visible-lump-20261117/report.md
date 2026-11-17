# Mana-clean gate `codex-dragonstorm-visible-lump-20261117`

Mana-clean gate run: 1 decks, games (seed 30000000 + game index) dragonstorm 62000-66999; src `4f1fba3d1d77`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP | PHANTOM | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|
| dragonstorm | 5000 | issues | 93 | 1 | 86 | 4 | 2 | 10400 | 1014 | 14749 | 244 |

**MISS = 91, ORACLE-GAP = 2.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=38609 plans=181572 skipped=0 applies=63187233 frontiers=57710189 pruned=28040920 options=53605359 engine_drops=11507 gate_rejects=584330(checked 584330, skipped 0) MISS=91 ORACLE-GAP=2 UNMODELED=0 CAPPED=0 ms=2448647 ===
=== MANA ORACLE classes: ORACLE-GAP=2 ENUM-GAP=1 APPLY-DROP=86 APPLY-DROP-ORDER=4 PHANTOM=10400 DOMINATED-LAND=14749 DROP-COVERED=1014 HIDDEN-INFO=244 ===
=== MANA ORACLE enum-gap rejections: noop=15722 hidden=4932 host_blind=0 cont_land=239 ===
=== MANA ORACLE hit-only cuts: 15238096 unpayable-payment subtrees not built ===
=== MANA ORACLE order fold: 47 plans past the order bound searched by fold class ===
=== MANA ORACLE hit-only cuts: 12862 frontiers below a dropped action not built ===
=== MANA ORACLE capped-plan escalation: 1451 retried, 1451 finished ===
=== MANA ORACLE unmodeled: 135 x floating pool too large to enumerate set-asides ===
=== MANA ORACLE rollback census: lost=91 depth0=0 depth1=20 depth2=9 depth3+=37 inconsistent=0 capped=18 unfaithful=2 no-failed-payment=0 unmodeled=0 reorder=4 off-menu=1 need_in_plan=33 need_later=53 pay_rollback_rescued=0/86 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP |
|---|---|---|---|---|---|---|
| dragonstorm | 38609 | 181572 | 1 | 86 | 4 | 2 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth1 | depth2 | depth3+ | capped | unfaithful | reorder | off-menu | need-later |
|---|---|---|---|---|---|---|---|---|
| dragonstorm | 20 | 9 | 37 | 18 | 2 | 4 | 1 | 53 |

## ENUM-GAP (1 flags, 1 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **dragonstorm** x1 -- T4 m1 `{no land [ordered] + cast:Rite of Flame + cast:Pyretic Ritual + cast:Seething Song + cast:Seething Song + cast:Apex of Power + cast:Apex of Power}`  
  a mana gate kept this plan off the menu, but it pays every action with pay1[Lotus Bloom#20:R] ; pay2[(float only)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Dwarven Hold#14:R] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)] ; pay9[(float only)]  
  board: board=[Dwarven Hold#14, Lotus Bloom#20(new)] hand=[Apex of Power, Pyretic Ritual, Apex of Power, Rite of Flame, Seething Song, Seething Song] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30062821 --game-index 62821`

## APPLY-DROP (86 flags, 41 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **dragonstorm** x6 -- T6 m1 `{land=Mountain [ordered] + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power + cast:Irencrag Feat + cast:Karrthus, Tyrant of Jund}`  
  engine dropped [cast Karrthus, Tyrant of Jund]; every action pays with pay1[Mountain#26:R] ; pay2[(float only)] ; pay3[Lotus Bloom#18:B Mercadian Bazaar#22:R] ; pay4[(float only)] ; pay5[keep-floating {B}] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)] ; pay9[(float only)] ; pay10[Lotus Bloom#19:G] || ROLLBACK depth3+: keep the engine's first 2 mana decisions, re-make the rest (decision #11 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Lotus Bloom#19:G keep-floating {B}] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)] ; pay9[(float only)] ; pay10[(float onl  
  board: board=[Mercadian Bazaar#22, Ruby Medallion#43, Lotus Bloom#18(new), Lotus Bloom#19(new)] hand=[Irencrag Feat, Seething Song, Apex of Power, Desperate Ritual, Karrthus, Tyrant of Jund, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30065244 --game-index 65244`
- **dragonstorm** x6 -- T6 m1 `{land=Mountain [ordered] + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power + cast:Irencrag Feat}`  
  engine dropped [cast Karrthus, Tyrant of Jund]; every action pays with pay1[Mountain#26:R] ; pay2[(float only)] ; pay3[Lotus Bloom#18:B Mercadian Bazaar#22:R] ; pay4[(float only)] ; pay5[keep-floating {B}] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)] ; pay9[(float only)] ; pay10[Lotus Bloom#19:G], reaching a board the menu's other order of these actions does not (Mercadian Bazaar#22 tapped by oracle only; Mercadian Bazaar#22 storage counters differ; Lotus Bloom#19 only in engine; Karrthus, Tyrant of Jund#16 only in oracle; hand cards differ; graveyard size differs; library size differs; spells cast differs; float engine {R}{R}{R}{R}{R}{R}{R}{R}{R}{R} oracle {R}{R}{R}{R}{R}{  
  board: board=[Mercadian Bazaar#22, Ruby Medallion#43, Lotus Bloom#18(new), Lotus Bloom#19(new)] hand=[Irencrag Feat, Seething Song, Apex of Power, Desperate Ritual, Karrthus, Tyrant of Jund, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30065244 --game-index 65244`
- **dragonstorm** x6 -- T6 m1 `{land=Mountain [ordered] + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power}`  
  engine dropped [cast Karrthus, Tyrant of Jund]; every action pays with pay1[Mountain#26:R] ; pay2[(float only)] ; pay3[Lotus Bloom#18:B Mercadian Bazaar#22:R] ; pay4[(float only)] ; pay5[keep-floating {B}] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)] ; pay9[(float only)] ; pay10[Lotus Bloom#19:G], reaching a board the menu's other order of these actions does not (Mercadian Bazaar#22 tapped by oracle only; Mercadian Bazaar#22 storage counters differ; Lotus Bloom#19 only in engine; Karrthus, Tyrant of Jund#16 only in oracle; hand cards differ; graveyard size differs; library size differs; spells cast differs; float engine {R}{R}{R}{R}{R}{R}{R}{R}{R}{R} oracle {R}{R}{R}{R}{R}{  
  board: board=[Mercadian Bazaar#22, Ruby Medallion#43, Lotus Bloom#18(new), Lotus Bloom#19(new)] hand=[Irencrag Feat, Seething Song, Apex of Power, Desperate Ritual, Karrthus, Tyrant of Jund, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30065244 --game-index 65244`
- **dragonstorm** x5 -- T4 m1 `{land=Mountain [ordered] + cast:Desperate Ritual + cast:Apex of Power + cast:Irencrag Feat + cast:Dragonlord Kolaghan}`  
  engine dropped [cast Dragonlord Kolaghan]; every action pays with pay1[Mountain#26:R] ; pay2[Lotus Bloom#18:W Lotus Bloom#19:B Sandstone Needle#46:R keep-floating {B}{R}] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)] || ROLLBACK depth3+: keep the engine's first 1 mana decision, re-make the rest (decision #8 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Lotus Bloom#18:W Lotus Bloom#19:B Sandstone Needle#46:R keep-floating {B}{R}] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)]; MTG_PAY_ROLLBACK (force  
  board: board=[Sandstone Needle#46, Ruby Medallion#43, Lotus Bloom#18(new), Lotus Bloom#19(new)] hand=[Apex of Power, Mountain, Desperate Ritual, Dragonlord Kolaghan, Utvara Hellkite, Irencrag Feat] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30063377 --game-index 63377`
- **dragonstorm** x5 -- T5 m1 `{no land [ordered] + cast:Desperate Ritual + cast:Apex of Power}`  
  engine dropped [cast Utvara Hellkite]; every action pays with pay1[Sandstone Needle#47:R] ; pay2[Sandstone Needle#49:R Lotus Bloom#19:B Mountain#27:R] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)] ; pay9[(float only)], reaching a board the menu's other order of these actions does not (Ruby Medallion#43 only in oracle; Utvara Hellkite#59 only in oracle; Dragonlord Kolaghan#9 only in oracle; Scourge of Valkas#52 only in oracle; hand size differs; graveyard size differs; opponent life differs; spells cast differs; float engine {R}{R}{R}{R}{R}{R}{R}{R}{R}{R}{R} oracle {R}{R}{R}{R}) || ROLLBACK depth3+: keep the engin  
  board: board=[Sandstone Needle#49, Mountain#27, Ruby Medallion#44, Sandstone Needle#47, Lotus Bloom#19(new)] hand=[Desperate Ritual, Utvara Hellkite, Pyretic Ritual, Apex of Power] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30065288 --game-index 65288`
- **dragonstorm** x5 -- T6 m1 `{no land [ordered] + cast:Desperate Ritual + cast:Seething Song + cast:Apex of Power}`  
  engine dropped [cast Karrthus, Tyrant of Jund]; every action pays with pay1[Mercadian Bazaar#22:R] ; pay2[(float only)] ; pay3[Lotus Bloom#18:B Lotus Bloom#19:G keep-floating {B}{G}{G}] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)] ; pay8[(float only)] ; pay9[(float only)] ; pay10[(float only)], reaching a board the menu's other order of these actions does not (Lotus Bloom#19 only in engine; Mountain#26 only in oracle; Karrthus, Tyrant of Jund#16 only in oracle; hand size differs; graveyard size differs; library size differs; spells cast differs; float engine {R}{R}{R}{R}{R}{R}{R}{R}{R}{R} oracle {R}{R}{R}{R}{R}{R}{R}{R}{R}{R}{R}{R}) || ROLLBACK depth3+:  
  board: board=[Mercadian Bazaar#22, Ruby Medallion#43, Lotus Bloom#18(new), Lotus Bloom#19(new)] hand=[Irencrag Feat, Seething Song, Apex of Power, Desperate Ritual, Karrthus, Tyrant of Jund, Mountain] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30065244 --game-index 65244`
- **dragonstorm** x3 -- T4 m1 `{no land [ordered] + cast:Pyretic Ritual + cast:Pyretic Ritual + cast:Lathliss, Dragon Queen + cast:Irencrag Feat + cast:Scourge of Valkas}`  
  engine dropped [cast Irencrag Feat, cast Scourge of Valkas]; every action pays with pay1[Mountain#26:R] ; pay2[(float only)] ; pay3[Unclaimed Territory#57:C keep-floating {R}] ; pay4[Mercadian Bazaar#22:R] ; pay5[(float only)], reaching a board the menu's other order of these actions does not (Unclaimed Territory#57 tapped by oracle only; 5/5 Dragon Token#1000 only in oracle; opponent life differs; float engine {R}{R} oracle {R}{R}{R}) || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Unclaimed Territory#57:C keep-floating {R}] ; pay4[Mercadi  
  board: board=[Mercadian Bazaar#22, Mountain#26, Unclaimed Territory#57, Ruby Medallion#43] hand=[Dragonlord Kolaghan, Pyretic Ritual, Apex of Power, Scourge of Valkas, Lathliss, Dragon Queen, Pyretic Ritual, Irencrag Feat] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30062531 --game-index 62531`
- **dragonstorm** x3 -- T5 m1 `{no land [ordered] + cast:Rite of Flame + cast:Rite of Flame + cast:Lathliss, Dragon Queen}`  
  engine dropped [cast Desperate Ritual, cast Scourge of Valkas]; every action pays with pay1[Sandstone Needle#47:R] ; pay2[(float only)] ; pay3[Unclaimed Territory#57:C keep-floating {R}] ; pay4[(float only)] ; pay5[Unclaimed Territory#58:C], reaching a board the menu's other order of these actions does not (Unclaimed Territory#57 tapped by oracle only; Unclaimed Territory#58 tapped by oracle only; Scourge of Valkas#52 only in oracle; 5/5 Dragon Token#1000 only in oracle; hand size differs; graveyard size differs; opponent life differs; spells cast differs; float engine - oracle -) || ROLLBACK depth1: keep the engine's first 2 mana decisions, re-make the rest (decision #4 is the payment that   
  board: board=[Unclaimed Territory#57, Unclaimed Territory#58, Ruby Medallion#45, Sandstone Needle#47] hand=[Rite of Flame, Sandstone Needle, Rite of Flame, Desperate Ritual, Scourge of Valkas, Lathliss, Dragon Queen] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30065191 --game-index 65191`
- **dragonstorm** x3 -- T5 m1 `{land=Unclaimed Territory [ordered] + cast:Seething Song + cast:Apex of Power + cast:Scourge of Valkas}`  
  engine dropped [cast Karrthus, Tyrant of Jund]; every action pays with pay1[Mountain#26:R Lotus Bloom#18:B] ; pay2[Mercadian Bazaar#22:R Sandstone Needle#46:R keep-floating {B}{R}] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Unclaimed Territory#57:G(creature-only)], reaching a board the menu's other order of these actions does not (Mountain#27 tapped by engine only; Lotus Bloom#18 only in engine; Ruby Medallion#44 only in oracle; Karrthus, Tyrant of Jund#16 only in oracle; hand size differs; graveyard size differs; opponent life differs; spells cast differs; float engine {R}{R}{R}{R}{R}{R}{R} oracle {R}{R}{R}) || ROLLBACK capped: the payment search hit its cap before a prefix was found;  
  board: board=[Sandstone Needle#46, Mercadian Bazaar#22, Ruby Medallion#43, Mountain#26, Mountain#27, Lotus Bloom#18(new)] hand=[Unclaimed Territory, Apex of Power, Seething Song, Scourge of Valkas, Karrthus, Tyrant of Jund] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30065294 --game-index 65294`
- **dragonstorm** x3 -- T5 m1 `{land=Unclaimed Territory [ordered] + cast:Seething Song + cast:Apex of Power}`  
  engine dropped [cast Karrthus, Tyrant of Jund]; every action pays with pay1[Mountain#26:R Lotus Bloom#18:B] ; pay2[Mercadian Bazaar#22:R Sandstone Needle#46:R keep-floating {B}{R}] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Unclaimed Territory#57:G(creature-only)], reaching a board the menu's other order of these actions does not (Mountain#27 tapped by engine only; Lotus Bloom#18 only in engine; Ruby Medallion#44 only in oracle; Scourge of Valkas#50 only in oracle; Karrthus, Tyrant of Jund#16 only in oracle; hand size differs; graveyard size differs; opponent life differs; spells cast differs; float engine {R}{R}{R}{R}{R}{R}{R}{R}{R}{R}{R} oracle {R}{R}{R}) || ROLLBACK capped: the paym  
  board: board=[Sandstone Needle#46, Mercadian Bazaar#22, Ruby Medallion#43, Mountain#26, Mountain#27, Lotus Bloom#18(new)] hand=[Unclaimed Territory, Apex of Power, Seething Song, Scourge of Valkas, Karrthus, Tyrant of Jund] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30065294 --game-index 65294`
- **dragonstorm** x2 -- T4 m1 `{land=Mountain [ordered] + cast:Desperate Ritual + cast:Apex of Power + cast:Irencrag Feat + cast:Dragonlord Kolaghan}`  
  engine dropped [cast Dragonlord Kolaghan]; every action pays with pay1[Mountain#26:R] ; pay2[Lotus Bloom#18:W Lotus Bloom#19:B Sandstone Needle#46:R keep-floating {B}{R}] ; pay3[(float only)] ; pay4[(float only)] || ROLLBACK depth2: keep the engine's first 1 mana decision, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Lotus Bloom#18:W Lotus Bloom#19:B Sandstone Needle#46:R keep-floating {B}{R}] ; pay3[(float only)] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Sandstone Needle#46, Ruby Medallion#43, Lotus Bloom#18(new), Lotus Bloom#19(new)] hand=[Apex of Power, Mountain, Desperate Ritual, Dragonlord Kolaghan, Utvara Hellkite, Irencrag Feat] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30063377 --game-index 63377`
- **dragonstorm** x2 -- T6 m1 `{no land [ordered] + cast:Rite of Flame + cast:Lathliss, Dragon Queen}`  
  engine dropped [cast Dragonlord Kolaghan]; every action pays with pay1[Mountain#34:R] ; pay2[Sandstone Needle#47:R Lotus Bloom#21:B keep-floating {B}] ; pay3[Sandstone Needle#48:R keep-floating {B}] ; pay4[(float only)] ; pay5[(float only)], reaching a board the menu's other order of these actions does not (Lotus Bloom#21 only in engine; Dragonlord Kolaghan#9 only in oracle; 5/5 Dragon Token#1000 only in oracle; hand size differs; graveyard size differs; spells cast differs; float engine - oracle -) || ROLLBACK depth3+: keep the engine's first 1 mana decision, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Sandstone Needle#47:  
  board: board=[Sandstone Needle#47, Mountain#34, Sandstone Needle#48, Lotus Bloom#21(new)] hand=[Pyretic Ritual, Dragonlord Kolaghan, Rite of Flame, Seething Song, Seething Song, Lathliss, Dragon Queen] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30063357 --game-index 63357`
- ... 29 more signatures in flags.jsonl

## APPLY-DROP-ORDER (4 flags, 4 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **dragonstorm** x1 -- T3 m1 `{no land [ordered] + cast:Ruby Medallion + cast:Rite of Flame + cast:Pyretic Ritual + cast:Seething Song + cast:Dragonlord Kolaghan}`  
  engine dropped [cast Rite of Flame, cast Pyretic Ritual, cast Seething Song, cast Dragonlord Kolaghan]; the order Pyretic Ritual > Rite of Flame > Ruby Medallion > Seething Song > Dragonlord Kolaghan pays every action with pay1[Sandstone Needle#46:R] ; pay2[(float only)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[Unclaimed Territory#57:B(creature-only)]  
  board: board=[Sandstone Needle#46, Unclaimed Territory#57] hand=[Dragonlord Kolaghan, Apex of Power, Scourge of Valkas, Rite of Flame, Pyretic Ritual, Seething Song, Ruby Medallion] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30064556 --game-index 64556`
- **dragonstorm** x1 -- T2 m1 `{land=Mountain [ordered] + cast:Rite of Flame + cast:Ruby Medallion + cast:Pyretic Ritual + cast:Pyretic Ritual + cast:Seething Song + cast:Dragonlord Kolaghan}`  
  engine dropped [cast Pyretic Ritual, cast Pyretic Ritual, cast Seething Song, cast Dragonlord Kolaghan]; the order Rite of Flame > Pyretic Ritual > Pyretic Ritual > Ruby Medallion > Seething Song > Dragonlord Kolaghan pays every action with pay1[Mountain#26:R] ; pay2[(float only)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[Unclaimed Territory#57:B(creature-only)]  
  board: board=[Unclaimed Territory#57] hand=[Pyretic Ritual, Ruby Medallion, Dragonstorm, Seething Song, Dragonlord Kolaghan, Mountain, Rite of Flame, Pyretic Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30064889 --game-index 64889`
- **dragonstorm** x1 -- T3 m1 `{land=Mountain [ordered] + cast:Rite of Flame + cast:Ruby Medallion + cast:Rite of Flame + cast:Pyretic Ritual + cast:Irencrag Feat + cast:Dragonlord Kolaghan}`  
  engine dropped [cast Rite of Flame, cast Pyretic Ritual, cast Irencrag Feat, cast Dragonlord Kolaghan]; the order Rite of Flame > Pyretic Ritual > Rite of Flame > Ruby Medallion > Irencrag Feat > Dragonlord Kolaghan pays every action with pay1[Mountain#26:R] ; pay2[(float only)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[Unclaimed Territory#57:B(creature-only)]  
  board: board=[Unclaimed Territory#57] hand=[Mountain, Irencrag Feat, Pyretic Ritual, Apex of Power, Dragonlord Kolaghan, Ruby Medallion, Rite of Flame, Rite of Flame] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30065592 --game-index 65592`
- **dragonstorm** x1 -- T2 m1 `{land=Mountain [ordered] + cast:Rite of Flame + cast:Ruby Medallion + cast:Pyretic Ritual + cast:Desperate Ritual + cast:Seething Song + cast:Dragonlord Kolaghan}`  
  engine dropped [cast Pyretic Ritual, cast Desperate Ritual, cast Seething Song, cast Dragonlord Kolaghan]; the order Rite of Flame > Desperate Ritual > Pyretic Ritual > Ruby Medallion > Seething Song > Dragonlord Kolaghan pays every action with pay1[Mountain#26:R] ; pay2[(float only)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[Unclaimed Territory#57:B(creature-only)]  
  board: board=[Unclaimed Territory#57] hand=[Seething Song, Ruby Medallion, Dragonlord Kolaghan, Desperate Ritual, Pyretic Ritual, Dragonstorm, Mountain, Rite of Flame] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30066357 --game-index 66357`

## ORACLE-GAP (2 flags, 2 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **dragonstorm** x1 -- T5 m1 `{no land [ordered] + cast:Ruby Medallion + cast:Ruby Medallion}`  
  3 oracle outcomes, none covers the engine's -- nearest pay1[Dwarven Hold#14:R] ; pay2[Mercadian Bazaar#23:R]: Dwarven Hold#14 storage counters differ; float engine - oracle {R}; engine ended board=[Mercadian Bazaar#23(T), Dwarven Hold#14(T), Ruby Medallion#43(new), Ruby Medallion#45(new)] hand=[Dragonstorm, Lotus Bloom, Desperate Ritual, Utvara Hellkite, Seething Song] float=- life=20  
  board: board=[Mercadian Bazaar#23, Dwarven Hold#14] hand=[Dragonstorm, Lotus Bloom, Ruby Medallion, Desperate Ritual, Utvara Hellkite, Ruby Medallion, Seething Song] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30062628 --game-index 62628`
- **dragonstorm** x1 -- T5 m1 `{no land [ordered] + cast:Ruby Medallion + cast:Ruby Medallion + suspend:Lotus Bloom}`  
  3 oracle outcomes, none covers the engine's -- nearest pay1[Dwarven Hold#14:R] ; pay2[Mercadian Bazaar#23:R]: Dwarven Hold#14 storage counters differ; float engine - oracle {R}; engine ended board=[Mercadian Bazaar#23(T), Dwarven Hold#14(T), Ruby Medallion#43(new), Ruby Medallion#45(new)] hand=[Dragonstorm, Desperate Ritual, Utvara Hellkite, Seething Song] float=- life=20  
  board: board=[Mercadian Bazaar#23, Dwarven Hold#14] hand=[Dragonstorm, Lotus Bloom, Ruby Medallion, Desperate Ritual, Utvara Hellkite, Ruby Medallion, Seething Song] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Dragonstorm/Dragonstorm.cod' --profile 'decks/Dragonstorm/Dragonstorm.profile.json' --games 1 --seed 30062628 --game-index 62628`

