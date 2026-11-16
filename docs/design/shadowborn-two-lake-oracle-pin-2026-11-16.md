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

The largest residual drop, seed 30,085,008, is a payment-timing tradeoff.
Diabolic Intent is payable from Swamp plus Lake, which is how the normal
on-demand payer casts it. The oracle instead cracks Lotus Bloom during Intent
and leaves Lake for the tutored Thrumming Stone. The later Stone then pays
and starts the Apostle chain. Seed 30,087,083 similarly finds an oracle line
that taps Lake for one black while the normal payer spends ritual float,
changing whether the following Stone can be funded. Both cross an Intent
breakpoint, so the initial plan does not name the tutored card. Whether a
known later cast can justify spending a source during an earlier payable
cast is now a user question, shared with Dragonstorm's Lotus timing case.

The fresh PGO block on games 90,000–94,999 remained oracle-on/off
play-identical. It had zero `ORACLE-GAP`, `ENUM-GAP`, or `APPLY-DROP-ORDER`,
and seven `APPLY-DROP` flags in two signatures. Six are a post-Intent
continuation to Sire of Insanity: the plan cannot name the tutored line,
and the oracle cracks Lotus Bloom during the payable Intent. One is seed
30,092,453, where the *original plan* names Ritual, Culling, and Sire.
The activation reserve kept a Swamp for a later Apostle activation, so
Dark Ritual's black payment took Blood Crypt, the only red source for
Sire. Holding Blood Crypt instead lets the Swamp pay Ritual, Culling's
black float pay its own cost, and Blood Crypt pay Sire's red pip.

The activation hold now prefers a unique source of a later named cast's
colour when the current payment does not need that colour; the default-on
`MTG_ACT_HOLD_UNIQUE_LATER` flag provides a same-binary `=0` control. A focused
replay of seed 30,092,453 has zero misses, and a unit assertion fixes the
reserve choice. The other six flags remain subject to the on-demand
payment-timing question; this change does not address them.

The focused same-binary `=0` arm reproduces the old reserve choice. A
pooled paired batch compared the flag on/off on 5,000 fresh Shadowborn
games and eight regression cells (20,900 games total). Shadowborn's
5,000 win turns, play digests, and work units were byte-identical; the
focused oracle reproduction is the actual coverage gain. The exact
searched Shadowborn and Hinata regression cells were also identical.
Three Selesnya depth-zero play digests changed across 2,000 games, with
zero win-turn changes. Prevent Damage and Boars mismatches from the full
regression were identical on both arms. The full combined suite had 123
ground-truth mismatches (48 smoke, 75 regression), mostly the already
documented baseline drift; its strict viewer replay passed all 864
references, both suboptimal games stayed beaten, and the Shadowborn-only
11-job regression was fully green. No broad GT rebaseline was made.
