# Handoff: KittyEquipment mana-clean gate (branch `wip/mc-kitty`, 2026-11-04)

Worktree `.claude/worktrees/mc-kitty`, based on origin/phase-1-2-deck-analyzer `b379c41b4`. Not pushed.
Input: `logs/mana_clean/pass1-kittyequipment-r2/report.md` (src 644270cdc4ce, 42,000 games): UNMODELED 56,506 (all
"combo-route macro"), ENUM-GAP 183 (178 signatures). All changes so far are ORACLE-ONLY (play byte-identical).

## Commit

`927b6156e` fix(mana-oracle): audit the equip route's macro link by link; ENUM-GAP keyed as the menu, noop chain in
both orders. HEAD:src = `2d767a1caac79e99b319ec329ff1e4d1b8141d14`. (Squash with this doc before landing.)

## Mechanisms

1. **UNMODELED "combo-route macro" -> audited.** The equip route (`equiproute::Run`) realises its macro as a chain of
   plain menu plans, each applied with `ApplyPlan`. Every link apply now goes through
   `manaoracle::RouteLinkApply` (`src/ai/ManaOracle.cpp:41`; call sites `src/ai/DecisionProviders.cpp:42710, 42910,
   43128, 43220`), which is `ApplyPlan` plus a tape that only the oracle's `EngineApply` arms (`RouteTape`,
   `src/ai/ManaOracle.h`). The trial inside `EquipRouteApply` (`DecisionProviders.cpp:43406`) runs under
   `RouteTapeSuspend`; a losing DFS branch is rewound (`RouteTapeMark/Rewind`). `AuditRoute`
   (`ManaOracle.cpp:2874`) audits each recorded link with `AuditPlan(link.before, link.plan, link.after/drops)` inside
   `EquipRouteLinkFrame` (`DecisionProviders.cpp:43435`: InRoute + the DFS flag, so the oracle's applies replay the
   route's). Flags carry the prefix `equip-route link k/n of {...}:`. Summary line: `=== MANA ORACLE combo routes: N
   equip-route macros audited, M links, X ms ===`. EDF / Alchemist routes (hand-rolled payments) stay UNMODELED. A
   route apply that finds no kill reports UNMODELED "equip route: the replay found no kill" (never seen yet).
2. **ENUM-GAP, 147 of 183 (the "fewer resources: ...plan#N 1>0" shape): oracle keying asymmetry.** `JudgeHit`
   (`ManaOracle.cpp:3214`) runs inside `Realize`, which keys the plan's own sources apart (`t_plan_srcs`: the equip's
   Equipment and creature; `t_key_twin_taps`), while the menu's boards (`MakeMenuView`) are keyed outside. Same board,
   different keys -> "fewer resources". Fix: `JudgeHit` computes unkeyed (scope `Unkeyed`). Repro seed 30009252 T3.
3. **ENUM-GAP, the other 36 (no matching core): noop reduction order.** `PerformedPlan` (`ManaOracle.cpp:2674`)
   removed actions greedily in plan order; a cast that the Sram-draw continuation re-makes anyway also "does
   nothing", so it dropped one Bone Saw copy first and ended on a plan no enumeration offers (the menu has the plan
   less only the equip, which Sigarda's Aid made a no-op). Fix: it returns its whole reduction chain; the noop test
   (`ManaOracle.cpp:3324-3325`) runs it activations-first AND in the old order and accepts any chain plan that is on
   the menu / enumerated (each is realised by the same script on the same board, so sound; the old final is still
   checked, so only flags can go). Repro seed 30001917 T2.

No ENUM-GAP turned out to be an engine enumeration miss: every one examined was oracle-side.

## Measurements (new = Release of `927b6156e`)

| check | result |
|---|---|
| the 108 games holding r2's 183 ENUM-GAPs (oracle 2) | ENUM-GAP 0, UNMODELED 0, MISS 0; 137 route macros audited, 454 links, 0 issue flags (417 DOMINATED-LAND, disclosed) |
| play identity, old (primary `build/PGO`, src 644270) vs new Release, oracle off, KittyEquipment gi 0-1999 (20 jobs x 100) | 20/20 job digests and units identical |
| unit tests (full `mtg-test`) | 2563/2563 pass; new test "route tape: ..." in `test/unit/test_mana_oracle.cpp` |
| r2 on gi 0-1999 (before) | ENUM-GAP 10, UNMODELED 2721 |
| new on gi 0-1999 (after; same 13,220 decisions) | **ENUM-GAP 0, UNMODELED 0, MISS 0, ORACLE-GAP 0**; 2721 route macros audited (= r2's 2721 UNMODELED), 9564 links, no ISSUE flag on any link; DOMINATED-LAND 2239 (disclosed) |

Oracle CPU: r2 ms_on 50.07M vs ms_off 24.75M (oracle ~602 ms/game, PGO). New, gi 0-1999 (Release, contended box):
oracle total 2,340,101 ms (~1,170 ms/game; not comparable with r2's PGO number), of which the route audit is 8,316 ms
= **0.36%** of oracle CPU (~4.2 ms/game). The gate's fresh-block run (PGO) gives the like-for-like total.

## Running jobs (leave alive)

* (finished) the oracle-on gi 0-1999 run: `build/scratch/mc-kitty/on_new.{out,err}`.
* pid 3154233 `buildq.sh pgo` (PGO_THREADS=8) in the worktree, log `build/scratch/mc-kitty/build_pgo.log`; was still
  waiting for a build slot (mc-lfe, mc-bruna held both). On finish `build/PGO/SRC_TREE` should equal `2d767a1c...`.

## Next steps

1. (done) same-game before/after: see the table.
2. When PGO is built: the FINAL gate on a FRESH block (scratch state = copy of the primary's untracked
   `test/mana_clean.json`, already at `build/scratch/mc-kitty/mana_clean_state.json`, last block 0-41999):
   `cd .claude/worktrees/mc-kitty && MTG_RSS_CAP_GB=43 python3 scripts/mana_clean.py run kittyequipment --games 5000
   --threads 8 --state /workspaces/MagicDeckTester/build/scratch/mc-kitty/mana_clean_state.json --name
   mc-kitty-pass2` (runs both arms, checks play identity on/off; games 42000-46999).
3. `THREADS=8 bash test/regression.sh --smoke` (oracle-only change: expect zero moved rows; do NOT accept GT).
4. Add a section 13.13 to `docs/design/mana-completeness-oracle.md` (mechanisms above + the gate table), squash this
   doc and the WIP into reviewable commits; the coordinator lands it.
