# Mill / K: VERIFY repaired, fleet verify queued, a sound crossing cut (2026-11-26)

Written by the parent session from the agent's final message (its harness blocks report files).

VERIFY is fixed: turning it on no longer changes play, and it now catches the Archive Trap defect. The fleet soundness sample and the identity batch for the CPU cut are queued on the box lock and have not run yet, so tasks 2 and 3 have no final numbers. I handed off at 250K as you asked. Both queued batches are still running.

`wip/mill-k` is pushed:
- `65e481672` is the cherry-pick of c599d121f (MTG_KO_BOUNDARY_SIM default ON).
- `402b1682c` fixes VERIFY and adds MTG_KO_CROSS_ROUND_STOP and the MTG_KOB_TIMING diagnostic.
- `f5ecf5a16` adds a guard so the Windows build compiles.
- `b61ce4cb8` is the logs and `logs/mill-k/mill-k-handoff2.md`.

#### 1. VERIFY: both defects found and fixed
**(a) Why VERIFY changed play.** The old reference run happened inside the main process, after the kill-only run:
- It spent the same budget as the real search, so later searches ran out of budget sooner.
- Its memo entries were then reused by later play.
- It started from the budget the kill-only run had left, not the budget the kill-only run started with.
- It ran on every depth>0 rollout, even where kill-only never armed. That is why T's units moved too (13181 to 13206).

There are 333 thread-local caches, so saving and restoring them by hand was not workable.

**Fix:** at the point where a rollout arms its kill-only turn, the process now forks, at most once per rollout.
- The child runs the rest of that rollout as the full solve and sends back its result.
- The parent carries on with kill-only and compares the two results when the rollout returns.
- The child starts from exactly the same game state, budget, memos and leaf table, and shares nothing the parent can see. So VERIFY cannot change play.
- MISMATCHES counts only pairs where neither run was cut by the budget. Pairs a budget cut touched are reported separately as `bound_diff` / `bound_same`.
- A child that dies or does not answer within 30 minutes is counted as `lost` and never compared.
- On Windows VERIFY prints that it is not supported and checks nothing.

**(b) Why armed_turns showed 0.** This was a reporting bug, not an arming bug. The armed and skipped counters only counted when MTG_ROLLOUT_STATS was set. The per-job K flags did reach the code. The totals were also process-wide, so a pooled batch could not split them by arm.
- **Fix:** the counters now count whenever VERIFY is on.
- There is a new per-job line: `[ko-verify] job=… forks verified MISMATCHES bound_diff bound_same lost`.
- Each MISMATCH line names the job, game index, turn, depth, cutoff, both results and the board.

**Confirmed on seed 58077083 gi83:**

| arm | units, VERIFY off / on | digest | ko-verify |
|---|---|---|---|
| T | 13181 / 13181 | identical | no armed rollouts |
| K (boundary sim off) | 28633 / 28633 | identical | 77 verified, **MISMATCHES=2** (the Archive Trap board, t3: kill-only 9 vs full 3) |
| KF | 12503 / 12503 | identical | 20 verified, **MISMATCHES=0** |

VERIFY makes the K game about 3.4x slower in wall time.

**Unit tests:** two new cases.
- One checks that VERIFY reports a mismatch exactly when the kill-only answer differs from the full solve.
- One checks that VERIFY is inert: same answer, units and counts with it on and off.

The full suite passes, 4197/4197. The existing synthetic Archive Trap board does not fail with the boundary sim off, so the game above is the real demonstration that VERIFY catches the defect.

#### 2. Fleet soundness sample: queued, not run
- **Batch:** 31 armed decks, standard and 2HG, 40 games each (2480 games), KF arm at play settings, VERIFY on, one pooled batch.
- **Where:** `build/scratch/mill-k/kverify/`, runner PID 4029505, binary `mtg-kv`. It is queued behind your two batches.
- **Pass:** MISMATCHES=0 on every job, and lost=0 or explained.
- **Risk:** each fork copies the process page tables, which may be slow in a 32-thread process. If the first heartbeat is low, verifying a sample of armings would fix it without affecting play.

The analysis commands are in the handoff file.

#### 3. KF CPU cut
**The cut: MTG_KO_CROSS_ROUND_STOP**, default ON, armable per job. The kill-only boundary check now stops its crossing as soon as the turn counter advances.
- **Why it is sound:** the check only asks "did the opponent lose while it is still this turn", and the turn counter never goes back. So nothing after the advance (our next untap, upkeep and draw) can change the answer.
- **Extra and free turns:** they keep the same turn number, so the crossing runs in full there as before.
- **Nested crossings:** the stop is tied to that one copied state, so a crossing nested inside it runs in full.

**Measured on 30 Mill KF games, single thread:**
- The part after the advance went from 100 ms to 4 ms.
- Total crossing cost went from 298 ms to 202 ms (-32%), out of about 27 s of CPU. KF's crossing overhead on Mill drops from about 1.1% to about 0.75%.
- Digest and units were identical with the cut on and off.

The A/B's KF-vs-K CPU figures (x1.017 / x1.025) are mostly noise: rows that are byte-identical in the same A/B range from x0.88 to x1.03. The direct timing above is the reliable figure.

**What I did not cut:**
- The state copy (71 ms) and the end step plus opponent's turn (127 ms) are where the deck-out can happen, so they stay.
- A memo for repeated states would save at most about 20% of that, would cost a key build per crossing, and would need a proof that the key is exact for mid-turn states.
- A per-deck "this deck cannot end the game in the boundary" flag is the list-of-mechanisms shape that already failed once.

I found no further sound cut, so I added nothing lossy.

**Identity batch: queued, not run.** It checks byte-identity before the cut can be counted as landed.
- **Batch:** Mill, Mill 2HG, Gods, Maze's End and Landfall, 300 games each. Arms: KF with the stop, KF without it, and K on the Mill rows only as a CPU reference.
- **Where:** `build/scratch/mill-k/ident/`, runner PID 4047687. It is queued behind the verify batch.
- **Pass:** every stop/no-stop pair has the same digest and units. Then report the CPU per row.
- **If a pair differs:** something after the advance (for example the combat-pin read counter) feeds later play. Find it, and either reproduce that effect or drop the cut.

#### What is left
- **Fleet soundness:** run the analysis when `kverify/DONE` appears and root-cause any clean MISMATCH. The old "0 of 22,084" figure stays unsupported until then.
- **Identity:** check the pairs and report CPU when `ident/DONE` appears.
- **Diagnostic:** MTG_KOB_TIMING only prints. Keep it or delete it when you land.

There are no questions for the USER from this work.
