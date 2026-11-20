# P7-7 handoff (Maze's End performance) -- 2026-11-19 ~17:00 UTC

Read large files by grep + line ranges only (TurnSolver.cpp 137K lines, DecisionProviders.cpp 69K).
Worktrees: `.claude/worktrees/maze-p7` (branch `wip/maze-p7`) and `.claude/worktrees/maze-p7f` (branch `wip/maze-p7f`,
NOW ON ORIGIN). Scratch: `build/scratch/maze/p7/` (tools in `tools/`).

## Done
- **(c) INTEGRABLE at 17aaffe3e on wip/maze-p7** (MTG_MAZE_CASTDOM_KEEP_A_ONLY, default ON, + MTG_BP_WAVE_SHAPES
  diagnostic). d8c: d1b1 gi118 (seed 173119) d8 b0 = T7 for base AND keepA, identical digest 79807fdd (base's T6 at
  d1b1 was budget luck); d5b20 gi528 (seed 171529) keepA d8 b0 = T6 (recovers). chain11 tiers on 7eaf3a12f: smoke 1
  play-changed same score (mazesend s1001 gi127); regression 0 slower / 1 faster (s3003 gi139 6->5) / 3 same-score.
  Scoped accept `--deck=mazesend,mazesend2hg` both tiers, check_gt_logs consistent (1301), pushed. Windows CI on
  7eaf3a12f (src) = SUCCESS 44464150605; 17aaffe3e is GT-only.
- shp2 (chain10, 30042 gi1 d5 b0 MTG_BP_WAVE_SHAPES): 96.5% of 8.20M wave applies at T6 d1 (7.92M); 100% of T6 d1 and
  T5 d1 applies GIVEUP with Guild Summit on the battlefield (`Sb`) under the naive bound.
- P7d fold on wip/maze-p7f: 872aacbec (unit: dropped an order-dependent fold count), ac3892ec1 (Summit-aware (S) bound
  + names bound (N)), **4a896585f (SOUNDNESS FIX, see below)**.

## The soundness bug (found + fixed, 4a896585f)
ab6 census (mtg.p7i = ac3892ec1): PROOF 387826 nodes (91.1%), CODE-1 0, CODE-2 0, OTHER 37673, **UNSOUND=41**.
Debug builds (p7j/p7k/p7l, uns2-4 probes, 1 thread outside the lock) showed every unsound proof came from (N), with
`pool=0 libland=0 libgate=17..23`: **hand/library Card instances carry no type bits in play, so `c.IsLand()` is false**
for every one; (N)'s pool and the OLD (C) bound's new-name count were always 0 in games (unit tests build cards from full
definitions, so they passed). Fix: read `MazeDefIsGate(LookupCached(c))` (as the rest of the Maze provider does).
Regression unit case added (stripped m_type_mask). The ab6 fold-arm results (4 slower b0 games, x30042 units 44.6M ->
22.1M) are therefore INVALID -- remeasure with ab7.

## ab6 results that DO stand (unaffected by the Maze bound)
- Maze budgeted cells d5b20 200 / d3b10 100 / d1b1 100 / d0 50: byte-identical fold on/off (no constant leaf there).
- nofold (cert on, fold off) == base on every b0 game (identical digests and units): the WinlessDevelop / label-scope
  consumers move nothing.
- Cross-deck (fold off vs on, 30 games each): Fungus and Gods identical digests AND units at d5b20/d3b10. EDF d3b10 28/30
  same digest (units 6735 -> 7330), d5b20 30/30 same (6300 -> 5624); Snow d3b10 30/30 same (33417 -> 40098), d5b20
  29/30 same. No win-turn changes (better 0 / worse 0 in all four decks). EDF + Snow play DOES move (digests) ->
  per CLAUDE.md they need their own held-out A/B and scoped GT accept when the fold ships.

## ab7 (DONE, mtg.p7m = 4a896585f == c3738cc3c's mtg; 1204 Maze's End jobs) -- THE FOLD IS SOUND
- c3738cc3c: unit compile fix (Library has no pop_back). chain17: build OK, mtg byte-identical to mtg.p7m, `Maze*P7*`
  8/8 pass, full unit suite exit 0. (build16 had failed on that test only; mtg itself relinked from the fix -- verified.)
- Census: **UNSOUND = 0**. PROOF 168187 nodes (35.3%) / 2.57M units (14.3%); CODE-1 (Summit: S and N open) 153236
  (32.1%) / 10.39M (57.9%); CODE-2 (bound open) 112832 (23.7%) / 0.96M (5.3%); OTHER (code 0 or -3) 42606 / 4.04M.
  folded=333878 (fold arms). Census totals are batch-global (cen_ jobs incl. the 20M-abandoned cen_x30042).
- Quality: every b0 fold game digest-identical to base (b0 net 0, 0 slower, d8 b0 10/10 identical); budgeted cells
  identical; nofold == base.
- **Units saved (b0, fold vs base):** d3: 181001 -1.1%, 30001 -1.9%, 30021 -0.9%, 30041 -0.8%, 30061 -2.5%;
  d5: 181001 -3.0%, 30001 -2.2%, 30021 -12.3%, 30041 -7.5%, 30061 -17.8%; **d8 188001 -15.4%** (1.235M -> 1.044M/g);
  **30042 gi1 d5 b0 -0.02%** (44.59M -> 44.58M): Guild Summit still blocks every proof there.

## Remaining, in order
1. 30042: split CODE-1 into "Manabond reachable (incl. library)" vs "(S) count open" (one more code, census on
   cen_x30042 only, 1 thread outside the lock, ~14 min) so the USER decision has its number. My analysis: (S)+(N) is
   the provable maximum -- under Summit a drawn Exploration (with Amulet, via Maze's End re-entries), Charm or Talon
   replaces itself, so only library COUNTS bound it, and a Manabond anywhere voids every count bound but (N). Anything
   tighter (library ORDER, draw odds, "Manabond probably not drawn") is clairvoyant/heuristic -> USER decision.
2. Ship the fold (sound, default ON): EDF + Snow held-out A/Bs (ab6 showed their digests move: EDF d3b10 2/30, Snow
   d5b20 1/30; no win-turn change in 30), net <= 0 per deck, slower games at d8 b0 (abandon >= 50M). Then smoke +
   regression with the fold, scoped accepts for every deck that moves (mazesend*, edf*, snow* as measured -- scope by
   validity, never to keep GT still), check_gt_logs, rebase maze-p7f onto wip/maze-p7 (never merge), push wip/* only,
   CI watch `--interval 60` in background.
3. P7-4 item 4 (per-node playout cost: profile the greedy subset walk) still open.

## USER rulings (verbatim)
- "I suppose P7's prune premise is fine." (the dominance-fold premise is SIGNED OFF)
- "3-8 We should address this at some point or we won't be able to do the value-leaf. I'll leave the timing to you.
  3-9 This means we probably need to work more on performance to start which fits with 3-8. We'll need more effective
  prunes to be applied. Higher budget is not out of the realm of consideration, but only after we have exhausted other
  avenues."
- "It would be nice if there was no extra branching given how easy the mana is in this list."
- "What I mean by pruning fetches is about the set of targets. Not whether to use it or not." / "Though, if there is
  idle mana we should use it." / "And that is fine, because not fetching would be dominated, but this would be
  assuming we can replay it." (P5d owns TapTutorPutEval / fetch ranking.)
- F7 owns continuation land drops; F5 joint prepay / unpayable-plan prune; F6 Maze's End tap order; V3 viewer.
- Coordinator (2026-11-19): build a PROVEN Summit-aware bound (done: (S)+(N)); a sound fold is default ON; only an
  unprovable/heuristic version goes to the USER. Single-game 1-thread probes must NOT hold build/heavy.lock.
- Box rules: one heavy process under `flock /workspaces/MagicDeckTester/build/heavy.lock`; `MTG_BUILD_JOBS=12
  ./build.sh`; batches `--threads $(nproc)`, default cache, `MTG_RSS_CAP_GB=20`; no timeouts; waiters on PID/markers;
  push wip/* only; rebase never merge; don't touch references/ or the viewer. Hand off past ~250K context.
