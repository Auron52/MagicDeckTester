# Mana-clean gate `codex-spirits-own-land-order-20261116`

Mana-clean gate run: 1 decks, games (seed 30000000 + game index) spirits 25000-29999; src `811757cbf489`, binary `build/PGO/mtg`, `MTG_MANA_ORACLE=2`. ISSUE classes: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER, DOMINATED-CREATURE, DOMINATED-ONESHOT, DOMINATED-HASTE, DOMINATED-OTHER, ORACLE-GAP, UNMODELED, CAPPED, CAPPED-DECISION (disclosed, not failed: DOMINATED-LIFE). The sections below the verdict table read only the first 5000 whole flag lines per deck and class; the table's counts are exact.

## Verdict per deck

| deck | games | verdict | issues | ORACLE-GAP | PHANTOM | CONTINUATION-DIFFERS | DROP-COVERED | DOMINATED-LAND |
|---|---|---|---|---|---|---|---|---|
| spirits | 5000 | issues | 3 | 3 | 3131 | 577 | 549 | 3526 |

**MISS = 0, ORACLE-GAP = 3.** Play identity with the oracle off: byte-identical.

```
=== MANA ORACLE: decisions=41468 plans=338591 skipped=0 applies=33233135 frontiers=30952772 pruned=2571325 options=18113614 engine_drops=4253 gate_rejects=1438200(checked 1425800, skipped 12400) MISS=0 ORACLE-GAP=3 UNMODELED=0 CAPPED=0 ms=855139 ===
=== MANA ORACLE classes: ORACLE-GAP=3 PHANTOM=3131 DOMINATED-LAND=3526 CONTINUATION-DIFFERS=577 DROP-COVERED=549 ===
=== MANA ORACLE enum-gap rejections: noop=0 hidden=0 host_blind=0 cont_land=0 ===
=== MANA ORACLE hit-only cuts: 20794170 unpayable-payment subtrees not built ===
=== MANA ORACLE unmodeled: 36 x gate-rejected loop / combo variant (not driven) ===
=== MANA ORACLE rollback census: lost=0 depth0=0 depth1=0 depth2=0 depth3+=0 inconsistent=0 capped=0 unfaithful=0 no-failed-payment=0 unmodeled=0 reorder=0 off-menu=0 need_in_plan=0 need_later=0 pay_rollback_rescued=0/0 ===
```

## Per deck

| deck | decisions | plans | ORACLE-GAP |
|---|---|---|---|
| spirits | 41468 | 338591 | 3 |

## ORACLE-GAP (3 flags, 1 signatures)

_the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here._

- **spirits** x3 -- T5 m1 `{land=Secluded Courtyard + cast:Millicent, Restless Revenant + cast:White Orchid Phantom}`  
  21 oracle outcomes, none covers the engine's -- nearest pay1[Godless Shrine#16:W Watery Grave#59:U Secluded Courtyard#40:W(creature-only)] ; pay2[Godless Shrine#17:W]: Hallowed Fountain#18 tapped by engine only; Secluded Courtyard#40 tapped by oracle only; same core; float engine - oracle -; engine ended board=[Hallowed Fountain#18(T), Godless Shrine#16(T), Spirit of the Labyrinth#46, Godless Shrine#17(T), Drogskol Captain#6, Mausoleum Wanderer#24, Drogskol Captain#7, Secluded Courtyard#40(new), White Orchid Phantom#60(new), Island#19(T)(new), Millicent, Restless Revenant#31(new)] hand=[Metallic Mimic] float=- life=10  
  board: board=[Hallowed Fountain#18, Godless Shrine#16, Spirit of the Labyrinth#46, Godless Shrine#17, Drogskol Captain#6, Watery Grave#59, Mausoleum Wanderer#24, Drogskol Captain#7] hand=[Metallic Mimic, Millicent, Restless Revenant, White Orchid Phantom, Secluded Courtyard] float=- life=10  
  repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=5 build/Release/mtg 'decks/Spirits/Spirits.cod' --profile 'decks/Spirits/Spirits.profile.json' --games 1 --seed 30025459 --game-index 25459`

