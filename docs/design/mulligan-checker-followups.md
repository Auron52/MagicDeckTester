# Mulligan checker: open follow-ups

Status: **open, awaiting the user's decisions** (2026-10-16). The checker itself is built:
`tools/mulligan/` (page + `keepcore.js`, see its README) and its gate `test/mulligan_app_check.js`
(28 decks exact, Anti-Lifegain WARN, reproduced only through the shipped-table fallback).

Building it turned up three artifact problems. They are written up here with the options measured
so far.

## 1. Gate: a keep table may not be adopted without the raw sidecar that produced it

**Problem.** Anti-Lifegain's table was deepened from 3 to 6 mulligans on 2026-07-14 (`ec297a5b`) by
an extend-depth run (`MTG_KEEP_PRIOR_RAW` from the committed 3-level raw sidecar). The run's own raw
output, with the 3-, 2- and 1-card tables and their game counts, was never committed. Its
`logs/Anti-Lifegain_gen/` folder no longer exists, and no branch, stash or worktree holds it. Play is
unaffected: the engine plays the committed 6-level table. What is lost is every statistic for 4–6
mulligans, plus anything a later pooling or extension run would have reused.

**Proposal.** Add `node test/mulligan_app_check.js <Deck>` to `scripts/mullgen.sh`'s artifact check,
so adoption fails unless the committed raw sidecar reproduces the compiled table (exit 0 with no
WARN). It takes seconds on most decks and about a minute on FiveColour. This is the same lesson as
"a mandated step with no gate doesn't happen": the commit message recorded which raw it shipped, and
nothing checked that it was the right one.

## 2. Bottoming rule not recorded in older tables

**Problem.** Two build-time choices change the bottom argmin:
- the adaptive floor (only cells with `count > sub_target` may win),
- the shrinkage refinement (`MTG_KEEP_BOTTOM_SHRINK`, used for FiveColour).

Raw sidecars generated before the `sub_target` stamp carry neither. Twelve are in that state:
Anti-Lifegain, Auras, Creature Giving, Dragonstorm, FiveColour, Goblins, Hinata2, Knights, Minotaur,
burn, slivers_vial, treasure_hunt. Nor does the compiled table record shrinkage anywhere. The counts
cannot settle it: Creature Giving and Goblins have the same 2/18/30 adaptive shape, but Creature
Giving shipped with no floor and Goblins with floor 2. The page works it out by checking a prefix of
the compiled table.

**Measured impact on play: none.** For every KEPT hand both rules choose the same bottom (Creature
Giving 5.3 M, Goblins 2.7 M, Snow 0.4 M kept-hand decisions; 0 differ). The generator's refine loop
always samples a keepable hand's best bottom fully. The rule only changes the bottom target of hands
the table mulligans. Those matter only for the page's "if you keep anyway" advice and for the play
viewer's bottom hint when a human keeps against the table.

**Options.**
- **A.** Nothing more. The page's calibration settles it whenever the compiled table is present.
- **B.** Have the generator and `RunKeepMerge` stamp `bottom_floor` and `bottom_shrink` into the
  raw-sidecar meta and the compiled `exhaustive_keep` block. This changes only the analyzer, not play.
  It also closes an engine-side gap: today a merge cannot rebuild a shipped table from its raw unless
  the operator passes `MTG_MERGE_BOTTOM_FLOOR` / `MTG_KEEP_BOTTOM_SHRINK` by hand, a fact the code
  comment beside `MTG_MERGE_BOTTOM_FLOOR` acknowledges.
- **C.** Backfill the stamp into the twelve existing raws (metadata only, verified by the gate). This
  edits committed artifacts, so it needs the user's approval.

Recommended: A + B; C optional.

## 3. Anti-Lifegain's missing deeper levels

The page's shipped-table fallback (in `keepcore.js`: `useShippedTable`, `recoverTerminal`) reproduces
all six levels exactly. It recovers `Dopt[4]` from the table's own keep flags: 5.5125 ±0.0125 turns on
the draw and 6.2345 ±0.0094 on the play, and those values reproduce every shipped call at 0–3 mulligans.
Statistics for 4–6 mulligans are gone. Options if they are wanted back:
- **Extend-depth regeneration** from the committed 3-level raw on today's engine. The play digest has
  changed since July, so every cell is re-sampled thinly and the moved ones refined. The result is a
  new table; adopting it is a play change (held-out A/B, GT accept). Size it with a feasibility probe
  first, and run it alone on the box per the generation rules.
- **Full regeneration** via `scripts/mullgen.sh run decks/Anti-Lifegain`. The original took about
  26.5 M games.
- **Check the secondary machine** for a surviving `logs/Anti-Lifegain_gen/` raw from July.

Since play is unaffected, neither regeneration is urgent. Fold it into the table's next regeneration.
