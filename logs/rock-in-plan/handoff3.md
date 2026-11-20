# rock-in-plan handoff 3 (2026-11-20 ~22:50 UTC, agent 3 at ~253K context)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-ade0a1bc23ae96aa7`, branch `wip/rock-in-plan`. Push ONLY that
branch, and NEVER push phase-1-2-deck-analyzer (the parent lands it). Scratch is `S=/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/`.
Read handoff2.md first (the mechanism, ab/ab2/ab3, the d8 results). This file covers what agent 3 added.

## Done

* `bc710d3f3` (local, NOT pushed) contains:
  * Default flip: `EnvOn("MTG_LINE_KEEP_ROCK", true)` in src/ai/EngineFlags.h, with the comment updated.
  * Exact CPU cut 1: `LineBillPayableOn(GameState&)` judges the rest ON a scratch board the caller discards, which saves
    one GameState copy per held-out set paid and per table option made (ManaPayment.cpp). Verified exact by chain1:
    all 80 `logs_nivx/*.wins` (Niv 1000 pairs, both arms, seeds 660000) are byte-identical to `logs_ab3`.
  * Exact CPU cut 2 (UNVERIFIED, built by chain2): a two-step oracle table ask in the rung. MinimalPayments' lazy stop
    returns a prefix of the same table, so it first asks for `MTG_LKR_TABLE_PREFIX` options (default 1; 0 = one full ask
    of 64). If none of those keeps the rest, it asks for the full 64 and continues from where the prefix ended.
  * Exactness guard (found by reading): no tight test for a RAMP-FILTER rock (Signet) on a board with an untapped
    `is_filter` land (Cascade Bluffs) that one of the rock's colours can feed. The Signet's {U} into the Bluffs makes two,
    and when the Signet's own feed was {C} the board alone cannot feed the Bluffs in its place, so the tight bound stops
    being a necessary condition. The diagnostic never saw such a case, and ab3 == ab. This guard can only re-admit
    repairs, so play can only move toward the uncut lever.
  * Print-only stats under MTG_LINE_REDUCER_KEEP_STATS: phases now split `fired-held` and `fired-table`. A new
    `[line-keep-work]` line gives held_try, held_paid, tables, table_opts, opt_try, table_ms, and fire_idx 0/1/2/3+.
  * Unit tests pinned for the flip (the 3 cases that failed in chain1's units run with the default ON):
    test_line_keep_all.cpp (the off arm also pins LINE_KEEP_ROCK off); test_niv_payment_holes.cpp (new block: with the
    feed route OFF, the rock keep ON alone keeps the Servant; the hole block pins both off); test_angels_cast_order.cpp
    (the LINE_SPARE_TIGHT loop pins the rock off, plus a third arm: spare-tight off, rock on, so the hand is empty).
    Not yet re-run, because chain2 runs them.

## Niv CPU: where the +6.5% goes (measured)

* 200-game Niv probe (seeds 680000, lever on, stats): the rung's own time is 16.6 s, about 8% of the on arm's CPU. That
  matches the ab3 Niv numbers: units 0.9942 vs CPU 1.065, i.e. the extra CPU is per-unit cost, which is the rung.
* Rung phases (ms, chain1 build): other fired-table 4072 (13.8k calls at ~295 us), rock fired-table 2706, rock
  strand-nofire 3606, rock filtered (witness) 2904, other filtered 1078, fired-held only ~590, joint-unpayable ~520.
* Call graph of the heavy game gi58 (seed 680058; perf dwarf, g58.perf.data): the rung is 10% of the game. Within it,
  manaoracle::MinimalPayments (the oracle table, mostly PayEnum::Verify) is 41%, the greedy payments
  (TapForCostSharedImpl: trial, witness, hold-outs) are 32%, and GameState copies are ~8% direct.
* 156.7k tables per 200 games: about 1/3 fire and 2/3 are no-fire strands (which need the full table to prove no
  option works). Average 4.1 options per table.
* Cut 1 alone (chain1) took the rung from 16.6 to 15.8 s. The nivx CPU on/off ratio of 1.0695 is within noise of ab3's
  1.0650.
* Biggest remaining exact cut not taken: rock strand-nofire on Niv boards. The tight test is off there because of the
  guards (an untapped Izzet Signet / Ferrous Lake = ramp_filter; a Generator Servant = sac-for-mana). The top
  `[lkr-nofire]` sample is rock=Izzet Signet {2}, rest {U}, board Cascade Bluffs + an untapped second Signet. Extending
  the tight test soundly across those guards needs a proof per converter shape (two generic-fed converters both fed by
  the rock's outputs break the one-feed exchange argument). It was not attempted.

## Running job: chain2 (agent 3's own run)

* Lane job wrapper PID **4005469**: `scripts/box_lane.sh --slots 4 --cap-gb 6 -- $S/chain2.sh`. Lane log
  `$S/chain2.lane.log`, which is empty until admitted. Status lines go to `$S/chain2.status`. DONE marker `$S/chain2.done`
  holds FAIL (build), STOPPED (units failed or the slice was not identical; tiers skipped), or DONE.
* Steps: `./build.sh` (log `$S/chain2.build.log`), then units (`$S/chain2.units.log`), then 3 Niv prefix probes
  (`MTG_LKR_TABLE_PREFIX` = 1, 0, 4; stats on; `$S/pfx{1,0,4}.err/.out`, `$S/logs_pfx*`), then the exactness slice
  `manifest_exact2.json` (Niv 1000 both arms plus 14 rock decks x 200 on-arm, all ab3 jobs; `$S/exact2.out`,
  `$S/logs_exact2`), then IF units rc=0 AND 0 diffs: `env THREADS=4 MTG_PGO=0 bash test/regression.sh --both` from the
  worktree (log `$S/tiers.log`; results in the worktree's test/results + test/logs).
* A waiter on it ran from agent 3's session and dies with it. Arm a new one:
  `until [ -f $S/chain2.done ]; do sleep 30; done` (run_in_background). The tiers at 4 threads take hours.

## What to check

1. `cat $S/chain2.status`. "units rc=0" is required. If rc != 0: `grep -nE "ERROR|TEST CASE" $S/chain2.units.log`. The
   3 pinned tests were edited blind (the first Niv-hole block's checks were taken from the failure lines; the Angels
   third arm asserts an empty hand), so fix any test whose expectation is wrong, never the engine to fit it.
2. The exactness bar: "exact2 diffs=0" (every logs_exact2 .wins byte-identical to logs_ab3) AND "pfx1-vs-pfx0
   diffs=0", "pfx1-vs-pfx4 diffs=0" (every prefix value gives the same games). Anything else means the lazy table or
   the Bluffs guard moved play: find the job with `cmp`, then bisect with MTG_LKR_TABLE_PREFIX=0.
3. CPU: `grep -h "line-keep-all\|line-keep-work\|fired-table\|nofire" $S/pfx{1,0,4}.err`, comparing the rung ms
   (`[line-keep-all] ... ms=`) and table_ms across prefixes. pfx0 = the old single full ask. Pick the default with the
   lowest rung ms; play is the same for every value, so a change needs no tier re-run. The Niv on/off CPU from the
   exactness slice: `python3 logs/rock-in-plan/compare.py $S/logs_exact2 $S/exact2.out` (only the niv row has both
   arms). Before: 1.0650 (ab3) / 1.0695 (nivx, cut 1).
4. Tiers (`$S/tiers.log`; the per-game audit prints at the end): audit every changed game per
   .claude/skills/regression-testing.md; run `bash test/classify_turn_later.sh <mode>` and d8 b0 on any searched slower
   game; compute the net per deck. If every deck nets <= 0 in smoke AND regression: `bash test/regression.sh --both
   --accept` in the worktree, then `python3 test/check_gt_logs.py`, then commit GT. If any deck nets > 0: STOP and report,
   do not accept.
5. Finish docs/design/rock-in-plan-2026-11-19.md (the CPU section: the numbers above, the two exact cuts, the Bluffs
   guard, the verdict). Commit, `git fetch && git rebase origin/phase-1-2-deck-analyzer` (rebuild + smoke if src
   replayed), then push `wip/rock-in-plan` only.

## USER rulings (verbatim, carried from the briefs)

* "NOTE: MTG_LINE_KEEP_ALL (the general widening) is NOT adopted. The USER asked to adopt it only if positive on
  average, and AccomplishedAlchemist measured net +2/30k. So the rock lever must stand on its own measurement and must
  not reintroduce LKA's AA regression; include AccomplishedAlchemist in the A/B if the lever is live there."
* "The bar: every slower game recovers at --depth 8 --budget-ms 0, and every deck net <= 0. Report CPU per deck. The
  USER says cut CPU cost; never justify it by headroom."
* "If the bar is met, flip it default ON. Then units (full), plus smoke + regression from the worktree; report
  per-deck changes for the parent to accept. Do NOT accept GT."
* "If the bar is NOT met, root-cause the slower games and report; do not ship default ON."
* Agent 3's brief (the parent, superseding "do NOT accept GT" for this step): "Prepare (do NOT land) the default-ON flip
  in your branch ... run the full unit suite, then `THREADS=4 MTG_PGO=0 bash test/regression.sh --both`, audit every
  changed game (per the skill), run classify / d8 b0 on any searched slower game, and if the net is <= 0 per deck accept
  both tiers in your worktree (`--both --accept`) and `python3 test/check_gt_logs.py`. Commit and push wip/rock-in-plan.
  If a deck nets > 0 in smoke/regression, stop and report rather than accept." and "USER rule: a fix's CPU cost must be
  cut, never justified by headroom." and "Niv CPU: ... cut it EXACTLY (play byte-identical; verify by .wins identity on a
  Niv slice) ... If no exact cut exists, say why."
* Machine: the small-job lane only (`scripts/box_lane.sh`, <= 4 slots); ./build.sh only; wait ~200 s after edits (9p
  lag); delete touched objects on the build volume before building; no `timeout`; waiters on PID / DONE marker (never
  pgrep -f / pkill -f); rebase, never merge; never touch references/.

## Open USER question (from the parent)

* Niv-Mizzet Combo: +1 on the first 1,000 pairs (3 faster / 4 slower, all 4 slower recover at d8 b0), -14 on 2,000 fresh
  pairs, -13 pooled over 3,000. The default-ON flip is prepared on the pooled reading. A 200-game Niv probe on seeds
  680000 (not an A/B) also showed gi152 on unwon vs off T8 and gi56 on T5 vs off T6.
