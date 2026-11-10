# Last week's decks: regression ground truth audit (2026-11-10)

This records the baseline update after the mana and search fixes for Kithkin,
Merfolk, Jumbo Cactuar, Shadowborn Apostle, Extra Turns, and Modern RW Burn.
The full smoke and regression suites were run against the Jumbo payment fix
(`21c2fa3f65e9` source tree). Their 183 smoke and 291 regression job digests
matched the preceding full runs exactly. The resulting per-game logs for all
decks except Dragonstorm were accepted with the harness's filtered `--accept`.
The `--deck` filter was used because a Dragonstorm rollout fix was still under
review; it promoted only the audited rows and their corresponding `.wins` logs.

In that full regression run, nine searched games slowed, all in Shadowborn.
The 4x and 16x replay classified all nine as search-budget churn. Twenty
searched games improved. Every same-score searched play change was reviewed
with `test/explain_game.py`, and no illegal action or reference drift was found.
The full smoke run had four searched slowdowns, nineteen improvements, and
forty-seven same-score searched play changes; those lines were also reviewed.
The depth-zero changes were recorded as a lighter sanity check, including the
Shadowborn and Landfall improvements. The weighted searched score improved
overall in both tiers.

The Dragonstorm rollout fix (`c783ff4c9`) was then built in Release and tested
against only the Dragonstorm and Dragonstorm2HG rows. The focused gi31 replay
changed the searched win from turn 5 to turn 4. The patched smoke audit found
zero slower and six faster searched games; regression found zero slower and ten
faster searched games. Four smoke and seventeen regression same-score searched
play changes were inspected; they kept the same win turn with legal lines.
Depth-zero had two slower and nineteen faster games in smoke, and nine slower
and fifteen faster games in regression. The patched Dragonstorm rows and their
`.wins` logs were then accepted with filtered `--accept`.

The final Release unit suite passed 2,682/2,682 cases and 3,579,286 assertions.
The reference safety scan passed across 195 checkouts/worktrees before push.
The strict viewer reference replay passed all 787 references: 312 exact,
474 repaired, one beaten suboptimal reference, and zero drift, divergence,
enum-gap, mulligan drift, or contract failures.

The accepted ground truth is a measurement baseline for these exact changes;
it does not imply that the Jumbo, Shadowborn, or Extra Turns mana-clean queues
are complete. Their remaining oracle findings are tracked in the mana-clean
reports and deck-specific design notes.
