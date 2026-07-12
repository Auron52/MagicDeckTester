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

## Why this model — the four motivating uses (settled with the user, 2026-07-09)

1. **An option to greatly improve non-clairvoyant play** (a strong d0 policy — the `eval_model` ranker).
2. **Quick evaluation / testing of a deck without compromising much on quality** (fast d0 ≈ search quality).
3. **Higher-quality and faster rollouts** (the `value_model` leaf — O(1) win-turn vs a greedy playout).
4. **Reduce time spent by search: search only needs to go up to the turn where the model predicts a win**
   (use the value model's predicted win turn as a search depth/cutoff bound). *Not yet implemented — top
   follow-up.*

Eventually (phase 2) this model approach may train decks to play *against each other*. Implication for
scope: the d0 ranker (#1) and the value/leaf model (#2/#3/#4) are equally first-class deliverables — a
"high-quality per-deck solution" means BOTH a non-clairvoyant d0 policy AND a committed value sidecar.

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

### ▶ NEXT STEPS (resume here — 2026-07-09, post value-leaf characterization)

State: all work pushed to `learned-d0-evaluator`. Tree clean, nothing running. This session GROUNDED the
value proposition and CHARACTERIZED the value leaf's depth behavior; tried + reverted a race-clock feature.

**Grounded results (this session):**
- **The value leaf is a same-depth CHEAPER leaf, NOT free depth.** value-leaf d1 is WORSE than heuristic d1
  (TH: value-leaf d1 = 5.33 vs heuristic d1 = 4.22); it only *catches up at equal depth* (value-leaf d5 =
  4.147 = heuristic d5 = d7, within +0.02 on held-out 2002/3003/7007). So it delivers uses #2/#3/#4 (leaf-cost
  reduction) — NOT use #1 "see ahead" (making d1 play like d5), except on depth-flat decks where d1≈d5 anyway.
- **Speedup grounded: value-leaf d5 = 5.5× faster than heuristic d5 on TH** (2.0s vs 11.1s, 100g) at quality
  parity. This is the core deliverable — deep-search quality at O(1) leaf cost.
- **The "aggro residual" (lever #1) is noise on SOLVED decks.** All aggro decks are near-optimal AND depth-flat:
  burn d1=d3=4.325 (d5=4.320), knights d1=4.300≈d5=4.295, slivers d1=4.355→d3=4.285 (mild 0.07). Value-leaf
  matches heuristic at d3 within ~0.02–0.035. There is no meaningful aggro headroom to close.
- **Race-clock feature (`our/opp_clock_to_lethal`, ceil(life/power)) TRIED + REVERTED.** Motivated (a ratio the
  linear leaf can't form), but INERT: burn d1 slightly worse (4.425→4.450), knights unchanged, slivers +1 game.
  Cause = goldfish opponent is passive (no board to race → combat clock has ~no signal) and the model already
  captures the win turn from life/power. No shipped model used it; reverted to keep the feature set clean.

Prior session committed: ROLLOUT LABELS (`MTG_EVAL_ROWS_ROLLOUT` + `rollout_depth`), the search-depth reframe,
the game-reading taxonomy. Value-model-as-feature v1 tried + REVERTED (crude approx non-discriminative).

**The settled picture (metric = avg win turn, loss=9; target = the SEARCH, laddered d1→d3→d5→d7):**
- **Per-deck solution = the value model in a d3–d5 value-leaf search.** It matches or beats d1-heuristic on
  all 5 value-model decks and lands within ~0.03–0.06 of full d5-search, at an O(1) leaf. It also plays SANE
  lines (burn g29: value-leaf d1 == d5-search, holds Shard Volley, wins T5; the d0 ranker dribbles to T8).
- **The pure d0 RANKER is a dead end for search quality** — myopic (can't see a play's multi-turn cost); an
  accurate post-plan value needs plan application = a 1-ply search (so value-model-as-feature buys nothing
  over the value-leaf search). Rollout labels still made it the best *pure-d0* option and fixed antilife/burn
  vs the hand-tuned baseline, but it structurally can't reach a 1-ply search.
- **Gap decomposition (from reading games): learnable sequencing (recoverable by multi-turn value) +
  justified clairvoyance (small, irreducible — antilife 2/120 shuffle games) + indifferent sloppiness
  (recoverable; rollout labels remove it).** Only justified clairvoyance is a true floor.

### ★★ D0 POLICY BEATS THE HEURISTIC — 5/5 decks (2026-07-09, user's priority)

The user's core goal: the model as a **d0 replacement** (a static, fully non-clairvoyant plan ranker — no
playout, no search) that BEATS the hand-tuned `EvalCard` heuristic. Was stuck at parity/losing (knights −0.23).
**Now WINS (or ties) on every deck.** Two fixes:

1. **FAITHFUL ANCHOR (the key fix).** The d0 ranker had no faithful anchor: `plan_baseline_eval` was a
   name-derived `EvalCard` sum over CASTS, omitting the Vial/X/ritual evals the real ranking key (`total_eval`)
   carries. On Vial decks (knights/slivers) the name-based value was off by hundreds, so even a tiny learned
   weight on it ADDED NOISE → the model LOST to the policy it imitates. Fix: at `Solve::consider`, set
   `psum.baseline_eval = total_eval` (the exact key, available right there). Proven: `{plan_baseline_eval:1}`
   now reproduces the heuristic EXACTLY on all decks incl. Vial. Byte-identical when no model attached.
2. **Per-deck label + regularization.** rollout labels for aggro/Vial (burn/knights/slivers), searched for TH
   (combo — rollout durdles it to +0.64; searched-linear needed). Antilife (assemble-combo) over-corrected at
   `lam=0.001` (+0.04); a `lam` sweep is monotonic → `lam≥0.05` WINS (5.52 vs 5.59). searched-linear COLLAPSES
   antilife to 9.0 (conjunction a linear model can't represent — the durdle trap), so rollout+high-lam is it.

**Final d0 scorecard (held-out 2002/3003/7007 / fresh 4004/9009; avg win turn, lower better):**

| deck | heuristic d0 | model d0 | Δ (3-seed) | Δ (fresh 2-seed) |
|---|---|---|---|---|
| slivers | 4.718 | 4.442 | **−0.276** | −0.247 |
| antilife | 5.591 | 5.522 | **−0.069** | −0.140 |
| knights | 4.553 | 4.484 | **−0.069** | −0.020 |
| TH | 5.580 | 5.540 | **−0.040** | −0.047 |
| burn | 4.682 | 4.669 | −0.013 | 0.000 (tie; heur d0 ~optimal, depth-flat) |

The model **never loses** and wins clearly on 4/5. This is a fully non-clairvoyant d0 policy (no playout, no
draw-reading) beating the hand-tuned heuristic — the user's goal #1 in its clean d0 form. Winning per-deck
sidecars in `logs/eval/*_d0_anchor.eval.json` (+ TH's committed searched `decks/treasure_hunt.eval.json`).
Next: adopt (move to `decks/*.eval.json`, flip `MTG_EVAL_MODEL` default, rebaseline GT); the anchor also wants
the ROOT-ranking seam (TurnSolver.cpp:5220) + the eval DUMP made faithful for full train/serve consistency
(currently the tiny baseline_eval weight makes the dump skew immaterial, but fix it before scaling).

**Open next levers (pick by EV):**
1. ~~Close the aggro residual~~ **CLOSED as noise.** Aggro decks are depth-flat + near-optimal; residual ~0.02.
2. **Push value-leaf FIT higher (user's pick).** Race-clock reverted (inert); GBDT-regression a wash (same
   board-only inputs). BUT **v6 DISTRIBUTIONAL features are a real win where library uncertainty drives the game**
   (see the entry below): the board-only leaf read library SIZE but not COMPOSITION, so it couldn't amortise the
   rollout its de-clairvoyed label already targets. Adding the (non-clairvoyant, public) remaining-library
   multiset counts (`lib_lands/creatures/damage_sources/draw_engines/land_density_pct`) cut TH held-out RMSE
   1.145→0.947 (−17%) and improved TH **d1** play −0.54 (5.44→4.90). Inert on board-driven aggro (burn/slivers).
   Open: this is the right INPUT for a non-clairvoyant policy (lever 5); a hand-composition variant may extend it.
3. **★ ADOPTION is now the top lever (needs user sign-off).** Both deliverables GROUNDED (tables above): goal #1
   (value-leaf d5 beats heur d1 on all 5 decks, held-out) and goals #2/#3/#4 (1.6–25× speedup — but see the
   2026-07-11 CORRECTION below: the raw leaf is a WEAK-but-cheap evaluator, d5≈heuristic-d2; the win comes from
   the HYBRID taking verified wins fast + redoing the heuristic where the leaf is weak, NOT matched-depth parity).
   Adoption = flip `MTG_VALUE_MODEL` default on-when-present + rebaseline smoke/regression GT to the value-leaf
   lines (NOT byte-identical — same LP, different plans). Deliberate GT change → user decides. Open sub-question:
   what search depth the adopted harness uses (d3 = fast + ties d1-heur; d5 = beats everywhere).
4. **Goal #4 — search-horizon cutoff:** LOW value; the ID search already early-terminates at the first verified
   win (TurnSolver.cpp:5875) and shallow passes are near-free via shared memo. Model-seeded start = minor.
5. **★ Non-clairvoyant "see-ahead" (the real goal #1) — reshuffle-averaged search.** value-leaf d5 beats d1 but
   is a CHEAP CLAIRVOYANT search (branching reads real draws; see honesty caveat). The principled non-clairvoyant
   policy is a search whose BRANCHING averages over K reshuffled futures + the value leaf — the "mental sim under
   uncertainty" a human does. Machinery exists (label gen already does K-reshuffle search; `MTG_SHUFFLE_SALT_SEARCH`
   decouples search-shuffle from execution). Untested as a PLAY mode. Measures: does reshuffle-avg d3/d5 beat
   heuristic d1? If yes → non-clairvoyant see-ahead is real; if no → non-clairvoyant play is genuinely ~d1-bounded.
   A real (but non-trivial) build; the clearest path to the user's original "greatly improve non-clairvoyant play".
6. **Hinata: DEFERRED** — no value sidecar (searched-value dump pathologically slow) + no mulligan profile yet.

**Reframe for use #1 ("beat d1-search / see ahead") — d1 IS BEATABLE; the target is FIT QUALITY (corrected
2026-07-09, per user).** An earlier draft here claimed d1 was a structural wall for a non-clairvoyant policy.
**That was wrong, on two counts:**
1. **Conceptual (the decisive point).** `d1-heuristic`'s leaf is a *greedy play-out of the myopic d0 baseline*
   policy — a WEAK, non-optimal estimate, not the truth. Better judgment beats it. A human beats d1 exactly
   because their positional evaluation is better than rolling the greedy policy forward. So d1 is not a ceiling.
2. **Empirical.** `SolveWithLookahead(depth<=0)` returns plain `Solve()` (TurnSolver.cpp:6094) — **the value
   model is NOT consulted at d0.** The prior "value-leaf d0" table was measuring the baseline d0, not the model.

**The value model's LABEL is the de-clairvoyed SEARCHED win turn — strictly better than a greedy play-out.** So
a well-fit value leaf *should* beat `d1-heuristic`, whose leaf IS that greedy play-out. The right yardstick is
**value-leaf d1** (identical 1-ply branching to heuristic d1; value leaf vs play-out leaf) **beating heuristic
d1**, with the ceiling being *searched* quality — which sits BELOW heuristic d1 (burn searched≈4.32 < heur d1
4.35; TH searched≈4.15 < heur d1 4.22). value-leaf d1 currently TRAILS (burn 4.45 vs 4.35; TH 5.33 vs 4.22) only
because the current LINEAR value model is a worse leaf than the play-out.

**HOW THE MODEL ALREADY BEATS d1-heuristic — via cheap DEPTH, not a shallow d0/d1 eval (measured, reconciled
with the user 2026-07-09).** The right lens is `value-leaf d5` (the model as the LEAF of a depth-5 search) vs
`heuristic d1`. It WINS on the decks where d1-heur is suboptimal, at COMPARABLE cost:

| deck | heur d1 | value-leaf d5 | Δ | cost heur-d1 / value-leaf-d5 (100g) |
|---|---|---|---|---|
| burn | 4.353 | 4.347 | −0.007 (tie; burn depth-flat, d1 already optimal) | 0.26s / 1.93s |
| slivers | 4.340 | 4.260 | **−0.080 (beats)** | 0.23s / 0.34s |
| TH | 4.220 | 4.147 | **−0.073 (beats)** | 1.78s / 2.10s |

So goal #1 IS met — the model "sees ahead" (d5) for ~the price of d1 because its leaf is O(1), and d5 beats d1.
This is what the earlier "value-leaf d5 beats d1-heur" table measured: **d5 with the model in the leaf nodes,
NOT a d0 model.**

**The FIT lever (GBDT / richer leaf) is a WASH for the shallow-depth goal (measured).** GBDT-regression value
model ≈ linear at every depth (burn d1 4.41 vs 4.45; slivers d1 4.65 vs 4.67, d3 4.28=4.28, d5 4.26=4.26). A
fancier leaf does NOT let a SHALLOWER search beat d1-heur: the value leaf needs real branching depth (d3+) to win,
because at d1 the 1-ply-out positions are unresolved (combo not assembled) and no static leaf can judge them as
well as searching deeper. The leaf is fit-saturated at the non-clairvoyant information limit (RMSE ≈ 0.5 turns =
the residual is clairvoyance, not learnable structure). **Conclusion: the way to beat d1-heur is cheap DEPTH
(value-leaf d3/d5), which already works on the headroom decks; pushing leaf FIT buys nothing there.** The
"pure d0/d1 judgment beats d1-search" goal is branching-limited, not a fit target after all.

### ★ GROUNDED RESULTS — both deliverables, all 5 value-model decks, held-out seeds (2026-07-09)

**(1) Goal #1 — value-leaf beats heuristic d1 on EVERY deck (avg over held-out seeds 2002/3003/7007, 150g):**

| deck | heuristic d1 | value-leaf d3 | value-leaf d5 | Δ(d5−h1) | first depth ≤ h1 |
|---|---|---|---|---|---|
| burn | 4.289 | 4.300 | 4.276 | **−0.013** | d5 (d3 ties) |
| knights | 4.364 | 4.382 | 4.356 | **−0.009** | d5 |
| slivers | 4.331 | 4.271 | 4.253 | **−0.078** | d3 |
| TH | 4.242 | 4.258 | 4.178 | **−0.064** | d5 (d3 ties) |
| antilife | 4.124 | 4.418 | 4.067 | **−0.057** | d5 |

value-leaf **d1** is always worse (branching-limited); **d3** ties/beats on aggro, **d5** beats everywhere.
So the model DOES "see ahead" past d1 — it needs ≥3 plies of branching, with the O(1) leaf making that cheap.

**HONESTY CAVEAT — value-leaf d5 is a cheap CLAIRVOYANT search, not a non-clairvoyant policy.** Only the *leaf*
is non-clairvoyant; the d5 *branching* still draws from the real seeded library (`GameState s = state;` +
advance, no reshuffle — TurnSolver.cpp:5693+). So "value-leaf d5 beats heuristic d1" is *deeper clairvoyant
search beating shallower clairvoyant search* (both read real draws; d5 wins by depth), made cheap by the O(1)
leaf. This fully delivers uses #2/#3/#4 (a fast drop-in for the deep search). It is NOT the same as goal #1's
first phrasing ("greatly improve *non-clairvoyant* play"): the truly non-clairvoyant policy is value-leaf **d0**
(no library-reading branching), which stays bounded (can't beat d1) — and GBDT/features didn't move it. A
principled non-clairvoyant "see-ahead" would be a *reshuffle-averaged* search (branching over sampled futures +
value leaf, the mental-simulation-under-uncertainty a human does) — expensive (K× branching), not yet built.
The user's d1-is-beatable point holds for that; the value-leaf-d5 result is the clairvoyant-search speedup.

**(2) Goals #2/#3/#4 — matched-quality speedup: value-leaf d5 vs heuristic d5 (EXACT LP parity, Δ=0.0000):**

| deck | heur d5 LP | value-leaf d5 LP | wall heur / value (100g) | speedup |
|---|---|---|---|---|
| burn | 4.3467 | 4.3467 | 48.55s / 1.93s | **25.1×** |
| slivers | 4.2600 | 4.2600 | 6.18s / 0.34s | **18.4×** |
| TH | 4.1467 | 4.1467 | 11.75s / 2.01s | **5.8×** |
| knights | 4.3200 | 4.3200 | 3.46s / 0.93s | 3.7× |
| antilife | 4.0600 | 4.0600 | 15.36s / 9.82s | 1.6× |

Same depth, same aggregate quality, 1.6–25× less wall-clock (biggest where the greedy play-out is longest =
grindy decks; smallest on combo, where branching above the leaf dominates). NB: same LP ≠ byte-identical lines
(the leaf picks different plans), so adoption requires a deliberate GT rebaseline. Driver scripts:
`scripts/value_leaf_matrix.py` (goal-#1 depth table), `scripts/value_leaf_speedup.py` (matched-quality
speedup); raw output in `logs/eval/{matrix,speed}_results.txt`.

### ★ v6 DISTRIBUTIONAL features — "rollout under uncertainty, built in" (2026-07-09, user's idea)

**Question (user):** can we build a model that has some rollout-under-uncertainty *built in* and decides with it?
**Answer: yes — the LABEL already targets it (de-clairvoyed = expected win turn over K futures); what was missing
were the INPUTS.** The board-only leaf read library SIZE but nothing about COMPOSITION, so it couldn't tell "6
burn left in 18" from "0 left" and couldn't amortise the rollout. The remaining-library MULTISET is PUBLIC (own
decklist minus visible zones; only ORDER is hidden/forbidden). Added 5 order-invariant counts (summed over the
whole library, never position i → non-clairvoyant): `lib_lands, lib_creatures, lib_damage_sources,
lib_draw_engines, lib_land_density_pct` (KeepModel v6). This is the human's "8 burn in 20 → ~40% to draw one".

**Result — real win exactly where library uncertainty drives the game:**
- **TH (Land's-Edge / dig combo):** held-out RMSE 1.145 → **0.947 (−17%)**; `lib_draw_engines` is a top-weight
  coef. Value-leaf **d1** play 5.44 → **4.90 (−0.54)** — nearly halves the gap to heuristic d1 (4.24). d3/d5
  converge (branching washes out the leaf once you search deeper).
- **burn / slivers (board-driven aggro):** inert (RMSE −0.009; d1 unchanged/slightly worse). The outcome is
  driven by the visible board + hand, not the uncertain library, so composition adds no signal.

**v7 HAND composition (follow-up):** same idea for the OWN hand (public) — the most immediate driver of a
shallow decision: `hand_creatures, hand_damage_sources, hand_draw_engines, hand_castable_now`. On TH (3-way on
identical rows): board-only d1 5.084 → +lib 4.944 → **+hand 4.816** (cumulative −0.27, gap to heur-d1 4.242 down
from +0.84 to +0.57). Inert on board-driven aggro *play* (burn/slivers d1 unchanged — their easy picks don't
flip) but the hand feats get real weight there (`hand_damage_sources` +338 on burn) = they improve the FIT.
All wash out at d3+ (branching dominates the leaf).

**Reading:** the distributional inputs (lib + hand) are the RIGHT foundation for a non-clairvoyant policy (they
add signal precisely where "rollout under uncertainty" matters — shallow depth, uncertainty-driven decks), and
they compose with lever 5 (reshuffle-averaged search). Append-only + byte-identical when the model is off
(featurizer runs only under the model gates). Still below CLAIRVOYANT heur-d1 at d1 (heur's leaf reads real
draws); closing that fully needs the non-clairvoyant search. Rows: `logs/eval/*_v6dist.rows`, `*_v7.rows`.

Do NOT: hand-fix provider heuristics (user decision — trust the non-clairvoyant model); retry combo-readiness
features (durdle trap); use searched labels for a non-clairvoyant d0 policy (they inherit clairvoyant
sloppiness). Older budget-starvation/depth-reinvestment framing below is superseded by the sections above.

**★ Antilife AND burn d0 — SOLVED via ROLLOUT LABELS (2026-07-09). The "fundamental gap" was a label
artifact, not capacity.** Non-clairvoyant rollout labels (`MTG_EVAL_ROWS_ROLLOUT`: label each candidate by
apply-plan→greedy-baseline-`SimulateToEnd`, not the clairvoyant earliest-win search) let a plain **LINEAR**
model reach **baseline parity** on both: antilife 87.8%/87.2% vs 88.0%/88.5% (was 30–55%); burn LP
4.723/4.693 vs 4.740/4.693 (was ~0.1t worse). The searched label was over-crediting durdle lines a real d0
can't realise. **Label choice is deck-dependent**: rollout (imitate baseline) wins for aggro/durdle-prone
(burn/antilife); **searched (distill optimum) still wins for TH** — rollout HURTS TH (77.7% vs 86.7%,
combo needs the searched line). Engine change inert by default; rollout dumps ~10× cheaper (no search tail).
Full detail + the imitation-vs-distillation model in the session-log entry "★ ROLLOUT LABELS solve antilife".
**Next:** (a) commit per-deck ship-worthy linear sidecars (rollout for burn/antilife, searched for TH);
(b) re-test the SHARED cross-deck model with rollout/imitation labels (may pool where searched labels
collapsed). Superseded: the "combo-readiness features / GBDT capacity" narrative for antilife d0.

### All 6 decks d0 — per-deck best label + result (2026-07-09)

Ran the rollout-vs-searched label choice across every deck (d0, held-out seeds, LINEAR unless noted;
rollout dumps are d3-play/K8/400g, ~cheap). **`MidGameEvaluator` linear sidecars are near/at parity on
all decks; the label recipe is deck-dependent.**

| deck | archetype | best d0 model | learned | baseline | Δ |
|---|---|---|---|---|---|
| Treasure Hunt | value/combo | **searched**-lin | 87.2% | 86.7% | **beats** |
| burn | aggro | rollout-lin | LP 4.723/4.693 | 4.740/4.693 | **parity/better** |
| Anti-Lifegain | combo (assemble) | rollout-lin | 87.8%/87.2% | 88.0%/88.5% | parity (−0.01/+0.06 LP) |
| knights | aggro | rollout-lin | 99.3%/99.3% | 100%/100% | near (−0.22 LP) |
| slivers | aggro | rollout-lin | 100%/99.7% | 100%/100% | near (−0.04/−0.10 LP) |
| hinata | combo (spellslinger) | rollout-lin | 56% | 59% | near non-clairvoyant ceiling |

Findings:
- **Rollout (imitation) label wins on aggro + assemble-combo** (burn/antilife/knights/slivers); **searched
  (distillation) label wins on TH** (Land's-Edge needs the searched optimum). GBDT overfits every aggro
  deck (knights rollout-GBDT LP +1.41; burn/hinata likewise) — **linear is the right class for imitation
  labels**; GBDT only helped searched-label antilife (now obsolete).
- **hinata is the honest ceiling case.** d0 baseline is only 59% but d1 = **96%** — that 37-pt jump is
  largely **clairvoyance** (1-ply lookahead sees the immediate combo payoff a non-clairvoyant d0 can't).
  So ~59% is near the non-clairvoyant d0 ceiling; rollout-lin's 56% (imitating the weak baseline) is about
  as good as non-clairvoyant d0 gets. **Distilling the stronger d1 policy via deeper rollout labels is
  intractable for hinata** — depth-1/2 rollout labels (a per-turn search × K × candidates) time out on the
  combo. Added the `rollout_depth` knob (`MTG_EVAL_ROLLOUT_DEPTH`, env; `EnumerateEarliestWins` int param,
  inert default 0) for decks where it IS affordable; hinata isn't one.
- Per-deck best d0 sidecars persisted in `logs/eval/*_rollout_lin.eval.json` (+ TH's searched
  `th_v2rank`/`treasure_hunt.eval.json`). Adoption to `decks/*.eval.json` (+ GT rebaseline) is the
  remaining deliberate step.

**Loose ends:** hinata value sidecar still pending its (slow) dump — re-dump at budget ~400 and commit
`decks/Hinata2.value.json` to complete the set. Value-model ADOPTION (flip `MTG_VALUE_MODEL` default on +
rebaseline GT to the faster search) is a deliberate decision, not yet made. Pre-existing TH smoke GT
staleness still needs a deliberate rebaseline (unrelated to this work).

### Budget-starvation depth-reinvestment A/B on TH's Land's-Edge line — DONE (2026-07-09)

Ran the doc's flagged "reinvest the value-model speedup into DEPTH" test on TH. **The lever is DEPTH,
not budget** — a budget sweep at d5 (below) shows baseline is already converged at *budget 10*, so the
doc's "starved at ~b200, recovers at ~b2000" framing is a depth phenomenon (short horizon can't see the
multi-discard Land's-Edge kill), not a budget one. Everything below is threads=1 for clean wall-clock;
seeds 2002/3003 are held-out (training used 20000).

**1. Budget is not the lever at d5** (100g, s2002): baseline exact-rollout is flat 98–99/4.08–4.10 across
budget 10→2000, just linearly slower (4.5s→35.6s); the value-leaf ties it (99/4.10) flat at ~3s. So at
adequate depth the value-leaf is the doc's known ~5–11× speed win, and budget buys nothing.

**2. Depth IS the lever** (100g, s2002, budget 200). Baseline needs d3+ (13–16s) to reach converged
99/4.101; the value-leaf reaches it at d5 in 3.3s — so *at equal wall-clock the baseline is stuck shallow*:

| depth | baseline won/avg/wall | value-leaf won/avg/wall |
|---|---|---|
| 1 | 98/4.153/2.4s | 94/5.106/1.7s |
| 2 | 98/4.092/4.9s | 96/4.344/1.9s |
| 3 | 99/4.101/13.1s | 98/4.153/2.2s |
| 5 | 99/4.101/16.2s | 99/4.101/3.3s |

**3. Equal-wall-clock A/B** (500g, budget 200, loss-penalized avg, losses=9):

| seed | baseline d1 (~5s) | baseline d2 (~16s) | value-leaf d5 |
|---|---|---|---|
| 2002 | 488/500 LP 4.268 | 491/500 LP 4.202 | **492/500 LP 4.182** @ 12.1s |
| 3003 | 484/500 LP 4.290 | 487/500 LP 4.242 | 486/500 LP 4.254 @ 16.2s |

At value-d5's wall-clock the baseline sits at ~d1–d2. **s2002: value-d5 wins** (LP 4.182 < baseline-d2's
4.202, and cheaper). **s3003: ~wash/slight-loss** (value-d5 LP 4.254 ≈ baseline-d2 4.242 at ~equal 16s —
the value-leaf's wall-clock is seed-variable, 12s vs 16s). The aggregate delta is small because TH already
wins ~97–98% at d1.

**4. Per-game, the gain is directional, not noise** (s2002 500g, baseline-d1 vs value-d5): of 33 games that
differ, value-d5 wins 32 — **+4 net wins (5 loss→win, 1 win→loss) and 27 faster kills, baseline faster in
0**. The Land's-Edge line concretely benefits from horizon: deeper search finds the multi-discard kill
earlier / at all. So "reinvest into depth" is a *real* concentrated improvement on the starved subset, just
swamped in the aggregate by TH's already-high shallow win rate.

**5. Clairvoyance adjudication — the prescribed instrument is STRUCTURALLY INAPPLICABLE to TH, resolved a
different way.** `MTG_SHUFFLE_SALT_SEARCH` (and even mid-game `MTG_SHUFFLE_SALT`) are **byte-inert** on TH
(0 win-turn diff across salts 1/2/3/7); only `MTG_SHUFFLE_SALT_OPENING` moves games (164/120-dump). Cause:
**TH has zero mid-game shuffle events** — its library is fixed by the opening shuffle and fully known
thereafter (Treasure Hunt / Land's Edge read/discard, never `Shuffle()`). The decouple instrument only
perturbs reshuffle *realizations*, so it can't perturb TH's static known draw order. (Note: burn d3/budget200
was also inert in this run — the reshuffle-decouple family only bites decks with mid-game shuffles like the
hinata always-shuffle line; that deck's `.txt` isn't present in this checkout.) **Adjudicated instead by
showing the value-leaf adds no clairvoyance of its own:** value-d5 vs baseline-d5 differ in only **2/500**
games (gi168 win→loss, gi176 7→6 — net 0), i.e. the value-leaf faithfully reproduces the engine's
*already-accepted* d5 policy (the one the regression GT runs at d5), ~5× cheaper. The d5-over-d1 advantage
is therefore the engine's ordinary lookahead-into-known-library, a pre-existing accepted property — NOT new
clairvoyance introduced by the reinvestment.

**Verdict.** The value-model speedup converts cleanly into reaching converged (d5) quality at ~5× lower
wall-clock, and on the Land's-Edge subset that shows up as +4 wins / 27 faster kills vs an equal-wall-clock
shallow baseline. But on TH it is **"reach the existing ceiling cheaper," not a NEW ceiling** — TH converges
by d3–d5 regardless, so the aggregate equal-wall-clock gain is modest and seed-variable (clear on s2002,
wash on s3003). The reinvestment thesis is *validated in mechanism* but TH is a weak showcase because it's
nearly converged even shallow. **Next, to actually demonstrate a headroom win, this test wants a deck whose
search does NOT converge by d5 within the suite budget** (a genuinely budget-starved / deep-combo deck) —
there the cheaper leaf should unlock depth the baseline can't afford at all. TH proved the plumbing + no
clairvoyance regression; it did not (because it can't) prove a large quality ceiling gain.

### Antilife d0 dump methodology — PINNED, and the doc's diagnosis was wrong (2026-07-09)

Executed the d0-plan step 1 ("pin the antilife dump methodology"). **Root cause found, and it is NOT
seed/K/budget as the doc claimed — it's the dump's PLAY DEPTH.** Isolation experiment (all A/B at d0,
held-out seed 2002, 300g; baseline = hand-tuned 88.7%):

1. **The rows were never the mystery.** Retraining *linear-rank* from the saved `logs/eval/antilife_v3.rows`
   reproduces the doc's ~30.8% (got 34.0%); the doc's saved `/tmp/antilife_rank.eval.json` serves 29.3% as
   claimed. So the LINEAR result is fully reproducible from committed rows.
2. **The 53% model still exists** (`/tmp/antilife_gbdt.eval.json`, serves **53.0%**) — but it can NOT be
   reproduced from `antilife_v3.rows`: GBDT trees=150/depth=5 on those rows caps at ~34.7% even matching its
   leaf count (3865 vs its 4161). Same v3 featurizer (splits on `plan_face_damage`, no `plan_baseline_eval`),
   so it trained on a *different, better* v3-featurizer dump that was never saved.
3. **The lever is the play depth of the DUMP.** The label search (`EnumerateEarliestWins`) always runs at
   full depth + unlimited budget (`FromVirtualMs(1000000)`), so `--budget-ms`/`--depth` in the dump command
   affect ONLY *which states get visited*, not label quality. A **d0-played** dump durdles (antilife d0 ≈
   the problem itself) → its rows lack combo-assembled payoff positions → GBDT learns 29%. A **d3-played**
   dump reaches the winning combo states → informative rows that teach "cast the piece."

**Pinned + reproducible recipe (beats the doc's lost 53% model):**
```
# DUMP from d3 play (reaches combo-payoff states); label search is full-depth regardless of budget:
MTG_DUMP_EVAL_ROWS=logs/eval/antilife_d3play.rows MTG_EVAL_ROWS_K=8 \
  build/Release/mtg decks/Anti-Lifegain.cod --games 400 --seed 20000 --depth 3 --budget-ms 400 \
  --max-turns 8 --threads 12
# TRAIN GBDT (linear COLLAPSES to 0% on these rows — combo conjunction needs nonlinearity):
scripts/train_eval_gbdt.py --rows logs/eval/antilife_d3play.rows --trees 200 --depth 5 --min-leaf 20 \
  --out <model>.eval.json
```
Result (d0, `logs/eval/antilife_pinned_gbdt.eval.json`, trained on ~10k d3-play rows): **s2002 55.3%,
s3003 48.0%** — reproduces AND exceeds the doc's 54.8%. **Deterministic** (t1==t4, fixed-point GBDT).
Hyperparam notes: trees 200 > 150 > 120 (default underfits); **depth 5 is the sweet spot — depth 6 overfits
to 24%**; min-leaf 20 > 15; the *linear* ranker collapses to 0% on d3-play rows (only GBDT represents the
combo). Artifacts persisted in `logs/eval/` (`antilife_d3play_pinned.rows`, `antilife_pinned_gbdt.eval.json`).
Dump caveat: the full-depth label search has a **pathological heavy-tail game** at 400g (one combo state's
unlimited-budget search churns for minutes) — ~10k rows from the first ~380 games is plenty; kill the tail.

**Status: antilife d0 is UNBLOCKED and tunable.** It sits at ~48–55% vs the hand-tuned baseline's ~88% —
that residual is d0-plan **step 2** (capacity/features to close the gap), NOT a methodology blocker anymore.
NB the doc's standing warning still holds: combo-*readiness* features backfire on non-clairvoyant d0 (teach
it to wait/durdle); the win here came from better *training-state coverage* (d3-play rows) + GBDT capacity,
not from telling the model about the combo.

### ★ ROLLOUT LABELS solve antilife (and burn) d0 — the "fundamental gap" was a LABEL artifact (2026-07-09)

d0-plan step 2. The doc's standing conclusion — *"no learned model reaches the hand-tuned baseline on
burn/antilife d0; the residual is combo/sequencing feature-completeness / capacity"* — **is wrong.** It was
the **label**. Fixed it; antilife and burn d0 now reach **baseline parity with a plain LINEAR model.**

**Root cause.** The label came from `EnumerateEarliestWins` = the CLAIRVOYANT earliest-win **search** per
candidate (de-clairvoyed over K reshuffles). That search over-credits a **durdle** plan: after passing, the
clairvoyant search still finds a fast win by reading the library, so "pass" gets a deceptively good label —
a line a real non-clairvoyant d0 cannot realise. Capacity/features/K/rows can't fix a target that rewards
durdling.

**Fix — non-clairvoyant ROLLOUT labels** (engine change, inert by default): new
`MTG_EVAL_ROWS_ROLLOUT` gate + `EnumerateEarliestWins(..., rollout_label=true)`. Label each candidate by
**apply the plan → advance a turn → `SimulateToEnd` at depth 0** (the greedy **baseline d0 policy** played
forward), averaged over the same K reshuffles. This is exactly what a non-clairvoyant player achieves from
here, so it does NOT over-credit durdle (greedy play from a durdle state wins slower). Mechanically it
mirrors `FSLineTail`'s advance step (`SimulateEndAndStartNextTurn`+`ExpireStagedCards`) then swaps the
search for the greedy rollout. Play path untouched (`EnumerateEarliestWins` is offline-only); model-off
byte-identical. **Bonus: rollout dumps are ~10× cheaper** — no clairvoyant-search pathological tail (antilife
400g: **15.7s** vs *minutes*).

**Results — d0, held-out seeds, vs hand-tuned baseline (LP = loss-penalized avg turn, lower better):**

| deck | searched-label (old) | **rollout-label LINEAR** | baseline |
|---|---|---|---|
| Anti-Lifegain | 30–55% | **s2002 87.8% / LP 5.735; s3003 87.2% / LP 5.800** | 88.0% / 88.5% (Δ −0.01, +0.06 — **parity**) |
| burn | LP 4.843 (~0.1t worse) | **s2002 LP 4.723; s3003 LP 4.693** | 4.740 / 4.693 (Δ −0.017, 0.000 — **parity/better**) |
| Treasure Hunt | 87.2% (searched ✓) | 77.7% lin / 83.3% GBDT (**worse**) | 86.7% — **keep SEARCHED for TH** |

Deterministic (t1==t8, fixed-point linear). Artifacts: `logs/eval/{antilife,burn}_rollout.rows`,
`logs/eval/{antilife,burn}_rollout_lin.eval.json`, `logs/eval/th_rollout.rows`.

**The unifying model (important).** A rollout label trains the ranker to **imitate the baseline policy**
(reproduce what greedy baseline play achieves); a searched label **distills the clairvoyant optimum**.
So the best label is **deck-dependent**:
- **Rollout wins** where the hand-tuned baseline is already good AND the ranker can imitate it — proactive
  aggro (burn) and durdle-prone combo where the *labels* (not the play) were the trap (antilife).
- **Searched wins** where the baseline is *suboptimal* but the winning line is *findable* by search and the
  ranker can capture its structure — TH's Land's-Edge combo (greedy rollout misvalues it; the searched
  optimum is needed). Rollout HURT TH (77.7% vs 86.7%), GBDT didn't rescue it (83.3%).

**Consequences / TODO:**
- **antilife d0 is SOLVED** (parity), and **burn d0's residual gap is closed** — both with a *linear* model.
  This retires the doc's "capacity/GBDT/feature-completeness" narrative for these decks: it was the label.
- Ship candidate: per-deck, pick the label that wins (rollout for burn/antilife, searched for TH), train
  linear, A/B, adopt where ≥ baseline. Antilife/burn linear sidecars are now ship-worthy at d0.
- Re-examine the **shared cross-deck** model with rollout labels — imitation labels + linear may pool far
  better than searched labels did (searched-shared-linear collapsed both TH+burn). Untested.
- Caveat: rollout labels imitate the baseline, so they match it, rarely *exceed* it — the value is
  replacing hand-tuned `EvalCard` (generalization to new decks) and enabling the shared model, not beating
  the baseline where it's already good.

---

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

### ★ CORRECTION: the d0→search gap is LEARNABLE (feature-limited), NOT clairvoyance (2026-07-09)

Reframed the whole evaluation on the user's steer: **the metric is avg win turn (loss=9), and the target
is the SEARCH (d3, ideally d5–d7) — not the hand-tuned d0 baseline.** In that frame the earlier "parity"
claims evaporate: the best learned d0 trails d3-search on every deck (LP, s2002):

| deck | hand-d0 | learned-d0 | **d3 search** | d5 |
|---|---|---|---|---|
| burn | 4.74 | 4.66 | **4.29** | 4.29 |
| antilife | 5.65 | 5.66 | **4.05** | 4.00 |
| knights | 4.49 | 4.72 | **4.29** | 4.29 |
| slivers | 4.74 | 4.80 | **4.26** | 4.27 |
| TH | 5.58 | 5.58 | **4.18** | 4.15 |

**I initially mis-attributed burn's gap to clairvoyance. The user pushed back (burn is simple: drop
creatures early, use mana efficiently — clairvoyance rarely matters), and a decisive test proved them
right.** Comparing the *de-clairvoyed* rollout labels (K-reshuffle averaged, so the future is washed out):

- **depth-0 (greedy-baseline) labels**: mean best-per-decision = **4.907**
- **depth-3 (d3-policy) labels**: mean best-per-decision = **4.705**

The d3 policy is **0.2t better than greedy even non-clairvoyantly** ⇒ the gap is a *policy-quality* gap,
learnable, not clairvoyance. So a better non-clairvoyant policy provably exists (~4.7 vs greedy 4.9).

**Why the learned d0 still plateaus at ~4.66 despite depth-1/2/3 labels + linear/GBDT:** two structural
reasons, both now understood.
1. **A d0 policy plays *itself* forward.** Training on depth-3 labels teaches d3's *first* move, but after
   that the model plays d0 forward — so it only reaches d0-forward quality, never d3-forward, unless it
   ranks like d3 at *every* decision.
2. **The features can't separate the deeper policy's preferred plan.** The plan summary (total MV, face
   damage, num-spells, baseline_eval…) is too coarse to encode "develop this creature now so it attacks
   next turn." Label quality is fine; **feature/representation capacity is the bottleneck.**

**⇒ The path to close the gap to search is RICHER FEATURES + the depth-3 (or deeper) rollout label as the
target** — not more label depth alone, and not chasing clairvoyance we can't have. Top candidate feature:
**value-model-as-feature** — rank each plan by the value model's predicted win-turn of the *resulting*
position (the value model already encodes full-horizon search, so this injects multi-turn value the coarse
plan-summary misses, and inherits the user's d5–d7 depth for free). Secondary: hand-crafted
non-clairvoyant sequencing features (develops-attacker, curve/mana-efficiency, holds-reach).

**Value-model-as-feature — first attempt FAILED (crude approximation), reverted (2026-07-09).** Implemented
`plan_value_eval` (v5 feature + shared `PlanValueEval` helper) computing the value model's win-turn on an
**estimated** post-plan position — the pre-plan value features *adjusted* by the plan summary (opp_life −=
face_damage, our_creatures += creatures_cast, hand/library/graveyard deltas). Lockstep-trivial (pure
function of state+summary, like `plan_baseline_eval`) and byte-identical inert by default. But it **did not
discriminate plans**: measured `plan_value_eval` range within a decision = **0.0 across all 1711 decisions**
— the value GBDT doesn't split finely enough on the few coarse fields I adjust, so every candidate got the
same predicted win-turn. A constant-but-large feature then *destabilised* the ranker (burn collapsed to LP
6.33). Reverted the 4-file change. **Lesson: the approximation is too crude — the value model needs the
REAL applied post-plan position** (actual P/T, tapped mana, combat, triggers) to tell plans apart. Correct
build: compute the feature *inside* the plan application — `EnumerateEarliestWins` already `ApplyPlanDirect`s
each candidate (so the dump gets the true post-plan state for free); the seam must likewise apply each
candidate subset (added d0 hot-path cost) and evaluate the value model on the resulting `GameState`. That is
the next attempt; the crude shortcut is a dead end.

**Also settled here:** a *shared* cross-deck LINEAR ranker still collapses under rollout labels (burn/antilife
0%) — sharing needs nonlinearity/deck-conditioning. And `MTG_EVAL_ROLLOUT_DEPTH` (the rollout-policy depth
knob) is committed; depth>0 is affordable on aggro but intractable on combo (hinata).

### ★ Yardstick = SEARCH depth; value-leaf matches/beats d1-heuristic, reaches d5-search cheaply (2026-07-09)

Per the user, the right yardstick is the SEARCH, laddered by depth (d1 → d3 → d5 → d7), and the model
should "see" turns ahead like a chess player's intuition — not just imitate the hand-tuned d0. The fair
matchable bar (user): **d1 with HEURISTIC rollout vs the model** (using the model as the rollout leaf on
both sides makes d0 unable to win, so compare d1-heuristic to the model policy). Clairvoyant library-
manipulation effects excepted.

**Result (LP, loss=9, s2002):**

| deck | d1-heur (bar) | d3-search | d5-search | **value-leaf d5** (cheap) | d1-value-leaf | d0 ranker |
|---|---|---|---|---|---|---|
| burn | 4.303 | 4.29 | 4.29 | 4.32 | 4.40 | 4.66 |
| knights | 4.30 | 4.29 | 4.29 | 4.32 | 4.45 | 4.72 |
| slivers | 4.355 | 4.26 | 4.27 | **4.26** | 4.63 | 4.80 |
| antilife | 4.12 | 4.05 | 4.00 | **4.06** | 5.54 | 5.66 |
| TH | 4.22 | 4.18 | 4.15 | **4.147** | 5.34 | 5.58 |

**Findings:**
1. **The value model in a shallow (d5) search is the winning "fast near-search policy."** value-leaf d5
   **matches or beats d1-heuristic on all 5 decks** — beats on slivers/antilife/TH (deep search matters,
   the value model encodes it), +0.02 on burn/knights (d1's greedy leaf is already near-optimal; a
   regressor can't quite match a real playout) — and lands within **~0.03–0.06 of full d5-search**, at an
   O(1) leaf instead of a full rollout. This is the chess-intuition goal realized: the value model sees the
   outcome without playing it out.
2. **The pure d0 RANKER is the weak artifact** (~4.66 burn, far from every search depth). A static per-plan
   score cannot encode multi-turn planning; computing an accurate post-plan value requires *applying* each
   plan = a 1-ply search (so value-model-as-feature buys nothing over just running the value-leaf search —
   see the reverted v1). **d0-ranker is a dead end for reaching search quality; the value model is the path.**
3. **The value model is NOT data-limited** (burn d1-value-leaf: 2175 rows = 4007 rows = 4.40) nor
   capacity-limited (t120→t400 no better). Its shallow-depth shortfall vs a greedy playout is *regression
   fidelity*, which shrinks with depth (d1 4.40 → d5 4.32) as the leaf becomes less decisive.
4. **d1-value-leaf < d1-heuristic** everywhere: at d1 the exact greedy playout beats the value regressor;
   the value model's win is SPEED at adequate depth, and QUALITY on combo where greedy misplays.

**⇒ The per-deck "high-quality solution" is the value model used in a d3–d5 value-leaf search** (cheap,
matches/beats d1, ~0.05 off full search), NOT a pure d0 ranker. Remaining: (a) climb the ladder to d7 as
the yardstick; (b) now that gaps are ~0.05, READ individual games (not hand-wave) to attribute the residual
to clairvoyance — especially the shuffle decks (antilife/hinata) where the user expects a bit more; (c)
goal #4 (bound real-search depth by the value model's predicted win turn) to make the deep search itself
cheaper. Deprioritized: pushing the d0 ranker further (structurally capped below a 1-ply search).

### Reading games — clairvoyance taxonomy (2026-07-09, with the user)

Per the user's methodology (close gaps, THEN read individual games instead of hand-waving), read divergent
games (non-clairvoyant d0 vs clairvoyant d5-search). Seed hygiene verified: trained on seed 20000, all A/B
on held-out 2002/3003/7007; the antilife value-leaf-d5-beats-d1 result replicates on fresh seed 7007
(4.033 vs 4.073).

**Three distinct gap sources emerged — only one is irreducible:**

1. **Learnable play quality (recoverable).** *Burn game 18/29, identical draws (no shuffle):* d5 wins T5,
   the d0 ranker wins T8 — purely because the ranker dribbles one spell/turn while the search holds Shard
   Volley and assembles a burst. Zero clairvoyance (same draws). The **pure d0 ranker is myopic** (can't see
   a play's multi-turn cost); the **value-leaf search plays these lines correctly** (why value-leaf d5 ≈
   search while the ranker sits at 4.66). Fully recoverable.
2. **Justified clairvoyance (irreducible).** *Antilife game 18:* the search plays Forest T2 (declines to
   crack Marsh Flats) to PRESERVE a library top it can see holds Plague Drone + Invigorate → wins T4; the
   blind d0 fetches, reshuffles the combo away, loses. A non-clairvoyant policy structurally can't make this
   call. This is the shuffle-clairvoyance channel the decouple instrument measures — small (2/120 antilife
   games at d3). The true non-clairvoyant floor.
3. **Indifferent sloppiness (recoverable — and the reason rollout labels win).** *Antilife game 18:* the
   search casts Aria of Flame T2 with no enabler, GIFTING the opponent 10 life (verified: `opponentLife
   20→30`; Aria has `etb_opponent_lifegain:10`, flipped only by a `lifegain_to_loss` enabler). The play is
   genuinely BAD — clairvoyance just tells the engine it won't be punished (it wins T4 regardless), so the
   SEARCHED label never flags it. This is the mechanism behind insane search lines: **clairvoyance doesn't
   only reward clever plays, it erases the cost of careless ones, and searched labels inherit the missing
   penalty.** A **non-clairvoyant ROLLOUT label re-introduces the penalty** (a blind greedy playout after
   gifting 10 often does NOT win T4) → the model plays saner at zero win-rate cost (the play was
   outcome-neutral). This is *why* rollout labels fixed antilife d0.

**Consequence for the target:** the irreducible non-clairvoyant gap is ONLY the *justified* clairvoyance
(category 2, small). Categories 1 and 3 are recoverable, so the achievable floor is well below the raw
d0-vs-search gap. Method for future game-reads: sort each divergence into learnable / justified / indifferent;
NB fetchland decks confound per-game comparison (different plays → different shuffles → different draws), so
read no-shuffle decks (burn/knights/TH) for clean play-quality attribution and use the decouple instrument
for the shuffle channel. Decision (user): do NOT hand-fix the eager pre-enabler Aria cast or similar
provider heuristics — the non-clairvoyant (rollout-labeled) model should iron these out on its own, because
a bad-but-unpunished play doesn't advance the win so the model has no reason to keep it. Trust the model,
not hand-tuning.

## The permanent regression gate

At every phase: with **no sidecar or `MTG_EVAL_MODEL` unset**, smoke + regression digests are
**byte-identical** to committed GT. The seam defaults `rank_value = total_eval`; the sidecar is
presence-gated; the dump hook is env-gated. This "no-op reproduces GT" property is what makes the
whole feature safe to land incrementally.

### ★ d0 vs SEARCH gap + clairvoyance attribution (2026-07-09)

After the d0 model beat the heuristic, measured how far it is from the SEARCH and attributed the gap.

**Bottoming correction (user caught it):** bottoming is table-based (exhaustive `*.keepmodel.exhaustive.
profile.json.gz` sidecar, `bottoming_enabled=on`) → depth-independent + non-clairvoyant, when the sidecar is
present. It was present for 4/5 decks (knights/slivers/TH/antilife) but MISSING for burn, so burn alone fell to
the depth-dependent CLAIRVOYANT lookahead-bottoming fallback (keeps worse hands at d0, reads library at d5).
Committed burn's R=100 table (`bottoming_enabled` flipped on) → burn now fair. Effect: burn d0-vs-d5 gap
0.393→0.198 (the 0.164 removed was pure bottoming-clairvoyance, isolated via `MTG_CONFOUND_BOTTOM`:
heur-d5 4.276 clairvoyant → 4.440 de-clairvoyed). Verified knights mulligan hands keep the SAME bottomed hand
at d0/d5 (table = depth-independent). Burn GT now needs a rebaseline (bottoming changes its play).

**Fair gap-to-search map (all 5 decks, table bottoming, held-out):**

| deck | model d0 | heur search | gap | regime |
|---|---|---|---|---|
| knights | 4.484 | 4.356 (d5) | +0.13 | aggro (close) |
| slivers | 4.442 | 4.253 (d5) | +0.19 | aggro (close) |
| burn | 4.549 | 4.351 (d5) | +0.20 | aggro (close) |
| TH | 5.540 | 4.169 (d3) | **+1.37** | combo (huge) |
| antilife | 5.522 | 4.069 (d3) | **+1.45** | combo (huge) |

**Combo gap = dominated by CLAIRVOYANCE (game-read, TH seed 4025, fair bottoming).** Both play the
Treasure-Hunt/Land's-Edge combo (draw a land pile, Land's-Edge it to the face). Treasure Hunt draws "until a
nonland" = count depends on the HIDDEN library order. SEARCH fires Treasure Hunt on T4 (a 13-land clump on top →
instant lethal) because its d5 lookahead reads the real library; MODEL d0 (non-clairvoyant) fires it into a
2-land clump (fizzle) T6 and only lucks into the payoff T7 → +3-turn gap. This is irrecoverable *justified
clairvoyance* (timing a library-dependent payoff you can see). Small recoverable slice (model durdles T1-T3 +
double-casts Throes). Aggro gaps (~0.15-0.20) are small and likely mostly recoverable/minor mid-game clairvoyance.
A truly fair search baseline would need a reshuffle-averaged (de-clairvoyed) search — the `MTG_SHUFFLE_SALT_SEARCH`
decouple is INERT on no-shuffle decks (burn/knights/TH), so that's the deferred "extra work" for a clean ceiling.

**Aggro gap = RECOVERABLE, no clairvoyance (game-read, knights seed 4033, same opening hand).** Lines diverge
on a pure board-development call: T3 the MODEL casts Acclaimed Contender (1 body) while SEARCH casts Venerable
Knight + Worthy Knight (2 bodies, same 3 mana). SEARCH's wider board + T4 lords (Inspiring Veteran + Marshal
pump the team) → lethal T4; the model is one body short → lethal T5. No hidden info — going wider beats one
bigger card on tempo. Confirms the split: COMBO gap = clairvoyance (irrecoverable), AGGRO gap = recoverable
sequencing. Concrete lever: the d0 model must value board WIDTH (body count, esp. with lords), not just
plan_power_added (total power). This is a d0 feature/label improvement, not a clairvoyance floor.

### Closing the gap to search — what moves it and what doesn't (2026-07-09)

Attacked the RECOVERABLE (non-clairvoyance) gaps, esp. aggro. Findings:
- **More DATA does NOT help (answered directly).** knights d0 learning curve is FLAT: 656 → 5248 decisions =
  4.467 → 4.464. Saturates <1k decisions. The gap is NOT data-limited.
- **Every label/capacity lever fails on the aggro gap:** searched labels = rollout (4.484), `rollout_depth=1`
  is WORSE (4.51/4.55), GBDT collapses (5.44), higher lam worse. The recoverable aggro gap (~0.13) sits at the
  IMITATION CEILING: the labels don't DISCRIMINATE the tied plans (go-wide vs Acclaimed Contender reach similar
  outcomes under both greedy AND searched continuation → neither punishes the narrow line), so imitation can't
  learn the preference. Closing it would need a signal outside imitation (a hand-crafted width heuristic — which
  the user has ruled out) or is simply the ~0.13 floor of this approach.
- **v9 `plan_dig_yield`** (plan casts dig engine × lib land-density = expected Treasure-Hunt yield, a
  non-clairvoyant proxy for the search's clairvoyant dig timing) added + built, **UNTESTED on combo** (TH dump
  is slow; deferred). Byte-identical when the model is off. Intended to recover a slice of the combo gap.
- **Net:** d0 beats heuristic 5/5 and is near the achievable non-clairvoyant ceiling. Aggro ~0.13-0.20 from
  search (imitation floor); combo ~1.4 from search (clairvoyance). Data is not the lever; discriminating labels
  / a de-clairvoyed search baseline are the open directions.

### The saturation is a MODEL-CAPACITY ceiling, not a data ceiling — but only knights can use it (2026-07-09)

User pushback: "I still wonder whether we might be using the wrong approach if it maxes out on the data it can
use effectively so quickly." Correct instinct — investigated with three diagnostics (`scripts/label_discrim.py`,
`feature_collision.py`, `linear_ceiling.py`), decomposing within-decision label variance (rows sharing (seed,turn)
= the competing candidates of ONE decision):

1. **Labels are NOT dead.** knights: 77% of decisions have label spread (mean 0.82 turns); searched labels are
   *less* discriminating than rollout (spread 0.62 vs 0.82). So "weak teacher / no gradient" is not the story.
2. **Features are NOT the ceiling (aggro).** feature-collision (same feature vector, different label) ≈ 0;
   a lookup table on the features explains **95–100%** of within-decision label variance (knights 0.955, slivers
   0.978, burn 1.000). Combo is genuinely feature-limited (antilife 0.658, TH 0.568 = the clairvoyant library
   order the non-clairvoyant features can't see — consistent with the combo=clairvoyance split).
3. **The LINEAR FORM is the ceiling.** A global-linear within-decision fit captures only **0.42–0.59** of that
   0.95+ table ceiling (knights 0.586, slivers 0.587, burn 0.420). The 0.37–0.58 gap is pure functional-form loss
   — interactions a linear ranker can't represent. THIS is why it saturates on <1k rows: ~9-12 weights fit fast,
   then it's structurally done. Pairwise feature crosses recover part of it and **generalize** (held-out
   knights 0.591→0.685, slivers 0.591→0.644, burn 0.444→0.504, TH 0.132→0.229) — real interaction signal.

**End-to-end (the metric that counts, held-out win-turn): a REGULARIZED GBDT helps knights, and CORRECTS the
earlier "GBDT collapses" claim** — that collapse was the config (depth 4, lr 0.15, free warm-start, no min-leaf
control). The fix: `--init-linear` (or new `--init-model <anchored sidecar>`) + shallow + high min-leaf. Recipe
g2 = init-linear, depth 3, lr 0.05, 80 trees, min-leaf 50:
- **knights (aggro): −0.012 vs linear, ROBUST** across 3 disjoint seed sets (tune 2002/3003/7007, first-5, fresh
  4020-4025). Extends the beat-heuristic margin 0.030 → 0.042 (~40% larger). Extra depth-4 capacity (g3) ties g2
  on fresh → g2 is the pick. Saved `logs/eval/knights_d0_gbdt.eval.json`.
- **burn (aggro): +0.002 = WASH.** Table ceiling 1.0 but the extra R² is argmax-irrelevant (doesn't flip the top
  pick), so capacity buys nothing.
- **slivers (Vial): +0.235 = HURTS BADLY** (worse than heuristic). **antilife (Vial): +0.053 = HURTS.** These are
  ANCHOR-DOMINATED: the hand-tuned heuristic is already near-optimal (slivers linear beats heuristic by 0.19, the
  biggest margin of any deck — almost all from the anchor + a tiny correction), and the rollout labels are NOISIER
  than the anchor. Capacity lets the trees deviate from the good anchor toward label noise → worse. Warm-starting
  the GBDT from the anchored linear (`--init-model`) does NOT save it — the pairwise trees still misrank.

**Strategic conclusion (answers the "wrong approach?" question).** The linear model genuinely maxes out fast
because it is linear — a real, measured capacity ceiling. But capturing more of it improves PLAY only where
(a) the model is capacity-limited AND (b) the labels are a BETTER TEACHER than the anchor. That holds only on
knights. On Vial decks the anchor already IS the ceiling and the labels are worse than it, so neither data nor
capacity helps (capacity hurts). **The binding constraint is LABEL/TEACHER QUALITY, not data quantity or model
capacity.** The lever that would move every deck is a stronger, de-clairvoyed teacher — the reshuffle-averaged
search (already the deferred "fair baseline" build) — not more data and not a fancier model. Ship: knights GBDT
sidecar + `--init-model` trainer flag + the 3 diagnostics; adopt knights-GBDT when d0 sidecars are adopted.

### Can we "average out" clairvoyance with more data? NO — measured (2026-07-09)

User's image: train on the clairvoyant search, throw enough games at it so clairvoyance averages out.
Averaging removes VARIANCE (which library you drew) but not the BIAS: the searched label is
`E_f[max_{pi|f} outcome]` (continuation chosen AFTER seeing future f = strategy fusion), while the
achievable non-clairvoyant value is `max_pi E_f[outcome]`. `E[max] >= max[E]` (Jensen) always, and the
gap = value of hidden info (EVPI) — a property of max/expectation ORDER, not sample size. Un-averageable.

**Measured it** (`scripts/evpi.py`, pairs the aligned rollout vs searched dumps by candidate position;
fusion_regret = rollout[argmin searched] - rollout[argmin rollout] = honest turns lost by trusting the
clairvoyant pick):
- **knights (aggro):** raw clairvoyance gap 0.225t, argmin-disagree 20.6%, **fusion regret 0.029t** (7.4% of
  decisions, max 1.0). Clairvoyance is decision-BENIGN -> searched labels SAFE -> matches searched~=rollout.
- **antilife (Vial):** raw clairvoyance gap 0.231t (SAME as knights!), argmin-disagree 58.9%, **fusion regret
  0.390t** (54% of decisions, max 3.38). Clairvoyance is decision-TOXIC -> this is exactly WHY antilife's
  searched-linear model COLLAPSED to 9.0 (strategy fusion poisons a majority of decisions).

**The raw outcome gap (0.225 vs 0.231) does NOT distinguish safe from toxic — the decision-level fusion regret
does (13x apart).** So you cannot look at "how much earlier does the search win" and decide whether to trust
its labels; you must look at whether the clairvoyant ARGMAX survives honest evaluation. Confirms: on high-fusion
decks (antilife/combo) the strong teacher (searched) is unusable, so we're stuck on the weak greedy teacher ->
a strong-AND-honest teacher (reshuffle-averaged search, E inside the max) is the ONLY lever there. On aggro,
searched is already safe and the model ~=captures it, so the reshuffle search adds little at the decision level
(EVPI~=0). NOTE the reshuffle search is a TRAINING-LABEL generator, NOT a play-time default (per user). TH has no
aligned rollout/searched pair dumped (counts differ: rollout 1699 vs train 1081) — antilife is the combo exemplar.

### Bootstrap teacher (Q^model = one policy-iteration step): helps antilife, does NOT compound (2026-07-09)

Followed the "stronger non-clairvoyant teacher" thesis with the CHEAP realization: use the trained d0 MODEL
as the rollout CONTINUATION (it reads only board/hand/lib-summary features, never library order -> non-clairvoyant
by construction, and stronger than greedy). Label candidate a = E_f[play a, then follow the model], averaged over
K reshuffles = Q^model(s,a) = one step of policy improvement over the model. Mechanism confirmed in code: the
rollout continuation goes through Solve, which re-ranks by the attached evaluator (TurnSolver.cpp:5214) when
MTG_EVAL_MODEL is set -> dumping rows WITH the d0 model attached makes the continuation Q^model. No new engine
code needed; no draw-order decoupling needed (the model can't peek).

Antilife (highest fusion regret = where theory predicts the biggest win), 150 games seed 20000 K=8, held-out
fresh seeds 4020-4023:
  heuristic 5.430 | v1 anchor (greedy cont) 5.300 | **v2 Q^model iter1 5.285 (best, better on ALL 4 seeds)** |
  v3 Q^model iter2 5.305 (REGRESSED past v2).
So: ONE bootstrap step helps (margin over heuristic 0.130 -> 0.145), but it does NOT compound — iter2 oscillates
back (approximate policy iteration wobble: on-policy state drift + label noise). **v2 is the keeper**
(logs/eval/antilife_d0_qmodel_v2.eval.json). Q^model labels have LOWER spread than greedy-cont (0.95 vs 1.32,
18% dead vs 7%) — the stronger continuation rescues more lines, so first-move differences compress (more honest:
greedy over-weighted first moves it couldn't recover from). Gain is real but modest.

### ★ Full-strength honest teacher: BUILT + tested — does NOT beat the cheap bootstrap (2026-07-09)

Built the draw-order-decoupled reshuffle-averaged search the prior NEXT STEP prescribed, and A/B'd it. **The
prediction ("bigger step than the bootstrap on high-fusion decks") is REFUTED. The cheap Q^model bootstrap is
the keeper.** The recoverable Vial/combo gap is NOT closed by a stronger *non-clairvoyant* teacher — consistent
with the EVPI finding that antilife's residual is mostly JUSTIFIED (irreducible) clairvoyance, which no
non-clairvoyant teacher can recover by construction.

**The build (committed, inert by default).** Extended the shuffle-decouple to the BASE draw order. New
thread-local `g_honest_teacher` (`GameState.h`) + `HonestTeacherGuard`; in `SimulateToEndImpl` (TurnSolver.cpp
~5441) when set and `depth>0`, the continuation chooses each turn's plan against a RESHUFFLED unseen library (a
random future) then RESOLVES it against the true order — so the depth>0 lookahead can't read the real draws
(`g_shuffle_eval` only decoupled mid-game shuffle EVENTS, not the opening order). Reshuffle salt folds
`shuffle_salt_search` (varied per-k by the dump loop) → the K outer samples get independent futures; applied at
EVERY continuation ply → the whole continuation policy is non-clairvoyant ("E inside the max" at every level).
The reshuffled lookahead passes `tt=nullptr` (the clairvoyant leaf memo keys on library SIZE, not order — reusing
it would return a real-order value for a reshuffled state). Wired via `MTG_EVAL_ROWS_HONEST` +
`EnumerateEarliestWins(..., honest)`. Byte-identical when off (baselines reproduce documented values exactly:
antilife heuristic 5.430, slivers 4.7178≈doc 4.718, TH 5.58).

**Antilife A/B — teacher held-recipe-constant (`--rank`, per-teacher best lam), d0, LP (loss=9), 150g/seed:**

| teacher (continuation) | tune seeds 4020-4023 | **fresh seeds 5050-5055** | won (fresh) |
|---|---|---|---|
| heuristic | 5.430 | 5.914 | 806/900 |
| Q^model bootstrap (v2, depth-0 model cont.) | 5.285 | **5.807 (best)** | **808/900** |
| honest_d1 (decoupled 1-ply cont.) | 5.270 (best) | 5.840 | 791/900 |
| honest_d2 | 5.283 | 5.860 | 791/900 |
| honest_d3 | 5.283 | — | — |

On the TUNING seeds honest_d1 edged the bootstrap by 0.015, but on FRESH seeds the ordering REVERSES: the cheap
bootstrap wins (5.807 < 5.840) AND wins more games (808 vs 791). The honest edge was tuning-seed noise/overfit.
Deeper honest lookahead (d2/d3) never beats d1 and converges to the bootstrap level — extra decoupled depth buys
nothing (and its no-memo cost is ~5× the bootstrap's O(1) continuation). **Verdict: the full-strength honest
teacher ≈ the cheap bootstrap on antilife, at much higher cost → not worth it.**

**Cheap-parallel bootstrap (Q^model, dump with the deck's d0 model attached, train `--rank`, A/B):**
- **slivers (Vial): GENERALIZES.** Bootstrap 4.3762 vs committed anchor 4.3838 on FRESH seeds (4004/9009/5050/6060,
  800g) = **−0.008 robust** (−0.015 on lam-select seeds 2002/3003/7007). Same direction + magnitude as antilife's
  one-step bootstrap gain → the ~0.008-0.015 one-step improvement is a real, deck-general property on high-fusion
  Vial decks. Saved `logs/eval/slivers_vial_qmodel.eval.json`.
- **TH (combo): COLLAPSES.** Bootstrap LP 7.15 vs committed searched 5.54 (heuristic 5.58) — much WORSE. TH needs
  the SEARCHED optimum; the rollout-style Q^model continuation durdles it (imitation label = the durdle trap for
  combo). Reconfirms the label recipe is deck-dependent: rollout/imitation (incl. Q^model bootstrap) for
  aggro/Vial, SEARCHED for combo/TH. Do NOT bootstrap TH.

**Net conclusion for the teacher-strength thesis.** Both the cheap bootstrap AND the expensive honest search land
at the same ~0.01-0.015 improvement ceiling on the high-fusion Vial/combo decks — because that residual is
justified clairvoyance (EVPI), not recoverable non-clairvoyant sequencing. The **cheap Q^model bootstrap is the
adoptable lever** (slivers/antilife: +~0.01 over the anchor, O(1) continuation, no engine build needed at
serve time). The honest-teacher machinery is kept (committed, gated) as the *correct* non-clairvoyant label
generator and the instrument that PROVED the ceiling is real — but it is not the path to a bigger gain. Artifacts:
`logs/eval/antilife_honest_d{1,2,3}.rows`, `logs/eval/anti-lifegain_honest_d{1,2}.eval.json`,
`logs/eval/{slivers_vial,treasure_hunt}_qmodel.{rows,eval.json}`; driver `scripts/honest_teacher_ab.py`.

### ★ Follow-up (user steer): reshuffle-avg search as PLAY, + push the MODEL with honest labels as TARGET (2026-07-09)

User: "brief test of reshuffle-avg search as PLAY (perf+quality), but keep focus on the MODEL — I don't believe
we've hit its limits; maybe use the reshuffle-avg search as the TARGET if not the training method." Both done.

**(A) Reshuffle-avg search as a PLAY policy — INERT or self-defeating (brief test).** Added `MTG_HONEST_PLAY`
(gates `g_honest_teacher` around the real search; inert by default, VERIFIED byte-identical). d5, held-out
2002/3003, heuristic-leaf: **dLP = +0.000 on antilife/slivers/TH, at 0.97-1.33× wall-clock.** Zero quality change,
pure overhead. Mechanism: `FullSearchLine` commits VERIFIED in-horizon wins (real simulation of the true library);
the tail decouple only perturbs the greedy ESTIMATE beyond the horizon, which never flips a committed line. A
*true* non-clairvoyant play policy would have to decouple the IN-HORIZON branching — which removes win
VERIFICATION, the search's entire strength on these verify-driven decks. So reshuffle-avg search as PLAY is either
inert (cheap tail decouple) or self-defeating (full decouple kills verification). Confirms non-clairvoyant play is
~search-verification-bounded here; NOT a viable play mode. Kept the gate as an instrument.

**(B) Model push with honest labels as the target — capacity helps ONLY the capacity-limited deck, marginally.**
The honest teacher gives a BETTER (de-clairvoyed, non-clairvoyant) target than rollout/bootstrap — antilife label
discrimination: honest 9.1% dead / spread 1.16 vs bootstrap 18.4% / 0.95 (richer gradient). Tested whether more
model CAPACITY finally exploits it (the prior "GBDT hurts Vial" was vs NOISIER rollout labels):
- **antilife (clairvoyance-limited, feature table-ceiling 0.658): capacity HURTS.** GBDT-honest 5.866/5.879 (fresh)
  vs linear-honest 5.840 vs bootstrap **5.807**. The extra honest-label spread is variance the trees overfit, not
  play signal — the EVPI wall (residual is hidden library order the features can't see).
- **slivers (capacity-limited, table-ceiling 0.978): GBDT-honest is the NEW BEST.** LP **4.3738** (fresh
  4004/9009/5050/6060, 800g) edges bootstrap 4.3762 and anchor 4.3838 (−0.010 vs anchor). Real but tiny (+0.0024
  over bootstrap). NB slivers honest labels have LOWER spread (0.731 vs rollout 0.982) — the de-clairvoyed
  continuation compresses first-move gaps — so there's little for capacity to grip; the win is at the margin.
  Saved `logs/eval/slivers_honest_gbdt.eval.json` (candidate best slivers d0).

**Reading (answers "have we hit the model's limits?").** On the tested decks, essentially YES for non-clairvoyant
d0: every model lever (teacher strength, capacity, form) now lands within ~0.01 of the same per-deck ceiling.
Capacity + a better teacher squeezes a marginal win ONLY where the deck is capacity-limited (slivers), and HURTS
where it's clairvoyance-limited (antilife) — exactly what the earlier feature_collision/linear_ceiling diagnostics
predicted (capacity helps iff table-ceiling is high AND labels beat the anchor). The one place with LARGE
headroom is COMBO (antilife/TH, model +~1.4 from search), and its bottleneck is FEATURES = the hidden library
order (clairvoyance), NOT capacity or teacher.

### ★ Combo feature lever TESTED + CLOSED — it's an INFORMATION limit, not a modeling one (2026-07-09)

User steer: "try richer combo features; ideally EXTRACT them naturally from the searched results rather than
hand-defining; if impractical, hand-defining beats nothing." Tested the natural-extraction path (which turns out
to be the practical one) and it does NOT move combo — because the wall is information, not representation.

**Key framing.** "Natural extraction" of plan×library interactions = let a **GBDT auto-discover** them: the
featurizer already emits BOTH plan features (plan_draw_engine, ...) AND the v6/v7 order-invariant library/hand
composition counts (lib_land_density_pct, lib_draw_engines, ...) + the v9 hand-defined `plan_dig_yield`. A GBDT
splits first on a plan feature (varies across a decision's candidates) then on lib_density (varies across
decisions) → it LEARNS "a dig-plan's value depends on library land-density" without anyone writing the product.
So hand-defining (v9) is just a baseline; the trees mine the interaction from the search's own rankings.

**Result — auto-interaction HURTS combo; the committed linear stays best (TH d0, held-out 2002/3003/7007, 150g):**
- committed linear (searched, NO lib feats): **LP 5.540** (best) ; heuristic 5.580.
- GBDT auto-interaction on searched rows (has lib feats), 3 capacities: 5.591 / 5.593 / 5.596 — all WORSE than
  heuristic.
- fresh LINEAR retrained WITH lib feats: 5.573–7.309 — worse, and it assigns **plan_draw_engine a large NEGATIVE
  weight** (overfits the searched-label correlation "cast dig ⇒ not-won-yet" → learns to AVOID the combo engine).

**Why it can't work (the decisive point).** The combo edge is the library **ORDER** — the search wins by reading
that the next few cards are a land clump (Land's Edge / Treasure Hunt). The v6/v7 features are order-INVARIANT
counts: "6 lands in 18" cannot encode "the next 3 are lands." So NO non-clairvoyant feature — composition,
auto-discovered interaction (GBDT), hand-defined product (v9), pairwise cross, or even a future NN over the same
inputs — can capture it, because they all read the same order-blind information. This is an INFORMATION-theoretic
limit (the EVPI/hidden-order wall), not a capacity / feature-engineering / representation limit. A fancier model
over non-clairvoyant inputs hits the identical wall.

**Conclusion (answers "have we hit the model's limits?"): YES, definitively, for non-clairvoyant d0.** Aggro is at
the imitation ceiling; combo is at the information ceiling. The committed TH searched-linear is TH's best d0; the
combo gap to search is irreducibly clairvoyant. Do NOT pursue richer combo features further. The value proposition
stands where it always was: the model matches/beats the HEURISTIC non-clairvoyantly at d0 speed, and the value
LEAF delivers search quality via cheap DEPTH — not by out-reading the search at d0.

Everything else is at the ceiling.

### ★ WHY the NC search falls short of the human — it DEPLOYS THE COMBO LATE (2026-07-09, mechanism traced)

Followed up CORRECTION 2 with the user: *why* does our NC policy lose to human play on the antilife references?
Traced per-game (matched openings, `MTG_DUMP_WINS` per-game turns + `--log-dir` game logs, `env -u`). Two components:

**(A) Systematic: NC deploys the combo ~half a turn late (the dominant, deterministic shortfall).** Matched over
the 30 reference games (K16 d2): **NC is +0.53 turns vs human, +0.93 vs clairvoyant.** The mechanism is a
**tempo/sequencing** failure — NC casts the enabler **Tainted Remedy** on average **turn 3.30 vs clairvoyant 2.78
(+0.52 later; later in 9/23 games)**, and that +0.52 combo-deploy delay maps almost 1:1 onto the +0.53 win-turn
shortfall. Concrete traces (same opening, byte-reproducible via `--seed S --game-index GI --games 1`):
- **gi22** (human T4, clair T4, NC T6): both hold Tainted Remedy in the OPENING hand. Clairvoyant deploys it T3 →
  Aria T4 → win. NC spends T3 on **Idyllic Tutor** (durdles for a piece it doesn't need), delays Remedy to T4,
  Aria T5–T6. Idyllic Tutor *usage is identical* across policies (8 vs 8) — it's not "tutors more", it's *when*.
- **gi15** (human T4, clair T4, NC T6): NC casts **Aria of Flame with no Remedy in play → opponent GAINS 10 life**
  (20→30; Aria ETB is "each opponent gains 10 life", inverted to −10 only with Remedy out). A 20-life swing thrown
  away. The human/clairvoyant hold Aria for the combo. An occasional misplay, downstream of the same "doesn't
  value on-curve combo deployment" root.

**Root cause:** the reshuffle-avg objective (min AVERAGE win turn over K futures, shallow d2 + greedy leaf) can't
reward on-curve combo deployment: (1) the **weak continuation** can't demonstrate the faster kill, so the objective
is ~indifferent to a 1-turn tempo loss; (2) **averaging** over futures rewards robust/consistent sequencing and
dilutes the specific fast line. The human, though non-clairvoyant, deploys on curve via combo KNOWLEDGE the flat
objective lacks. This is exactly why NC is a LOOSE ceiling estimate (CORRECTION 2) — and why a stronger teacher
would need a stronger (deeper/recursive) continuation, which is intractable. **The references remain the best
ceiling yardstick.**

**(B) Variance: finite-K Monte-Carlo noise → occasional bricks.** The loss set is UNSTABLE across K (K8→{5,13,15},
K16→{10,13}, K32→{5}) while win count climbs (28→29→30/31). Shifting losses = estimator noise, not a fixed
structural failure on specific games; reducible with K but expensive (K32 d2 ≈ minutes/deck). Data:
`logs/eval/{alln,allc}` game logs; per-game table in session notes; traces `logs/eval/tr_*`, `g22{n,c}`.

### ★★★ THE MULLIGAN CONFOUND — benchmark human vs search on IDENTICAL hands (2026-07-09, user caught it)

The whole human-vs-search benchmark was confounded: my runs used the goldfish/search runner, which makes its OWN
mulligan decision from the profile, while the references' hands came from the human's mulligan. **Only 14/30 antilife
kept hands actually matched** (and 11 references mulliganed). So every "human vs clairvoyant/NC" number above compared
DIFFERENT opening hands. FIX: wired `--force-mulligan "<count>:<nums>"` (previously claude-play-only) into
`GoldFishRunner` (validated: reconstructs the human's exact kept hand byte-for-byte). Re-ran with each reference's
exact hand forced onto all policies:

| policy | LP, IDENTICAL hands | LP, autonomous (confounded) |
|---|---|---|
| clairvoyant d5 | 4.233 | 4.097 |
| **human** | **4.500** | 4.500 |
| **NC honest K8 d2** | **4.733** | ~5.0–5.27 |
| NC q25 / q10 | 5.033 / 4.867 | (looked ~equal) |

**Findings:** (1) The mulligan confound was hiding **~0.3 turns** — forcing identical hands drops NC from ~5.0 to
**4.733**, so the play gap to human is only **0.23** (not 0.53), and clairvoyance is worth **0.27** (human 4.50 →
clairvoyant 4.23). (2) The optimism "lever" (q25/q10) was a **confound artifact** — on identical hands the honest
MEAN is best; optimism is strictly worse. (3) Corollary: the engine's autonomous mulligan differs a lot from the
human's (it over-mulligans on many keep-7s) — a separate MULLIGAN-quality issue from the PLAY search. **Method rule
going forward: benchmark the search against references ONLY with `--force-mulligan`, never autonomous mulligan.**
Data: `logs/eval/forced_bench.log`; driver `/tmp/forced_bench.py` (per-ref exact-hand replay).

### ★★★ ON IDENTICAL HANDS, THE PLAY SEARCH IS AT HUMAN PARITY (within noise) (2026-07-09)

With the mulligan confound removed (force-mulligan), chased the residual NC-vs-human PLAY gap on the 30 antilife
references. Per-game paired diff (NC − human), identical hands: **mean +0.233, but NOT significant** — bootstrap
95% CI **[−0.067, +0.533]** (includes 0), t=1.49. NC is FASTER than the human on 4 games, slower on 8, tied on 18.
So the play search is statistically indistinguishable from human play once hands are controlled. The earlier
"0.5–0.9 gaps" were entirely mulligan confound + the env-presence measurement bug.

Tracing the slower games shows the errors are REAL but small and self-cancelling: gi11 skips a turn-1 fetchland
(+3), gi9 plays its turn-1 mana dork a turn late / fetches the wrong color to enable it (+2) — both are ramp/tempo/
mana-sequencing imperfections a human avoids. But they're offset by games where NC beats the human, and blunt fixes
just move them around: clairvoyant-continuation FIXED gi9 (−3) but REGRESSED 5 other games (+1 each) → net wash
(4.767 vs 4.733); optimistic root modes (q25/q10) are strictly worse on clean hands. So there's no free uniform win
in the play policy — it's near its practical floor, and the remaining measurable diffs are within the 30-game noise.
The one SIGNIFICANT, irreducible gap is clairvoyance: human/NC ≈ 4.6–4.73 vs clairvoyant 4.23 (~0.4). Rejected the
MTG_NC_ROOT_MODE / MTG_NC_CLAIRV_CONT experiment gates (reverted — both non-helpful on clean hands). **Conclusion:
the play component is not where quality is left on the table; mulligan quality + more reference data (to tighten the
CI) are the better targets.** Data: `logs/eval/{forced_bench,fb_cc,ncf_pergame}.log`.

### ★★ A DEEPER TEACHER DOES NOT HELP — depth was never the bottleneck (2026-07-09, user asked to try d5–d8)

Hypothesis (user): the durdle is myopia — a deeper continuation (d5–d8, cost OK for a training-only teacher) would
see the faster kill and deploy the combo on curve. **Tested and REFUTED.** Matched on the same 26 antilife games at
the same K8:

| policy | won | LP | Tainted-Remedy deploy turn |
|---|---|---|---|
| clairvoyant | 26/26 | 4.077 | 2.96 |
| human | — | 4.500 | — |
| NC **d3** K8 | 23/26 | 5.385 | **3.41** |
| NC **d5** K8 | 22/26 | 5.462 | **3.41** |

d3→d5 is **byte-identical on Remedy-deploy (3.41)** and 25/26 games identical win-turn (1 *worse*). Deeper is, if
anything, slightly worse (a stronger continuation *rescues* the durdle line in the averaged futures, flattening the
tempo signal further). **Control that explains it: the CLAIRVOYANT search is depth-SATURATED at d2** — antilife
clairvoyant LP is byte-identical 4.0968 at d2 = d3 = d5. The deck's kill is within a 2-ply horizon, so lookahead
depth was never the binding constraint for *anyone*; the entire NC shortfall is the **averaging objective**
(`max_π E_f[win_turn]` rewards robustness / washes out tempo) + finite-K variance, neither of which more depth
touches. Deep rollout is also prohibitively slow (d5 K8 stalled for many minutes on the long games). **Conclusion:
do NOT pursue a deeper-continuation teacher.** Levers that could actually move it (unproven, for discussion): a
tempo-sensitive objective (quantile/best-of instead of mean — but reintroduces fusion optimism), explicit
combo-assembled→deploy knowledge (hand-tuning the project resists), or imitation-learning from the human references
(the real near-ceiling policy). Data: `logs/eval/deep_d{3,5}`.

### ⚠️ "EVPI IS SMALL" WAS AN OVERSTATEMENT — it's UNRESOLVED and the baseline is confounded (2026-07-09, user pushback)

The user challenged "EVPI is small." Correct to challenge — I conflated an **upper bound** with a measurement.
What we can actually measure is `human_LP − clair_LP` on the exact reference openings (single-game exact replay,
`--seed S --game-index G --games 1`, `env -u`, depth 5), which upper-bounds EVPI ONLY because a human is *some*
non-clairvoyant policy (ceiling ≤ human). **K does NOT enter — both terms are K-free** (human = references,
clair = ordinary search). Per deck:

| deck | n | human_LP | clair_LP | human−clair (loose EVPI upper bd) |
|---|---|---|---|---|
| antilife | 30 | 4.500 | 4.100 | **+0.400** |
| burn | 16 | 4.625 | 4.438 | **+0.188** |
| slivers | 4 | 4.000 | 4.000 | +0.000 |
| TH | 1 | 5.000 | 5.000 | +0.000 (n=1, useless) |
| knights | 3 | 4.667 | **5.000** | **−0.333** |
| Hinata | — | (7, ref) | — | clairvoyant search INTRACTABLE at d5 (>4 min/game, combo explosion) |

**Three reasons these are NOT clean EVPI:** (1) `human−clair = EVPI + human-suboptimality − our-search-suboptimality`
— three tangled terms. (2) **Our "clairvoyant" search is NOT the clairvoyant optimum**: on knights a HUMAN beats it
by 0.33 (and antilife gi25 human T4 < clair T5), so `clair_LP` is not a valid lower bound on the ceiling there —
the baseline itself is soft. (3) fetchland reshuffles diverge the library once policies differ, adding per-game
noise. So: antilife EVPI ∈ [0, 0.40], burn ∈ [0, ~0.19], slivers ≈ 0 — these upper bounds ARE well below the
retracted 0.6–0.79 table (that table WAS overstated), but 0.40 on a turn-4 deck is ~10% of the clock — **NOT
"small"**, and the true EVPI within [0, bound] is genuinely unresolved. Clean EVPI is blocked from BOTH ends: the
NC ceiling estimator is weak (durdles), and the clairvoyant baseline is itself beatable. Honest status: **EVPI is
un-pinned per deck; do not claim it's small.** Data: `logs/eval/evpi_bound.log`.

### ⚠️⚠️ CORRECTION 2 — the EVPI numbers below are WRONG: a HUMAN beats our NC policy (2026-07-09, user review, DEFINITIVE)

The whole ceiling table below is **retracted as an EVPI measurement.** Two independent errors, both found by
running the user's suggested test — compare against the hand-played **references** (genuine non-clairvoyant human
play), same openings:

1. **Measurement bug: the "clairvoyant baseline" was accidentally the NC policy.** `MTG_NC_SEARCH` is gated on env
   **presence** (`getenv(...) != nullptr`), so `MTG_NC_SEARCH=` (empty *but present*, as a bash `VAR= cmd` prefix)
   turns NC **on**. Several baselines were measured that way, so "clairvoyant vs NC" was NC-vs-NC. Corrected with
   `env -u MTG_NC_SEARCH`: **real antilife clairvoyant search = ~turn 4.0, 100% wins** (not the 4.075/4.79 quoted).
   The deck is as fast as the user said. (Fix in your own scripts: `env -u`, or use `scripts/nc_ceiling.py` which
   `env.pop`s the vars correctly.)

2. **Our NC policy is WEAKER THAN A HUMAN → it overstates EVPI by ~3×.** On antilife's 30 references (seeds 1–31,
   same openings): **human 30/30 @ 4.50**, **clairvoyant search 31/31 @ 4.097**, **our NC policy 28/31 @ 4.82
   (LP 5.23)**. The human — a non-clairvoyant player — **wins every game our "non-clairvoyant ceiling" policy
   loses.** So `ReshuffleAvgChoosePlan` is a poor lower bound on the ceiling, and its 1.13 gap to the search is
   mostly *policy weakness*, not clairvoyance value.

**The real EVPI is SMALL.** The true non-clairvoyant ceiling is bounded: `clairvoyant ≤ ceiling ≤ best-known-NC`.
Best-known-NC = the human (30/30 @ 4.50), and the mulligan *table* mulligans BETTER than a human (user), so the
ceiling with ideal mulligans is **< 4.50**. Thus **antilife EVPI = ceiling − clairvoyant ≤ 4.50 − 4.097 = 0.40**,
and likely well under that. Not 0.79. Same logic voids TH's "0.6" (only 1 TH reference @ turn 5, but clairvoyant
TH is ~4.1–4.2 at ~100% — a fast deck; the 0.66 NC-gap is an upper bound we know is loose). **The best available
ceiling yardstick is the references (human play), NOT `ReshuffleAvgChoosePlan`.**

What still stands from CORRECTION 1: forward simulation beats the static d0 model (NC d0 >> static d0). And the
second-main fix (below) is real and kept — `SimulateToEndImpl` no longer `Solve`s the continuation second main
greedily under `g_honest_teacher` (searches it via reshuffled `SolveWithLookahead`); inert in production (the guard
is never set there). It's a correct strengthening but does NOT make NC a trustworthy ceiling — the human still beats
it. Data: `logs/eval/{th_highk,nc_secondmain_fix}.log`; references `references/Anti-Lifegain/*.json`.

---
### ⚠️ CORRECTION 1 (SUPERSEDED by CORRECTION 2) — NC numbers overstate EVPI (2026-07-09, user review)

The user flagged the EVPI gaps as too large — esp. TH (0.6), antilife (0.79), burn (0.15) — and was RIGHT.
`ReshuffleAvgChoosePlan` is a WEAK non-clairvoyant policy → LOWER BOUND on the ceiling → OVERESTIMATES EVPI.
Weaknesses: (1) greedy/heuristic CONTINUATION can't pilot a multi-turn combo forward; (2) under-powered K/depth
(TH d2 still dropping at K32); (3) greedy second main (now fixed). CORRECTION 2 supersedes this with a concrete
number: the human (a real NC player) beats the NC policy, so the gap is policy weakness, and real EVPI ≤ 0.40.

### ★★ THE NON-CLAIRVOYANT CEILING — measured (reshuffle-averaged search as a play policy) (2026-07-09)

Built the real thing the prior sessions kept deferring: `TurnSolver::ReshuffleAvgChoosePlan` (gate
`MTG_NC_SEARCH`, `MTG_NC_K`/`MTG_NC_DEPTH`, inert by default). At each real decision it ranks candidate
plans by their win turn AVERAGED over K reshuffled futures (common random numbers across candidates) with an
honest depth-D continuation (each ply re-planned against a fresh reshuffle → non-clairvoyant at every level;
depth 0 = greedy non-clairvoyant rollout). The chosen plan executes against the TRUE library; re-decide each
turn. As K/depth grow this is the strongest tractable non-clairvoyant policy = the CEILING the user asked to
measure. Swept (K,depth) vs references, quality (LP, loss=9) AND wall-clock, 100g × seeds 2002/3003.
Full data: `logs/eval/nc_ceiling_results.txt`; driver `scripts/nc_ceiling.py`.

**The ceiling table (LP; NC ceiling = best cell, ~K8 d2):**

| deck | heur d0 | d0-model | **NC ceiling** | heur d1 (clairv.) | search (d5) | headroom d0→ceiling | EVPI ceiling→search |
|---|---|---|---|---|---|---|---|
| slivers | 4.740 | 4.460 | **4.275** | 4.360 | 4.265 | **0.185** | ~0 |
| knights | 4.515 | 4.435 | **4.325** | 4.325 | 4.315 | **0.110** | ~0 |
| burn | 4.590 | 4.575 | 4.520 | 4.370 | 4.365 | 0.055 | 0.155 |
| TH | 5.655 | 5.615 | **4.805** | 4.275 | 4.205 | **0.810** | 0.600 |
| antilife | 5.625 | 5.515 | **4.865** | 4.135 | 4.075 | **0.650** | 0.790 |

**Findings (this is the money data):**
1. **The non-clairvoyant ceiling is MEANINGFULLY ABOVE the static d0 model on every deck** (0.055–0.81). So the
   d0 model is NOT at the non-clairvoyant limit — the earlier "we've hit the model's limit" was true only for
   the STATIC d0 *function class*. Planning-under-uncertainty is a real, unclaimed lever.
2. **The static d0 model captures almost NONE of it — because it can't simulate forward.** Even NC **depth-0**
   (a greedy rollout merely AVERAGED over K futures, the cheapest possible forward sim) crushes the static model:
   antilife 4.925 vs 5.515, TH 5.055 vs 5.615, slivers 4.445 vs 4.460. The value was never in a better static
   evaluator (why every capacity/feature/teacher lever plateaued) — it's in DOING the rollout at decision time.
3. **Two regimes.** (a) **Aggro-Vial (slivers/knights): NC ceiling ≈ the clairvoyant search (EVPI≈0)** — a
   non-clairvoyant policy loses essentially nothing vs reading real draws; on slivers NC-d1 (4.330) even BEATS
   the clairvoyant heuristic-d1 (4.360) — averaging over futures > a shallow real-draw peek. (b) **Combo
   (TH/antilife): NC ceiling far ABOVE the search (EVPI 0.6–0.79 = irreducible clairvoyance) but ALSO far above
   d0 (headroom 0.65–0.81)** — you can't read the land clump, yet forward-sim-under-uncertainty still recovers a
   lot. burn is in between (moderate EVPI 0.15; its d1 gain is clairvoyant burn-sequencing).
4. **Convergence:** NC plateaus by **depth 2** (d3 = d2 or slightly worse from variance); **K8** suffices (K16
   marginal / noisier on durdle decks). Aggro gets most headroom at depth 0–1; combo needs d1–d2.

**Performance / the speed bar for a learned lookahead (ms/game, 12 threads):**

| deck | d0-model | value-leaf d5 (clairv. search) | **NC ceiling (K8 d2)** | NC d3 |
|---|---|---|---|---|
| slivers | 1.3 | 3.3 | 120 | 1436 |
| knights | 7.8 | 9.2 | 48 | 437 |
| TH | 17 | 22 | 103 | 892 |
| antilife | 191 | 147 | 270 | 816 |

The NC search is **6–90× the d0 model** and 5–40× the (clairvoyant) value-leaf search; depth 3 is prohibitive
(0.4–1.4 s/game). So the ceiling is expensive — exactly the setup for a learned lookahead that "pays
significantly less," per the user. NB the reference `value-leaf d5` already matches the clairvoyant search
cheaply (2–22 ms) — but it is CLAIRVOYANT; the non-clairvoyant equivalent is a value leaf inside the NC search.

### Toward a learned lookahead — what amortises the NC search, and what doesn't (2026-07-09)

Two amortisation attempts, guided by the ceiling data (the goal: reach the NC ceiling far cheaper).

**(1) Value LEAF inside the NC search (replace the greedy rollout with the O(1) value model).** Matched (K,depth)
gives IDENTICAL quality (slivers/TH/antilife K8 d2 unchanged) but only helps COST where rollouts are long:
antilife K8 d2 270→132 ms (~2×), K8 d1 205→97 ms; slivers/TH unchanged (~0). Diagnosis: the bottleneck is the
reshuffle-averaged **branching** (K × #plans × depth turns), not the leaf — so the leaf only pays off
proportionally to rollout length (durdle decks). A modest cost-reducer, not the win.

**(2) Fully-static amortisation (train a d0 model to REPRODUCE the NC ranking) — the crux.** The ceiling data
already shows why this is hard: even NC **depth-0** (a greedy rollout merely averaged over K) beats the static d0
model by 0.5–0.6 turns on combo — because the winning quantity is *the expected outcome of playing forward*,
which is a rollout, not a feature function. A static evaluator (any capacity) cannot represent an expectation
over trajectories it doesn't run.

Confirmed it end-to-end (the crux experiment): dumped states from NC play itself (ON-POLICY, no covariate
shift) labelled with the ceiling's own honest reshuffle-avg K8/d2 signal, trained linear + GBDT, A/B'd d0:

| deck | d0-model | best ON-POLICY static (lin/GBDT) | NC ceiling | headroom captured |
|---|---|---|---|---|
| slivers | 4.460 | 4.440 | 4.275 | **~11%** |
| antilife | 5.515 | 5.495 | 4.865 | **~3%** |

Even trained on-policy on the ceiling's exact labels, the static model plateaus at ~d0 (captures 3–11% of the
headroom). Rows `logs/eval/{slivers,antilife}_ncpolicy.rows`; sidecars `*_ncpolicy_{lin,gbdt}.eval.json`.

**Verdict on "can a more complex model reach the non-clairvoyant ceiling?"** A more complex *static evaluator*: NO
(proven three ways now — feature/capacity/teacher plateau, and now on-policy imitation of the ceiling itself). To
reach the ceiling you must SIMULATE FORWARD at decision time. The options and their standing:
- **Run the NC search directly** — reaches the ceiling; costs 48–270 ms/game (6–90× d0). Fine for the project's
  OFFLINE deck-comparison purpose (≈ minutes per deck at 1000 games); not a cheap real-time policy.
- **Value leaf inside NC** — same quality, ~2× cheaper only on long-rollout (durdle) decks; branching still dominates.
- **A model that learns forward DYNAMICS** (recurrent / latent-rollout / MuZero-style, amortising the branching, not
  just the leaf) — the ONLY path to a cheap ceiling-quality non-clairvoyant policy, and a large speculative build.
  Its payoff hinges on whether latent dynamics are learnable from these features (the same information the static
  eval has), which the static-imitation failure makes uncertain. This is the honest "learned lookahead" frontier.

### ▶ NEXT STEP (for the next session)

The whole non-clairvoyant landscape is now MAPPED: the d0 STATIC model is at its ceiling (teacher/capacity/feature/
on-policy-imitation all plateau), and the non-clairvoyant POLICY ceiling (reshuffle-averaged search) sits 0.06–0.81
above it — reachable only by simulating forward at play time (no static shortcut). Open directions, by EV:
1. **ADOPTION** (top lever, needs user sign-off). Lock in `decks/*.eval.json` = {knights: knights_d0_gbdt,
   antilife: antilife_d0_qmodel_v2, slivers: slivers_vial_qmodel (or slivers_honest_gbdt), burn/TH: linear
   anchors}, flip `MTG_EVAL_MODEL` default on-when-present, rebaseline smoke/regression GT. Deliberate GT change.
2. **Adopt the NC search as an OFFLINE analysis policy** — it reaches the non-clairvoyant ceiling and is affordable
   for the project's deck-comparison purpose (48–270 ms/game). Would need a `decks/*` opt-in + accepting the cost.
   No new build; `MTG_NC_SEARCH` exists. This is the concrete way to actually PLAY closer to the ceiling today.
3. **Learned forward-dynamics model (MuZero-style)** — the only path to a CHEAP ceiling-quality policy; large
   speculative build; payoff uncertain (static imitation of the ceiling failed, so latent dynamics may be equally
   information-limited). The honest "learned lookahead" frontier the user asked about.
4. **Combo is clairvoyance-bounded** (information limit, CLOSED); the value LEAF (cheap clairvoyant depth) is its
   answer, not a smarter non-clairvoyant model.

The non-clairvoyant d0 model is now fully characterized (at its per-deck ceiling everywhere). The live levers are
(1) ADOPTION and (2) reshuffle-averaged search as a PLAY mode (the harder, unbuilt in-horizon decouple — the cheap
tail decouple was measured inert this session). If continuing the MODEL thread, the only remaining upside needs
NEW INFORMATION (interactive opponent / phase 2), not a better non-clairvoyant d0.

MEMORY NOTE (env): the label DUMPS (EnumerateEarliestWins, uncapped memo `FromVirtualMs(1000000)`, TurnSolver.cpp:5999)
are memory-heavy on durdle decks x threads; run ONE at a time. WSL2 lags returning freed anon pages (looks like
near-OOM for ~20-60s, self-reclaims). d0 A/B runs are light.

---

## NC SEARCH QUALITY — the reshuffle-averaged objective is MANA-OPTIMISTIC; a land-drop tempo bonus recovers most of the gap (2026-07-10)

Picking up "continue investigating non-clairvoyant search quality." Built `scripts/ref_bench.py` — the ONLY
correct per-game reference comparison: for each `references/<deck>/*.json` it forces the ref's exact opening hand
(`--force-mulligan`) into BOTH the clairvoyant search (depth 5, no NC env) and the NC reshuffle search
(`MTG_NC_SEARCH` K8 d2), beside the human's saved win turn. (Memory `--game-log-dir` is manifest-only; the goldfish
runner writes JSON decision logs via `--log-dir`.) ⚠️ **The NC search uses ~3 GB RSS/game** (uncapped reshuffle
memo) — run ≤2–4 concurrent or the OOM killer SIGKILLs siblings (`rc=-9`). Stream rows so a mid-run OOM keeps partial data.

**Clean antilife per-game result (n=30, identical hands, no losses so LP = avg win turn):**
clairvoyant **4.133**, human **4.500**, NC **4.767**. The 0.267 NC-vs-human gap is CONCENTRATED, not diffuse:
gi11 (NC 8 vs human/clair 5), gi9 (NC 7 vs human 5, clair 4), gi8 (NC 6 vs human 4), plus five +1 games. The old
memory "gi15 Aria-without-Remedy" misplay is RESOLVED (NC now byte-identical to human/clair — the reconciled fixes
fixed it). Do not trust the stale per-game notes; re-measure with ref_bench.

**gi11 traced — NC SKIPS ITS TURN-1 LAND DROP.** Hand after 1 mull = {2 fetchlands, Plague Drone, Swords, 2×Invigorate}
(a 2-lander). NC plays NEITHER fetch on T1 (life stays 20), stays a land behind all game, deploys the kill T8; clair
and human play a fetch T1 and win T5. NOT clairvoyance (clair=human=5), NOT horizon (byte-identical durdle at NC
depth 3 AND 4), NOT a CRN/reshuffle break (`ShuffleAfterSearch` is a no-op by default, so a fetch-crack preserves the
common-random-number future). The `MTG_NC_DEBUG` instrument (TurnSolver.cpp, in ReshuffleAvgChoosePlan) prints the
per-plan sums at T1:
```
play Wooded Foothills sum=58(avg7.25) | play Bloodstained Mire 56(7.00) | DEFER 52(6.50)   <- defer strictly best
```
Deferring scores **0.5 avg-turns "better"** — and it's exactly BACKWARDS (defer→T8, land→T5). ROOT CAUSE: the
reshuffle averaging shuffles the TRUE (here land-screwed) library away, so its mean future has NORMAL land density.
The objective is **optimistic about mana** — it can't see that THIS game is mana-light, so the T1 land's value ≈ 0
while its costs (1 life to crack + one fewer library land) are slightly negative. On T2+ the calculus flips and NC
does play lands; only the do-nothing-else T1 drop is skipped. This is the general "averaging washes out tempo"
limitation, now pinned to a specific mechanism and decision.

**FIX — `MTG_NC_TEMPO=<avg-turns>` land-drop bonus (default 0 = off, byte-identical).** In ReshuffleAvgChoosePlan,
subtract `round(TEMPO*K)` from any plan that makes its land drop before taking the min. A land drop is screw-insurance
the mean-objective under-prices; the bonus prices it back. It never overrides a real win-turn difference bigger than
TEMPO. Swept on refs (`scripts/nc_tempo_sweep.py`, forced hands, NC-only), LP by TEMPO:

| deck | human | t=0 | t=0.5 | t=0.7 | t=1.0 |
|------|-------|-----|-------|-------|-------|
| antilife (n30) | 4.50 | 4.77 | 4.60 | 4.60 | **4.57** |
| burn (n16)     | 4.62 | 4.44 | 4.38 |  —   | 4.38 |
| slivers (n4)   | 4.00 | 4.00 | 4.00 |  —   | 4.00 |

**ZERO games regressed at any TEMPO on any deck** (no fetch-in-hand / Land's-Edge blow-up on burn — the win-turn
objective still keeps lands when pitching them matters). Antilife gap to human 0.27→0.07 (t=1.0) / 0.10 (t=0.5);
burn already beats human and improves; slivers flat. **t=0.5 captures most of the win at half the override risk**;
t=1.0 wins the marginal gi11 6→5. Fixes: antilife gi11 (−3), gi10/gi29/gi5 (−1 each), burn gi12 (−1).

**RESIDUAL after tempo (a DIFFERENT, finer class — land SELECTION / on-curve sequencing).** gi9 is tempo-INVARIANT
(7 at every TEMPO): NC plays *a* T1 land but the WRONG one — **Godless Shrine (W/B shock) instead of a green fetch**
— so it can't cast its T1 mana dork **Ignoble Hierarch** and delays it to T3 (clair plays Windswept Heath→green→Hierarch
T1, wins T4). This is the memory "dork-late / wrong-color" case: not a missed drop but a colour-enabling land choice +
mana-dork ramp the averaged shallow objective under-values (same mana-optimism, but the lever is *which* land, not
*whether*). gi21/gi22/gi28/gi1 (+1 each) are the same family. A land-drop bonus can't reach these; they'd need a
curve/colour-coverage term (value the land that maximises castable spells / enables a ramp creature) — harder, and
riskier to encode. The mana-dork ramp EvalCard fix (MTG_DORK_RAMP) doesn't reach the NC ranking (NC ranks by averaged
win-turn, not static eval).

**STATUS:** land-drop tempo bonus is a clean, safe, measured win that moves NC into a satisfactory range vs the human
references (antilife within 0.07–0.10, burn/slivers ≥ human). It's a HEURISTIC judgment the averaged search genuinely
can't resolve from its objective (per heuristic-optimization skill). Flag lives in TurnSolver.cpp behind
`MTG_NC_TEMPO`; `MTG_NC_DEBUG` prints the tie/sum structure. Adoption target = the NC-search config (teacher/ceiling),
NOT a shipped provider — pending user sign-off on a value (0.5 recommended). Repro: `scripts/ref_bench.py --deck <d>`,
`scripts/nc_tempo_sweep.py --deck <d> --tempos 0 0.5 1.0`.

### Tempo bonus is DECK-DEPENDENT, not a universal win — broad autonomous validation (2026-07-10)

User (correctly): "this is a general change, test it extensively, not just on my reference files — especially if we
want to turn it on in general." Two refinements + a broad sweep followed.

**GATE added: `MTG_NC_TEMPO_LANDS` (default 99 = ungated).** The bonus now applies only while the active player
controls FEWER than this many lands (still building the mana base). Rationale (user: "even turn 2 is safe, but with
TH you may want to wait for a Treasure Hunt to resolve before playing" a land): the mana-optimism pathology is
EARLY; late, a land in hand can be a RESOURCE not a wasted drop — TH's win-con is **Treasure Hunt + Land's Edge**
(`Discard a land: 2 damage`), so lands are ammunition and forcing a late drop throws away reach. A lands-in-play gate
(not a raw turn cap) self-adjusts: it switches off right as TH reaches ~2-3 lands / casts Treasure Hunt, yet still
fires if you're genuinely land-light late. Gating to L<2 costs ~0.06 of the antilife gain (a few antilife land drops
that helped happened at 2+ lands) — a real safety/benefit trade.

**Thread-invariance confirmed:** per-game seed = base+game_index, independent of `--threads`; antilife 40g at threads
2 vs 8 = byte-identical (37 won, 4.64865). So the NC sweep scales to many threads (bumped 2→6) with no result change.
(The ~3 GB/game OOM risk is only the FORCED-hand durdle runs; autonomous mulligan games are light + fast.)

**BROAD AUTONOMOUS SWEEP (`scripts/nc_tempo_bigsweep.py`, 150 games/deck, fresh seed 40000, disjoint from refs 1-31
and regression):** this is the real no-regression gate — the runner mulligans + plays itself over many seeds, not the
30 hand-played hands. dLP vs baseline (loss-penalised avg win turn, lower=better):

| deck | baseline LP | t0.5_L2 | t0.5_L3 | t1.0_L2 | read |
|------|-------------|---------|---------|---------|------|
| antilife | 4.860 | **−0.060 (+2 won)** | −0.053 | −0.053 | clear HELP |
| burn     | 4.340 | **+0.013** | +0.013 | +0.013 | tiny HURT (consistent) |
| slivers  | 4.247 | 0.000 | 0.000 | 0.000 | neutral |
| knights  | 4.420 | 0.000 | 0.000 | 0.000 | neutral |
| TH       | 4.793 | −0.007 (−1 won) | −0.000 (−1 won) | −0.007 (−1 won) | ~neutral, faster wins but 1 more loss |

**VERDICT: do NOT turn the tempo bonus on globally.** It clearly helps only the mana-hungry combo deck (antilife);
it's a small but consistent HURT on burn (+0.013) and gives TH a subtle faster-but-loses-1 tradeoff. The reference
subset had shown burn IMPROVING — an artifact the 16-hand sample missed and the 150-game sweep caught. This is an
ARCHETYPE-LEVEL knob: adopt on antilife (and decks like it — mana-hungry, no land-as-resource mechanic), leave OFF
on burn/TH (land-pitch decks) and where neutral. A wider run (400 games, ungated L99 controls, seed 50000) is
in flight to (a) confirm the antilife gain and (b) separate burn's +0.013 / TH's −1 from noise with more power.

### DECISIVE (400-game powered sweep): the GATE is load-bearing — gated tempo is a clean no-regression win (2026-07-10)

Re-ran the broad sweep at 400 games/deck (seed 50000, threads 6 — thread-invariant), adding UNGATED (L99) controls.
The 150-game small effects were partly noise; the powered numbers (dLP vs baseline, lower=better):

| deck | baseline LP | gated t0.5_L2 | gated t1.0_L2 | UNGATED t0.5_L99 | UNGATED t1.0_L99 |
|------|-------------|---------------|---------------|------------------|------------------|
| antilife | 4.670 | -0.017 (+1) | -0.017 (+0) | -0.028 (+3) | -0.040 (+3) |
| burn     | 4.460 |  0.000 | -0.003 | -0.008 | -0.010 |
| slivers  | 4.300 |  0.000 |  0.000 |  0.000 |  0.000 |
| knights  | 4.365 |  0.000 |  0.000 |  0.000 |  0.000 |
| TH       | 5.047 | -0.022 (+2) | -0.017 (+2) | +0.043 (-8) | +0.143 (-18!) |

THE GATE IS LOAD-BEARING; the user's TH instinct was the crux. UNGATED tempo helps antilife/burn MORE but is
CATASTROPHIC on TH (t1.0_L99 loses 18/400 games -- playing lands late that should be Land's-Edge ammo). The
lands-in-play GATE confines the bonus to the mana-building phase and turns TH into a small GAIN (+2 won). With the
gate the bonus is NEUTRAL-OR-BETTER on all five decks -- ZERO regressions. Corrections vs the 150-game/reference reads:
burn's "+0.013 hurt" was NOISE (gated ~0); TH's "-1 loss" was NOISE (gated +2). Antilife gain is REAL but MODEST on
random games (-0.017 gated) vs the concentrated reference blunders (gi11 -3), because land-skip blunders are rarer in
random games than in hand-played refs.

RECOMMENDATION: adopt the GATED tempo bonus for the NC search -- MTG_NC_TEMPO with MTG_NC_TEMPO_LANDS~2. Safe to
enable generally ONLY because of the gate (ungated is not). t1.0_L2 catches the concentrated blunders most fully
(gi11 -> T5) at zero measured regression; t0.5_L2 is the conservative pick. NC-search (teacher/ceiling) knob, not a
shipped-search change. Repro: scripts/nc_tempo_bigsweep.py --games 400 --seed 50000 --threads 6. Remaining NC residual
(unfixed by tempo): the dork-late/wrong-COLOUR land-SELECTION class (gi9), a curve/colour-coverage lever, deferred.

### Moved the tempo bonus INTO THE PROVIDER LAYER (safe generic default + archetype overrides) (2026-07-10)

User: "isn't this flag a heuristic anyway? implement a safe rule in the GenericProvider, potentially overrides for TH
and Anti-Lifegain as needed -- so it doesn't bite new decks but can be aggressive where needed." Correct: the env flag
was the A/B SELECTOR stage (per the heuristic-optimization skill); adoption = the provider. The codebase already had
the mirror concept -- PreferHoldLandDrop (BurnProvider banks lands for Searing Blaze landfall) + the main search's
develop-vs-hold tiebreak (TurnSolver.cpp:5253). The MTG_NC_TEMPO_LANDS env gate was a crude global proxy for that.

New Hook 22 DecisionProvider::NcLandDropTempoBonus(state, controller) -> double (avg-turns bonus for a land drop):
- base default 0.0 (inert; unknown decks unaffected).
- GenericProvider = SAFE: 0 if PreferHoldLandDrop; else lands_in_play < 2 ? 0.5 : 0.0 (turns 1-2 only, pure tempo).
- AntiLifegainProvider = AGGRESSIVE: 1.0 ungated (dorks + on-curve enabler, no land-as-resource -> always develop).
- ReshuffleAvgChoosePlan calls the provider by default; MTG_NC_TEMPO/_LANDS override with a flat gated bonus (A/B).

400-game autonomous validation (nc_tempo_bigsweep.py seed 50000, "provider" = env unset), dLP vs baseline:

| deck | provider (ADOPTED) | global gated t1.0_L2 | global ungated t1.0_L99 |
|------|--------------------|----------------------|-------------------------|
| antilife | -0.040 (+3 won) | -0.017 | -0.040 |
| burn     | 0.000 | -0.003 | -0.010 |
| slivers  | 0.000 | 0.000 | 0.000 |
| knights  | 0.000 | 0.000 | 0.000 |
| TH       | -0.022 (+2 won) | -0.017 | +0.143 (-18 won!) |

Provider default is STRICTLY BETTER than any global setting: full antilife gain (-0.040) via the archetype override,
while the generic safe rule keeps TH a small GAIN (-0.022) where global ungated loses 18/400. TH needed NO override;
only Anti-Lifegain did. Shipped (non-NC) play byte-identical (hook only in ReshuffleAvgChoosePlan / MTG_NC_SEARCH).
Files: DecisionProvider.h (Hook 22), DecisionProviders.{h,cpp}, TurnSolver.cpp. MTG_NC_DEBUG kept (inert). Follow-ups:
burn could take a mild override; gi9 land-SELECTION/colour class is the next provider lever (which land, not whether).

### Speed/quality landscape + the "distill NC" prize (2026-07-10)

Benchmarked the fast policies vs the search (scripts/speed_quality.py, nc_budget.py; LP + ms/game). Two decisions came out:

**value-leaf = the fast CLAIRVOYANT search alternative, already excellent.** Reproduces deep-search LP EXACTLY at
2-32x speedup (slivers 32x, burn 15x, TH/knights 6x, antilife 2x); deterministic + inspectable, no NN risk. This is
the performant search-replacement for OFFLINE deck comparison (clairvoyance is fine when goldfishing).

**The "distill high-budget NC into a fast model" prize is DECK-DEPENDENT, and "high-budget" is NOT the lever.**
Non-clairvoyant search SATURATES fast (its limit is information/EVPI, not compute): antilife NC K8d2 4.700 -> K32d3
4.650 (8.7x cost); TH K8d2 4.575 -> K16d3 4.425 (17x cost). Prize = gap from the fast STATIC d0 to the NC ceiling:
- antilife (aggro-combo): static d0 ~4.80 ~= NC ceiling ~4.65 -> prize ~0.15, NONE. Static d0 already at the NC limit.
- TH (dig/combo): static d0 ~5.6 vs NC ceiling ~4.43 -> prize ~1.2 turns that the STATIC distillation demonstrably
  CANNOT capture (the NC value is forward Treasure-Hunt-timing simulation; a static eval can't hold it). Likely
  bigger on Hinata (deep combo -- user flagged as the tough perf case, worth testing profile-free).
Residual after a perfect fast NC model on TH: ~4.43 vs clairvoyant value-leaf 4.10 = ~0.3 = EVPI (irreducible).

**Implication for the dynamic-distillation prototype:** worth trying ONLY on dig/combo decks (TH, Hinata) where
static fails and the prize is ~1 turn, and ONLY if fast NON-clairvoyant play is needed (references/realism/phase-2 --
for offline analysis the clairvoyant value-leaf already dominates on both axes). NOT worth it on aggro/antilife (no
prize). The target is capturing forward-simulation value at inference (which needs learned rollouts / a dynamic
model), NOT high budget (NC is already saturated + cheap-ish). FOLLOW-UP: try Hinata through the speed/quality +
prize lens (its search is the hardest perf case). Data: logs/eval/{speed_quality,nc_budget}.txt.

### WHY fast non-clairvoyant play matters (user rationale, 2026-07-10)

Three concrete uses -- the first two apply to the CURRENT offline deck-comparison purpose, not just phase 2:
1. **Clairvoyance-abuse detection in A/B card testing.** Comparing card A vs card B: if B's edge comes from the search
   ABUSING clairvoyance (timing to a known library), a non-clairvoyant policy reveals A is actually better in real
   play. Lets us DISCOUNT clairvoyance-driven advantages and trust the deck-comparison verdict. (Core to the project's
   validity.)
2. **Human-legible play.** Clairvoyant play is hard to follow -- it plays "poorly" in many spots but is never punished
   because it knows the future. A non-clairvoyant, reshuffle-AVERAGED policy should play in a way that makes more human
   sense (robust lines, not future-peeking), so its games are interpretable.
3. **Phase 2 (vs real opponents).** Clairvoyant search is illegal against a real opponent (can't predict their
   confounding choices). Sketch: train a model to GOLDFISH via non-clairvoyant search, then bootstrap to vs-other-decks
   by alternating who plays with search until play converges (goldfish model -> vs-field model -> vs-specific-opponent).

These make the fast NON-clairvoyant model a validity/interpretability tool for TODAY (1,2) and the phase-2 foundation (3),
independent of the clairvoyant value-leaf (which stays the raw offline-speed tool).

### The Hinata NC "decouple leak" was the MULLIGAN BOTTOMER, not the turn policy — localized + fixed (2026-07-10)

The 2026-07-10 decouple test first read as "NC-d1 exploits Ponder-shuffle clairvoyance on Hinata" (coupled 6.983 ->
decoupled ~7.3, avg ~+0.32). **That attribution was WRONG.** Localizing it:

- The `MTG_SHUFFLE_SALT_SEARCH` instrument decouples only mid-game shuffle EVENTS read under `g_shuffle_eval` on a state
  carrying the real `shuffle_salt_search`. `ReshuffleAvgChoosePlan` does NOT qualify: it reshuffles per-k with its own
  `rs` (game_seed-derived) and STAMPS `s.shuffle_salt_search = rs`, so every planning/rollout shuffle folds `rs` and is
  executor-order-INDEPENDENT. Static trace => the NC turn policy is salt-independent; the instrument is inert on it.
- So where did decoupled-A (executor salt held at 0, only `shuffle_salt_search` changed 0->7777) move the result?
  **d0 leaked TOO** (+0.5, 21/24 vs 24/24) with the honest-teacher continuation OFF — ruling out both the honest
  continuation AND the "shared enumeration" hypothesis. The only remaining salt-dependent path is `AIEngine::BottomCards`
  under `LookaheadBottoming()` (= `m_lookahead_depth > 0`, true under NC's `--depth 5`): its per-candidate `RolloutWinTurn`
  runs under `ShuffleEvalGuard(true)` on the REAL state (`shuffle_salt_search=7777`) and keeps the true post-bottom draw
  order -- a CLAIRVOYANT mulligan bottomer. Hinata has no exhaustive keep/bottom profile (too expensive to generate), so
  it falls back to this lookahead bottomer; the NC play policy inherits it because it shares the AIEngine mulligan path.

**FIX — `MTG_NC_BLIND_BOTTOM` (default off => byte-identical).** When on, the lookahead bottomer reshuffles each
candidate's trial library to an unseen future (game_seed-derived salt, executor-order-independent; the removed card truly
bottoms) and averages over K (`MTG_NC_BLIND_BOTTOM_K`, default 4), matching the NC turn policy's reshuffle-averaged
evaluation -- so the removal is judged on EXPECTED win turn over unknown draws, not the one true sequence.

**CONFIRMATION (Hinata NC-d0, 24g seed 2002):**

| bottomer | coupled | decoupled-A | dLP | read |
|----------|---------|-------------|-----|------|
| clairvoyant (blind OFF, default) | 24/24  6.667 | 21/24  7.167 | **+0.50** | leaks — the mulligan bottomer |
| **blind ON** (K=2)               | 22/24  7.409 | 22/24  7.409 | **0.000 (byte-identical)** | leak GONE |

FINDINGS: (1) The NC turn policy (`ReshuffleAvgChoosePlan`) is genuinely NON-clairvoyant — the entire Hinata decouple
delta was the clairvoyant lookahead bottomer. The earlier "Ponder-shuffle clairvoyance / shared enumeration" mechanism
is RETRACTED. (2) Honest non-clairvoyant bottoming COSTS ~0.74 turns on Hinata (6.667 -> 7.409) — that gap IS the
clairvoyance value of the bottoming decision itself, real and now honestly excluded. (3) Decks WITH a validated
exhaustive blind keep/bottom profile (`bottoming_enabled`) are already non-clairvoyant here; `MTG_NC_BLIND_BOTTOM` fills
the gap for profile-less decks (Hinata). Default OFF is byte-identical (verified: NC-d0 coupled 24/24 6.667 unchanged;
regression smoke 18/18 PASS, all digests identical). (4) TH stayed flat under the instrument only because its reads are OPENING-order (Treasure Hunt), which a
MID-game salt can't decouple — still NOT proof TH is clean (an opening-order decouple would be needed).

**IMPLICATION for the pipeline:** a truly non-clairvoyant NC run (needed for motivations 1 & 2 — clairvoyance-abuse
A/B and human-legible play) must pair `MTG_NC_SEARCH` with `MTG_NC_BLIND_BOTTOM` on profile-less decks; otherwise the
mulligan stage re-injects clairvoyance the play policy carefully avoids. Candidate for auto-enabling under NC (adoption
call — report to user). Drivers: scripts/{decouple_test,nc_leak_localize}.py. Data: logs/eval/{decouple_test,blind_confirm}.txt.

### Value-leaf HYBRID — a safe default-on vehicle under the NODE budget (2026-07-10)

The clairvoyant value-leaf is ~parity + 2–24× UNBUDGETED at d5 (this describes the CLAIRVOYANT leaf; the
NON-clairvoyant adoption leaf is weak-but-cheap — see the 2026-07-11 CORRECTION at the end of this doc). But the
regression harness runs a deterministic
NODE budget (`SearchBudget::FromVirtualMs`, d3=10 / d5=20 virtual-ms; interior nodes + heuristic-rollout leaves consume
budget, value-leaf leaves are free). Under that budget the value-leaf's cheap leaf buys **no extra search** — the budget
is spent on interior nodes — so when it commits a SHALLOW pass it plays at the shallow-pass quality where the leaf
estimate is unreliable (measured cost: antilife d4 +0.06, d3 +0.25 LP). Ungated value-leaf therefore *regresses*
antilife under the real budget even though it's a pure win unbudgeted.

**Crossover K\* (unbudgeted, depth at which value-leaf reaches the heuristic ceiling):** slivers/knights **d4**,
antilife/TH/burn **d5** → universal safe **K = 5**.

**HYBRID (`MTG_VALUE_MIN_DEPTH`, default 0 = off = byte-identical).** Run the whole search once with the cheap
value-leaf; only if the committed pass is BOTH shallower than K AND **unverified** (its `win_turn` is a beyond-horizon
leaf ESTIMATE, not a real-simulation result) re-run that one decision with the exact heuristic leaf on a fresh budget.
The **verified-win check is load-bearing**: a win within the committed horizon is decided by real simulation, so the
leaf can't have mis-ranked it — redoing verified wins fired on nearly every game of the fast decks and erased the
speedup (antilife 124 ms → 62 ms after adding `!verified`). The value-leaf pass doubles as a cheap reachability probe;
the redo gets a fresh `line_cache` (uncontaminated by value-leaf entries) via `ForceHeuristicLeafGuard`.

Code: `TurnSolver::FullSearchLineHybrid` + `g_force_heuristic_leaf`/`ForceHeuristicLeafGuard` (TurnSolver.cpp);
`AIEngine::TakeTurn` full-depth path reads `MTG_VALUE_MIN_DEPTH` and calls the hybrid.

**RESULTS (250 g, seed 2002, K=5):**

| deck | budgeted d5=20 (LP / speedup) | unbudgeted d5 |
|------|-------------------------------|---------------|
| slivers  | parity, **2.73×** | == plain value-leaf |
| knights  | parity, **1.80×** | == plain value-leaf |
| burn     | parity, **1.67×** | == plain value-leaf |
| TH       | **−0.012**, **1.28×** | == plain value-leaf |
| antilife | parity, **1.11×** | == plain value-leaf |

No regression on any deck at any depth; d3=10 stays ≈1× (no pass reaches K). Unbudgeted hybrid is byte-identical to
plain value-leaf (the redo never fires — everything is either deep or verified). This makes value-leaf a **safe
default-on**: attach `<deck>.value.json` + set `MTG_VALUE_MIN_DEPTH=5` and the budgeted regime gets speedup with no
quality cost. Drivers: scripts/valueleaf_{adaptive,budget,headroom,adopt}.py. Data: logs/eval/valueleaf_*.txt.

### Start-gate relaxation — the transitional-depth optimization, DONE + validated (2026-07-11)

The node budget's start gate skips pass K when its estimate exceeds `kStartGateAlpha(=1.10) * remaining`, committing
K-1 (which then triggers the hybrid's separate, expensive heuristic redo). But a value-leaf pass K is CHEAP (free
leaves), so a pass that slightly overshoots is worth FINISHING inside the first search rather than rolling back +
redoing. **`MTG_VALUE_STARTGATE_ALPHA` (default 1.0 = off = byte-identical): when the value-leaf is active, multiply the
start-gate alpha** so a nearly-affordable transitional pass starts (and runs a little over budget, still capped by the
overrun guard); a genuinely-explosive pass (estimate many× remaining, e.g. slivers g4) is still rejected → no blowup.
Determinism preserved (the gate keys on work-units, not the clock). This makes the redo RARE rather than making it
cheaper (slivers redos 62→26, knights 32→12 at the probe).

Code: `vl_active` + `gate_alpha` in `FullSearchLine`'s deepening loop. Diagnostics: `MTG_HYBRID_STATS` prints the
value-leaf probe's committed-depth histogram + redo rate at exit (drove the finding that most redos land at K-1).

**MEASURED (250 g/deck, redo stays heuristic; α relaxation is quality-neutral-or-better + faster EVERYWHERE):**

| deck | d5 α8 (train 2002) | d5 α8 (held-out 7007) | d3 α8 (2002) |
|------|--------------------|------------------------|--------------|
| antilife | 1.06× / 0.000 | 1.02× / 0.000 | 1.01× / 0.000 |
| slivers  | ~1.3× / 0.000 | 1.03× / 0.000 | 1.04× / 0.000 |
| TH       | 1.14× / −0.008 | ~1.0× / −0.008 | 1.04× / 0.000 |
| burn     | 1.71× / 0.000 | 1.09× / 0.000 (1.41× on 3003) | 1.06× / 0.000 |
| knights  | 1.19× / 0.000 | 1.11× / 0.000 | 1.02× / 0.000 |

Gains saturate by α5–8 and don't regress up to α12; **α8 chosen** (near-max speed, less over-budget-aggressive than α12,
overrun guard caps the tail regardless). Held-out 3003/7007 confirm quality-neutral (TH ±0.008 is seed noise) + no
speed regression. Adopt α8 with the value-leaf default-on package. Drivers: scripts/valueleaf_startgate.py; data:
logs/eval/valueleaf_startgate*.txt.

**d3 is NOT unhelped (premise refuted).** Even without α, the hybrid at d3 is already 1.02–1.15× faster than plain
heuristic (H) at equal quality — the majority *verified-win* decisions use the cheap free-leaf probe and skip the redo;
only unverified ones pay probe+redo. (Ungated value-leaf V0 is far faster but quality-wrecked: slivers 4.332 vs 4.284,
antilife 4.468 vs 4.156 — the redo repairs that while keeping net speedup.) So d3 already benefits; α8 adds a little
more (1.01–1.06×). Data: logs/eval/valueleaf_d3_check.txt.

**REJECTED alternatives (measured, kept the simple heuristic full-ladder redo):**
- *Value-leaf redo* (push the cheap leaf deeper in the redo instead of heuristic): unlimited = quality-neutral but
  BLOWS UP on high-branching (slivers 0.66–0.84×); bounded (mult 2) re-lands in the inaccurate K-1 regime and HURTS
  quality (antilife +0.020, slivers +0.012); one-short (extend only when committed==K-1) still slivers/TH-slower.
  logs/eval/valueleaf_redo*.txt.
- *Surgical redo* (skip the redo's intermediate passes — run a SINGLE heuristic pass at the value-leaf's committed
  depth C instead of the full deepening ladder). Intuition: the low-depth passes are "pure waste." **MEASURED (250g
  2002, α8): 3–8× SLOWER**, quality-neutral (antilife d3 0.66× / d5 0.74×, slivers **0.13–0.17×**, TH 0.38–0.49×,
  burn 0.25–0.30×, knights 0.35×). The intuition is INVERTED by the budget dynamics: the heuristic leaf *costs
  budget*, so heuristic deepening commits at a SHALLOWER, cheaper affordable depth than the free-leaf value-leaf
  reached — the intermediate passes *find that depth and stop there*. Forcing depth C runs a deeper, exponentially
  pricier heuristic tree (slivers rides its g4 branching to depth 4 for free under value-leaf, but a *heuristic*
  depth-4 pass on that explosion is enormous vs. the ladder's wise depth-2 commit). So the intermediate passes earn
  their keep; kept the simple ladder redo. logs/eval/valueleaf_surgical.txt.
- *Mixed value-leaf→heuristic tree* (per-pass leaf switching within one search): same root issue — you'd still have to
  choose the heuristic pass's depth, and the affordable depth is what the ladder already discovers. NOT built.

### ADOPTED default-ON (2026-07-11)

Flipped three defaults so any deck shipping `<deck>.value.json` uses the value-leaf hybrid by default:
`UseValueModel()` → true (override `MTG_VALUE_MODEL=0/off/no/none/false`), `MTG_VALUE_MIN_DEPTH` → 5,
`MTG_VALUE_STARTGATE_ALPHA` → 8. Decks without a sidecar (Hinata) are untouched (`m_value_model` empty → plain
search — verified: all Hinata regression digests byte-identical).

**Decided by the PRIMARY metric — linear loss-penalised avg win turn (loss = max_turns+1), per the user** ("with the
average, any turn slower is as important as any other" → a win→loss is just +1 turn, not special). Value-off vs
value-on(K5,α8), 250 g/deck × **6 seeds** (1001/2002/3003/7007/4004/5005):

| deck | net dLP d5 (6 seeds) | per-seed | verdict |
|------|----------------------|----------|---------|
| TH       | −0.016 | −0.003 | better |
| knights  | −0.008 | −0.001 | better |
| slivers  | −0.004 | −0.001 | better |
| burn     | +0.008 | +0.001 | ~zero |
| antilife | +0.012 | +0.002 | ~zero |

No d5 degradation above noise on any deck (the +0.012 antilife/TH lean on the first 3 seeds was seed variance — TH
even flips net-*better* with 6 seeds). d3 similar (TH −0.012, rest ~0). Data: logs/eval/valueleaf_adopt_lp*.txt,
scripts/valueleaf_adopt_lp.py.

**Individual win→loss are honest non-clairvoyant lines, not blunders.** Traced TH gi18/gi59 + burn gi412 + th gi168:
each is a *physically-different-game* divergence — the value-leaf plays a reasonable different dig/land line, which on
the deterministic library reorders draws and loses a knife-edge T7/T8 marginal win. The heuristic's earlier win
*exploits its clairvoyant rollout* to sequence cycling/lands against the known deck; the value-leaf's honest line is
slower/loses there — arguably MORE illuminating of real deck robustness (aligns with the clairvoyance-discounting goal).
Aggregate regression audit is LP-better: **13 earlier + 1 loss→win vs 7 later + 2 win→loss**.

**GT rebaselined + accepted** (smoke + regression) via `--accept-with-regressions` with the above rationale (the
harness's hard win→loss gate is over-strict for the linear-LP objective; the 2 accepted flips are +1 turn each, dwarfed
by the 13 earlier wins). Overnight GT still stale (deferred). Speedup carried over from the hybrid+α measurements
(1.1–2.7× at the d5=20 gate budget).

### CORRECTION: the value-leaf is a WEAK-BUT-CHEAP evaluator (d5 ≈ heuristic-d2), not near-parity (2026-07-11)

An earlier correction here (now removed) claimed "near-parity, depth-insensitive, worse only at d5." That was built on a
**confounded** matrix that measured the value arm with `MTG_VALUE_MIN_DEPTH=5` — so every committed depth < 5 (unverified)
secretly **re-ran the heuristic**. V3/V4 were therefore the *hybrid*, not the raw leaf (which is why they equalled H3/H4
exactly), and the impossible "V4 beats V5" was the giveaway (a deeper search cannot be worse unless d3/d4 were the
heuristic). Re-measured with the redo DISABLED (`MTG_VALUE_MIN_DEPTH=0`, PURE value-leaf, 1000 g × 4 seeds, unbounded;
`scripts/valueleaf_depth_matrix.py --value-min-depth 0`, logs/eval/valueleaf_depth_matrix_pure.txt):

- **The pure value-leaf improves MONOTONICALLY with depth** on every deck (the V4>V5 inversion was 100% the hybrid).
- **It is much WORSE than the heuristic at every matched depth**, catastrophic when shallow — V1−H1: antilife **+1.23**,
  TH **+1.13**, slivers +0.25, knights +0.18, burn +0.16 (turns). It is a weak leaf that *needs depth* to be usable.
- **Consistent trust depth: pure value-leaf-d5 ≈ heuristic-d2** on all 5 decks (V5−H2 ≈ 0: antilife −0.003, slivers
  −0.002, TH −0.009, burn +0.0015, knights −0.0025), and always beats heuristic-d1. A full d5 leaf search buys H2 quality.
- **Matched-depth residual V5−H5** (the "worse at generous budget"): knights **0.000**, slivers +0.0003, burn +0.0033,
  antilife +0.009, **TH +0.0165** — TH worst because it is the only deck where depth genuinely helps the heuristic
  (H3 > H4 > H5). Cost V5 vs H5: 10–38× cheaper.

**Corrected mental model.** The value-leaf is NOT a matched-depth substitute for the heuristic. The adopted **hybrid** is
LP-neutral because it (a) takes **verified wins** cheaply — those are decided by real simulation, leaf-independent, so
exact — and (b) **redoes the heuristic** below `MIN_DEPTH=5`, exactly where the raw leaf is weak. So the value proposition
is **speed on verified-win games at heuristic quality**, not standalone quality parity. `MIN_DEPTH=5` is the right trust
threshold: d5 is where the raw leaf finally reaches ≈H2.

**The uniform crossover (motivates the fallback).** Across all 5 decks **H3 ≥ V5 ≥ H2**: heuristic-d3 beats or ties
value-leaf-d5, and value-leaf-d5 beats or ties heuristic-d2. So the escalation rule is near-universal: keep the value-leaf
while the heuristic can only afford ≤ d2; fall back once it can afford d3+.

### The depth-aware fallback — BUILT + validated (2026-07-11)

`FullSearchLineHybrid` now: run the cheap value-leaf; if the committed line is a **verified win** keep it (leaf-
independent, exact); else if committed depth ≥ the deck's **`value_trust_depth`** keep it (the leaf matches the heuristic
there); else **escalate** — one heuristic search on the **remaining** shared budget (its start gate commits the deepest
affordable Hd), taken only if it clears the crossover `Hd > committed − 3` (value-leaf-d(k) ≈ heuristic-d(k−3), uniform),
else keep the value-leaf. No bespoke Dh estimator (reuses the start gate); no fresh budget (spends what the value-leaf
left). `escalate_below` at the call site = env `MTG_VALUE_MIN_DEPTH` override (0 ⇒ pure leaf) → `value_trust_depth` →
else `user_depth+1` (escalate at every depth).

**`value_trust_depth` is per-model, in `<deck>.value.json`, and DERIVED not hand-set** by
`scripts/valueleaf_calibrate_trust.py` (run once per value model, ideally at model-creation): it is the shallowest depth
where the pure leaf reaches converged-heuristic quality (`V_d − min_d H_d ≤ tol=0.002`), else UNSET ⇒ escalate always.
Calibrator output: **knights = 5, slivers = 5** (verified-win-dominated, V5=H5), **antilife / TH / burn = UNSET**
(V5 still +0.009 / +0.019 / +0.003 over H_conv).

**Validated (`scripts/valueleaf_fallback_ab.py`, 500 g × 4 seeds; fb − pure-heuristic):**
- **Unbounded d5:** fb − heuristic = **+0.0000 on every deck** (residual fully closed), at **0.7–0.8×** the heuristic's
  cost where it escalates (antilife/TH/burn) and **15–32× cheaper** on the trust-marked decks (knights/slivers, zero
  escalation). Strict Pareto win over the pure heuristic.
- **Gate d3=10:** the raw leaf is catastrophic (pure − heur: antilife **+0.344**, TH +0.077, knights +0.031); the fallback
  restores parity (fb − heur ≤ +0.003). So the fallback makes the value-leaf **safe at any depth**, not just a generous-
  budget polish.
- **Gate d5=20:** fb − heur ≤ +0.005 (recovers ⅔–all of the residual under the tight node budget).

**NC-path note.** The heuristic escalation leaf is *clairvoyant*, so it is a clairvoyant-path quality lever and lives only
in the `s_full_depth` branch — `MTG_NC_SEARCH` (`ReshuffleAvgChoosePlan`) is a mutually-exclusive branch and is
unaffected. On a non-clairvoyant/dynamic-model path the value-leaf is itself the endpoint; the reusable piece is the
`value_trust_depth` *data* in the profile, not the heuristic escalation. Keep the escalation leaf pluggable.

---

## ▶▶ d0-DYNAMIC-MODEL branch — non-clairvoyant PLAY model (AlphaZero-lite), play-only (2026-07-11)

New serious push (branch `d0-dynamic-model`, off phase-1-2 71d685a): the user's goal #1 — a strong non-clairvoyant
PLAY policy — via a **richer/dynamic model** where the static ranker plateaus. **Mulligans/bottoming stay on the
exhaustive table; this model is PLAY-ONLY.** No prior MuZero prototype existed (checked all branches/stash/reflog).

**The framing that matters — we have a perfect simulator, so it's AlphaZero-lite, not latent MuZero.** Generic MuZero
*learns* dynamics because it has no simulator; we have a fast exact one (a full game sims in ms). So the dynamic model =
(1) a **value** used as the LEAF of a shallow real-engine NC search + (2) a **policy prior** that prunes the branching
to top-M plans — keeping the engine as the (free, exact) dynamics. This dodges the static-imitation death: the static
argmax of the NC ceiling failed (3–11% headroom), but a value used INSIDE a forward search is the tested-good regime,
and keeping the ceiling's pick in a small top-M is a far easier target than top-1 exact.

### The NC cost/quality FRONTIER — what the model must beat (scripts/nc_frontier.py, 60g × seeds 2002/3003/7007)

Cross-seed means (single-seed reads are UNRELIABLE — seed-to-seed LP swing ~0.3 dwarfs the depth effect; always aggregate):

| deck | static d0-model | NC-d0 | NC-d1 | NC-d2 | NC-K16d2 | value-leaf d5 (clairv.) |
|---|---|---|---|---|---|---|
| TH | 5.700 | 5.095 | 4.978 | 4.884 | **4.861/4.867** | 4.250 |
| antilife | 5.561 | 4.939 | 4.950 | 4.978 | ~5.0 | 4.089 |

**Findings that reshape the plan:**
1. **The static learned evaluator is STRICTLY DOMINATED by a raw K-averaged rollout at equal-or-less wall-clock**
   (TH NC-d0 4.900 @31ms beats static 5.700 @34ms; antilife NC-d0 4.667 vs static 5.283 at ~160ms). Forward sim is
   better *per millisecond* — the static-model path is dead, not merely weaker. Cleanest possible "must simulate forward".
2. **The model is now a SPEED play, not a quality unlock.** A cheap NC search already reaches the ceiling — so the
   deliverable is ceiling-quality at sub-NC cost (for high-volume clairvoyance-abuse A/B + phase-2), not a new ceiling.
3. **Cost is deck-structured:** TH's cost is DEPTH-branching (d0→d1→d2 = 31→77→199ms); antilife's is per-plan
   ENUMERATION/rollout (flat ~160ms across depths — even the static model pays 161ms to enumerate). → TH wants the
   **policy-prior** (prune the K×#plans branch); antilife wants the **value leaf** (kill the flat per-plan rollout).

### TEACHER DEPTH pinned = K16 d2 (scripts/nc_teacher_depth.py, d2 vs d3, K8/K16, 3 seeds, 40g)

Cross-seed mean LP: **TH** K8d2 4.942 · K8d3 4.917 (7.7× slower) · **K16d2 4.867** · K16d3 4.883 (*worse* + 8–10×
slower). **antilife** K8d2 5.008 · K8d3 4.942 · K16d2 5.025 · K16d3 4.842 (all within seed noise, ~flat). **Width (K)
beats depth past 2 on TH; depth>2 is noise-to-harmful.** Unified teacher = **K16 d2** (best for TH, human-beating for
antilife). The doc's earlier "K16d3 4.425 improvement" does NOT reproduce robustly across seeds (2002 only).

### TEACHER validated HUMAN-COMPETITIVE on ALL reference decks (scripts/ref_bench.py, K16 d2, tempo on)

Per the user's gate ("if it can't match the human beyond shuffle/risk variance, improve the teacher first"). LP (losses=9),
identical forced human hands:

| deck | n | human | clairvoyant | **NC teacher (K16d2)** | verdict |
|---|---|---|---|---|---|
| antilife | 30 | 4.500 | 4.167 | **4.467** | NC BEATS human, closer to clairvoyant than human |
| burn | 16 | 4.625 | 4.375 | **4.375** | NC = clairvoyant (EVPI≈0), beats human |
| slivers | 4 | 4.000 | 4.000 | 4.000 | tie (all easy) |
| knights | 3 | 4.667 | 4.667 | 4.667 | tie |
| TH | 2 | 4.000 | 4.000 | 4.000 | tie |

The residual antilife NC-slower games (gi21/gi28/gi1/gi29 +1) are mostly flagged EVPI (clairvoyant sees a draw NC can't)
= the "explainable variance" to set aside. **Teacher gate PASSED — distill from it.**

### WITHIN-TURN non-clairvoyance RE-VERIFIED (scripts/decouple_test.py, decoupled-A = clean probe)

The decoupled-A probe holds the EXECUTED order fixed (salt 0) and changes only the search-seen order; a clairvoyant
policy drops, a non-clairvoyant one is flat. (decoupled-B/C also change the executor salt → different games → their +0.2
is shuffle variance, NOT clairvoyance.) Results:
- **antilife (fetchlands):** NC-d1, NC-d2, NC-d1-blind all **dLP(A) = +0.000** → fetch-shuffle order not exploited.
- **TH:** flat (no mid-game shuffle to peek).
- **Hinata (Ponder):** NC-d1 leaks +0.125 (the known mulligan-BOTTOMER leak); **NC-d1 + `MTG_NC_BLIND_BOTTOM` →
  coupled 6.533 = decoupled-A 6.533 (byte-identical)** → blind-bottom closes it. So the TURN policy is genuinely NC;
  profile-less decks (Hinata) must pair NC with blind-bottom.

### BUILD PLAN (the model)

1. **Labels** — reuse `MTG_DUMP_EVAL_ROWS` + `MTG_EVAL_ROWS_ROLLOUT`/`_HONEST`/`_ROLLOUT_DEPTH=2` + `_K=16` while
   PLAYING with `MTG_NC_SEARCH` K16 d2 (on-policy states, honest reshuffle-avg labels = the teacher's own objective).
2. **De-clairvoyed VALUE model** — train (fixed-point GBDT/linear infra) toward those NC labels. This is the search LEAF
   (distinct from the shipped `value.json`, which used CLAIRVOYANT search labels).
3. **Wire a truncated value LEAF into `ReshuffleAvgChoosePlan`** (replace/супcap the greedy `SimulateToEnd` continuation
   with a depth-d truncated rollout + V(leaf)) — engine change, env-gated, inert by default.
4. **Policy-PRIOR** — a plan-ranker for top-M pruning (top-M recall is the target metric, not top-1).
5. **Measure** the shallow pruned value-leaf NC search vs the K16-d2 teacher: LP parity at what speedup? Non-clairvoyance
   preserved (decouple probe). Start on TH (biggest depth-branch prize) + antilife (per-plan-rollout prize).

Scripts added this session: `scripts/nc_frontier.py`, `scripts/nc_teacher_depth.py`; `scripts/decouple_test.py`
extended (antilife deck + NC-d2 + blind variant).

### BUILD progress (2026-07-11, same session)

- **Labels DONE + pipeline validated.** `MTG_DUMP_EVAL_ROWS` + `MTG_DUMP_VALUE_ROWS` + `MTG_EVAL_ROWS_ROLLOUT`
  `_HONEST` `_ROLLOUT_DEPTH=2` `_K=8` (honest reshuffle-avg = teacher objective), heuristic-d3 state collection
  (round-0; DAgger to NC-play states later). TH 300g → 13,162 eval / 1,227 value rows; antilife 220g → 5,104 /
  904. **Labels pass the durdle-trap check**: T1 land-drop beats idle (5.38 vs 6.25), T2 cast beats land beats pass
  (4.00 < 4.62 < 4.75). Files `logs/eval/{TH,antilife}_ncteach_{eval,value}.rows`.
- **De-clairvoyed VALUE models fit** (`train_eval_model.py --value`, held-out by seed%5): **TH RMSE 0.878**,
  **antilife 0.758** turns (vs predict-mean 1.329 / 0.977) — comparable to the shipped CLAIRVOYANT value models
  (doc's TH ~0.947). So the honest NC labels are learnable to similar fidelity; the value leaf is viable in
  principle. Linear fit is collinear (intercept −70, library_size +1.16) = a weak STATIC leaf, as expected — it
  earns its keep only INSIDE the shallow search. GBDT + more value rows (only ~1k now; want ~5k) is the tightening
  lever, deferred until the wiring proves the approach.
- **NEXT (the decisive step): wire a truncated value leaf into `ReshuffleAvgChoosePlan`.** Insertion = in
  `SimulateToEndImpl`'s `g_honest_teacher && depth>0` continuation, add an env-gated horizon H: after H honest
  turns, return `V(leaf)` (the FSLineWin `depth<=0` value-leaf pattern: `feats=ExtractMidGameFeatures(state,{})`,
  `w=round(vm.Score/1000)` clamped) instead of playing to the end. Attach a SECOND value sidecar slot (`m_value_model`
  is the clairvoyant one; the NC leaf needs the de-clairvoyed model — either a new `m_nc_value_model` field or swap by
  gate). Inert by default. Then measure the shallow pruned value-leaf NC vs the K16-d2 teacher (LP parity at what
  speedup; decouple-probe intact). Then the policy-prior pruner on the eval rows (top-M recall metric).

### ▶▶▶ DYNAMIC MODEL (d0 replacement) — dynamics helps, but inputs are the ceiling (2026-07-11)

User steered HARD to the d0 replacement (fast one-shot policy, NOT leaf/search). Confirmed decisively a STATIC d0
policy CANNOT reach the validated NC-K16-d2 teacher: gap **0.80 turns TH, 0.68 antilife** (scripts/d0_policy_ab.py,
held-out): heuristic-d0 5.643/5.520, best static-d0 5.583/collapse-7.4, teacher 4.787/4.843. Linear+GBDT+v6/v7 feats
all plateau/collapse; capacity made TH worse. Labels learnable; the DECISION needs a forward rollout static can't do.

**Built the dynamic model in C++** (`tools/dyntrain/{nn.h,main.cpp}`, standalone, reads `.rows`; container has no
torch/pip/numpy + no net). Latent-rollout net: `h0=tanh(rep(state))`, plan injected step 0, `h_{t+1}=tanh(dyn([h_t;a]))`
x T, `pred=val(h_T)`; matrix manual backprop + Adam; pairwise-rank + MSE. T=0 = plain MLP over [state;plan] baseline.
In-process inference free (same C++). torch-in-docker deferred to scale-up.

Held-out PICK-REGRET (avg extra win turns vs teacher's best plan; H=64, 70ep):

| deck | MLP T=0 | dynamic T=2 | T=3 | heuristic-pick | random |
|---|---|---|---|---|---|
| TH | 0.241 | **0.203** | 0.216 | 0.423 | 0.559 |
| antilife | 0.335 | **0.320** | 0.372 | 0.368 | 0.492 |

- **Dynamics (T=2) BEATS MLP (T=0) consistently** (needs capacity; H=32/40ep near-tie was underfit) and both beat the
  heuristic. Recurrent latent-rollout is a REAL lever — dynamic thesis holds in principle. T=3 overfits small antilife.
- **BUT absolute regret ~0.20/0.32 is NOT teacher-zero**; dynamics edge over MLP only ~0.02-0.04. Remaining gap points
  at INPUTS (40 hand-crafted summaries lose card-level structure), not model class. Card-level embeddings + set-pooling
  = likely next lever (bigger build).
- Pick-regret is a PROXY (teacher's on-policy states); real d0 PLAY has covariate shift + compounding. True go/no-go =
  wire NN into engine (Seam A, env-gated) + measure actual d0 play LP vs teacher (DAgger if covariate shift bites).

NEXT: (a) wire NN inference into engine, measure real d0 play; (b) card-level inputs. Scripts: tools/dyntrain/,
scripts/d0_policy_ab.py, scripts/nc_frontier.py, nc_teacher_depth.py.

### DYNAMIC MODEL wired into the engine — GREAT LEAF, d0 COLLAPSE (covariate shift + myopia) (2026-07-11)

Wired NN inference in-process: `src/ai/DynModel.h` (load + forward), `GameState::m_dyn_model` (fwd-decl + field,
threaded like m_evaluator), `AIEngine` stamps from `MTG_DYN_MODEL` env (loads once), TurnSolver Seam A serves via
`DynPlanScore`. Build: `cmake --build build --config Release --target mtg`. Inference parity EXACT
(tools/dyntrain/infer_test.cpp reproduces the trainer's top1/regret). Fixed a train/serve skew: the dyn Seam-A path
uses `PlanBaselineEval` (matches the EmitEvalRows dump), not `total_eval`.

**TH d0 play: heuristic 82.5%/5.09 | dyn-d0 17.5%/6.43 (COLLAPSE) | dyn-d3-leaf 100%/4.125 (clairvoyant-search
quality).** So the model is an EXCELLENT evaluator/leaf but FAILS as a standalone d0 policy. Diagnosis: not a bug
(d3 perfect), not keep/bottom poisoning (TH exhaustive table; d0 => LookaheadBottoming off), not candidate-ranking
(TH d0 often has 1 candidate/turn — heuristic faces the same and WINS). It durdles via an early pure-play divergence
(unpinned). **DAgger round-1 did NOT fix it** (dumped 34k model-own states + teacher labels, retrained, still 17.5%).

Confirms the doc's deepest finding across FOUR model classes now (linear, GBDT, MLP, dynamic NN): a static/one-shot d0
argmax can't hold multi-turn value; teacher quality needs forward sim at decision time.

**USER DECISION: keep pushing d0 (PRIMARY); the leaf is a detour that falls out for free once d0 trains well.** Resume
plan (see memory d0-dynamic-model-direction): (1) PIN the divergence (heuristic vs dyn game-log diff on a heur-win/
dyn-loss game; suspects = t1 land selection, second-main/combat path, multi-candidate cascade); (2) more DAgger rounds;
(3) card-level inputs (embeddings+set-pool — the likely real lever; may justify torch-in-docker). Files: tools/dyntrain/,
src/ai/DynModel.h, logs/eval/TH_dyn*.json, scripts/d0_policy_ab.py. Branch d0-dynamic-model, uncommitted.

---

## 2026-07-11 (overnight): d0 COLLAPSE WAS A ONE-LINE SCALE BUG, not model quality

The "dyn model collapses as a standalone d0 policy (10-17%)" finding was **misdiagnosed** as covariate
shift / combo myopia. It is a wiring/scale bug at Seam A (TurnSolver::Solve):

- `Plan best;` initializes the do-nothing/pass baseline with `value = -1` (TurnSolver.h: `int value = -1;
  // -1 = nothing castable`), a **total_eval-scale sentinel**. The empty subset is never passed through
  `consider()` (line ~1874 guards `!sel.empty()`), so do-nothing keeps value = -1.
- `DynPlanScore` returns predicted-win-turn on an **all-negative scale** (`-pred*1000` ≈ -4000..-6000,
  higher/less-negative = fewer turns). The compare `rank_value > best.value` is then `-4362 > -1` =>
  FALSE for every real plan, so the dyn model **can never choose to act** — it passes every main phase
  and durdles. It only "wins" the ~10% of games where a plan is outright lethal (`wins_this_turn`
  dominates value). The heuristic path uses `total_eval >= 0 > -1` so it always develops -> no collapse.
- This also explains: (a) DAgger v1==v2 byte-identical (retraining the value head can't beat a scale
  wall — pass always wins); (b) infer_test parity was exact (the model ranks fine on teacher states; it
  just never gets to act in play).

**FIX** (TurnSolver.cpp ~1457, scoped to the dyn path so heuristic/static-eval are byte-identical):
```cpp
const bool use_dyn_policy = state.m_dyn_model && !state.m_dyn_model->empty();
if (use_dyn_policy) { best.value = std::numeric_limits<int>::min(); }
```
This floors the do-nothing baseline so any real dyn-scored plan wins the compare — matching the
heuristic's "always develop if you can" semantics; the model then does its real job (RANK among real
plans). Winning plans still dominate via wins_this_turn.

### Result (TH, 100g x3 seeds, --depth 0)
| policy            | win% | LP    |
|-------------------|------|-------|
| heuristic d0      | 84.7 | 5.643 |
| dyn v1 d0 (fixed) | 84.0 | 5.627 |
| dyn v2 d0 (fixed) | 84.3 | 5.600 |
| NC teacher K16d2  | 95.3 | 4.787 |

The model went from 10% (collapse) to **heuristic parity**. The 0.85 LP / ~11pp gap to the NC teacher
is still open. It is NOT a collapse — it is the honest "can a Seam-A d0 spell-ranker beat the heuristic
toward the teacher?" question.

### Why parity (not teacher) — structural factors
- **~43%** of real d0 decisions have >=2 candidates (measured, 5 games) -> the ranker DOES act often
  (not as capped as feared). But its ranking ties the heuristic on its own states.
- **Train/serve candidate mismatch**: the label dump enumerates 10.73 candidates/decision (incl. land
  variants); Seam-A Solve at d0 enumerates only spell subsets (land folded via greedy, `land_decided
  =false`). The land drop — a big part of the teacher's edge — is NOT in the d0 candidate set.
- **Train/serve continuation mismatch**: teacher labels are the win-turn assuming TEACHER continuation
  (incl. its land sequencing); serve-time continuation is greedy-land + model-spell. On-policy/DAgger
  labeling (MTG_EVAL_ROWS_ROLLOUT depth>0) is the lever that reflects the actual serve policy.

### Overnight sweep in flight (scripts/overnight_dyn.sh -> logs/eval/overnight_dyn_findings.txt)
Trains a (rows{base,pool} x T{0,2,3} x H{64,128}) grid, measures d0 play LP each, then 2 DAgger rounds
on the best config. Empirically answers: can ANY training push the Seam-A d0 ranker past parity?

### Antilife generalization (100g x3, depth 0) — fix is NOT TH-specific
| policy            | win% | LP    |
|-------------------|------|-------|
| heuristic d0      | 91.0 | 5.520 |
| dyn T2H64 d0      | 91.0 | 5.450 |
| NC teacher K16d2  | 95.0 | 4.843 |
Antilife (fetchland deck) dyn also plays at parity (marginally better LP, 2/3 seeds), NOT collapse.
Same shape as TH: model ties heuristic, ~0.68 LP gap to teacher remains. Confirms the scale fix is
deck-agnostic and the "parity, not teacher" ceiling is structural (land-fold + continuation), not a bug.

## 2026-07-11 (overnight): SWEEP VERDICT — training/architecture is exhausted; the cap is STRUCTURAL

Ran scripts/overnight_dyn.sh: a (rows{base=13k teacher, pool=+on-policy} x T{0,2,3} x H{64,128}) config
grid + 2 DAgger rounds on the best config, each measured on d0 PLAY LP. Results (heuristic d0 = 5.643):

- **All 12 configs cluster at 5.64–5.78 LP** (heuristic parity). Best = pool T0 H128 @100gx3 = 5.557.
- **T0 (plain MLP) won**, not the dynamic latent-rollout T2/T3 — the "dynamic" machinery is NOT the
  bottleneck at play time (it had marginally better held-out pick-regret, which did not convert to LP).
- **Held-out pick-regret (0.17–0.25) does NOT predict play LP** — pool cut regret to 0.17 but stayed at
  parity. Ranking teacher-states better ≠ playing better (covariate shift + few decisions bind outcome).
- **DAgger flat**: 5.557 -> 5.620 -> 5.563 across 2 rounds (~1660 on-policy rows/round, noise-level).
- **Held-out seed check kills the marginal edge**: on fresh seeds (4001/4002/9009/9010; training used
  seeds 40000+), heuristic 5.913 | best 5.925 | v2 5.888 — all within +-0.03 LP. The 0.09 "win" on the
  sweep seeds was overfit. The dyn models are statistically indistinguishable from the heuristic at d0.

**Conclusion**: a Seam-A d0 spell-ranker caps at heuristic parity regardless of model class, capacity,
data pool, or DAgger. Training is not the lever. The 0.85 LP gap to the NC teacher is structural, from
the two mismatches already identified:
  1. **Land not in the d0 candidate set** — at d0 `fold_land=false` (AIEngine.cpp:1287), so the land is
     played greedily by TryPlayLand BEFORE Solve ranks spell subsets. The model never sees/controls the
     land drop, which is a large part of the teacher's edge on both TH (ramp) and antilife (fetch).
  2. **Continuation mismatch** — teacher win-turn labels assume teacher land-sequencing continuation;
     serve is greedy-land + model-spell.

## RECOMMENDATION (needs user sign-off — a scope change): FOLD THE LAND DROP INTO THE d0 dyn CANDIDATE SET
Make the d0 dyn policy rank (land-choice x spell-subset) plans, not spell-subsets-with-greedy-land. The
infra exists (Plan.land_decided/land_to_play/fetch_target; EnumeratePlans enumerates land x spell for the
depth>0 search; fold_land path in AIEngine.cpp:1346-...). Minimal shape: when use_dyn_policy at d0, run a
land-inclusive enumeration (mirror the depth>0 fold) and score each candidate's resulting state with
DynPlanScore; play the chosen land + spells. Scope it to the dyn path (byte-identical otherwise). Risks
to validate with the user's review + regression suite: fetchland targets, Karoo bounce-lands, the
"TH-before-land-drop" defer special-case, Reliquary Tower. This is the pivotal experiment for whether a
learned d0 policy can BEAT the heuristic (vs merely match it at d0 speed). Deferred pending sign-off
because it changes what the policy fundamentally decides (spell-only -> spell+land), which also changes
the training-data candidate distribution.

### Standing win regardless of land-folding: the SCALE FIX makes dyn-d0 a usable heuristic-parity policy
at d0 speed (both decks), which already serves the SPEED goal (fast non-clairvoyant play for
clairvoyance-abuse A/B). It just doesn't yet BEAT the heuristic. Uncommitted on branch d0-dynamic-model.

## 2026-07-11: LAND-FOLDING BEATS THE HEURISTIC (the first real progress past parity)

User approved letting the d0 policy DECIDE THE LAND DROP. Implemented as a 1-ply resulting-state VALUE
lookahead: enumerate land-inclusive candidate plans (EnumeratePlansWithLand -- each land choice x spell
subset, + defer/idle), apply each, score the RESULTING board with a value model, pick the best (lethal
dominates). The land choice / targets / X are all discriminated because they're reflected in the applied
board -- the Seam-A plan-DIGEST ranker only saw a has-land BIT and couldn't (measured: two land choices
gave a byte-identical feature vector).

Wiring: TurnSolver::SolveD0LandFold (new) + AIEngine d0 branch (MTG_D0_LANDFOLD gate: skip greedy land,
use SolveD0LandFold, play its chosen land via the fold path). Byte-identical unless the flag is set AND a
value model is attached.

KEY OBSTACLE + FIX -- the value model must be trained on the RIGHT distribution. Existing value models
(trained on VISITED positions only, all "developed") EXTRAPOLATE on the idle/defer resulting states the
land-fold enumeration scores: the clairvoyant GBDT ranked "do nothing" best and COLLAPSED to 0.7%. Fix =
a RESULTING-STATE per-candidate row dump (MTG_DUMP_RSVALUE_ROWS): EnumerateEarliestWins captures each
candidate's post-plan features (SetCaptureResultFeats, gated -> zero cost on the teacher path), EmitEval
rows emits K-reshuffle-AVERAGED resulting features (marginalises within-turn draws) + the de-clairvoyed
win-turn label. Trains the value on the exact idle/develop distribution it serves on -> no collapse.

### Result (TH, 100g x3, depth 0; RS-value GBDT trained on 80 games / 424 dec / 4465 candidates seeds 40000+)
| policy                              | train 2002/3003/7007 | held-out 4001/4002/9009/9010 |
|-------------------------------------|----------------------|------------------------------|
| heuristic d0                        | 5.643                | 5.913                        |
| d0-landfold GBDT K=1 (clairv draws) | 5.390                | 5.450                        |
| d0-landfold GBDT K=8 (NON-CLAIRV)   | **5.453**            | **5.698**                    |
| NC teacher K16d2                    | 4.787                | ~4.9                         |

- **The NON-CLAIRVOYANT land-fold policy BEATS the heuristic by 0.19 (train) / 0.22 (held-out) LP** -- the
  first NC d0 policy to beat the heuristic. Closes ~25% of the heuristic->teacher gap.
- MTG_D0LF_K averages each plan's resulting-state value over K reshuffles of the UNSEEN library (common
  random numbers). K=1 = fast within-turn-clairvoyant proxy (0.25-0.46 LP better = an UPPER bound); K=8 =
  honest NC. The K=1<->K=8 gap (0.06 train / 0.25 held-out) is exactly the within-turn draw clairvoyance.
- Land discrimination confirmed in the rows: at T1 the enumerated candidates (untapped dual / tapped land
  / defer) get distinct resulting features AND distinct labels (tapped-first rated best -- real strategy).

### NEXT levers (all NC): more training data; a stronger/dynamic value net; higher serve-K; generalize to
### antilife (fetchlands -- the land choice matters even more). Tools: MTG_DUMP_RSVALUE_ROWS (dump),
### scripts/train_eval_gbdt.py --regression (value model), MTG_D0_LANDFOLD + MTG_VALUE_PROFILE + MTG_D0LF_K.

### Learning curve (how much data helps) + antilife generalization
TH RS-value GBDT, NON-CLAIRVOYANT (K=8), held-out seeds 4001/4002/9009/9010 (heuristic 5.913):
| games | decisions | NC-LP |
|-------|-----------|-------|
| 10    | 52        | 5.967 (WORSE -- overfits) |
| 20    | 101       | 6.000 (WORSE) |
| 40    | 202       | 5.717 (crossover) |
| 80    | 424       | 5.710 |
| 160   | 833       | 5.705 (PLATEAU) |
=> ~40 games (~200 decisions) is the crossover; plateaus HARD by ~80. Beyond that, more data does
nothing (160 == 80). Train-RMSE RISES with data (0.23->0.37) = healthy less-overfit signature. DATA IS
NOT THE REMAINING LEVER -- the plateau (5.71) is well short of the teacher (~4.9), so model/feature
quality (or the 1-ply-vs-search ceiling) is what remains.

ANTILIFE generalization (fetchland deck -- the land choice matters MORE), held-out, NC K=8:
| policy                    | win% | NC-LP |
|---------------------------|------|-------|
| heuristic                 | 91.0 | 5.595 |
| landfold (weak 20-tree)   | 94.0 | 5.180 |
=> land-fold beats heuristic by 0.42 LP on antilife (vs 0.22 on TH) with only a 20-tree model -- the
gain is LARGER where the land decision is richest (fetchlands), closing ~55% of the antilife gap.
CONCLUSION: land-folding + resulting-state value is a real, non-clairvoyant, cross-deck win over the
heuristic. Remaining gap to the teacher is a model/approach lever, not data.

### Option 1 (stronger value net + serve-K) -- TH, held-out, NC
SolveD0LandFold now scores with the DYN net (m_dyn_model, PredictWinTurn) when attached, else the
MidGameEvaluator (m_value_model). DynModel trained via tools/dyntrain on the RS-value rows (a RANKER over
resulting states -- grouped by decision). Results:
| scorer / K                 | NC-LP | note |
|----------------------------|-------|------|
| heuristic                  | 5.913 | |
| GBDT 120/4  K8             | 5.705 | pure-python trainer, plateaus |
| DynNet T0H128 K8          | 5.595 | MLP beats GBDT (+0.11) |
| DynNet T0H128 K16         | 5.500 | serve-K sweet spot |
| DynNet T0H128 K32         | 5.512 | K plateaus at 16 |
| DynNet T2/T3 (dynamics)    | >=5.615 | dynamics do NOT help; MLP width does |
Best = DynNet MLP T0H128 @ K16 = **5.500**, closing 37% of the heuristic(5.913)->teacher(4.787) gap, all
non-clairvoyant. Net > GBDT; serve-K helps (variance reduction) to ~16; dynamics inert (as in the earlier
sweep). Remaining ~0.71 LP to the teacher = a 1-PLY-vs-search ceiling -> next levers = richer features
(opt 2) and a shallow NON-CLAIRVOYANT lookahead with the value leaf (opt 3, the real headroom).

## 2026-07-11: PIVOTAL DIAGNOSTIC -- the learned VALUE model is DOMINATED by a greedy rollout (on TH)
NC ladder + speed (TH, held-out 4001/9009, 100g):
| policy                                | NC-LP | ms/game |
|---------------------------------------|-------|---------|
| heuristic d0                          | 5.915 | 48.3 |
| land-fold value-model (DynNet T0H128 K16) | 5.470 | 45.7 |
| NC teacher K16 d0 (greedy rollout, folds land) | 5.075 | 43.5 |
| NC teacher K16 d1                     | 4.920 | -- |
| NC teacher K16 d2 (target)            | 4.885 | -- |
KEY: NC-teacher-d0 is BOTH FASTER AND BETTER than the learned-value land-fold (5.075 vs 5.470, 43.5 vs
45.7 ms). Since NC-d0 ALREADY folds the land (same EnumeratePlansWithLand candidates), the ONLY difference
is the SCORER: an actual K-reshuffle GREEDY ROLLOUT-to-end vs my learned value model. The value model is a
LOSSY approximation of that rollout (compresses the whole future into 40 features) and loses ~0.4 LP -- and
on short TH games the rollout is so cheap the value model buys NO speed. So the learned-value d0 policy is
DOMINATED on TH: the rollout it approximates is both cheaper and more accurate.
IMPLICATIONS / reframe:
- The right "fast NON-CLAIRVOYANT policy that's close to the teacher" is likely just NC-teacher at LOW
  depth (d0 = 5.075 already fast+strong; d2 = 4.885 target), NOT a learned value model.
- A learned value LEAF only pays off where the ROLLOUT is EXPENSIVE (long games / big decks / deep search)
  -- untested here; TH games are ~5-8 turns so rollout is cheap. This is the condition to check before
  more value-model work.
- What IS validated + kept: (1) the do-nothing SCALE FIX; (2) LAND-FOLDING as the lever (letting the policy
  decide the land beats a spell-only ranker) -- but the SCORER should be a rollout, not a static value, on
  cheap-rollout decks. (3) The land-fold value-model still BEATS the heuristic (5.47 vs 5.91) and is a valid
  fast baseline; it's just dominated by NC-d0.
NEXT (post-compact): measure NC-d0/d1 speed+LP as the actual fast-NC deliverable; check whether any deck has
expensive-enough rollouts that a value leaf wins on the speed-quality frontier; if not, the value-model d0
line is a negative result (documented) and the fast-NC answer is low-depth NC search.

## 2026-07-11 (later): DECISIVE NEGATIVE — the learned value model is Pareto-DOMINATED by NC-d0 (both decks, clean)
Re-ran the NC ladder CORRECTLY (the pivotal table above ran NC at `--depth 0`, where the MTG_NC_SEARCH block
— which lives inside `if (m_lookahead_depth>0)` in AIEngine — never fires, so it silently measured the plain
heuristic; NC must run at `--depth 1` as the dispatch gate, with MTG_NC_DEPTH the real lookahead). Speeds
below are CLEAN (one policy at a time; the earlier ladder ran TH+antilife concurrently and inflated ms 4-5x).

TH (held-out 4001/4002/9009, 200g):        | antilife (held-out, 100-150g):
| policy               | LP    | ms/game |  | policy               | LP    | ms/game |
|----------------------|-------|---------|  |----------------------|-------|---------|
| heuristic-d0         | 5.875 |   8.6   |  | heuristic-d0         | 5.533 |  64.4   |
| NC-K16-d0            | 5.102 |  10.3   |  | NC-K16-d0            | 4.838 |  65.4   |
| NC-K16-d1            | 4.888 |  29.6   |  | NC-K16-d1            | 4.853 |  77.4   |
| NC-K16-d2 (teacher)  | 4.853 | (568*)  |  | NC-K16-d2 (teacher)  | 4.890 | (422*)  |
| landfold-value K16   | 5.613 |   9.1   |  | landfold-value K16   | 5.000 |  98.0   |
| landfold-dyn  K16    | 5.49  |  ~9     |  | landfold-dyn  K16    | 4.930 |  98.3   |
(*d2 ms from the contended ladder; still the slow one.) value/dyn use TEACHER-d2-distilled labels (best case).

THREE decisive facts:
1. **NC-d0 Pareto-dominates the value model on BOTH decks** — faster AND better. TH: NC-d0 5.102@10.3ms vs
   value 5.613@9.1ms (value is 1ms faster but 0.5 LP worse). Antilife: NC-d0 4.838@65ms vs value 5.000@98ms
   (value is SLOWER — its land-enumeration explodes on fetchlands — AND worse). There is no regime among our
   decks where the learned value leaf wins the speed/quality frontier.
2. **The model is FEATURE/COMPRESSION-bound, not label/data/architecture-bound.** TEACHER-d2-distilled labels
   (honest MTG_EVAL_ROLLOUT_DEPTH=2 + MTG_EVAL_ROWS_HONEST=1) do NOT move TH (GBDT 5.672 vs old 5.677; DynNet
   5.488 vs 5.457) and only partly help antilife (5.18→4.93). Combined with the plateaued learning curve
   (≤80g) and the architecture sweep (T0 MLP won), every lever except the 40 hand-crafted features is
   exhausted. The features can't reproduce even the teacher's plan RANKING.
3. **WHY there's no niche: goldfish games are SHORT (~5-8 turns), so a greedy K16 reshuffle rollout (NC-d0)
   is already near-heuristic-speed** (TH 10.3 vs 8.6ms; antilife 65 vs 64ms — the antilife heuristic itself
   pays per-plan rollout cost). The teacher's only EXPENSIVE computation is d2 branching, and d2 beats d1 by
   just 0.007-0.07 LP — nearly worthless. So there is no expensive-but-valuable computation for a value model
   to cheaply distill: the cheap depths are already near-teacher, and the one costly depth isn't worth it.

**THE FAST NC DELIVERABLE IS NC-d0 (or NC-d1) RUN DIRECTLY, not a learned model.** NC-d1 is essentially AT
the teacher (TH 4.888 vs 4.853; antilife 4.853 vs 4.890) at 1.2-3.4x heuristic speed; NC-d0 is within
0.05-0.21 LP at ~heuristic speed. This MEETS the user's stopping condition ("very close to the teacher") —
but via low-depth NC search, not a value model. The learned value model is a DOCUMENTED NEGATIVE for d0 PLAY
on short-game decks. Its only viable futures: (a) card-level features (embeddings + set-pooling; big build,
likely torch-in-docker) to break the compression bound — but even then it caps at ~NC-d1 quality it can't
beat NC-d0 on speed; (b) a genuinely long-game (15+ turn) grindy deck where the rollout tail is expensive
and a truncating value LEAF finally wins — we have no such deck; (c) a phase-2 DEEP-search value leaf (a
different use case from d0 play, where a good value fn accelerates a search that must branch deep).
Harness: scripts/nc_ladder.py (LP+ms ladder). Knobs: MTG_NC_SEARCH/_K/_DEPTH at --depth 1; MTG_D0_LANDFOLD +
MTG_VALUE_PROFILE/MTG_DYN_MODEL + MTG_D0LF_K at --depth 0. Teacher labels: MTG_DUMP_RSVALUE_ROWS +
MTG_EVAL_ROWS_ROLLOUT + MTG_EVAL_ROLLOUT_DEPTH=2 + MTG_EVAL_ROWS_HONEST + MTG_EVAL_ROWS_K=8.

## 2026-07-12: IS THE TEACHER CLOSE TO HUMAN? — YES, validated on all 5 ref decks (ref_bench, forced human hands)
User's concern: "close to the teacher" only matters if the TEACHER is close to HUMAN. Ran scripts/ref_bench.py
(forces each references/<deck>/*.json's EXACT opening hand into the search via --force-mulligan, compares
per-game win turns: HUMAN vs CLAIRVOYANT search vs NON-CLAIRVOYANT teacher). LP (losses=max_turns+1):
| deck     | n  | human | clairvoyant | NC teacher K16 d2 | verdict |
|----------|----|-------|-------------|-------------------|---------|
| slivers  |  4 | 4.000 | 4.000       | 4.000             | NC = human = clair (forced line) |
| knights  |  3 | 4.667 | 4.667       | 4.667             | NC = human = clair |
| TH       |  2 | 4.000 | 4.000       | 4.000             | NC = human = clair |
| burn     | 16 | 4.625 | 4.375       | 4.375             | NC = CLAIR, BEATS human |
| antilife | 30 | 4.500 | 4.167       | 4.467             | NC BEATS human; 0.30 below clair |
=> The teacher (NC K16 d2) is human-competitive-or-SUPERHUMAN on EVERY reference deck. On forced-line decks it
exactly equals human=clairvoyant; on burn/antilife it BEATS the human. The ONLY residual is antilife's 0.30 LP
gap to the CLAIRVOYANT ceiling (4.467 vs 4.167) — the irreducible NON-CLAIRVOYANCE TAX (perfect future
knowledge), which a human ALSO pays. So the teacher is NOT the weak link; it is the right target.

IS d2 A HIGH ENOUGH SEARCH LEVEL? YES. Goldfish strength sweep (scripts/nc_teacher_strength.py, antilife,
held-out, 3 seeds so ~0.1 noisy): K16-d2 4.917@281ms | K32-d2 4.879@444ms | K16-d3 4.929@833ms. DEPTH is
EXHAUSTED past 2 (d3 = 4.929, NO gain over d2, 3x slower) — expected: a non-clairvoyant search's deeper plies
look ahead against IMAGINED reshuffled futures, which carry no extra signal. WIDTH (K) helps marginally
(K32 ~0.04 LP, within noise, 1.6x cost) — an optional hair more strength, not a level change. Confirms the
earlier pin: TEACHER = NC K16 d2 (width>depth past 2, d3 noise-to-harmful).
NET: fast NC play (NC-d0/d1) ≈ teacher (K16 d2) ≈ human. The whole chain is validated; the teacher is at the
human ceiling modulo the shared non-clairvoyance tax. Tools: scripts/ref_bench.py, scripts/nc_teacher_strength.py.

## 2026-07-12: MODEL-vs-human + MTG_STABLE_SHUFFLE (common-random-numbers reshuffle)
MODEL on the human hands (ref_bench --model-dyn, antilife 30 refs, land-fold DynNet teacher-d2 K16):
human 4.500 | clairvoyant 4.167 | NC teacher 4.467 | **MODEL 4.933**. The learned model is ~0.45 LP
(≈half a turn) behind human AND teacher on the exact human hands -> NOT human-level; the off-by-default
d0 gate (MTG_D0_LANDFOLD) is not ready to turn on. Consistent with the goldfish dominance finding.

WHY a T1 fetch by two policies diverges the draws (user Q): the fetch's post-search reshuffle
(ShuffleAfterSearch) was a Fisher-Yates keyed on (game_seed, search_count) over the CURRENT library
contents. Different fetch target (human Overgrown Tomb vs teacher Stomping Ground) => different remaining
multiset => different order; and search_count differs if fetch counts differ. So win-turn deltas between
two policies were partly SHUFFLE LUCK, not play. On antilife most human!=teacher games are this
(fetchland-shuffle draw divergence), NOT mistakes -- matches the user's prior ("neither side misplays").
FIX (commit 60080b1, OFF by default = byte-identical): Library::ShuffleByKey orders the live library by a
per-copy key splitmix64(seed, m_number). m_number is a stable deck-setup ID identical across two same-seed
games, so a card's rank is independent of the multiset / search_count -> removing a card leaves the rest's
order UNCHANGED. Two policies then draw the same future modulo the one card each removed; only genuine play
differences move draws. Gated MTG_STABLE_SHUFFLE. Verified: seed22gi21 OFF-diverges/STABLE-aligns (pure
shuffle luck); seed10gi9 diverges under both (genuine play diff, preserved). NC & CLAIR draw identical under
STABLE on gi14. OPEN WRINKLE: the human REFERENCES were recorded under the OLD shuffle, so machine-vs-human
alignment still needs the human's DECISIONS replayed under STABLE (references are commit-only, can't
regenerate). Machine-vs-machine A/B (teacher/model/clairvoyant/NC-d0/d1) is now shuffle-clean. DECISION for
user: make STABLE default (GT rebaseline smoke+regr+overnight) vs keep as a comparison-only flag.

## 2026-07-12 (autonomous): pushing the land-fold MODEL toward human on the antilife ref hands -- every CHEAP lever is dead
Goal: close the model's gap to human on the 30 antilife reference hands (baseline model 4.933 | human 4.500 |
teacher 4.467). Ran the levers:
- **DECISIVE diagnostic -- the 1-ply STRUCTURE is not the limit, the NET-as-leaf is.** NC-K16-d1 is a 1-ply
  ROLLOUT-leaf policy -- the SAME structure as the land-fold net, only the scorer differs (greedy rollout vs
  net). On the ref hands NC-d1 = **4.567** (~human), NC-d0 = 4.700. So a good 1-ply leaf REACHES human; the
  net's 4.933 is entirely "the net is a worse value estimator than a rollout" -- i.e. its INPUTS/compression,
  not covariate shift and not a horizon ceiling.
- **DAgger: DEAD.** Round 1 (200 on-policy games, teacher-d2 labels, aggregated + retrained): 4.933 -> 4.967
  (flat). Round 2 (add 200 more on-policy games): -> 5.500 (WORSE). More on-policy data dilutes the teacher-
  optimal states / teaches the weak model's habits. Also reconfirmed pick-regret does NOT predict play LP
  (round-2 train regret 0.09 IMPROVED while play LP got worse).
- **Capacity: DEAD.** H256/384/512 in the from-scratch C++ trainer are too slow to sweep, and the prior arch
  sweep already showed H64==H128 (8k rows overfit a bigger net). Not the bottleneck.
- **Generic feature (v10 our_noncreature_perms): FLAT.** Added enchantment/artifact/PW visibility (Aria of
  Flame + Tainted Remedy were INVISIBLE to the 48 features -- diagnosed from gi17 where the model cast Aria
  without Remedy, healing the opp to 26, and durdled to T7 vs human T4). Re-dumped + retrained: 4.933 -> 4.967
  (flat, within noise). A generic COUNT doesn't help -- the net needs to tell Aria from Remedy from any other
  enchantment = CARD IDENTITY, not a bucketed count. Reverted (append-only, byte-identical; kept the finding).
CONCLUSION: the land-fold value net is bound by its INPUTS LACKING CARD IDENTITY. Every cheap/moderate lever
(labels, data, DAgger, capacity, a generic feature) is exhausted at ~4.93 (0.4 behind human). The ONLY
remaining lever is CARD-LEVEL features (per-card embeddings + set-pooling over hand/board/library) -- a
substantial from-scratch build (no torch/pip in the container) with a CAPPED, DOMINATED payoff on these decks:
even a perfect net caps at the rollout-leaf's 4.567 and stays dominated by NC-d1 (which is cheap on short
games). Card-level features are only worth it for a FUTURE expensive-rollout regime (phase-2 deep search leaf).
Tooling added: scripts/model_ref_eval.py (fast MODEL-only ref eval, retry-robust), scripts/nc_ladder.py,
scripts/nc_teacher_strength.py. Data under logs/model_improve/ (gitignored).

### combo-feature attempt (v10b, param-based) -- ref-hand gain was NOISE, reverted
Followed up the flat generic-count feature with a MOTIVATED param-based pair: remedy_active (is a
lifegain->loss flip in play, via RemedyActive()) + opp_lifegain_engines (our permanents forcing opp
lifegain, via etb_opponent_lifegain/verse_damage/tap_opponent_lifegain) -- the exact Aria x Remedy combo an
MLP hidden layer could learn. Re-dumped (features fire: remedy_active in 32% of rows) + retrained. Result:
ref hands 4.933 -> **4.867** (looked like a win, worse on 9/30 vs 12/30), BUT antilife GOLDFISH held-out
(600 games, the larger sample) 5.042 -> **5.137 (WORSE)**. The ref-hand gain was noise/overfit to the 30
human hands; no robust skill gain (adding input dims to a 4k-row set overfits). Reverted (append-only,
byte-identical). FINAL: no cheap/moderate hand-crafted feature robustly moves the net -- generic is too
coarse, specific overfits the small data. Card-level LEARNED embeddings remain the only real lever, and stay
dominated by NC-d1 on short-game decks. The fast NC policy at human level is NC-d1/d0 (rollout leaf), not the net.

## 2026-07-12 (autonomous, user greenlit): CARD-IDENTITY features (bag-of-cards) -- NEUTRAL/overfit, not the lever
Built the card-level infrastructure the earlier findings pointed at (MTG_CARD_FEATURES, OFF by default =>
byte-identical): a per-deck card VOCABULARY set once at load (SetCardFeatVocab from deck.mainboard), and
ExtractMidGameFeatures APPENDS, after the 46 fixed enum features, two integer counts per vocab card [copies
in our hand, copies on our battlefield] -- giving the value net CARD IDENTITY (so it could learn Aria x
Remedy etc. the coarse aggregates can't express). The trainer auto-detects the wider width + buckets the
appended (non-"plan_") columns as STATE; DynModel routes by column order; dump + serve use the SAME vocab =>
lockstep. Antilife vocab = 23 distinct cards -> 46 appended features. Wired end to end (main.cpp vocab,
KeepModel featurizer, AIEngine dump headers with sanitized card names).
CLEAN A/B (SAME 400 teacher-d2 games, H96/e45; card model vs the identical rows with card columns stripped):
| model                | ref-hand LP | goldfish LP (600g) | held-out pick-regret |
|----------------------|-------------|--------------------|----------------------|
| no-card (46 enum)    | 4.767       | 5.097              | 0.109                |
| +card identity (92)  | 4.933       | 5.102              | 0.140                |
=> Card identity is NEUTRAL on the reliable goldfish sample (5.102 vs 5.097 = noise) and WORSE on the 30 ref
hands + worse pick-regret = mild OVERFIT (46 independent per-card weights on ~11k rows). It adds no robust
decision signal: the coarse features already capture what the shallow decision needs, and the ~0.5 LP the net
trails the ROLLOUT leaf (NC-d1 4.567) is NOT missing card identity -- it is the fundamental gap between a
STATIC value function and FORWARD SIMULATION. A rollout SIMULATES the combo; a value net (any inputs)
APPROXIMATES it, and richer inputs don't close a simulation gap. (Also: the 100g->400g "data helped" read on
ref hands 4.933->4.767 REVERSED on goldfish 5.042->5.097 = ref-hand noise again; data is not the lever.)
DECISION: kept MTG_CARD_FEATURES as gated, byte-identical, documented infrastructure (repeatable card-level
experiments; a future learned-EMBEDDING variant -- parameter sharing vs these independent per-card weights --
would reuse the vocab/dump plumbing). But bag-of-cards is a NEGATIVE, and the neutral signal is evidence an
embedding variant would also be marginal (embeddings fix overfit, not absent signal). NET of the whole model
push: labels, data, capacity, DAgger, generic feature, combo feature, AND card identity ALL fail to robustly
beat the rollout leaf -> the fast NC policy at human level is NC-d1/d0 (rollout leaf), not the learned net.

## 2026-07-12 KEY DIAGNOSTIC: the model's leak is the OPENING (turns 1-2), NOT the combo -> depth-on-opening
Added a PER-TURN pick-regret breakdown to the dyntrain eval (regret bucketed by decision turn). On the
antilife teacher-d2 rows the model's held-out pick-regret is CONCENTRATED in the opening and near-zero late:
  turn 1: 0.21  turn 2: 0.17  turn 3: 0.09  turn 4: 0.05  turn 5: 0.04  turn 6-8: ~0.05->0.00
=> turns 1-2 are ~65% of the model's TOTAL pick-regret; the model already plays turns 5-8 (incl. combo/kill
turns) NEAR-OPTIMALLY. Robust across the card model and a different holdout split (turn-1 0.25-0.35 every time).
REFRAMES the whole push: the hard part is NOT the complex combo turn (the model + exact-lethal check handle it)
-- it is the OPENING DEVELOPMENT (which land to fetch / dork to play), whose payoff is many turns away and so is
hardest for a 1-PLY STATIC value to evaluate (longest horizon). This is exactly where DEPTH/lookahead should
help, and an opening-specific depth benefit would WASH OUT in the teacher's aggregate d2-vs-d3 tie (d3 could help
turns 1-2 while neutral/noise elsewhere). 
NEXT (resume hypothesis): test whether DEPTH helps the OPENING specifically -- (a) NC teacher per-turn win
contribution at d0/d1/d2/d3 (does deeper search improve turn-1-2 lines?), and (b) a HYBRID fast policy = bounded
lookahead (depth 1-2) on turns 1-2 where the net is weak + fast net on turns 3+ where it's near-optimal (the
user's "bolt-on search for the hard turns", applied to the OPENING not the combo). Tooling: dyntrain per-turn
eval (byturn=true). Rows: logs/model_improve/al_{nocard,cardfeat}.rows (400 teacher-d2 games).

## 2026-07-12 DEPTH investigation (user: don't discount depth for the opening) -- cost + plan
Cost gauge (antilife): NC-d5 PLAY ~28-140s/game (a single d5 game up to 2min) = ~15-70x NC-d2 (~2s/game).
=> deep NC PLAY is feasible-but-slow; deep NC LABELS (MTG_EVAL_ROLLOUT_DEPTH=5, a d5 continuation PER candidate
PER reshuffle) are ~infeasible for a full dump. KEY LEVER: the model's leak is ONLY turns 1-2, so if depth helps
we only need DEEP labels for the OPENING (2 decisions/game) + keep cheap d2 labels for turns 3+ = a MIXED-DEPTH
label scheme that makes deep opening labels tractable.
DECISIVE TEST RUNNING (bg, logs/model_improve/depth_test.{py,out}): NC-K16 d2 vs d3 vs d5 aggregate LP on
antilife (3 seeds x 40g). IF d5 meaningfully beats d2 -> NC depth DOES help (prior d3=d2 was noise) -> pursue
mixed-depth deep-opening labels + possibly serve-side bounded lookahead on turns 1-2. IF d5 ~= d2 with a good
sample -> NC depth is genuinely non-clairvoyance-plateaued and the opening leak is NOT fixable by NC depth (look
to serve-side clairvoyant-free lookahead or accept the limit). Read depth_test.out on resume.

## 2026-07-12 CRYSTALLIZED: static ranker is DOMINATED -> pivot from "better model" to "model as PRIOR"
The depth-schedule idea (below) and this whole session's static-model push (card features, DAgger, deeper
labels) all polish the model's TOP-1 STATIC ARGMAX -- and the frontier data says that is the wrong tool:

  antilife GOLDFISH (scripts/nc_frontier.py, 180g): static d0-model **5.561** vs NC-d0 **4.939** vs NC-d1
  4.950 vs NC-d2 4.978.  TH: static 5.700 vs NC-d0 5.095 vs NC-d1 4.978 vs NC-d2 4.884.

Two facts, both goldfish-solid (NOT a 30-hand ref artifact -- the earlier "improvements were noise" lesson
was about ref, this is the 180g frontier):
  (1) The static evaluator is STRICTLY DOMINATED by a raw K-averaged rollout LEAF at equal-or-less wall
      clock (0.62 LP on antilife). A better static label/feature can shrink prediction MSE but cannot beat
      the tool: the rollout closes the gap, the static argmax cannot. => card-features (neutral), DAgger
      (flat), depth-labels (see d5 test) are all polishing top-1 argmax = the wrong lever for QUALITY.
  (2) DEPTH is flat on antilife: NC d0~=d1~=d2 (4.939/4.950/4.978), and the running d2-vs-d3 test agrees
      (5.100 vs 5.108). d5 is the last "don't discount it" check (bg), but the label path is refuted a level
      DEEPER than depth: even PERFECT deep labels only make a better static argmax, which is dominated.

PIVOT (the doc's own frontier conclusion, line ~1988): the model's value is INSIDE a forward search, as
(a) a VALUE LEAF of a shallow NC search and/or (b) a POLICY PRIOR that prunes the K x #plans branch to the
top-M -- keeping the engine's rollout as the free exact dynamics. A naive value-LEAF is refuted for antilife
(it just reproduces the dominated static 5.561), so the built lever is the PRIOR:

  MTG_NC_TOPM=<M> (TurnSolver::ReshuffleAvgChoosePlan): the model scores every candidate once (K=1, ranking
  only), only the top-M (plus any this-turn lethal) get the expensive K-reshuffle rollout, the rest are left
  unrolled (sentinel). Preserves the ROLLOUT that closes the gap, paid on M plans not N -> the "model as a
  SPEED play" deliverable for antilife's flat per-plan enumeration/rollout cost. Inert/byte-identical unset.
  Bet: top-M reliably CONTAINS the rollout's true best (far easier than top-1 exact) -> LP ~= NC-d0 at a
  fraction of the wall-time. RISK (1-game smoke: TOPM=4 moved one game 4->5): M too small can prune the best
  plan; the sweep finds the crossover. Measured by scripts/nc_topm_sweep.py (auto-chained after d5 frees CPU;
  results -> logs/model_improve/topm_sweep.out). Also landed inert: MTG_EVAL_DEPTH_SCHEDULE (per-turn label
  depth ladder "5,4,3,2") kept in case d5 surprises, but see fact (1) -- deep labels only help a dominated
  argmax, so this is a low-priority fallback.

## 2026-07-12 RESULTS: policy-prior (MTG_NC_TOPM) WORKS with a good ranker + DEPTH is dead (d5 measured)
Sweep across all 5 decks (scripts/sunday_topm_all.sh, K16 d2, 6 seeds {4001,4002,9009,2002,3003,7007} x40g
=240g/config). LP (losses=mt+1), dLP vs TOPM=off (pure rollout NC), wall-time for 240g:

  antilife (DYN model):  off 5.083/138s | top8 5.087/133s | top4 5.042/101s | **top2 4.933/83s** | top1 5.412/82s
  TH       (DYN model):  off 4.858/173s | top8 4.867/140s | **top4 4.896/84s** | top2 5.183/47s | top1 5.375/28s
  burn     (value GBDT): off 4.454/7s  | top8 4.467/6s | top4 4.487/5s | top2 4.633/4s | top1 5.158/3s
  knights  (value GBDT): off 4.346/33s | top8 4.400/25s | top4 4.675/22s | top2 5.421 | top1 6.804
  slivers  (value GBDT): off 4.279/67s | top8 4.392/34s | top4 4.688/21s | top2 5.292 | top1 5.771

INTERPRETATION -- the prior's quality is ENTIRELY the ranker's quality:
- DYN models (teacher-d2 trained, antilife/TH) are GOOD priors. antilife **top-2 is BETTER AND 40% faster**
  (4.933 vs 5.083, 235 vs 229 wins -- the regularization effect: the prior prunes plans the 16-sample rollout
  occasionally mis-ranks as best; validated at 240g, was -0.166 at 120g). TH **top-4 is lossless (+0.037 =
  noise) at 51% faster** (84s vs 173s) -- the doc's "TH wants the policy-prior" (depth-branch cost) confirmed.
- value.json GBDTs (burn/knights/slivers, older CLAIRVOYANT-trained MidGameEvaluators) are WEAK priors: burn
  tolerates top-4 (near-lossless), but knights/slivers regress even at top-8 (+0.054/+0.112). Too weak to prune
  safely -- but these decks have NO dyn model; a teacher-d2 dyn would likely behave like antilife/TH.
- top-1 (pure model argmax as "prior") regresses EVERYWHERE (dominated, as established) -- the value is the
  model PRUNING to a small set the rollout then decides, NOT the model deciding.

=> RESOLUTION of the session: the static model is dominated as a POLICY but VALUABLE as a PRIOR. antilife top-2
makes the NC search both FASTER and BETTER; TH top-4 lossless + 2x faster. "Model as a speed play" CONFIRMED,
with a quality bonus on antilife. Per-deck sweet spot: antilife top-2, TH top-4/8, burn top-4/8; knights/slivers
need a dyn ranker before pruning is safe. ADOPTION = per-deck M in the archetype provider (heuristic-opt skill;
needs user sign-off + held-out validation -- these 6 seeds mix train/frontier/depth sets).

DEPTH IS DEAD (measured, re-run after the fd-clobber, logs/model_improve/d5_rerun.out):
  NC-K16 antilife 120g:  d2 5.100/48s | d3 5.108/188s | **d5 5.092/6363s (106 MINUTES)**.
d5 == d2 within noise (0.008) at 130x the wall-clock. The user's "maybe we need d5/d6" hypothesis is decisively
answered: NO. NC depth past 2 buys nothing (deeper plies plan vs imagined reshuffled futures = no signal). The
label-depth path (MTG_EVAL_DEPTH_SCHEDULE) is confirmed a dead end -- deeper labels only make a better argmax,
which is dominated, AND depth itself doesn't even improve the rollout it would distill.

## 2026-07-12 MODEL ROUTE THAT WORKS: color-aware castability (MTG_MANA_FEATURES) -- diagnosis-driven
The failure analysis (dyntrain fail-analysis pass) showed the model's residual mis-ranks are MANA-
SEQUENCING, and reading the code found the cause: HandCastableNow checks only mv<=untapped_sources --
BLIND TO COLOR. So two resulting states with the same total mana but different untapped colors are
feature-identical to it (the src_r/src_u distinction the failures turn on). Added MTG_MANA_FEATURES:
hand_castable_colored (nonland hand cards whose per-color pip demand the untapped src[] satisfy) +
hand_colorscrew (affordable by MV, wrong colors). Appended after the enum block (byte-identical off).

A/B (same games+labels, model trained WITH all cols vs WITH the 2 mana cols stripped; held-out):
  TH   (38% mana-aliased): top1 9.1->22.7%  recall@2 90.9->95.5%  pick-regret 0.074->0.023 (3x)
                           STANDALONE land-fold play LP 5.481 -> 5.362 (-0.119, a REAL pure-model gain)
  antilife (0% aliased):   flat (regret 0.105->0.080, play 5.153->5.172) -- feature adds nothing where
                           color was never the missing signal, exactly as the aliasing diagnosis predicted.
=> The color feature helps precisely the decks whose failures were mana-aliased. This is a genuine model-
representation improvement (NOT search): a feature derived from diagnosing WHY the model mis-ranks,
validated on the standalone model's PLAY. First lever this whole push that moved standalone play on the
right axis. CONFIRMING at scale + generalizing to slivers/knights (multi-color, expect help) vs burn
(mono-red control, expect flat): scripts/mana_feature_all.sh -> logs/model_improve/mana_feature_all.out.
Small-sample caveat (TH n=22 test decisions; standalone play on 320 held-out games is the trustworthy leg).

## 2026-07-12 CORRECTION: the mana-feature TH win did NOT replicate -- it is NEUTRAL at scale
The TH standalone gain (5.481->5.362) above was n=22-decision small-sample NOISE. Broader A/B
(scripts/mana_feature_all.sh, 5 decks, 180g dump + 320g held-out play, model H128 e100):
  TH std 5.403->5.438(worse) | slivers 4.569->4.650(worse) | knights 4.528->4.531(flat) |
  antilife 5.200->5.131(BETTER -- the 0%-aliased CONTROL, opposite of the prediction) | burn flat.
  PRIOR top-4 LP flat within +-0.035 everywhere. Mean effect ~0, +-0.08 deck scatter, prediction FAILED.
=> MTG_MANA_FEATURES is a NEGATIVE (kept inert/off). 7 falsified levers now (depth, DAgger, card-identity,
policy-CE, capacity, K32-labels, color-features) => the standalone static model is at a robust ~0.5-LP
ceiling. Measuring the irreducible LABEL-NOISE floor next (MTG_LABEL_SALT, label_noise_floor.sh) to tell
whether the residual is model-fixable or noise -- if the model's ~0.1-0.17 pick-regret ~= the floor, the
model is near-optimal and the "gap" is largely an artifact of K=8 label sampling.

## 2026-07-12 DECISIVE: the model is a NEAR-OPTIMAL RANKER at the LABEL-NOISE FLOOR (why no lever helped)
Measured the irreducible label-noise floor (MTG_LABEL_SALT, scripts/label_noise_floor.sh): label the SAME
games twice with independent K=8 reshuffle streams, grade dump-A's teacher-best plan under dump-B's labels.
  antilife: floor pick-regret = 0.120  (teacher argmin AGREES with its own relabel only 67%; |dLabel|/cand 0.25)
  TH:       floor pick-regret = 0.099  (agree 65%; |dLabel|/cand 0.23)
Model pick-regret on the same served rsvalue rows:  antilife 0.173 (excess 0.053 over floor)  TH ~0.09-0.11
(AT the floor). => The teacher DISAGREES WITH ITSELF ~1/3 of the time; the model already ranks within ~0.05
(antilife) / ~0 (TH) of that irreducible floor. **The model is a near-optimal ranker; ~70-100% of its
measured pick-regret is K=8 label sampling noise, NOT model error.** This EXPLAINS all 7 falsified levers
(depth/DAgger/card-identity/policy-CE/capacity/K32-labels/color-features): you cannot rank better than the
labels permit, and the labels are noise-limited, not model-limited. Cleaner (higher-K) labels lower the floor
but don't help PLAY -- because play is not ranking-limited.

THREE-WAY PROOF the standalone static model cannot reach the teacher, and WHY:
  (1) DOMINATION: static d0-model 5.561 vs NC-d0 rollout 4.939 goldfish (the rollout SIMULATES, closes the gap).
  (2) FEATURE/RANKING CEILING: recall & pick-regret invariant to loss/capacity/model-class/features.
  (3) LABEL-NOISE FLOOR: model pick-regret ~= the teacher's own self-disagreement floor.
The standalone PLAY gap (std ~5.2 vs teacher/prior ~4.3-4.9) is therefore NOT a ranking deficiency the model
can train away -- it is the STATIC-VALUE-vs-FORWARD-SIMULATION gap (+ covariate shift when the model plays its
own games). The teacher's entire edge IS the forward simulation, which is not encodable in a static value.

CONSEQUENCE for the deliverable: the "prior" (model prunes -> minimal rollout decides) is NOT a fallback-to-
search concession -- it is the PRINCIPLED architecture the proof implies: a near-optimal static ranker plus the
MINIMAL forward simulation that a static value provably cannot contain. The rollout supplies exactly the missing
piece, nothing more. The ONLY way to put the simulation INSIDE the model is a learned WORLD-MODEL (MuZero-style
latent dynamics with multi-step state-reconstruction supervision) -- a big, uncertain, multi-day build needing
per-turn trajectory dumps; the latent-dynamics net (T2/T3) already showed no ranking lift, so odds are guarded.
That is the one categorically-different lever left; bring to the user before committing days. NOT launched.

## 2026-07-12/13 WORLD-MODEL (the last categorically-different lever): built + tested
Built the "learn to simulate" model to test whether a learned forward simulator can beat the static-value
label-noise floor. Foundation: MTG_DUMP_TRAJ (commit) dumps teacher executed trajectories (per game, per
turn: state feats + turns-to-go). Trainer (tools/worldmodel/): encoder rep + DYNAMICS dyn + DECODER dec +
VALUE val, dual-dataset -- value head ranks per-candidate rsvalue rows (coverage), dynamics/reconstruction/
bootstrap learn from trajectories (shared encoder). Serve idea: BOOTSTRAPPED value V(s)=0.5 val(h)+0.5(1+
val(dyn(h))) averages consistent estimates across the learned trajectory -> the one mechanism that could
denoise below the per-state floor.

SWEEP (400 antilife + 400 TH trajectory games; rsvalue held-out pick-regret, floor ~0.12/0.10):
  antilife baseline(value-only) 0.183 | recon.1/boot.5 STATIC 0.133 | recon.5/boot0 boot 0.156 | others 0.16-0.19
  TH       baseline 0.162 | best static 0.160 (flat) | most configs WORSE
VERDICT (core mechanism): the BOOTSTRAPPED forward-simulated value is a clear NEGATIVE -- worse EVERYWHERE
(0.25-0.39 vs 0.16-0.18) because the learned dynamics is too inaccurate (reconstruction error stays high) to
denoise. The "model simulates forward to a better value" hypothesis is REFUTED on this data. The only flicker
is reconstruction-as-a-REGULARIZER on antilife's static value (0.183->0.133) but it does NOT replicate on TH
and is n=105 (the same small-sample shape that faked the mana-feature win) -> replicating across holdout splits
before believing it (logs/model_improve/wm_repl.out). Even if real, pick-regret is mostly label noise (floor
analysis) so it likely won't move PLAY -- would need a serve+play-LP confirmation.

## 2026-07-13 WORLD-MODEL VERDICT: NEGATIVE (8th falsified lever) -- with the ROOT CAUSE
Replication across 4 holdout splits settles it: world-model (recon.1/boot.5) mean pick-regret = value-only.
  antilife: value-only 0.1668 vs world-model 0.1655 (identical, +-0.03 split scatter; the 0.133 was ONE split)
  TH:       value-only 0.2234 vs world-model 0.2272 (slightly worse)
The bootstrapped forward-simulated value is WORSE everywhere; the recon-regularizer is split-selection NOISE.

ROOT CAUSE (from the training losses): val loss 0.41 (predict current value directly) vs boot loss 1.35
(predict next value via the learned dynamics) = rolling the value forward ONE step is 3.3x worse. The learned
dynamics LOSES value-relevant information because the next state depends on the HIDDEN DRAW the model cannot
predict. This is the entire investigation in one number: forward simulation beats every static/learned predictor
*because a real rollout SAMPLES actual draws*, and any non-clairvoyant model (static value OR learned world-model)
cannot. Non-clairvoyance is not a feature/architecture problem -- it is the irreducible structure of the task.

FINAL PICTURE (8 falsified levers: depth, DAgger, card-identity, policy-CE, capacity, K32-labels, color-features,
world-model): the standalone learned model is a NEAR-OPTIMAL RANKER at the label-noise floor, and it CANNOT reach
the teacher's PLAY quality because the teacher's edge is forward simulation over sampled draws, which no model can
internalise. The deliverable that reaches teacher is therefore necessarily hybrid: a near-optimal learned ranker
(prunes the branch, top-M) + the MINIMAL real rollout that supplies the sampled-draw simulation (the NC prior,
MTG_NC_TOPM). That is not a fallback -- it is the architecture the evidence forces. Reusable infra kept (inert):
MTG_DUMP_TRAJ, tools/worldmodel, MTG_MANA_FEATURES, MTG_LABEL_SALT, dyntrain fail-analysis + --gamma.
