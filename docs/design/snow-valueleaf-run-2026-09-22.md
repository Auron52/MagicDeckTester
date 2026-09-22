# Snow value-leaf run of 2026-09-22: slow games, phase C's silent pool death, and the ETA

Self-contained record of the overnight run the user asked for ("run the value-leaf for snow
overnight ... even if the value-leaf is too slow the slow games there would give us direction for
the next steps"). Written 2026-09-22 evening, at the close of the session that ran it. The next
session is an optimization session; this is its input.

Engine: `d665aa34` on `phase-1-2-deck-analyzer` -- the breakpoint-continuation adoption IS in
(`MTG_MINT_CREDIT_EXACT`, `MTG_BP_REPLAY_COST`, `MTG_BP_NEW_ONLY` default on, plus fix 11). The
queue's freeze (`logs/vlq_snow/freeze.commit` = d665aa34, `freeze.src` = f032c5bf) proves it, and
`check_freeze` saw the play digest move under the adoption (dd9da751 -> ce694a50) at 13:05, so it kept
only the completed full sets of the earlier phase-A rows and re-ran everything above them on the new
engine. Every phase-C game and every slow-game line below is the adopted engine.

## 1. Timeline

| phase | window (UTC) | result |
|---|---|---|
| A rows | 13:06 -> 15:35 | 19 jobs / 339 games re-run above the kept full sets; 14,771 rows total; 94 games over 30 s, 19 over 1 h, slowest 2.48 h |
| B train | 15:36 | heldout RMSE 0.5559 |
| C matrix | 15:36 -> 16:39 | the pooled `mtg --batch` (832 chunks, 20,800 games) **vanished after 63 min** with 32 games in flight; 3 chunks banked (51 games) |
| C.5 / D | 16:39 | risk gate clean over an empty table; D skipped (no H/V rows) -> the staged sidecar has no `value_play`, no crossover, no trust |
| E measure | 16:39 -> 17:50 | staged vs live **-0.0051 turns, paired t -6.49, better on 8 of 8 seeds, at 1.25x core-s** (45,097 vs 36,007); depth sweep d4 -0.0035 (t -1.33), d5 -0.001, d6 -0.003, all null |
| F | 17:50 | refused: no live sidecar (the designed first-model adoption gate) |
| C resumed | 18:32 -> 19:11 | under `strace`; died the same way at 39 min and this time SAID why: the engine's RSS watchdog, section 5 |

Phase E is a quality/cost TRADE, not a clean win, so the sidecar was NOT installed
(`logs/eval/Snow.value.STAGED.json` is the staged model). The 2026-09-21 shape probe found Snow plays
fine leafless (fit_nl better than the heuristic control at 1.16x units), so `leaf:none` with a
measured `mull_gen_depth` (the Stompy / Goblins precedent) remains a live option.

## 2. Slow games, phase A (labelling, no ceiling but the label budget)

`logs/vlq_snow/rows.batch.log` holds every `SLOW-GAME` line of this run's phase A. The twelve worst,
each a self-contained repro (`build/Release/mtg decks/Snow/Snow.cod --profile decks/Snow/Snow.profile.json`
plus the flags below; phase A adds the labeller env, see `scripts/valueleaf.sh` phase_rows):

| wall | units | win turn | repro |
|---|---|---|---|
| 2.48 h | 224,538,485 | 6 | `--seed 901815 --game-index 65 --games 1` |
| 2.32 h | 222,017,529 | 8 | `--seed 902282 --game-index 32 --games 1` |
| 2.12 h | 146,137,112 | 7 | `--seed 900858 --game-index 108 --games 1` |
| 2.11 h | 151,137,648 | 6 | `--seed 901481 --game-index 231 --games 1` |
| 2.09 h | 152,024,888 | 6 | `--seed 902259 --game-index 9 --games 1` |
| 2.09 h | 141,409,991 | 6 | `--seed 901236 --game-index 236 --games 1` |
| 2.08 h | 126,288,289 | 7 | `--seed 902245 --game-index 245 --games 1` |
| 2.07 h | 136,627,142 | 7 | `--seed 901443 --game-index 193 --games 1` |
| 2.07 h | 138,644,361 | 6 | `--seed 901554 --game-index 54 --games 1` |
| 2.06 h | 161,449,510 | 7 | `--seed 901695 --game-index 195 --games 1` |
| 2.03 h | 158,586,454 | 6 | `--seed 901052 --game-index 52 --games 1` |
| 2.01 h | 88,890,151 | 8 | `--seed 901893 --game-index 143 --games 1` |

Reading: every one of these COMPLETED (win turns 6-8; phase A's manifest has no condemn block, no
unit ceiling and no wall backstop, and `rows.batch.log` holds zero `ABANDONED` lines), so these are
the natural cost of labelling those games: 89M-225M units at 11-30 units/ms. Mechanism (established
2026-09-21, `snow-breakpoint-degeneracy.md`): breakpoints -- 2.95M breakpoint consultations per game
from eight repeatable card-to-hand permanents -- with the label budget (`MTG_VALUE_LABEL_BUDGET_MS`,
default 1e6 virtual ms = 9e8 units per position) as the thing that lets a heavy position run for
hours. Nothing in this run contradicts that diagnosis; the adoption did not remove it.

## 3. Slow games, phase C (H4/H5, ladder leaf + lazy leaf, per-game unit ceiling)

`logs/vlq_snow/slow_games.log` (phase C's `SLOW-GAME` lines, 132 games dispatched in the first hour).
Per cell, finished games only -- censored twice, by the pool's death and by the 40M-unit cap that
bounds calibration games (`job.abandon_units`), so the true means are higher:

| cell | n | mean s/game | max s | calibration median (units) | frozen ceiling (25x) |
|---|---|---|---|---|---|
| H5 s8008 | 12 | 933 | 2744 | 1,499,673 | 40,000,000 (floor) |
| H5 s9009 | 18 | 1067 | 2726 | 32,506,748 | 812,668,700 |
| H5 s10010 | 16 | 1035 | 2309 | 13,262,063 | 331,551,575 |
| H5 s11011 | 17 | 848 | 2072 | 16,833,755 | 420,843,875 |
| H4 s8008 | 15 | 467 | 2064 | 3,328,765 | 83,219,125 |
| H4 s9009 | 18 | 743 | 1821 | 13,207,457 | 330,186,425 |
| H4 s10010 | 17 | 522 | 1431 | 6,929,789 | 173,244,725 |
| H4 s11011 | 12 | 310 | 1062 | 8,417,882 | 210,447,050 |
| H3 s8008 | 7 | 108 | 235 | -- | -- |
| H3 s11011 (resume) | -- | -- | -- | 1,904,718 | 47,617,950 |

Abandoned at the 40M calibration cap in the three banked H5 chunks: s9009 12 of 25, s10010 7 of 25,
s8008 5 of 25 -- so H5 s9009's median is itself half censored, which is why its frozen ceiling is
812M units (about 5 h of one core). Two of those capped games were replayed single-threaded with no
ceiling (`logs/snowdiag/crashprobe_*.out`): H4 s11011 gi2 finishes at 78.5M units (win turn 6) in
1,491 s; H4 s8008 gi24 at 77.7M units (win turn 8) in 1,501 s. A 40M cap abandons ordinary
25-minute games on this deck.

Single-thread rate from the eight completed replays: 17.7K to 65.7K units/s (d4), i.e. 18-66
units/ms -- the pool at 32 threads runs each game ~30% slower than that.

The fifteen worst phase-C games (pool wall; `wt=-2147483648` = abandoned at the unit cap):

| wall s | units | wt | repro (arm env: `MTG_VALUE_MODEL=0 MTG_VALUE_PROFILE=logs/eval/Snow.value.STAGED.json MTG_LADDER_VALUE_LEAF=1 MTG_LAZY_LEAF=1 MTG_TT_CAP=8000000`, `--max-turns 8 --threads 1 --ignore-play-profile --depth <d>`) |
|---|---|---|---|
| 2744 | 40,000,820 | abandoned | H5 `--seed 8025 --game-index 17` |
| 2726 | 40,002,895 | abandoned | H5 `--seed 9017 --game-index 8` |
| 2309 | 30,461,899 | 6 | H5 `--seed 10011 --game-index 1` |
| 2225 | 31,391,113 | 5 | H5 `--seed 8030 --game-index 22` |
| 2144 | 40,000,313 | abandoned | H5 `--seed 9011 --game-index 2` |
| 2072 | 40,001,463 | abandoned | H5 `--seed 11021 --game-index 10` |
| 2064 | 77,471,201 | 8 | H4 `--seed 8032 --game-index 24` |
| 2014 | 24,543,608 | 6 | H5 `--seed 10020 --game-index 10` |
| 1821 | 40,000,353 | abandoned | H4 `--seed 9011 --game-index 2` |
| 1807 | 40,000,669 | abandoned | H5 `--seed 11032 --game-index 21` |
| 1790 | 40,000,576 | abandoned | H5 `--seed 10022 --game-index 12` |
| 1763 | 40,001,258 | abandoned | H5 `--seed 11023 --game-index 12` |
| 1689 | 40,004,431 | abandoned | H5 `--seed 9031 --game-index 22` |
| 1571 | 40,000,598 | abandoned | H5 `--seed 10034 --game-index 24` |
| 1570 | 40,001,119 | abandoned | H5 `--seed 11027 --game-index 16` |

Note the same game index recurring across cells (gi=2 of seed 9009 is abandoned at both H4 and H5;
gi=12 of 10010 and 11011 likewise): the cost is a property of the GAME (its opening hand and draws),
not of the depth alone. That is the population the skip list removes -- and on Snow it is so large
that `--max-skip-frac` trips and the ceiling is disarmed (`depth-matrix-degenerate-games.md`, Snow
section, 2026-09-15). The 16:36 heartbeat (`logs/vlq_snow/heartbeat.txt`) lists the 25 games that
were in flight when the pool died, with their ages; the longest was 0.76 h.

## 4. ETA of phase C as configured: days, not a night

Throughput in the first hour on 32 workers, H4/H5 cells (the queue runs slowest-first): 100 games
finished per hour, ~19 core-minutes per game, with the calibration games capped at 40M units. After
calibration the ceilings are 83M-812M units (0.5-5 h of one core), so the 120 min predictive cut and
the 210 min hard cap become the real bound of the tail. H4+H5 alone are 8 cells x 400 games = 3,200
games: 25-42 h before the tail. V4/V5 are also exempt from condemnation (`NEVER_CONDEMN=5` is the
user's floor, clamped in `scripts/valueleaf.sh`) and unmeasured. Total: **40-80 h**. The user's
reading: "40 to 80 hours suggests we have some real issues to address. We probably will have to spend
more time optimizing."

## 5. Phase C's pool death: RESOLVED -- the engine's own RSS watchdog, fed by a stale table cap

**Cause.** `scripts/attic/valueleaf_depth_matrix.py` pinned `MTG_TT_CAP=8000000` on the pool, a
transposition-table cap sized in August for the 47 GB / 24-worker box (~0.5 GB per table). Since
2026-09-15 the engine derives that cap from the machine's RAM and worker count
(`src/core/MemBudget.h`: budget = MemTotal/2, reserve = workers x 250 MB, TT = (cache/workers)/3/64 B
-- about 0.6M entries per table on this box), but an explicit env value always wins, so the pin
overrode the derivation: 32 workers x 8M x 64 B = 16 GB of table, plus the plan cache (3.2 GB) and
the line cache (1.6 GB), under the engine's RSS watchdog at 3/4 of RAM = 17.60 GiB on this 23.46 GiB
box. The watchdog does what it is documented to do -- flush stdout, print the pools and the in-flight
games to stderr, `_Exit(137)` "so the box survives" -- and the driver dropped the stderr and read the
exit as completion.

**Proof.** The resume at 18:32, run with the pool under `strace` and the driver patched to keep the
pool's stderr, died at 19:11:08 (39 min): `!! POOL EXITED rc=137 (error) with 80/1012 chunks
reported`, stderr tail `[rss-cap] mtg: rss=17.61G EXCEEDS the cap 17.60G (MTG_RSS_CAP_GB) -- aborting
this process so the box survives ... pools: fsl=818M/818M(of 1605M) plan=2368M/3199M(of 3200M)`, then
the four in-flight games it named, and strace `+++ exited with 137 +++` on every thread (an
`exit_group`, no signal -- which is exactly why the kernel logged nothing the first time). The
30-second memory samples (`logs/snowdiag/mem_rerun.log`) show the pool's RSS climbing 14.7 GB at
18:58 -> 17.7 GB at 19:07 -> 17.9 GB at 19:10, then gone. The first death at 63 min matches in
every measurable respect (same pool, same env, same footprint curve, exit within seconds of a game
finishing, nothing in the kernel log); it differs only in that nobody kept the stderr.

**Fix (in this commit).** The driver no longer sets `MTG_TT_CAP`; the engine's per-box derivation
is the bound, and `MTG_MEM_BUDGET_MB` / `MTG_TT_CAP` in the launching environment remain the
documented overrides. Result-neutral by the cap's contract (a refused store recomputes); wall clock
on cache-hungry tail games may move, which the next phase-C run will show. Not yet exercised by a
run at the time of writing.

The forensics below were done BEFORE the traced resume answered the question, from artifacts alone;
they are kept because every exclusion in them is still true and is what pointed at a non-signal exit.

The pool started 15:36:02 and was gone by 16:39:37, four seconds after a game finished (the last
`SLOW-GAME` line, `slow_games.log`, is 16:39:33). The python driver then declared the generation
complete and marked `C_matrix` done, because it never read the pool's exit code and dropped every
non-`SLOW-GAME` stderr line. Forensics, all from artifacts, none from re-running:

* **Not an OOM kill.** `/proc/vmstat oom_kill 0`, the cgroup's `memory.events` all zero, `dmesg`
  has no OOM line. (Peak cgroup memory was 24.27 GB of 25.19 GB with 24 GB swap, so the box was
  full, but nothing was killed for it.)
* **Not SIGABRT / SIGSEGV / SIGBUS -- so not `std::terminate`, not an uncaught throw, not the
  allocator, not a memory fault.** This kernel has `print-fatal-signals=1` and `exception-trace=1`;
  a test `os.abort()` logged `potentially unexpected fatal signal 6` at once, and `dmesg` holds
  NOTHING between 16:26 and 16:53.
* **Not the wall-clock rules.** Longest in-flight game 0.76 h at 16:36 against `max_game_sec` 3600;
  the predictive/hard caps abandon a game, they never exit the pool.
* **Not self-inflicted.** The session made zero tool calls between 13:xx and 17:50 (transcript
  scanned with `logs/snowdiag/killgrep.py`); the only kill-capable background script polls
  `kill -0`; the binary was built 12:33 and not touched; no other file on the whole box was written
  between 16:34 and 16:45; the two other Claude sessions on the machine wrote nothing today.
* **Not a normal exit.** Five chunks were in flight with 16-20 finished games each; a normal end
  prints every job's result line and `=== BATCH done`; none appeared.
* **Not a deterministic per-game bug.** The 25 games marked `[RUNNING]` in the heartbeat were
  replayed single-threaded with the exact arm env: 8 finished normally (exit 0), the other 16 ran
  31+ minutes without a throw before being stopped (past the point the pool had reached for most).
* The engine has no `exit()` outside `main.cpp`, no self-signalling, and the batch runner's only
  `abort()` sites are gated diagnostics.

That left a non-signal exit or an unlogged signal -- and `_Exit(137)` from the watchdog is the
former. (The `exit()` grep that "found nothing outside main.cpp" missed it because the call is
`_Exit`, capital E; a lesson for the next grep.) Two changes make any future pool death
self-describing, both in `scripts/attic/valueleaf_depth_matrix.py` + `scripts/valueleaf.sh`:

1. The driver now records the pool's exit code and prints the last 40 unmatched stderr lines
   (`!! POOL EXITED rc=<n> (signal <k>) with <done>/<jobs> chunks reported -- NOT complete`), returns
   exit 3, and `valueleaf.sh` refuses to mark phase C done over it (resume is unchanged).
2. `VL_POOL_WRAP="<argv prefix>"` runs the pool under a tracer without touching its arguments.
   Diagnostic only. The resume at 18:32 used
   `strace -f --seccomp-bpf -e trace=%signal,kill,tgkill,tkill,exit_group -o logs/snowdiag/poolC_strace.log`,
   which records every delivered signal with its sender pid (`si_pid`) and the exit reason
   (`+++ killed by SIG... +++` / `+++ exited with N +++`) at negligible cost.

Outcome of the traced resume: the cause above. The resume banked 80 more chunks (H1-H3 mostly) before
it died; they stay in `matrix.txt.cells.json` and a later resume continues from them.

## 6. Where everything is

* Queue: `logs/vlq_snow/` -- `driver.log` (timeline), `rows.batch.log` (phase A slow games),
  `slow_games.log` (phase C slow games), `heartbeat.txt`, `matrix.log`, `matrix.txt.cells.json`
  (banked chunks + stored ceilings), `matrix.txt.skipped.json`, `wins/`, `m_snow.log` (phase E).
* Staged model: `logs/eval/Snow.value.STAGED.json` (eval_model + provenance only).
* Replays and forensics: `logs/snowdiag/crashprobe_*.out`, `crashprobe.sh`, `crashprobe2.sh`,
  `killgrep.py`, `phaseC_rerun.sh`, `valueleaf_rerun.log`, `poolC_strace.log`.
* To resume phase C: the C..E markers are already removed; `bash scripts/valueleaf.sh run decks/Snow`,
  alone on the box, continues from the 83 banked chunks. HEAD has since moved (the other session's
  equip-host fix); `check_freeze` will run a smoke tier for the play digest and keep or re-queue
  accordingly. Rebuild first. Watch `[batch] heartbeat` and the pool's RSS in the first hour: with
  the table cap now derived per box the pool should plateau well under the 17.6 GiB watchdog.

## 7. Direction for the optimization session

The cost is in the games, not the harness: the same game indices are the expensive ones at every
depth, the label budget lets one position run for hours, and the depth-matrix's bounding cannot hold
on this population. The three levers already named and NOT yet taken (all user decisions, see
`snow-breakpoint-degeneracy.md` and `depth-matrix-degenerate-games.md`): the breakpoint mechanism
itself (turn-basis mana evaluation instead of per-segment continuation planning), the phase-A label
ceiling (30,000 virtual ms measured to KEEP more rows), and phase C's bounding under the depth-5
condemnation floor. The repro lines above are the test set.
