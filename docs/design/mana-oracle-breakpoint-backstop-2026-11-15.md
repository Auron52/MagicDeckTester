# Oracle breakpoint backstop

The 2026-11-15 Gods mana-clean diagnostic stopped after 311 of 312 oracle-on
chunks. Seed 30034271, turn seven, main one, had a 12-plan decision whose
`ApplyPlanDirect` repeatedly derived breakpoint continuations of roughly
2,700 to 20,300 plans. The old decision work cap checked between audits;
it could not stop a long derivation inside one apply. That chunk ran about
45 minutes without a verdict before it was stopped. A focused Release run
with the default 300 priced-second cap was also stopped after five minutes;
its trace showed hundreds of repeated derivations and no cap verdict.

The oracle now polls the priced decision budget during breakpoint enumeration,
including deferred odometer visits. An interrupted derivation is discarded
through the existing `g_bp_derive_interrupted` path, and its decision reports
`CAPPED-DECISION`. A second oracle-only elapsed-time backstop catches physical
work intentionally muted by the play engine's virtual-cache ledger; it uses
the same numeric decision limit as elapsed seconds. The backstop is a safety
classification, so timing can affect *when* a diagnostic is capped, but a
truncated audit can never report a clean result. Neither poll runs in normal
play. At the default setting, it bounds an uninterrupted breakpoint walk to
about five minutes rather than tens of minutes or hours.

Focused runs with 30, 60, 75, and 120 priced-second caps finished in about
13, 44, 58, and 85 seconds of oracle audit wall time, respectively; all
reported the 650 unaudited gate-rejected plans as `CAPPED-DECISION`.
Those runs reached the priced cap at an audit boundary, so they did not
exercise the elapsed-time backstop. The default-cap reproduction did:
seed 30034271, turn seven, main one finished in 304.6 elapsed seconds.
The audit recorded 171.951 priced seconds and 301.254 elapsed audit seconds,
marked `WALL-BP-CAP`, and classified all 650 unaudited gate-rejected plans as
`CAPPED-DECISION`. The play result was a turn-eight win. The focused evidence
is `build/scratch/oracle_cap_gods34271_wall300.{out,err}`.

This patch contains the diagnostic hang. The affected Gods decision remains
unclean; increasing the cap or optimizing its repeated derivations would be
needed to audit every plan in that decision. A fresh 5,000-game gate remains
due after the remaining mana misses are fixed.
