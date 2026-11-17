# Dragonstorm: Lotus Bloom for a visible later cast

The Dragonstorm mana-clean block on indices 57,000–61,999 found a legal
Karrthus line in seed 30,060,428 that the real payer drops. Its first
Seething Song is payable from Mountain and Sandstone Needle, so the
on-demand rule leaves Lotus Bloom intact. The oracle instead cracks Bloom
for black during that Song, spends two black units on Song's generic cost,
and carries one black unit through the following red spells to Karrthus.
The earlier crack keeps a red source untapped. The user clarified that
on-demand may include a visible later cast's need.

`MTG_ONDEMAND_VISIBLE_LUMP` (default on; `=0` control) applies to Dragonstorm
and compares those two payments when the current bill has a generic
component and a continuation needs a color that the non-lump board cannot
supply. A cast already named in the line provides that need directly; when
the line opens an Apex breakpoint, the sole visible creature in hand can
provide the later off-color need. Multiple visible creatures are competing
continuations whose color choice must wait until the search chooses one. It
accepts the early lump crack only if the current bill pays, the needed
later colored pips remain in float, and the candidate leaves more supply
for a different color the same continuation owes. Lake of the Dead
payments continue through their existing hold and crack logic. A focused
unit test uses Seething Song followed by a Karrthus-shaped bill.

The exact seed 30,060,428 turn-six replay now has no payer drop or oracle gap;
the flag-off control has twelve APPLY-DROP reports. The first broad trial
affected Shadowborn Apostle's depth-zero win and made Dragonstorm regression
seed 3,288 slower even at depth eight. The narrowed rule leaves Shadowborn
unchanged and restores seed 3,288's turn-five win. Focused tests cover the
named line, the single visible creature with a shared wild Territory, and
two competing visible creatures.

In a fresh 5,000-game paired Dragonstorm block (seeds 9,600,001–9,605,000),
the narrowed and flag-off arms both average 4.3642 loss-penalized turns. One
game wins a turn earlier, one game loses a turn-eight win, and twelve other
play digests change without a turn change. The slower game recovers its
turn-eight win at depth eight with no time budget on both arms; its turn-two
divergence occurs while Bloom is still in hand. The earlier broad trial used
10,329,518 versus 10,331,630 off search units, while its parallel job wall
time was 553,690 versus 539,363 ms, possibly including scheduling noise.

The full Release unit suite passes 3,374 cases and 3,614,831 assertions.
Shadowborn's ten smoke/regression cells are byte-identical to saved ground
truth. Dragonstorm's narrowed deck check has one searched game one turn
slower (seed 2,173), which recovers at depth eight with no time budget; one
other searched play digest changes at the same win turn. Its smoke depth-zero
cell improves one turn-game and its regression depth-zero cell slows one.
The strict viewer check passes 864 references with no drift or divergence;
both suboptimal references remain beaten. The fresh PGO mana-clean gate on
indices 62,000–66,999 reports 93 issues: 86 APPLY-DROP, four
APPLY-DROP-ORDER, one ENUM-GAP, and two ORACLE-GAP. These are new seeds, so
their count is not an A/B delta against the earlier 63-issue block. The
remaining signatures and repro commands are in
`logs/mana_clean/codex-dragonstorm-visible-lump-20261117/report.md`.

## 2026-11-17: compare the sources after paying Song

Seed 30,074,224 turn five has Mountain, two Unclaimed Territories, and
Lotus Bloom. The visible line is Seething Song into Karrthus. Ordinary
payment taps both Territories for Song's generic bill, so Song's five red
cannot pay Karrthus's black and green pips. Cracking Bloom during Song
leaves both Territories available. The original `short_with_shared` guard
sees both pips covered *before* paying Song and never tries the crack.
Simply removing that guard is insufficient: `AvailableManaPool` counts
the uncracked Bloom as three wild units, masking the two wild sources
saved by the early crack in the after-payment comparison.

`MTG_ONDEMAND_VISIBLE_AFTERPAY_WILD` is default on. The payer now tries
each visible later pip, compares coloured plus **non-lump wild** supply
after the ordinary and early-crack payments, and accepts the crack only
when a different owed colour gains supply while the chosen colour's pip
remains floated. `=0` restores the old prepayment check and old coloured
comparison for a same-binary A/B. The focused test fails on the old arm,
passes on the new arm, and verifies Karrthus can be paid after Song
resolves. The complete Release unit suite passed 3,384/3,384. Exact seed
30,074,224 changes from one APPLY-DROP on the old arm to zero oracle
issues on the new arm.

On 5,000 paired held-out Dragonstorm games (indices 77,000–81,999,
PGO+LTO binary, same seeds), no win turn moved; four play digests changed
at the same win turn. Search work was 9,985,030 units new versus
9,976,007 old (+0.09%). The fresh 5,000-game mana-clean gate on these
indices is running; its verdict belongs in
`logs/mana_clean/codex-dragonstorm-afterpay-wild-20261117/report.md`.
