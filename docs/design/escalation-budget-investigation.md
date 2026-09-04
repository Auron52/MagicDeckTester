# The hybrid's escalation budget: why "rejected" value leaves were config artifacts — investigation state

**Status: OPEN INVESTIGATION (2026-09-03), being tackled in earnest next.** USER's bar, in their
words: *"I don't want to just disable the value-leaf, as I consider that a failure of our approach
here. I think I have, in the past, accepted a small amount of quality regression to install some
value-leaf entries, but in reality I shouldn't need to make that tradeoff for the most part."*
The working tree was deliberately REVERTED to committed HEAD after this doc was written (shared
worktree; the half-adopted state below is cheaply reproducible from the recipes). All measurement
logs live in `logs/cg_leaf_ab/` (gitignored, this machine).

## What was established (all on the current tree, 2026-09-03)

**1. The Creature Giving value-leaf "play rejection" (2026-08-06, re-confirmed twice today) was
entirely an escalation-starvation artifact.** The hybrid (`FullSearchLineHybrid`) runs a cheap
value probe, then escalates to a heuristic re-search; under the legacy default the escalation runs
on the probe's LEFTOVER budget (`MulliganProfile.h` value_trust comment: "escalates to one heuristic
search on the remaining budget"). Decks whose `value_play` carried no `escalation_fresh_frac` —
including CG in every A/B it ever had, and every phase-E adoption A/B of a first model
(`BuiltinDefaultPlay` has no frac) — measured the starved config. Held-out 16 fresh seeds x 500 g,
CG d5/b40, model armed vs no model:

| escalation budget | quality vs ctl | wall |
|---|---|---|
| legacy leftovers | +0.0083 (t=+5.2, 15/16 seeds worse) | 2.40x faster |
| fresh 0.5x | +0.0047 (t=+3.6, 12/16 worse) | 2.47x |
| **fresh 1.0x** | **−0.0001 (t=−0.1) and −0.0005 (t=−0.5) — PARITY, twice** | **2.14–2.18x** |

Zero win↔loss flips anywhere. The freshly REGENERATED model (11,707 rows on current play, RMSE
0.475, staged at `logs/eval/Creature Giving.value.STAGED.json`) showed the same: +0.01175 (t=+6.6,
0/8 seeds) under starved phase E. **The model was never the problem.**

**2. Fleet A/B of flat fresh fractions (regression tier, 34,700 games).** `MTG_ESCALATION_FRESH_FRAC`
env, one number for all decks (USER: the per-deck field is a footgun — its ABSENCE silently meant
"starved"; per-deck numbers are fine only when the value-leaf pipeline derives them):

- flat 1.0 vs flat 0.5: 1.0 better on 13/16 decks, worse on NONE, net −0.0025/game
  (hinata −0.0106, CG −0.0072, th −0.0058, mirrorwing −0.0050). Cost: +15% tier makespan,
  +21–72% per-deck search ms — and that cost multiplies through every budgeted workload
  (keepgen, value-leaf gen phases A/E, analyze), which the USER explicitly declined to gloss over.
- flat 0.5 vs legacy GT: **net +0.0018 WORSE than legacy** (hinata +0.0063, th +0.0042). Mechanism:
  legacy is not uniformly starved — leftovers often EXCEED half the budget (the probe is cheap),
  so flat 0.5 caps the common generous case while fixing the rare starved one. Only 1.0 dominated.
- flat 1.0 vs legacy GT, overnight tier (held-out): net −0.0004 over 225,600 games, 11/12 moved
  decks better, 118 faster / 25 slower games; 20/25 churn, 5 persist incl. ONE
  **win→loss** (mirrorwing_overnight_d5_s7007 gi173, 6→unwon at 4x AND 16x) — logged, unresolved.

**3. THE SMOKING GUN (`MTG_HYBRID_STATS=1`, CG d5/b20 s2002 250 g, logs/cg_leaf_ab/hs_cg_*.log):**
76% of decisions end at a probe VERIFIED win (250/329, case-1, pure profit). Of the 79 escalations,
**100% fall short of user depth in EVERY arm** (`redo_short == redos`), and the achieved-depth
histogram shows the escalation re-climbing the ladder COLD and topping out absurdly shallow while
the probe itself reaches d4–d5:

| arm | h1 | h2 | h3 |
|---|---|---|---|
| legacy | 68 | 7 | 3 |
| fresh 0.5 | 56 | 19 | 3 |
| fresh 1.0 | 26 | 48 | 4 |

So even at FULL fresh budget the heuristic re-search commits at depth ~2 where the pure-heuristic
control ladder (same b20) reaches ~d4+ — this is the USER's "budgeting logic has issues" suspicion,
confirmed. **Prime suspect:** the diagnostic arms ran the LEGACY FULL-LADDER escalation
(`escalation_cap=0`) — the adopted single-pass predicted-affordable escalation + value-ranked beam
(`value_play.escalation_cap>0` + `beam_width`, as antilife ships: cap 5, W=3, leafdepth 2) was NOT
engaged. The cold ladder re-pays every shallow pass at heuristic-rollout prices inside f×budget;
the single-pass path exists precisely to skip that.

## Next steps (the "in earnest" plan)

1. **Reference distribution:** instrument the pure-heuristic CONTROL's committed depth at b20/b40
   (no plain-ladder counterpart of MTG_HYBRID_STATS exists — add a default-off counter or reuse the
   hybrid stats shape). Without it, "escalation reaches h2" has no baseline.
2. **Re-run the hybrid-stats matrix with the adopted escalation design engaged**: CG block with
   `escalation_cap` (=5) + beam (W=3/leafdepth 2), × frac arms. Hypothesis: single-pass + beam
   reaches the control's depth at far less than 1.0×budget, giving parity WITHOUT the fleet cost —
   the no-tradeoff outcome the USER wants. (Also fix hinata diagnostic runs: deck file is
   `decks/Hinata2/Hinata2.cod`, not .txt — the hs_hinata_* logs are empty.)
3. **Then revisit the flat number** (or conclude the number barely matters once the escalation is
   structurally cheap). Also chase the mirrorwing win→loss persister before any adoption.
4. Re-do the adoptions cleanly: engine change + CG model install + 3-tier rebaseline.

## Reproduction recipes (everything reverted; re-create from here)

- **Engine edits (were built + 816/816 unit-clean + full tiers run):** delete
  `value_play.escalation_fresh_frac` (field in `MulliganProfile.h` ~L45, parse in
  `MulliganProfileIO.h` ~L1229, pass-through in `AIEngine.cpp` ~L2187, param in
  `TurnSolver.h`/`FullSearchLineHybrid`); in TurnSolver.cpp's "Escalation budget source" block set
  `s_fresh_frac` default to the chosen flat value (env stays as A/B hatch, −1 = legacy). Strip the
  frac key from the 4 sidecars (antilife/dragonstorm/hinata2 carried 0.5 — flat 0.5 is a no-op for
  their driving cells).
- **CG ship sidecar:** `logs/cg_leaf_ab/ship/Creature Giving.value.json` = fresh staged model +
  enabled block d5/b20 (+ mull_gen d1/b3 + expected_buckets 20 from the 2026-09-03 phase F, already
  committed in the live carrier at 14509067). Scratch A/B apparatus: `logs/cg_leaf_ab/apparatus/`
  (parked model), `ctl_bare/` (no sidecar), manifests `manifest*.json`, per-game wins in run_*.err.
- **Fleet A/B logs:** `frac05_batch.log`/`frac10_batch.log` (regression tier at 0.5/1.0),
  `f1_over.log`+`f1_classify_over.log` (overnight at 1.0 + classification incl. the win→loss).
- Suite budgets: enabled block owns DEPTH, cell `budget_ms` overrides freely; d0/d3 cells pass
  `--ignore-play-profile` (which also bypassed the per-deck frac — moot once flat).

## Related docs to update when this closes

`value-leaf-quality-floor.md` ("untrusted = cannot cost quality" — FALSE under starved escalation),
`value-leaf.md` skill (phase E measures the starved config for first models), `escalation-beam-verify.md`,
`overnight-audit-2026-07-11.md`, `escalation-interior-reuse.md`, `fallback-budget-renewal-handoff.md`
(all carry per-deck-frac framing), `analysis-Creature Giving.md` (rejection record),
`learned-d0-policy.md`. The stale parked model `Creature Giving.value.DISABLED.json` is superseded
by the fresh staged one when adoption lands.

## Session 3 (2026-09-04): the accounting defect found — R=120 misprices heuristic passes fleet-wide

The user's hypothesis — "value-leaf cost and heuristic cost may not be the same; if heuristic nodes
cost more perhaps we need to change how we account for them" — is CONFIRMED, with the sign varying
per deck. The single-pass escalation's affordability walk prices a heuristic pass at
`chat[d] = probe_cost[d] + R * probe_leaves[d]` with R frozen at the deck-agnostic 120 prior
(`value_play.escalation_r`, designed to be "calibrated offline" — **no deck in the fleet carries
it**; all 11 enabled decks run the prior). Measured actual R (heuristic rollout units per un-beamed
probe leaf, sampled from first-fit passes; new MTG_HYBRID_STATS diagnostics in the working tree):

| deck | measured R | prior error | esc rate/depth (250g s2002, own budget) |
|------|-----------|-------------|------------------------------------------|
| Anti-Lifegain | 15.6 | 8x overpriced | 47 esc, d3.28 |
| Creature Giving (staged) | 15–21 | 6x overpriced | 89 esc, d1.42 legacy |
| burn | 35 (n=4) | 3x over | 24 esc, d1.46 |
| Hinata2 | 40.8 | 3x over | 325 esc, d2.58 |
| Dragonstorm | 81 | mild over | 61 esc, d2.80 |
| FiveColour | 86 | mild over | **455 esc, d1.22** (starvation poster child, legacy budget) |
| Goblins | 96 (n=2) | ~ok | 5 esc |
| treasure_hunt | 156 | UNDERpriced | 66 esc, d1.53 |
| StompySurprise | 180 | UNDERpriced | 61 esc, d1.18 |
| Auras / slivers_vial / Knights | — | few/no escalations | R barely matters |

Pattern: beam decks (W3/ld2) measure R 15–41 (the beam prunes the rollout frontier); no-beam decks
measure 85–180. A flat prior cannot serve both. Everywhere measured: `abort_first=0, fellback=0,
wasted_units=0` — the walk NEVER overshoots, i.e. the current design errs exclusively toward
under-search, so lowering R to the measured value is waste-free by construction (and the overrun
fallback + climb still guard a wrong calibration in either direction).

**Decomposition of the CG starvation (esc depth 1.42 vs control 2.11):** two stacked causes.
(a) ACCOUNTING: with R=120 the hint targets t1 on 79/89 escalations *with budget in hand*
(probe leftover at escalation: mean 27%, bimodal — 37/89 arrive <10%, ~40/89 arrive 30–80%).
Calibrated R alone on the LEGACY budget: 1.42 -> 1.93 (R=5..25 ladder plateaus ~1.9), zero cost.
(b) GENUINE starvation: the <10%-leftover cohort is pinned at h1 under any R; only a
fresh/reserved budget helps. Probe units/leaf ~0.9 vs heuristic 15–180: the probe's cheap leaves
are why it can afford d4–d5 while the escalation cannot re-search there.

**Depth ladder (CG d5/b20 s2002, cap5+beam, control unverified mean 2.11):** legacy 1.42;
R120 arms: f0.5 1.90, f0.6 2.08, f0.7 2.20, f0.8 2.36, f1.0 2.53. R=10 arms: legacy 1.93,
f0.4 2.31, f0.5 2.42, f0.6 2.58. Calibrated R reaches f1.0-class depth at ~half the allowance.
MTG_ESC_SPLIT (probe cap, total<=1.0 by construction): 0.5 -> 2.03 but perturbs ALL probe
decisions (378 vs 339) — dominated by r10leg (1.93, probe untouched); set aside.

**CG held-out quality (16 seeds x 500g d5/b40, ho6 batch paired vs the ho4 ctl arm; smoke
51/51 byte-identical first so old/new runs pair validly):**
- r10leg (zero-cost R fix alone): +0.0056 t=+3.05, 3 win->loss — WORSE. Depth 1.93 is not enough;
  the accounting fix alone does NOT buy quality parity.
- r10f04: t=+0.63, 0 flips, **0.39x ctl wall — cheapest arm measured, beats cap10's 0.43x**.
- r10f05: t=+0.09, 0 flips, 0.42x. r10f06: t=-0.40, 0 flips, 0.43x.
- (cap10 = R120+f1.0 reference: t=-0.33, 0 flips, 0.43x.)
So calibrated-R + fresh 0.5 (+/-0.1) reproduces the fresh-full design's held-out quality at equal
or lower wall with HALF the worst-case budget allowance — the user's "same depth without 100%"
exists and is measured.

**Hinata2 (worst-cost deck under fresh-full):** shipped (f0.5, R120) esc depth 2.58 (ctl 2.35);
r40f05 -> 2.75, r40f04 -> 2.60 (shipped depth at 20% less allowance). Held-out A/B vs shipped:
see hho batch (logs/esc_diag/manifest_hho.json, hho.log/.err).

**Candidate design for the USER's decision (nothing adopted):** per-deck calibrated
`escalation_r` (measured, ideally emitted by the value-leaf generation so per-deck numbers stay
one-process) + `escalation_fresh_frac` ~0.4–0.5 fleet-wide, replacing the flat-1.0 proposal. The
open fleet question is the suite-makespan cost vs shipped at f0.4–0.5 (the reverted flat-1.0
package measured ~+15%; the allowance halves but actual spend is what counts — needs a tier-scale
A/B). The 3 blockless model decks (Dragons/Minotaur/Mirrorwing, full-ladder escalation, no cap)
sit outside this mechanism and still need generated cap/beam blocks.

Artifacts: logs/esc_diag/ run_sweep2.sh + sw2_*/sw3_*/sw4_* (depth ladders), fleet_*.err (fleet R
table), ship_cap_r*/ship_cap_f0[67]/hin_r40* (scratch sidecars), manifest_ho6.json + ho6.log/.err +
analyze_ho6.py (CG held-out), manifest_hho.json + hho.* (hinata held-out). Diagnostics code: the
MTG_HYBRID_STATS additions in src/ai/TurnSolver.cpp (working tree; leftover deciles, Rsample,
hint-target histogram, abort/waste counters) — byte-identical with stats off (smoke-verified).

### Session 3b (2026-09-04): the honest-budget alternative measured — and beaten

The user's proposal: budget should accurately reflect effort, so starved decks might best be fixed
by raising THAT deck's play-profile budget (legacy leftover escalation, no hidden frac multiplier)
rather than a fleet frac. Measured head-to-head (held-out 16x500g, hob batch; calibrated R on all
arms): **the B-raise design loses on both decks, both axes.** CG legacy-r10 at B50 (=1.25x the ho
rig's B40): t=+2.37 worse than ctl at 0.449x wall (frac 0.5: parity t=+0.09 at 0.418x); B60:
t=+1.31 at 0.435x. Hinata legacy-r40 at B25: t=+2.06 worse than shipped with 32 win->loss flips at
1.107x wall; B30: net parity t=-0.22 but 29 w->l / 11 l->w churn at 1.197x (frac 0.5: QUALITY WIN
t=-2.74, ZERO win->loss, 1.047x). Why: a bigger B mostly feeds the PROBE (deeper commits — CG
escalations even drop 89->47 — and wholesale different unverified lines = the flip churn), while
frac lands its extra effort exactly on the escalated (hard) decisions. Targeted allocation wins.

**Resolution of the budget-honesty concern without losing the frac mechanism:** express the
escalation allowance as an ABSOLUTE per-deck `escalation_budget_ms` (frac x budget_ms in today's
units — e.g. hinata frac 0.5 @ B20 == 10ms) instead of a hidden multiplier. Total worst-case
effort = budget_ms + escalation_budget_ms, both visible in the block, both emittable per-deck by
one process alongside escalation_r. Semantics honest; mechanism unchanged; nothing adopted.

### Session 3c (2026-09-04): budget scaling under the calibrated design — measured USEFUL

The user's requirement: the design must scale up usefully (extra budget -> better play), and their
refinement: probe-deepening is NOT low-value — a deeper probe that VERIFIES a win is the best
outcome (done, real simulation, no escalation needed); the harm is only starving the heuristic redo
when the win is not going to be found. Both confirmed. Trust coverage is already maximal by default
(no value_trust_depth -> escalate_below = lookahead_depth+1: EVERY unverified commit escalates),
and the frac allowance scales with the operating budget automatically, so the calibrated design
has no fixed-depth saturation. Hinata held-out (r=40, frac=0.5; 16x500g paired): B20->B30
d=-0.0165 t=-6.84 (net +10 games) at 1.204x wall; B30->B40 a further d=-0.0076 t=-6.28; total
B20->B40 d=-0.0241 t=-8.80 at 1.402x wall — monotone quality gain, SUB-linear wall growth (2x
budget -> 1.4x wall; verified decisions stop early and spend nothing). Structure scales in the
healthy shape: verified d5 probe commits RISE (249->280->294 per 250g) while escalations become
fewer but deeper (320@2.75 -> 288@3.07 -> 270@3.32). Contrast the legacy-leftover B-raise arms
(3b): same budgets, quality flat-to-worse with heavy win-flip churn. The allowance form stays a
FRACTION (absolute ms was considered and rejected: cells/gen override budget_ms per context —
b3 gen cells to b40 held-out — so the allowance must track the operating budget). CG note: CG is
budget-saturated at b40 (all arms within ±0.004 of avg 4.672), so scaling tests there are
insensitive by construction — use hinata-class decks.
