# p18j-int handoff / resume file (2026-11-20 ~19:00 UTC, agent 3)

Worktree `.claude/worktrees/p18j-int`, branch `wip/p18j-int`. REBASED 2026-11-20 onto origin c86a86333. The shared branch is
NEVER pushed from here; push wip/p18j-int only. Scratch: `/workspaces/MagicDeckTester/build/scratch/p18j-int/` (tools/,
AB1/, AB2/, AB3/, ESC/, rp/).

## Done
- AB1 + AB2 analysed: docs/design/sparse-heuristic-ladder.md "Measurements" (AB1, walk-edge gap, AB2, escalation path 2),
  committed 9b4d6393b. Headlines: walk-edge fix +4 Maze (0 losses); NO mode meets the bar on any deck in AB2; AB1's
  mode-5 "35-79 % skipped / leafless trees grow" was a TRACE ARTIFACT (b1-b3 H records leaf=0); the superseded rollouts
  sit in passes 1-3 (mode 5 runs them exactly), so the "early-win cutoff" (Q1 default) has nothing to cut: NOT built.
  Escalation path 2: 18.4 % of units on the 7 decks that reach it; 92.9 % of its leaf units no-win, 72.9 % useless.
- tools: abreport.py now reads cpu= (was wall ms); new escshare.py, supdepth.py, wtree.py, pairdec.py, mkoracle.py.

## UNCOMMITTED src (src/ai/TurnSolver.cpp; commit once chain4's build passes)
- MTG_P16_ORACLE lines may carry a deck tag (measurement only); mode-3 lookahead arm key `_pwe1` (walk-edge roll + window
  cut, A/B only, default off); b1/b2/b3 [p16] H records carry their leaf units (print-only).

## Running (2026-11-20 18:50 UTC)
- chain4 PID 1807469 (`tools/chain4.sh`), queued on BOX.lock behind provreg build_units.sh and alch-t3 chain7: build ->
  `mtg_d3` -> units (`unit3.log`, rc in AB3.status) -> ONE pooled batch `AB3/manifest.json` (198 jobs, 39,600 games, AB2's
  seeds 17,100,000+10,000b x 6 x 200): 7 bare decks x {live, oracle, sg3, sg3_pwe1} + Sea x {heur, live, oracle, sg3,
  sg3_pwe1}; env MTG_P16_TRACE=1 MTG_P16_ORACLE=oracle_ab2.txt (from AB2's live/heur trace) MTG_P16_ORACLE_ARM=/oracle/.
  Status `AB3.status`, DONE marker `AB3.DONE`. Check `[batch] heartbeat` in AB3/trace.err within 10 min of "batch".

## Remaining, in order
1. AB3: sanity -- live digests == AB2 live (else the oracle file does not apply); oracle coverage (oracle arm decisions
   with W passes). `python3 tools/abreport.py AB3 live` (Sea: `AB3 heur`). Oracle = the CEILING of skipping every
   superseded rollout with perfect K. sg3 / sg3_pwe1 = mode 3 on the bare decks (a flip candidate if quality even AND
   units/CPU lower; Maze needs _pwe1 -- that is a new arm key, flipping with it means making it the deck's key).
2. Fill the doc ("AB3"), then flip any deck that meets the bar (rebase onto origin first, never during a build; ./build.sh;
   smoke + regression --both under BOX.lock; per-game audit; --accept --deck=...; check_gt_logs.py; commit per deck; push
   wip/p18j-int only). Otherwise flip nothing.
3. Surface the USER questions below (with AB3 numbers).

## Open USER questions
- Q1 (revised): no shape meets the vision on bare decks; the waste is in passes 1-3 and is partly pre-pay. AB3's oracle
  gives the ceiling per deck. Proposal depends on it.
- Q2: mode 3 stays on Boars (AB2: heur +7 / sparse +2 vs mode 3). Morph / Spirits not re-measured.
- Q3: walk-edge gap in every leafless shape -- not authorized; `_pwe1` measures it for mode 3 only (arm key).
- Sea Creatures: sparse 0.943 units but +1 game (the same game heur loses: bare ladder vs model) -> not flipped.

## USER rulings (verbatim, from the parent brief)
- "I don't want the heuristic ladder for any deck."
- "finish P18 ... use a better name like sparse heuristic ladder"
- "The idea is that I want to actually skip some of the rollouts we don't need. I don't see an advantage to doing all of them."
- "Delaying the rollouts to the end is also beneficial, but does not encompass my full design."
- "My preferred vision would be that it uses less rollouts when more certain and more when less certain. So, it would be closer to the heuristic rollout, but would still bank savings." / "There shouldn't be cases where it is so uncertain it just runs the ladder." / "It should at least cut out a significant chunk of it."
- "I was primarily just thinking about 1 here. 2 is possible if you can come up with lossless ideas for it, though."
- "I guess being no worse on average is okay for the bar here, since we aren't impacting the search. This is just the rollouts which already make mistakes. However, this rule should apply to just your prunes in 2 in isolation."
- "And, to be clear, it does need to be better on some axis. In this case we expect to improve performance. If the quality is even and performance better, that is sufficient."
- Escalation: "Good question on escalation. That is a maybe and only really a good idea if we can drop a lot of lines that we have shown will not win." -> MEASURE ONLY (share of the fallback's lines provably non-winning), do NOT build.
- 2026-11-20 mode-3 decks (Boars, Morph, Spirits): fold into sparse IF AB1 shows sparse at least as good as their mode 3 ("3. Sure."). AB1: NOT met.
- 2026-11-20 Sea Creatures' emulated ladder approved as interim ("4. Sounds good."); Sea is a sparse candidate like any deck.
- Sea Creatures / Merfolk Turns / Boars adoptions landed (df5eefe3a); re-measure them on the rebased tree before flipping.

## Measurements: see docs/design/sparse-heuristic-ladder.md (AB1 tables).
