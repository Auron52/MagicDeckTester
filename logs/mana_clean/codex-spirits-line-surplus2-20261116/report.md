# Mana-clean gate `codex-spirits-line-surplus2-20261116`

Mana-clean gate run: 1 decks, games (seed 30000000 + game index) spirits 30000-34999; src `d2733ac68ff4`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND |
|---|---|---|---|---|---|---|---|
| spirits | 5000 | clean | 0 | 3334 | 658 | 533 | 3446 |

**MISS = 0, ORACLE-GAP = 0.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=41473 plans=340869 skipped=0 applies=33478758 frontiers=30916646 pruned=2527698 options=18536990 engine_drops=4525 gate_rejects=1424839(checked 1415149, skipped 9690) MISS=0 ORACLE-GAP=0 UNMODELED=0 CAPPED=0 ms=895695 ===
=== MANA ORACLE classes: PHANTOM=3334 DOMINATED-LAND=3446 CONTINUATION-DIFFERS=658 DROP-COVERED=533 ===
=== MANA ORACLE enum-gap rejections: noop=0 hidden=0 host_blind=0 cont_land=0 ===
=== MANA ORACLE hit-only cuts: 20859849 unpayable-payment subtrees not built ===
=== MANA ORACLE capped-plan escalation: 34 retried, 34 finished ===
=== MANA ORACLE unmodeled: 46 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE rollback census: lost=0 depth0=0 depth1=0 depth2=0 depth3+=0 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=0 off-menu=0 need_in_plan=0 need_later=0 pay_rollback_rescued=0/0 ===
```

## Per deck

| deck | decisions | plans |  |
|---|---|---|
| spirits | 41473 | 340869 |  |
