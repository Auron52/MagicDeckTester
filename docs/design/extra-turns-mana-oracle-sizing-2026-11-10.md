# Extra Turns mana oracle sizing tail

The 32-game `lastweek-20261110-extra` oracle-on sizing probe did not give a
useful per-game cost estimate: job `extra_turns__p3` (seed 30000003, game index
3) took 628,597 ms, while other completed jobs varied from 125 ms to 350,687
ms. Its ordinary search work was only 70 units. A direct single-game replay on
the same PGO binary took 0.13 s with `MTG_MANA_ORACLE=0`, showing that the tail
is in the oracle audit rather than the game search.

I bounded the exact p3 replay with `MTG_MANA_ORACLE_DECISION_S=1` and enabled
`MTG_MANA_ORACLE_HEAVY=1`. Oracle-on finished in 11.69 s, used 3.2 GiB peak
RSS, and made 198,971 oracle `ApplyPlan` calls. Nine turn-four main-phase
decisions spent even this one-priced-second budget; their menus reached 980
plans (another reached 932), with thousands of mana-gate-rejected variants.
The output recorded 7,404 `CAPPED-DECISION` flags. An unmodified oracle-on
replay was still active after 12 s and was stopped by the diagnostic timeout.

The default per-decision budget is 300 priced seconds
(`ManaOracle.cpp::DecisionCapNs`). Lowering it would make the run shorter by
leaving plans unaudited. `CAPPED-DECISION` fails the mana-clean gate by design,
so that is not a valid clean-gate fix. The existing gate-rejection loop already
deduplicates by `SiblingKey`, while each menu plan is audited in full because
cast order and land position may change payment and outcome.

The concrete reuse point is `AuditDecision`'s per-plan `Realize` call: an exact
ordered plan on the same complete pre-decision state has the same payment
frontier. A cache key would need the full root state (including hidden library
order, resources and phase), the ordered actions with all choices and source
IDs, land placement, and whether the audit is pre-combat. The existing
`SiblingKey` deliberately sorts actions and cannot key this cache; it groups
plans whose different cast orders the oracle must compare. A smaller candidate
is reusing `AuditPlan`'s completed `Realization` for `RollbackCore`'s `k=0`
retry on an APPLY-DROP, but p3's many full-plan audits mean that alone is
unlikely to remove this sizing tail. Neither candidate has been implemented
or measured yet.

## Targeted profile and implementation boundary

An eight-second `perf record` of the unmodified PGO p3 replay sampled the
turn-four tail. The largest exclusive symbols were `BuildSimKeyBase` (14.08%),
`GameState` copy construction (7.79%) and assignment (3.46%); the remaining
samples spread across key construction, `ApplyPlanDirect`, payment and frontier
pruning. This is repeated full-state application and identity work, not one
isolated slow payment routine. The one-second-cap replay gives a small,
repeatable workload for evaluating a future cache: 198,971 applies, an exact
class-count target under the same cap, and an oracle-on/off win turn of four.

No reuse has been landed. `SiblingKey` is intentionally order-insensitive,
while an exact `Realize` cache must preserve every choice that can change an
apply, hidden library order, and payment-frontier order. `ActionKey` names only
a subset of `Action` fields, so treating it as that exact key would risk
silently reusing a result for a different plan. An exact key or equivalence
proof is needed before the oracle can skip any of these applies while still
claiming full audit coverage.
