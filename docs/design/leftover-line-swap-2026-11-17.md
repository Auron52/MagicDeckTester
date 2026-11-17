# Leftover swap: a payment keeps the floating colour the line owes, not a spare over-produced unit (2026-11-17)

Lever: `MTG_LEFTOVER_LINE_SWAP` (heurarm slot `LEFTOVER_LINE_SWAP`), DEFAULT ON; `=0` restores the old leftover
byte-for-byte.

## The defect

Dragonstorm mana-clean gate `codex-dragonstorm-ruby-song-20261117`, seed `30072155` T5, plan
`{Ruby Medallion, Seething Song, Karrthus, Tyrant of Jund}`: APPLY-DROP x2. The board is:
- two Sandstone Needles;
- Unclaimed Territory;
- Scourge of Valkas;
- Lotus Bloom.

1. Ruby's `{2}` cracks Bloom for `{B}` and floats one `{B}`.
2. Seething Song's `{1}{R}` (Ruby out): the payer drains the floating reserve first, so the `{B}` pays the `{1}`.
   It then taps a Needle for `{R}{R}`, and the second `{R}` floats.
3. Karrthus (`{3}{B}{R}{G}` after Ruby) has the Territory for one of `{B}` and `{G}`, and nothing for the other.

The oracle pays Song with the Needle's `{R}{R}` and keeps the `{B}` floating. The Needle is tapped in both
payments. They differ only in which unit paid the generic pip.

Seed `30074081` T4 is the same defect with two colours,
`{Desperate Ritual, Karrthus, Scourge of Valkas}` (APPLY-DROP x1):
- Desperate Ritual floats `{R}{R}{R}`.
- Karrthus's `{3}{B}{R}{G}` drains that red into its generic and cracks both Lotus Blooms.
- The leftover is Bloom `{B}`/`{G}`, so Scourge's `{1}{R}{R}{R}` has one Mountain for three red pips.

## The change

`SwapLeftoverForLine` runs after every successful outermost payment in `TapForCostShared`, which the executor,
the rollout and the human's automatic payment all pay through, so the worlds stay in lockstep. Which unit pays a
generic pip is the controller's choice inside one payment (CR 601.2h). A swap therefore changes no tap and no
float total: it re-attributes `k` generic pips from a drained floating colour X to units of a colour Y that this
payment produced and left floating.

It swaps only when all of these hold:
- the applying line's hold contains this cost and still owes coloured pips after it (`LineRestAfter`);
- the rest of the line is provably short as left: its coloured pips exceed the board's units of those colours
  plus its wild units, summed over colours (`AvailableManaPool`: the float plus every untapped source);
- it is not short after the swap.

So it only ever replaces a leftover that strands the line with one that does not.

Several produced colours may stand in for one drained colour together (s30074081: both Blooms' `{B}` and `{G}` for
the ritual's `{R}{R}`). The swaps accumulate on a trial copy and are committed at the first point the rest is no
longer short.

Scope and exclusions:
- **General float, WUBRG units only.** The creature-only and restricted reserves keep their provenance.
- **Never for a line that opens a breakpoint (`g_line_opens_unseen`).** Such a line owes more than its hold names,
  and the board read cannot see the opener's own mana. A first version swapped back the `{B}` that the
  visible-closer lump rule cracked Lotus Bloom for at Apex of Power (s30073082, `MTG_ONDEMAND_VISIBLE_CONTRIB`),
  to keep `{R}` that Apex's ten `{R}` would have covered anyway.
- **Never for nested or refine payments, the oracle's scripted copy, or human play.**

## Measurement

Exact seeds: s30072155 APPLY-DROP 2 -> 0 (lever off: 2), s30074081 1 -> 0 (lever off: 1). s30073082 stays clean.

Paired all-deck A/B, one pooled Release batch, both arms per job via the heurarm flag. Dragonstorm 5,000 pairs,
every other suite deck 1,000, games 550,000+ (`build/scratch/claude_leftover_swap_ab/`):

| deck | earlier | later | digest-only | work units |
|---|---|---|---|---|
| all 56 suite decks (Dragonstorm 5,000; each other 1,000) | 0 | 0 | 0 | +0.00% (identical) |

Every game in all 56 decks is byte-identical, work units included. The shape (a line hold that is provably short
as left, with a drained float and an over-produced colour) never decided a game in this sample. It is adopted as a
payment correctness fix: the oracle's line is legal and keeps the line's casts. Unit suite 3,396/3,396.

Tests: two `test_dragonstorm_followups.cpp` cases, both covering both arms:
- "Leftover swap: Seething Song keeps the floating {B} Karrthus owes" (s30072155): with the lever off, Karrthus
  does not pay.
- "Leftover swap: Karrthus leaves the ritual's {R}{R} for Scourge" (s30074081): with the lever off, Scourge does not
  pay.
