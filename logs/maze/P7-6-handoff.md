# P7-6 handoff (Maze's End performance) -- 2026-11-19 ~06:00 UTC

Read large files by grep + line ranges only (TurnSolver.cpp is 137K lines, DecisionProviders.cpp 69K).
Worktrees: `.claude/worktrees/maze-p7` (branch `wip/maze-p7`, (a)+(b)+(c)) and NEW `.claude/worktrees/maze-p7f`
(branch `wip/maze-p7f`, LOCAL ONLY, off 7eaf3a12f: the P7d fold, commit 0ac16214f, unbuilt when written).
Scratch: `build/scratch/maze/p7/` (tools in `tools/`). Binaries: `mtg.p7f` = (a)+(b); `mtg.p7g` = (c) edd113f3b;
`mtg.p7h` = 7eaf3a12f once chain10 builds it; `mtg.p7i` = the P7d fold once chain12 builds it.

## Done (unchanged from P7-5)
- **258cf0d38 = INTEGRABLE (a)+(b)** (`MTG_MAZE_CASTDOM_WINDOWS` + `MTG_MAZE_CASTDOM_PERPLAN`): smoke GT ebbc388b0 +
  regression GT 258cf0d38 accepted, check_gt_logs consistent, d8 b0 recovered, Windows CI 44427718044 SUCCESS.
- **edd113f3b (c)** `MTG_MAZE_CASTDOM_KEEP_A_ONLY` (default ON) + `MTG_BP_WAVE_SHAPES` diagnostic; 7eaf3a12f adds a
  print-only node class to that diagnostic. ab5 A/B: searched net -5, d0 300/300 identical, units -0.4..-1.5%, b0 26
  cheaper / 0 dearer. Two slower games owe d8 b0: d1b1 gi118 (seed 173119) 6->7, d5b20 gi528 (seed 171529) 6->7.

## Running (all queued on the heavy lock; ~13 waiters at 05:46 UTC, flock order is not FIFO)
- **d8c/** (hbatch PID 1228048, `mtg.p7g`): the two slower games at d8 b0 (abandon 200M) + their own cell, base
  (KEEP_A_ONLY=0) and keepA. Marker `d8c/DONE` -- check `d8c/run.out` is non-empty (a stale DONE was deleted
  earlier). Report `grep played= d8c/run.out`. Bar: keepA d8 b0 <= base's b-cell turn, or = base's d8 b0.
- **chain10** (PID 1183231, `tools/chain10.sh`): build of 7eaf3a12f in maze-p7 -> `mtg.p7h`, then `shp2/` = 30042 gi1
  d5 b0 abandon 20M with `MTG_BP_WAVE_SHAPES=1`. Log `chain10.log` ("build10 exit N"), marker `chain10.DONE`. If the
  build fails (7eaf3a12f's diagnostic was never compiled) fix it first. Read `grep '\[bp-shapes\] node' shp2/run.err`
  (NOWIN / GIVEUP / open per class; GIVEUP = Summit/Manabond/Burgeoning in hand or play).
- **chain12** (PID 1242083, `tools/chain12.sh`, out `chain12.out`, log `chain12.log`, marker `chain12.DONE`;
  STOPPED there = build or unit failure): builds maze-p7f (log `build12.log`) -> `mtg.p7i`, runs
  `mtg-test -tc='Maze*P7*'` (log `units12.log`), then the **ab6/** pooled batch (1644 jobs, `tools/mkab6.py`):
  * Maze arms: `base` = `MTG_MAZE_WINLESS_CERT` off (no certificate at all), `nofold` = cert on but
    `MTG_LASTPLY_NOWIN_FOLD` off (b0 only; isolates the WinlessDevelop closure, which the certificate also arms in
    unbudgeted play), `fold` = shipped default. Jobs: 30042 gi1 d5 b0 abandon 50M (3 arms) + `cen_x30042_g1`
    (census, abandon 20M); b0 d3/d5 screen subset (30001/30021/30041/30061 x gi0-4) + fresh 181001 gi0-19, abandon 10M;
    10 census b0 games (`cen_b0d*`); budgeted IDENTITY cells d5b20 200 / d3b10 100 / d1b1 100 / d0 50 (expect equal
    digests: Maze's End budgeted play has no constant leaf).
  * Cross-deck: Snow, Fungus, EDF, Gods (the other providers with ProvenWinlessThisTurn) at d5b20 / d3b10, 30 games,
    fold off vs on. Their sidecars are `ladder single, leaf none` (Gods: none), so their budgeted probe passes are
    constant-leaf: **the fold will likely MOVE their budgeted play** (sound, re-priced work). If so they need per-deck
    A/B (net <= 0) and their GT moves in the tiers -- report to the coordinator; do NOT scope the fold to Maze's End to
    keep GT still (CLAUDE.md: "Scope a change by where it is VALID, never by what keeps the GT still").
  * Report: `python3 tools/abrep.py ab6 base` (and `... ab6 nofold`); census lines `grep '\[lastply\]' ab6/run.err`
    (PROOF / CODE-1 = Guild Summit / CODE-2 = bound open / OTHER, nodes + units each; `UNSOUND` MUST be 0).
    Note census + fold counters are batch-global (census only from cen_ jobs, `folded=` only from fold arms).

## What P7d (maze-p7f, 0ac16214f) is
- Reuses the repo's ONE proof hook: `MazesEndProvider::ProvenWinlessThisTurn` (Certificate() now Implemented;
  `MazeWinlessBound` returns 1 proof / -1 Guild Summit / -2 bound open / -3 card outside the reviewed list; lever
  `MTG_MAZE_WINLESS_CERT`, default ON). Bound (DecisionProviders.cpp, grep `MazeWinlessBound`): D <= G0 + min(P, C)
  where P = open drops + Explorations in hand + Charm + Talon (hand, or Scrying->library) + Burgeoning copies + Maze's
  Ends in play (invalid with Manabond reachable), C = new-name Gates in hand + Scryings + Charm + Maze's Ends in play +
  (Amulet reachable ? re-entries : 0). Gives up with Guild Summit in hand/play.
- Engine: `lastply` namespace + site at the top of FSLineWin after the depth<=0 block (grep `LAST-PLY NO-WIN FOLD`):
  depth == 1, non-root (g_fsline_nest > 0), not rollout / cs-solver, no extra turns, `FsNextLeafConstant`, winlesscert
  enabled, `MTG_LASTPLY_NOWIN_FOLD` (slot, default ON) -> if certified return {max_turns+1, {}}. Census slot
  `MTG_LASTPLY_NOWIN_CENSUS` (re-searches the node, never cuts). Test seam `TurnSolver::LastPlyFoldProbeForTest`;
  unit test `test/unit/test_mazes_end_p7d.cpp` (bound arithmetic + leafless search identical on/off). DecisionProvider.h
  doc of ProvenWinlessThisTurn updated (this consumer applies at every budget).
- Side effects of implementing the certificate: label-scope sites (WinlessCertificateActive M1/M2) and
  WinlessDevelopActive (unbudgeted play, default ON) now also fire for Maze's End -> b0 Maze play may move (ab6's
  `nofold` arm measures that part).
- Fold is effectively inert in Maze's End BUDGETED play (no sidecar -> rollout leaf at every pass; the constant leaf
  appears only in the b0 lazy-leaf probe). Structural, not an unbudgeted gate: it fires wherever the leaf is constant.
- **Expected weakness (analysed, not yet measured):** 30042 has Guild Summit in hand from T2, so its T6 d1 nodes likely
  read CODE-1 (no proof). No sound counting bound survives Summit + Manabond/Exploration in the library (each Gate
  entry draws; draws can add permissions without limit up to library composition; shuffles after any fetch make
  draw order unknown). If the census shows Summit dominates, the only further cut is a heuristic/clairvoyant
  narrowing -> that is a USER decision: surface it with the census numbers, do not build it.

## Remaining, in order
1. d8c verdict (above). If both recover: run `tools/chain11.sh` after chain10's build lands (it requires clean src in
   maze-p7 and build/Release/mtg == mtg.p7h): smoke + regression on (c); accept `--deck=mazesend,mazesend2hg` in both
   tiers (inspect diff first; ACCEPT_ACK one line), `python3 test/check_gt_logs.py`, commit, push wip/maze-p7, CI watch
   in background (`gh run list --branch wip/maze-p7 --limit 3`; `gh run watch <id> --interval 60 --exit-status`).
2. chain10's shp2 sizing (diagnostic classification) -- record it.
3. chain12 / ab6: if build or units fail, fix in maze-p7f and relaunch. Then: UNSOUND must be 0; Maze budgeted cells
   identical; b0 net <= 0 and units saved; cross-deck movement decides whether Snow/Fungus/EDF/Gods need A/Bs.
   Every slower game at d8 b0 (abandon >= 50M) must recover. Then full unit suite, smoke+regression with the fold,
   scoped accepts for the decks that moved, rebase maze-p7f's commit onto wip/maze-p7 (rebase, never merge), push.
4. P7-4 item 4 (per-node playout cost: profile the greedy subset walk) still open.

## USER rulings (verbatim)
- "I suppose P7's prune premise is fine." (the dominance-fold premise is SIGNED OFF)
- "3-8 We should address this at some point or we won't be able to do the value-leaf. I'll leave the timing to you.
  3-9 This means we probably need to work more on performance to start which fits with 3-8. We'll need more effective
  prunes to be applied. Higher budget is not out of the realm of consideration, but only after we have exhausted other
  avenues."
- "It would be nice if there was no extra branching given how easy the mana is in this list."
- "What I mean by pruning fetches is about the set of targets. Not whether to use it or not." / "Though, if there is
  idle mana we should use it." / "And that is fine, because not fetching would be dominated, but this would be
  assuming we can replay it." (P5d owns TapTutorPutEval / TapTutorPutCandidates / fetch ranking -- don't edit them.)
- F7 owns the search hole where a breakpoint continuation never offers a second land drop after a mid-turn tutor;
  P7d does not touch continuation land handling (keep it that way; mention any overlap).
- Box rules (USER 2026-11-18): one heavy process machine-wide under `flock /workspaces/MagicDeckTester/build/heavy.lock`;
  `--threads $(nproc)`, default cache budget, `MTG_RSS_CAP_GB=20`; builds `MTG_BUILD_JOBS=12 ./build.sh` inside it. No
  timeouts; waiters on PID (`kill -0`) or markers. Push only wip/maze-p7 (never the shared branch). Rebase, never merge.
  Don't touch `references/` or the running viewer. Hand off past ~250K context.
