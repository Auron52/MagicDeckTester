# rock-in-plan handoff 5 (2026-11-21, agent 6)

Two worktrees now:
* OLD `/workspaces/MagicDeckTester/.claude/worktrees/agent-ade0a1bc23ae96aa7`, branch `wip/rock-in-plan`, local head
  `e02588b84` (= origin's 9379d1fd9 + the fix below; NOT pushed). Lane jobs below run FROM this tree (cwd, decks,
  cards.json, and the b9 build compiles ITS src) -- do not change its src or rebase it while they run.
* NEW `/workspaces/MagicDeckTester/.claude/worktrees/rock-rebase`, branch `wip/rock-in-plan-rb`, head `d2e2277f2` =
  the whole rock-in-plan stack rebased onto origin/phase-1-2-deck-analyzer (c9a2e1ff6 at rebase time). HeuristicArm.h
  conflict resolved (append both); the 9379d1fd9 GT accept was DROPPED (all test/ files = origin's; check_gt_logs: 1358
  consistent, 0 stale). GT must be re-accepted on this tree. Push target: `git push -f origin
  wip/rock-in-plan-rb:wip/rock-in-plan` (the wip branch is ours; the brief asked for the rebase + push). NEVER push
  phase-1-2-deck-analyzer; never join the push queue (the parent lands it).

Scratch `S=/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/`. `$S/wins_cmp.py <base.wins> <new.wins> [max_turns]`
prints faster/slower/net (unwon = max_turns+1, default 8).

## Done

1. **Niv d0 root cause** (smoke d0 niv +20: 21 slower / 14 faster). Replays `$S/d0/g82_{0,1}`, `g373_{0,1}` (+ .txt
   renders): the rung's oracle repair table CRACKED Generator Servant (direct-action "{T}, sacrifice: {C}{C}") to pay
   Izzet Signet's {2} so the rest of the line stayed payable. gi82 (seed 1083) T3: Servant gone, no attack, T5 ->
   unwon. gi373 (seed 1374) T4: Curiosity was on the Servant; T6 -> unwon. The engine's own rule is that a SacAction
   source makes mana only through a plan's SacForMana action (ManaOracle ENUM-GAP pass, NoSacActionScope); the rung's
   MinimalPayments call ignored it, i.e. the rung made a sacrifice no search decided.
2. **Fix** `MTG_LKR_NO_SAC_ACTION` (default ON, heurarm slot LKR_NO_SAC_ACTION, per-job overridable): the rung's table is
   built under the new `manaoracle::NoSacActionRouteScope` (ManaOracle.h/.cpp), and a direct-action sac source (not a
   land/dork/rock template, not pay-sac, not on-demand) is then no guard on the rock's tight test (CPU cut). Commit
   e02588b84 (old tree) = d2e2277f2 (rebased).
3. Binary `mtg.b8` (table scope + an INEFFECTIVE guard variant -- it read HumanPreTapOfferFor's route, which is unset
   for a sick Servant; replaced by the structural predicate in e02588b84):
   * Niv 200 (680000) with `MTG_LKR_NO_SAC_ACTION=0`: 0 diffs vs logs_pfx1 (flag-off path byte-identical).
   * `MTG_LKR_TIGHT_CHECK=1` Niv 200: 0 `[lkr-tight-bad]`.
   * chain9 (big lock, 32 thr): **112/112 non-Niv on-arm .wins byte-identical to ab3**; Niv off arm identical to ab3.
   * **Niv searched held-out, new vs off: set 660000x1000 net -3 (3 faster, 0 slower); set 670000x2000 net -14 (15
     faster, 1 slower: gi367 seed 670367 T4 -> T5, already slower under the old lever). Pooled 3000: net -17, 1 slower.**
     (Old lever vs off on the same pairs: +1 and -14.) Files `$S/logs_b9`, `$S/b9.out`, base arms `$S/logs_ab3`,
     `$S/logs_niv2`.
   * Niv CPU on/off on the 1000-pair slice: **1.0617** (units 0.9963) -- b8 has no guard cut yet; was 1.0630.
   * Units with mtg-test.b8: 3719/3720; the one failure (test_pirates_provider:413, folders 60 != 59) is
     ENVIRONMENTAL: the test resolves decks/ exe-relative, and a copied binary under build/scratch resolves the PRIMARY
     checkout, which gained "1 Drop Burn". Not ours; the rebased tree's own build should pass.

## Running / queued jobs (check with `kill -0 <pid>`; never pgrep -f)

* **b9** was KILLED by agent 6 (its own job: the lane build of mtg-test at 2 jobs would have taken ~1.5 h). Its mtg
  had already linked from the final src; it is snapshotted as `$S/mtg.b9x` (= e02588b84). The probes moved to the
  FRONT of chain10 (below): Niv 200 (680000) stats `$S/st2.err` (b9x) paired with `$S/st1r.err` (b8, same time),
  tight-check `$S/tc11.err`, and the d0 Niv 3-arm (off / c7 = KEEP_ROCK on + NO_SAC_ACTION=0 / on), 30,000 pairs each,
  seed base 710000 (`$S/d0ab/niv3arm.json` -> `$S/d0ab/logs_3arm`, `3arm.out`).
  Checks: (a) `logs_st2` vs `logs_st1r` (and logs_st1) .wins identical (the structural guard cut must be exact vs the
  b8 play); (b) `tc_bad` in chain10.status = 0; (c) CPU: `[line-keep-all] ... ms=` and `[line-keep-phase] rock
  strand-nofire` st2 vs st1r; (d) d0: `cat $S/d0ab/logs_3arm/off__*.wins > off; ... on ... c7`, then
  `python3 $S/wins_cmp.py off on` -- on vs off must net <= 0 (c7 vs off shows the old lever).
* **d8_367** (lane, 2 slots, waiter PID 256620): `$S/d8_367.sh`, Niv seed 670367 gi 367 at `--depth 8 --budget-ms 0`,
  both arms (mtg.b8), `$S/d8_367/a{0,1}.out`, DONE `$S/d8_367.done`. Must recover (same turn both arms).
* **chain10** (big lock, flock PID 263392, queued 15:34 behind two others): `$S/chain10.sh`: the probes above, then in the NEW tree: full-core
  `./build.sh`, units (`$S/chain10.units.log`), then `THREADS=$(nproc) MTG_RSS_CAP_GB=40 bash test/regression.sh --both`
  (`$S/tiers10.log`; results in the new tree's test/results + test/logs). Status `$S/chain10.status`, DONE
  `$S/chain10.done`.

## What remains, in order

1. Read b9 results (checks a-d above). If (a) or (b) fails, the structural guard cut is not exact: revert just that
   predicate (keep the table scope), say so, and re-run. Record the CPU numbers.
2. Read d8_367 (must recover).
3. After chain10: audit every changed case vs origin GT per `.claude/skills/regression-testing.md`; the bar is per DECK
   net <= 0 at searched depths AND at d0 over smoke + regression, and every searched slower game recovers at d8 b0
   (`scripts/box_lane.sh --slots 1`). Then, in the NEW tree under claude_box.lock (not the lane):
   `bash test/regression.sh --both --accept` (check how the previous accept was done; it may re-use the inspected run),
   `python3 test/check_gt_logs.py`, commit GT, push `wip/rock-in-plan-rb:wip/rock-in-plan` (force; wip branch only).
4. Update docs/design/rock-in-plan-2026-11-19.md (the "Niv d0 root cause" section is already there) with the numbers.

## USER rulings (verbatim)

* rock-in-plan: "Sounds good. Let's turn it on if the unwon looks okay. We should indeed support this." (gi152 settled
  as clear clairvoyance by agent 5.)
* Bar: every slower game must recover at `--depth 8 --budget-ms 0`, and every DECK's aggregate must be net <= 0 ("+1 is
  not neutral. 0 is neutral."). There is NO documented lighter bar for d0.
* "3x is not a CPU licence" -- cut the lever's CPU with exact cuts (byte-identical play, proven by an identity slice).
* Parent: full tiers never in the lane; one big job under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock`
  at `nproc` threads, MTG_RSS_CAP_GB=40; small work via `scripts/box_lane.sh` (<=4 slots).
* Carried: "MTG_LINE_KEEP_ALL (the general widening) is NOT adopted ... the rock lever must stand on its own measurement".
