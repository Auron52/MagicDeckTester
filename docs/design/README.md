# `docs/design/` index

108 documents, ~1.6 MB. They are not homogeneous: some are live specifications, some are
write-ups of shipped features, and some are **measured dead ends that must not be re-attempted**.
Without this index a new agent cannot tell those apart without reading all of them — which is
exactly how a measured dead end gets re-implemented.

**Read [Dead ends](#dead-ends-measured-do-not-repeat) before proposing any search, mana, or
mulligan work.** Everything in that section was built and measured, and lost. Re-running one is
pure waste; the one-line "why it lost" is there so you can tell whether your idea is genuinely
different.

## Tags

| Tag | Meaning |
|---|---|
| `SPEC` | Live specification or plan of record. Describes what should be built; read before building it. |
| `ADOPTED` | Shipped and in the binary today. The doc is the rationale + the off-switch. |
| `DEAD-END` | Built and **measured**; it lost. Do not repeat without a materially different design. |
| `DEFERRED` | A real idea, deliberately parked. Not being worked on; nothing depends on it. |
| `OPEN` | A known live defect or shortfall, root-caused or reproduced, fix not yet shipped. |
| `HISTORICAL` | Record of a past investigation, audit, or resume state. No live action. |

A doc can carry a second tag when half of it shipped and half did not.

Authoritative *process* lives in `.claude/skills/` (rules, AI, analyze-deck, regression-testing,
heuristic-optimization, mulligan-profile, claude-play, coding-conventions), not here. This
directory is the *engineering record*.

---

## Start here

| Doc | Tag | What it is |
|---|---|---|
| [code-pruning-and-refactor-backlog.md](code-pruning-and-refactor-backlog.md) | `SPEC` | The live refactor/pruning backlog with a Status header naming the commit for each finished item. Includes a "Do NOT do these" list of load-bearing things not to clean up. |
| [rollout-executor-lockstep.md](rollout-executor-lockstep.md) | `SPEC` | **The single most important correctness doc.** The engine is implemented twice (executor + rollout); every historical divergence was a real bug. Explains the bug class, the detection method, and the open cases. |
| [exhaustive-keep-policy.md](exhaustive-keep-policy.md) | `ADOPTED` | The base design of the bucketed exhaustive keep/bottom mulligan profile that every deck's `.keepmodel.exhaustive.*` artifact implements. |
| [per-deck-folder-layout.md](per-deck-folder-layout.md) | `ADOPTED` | The `decks/<name>/` layout and the raw-artifact policy (commit the gzipped raw, never the uncompressed). *Header still says "deferred/not started" — stale; the move shipped.* |
| [batch-runner.md](batch-runner.md) | `ADOPTED` | Why every long run must pool into ONE `--batch` manifest instead of a loop of invocations (load-imbalance tail). |

---

## Dead ends (measured, do not repeat)

Each of these was **built and measured**. The one-liner is why it lost.

| Doc | What was tried | Why it lost |
|---|---|---|
| [enumeration-feasibility-via-executor.md](enumeration-feasibility-via-executor.md) | Leaf-apply feasibility gate — probe each plan with the real executor (`PlanFeasibleOnScratch`) inside `Solve::consider`. | Two independently fatal reasons: catastrophic perf (smoke 143 s → wedged >270 s at 10/21 cases, because the probe sat in the **rollout leaf**), and every deck that finished came back byte-identical — it found zero strands. Rule learned: **nothing slow may live in the rollout.** |
| [escalation-interior-reuse.md](escalation-interior-reuse.md) | Game-persistent "true leaf cache" (`MTG_LEAF_CACHE`) to reuse the value-leaf probe's interior traversal. | Unsound, and the reuse barely exists — ~87 % of Hinata's rollout leaves are non-repeats. The escalation is expensive because it explores *distinct* states, not re-rollable ones. Scaffold reverted; the byte-identical `MTG_TT_STATS` instrument was kept. The interior-node counter then killed the secondary lever too. |
| [escalation-and-rollout-cost.md](escalation-and-rollout-cost.md) (§3) | Warm-start the escalation's branch-and-bound with the value-leaf's committed win turn as an initial cutoff. | Subsumed by `MoveOrderPlans`: ~9 % ceiling on TH and unsound at that (the optimistic estimate over-pruned 2 wins). The incumbent tightens within the first plan or two, so an initial cutoff only prunes cheap late-winning branches. Marked "do not re-run". |
| [escalation-beam-verify.md](escalation-beam-verify.md) | Beam-aware climb estimate (`MTG_ESC_CLIMB_GROWTH=1`) — estimate the next climb depth from *beamed* growth. | Decisive 3-way A/B over 6 seeds: mode0 +0.0133 vs mode1 +0.0125 dLP — a 0.0008 "recovery" that is pure noise. Stays a dormant default-0 knob. |
| [post-breakpoint-search.md](post-breakpoint-search.md) (§nested) | Nested breakpoints, `MTG_BP_DEPTH=2`. | Budget dilution: widening the candidate set at a fixed per-decision budget loses on held-out seeds (Hinata, the branchiest deck, worst). Re-measured on the *fixed* engine and still rejected — an honest loss, not a defect. Legitimate only at unbounded budget. **Would have been adopted on smoke+regression alone** — the held-out split is what caught it. |
| [post-breakpoint-search.md](post-breakpoint-search.md) (§step 2) | Breakpoint waves placed at the **root**, one wave phase per committed decision. | Reaches nothing: `FullSearchLineHybrid` runs ~twice per *game*, not per turn, and at the decisive case's roots no candidate opened a breakpoint at all (`entered=2 no-slots=2`). The adopted placement is deferred waves inside the search. |
| [learned-d0-policy.md](learned-d0-policy.md) (§d0 ranker) | A pure d0 (greedy-leaf) ranker as the route to search quality. | Myopic — it cannot see a play's multi-turn cost. Repeatedly negative. **The value model is the path, not the d0 ranker.** |
| [learned-d0-policy.md](learned-d0-policy.md) (§features) | Race-clock features (`our/opp_clock_to_lethal`); a combo-aware d0 feature for antilife; value-model-as-feature v1. | Race-clock: inert, the label already captures the win turn — reverted to keep the feature set clean. Combo feature: made antilife d0 **worse** (0.7 % A/B), reverted. Value-model-as-feature v1: the crude approximation is non-discriminative; the next attempt needs the real model, not the shortcut. |
| [dragonstorm-d0-divergence-digest.md](dragonstorm-d0-divergence-digest.md) | Encoding a *correct* "deploy an early Dragon when the storm is ≥3 turns out" heuristic into the greedy rollout. | d5 came back **identical** and blind d0 slightly worse. The search already rollout-scores every land/dragon/hold plan, so encoding the call in greedy changes nothing it does; and the blind rollout cannot capitalize on the early board, so "deploy more" just re-introduces ritual waste. **Sharpened lesson: the rollout policy benefits from anti-WASTE rules, not from good-judgment rules the search already makes.** |
| [sequential-plan-evaluation.md](sequential-plan-evaluation.md) (§Medallion split) | `MTG_MEDALLION_SPLIT` — generate staggered "M1 → discounted ritual → M2" lines the block insertion misses. | ~+0.005t **worse** on Dragonstorm d5. The subset enumerator already offers 1-/2-Medallion lines, so the extra orderings only spend search budget for no realized win. Reverted. |
| [dragonstorm-cast-order-search.md](dragonstorm-cast-order-search.md) (§not adopted) | Widening the targeted cast-order generator to more orderings. | Same shape: extra orderings buy fewer nodes in 20 ms for no realized win. The *targeted* generator was adopted; the widening was not. |
| [land-signature-completeness.md](land-signature-completeness.md) | Demand-driven colour bucketing of the land enumeration (capping colours by demand). | Measured dead on all 8 decks. The audit it required is what surfaced the real defect (an incomplete dedupe key), which *did* ship. Separately: **splitting every land signature** rather than promoting the dominant land cost +61 % on Slivers for zero quality — split only the genuinely incomparable. |
| [hinata-spasm-gate-rootcause.md](hinata-spasm-gate-rootcause.md) | The Hinata Reality-Spasm gate (`MTG_HINATA_SPASM_GATE`), and a cleaner whole-turn redesign. | Committed modes are both defective (`strict` has no Win escape and fires on subsets; `soft`'s in-hand check is inverted). The redesign is real but measured ~0.5 % — within noise. User: not worth rebuilding. Preserved on `recovered/hinata-spasm-gate-redesign`. |
| [hinata-gate-sweep-audit.md](hinata-gate-sweep-audit.md) | Granular one-gate-at-a-time `MTG_UNPRUNE` sweep of every Hinata narrowing gate. | Nothing authored, nothing adopted, all scaffolding reverted. Useful as the worked example of the granular-gate protocol; not as a source of pending wins. |
| [slivers-restricted-mana-tap-order-bug.md](slivers-restricted-mana-tap-order-bug.md) (§attempt 1) | `ManaSourceRank`: count only non-`Colorless` colours toward `ncol`. | Promising but a **tradeoff**, not a win — reverted. If retried it must be gated in the archetype provider, never the root. |
| [shuffle-variance-instrument.md](shuffle-variance-instrument.md) (§ponder) | A Ponder-keep exception for a top slightly worse than a fresh dig. | Sub-noise on win %. Not adopted; `MTG_PONDER_FORCE` scaffolding reverted. |
| [burn-blaze-landfall-audit.md](burn-blaze-landfall-audit.md) | Holding Searing Blaze for landfall (cast-for-3 instead of cast-for-1). | Net-neutral on win turn — the +2 face never crosses a lethal threshold in the goldfish. Rejected under the "measured-safe-but-marginal is a reject" rule. **Nuance kept for the user:** it is strictly-better-or-neutral and plays *more correctly*; shipping it would be a veto call, not a measured win. |
| [viewer-bounce-land-xspell-ordering.md](viewer-bounce-land-xspell-ordering.md) (§item 3) | The hypothesis that the enumerator over-credits a bounce-land + X-spell line. | **Disproven** by replay — the enumerator is right. The residual is a viewer convenience with a working workaround, GT-neutral. |
| [rollout-executor-lockstep.md](rollout-executor-lockstep.md) (§4) | Making the rollout label its scry/surveil **source** the way the executor does. | Breaks reference intent-replay, which anchors on `(kind, index, source)`: `treasure_hunt/claude_s5_gi4` replayed from a turn-8 win to a **loss**, 138 ok → 1 play-drift. Reverted. **Do not retry blind** — any change to a decision's `kind`/`source` label is a reference-replay change. |
| [dragonstorm-mulligan-reference-gap.md](dragonstorm-mulligan-reference-gap.md) | `curve_check: none` in the Dragonstorm profile. | Actually *measured net-positive* (d3 −0.022, d5 −0.030) — but **deferred by the user** ("just report, don't change yet"). Listed here so it is not re-measured; it is a decision waiting on the user, not a loss. |
| [accelerant-ordering-and-self-funding.md](accelerant-ordering-and-self-funding.md) (§dead ends) | Ordering the sequenced ritual walk by `CastOrderRank`; a fast path for "the board pays for every accelerant outright". | `CastOrderRank` discards free `SacForMana` float — order by **cost**, not rank. The fast path measured ~0 %: on the go-off turns that dominate the hot path the board *cannot* pay outright, which is the whole point. Also corrects the false claim that Hinata shares Dragonstorm's ordering defect — it does not. |
| [clairvoyant-reference-shortfalls.md](clairvoyant-reference-shortfalls.md) (§dead ends) | For the three references the search finishes later than the human: unbounded budget; depths 6–9; all 15 `MTG_UNPRUNE` gates individually; global `MTG_UNPRUNED`; `MTG_VALUE_MODEL=0`, `MTG_ESC_BEAM=0`, `MTG_NO_GROUP_CAP`, `MTG_NO_MOVE_ORDER`, `MTG_COLOR_BLIND_TIEBREAK`. | All flat. Global `MTG_UNPRUNED` makes Dragonstorm `s1_gi0` *worse*; `MTG_LEGACY_SEARCH=1` much worse on TH. **Neither shortfall is a resource problem** — do not throw budget or depth at them. |

Also recorded in the backlog's "Do NOT do these": do not remove or narrow
`MTG_UNPRUNED`/`MTG_UNPRUNE`/`UnprunedGate` (the user's core architectural bar), do not fold the
`DecisionProvider` archetype split back into the solver, and do not add a precompiled header as
the answer to compile time.

---

## Search, enumeration, and mana

| Doc | Tag | One line |
|---|---|---|
| [plan-odometer-factorization.md](plan-odometer-factorization.md) | `ADOPTED` | Factors the mana side out of the main-phase plan odometer; three byte-identical increments, −38 % instructions on Dragonstorm. Includes the sound-vs-unsound `ManaPruneBound` story. |
| [exact-mana-enumeration.md](exact-mana-enumeration.md) | `DEFERRED` | Replace the flat `wild` pool with an achievable-pool frontier. Measured and scoped, not built. |
| [post-breakpoint-search.md](post-breakpoint-search.md) | `ADOPTED` | Mid-turn breakpoints were search-free zones; `Plan::bp_choice` gives them a searched continuation. Also the deferred-waves budget lever. Two dead ends inside (above). |
| [sequential-plan-evaluation.md](sequential-plan-evaluation.md) | `ADOPTED` | Within-turn ordering in the main-phase enumerator: what the odometer covers, what the ordering oracle (`MTG_SEARCH_ORDER`) covers, and the value-sign-aware ETB ordering rule. |
| [accelerant-ordering-and-self-funding.md](accelerant-ordering-and-self-funding.md) | `ADOPTED` | Accelerant cast-order tiers hoisted to the root; sequenced ritual credit at the leaf so a ritual cannot fund its own cost; the standing `MTG_AFFORD_AUDIT` stranded-accelerant detector. |
| [land-signature-completeness.md](land-signature-completeness.md) | `ADOPTED` | An incomplete land dedupe key made some land abilities unreachable. Split the incomparable, promote the dominant. Hatch `MTG_LEGACY_LAND_SIG=1`. |
| [dragonstorm-search-pruning.md](dragonstorm-search-pruning.md) | `SPEC` | Byte-identical over-splice skip + an acceleration-ordering heuristic. Step 1 confirmed; the rest not implemented. |
| [dragonstorm-payoff-prune.md](dragonstorm-payoff-prune.md) | `ADOPTED` | Hold rituals until a payoff is reachable. Default-on for Dragonstorm only, opt-out `MTG_UNPRUNE=payoffprune`. |
| [dragonstorm-cast-order-search.md](dragonstorm-cast-order-search.md) | `ADOPTED` | A targeted cast-ordering search replacing a fixed `CastOrderRank` for a combo deck. |
| [dragonstorm-goff-lethal-recognition.md](dragonstorm-goff-lethal-recognition.md) | `ADOPTED` | The search failed to recognize its own go-off as lethal. Root-caused and fixed, default on. |
| [dragonstorm-float-colour-collapse.md](dragonstorm-float-colour-collapse.md) | `ADOPTED` | Two enumeration prunes that took the worst Dragonstorm rollout from ~31.7 min to ~11 s (~90–180×). |
| [escalation-and-rollout-cost.md](escalation-and-rollout-cost.md) | `OPEN` | Levers, measurements, and telemetry for escalation/rollout cost. Partial results shipped gated; the big lever is still open. Contains a dead end (above). |
| [escalation-beam-verify.md](escalation-beam-verify.md) | `OPEN` | Value-guided beam for the escalation — reuse the leaf-value pass, do only the incremental rollout. |
| [escalation-interior-reuse.md](escalation-interior-reuse.md) | `DEAD-END` | See dead ends. Kept for the `MTG_TT_STATS` / interior-node instrumentation it left behind. |
| [anytime-search-budget-prediction.md](anytime-search-budget-prediction.md) | `DEFERRED` | Tighten mid-line budget prediction so an overrunning line is abandoned sooner. |
| [fallback-budget-renewal-handoff.md](fallback-budget-renewal-handoff.md) | `DEFERRED` | Escalation fallback tuning: budget renewal + single-depth fallback. Set up, never measured. |
| [mana-source-reservation.md](mana-source-reservation.md) | `DEFERRED` | "Leaving sources up." Largely superseded by whole-turn batch payment. |
| [nc-land-selection-color.md](nc-land-selection-color.md) | `DEFERRED` | The non-clairvoyant search drops lands on curve but still picks the *wrong* land when colour matters. |
| [searched-scry-disposition.md](searched-scry-disposition.md) | `SPEC` | User direction: scry/surveil disposition must be a **search branch**, not a policy hook. Two TH investigations dead-ended on "no fixed rule can express this". |
| [searched-cleanup-discard.md](searched-cleanup-discard.md) | `DEFERRED` | Searched cleanup discard, built behind `MTG_SEARCHED_DISCARD` — **ships OFF**. |
| [model-performance-levers.md](model-performance-levers.md) | `DEFERRED` | Menu of performance techniques (same play quality, less compute) for the NC policy / value leaf. |
| [enumeration-feasibility-via-executor.md](enumeration-feasibility-via-executor.md) | `DEAD-END` | See dead ends. The *reframe* at the bottom — the much smaller real change — is the live part. |

## Rollout/executor fidelity and rules correctness

| Doc | Tag | One line |
|---|---|---|
| [rollout-executor-lockstep.md](rollout-executor-lockstep.md) | `SPEC` | The bug class (the rollout commits a line the executor does not reproduce), the detection method, and the land-drop divergence casebook. |
| [failed-cast-atomic-rollback.md](failed-cast-atomic-rollback.md) | `ADOPTED` | A failed cast must roll back atomically instead of leaving partial state. |
| [irencrag-max-casts-execution-enforcement.md](irencrag-max-casts-execution-enforcement.md) | `ADOPTED` | Rules violation fixed: `max_casts_after = 1` is now enforced at execution, not just enumeration. |
| [unclaimed-territory-restricted-mana.md](unclaimed-territory-restricted-mana.md) | `ADOPTED` | Faithful restricted mana for Unclaimed Territory / Cavern of Souls / Secluded Courtyard (`colored_creature_only`). |
| [spectacle-and-invigorate-combo-enumeration.md](spectacle-and-invigorate-combo-enumeration.md) | `ADOPTED` | Enumerating the Spectacle and Invigorate-combo lines. |
| [commit-the-line-replay-fidelity-tail.md](commit-the-line-replay-fidelity-tail.md) | `ADOPTED` | The commit-the-line replay-fidelity tail (fd-diverge optimism); burn `d3 s6006` 8 → 0. |
| [dragonstorm-plan-execution-fidelity-bug.md](dragonstorm-plan-execution-fidelity-bug.md) | `ADOPTED` | Storage-under-burst execution fidelity bug, found by a 5-day Claude-play sweep. |
| [stable-shuffle.md](stable-shuffle.md) | `ADOPTED` | `MTG_STABLE_SHUFFLE` common-random-numbers mid-game reshuffle — the **default**; `MTG_LEGACY_SHUFFLE` reverts. |
| [card-numbering-shuffle-consistency.md](card-numbering-shuffle-consistency.md) | `ADOPTED` | `Library::ShuffleByKey` CRN consistency across batch/viewer/references/audit; hatch `MTG_LEGACY_UNNUMBERED`. |
| [card-numbering-affects-play.md](card-numbering-affects-play.md) | `HISTORICAL` | The original symptom record: the same deck+seed played differently depending on whether numbering was on. |
| [shuffle-variance-instrument.md](shuffle-variance-instrument.md) | `ADOPTED` | An independent shuffle salt for evaluating stochastic decisions without breaking clairvoyance. |
| [d0-mana-realization-strand.md](d0-mana-realization-strand.md) | `OPEN` | Greedy wastes accelerants on go-offs it cannot pay. Root-caused with a proof-of-concept; the accurate fix is open. |
| [checkline-medallion-declared-order.md](checkline-medallion-declared-order.md) | `OPEN` | A payable Medallion+ritual Apex line is rejected as illegal; classification fixed, but the enumerator's same-turn-Medallion blind spot remains. Has an open question for the user. |
| [same-turn-cost-reduction-fidelity.md](same-turn-cost-reduction-fidelity.md) | `OPEN` | Same-turn cost-reduction fidelity gap (Hinata / future reducers). Low frequency, deferred. |
| [slivers-restricted-mana-tap-order-bug.md](slivers-restricted-mana-tap-order-bug.md) | `OPEN` | Restricted-mana tap-order bug, root-caused; fix attempt 1 was a tradeoff (see dead ends). |
| [gi22-durdle-and-irencrag-apex.md](gi22-durdle-and-irencrag-apex.md) | `OPEN` | 9 persistent Dragonstorm durdles. **Not** a budget/value-leaf problem — the user corrected that mis-filing; they are fixable. |
| [bottomcards-undercount-beyond-maxmull.md](bottomcards-undercount-beyond-maxmull.md) | `OPEN` | `BottomCards` under-bottoms when `mulligan_count > table max_mull`. Confirmed by inspection; fix touches core play + GT. |
| [crackle-hinata-declared-targets.md](crackle-hinata-declared-targets.md) | `SPEC` | Crackle with Power: declared targets, derived Hinata discount, faithful damage. In progress. |
| [crackle-reality-spasm-overgeneration.md](crackle-reality-spasm-overgeneration.md) | `DEFERRED` | Reality-Spasm-funded Crackle plans over-generate at enumeration. |
| [antilife-fetch-heuristic-overprune.md](antilife-fetch-heuristic-overprune.md) | `HISTORICAL` | Fetch heuristic mis-valued colour multiplicity; fixed 2026-07-09 via a ranking fix. |
| [escalation-refactor-drift.md](escalation-refactor-drift.md) | `HISTORICAL` | Play drifted vs GT after an escalation refactor. Root cause was a single **non-gated default flip** (`s_fresh_frac`), not a subtle refactor bug. |

## Mulligan profiles and keep models

| Doc | Tag | One line |
|---|---|---|
| [exhaustive-keep-policy.md](exhaustive-keep-policy.md) | `ADOPTED` | The base design: exhaustive bucketed keep/bottom policy. |
| [adaptive-batched-keepgen.md](adaptive-batched-keepgen.md) | `ADOPTED` | The **one** way to run keep-gen: a single continuous barrier-free pool, incremental, restartable, adaptive. Written because agents repeatedly mis-invoked the gen. |
| [mulligan-reconstruct-lower-r.md](mulligan-reconstruct-lower-r.md) | `ADOPTED` | Reconstruct cheaper profiles from one full-R raw with no re-rollout. |
| [change-detection-carry.md](change-detection-carry.md) | `ADOPTED` | Whole-pool warm start (`MTG_KEEP_PRIOR_RAW`). Read the "Phase 1 finding" box. |
| [execution-trace-carry.md](execution-trace-carry.md) | `ADOPTED` | Reuse the prior for cells a change did not touch — the lever that cuts near-threshold + bottoming re-runs. |
| [hinata-mulligan-profile.md](hinata-mulligan-profile.md) | `ADOPTED` | Hinata R=22 profile, generated on frozen `7f3aaa8`, A/B-validated before adoption. |
| [auras-mulligan-profile.md](auras-mulligan-profile.md) | `ADOPTED` | Auras R=40 profile, generated on frozen `a6b4160`, A/B-validated. |
| [th-keep-model-overmulligans-th-hands.md](th-keep-model-overmulligans-th-hands.md) | `ADOPTED` | TH over-mulliganed Reliquary-Tower hands; fixed by a `KeepFloor` provider hook (`MTG_TH_KEEPFLOOR`, default on). |
| [keep-model-selection-by-runner.md](keep-model-selection-by-runner.md) | `SPEC` | **Selection bug of record:** keep-models were selected by held-out per-hand turn-regret, a proxy that does not transfer. Select by the real runner. |
| [better-mulligan-model.md](better-mulligan-model.md) | `SPEC` | A keep decision that beats static rather than tying it. Phase 1 shipped; phases 2–5 open. |
| [mulligan-profile-scaling-and-pruning.md](mulligan-profile-scaling-and-pruning.md) | `SPEC` | Adaptive sampling, pruning, and force-merge. Mostly not built. |
| [structural-bucket-merge.md](structural-bucket-merge.md) | `DEFERRED` | Byte-identical-definition bucket merge. **Parked with an explicit trigger** — implement only when it fires. |
| [exhaustive-keep-progress-reporting.md](exhaustive-keep-progress-reporting.md) | `DEFERRED` | Live progress reporting for keep/bottom generation. |
| [exhaustive-profile-workflow-deferred.md](exhaustive-profile-workflow-deferred.md) | `DEFERRED` | Two workflow improvements queued behind the Hinata pipeline. |
| [nc-mulligan-table-generation.md](nc-mulligan-table-generation.md) | `DEFERRED` | Non-clairvoyant mulligan-table generation. |
| [play-digest-and-pooling-gate.md](play-digest-and-pooling-gate.md) | `ADOPTED`/`DEFERRED` | The per-deck play digest shipped as the regression tripwire (it is the `/<digest>` half of every GT fingerprint); the mulligan-pooling gate half is not built. |
| [rollout-config-digest-depth-blindness.md](rollout-config-digest-depth-blindness.md) | `ADOPTED` | **Measured defect in a shipped gate, fixed additively:** `play_digest` hashes only the 64-game battery, never `depth`/`budget_ms`/`max_turns` — identical hash at d5 and d6 while 2 % of keep rollouts differ. Now stamped as separate meta fields and compared when both sides carry them, so **no existing artifact was invalidated**. |
| [dragonstorm-mulligan-tractability.md](dragonstorm-mulligan-tractability.md) | `HISTORICAL` | **Answer: NO.** A definitive R=40 Dragonstorm profile is ~80–90 h, not one overnight. |
| [dragonstorm-mulligan-reference-gap.md](dragonstorm-mulligan-reference-gap.md) | `DEFERRED` | The Dragonstorm *play* is perfect; the mulligan heuristic is the gap. Deferred by the user; see dead ends for the measured quick fix. |
| [hinata-profile-generation.md](hinata-profile-generation.md) | `HISTORICAL` | The multi-day chunked cross-machine generation recipe for Hinata. |
| [hinata-gate-generation.md](hinata-gate-generation.md) | `HISTORICAL` | Hinata generation with the escalation confidence gate (T=0.70). |
| [hinata-gen-perf-r1.md](hinata-gen-perf-r1.md) | `DEFERRED` | R=1 diagnosis of Hinata gen cost (value.json + combo-line cost). Captured so the secondary machine does not re-burn it. |
| [burn-vs-slivers-gen-perf.md](burn-vs-slivers-gen-perf.md) | `OPEN` | Burn vs Slivers keep-gen ~6× perf gap, queued investigation. |

## Value leaf and learned models

| Doc | Tag | One line |
|---|---|---|
| [value-leaf-fallback-table.md](value-leaf-fallback-table.md) | `ADOPTED` | Value-leaf × heuristic-depth table driving the runtime fallback crossover. Core shipped for the 5 non-Hinata decks. |
| [auras-value-leaf.md](auras-value-leaf.md) | `ADOPTED` | The Auras value leaf — generated late, on a frozen commit, after `ref_bench` showed play at human parity. |
| [dragonstorm-d5-default-and-value-leaf.md](dragonstorm-d5-default-and-value-leaf.md) | `ADOPTED`/`DEFERRED` | Model-less d5 default shipped; the Dragonstorm value leaf itself is deferred. |
| [learned-d0-policy.md](learned-d0-policy.md) | `SPEC` | Non-clairvoyant policy distillation. The long ledger of what was tried; several dead ends inside (above). |
| [value-leaf-budget-constrained-fallback.md](value-leaf-budget-constrained-fallback.md) | `DEFERRED` | A more robust value-leaf fallback under budget constraint. |
| [antilife-valueleaf-deep-cells-overnight.md](antilife-valueleaf-deep-cells-overnight.md) | `OPEN` | ⚠️ The committed antilife deep-cell table **does not reproduce** — the d5 consistency anchor fails. Needs user attention. |

## Per-deck ledgers and findings

| Doc | Tag | One line |
|---|---|---|
| [analysis-Auras.md](analysis-Auras.md) | `HISTORICAL` | Auras (GW Bogles hexproof-aura aggro) analysis ledger. |
| [analysis-Dragonstorm.md](analysis-Dragonstorm.md) | `HISTORICAL` | Dragonstorm analysis ledger — coverage, fetch, classify. |
| [analysis-goblins.md](analysis-goblins.md) | `HISTORICAL` | Goblins ledger; engine functionally complete with a stage-4 baseline profile. |
| [dragonstorm-impl-hooks.md](dragonstorm-impl-hooks.md) | `HISTORICAL` | Dragonstorm implementation hook map + mid-build resume state (anchored by function/pattern, not line number). |
| [dragonstorm-dragon-picker.md](dragonstorm-dragon-picker.md) | `DEFERRED` | A Dragon-put override dialog. The *selection rule* is implemented and correct; only the dialog is missing. |
| [treasure-hunt-open-findings.md](treasure-hunt-open-findings.md) | `OPEN` | TH deferred items, all reproducible against `685be48`. |
| [th-lands-edge-lethal-shortfall.md](th-lands-edge-lethal-shortfall.md) | `ADOPTED` | Land's Edge lethal held a turn too late; fixed by holding the land drop. |
| [th-reliquary-defer-gi627.md](th-reliquary-defer-gi627.md) | `ADOPTED` | A genuine TH search-quality misplay (gi627), verified end-to-end by human play. Not a clairvoyance artifact. |
| [antilifegain-swords-targeting.md](antilifegain-swords-targeting.md) | `ADOPTED`/`DEFERRED` | Swords-to-Plowshares as a life-loss tool: human-play targeting shipped; the larger combo deferred. |
| [pump-enemy-swords-line.md](pump-enemy-swords-line.md) | `DEFERRED` | Pump an *enemy* creature then Swords it (Tainted Remedy shell). Parked. |
| [auras-same-turn-aura-enum-perf.md](auras-same-turn-aura-enum-perf.md) | `OPEN` | Same-turn creature→aura enumeration is slow on aura-dense states. A perf pathology, not a correctness bug. |
| [fea3a2c-regression-slowdowns.md](fea3a2c-regression-slowdowns.md) | `HISTORICAL` | Classification of the `fea3a2c` cast-order slowdowns — net strongly positive, 30 searched cases slower. |
| [burn-blaze-landfall-audit.md](burn-blaze-landfall-audit.md) | `DEAD-END` | See dead ends. |

## Harness, tooling, and the play viewer

| Doc | Tag | One line |
|---|---|---|
| [batch-runner.md](batch-runner.md) | `ADOPTED` | `mtg --batch`: one pooled work queue, one load-imbalance tail. |
| [auto-audit-integration.md](auto-audit-integration.md) | `ADOPTED` | The regression harness auto-runs the per-game audit and gates `--accept` on it. |
| [scenario-harness.md](scenario-harness.md) | `ADOPTED` | `mtg --scenario`: run one hand-built board through the real turn engine and assert the outcome. |
| [reference-intent-replay.md](reference-intent-replay.md) | `ADOPTED` | The protocol check replays references by **intent** (plan content + engine defaults) instead of raw positional indices — 105 ok → near-total on the same binary. |
| [decision-indexed-choice-protocol.md](decision-indexed-choice-protocol.md) | `SPEC` | The decision-indexed choice protocol, chosen by the user over the alternative. Staged implementation. |
| [claude-play-decision-parity.md](claude-play-decision-parity.md) | `ADOPTED` | Make a human-driven game match the game the search plays, and show the human what the AI would have chosen. |
| [claude-play-mulligan-reproducibility.md](claude-play-mulligan-reproducibility.md) | `ADOPTED`/`DEFERRED` | Mulligan record+replay shipped; player-controlled mulligan still deferred. |
| [claude-play-mulligan-latency.md](claude-play-mulligan-latency.md) | `ADOPTED` | claude-play startup latency root-caused to exhaustive-keep sidecar parsing, and fixed. |
| [viewer-explicit-target-set.md](viewer-explicit-target-set.md) | `ADOPTED` | Explicit wide target sets + Soulfire own-target selection. |
| [viewer-mana-color-fidelity.md](viewer-mana-color-fidelity.md) | `ADOPTED`/`DEFERRED` | Multi-colour sources must not pay off-colour pips: viewer verdict fixed; the full mana-model change for search/batch is deferred. |
| [viewer-magma-opus-modeling.md](viewer-magma-opus-modeling.md) | `ADOPTED` | Magma Opus: the functional blocker fixed GT-neutrally; the full-faithful discount is partial. |
| [viewer-fixes-2026-07-27.md](viewer-fixes-2026-07-27.md) | `SPEC` | Plan of record for a 12-item play-viewer batch, status tracked inline. |
| [viewer-bounce-land-xspell-ordering.md](viewer-bounce-land-xspell-ordering.md) | `OPEN` | A viewer convenience for hand-building an X-spell + Karoo line. GT-neutral, workaround exists. |
| [viewer-options-menu-toggles.md](viewer-options-menu-toggles.md) | `DEFERRED` | Per-decision surfacing toggles in the viewer options menu. |
| [deck-onboarding-hardening.md](deck-onboarding-hardening.md) | `SPEC` | Turn the deck-onboarding pipeline into a set of enforced gates. In progress. |
| [dragonstorm-d0-divergence-digest.md](dragonstorm-d0-divergence-digest.md) | `SPEC` | The repeatable automated loop for finding d0/rollout-quality fixes. Method proven; one worked example shipped. |
| [per-deck-folder-layout.md](per-deck-folder-layout.md) | `ADOPTED` | The `decks/<name>/` layout and raw-artifact policy. *Header stale.* |

## Audits and historical records

| Doc | Tag | One line |
|---|---|---|
| [reference-shortfall-audit-2026-07-28.md](reference-shortfall-audit-2026-07-28.md) | `HISTORICAL` | Full per-game sweep of all 140 references: human vs clairvoyant search vs non-clairvoyant search on the identical opening hand. |
| [clairvoyant-reference-shortfalls.md](clairvoyant-reference-shortfalls.md) | `OPEN` | The three references where the shipped search finishes later than the human — two different search failures, neither a resource problem. |
| [overnight-audit-2026-07-11.md](overnight-audit-2026-07-11.md) | `HISTORICAL` | The doubly-stale overnight GT rebaseline (proven stale by 35 **d0** win→loss, which a search-only change cannot cause). |
| [antilife-reference-shuffle-alignment.md](antilife-reference-shuffle-alignment.md) | `HISTORICAL` | Of 30 Anti-Lifegain references, 6 kept / 24 deleted after a shuffle change — the rule and the method used to decide. |
| [hinata-gate-sweep-audit.md](hinata-gate-sweep-audit.md) | `DEAD-END` | See dead ends. Read for the granular-gate protocol, not for pending wins. |
| [escalation-refactor-drift.md](escalation-refactor-drift.md) | `HISTORICAL` | See above — the "subtle refactor bug" that was really one ungated default flip. |

---

## Conventions for adding a doc here

- **Deferred work goes in git, not private agent memory** (see [CLAUDE.md](../../CLAUDE.md)).
  A deferred item belongs to everyone; per-agent memory is shared with no one.
- Keep each doc **self-contained** — no references to any agent's private notes.
- Put a **Status line directly under the title**: one of the tags above, the date, and the
  commit if something shipped. This index is generated by reading those lines; a doc without
  one has to be classified by hand.
- When something is **measured and loses, say so in the doc and add it to the dead-ends table
  above with its one-line reason.** That table is the whole point of this file.
- When a doc's status changes, update **both** the doc and its row here in the same commit.
