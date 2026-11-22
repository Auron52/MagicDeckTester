# Bruna APPLY-DROP: handoff 6 (2026-11-22, agent 6 -> agent 7)

History: `handoff5.md`, `handoff4.md`, `agent5/*.txt` (this directory). This file supersedes their "remaining" lists.
Worktree `.claude/worktrees/bruna-apply-drop`, branch `wip/bruna-apply-drop`. Scratch `build/scratch/bad/` (agent 6: `a6/`).
Compare arms: `python3 build/scratch/bad/cmp_arms.py <wins_dir> <batch.out> <armA> <armB>` (net = A - B, unwon = 9).
Do NOT use `.claude/worktrees/agent-a1651726d5b941608`: another session worked in it (it holds a copy of ece73c339 as
f4d2ebd54 on `wip/bruna-apply-drop-a6`; ignore that branch).

## Commits (wip/bruna-apply-drop, not pushed)
- d97ac2b14 memo engages + bypass counters + kAuraMaxIds 40 + MTG_EVAL_COMBAT_TOKEN_SELF (default OFF).
- ece73c339 fix 1 amend, the gi21770 cause (below) + `docs/design/d0-lord-pump-lethal-projection.md` + comment fix.
- 6f868c056 heurarm slot MTG_CREATURE_FLOAT_SWAP (default ON) for the amend. UNBUILT until lock7 (below).

## Step 1 (CPU): DONE except the instruction-count confirmation
- fix6 (= d97ac2b14) counters, VERIFY on (fix6/verify/*.err): EDF d0 200 games hits 502,965 / misses 64,077, bypass
  232 no_scope (+147 scope_why none/board), long=0, VERIFY_MISMATCH 0. Jumbo d0 / Sea d0: 0 memo calls (their helper
  calls come from SPWF / CastSpellFromHand, prefilter only).
- So the 881K unmemoised SeqWalk calls in cg5 were the fix5 binary's kAuraMaxIds=14 long-prefix bypass; fix6's 40 removes it.
- fix6 user-time screens (one thread each, fixes on vs off, 3 pairs side by side): EDF d0 37.93/36.22 = **1.047** (fix5
  1.081), Jumbo d0 5.99/5.74 = 1.044, Sea d0 11.89/11.49 = 1.035. These screens are noisy: cg5 Sea d0 Ir on/off was
  5.188G/5.164G = 1.0045 (helper 1.47%), so Sea is at noise. lock7 measures instructions:u (perf stat) to settle EDF/Jumbo.
- No further cut was made. If perf shows EDF d0 or Jumbo d0 > ~1.01 Ir, profile fix7 (cg5/run.sh shape) and cut.

## Step 3 (gi21770): ROOT-CAUSED + FIXED (ece73c339), awaiting lock7
Cause: before tapping, the payer drains the GENERAL float ({G}) onto Bruna's generic pip; two Sages' WWW+UUU pay the
rest, and fix 1's exact booking (which reads tap_bill, the bill AFTER the drain) books the over-produced {U}
creature-only. Skycloud Expanse then has no {1}, so Wings drops. The oracle pays Bruna with the Sages and keeps {G}.
Fix: commit_leftover swaps up to k restricted leftover units for the drained general units that paid generic pips
(W..G units beyond the cost's own pips of that colour; no wild/{C}/snow). It is gated on PlanTraits: the line casts
exactly one creature and still owes mana, so no later creature of the line could spend the restricted unit. It runs
on the greedy and first-backtracker paths only (not the filter retry). Unit subcase "gi21770 T8" in test_bruna.cpp
also checks the negative (a second creature in the plan keeps the unit restricted). Decks it can touch: bruna,
boars (Somberwald Sage), slivers (Ancient Ziggurat).

## Step 2 (MTG_EVAL_COMBAT_TOKEN_SELF)
Spirits d0 gi2341 (seed 1412341) root cause: on T4 opp is at 12. {Drogskol Captain + Supreme Phantom} deals 14,
lethal through the lords' pump. {Millicent + Mausoleum Wanderer + Phantom} deals 11. Both read proj_atk=8, so
neither reads as a win: the projection ignores lords cast this turn. Lever off, eval 1900 beats 1800 (lethal by luck).
Lever on, Millicent's +200 (correct: 1 token x 1 power x 2 attacks) makes it 2000 > 1900, and the game is won on T5.
The lever's credit is right; the defect is the lethal projection (a separate, broad play change). It is documented,
not built: `docs/design/d0-lord-pump-lethal-projection.md`. No sound amendment of the lever itself removes the game
(the A(A-1)/2 token-horizon form zeroes Kraken at T5 and kills the Sea gain).
Re-measure RUNNING: `a6/tok3` (fix6, seed base 1610000; t1 = lever on, t0 = off; sea 8000, sea2hg 2000, spirits 4000,
sead0 200k, spiritsd0 200k pairs). Result in `a6/tok3/cmp.txt` (DONE = `a6/tok3/DONE`).
Then: every slower game needs d8 b0 (`ab/slow_replay.py <list> 1610000 <out> MTG_EVAL_COMBAT_TOKEN_SELF`, run under the
lock, or in the lane at 2 slots). If every deck nets <= 0 (searched and d0) and all recover: flip it default ON
(EngineFlags.h EvalCombatTokenSelfOn false -> true), as a strictly-better fix. Otherwise leave it OFF and report (USER call).

## Running jobs (agent 6's; none USER-requested)
| job | PID | what | DONE marker / output |
|---|---|---|---|
| tok3 | 850661 (run.sh) | lever A/B under the box lock, then `cmp.txt` | `a6/tok3/DONE`, `a6/tok3/cmp.txt` |
| lock7 | 858877 (lock7.sh) | waits for tok3, then takes the lock. Full-core build of the worktree -> `fix7/`, Bruna units, gi21770 swap 1/0, perf instructions:u screens (fixes on/off: EDF d0 200, Jumbo d0 3000, Sea d0 10000, Bruna d0 5000), then the swap A/B (`a6/swap/m.json`, seed 1710000, on/off = MTG_CREATURE_FLOAT_SWAP; bruna/boars/slivers searched + 2hg + d0 50k) | `a6/lock7.DONE`; progress `a6/lock7.status`; A/B `a6/swap/cmp.txt` |
Waiters: `kill -0 <pid>` or the DONE files. Never edit the worktree src while lock7 has not finished its build
("build rc=" in lock7.status).

## Remaining, in order
1. lock7: the units must pass (incl. the new gi21770 subcase), gi21770 swap=1 should win on T8 like the
   fixes-off arm (swap=0 = fix 1 alone = unwon), and the perf ratios should be near noise. Swap A/B: every deck net <= 0; d8 b0 for each slower game. If it
   fails, leave MTG_CREATURE_FLOAT_SWAP default ON only if the bar is met; otherwise set it OFF and report.
2. Step 2 decision (above) from tok3.
3. Step 4 (report only; do NOT adopt): 7a Jumbo d0 +6 = gi 1197 1368 1431 1447 (+ others in agent5/cmp_a.txt).
   gi1447 (seed 1311447): 7a's creature-ahead-of-land rule taps Voyaging Satyr (a land untapper) early on T3, so
   Nylea APPLY-DROPs (4 -> 7). Suggested alternative: exclude land-untapper "dorks" from 7a's creature-first rank. 7b
   Niv d0 +13 (agent5/cmp_b.txt). gi532 (seed 1310532): both arms choose {Ponder, Signet, Brainstorm} on T3. Off
   drops Ponder; 7b pays it by sacrificing Generator Servant, so T4 has no Niv (5 -> 7). Suggested: charge a
   sacrificed Generator Servant (the Niv haste/CC enabler) in the d0 plan score, or keep it out of 7b's payments.
4. GT (brief step 5): rebase onto origin/phase-1-2-deck-analyzer (rebase only), `./build.sh`, units (fix the
   Pirates routing test if it fails: it counts decks/ next to the binary), `MTG_PGO=0 test/regression.sh --both` under
   the box lock (MTG_RSS_CAP_GB=40), attribute every changed game, d8 b0 or escalated budget for every slower game,
   `--both --accept`, `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`, push
   wip/bruna-apply-drop ONLY.

## Key numbers
| item | value |
|---|---|
| EDF d0 memo (fix6, 200 g) | 502,965 hits / 64,077 misses, 0 mismatch |
| EDF d0 cpu on/off (user) | fix5 1.081 -> fix6 1.047 |
| Sea d0 Ir on/off (cg5) | 1.0045 |
| token lever (agent 5, 60k Sea d0) | -1928 (1841 / 114); spirits d0 +1 (gi2341, root-caused above) |
| 7a / 7b all-deck | +3 / +6 (report only) |

## USER rulings (verbatim; see handoff4 "Brief rulings" for the rest)
- "The bar is every deck net <= 0 ("+1 is not neutral. 0 is neutral."), so do NOT treat it as acceptable."
- "USER: cut CPU; 3x is not a CPU licence. Prefer exact cuts (identical play)."
- "Rule-override levers 7a ... and 7b ... break standing rules, so the USER decides; report the numbers and do NOT adopt."
- "A cast-order or keep change for Sea is a USER-owned policy, so if the only fix is a Sea cast-order rule, build it
  default OFF, measure it, and report it for the USER instead of adopting it."
- Agent-6 brief: "If every deck then nets <= 0 (searched and d0) and every slower game recovers at d8 b0, it meets the
  adoption bar. Flip it default ON and report it as a strictly-better fix. If it still fails the bar, leave it OFF and
  report the numbers. Whether to adopt it is then a USER decision." / "A confirmatory re-measure needs no ask." /
  "Keying the memo on the line cost is already ruled out as unsound." / "NEVER push phase-1-2-deck-analyzer."
- Box: big runs under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with MTG_RSS_CAP_GB=40; small
  work via `scripts/box_lane.sh --slots N` (N <= 2); a full-core build under the lock is legitimate. No timeouts, no
  pgrep -f. References are commit-only. Hand off at ~250K.
- Disclosure: agent 6 ran ~8 sub-second d0 single games inline at nice 19 (MTG_RSS_CAP_GB=2) for root causes (lane
  backlog ~2 h).
