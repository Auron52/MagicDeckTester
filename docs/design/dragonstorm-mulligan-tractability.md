# Dragonstorm mulligan gen — R=40 tractability finding (2026-07-24)

**Question:** is a definitive R=40 exhaustive mulligan profile for Dragonstorm tractable overnight
(≈16 h) now that the scheduler is a single continuous pool (floor→refine barrier removed 43f3f2b,
sub-table fusion 30fee8b) and the float-colour collapse shipped (3831145)?

**(Updated 2026-09-03: SUPERSEDED — an R=40 profile was generated and adopted 2026-07-27,
5e646e77, via the adaptive floor-1 cap-40 route, without levers (1) or (3); the recommendation
below not to burn cores on the exact gen no longer applies.)**

**Answer: NO.** Empirically **~80–90 h** at the current rollout rate. The scheduler is no longer the
bottleneck — the **degenerate rollout ATOM** is (the known ritual/Lotus enumeration blowup). This is
exactly the split the capture memory predicted: the one-pool rewrite collapses all tails *down to* the
atom, but the atom itself is a separate lever.

## The run (frozen commit f2a56b1, d3/b10, K=17 force-merge Karrthus+Kolaghan)

- Discovery: 113 s single-threaded → K=18 → force-merge → **17 buckets**.
- Cell counts: **size7=188,770**, size6=61,835, size5=17,933, size4=4,487 (fused sub-cap batches =
  (size6+5+4)×2 = **168,510**, each capped to R=40).
- Total work ≈ size-7 floor (0.76 M) + sub-cap (**6.74 M**) + size-7 refine (adaptive, up to ~14 M) ≈
  **≥11 M rollouts**.
- Measured pool rate: **~40 rollouts/s effective** (24 cores) — in ~11 min only **4.0 % of the size-7
  floor** completed and **0 % of the sub-cap** (blocking feed does size-7 floor first). → size-7 floor
  alone ≈ 4.6 h; sub-cap alone ≈ **47 h**; full R=40 ≈ **80–90 h**.
- A partial run does **not** emit a usable profile: the fused pool fixes refs (and thus refines / emits)
  only after the sub-cap completes, so a 16 h kill leaves only a journal, no coherent profile.

## Root cause — the degenerate atom (unchanged conclusion, refined data)

`MTG_KEEP_SLOW_MS=120000` captured **9 rollouts > 2 min in ~11 min** (log:
`logs/Dragonstorm_gen/slow_captures_f2a56b1.txt`), all `Lotus Bloom`/ritual/`Apex` combo hands:

| ms | hand |
|---|---|
| 640,894 | Apex of Power x2; Lotus Bloom x4; Desperate Ritual x1 |
| 454,264 | Lotus Bloom x4; Karrthus x2; Desperate Ritual x1 |
| 377,715 | Rite of Flame x1; Pyretic Ritual x2; Lotus Bloom x4 |
| 178,256 | Ruby Medallion x1; Pyretic Ritual x3; Lotus Bloom x1; Irencrag Feat x1; Desperate Ritual x1 |
| … | (5 more, 126 k–162 k ms) |

The float-colour collapse (3831145) fixed the **captured worst case** (Lotus x3;DespRit x3, 31 min → ~11 s)
by collapsing the *colour* fan-out. But two residual enumeration blowups remain and dominate now:
1. **Lotus Bloom x4 + rituals** — the ritual/Lotus *plan-count* powerset (how many/which sources to sac
   feeding how many spells), still large even mono-colour.
2. **Apex of Power** — the `impulse_exile` **play-from-library** enumeration (which subset of the exiled
   top-N to cast) blows up independently of colour; `Apex x2 + Lotus x4` is the worst (640 s).

The distribution is heavy-tailed (prior capture on 0511387: p50 5.3 s, p90 33 s, max 31 min over 3643
games) — with ~10 % of rollouts > 33 s, a handful of stuck workers cap the whole pool at ~40/s.

## Candidate levers (for user direction — NOT adopted autonomously)

1. **Colour-aware / sequenced feasibility (the memory's #1 lever).** Credit ritual float by its *actual*
   produced colour (or a sequenced "castable from a running total" bound) instead of the current
   **non-sequenced WILD** credit, so the affordability model rejects the durdle combos the WILD credit
   accepts. **This CHANGES rollout results → not byte-identical → new commit → the
   heuristic-optimization workflow (measure win%/avg-turn on train, validate held-out) + explicit user
   approval.** Hazard on record: a *tighter* (sequenced) bound already caused a Dragonstorm smoke
   regression once (3 fails), and the user corrected the earlier "colour-stuck" framing (red pays
   generic), so the exact bound must be chosen carefully. This is the highest-leverage fix but it is a
   **deferred modeling decision the user owns.**
2. **Tame the Apex `impulse_exile` play enumeration** — bound/collapse the subset-of-exiled-cards
   powerset (possibly byte-identical via a dominance argument, like the existing accel-prefix collapse).
   Separate from the ritual lever; targets the 640 s Apex hands specifically.
3. **Deterministic per-rollout virtual-budget cap** — abort a rollout's search once it exceeds a fixed
   *node/plan* count (deterministic → poolable), returning the current best win-turn. Bounds every
   rollout → tractable. Changes results (a modeling change; needs the same measure/approve workflow), but
   for durdle hands the capped result is often ~correct (they can't combo).
4. **Accept a lower-quality tool for the mulligan rollouts** — e.g., the value-leaf evaluator for the
   keep rollouts (the user's stated "give up a little quality to bring down cost" philosophy). Would NOT
   fix the enumeration blowup (that's plan *enumeration*, not leaf eval), so it does not unblock on its
   own.

## Recommendation

R=40 needs lever (1) or (3) first — both are **modeling changes the user must direct/approve** (not
byte-identical, and they change the frozen commit, so the gen must run *after* they land). Until then:
- **Do not** burn cores on the intractable exact gen (it produces no coherent partial profile).
- The scheduler work is done and correct; the remaining blocker is purely the rollout atom.
- Suggested path: user approves the colour-aware/sequenced feasibility bound (or a virtual-budget cap)
  via the heuristic-optimization workflow → re-freeze the commit → then R=40 (or a pooled multi-machine
  run) becomes tractable.

Artifacts: `logs/Dragonstorm_gen/slow_captures_f2a56b1.txt` (reproducer hands+seeds),
`logs/Dragonstorm_gen/partial_journal_f2a56b1.json` (partial floor, f2a56b1-only), launch script
`scripts/dragonstorm_keepgen.sh`. See also `memory/dragonstorm-degenerate-game-capture.md` and
`docs/design/dragonstorm-float-colour-collapse.md`.

## Update 2026-07-25 (commit 365f3d3) — the Apex captures were contention-distorted; no byte-identical Apex cut

A single-threaded replay of the captured worst Apex hands (via `MTG_KEEP_REPLAY`, `run_one` in
`src/analyzer/ExhaustiveKeep.cpp:946-1039`; reverse-engineered `--seed 10000001`, d3/b10/max-turns 8,
`MTG_LOTUS_PREFIX=1`) shows the `SLOW-ROLLOUT ms` ranking in the capture logs is **wall-clock under
24-core contention, not algorithmic cost**:

| hand | captured (all-cores) | true single-threaded @365f3d3 | win |
|---|---|---|---|
| Sandstone Needle x2; Apex of Power x4 | 91,719 ms | **~5.5 s** | T5 |
| Pyretic Ritual x1; Apex of Power x2; Lotus Bloom x3 | 68,650 ms | **~24 s** | T6 |
| Ruby Medallion x1; Pyretic Ritual x1; Apex of Power x3; Lotus Bloom x1 | 54,712 ms | **~23 s** | no-win |

Consequences for the levers above:
- **The multi-Apex CHAIN is the *cheapest* of the three** single-threaded — the opposite of the capture
  ranking. The genuinely slow class is **ritual + Apex + Lotus**, whose cost is the Pyretic/Lotus
  **plan-count powerset** that funds the Apex payoff (odometers 512–1024), NOT the chain. So lever (2)
  as originally framed ("collapse the subset-of-exiled-cards powerset, possibly byte-identical") targets
  the wrong thing; the ritual powerset (lever 1's territory) dominates.
- **No byte-identical Apex cut exists.** With the first Apex's 10-of-one-colour float still up, casting a
  *second* Apex (`cast_from_hand=false` → no float, `TurnSolver.cpp:4082/4798`; a pure 10-mana dig) can
  reveal a castable Dragon/Dragonstorm and win *this* turn — a real line stopping at one Apex misses.
  Only `CanPay`-failing subsets are safely prunable, and `consider()` already drops those. So any Apex
  tractability lever is **result-changing (heuristic)**, adopted via the heuristic-optimization workflow
  + explicit approval — same class as levers (1)/(3).
- **All four existing collapses already reach the Apex-staged (`m_is_staged`) copies** (they operate on
  `CollectActions` output, whose `ap.hand` includes merged staged cards — `TurnSolver.cpp:1400`, staged
  pushed at `4790`, real-executor merge `AIEngine.cpp:1152-1166`). So the residual is not a coverage
  gap; it is exactly what the prunes deliberately keep (staged Apex counts as a reachable payoff, and
  `DropRitualGroupsIfNoPayoff`'s `feasible_net >= cheapest_payoff` test passes so all rituals stay).

**Recommended Apex lever ("gate #2" — payoff-reachability gate):** keep a chained Apex — and keep ritual
mana beyond `Apex-cost + 10` — only when a *real* payoff (Dragon/Dragonstorm) is reachable after paying
for it; otherwise prune. Caps BOTH slow classes (Apex has no `{X}` → surplus ritual mana is wasted
unless it funds a chained Apex or a dragon). Hook points (must stay in lockstep, like `OrderingOpaque`):
staged-Apex candidate emit `TurnSolver.cpp:1878-1903`; group pre-drop beside `DropRitualGroupsIfNoPayoff`
(`2355`/`3105`); per-subset `consider()` `2628`; chain recursion `AIEngine.cpp:2020` + `TurnSolver.cpp:4809`.
Preferred over a blind chain-depth cap because it preserves dig-to-lethal lines precisely when a dragon
is reachable. Branch-stats caveat: `MTG_BRANCH_STATS` fires only in `EnumeratePlans` (`TurnSolver.cpp:6693`),
NOT in the rollout-leaf `Solve`, so the Sandstone chain's cost is invisible to it (lives in the leaf
re-solve) — use `MTG_ROLLOUT_STATS` (SimulateToEnd count) + wall time for the leaf class.
