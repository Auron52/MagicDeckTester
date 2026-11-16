# Dragonstorm: preserve Lotus black through rituals

The pooled mana-clean gate `codex-mana-queue-20261115` found 147
`APPLY-DROP` flags in Dragonstorm. One recurring line at seed 30,042,142
casts Desperate Ritual, Seething Song twice, then Dragonlord Kolaghan.
The legal payment cracks Lotus Bloom for black and pays the first red pip
with Mountain. The engine cracked Bloom for red and stranded Kolaghan's
black. A first ordering trial ranked later color shortages ahead of the
current pip, but it still favored red because the declared line owed more
red pips. A second trial prioritizes a later color with no ordinary source;
that chose Lotus black.

The black choice alone still dropped Kolaghan: Seething Song's generic
payment spent the last floating black unit before its five red mana arrived.
The existing surplus-first float drain treats the later red pips as an
immediate demand. Once its surplus pass is exhausted, its fallback kept the
old color order and consumed the black. The trial's second lever orders
that forced-spend pass by the remaining float amount, spending red before
the sole black unit. It changes only the generic assignment; every cast
still pays its printed cost.

With both `MTG_LUMP_LATER_SHORT_FIRST=1` and
`MTG_LINE_DEMAND_FALLBACK_ABUNDANT=1`, the focused seed's four
`APPLY-DROP` flags became zero and the oracle reports no issue. A four-seed
replay still has 14 drops on an Unclaimed Territory line and three
`ENUM-GAP` flags from cast-order/gate cases; those are separate work.

A fresh 1,000-game Dragonstorm pilot (seeds 9,400,001–9,401,000) was
play-identical. A second 5,000-game pair (seeds 9,401,001–9,406,000)
won 4,952 games in each arm, with three games one turn faster, none slower,
and averages 4.3722 off versus 4.3716 on. Search units were 9,341,429
off versus 9,338,850 on. Ten mana-sensitive decks on 50 paired games each
had identical play and win turns. The current Release unit suite passed
3,353 cases and 3,614,055 assertions.

The combined 565-row smoke/regression harness, with both switches on, found
71 ground-truth mismatches, the same broad drift reported before these
switches. A strict viewer run found zero play drift and zero enum gaps across
864 references; both required-beat games passed. Of 555 rows shared with
the previous pooled run, 550 results were identical. The other five and
the ten new Red Elementals rows were rerun with both switches off on the
same Release binary. Fourteen of those 15 matched exactly. The sole change
was Dragonstorm's depth-zero smoke row: 4.9620 off to 4.9610 on. No
ground truth was accepted for the unrelated baseline drift.

The fresh PGO mana-clean gate `codex-dragonstorm-lotus-20261116` completed
on indices 47,000–51,999. Oracle-on/off play was byte-identical. It found
287 issues: 3 `ENUM-GAP`, 126 `APPLY-DROP`, 121 `APPLY-DROP-ORDER`, and
37 `ORACLE-GAP`; there were no unmodeled or capped decisions. Of the 126
payment drops, 64 drop Kolaghan, 54 Karrthus, and 8 Scourge of Valkas.
Many legal payments use Unclaimed Territory's creature-only black or
green after an earlier ritual. Dragonstorm remains beta; its next fresh
block is 52,000–56,999. The exact report is
`logs/mana_clean/codex-dragonstorm-lotus-20261116/report.md`.

Raw paired data: `build/scratch/dragonstorm_lump_*.{out,err}`.
