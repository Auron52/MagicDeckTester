# Handoff: Jumbo unlock-order budget price (MTG_UO_BUDGET_PRICE), 2026-11-20

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-a1cd888d17514cedb`, branch `wip/jumbo-uo-budget`
(based on origin/phase-1-2-deck-analyzer 8fbbc5023), pushed.

## USER rulings (verbatim)
- 2026-11-20: "I'm okay with trying (a), but we will reserve judgement until we see the results. 20% extra is kind of painful"
- Adoption bar: every slower game must recover at `--depth 8 --budget-ms 0`, and every DECK must be net <= 0
  ("+1 is not neutral. 0 is neutral.").
- Do NOT adopt; do NOT push the shared branch (parent does that).

## Done
- bfac80e72: the lever (heurarm UO_BUDGET_PRICE default OFF, UO_BUDGET_CUT default ON under price, MTG_UO_BUDGET_PRICE_W
  default 16, unit site unlock_order, census u_unlock_order, MTG_UO_PRICE_STATS diag, test hooks, 2 unit tests).
  Unit tests 3776/3776.
- W=16 calibration (doc section "Pricing the walks").
- d8 b0 inertness: Jumbo gi 719500-719599, arms on/pc/p 100/100 identical (digest, turn, units).
- d71fdf0c1: doc section with the above; A/B section says QUEUED.

## Running
- A/B: `flock claude_box.lock run_batch.sh ab.json ab 32 ...`, flock PID 3722194, detached with setsid (re-queued 13:01 UTC, 4th waiter behind
  the mana-clean gate PID 3457225 and three other waiters). Output `/workspaces/MagicDeckTester/build/scratch/claude_jumbo_uo_budget/ab/`
  (batch.out, batch.err, logs/*.wins); DONE marker `ab/DONE` (rc=...). Binary snapshot `mtg.w16` (bfac80e72).
  Check the `[batch] heartbeat` in ab/batch.err within 10 min of its start (should be ~32/32).

## Remaining, in order
1. When ab/DONE appears: `python3 logs/jumbo-uo-budget/compare.py /workspaces/MagicDeckTester/build/scratch/claude_jumbo_uo_budget/ab`
   (per deck: faster/slower/net vs `on`, units, CPU vs on, CPU vs off).
2. `python3 logs/jumbo-uo-budget/mkrec.py <ab dir> <ab dir>/../rec.json pc,p` -> d8 b0 re-run of every slower game
   (arms on, pc, p). Run via the lane: `BIN=.../mtg.w16 scripts/box_lane.sh --slots 4 --cap-gb 4 -- logs/jumbo-uo-budget/run_batch.sh <rec.json> <outdir> 4`.
   Recovered = d8 b0 turn <= the `on` arm's budgeted turn (rec.slower.json holds it).
3. Fill the A/B table + recovery into the doc section (replace "QUEUED"), commit, push wip branch.
4. Report: lever + commit, W=16 and why, inertness, per-deck table, recovery, recommendation.

## Update 2026-11-20 ~18:20 UTC (successor agent): ALL REMAINING STEPS DONE
- A/B finished 16:23 rc=0; compare output build/scratch/claude_jumbo_uo_budget/compare.txt.
- Per deck vs on: Jumbo pc/p net +2 (gi 722372 8->9, 727984 4->5), Bruna pc/p net +1 (gi 720338 6->7); rest 0.
- Jumbo CPU/game: off 179, on 213 (x1.192 vs off), pc 197 (x0.924 vs on), p 201 (x0.946 vs on).
- d8 b0 (rec/): all 3 recover, the arms are byte-identical.
- Doc filled in; the verdict is that neither arm meets the net <= 0 bar. Nothing was adopted; the shared branch was not touched.
