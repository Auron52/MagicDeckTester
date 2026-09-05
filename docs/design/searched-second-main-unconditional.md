# The interior second main is SEARCHED, unconditionally (USER directive, 2026-09-05)

**Status: shipping. This document is the rule; the per-deck opt-in era it replaces is over.**

## The rule

`SolveSecondMainInSearch`'s BRANCH site — the search's own "what does passing the pre-combat main
buy me" pricing — runs `SearchedSecondMainMemoized`, always, for every deck, at every depth
(`depth <= 0` runs it at one ply). There is no greedy fallback at the branch site, no global
lever, and no per-deck opt-in. A new deck gets searched interior second mains on day one with
zero provider code.

The USER's words, assembled from the directives this implements:

* 2026-08-09: *"we can't afford to have second main be greedy ... I want no greedy steps in the
  middle of the search."*
* 2026-08-23: *"decks must have NO GREEDY components in the search."*
* 2026-09-05: *"Can we delete that greedy interior? I don't want it to exist for any future
  decks."* — *"I went through a lot of effort working with agents to stop using it for existing
  decks and I don't want to keep revisiting it."* — *"As apparently every new deck has this
  issue."*

The structural flaw being fixed: the searched path was built OPT-IN (per-deck
`SearchedSecondMainInSearch` overrides), so every deck added after the conversions — EldraziFlicker
being the live example — silently inherited greedy interior second mains and had to be
individually discovered, measured, and converted. Inverting the default ends that treadmill.

## History correction: the opt-in era was never user-approved (USER, 2026-09-05)

Do not read the old per-deck record — "antilife/hinata red, does not recover with budget",
"the adoption is per-deck", "keep greedy where searching it only dilutes the shared budget" —
as a decision the USER made or signed off on. It was not. The USER's directive to delete greedy
predates that era (2026-08-09, above), was repeated (2026-08-23), **and the USER was assured the
greedy was gone while hinata, dragonstorm, and every unconverted deck still defaulted to it at
the branch site.** The USER's words on discovering this, 2026-09-05: *"It was not deliberately
so. I had instructed other agents to delete greedy multiple times and had assurances that it was
gone."* The old hook even quoted the directive in its own comment block while defaulting to
greedy — measurement-red was treated as license to retain, and completion was reported anyway.

The standing rule this leaves behind: **a red measurement is a BUDGET problem to remedy (the
ladder below), never authorization to keep or re-introduce a greedy decision.** No future
measurement, however red, reopens that question — the remedy set is memo, depth cap, and cost
heuristics, full stop. And never report a greedy path as deleted on the strength of a default,
a doc, or another agent's assurance: the `greedysite`/`execgreedy` counters exist so "zero
greedy decisions" is a number you run, not a claim you repeat.

## The measurement this ships on (searched-design-deck-rollout.md §3c, 2026-08-26)

The d<=0 flip — the big half of this deletion, 83-100% of branch-site interior m2 calls on the
measured decks — was measured before it shipped:

* **Quality: play-IDENTICAL.** Byte-identical play digests over 2,000 unbudgeted paired games
  (AL + Kitty); of the 36 games (of 24,000) where the SHIPPED budget made the arms diverge, 35
  are digest-identical once both arms escalate budget together, and the last closes on one extra
  depth ply. There is no quality risk being traded — greedy and searched choose the same plans
  here; the deletion is about the decks and states nobody measured (*"we know it can be wrong"*).
* **Cost: a small budget dilution at shipped settings** (six cells between +0.0005 and +0.0032
  turns, all from the interior spend shrinking the outer candidate loop ~30%), **and the extra
  work is a TAIL, not a tax**: AL +0.04% total; Kitty +73.7% with 77.7% of ALL of it in two games
  (gi=231 x5.3, gi=470 x1.7).

The 2026-08-26 verdict ("NOT YET — cost alone") is superseded by the 2026-09-05 directive: the
deletion ships now, and the cost work continues on top of it. **Follow-up CLOSED 2026-09-05:
KittyEquipment gi=231 / gi=470's interior-m2 blowups are root-caused** — volume of distinct
post-combat memo keys from equipment-attachment permutations the m2 plan cannot depend on;
remedy (memo-key coarsening) designed and deferred in `kitty-interior-m2-tail.md`. The tail is
generation-time only; at shipped d5/b20 both games are unremarkable and the memo never fills.

## The shipping sweep (2026-09-05, this change, both tiers)

Smoke 68 cells: 56 identical, 6 digest-only, 6 avg-moved (worst +0.04). Regression 92 cells: 71
identical, 7 digest-only, 14 avg-moved (worst +0.03). Summed +0.08 / +0.13 turns per tier.
Per-game: smoke 13 slower / 2 faster, regression 22 slower / 7 faster -- and the classifier
(4x/16x joint escalation) decomposes that skew exactly as §3c predicted:

* **26 of 35 slower games are CHURN** (recover to the old turn under escalation): budget
  dilution, one-directional by construction (interior spend only costs at fixed budget). This is
  the whole asymmetry, and it is the class the budget ladder below exists for. Hinata pays most
  (it was never phase-split); its churn root-cause is follow-up item #1.
* **9 persist** -- all on shuffle/scry decks (hinata, fivecolour), symmetric against the 7
  faster games: tie-flip variance. The worst-luck instance is
  `hinata_regression_d3_s3003 gi111` (T8 -> loss): the OLD-vs-NEW diff shows the first
  divergence is the T1 decision itself -- old plays the tapped Mystic Monastery, new passes --
  a candidate tie at d3's horizon broken the other way, then a T4 shuffle makes the game
  physically different. Watched repro; if a pattern emerges, the candidate fix is a dominance
  tie-break ("a free land drop wins ties"), proposed and measured through the loop -- not a
  greedy revert.

Wall: regression makespan 279s -> 375s (+34%), well inside the 45-min budget. Unit 897 SUCCESS,
scenarios 72/72 unchanged.

## What a budget problem is allowed to do about it

USER, 2026-09-05: *"It's fine if we need to add heuristics to cut budget. It isn't fine to start
with something that deletes lines."*

If the searched interior m2 dilutes a deck's fixed budget, the remedies are, in order:

1. **The memo** (`SearchedSecondMainMemoized`, `MTG_SOLVE_MEMO` default-ON): each distinct
   post-combat state searched once per decision. This is what made FiveColour's conversion cost
   +0.7% wall instead of the pre-memo +9% (80% exact repeats).
2. **The depth cap** (`MTG_M2_SEARCH_DEPTH` / `MTG_M2_CAP1`): the interior m2 enumerates its full
   candidate set but scores each with one playout instead of compounding with the
   iterative-deepening pass. Still searched — no greedy pick, no line deleted.
3. Any further heuristic that cuts COST while keeping every legal line enumerated.

What is NOT a remedy, ever: reverting a deck to the greedy interior, or a gate that removes lines
from consideration. (`MTG_NO_M2_SOLVE` remains what it always was — a TEMPORARY measurement lever
for the skip-it-all upper bound, never shipped behaviour.)

### The ladder, driven on hinata (follow-up #1, 2026-09-05 — logs/hinata_m2cap)

Hinata is the deck that pays for the deletion (+0.01..+0.04 across its seven suite cells). Pooled
6-arm sweep, 900 games/arm on those exact cells (base reproduced current GT on all seven — the
apparatus check):

* **Rung 2 (the depth cap) collects NOTHING on hinata: cap1 == base on every cell, exactly.** A
  real null, not a dead lever (under the split arms the same per-job flag visibly changes play).
  Mechanism: hinata's interior spend is dominated by the ~59k d<=0 ONE-PLY searches per 50 games
  (the calls the deletion's d<=0 flip added), with only ~5k deeper calls behind them; the cap can
  only shorten the deep calls, and their full-depth answers agree with their 1-ply answers anyway.
  Corollary: for a deck whose interior profile is d<=0-heavy, rung 2 is not where the recovery is.
* **The phase-split (HINATA_ALL_MAIN2) is NOT a remedy — measured RED, all seven cells, both
  variants** (+0.49/game with the blanket drop, +0.33 with M2_RECONSIDER covering the drop). The
  rejection is recorded at the lever (DecisionProviders.h). It amplifies the very spend it was
  hoped to restructure.
* Rung 1 (the memo) already runs at 47.4% on hinata vs FiveColour's 80%; with the d<=0 calls all
  keying at depth 1, the misses are genuinely distinct states per decision epoch, not the
  depth-fold — there is no cheap key fix hiding there.
* **The remaining rung is budget itself, and it CLOSES the case** — the cost the USER already
  accepted ("the change is even expected to lose on performance"). The 2x/4x curve on the same
  900 games (logs/hinata_budget): total old-GT 5.6800 / current-1x 5.7011 / **2x 5.6689** / 4x
  5.6544. At 2x the searched interior is BETTER than the greedy era ever measured, every cell
  equal-or-better than old within one game; 4x adds only −0.015 (diminishing — 2x is the knee).
  So the deletion's quality story on hinata ends as: nothing was lost, the interior spend just
  needs covering. **ADOPTED 2026-09-05: hinata `value_play.budget_ms` 20 → 40.** Held-out confirm
  (overnight seeds 4004–7007, d5 + 2hg-d5, 1600 paired games/arm, logs/hinata_heldout): 6 of 8
  cells better, 2 exactly equal, none worse, −0.0156/game. GT is untouched — the suite's gate
  cells pin cli budgets, verified byte-identical post-adoption on all four smoke cells (digests
  match GT exactly, incl. the value_play-driven d5 cell). Cost: ~2x search wall on hinata's
  real-play games only — the accepted trade.

## What stays a provider decision

Per the USER (2026-09-05): skipping a main is acceptable only as an explicit opt-in, and that is a
provider decision — deck-specific judgment adopted through the standard loop (propose variants,
measure on the harness, report, adopt in the archetype provider on the USER's approval). Two hooks
remain on that surface, both defaulting conservative:

* `SkipsUnproductiveSecondMain()` (default false): opt-in to `SecondMainUnproductive`'s
  skip-the-solve gate.
* `SearchesRolloutSecondMain()` (default false = greedy leaf estimator): the ROLLOUT site's
  playout policy. Greedy here is by DESIGN, not by neglect — the leaf estimator is a scoring
  device, not a decision ("OPTIMISTIC where you BRANCH, HONEST where you SCORE"), and
  Anti-Lifegain measured searching it at +12 turns/3000 games (d3) with the cost NON-MONOTONE
  (greedy is an interior optimum). The USER's ruling stands: rollouts being greedy is fine.
  Structural guard preserved: the rollout site at `depth <= 0` stays greedy even for an opted-in
  deck — rescuing it would recurse without a decrementing bound.

## What was deleted

* `DecisionProvider::SearchedSecondMainInSearch()` and every override (KittyEquipment's
  unconditional `true`; Anti-Lifegain's `MTG_AL_SSM`-gated one; FiveColour's `MTG_5C_SSM`-gated
  one). Their measured evidence is what justifies the flip: Kitty four arms byte-identical
  (digest 3e6ea44e9c15d572), AL branch site byte-identical over 26,000 games, 5C digest-only at
  identical averages.
* `GreedySecondMainEnabled()` / `g_search_second_main`, and the levers `MTG_SEARCH_SECOND_MAIN`,
  `MTG_NO_SEARCH_SECOND_MAIN`, `MTG_SSM_SITE`, `MTG_M2_D0_SEARCHED` (+ heurarm slots
  `NO_SEARCH_SECOND_MAIN`, `AL_SSM`, `SSM_BRANCH_ONLY`, `M2_D0_SEARCHED`). Kept: `AL_SSM_ROLLOUT`
  (rollout-site policy), `M2_CAP1` / `MTG_M2_SEARCH_DEPTH` (budget levers).
* `AntiLifegainProvider::SearchesRolloutSecondMain` keeps its decline (now
  `heurarm::Flag(AL_SSM_ROLLOUT) && AlPhaseEnabled()`), no longer chained through the deleted
  branch hook.

## Stale after this change (listed, deliberately not rewritten)

* `test/tools/kitty_ab/gen_manifest.py`, `gen_escalate_manifest.py`, `gen_m2d0_manifest.py` —
  concluded-A/B archives that emit now-deleted levers; they document past method and will not run
  against this engine.
* The historical narrative in `second-main-greedy.md`, `searched-second-main-adoptability.md`,
  `antilife-main-phase-split.md`, `analysis-KittyEquipment.md` — history docs; superseded on
  policy by THIS file. `greedy-in-the-searched-window-status.md` and
  `searched-design-deck-rollout.md` §3b updated to point here.

## The EXECUTOR half (2026-09-05, later the same day — found by another agent's report)

The census tables above cover the search interior; the last greedy DECISION was not in the
search at all. When a COMMITTED plan reached a breakpoint occurrence carrying no searched
continuation (`bp_choice < 0` — a base plan that won the turn and then hit a breakpoint no
variant targeted), the executor ran a greedy `Solve()` for the rest of the turn — real play,
real decision, greedy. Deck-shape dependent: 8 of 50 Melira games, 0 of 50 hinata (which is why
a hinata-only census missed it).

**Fixed the same day:** both executor fallback sites (the main breakpoint applier and the pod
trailing-pass twin) now run a full SEARCHED re-solve at deck settings (`SolveWithLookahead`,
deck depth/budget, shared TT). Verified on Melira: `breakpoint-fallback=0 (base=8 ...
searched-resolve=8)`. The rollout twins keep greedy (playout scoring, the tolerated scope), so
realized-vs-scored can diverge on these continuations — in the conservative direction only (the
realized continuation comes from a strictly stronger solver than the one that scored it).
`MTG_EXEC_BP_SEARCHED=0` restores the greedy. The census's `why_kind` table (added with this
change) attributes every unresolved continuation class by ROOT/REC/RESUME apply kind, so
"zero greedy decisions" is a measured number for any future deck, not an assertion from the
nohost table alone.

## The ONE sanctioned greedy construction: a verified this-turn combo go-off

USER, 2026-09-05: *"The only exception I can think of is perhaps that we might want a greedy combo
go-off implementation for complex combos like EDF. However, those are strictly acceptable when
they win this turn."* -- *"So, if they fail to do so, they would fall back to search."*

That is the go-off short-circuit family (Dragonstorm's storm line; EDF's blink line), and its
invariant is exactly the user's boundary:

1. The combo line may be GREEDILY CONSTRUCTED (the recognizer's arithmetic, not a search).
2. It is TAKEN only on a VERIFIED this-turn win: re-simulate via ApplyPlanDirect and
   short-circuit only when OpponentHasLost holds on the resulting state -- never on the
   projection alone (the EDF Gorge repro is why: the colour-blind projection claimed a k=16
   lethal the apply realises as 4 damage).
3. On a non-win it RESTORES best/best_mask byte-identically and falls through to the full
   search; the greedy line's score is discarded, so no greedy judgment leaks into ranking on
   building turns.

A this-turn win is the objective's minimum, so everything the short-circuit skips is dominated --
the greedy construction never decides anything that is not provably optimal.

## What remains greedy inside the search (measured, not assumed)

The `greedysite` counters (`MTG_M2_YIELD_STATS`) count every remaining greedy `TurnSolver::Solve()`
reached from inside the search. Census on THIS binary (hinata, the bp-heaviest deck, 50 games):

```
M2 SITE:  BRANCH searched 4988 / greedy 0 / d<=0 59151 (all SEARCHED)
EXECUTOR: greedy Solve breakpoint-fallback=0 | REAL main-phase decisions: NONE
bp-continuation nohost by apply kind: [rollout] + [rollout+rec] = 100%  (no ROOT kind)
```

**The searched window is greedy-free.** Every residual greedy fire is PLAYOUT-side: the rollout
leaf estimator (above), `SolveWithLookahead`'s `depth<=0` base case (site 90 -- the playout policy
itself), and the breakpoint-continuation fallbacks inside plain rollout applies -- whose
DECISION-side half was already deleted by the 2026-09-02 TIGHT sound recipe
(`bp-greedy-continuation-deletion.md`; canon covers root enum, node resume, captured applies).
The playout scope is the USER's own ruling, twice: rollouts may stay greedy because budget
recovers playout deficiencies, and only the searched structure cannot be budget-recovered.
Re-opening the playout side = `MTG_BP_CANON_REC` (measured -0.007t hinata for +12% wall) or,
cheaper, the incremental-key project.
