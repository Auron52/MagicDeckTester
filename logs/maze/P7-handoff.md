# P7 handoff / checkpoint (Maze's End performance) -- 2026-11-18

Worktree `.claude/worktrees/maze-p7`, branch `wip/maze-p7` (from 7093daceb). Results so far: `logs/maze/P7-results.md`.
Scratch: `build/scratch/maze/p7/` (on the build volume).

## Done
- `1ce7f76d5` fold1: provider-declared CAST / LAND DOMINANCE FOLD (engine `CastDominanceFold`, TurnSolver.cpp;
  hooks `DecisionProvider::CastDominanceLive/CastDominanceClass`; Maze's End levers `MTG_MAZE_CAST_DOMINANCE`,
  `MTG_MAZE_LAND_DOMINANCE`, default ON; diag `MTG_CAST_DOM_STATS`, `MTG_TURN_CENSUS_UNLIMITED`). Units 3540/3540.
- fold1 measured (ab1/, smoke1.log, d8a/): net -8 held-out, but d8 b0 found a SOUNDNESS HOLE (d3b10 152062 gi61 loses;
  d1b1 153072 gi71 T7 vs T6): a plan's breakpoint continuation can spend the mana "P + free cast" takes.
- `8f59d7441` fold2 (committed + pushed to origin/wip/maze-p7; NOT YET BUILT when committed -- build4 in the chain
  builds exactly this tree): fold A only drops breakpoint-free plans (class bits kCastDomAdd / kCastDomLandOk /
  kCastDomOpensBp in DecisionProvider.h; Exploration opens a bp), and not live with a Guild Summit controlled. Unit
  test updated (test_mazes_end_p7.cpp). If build4 fails, fix the compile error, rebuild under the heavy lock and
  re-launch the chain from the ab2 step (`tools/chain2.sh` waits on build4.DONE; delete it and re-run).

## Why handed off at ~200K
The box's heavy lock had ~8 queued jobs; every poll of a 200K-context agent re-reads the whole context, so a fresh
agent waits far cheaper. Nothing is half-edited.

## Running (chain `build/scratch/maze/p7/tools/chain2.sh`, PID in `chain2.pid`, log `chain2.log`, DONE `chain2.DONE`)
build4 (fold2, under heavy lock) -> mtg.p7c -> units3.log -> ab2/ (base vs fold2 + d8 b0 rechecks + b0 screen) ->
smoke2.log -> reg2.log (`--deck=mazesend`). Each heavy step takes `build/heavy.lock`.

## Remaining, in order
0. Wait on the chain with a background waiter on its PID (`while kill -0 $(cat chain2.pid); do sleep 30; done`) or on
   `chain2.DONE` / `ab2/DONE`; never pgrep -f. `chain2.log` records each step.
1. Read ab2 (`python3 tools/abrep.py ab2 base` from `build/scratch/maze/p7`; arms base / fold2; NOTE abrep's cell
   split treats `d8b0<cell>` and `b0d<d>_s<base>` jobs as cells too -- read those rows by name). The d8 b0 rows for
   fold2 must match base's d8 b0 turns in `d8a/` (base was not re-run there: fold off = identical code path) and
   `smk` rows (base + fold2 both in ab2). Recovery bar: every game slower under fold2 must reach base's result at
   d8 b0 (fold2 d8b0 <= base budgeted turn, or at worst == base d8b0 -- report both readings, as P5 did).
   fold1 reference (ab1, sections 3-5 of P7-results.md): net -8 held-out, 24 better / 16 worse; fold2 is expected to
   keep less of the cut (fewer plans dropped) -- report units per shape vs base and vs fold1.
2. Any new slower game: d8 b0 both arms (abandon 200M) as one pooled batch.
3. Smoke2: only mazesend* may move; reg2: mazesend keys. Accept smoke + regression GT for mazesend in the worktree
   (`regression.sh --smoke --accept --deck=mazesend,mazesend2hg`; `--accept --deck=mazesend`), then
   `python3 test/check_gt_logs.py`. Commit fold2 + GT; push `wip/maze-p7` only.
4. Final report in P7-results.md.

## USER rulings (verbatim, from the brief)
- "3-8 We should address this at some point or we won't be able to do the value-leaf. I'll leave the timing to you.
  3-9 This means we probably need to work more on performance to start which fits with 3-8. We'll need more effective
  prunes to be applied. Higher budget is not out of the realm of consideration, but only after we have exhausted other
  avenues."
- (B disclosures) "It would be nice if there was no extra branching given how easy the mana is in this list."
- Coordinator (USER): one heavy process machine-wide under `flock /workspaces/MagicDeckTester/build/heavy.lock`,
  `--threads $(nproc)`, default cache budget, `MTG_RSS_CAP_GB=20`; lower MTG_MEM_BUDGET_MB (never threads) if RSS nears
  the cap; report memory/cost outliers as defects.

## Measurements so far (fold1; superseded by fold2 once ab2 lands)
- d8 b0 games P5 "could not finish": all finish with a higher ceiling (142006 gi5 T5 2.47M; 141096 gi95 T6 4.28M;
  143042 gi41 T6 15.9M) -- P5's 2M ceiling was too low.
- d3 b0 decision cost: turn-2 decision 2.0M units (base) -> ~0.16M (fold1).
- b0 d5 abandons left on both arms: 3 are mulligans (bottoming plays a full b0 game per legal subset; vanishes with a
  keep table), 2 genuine (30042 gi1, 30065 gi4) -- census queued then cancelled (cen2/ manifest ready).

## For the final report (collected so far; surface to the coordinator, do not block)
- USER sign-off wanted on the fold's DOMINANCE PREMISE (P7-results.md section 2 + section 6): Exploration / Burgeoning
  / Manabond / Amulet in play is never worse than in hand; main-phase mana left unspent has no other use (not live with
  an untapped/held Heap Gate, a held Gateway Plaza, or a Guild Summit controlled); only breakpoint-free plans dropped.
- **P6 interaction:** off-main Maze's End activation (wip/maze-p6) creates an off-main mana use -> when P6 lands,
  CastDominanceLive must also go not-live while an affordable off-main activation is possible (or P6's integrator
  re-derives the premise). State this to the coordinator.
- **d8 b0 ceiling:** P5's three "cannot finish" games finish with a larger ceiling (2.5M / 4.3M / 15.9M units).
  Recommend abandon_units >= 50M for d8 b0 recovery checks on this deck.
- **b0 backstop rows:** with the fold, more screen blocks will finish at d3 b0; the suite's backstop rows were chosen
  under the 2M-unit screen on an older tree. Re-screening is a suite change for the coordinator.
- **b0 + no keep table:** a mulligan at b0 plays one full b0 game per legal bottoming subset (3 of 5 d5 b0 abandons);
  disappears once the keep table (exhaustive bottoming) exists -- the mulligan profile comes before the value leaf.
- **Starvation gi100 (111101):** still T7 at b20/b100 (T6 at b500) on fold1: a T3 land choice (Dimir vs Golgari, the
  latter enabling a second Burgeoning); a move-order idea, not a prune, would need its own A/B.
- Census tool for b0: `MTG_TURN_CENSUS=<path> MTG_TURN_CENSUS_UNLIMITED=1 MTG_TURN_CENSUS_JOB=1` (units column is
  per-thread so it stays attributable in a pooled batch; the counter deltas are contended). `cen2/manifest.json` holds
  the two genuine heavy d5 b0 games (30042 gi1, 30065 gi4) plus the three mulligan games, ready to run.
