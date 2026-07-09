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
   (value-leaf d5 beats heur d1 on all 5 decks, held-out) and goals #2/#3/#4 (exact-parity 1.6–25× speedup).
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
