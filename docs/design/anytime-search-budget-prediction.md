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

## THE REQUIREMENT, SHARPENED (user, 2026-09-09)

The directive above is not abstract — it comes from a concrete way of running the engine:

> *"there are some super degenerate games out there and running a lot of games at unlimited budget
> has a high chance of hitting a few of these cases. So, it is far too costly to run at unlimited.
> Hence, if I want high quality I want to use a large, but finite budget. ... With such a budget we
> should be able to get those truncated down to a small number of games and would be unlikely to
> lose much accuracy as a result. However, if we have truly lossy cases like this one for high
> budgets this no longer works as intended."*

and

> *"the goal is to have nothing with fully specific unlimited budget code. Unlimited budget is not a
> special case, it is just the limit to which increasing budget converge toward."*

So the engine owes two properties, neither of which it currently has:

1. **CONVERGENCE.** Raising the budget must monotonically reduce truncation, so a large finite
   budget is a usable stand-in for unlimited. Unlimited must fall out of the same formulas as their
   limit, not be selected by an `if (Unlimited())` branch.
2. **GRACEFUL LOSS.** The residual truncations must cost almost nothing. A truncation may stop the
   search EXPLORING; it must not make it FORGET something it already proved.

### Measured 2026-09-09: the shipped guard has NEITHER property

Snow, 40 games, seed 5500001, `--depth 5`, control binary (`MTG_ROLLOUT_STATS`):

| budget (virtual ms) | `Limit()` units | overrun ceiling | ceiling / budget | aborted passes | avg | wall |
|---|---|---|---|---|---|---|
| 20 (play) | 18,000 | 1,000,000 | **55x** | 0 | 6.0500 | 247 s |
| 60 | 54,000 | 1,000,000 | **18.5x** | 0 | 6.0500 | 876 s |
| 200 | 180,000 | 1,000,000 | **5.6x** | **1** | 6.0250 | 1,600 s |

**Property 1 fails, and fails in the exact direction that matters.** `kOverrunFloor` is an absolute
unit count, so the allowance is FROZEN while the start gate admits ever-larger passes. Turning the
budget up therefore makes truncation *more* likely, not less. Above ~556 virtual-ms the floor stops
binding and the ratio pins at `kOverrunBeta` = **2x forever** — the tightest ratio the guard can
have — so "a large but finite budget" lands permanently in the worst band.

**Property 2 fails too, and it is measurable.** A new counter (`rescuable` in the `id_pass`
stats line) counts aborted passes that had ALREADY PROVEN a strictly better win than the shallower
line the rollback commits. Snow, 12 games, play settings, ceiling tightened to force aborts:

| ceiling | aborted | **rescuable** |
|---|---|---|
| 0.5x budget | 61 | **14 (23%)** |
| 1x budget | 40 | 3 |
| 2x budget | 11 | 0 |

So "a pass that had already proven a win can be discarded" is not a theoretical reading of the
code — it happens, at ~23% of aborts once aborts are common.

### What was built

**(a) ANYTIME COMMIT (`MTG_ID_ANYTIME`).** On abort, keep the attempt when it strictly beats the
last completed pass, instead of discarding it unconditionally. Sound because an aborted attempt is
PESSIMISTIC-ONLY: every abort site returns `max_turns + 1` and every `best` update is a strict
improvement test, so a poisoned child can never overwrite a real one, and `FSLineStoreNoWin` already
refuses to memoise a no-win when anything truncated beneath it. Hence a win in the attempt was
genuinely computed; what the abort destroys is only the exploration of alternatives, which can make
the attempt worse than the shallower pass but never falsely better. So the rule is a min, not a
replacement.

**(b) BUDGET-PROPORTIONAL CEILING (`MTG_OVERRUN_PROP`, `MTG_OVERRUN_MULT`).** Keep the shipped
expression INTACT and add one budget-proportional term to the max, so the allowance can only ever
grow relative to today:

```
ceiling = used_before + max(kOverrunBeta       * budget->EffectiveLimit(),   // shipped
                            kOverrunFloor,                                   // shipped
                            kOverrunBudgetMult * budget->EffectiveLimit())   // new   (saturating)
```

`EffectiveLimit()` is new on `SearchBudget`: it reports LLONG_MAX when unlimited, so "unlimited"
stops being a sentinel 0 that collapses every allowance derived from it to zero and forces an
`if (!Unlimited())` arm. With saturating arithmetic the ceiling becomes unreachable in the limit —
**unlimited falls out of the formula and the special case is deleted.**

`kOverrunBudgetMult` = 55 places the crossover just ABOVE ship settings: 55 x 18,000 = 990,000, a
shade under `kOverrunFloor`, so at 20 virtual-ms and every smaller budget the shipped floor still
binds and behaviour is bit-for-bit unchanged; above ~20.2 virtual-ms the proportional term takes
over (9.9M at 200 virtual-ms, where the frozen floor would still say 1M). **"Monotone" here means in
the ALLOWANCE, not in quality** — a looser ceiling still changes which line is committed where the
guard used to fire, and heuristic evaluation is not monotone in search effort. What containment
guarantees is that no win is lost to a truncation that survives today.

The two ESCALATION ceilings were already `2 x Limit()` with no absolute floor, i.e. they never had
the non-convergence defect. Their value is deliberately unchanged; only their `!Unlimited()` arm is
replaced by saturating arithmetic.

**TWO WRONG CUTS, RECORDED BECAUSE BOTH ARE EASY TO REPEAT.**

1. *Anchoring on `Remaining()` instead of `Limit()`.* `Remaining()` SHRINKS as the shallow ladder
   passes spend budget, so the deepest and most valuable pass received the TIGHTEST ceiling —
   exactly backwards. On 12 Snow games: 15 aborts (vs 0 shipped), a byte-IDENTICAL digest, and
   **2.03x the wall time**. The truncations changed no play at all; they just made the executor
   re-search shallower and buy the same work twice. Same mechanism that made a 50,000 fixed floor
   +24% worse — a tighter guard is not a cheaper guard.
2. *Leaving that `Remaining()` anchor on the ESCALATION sites after fixing the ladder.* This cost
   `fivecolour_smoke_d5_s1001` gi2 (5 -> 6, avg 5.1333 -> 5.1467) and was the ONLY smoke failure of
   the whole exercise. **The attribution is isolated**: re-running smoke with the ladder ceiling
   tightened to 198,000 but the escalation at its shipped value gives 73/73 PASS. The first,
   plausible-sounding read — "the tight ladder ceiling cut a pass before it proved the T5 line, and
   the anytime commit cannot recover what was never computed" — was WRONG. Trace a mover before
   recording causality.

### ADOPTED 2026-09-09 — both default ON, hatches kept

Measured on Snow (`--depth 5`, seed 5500001) and the smoke suite:

| arm | avg | digest | aborts | rescuable | wall |
|---|---|---|---|---|---|
| HEAD binary, ship settings, 300 games | 6.0867 | `43d00a181d6aa29b` | 1 | — | 1,602,534 ms |
| new binary, flags OFF | 6.0867 | `43d00a181d6aa29b` | 1 | 1 | 1,617,597 ms |
| anytime only | 6.0867 | `43d00a181d6aa29b` | 1 | 1 | 1,587,267 ms |
| **ADOPTED (both, defaults)** | 6.0867 | `43d00a181d6aa29b` | 1 | 1 | 1,658,069 ms |
| control @ 200 virtual-ms, 40 games | 6.0250 | `777dec0a8ae7451f` | 1 | 0 | 1,143,789 ms |
| **ADOPTED @ 200 virtual-ms** | 6.0250 | `777dec0a8ae7451f` | **0** | 0 | 1,119,727 ms |

- **Parity holds**: the HEAD binary reproduces the flags-off digest at 300 games, and smoke is
  73/73 with 0 configs changed.
- **Ship settings are untouched** by construction (the crossover sits above them), so this carries
  no adoption risk at any setting currently in use.
- **The regime it exists for works**: at 200 virtual-ms the truncation disappears (1 -> 0) with an
  identical digest. That is convergence — a larger budget now truncates *less*, where before it
  truncated *more*.
- Unit 74/74, scenarios 74/74.

An earlier arm proved the anytime commit is load-bearing rather than decoration: with the ladder
ceiling at 11x and NO floor, 300 Snow games diverged and scored WORSE without it
(6.0900 / `3a7babe9803ea2e7`, 6 proven wins discarded) and returned to byte-identical with it
(6.0867 / `43d00a181d6aa29b`).

### Still open after this change

- **The residual truncation is bounded, not abolished.** The anytime commit preserves only what a
  pass PROVED; a pass cut before it ever reached a better line has nothing to rescue. Closing that
  needs candidate fix (3), resumable passes — the memo tables already survive within a decision, so
  quantify how much of an aborted pass is actually re-done before assuming a resume buys anything.
- **Candidate fix (2), the predictor, is untouched.** It remains the strictly-better lever: a pass
  never started unaffordably loses nothing at all.
- **A ~200,000 ladder ceiling at ship settings** (worth ~3% wall and a shorter p99 on the
  2026-09-08 held-out sample) now has smoke evidence too, and is deliberately not taken here. Still
  the user's call.
- **The depth-fallback escalation path is not anytime-rescued** — see the comment at its abort site:
  a fully-aborted descent sets `hcommitted = 0` and the take-decision discards `hline` wholesale, so
  a rescue there would be a no-op without also asserting a committed depth that was never searched.

### Other places unlimited is still a special case (catalogue, 2026-09-09)

- **A GENUINE NON-CONVERGENCE, NOT FIXED HERE**: `TurnSolver.cpp` `esc_k` — the escalation re-scores
  the WHOLE candidate pool when the budget is unlimited, but stays pinned at `s_esc_k` (3) at every
  finite budget however large. Its own comment records fixing the unlimited case for exactly this
  reason; the finite case was left behind. Same defect class as this document, needs its own
  measurement, and it is inert for Snow (no value model attached).
- **Infinity spelled as a sentinel 0**: `groupwave::g_state.bound_limit` uses `0.0` to mean
  unbounded, the same trap as `Limit()` returning 0. Works today; it is the shape that bit us.
- **Redundant but harmless**: ~10 sites write `!budget->Unlimited() && budget->Exhausted()`, but
  `Exhausted()` already returns false when unlimited.
- **Already correct limits**: the breakpoint/group WAVES (`BpWavesHere`) are written to this
  philosophy explicitly ("an UNLIMITED budget is just the end of that scale"), and the value-label
  per-position ceiling scales as `Limit()`.

### Guardrails for whoever picks this up

- The backstop cannot simply be deleted: it exists because "a single flooded turn deep in a line can
  branch orders of magnitude beyond the ~6x growth assumption → a multi-minute / hung search"
  (`SearchBudget.h`). Removing it without (2) reintroduces hangs — measure before/after wall on
  Snow's tail (its p99 is ~100 s at 1,200 games, max ~268 s).
- Snow is the right test deck: it is the only deck that trips the guard at all (smoke: 0 suite
  configs change at a 5x tighter floor), 6.3x the next-worst deck's mean cost.
- Judge quality on a HELD-OUT sample. The 300-game train set showed a 1-game regression that did not
  reproduce on 1,200 held-out games, and a "p99 −28%" that shrank to −8.6%.
