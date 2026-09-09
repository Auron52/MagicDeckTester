# Anytime search: tighten mid-line budget prediction

Status: **deferred / not being worked on.** Captured here (not in any agent's private
notes) so it is available to everyone. Separate from the keepgen-speed work that
*reduces* the per-decision budget (`--budget-ms`, `MTG_EQUIV_DEPTH`); this item is about
*spending a given budget better*.

## Problem

Each decision the search makes runs under a per-decision time budget (`budget_ms`, e.g.
20ms in play / rollouts). The search **already** attempts to predict, before descending a
line, whether that line can complete within the remaining budget, and skip it if not.

The observed behaviour (user report, 2026-07-05) is that lines nonetheless often get **cut
off mid-computation** — the search commits to going deeper, then the budget expires partway
through, and the partial deep work is discarded. That work is unproductive: it neither
completes to improve the chosen move nor is cheap enough to have been free. So the effective
useful throughput under the budget is lower than the budget nominally buys.

This matters two ways:
- **Keepgen / offline generation**: wasted per-rollout time directly inflates the wall-clock
  of expensive profiles (Hinata, Anti-Lifegain), where the whole feasibility question is
  rollouts-per-second. Cheaper *and* better-spent budget compound.
- **In-play search quality**: a decision that burns its budget on an abandoned deep line
  makes a worse move than one that spent the same budget on completed shallower work.

## The want

Make the existing "will this line exceed the remaining budget?" prediction **more accurate /
less optimistic**, so fewer lines are started-then-guillotined. The goal is that when the
budget expires, the time was spent on work that actually informed the move.

## Step 1 — MEASURE (do this before changing the predictor)

We do not currently know how often the cutoff actually fires mid-line, or how much time it
wastes ("not sure what the actual data says" — user). Before touching the heuristic, add a
lightweight, behaviour-neutral instrument (STDERR/diagnostic only, never folded into results)
that records per decision:
- budget granted vs wall-time actually used (did it overrun / underrun?);
- how many lines were *started* and then abandoned to the budget vs completed;
- wall-time spent inside lines that were ultimately discarded (the wasted fraction).

Aggregate over a regression battery (burn + a heavy deck) to get the real distribution. If
mid-line guillotining is rare, this item is low priority; if it is a large fraction of budget,
it is a real speed + quality lever.

## Candidate improvements (choose after measuring)

- **Iterative deepening / always-hold-a-complete-answer.** Never let a budget expiry leave the
  decision with only partial deep work: keep the best *fully evaluated* move from a shallower
  completed pass, and only adopt a deeper result once it completes. A mid-line cutoff then
  costs nothing beyond the abandoned pass — the move is still the last completed one.
- **Better pre-descent cost estimate.** The current predictor is evidently too optimistic about
  how long a deeper line takes (or too coarse in when it re-checks). Calibrate the estimate
  against measured per-node cost for this deck/state, and/or check the clock at finer
  granularity so an overrunning line is abandoned sooner rather than at the end.
- **Reserve-and-commit.** Only descend a line if `estimated_cost <= remaining_budget * margin`
  (margin < 1); tune the margin from the measured overrun distribution so the tail of
  guillotined lines shrinks.

## Non-goals / guardrails

- Must stay **behaviour-neutral when disabled** and keep the search-primary contract: this is
  about *when to stop spending*, not about pruning the search space with new heuristics. Any
  change must preserve an unpruned/legacy A/B so its effect on chosen moves is measurable and
  byte-diffable.
- Distinct from `--budget-ms` / depth reduction (which *lower* the budget deliberately) and
  from the equivalence/keepgen pipeline; this is a general engine-quality improvement that
  those settings sit on top of.

---

## STEP 1 IS DONE (2026-09-08) — and it named a second, sharper problem

Step 1 above asked for the measurement ("we do not currently know how often the cutoff actually
fires mid-line, or how much time it wastes"). Measured on Snow at shipped play settings
(d5 / 20 virtual-ms → `Limit()` = 20 × 900 = **18,000 units**):

- **Deck-wide the cutoff is RARE**: `id_pass starts=5983, aborted=1` over 300 games —
  `waste_share = 1.95%` of units. It is not a general throughput tax.
- **But when it fires it is enormous**: the single worst game spent **1,001,374 of its 1,778,424
  units (56%)** inside ONE aborted pass.
- **Why that number**: the guard is `max(kOverrunBeta * Limit(), kOverrunFloor)` =
  `max(2 × 18,000, 1,000,000)`. The FIXED FLOOR SWAMPS THE INTENDED CEILING BY 55x, and only stops
  binding above a ~556 virtual-ms budget — i.e. never at any setting we ship.
- **Sweeping the floor is NOT the fix.** 200k measured quality-EXACTLY-neutral on 1,200 held-out
  games (0 of 1,200 win turns changed) for −3.2% wall / −8.6% p99, and cross-deck inert (smoke 0
  configs changed). But 50k was **+24% WORSE on wall** than shipped — cutting passes that would have
  completed just pays for the same work again a level shallower. Lever exposed as
  `MTG_OVERRUN_FLOOR` (default unchanged = 1000000, so nothing moved). See `analysis-Snow.md`.

## USER DIRECTIVE (2026-09-09): IT MUST NOT GENERICALLY TRUNCATE AT FINITE BUDGETS

> *"we'll need to fix it so that it does not truncate at finite budgets ... (does not generically
> truncate)"* — after being walked through the mechanism and asking, unprompted: *"I'm concerned
> about any limits preventing us from finding a win."*

This RAISES the existing no-lossy-truncation bar (`heuristic-optimization.md` Rule 0b). That bar's
test is the INFINITE-budget one, which today's guard passes trivially — `SetOverrunLimit` is only
armed inside `if (budget != nullptr && !budget->Unlimited())`, so at unbounded budget it does not
exist. The new requirement is stronger: **at a FINITE budget the search must not discard work
generically.**

### What is wrong with the current shape

The guard is a *generic* guillotine on a unit count. On abort (`TurnSolver.cpp`, the ID loop):

```
if (aborted) { line = prev_line; committed_depth = prev_committed; break; }   // attempt DISCARDED
```

The whole `attempt` is thrown away **regardless of what it contains**, and inside the aborted pass
the rollout returns `max_turns + 1` (`leafeval::kInvalid`), i.e. it actively reports those leaves as
LOSSES. So the failure mode the user is worried about is real and specific:

> **A pass that had already PROVEN a win can be discarded, and the shallower line committed instead.**

A found win is a fact about the position. Throwing it away because the pass did not finish exploring
*alternatives* is the generic-truncation shape, not a reasoned dominance prune.

### Candidate fixes, best first (all must keep an A/B hatch + the unpruned diff)

1. **NEVER DISCARD A PROVEN WIN.** Before rolling back, check whether `attempt` already carries a
   winning line; if so commit it (it cannot be worse than the shallower pass's line by the metric).
   Narrow, cheap, and directly answers the user's concern. Verify first whether `FSLineWin` returns a
   usable partial line on abort or whether the `max_turns + 1` poisoning has already destroyed it —
   if the latter, the abort path must preserve the best line found BEFORE the overrun fired.
2. **FIX THE PREDICTOR, so the pass is never started unaffordably.** The original Step 2 of this doc.
   Strictly better than any guillotine: it loses nothing, it only stops budget being spent on work
   that will be thrown away. Calibrate the pre-descent estimate against measured per-node cost
   (`g_probe_cost` / `g_probe_leaves` already record per-depth cost), and/or reserve-and-commit with
   a margin tuned from the measured overrun distribution.
3. **MAKE THE DEEP WORK RESUMABLE** rather than discarded — the TT and the enum/solve memos already
   survive within a decision, so quantify how much of an aborted pass is actually re-done before
   assuming a resume buys anything.
4. Retune / remove `kOverrunFloor` — LAST, and only as a backstop for genuine hangs. It is the lever
   most likely to trip the user's concern, because a tighter guillotine cuts deeper lines sooner.

### Guardrails for whoever picks this up

- The backstop cannot simply be deleted: it exists because "a single flooded turn deep in a line can
  branch orders of magnitude beyond the ~6x growth assumption → a multi-minute / hung search"
  (`SearchBudget.h`). Removing it without (2) reintroduces hangs — measure before/after wall on
  Snow's tail (its p99 is ~100 s at 1,200 games, max ~268 s).
- Snow is the right test deck: it is the only deck that trips the guard at all (smoke: 0 suite
  configs change at a 5x tighter floor), 6.3x the next-worst deck's mean cost.
- Judge quality on a HELD-OUT sample. The 300-game train set showed a 1-game regression that did not
  reproduce on 1,200 held-out games, and a "p99 −28%" that shrank to −8.6%.
