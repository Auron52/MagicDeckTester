# Mana-clean gate `20261127-215946`

Mana-clean gate run: 1 decks, games (seed 30000000 + game index) zombies 0-4999; src `a9090dff06e5`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | PHANTOM | DROP-COVERED | DOMINATED-LAND |
|---|---|---|---|---|---|---|
| zombies | 5000 | clean | 0 | 38111 | 35672 | 313 |

**MISS = 0, ORACLE-GAP = 0.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=39257 plans=296010 skipped=0 applies=6739632 frontiers=6056443 pruned=475526 options=2730736 engine_drops=73783 gate_rejects=509927(checked 509816, skipped 111) MISS=0 ORACLE-GAP=0 UNMODELED=0 CAPPED=0 ms=379615 ===
=== MANA ORACLE classes: PHANTOM=38111 DOMINATED-LAND=313 DROP-COVERED=35672 ===
=== MANA ORACLE enum-gap rejections: noop=0 hidden=0 host_blind=0 cont_land=0 ===
=== MANA ORACLE hit-only cuts: 3681507 unpayable-payment subtrees not built ===
=== MANA ORACLE order fold: 910 order variants are an earlier order's apply (trailing activations) ===
=== MANA ORACLE hit-only cuts: 116346 frontiers below a dropped action not built ===
=== MANA ORACLE board memory: 0 searches capped by MTG_MANA_ORACLE_BOARD_MB, 475526 pruned frontier boards and 3302 hit-only outcome boards given back early ===
=== MANA ORACLE shared boards: 437262 outcome boards held for 563541 outcomes (126279 shared); 0 share checks ===
=== MANA ORACLE unmodeled: 47796 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE rollback census: lost=0 depth0=0 depth1=0 depth2=0 depth3+=0 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=0 off-menu=0 need_in_plan=0 need_later=0 pay_rollback_rescued=0/0 ===
```

## Per deck

| deck | decisions | plans |  |
|---|---|---|
| zombies | 39257 | 296010 |  |

