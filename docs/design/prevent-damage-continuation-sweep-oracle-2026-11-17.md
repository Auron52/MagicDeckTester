# Prevent Damage: continuation-aware frontiers, sweep-undone cover, 400K retry (2026-11-17)

The stale-clean refresh `codex-clean-refresh-newsrc-c-20261117` (games 107,000–111,999, source `f7cce0693b6a`)
left Prevent Damage with 30 failing flags: 22 CAPPED and 8 ORACLE-GAP. All three causes are in the audit
instrument (`src/ai/ManaOracle.cpp`), not in the engine's payment. Play is unchanged: the one engine-side line is a
call that returns at once unless the state is the oracle's own copy.

## 1. CAPPED (22 flags, two seeds): retry ceiling 200,000 -> 400,000

Seed `30111776` T8 (Manabarbs / Spellshock + Dina, 16 flags) and seed `30108897` T8 (Living Wish, 3 flags,
reported x3) are late-game plans that need between 200,000 and 400,000 applies. Exact replays with
`MTG_MANA_ORACLE_NODES_RETRY=400000`:

| seed | retried / finished | CAPPED | ORACLE-GAP | other | oracle ms |
|---|---|---|---|---|---|
| 30111776 | 140 / 140 | 0 | 0 | DOMINATED-LIFE 14 (disclosed) | 388,725 |
| 30108897 | 15 / 15 | 0 | 0 | DOMINATED-LIFE 6 (disclosed) | 154,115 |

Logs: `build/scratch/spirits_shock_pair_5k/pd_30111776_cap400k.out`, `pd_30108897_cap400k.out`. The default is
now 400,000. The ceiling binds only a plan that already exhausted the previous one, so the added work is at most
(plans capped at 200,000) x 200,000 applies: 4.4M against that gate's 218M. The earlier note that kept 200,000
"because every late-game plan would pay" was mistaken about who pays. The next fresh gate measures the real cost.
`scripts/mana_oracle.py`'s CAPPED text, which still said 20000, now says 400000.

## 2. ORACLE-GAP x2, seed 30111507 T5 `{no land + cast:Pyrohemia}`: continuation-blind frontier prune

The engine pays Pyrohemia with Karplusan R + City of Brass R + Ancient Tomb. Its breakpoint continuation is then
re-solved and returns `land=Ancient Tomb, Pyrohemia x2 + Dina sac`: ping (Reflecting Pool R), a second ping that
cannot be paid, then Dina's `{1}` off the new Tomb (`{C}` floats). The oracle's other first payment (Pool +
Karplusan + Tomb, City for the ping) leaves City rather than Pool untapped at the breakpoint, and the continuation
solved on that board is `Pyrohemia x2` alone. At the third payment both histories stand on the same visible board
with the same `{R}` bill, so the Pareto prune merged them and kept the one whose continuation never sacrifices.

Fix: `Hook::cont_sig`, an order-sensitive signature of every continuation plan applied on the oracle's copy before
the frontier (`manaoracle::NoteContinuation`, called from `apply_continuation_plan` in `TurnSolver.cpp`). It is
captured into `FrontierRec::cont_sig` and mixed into the prune key only when non-zero: the same shape as the
`PaySacSpareScope` repair of 2026-11-16. The seed's replay has ORACLE-GAP 2 -> 0, applies 11,856 -> 11,970 (+1%).

## 3. ORACLE-GAP x6, seed 30109140 T5 `{land=Brushland + GSZ x3 + Rolling Earthquake}`: the sweep is life-dependent

The engine pays GSZ with Brushland G + Reflecting Pool R + Ancient Tomb (3 life left), then Earthquake with Grand
Coliseum R after Tamanoa has entered (one more Vito drain: opponent 17). The oracle prunes that history at the
second payment under the strictly healthier Pool G + Brushland C (4 life), which is sound for every mana resource.
But the end-of-main-1 pain sweep (`TapPainSourcesIfUseful`) skips a tap whose damage would kill before Tamanoa's gain
resolves. At 4 life it fires Tarnished Citadel (3 damage, +3, Vito drains 3: opponent 14), and at 3 life it
cannot. The oracle's outcome beats the engine's on life and opponent life, but its tapped Citadel is a resource the
engine's board still has, so nothing covered and the flag was ORACLE-GAP.

Fix: a fail-closed fallback in `AuditPlan`'s cover check. When nothing covers and either side's sweep tapped lands,
both boards are compared with the sweep undone (`UnSwept`: the swept lands untapped, both life totals as the first
sweep tap found them, `g_endm1_presweep_life`). Any other trace of the sweep (a counter, a death) keeps the cores
apart, so the fallback can never cover wrongly. A cover found there is classed on the pre-sweep resources. The seed
now reports ORACLE-GAP 0, DOMINATED-LIFE 30 (disclosed), applies unchanged.

## Remaining

A fresh PGO 5,000-game Prevent Damage gate on the committed source. The next untouched block is 112,000–116,999.
