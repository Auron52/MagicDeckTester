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

## Running
- rev batch (`$O/run_rev.sh` PID 2099824, heavy_run -> `$O/rev_inner.sh`, `$O/m_rev.json` 4809 jobs, mtg.p6; out
  `$O/rev.out`, marker `$O/rev.done`): (1) d8 b0 blind g231/g320 salts 1-4 `BL_*_rev` (+ `_cand` g320 s4);
  (2) `*_revB` = blw's candB jobs + reveal (blind_salt 1), `*_idB` = every 5th candB on mtg.p6 (digest identity vs
  blw candB); (3) `*_rev` clairvoyant on the ac fixidle jobs (+ d0 on ac fix).
  Analyse: `cat $P4/../p5d/abb/run.out $P4/blw/*.out $O/rev.out | grep -v ^AMU > x; python3 $P4/../p5d/anab.py x shipB`
  (blind) and `cat $P4/ac/rows.out $O/rev.out > y; python3 $P4/../p5d/anab.py y fixidle` (clairvoyant).
