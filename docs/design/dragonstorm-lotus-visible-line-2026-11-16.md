# Dragonstorm: Lotus Bloom for a visible later cast

The Dragonstorm mana-clean block on indices 57,000–61,999 found a legal
Karrthus line in seed 30,060,428 that the real payer drops. Its first
Seething Song is payable from Mountain and Sandstone Needle, so the
on-demand rule leaves Lotus Bloom intact. The oracle instead cracks Bloom
for black during that Song, spends two black units on Song's generic cost,
and carries one black unit through the following red spells to Karrthus.
The earlier crack keeps a red source untapped. The user clarified that
on-demand may include a visible later cast's need.

`MTG_ONDEMAND_VISIBLE_LUMP` (default on; `=0` control) compares those
two payments when the current bill has a generic component and a named
continuation needs a color that the non-lump board cannot supply. It
accepts the early lump crack only if the current bill pays, the named
later colored pips remain in float, and the candidate leaves more supply
for a different color the same continuation owes. Lake of the Dead
payments continue through their existing hold and crack logic. A focused
unit test uses Seething Song followed by a Karrthus-shaped bill.

Validation pending: exact seed replay, paired outcome and cost audit,
Release unit suite, regression and viewer, then a fresh PGO mana-clean
gate. The trial lives in a separate worktree while the Spirits gate runs.
