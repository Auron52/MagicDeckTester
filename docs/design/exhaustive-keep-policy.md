# Exhaustive bucketed keep/bottom policy

Status: **committed** (8b239cf core; c3dad35 bottoming flag + scorer), as of 2026-07-03. Keep
integration, aggregation, merge tool, bottoming phase 2, A/B harness, the `bottoming_enabled` profile
flag, and the `MTG_SCORE_COMPS` attribution scorer are all built + committed. **In-game A/B done on
real R=20 and R=100 profiles** (results below): keep adopts; **bottoming REVERSED to ON** — blind
exhaustive bottoming is the correct policy when blind to the shuffle, confirmed by the confounded
in-game A/B (`MTG_CONFOUND_BOTTOM`, which reshuffles the library *after* the bottoming decision so the
clairvoyant lookahead's peek is worthless): once the peek is removed, blind ≥ lookahead. Slivers R=100
ships bottoming ON; the generation default is now ON (was off), validated per profile via the
confounded A/B during adoption. Decks with no exhaustive table fall back to lookahead bottoming.
Operational guide: [`.claude/skills/mulligan-profile.md`](../../.claude/skills/mulligan-profile.md). This is the successor to the learned-keep-model effort
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

## In-game A/B + bottoming attribution (2026-07-03, real R=20 profile)

Definitive in-game sweep on the real 10-bucket R=20 Slivers profile (Slivers, 16–24 seeds × depths
0/3/5 × 1–2k games), via `test/keepmodel_exhaustive_ab.sh`:

- **Keep (vs static): exhaustive wins −0.028t win-turn, 16/16 seeds every depth.** 100% win rate both
  sides (pure speed metric). Clears the *trained* static baseline unanimously — the thing the learned
  keep-models never did. **Keep adopts** (presence-gated default-on).
- **Bottom (vs lookahead): a clean split.** d0 (vs the heuristic bottomer, since lookahead is off at
  depth 0): exhaustive **wins** +0.024t, 24/24. d3/d5 (vs clairvoyant lookahead): exhaustive **loses**
  0.045t, 0/24.

**Attribution of the d3 loss (does it lose only to clairvoyance?).** Both runs share the same starting
library per game index and the same keep decision, diverging only at bottoming. For the 50 losing d3
games we logged both kept subhands and **re-scored each at R=400** (`MTG_SCORE_COMPS`) — a *non-circular*
blind-EV comparison (average over 400 library orders), since the R=20 V that drove the bottoming is
itself noisy. Result: **28 R-noise / 18 clairvoyance / 4 tie.** I.e. in a *majority* of losses,
exhaustive kept a hand that is genuinely blind-*worse* (mean −0.11t) than lookahead's — the R=20 argmin
mis-ranked near-tie subhands. (Correction to an earlier inference: the d0 blind-vs-blind win does *not*
prove pure clairvoyance; it only shows the noisy bottoming beats the crude heuristic. Only the per-game
re-score reveals the noise.)

> **SUPERSEDED — bottoming is now ALWAYS ON and there is no off switch.** The bullets below record
> the ORIGINAL 2026-07 policy and are kept for provenance only. Generation bakes
> `bottoming_enabled=true` unconditionally (`analyzer/main.cpp`), the `MTG_KEEP_BOTTOMING` gen-time
> switch was **removed** (commit `ea2d530`), `mullgen.sh`'s artifact check *fails* the run if the
> flag is not true, and every keep table in the repo reads true. The live position is
> `.claude/skills/mulligan-profile.md` ("Bottoming: always on"): a bad confounded bottoming A/B
> means **raise R or fix the heuristic**, never ship bottoming off. Do not cite the bullets below as
> current doctrine — they outlived the policy by months and caused a shipped adoption to be hedged
> as though enabling bottoming had been an agent's decision.

**Consequences as originally encoded (HISTORICAL — see the banner above):**
- Low-R bottoming is worse than the free lookahead bottoming → **ships off** (`bottoming_enabled`
  default false; low-R profiles are keep-only). Only a validated high-R run whose re-attribution shows
  ties/beats-lookahead (or loses *only* to clairvoyance) sets it true.
- Keep is robust at low R; bottoming specifically needs the high R only the slow-box grind buys.
- Deck-life tiering (static skipped): defaults → low-R exhaustive keep (bottoming off) → high-R
  exhaustive. See the skill.

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
  (env: `MTG_KEEP_ROLLOUTS` R default 100; max_mull is FIXED at 6 = down to keep-1, no knob; `MTG_EQUIV_SEED` bucketing seed
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

## R=100 adoption + bottoming reversal (2026-07-04)

The definitive Slivers table was generated at commit `9c11ae5` as **R=100**, pooled from an R=20
(seed_base 20260704) + R=80 (seed_base 20260705) run via `MTG_KEEP_MERGE` (both stamped 9c11ae5,
matching bucket_fp/deck_fp, distinct seeds). R is **per-mode**, so R=100 = 100 games/mode/hand
(per-mode label stderr ~0.042t). Canonical files `decks/slivers_vial.keepmodel.exhaustive.{profile,raw}.json`.

- **Keep**: R=100 vs R=20 in-game A/B on fresh off-training seeds (770001–16, d0/d3): R=100
  marginally-but-consistently better (−0.0065t, 16/16 seeds), differing on only 4.4% of decisions
  (all near-ties). Adopt R=100. Its value over R=20 is precision, not a big win-turn jump — which
  also calibrates that lower R is defensible on expensive decks.
- **Bottoming — REVERSED to ON.** The R=100 blind-EV attribution (see
  `mulligan-profile-scaling-and-pruning.md`) showed blind bottoming makes *better* blind decisions
  than clairvoyant lookahead (mean Δ = −0.093t, 69% ours-better, per-all-games −0.0192t, no tail
  > 0.2t). The whole +0.0226t in-game loss is clairvoyance, which we discount. ⇒ `bottoming_enabled=true`
  (re-merged with `MTG_KEEP_BOTTOMING=1`). Gains: better blind decisions, ~3.5× faster bottoming,
  honest/non-clairvoyant. Cost: goldfish win-turn ~0.0226t slower → deliberate Slivers GT shift
  (accept via per-game audit).

## Next steps

1. **Re-baseline Slivers *overnight* GT** for the bottoming-on win-turn shift (per-game audit → `--accept`).
   Smoke + regression GT already reflect bottoming-on (re-baselined at 4136cc6, after the bottoming-on
   profile shipped at 3960ca4); only the overnight seeds (s4004–7007) remain.
   **Adoption gate for any new profile's bottoming = the confounded in-game A/B** (`MTG_CONFOUND_BOTTOM`,
   `test/keepmodel_burn_confound.sh` shape): the *non-confounded* bottoming A/B is misleading because it
   scores clairvoyant lookahead on the very library it peeked at (burn: blind "lost" +0.076t naive → **won
   −0.0098t confounded**; R=400 blind-EV probe shows the table's picks ARE the blind-argmin and lookahead's
   peek-driven deviations are blind-worse by 0.5–1.9t). Validate the next couple profiles this way before
   treating default-on as fully settled.
2. **Efficiency/pruning build** — the adaptive-sampling, force-merge, and rare-tail levers in
   `mulligan-profile-scaling-and-pruning.md` (needed for expensive decks: Anti-Lifegain K=23, Hinata K=20).
3. **Pooling gate** — the per-deck play-digest in `play-digest-and-pooling-gate.md` (demote commit-hash
   to advisory).
4. **Secondary machine** — Knights R=5 test in flight; higher-R or pooled Knights later for a
   deployable profile (its play/draw split is structural, just coarse at R=5).

## Loose ends / notes

- `decks/slivers_vial.keepmodel.exhaustive.{profile,raw}.json` is now the **committed R=100** table
  (bottoming ON). The `.r20`/`.r80` half-sidecars are kept on disk for provenance but are intermediate
  and not committed.
- `bottoming_enabled` (profile flag) governs runtime use of exhaustive bottoming. **Generation always
  sets it true and there is no off switch**; the loader defaults it ON for a present-but-keyless block
  so an ancient file cannot silently ship with its bottoming half dead. `MTG_EXHAUSTIVE_BOTTOM` is a
  3-state override (unset=follow flag, 0=off, 1=on) that exists **for `test/keepmodel_exhaustive_ab.sh`
  only** — `KM_MODE=keep` pins it to 0 on BOTH arms to isolate the keep effect. (The gen/merge switch
  `MTG_KEEP_BOTTOMING` was removed in `ea2d530` and is no longer read anywhere.)
- `MTG_SCORE_COMPS` (parallel) re-scores explicit subhand comps at high R — the non-circular
  clairvoyance-vs-R-noise test.
- Thread-safety: the evaluator's worker uses `vector<SizeTable>` (not map) + const `bof.find()` for
  lock-free concurrent reads/distinct writes (an earlier `map::operator[]` race was benign but fixed).
