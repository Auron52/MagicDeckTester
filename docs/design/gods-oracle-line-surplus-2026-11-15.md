# Oracle prepayment across a named line

Gods' preliminary 2026-11-15 mana-clean block had ten games with
`ORACLE-GAP` on Careful Study or Faithless Looting followed by Magus of the
Bazaar. The engine tapped ordinary one-mana lands before the first one-mana
spell, leaving up to two mana floating for Magus. The oracle stopped its tap
search at the first paying set and offered extra taps only when that set
included a multi-unit source. It could not reproduce the engine's own board;
the nearest option usually tapped the painful Cephalid Coliseum instead.

The oracle's payment table now extends a successful one-mana first payment by
up to two further ordinary one-mana taps, bounded by the later mana costs
named in the plan. This is audit-only and default-on; `MTG_ORACLE_LINE_SURPLUS=0`
restores the old table. The normal payer and play search are untouched. The
oracle still verifies each expanded set by actually paying on a scratch board,
and the existing payment and decision caps disclose incomplete audits.

On the ten flagged games, all 30 `ORACLE-GAP` events disappeared. Oracle
applies rose from 490,644 to 707,988 (+44.3%); oracle measured time rose from
18,396 to 30,501 ms (+65.8%) on this deliberately difficult sample. A separate
100-game sample (seeds 30035000–30035099) had zero gaps in either arm, 289,121
to 322,289 applies (+11.5%), and 55,405 to 57,277 measured oracle ms (+3.4%).
All 110 paired play digests and game work units were identical. The ten-game
data are `build/scratch/gods_gap_ten_{base,narrow}.*`; the 100-game data are
`build/scratch/gods_surplus_probe100_{base,narrow}.*`.

This closes the observed oracle coverage cluster, not the Gods gate. The
preliminary block also had apply drops, caps, and an unmodeled plan; a fresh
5,000-game PGO gate is still due after source validation.
