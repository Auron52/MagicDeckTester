# Escalation: spend the reserve on reaching TRUST, not on a heuristic pass below it

Proposed by the user, 2026-08-14. Not implemented. A budget-ALLOCATION change at one decision point,
measurable with the existing harness.

## The situation

The hybrid runs the cheap value leaf, then escalates any line committed BELOW the deck's trust depth
to a heuristic pass (`escalate_below = value_trust_depth`, `AIEngine.cpp:1881`). On FiveColour trust
is 6 and the play depth is 6, so a line that commits at 6 is trusted and skips escalation entirely;
a line that commits at 1-5 -- typically because the budget ran out -- escalates.

When it escalates, `TurnSolver.cpp:16060` predicts the affordable HEURISTIC depth:

```
chat[d] = probe_cost[d] + R * probe_leaves[d]          // R frozen per deck, default 120
walk chat[] through the start gate against esc_budget->Remaining()
target  = min(escalation_cap, deepest d that fits)
```

runs one heuristic pass there, and lets the crossover decide whether to take it. If nothing fits, the
value line is kept (`hcommitted = 0`).

**The gap: nothing considers spending that same reserve on one more VALUE ply.** The reserve is only
ever offered to the heuristic.

## Why that is the wrong default

Two things are true at once and they point the same way.

**1. A value ply is far cheaper than a heuristic pass at the same depth.** The estimate above says so
structurally: the heuristic term is `R * probe_leaves[d]` with R ~ 120, and the value leaf is O(1) --
it does no rollout, so it has no such term. The measured version of the same fact, from the
FiveColour 400x4 matrix: **V6 at 12.7 s/game against H5 at 844** -- 66x. A deterministic
cross-check on 200 games (2026-08-14, `MTG_DUMP_UNITS`): the no-sidecar arm at depth 5 does 48.3M
rollout units against the sidecar arm's 39.2M at the same depth, and 2.75x the wall.

**2. Reaching trust does not just improve the line, it CANCELS the verification.** At a committed
depth >= trust the crossover entry is the never-fall-back sentinel, so there is nothing further to
pay. Today we spend the reserve on a weaker answer AND still owe the comparison. Measured on
FiveColour d5-vs-d6 with the sidecar, paired over 200 games: d6 is cheaper in 23 games, dearer in 6,
identical in 171, median 60,524 units against 67,067 -- the trust effect is already visible where the
search happens to get there on its own.

So when the search stops one ply short of trust, the reserve is being spent on the more expensive of
the two options, to buy the outcome that still needs checking.

## The symptom that gives it away (user, 2026-08-14)

**Raising the budget sometimes LOWERS total time on a deck with a trusted depth.** That is close to
inexplicable under any other account -- a bigger per-decision budget can only let the search do more
work -- and it falls straight out of this one:

* small budget  -> the value search commits BELOW trust -> escalation fires -> a heuristic pass, with
  its `R * leaves` rollout term, on top of the value search that already ran
* larger budget -> the value search REACHES trust -> `escalate_below` is not triggered at all -> the
  heuristic pass never happens, and the crossover has nothing to verify

The extra budget buys its own saving, and more: it removes a cost term rather than adding one. This
makes the whole proposal falsifiable rather than merely plausible -- the non-monotonicity is a
prediction, and it should appear as a DIP in deterministic work units (not wall) as budget rises,
concentrated on decks whose trust depth sits at or just above the depth the budget can reach.

It also means the current behaviour has a perverse edge: a deck can be made faster by being given
MORE resource, which is not a property anyone tuning `budget_ms` would expect, and which would read
as noise to anyone sweeping budgets on wall-clock.

## The change

At the escalation decision, before predicting the heuristic depth: estimate the cost of continuing
the VALUE search to `value_trust_depth` and, if the reserve affords it, do that instead.

```
value_cost(d) ~ probe_cost[d]                    // no R * leaves term: the leaf is O(1)
if committed < trust and value_cost(trust) fits esc_budget:
        deepen the value search to trust        -> trusted line, NO heuristic pass at all
else:
        existing predicted-affordable heuristic pass
```

The estimate needs no extra search: `g_probe_cost[]` and `g_probe_leaves[]` are already computed by
the probe, which is exactly why the heuristic walk is free today.

## What has to hold

**Completing, not starting.** "Affordable" must mean reaching trust AND finishing. A value pass that
dies at depth 6 mid-way leaves neither a trusted line nor a heuristic check -- strictly worse than
today. Reuse the existing overrun-guard-with-depth-fallback shape (`TurnSolver.cpp:16087`): bound the
attempt, and on abort fall back to the heuristic path rather than to nothing.

**Determinism.** The predicted walk must stay a pure function of this decision's probe structure. The
frozen-R rule (`TurnSolver.cpp:16032`) exists because an adaptive EMA made play depend on the
game->thread schedule, which breaks GT digests and cross-machine reproducibility. Any constant this
adds is frozen per deck the same way -- calibrated offline into the profile, never learned in play.

**On-policy only.** Like the other value_play levers, this applies when the resolved depth IS the
block's `target_depth` (`vp_here`). A harness case pinning d3 via `--ignore-play-profile` must not
see it.

**Only where trust is reachable.** If `value_trust_depth > target_depth` the deepening can never
land, and the branch must not fire. On a deck whose trust depth is UNSET the question does not arise.

## How to measure it

Standard heuristic-optimization loop (`.claude/skills/heuristic-optimization.md`): temporary runtime
selector, sweep the regression suite's train seeds, validate the winner on overnight (held-out)
seeds, report, adopt in the profile only on approval.

The decks that can show anything are the ones with a trust depth below their play depth and a real
escalation rate -- FiveColour first (trust 6, cap 5, and now in all three suite tiers). Report BOTH
axes, because the change is meant to be quality-neutral-or-better AND cheaper:

* loss-penalized avg win turn, paired per seed
* deterministic work (`MTG_DUMP_UNITS`), not wall -- the wall reading that motivated this question was
  non-monotonic (d5 3.07 / d6 3.24 / d7 2.70 s/game) purely from contention, and units are immune

A useful instrument to add first: count how often the search stops one ply short of trust. If that is
rare, the ceiling on this whole idea is low and it should be dropped before implementation -- which is
the cheapest possible outcome and worth knowing on day one.

Related: `value-leaf-fallback-table.md` (the crossover this exploits), `.claude/skills/value-leaf.md`
(trust depth / escalation cap semantics), `depth-matrix-degenerate-games.md` (where the 66x and the
per-game unit instrument come from).
