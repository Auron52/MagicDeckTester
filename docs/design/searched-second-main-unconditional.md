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
deletion ships now, and the cost work continues on top of it. **Open follow-up (budget
heuristics, the allowed class): root-cause KittyEquipment gi=231 / gi=470's interior-m2 blowups**
so the dilution goes to zero instead of merely being small.

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

## What remains greedy inside the search (measured, not assumed)

The `greedysite` counters (`MTG_M2_YIELD_STATS`) count every remaining greedy `TurnSolver::Solve()`
reached from inside the search. After this change the expected residue is: the rollout leaf
estimator (by design, above), `SolveWithLookahead`'s `depth<=0` base case (site 90 — the playout
policy itself), and the BREAKPOINT-CONTINUATION fallbacks (sites 0-7, where `bp_searched_plan`
returns false). The bp continuations are the one remaining greedy DECISION class; converting them
is tracked separately (see `bp-greedy-continuation-deletion.md`) and must clear the same bar:
budget heuristics allowed, line deletion not.
