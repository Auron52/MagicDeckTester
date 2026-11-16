# Mana-clean gate `codex-spirits-souls-hand-20261116`

Mana-clean gate run: 1 decks, games (seed 30000000 + game index) spirits 20000-24999; src `fff330d1eee7`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | APPLY-DROP-ORDER | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND |
|---|---|---|---|---|---|---|---|---|
| spirits | 5000 | issues | 12 | 12 | 2836 | 522 | 626 | 3607 |

**MISS = 12, ORACLE-GAP = 0.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=41226 plans=340998 skipped=0 applies=31341040 frontiers=29076305 pruned=2061878 options=16432193 engine_drops=3990 gate_rejects=1423936(checked 1412005, skipped 11931) MISS=12 ORACLE-GAP=0 UNMODELED=0 CAPPED=0 ms=779449 ===
=== MANA ORACLE classes: APPLY-DROP-ORDER=12 PHANTOM=2836 DOMINATED-LAND=3607 CONTINUATION-DIFFERS=522 DROP-COVERED=626 ===
=== MANA ORACLE enum-gap rejections: noop=0 hidden=0 host_blind=0 cont_land=0 ===
=== MANA ORACLE hit-only cuts: 19941341 unpayable-payment subtrees not built ===
=== MANA ORACLE unmodeled: 43 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE rollback census: lost=12 depth0=0 depth1=0 depth2=0 depth3+=0 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=12 off-menu=0 need_in_plan=0 need_later=0 pay_rollback_rescued=0/0 ===
```

## Per deck

| deck | decisions | plans | APPLY-DROP-ORDER |
|---|---|---|---|
| spirits | 41226 | 340998 | 12 |

## Rollback census

_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu = a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests on an engine mana choice the oracle does not drive (no depth)._

| deck | reorder |
|---|---|
| spirits | 12 |

## APPLY-DROP-ORDER (12 flags, 2 signatures)

_as APPLY-DROP, but the payable realisation needs a different cast order._

- **spirits** x6 -- T4 m1 `{land=Flooded Strand + vial:White Orchid Phantom + cast:Millicent, Restless Revenant}`  
  engine dropped [cast Millicent, Restless Revenant]; the order Millicent, Restless Revenant > vial:White Orchid Phantom>Azorius Chancery pays every action with pay1[Azorius Chancery#5:W Unclaimed Territory#52:C Godless Shrine#17:W]  
  board: board=[Usher of the Fallen#55, Aether Vial#3, Azorius Chancery#5, Unclaimed Territory#52, Aether Vial#1, Mausoleum Wanderer#27, Supreme Phantom#50] hand=[White Orchid Phantom, Millicent, Restless Revenant, Flooded Strand] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Spirits/Spirits.cod' --profile 'decks/Spirits/Spirits.profile.json' --games 1 --seed 30021365 --game-index 21365`
- **spirits** x6 -- T4 m1 `{land=Flooded Strand + vial:White Orchid Phantom + cast:Millicent, Restless Revenant}`  
  engine dropped [cast Millicent, Restless Revenant]; the order Millicent, Restless Revenant > vial:White Orchid Phantom>Azorius Chancery pays every action with pay1[Azorius Chancery#5:W Unclaimed Territory#52:C Watery Grave#59:U]  
  board: board=[Usher of the Fallen#55, Aether Vial#3, Azorius Chancery#5, Unclaimed Territory#52, Aether Vial#1, Mausoleum Wanderer#27, Supreme Phantom#50] hand=[White Orchid Phantom, Millicent, Restless Revenant, Flooded Strand] float=- life=20  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Spirits/Spirits.cod' --profile 'decks/Spirits/Spirits.profile.json' --games 1 --seed 30021365 --game-index 21365`

