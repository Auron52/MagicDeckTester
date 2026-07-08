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

**Empirical results (Treasure Hunt, trained on seed 20000, tested on 2002):**
- **Learning curve**: held-out RMSE flattens at ~2–4k rows (0.92→0.89 turns). **Answers the data
  question**: a linear model saturates at ~2.5k rows; the ~0.89-turn floor is set by *feature
  coarseness*, not data quantity (many candidate plans share features — e.g. land-only turn-1 plans
  differ only by which land, which the v1 summary doesn't encode).
- **A/B**: d3 learned = **100% / 4.17** (≈ baseline 100% / 4.15 — safe but near-**inert**; the
  search overrides the model). d0 learned = **7% / 6.14** (baseline 86.7% / 5.15) — **catastrophic**,
  identical under `MTG_LEGACY_SEARCH` (so it's `Solve` itself, not the full-depth leaf).
- **Diagnosis (NOT a bug)**: on its *training* distribution the model's rank-regret is only 0.285
  turns and it correctly favors casting (within-decision label slope −0.39). The d0 collapse is the
  **train/serve gap**: weak `FSLineTail` labels + **covariate shift** (a round-0 model trained on
  baseline-d0 states plays into flooded states it never saw and compounds). d3 masks it because the
  search's rolled-out win-turn dominates the (learned) value tiebreak.

**Next step — stronger labels (user directive): "learn from better paths used in searched
clairvoyant play."**
- `EnumerateEarliestWins` labels each candidate via an **`FSLineTail`** (commit-the-line) tail —
  the "default search path" flagged as inefficient. Replace it with a **per-turn re-search**
  continuation, ideally at **high depth/budget (~d7 / budget 10000)** for the "most correct" label
  (user's words); cost is the main risk, so make label depth/budget env-configurable and validate
  cheaply (moderate depth, few games) before scaling.
- **API constraints found**: `AIEngine::RolloutWinTurn` is **private**; `EnumeratePlansWithLand` /
  `ApplyPlanDirect` are **not exposed**; `GameEngine::PlayOut`→`RunTurn` does `++turn; UntapStep…`
  so it **cannot start mid-turn**. Therefore the strong continuation must resume **post-combat** the
  way `FSLineTail` does. Cleanest design: add an optional continuation callback
  `std::function<int(GameState&)>` to `EnumerateEarliestWins` (default null → today's `FSLineTail`,
  byte-identical); `AIEngine` passes a lambda backed by a **separate high-depth labeling `AIEngine`**
  (so `MTG_EVAL_MODEL` stays OFF during labeling and the real game's `m_committed_line` isn't
  touched). Gate the strong path behind e.g. `MTG_DUMP_EVAL_ROWS_STRONG=1`.
- **Also needed** (independent of labels): (a) **DAgger** — relabel on the *student's own* d0 states
  and retrain, to close the covariate-shift collapse for standalone-d0; (b) **richer features** —
  encode the plan's specific effect (per-card / post-plan board) so plans that differ only in *which*
  card/land are distinguishable (the 0.89 RMSE floor).
- **Priority (user)**: optimize **standalone-d0 first**, but keep it capable of **both** uses.
- The bad v1 TH sidecar was NOT committed (regenerate from `scripts/train_eval_model.py`); training
  rows persist at `logs/eval/th_train.rows`.

## The permanent regression gate

At every phase: with **no sidecar or `MTG_EVAL_MODEL` unset**, smoke + regression digests are
**byte-identical** to committed GT. The seam defaults `rank_value = total_eval`; the sidecar is
presence-gated; the dump hook is env-gated. This "no-op reproduces GT" property is what makes the
whole feature safe to land incrementally.
