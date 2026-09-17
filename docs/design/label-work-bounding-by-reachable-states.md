# Bounding the labeller's expensive cases by reachable STATES, not enumerated plans

**Status: DEFERRED (parked 2026-09-17 for budget). Direction set by the user; nothing built.**
Companion to `label-goff-tractability.md` (the measurements) and `bound-qualified-nowin-memo.md`
(the state-keyed refutation memo the ladder already has).

## 1. What the user asked for (2026-09-17, verbatim gist)

> What we really want is to figure out expensive cases and bound the work done by them. More of a
> "which states can be reached" type of thing rather than a full list of plans. We also want any
> combo-off cases to be detected and ideally even failed combo cases to be bounded.

And, on the bounded label (`MTG_LABEL_HORIZON`, tractability section 6): *"I doubt we want to
restrict it this way."* So the horizon cut is a diagnostic, not an adoption candidate, and the
labeller stays what it is today: an **exhaustive search to the turn-8 cap** (depth is
`max_turns - turn + 1`, i.e. "to turn 8"), one work budget of 1,000,000 virtual ms per sample with
the overrun guard armed (`MTG_VALUE_LABEL_BUDGET_MS`, 0 = unlimited), and the ladder paying only up
to the label's turn. Quality first: a label may not move later to buy time.

## 2. Where the work goes today

Each ladder pass is an exact proof at its cut turn, and its cost is proportional to the number of
**plans** (lines) enumerated, deduplicated only by state key within a pass (`MTG_LABEL_LADDER_DEDUP`)
and by the bound-qualified no-win memo across candidates. The long tail (section 6 of the
tractability doc) is samples with LATE or ABSENT wins: they pay every pass to turn 8, and the deep
passes of a combo deck enumerate every ordering of a loop that either closes (and then any
ordering wins -- a full list is waste) or cannot close (and then every ordering fails -- a full
list is waste again). The per-pass instrument (`[ladder] t<turn> cands=N result=R | dd0=..ms/applies`)
shows this directly: seed 900193's third sample spent 346 s and 2.2 M applies on pass 4 alone.

What already reasons over STATES rather than plans, and is therefore the seed of the design:

| Piece | Where | What it is |
|---|---|---|
| Winless certificate | `ProvenWinlessThisTurn`, `src/ai/DecisionProviders.cpp` | a monotone reachability fixpoint over resources (mana bound, unbounded-loop detection, dig/untap events, outlets); certifies a turn winless with no enumeration. Admissible: may only over-credit. Two under-credits fixed 2026-09-16 (tractability 6b); before that it over-labelled 19 of 1,067 reference samples. |
| Stuck-turn state closure | `MTG_WINLESS_DEVELOP` | infinite-mana turns bucketed go-off / stuck by proof; stuck turns close over states (`BuildDedupKey`), one leaf per state -- the user's doctrine. |
| Combo route (play side) | `Action::Kind::ComboRoute`, x999 = verified win this turn, x1 = develop | a positive go-off certificate the play search already trusts. |
| No-win memo | `bound-qualified-nowin-memo.md` | state-keyed refutations shared across candidates. |
| Dominance prune | `MTG_LABEL_GOFF_DOM`, default ON, width 128 | LOSSY by design; identical labels on the one game checked (900045). Under quality-first, a default to justify or retire. |

## 3. The shape of the design (a sketch, not a plan)

1. **Classify the tail before building anything.** Re-measure job 0 exactly (H=0) on the fixed
   commit -- the "unfinished in 7 h" and "900157 = 109 min" figures were taken with the broken
   certificate -- with the `[ladder]` instrument on, and sort the expensive samples into: late win,
   no win by 8, loop closes (go-off), loop cannot close (failed combo). Each class wants a different
   bound, and the ratio of plans to distinct states per pass (the memo counters) says how much a
   state-level treatment can recover.
2. **A positive certificate for go-off.** The winless certificate proves "no line wins"; its mirror
   proves "some line wins" from the same reachability fixpoint (loop reachable + outlet reachable +
   the bound covers the opponent's life). The play side has this as the verified x999 route; the
   ladder should accept it at a cut instead of searching for the line. A certified win at cut t IS
   the label t. Soundness bar: the certificate must be a real, executable win -- reuse the route's
   verification, do not re-derive it.
3. **Bound the failed combo.** When the fixpoint proves the loop cannot close this turn (a piece
   unreachable: not in hand, not diggable, not wishable within the mana bound), the enumeration of
   partial combo lines is pointless even when the turn is not winless overall (combat may still
   win). Today the certificate is all-or-nothing per turn; the finer result -- "the combo branch is
   dead, develop and combat remain" -- should prune the combo branches of the pass and leave the
   rest exact. That is the stuck-turn closure applied inside a pass.
4. **Work bounded per distinct reachable state.** The target cost model for a pass is
   O(reachable states at the cut), reached by keying the pass on the state closure rather than on
   plans; the dedup and the no-win memo are the existing halves of that key. Measure with the
   memo counters, not wall clock (see `search-cost-attribution-method`).

## 4. Constraints carried over

- Labels may not move later. Every bound here is a proof (a certificate) or a lossless closure;
  anything lossy is a diagnostic, like the horizon.
- The certificate is admissible in ONE direction only: it may over-credit resources and refuse to
  certify, never under-credit. Both 2026-09-16 holes were under-credits; the audit method that found
  them (bisect the label-path levers on an "earlier" row, then trace the winning line) is the way to
  find the next.
- The exact reference rows from the cancelled value-leaf run are over-labelled by the old
  certificate; the value-leaf re-run is a fresh queue on 1806a7e9 or later.
