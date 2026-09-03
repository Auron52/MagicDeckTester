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

## IMPLEMENTATION + EMPIRICAL FINDING (2026-07-15)

Implemented fix #1 as a **fallback budget reserve** (option 2 in spirit): env `MTG_VALUE_FALLBACK_RESERVE`
(fraction, default 0 = OFF = byte-identical), gated to `consistent_fallback` decks (`value_trust_depth==0`:
Hinata/burn/TH/antilife; Slivers/Knights untouched). In `FullSearchLineHybrid` the value-leaf probe runs on
a capped budget `Limit - reserve` (a separate `SearchBudget` sharing the used baseline), and its spend is
folded back so the heuristic escalation redo runs on `Remaining >= reserve`. Verified OFF = 18/18 smoke
byte-identical (all six value decks). Files: `src/ai/TurnSolver.{h,cpp}` (FullSearchLineHybrid), caller
`src/ai/AIEngine.cpp` (~1409). **UNCOMMITTED** as of 2026-07-15 (held — see dependency below).

**What the A/B showed (per-game, decisive):**
- The value-leaf probe naturally consumes **50–90%** of the per-decision budget, so a *moderate* reserve
  **doesn't bind** (`MTG_HYBRID_STATS` identical at reserve 0 and 0.5; only ≥0.9 changes the probe depth).
- At reserve **≥0.9** all four reproducing Hinata win→loss games recover — and recover to **exactly the
  pure-heuristic win turns** (117→T6, 248→T6, 84→T8, 180→T8, matching `MTG_VALUE_MODEL=0`). Even 6006+84,
  earlier misread as a "genuine" loss (lost at 1280 ms with value on), is fallback starvation — it recovers.
- **BUT** reserve ≥0.9 ≈ disabling the value leaf in play (probe gets ~1 ms). Since value-leaf-vs-pure-
  heuristic is avg9-**neutral** on these decks (overnight +0.0006/10.8k), recovering the churn *this way* is
  just a neutral revert to pure heuristic — it gives back the loss→win games. There is **no moderate reserve
  that is net-positive as-is**, because the heuristic fallback needs ~the whole budget to find the win.

**Root blocker + the unlock (coordination, 2026-07-15):** the reserve is the right lever but **budget-blocked
on the cost of the heuristic fallback rollout**. A separate workstream is **pruning the leaves the heuristic
rolls out on fallback**. If the fallback rollout gets materially cheaper, it finds the win with far less
budget → a **moderate** reserve (probe keeps its cheap verified wins, fallback still finishes) becomes viable
and potentially **net-positive**, not a neutral revert. *(2026-09-03: the held patch NO LONGER
EXISTS anywhere — `MTG_VALUE_FALLBACK_RESERVE` has zero hits in src and zero commits on any
branch; rebuild from this description if ever wanted.)* So: **hold the reserve change** (keep it OFF/uncommitted
to avoid conflicting with the fallback-leaf-pruning edits in the same `FullSearchLineHybrid`/`FSLineWin` path),
then re-run the reserve fraction sweep once cheaper fallback rollouts land. Target: a fraction where Hinata
avg9 goes **negative** (net better), not just back to pure-heuristic-neutral.
