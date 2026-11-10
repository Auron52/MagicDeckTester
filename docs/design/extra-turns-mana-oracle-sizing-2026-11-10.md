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
