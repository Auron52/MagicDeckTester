# p18j-int handoff / resume file (2026-11-20 ~22:35 UTC, agent 3 stopping: parent wrap-up)

Worktree `.claude/worktrees/p18j-int`, branch `wip/p18j-int` (pushed). Rebased 2026-11-20 onto origin c86a86333. The shared
branch is NEVER pushed from here. Scratch: `/workspaces/MagicDeckTester/build/scratch/p18j-int/` (tools/, AB1-AB3/, ESC/).

## Done (commits)
- 9b4d6393b doc: AB2 analysis + escalation path-2 shares; AB1's mode-5 diagnosis corrected (trace artifact).
- 6d430e300 src (measurement only, built + units rc 0): deck-tagged MTG_P16_ORACLE lines; mode-3 arm key `_pwe1`;
  b1-b3 [p16] H records carry leaf units.
- be35a6a6a doc: AB3 (oracle ceiling, mode 3 on bare decks, walk-edge gap in mode 3).
- No deck flipped; no tiers run (parent: deferred to a later session). No jobs running.

## Key measurements (docs/design/sparse-heuristic-ladder.md, "Measurements")
- AB2: sparse (mode 4 + walk-edge fix) quality-even on all 7 bare decks, units 0.998-1.011 (no saving); fix alone +4 Maze.
  Sea sparse 0.943 units at +1 (the same game heur loses). Boars: mode 3 best (heur +7, sparse +2 at 1.2x units).
  sg5: Shadowborn +8, Maze +3, Martyr +1, units 0.976-1.000.
- Superseded rollouts live in passes 1-3 (mode 5 runs those exactly); the leafless trees do NOT grow (W_k 0.15-0.95x the
  rollout pass's tree) -> the "early-win cutoff" has nothing to cut; not built.
- AB3 oracle ceiling (perfect K, quality 0 / Maze +1): units Sea 0.721, Hydras 0.850, Shadowborn 0.910, Stuffy 0.910,
  Martyr 0.919, Merfolk Turns 0.935, Mill 0.968, Maze 0.980.
- AB3 mode 3 on bare decks: units 1.02-1.70x; better quality on Martyr -5, Shadowborn -14, Stuffy -6, Sea -2; Maze +118
  (edge roll: -91), Mill +30 (edge roll: +8).
- Escalation path 2: 18.4 % of units on the 7 decks reaching it (10.4 % of 16); 92.9 % of its leaf units no-win, 72.9 %
  of its units useless -- known only after rolling out.

## Remaining, in order (next session)
1. USER answers to Q1-Q3 below.
2. Any flip: rebase onto origin (never during a build), ./build.sh, smoke + regression --both under BOX.lock, per-game
   audit, --accept --deck=..., check_gt_logs.py, commit per deck, push wip/p18j-int.

## Open USER questions
- Q1: the vision's ceiling on the bare decks is the oracle above (2-28 % units); a real K chooser pays for the foresight
  out of it. Worth building only for Sea / Hydras (15-28 %) and maybe Shadowborn / Stuffy / Martyr (8-9 %)? Mill / Maze
  (2-3 %) have nothing to bank.
- Q2: mode 3 stays on Boars. Mode 3 also plays better on Martyr / Shadowborn / Stuffy / Sea, but at 1.16-1.33x units:
  a quality-for-cost trade, the USER's call.
- Q3: the walk-edge gap costs mode 3 dozens of games where it bites (Maze +118 -> -91, Mill +30 -> +8 with `_pwe1`).
  Extend engine-wide (moves GT on ~30 decks; its own A/B)? Boars / Morph / Spirits under `_pwe1` are not measured yet.

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
