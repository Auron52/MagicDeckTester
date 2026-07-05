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
