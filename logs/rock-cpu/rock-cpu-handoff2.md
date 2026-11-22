# Rock CPU handoff 2 (agent rock-cpu 2, 2026-11-22 ~12:35 UTC) -- written for a Codex agent with no context

Goal: measure lossy / priced CPU cuts of the rock rung (`MTG_LINE_KEEP_ROCK`, line-keep rung in `LineReducerKeepHold`,
src/ai/ManaPayment.cpp) and REPORT A MENU (per deck: CPU saved vs quality change, with a recommendation). ADOPT NOTHING,
flip no default. Earlier context: `logs/rock-cpu/rock-cpu-handoff.md` (step-1 table, scripts) and
`docs/design/rock-in-plan-2026-11-19.md` (sections "Current per-deck CPU of the rung" onwards; results are appended there).

## USER rulings (verbatim, relayed)
- "Sure, we can do your recommendation on Rock Niv." (fresh per-deck CPU measurement, then a cut that gives up some quality)
- "I'm mostly concerned with searched."
- "I don't like leaving gaps" (a d8 b0 replay stopped by the unit cap is re-run at an escalating budget).
- "Sounds good. Let's turn it on if the unwon looks okay. We should indeed support this."
- 11-19: cut CPU; the suite's 3x limit is not a CPU licence.
- 12:35: "Claude usage is at 5%, so keep running until it runs out, but keep a Codex handoff current."

## Rules for this work
- Branch wip/rock-cpu only (worktree `.claude/worktrees/rock-cpu-dev`). NEVER push phase-1-2-deck-analyzer, never join the
  push queue. Rebase, never merge. References are commit-only.
- Big runs: one pooled `--batch` under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock`, MTG_RSS_CAP_GB=40,
  MTG_PGO=0. Small runs: `bash scripts/box_lane.sh --slots N --cap-gb G -- <cmd>`, N <= 2 (lane is backlogged).
- Launch with setsid nohup; wait with `kill -0 <real pid>` or a DONE marker, never pgrep -f; no `timeout`.
- Bar: every slower game recovers at `--depth 8 --budget-ms 0`; only CLEAR clairvoyance may stay unrecovered
  (test with MTG_NC_SEARCH=1).

## Done
- Step 1 per-deck table + 3 default-OFF cuts: commit b9013484b (handoff commits 22a9f3297, 9c01aee92).
- d8 b0 replays of the 9 step-1 slower games: ALL RECOVER (equal win turn on/off; none hit the unit cap). In doc.
- perf_base100 (100 searched / 300 d0 games per deck, instr on/off, start-up 416 M subtracted): niv 1.048 / 1.041 d0,
  extraturns 1.039, hinata 1.024 / 1.038 d0, ALL 1.020 / 1.009. Table in doc.
- Step 1 of this agent: `mtg.dev` with all new flags off is BYTE-IDENTICAL to `mtg.base` (64/64 m1 jobs). In doc.
- Step 2 calibration (`ab2/calib.txt`): rung time niv 88% in horizon playouts; extraturns / hinata ~97% / 86% in the
  search window (so playout cuts can only help Niv). MTG_LKR_PRICE_MILLI 1x = **85**, 0.5x = **43**. In doc.
  (doc edits since 9c01aee92 are committed with this handoff.)

## Running (all paths under /workspaces/MagicDeckTester/build/scratch/claude_rock_cpu/ab2/)
Chain `run.sh`, real PID **860618**, holds the box lock since 12:22 UTC. Progress lines in `status`; `DONE` / `STOPPED`
markers. Phases: identity + stats (done) -> batch A (`A.json` -> `A.out`, `A.err`, `logs_A/`; arms def, poff
=MTG_LKR_PLAYOUT_OFF, pnt =MTG_LKR_PLAYOUT_NO_TABLE, price =MTG_LKR_PRICE at MILLI 85; started 12:24, ~45-60 min, 32/32 busy)
-> batch B (`B.json` -> `B.out`, `logs_B/`; arm price05 = MTG_LKR_PRICE at MILLI 43, same seeds) -> perf instruction
counts (`perf/`, 1 thread per (deck, arm) process, searched seeds 796000) -> `DONE`.
Seeds: searched base 780000 (niv 3000 games, extraturns / hinata 1500, stompy / stuffy / merfolkturns / dragons /
galadriel / fivecolour 500), d0 base 790000 (500 each). Scripts copied to `logs/rock-cpu/` (run.sh, mk2.py, calib.py,
identchk.py, perf2.sh).

## Progress since: batch A compared (results in doc), d8 b0 replays queued in the lane: manifest ab2/d8_ab2.json, wrapper `box_lane.sh` PID 914899, out `d8_ab2.out`, logs `logs_d8_ab2/`, marker `d8_ab2.done` (MTG_LKR_PRICE_MILLI=85 set for the price check jobs). Step 1 below is DONE (cmp_*.txt in logs/rock-cpu/), except price05 (needs batch B).

## Progress 13:30 UTC: chain DONE (batch A, batch B, perf); results + interpretation in the doc ("Batch B ...").
Finding: the price rounds up per call, so MILLI 85 and 43 both charge ~1 unit per call (price NOT measured at 1x / 0.5x);
fix = carry the residual on the decision frame, then re-measure. Remaining: d8_ab2 replays (lane, marker
$S/d8_ab2.done), then write the menu (step 4 below).

## Progress 13:30 UTC (2): price residual FIX committed (SearchBudget::LkrMilliResidual, carried per decision budget).
Chain `ab2/run2.sh`, real PID **921057**: sleeps 210 s (9p clock), takes the box lock, builds rock-cpu-dev at full core
(`build_dev2.log`), stages `$S/mtg.dev2`, identity vs base (`ab2/ident2.txt`; must say 0 differ), batch C = arm pricec
at MILLI 85 (`C.out`, `logs_C/`), batch D = arm pricec05 at MILLI 43 (`D.out`, `logs_D/`), searched only, same seeds
as A, then perf counts (`ab2/perf2/`, arms pc85 / pc43, seeds 796000). Progress `ab2/status2`; markers `ab2/DONE2` /
`ab2/STOPPED2`. Compare vs A's def: `cat ab2/A.out ab2/C.out ab2/D.out > ab2/ACD.out; mkdir -p ab2/logs_ACD;
ln -sf $S/ab2/logs_A/* $S/ab2/logs_C/* $S/ab2/logs_D/* ab2/logs_ACD/; python3 cmp.py ab2/ACD.out ab2/logs_ACD def pricec --list`
(and pricec05). perf: instr(pc85__k__s.perf)-416153462 over instr(ab2/perf/def__k__s.perf)-416153462.
Then d8 b0: the price is inert at b0, so its slower games recover by construction (the d8_ab2 manifest includes two
price games as a check).

## Progress 14:50 UTC: d8_ab2 DONE, all recovered (doc). run2.sh still waiting for the box lock (since 13:31).

## Next, in order
1. When `status` shows `batchA`: `cd .../claude_rock_cpu && for a in poff pnt price; do python3 cmp.py ab2/A.out ab2/logs_A def $a --list > ab2/cmp_$a.txt; done`
   For price05: `cat ab2/A.out ab2/B.out > ab2/AB.out; mkdir -p ab2/logs_AB; ln -sf $PWD/ab2/logs_A/* $PWD/ab2/logs_B/* ab2/logs_AB/;
   python3 cmp.py ab2/AB.out ab2/logs_AB def price05 --list > ab2/cmp_price05.txt`.
   Expect d0 byte-identical for every arm (cuts inert at d0 by construction): verify (faster = slower = digest-only = 0).
2. When `DONE`: instruction ratios: `python3 perfsum.py ab2/perf def poff pnt price price05` (subtract 416,153,462
   start-up instructions per process from both sides before taking ratios).
3. d8 b0 replays of every searched slower game, both arms (def + the cut): `python3 mkd8.py ab2/cmp_<arm>.txt ab2/d8_<arm>.json 780000 790000 '{"def":{},"<arm>":{<flags>}}'`.
   The price arms are INERT at b0 (DecisionBudget() is nullptr at an unlimited root), so their slower games recover by
   construction (both arms identical); only poff / pnt need real replays. Pool poff + pnt into ONE manifest and run it in
   the lane: `setsid nohup bash scripts/box_lane.sh --slots 2 --cap-gb 8 -- $S/lanebatch.sh d8_ab2 $S/mtg.dev $S/ab2/d8_ab2.json &`
   (marker `$S/d8_ab2.done`). Escalate the budget if a game is abandoned at the unit cap.
4. Write the menu in the doc and report it: per deck, CPU ratio (batch CPU and perf instructions) and searched net turns /
   slower games for each cut, then d0; recommendation. Note: poff / pnt are predicate gates in the horizon playout (they
   change b0 play too; lossy), price is the budget-priced shape (inert at b0).
