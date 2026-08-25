# The three measurement modes (USER, 2026-08-25)

**Status: the USER's stated framing, recorded so measurements can be labelled by mode and so
"which mode was this measured in?" is always an answerable question.** Not a code change; a
convention. It supersedes the loose habit of calling everything "play settings".

> *"Realistically we will end up with three modes: Mulligan profile generation (typically low
> depth and budget or value-leaf trust), Play settings (for normal cases and most comparisons)
> and high quality settings (maybe something like budget = 10000 and depth = 7? The budget would
> just be to cut short degenerate games) which are for more reliably differentiating very close
> cases and cases where I'm concerned about budget truncation."*

| mode | settings | what it is for |
|---|---|---|
| **1. Mulligan profile generation** | low depth + low budget, or value-leaf trust | the keep/bottom tables. Volume is the point: the table needs many hands, so per-game fidelity is traded for count. See `mulligan-profile.md` / `value-leaf.md`. |
| **2. Play settings** | the deck's `value_play` depth + shipped budget | the default for normal cases and MOST comparisons. This is what `test/regression_cases.sh` d5 cells and the adoption bar mean by "at play settings". |
| **3. High quality** | e.g. depth 7, budget ~10000 | reliably differentiating VERY CLOSE cases, and any case where budget truncation is the worry. **The budget here is not a search allowance -- it exists only to cut short degenerate games.** |

## Why mode 3 is not just "mode 2 with bigger numbers"

Its purpose is different, and that changes what counts as a defect.

* **It is the mode that exposes REACHABILITY bugs.** A line that no depth or budget can reach is
  invisible in modes 1 and 2 -- they simply never look far enough for the gap to show. Mode 3 is
  where it surfaces, which is why a completeness fix earns adoption on the infinite-budget test
  even when it measures 0/0 at play settings. Frequency-at-play-settings is the wrong axis for
  that class (USER: *"it is possible which is reason enough to support it"*).
* **It is the mode deck screening leans on for close calls.** Comparing two cards that rarely
  diverge means the verdict is decided by the handful of games where they DO -- exactly the games
  a truncation hides. A rare truncation is therefore not a rare problem here; it is a bias
  concentrated on the only games that matter. See `.claude/skills/deck-screening.md`.
* **The gate-2 procedure is already mode 3 in disguise.** "Re-run the deleted wins at depth = the
  target win turn with a very large budget" is exactly this mode, applied to one question. Naming
  the mode makes that procedure an instance of a general setting rather than a one-off ritual.

## Practical rules

1. **Label every measurement with its mode.** "63 games faster" means nothing without it -- the
   equip-ETB breakpoint lever read +79% cost at the d3 gate cells and 1.121x at play settings, and
   the condemnation opt-in was adopted on a saving that only existed at d3.
2. **Gate cells are not a mode.** The suite's d0/d3/d5 cells are a REGRESSION TRIPWIRE (cheap,
   disjoint seeds, fixed budget). d0 in particular is a determinism/behaviour check, not a
   quality measurement -- a lever that cannot bind without lookahead reads as inert there for
   structural reasons (see the `MTG_METALCRAFT_CREDIT` d0 result in `analysis-KittyEquipment.md`).
3. **Do not use mode 1 numbers to judge play quality.** Value-leaf trust and low depth are chosen
   for throughput; a quality delta measured there is measuring the apparatus.
4. **A mode-3 run is expensive.** Budget it like a generation job: pooled into ONE
   `mtg --batch`, never a loop of per-item invocations (CLAUDE.md).

## Open: mode 3 is not yet a first-class setting

Today a mode-3 run is assembled by hand (`--depth 7 --budget-ms 10000`, or by omitting `--depth`
and overriding the budget where a `value_play` profile is attached -- note the engine REFUSES
`--depth` on a deck whose profile sets a target depth, so the two are not interchangeable). Worth
considering, and NOT yet decided:

* a named mode in the harness (a fourth tier, or a `--quality` flag) so mode-3 numbers are
  reproducible and comparable across sessions rather than re-derived per investigation;
* whether the value leaf should be OFF in mode 3 -- it is a learned O(1) stand-in for the horizon
  rollout, so trusting it in the mode whose purpose is horizon-honesty deserves a decision rather
  than an accident (`MTG_LEAF_*`, `value-leaf.md`);
* how mode 3 interacts with the per-deck `budget_ms` a profile already carries.
