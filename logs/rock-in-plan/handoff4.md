# rock-in-plan handoff 4 (2026-11-21 ~02:10 UTC, agent 4 at ~190K context)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-ade0a1bc23ae96aa7`, branch `wip/rock-in-plan` (pushed to
origin at 019a98095). Push ONLY that branch; NEVER push phase-1-2-deck-analyzer, never join the push queue (the parent
lands it). Scratch `S=/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/`. Read handoff3.md for the mechanism and
the earlier measurements; this file covers agent 4.

Agent 4 stopped early on purpose. The only thing left is a wait of several hours on the big lock, and polling at ~190K
context would cost more than a fresh agent re-reading this file.

## Done (commits on wip/rock-in-plan)

* chain3 (lane; bc710d3f3 binary): build ok, **units 3720/3720 passed**, prefix probes 0/1/4 byte-identical (0 diffs),
  **exactness slice: 192 of 192 `.wins` byte-identical to logs_ab3**. Niv on/off CPU on that slice 1.0659 (ab3 1.0650);
  the box was contended (load 35-39), so that ratio is noise.
* gi152 (seed 680152, game_index 152) at `--depth 8 --budget-ms 0`: BOTH arms unwon (off digest d70174a17fc9a5bf, on
  dc791ba85a50ac1f, ~30 min each; `$S/d8_152.out`, `$S/logs_d8_152`). Niv max_turns = 8 (unwon counts 9 in `avg`), so the
  off arm's budgeted win was on the LAST turn, and the unbudgeted search finds it on neither arm.
* Paired 1-thread CPU probes (Niv 200 games, seeds 680000, two processes side by side; rung ms from `[line-keep-all]`):
  prefix 0 (one full ask) 18.4 s and 18.1 s, prefix 1 19.4 s, **prefix 2 14.7 s and 14.8 s**, prefix 4 15.0 s. Files:
  `$S/cpfx{0,1,2,4}.*`, `$S/dpfx{0,2}.*`. All runs byte-identical to pfx1.
* `da3d3cb61`: table prefix default 1 -> 2; **Signet-pair tight test** (`MTG_LKR_SIGNET_PAIR`, default ON). An untapped
  board ramp-filter converter with the rock's own two colours (a second Izzet Signet, Ferrous Lake) is no longer a guard
  on a ramp-filter rock's tight joint test. The proof is in the code comment. Two guards are proven necessary by
  counterexample and stay: Cascade Bluffs, and a {C}-only rock (Sol Ring) with a board Signet. The Generator Servant guard
  also stays (the joint backtracker does not crack it). UNVERIFIED until chain7.
* `019a98095`: docs/design/rock-in-plan-2026-11-19.md has the CPU section, the USER ruling verbatim and the gi152 verdict.
* The parent's ruling was applied: the first tier run (chain3, in the lane at 4 threads) was killed by PID at ~40% of
  smoke, and the lane was released. It was agent 4's own run.

## Running job: chain7 (ONE big job, queued on claude_box.lock)

* Wrapper PID **4110419**: `flock build/scratch/claude_box.lock $S/chain7.sh`. Status lines go to `$S/chain7.status`
  (the first line is "queued ..."). The DONE marker `$S/chain7.done` holds FAIL (build), STOPPED (a check failed, tiers
  skipped) or DONE. A mana-clean gate holds the lock for a few hours, so chain7 waits behind it.
* Steps, at `nproc` threads with MTG_RSS_CAP_GB=40: rebuild ManaPayment.o + `./build.sh` (`$S/chain7.build.log`). Then,
  concurrently: units (`$S/chain7.units.log`); `MTG_LKR_TIGHT_CHECK=1` Niv 200 (`$S/tc.err`; `[lkr-tight-bad]` count
  must be 0); paired SIGNET_PAIR 0 vs 1 stats probes (`$S/sp{0,1}.err/.out`, wins vs logs_pfx1 must be identical). Then
  the exactness slice `manifest_exact2.json` -> `$S/logs_exact3` vs logs_ab3 (must be 0 diffs of 192). Then, if all pass,
  `THREADS=$(nproc) bash test/regression.sh --both` (`$S/tiers7.log`; results in the worktree's test/results + test/logs).
* Waiter: `S=...; while kill -0 4110419 2>/dev/null && [ ! -f $S/chain7.done ]; do sleep 30; done` (run_in_background;
  never pgrep -f).

## What remains, in order

1. When chain7 is done, read `$S/chain7.status`. On STOPPED: if `tc bad` > 0 or there are any sp/exact3 diffs, the
   Signet-pair proof has a hole. Turn it off (`EnvOn("MTG_LKR_SIGNET_PAIR", false)`, or revert that hunk), say so, and
   re-run the slice. Never fix the engine to fit a test. If units fail, read `grep -nE "ERROR|TEST CASE"`.
2. CPU: `grep -h "line-keep-all\|line-keep-work" $S/sp0.err $S/sp1.err` (rung ms and table_ms, SIGNET_PAIR off vs on,
   paired). Also run `python3 logs/rock-in-plan/compare.py $S/logs_exact3 $S/exact3.out` (the niv row has both arms; it
   is cleaner at full cores than the contended 1.0659).
3. Tiers: audit every changed game per .claude/skills/regression-testing.md, run `bash test/classify_turn_later.sh <mode>`
   and a d8 b0 replay (`scripts/box_lane.sh --slots 1`) on any searched slower game, and compute the net per deck.
4. **gi152 decision.** The USER's condition is below. Agent 4's reading: the lever is not worse than off at unlimited
   budget (both arms unwon at d8 b0), but the game does not "recover" to the off arm's budgeted T8. The parent said: "If
   it does NOT recover, root-cause it and report before accepting anything." So do NOT accept GT until the parent/USER
   settles this. A cheap root-cause while chain7 waits: replay seed 680152 on both arms with game logs at play settings
   (`manifest_nivp_on.json` / `manifest_nivp_off.json` restricted to gi152). Find the first divergence and whether the
   off arm's T8 kill depends on a payment the rung changes, then report the mechanism.
5. If gi152 is settled as okay, every deck nets <= 0 in smoke AND regression, and chain7 passed: `bash test/regression.sh
   --both --accept` in the worktree (under claude_box.lock, not the lane), then `python3 test/check_gt_logs.py`. Commit
   the GT, then `git fetch && git rebase origin/phase-1-2-deck-analyzer` (rebuild + smoke if src replayed), then push
   wip/rock-in-plan only.

## USER rulings (verbatim)

* 2026-11-21, relayed by the parent: "Sounds good. Let's turn it on if the unwon looks okay. We should indeed support
  this." Parent's gloss: default ON is approved on one condition, that the Niv probe game that went from a T8 win to
  unwon must recover at --depth 8 --budget-ms 0 (the T6->T5 one is just a note). "If it recovers, go ahead with the
  default-ON tiers and accept GT in your worktree (under claude_box.lock, not the lane ...). If it does NOT recover,
  root-cause it and report before accepting anything. Record the ruling verbatim in your doc and handoff. Do not push the
  shared branch."
* Parent ruling (2026-11-21): "A full tier ... must not run inside scripts/box_lane.sh; it holds all 4 slots for hours
  while six other agents' small jobs queue behind it ... run the tier as a big job under `flock
  /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with the machine's full thread count (`nproc`) and
  MTG_RSS_CAP_GB=40. Keep MTG_PGO=0 only if you need it for a wall-clock comparison ... queue behind it with a background
  waiter keyed on a PID or DONE file, not polling. Do not edit chain3.sh while bash is reading it; write a new script
  instead."
* Carried from handoff3 (still in force): "NOTE: MTG_LINE_KEEP_ALL (the general widening) is NOT adopted ... the rock
  lever must stand on its own measurement"; "The bar: every slower game recovers at --depth 8 --budget-ms 0, and every
  deck net <= 0. Report CPU per deck. The USER says cut CPU cost; never justify it by headroom."; "USER rule: a fix's CPU
  cost must be cut, never justified by headroom."; "Niv CPU: ... cut it EXACTLY (play byte-identical; verify by .wins
  identity on a Niv slice) ... If no exact cut exists, say why."
* Machine: one big process at a time under claude_box.lock; small work via scripts/box_lane.sh (<= 4 slots); ./build.sh
  only; wait ~200 s after edits (9p lag) and delete touched objects before building; no `timeout`, no pkill -f / pgrep -f;
  rebase, never merge; never touch references/.
