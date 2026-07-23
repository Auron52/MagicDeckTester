# Dragonstorm: model-less d5 default (shipped) + value-leaf plan (deferred)

2026-07-21. Two linked items: a shipped resolver fix, and a deferred value-leaf build.
Context: Dragonstorm was added to the regression suite (viewer bug-bash) but never got a
learned value model, unlike every other suite deck.

## Bug found + FIXED: model-less "d5" case silently ran at depth 0

The regression harness (`test/regression.sh`) emits every **d5** case *without* an explicit
`depth`, trusting each deck's enabled `value_play` block to set depth 5. Dragonstorm has **no
`.value.json` sibling** (the only suite deck without one), so with no block driving, the resolver
fell through to greedy **depth 0**. Result: `dragonstorm_*_d5` GT (avg 8.04, digest `ba615fa2…`)
was a **depth-0 run mislabeled as d5** — byte-identical to an actual d0 run. Real depth-5 play is
**~5.67** (right next to d3's 5.62); there was never a "d5 < d3 anomaly", just a depth-0 case.

**Fix (per user: "the default should always be d5 budget 20 when we don't have a model unless we
specify otherwise" — the intended behavior):** `ResolvePlaySettings`
(`src/ai/MulliganProfile.h`) now defaults an **omitted depth** (no enabled block) to the built-in
default depth **d5**, not 0 — so a model-less deck asked to search (a budget given with no
`--depth`) never silently collapses to greedy depth 0 (which ignores the budget). An explicit
`--depth` still wins (the d0/d3 coverage cases pin their depth + `ignore_play_profile`, unchanged).
The `[play]` line now reads `depth=5 … source=default(depth)+cli(budget)`.

Containment (smoke): **only** `dragonstorm_smoke_d5` moved (8.04→5.6667); d0/d3 + all 6 other
decks byte-identical PASS; scenarios 4/4. GT rebaselined smoke+regression (overnight deferred —
contained change, only `dragonstorm_*_d5` moves).

## GENERATED 2026-07-22 (PROVISIONAL) — value-leaf model + tables built and adopted

Built the value-leaf on the frozen post-Unclaimed commit (e6c1f2c), per the user's explicit request
to generate it now (this pre-empts the reference-gating in the "Deferred" plan below — no hand-played
Dragonstorm references exist yet, so the model is **NOT** reference-validated; treat as PROVISIONAL).

- **Label = ROLLOUT (not searched).** Chosen because the matrix shows Dragonstorm's heuristic is
  already GOOD (converges cheap by d3), which is exactly the regime where `learned-d0-policy.md` says
  rollout labels (imitate the baseline) beat searched labels (distill a clairvoyant optimum the
  baseline doesn't reach) — and rollout dumps are ~10x cheaper (no clairvoyant heavy-tail). The
  "Deferred" section below anticipated a searched label; rollout is the better fit given the measured
  heuristic quality, but **searched labels remain an untried alternative** worth an A/B later.
- **Pipeline:** d3-play rollout dump (K=8, `MTG_EVAL_ROWS_ROLLOUT=1`, 400g) → 9164 rows → GBDT
  (200 trees, depth 5, min-leaf 20; pair-acc 73.8%) → `decks/Dragonstorm/Dragonstorm.value.json`
  `eval_model`. Depth matrix (`--hdepths 1 2 3 --vdepths 1 2 3 4 5 --games 400 --seeds 4004 5005`,
  unbounded): heuristic converges H3=4.778; value-leaf V5=4.786 (**+0.009 vs H3**). So
  `value_trust_depth=UNSET` (never within tol=0.002 of converged H → escalate every depth) and
  `value_no_fallback=False` — identical profile to the other combo decks (hinata/antilife). Wrote
  `value_leaf_table` + `value_fallback_crossover` via `valueleaf_table_to_metadata.py` and a combo
  `value_play` block mirroring hinata/antilife (depth-aware value-ranked beam W3 leafdepth2 +
  budget-restore fresh0.5, regime heavy, target_depth 5, escalation_cap 5) — the beam bounds the
  combo tail (a raw unbounded value-on run hung on one combo state; the beam fixes it).
- **Adoption measure (regression, multi-seed):** quality-NEUTRAL — d3 s2002 −0.003 / s3003 +0.013,
  d5 s2002 −0.004 / s3003 +0.012 (**mean +0.0045**), ~**1.8x faster** at budget (50g timed 13.9s vs
  24.8s). d0 byte-identical (the d0 ranker gate `MTG_EVAL_MODEL` stays off — only the *value leaf*
  engages at d3/d5), all non-Dragonstorm decks byte-identical. Per-game it's a wash (d5 s2002: 4
  faster incl. gi13 6→5, 3 slower incl. gi179 6→7). Smoke seed s1001 d5 was a small-sample +0.053
  outlier; the powered seeds are neutral.
- **Caveats to revisit:** PROVISIONAL 2-seed/400g table; rollout (not searched) label unvalidated
  vs searched; NOT reference-validated (build refs then re-check); `value_trust_depth=UNSET` means
  the leaf is escalated everywhere so the speedup is the main win, not clairvoyant depth. Artifacts:
  `logs/eval_dragonstorm/` (rows, gbdt, budgeted_ab), `logs/eval/valueleaf_depth_dragonstorm.txt`.

## Deferred: build a value-leaf model for Dragonstorm (gated on references)

**Why it's the right general lever (not per-deck hand heuristics):** the user dislikes
hand-authoring per-deck d0 heuristics, and d0 is inherently weak for a combo deck (the storm
payoff only materializes after assembling a multi-cast chain — greedy myopia can't value it). The
value-leaf is the automated, "learn-not-hand-author" alternative and is **trained on the
clairvoyant earliest-win SEARCH label**, NOT the greedy heuristic
(`src/ai/AIEngine.cpp` MTG_EVAL_ROWS_* comments). The d5 search plays Dragonstorm well (finds the
ritual→payoff chain within 5 plies), so the model distills *good* labels. It then upgrades the
live search's **truncated rollout**, which caps the tail with the O(1) value-leaf instead of
playing greedily to the end (`src/ai/TurnSolver.cpp` `SimulateToEndImpl`, ~L6358). So the
value-leaf *is* the "value-leaf rollout backup" — already the mechanism, not a new idea.

**The one caveat:** the training label is clairvoyant but the live model is non-clairvoyant
(can't see future draws) → optimistic on draw-dependent lines. For Dragonstorm the gap is smaller
because the combo is in-hand (visible), not draw-gated.

**Sequencing (why it waits):** generate the value-leaf **late, on a frozen commit, only once play
is validated** (the mulligan/value-model rule) — don't distill a buggy line. The user will provide
more hand-played **references** (`references/Dragonstorm/claude_s*_gi*.json`) first to validate the
engine's Dragonstorm play; build the value-leaf after that. Until then the model-less **d5/20
default** (shipped above) gives real depth-5 play.

**When ready:** follow `.claude/skills/mulligan-profile.md` / the value-leaf pipeline — generate on
a frozen commit, derive the value_trust_depth / value_leaf_table, write `Dragonstorm.value.json`
next to the deck, validate against the references, then rebaseline GT. After that the `d5` case
runs value_play-driven like every other deck (and the harness's depth-key-drop for d5 becomes
correct for Dragonstorm too).
