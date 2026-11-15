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

The fresh PGO gate `codex-gods-post-surplus-20261115` completed all 5,000
games on seeds 30000–34999, with a paired oracle-off arm on the same games.
All 157 paired chunk play digests were byte-identical. The ten earlier
`ORACLE-GAP` games are absent: the full run reports zero `ORACLE-GAP` flags.
It is not a clean gate. Thirteen distinct games still have issues: two with
`APPLY-DROP`, six with `CAPPED`, four with `CAPPED-DECISION`, and one with
`UNMODELED`. The exact event counts are 3, 46, 4,178, and 1 respectively.
The full report is `logs/mana_clean/codex-gods-post-surplus-20261115/report.md`.

The three remaining `APPLY-DROP` events occur in two games. In both, a cast
spends the last source of a color needed by a later cast: Study consumes the
only red-capable Reflecting Pool before Faithless Looting, or flashback
Mystic Retrieval and Study consume the sources needed by Looting. The existing
`MTG_SCARCE_COLOR_MASK=1` payer lever clears all three events in focused
oracle replays. A 1,000-pair Gods play trial changed only two games; one
changed from a turn-seven win to no win by turn eight, while the other still
won on turn eight. The slower game persists at depth eight with unlimited
budget: the mask arm waits on turn five instead of casting Study, Looting,
and activating Coliseum. The mask therefore remains default-off pending a
narrower fix. Trial data are under `build/scratch/gods_scarce_mask_trial/`.

The one `UNMODELED` game (seed 30034901) was an alternate cast order whose
hit-only payment search stopped at the initial 2,000-apply cap. A focused
replay with a 20,000-apply initial cap completed with zero issue flags. The
oracle now retries only a capped sibling order under the existing 20,000-apply
escalation ceiling. At default settings the patched replay finished all 21
capped searches and reported zero `UNMODELED` or other issue flags, in 333,053
applies. The PGO control with the raised initial cap took 455,340 applies;
their measured times are not compared across builds. This audit-only change still
needs a fresh full gate; focused logs are
`build/scratch/gods_alt_order_{cap,retry}_34901.*`.

The six `CAPPED` games were separately replayed on one fresh PGO binary with
the ordinary 20,000-apply retry ceiling and a 100,000-apply ceiling. The 20,000
arm reproduced all 46 `CAPPED` flags in 1,940,377 oracle applies and 65,488
measured oracle ms. The 100,000 arm concluded every plan: zero issue flags,
2,675,651 applies, and 93,889 oracle ms. All six per-game `.wins` files were
byte-identical. The retry ceiling is now 100,000 by default, reached only when
the initial 2,000-apply pass caps; this is an audit-only change. The paired
replay data are `build/scratch/gods_caps{20k,100k}/`. A fresh full gate is still
owed, including the four earlier `CAPPED-DECISION` games.
