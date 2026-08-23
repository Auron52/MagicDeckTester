# Greedy inside the search: what we found, and what it means

**Status: PAUSED 2026-08-23, resuming Monday. Nothing is running. Tree is clean.**
Self-contained — everything needed to pick this up is in this file and in git.

---

## READ THIS FIRST IF YOU ARE ANOTHER AGENT

**The OVERNIGHT ground truth is stale.** Dragonstorm's `GradesNoWinLeaf()` opt-out was removed
(commit `8dc20bdc`, USER-approved). Smoke and regression GT were rebaselined and accepted; **the
overnight tier was NOT**. Its `dragonstorm d3 s5005` cell contains game 227, which the change turns
from a turn-8 win into a loss at that gate cell, so **an overnight run will show a real regression
there. That is expected, not a new defect.** Either rebaseline the overnight tier
(`bash test/regression.sh --overnight`, inspect, then `--overnight --accept`) or leave it alone; do
NOT root-cause it as a fresh bug, and do NOT `--accept` it without inspecting.

Everything else is green: smoke 39/39, regression 65/65, reference gate 208 refs clean, CI green on
ubuntu + windows + determinism parity.

---

## 1. The question we were actually answering

The USER's standing directive is *"search should be truly search at every level. Greedy is simply
too unreliable to be part of it."* On 2026-08-23 that was sharpened to **"We shouldn't have any
greedy within the searched window."**

"Greedy" here means `TurnSolver::Solve()` — a fast one-shot heuristic that picks a play without
searching. The engine is supposed to use real search instead. The job was: find every place greedy
still runs inside the search, and remove it if removing it clears the adoption bar.

## 2. What we found — the short version

**The engine's search is real, but at production budgets the thing that RANKS its options is a
greedy playout, 83–99% of the time.**

`SolveWithLookahead` uses iterative deepening: it evaluates every candidate at pass 0, then pass 1,
then pass 2, and so on, each pass overwriting the last. The decision that ships is whichever pass
finished before the budget ran out. **Pass 0 is fully greedy** — its interior second main and its
whole rollout both take `Solve()`, because `SolveSecondMainInSearch` short-circuits at `depth <= 0`.

Measured at play settings, 200 games (`MTG_M2_YIELD_STATS=1`, the `COMMITTED PASS` line — note it is
CUMULATIVE, so the share finally committing at pass k is `(p[k]-p[k+1])/p[0]`):

| deck | decisions | got past pass 0 | **final decision made by the GREEDY pass 0** |
|---|---|---|---|
| antilife | 7,046 | 38 | **99.5%** |
| fivecolour | 238,775 | 12,543 | **94.7%** |
| kitty | 135,455 | 23,223 | **82.9%** |

So the outer candidate loop is genuine search — it enumerates the options and plays each one out.
But the *evaluation* used to compare them is greedy in the overwhelming majority of decisions,
because the budget dies before pass 1 completes.

## 3. Why this matters more than the per-deck work

Three results had been filed separately as clean nulls:

* `MTG_5C_SSM=0` is byte-identical to default on FiveColour over 60 games.
* Anti-Lifegain's branch-site hook is byte-identical over 26,000 games.
* The searched-m2 adoptions generally measure inert.

**They have one cause.** The per-deck `SearchedSecondMainInSearch()` hooks only affect passes >= 1,
and passes >= 1 rarely commit. These are not inert levers — they are levers on a path the budget
rarely reaches. Three independent "byte-identical" results should have been treated as a signal to
check the binding rate, not as three clean nulls.

**Consequence: adding the hook to more decks is close to decorative until the pass-0 dominance
changes.** The rollout project (`searched-design-deck-rollout.md`) should not resume deck-by-deck
until this is understood.

## 4. The open question for Monday

**Why does iterative deepening almost never get past pass 0 at a 20-virtual-ms budget?** That is a
budget/cost question, not a per-deck one. Things worth knowing before changing anything:

* Is pass 0 genuinely that expensive, or is pass 1 disproportionately so? There is a start gate
  (`sub_depth > 0` cost-ratio estimate) that SKIPS a pass it predicts will not fit — measure how
  often the gate is what stops pass 1, versus the budget actually running out.
* What does the committed-pass histogram look like at 10x / 100x budget? If pass 1+ commits often
  there, this is purely a budget-allocation story.
* Is `budget_ms` (VIRTUAL units) sized for this? The decks ship at 20.

**Do not "fix" this by raising the default budget without measuring** — perf is a shipping
constraint and the adoption bar (below) applies.

## 5. The adoption bar all of this is measured against

USER, 2026-08-23. Both are gates; test both, block on either.

1. **"Are we improving the play generally?"** — OVERALL quality AT PLAY SETTINGS (the deck's
   resolved `value_play` depth/budget, NOT the suite's pinned d3/d5 gate cells), on a LARGE sample.
   Worse on some seeds is fine if the average improves. A change should improve quality, or be
   quality-neutral with other upside (a pure perf win counts). A small quality loss for a large perf
   win is legitimate but is the USER's call — and **look for a strict win first**; only escalate the
   trade if a strict win is genuinely impractical.
2. **"Are we preventing search from finding a win?"** — tested GAME BY GAME, at **unlimited budget**
   and the **depth the game won at BEFORE** the change. Win returns => recoverable, gate 1 decides.
   Win never returns => the change deleted the line structurally, which blocks on its own.

Escalating budget/depth is gate 2's instrument and proves nothing about gate 1. Escalate to
CONVERGENCE, not to a round number.

## 6. What was decided and shipped along the way

| item | outcome | commit |
|---|---|---|
| antilife's `GradesNoWinLeaf` opt-out (never existed; the deck was net-worse on a 2-game sample) | re-measured: **-66 turns / 172,000 paired**, keeps the default | `7038dded` |
| dragonstorm's `GradesNoWinLeaf` opt-out | **REMOVED** — both gates clear; no deck opts out now | `8dc20bdc` |
| the "escalate before opting out" rule | **RETRACTED** — it was gate 2's test used to answer gate 1 | `14505f0e` |
| searching antilife's ROLLOUT second main | **REJECTED** — +10 turns / 30,000 paired at play settings, ~2.2 sigma worse; the `MTG_M2_CAP1` strict-win route is inert on this deck | `b851c8b5` |

## 7. Instruments added (all default OFF or behaviour-neutral)

* `MTG_LEAF_NOWIN_FORCE` — force the no-win tie-break past a provider opt-out, so an opt-out can be
  re-validated without a scratch build.
* `MTG_AL_SSM_ROLLOUT` — re-open antilife's declined rollout second main (default OFF, rejected).
* `MTG_M2_CAP1` — cap the interior m2 solve to depth 1 (per-job form of `MTG_M2_SEARCH_DEPTH=1`).
* `MTG_M2_YIELD_STATS=1` now also prints **`M2 PATH`**, **`M2 SITE`** and **`COMMITTED PASS`** —
  which second-main path ran, at which call site, and which pass committed. This is the instrument
  that found everything in section 2.
* `scripts/leaf_tiebreak_check.py` — per-deck leaf tie-break A/B; measures at PLAY settings by
  default, pools multiple decks into one batch, `--seed-base` to EXTEND a sample, and refuses to
  call a sign below 20 changed games.

## 8. A trap worth not re-discovering

`MTG_NO_SEARCH_SECOND_MAIN` **does not do what its comment claims.** It is documented as "the kill
switch for the per-deck opt-ins"; it is not. `GreedySecondMainEnabled()` returning true only yields
`searched = site_on && provider_hook`, which is the default path anyway — so the flag can cancel
`MTG_SEARCH_SECOND_MAIN=1` and nothing else. Verified on fivecolour: with the switch set, BRANCH
searched 4,529, identical to default. **A/B-ing searched-vs-greedy on an opted-in deck with this
lever produces a false null.** Not fixed — fixing it is a lever change worth a USER decision.

Also unrelated but recorded: **KittyEquipment is expensive at its shipping d5/20** — 23 games over
30 s per 4,000, worst 75.3 s — and it has **no rows in `test/regression_cases.sh`**, so that cost
has never appeared in any suite tier.
