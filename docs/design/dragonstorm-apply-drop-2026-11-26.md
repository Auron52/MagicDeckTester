# Dragonstorm APPLY-DROP: the off-colour sources (2026-11-26)

Mana-clean gate `claude-queue-20261125a` (src `773d4e440d5c`, Dragonstorm games 87000-158999, seed 30000000 + gi):
Dragonstorm had 1,137 issues, 728 of them APPLY-DROP. Branch `wip/ds-apply`. Scripts and numbers: `logs/ds-apply/`.

## The cluster

The 728 flags are 146 (seed, turn) events in 144 games. The dropped cast is almost always one of the deck's two
off-colour closers: Karrthus, Tyrant of Jund `{4}{B}{R}{G}` (77 events) and Dragonlord Kolaghan `{4}{B}{R}` (48). Every
one of the 146 events has Lotus Bloom or Unclaimed Territory on the board, and those (plus Apex of Power's ten of one
colour) are the deck's only `{B}` / `{G}` sources. 112 events are declared drops (the closer is in the plan) and 34
are "need-later" (the closer is beyond an Apex of Power breakpoint and not named by the plan).

The line-keep rung (`LineReducerKeepHold`, crit mode) cannot reach these: its gate counts only lands, rocks and ready
dorks, so a Bloom, a Territory and a floating unit are all invisible to it, and its exact judge (the complete
backtracker) does not model an on-demand Bloom crack at all. The drops come from two of the on-demand payer's own
rules instead.

1. **The visible-closer lump rule cracked a Bloom early on a comparison that reads an uncracked Bloom as no supply.**
   `MTG_ONDEMAND_VISIBLE_LUMP` (Dragonstorm only) cracks a lump for a colour a later cast owes when the early crack's
   board has more of some other owed colour than the ordinary payment's. It subtracts the lump's own wild supply from
   the ordinary board (an uncracked Bloom "cannot replace two distinct Territory colours"), so trading a Bloom for one
   extra `{R}` reads as a gain. s30152662 T5 `{Seething Song, Irencrag Feat, Karrthus}`: `{B}` and seven `{R}` float
   (the first Bloom was cracked for Song); Irencrag's `{1}{R}{R}{R}` is paid by the float alone, but the rule cracked
   the second Bloom for `{B}` (the generic then ate a `{B}` and one more `{R}` stayed), and Karrthus's `{G}` had no
   source.
2. **The leftover swap was too narrow.** `MTG_LEFTOVER_LINE_SWAP` re-attributes a generic pip the float drain paid
   with a colour the line still owes to a leftover unit of another colour (same taps, same float total). It moved only
   units the payment PRODUCED net of the float, and judged the rest without the float of the ritual being paid for.
   s30087280 T4 `{Desperate Ritual, Scourge of Valkas, Irencrag Feat, Dragonlord Kolaghan}`: `{B}{R}{R}` floating;
   Irencrag drained the `{R}{R}` into its red pips and the `{B}` into its `{1}`, then tapped Sandstone Needle for
   `{R}{R}` and floated one. That `{R}` was not "produced" (the float's `{R}` went 2 -> 1), and without Irencrag's seven
   `{R}` Kolaghan's `{R}` read short too, so the swap was refused and Kolaghan lost its `{B}`.

## The change (two levers, DEFAULT ON, each `=0` byte-identical)

- `MTG_ONDEMAND_VISIBLE_LUMP_EXACT`: the early crack is made only when the ordinary payment leaves the later bill
  (the named rest's pips, a visible closer's pips past an opener, the rest's generic) unpayable and the early crack's
  board pays it, both judged exactly: `LumpAwareBillPayable` tries every crack of the lumps left (uncracked, or cracked
  for each colour the bill has a pip of) with the complete backtracker, plus the float of the ritual this payment is
  for. When neither board pays it the comparison stands as before.
- `MTG_LEFTOVER_SWAP_WIDE`: the swap may move any leftover unit (one that floated before the payment as well as one it
  produced: both were in the pool it spent from, CR 601.2h), and its strand test credits the ritual's float.

Both are mana-payment choices (the standing exemption), in the shared payer: executor, rollout and the payability
walks move together, at every budget. The USER's ruling for this deck (*"spend the counters if they are required for
the line"*) is not engaged: neither lever spends a counter the greedy would not.

Unit tests: `test/unit/test_dragonstorm_line_keep.cpp` (both repro boards, lever on and off, plus a no-strand control).

## Measurements

(filled in below as they land)
