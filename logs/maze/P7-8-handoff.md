# P7-8 handoff (Maze's End P7c + P7d + census split + (O)) -- 2026-11-20 ~11:00 UTC

Worktree `.claude/worktrees/maze-p7`, branch **`wip/maze-p7` = 539bd4f6c** (rebased onto batch 1 = origin/phase-1-2-deck-analyzer
8fbbc5023; pushed; pre-rebase backup `backup/maze-p7-pre-rebase-p78` = c8f017b1d). `wip/maze-p7f` is superseded (its P7d commits
are on wip/maze-p7). Scratch: `build/scratch/maze/p7g/` (tools in `tools/`). Binary for the rebased tip: `p7g/mtg.p7r`.

## Done (hashes on the rebased branch)
- Rebase: P7c (a)+(b) fc204f2f8 -> rebased, (c) edd113f3b, P7d fold 407986c03..eaf9e473e (incl. the soundness fix), census split
  78ecc9d6f, (O) 539bd4f6c. My old GT accept commits (17d414e8f, ebbc388b0, 258cf0d38, 17aaffe3e) were DROPPED in the rebase (GT is
  a measurement: re-accept under the rebased binary). Conflicts: HeuristicArm.h slot lists and one DecisionProviders.cpp function
  block, both resolved as a union (batch 1's F5/Summit slots first, then P7c/P7d slots).
- Units: full suite pass on the rebased build (units4all exit 0; `Maze*P7*` pass). Windows CI on 539bd4f6c: **SUCCESS** (44706799648).
  (Earlier, on the pre-rebase combined tip b86e5a5b3: CI 44676407792 SUCCESS.)
- Census split (78ecc9d6f): decline codes are diagnostic only (every consumer reads proof > 0; play-identical). -1 Manabond
  controlled/in hand + (S) count open; -6 same but the count alone would close; -5 / -7 Manabond in the library only (count open /
  would close); -4 no Manabond, count open.
- (O) library-order Summit bound (539bd4f6c, MTG_MAZE_WINLESS_ORDER default ON, proof code 4, census PROOF-O, print-only
  `[maze-order]` decline census). Reads exactly the order the search sees (current order + every future keyed reshuffle,
  Library::ShuffleKey now shared with ShuffleByKey); falls back in blind / MTG_NO_SEARCH_SHUFFLE / MTG_LEGACY_SHUFFLE views.
  Unit case "P7-8 ... (O)" covers proof, lever off, blind fallback, sources on top, Manabond on top.

## Measurements
- **Summit split on 30042 gi1 d5 b0** (ab10 census, rebased engine; the game is now 12.2M units, was 44.6M before batch 1):
  CODE-5 (Manabond only in the library AND the (S) count open) 39,449 nodes = 64.5% / 1.38M units = 67.0% of census units;
  OTHER 35.4% / 32.9%; PROOF 68 nodes; CODE-1 / -4 / -6 / -7 = **0**. Pre-rebase (ab9, 44.6M game + d3/d5/d8 cells, batch-global):
  CODE-5 239,959 nodes / 26.6M units (62.9%), CODE-1 2,553 / 0.25M, CODE-4/6/7 = 0. => **"too many sources" binds at EVERY Summit
  decline; a Manabond is never the only blocker** (a "Manabond not drawn" narrowing would prove nothing by itself).
- **(O) proves 0 nodes**: `[maze-order] calls=55628 proof=0 | every card drawable at round 0 = 55628`; R - library >= 10 in every
  call (R = draws + reshuffles bound). ab9 (pre-rebase): units ord == agn on every cell. Sound (UNSOUND=0) but inert: with ~8-10
  entry sources, every Maze's End fetch is a reshuffle, so the union of reachable tops covers the whole library. Making it bite
  needs a MANA bound on reshuffles (fetch {3}, Scrying {1}{G}, Charm {G}{G}) -- not built; next step if wanted.
- Maze's End fold identity on the rebased engine (ab10): b0 d5 20/20 and d8 10/10 identical digests cert0 vs shipped; units
  d5 -2.7%, d8 -8.6%. Pre-rebase ab9: every b0 cell identical across cert0/agn/ord; fold vs cert0 units d3 -1..-3%, d5 -2..-18%,
  d8 -15.4%; budgeted d5b20 50/50 identical.
- **EDF/Snow held-out A/B (ab10, fold off vs on, seeds 191001.. / 192001.. / 193001.. / 194001.. / 195001..)**:
  EDF: d5b20 1000 net 0 (1000/1000 same digest), d3b10 1000 net 0 (999 same), 2HG d5b20 300 net 0, b0 d3/d5 40+40 net 0 => **PASS**.
  Snow: d5b20 1000 net 0 (996 same), d3b0/d5b0 6+6 net 0, **d3b10 1000 net +1** (one slower: gi961, seed 192962, 6 -> 7) =>
  **Snow net +1: FAILS the strict per-deck bar by one game.**
- Snow gi961: d8 b0 RECOVERS (both arms T6, identical digest 5d21da9fdc8c033e; fold 1.83M vs 2.21M units). d3 single runs:
  b0 6/6; b10, b20, b40 base 6 / fold 7; **b100 6/6, b400 6/6** (the fold recovers at larger budgets: the loss is
  confined to small budgets, b10-b40). Census on gi961 d3b10: UNSOUND=0 (237 proofs, none won).
  First divergence T3: base Coldsteel Heart, fold Ice-Fang Coatl. MTG_FS_ROOT_DUMP=3 (`p7g/g961/dump_{base,fold}.txt`): in the
  third root pass both find Coatl+Augur -> T7; base then evaluates the remaining plans as **bp-child** (breakpoint children) and
  finds Heart+Augur -> **T6**; fold evaluates every remaining plan as **tail** (no breakpoint children) -> 9. So the fold changes
  which path the root takes for plans after the incumbent (breakpoint wave admission / a cache the folded node feeds), not the
  proof's truth. NOT root-caused yet: next step is to find the gate that sends those plans to the tail path in the fold arm
  (TurnSolver.cpp ~120200-120340 bp-child vs ~120776 tail; candidate causes: budget-wave admission after the fold's saved units,
  or a no-win cache entry the folded node stores with an empty line). This is OWED before the fold ships on Snow.

## In flight (started by me)
- **chain5 DIED at 11:08 UTC, right as the smoke batch started** (smoke4.log stops at `=== BATCH`; no `smoke4 exit`
  line; cause unknown -- possibly killed with the session's background tasks). **The tiers have NOT run.** Relaunch as is:
  `bash build/scratch/maze/p7g/tools/chain5.sh` (its wait on PID 2309277 passes at once; it re-checks the binary == mtg.p7r).
  Original description -- chain5 (PID 2323111): smoke then regression with `VPC_SKIP=1` (coordinator hold), then
  `MTG_BIN=p7g/mtg.p7r python3 test/viewer_protocol_check.py --strict --only Maze_s_End`. Logs `p7g/smoke4.log`, `reg4.log`,
  `vpc4.log`; status lines in `p7g/chain4.log`; DONE marker `p7g/chain4.DONE`. Binary checked == mtg.p7r before the tiers.
- cen11 (heavy_run PID 2345850): Snow + EDF census (400 games each of d5b20 / d3b10, + Snow gi961) -- soundness of their certificates
  under the fold; read `grep lastply p7g/cen11/run.err` (UNSOUND must be 0); DONE marker `p7g/cen11/DONE`.

- **cen11 DONE: EDF + Snow census UNSOUND=0** (1601 games: 400 each of EDF/Snow d5b20 + d3b10, + Snow gi961; PROOF 19,433
  nodes, none won; OTHER 492,049). Their certificates are sound under the fold.

## Remaining, in order
1. When chain4.DONE lands: read smoke4/reg4 (moved keys; expect mazesend*, and edf*/snow* if the fold moves them), d8 b0 every
   PERSISTS slower game, then scoped accepts (`--deck=` every deck that moves, incl. `<deck>2hg`), `python3 test/check_gt_logs.py`.
   VPC: "full corpus held (Galadriel s10 / F5R, owned elsewhere); Maze_s_End strict = <vpc4 result>".
2. Root-cause Snow gi961 (above) and fix, or the USER/coordinator decides on the +1 (bar: "+1 is not neutral. 0 is neutral.").
3. (done) cen11: EDF/Snow UNSOUND = 0.
4. (O): optional mana-aware reshuffle bound if more Summit proofs are wanted; otherwise (O) stays as a sound, inert default.
5. Then READY FOR BATCH 2 (push wip/* only; the parent lands it).

## USER rulings (verbatim)
- 2026-11-20 (Guild Summit / 30042): "Don't we already run clairvoyantly or are you talking about in a heuristic?"
  Coordinator reading: the search already plays the known library order, so a bound reading the same order is a SOUND fold
  relative to the search's information; build it default ON; fall back where the node's library is hidden/shuffled.
- "I suppose P7's prune premise is fine." (dominance-fold premise SIGNED OFF)
- "3-8 We should address this at some point or we won't be able to do the value-leaf. I'll leave the timing to you.
  3-9 This means we probably need to work more on performance to start which fits with 3-8. We'll need more effective
  prunes to be applied. Higher budget is not out of the realm of consideration, but only after we have exhausted other
  avenues."
- "It would be nice if there was no extra branching given how easy the mana is in this list."
- "What I mean by pruning fetches is about the set of targets. Not whether to use it or not." / "Though, if there is
  idle mana we should use it." / "And that is fine, because not fetching would be dominated, but this would be
  assuming we can replay it."
- Coordinator: a sound fold is default ON; only an unprovable/heuristic version goes to the USER. Single-game 1-thread probes
  must NOT hold build/heavy.lock. VPC full-corpus HOLD (Galadriel s10 / F5R, owned elsewhere): tiers with VPC_SKIP=1, then the
  Maze_s_End strict gate separately.
- Box rules: heavy work via `bash /workspaces/MagicDeckTester/scripts/heavy_run.sh` (its exec bit was lost on 11-20 -- call it
  through `bash`); `MTG_RSS_CAP_GB=20`; waiters on PID/markers; push wip/* only; rebase never merge; no timeouts.
