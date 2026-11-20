# p18j-int handoff / resume file (2026-11-20 ~16:10 UTC, agent 2 stopping at the context rule)

Worktree `.claude/worktrees/p18j-int`, branch `wip/p18j-int`. REBASED 2026-11-20 14:2x onto origin/phase-1-2-deck-analyzer
c86a86333 (clean, 20 commits replayed). The shared branch is NEVER pushed from here; push wip/p18j-int only.
Scratch: `/workspaces/MagicDeckTester/build/scratch/p18j-int/` (tools/, AB1/, AB2/, ESC/, rp/ single-game repros).

## Done
- AB1 (102,370 games, EXIT 0) analysed: tables + findings in docs/design/sparse-heuristic-ladder.md "Measurements / AB1".
  Headline: mode 4 skips 0-4 % of rollouts on bare decks; mode 5 skips 35-79 % but units/CPU ~1.0 (incumbent-free
  leafless trees eat the saving); Maze's End sparse +10 / sg5 +12; mode 3 beats sparse on Boars (+9) / Morph (+8) ->
  mode-3 fold NOT met; emulated decks: sparse = bare ladder, +33 Landfall / +12 Niv -> not flipped.
- Maze root cause (rp/maze84, MTG_FSW_TRACE=1 MTG_FSW_TURN=5): the SAME breakpoint node-child at T5 d1
  (Maze's End;Manabond,Maze's End, cont=[k4:Maze's End,k30:Maze's End]) scores tail=5 under the rollout leaf and tail=9
  under the walk's constant leaf -> leafless pass 2 "refutes" the T+1 win, pass 3 commits T+2.
- Fix (UNCOMMITTED until built + checked): `p18::t_walk_edge` -- inside a walk pass, a leaf at or before the window's
  last turn is rolled out (MTG_P18_WALK_EDGE_ROLL, default on; affects only modes 4/5, which no deck ships).
  Diagnostics: MTG_P18_WALKDIAG (bit1 fresh tables, bit2 unbudgeted leafless vs rollout re-run, prints [p18diag]);
  MTG_ESC_P2_MEAS=1 prints [escp2] per deck at exit (escalation PATH 2 decisions/units/leaves/no-win leaves).

## Running / queued (2026-11-20 ~16:10 UTC)
- tools/build_diag.sh DONE (bd.rc 0, 15:52): mtg_d1 = the walk fix + diagnostics, WITHOUT the `_pwe0` arm key.
- **chain3 QUEUED** (PID in build/scratch/p18j-int/chain3.pid; detached, waiting on BOX.lock behind the alch-t3 tiers6
  smoke+regression run): holds the lock across incremental `./build.sh` (adds `_pwe0`) -> `mtg_d2` -> unit tests
  (`unit2.log`, rc in AB2.status) -> ONE pooled batch `AB2/manifest.json` (260 jobs, 47,200 games, MTG_P16_TRACE=1
  MTG_ESC_P2_MEAS=1). Status lines `AB2.status`; DONE marker `AB2.DONE` ("EXIT 0", or "BUILD FAIL"). CHECK the
  `[batch] heartbeat` in AB2/trace.err within 10 min of the "batch" status line. Nothing wakes an agent when it ends.
  AB2 contents:
  * ESC: 16 escalation / learned-hybrid decks (Bruna, Creature Giving, Extra Turns, Kitty Equipment, Gods, Landfall
    Elementals, Prevent Damage, Vampires, Anti-Lifegain, Breaching Dragonstorm, Burn, Critter Lifegain, Five Colour,
    Hinata, Knights, White Knights) x 2 x 50 games, live only, seeds 18,000,000 (+10,000). Read
    `grep '^\[escp2\]' AB2/trace.err`: per deck decisions, p2_units, leaves, leaf_units, nowin_leaves/units (rollout
    leaf returned no win at all), useless_leaves/units (leaf result > its cutoff: could not change the pass),
    decisions_no_win. Report: nowin_units / leaf_units, useless_units / p2_units, and p2_units as a share of all
    units (sum used from the [p16] lines of the same jobs). MEASURE ONLY (USER).
  * AB2: bare Hydras, Martyr Lifegain, Maze's End, Merfolk Turns, Mill, Shadowborn Apostle, Stuffy Doll with arms
    live / sparse (mode 4 + fix) / sparse_pwe0 (mode 4, fix OFF: the isolation A/B of the fix) / sg5 (mode 5 + fix);
    Sea Creatures (now emulated + model) and Boars (mode 3, d6b40) with live / heur / sparse / sparse_pwe0 / sg5.
    6 x 200 games, seeds 17,100,000 + 10,000 b. Analyse: `python3 tools/abreport.py AB2 live`, `... AB2 sparse_pwe0`
    (the fix alone), `python3 tools/decstats.py AB2`, `python3 tools/firstdiv.py AB2 <deck> live sparse` per loser.
  NOTE: the rebased tree plays the AB1 Maze games differently (17000084/7/71 equal across live/sparse/no-fix on mtg_d1),
  so the fix can only be judged from AB2, not by re-running AB1's losers.

## Remaining, in order
1. When AB2.DONE: confirm units passed (AB2.status "units rc=0"), then analyse as above and fill the doc
   ("Measurements": AB2 + escalation share).
2. Root-cause any remaining sparse/sg5 loser (firstdiv), fix the MODE; every slower game must recover at
   --depth 8 --budget-ms 0 (single-game repro, tools/repro.sh <bin> g1 <deck> <arm> <base> <gi> <out> --depth 8 --budget-ms 0).
3. Flip only decks where sparse (fixed) net <= 0 AND units < 1 vs live (expected few or none: mode 4 skips ~0-4 % of
   rollouts). Mode-3 decks stay on mode 3 (AB1: fold condition not met). Emulated decks (Landfall, Niv, Melira, Modern RW
   Burn, now Sea) stay emulated unless AB2 says otherwise for Sea. For any flip: `{"value_play":{"ladder":"sparse"}}`
   keeping the deck's other value_play keys; smoke + regression `env -C <worktree> bash test/regression.sh --both`
   (under the BOX lock), per-game audit, `--accept --deck=...`, `python3 test/check_gt_logs.py`, commit per deck,
   push wip/p18j-int only.
4. Surface to the USER (open questions below).
5. (2)-type prunes only if time allows.

## Open USER questions (with numbers; surface, do not block)
- Q1: Mode 4 ("sparse" as built) skips only 0-4 % of rollouts on every bare deck (census: 2.5-29 % of their units are
  superseded rollouts) because a miss runs the whole ladder -- the case the USER ruled out. Mode 5 (miss -> priced
  sampled gate) skips 35-79 % but units/CPU stay ~1.0 (0.93-1.02 / 0.91-1.03): leafless passes have no incumbent and
  their trees grow by about what the rollouts saved (Mill per decision: tree 639 -> 7,976 units, rollouts 9,362 ->
  1,968). Should "sparse" become mode 5 (after its Hydras +4 refusal losses are fixed), and is a rollout cut with no
  unit/CPU saving acceptable, or must the leafless trees get an incumbent bound first?
- Q2: Mode-3 fold not met (sparse vs mode 3: Boars +9, Morph +8, Spirits +1; mode 3 vs heur: -12, -8, -1 at 0.70-0.80x
  units). Keep Boars/Morph/Spirits on mode 3? (default taken: yes.)
- Q3: The walk-edge gap (a constant leaf standing for the rest of a window turn) exists in EVERY leafless shape (FIT
  probes, escalation probes, emulated warm-ups, mode 3's lookahead), not only the walk. Fixed only in the walk here;
  extend engine-wide (moves GT on ~30 decks; needs its own A/B)?

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
