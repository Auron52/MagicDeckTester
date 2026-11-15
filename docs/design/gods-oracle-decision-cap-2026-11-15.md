# Gods decision-audit cap

The incomplete Gods block on seeds 30,035,000–30,039,999 reported
`CAPPED-DECISION` on seeds 30,035,211, 30,035,417, and 30,035,829. The
decision auditor's 300-second limit applies both to deterministic priced work
and to an elapsed-wall backstop. Observe-mode enumeration, which must visit
plans the real mana gates rejected, dominates these decisions.

Single-worker PGO replays with `MTG_MANA_ORACLE_DECISION_S=900` and the
cost trace finished all three. Their expensive decisions were:

| Seed | Turn/main | Priced seconds | Wall seconds | Gate plans checked | Verdict |
|---|---:|---:|---:|---:|---|
| 30,035,417 | T5 m1 | 263.502 | 335.592 | 307 | No issues |
| 30,035,211 | T6 m1 | 205.205 | 274.589 | 400 | No issues |
| 30,035,829 | T3 m1 | 560.369 | 146.425 | 400 | No coverage gap; four `DOMINATED-LIFE` reports |

The first seed exceeded the 300-second *wall* backstop while staying below
300 priced seconds; the second had hit the wall backstop in the earlier pooled
run even though it finished in 275 wall seconds alone. The third genuinely
needed more than 300 priced seconds. The default decision budget is now 600
seconds, still finite. This is an audit-coverage change: it does not affect
the played game or change which oracle findings count as issues. The next
fresh 5,000-game Gods gate must confirm that the pooled run stays under the
new bound; otherwise improve the observe enumeration rather than treating a
truncated decision as clean.

Raw probes: `build/scratch/gods_counter_mask_ab/capped35417_900.*` and
`build/scratch/gods_counter_mask_ab/capped_other_900.*`.

The fresh Gods-trained PGO build stamped source tree
`d3191eecc309faa9f426511b261f33dd1cfb7a22` and passed the 3,350-case
Release unit suite. A three-job pooled replay at the new *default* 600-second
cap finished all three seeds with the same play digests as the 900-second
probes, zero `CAPPED`, zero `CAPPED-DECISION`, zero `ORACLE-GAP`, and zero
`UNMODELED`. The expensive decisions used the same deterministic priced work;
the pooled wall times were 305.350, 333.290, and 149.770 seconds. The four
`DOMINATED-LIFE` observations are disclosed, not mana-clean issues. Raw output
is `build/scratch/gods_counter_mask_ab/capped_default600.*`. A full fresh
5,000-game Gods gate remains owed after the payment drops are repaired.
