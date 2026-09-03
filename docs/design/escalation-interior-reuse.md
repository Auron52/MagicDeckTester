# Escalation efficiency: reuse the value-leaf probe's interior traversal

**Status:** the PRIMARY lever (a game-persistent "true leaf cache") was BUILT and MEASURED — it does
**not** pay (negligible + unsound; see "RESULT" below). The SECONDARY lever (a leaf-independent interior
child-state cache) is still un-built and is now the only remaining reuse option; it needs the
interior-fraction measurement first. Goal was: make the value-leaf hybrid's heuristic **escalation**
cheaper by reusing search work instead of re-traversing the tree from scratch.

Companions: `escalation-and-rollout-cost.md` (the lever menu + the DEAD warm-start-bound result),
`learned-d0-policy.md` (the value-leaf hybrid), `overnight-audit-2026-07-11.md` (the budget-exhaustion
regression the fresh-budget fix addresses).

## CORRECTIONS (2026-07-16, later): two earlier "DEAD" calls below were WRONG diagnoses

Two conclusions in the RESULT/interior sections below were later refuted by direct measurement (the user
correctly flagged both as "sounds like a bug"):

1. **"Leaf cache is UNSOUND" — WRONG. It is SOUND.** A verify-on-hit harness (`MTG_LEAF_VERIFY`: recompute
   every hit fresh and compare) found **0 stale hits over 283,014 verified hits** on hinata. The earlier
   "hinata's deterministic work rose" was NOT stale recompute — it was the cache freeing budget (a hit does
   not `Consume`) so the deterministic **start-gate** (which reads `budget->Remaining()`) let the escalation
   ladder **deeper**. Legitimate depth change, sound results (LP-neutral). The persistent cache's cross-decision
   reuse is still small (~0.14pp added hit rate), so it does not help QUALITY — but it is not a bug.

2. **"Single-depth costs MORE / skip-earlier-depths is dead" — WRONG.** That was a DEPTH-MISMATCH confound:
   the single-depth config targeted the PROBE's committed depth (~d5) while the ladder early-exits/start-gates
   at h2/h3. At MATCHED depth (`MTG_ESC_MEASURE`: cold single pass at the ladder's ACTUAL committed depth),
   single/ladder = **0.825 hinata / 0.817 antilife** — the single pass is ~18% CHEAPER, so the ladder's shallow
   passes ARE ~18% overhead. Also fixed a real bug: the `MTG_ESC_SINGLE` path passed `tt=nullptr` straight to
   `FSLineWin`, so it ran with NO leaf cache (the ladder always builds a local table) — an unfair comparison.
   CATCH: capturing the 18% needs the affordable depth K, and a BLIND predictor (`MTG_ESC_JUMP`) overshoots on
   hinata (rollout cost accelerates irregularly; only the ladder's per-pass measurement tracks it). A
   measurement-based K predictor is the open path. See the hinata-escalation-budget-restore memory FOLLOW-UP 7.

The sections below are the ORIGINAL (partly-superseded) analysis; read them through the corrections above.

## THE WIN BEING BUILT: measurement-based K-predictor for the escalation (MTG_ESC_PREDICT)

Since skip-earlier-depths is real (18% at matched depth) but a BLIND predictor overshoots, build a
MEASUREMENT-based one. The probe ladders d1..committed for free (cheap value leaf) and reveals, per depth, the
LEAF COUNT (the quantity the escalation's dominant rollout cost scales with). The escalation then:
1. **Records** the probe's per-depth leaf counts (`g_probe_leaves`, in `FullSearchLine`'s loop, gated
   `g_probe_recording`). Use the **CUMULATIVE** sum (prefix), NOT the per-pass delta — a deep pass's frontier
   is mostly memo hits so its delta ~0 and undercounts (was the dominant lossy bug; fixed).
2. **Calibrates** its own per-leaf cost with ONE cheap d1 pass (`esc_c1`).
3. **Predicts** K = deepest d with `esc_c1 * cumL[d]/cumL[1] <= mult*esc_budget` (mult default 1.0), capped at
   the probe's reach `committed`.
4. **Runs one pass at K** (it IS the committed pass when the estimate is right — no waste), then to stay
   LOSSLESS (never shallower than the ladder) ladders UP via the start-gate while the next depth's estimated
   cost fits, and falls back ONE depth on abort.

Code: predict branch in `FullSearchLineHybrid` (`TurnSolver.cpp`, `if (s_esc_predict)`); leaf-count recording
in `FullSearchLine`'s pass loop. Gated `MTG_ESC_PREDICT` (off => byte-identical, verified 3003653).

**Measured (120g s1001, deterministic turn_steps = escalation work):**
- ANTILIFE: byte-identical LP (all 3 seeds), **−23..−41% work** (0% abort — regular growth => exact estimate).
- HINATA: simple fallback-only version **−12% work for +0.008 LP** (wins unchanged, turn-later churn). The
  LOSSLESS ladder-up version is +5% work / +0.017 LP — WORSE (over-shoots 17% deeper, still 12.9% lossy), so
  the SIMPLE version is the better hinata tradeoff today.

## CORRECTION (2026-07-16, later still): the hinata "−12%" was a ROLLOUT-ONLY measurement artifact — DEAD

The "−12% hinata" above counted only `turn_steps` (rollouts). Re-measured with **TOTAL work = interior_nodes
+ turn_steps** (the calibration d1 pass and the cold pass-at-D both RE-TRAVERSE interior the ladder would have
memoized), hinata is **work-NEUTRAL and quality-NEGATIVE**:

| deck (d5 b20, s1001) | won | avg | LP | total work (interior / rollout) |
|----------------------|-----|-----|-----|---------------------------------|
| hinata baseline (120g) | 110 | 5.7546 | 6.0250 | 15,713,008 (9.42M / 6.29M) |
| hinata predict (120g)  | 109 | 5.7706 | **6.0667** | **15,726,611** (9.61M / 6.12M) — +0.1% work, +0.042 LP, −1 win |
| antilife baseline (150g) | 144 | 4.4514 | 4.6333 | 659,011 (391K / 268K) |
| antilife predict (150g)  | 144 | 4.4514 | **4.6333** | **564,652** (382K / 183K) — **byte-identical, −14.3% work** |

Root cause: **"skip earlier depths" only pays when the committed depth is DEEP.** A cold `FSLineWin(D)`
recomputes the interior that the ladder's shallow passes had memoized; for SHALLOW D (hinata commits mostly
d2–d3) that recomputation ≈ the shallow passes' cost, so the rollout saving is cancelled by the added interior
(+2%). For DEEP, REGULARLY-growing escalations (antilife d4–d5) the skipped shallow passes are non-trivial AND
the leaf-growth is predictable, so the replay predicts D **exactly** (byte-identical) and the skip genuinely
saves −14%. The cold-regime audit confirms hinata is unpredictable: **25% lossy** (predict shallower than the
ladder) — the depth-decay (per-leaf cost falls d1→d2, so `chat[2]` over-estimates real `c2`) under-commits the
pass-3 gate. Making hinata lossless is therefore **moot** — there is no work win there to preserve.

Practical value is LOW: the optimization helps **antilife** (already cheap: 565K work) by 14% and does NOT
help **hinata** (the expensive deck: 15.7M work). It is a per-deck opt-in at best, not a general speedup.
`MTG_ESC_PREDICT` is off by default (byte-identical: hinata OFF turn_steps=5,476,754 == baseline). Kept the
`MTG_ESC_PREDICT_AUDIT` / `_COSTCURVE` / `[hladder]` instruments (all gated) for future work.

## FINAL (2026-07-16): losslessness IS achievable on hinata, but exact & skip are MUTUALLY EXCLUSIVE

Pushed on hinata directly (user: "if we nail hinata it helps other decks"). Established the clean, verified
characterization below. Confirmed first (`MTG_ESC_MEASURE`, clean per-escalation, both play-invariant): the
skip saves ~16-18% at MATCHED depth (hinata single/ladder=0.837, antilife=0.823) -- the ladder does NOT
benefit us; a single cold pass at the committed depth is cheaper. So the skip is real; the blocker is purely
reproducing the ladder's committed depth D.

**The CLIMB design (correctness by measurement, not prediction).** Predicting D from cheap signals is 26-32%
lossy on hinata (irregular costs: leaf-scaling, interior/rollout-split, amortized-R all failed). Fix: GUESS a
start depth D0 (cheap, from probe structure x amortized R), then run the REAL ladder from `lo = D0-2` climbing
UP, deciding each pass with the ladder's start-gate on MEASURED costs (`FSLineWin` incremental ~= cold on
hinata's steep growth). A cold `FSLineWin(state,D)` is a pure function of state+depth, so hitting the ladder's
exact D is byte-identical. Two bugs found+fixed via the `[climb] cmeas` dump: (1) the one-time d1 R-seed pass
memoized d1 into the climb's cache -> `run_meas(1)` measured 0 -> ratio exploded -> stopped short (removed the
seed; default R prior); (2) the gate used an ESTIMATED cost just below the start -> inflated ratio -> stopped
short (gate ONLY where both adjacent costs are MEASURED, or the pass-2 kDefaultGrowth bootstrap the ladder
itself uses; below that, run without gating -> never stop early on an estimate). Result: **hinata 100% EXACT
(89/89, 0% lossy), byte-identical AB (110 won / 5.7546 avg / 6.0250 LP, work -0.004%).** Losslessness ACHIEVED.

**But it is NEUTRAL, and that is FUNDAMENTAL.** To gate the boundary EXACTLY you must MEASURE the two adjacent
passes (need c_{D-1}, c_{D-2} for the growth ratio). The skip's saving comes from running ONLY the committed
pass. You cannot skip the passes you must measure. On hinata escalations are SHALLOW (D=2-3), so the boundary
IS the shallow passes -> nothing to skip while exact -> work NEUTRAL. Measured across configs (all
byte-identical): climb `lo=D0-2` hinata -0.004% / antilife -0.6%; `lo=D0-1` antilife +2.1% (cold boundary
passes sum to ~= the ladder either way). The only config that SAVES is the SINGLE cold pass at predicted D
(skip everything, run 1 pass) = 0.82x ladder = ~-14% -- but it needs EXACT prediction (no neighbour
measurement), so it is byte-identical only where growth is REGULAR (antilife -14%) and 26% LOSSY where
irregular (hinata). Warm-tt/bottoming extension (`MTG_ESC_PREDICT_WARM`, 87% of hinata's escalation volume):
byte-identical but **+10% work** (rollout +25%; the warm cache aids the ladder's tight internal loop more than
separate measured passes) -- DEAD.

**Bottom line / answer to "can we nail hinata":** CORRECTNESS yes (the climb reproduces the ladder
byte-identically even on hinata's irregular costs -- lossless on ANY deck); SAVING no on hinata (structural:
shallow escalations have no skippable tail while staying exact). The one real win is **antilife -14%
byte-identical via the single-cold-pass predictor** (regular growth -> exact prediction), generalizing to any
regular-deep-escalation deck; NOT to hinata. New knobs (all gated, default byte-identical, verified 5,476,754):
`MTG_ESC_PREDICT_LOOKBACK` (default 2), `MTG_ESC_PREDICT_WARM` (dead), `MTG_ESC_PREDICT_MULT`, plus the
`[climb]`/`[hladder]`/`[costcurve]` audit dumps.

**Audit (`MTG_ESC_PREDICT_AUDIT`: shadow-run the ladder, compare committed depths):** 70% exact, 17% deeper
(safe), **12.9% lossy** (predict shallower = quality risk). Two lossy causes: (1) deepest-pass leaf delta = 0
[FIXED via cumulative]; (2) OPEN — **per-leaf rollout cost decreases with depth** (a leaf at depth d rolls out
turn+d..game-end = fewer turns deeper), so calibrating R from d1 (long rollouts) over-predicts deep cost and
picks K one short. FIX IDEA: model rollout length ~ `(max_turns - turn - d)`, or calibrate R at a mid depth.

**Open / next:** (a) fix lossy cause #2 for a lossless hinata; (b) measure burn/TH (escalate a lot, no trust
depth — likely helped) + the aggro decks; (c) full smoke byte-identical-OFF; (d) adoption (antilife = clear
pure win; per-deck or global flag) + GT rebaseline. See hinata-escalation-budget-restore memory FOLLOW-UP 9/10.

## RESULT (2026-07-16): the game-persistent leaf cache does NOT beat the per-call table — DEAD

Built `MTG_LEAF_CACHE` = a game-persistent `TranspositionTable` (AIEngine-owned, threaded into the hybrid
as the rollout `tt`, cleared per game), so the escalation's `SimulateToEnd` rollout leaves persist across
a game's decisions instead of being thrown away per decision. Measured d5 b20 100g s1001 with the kept
`MTG_TT_STATS` hit-rate instrument + deterministic `MTG_ROLLOUT_STATS` (`turn_steps`, a cache hit skips the
rollout so it directly measures removed work; OFF is bit-reproducible — verified 5476754 twice):

| deck | per-call (OFF) turn_steps / tt-hit% | persistent (ON) turn_steps / tt-hit% |
|------|-------------------------------------|--------------------------------------|
| hinata   | 5,476,754 / 12.94% | **5,503,224** / 13.08%  (work went UP) |
| antilife |   174,819 / 29.96% |   168,988 / 27.03%  (work down ~3.3%) |

Two independent kills:
1. **Unsound.** `turn_steps` is deterministic, yet hinata's ON value is *higher* than OFF. A pure
   memoization cache can only skip rollouts (work ≤ OFF), so a higher value proves the cache **changed the
   search** = stale cross-decision hits that alter the committed line. `BuildSimKey` is exact only WITHIN
   one decision; it omits state that is constant within a decision but varies across them, so cross-decision
   reuse is not byte-identical (folding the full ordered library under search-shuffle is necessary but not
   sufficient). Not shippable.
2. **Negligible even where sound.** The per-call table already captures ~all available leaf reuse — the
   recurring rollout leaves are overwhelmingly INTRA-decision (hinata 12.94%, antilife 29.96% hit rate
   already). A persistent cache added only ~0.14pp of hit rate on hinata (and those were the corrupting
   stale ones). Antilife's ~3.3% is the sound ceiling — not worth the memory + a per-field key audit.

So the user's hypothesis — *"the transposition table cache should not be able to beat a true leaf cache"* —
is **refuted by measurement in the other direction**: the `TranspositionTable` already IS the true leaf
cache (it memoizes `SimulateToEnd` rollout RESULTS). It is per-decision-scoped because its key is only
per-decision-exact; extending it cross-decision doesn't beat it (the reuse it would add barely exists) and
breaks byte-identity. The escalation is expensive because it explores a large number of DISTINCT leaf
states (~87% of hinata's rollout leaves are non-repeats), not because it re-rolls reusable ones.

**Action taken:** reverted the `MTG_LEAF_CACHE` scaffold (unsound); KEPT `MTG_TT_STATS` (byte-identical
hit-rate instrument, `TranspositionTable.h`) for any future reuse question. Byte-identical baseline
re-verified after revert (hinata 5476754, antilife 174819).

**The interior-node counter then killed the SECONDARY lever too.** Added `interior_nodes` +
`interior_esc` to `MTG_ROLLOUT_STATS` (kept instruments; the interior node = one
`EnumeratePlansWithLand`+`ApplyPlanDirect`+GameState-copy at the FSLineWin loop). Hinata d5 b20 100g s1001,
escalation ON (the shipped config):

- interior_nodes = 8.11M, turn_steps (rollouts) = 5.48M → **interior is 60% of the (interior+rollout)
  work**. So interior work IS large — the user's intuition that "the search work prior to the rollouts is
  expensive" is CORRECT.
- BUT interior_esc = 282K → the heuristic **escalation** does only **3.5%** of interior nodes (antilife:
  5.7%). The other ~96% are the **probe** (cheap value leaf → ladders deep → explores the whole tree).
- Cross-check: with escalation OFF (`MTG_VALUE_MIN_DEPTH=0`) turn_steps collapse (hinata 5.48M→0.48M),
  confirming ~all rollouts are the escalation's; the probe does ~0 rollouts (value leaf replaces them).

Full hinata work split (shipped config): **probe ≈ 7.8M interior + ~0 rollout ≈ 61% of total; escalation ≈
0.28M interior + ~5.0M rollout ≈ 39%, of which ~94% is rollouts.** The design doc's founding premise — "the
interior traversal is done twice, so share it" — is FALSE: the escalation barely traverses interior (it is
rollout-bound), so a probe↔escalation interior child-state cache would save only ~2% of total work. DEAD.

## Bottom line: no reuse lever helps escalation cost

Both reuse caches are measured dead. The escalation is **rollout-bound**, and cross-decision rollout reuse
is negligible + unsound (the leaf-cache result). The single biggest cost — the probe's deep value-leaf
interior exploration (~61%) — is **not redundant work**; it is the value-leaf search doing its job, and its
recurring states are intra-decision (already deduped by the per-call `FSLineCache`). There is nothing to
"reuse."

The real levers for escalation cost are therefore NOT reuse but **doing less**, and they already exist:
- **Confidence-gate** (`MTG_ESCALATION_GATE`): skip escalations predicted to be no-ops — cuts escalation
  rollouts directly. Built; frozen for generation (T=0.70). This is the right lever.
- **Fresh-budget fix** (`MTG_ESCALATION_FRESH_FRAC=1.0`, working-tree default): recovers the antilife
  budget-exhaustion regression; surgical/byte-identical off the escalating decks. Pending accept.
  *(2026-09-03: the 1.0 default was REVERTED 2026-07-17 as drift — see escalation-refactor-drift.md;
  the env default is −1 (legacy) and the fix ships per-deck as `value_play.escalation_fresh_frac=0.5`.)*
- **Truncated rollout horizon** (`MTG_ROLLOUT_HORIZON`): caps the escalation's rollout length.
- Probe-cost levers (cap probe depth / lower the value startgate alpha) trade quality for speed and were
  already REJECTED (drop to the no-escalation floor).

All kept instruments are byte-identical when their env flag is unset: `MTG_TT_STATS` (leaf-cache hit rate,
`TranspositionTable.h`) and `MTG_ROLLOUT_STATS`'s `interior_nodes`/`interior_esc` (`TurnSolver.cpp`).

## Background: what the hybrid does, and where the waste is

`FullSearchLineHybrid` (`src/ai/TurnSolver.cpp` ~6381) runs TWO full `FullSearchLine` searches for an
escalated decision:
1. **Probe** — `FullSearchLine` with the cheap value leaf (O(1) `MidGameEvaluator::Score`). Ladders d1→d5,
   commits a line at depth C. If the win is VERIFIED (within the searched horizon) it is kept — the probe
   paid off (no escalation). If UNVERIFIED and committed below the trust depth, it **escalates**.
2. **Escalation** — a second `FullSearchLine` with `g_force_heuristic_leaf=true` (the exact heuristic
   rollout leaf, `SimulateToEnd`). Also ladders d1→d5, from an EMPTY memo.

**The premise (REFUTED — see RESULT above):** the doc assumed for any escalating line "the probe's entire
traversal is thrown away and redone with the heuristic leaf... the interior traversal is done twice." The
interior-node split MEASURED this false: the escalation does only ~3.5% (hinata) / ~5.7% (antilife) of
interior nodes — it is rollout-bound, not interior-bound — so there is no "done twice" interior waste to
reclaim.

This is deck-shaped: the probe pays off when the **verify-rate is high** (antilife ~72% verified → probe
saves the escalation), and escalates more when low (hinata ~56%). The probe's deep exploration (not the
escalation) is the dominant cost, and it is genuine work, not redundant.

## What the diagnostics established (so we don't re-derive)

Measured on antilife/hinata d5 b20, deterministic `MTG_ROLLOUT_STATS` (contention-proof):
- **The ladder's efficiency is the MEMO, not the incumbent.** A single deep pass done cold costs MORE than
  the whole d1→d5 ladder (hinata: 23.86M vs 14.5M rollout-steps; antilife: 789K vs 362K). Seeding the single
  pass with the value-leaf's committed win-turn as a B&B cutoff (`MTG_ESC_SINGLE_WARM`, a diagnostic) did
  NOT reduce work (antilife 805K ≈ 789K). So the shallow passes are not waste — they populate a
  transposition memo (`FSLineCache`, keyed by `(state, remaining-depth)` via `BuildSimKey`) that the deep
  pass reuses for repeated states, saving BOTH interior and leaf work. => single-depth / "roll out only one
  depth" is a REGRESSION (more work), and warm-start-bound reuse is confirmed dead (matches lever #3).
- **The memo is leaf-DEPENDENT.** `FSLineCache` stores `(state, depth) -> SearchLine{win_turn, phases}`; the
  win_turn is computed from the leaf. The probe's memo holds value-leaf win-turns, so the heuristic
  escalation CANNOT reuse the probe's memo — it would return wrong answers. This kills "hand the probe's
  cache to the escalation."
- **Only the leaf-INDEPENDENT slice is reusable:** `EnumeratePlansWithLand` + `ApplyPlanDirect` + the
  `GameState` copy that produces each child state. Per whole-engine profiling that is ~ApplyPlanDirect 22% +
  GameState copy 13% ≈ **35% ceiling**. The rollouts (the escalation's dominant NEW work) and the memo (must
  be rebuilt, leaf-specific) are NOT reusable. So the reuse ceiling is that interior slice, biggest on
  escalation-heavy decks (hinata).

## The plan (SUPERSEDED by the RESULT section above)

The original plan proposed building (1) a game-persistent "true leaf cache" and (2) a leaf-independent
child-state cache, gated on an interior-node measurement. All of that was DONE: (1) was built and measured
dead (negligible + unsound); the interior measurement then killed (2) before building (the escalation does
~3.5-5.7% of interior nodes, so a probe↔escalation child-state cache saves ~2% of total). See the RESULT
and "Bottom line" sections at the top. The still-valid reference facts are kept below; the build steps are
not worth re-deriving.

Re-opening "is value-leaf net-positive for hinata normal play": this was contingent on an efficient
escalation making value-ON fast enough to flip the measured net-negative (value-OFF LP 6.0867 vs value-ON
6.0955, ~3× slower). Since no reuse lever pans out, the escalation cannot be made cheaper via reuse, so this
does NOT flip on efficiency grounds — the value-leaf's hinata cost is inherent (deep probe + rollout-bound
escalation). Re-judge only if a non-reuse lever (gate, horizon) changes the cost/quality balance.

## Explicitly out of scope (measured dead / rejected)

- Single-depth / "roll out only one depth" escalation (`MTG_ESC_SINGLE`): more work (loses the memo). DEAD.
- Warm-start the escalation's B&B with the value-leaf bound (lever #3 / `MTG_ESC_SINGLE_WARM`): no work
  reduction. DEAD.
- Reserve/cap the probe budget (`MTG_ESC_SPLIT`): drops quality to the no-escalation floor (probe's deep
  reach finds verified wins). REJECTED.
- Value-leaf ranking as a HARD candidate filter (top-K): unsound search-restriction (the sound version —
  rank-ordering for B&B — is already `MoveOrderPlans`). REJECTED.
- Game-persistent leaf cache (`MTG_LEAF_CACHE`): cross-decision rollout reuse is negligible (~0.14pp added
  hit rate) and UNSOUND (stale hits changed play — hinata's deterministic work rose). DEAD (built, measured,
  reverted). See RESULT above.
- Leaf-independent interior child-state cache (probe↔escalation enumerate+apply sharing): the escalation
  does only ~3.5-5.7% of interior nodes, so this saves ~2% of total work. DEAD (killed by the interior-node
  measurement before building). See RESULT above.
