# Maze's End P5d -- P5d-5 checkpoint/handoff (2026-11-20). Predecessors: logs/maze/P5d4-handoff.md, P5d3-handoff.md (USER rulings VERBATIM there)

Worktree `.claude/worktrees/maze-p5d`, branch `wip/maze-p5d` (base 021c821d0). Push ONLY wip/maze-p5d. Scratch
`build/scratch/maze/p5d5/` (= `$O`); P5d-4 scratch `build/scratch/maze/p5d4/` (= `$P4`).

**WORKING TREE src/ IS DIRTY WITH A TEMP LEVER -- NEVER COMMIT IT AS IS.** `MTG_MAZE_IDLE_FETCH_REVEAL` (heurarm slot
`MAZE_IDLE_FETCH_REVEAL`, default OFF) in `MazeIdleFetchAdmit`; copy `$O/reveal-temp.patch`. Binary `$O/mtg.p6`.

## g320 blind salt 4 -- ROOT CAUSE (single-game repros, `$O/rp.sh <name> <salt> [env]`, d8 b0, seed 201321 gi 320)
- Repro: mtg.p5 6 (`MTG_MAZE_IDLE_FETCH=0`) vs 9 (shipped). T1/T2 identical; diverges at T3 (real board Cliffgate, Simic
  Guildgate, Exploration, Maze's End; hand Exploration x2, Burgeoning, Archdruid's Charm, Guild Summit, Maze's End).
  No-(a): Maze's End + **Guild Summit** (line win 5 in the search's sampled world, `fd_noidle.err`); (a): Maze's End +
  Exploration + Burgeoning (best line win 6, `fd_idle.err`). No fetch is affordable at T3, so the root menu is untouched.
- The removed plan is ONE TURN DEEPER, in Guild Summit's subtree: T4 host `{land = Gruul Guildgate, actions = []}`
  (`[idle-prune] T4 ... drops_open=2 hand_lands=1 returns=0 land=Gruul Guildgate acts=`, `log_u2idle.err`). Its line was
  Gruul Guildgate -> Guild Summit draws -> breakpoint continuation: play the drawn Gate in the Exploration drop -> Maze's
  End fetch (`bp[k4:Sea Gate k30:Maze's End]`) -> T5 win. (a) reads that host as "no fetch" because the fetch is in the
  BREAKPOINT continuation, not in `actions`, and removes it in favour of `{Gruul, [fetch X]}`, which fetches BEFORE the
  Gate-enter draw. The prune is not choosing fetch over idle mana there; it is forcing the fetch AHEAD OF A REVEAL.
- AMENDMENT (TEMP lever `MTG_MAZE_IDLE_FETCH_REVEAL`): (a) does not remove a plan whose land or PlayLand actions put a
  Gate in under one of our draw-on-Gate-enter watchers (Guild Summit), or that casts such a watcher. The continuation after
  the reveal is enumerated again, so (a) still applies there with the drawn cards known. **g320 s4 d8 b0: 9 -> 6**
  (`s4_rev`); the TEMP binary with the lever off still gives 9 (`s4_p6cand`), so the TEMP slot is inert when off.
- Real-play rationale (not clairvoyance): drawing first and then fetching lets the fetch target and the land-drop
  use depend on the draw. Real play sees that draw too.

## Measurements (rev batch DONE, `$O/rev.out`; mtg.p6 lever OFF == mtg.p5: idB 400/400 digests identical to blw candB)
- d8 b0 BLIND salts 1-4 totals: g320 rev 8/6/6/6 = 26 (= fix 26; shipped 29; ship 25); g231 rev 7/8/6/8 = 29 (= fix 29;
  shipped 30; ship 28 -- the remaining +1 is the Amulet tier's, not (a)'s).
- BLIND sample-wide (blind_salt 1, net turns): revB vs shipB d5 -9, 2HG -9, d3 -11, d1b3 -20, d1b1 +1 (std -39, 2HG -9);
  revB vs candB (shipped) d5 -1, 2HG -1, d3 -5, d1b3 0, d1b1 +4 (std -2, 2HG -1); (a)-with-reveal on fix: std -11 (was -9),
  2HG +2 (was +3).
- CLAIRVOYANT (ac seeds, net turns): rev vs shipped (fixidle == cand) d5 -16 (18/2), 2HG -3, d3 -3, d1b3 -3, d1b1 0, d0 0
  (identical); rev vs ship d5 -12, 2HG 0, d3 -6, d1b3 -7, d1b1 +2, d0 -16; rev vs fix d5 -4, 2HG +1, d3 -2, d1b3 +1, d1b1 -2.
  The games rev recovers include most of the old "(a) clairvoyant losses" (d5 g81 g231 g629 g823, 2HG g254, d1b3 g64,
  d1b1 g186), so the reveal mechanism, not only clairvoyance, was behind several of them.

## Running
- d8 (`$O/run_d8.sh` PID 2128603, `$O/m_d8.json` 110 jobs = the 55 games where rev is slower than shipped (3
  clairvoyant: d5 g516 g984, d1b1 g159; 52 blind salt 1), arms rev / cand at d8 b0 (50M ceiling); out `$O/d8.out`,
  marker `$O/d8.done`). Read: rev <= cand on each?
- (DONE) - rev batch (`$O/run_rev.sh` PID 2099824, heavy_run -> `$O/rev_inner.sh`, `$O/m_rev.json` 4809 jobs, mtg.p6; out
  `$O/rev.out`, marker `$O/rev.done`): (1) d8 b0 blind g231/g320 salts 1-4 `BL_*_rev` (+ `_cand` g320 s4);
  (2) `*_revB` = blw's candB jobs + reveal (blind_salt 1), `*_idB` = every 5th candB on mtg.p6 (digest identity vs
  blw candB); (3) `*_rev` clairvoyant on the ac fixidle jobs (+ d0 on ac fix).
  Analyse: `cat $P4/../p5d/abb/run.out $P4/blw/*.out $O/rev.out | grep -v ^AMU > x; python3 $P4/../p5d/anab.py x shipB`
  (blind) and `cat $P4/ac/rows.out $O/rev.out > y; python3 $P4/../p5d/anab.py y fixidle` (clairvoyant).

## USER rulings 2026-11-20 (VERBATIM, relayed by the parent)
Q1 (the lands-short "win out" tier 1000 vs C2=100): "I'm not exactly sure what to do with this ranking and my original comments were not directed at spells, but other "use spare mana" effects. I don't think activating strictly beats or loses to spells. Both are good and better in certain circumstances. That means that potentially A is okay, but the best answer is conditional. If you need to deploy more lands from hand Exploration, Manabond and maybe even Burgeoning are better. If you need more lands in hand activation or Guild Summit are better. Guild Summit is typically better relatively early or when you have the means to drop a number of gates, whereas the activation is better late, especially when you are nearing the win (and in fact you need to activate to win) as long as you don't strand the Maze's end in hand."
(Option "A" = lower the lands-short tier to 100, C2.)

Q2 (Manabond counting as "comes back" in prune (a)): "I think this case depends more on whether we care about the cards discarded. For example a Gates Ablaze does nothing in goldfish and can be freely discarded. An extra Guild Summit is usually not needed. 1 Exploration is possibly worth keeping if we don't have one out, but otherwise we can probably drop all of the other accelerators since Manabond can drop multiple lands in a turn. Amulets are usually good to keep if you can because of how they interact with Maze's End so they would prefer to be kept. It might be worth going through a discard heuristic to help with this process of determining what is important."

## Plan from the rulings (parent's reading; P5d-6 builds it)
- The "win out" tier was meant to beat other spare-mana sinks, not spells. Against spells the fetch's rank is CONDITIONAL:
  lands in hand exceed the drops available (need to DEPLOY) -> Exploration / Manabond / Burgeoning outrank the fetch;
  short of lands for this/next turn's drops (need lands IN HAND) -> the fetch and Guild Summit outrank them (Summit early
  or when several Gates can drop; the activation late / near the win, it is also the win action, provided Maze's End is
  not stranded in hand). Interim: C2 (100) is acceptable to the USER and may replace 1000 now.
- Manabond counts as "comes back" in (a) when the nonland cards its yes would discard are LOW-VALUE. Discard-value
  ranking per the USER: Gates Ablaze free; an extra Guild Summit usually free; keep 1 Exploration if none is in play,
  otherwise other accelerators droppable; Amulets prefer to be kept. Reuse an existing discard heuristic / bucket (profile
  `discard` / card scores; the Manabond yes/no provider near TurnSolver.cpp:114523, DecisionProviders.cpp:69865-69920)
  rather than a parallel one; consider the same ranking for the Manabond end-step yes/no itself.
- These are provider ORDER/RESTRICT rules (allowed). Measure as before: held-out blind + clairvoyant A/B, d8 b0 recovery of
  every slower game, and the d0 row.
- The reveal-guard amendment (this handoff, TEMP lever `MTG_MAZE_IDLE_FETCH_REVEAL`) is a fix to prune (a) itself and
  composes with both; P5d-6 should land it as part of (a) (or keep it as a measured patch) per the parent.
