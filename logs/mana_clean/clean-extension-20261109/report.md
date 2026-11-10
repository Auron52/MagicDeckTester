# Mana-clean gate `clean-extension-20261109`

Mana-clean gate run: 9 decks, games (seed 30000000 + game index) auras 208000-307999, colossus_hammer 185000-284999, critterlifegain 310000-409999, giants 179000-278999, kithkin 125000-224999, knights 210000-309999, lifegainheliodcombo 310000-409999, modern_rw_burn 125000-224999, whiteknights 210000-309999; src `3bda5c647e7c`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | DOMINATED-LIFE | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND | HIDDEN-INFO |
|---|---|---|---|---|---|---|---|---|---|
| auras | 100000 | clean | 0 | 169470 | 0 | 0 | 0 | 25 | 1 |
| colossus_hammer | 100000 | clean | 0 | 0 | 0 | 0 | 0 | 4 | 0 |
| critterlifegain | 100000 | clean | 0 | 0 | 0 | 0 | 0 | 27959 | 0 |
| giants | 100000 | clean | 0 | 0 | 20325 | 0 | 0 | 16282 | 0 |
| kithkin | 100000 | invalid | 0 | 0 | 0 | 64784 | 4342 | 0 | 0 |
| knights | 100000 | clean | 0 | 0 | 0 | 0 | 0 | 358 | 0 |
| lifegainheliodcombo | 100000 | clean | 0 | 0 | 26 | 0 | 44 | 0 | 0 |
| modern_rw_burn | 100000 | clean | 0 | 0 | 170 | 0 | 0 | 0 | 0 |
| whiteknights | 100000 | clean | 0 | 0 | 33772 | 0 | 0 | 9729 | 0 |

**MISS = 0, ORACLE-GAP = 0.** Play identity with the oracle off: **MOVED -- the run is invalid**.

```
=== MANA ORACLE: decisions=6874277 plans=56355016 skipped=0 applies=5613571705 frontiers=4394355008 pruned=170856492 options=1322403049 engine_drops=123578 gate_rejects=132632797(checked 130598909, skipped 2033888) MISS=169470 ORACLE-GAP=0 UNMODELED=0 CAPPED=0 ms=71571764 ===
=== MANA ORACLE classes: PHANTOM=54293 DOMINATED-LIFE=169470 DOMINATED-LAND=54357 CONTINUATION-DIFFERS=64784 DROP-COVERED=4386 HIDDEN-INFO=1 ===
=== MANA ORACLE enum-gap rejections: noop=68036 hidden=1 host_blind=0 cont_land=17 ===
=== MANA ORACLE unmodeled: 20678248 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE unmodeled: 116 x scripted payment refused: scripted payment did not pay {R}{R}{R}{R}{R}{R} ===
=== MANA ORACLE rollback census: lost=0 depth0=0 depth1=0 depth2=0 depth3+=0 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=0 off-menu=0 need_in_plan=0 need_later=0 pay_rollback_rescued=0/0 ===
```

## Per deck

| deck | decisions | plans |  |
|---|---|---|
| auras | 718504 | 13060245 |  |
| colossus_hammer | 570726 | 3270031 |  |
| critterlifegain | 762389 | 3660140 |  |
| giants | 1028405 | 6212388 |  |
| kithkin | 804312 | 5782759 |  |
| knights | 766723 | 3530034 |  |
| lifegainheliodcombo | 822960 | 9780625 |  |
| modern_rw_burn | 669054 | 8535000 |  |
| whiteknights | 731204 | 2523794 |  |

## Kithkin identity follow-up

The only oracle-on/off play mismatch was chunk `kithkin__g173500` (250 games):
oracle-on averaged 4.4920 turns with digest `4817e9fe3ba0b4fb`; oracle-off
averaged 4.4880 with digest `fe32fade83d537d0`. Comparing the oracle decision
traces with a replay of that chunk isolates seed `30173535` (game index 173535):
the original on arm reported one plan at T3 main phase 2, where the replay
reported zero. The original on arm reached T7; the replay reached T6.

Three focused oracle-on replays all produced the oracle-off chunk digest and
average: 250 one-game jobs, the original 250-game job alone, and the 195-job
Kithkin prefix through this chunk at 29 threads. The per-game oracle-on and
oracle-off results matched in the 250-job replay. Artifacts are under
`build/scratch/mana_clean/clean-extension-20261109/kithkin-split/`.
The mismatch did not reproduce, so Kithkin remains **invalid**; this run is
not a clean stamp for it.
