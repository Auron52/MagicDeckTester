# Mana-clean gate `lastweek-20261110-shadowborn-order`

Mana-clean gate run: 1 decks, games (seed 30000000 + game index) shadowborn_apostle 20000-24999; src `1d6ab1a69b4b`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP | UNMODELED | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shadowborn_apostle | 5000 | issues | 5429 | 5 | 95 | 554 | 16 | 4759 | 23241 | 45 | 761 | 10699 |

**MISS = 654, ORACLE-GAP = 16.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=42705 plans=408732 skipped=0 applies=20807967 frontiers=15720110 pruned=2727477 options=13110048 engine_drops=29438 gate_rejects=346505(checked 346505, skipped 0) MISS=654 ORACLE-GAP=16 UNMODELED=4759 CAPPED=0 ms=253366 ===
=== MANA ORACLE classes: ORACLE-GAP=16 ENUM-GAP=5 APPLY-DROP=95 APPLY-DROP-ORDER=554 PHANTOM=23241 DOMINATED-LAND=10699 CONTINUATION-DIFFERS=45 DROP-COVERED=761 UNMODELED=4759 ===
=== MANA ORACLE enum-gap rejections: noop=0 hidden=0 host_blind=0 cont_land=0 ===
=== MANA ORACLE capped-plan escalation: 1 retried, 1 finished ===
=== MANA ORACLE unmodeled: 178292 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 4759 x too many actions to try every order ===
=== MANA ORACLE rollback census: lost=654 depth0=60 depth1=22 depth2=13 depth3+=0 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=554 off-menu=5 need_in_plan=91 need_later=4 pay_rollback_rescued=0/95 ===
```

## Per deck

| deck | decisions | plans | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP | UNMODELED |
|---|---|---|---|---|---|---|---|
| shadowborn_apostle | 42705 | 408732 | 5 | 95 | 554 | 16 | 4759 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | depth0 | depth1 | depth2 | reorder | off-menu | need-later |
|---|---|---|---|---|---|---|
| shadowborn_apostle | 60 | 22 | 13 | 554 | 5 | 4 |

## ENUM-GAP (5 flags, 5 signatures)

_a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches._

- **shadowborn_apostle** x1 -- T4 m2 `{no land + cast:Abhorrent Overlord + cast:Dark Ritual + cast:Shadowborn Apostle}`  
  a mana gate kept this plan off the menu, but casting Shadowborn Apostle > Dark Ritual > Abhorrent Overlord pays every action with pay1[Swamp#43:B] ; pay2[Swamp#40:B] ; pay3[Swamp#41:B]  
  board: board=[Swamp#40, Swamp#41, Swamp#42, Thrumming Stone#57, Swamp#43(new)] hand=[Abhorrent Overlord, Dark Ritual, Sire of Insanity, Shadowborn Apostle, Thrumming Stone] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30022025 --game-index 22025`
- **shadowborn_apostle** x1 -- T4 m1 `{land=Swamp + cast:Shadowborn Apostle + cast:Abhorrent Overlord + cast:Dark Ritual}`  
  a mana gate kept this plan off the menu, but casting Shadowborn Apostle > Dark Ritual > Abhorrent Overlord pays every action with pay1[Swamp#41:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Shadowborn Apostle#28, Shadowborn Apostle#25, Shadowborn Apostle#36, Shadowborn Apostle#22] hand=[Profane Tutor, Swamp, Shadowborn Apostle, Abhorrent Overlord, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30022211 --game-index 22211`
- **shadowborn_apostle** x1 -- T4 m1 `{land=Swamp + cast:Shadowborn Apostle + cast:Abhorrent Overlord + cast:Dark Ritual + suspend:Profane Tutor}`  
  a mana gate kept this plan off the menu, but casting Shadowborn Apostle > Dark Ritual > Abhorrent Overlord pays every action with pay1[Lake of the Dead#14:B(crack)] ; pay2[(float only)] ; pay3[(float only)] ; pay4[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Shadowborn Apostle#28, Shadowborn Apostle#25, Shadowborn Apostle#36, Shadowborn Apostle#22] hand=[Profane Tutor, Swamp, Shadowborn Apostle, Abhorrent Overlord, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30022211 --game-index 22211`
- **shadowborn_apostle** x1 -- T5 m1 `{no land + cast:Abhorrent Overlord + cast:Shadowborn Apostle + cast:Dark Ritual + cast:Thrumming Stone + cast:Culling the Weak}`  
  a mana gate kept this plan off the menu, but casting Culling the Weak > Thrumming Stone > Shadowborn Apostle > Dark Ritual > Abhorrent Overlord pays every action with pay1[Lotus Bloom#17:B] ; pay2[(float only)] ; pay3[(float only)] ; pay4[Swamp#40:B] ; pay5[(float only)]  
  board: board=[Swamp#40, Shadowborn Apostle#21, Swamp#41, Shadowborn Apostle#22, Swamp#42, Lotus Bloom#17(new)] hand=[Abhorrent Overlord, Shadowborn Apostle, Dark Ritual, Thrumming Stone, Culling the Weak] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024020 --game-index 24020`
- **shadowborn_apostle** x1 -- T4 m2 `{no land + cast:Reaper from the Abyss + cast:Shadowborn Apostle + cast:Shadowborn Apostle + cast:Dark Ritual}`  
  a mana gate kept this plan off the menu, but casting Shadowborn Apostle > Dark Ritual > Reaper from the Abyss > Shadowborn Apostle pays every action with pay1[Swamp#41:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)]  
  board: board=[Lake of the Dead#14, Thrumming Stone#57, Swamp#41(new)] hand=[Reaper from the Abyss, Shadowborn Apostle, Lake of the Dead, Shadowborn Apostle, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024550 --game-index 24550`

## APPLY-DROP (95 flags, 24 signatures)

_the menu offers the plan; the engine's payment drops an action that a legal payment pays._

- **shadowborn_apostle** x39 -- T3 m1 `{land=Lake of the Dead + cast:Sire of Insanity}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Blood Crypt#2:R Lake of the Dead#16:B(crack)] || ROLLBACK depth0: keep the engine's first 0 mana decisions, re-make the rest (decision #1 is the payment that failed) and every action pays: pay1[Blood Crypt#2:R Lake of the Dead#16:B(crack)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Swamp#49] hand=[Sire of Insanity, Thrumming Stone, Shadowborn Apostle, Lake of the Dead, Shadowborn Apostle] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30020022 --game-index 20022`
- **shadowborn_apostle** x12 -- T3 m2 `{no land + cast:Sire of Insanity + cast:Shadowborn Apostle + cast:Dark Ritual}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)] ; pay2[(float only)] ; pay3[(float only)] || ROLLBACK depth2: keep the engine's first 0 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)] ; pay2[(float only)] ; pay3[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Shadowborn Apostle#21(T), Shadowborn Apostle#22(T), Lake of the Dead#14(new)] hand=[Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021585 --game-index 21585`
- **shadowborn_apostle** x7 -- T3 m2 `{no land + cast:Sire of Insanity + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Shadowborn Apostle#21(T), Shadowborn Apostle#22(T), Lake of the Dead#14(new)] hand=[Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021585 --game-index 21585`
- **shadowborn_apostle** x4 -- T3 m2 `{no land + cast:Sire of Insanity + cast:Dark Ritual}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)] ; pay2[(float only)] || ROLLBACK depth1: keep the engine's first 0 mana decisions, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)] ; pay2[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Shadowborn Apostle#21(T), Shadowborn Apostle#22(T), Lake of the Dead#14(new)] hand=[Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021585 --game-index 21585`
- **shadowborn_apostle** x4 -- T6 m1 `{no land + cast:Sire of Insanity + cast:Shadowborn Apostle + cast:Diabolic Intent}`  
  engine dropped [cast Diabolic Intent]; every action pays with pay1[Swamp#52:B] ; pay2[Lake of the Dead#16:B(crack) Lotus Bloom#17:R] ; pay3[Swamp#51:B] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Lake of the Dead#16:B(crack) Lotus Bloom#17:R] ; pay3[Swamp#51:B]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Shadowborn Apostle#27, Lake of the Dead#16, Swamp#52, Lotus Bloom#17, Swamp#51, Thrumming Stone#59] hand=[Sire of Insanity, Shadowborn Apostle, Diabolic Intent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024260 --game-index 24260`
- **shadowborn_apostle** x3 -- T3 m1 `{land=Lake of the Dead + cast:Sire of Insanity + cast:Shadowborn Apostle + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[(float only)] ; pay2[(float only)] ; pay3[Blood Crypt#2:R Lake of the Dead#14:B(crack)] || ROLLBACK depth0: keep the engine's first 2 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Blood Crypt#2:R Lake of the Dead#14:B(crack)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Shadowborn Apostle#21, Swamp#40, Shadowborn Apostle#22] hand=[Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Lake of the Dead, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021585 --game-index 21585`
- **shadowborn_apostle** x3 -- T3 m1 `{land=Lake of the Dead + cast:Sire of Insanity + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[(float only)] ; pay2[Blood Crypt#2:R Lake of the Dead#14:B(crack)] || ROLLBACK depth0: keep the engine's first 1 mana decision, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Blood Crypt#2:R Lake of the Dead#14:B(crack)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Shadowborn Apostle#21, Swamp#40, Shadowborn Apostle#22] hand=[Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Lake of the Dead, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021585 --game-index 21585`
- **shadowborn_apostle** x3 -- T4 m1 `{land=Lake of the Dead + cast:Sire of Insanity + suspend:Lotus Bloom}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)] || ROLLBACK depth0: keep the engine's first 0 mana decisions, re-make the rest (decision #1 is the payment that failed) and every action pays: pay1[Blood Crypt#2:R Lake of the Dead#14:B(crack)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Swamp#40, Shadowborn Apostle#22, Blood Crypt#2] hand=[Sire of Insanity, Thrumming Stone, Shadowborn Apostle, Lake of the Dead, Lotus Bloom] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021800 --game-index 21800`
- **shadowborn_apostle** x2 -- T3 m1 `{land=Lake of the Dead + cast:Dark Ritual + cast:Sire of Insanity}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[(float only)] ; pay2[Blood Crypt#2:R Lake of the Dead#14:B(crack)] || ROLLBACK depth0: keep the engine's first 1 mana decision, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Blood Crypt#2:R Lake of the Dead#14:B(crack)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Swamp#40] hand=[Dark Ritual, Thrumming Stone, Sire of Insanity, Swamp, Abhorrent Overlord, Lake of the Dead] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30020460 --game-index 20460`
- **shadowborn_apostle** x2 -- T3 m1 `{land=Lake of the Dead + cast:Sire of Insanity + cast:Shadowborn Apostle + cast:Dark Ritual}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[(float only)] ; pay2[(float only)] ; pay3[Blood Crypt#2:R Lake of the Dead#14:B(crack)] || ROLLBACK depth0: keep the engine's first 2 mana decisions, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[(the engine's own)] ; pay3[Blood Crypt#2:R Lake of the Dead#14:B(crack)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Shadowborn Apostle#21, Swamp#40, Shadowborn Apostle#22] hand=[Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Lake of the Dead, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021585 --game-index 21585`
- **shadowborn_apostle** x2 -- T3 m1 `{land=Lake of the Dead + cast:Sire of Insanity + cast:Dark Ritual}`  
  engine dropped [cast Sire of Insanity]; every action pays with pay1[(float only)] ; pay2[Blood Crypt#2:R Lake of the Dead#14:B(crack)] || ROLLBACK depth0: keep the engine's first 1 mana decision, re-make the rest (decision #2 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Blood Crypt#2:R Lake of the Dead#14:B(crack)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Shadowborn Apostle#21, Swamp#40, Shadowborn Apostle#22] hand=[Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Lake of the Dead, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021585 --game-index 21585`
- **shadowborn_apostle** x2 -- T4 m1 `{land=Lake of the Dead + cast:Sire of Insanity + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Diabolic Intent}`  
  engine dropped [cast Sire of Insanity, cast Diabolic Intent]; every action pays with pay1[(float only)] ; pay2[Blood Crypt#2:R Lake of the Dead#16:B(crack)] ; pay3[(float only)] ; pay4[(float only)] || ROLLBACK depth1: keep the engine's first 1 mana decision, re-make the rest (decision #3 is the payment that failed) and every action pays: pay1[(the engine's own)] ; pay2[Blood Crypt#2:R Lake of the Dead#16:B(crack)] ; pay3[(float only)] ; pay4[(float only)]; MTG_PAY_ROLLBACK (forced on) does not recover it  
  board: board=[Blood Crypt#2, Swamp#53] hand=[Thrumming Stone, Sire of Insanity, Culling the Weak, Shadowborn Apostle, Lake of the Dead, Diabolic Intent] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30022379 --game-index 22379`
- ... 12 more signatures in flags.jsonl

## APPLY-DROP-ORDER (554 flags, 9 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **shadowborn_apostle** x112 -- T6 m2 `{no land + cast:Thrumming Stone + cast:Diabolic Intent + cast:Dark Ritual + cast:Culling the Weak + cast:Shadowborn Apostle}`  
  engine dropped [cast Diabolic Intent]; the order Dark Ritual > Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Swamp#52:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Lake of the Dead#14, Swamp#52(new)] hand=[Thrumming Stone, Diabolic Intent, Jetmir, Nexus of Revels, Dark Ritual, Culling the Weak, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30020642 --game-index 20642`
- **shadowborn_apostle** x112 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Culling the Weak + cast:Thrumming Stone + cast:Dark Ritual}`  
  engine dropped [cast Diabolic Intent]; the order Dark Ritual > Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Swamp#49:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Lake of the Dead#14, Swamp#49(new)] hand=[Diabolic Intent, Shadowborn Apostle, Culling the Weak, Thrumming Stone, Dark Ritual, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30020789 --game-index 20789`
- **shadowborn_apostle** x112 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Dark Ritual + cast:Culling the Weak + cast:Thrumming Stone + cast:Shadowborn Apostle}`  
  engine dropped [cast Diabolic Intent]; the order Dark Ritual > Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Swamp#41:B] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Lake of the Dead#14, Swamp#41(new)] hand=[Diabolic Intent, Swamp, Dark Ritual, Culling the Weak, Thrumming Stone, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30023475 --game-index 23475`
- **shadowborn_apostle** x112 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual}`  
  engine dropped [cast Diabolic Intent]; the order Dark Ritual > Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Swamp#48:B] ; pay2[Lake of the Dead#16:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Lake of the Dead#16, Swamp#48(new)] hand=[Diabolic Intent, Culling the Weak, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024655 --game-index 24655`
- **shadowborn_apostle** x56 -- T3 m1 `{land=Lake of the Dead + cast:Thrumming Stone + cast:Shadowborn Apostle + cast:Culling the Weak + cast:Diabolic Intent + cast:Dark Ritual}`  
  engine dropped [cast Diabolic Intent]; the order Dark Ritual > Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[(float only)] ; pay2[Lake of the Dead#14:B(crack)] ; pay3[(float only)] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Swamp#40, Swamp#41] hand=[Thrumming Stone, Sire of Insanity, Shadowborn Apostle, Culling the Weak, Diabolic Intent, Swamp, Lake of the Dead, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30023819 --game-index 23819`
- **shadowborn_apostle** x32 -- T4 m1 `{land=Lake of the Dead + cast:Culling the Weak + cast:Thrumming Stone + cast:Diabolic Intent + cast:Shadowborn Apostle}`  
  engine dropped [cast Diabolic Intent]; the order Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Lake of the Dead#14:B(crack)] ; pay2[(float only)] ; pay3[Swamp#42:B] ; pay4[(float only)]  
  board: board=[Swamp#40, Swamp#41, Swamp#42] hand=[Culling the Weak, Thrumming Stone, Jetmir, Nexus of Revels, Diabolic Intent, Sire of Insanity, Lake of the Dead, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024353 --game-index 24353`
- **shadowborn_apostle** x8 -- T4 m1 `{land=Lake of the Dead + cast:Thrumming Stone + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Diabolic Intent + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; the order act:Shadowborn Apostle(x1)>Jetmir, Nexus of Revels@27 > Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Lake of the Dead#16:B(crack)] ; pay2[(float only)] ; pay3[Swamp#47:B] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)]  
  board: board=[Swamp#48, Swamp#56, Swamp#47] hand=[Thrumming Stone, Culling the Weak, Shadowborn Apostle, Lake of the Dead, Diabolic Intent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30022104 --game-index 22104`
- **shadowborn_apostle** x8 -- T4 m1 `{land=Lake of the Dead + cast:Thrumming Stone + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Diabolic Intent}`  
  engine dropped [cast Diabolic Intent]; the order Thrumming Stone > Shadowborn Apostle > Culling the Weak > Diabolic Intent pays every action with pay1[Lake of the Dead#16:B(crack)] ; pay2[(float only)] ; pay3[Swamp#47:B] ; pay4[(float only)] ; pay5[(float only)]  
  board: board=[Swamp#48, Swamp#56, Swamp#47] hand=[Thrumming Stone, Culling the Weak, Shadowborn Apostle, Lake of the Dead, Diabolic Intent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30022104 --game-index 22104`
- **shadowborn_apostle** x2 -- T4 m1 `{land=Lake of the Dead + cast:Diabolic Intent + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual}`  
  engine dropped [cast Diabolic Intent]; the order Thrumming Stone > Shadowborn Apostle > Dark Ritual > Diabolic Intent > Diabolic Intent pays every action with pay1[Lake of the Dead#14:B(crack)] ; pay2[(float only)] ; pay3[Swamp#56:B] ; pay4[(float only)] ; pay5[(float only)] ; pay6[(float only)] ; pay7[(float only)]  
  board: board=[Swamp#45, Swamp#47, Swamp#56] hand=[Diabolic Intent, Diabolic Intent, Shadowborn Apostle, Thrumming Stone, Lake of the Dead, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30020884 --game-index 20884`

## ORACLE-GAP (16 flags, 11 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **shadowborn_apostle** x3 -- T5 m1 `{no land + cast:Abhorrent Overlord}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:W]: core keys differ in a field not listed here; float engine {B} oracle {B}; engine ended board=[Shadowborn Apostle#21, Lake of the Dead#14(T), Shadowborn Apostle#22, Thrumming Stone#57, Abhorrent Overlord#1(new), 1/1 Harpy Token#1000(new), 1/1 Harpy Token#1001(new), 1/1 Harpy Token#1002(new), 1/1 Harpy Token#1003(new)] hand=[Shadowborn Apostle, Diabolic Intent] float={B} life=20  
  board: board=[Shadowborn Apostle#21, Lake of the Dead#14, Shadowborn Apostle#22, Swamp#42, Thrumming Stone#57, Lotus Bloom#17(new)] hand=[Abhorrent Overlord, Shadowborn Apostle, Diabolic Intent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021258 --game-index 21258`
- **shadowborn_apostle** x2 -- T5 m1 `{no land + cast:Sire of Insanity + act:Shadowborn Apostle(x1)}`  
  3 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:R]: core keys differ in a field not listed here; float engine {R}{R} oracle {R}{R}; engine ended board=[Lake of the Dead#14(T), Shadowborn Apostle#22, Shadowborn Apostle#23, Thrumming Stone#57, Sire of Insanity#39(new)] hand=[Shadowborn Apostle] float={R}{R} life=20  
  board: board=[Lake of the Dead#14, Shadowborn Apostle#22, Shadowborn Apostle#23, Swamp#41, Thrumming Stone#57, Lotus Bloom#17(new)] hand=[Sire of Insanity, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30020444 --game-index 20444`
- **shadowborn_apostle** x2 -- T5 m1 `{no land + cast:Sire of Insanity}`  
  3 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:R]: core keys differ in a field not listed here; float engine {R}{R} oracle {R}{R}; engine ended board=[Lake of the Dead#14(T), Shadowborn Apostle#22, Shadowborn Apostle#23, Thrumming Stone#57, Sire of Insanity#39(new)] hand=[Shadowborn Apostle] float={R}{R} life=20  
  board: board=[Lake of the Dead#14, Shadowborn Apostle#22, Shadowborn Apostle#23, Swamp#41, Thrumming Stone#57, Lotus Bloom#17(new)] hand=[Sire of Insanity, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30020444 --game-index 20444`
- **shadowborn_apostle** x2 -- T6 m2 `{no land + cast:Sire of Insanity + act:Shadowborn Apostle(x1)}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#16:B(crack) Lotus Bloom#17:R]: core keys differ in a field not listed here; float engine {R} oracle {R}; engine ended board=[Lake of the Dead#16(T), Swamp#51(T), Thrumming Stone#59, Sire of Insanity#39(new)] hand=[Shadowborn Apostle, Shadowborn Apostle] float={R} life=20  
  board: board=[Lake of the Dead#16, Swamp#52(T), Lotus Bloom#17, Swamp#51(T), Thrumming Stone#59] hand=[Sire of Insanity, Shadowborn Apostle, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024260 --game-index 24260`
- **shadowborn_apostle** x1 -- T5 m1 `{no land + cast:Abhorrent Overlord + act:Shadowborn Apostle(x1)}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:W]: core keys differ in a field not listed here; float engine {B} oracle {B}; engine ended board=[Shadowborn Apostle#21, Lake of the Dead#14(T), Shadowborn Apostle#22, Thrumming Stone#57, Abhorrent Overlord#1(new), 1/1 Harpy Token#1000(new), 1/1 Harpy Token#1001(new), 1/1 Harpy Token#1002(new), 1/1 Harpy Token#1003(new)] hand=[Shadowborn Apostle, Diabolic Intent] float={B} life=20  
  board: board=[Shadowborn Apostle#21, Lake of the Dead#14, Shadowborn Apostle#22, Swamp#42, Thrumming Stone#57, Lotus Bloom#17(new)] hand=[Abhorrent Overlord, Shadowborn Apostle, Diabolic Intent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021258 --game-index 21258`
- **shadowborn_apostle** x1 -- T4 m1 `{land=Swamp + cast:Abhorrent Overlord + act:Shadowborn Apostle(x1)}`  
  3 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:W] ; pay2[(float only)]: core keys differ in a field not listed here; float engine - oracle -; engine ended board=[Lake of the Dead#14(T), Thrumming Stone#57, Shadowborn Apostle#30, Shadowborn Apostle#29, Shadowborn Apostle#28, Abhorrent Overlord#1(new), 1/1 Harpy Token#1000(new), 1/1 Harpy Token#1001(new), 1/1 Harpy Token#1002(new), 1/1 Harpy Token#1003(new), 1/1 Harpy Token#1004(new), 1/1 Harpy Token#1005(new), 1/1 Harpy Token#1006(new), 1/1 Harpy Token#1007(new), 1/1 Harpy Token#1008(new), 1/1 Harpy Token#1009(new), 1/1 Harpy Token#1010(new), Jetmir, Nexus of Revels#13(new)] hand=[Shado  
  board: board=[Shadowborn Apostle#21, Shadowborn Apostle#22, Lake of the Dead#14, Thrumming Stone#57, Shadowborn Apostle#30, Shadowborn Apostle#29, Shadowborn Apostle#28, Shadowborn Apostle#27, Shadowborn Apostle#26, Shadowborn Apostle#25, Shadowborn Apostle#23, Lotus Bloom#17(new)] hand=[Abhorrent Overlord, Shadowborn Apostle, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30022703 --game-index 22703`
- **shadowborn_apostle** x1 -- T4 m1 `{land=Swamp + cast:Abhorrent Overlord}`  
  5 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#14:B(crack) Lotus Bloom#17:W]: core keys differ in a field not listed here; float engine {B} oracle {B}; engine ended board=[Shadowborn Apostle#21, Shadowborn Apostle#22, Lake of the Dead#14(T), Thrumming Stone#57, Shadowborn Apostle#30, Shadowborn Apostle#29, Shadowborn Apostle#28, Shadowborn Apostle#27, Shadowborn Apostle#26, Shadowborn Apostle#25, Shadowborn Apostle#23, Abhorrent Overlord#1(new), 1/1 Harpy Token#1000(new), 1/1 Harpy Token#1001(new), 1/1 Harpy Token#1002(new), 1/1 Harpy Token#1003(new), 1/1 Harpy Token#1004(new), 1/1 Harpy Token#1005(new), 1/1 Harpy Token#1006(new), 1/1 Harpy Token#1007(new), 1/1   
  board: board=[Shadowborn Apostle#21, Shadowborn Apostle#22, Lake of the Dead#14, Thrumming Stone#57, Shadowborn Apostle#30, Shadowborn Apostle#29, Shadowborn Apostle#28, Shadowborn Apostle#27, Shadowborn Apostle#26, Shadowborn Apostle#25, Shadowborn Apostle#23, Lotus Bloom#17(new)] hand=[Abhorrent Overlord, Shadowborn Apostle, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30022703 --game-index 22703`
- **shadowborn_apostle** x1 -- T6 m1 `{no land + cast:Sire of Insanity + cast:Diabolic Intent}`  
  7 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#16:B(crack) Lotus Bloom#17:R] ; pay2[Swamp#51:B]: core keys differ in a field not listed here; float engine {R} oracle {R}; engine ended board=[Lake of the Dead#16(T), Swamp#51(T), Thrumming Stone#59, Sire of Insanity#39(new)] hand=[Shadowborn Apostle, Shadowborn Apostle] float={R} life=20  
  board: board=[Shadowborn Apostle#27, Lake of the Dead#16, Swamp#52, Lotus Bloom#17, Swamp#51, Thrumming Stone#59] hand=[Sire of Insanity, Shadowborn Apostle, Diabolic Intent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024260 --game-index 24260`
- **shadowborn_apostle** x1 -- T6 m1 `{no land + cast:Sire of Insanity}`  
  4 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#16:B Swamp#52:B Swamp#51:B Lotus Bloom#17:R]: Swamp#51 tapped by oracle only; Swamp#52 only in oracle; graveyard size differs; float engine {R}{R} oracle -; engine ended board=[Shadowborn Apostle#27, Lake of the Dead#16(T), Swamp#51, Thrumming Stone#59, Sire of Insanity#39(new)] hand=[Shadowborn Apostle, Diabolic Intent] float={R}{R} life=20  
  board: board=[Shadowborn Apostle#27, Lake of the Dead#16, Swamp#52, Lotus Bloom#17, Swamp#51, Thrumming Stone#59] hand=[Sire of Insanity, Shadowborn Apostle, Diabolic Intent] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024260 --game-index 24260`
- **shadowborn_apostle** x1 -- T6 m2 `{no land + cast:Sire of Insanity}`  
  2 oracle outcomes, none covers the engine's -- nearest pay1[Lake of the Dead#16:B(crack) Lotus Bloom#17:R]: core keys differ in a field not listed here; float engine {R} oracle {R}; engine ended board=[Lake of the Dead#16(T), Swamp#51(T), Thrumming Stone#59, Sire of Insanity#39(new)] hand=[Shadowborn Apostle, Shadowborn Apostle] float={R} life=20  
  board: board=[Lake of the Dead#16, Swamp#52(T), Lotus Bloom#17, Swamp#51(T), Thrumming Stone#59] hand=[Sire of Insanity, Shadowborn Apostle, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024260 --game-index 24260`
- **shadowborn_apostle** x1 -- T8 m2 `{no land + cast:Abhorrent Overlord}`  
  52 oracle outcomes, none covers the engine's -- nearest pay1[Swamp#40:B Swamp#54:B Swamp#48:B Swamp#49:B Lotus Bloom#17:W]: Lake of the Dead#15 tapped by engine only; Lotus Bloom#17 only in engine; Swamp#49 tapped by oracle only; Swamp#40 only in oracle; float engine - oracle -; engine ended board=[Shadowborn Apostle#25(T), Lake of the Dead#15(T), Shadowborn Apostle#21(T), Swamp#54(T), Lotus Bloom#17, Swamp#48(T), Shadowborn Apostle#38(T), Swamp#49, Swamp#52, Abhorrent Overlord#1(new), 1/1 Harpy Token#1000(new), 1/1 Harpy Token#1001(new), 1/1 Harpy Token#1002(new), 1/1 Harpy Token#1003(new), 1/1 Harpy Token#1004(new)] hand=[] float=- life=20  
  board: board=[Shadowborn Apostle#25(T), Lake of the Dead#15, Shadowborn Apostle#21(T), Swamp#40, Swamp#54, Lotus Bloom#17, Swamp#48, Shadowborn Apostle#38(T), Swamp#49, Swamp#52] hand=[Abhorrent Overlord] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=8 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024784 --game-index 24784`

## UNMODELED (4759 flags, 127 signatures)

_the oracle cannot drive something this plan needs, so it gives no verdict._

- **shadowborn_apostle** x320 -- T1 m2 `{no land + cast:Dark Ritual + cast:Dark Ritual + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Reaper from the Abyss + cast:Culling the Weak + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Thrumming Stone]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40(new)] hand=[Dark Ritual, Dark Ritual, Shadowborn Apostle, Shadowborn Apostle, Thrumming Stone, Reaper from the Abyss, Culling the Weak] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024989 --game-index 24989`
- **shadowborn_apostle** x280 -- T1 m2 `{no land + cast:Dark Ritual + cast:Dark Ritual + cast:Shadowborn Apostle + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Reaper from the Abyss + cast:Culling the Weak + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Thrumming Stone]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40(new)] hand=[Dark Ritual, Dark Ritual, Shadowborn Apostle, Shadowborn Apostle, Thrumming Stone, Reaper from the Abyss, Culling the Weak] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024989 --game-index 24989`
- **shadowborn_apostle** x224 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Culling the Weak + cast:Thrumming Stone + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Lake of the Dead#14, Swamp#49(new)] hand=[Diabolic Intent, Shadowborn Apostle, Culling the Weak, Thrumming Stone, Dark Ritual, Swamp] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30020789 --game-index 20789`
- **shadowborn_apostle** x224 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Dark Ritual + cast:Culling the Weak + cast:Thrumming Stone + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Lake of the Dead#14, Swamp#41(new)] hand=[Diabolic Intent, Swamp, Dark Ritual, Culling the Weak, Thrumming Stone, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30023475 --game-index 23475`
- **shadowborn_apostle** x224 -- T3 m2 `{no land + cast:Diabolic Intent + cast:Culling the Weak + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Lake of the Dead#16, Swamp#48(new)] hand=[Diabolic Intent, Culling the Weak, Shadowborn Apostle, Thrumming Stone, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024655 --game-index 24655`
- **shadowborn_apostle** x144 -- T3 m2 `{no land + cast:Dark Ritual + cast:Dark Ritual + cast:Diabolic Intent + cast:Culling the Weak + cast:Thrumming Stone + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40, Swamp#41, Swamp#42(new)] hand=[Dark Ritual, Dark Ritual, Diabolic Intent, Culling the Weak, Thrumming Stone, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024589 --game-index 24589`
- **shadowborn_apostle** x128 -- T1 m2 `{no land + cast:Dark Ritual + cast:Dark Ritual + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Reaper from the Abyss + cast:Culling the Weak}`  
  engine dropped [cast Thrumming Stone]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40(new)] hand=[Dark Ritual, Dark Ritual, Shadowborn Apostle, Shadowborn Apostle, Thrumming Stone, Reaper from the Abyss, Culling the Weak] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024989 --game-index 24989`
- **shadowborn_apostle** x112 -- T6 m2 `{no land + cast:Thrumming Stone + cast:Diabolic Intent + cast:Dark Ritual + cast:Culling the Weak + cast:Shadowborn Apostle + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Lake of the Dead#14, Swamp#52(new)] hand=[Thrumming Stone, Diabolic Intent, Jetmir, Nexus of Revels, Dark Ritual, Culling the Weak, Shadowborn Apostle] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=6 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30020642 --game-index 20642`
- **shadowborn_apostle** x112 -- T1 m2 `{no land + cast:Culling the Weak + cast:Diabolic Intent + cast:Thrumming Stone + cast:Shadowborn Apostle + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40(new)] hand=[Culling the Weak, Diabolic Intent, Thrumming Stone, Demonic Tutor, Thrumming Stone, Shadowborn Apostle, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024111 --game-index 24111`
- **shadowborn_apostle** x112 -- T1 m2 `{no land + cast:Dark Ritual + cast:Dark Ritual + cast:Shadowborn Apostle + cast:Shadowborn Apostle + cast:Thrumming Stone + cast:Reaper from the Abyss + cast:Culling the Weak}`  
  engine dropped [cast Thrumming Stone]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#40(new)] hand=[Dark Ritual, Dark Ritual, Shadowborn Apostle, Shadowborn Apostle, Thrumming Stone, Reaper from the Abyss, Culling the Weak] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=1 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30024989 --game-index 24989`
- **shadowborn_apostle** x84 -- T2 m2 `{no land + cast:Diabolic Intent + cast:Dark Ritual + cast:Thrumming Stone + cast:Shadowborn Apostle + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent, cast Shadowborn Apostle (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Blood Crypt#2, Swamp#40(new)] hand=[Diabolic Intent, Dark Ritual, Thrumming Stone, Shadowborn Apostle, Swamp, Swamp, Dark Ritual] float=- life=18  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30021191 --game-index 21191`
- **shadowborn_apostle** x72 -- T3 m2 `{no land + cast:Thrumming Stone + cast:Diabolic Intent + cast:Shadowborn Apostle + cast:Dark Ritual + cast:Dark Ritual + act:Shadowborn Apostle(x1)}`  
  engine dropped [cast Diabolic Intent, cast Shadowborn Apostle (activation)]; no payment of this order realises it, and it has too many actions to try every order  
  board: board=[Swamp#51, Swamp#46] hand=[Thrumming Stone, Diabolic Intent, Shadowborn Apostle, Dark Ritual, Dark Ritual] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod' --profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30023752 --game-index 23752`
- ... 115 more signatures in flags.jsonl

