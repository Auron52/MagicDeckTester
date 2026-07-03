# Exhaustive bucketed keep/bottom policy

Status: **in active development, uncommitted** (as of 2026-07-03). Keep integration, aggregation
raw-sidecar, **merge tool, bottoming phase 2, and the A/B harness are all built**; the R=20 validation
run **passed** (results below). Remaining before R=100: A/B smoke → commit → pull the other agent's
code → generate R=100 on the user's schedule. This is the successor to the learned-keep-model effort
([better-mulligan-model.md](better-mulligan-model.md),
[keep-model-selection-by-runner.md](keep-model-selection-by-runner.md)) — it *replaces modeling with
exhaustive evaluation* where the deck is compressible enough.

## R=20 validation (2026-07-03, diagnostics binary — pre realization-sampling/R-sweep)

The full R=20 diagnostic run over the 10-bucket Slivers space (13,845 distinct hands) **confirmed the
R=4 prelim at scale**:
- **Policy gap: D_static − D_opt = 0.068 turns avg** (draw 0.057, **play 0.079**) in expected goldfish
  win-turn. The exact optimal keep beats static by ~0.07t on the objective.
- **Static's error is dominated by OVER-KEEPING:** of 1709 disagreeing hand-types (20.6% of draw-mass),
  static over-keeps **16.0%** of mass and over-mulls only 4.6%. The top disagreements are land-flood
  hands the optimal policy correctly mulligans (e.g. *4× Cavern + 2× one-drop + Vial*). A few go the
  other way (strong aggressive hands static wrongly pitches). Optimal keeps ~18–20% of hand-types vs
  static's 34.5% — far more selective.
- **Noise at R=20 is well-controlled:** prob-weighted per-hand stderr 0.108t; **est. win-turn lost to
  label noise ≈ 0.0041t (~6% of the 0.068 gap)** — even R=20 resolves ~94% of the advantage. Answers the
  "do we need >R=100?" question: **no.** R=100 (√5× tighter) drops residual noise-regret to ~0.0018t.
  Caveat: 27.9% of mass sits within 2 stderr of the keep/mull threshold (1398 near-tie hand-types), so
  individual borderline hands still flip at R=20; R=100 firms those.

Two caveats carried forward: (1) this was the *diagnostics* binary (no realization-sampling / R-sweep),
so it validates gap+disagreement+noise but did not serialize a shippable table. (2) `D_static` uses
bucket representatives → it's the analyzer's *estimate* of the ceiling; **the in-game A/B is the
definitive static check** (harness built, see below).

## The idea

A hand's keep/mull decision is a pure function of its **bucket composition** once functionally-equivalent
cards are merged. Bucketing collapses the distinct-hand space enough to **evaluate every hand exactly**
instead of fitting a model:
- **Equivalence is objective-relative (goldfish), measured not assumed.** Two cards merge iff swapping
  one for the other in CRN probe hands never changes the clairvoyant win-turn. The goldfish is blind to
  opponent-facing differences (removal targets, combat, resilience), so far more merges than a player's
  real-game intuition — but only goldfish-relevant axes (cost, curve, mana production, clock, draw,
  mana-substitution like Vial) keep cards apart. Slivers: 36 cards → **K=10 buckets → 7758 distinct
  7-card hands** (vs C(60,7)=386M).
- **Evaluate V[size][composition] = expected goldfish win-turn** of keeping that composition, averaged
  over R library continuations (blind). From the V tables: optimal keep policy = exact backward-induction
  over the hypergeometric-weighted hand distribution; optimal bottoming = the (7-m)-subcomposition with
  the lowest V. Zero model/generalization error; the only approximation is Monte-Carlo label precision R.

## Why it wins (validated on Slivers)

- Exact optimal policy beat static by **~0.06 turns** (R=4 prelim) — a keep-decision-only gap (bottoming
  held optimal for both).
- Fixes static's **both** failure modes: over-keeping the 2-land+4-Vial trap (optimal MULL, V=5.75 vs
  mull 4.53 — 1.2 turns off), and over-mulliganing 1-rainbow-land+2-Vial (Vials substitute for lands →
  keepable; static's min_lands gate blindly pitches it).
- Captures interactions static structurally can't: **1 Ziggurat + 2 Vial = MULL** but **1 rainbow +
  2 Vial = KEEP** — Ziggurat makes creature-only mana so it can't cast the (artifact) Vials; the
  discovery kept Ziggurat a separate bucket (0.068 from rainbow lands) exactly for this, and it flows
  through even the noisy R=1 table.

## Feasibility (the generalization question)

Distinct-hand count ~ C(K+6,7), driven by the effective bucket count K. **1-ofs are the killers** (each
is a fresh dimension); 4-ofs self-collapse (4 copies → one bucket, count 4) even without cross-name
merging. Measured:

| profile | K | hands | full R=100 (×2 pd, ~110 rollouts/s) |
|---|---|---|---|
| Slivers | 10 | 7,758 | 3.9 h |
| 15 four-ofs, no cross-merge | 15 | 114,480 | 57.8 h |
| 10×4+4×3+4×2 | 18 | 316,018 | 160 h |
| 12 four-ofs + 12 one-ofs | 24 | 1,019,304 | 515 h |
| 60 one-ofs | 60 | 386,206,920 | infeasible |

Degradation ladder: **full exhaustive** (small K) → **probability-thresholded** (enumerate the common
core above a draw-prob floor, static/model fallback on the rare tail) → **model trained on
bucketed-composition features with CRN labels** (large K). Two levers that stack to extend reach:
1. **Low R** — noise only flips near-ties so the policy-regret-vs-R curve flattens early; the analyzer
   now prints a projected-regret-vs-R curve (extrapolated from per-hand variance) to read the minimum
   viable R per deck. A K=15 deck at R=15 is ~9 h.
2. **Thresholding** — coverage curve (cheap, no rollouts) says how many hands cover 95% of draw mass.

A feasibility pre-check mode (bucket → distinct-hand count + coverage curve, no rollouts) is planned so
"exhaustive vs thresholded vs model" is a two-second per-deck decision.

## Runtime / how it's wired

- **Bucketing:** `src/analyzer/EquivalenceDiscovery.{h,cpp}` — `DiscoverEquivalence()`, CRN substitution
  → behavioral signatures → single-linkage clustering at a distance threshold. Mode `MTG_EQUIV_DISCOVER=1`
  (env: `MTG_EQUIV_PROBES` default 200/400, `MTG_EQUIV_THRESHOLD` default 0.01, `MTG_EQUIV_DEPTH` 5).
  Threshold 0.01 sits in the empirical gap (merge-worthy ≤0.005 vs distinct ≥0.047); exact-match
  fragments as probes grow, so use a threshold. Slivers buckets: {Predatory,Muscle,Sinew,Leeching},
  {Cavern,Secluded,Unclaimed,Sliver Hive}, {Galerider,Plated,Striking}, {Crystalline,Hibernation}, +6
  singletons (Aether Vial, Ancient Ziggurat, Cloudshredder, Hatchery, Mutavault, Thrumming Hivepool).
- **Evaluator:** `src/analyzer/ExhaustiveKeep.{h,cpp}` — `RunExhaustiveKeep()`. Mode `MTG_KEEP_EXHAUSTIVE=1`
  (env: `MTG_KEEP_ROLLOUTS` R default 100, `MTG_KEEP_MAXMULL` default 3, `MTG_EQUIV_SEED` bucketing seed
  default 20260701 — FIXED across machines, `MTG_COMMIT` stamp). Realization-sampling: each rollout pulls
  the hand fresh from a freshly-shuffled library (samples WHICH bucket members fill slots ∝ deck counts
  AND the continuation) — removes representative bias, self-corrects near-equal merges. Reports: policy
  value D_opt vs D_static, full ranked disagreement decomposition, label-noise (per-hand stderr + mass
  near threshold + est. win-turn lost + projected-regret-vs-R). Writes `<deck>.keepmodel.exhaustive.profile.json`
  (runtime policy) + `<deck>.keepmodel.exhaustive.raw.json` (poolable raw sum+count).
- **Runtime policy:** `src/ai/ExhaustiveKeepPolicy.h` (bucket map + per-composition keep flags
  [(max_mull+1)×2] + reserved `bottom_keep`). Field on `MulliganProfile`; serialized in
  `MulliganProfileIO.h` (`ExhaustiveKeep{To,From}JsonObj`). `AIEngine::KeepHand` consults it FIRST
  (present=false → falls through to keep_model/static, so other decks byte-identical).
- **eval-hand probe:** `mtg <deck> --profile <exhaustive.profile.json> --eval-hand "n1;n2;..." [--eval-mull N] [--eval-draw]`
  runs the exact runtime keep predicate on a constructed hand.

## Aggregation (multi-machine pooling)

Raw sidecar stores per-(size,composition,pd) rollout **sum + count** + meta `{commit, bucket_fp, deck_fp,
seed_base, R, equiv_seed, ...}` (FNV-1a fingerprints, deterministic cross-machine). Bucketing seed
(`equiv_seed`, fixed) is decoupled from the rollout seed (`--seed` = the per-run seed_base) so buckets
are byte-identical everywhere while continuations are disjoint. **Merge = element-wise sum of sum+count**
across sidecars, gated on matching {commit, bucket_fp, deck_fp} and DISTINCT seed_base (no double-count).
Pooling is speed-agnostic: a slower second machine's rollouts sum in with no coordination (contribution ~
speed×time; ~340 effective-R/day on this machine for Slivers, so a slow box over days is a supplement,
best pointed at a *different/harder* deck than piling R onto one already done — 1/√R diminishing).

## Design decisions

- **Blind bottoming vs clairvoyant lookahead:** the current lookahead-bottoming is clairvoyant (searches
  the fixed library); the exhaustive table's bottom is blind (expected over continuations). On the
  goldfish's clairvoyant metric lookahead has an info edge; if exhaustive bottoming is slightly worse the
  gap = the clairvoyance premium, and blind-optimal is arguably the more realistic policy (a real player
  can't see their library) → may adopt anyway. Keep decision has no such issue (both blind).
- Keep A/B is apples-to-apples (both keep policies blind). Static comparison in the analyzer uses bucket
  representatives so `D_static` is approximate (static is inconsistent across equivalents — its flaw);
  the in-game A/B is the definitive static check.

## Next steps (pre-R=100, per user)

1. **Merge tool** — `MTG_KEEP_MERGE` mode: read raw sidecars, reject fingerprint mismatch / overlapping
   seed_base, sum, recompute policy → profile. (in progress)
2. **Bottoming phase 2** — record the argmin (7-m)-subcomposition in `bottom_keep`, serialize; consult in
   `AIEngine::BottomCards` (heuristic fallback); A/B vs clairvoyant lookahead.
3. **A/B harness** — reuse `test/keepmodel_ab_widseeds.sh` against the exhaustive profile; keep vs static
   (+ bottoming vs lookahead). MTG_DUMP_WINS writes `gi=N wt=M` to **stderr**.
4. **Test + commit** everything, then **pull the other agent's code** (rebase; likely conflict = the
   `KeepHand` edit in AIEngine.cpp), then generate **R=100 overnight** on the user's schedule (after pull,
   so the shipped table matches shipped play logic).

## Loose ends / notes

- `decks/slivers_vial.keepmodel.exhaustive.profile.json` currently on disk is a **throwaway R=1** plumbing
  artifact — do NOT commit or A/B it; regenerate at R=100.
- R=20 experiment (`logs/equiv/slivers_exhaustive_R20.txt`) is diagnostics-only (pre-serialization
  binary, no realization-sampling, no R-sweep) — validation gap/disagreement/noise; does NOT pool with R=100.
- Thread-safety: the evaluator's worker uses `vector<SizeTable>` (not map) + const `bof.find()` for
  lock-free concurrent reads/distinct writes (an earlier `map::operator[]` race was benign but fixed).
