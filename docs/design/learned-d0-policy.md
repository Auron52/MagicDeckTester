# Learned d0 evaluator — non-clairvoyant policy distillation

Status: **in progress** (Phase 1). Owner-facing design; the executable plan lives here so it is
durable and shared across machines/agents (per CLAUDE.md's `docs/design/` rule).

## Problem

The engine's fast decision is **d0** — `TurnSolver::Solve()` enumerates legal action subsets
(`CollectActions`), scores each by summing per-card `EvalCard()` static values plus an exact lethal
check, and returns the best `Plan` by the key `(wins, value, smallest_mask)`. `Solve()` is used
**twice**: as the depth-0 fast play policy, *and* as the rollout/leaf policy the deeper search
bottoms out into. So one hand-tuned heuristic (`EvalCard`) is the judgment behind both.

We want to replace that judgment with a **learned evaluator** distilled from the expensive
deep-search oracle (`FullSearchLine` / `EnumerateEarliestWins`, which give a per-plan win turn at a
chosen depth/budget), so we get search-quality decisions at d0 speed, **non-clairvoyantly**.

Scope: **mid-game play only.** Mulligan/bottoming already have a solution (`KeepModel` /
`ExhaustiveKeepPolicy`) and are explicitly out of scope here.

## The insight that drives everything

The clairvoyance problem lives in the **teacher/labels**, not the student. The deep search reads
the real shuffled library order (`state.players[i].library`), so its raw "best turn" can be a
high-variance line that only pays off in that one known future — the artifacts the
`MTG_SHUFFLE_SALT_SEARCH` instrument already catches. The student is non-clairvoyant **by feature
construction** (it never reads library order or the opponent's hand). The real work is
**de-clairvoying the labels**: label each plan with its *expected* win turn averaged over K sampled
futures, not its win turn in one known future. That converts a clairvoyant oracle into a sound
non-clairvoyant target — and it matches the project's primary metric (avg win turn, losses =
max_turns+1).

## Product decisions (settled with the user)

- **Serves both uses.** Satisfied for free: the seam is inside `Solve`/the search's plan ranking,
  and `Solve` *is* both the d0 policy and the rollout leaf.
- **Determinism is a testing requirement, not absolute.** Preferred: byte-identical digests (keeps
  the regression harness + cross-machine reference pooling working). Acceptable fallback if a
  heavier model clearly wins: **stable aggregate win-turn metrics** ("win turns aren't constantly
  changing"), validated by a repeated-run stability test. Escalation past the deterministic model is
  gated on measured headroom + a user discussion.
- **Model language is free but must be in-process C++-callable and fast.** Because it runs at every
  search leaf, an out-of-process call is a non-starter — this alone argues for a cheap model at the
  leaf, independent of determinism.

## Integration (grounded in the code)

**Strategy: learned plan *scorer*, reuse the enumerator.** Do not emit `Plan`s from a policy head
(combinatorial, data-hungry). Keep `CollectActions` + every provider legality/pruning hook, and
replace only the scalar the tie-break ranks on. The exact lethal check stays (rules-correct; the
model never learns lethality and can't hallucinate a win) — the model ranks only *non-lethal* plans.

Every `Plan::value` consumer is **ordinal** (`>`, `!=`) — move-ordering and equal-win-turn
tie-breaking (`TurnSolver.cpp` lines ~81, ~5135, ~6151). None use its magnitude as a threshold. So
overwriting `value` with the learned score at the ranking choke is clean and correct, and
byte-identical when the model is absent.

Two seams:
- **Seam A — `TurnSolver::Solve::consider`** (~[TurnSolver.cpp:1474](../../src/ai/TurnSolver.cpp#L1474)):
  compute `rank_value` = learned score for non-lethal plans when a model is attached and the A/B flag
  is on, else `total_eval`; use it in the `(wins, value, mask)` comparison and store it in
  `best.value`. This covers **both** stated uses (d0 play + rollout leaf).
- **Seam B — the full-depth search's root ranking** (post-loop over the `all` vector right before the
  sort at ~[TurnSolver.cpp:5131](../../src/ai/TurnSolver.cpp#L5131)): set `plan.value` = learned score
  for non-lethal plans. Flows to the equal-win-turn tiebreak at ~6151. A refinement layered after A
  is proven.

**Model handle.** A non-owning `const MidGameEvaluator* GameState::m_evaluator`, threaded exactly
like `m_required_pieces` (stamped in `AIEngine::HandleMulligan`, propagated through deep copies,
**never** folded into `BuildSimKey`). Not a `DecisionProvider` hook — providers are stateless
process-lifetime singletons and can't carry per-deck weights.

**Types (`src/ai/KeepModel.h`, mirroring `KeepScore`/`KeepFeature`).**
- `enum class MidGameFeature` — append-only, name-mapped (reorder = silently mis-evaluate a shipped
  model, exactly the `KeepFeature` contract).
- `struct MidGameEvaluator { std::vector<long long> coefs; long long intercept; long long Score(feats); }`
  — **fixed-point integer dot product** (associative, byte-identical across platforms; no
  float-in-the-argmax). Higher = better; the trainer fits toward `-win_turn`.
- `struct MidGamePlanSummary` — POD integer summary of a plan's board effect (spells, creatures,
  direct damage, total MV, plays-land), so the featurizer stays decoupled from `TurnSolver::Action`.

**Featurizer (`src/ai/KeepModel.cpp`, `ExtractMidGameFeatures(state, plan_summary)`).** Integer-pure,
non-clairvoyant, in `mtg-core` alongside `ComputeKeepFeatures`. Because the Phase-2 label dump hook
lives *inside* `Solve`/`consider`, the featurizer is called from the same site for both training-row
emission and runtime inference — lockstep is trivial (no train/serve skew).
- **Reads (all integer, all public):** both life totals; hand size + castable summary; active-player
  battlefield (tapped, counters, effective P/T, summoning sickness, mana sources by colour);
  graveyard/exile counts; **library size only**; mana available; turn; on-the-play; the plan summary.
- **Excludes (non-clairvoyance contract):** `players[i].library` order/contents, opponent hand,
  opponent library, the analysis-only shuffle-salt fields.

**A/B gate.** `UseLearnedEval()` reads `MTG_EVAL_MODEL` once (mirrors `DecisionUnpruned()`),
**default off** during development so every existing GT stays byte-identical even once sidecars
exist. Adoption (later) flips the default to on-when-present, like `keep_model`.

**Sidecar.** Per-deck `decks/<name>.eval.json`, `eval_model` on `MulliganProfile`, JSON keyed by
feature name (`ScoreToJsonObj` pattern), attached by `AttachEvalSidecar` (mirrors
`AttachExhaustiveSidecar`) at the `BatchRunner` + `main.cpp` play sites — never the analyzer's
rollout loads.

## Label generation (Phase 2)

Env-gated dump hook (`MTG_DUMP_EVAL_ROWS`, default off, zero overhead) inside `Solve`/`consider`:
at each real decision, for each enumerated plan emit `(features, plan_summary, label)` to `logs/`.
**Label = expected win turn over K sampled futures**: reuse the per-candidate oracle
(`EnumerateEarliestWins`) wrapped in the K-reshuffle salt-averaging the mulligan trainer already uses
(`MTG_KEEP_ROLLOUTS`, `ShuffleEvalGuard(true)`). Depth/budget/K are the label-quality knobs.
**DAgger**: round 0 states from d0 play; later rounds from the student's own play, re-labelled and
retrained, until held-out win-turn stops improving (fixes covariate shift).

## Training + "how much data" (Phase 3)

Distillation ⇒ data is **compute-bound, not availability-bound** (the overnight harness already emits
thousands of games × many decisions). Binding constraint = label quality (de-clairvoyed
depth/budget/K) + features, not row count. Answer volume **empirically** via learning curves
(held-out win-turn regret vs. #rows {1k,5k,25k,100k} and vs. label depth/budget); adopt the knee.
Prior: fixed-point **linear** saturates ~1k–10k rows; **GBDT** ~10k–100k. Trainer fits linear first,
then GBDT, quantizes to fixed-point, writes the sidecar.

## Validation (Phase 4)

- **Quality** via the regression harness A/B (train seeds) + overnight held-out: learned vs. d0
  whole-game avg-win-turn at d0 cost; and search-with-learned-leaf vs. -d0-leaf at fixed
  depth/budget, plus whether the learned leaf reaches the same quality at *lower* depth/budget.
- **Non-clairvoyance:** featurizer field audit + `MTG_SHUFFLE_SALT_SEARCH` decouple run shows no
  collapse.
- **Determinism/stability:** off-arm reproduces GT byte-for-byte (the permanent gate); on-arm
  byte-identical for a fixed-point model, or (if we escalate to a float NN) a repeated-run stability
  test replaces byte-identity.

## Escalation (Phase 5, conditional)

Only if the linear/GBDT learning curve shows real regret vs. the oracle: escalate to a float NN
(any language, fast in-process C++-callable inference), mind the leaf-eval cost, adopt the
aggregate-stability test, report the tradeoff, adopt on approval (per the heuristic-optimization
skill).

## Implementation status & findings (session log — read this first when resuming)

**Committed & verified:**
- **Phase 1** (`7e40508`): full integration scaffold — inert, **byte-identical** (smoke 18/18 +
  regression 30/30, digests exact, 0 play-changed). Types + featurizer + both seams + sidecar + gate.
- **Phase 2** (`3c999f4`): `MTG_DUMP_EVAL_ROWS` label dump (de-clairvoyed, K-reshuffle) +
  `SummarizePlanByNames` (one canonical name-based summary shared by seams and dump → no skew).
  Still byte-identical by default. `direct_damage` is 0 for v1.
- **Trainer**: `scripts/train_eval_model.py` — pure-Python ridge (no numpy), stores NEGATED
  fixed-point coefs so `Score = -predicted_win_turn` (higher=better). `--learning-curve` mode.

### SOLVED (2026-07-08 overnight): the collapse was the TRAINING OBJECTIVE, not features/labels/data

The v1 collapse (d0 = 11% vs baseline 86%) was **misdiagnosed** as weak labels + covariate shift.
A systematic ablation refuted every hypothesis except the objective:

| Change | d0 win% (seed 2002, 500g) | verdict |
|---|---|---|
| v1 ridge-regression, 25 feats | 11% | the collapse |
| + DAgger (on-policy states) | 11% | covariate shift **ruled out** |
| + 8 richer plan features (v2) | 11% | feature coarseness **ruled out** |
| + within-decision centering | 11% | still regression → no help |
| **→ pairwise RANKING objective (v2, `--rank`)** | **87.2%** | **fixed** (baseline 86.8%) |

**Root cause (found by tracing one game).** `FSLineTail` is already a full win-turn-minimizing
B&B — the labels are near-optimal (cast=5.56 < land=6.28 < pass=6.60 expected win turn). But **ridge
regression on absolute win-turn does not optimize the within-decision ranking**: over collinear,
coarse plan features it splits the "casting is good" signal and picks up a confounding positive
`draw_engine`/`total_mv` term (casting a setup spell *correlates* with "not yet won"), so the net
makes developing look like it *delays* the win. Result: the model scores the **do-nothing/pass plan
highest and durdles** — in the traced game it played 2 lands then passed every turn T3–T8, opp
stayed at 20. Four different regression models (v1, +DAgger, v2, v2-centered) gave the *identical*
11%/22-win trajectory — the signature of a structural (objective) fault, not a fine-ranking one.

**The fix** — `train_eval_model.py --rank`: pairwise learning-to-rank. Within each decision, anchor
on the oracle-best candidate (min label) and push it to outrank every other candidate, weighted by
the win-turn gap (logistic loss, GD, feature-standardized, folded back to fixed-point). This
directly encodes "cast beats pass" and is what d0 argmax actually needs. Serving is unchanged
(`Score = coefs·feats`, higher = better); intercept is 0 (it cancels in every pairwise diff).
Stability note: keep `lr·lam` small — `lr=0.3, lam=0.001` converges; `lr·lam≈1` flips `w` each epoch.

**Validated results (v2-rank, `logs/eval/th_v2rank.eval.json`, trained on 11.7k rows @ seed 20000):**
- **d0 standalone**: seed 2002 = **87.2%/5.552** (baseline 86.8%/5.558); seed 3003 = **87.0%/5.574**
  (baseline 85.8%/5.620). **Slightly beats** the hand-tuned baseline on both held-out seeds.
- **d3 search-leaf**: **99.0%/4.150** (baseline 99.0%/4.145) — tied. Works as **both** uses.
- **Deterministic**: identical game outcomes across thread counts (fixed-point integer dot product).
- **Data sufficiency (ranking)**: held-out pick-accuracy saturates by **~100–200 decisions (~1–2k
  rows)** and is flat after — **capacity-bound, not data-bound**. (Also: pick-accuracy is only ~42%
  yet play matches baseline — what governs win% is *avoiding the catastrophic pass-durdle*, not fine
  pick precision. So global RMSE / pick-accuracy are poor proxies; the game A/B is the real metric.)

**Interpretation of the d0→d3 gap (87%→99%).** Most of it is **clairvoyance** — the deep search
reads the real library; a non-clairvoyant d0 fundamentally can't. So *matching/slightly beating the
non-clairvoyant baseline is near the real ceiling* for standalone d0, and the label de-clairvoying
(K-reshuffle) is doing its job. Don't chase the full gap at d0.

**Ruled out / negative results (don't repeat):** DAgger *hurt* when added to the ranking model
(87%→62%, over-fits the on-policy distribution); richer features alone did nothing under regression;
within-centering didn't help under regression. The **strong-label pivot (per-turn re-search / d7)
is unnecessary** — `FSLineTail` labels were never the problem (they're a full search; the objective
was). That whole Phase-3b plan can be shelved unless a *harder* deck shows label-limited behavior.

### Generalization test — 3 decks, 2026-07-08

Ran the whole pipeline on three decks spanning archetypes. Result maps the method's reach precisely
(win% / loss-penalized avg win turn; A/B seed 2002):

| deck | complexity | learned **d0** vs baseline | learned **d3 leaf** vs baseline |
|---|---|---|---|
| Treasure Hunt | value/combo | **87.2%/5.552** vs 86.8%/5.558 — matches/beats | 99.0%/4.150 vs 99.0%/4.145 — ties |
| burn | aggro, tuned provider | 99.0%/4.91 vs 98.0%/4.65 — wins as often, **~0.2t slower** | 100%/4.320 vs 100%/4.325 — ties |
| Anti-Lifegain | combo (assemble pieces) | **30.8%**/8.24 vs 90.0%/5.65 — **much worse** (pairwise acc 57%) | 100%/4.060 vs 100%/4.060 — ties |

Three robust conclusions:
1. **The ranking objective is the fix and it generalizes as an objective** — no deck reverts to the
   regression durdle; every deck's ranker learns "cast beats pass."
2. **Search-leaf use is a SAFE drop-in on every deck** — at d3 the learned leaf ties baseline on all
   three (the search absorbs/corrects the linear model). Honest caveat: on converged decks "ties"
   partly means the leaf is *inert* (the search finds lethal regardless), so the claim is **never
   harms + deterministic**, not yet "improves". Whether a learned leaf enables *cheaper* search (same
   quality at lower depth) is the untested, high-value follow-up.
3. **Standalone-d0 quality tracks deck complexity; the ceiling is LINEAR CAPACITY.** Simple
   value/combo (TH) matches/beats baseline; tuned-aggro (burn) is ~0.2t slower (its hand-tuned
   `BurnProvider` sequencing is beyond a generic linear ranker); assemble-the-combo (Anti-Lifegain —
   lethal needs a *conjunction* of pieces a linear sum can't represent) drops to 30.8%. Only TH's
   sidecar is ship-worthy at d0; burn/antilife are kept in `logs/eval/` (uncommitted).
   A naive **shared** cross-deck linear ranker (TH+burn pooled) *collapses both* (10%/0%) — the decks
   demand opposite strategies, so one linear coefficient set serves neither. Per-deck (or nonlinear +
   deck-conditioned) is required.

The burn gap motivated **`plan_face_damage`** (v3, committed `d606e0c`): the summary punted
`direct_damage→0`, so non-lethal face burn was invisible (only the exact lethal check saw kills).
Summing *fixed* burn (`params.damage`; X excluded) helped burn only marginally — confirming the gap
is model **capacity**, not a missing feature. Inert for TH (variable Land's-Edge burn → d0 identical).

**Remaining levers (optional; ordered by expected value):** (a) **fixed-point GBDT** — the honest
capacity fix for tuned-aggro d0 (linear saturates ~150 decisions; nonlinearity is the lever, not more
data/features); (b) **interaction features** (draw-engine/face-damage × board context) as a cheaper
half-step; (c) a **shared cross-deck** ranker (the real payoff — one learned evaluator replacing
hand-tuned `EvalCard` everywhere). Priority per user: standalone-d0 first, both eventually.

**Artifacts:** trainer `--rank`/`--center` (`rank_fit`/`within_center`); harness `scripts/eval_ab.py`
(loss-penalized A/B) + `scripts/eval_regret.py` (within-decision pick-accuracy); v3 featurizer adds
8 append-only non-clairvoyant features (plan_cards_drawn, plan_noncreature_spells, plan_max_cast_mv,
**plan_draw_engine**, **lands_in_hand**, mana_left_after, taps_out, **plan_face_damage**). Winning TH
model shipped inert at `decks/treasure_hunt.eval.json`; rows persist under `logs/eval/`
(th_v3.rows / burn_v3.rows are the current training sets; regenerate a model via
`train_eval_model.py --rank`).

## The permanent regression gate

At every phase: with **no sidecar or `MTG_EVAL_MODEL` unset**, smoke + regression digests are
**byte-identical** to committed GT. The seam defaults `rank_value = total_eval`; the sidecar is
presence-gated; the dump hook is env-gated. This "no-op reproduces GT" property is what makes the
whole feature safe to land incrementally.
