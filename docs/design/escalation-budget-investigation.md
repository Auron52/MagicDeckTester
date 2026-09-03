# The hybrid's escalation budget: why "rejected" value leaves were config artifacts — investigation state

**Status: RESOLVED — ADOPTED 2026-09-03 (session 2; see "Session 2 findings" + "ADOPTION
IMPLEMENTED" below).** Fresh-full escalation is engine behavior, the per-deck frac field is
deleted, CG's model is live with cap+beam, and the tiers were rebaselined. USER's bar, in their
words: *"I don't want to just disable the value-leaf, as I consider that a failure of our approach
here. I think I have, in the past, accepted a small amount of quality regression to install some
value-leaf entries, but in reality I shouldn't need to make that tradeoff for the most part."*
That bar is MET: held-out control parity with zero win<->loss flips at 0.43x the control's wall.
Sections below are the investigation record; measurement logs live in `logs/cg_leaf_ab/` +
`logs/esc_diag/` (gitignored, this machine). Remaining follow-up: generated cap/beam blocks for
the 3 blockless model decks (Dragons, Minotaur, Mirrorwing).

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

## Session 2 findings (2026-09-03, "in earnest"): the depth deficit is REAL but the baseline was misread

**The control's committed depth is c2-c3, NOT d4-d5.** `MTG_HYBRID_STATS` now also records the
CONTROL (no value model) committed-depth histogram, split by verified — the UNVERIFIED subset is
the population the hybrid escalates, so it is the fair baseline (the earlier alarm compared the
escalation against the PROBE's d4-d5, which the probe only reaches because its leaf is O(1)-cheap).
CG d5/b20 s2002 x 250 g, mean committed depth of the unverified/escalated population:

| arm | CG | Hinata2 |
|---|---|---|
| control unverified (the target) | **2.11** (c2:324 c3:97, n=493) | **2.35** (n=666) |
| escalation, legacy leftovers, cap+beam engaged | 1.42 | 2.02 |
| escalation, fresh 0.5, cap+beam | 1.90 | (0.5 = hinata's shipped block) |
| escalation, fresh 1.0, cap+beam | **2.53 — DEEPER than control** | **3.17 — DEEPER than control** |
| escalation, fresh 1.0, NO cap (the session-1 arms) | 1.75 | — |

(CG "cap+beam engaged" = ship sidecar + `escalation_cap:5, beam_width:3, beam_leafdepth:2`, the
antilife/hinata template; CG b40 control unverified mean 2.34. Logs: `logs/esc_diag/`.)

So: (1) the session-1 "h1-h2 absurdly shallow" read was against the wrong baseline — the control
itself commits ~2.1-2.35; (2) engaging the adopted single-pass+beam design is worth ~+0.4-0.8
depth at every frac (1.75 -> 2.53 at frac 1.0 on CG); (3) even WITH cap+beam, frac 0.5 stays
below the control (1.90 vs 2.11 on CG) and legacy is far below — the USER's "budgeting logic"
suspicion, now quantified: the deficit is BUDGET-driven, not (only) structure-driven; (4) at
frac 1.0 + cap+beam the escalation out-deepens the control on both decks. Fleet data agrees:
hinata (cap+beam already shipped) still gained -0.0106 from 1.0-vs-0.5 in the session-1 tier A/B,
so cap+beam does NOT make 0.5 sufficient. Caveat: populations differ per arm (the probe verifies
wins early so the hybrid sees ~0.36 escalations/game vs the control's ~2 unverified decisions/game).

**The decision measurement (held-out, ONE pooled batch, `logs/esc_diag/manifest_ho4.json` +
`analyze_ho4.py`; CG d5/b40, seeds 20000-35000 x 500 g = 8000 games/arm):**

| arm | vs control (paired/seed) | win<->loss flips | wall |
|---|---|---|---|
| no-cap fresh 1.0 (session-1 config) | -0.0004 (t=-0.33) | 3 win->loss | 0.55x ctl |
| cap+beam fresh 0.5 | **+0.0031 (t=+2.71, 12/16 seeds) WORSE** | 0 | 0.41x |
| **cap+beam fresh 1.0** | **-0.0004 (t=-0.33) PARITY** | **0 both ways** | **0.43x** |

cap+beam @ fresh-full dominates: control-parity quality, no flips, 2.3x faster than the control
and 22% cheaper than the no-cap config it replaces. Fresh 0.5 is dead: worse than legacy on the
train fleet (session 1) AND worse than control held-out even with cap+beam (this table) — its
depth deficit (1.90 vs the control's 2.11) is exactly what the quality delta shows.

**Mirrorwing persister REPRODUCED and scoped** (`logs/esc_diag/mw_*.{log,err}`): the exact
overnight cell (300 g, s7007, d5/b20) under the current binary matches GT byte-for-byte at legacy
(gi44 the only unwon, == GT), and under fresh-full moves 4 games: gi27 7->8, gi88 6->unwon,
gi173 6->unwon (the known persister), gi183 8->6. Mirrorwing has a model sidecar but NO
value_play block — so it runs the hybrid with the FULL-LADDER escalation (no cap/beam) and is a
candidate for a generated block; the cell's net is +0.017 avg here vs the whole-tier net -0.0050
in mirrorwing's favor (session 1 overnight), i.e. knife-edge churn concentrated in one cell.

**ADOPTION IMPLEMENTED (this session): fresh-full escalation is ENGINE BEHAVIOR.**
`value_play.escalation_fresh_frac` is DELETED (field, parse, pass-through, hybrid param) — the
absent-key-means-starved footgun cannot recur; `MTG_ESCALATION_FRESH_FRAC` survives as a research
hatch only (default 1.0, -1 = legacy). The 0.5 keys were stripped from the antilife/dragonstorm/
hinata2 carriers. CG's fresh model went LIVE (`Creature Giving.value.json`: eval_model + enabled
d5/b20 + cap 5 + beam W3/ld2 + mull_gen d1/b3/K20; the `.value.DISABLED.json` copy was removed —
same model, git history retains every prior artifact). NOTE: 8 enabled decks (Auras, FiveColour,
Goblins, Knights, StompySurprise, burn, slivers_vial, treasure_hunt) had NO frac key and were
therefore running STARVED in production; they gain the fresh budget (and already ship caps 5-6).
The 3 blockless model decks (Dragons, Minotaur, Mirrorwing) get fresh-full with the full-ladder
escalation — generated cap/beam blocks for them are the natural follow-up.

## Next steps (the "in earnest" plan — ALL DONE in session 2, kept as the record of the method)

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
