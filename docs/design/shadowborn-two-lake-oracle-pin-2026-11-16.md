# Shadowborn: two Lake cracks retain the early-crack pin

The fresh Shadowborn PGO mana-clean gate on indices 85,000–89,999 was
oracle-on/off play-identical and found ten issues: seven `APPLY-DROP`, two
`APPLY-DROP-ORDER`, and one `ORACLE-GAP`. The report is
`logs/mana_clean/codex-shadowborn-residual-20261116/report.md`.

Seed 30,089,424, turn seven main two, casts Abhorrent Overlord with two Lakes
of the Dead and two Swamps. The engine needs both Lakes' cracks for the
seven-mana bill. Its on-demand payer tries a single pinned crack first; when
that cannot pay, it falls through to the ordinary two-crack loop and leaves
`scripted_early_ondemand_crack` set. The oracle's two-Lake script cleared the
pin merely because it contained a Lake crack, so its board could not match
the engine even when both Swamps were sacrificed.

`OracleOptionConsumesEarlyCrackPin` now consumes the pin only when an option
has exactly one on-demand land crack and no on-demand lump. The existing
single-Lake and Lake-plus-Lotus behavior stays covered by the same unit test;
the new assertion covers two Lake cracks. The exact seed now reports zero
mana-clean issues. The full Release suite passed 3,360 cases and 3,614,325
assertions. This is an oracle-script correction; it does not change normal
payment or search play. The next fresh Shadowborn block is 90,000–94,999.

Combined Shadowborn/Shadowborn2HG smoke and regression passed all 11 jobs with
zero per-game win or play-digest changes. All 561 scenarios passed. The strict
viewer replay had zero drift, dead shuffles, board divergence, enum gaps,
mulligan drift, or contract failures across 864 references; both suboptimal
games remained strictly beaten. No ground-truth promotion was needed.
