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

### ▶ NEXT STEPS (resume here — 2026-07-08)

State: value model committed + generalized (5 decks, `decks/*.value.json`, inert-gated); `plan_baseline_eval`
d0 feature committed (validated on burn); combo-aware d0 feature tried + reverted (negative). Everything
pushed to `learned-d0-evaluator` @ `abdf4ba`. Tree clean, nothing running.

**Where quality headroom actually is** (measured this session): converged decks (TH/burn/knights/slivers)
are at/near the non-clairvoyant ceiling at d0 — no headroom, and more data/features buy ~0. The two real
gaps are (a) **budget-starved / non-converging positions** and (b) **antilife d0** (learned 30–55% vs the
non-clairvoyant hand-tuned baseline's ~90% — achievable, but gated).

**Recommended next step (highest EV, tractable): convert the value model's 10–15× speedup into search
DEPTH and measure the quality gain on budget-starved decks** (the doc's own §5f "reinvest the savings").
- Concrete first test: a deck with a KNOWN budget-starvation gap — start with **TH's Land's-Edge line**
  (starved at ~b200, recovers at ~b2000). A/B `value-leaf at high depth/budget` vs `baseline at the
  suite's shallow budget` at **equal wall-clock**, diff per-game win turns.
- **Verify the gain is real, not clairvoyance**: re-run under the `MTG_SHUFFLE_SALT_SEARCH` decouple
  instrument (see the hinata-gate-sweep audit) — a gain that reverses when decoupled is a clairvoyance
  artifact, not a ceiling to beat.
- Why this over more d0 features: on converged decks the value-leaf only *ties* the rollout (leaf inert);
  the win is on positions the search can't resolve in budget, which is exactly what cheaper leaves unlock.

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
plan-summary misses, and inherits the user's d5–d7 depth for free). Needs seam+dump lockstep wiring
(evaluate the value model on each applied candidate plan) — scoped, not yet built. Secondary: hand-crafted
non-clairvoyant sequencing features (develops-attacker, curve/mana-efficiency, holds-reach).

**Also settled here:** a *shared* cross-deck LINEAR ranker still collapses under rollout labels (burn/antilife
0%) — sharing needs nonlinearity/deck-conditioning. And `MTG_EVAL_ROLLOUT_DEPTH` (the rollout-policy depth
knob) is committed; depth>0 is affordable on aggro but intractable on combo (hinata).

## The permanent regression gate

At every phase: with **no sidecar or `MTG_EVAL_MODEL` unset**, smoke + regression digests are
**byte-identical** to committed GT. The seam defaults `rank_value = total_eval`; the sidecar is
presence-gated; the dump hook is env-gated. This "no-op reproduces GT" property is what makes the
whole feature safe to land incrementally.
