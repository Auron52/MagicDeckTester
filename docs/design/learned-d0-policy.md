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

### GBDT capacity experiment — 2026-07-08 (committed `3610648`)

Built a **fixed-point ranking GBDT** (integer-threshold splits on integer features, integer leaves →
byte-identical serving; `MidGameEvaluator.trees`, trainer `scripts/train_eval_gbdt.py`, pairwise
LambdaMART-lite). Verdict: a real capacity lever, **not** a uniform upgrade.

| deck | linear d0 | GBDT d0 | note |
|---|---|---|---|
| Anti-Lifegain | 30.8% | **54.8%** | capacity helps where linear badly underfits (combo) |
| burn | 4.91t | 5.53t (worse) | linear was the sweet spot; pure GBDT overfits / covariate-shifts |
| Treasure Hunt | 87.2% | ~87% | neutral |

- **Model class is deck-dependent** and must be tuned on **game A/B**, not pairwise accuracy (burn hit
  85% pair-acc yet played *worse* — pair-acc doesn't see sequential covariate shift). Hybrid
  (linear-init + trees) recovered burn (~linear) but *destabilised* antilife (collapse) — not robust.
- **Nonlinearity ENABLES a shared cross-deck model** (the headline). A naive *shared linear* ranker
  collapses (TH 10% / burn 0%); a **shared GBDT** plays both — **TH 81% / burn 95% at d0, and ties
  baseline at the leaf (d3) for both**. A 3-deck shared GBDT (adds antilife) degrades (60/94/40%) —
  fixed capacity dilutes with more decks — but never collapses. So the "one learned evaluator for many
  decks" vision is *reachable with nonlinearity*, impossible with linear.
- **Even at best, no learned model reaches the hand-tuned baseline on burn/antilife at d0.** The
  residual gap is **combo/sequencing feature-completeness** (e.g. "plan assembles a lethal combo",
  "controls the pieces"), not model capacity. Next lever: combo-aware features, or expose the
  baseline `EvalCard` plan value as a feature (learn to *augment* the tuned heuristic, not replace it).

Determinism verified (identical across thread counts). Nothing activates trees by default; TH still
ships the linear sidecar. GBDT models for the 3 decks live in `/tmp` (regenerate via
`train_eval_gbdt.py`); training rows are `logs/eval/*_v3.rows`.

### Leaf VALUE model — measured result (2026-07-08): same quality, ~10–15× cheaper search

The uncommitted WIP is a **second, distinct** learned model: a leaf **value** model
(`MTG_VALUE_MODEL`, `value_model` sidecar, `GameState::m_value_model`) that REPLACES the search's
horizon rollout in `FSLineWin` (`depth<=0`) with an O(1) predicted win turn. This is different from
`eval_model` (which ranks plans at the `Solve` tie-break) — it attacks the doc's flagged high-value
follow-up: "does a learned leaf enable *cheaper* search?" Answer, measured: **yes.**

Trained a fixed-point **regression** GBDT (`train_eval_gbdt.py --regression`, label = searched win
turn) per deck. A/B vs the exact rollout (`scripts/eval_ab.py --value-model`, `threads=1` timing):

| deck | baseline (exact rollout) | value-leaf (GBDT) | speedup |
|---|---|---|---|
| TH d5, 150g s2002 | 54.4s → 98.7%/4.081 | **3.8s → 98.7%/4.081** (identical) | **~14×** |
| TH d5, 150g s3003 | 55.1s → 96.0%/4.014 | 5.5s → 96.0%/4.035 | ~10× |
| burn d3, 200g s2002 | 111.2s → 100%/4.325 | **7.6s → 100%/4.335** | **~15×** |
| burn d5, 200g s2002 | (minutes) | 24.8s → 100%/4.32 | — |

**Findings.**
1. **Same quality, dramatically cheaper.** The value-leaf matches baseline win%/avg-turn at adequate
   depth (d5 for TH, d3 for burn) at ~10–15× lower wall-time. The rollout was the slow link; the
   O(1) value skips a full greedy playout at every (usually non-decisive) leaf. Generalizes across
   both archetypes tested (value/combo + aggro).
2. **It's a SPEED win, not a quality win.** The value-leaf ties, never beats, the exact rollout —
   because for these decks the leaf is often inert (search resolves lethality within the horizon). At
   *shallow* depth, where the leaf is decisive, the model is slightly WORSE than the rollout (TH d1:
   92% vs 98%; GBDT >> linear there). So it's "same quality, cheaper at adequate depth," not a
   universal rollout replacement.
3. **GBDT >> linear for the value model too** (TH d1: 92% vs 80.7%). Linear win-turn regression is
   too coarse a leaf.
4. **Deterministic** across thread counts (fixed-point GBDT) — keeps the regression/pooling gates.

**Why this matters / next:** the analyzer's cost is dominated by these rollouts (skill 5f), so a
~10–15× cheaper leaf directly funds deeper search / bigger mulligan grids in the same overnight
window.

**Generalization — all 5 decks confirmed (2026-07-08), value sidecars committed (inert-gated).**
Trained a fixed-point regression GBDT per deck and A/B'd value-leaf vs the exact rollout (150g s2002,
threads=1). Every deck matches baseline quality at adequate depth (d5) at a large speedup:

| deck | baseline d3 | value-leaf d5 (quality) | speedup (base-d3 / value-d5) |
|---|---|---|---|
| Treasure Hunt | 18.1s (98.7%/4.081) | 3.8s (98.7%/4.081) | ~14× |
| burn | 111s (100%/4.325) | 24.8s* (100%/4.32) | ~15× (value-d3 7.6s) |
| knights | 22.1s (100%/4.32) | 2.15s (100%/4.32) | ~14× |
| slivers | 40.0s (100%/4.26) | 2.53s (100%/4.26) | ~16× |
| Anti-Lifegain | 38.3s (100%/4.06) | 16.0s (100%/4.06) | ~3–4× (combo leaf costlier) |

Robust pattern: (1) at **adequate depth (d5)** the value-leaf reproduces baseline win%/avg-turn
exactly — because those decks resolve lethality within the horizon, so the leaf is near-inert and the
speedup is pure (skip the expensive playout); (2) at **shallow depth (d3)** it's slightly worse where
the leaf IS decisive (antilife 97.3% vs 100% at d3, recovers to 100% at d5); (3) deterministic across
threads. So it's a same-quality, ~10–15× cheaper search (antilife's combo leaf is harder to value, so
~3–4×). Committed `decks/<name>.value.json` (5 decks), presence-gated + `MTG_VALUE_MODEL`-gated →
byte-identical with the flag off (verified: knights model-off identical with/without the sidecar).
Hinata sidecar pending its dump. Adoption (flip the default on + rebaseline GT to the faster search) is
a deliberate follow-up decision. Artifacts: rows `logs/eval/*_value.rows`; `train_eval_gbdt.py
--regression`.

### d0 lever: `plan_baseline_eval` — augment the tuned heuristic (2026-07-08, `248fd54`)

Implemented the doc's flagged #1 d0 lever: expose the hand-tuned baseline's own plan verdict as a
feature so the ranker learns to *augment* it instead of reconstructing "casting is good" from coarse
proxies. Feature = `Sum EvalCard(def, state)` over the plan's casts, computed by a shared
`TurnSolver::PlanBaselineEval` helper at BOTH the ranking seam and the label dump → lockstep,
non-clairvoyant. Appended (v4); name-keyed sidecars stay compatible; 0 for the null/leaf plan (value
model unaffected); byte-identical with no model.

**Result — burn d0, 300g, held-out seeds: closes ~45% of the gap to baseline.**

| seed | baseline LP | v3 ranker (no feat) | v4 (+plan_baseline_eval) | gap closed |
|---|---|---|---|---|
| 2002 | 4.740 | 4.907 (+0.167) | 4.843 (+0.103) | ~38% (avg-turn 4.879→4.773, ~47%) |
| 3003 | 4.693 | 4.880 (+0.187) | 4.800 (+0.107) | ~43% (avg-turn 4.782→4.685, ~51%) |

Real, replicated. The linear ranker still can't fully match the tuned `BurnProvider` sequencing, but
the feature is a clear step.

**Antilife (combo): plan_baseline_eval does NOT help — as predicted.** On antilife its coef trains to
~0 (inert); the linear ranker still durdles (`plan_num_spells < 0`) and GBDT reaches only ~28% (vs
baseline ~89%). This is the doc's already-identified residual: antilife lethal needs a *conjunction* of
pieces, and `plan_baseline_eval` is itself a linear per-card `EvalCard` sum — blind to the combo, so it
can't represent it. **The real antilife lever is a combo-AWARE feature** ("plan assembles/controls the
lethal set"), not a per-card value. So plan_baseline_eval is a burn/aggro-sequencing win, not a
combo-deck win. (Method note found here: for a COMBO deck the row *dump* must play at a high enough
`--budget-ms` to actually reach combo-assembled states, or the rows lack the informative positions —
budget 200 gave GBDT 10%, budget 2000 gave 28%; aggro decks like burn are insensitive to this.)

### ⚠️ CRITICAL training footgun (cost 3 wrong diagnoses here): default `lr*lam` collapses `--rank`

`train_eval_model.py`'s DEFAULTS are `lr=1.0, lam=1.0` → `lr*lam=1.0`, which is exactly the unstable
pairwise-ranking regime the earlier note warned about (the weight vector flips each epoch, yielding a
**durdling** model with `plan_num_spells < 0` = "cast nothing"). Retraining burn on the SAME rows that
produced 99% gave **53%** with the defaults — a collapse that looks like a bad feature or bad rows but
is **pure hyperparameters**. Diagnosis path that works: A/B the *saved* known-good model vs a fresh
retrain; identical rows + different model ⇒ hyperparameters. **Always train `--rank` with `--lr 0.3
--lam 0.001`.** A guard now warns when `lr*lam >= 0.5`. (Serving was never the issue — the committed TH
model still reproduces 86.7% at d0.)

### ⚠️ Pre-existing branch issue: TH smoke GT is stale (NOT caused by the above)

The `--smoke` gate FAILs `th_smoke_d0/d3/d5` (`regression_gt.txt` expects `150/4.13333/0e6f0a44…`; the
engine model-off produces `144/…/cdc448…`). burn/knights/antilife/hinata/slivers all PASS, and the
featurizer changes here are byte-identical for model-off (proven by those 19 PASSes + the TH `got`
digest being unchanged across the change). So this is **pre-existing GT staleness** — TH ships an
`eval.json` sidecar and its smoke GT is out of sync with the current engine. It predates this session's
work; the learned-d0 owner should re-inspect and rebaseline TH GT deliberately (not from an
uncommitted experiment). Flagged, not silently rebaselined.

### Combo-aware d0 feature for antilife — tried, NEGATIVE, reverted (2026-07-08)

Implemented two combo features for antilife's conjunction (`combo_enabler_active` = a
`lifegain_to_loss` enabler on board; `plan_opponent_lifegain` = Sum of opponent-lifegain the plan's
casts cause — the payoff that becomes damage under the enabler). Both non-clairvoyant, lockstep,
inert (0) for other decks. The hypothesis: a GBDT could split "enabler & plan_opp_lifegain>0" = the
combo firing, representing what a linear sum can't.

**Result: it made antilife d0 WORSE, and is reverted.** GBDT-with-combo A/B'd at **0.7%** (s2002 d0)
vs ~10–28% without. Two findings, both honest:
1. **Exposing the combo conjunction backfires on a NON-CLAIRVOYANT d0.** The oracle labels reward
   *waiting* for the full combo (casting a lone piece early genuinely delays the win), and a clean
   `enabler_active` signal lets the model learn "wait until the combo is primed" — a policy a
   clairvoyant search can afford but a d0 that can't guarantee drawing the enabler cannot. It durdles.
   The hand-tuned baseline (~89%) wins by playing *proactively* instead. So combo-readiness features
   are a trap for d0: they rationalise the durdle. (A *value/search* use, not d0 argmax, might differ.)
2. **My antilife EVAL-row dumps never reproduce the doc's saved antilife ranker** (my 10–28% vs the
   doc's 54.8%/linear-30.8%), across budgets (200→2000) and featurizer versions. The doc's saved
   `/tmp/antilife_rank.eval.json` still serves at ~29% on the current binary, so **serving is fine —
   the gap is dump METHODOLOGY** (seed/K/row-count/how the label search is configured). This is the
   real blocker for any antilife d0 tuning and should be pinned down (diff the doc's exact dump
   command) before more antilife d0 work. Reverted the C++ (kept the featurizer clean); the two
   features are easy to re-add from this description if a value-model or better-rows approach revisits.

## The permanent regression gate

At every phase: with **no sidecar or `MTG_EVAL_MODEL` unset**, smoke + regression digests are
**byte-identical** to committed GT. The seam defaults `rank_value = total_eval`; the sidecar is
presence-gated; the dump hook is env-gated. This "no-op reproduces GT" property is what makes the
whole feature safe to land incrementally.
