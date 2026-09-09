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

`kOverrunBudgetMult` = 11 reproduces the 200,000 arm of the 2026-09-08 fixed-floor sweep at ship
settings (11 x 18,000 = 198,000) — the one value with a held-out measurement behind it — while now
SCALING with the budget instead of being frozen. **`kOverrunFloor` is deliberately absent from this
expression**, not merely defaulted to zero: deleting the constant is what makes "truncation for
budget reasons only" structural rather than a setting someone can flip. It survives only in the
legacy `MTG_OVERRUN_PROP=0` arm.

**This is NOT monotone in quality and must not be claimed as such** — a ceiling change moves which
line is committed wherever the guard fires, and heuristic evaluation is not monotone in search
effort. The justification is measurement (1 better, 0 worse across the suite), not containment.

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

### THE AUDIT: is truncation only ever for BUDGET reasons? (user gate, 2026-09-09)

> *"we absolutely need to ensure that truncation is done only for budget reasons."*

Three mechanisms can cut the search short. Only one of them is the subject of this document, and
before this change it FAILED the gate.

| mechanism | where | budget-derived? |
|---|---|---|
| **Exhaustion** (`used >= Limit()`) | `BpWavesHere`/`GroupWavesHere` skip a deferred-rank wave phase; the second-main scan breaks its candidate loop | **YES** — "the budget is spent, stop adding optional work" |
| **Overrun ceiling** (aborts a whole running pass) | `FullSearchLine`'s ID loop | **WAS NO.** `max(2 x Limit(), 1,000,000)`: at ship settings `2 x Limit()` is 36,000, so the **1,000,000 constant was the only binding term**. A magic number, the same shape as the `guard++ < 16` correctness ceiling. **Now YES** — a pure multiple of the budget, no constant in the expression at all |
| **Beam width / enumeration caps** | value-guided beam (`_beam_i >= g_esc_beam_width`, escalation-only, near-leaf only, `0 = unlimited`); enumeration caps recorded under `TruncCompleteEnabled()` | **NO, and deliberately so** — these are adopted PRUNING heuristics with their own levers and A/Bs, not this guard. A separate arc |

`gamework::Abandoned()` is also folded into `Overrun()`, but it is **disarmed by default** and exists
only for unbounded value-leaf matrix generation ("this game is VOID"), so it is not a play-path
truncation.

**Mechanically, when does a truncation actually happen?** A decision runs iterative deepening under a
per-decision budget (ship: 20 virtual-ms x 900 = 18,000 units; one unit = one simulated turn-step in
a rollout, or one interior node with a plan applied). Before each pass a START GATE estimates
`cost(k-1) x growth` and refuses to begin pass k if that exceeds `1.1 x remaining` — **that is not a
truncation**, it stops holding a complete answer. If the pass does begin, the overrun ceiling is
armed and `budget->Overrun()` is polled in five places (entry to `FSLineWin` and `FSLineTail`, the
rollout's per-turn-step loop, and two breakpoint child loops). The non-obvious part: **`Limit()`
never stops a running pass** — it is consulted only by the start gate — which is why an 18,000-unit
budget routinely spent 1,000,000 units on a single pass.

### ADOPTED 2026-09-09 — both default ON, hatches kept

**COST IS REPORTED IN DETERMINISTIC UNITS, NOT WALL** (user, 2026-09-09: *"there is some contention,
so we shouldn't rely just on wall numbers"*). `units_total` from `MTG_ROLLOUT_STATS` is exact for a
fixed binary + config — a repeated FiveColour control reproduced 19,895,566 to the unit — whereas
wall on this box is not evidence. Quality is judged on paired per-game movers from the suite's
`.wins` audit. (Caveat, recorded rather than glossed: ACROSS BUILDS a ~1e-7 drift was seen — Snow
49,649,736 vs 49,649,741 for the same effective ceiling and an identical digest. Unexplained; far
below any effect claimed here, but do not assert bit-exact cross-build unit reproducibility.)

| arm | avg | digest | aborts | rescuable | **units** |
|---|---|---|---|---|---|
| Snow 300, HEAD binary | 6.0867 | `43d00a181d6aa29b` | 1 | — | 50,798,060 |
| Snow 300, new binary flags OFF | 6.0867 | `43d00a181d6aa29b` | 1 | 1 | 50,798,104 |
| Snow 300, anytime only | 6.0867 | `43d00a181d6aa29b` | 1 | 1 | 50,782,403 |
| **Snow 300, ADOPTED** | 6.0867 | `43d00a181d6aa29b` | 7 | 6 | **49,649,736 (−2.26%)** |
| FiveColour 100 @ d6/b20, control | 4.8300 | `67d0fb2007aa1b97` | 9 | **3** | 19,895,566 |
| **FiveColour 100, ADOPTED** | **4.8200** | `5e67f60de1b901e3` | 26 | 2 | **16,134,474 (−18.9%)** |
| Snow 40 @ 200 virtual-ms, control | 6.0250 | `777dec0a8ae7451f` | 1 | 0 | 46,202,057 |
| **Snow 40 @ 200 virtual-ms, ADOPTED** | 6.0250 | `777dec0a8ae7451f` | **0** | 0 | 46,420,331 (+0.47%) |

- **Quality is better, never worse.** Full regression suite: **98 unchanged, 1 BETTER, 0 worse** —
  `fivecolour_regression_d5_s2002` 4.8300 -> 4.8200, i.e. gi57 wins on T5 instead of T6. Smoke
  73/73, 0 configs changed. Unit 74/74, scenarios 74/74. Snow is byte-identical.
- **Cost falls where the budget is small** (−2.26% Snow, −18.9% FiveColour) because the ceiling is
  now tighter than the old fixed 1e6 there, so the engine stops burning 55 budgets on a doomed pass.
- **Cost RISES slightly where the budget is large** (+0.47% at 200 virtual-ms) because the pass that
  used to be truncated now runs to completion. **That is the change working as intended, and it
  corrects an earlier claim in this session of "−2.1% wall" at b200 — the opposite sign. That number
  was contention, which is exactly why wall was dropped as evidence.**
- **One rule serves both regimes** precisely because it is proportional: 198,000 at a 20 virtual-ms
  ship budget (tighter than 1e6) but 1.98M at 200 virtual-ms (looser than 1e6).
- **FiveColour is where the discarded-proven-win defect bites hardest**: its control discards 3
  proven wins per 100 games, against 1 per 300 on Snow.

An earlier arm proved the anytime commit is load-bearing rather than decoration: at the same ceiling,
300 Snow games diverged and scored WORSE without it (6.0900 / `3a7babe9803ea2e7`, 6 proven wins
discarded) and returned to byte-identical with it (6.0867 / `43d00a181d6aa29b`).

### Still open after this change

- **The residual truncation is bounded, not abolished.** The anytime commit preserves only what a
  pass PROVED; a pass cut before it ever reached a better line has nothing to rescue. Closing that
  needs candidate fix (3), resumable passes — the memo tables already survive within a decision, so
  quantify how much of an aborted pass is actually re-done before assuming a resume buys anything.
- **Candidate fix (2), the predictor, is untouched.** It remains the strictly-better lever: a pass
  never started unaffordably loses nothing at all.
- ~~A ~200,000 ladder ceiling at ship settings~~ — **TAKEN**, as `kOverrunBudgetMult = 11`, once the
  user's gate ("only for budget reasons", "okay if the results are better") was met: 1 game better,
  0 worse across the suite, −2.26%/−18.9% units. Expressed as a multiple of the budget, never as the
  absolute constant the 2026-09-08 sweep used.
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
