# Engine-wide: the first-decision root skips a free land drop on a depth-1 tail (DEFERRED)

Status: deferred (USER 2026-11-03: "That sounds like a reasonable deferred idea."). Found while root-causing the
Shadowborn Apostle INT-8 held-out result; the deck-local fix (a provider prune) is in that deck's analysis.

## The defect
On the early turns at the shipped budgets (d3 b10, d5 b20) the first-decision (FD) root ranks its candidate plans on a
depth-1 search of the next turn plus the greedy horizon playout. Among the candidates is the "(defer)" plan, i.e. no
land this main phase (`add_for_land("", "")` in `EnumeratePlansWithLandUncached`, TurnSolver.cpp). Two routes commit it:

1. **FD line.** The defer plan (alone or with casts) has the strictly lowest playout tail, and its unverified first
   phase is committed. Main 2 cannot recover the drop (`HeldLandBarredFromMain2`), so the turn loses its land.
2. **Fallback lookahead.** Every candidate is win=9 (no win inside the horizon); the graded no-win leaf (opponent life
   at the horizon) happens to prefer the pass.

It is NOT budget exhaustion (every candidate has a tail), NOT a tie (defer plans are scanned last and win only on a
strictly lower tail), and NOT a provider restriction. The tails it wins on are noise: on Shadowborn they read T7-T9
when the real games win on T5.

Measured on Shadowborn (200 held-out games, d3 b10, seed 7001): about 19 games / 22-23 passes skip an available
basic land drop on T1-T4; those games average ~6.1-6.2 against ~5.44 overall. 17-19 of the passes are route 1.

## Why it is probably not Shadowborn-only
Nothing in the mechanism is deck-specific: any deck whose early root decisions rest on a depth-1 tail can commit a
dominated no-land plan. Decks where holding a land is genuinely right (landfall timing, Karoo bounce, a hand-size
payoff) are exactly the ones where the defer plan must stay, so a fix cannot be a
blanket "always play a land".

## Candidate fixes (to measure, not adopt blind)
- **A. Dominance at the FD root:** a defer plan never outranks its land-first twin (same casts, plus the free land)
  unless the deck has a reason to hold lands. "Reason" read from card data: landfall / land-count-in-hand / bounce
  lands / discard-matters payoffs. Sound where the precondition holds.
- **B. Treat "searched_depth = 1, unverified" like win=9:** fall back to the lookahead instead of committing an
  unverified FD line. Changes every deck's play.
- Either way, also look at the fallback's no-win tie-break (opponent life at the horizon), which rewards the pass.

## Measurement plan
1. Count the no-land passes per deck on the regression seeds (a counter like Shadowborn's: a basic land in hand at main
   1, the drop unused, not the win turn; exclude lands whose play costs something, e.g. Lake of the Dead).
2. Prototype A (and B if A is not enough), held-out A/B on the decks that move, per the CLAUDE.md adoption bar (every
   slower game recovers at `--depth 8 --budget-ms 0`; every deck's aggregate net <= 0).
3. The adoption is the USER's call.
