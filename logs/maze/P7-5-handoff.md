# P7-5 handoff (Maze's End performance) -- 2026-11-19 ~05:20 UTC

Worktree `.claude/worktrees/maze-p7`, branch `wip/maze-p7`. Scratch: `build/scratch/maze/p7/` (tools in `tools/`).
Binaries: `mtg.p7f` = fc204f2f8 ((a)+(b)); `mtg.p7g` = edd113f3b ((c) + MTG_BP_WAVE_SHAPES); `mtg.p7h` = the node-class
diagnostic commit below, once chain10 builds it.

## Done
- **258cf0d38 = INTEGRABLE (a)+(b)** (src = fc204f2f8, `MTG_MAZE_CASTDOM_WINDOWS` + `MTG_MAZE_CASTDOM_PERPLAN`):
  smoke GT ebbc388b0 + regression GT 258cf0d38 accepted (`--deck=mazesend`; check_gt_logs consistent). Regression:
  365 unchanged, 3 mazesend* moved (d5 s2002 5.6685 -> 5.6522; d3 s2002 / d5 s3003 play only), 3 faster / 1 slower:
  d5 s3003 gi14 6 -> 7 (seed 3017, gi 14). d8 b0 (abandon 200M, `d8r8/`): BOTH arms T7, same digest c6e716007df49f7c
  (base 6.77M units, (a)+(b) 5.86M) -> base's b20 T6 beat its own d8 b0; recovers. Windows CI 44427718044 SUCCESS.
  Pushed to origin/wip/maze-p7.
- **edd113f3b (c)** `MTG_MAZE_CASTDOM_KEEP_A_ONLY` (default ON): `CastDominanceKeep` returns a mask
  (kCastDomKeepA/B); the Heap Gate / Gateway Plaza keeps block fold A only, the Maze's End keep blocks both. Units
  3661/3661. Plus `MTG_BP_WAVE_SHAPES=1` diagnostic (print-only). NOT pushed yet.
- **ab5 (c) A/B DONE** (`ab5/`, 3380 jobs, 13:54 wall, fresh seeds 171001-176020; base = KEEP_A_ONLY=0):
  searched net **-5** (d5b20 600: 2 better / 1 worse, -1; d3b10 300: 1 better, -5; d1b1 200: 0/1, +1; d1b3 200: 0/0);
  d0 300/300 identical digests; units -1.0% d1b1, -1.1% d1b3, -0.4% d3b10, -1.5% d5b20; b0 (40 + 20 games): 26 games
  cheaper, 0 dearer, one base abandon (s176001 d5 g3, >10M) finished T5 at 9.65M; b0 total ex that game -3.7%.
  Two slower games owe d8 b0: **d1b1 gi118 (seed 173119) 6->7**, **d5b20 gi528 (seed 171529) 6->7**.

## Running (queued on the heavy lock)
- `d8c/` (hbatch PID 1228048, `mtg.p7g`): the two slower games at d8 b0 (abandon 200M) + their own cell, both arms.
  Marker `d8c/DONE` (a STALE DONE from an older run was deleted 05:20 -- check `run.out` is non-empty). Report:
  `grep played= d8c/run.out`. Bar: keepA d8 b0 <= base's b-cell turn, or = base's d8 b0.
- `chain10.sh` (PID 1183231): build (MTG_BUILD_JOBS=12) of the committed node-class diagnostic -> `mtg.p7h`, then
  `shp2/` = 30042 gi1 d5 b0 abandon 20M with `MTG_BP_WAVE_SHAPES=1`. Log `chain10.log`, marker `chain10.DONE`.
  Read `grep '\[bp-shapes\] node' shp2/run.err`.

## d8c VERDICT (landed 05:4x UTC): BOTH SLOWER GAMES RECOVER
- d1b1 gi118: d8 b0 both arms T7, same digest 79807fdd66207090 (base 327K units, keepA 282K) -> base's b1 T6 beat its
  own d8 b0.
- d5b20 gi528: d8 b0 both arms T6, same digest e4f6fdfc7a5f1b4f (base 920K, keepA 730K) -> keepA recovers to base's T6.

## Remaining, in order
1. d8c recovered (above), so: `tools/chain11.sh` (smoke + regression on the (c) binary; needs clean src and
   build/Release == mtg.p7h), accept `--deck=mazesend` (both tiers), check_gt_logs, push wip/maze-p7, CI watch
   (`--interval 60`). (c) is then integrable.
2. Turn-2 fan-out (30042 gi1, `shp1/run.err`, MTG_BP_WAVE_SHAPES): **96.5% of the 8.2M wave applies are at T6 d1 --
   the LAST ply of the depth-5 LEAFLESS (lazy-leaf, constant-leaf) ladder pass.** Slots are the Guild Summit draw,
   Sylvan Scrying and Maze's End fetch breakpoints (bases `land=- [Guild Summit]`, `land=<Gate> []` with Summit in play,
   `[Sylvan Scrying]`), each with 30+ ranked continuations ("play land X after the draw", 29% of applies at rank >= 30);
   45% of T6 d1 applies are dup-state, 13% take an empty continuation. In a constant-leaf pass a d1 node's children all
   score max_turns+1 unless they win THIS turn, so every non-winning continuation is pure waste there.
   PROPOSED SOUND FOLD: at a non-root node with depth == 1 and `FsNextLeafConstant(state)`, ask the provider for a
   PROOF that no win is possible this turn and return {max_turns+1, {}}. Valid at every budget where the leaf is
   constant (not unbudgeted-only). Maze's End bound: win needs Maze's End untapped + {3} and >= 9 distinct Gates before
   the fetch; G (CountDistinctNamedLandsWithSubtype) + new-gate entries this turn (drops incl. Explorations, Charm put,
   Amulet+ME replays; pool = new Gate names in hand + tutors) + 1 < 10. GIVE UP (no proof) with Guild Summit, Manabond or
   Burgeoning on the battlefield / in hand (unbounded draws / end-step dump / off-main puts). shp2's `node` table sizes
   it: NOWIN = cut, GIVEUP = not cut. If GIVEUP dominates (Summit is in 30042's hand from T2), the bound needs a Summit
   draw term (draws <= entries + untapped Gates at Summit's ETB, each draw worth <= 1 entry) -- validate soundness with a
   census that flags any d1 node the bound called NOWIN that then found a win (must be 0) before shipping.
   **shp2 RESULT (chain10 done; `mtg.p7h` = 7eaf3a12f built, exit 0): the naive bound cuts NOTHING in 30042 --
   100% of the 7.92M T6 d1 applies are GIVEUP, every top row with Guild Summit ON THE BATTLEFIELD (`Sb`).** Typical
   rows: G = 4-6 distinct Gates, add <= 0-3, hl = 0 lands in hand, Burgeoning often in hand (`Bh`), Manabond absent.
   Without the Summit give-up most of these rows would be NOWIN (e.g. G=5 add<=3: 5+3+1 < 10). So the fold needs a
   SUMMIT-AWARE bound. Sketch (to be proved properly before building): Summit draws one card per Gate ENTRY, a drawn
   card is either a land (needs a drop) or an enabler (Exploration +1 drop, Charm +1 put), so with D <= entries the
   new Gates obey 2*new <= drops0 + hl + tutors + D and D <= new, i.e. new <= drops0 + hl + tutors (+ Summit's ETB
   taps when Summit is cast this turn); a Manabond reachable by draw (in library) breaks it via the end-step dump and
   must be handled (or give up only when Manabond can be drawn AND cast AND a dump could add Gates). Burgeoning is
   over-conservative as a give-up for a THIS-TURN win (it puts only on the opponent's land play, outside this turn)
   -- drop it from the give-up list. Validate with a census flagging any NOWIN d1 node that then wins (must be 0).
3. Handoff item 4 of P7-4 (per-node playout cost: profile the greedy subset walk) still open.

## USER rulings (verbatim)
- "I suppose P7's prune premise is fine." (the dominance-fold premise is SIGNED OFF)
- "3-8 We should address this at some point or we won't be able to do the value-leaf. I'll leave the timing to you.
  3-9 This means we probably need to work more on performance to start which fits with 3-8. We'll need more effective
  prunes to be applied. Higher budget is not out of the realm of consideration, but only after we have exhausted other
  avenues."
- "It would be nice if there was no extra branching given how easy the mana is in this list."
- Fetch options: all searchable, TARGET set pruned (not fetch-or-not), except: "Though, if there is idle mana we should
  use it." / "And that is fine, because not fetching would be dominated, but this would be assuming we can replay it."
  (P5d-2 owns the fetch rules -- don't edit TapTutorPutEval / TapTutorPutCandidates.)
- Box rules (USER 2026-11-18): one heavy process machine-wide under `flock /workspaces/MagicDeckTester/build/heavy.lock`;
  `--threads $(nproc)`, default cache budget, `MTG_RSS_CAP_GB=20`; builds `MTG_BUILD_JOBS=12 ./build.sh` inside it.
  Push only wip/maze-p7, never the shared branch.
