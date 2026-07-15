# More robust value-leaf fallback under budget constraint (deferred idea)

**Status:** deferred refinement. Prompted by the Hinata value-model (`490745c`) integration:
the value leaf is play-neutral at real budgets but can lose a game at the suite's tiny 20 ms
purely from **budget churn** (search reordered so the winning line surfaces later). Confirmed:
`hinata_smoke_d5_s1001 gi20` wins T7 without the value model, loses at budget ≤320 ms with it,
and **recovers to a T6 win at ≥1280 ms** — the line exists, the value leaf just needs more budget
to find it at 20 ms.

## The problem

`UseValueModel()` is default-ON and engages for any deck shipping `<deck>.value.json`
(`src/ai/DecisionProviders.cpp`). The value leaf is a cheap-but-weak leaf evaluator; for
"fallback" decks (Hinata: `value_trust_depth=0`, escalate at every depth) it still evaluates at
the search **horizon**, which reorders exploration. Under a tight budget the reordering can
truncate the search before it reaches a win it would otherwise find — a benign but real
low-budget wobble that shows up as a suite regression.

## Candidate fixes (measure each with the suite as A/B; keep byte-identical toggle)

1. **Reduce value-leaf search depth when budget-constrained.** When the per-decision budget is
   small (e.g. ≤ some threshold ms, or when node counts show the search is starving), drop to a
   shallower value-leaf evaluation (or skip it) so the search spends its budget on breadth toward
   the win rather than on reordered-but-truncated deep lines. Effectively: the value leaf helps
   when budget is ample, hurts when starved → gate it on effective budget.
2. **Adjust the virtual-budget mapping** if recent engine changes (allocator perf `8439ca9`,
   hybrid redo, start-gate relaxation) justify it — i.e. recalibrate how wall-budget maps to
   search effort so the value-leaf path isn't systematically starved at the suite's 20 ms.
3. **Gate weak-fallback value leaves to gen-only.** For decks where the value leaf gives no play
   benefit (Hinata trails the heuristic 0.5–0.7 LP; it's shipped for the ~1.2× gen speedup), use
   `<deck>.value.json` only during generation and NOT during play — keeps play byte-identical.
   Cheapest correctness-preserving option, but a special case.

## Validation

Any change must A/B on the suite (train seeds) and hold-out (overnight): the win-or-tie bar is
that Hinata (and other value-leaf decks) are no worse at suite budget AND unchanged at ample
budget. Confirm a chosen fix makes `hinata_smoke_d5_s1001 gi20` win at 20 ms without regressing
other decks. See the regression-testing skill for the budget-churn classification this addresses.
