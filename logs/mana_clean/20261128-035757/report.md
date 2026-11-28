# Mana-clean gate `20261128-035757`

Mana-clean gate run: 1 decks, games (seed 30000000 + game index) zombies 5000-9999; src `d705b7b89e01`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | PHANTOM | DROP-COVERED | DOMINATED-LAND |
|---|---|---|---|---|---|---|
| zombies | 5000 | clean | 0 | 39654 | 34756 | 544 |

**MISS = 0, ORACLE-GAP = 0.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=39196 plans=288974 skipped=0 applies=6790731 frontiers=6148355 pruned=491936 options=2728895 engine_drops=74410 gate_rejects=514972(checked 514901, skipped 71) MISS=0 ORACLE-GAP=0 UNMODELED=0 CAPPED=0 ms=956297 ===
=== MANA ORACLE classes: PHANTOM=39654 DOMINATED-LAND=544 DROP-COVERED=34756 ===
=== MANA ORACLE enum-gap rejections: noop=0 hidden=0 host_blind=0 cont_land=0 provider=0 ===
=== MANA ORACLE hit-only cuts: 3752800 unpayable-payment subtrees not built ===
=== MANA ORACLE order fold: 272 order variants are an earlier order's apply (trailing activations) ===
=== MANA ORACLE hit-only cuts: 117578 frontiers below a dropped action not built ===
=== MANA ORACLE board memory: 0 searches capped by MTG_MANA_ORACLE_BOARD_MB, 491936 pruned frontier boards and 2221 hit-only outcome boards given back early ===
=== MANA ORACLE shared boards: 410755 outcome boards held for 522577 outcomes (111822 shared); 0 share checks ===
=== MANA ORACLE unmodeled: 46434 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE rollback census: lost=0 depth0=0 depth1=0 depth2=0 depth3+=0 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=0 off-menu=0 need_in_plan=0 need_later=0 pay_rollback_rescued=0/0 ===
```

## Per deck

| deck | decisions | plans |  |
|---|---|---|
| zombies | 39196 | 288974 |  |

