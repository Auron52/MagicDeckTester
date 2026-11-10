# Mana-clean gate `sb-orderproof-20261110`

Mana-clean gate run: 1 decks, games (seed 30000000 + game index) shadowborn_apostle 30000-34999; src `5c3d77690140`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shadowborn_apostle | 5000 | issues | 210 | 2 | 44 | 163 | 1 | 19315 | 85 | 796 | 10743 | 4 |

**MISS = 209, ORACLE-GAP = 1.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=43024 plans=403774 skipped=0 applies=39185079 frontiers=31660892 pruned=3654892 options=21659309 engine_drops=20388 gate_rejects=344496(checked 344496, skipped 0) MISS=209 ORACLE-GAP=1 UNMODELED=0 CAPPED=0 ms=1109876 ===
=== MANA ORACLE classes: ORACLE-GAP=1 ENUM-GAP=2 APPLY-DROP=44 APPLY-DROP-ORDER=163 PHANTOM=19315 DOMINATED-LAND=10743 CONTINUATION-DIFFERS=85 DROP-COVERED=796 HIDDEN-INFO=4 ===
=== MANA ORACLE enum-gap rejections: noop=0 hidden=12 host_blind=0 cont_land=0 ===
=== MANA ORACLE unmodeled: 173676 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE rollback census: lost=209 depth0=0 depth1=16 depth2=24 depth3+=4 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=163 off-menu=2 need_in_plan=39 need_later=5 pay_rollback_rescued=0/44 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP |
|---|---|---|---|---|---|---|
| shadowborn_apostle | 43024 | 403774 | 2 | 44 | 163 | 1 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth1 | depth2 | depth3+ | reorder | off-menu | need-later |
|---|---|---|---|---|---|---|
| shadowborn_apostle | 16 | 24 | 4 | 163 | 2 | 5 |

## ENUM-GAP (2 flags, 2 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **shadowborn_apostle** x1 -- T6 m1 `{no land + cast:Abhorrent Overlord + cast:Dark Ritual + cast:Shadowborn Apostle}`  
  a mana gate kept this plan off the menu, but casting Shadowborn Apostle > Dark Ritual > Abhorrent Overlord pays every action with pay1[Swamp#40:B] ; pay2[Swamp#41:B] ; pay3[Swamp#42:B]  
  board: board=[Swamp#40, Swamp#41, Thrumming Stone#57, Swamp#42, Shadowborn Apostle#24, Shadowborn Apostle#22, Shadowborn Apostle#25, Shadowborn Apostle#23] hand=[Abhorrent Overlord, Dark Ritual, Thrumming Stone, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30032872 --game-index 32872`
- **shadowborn_apostle** x1 -- T4 m1 `{no land + cast:Reaper from the Abyss + cast:Dark Ritual + cast:Shadowborn Apostle}`  
  a mana gate kept this plan off the menu, but casting Shadowborn Apostle > Dark Ritual > Reaper from the Abyss pays every action with pay1[Swamp#40:B] ; pay2[Swamp#41:B] ; pay3[(float only)]  
  board: board=[Swamp#40, Swamp#41, Thrumming Stone#57, Shadowborn Apostle#28, Shadowborn Apostle#27, Shadowborn Apostle#26, Shadowborn Apostle#25, Shadowborn Apostle#24, Shadowborn Apostle#23, Shadowborn Apostle#22] hand=[Reaper from the Abyss, Dark Ritual, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033810 --game-index 33810`

## APPLY-DROP (44 flags, 16 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **shadowborn_apostle** x8 -- T6 m1 `{no land + cast:Culling the Weak + cast:Thrumming Stone + cast:Sire of Insanity + cast:Dark Ritual + eternalize:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Swamp#40:B] ; pay2[Lotus Bloom#17:R keep-floating {B}{R}] ; pay3[(float only)] ; pay4[(float only)] || ROLLBACK depth2: keep the engine's first 1 mana decision, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Lotus Bloom#17:R keep-floating {B}{R}] ; pay3[(float only)] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Swamp#40, Shadowborn Apostle#35, Shadowborn Apostle#28, Swamp#51, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033099 --game-index 33099`
- **shadowborn_apostle** x8 -- T6 m1 `{no land + cast:Culling the Weak + cast:Sire of Insanity + cast:Thrumming Stone + cast:Dark Ritual + eternalize:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Swamp#40:B] ; pay2[Lotus Bloom#17:R keep-floating {B}{R}] ; pay3[(float only)] ; pay4[(float only)] || ROLLBACK depth2: keep the engine's first 1 mana decision, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Lotus Bloom#17:R keep-floating {B}{R}] ; pay3[(float only)] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Swamp#40, Shadowborn Apostle#35, Shadowborn Apostle#28, Swamp#51, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033099 --game-index 33099`
- **shadowborn_apostle** x5 -- T6 m1 `{land=Swamp + cast:Diabolic Intent + cast:Sire of Insanity}`  
  engine dropped [cast Diabolic Intent]; every action pays with pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:R keep-floating {B}{R}] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:R keep-floating {B}{R}] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Shadowborn Apostle#21, Lake of the Dead#14, Shadowborn Apostle#25, Shadowborn Apostle#22, Shadowborn Apostle#26, Lotus Bloom#17(new)] hand=[Abhorrent Overlord, Diabolic Intent, Sire of Insanity, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30031427 --game-index 31427`
- **shadowborn_apostle** x4 -- T6 m1 `{no land + cast:Culling the Weak + cast:Thrumming Stone + cast:Sire of Insanity + cast:Dark Ritual}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Swamp#40:B] ; pay2[Lotus Bloom#17:R] ; pay3[Swamp#51:B] ; pay4[(float only)] || ROLLBACK depth2: keep the engine's first 1 mana decision, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Lotus Bloom#17:R] ; pay3[Swamp#51:B] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Swamp#40, Shadowborn Apostle#35, Shadowborn Apostle#28, Swamp#51, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033099 --game-index 33099`
- **shadowborn_apostle** x4 -- T6 m1 `{no land + cast:Culling the Weak + cast:Sire of Insanity + cast:Thrumming Stone + cast:Dark Ritual}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Swamp#40:B] ; pay2[Lotus Bloom#17:R] ; pay3[Swamp#51:B] ; pay4[(float only)] || ROLLBACK depth2: keep the engine's first 1 mana decision, re-make the rest (decision #4 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Lotus Bloom#17:R] ; pay3[Swamp#51:B] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Swamp#40, Shadowborn Apostle#35, Shadowborn Apostle#28, Swamp#51, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033099 --game-index 33099`
- **shadowborn_apostle** x2 -- T4 m1 `{no land + cast:Sire of Insanity + cast:Culling the Weak + cast:Diabolic Intent}`  
  engine dropped [cast Diabolic Intent]; every action pays with pay1[Swamp#40:B] ; pay2[Lotus Bloom#17:R] ; pay3[Swamp#41:B] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Lotus Bloom#17:R] ; pay3[Swamp#41:B]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Swamp#40, Shadowborn Apostle#21, Swamp#41, Shadowborn Apostle#22, Lotus Bloom#17(new)] hand=[Sire of Insanity, Culling the Weak, Diabolic Intent, Thrumming Stone, Diabolic Intent, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033631 --game-index 33631`
- **shadowborn_apostle** x2 -- T4 m1 `{no land + cast:Diabolic Intent + eternalize:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Swamp#40:B Lotus Bloom#17:R] ; pay2[Swamp#41:B] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)], reaching a board the menu's other order of these actions does not (Sire of Insanity#39 only in oracle; hand size differs; graveyard size differs; library size differs; float engine {B}{B} oracle {R}) || ROLLBACK depth3+: keep the engine's first 0 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[Swamp#40:B Lotus Bloom#17:R] ; pay2[Swamp#41:B] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)]; a lost action was not declared in the plan; MTG_PAY_ROLLBAC  
  board: board=[Swamp#40, Shadowborn Apostle#21, Swamp#41, Shadowborn Apostle#22, Lotus Bloom#17(new)] hand=[Sire of Insanity, Culling the Weak, Diabolic Intent, Thrumming Stone, Diabolic Intent, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033631 --game-index 33631`
- **shadowborn_apostle** x2 -- T5 m1 `{land=Swamp + cast:Demonic Tutor + cast:Sire of Insanity}`  
  engine dropped [cast Demonic Tutor]; every action pays with pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:R keep-floating {B}{R}] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:R keep-floating {B}{R}] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Lake of the Dead#14, Shadowborn Apostle#22, Lotus Bloom#17(new)] hand=[Demonic Tutor, Sire of Insanity, Profane Tutor, Thrumming Stone, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033916 --game-index 33916`
- **shadowborn_apostle** x2 -- T4 m1 `{land=Lake of the Dead + cast:Shadowborn Apostle + cast:Demonic Tutor + cast:Sire of Insanity}`  
  engine dropped [cast Demonic Tutor]; every action pays with pay1[(float only)] ; pay2[Lotus Bloom#17:R Lake of the Dead#14:B(crack) keep-floating {B}{R}] ; pay3[(float only)] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Lotus Bloom#17:R Lake of the Dead#14:B(crack) keep-floating {B}{R}] ; pay3[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Swamp#40, Swamp#41, Lotus Bloom#17(new)] hand=[Shadowborn Apostle, Demonic Tutor, Sire of Insanity, Abhorrent Overlord, Thrumming Stone, Lake of the Dead, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033977 --game-index 33977`
- **shadowborn_apostle** x1 -- T4 m1 `{land=Lake of the Dead + cast:Shadowborn Apostle + cast:Diabolic Intent + cast:Dark Ritual}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[(float only)] ; pay2[(float only)] ; pay3[(float only)] ; pay4[Blood Crypt#2:R Lake of the Dead#14:B(crack)] ; pay5[(float only)], reaching a board the menu's other order of these actions does not (Blood Crypt#2 only in engine; Shadowborn Apostle#21 only in oracle; Sire of Insanity#39 only in oracle; hand size differs; library size differs; spells cast differs; float engine - oracle {B}) || ROLLBACK depth1: keep the engine's first 3 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[(the engine's own)] ; pay4[Blood Crypt#  
  board: board=[Swamp#40, Shadowborn Apostle#21, Blood Crypt#2, Shadowborn Apostle#22] hand=[Lake of the Dead, Shadowborn Apostle, Diabolic Intent, Diabolic Intent, Sire of Insanity, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30030010 --game-index 30010`
- **shadowborn_apostle** x1 -- T4 m1 `{no land + cast:Shadowborn Apostle + cast:Diabolic Intent + cast:Dark Ritual}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Swamp#40:B] ; pay2[(float only)] ; pay3[(float only)] ; pay4[Blood Crypt#2:R Lake of the Dead#14:B(crack)] ; pay5[(float only)], reaching a board the menu's other order of these actions does not (Blood Crypt#2 only in engine; Shadowborn Apostle#21 only in oracle; Sire of Insanity#39 only in oracle; hand size differs; library size differs; spells cast differs; float engine - oracle {B}) || ROLLBACK depth1: keep the engine's first 3 mana decisions, re-make the rest (decision #5 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[(the engine's own)] ; pay4[Blood Crypt#2:  
  board: board=[Swamp#40, Shadowborn Apostle#21, Blood Crypt#2, Shadowborn Apostle#22] hand=[Lake of the Dead, Shadowborn Apostle, Diabolic Intent, Diabolic Intent, Sire of Insanity, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30030010 --game-index 30010`
- **shadowborn_apostle** x1 -- T4 m1 `{no land + cast:Dark Ritual + cast:Sire of Insanity + eternalize:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Shadowborn Apostle#21, Shadowborn Apostle#22, Lake of the Dead#14] hand=[Reaper from the Abyss, Dark Ritual, Sire of Insanity, Thrumming Stone, Shadowborn Apostle] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30031173 --game-index 31173`
- ... 4 more signatures in flags.jsonl

## APPLY-DROP-ORDER (163 flags, 9 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **shadowborn_apostle** x24 -- T6 m1 `{no land + cast:Culling the Weak + cast:Thrumming Stone + cast:Sire of Insanity + cast:Shadowborn Apostle + cast:Dark Ritual + eternalize:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; the order Culling the Weak > Shadowborn Apostle > Thrumming Stone > Dark Ritual > Sire of Insanity > eternalize:Shadowborn Apostle(x1)>Abhorrent Overlord@28 pays every action with pay1[Swamp#40:B] ; pay2[(float only)] ; pay3[Lotus Bloom#17:R Swamp#51:B keep-floating {B}{R}] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#40, Shadowborn Apostle#35, Shadowborn Apostle#28, Swamp#51, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033099 --game-index 33099`
- **shadowborn_apostle** x24 -- T6 m1 `{no land + cast:Culling the Weak + cast:Sire of Insanity + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + eternalize:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; the order Culling the Weak > Shadowborn Apostle > Thrumming Stone > Dark Ritual > Sire of Insanity > eternalize:Shadowborn Apostle(x1)>Abhorrent Overlord@28 pays every action with pay1[Swamp#40:B] ; pay2[(float only)] ; pay3[Lotus Bloom#17:R Swamp#51:B keep-floating {B}{R}] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#40, Shadowborn Apostle#35, Shadowborn Apostle#28, Swamp#51, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033099 --game-index 33099`
- **shadowborn_apostle** x24 -- T6 m1 `{no land + cast:Culling the Weak + cast:Thrumming Stone + cast:Sire of Insanity + cast:Shadowborn Apostle + cast:Dark Ritual + eternalize:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; the order Culling the Weak > Shadowborn Apostle > Thrumming Stone > Dark Ritual > Sire of Insanity > eternalize:Shadowborn Apostle(x1)>Jetmir, Nexus of Revels@28 pays every action with pay1[Swamp#40:B] ; pay2[(float only)] ; pay3[Lotus Bloom#17:R Swamp#51:B keep-floating {B}{R}] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#40, Shadowborn Apostle#35, Shadowborn Apostle#28, Swamp#51, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033099 --game-index 33099`
- **shadowborn_apostle** x24 -- T6 m1 `{no land + cast:Culling the Weak + cast:Sire of Insanity + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + eternalize:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; the order Culling the Weak > Shadowborn Apostle > Thrumming Stone > Dark Ritual > Sire of Insanity > eternalize:Shadowborn Apostle(x1)>Jetmir, Nexus of Revels@28 pays every action with pay1[Swamp#40:B] ; pay2[(float only)] ; pay3[Lotus Bloom#17:R Swamp#51:B keep-floating {B}{R}] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#40, Shadowborn Apostle#35, Shadowborn Apostle#28, Swamp#51, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033099 --game-index 33099`
- **shadowborn_apostle** x24 -- T6 m1 `{no land + cast:Culling the Weak + cast:Thrumming Stone + cast:Sire of Insanity + cast:Shadowborn Apostle + cast:Dark Ritual}`  
  engine dropped [cast Sire of Insanity]; the order Culling the Weak > Shadowborn Apostle > Thrumming Stone > Dark Ritual > Sire of Insanity pays every action with pay1[Swamp#40:B] ; pay2[(float only)] ; pay3[Lotus Bloom#17:R Swamp#51:B keep-floating {B}{R}] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#40, Shadowborn Apostle#35, Shadowborn Apostle#28, Swamp#51, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033099 --game-index 33099`
- **shadowborn_apostle** x24 -- T6 m1 `{no land + cast:Culling the Weak + cast:Sire of Insanity + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual}`  
  engine dropped [cast Sire of Insanity]; the order Culling the Weak > Shadowborn Apostle > Thrumming Stone > Dark Ritual > Sire of Insanity pays every action with pay1[Swamp#40:B] ; pay2[(float only)] ; pay3[Lotus Bloom#17:R Swamp#51:B keep-floating {B}{R}] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#40, Shadowborn Apostle#35, Shadowborn Apostle#28, Swamp#51, Lotus Bloom#17(new)] hand=[Culling the Weak, Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033099 --game-index 33099`
- **shadowborn_apostle** x16 -- T3 m2 `{no land + cast:Shadowborn Apostle + cast:Reaper from the Abyss + cast:Dark Ritual + cast:Dark Ritual + cast:Thrumming Stone}`  
  engine dropped [cast Reaper from the Abyss]; the order Dark Ritual > Thrumming Stone > Shadowborn Apostle > Dark Ritual > Reaper from the Abyss pays every action with pay1[Swamp#41:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Lake of the Dead#14, Swamp#41(new)] hand=[Shadowborn Apostle, Reaper from the Abyss, Dark Ritual, Dark Ritual, Swamp, Thrumming Stone, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30031429 --game-index 31429`
- **shadowborn_apostle** x2 -- T4 m2 `{no land + cast:Culling the Weak + cast:Diabolic Intent + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Dark Ritual}`  
  engine dropped [cast Diabolic Intent]; the order Shadowborn Apostle > Culling the Weak > Dark Ritual > Diabolic Intent > Diabolic Intent pays every action with pay1[Swamp#52:B] ; pay2[Lake of the Dead#16:B] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)]  
  board: board=[Lake of the Dead#16, Thrumming Stone#59, Swamp#52(new)] hand=[Culling the Weak, Diabolic Intent, Diabolic Intent, Shadowborn Apostle, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30031337 --game-index 31337`
- **shadowborn_apostle** x1 -- T4 m2 `{no land + cast:Shadowborn Apostle + cast:Culling the Weak}`  
  engine dropped [cast Culling the Weak]; the order Culling the Weak > Shadowborn Apostle pays every action with pay1[Lake of the Dead#14:B] ; pay2[(float only)]  
  board: board=[Lake of the Dead#14, Reaper from the Abyss#20(T)] hand=[Profane Tutor, Shadowborn Apostle, Culling the Weak] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30033110 --game-index 33110`

## ORACLE-GAP (1 flags, 1 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **shadowborn_apostle** x1 -- T5 m2 `{no land + cast:Reaper from the Abyss}`  
  10 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#14:B Swamp#48:B Swamp#50:B Lotus Bloom#17:W]: Lotus Bloom#17 only in engine; Swamp#48 only in oracle; float engine - oracle -; engine ended board=[Shadowborn Apostle#25(T), Lake of the Dead#14(T), Shadowborn Apostle#22(T), Shadowborn Apostle#21(T), Swamp#50(T), Lotus Bloom#17(new), Reaper from the Abyss#20(new)] hand=[Abhorrent Overlord] float=- life=20  
  board: board=[Shadowborn Apostle#25(T), Lake of the Dead#14, Shadowborn Apostle#22(T), Swamp#48, Shadowborn Apostle#21(T), Swamp#50, Lotus Bloom#17(new)] hand=[Abhorrent Overlord, Reaper from the Abyss] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30031552 --game-index 31552`

