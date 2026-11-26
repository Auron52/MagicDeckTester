# Mill / K: handoff 2 (2026-11-26 ~07:45 UTC)

Branch `wip/mill-k` (worktree `/workspaces/MagicDeckTester/.claude/worktrees/mill-k`). Commits this session, on top of
6212fc005:

- `65e481672`: cherry-pick of c599d121f (MTG_KO_BOUNDARY_SIM default ON).
- `402b1682c`: K VERIFY made inert by construction. MTG_KO_CROSS_ROUND_STOP (sound crossing cut, default ON).
  MTG_KOB_TIMING diagnostic. New unit tests.
- `f5ecf5a16`: POSIX guard on the child's `_exit` (for the Windows build).
- the logs commit that carries this file.

Pushed: `wip/mill-k` only.

## 1. VERIFY: both defects root-caused and fixed (402b1682c)

**(a) VERIFY perturbed play.** The old reference ran inside the parent process. It ran after the kill-only run, on
the shared `SearchBudget`, through `SimulateToEndImpl`. That caused four problems:

1. It spent the parent's units, so the budget ran out earlier and the ladders were cut.
2. Every memo it stored was then served to later play.
3. It saw the budget the kill-only run had left behind, not the one the kill-only run started with.
4. It ran for every depth>0 rollout with a cutoff before the horizon, even where the scope never armed. That is why
   T's units moved too: 13181 -> 13206.

There are 333 thread-local caches, so restoring state by hand was not feasible.

**Fix (namespace `kov` in TurnSolver.cpp):**

- `SimulateToEndImpl` is now a thin frame (`kov::FrameGuard`) over `SimulateToEndImplBody`. With VERIFY off it is a
  plain forward.
- At the KillOnlyScope arming site the process **forks** (`kov::ForkAtArm`), at most once per rollout frame.
  - The child sets `t_ko_suppress` and does not arm. It runs the rest of that invocation as the full solve, pipes
    back `{result, clean}`, and `_exit`s.
  - The parent runs armed. At the frame's return (`Reap`) it compares the two results.
- The child starts from the parent's exact state at the arming point: same game state, same budget state, same memos
  and leaf table. The parent shares nothing writable with the child, so VERIFY is inert to play.
- A mismatch is counted only when **neither** side was cut after the fork. "Cut" uses the `LandTwinMark` watermark:
  truncation, bind, condemnation drop, sibling defer, cut-win read, or a decision/game ceiling. Cut pairs are counted
  as `bound_diff` / `bound_same`.
- A child that dies, or does not answer within 30 min, is killed and counted as `lost`. It is never compared.
- On Windows, VERIFY prints that it is unsupported and checks nothing.

**(b) armed_turns=0.** This was a reporting defect, not an arming defect. `g_ko_turns` and `g_ko_skipped` were
counted only under `MTG_ROLLOUT_STATS`, so a VERIFY-only run always printed 0. The per-job arm did reach the path;
the counter just was not counted. Also, the dumper is process-global, so a pooled batch could not attribute counts
to an arm.

**Fix:** the counters are now also counted when VERIFY is on, and there is a per-job report line:

`[ko-verify] job=<name> forks verified MISMATCHES bound_diff bound_same lost`

Each MISMATCH line names the job, gi, turn, depth, cutoff, both results and the board at the arming point.

**Confirmed on seed 58077083 gi83** (`logs/mill-k/verify2/`: manifests plus results.txt; built from 402b1682c's
tree):

| arm | VERIFY | avg | digest | units | ko-verify |
|---|---|---|---|---|---|
| T | off / on | 3 / 3 | e9323241 / e9323241 | 13181 / 13181 | no armed rollouts |
| K (sim OFF) | off / on | 4 / 4 | 057854ac / 057854ac | 28633 / 28633 | forks=77 verified=77 **MISMATCHES=2** bound_diff=2 bound_same=37 |
| KF (sim ON) | off / on | 3 / 3 | e9323241 / e9323241 | 12503 / 12503 | forks=20 verified=20 **MISMATCHES=0** |

- With VERIFY on, the digest and units are identical to VERIFY off on every arm.
- Both of K's MISMATCHES are the Archive Trap board: t3 depth=1 cutoff=3, kill_only=9 vs full=3, with Archive Trap
  in hand and Polluted Delta on the battlefield or in hand.
- Cost: VERIFY on K ran 3.4x wall (77 forks in a small process).

**Unit tests** (test_leaf_kill_only.cpp; the full suite is 4197/4197 PASS):

- "VERIFY: the forked reference reports a mismatch exactly when the kill-only answer differs". It checks
  mismatches>0 iff the kill-only answer != the full solve, plus bound==0, with sim 0 and sim 1.
- "VERIFY: inert". It checks the same answer, units, armed and skipped counts with VERIFY on and off, on two boards.
- NOTE: the existing synthetic Archive Trap board does NOT fail with the sim off. The fallback candidate wins there
  anyway, so the game-level catch above is the real demonstration.

## 2. QUEUED: fleet soundness sample (task 2). Do not kill it.

- Runner: `build/scratch/mill-k/kverify/run.sh` (copy in `logs/mill-k/kverify_run.sh`), **PID 4029505**. It is
  queued on `claude_box.lock` behind the two parent batches (maze2hg-perf, land-1126 wfab).
- Manifest `build/scratch/mill-k/kverify/kv.json`:
  - 62 jobs: 31 armed decks x {std, 2HG}, 40 games each, 2480 games.
  - Seeds 60,000,000 + 10,000 per row; gi 0..39.
  - KF arm: NOM2=1, KO_BOUNDARY_SIM=1.
  - Play settings: the profile defaults, as in ab.json.
- Binary: `build/Release/mtg-kv`, a snapshot of the 402b1682c VERIFY code.
  - It has no MTG_KO_CROSS_ROUND_STOP slot (the cut came after it), so it runs the full crossing.
  - Its play is identical to the cut binary, pending the identity batch below.
- Env: `MTG_PGO=0 MTG_RSS_CAP_GB=40 MTG_LEAF_KILL_ONLY_VERIFY=1`, `--threads $(nproc)`.
- Outputs: `kverify/batch.out`, `kverify/batch.err`, `start.txt` (written when it gets the lock), `rc.txt`.
  DONE marker: `kverify/DONE`.
- **In the first 10 minutes after start.txt appears, check the `[batch] heartbeat` line in batch.err.**
  - Fork cost in a 32-thread process is the risk: each fork copies the page tables and holds mmap_lock.
  - If workers are far below 32/32 busy, or the wall projects much past ~2 h, sampling is the remedy, for example
    verifying 1 of every N armings. A fork cannot perturb play, so sampling stays inert. That is a code change and
    a relaunch. The run is an agent's own experiment, so it may be stopped for that.
- **Analysis:**
  ```
  grep '^\[ko-verify\] job=' build/scratch/mill-k/kverify/batch.err | sort
  grep 'MISMATCH\|LOST' build/scratch/mill-k/kverify/batch.err
  grep '^\[leaf-kill-only\] armed' build/scratch/mill-k/kverify/batch.err
  ```
- **Pass:** MISMATCHES=0 on every job, and lost=0 or explained. Report per deck: forks, verified, MISMATCHES,
  bound_diff, bound_same.
  - `bound_*` counts pairs a budget cut touched. They are not soundness evidence either way, but report them.
    bound_diff is the number of cut pairs whose answers differed.
  - A clean MISMATCH is a defect. Root-cause it from the board in its line: replay that game (seed = job seed + gi
    offset, `game_index`) with VERIFY on a 1-thread manifest, the same way verify2 was used.
  - A row with forks=0 means K never arms on that deck at play settings. Say so; it is not a pass of anything.

## 3. QUEUED: identity batch for the KF CPU cut (task 3)

**The cut (402b1682c):** `MTG_KO_CROSS_ROUND_STOP` (heurarm slot `KO_CROSS_ROUND_STOP`, DEFAULT ON).

- `KillOnlyBoundaryEndsGame`'s unfanned crossing now goes through `CrossEndsThisTurn`. Both the non-crossing-land
  branch and the crossing-land no-fan branch use it.
- It stops `SimulateEndAndStartNextTurn` right after `AdvanceTurnCounter`, but only when the counter actually moved.
  The guard is keyed on the copy's address (`t_cross_stop_at_round == &state`), so a nested crossing runs in full.
- **Why it is sound by construction:** the check's answer is `OpponentHasLost && turn_number == t`, and the counter
  never goes back. So nothing past the advance (our next untap, upkeep and draw) can change the answer. An extra or
  free turn keeps the round, and in that case the crossing runs on as before.

**Measured (MTG_KOB_TIMING, 30 Mill KF games, 1 thread, seeds 61,000,000; `logs/mill-k/kob_stop{0,1}.err`):**

- 42,918 crossings. copy 70.9 ms, pre-advance 127 ms, post-advance **100.0 -> 4.2 ms**.
- Crossing total 298 -> 202 ms (-32%) out of ~27 s of CPU. KF's crossing overhead goes from ~1.1% to ~0.75% of
  Mill CPU.
- Digest b1334b5f and units 909,704 are identical with the cut on and off.
- 6 crossings end the game. dup_in_decision=8,592 (20%) with dup_disagree=0, under a BuildSimKey census.

**Note:** the A/B's KF-vs-K CPU of x1.017 / x1.025 is mostly noise. Byte-identical rows elsewhere in that A/B range
over x0.88-x1.03. The direct crossing measurement above is the reliable figure.

**What remains in the crossing:**

- The copy, about 1.65 us each.
- The pre-advance part: end step, cleanup, and the opponent's turn, where the deck-out happens.
- A memo on duplicates would save at most ~20% of ~200 ms, and it would cost a key build per crossing. It would
  also need an exactness proof for BuildSimKey on mid-turn states. NOT done.
- A deck-flag certificate ("this deck cannot end the game in the boundary") is the mechanism-list shape that already
  failed once. NOT done: no lossy or list-based cut.

**Identity batch:**

- Runner: `build/scratch/mill-k/ident/run.sh` (copy in `logs/mill-k/ident_run.sh`), **PID 4047687**. It is queued on
  the box lock behind kverify.
- Manifest `build/scratch/mill-k/ident/id.json`: 72 jobs, 3600 games.
  - Rows: mill, mill2hg, gods, mazesend, landfall. Each row is 6 blocks of 50 games (300 per row); seeds 62,000,000
    + 10,000 per row + 50 per block.
  - Arms:
    - **S** = KF with stop ON.
    - **N** = KF with stop OFF.
    - **K** = sim OFF; Mill rows only, the CPU reference.
- Binary: `build/Release/mtg-id` (= f5ecf5a16 code). Outputs: `ident/batch.out`, `ident/wins/`. DONE marker:
  `ident/DONE`.
- **Pass:** every S job has the same digest and units as its N twin (same seed). Report the CPU of S vs N and of N
  vs K per row, from the `cpu=` field of batch.out, pairing jobs by the `_s<seed>` suffix:
  ```
  python3 - <<'EOF'
  import re,collections
  r={}
  for l in open('/workspaces/MagicDeckTester/build/scratch/mill-k/ident/batch.out'):
      m=re.match(r'(\S+)_([SNK])_s(\d+): .*digest=(\S+) .*units=(\d+) cpu=(\d+)',l)
      if m: r[(m[1],m[2],m[3])]=(m[4],int(m[5]),int(m[6]))
  rows=collections.defaultdict(lambda: collections.defaultdict(int)); bad=[]
  for (row,a,s),(d,u,c) in r.items():
      rows[row][a]+=c
      if a=='S' and (row,'N',s) in r and r[(row,'N',s)][:2]!=(d,u): bad.append((row,s))
  print('non-identical S/N:',bad)
  for row,v in rows.items(): print(row, dict(v), 'S/N=%.4f'%(v['S']/v['N']), ('N/K=%.4f'%(v['N']/v['K'])) if v['K'] else '')
  EOF
  ```
- If any S/N pair differs, the post-advance part had a thread-local side effect that play reads (a witness counter,
  for example `++combatpin::t_reads` in the post-advance resets). Root-cause it, and either replicate the side effect
  or drop the cut. Do NOT ship a non-identical "sound" cut.

## 4. Remaining steps, in order

1. When kverify's start.txt appears, check its heartbeat in the first 10 min (see section 2).
2. At kverify/DONE: run the analysis, root-cause every clean MISMATCH, and report per-deck figures.
3. At ident/DONE: check S/N identity and report CPU.
4. Commit the summaries under logs/mill-k (`git add -f`) and push wip/mill-k. The parent lands the shared branch.
5. Open: MTG_KOB_TIMING is a permanent diagnostic. It prints only and is one thread-local test per crossing. Keep it
   or delete it at landing.

## USER rulings (verbatim, from the briefs)

- "cut CPU"
- "suite headroom never justifies a fix's CPU cost"
- "A BUDGET MAY RE-PRICE WORK, IT MAY NOT MAKE A LINE UNFINDABLE"
- Sound skips are always preferred.
- The USER's adoption bar: "adopt if many decks gain at play settings; mitigate the few worse after; ignore seed
  noise and clairvoyant losses; d0 barely counts".
- "If it is a defect in K (for example, K does not treat "opponent draws from an empty library" as a kill), fix it in
  the general K code, not with a Mill-specific setting. The USER has a slight preference for no per-deck settings."
- Box: ONE big engine process at a time under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with
  MTG_RSS_CAP_GB=40 MTG_PGO=0; small jobs via `scripts/box_lane.sh --slots N` (N <= 2); builds under
  claude_build.lock, ~200 s after editing src; never `timeout`, `pgrep -f`, `pkill -f`.
- Git: push only wip/mill-k; never the shared branch; rebase, never merge.
