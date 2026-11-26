# Elves Genesis Wave post-put continuation -- handoff 2 (2026-11-26)

Worktree: `/workspaces/MagicDeckTester/.claude/worktrees/agent-a0234af8f175346b7`, branch `wip/elves-wavebp`.
Read `logs/elves/wavebp-handoff.md` first (mechanism, design, code sites, USER rulings). This file adds what
happened after it.

## Done (commits on wip/elves-wavebp, not pushed)
- `22bdb2433` WIP fix (handoff 1).
- `f11f1cab7` the fixes that made it pass:
  - `PlanOpensBreakpoint` land-search mark (`land_put_by_cast`) skips three shapes whose CAST cannot put a card
    permanent on our battlefield: an activated search (`land_search_activation_cost`: fetches, Khalni), Karametra's
    cast watcher (`karametra_cast_land_search`), Path at the opponent (`RemovalSearchesForController && enchant_target
    <= 0 && !HumanPlayActive()`). Without it their marked plans emitted variants that collapse onto the base plan.
  - `ElvesScalingAdmit` (DecisionProviders.cpp) blanks `bp_base` in the canon, so a Wave's bp variants group with the
    same (k, at) variant at the other X; the band had been applied to base plans only. Not behind the hatch (Elves only).
  - Unit tests: Path counts base plans only (`bp_choice == -1`); vampires gi24 commits the empty continuation
    (`kBpEmptyChoice`, `bp_all`); the Wave test checks X=2 and returns early under `HumanPlayActive()`.

## Measurements
- Repro seed 9500006 (`build/scratch/elves-wavebp/repro.sh`): new T4 at d5 b200, d5 b0, d3 b0; hatch off T5 at d5 b200, d5 b0.
- Units: the full default suite passes, 3966/3966. Subset (`build/scratch/elves-wavebp/subset.sh new <wt>`: test_elves*,
  test_path_to_exile, test_vampires_payer, test_landfall.cpp). Under UNPRUNED, 6 cases fail and under HUMAN, 12 fail.
  The lists are in `logs/elves/wavebp_sub_new_{unpruned,human}.fails`. The Genesis Wave test passes in both modes.
  **These failures are not yet compared against the base**; see remaining step 1.
- fd sweep: 0 `[fd-diverge]` in 200 Elves games (seed 7001, d5 b20, avg 4.33).
- Smoke (new): 42 keys move. 9 searched games are slower and 16 faster; one d0 game (gods gi308) changed its play at the
  same score. Keys and slower games are in `logs/elves/wavebp_smoke_moved.txt`. Logs are in
  `build/scratch/elves-wavebp/smoke_new_logs/`.
- **Hatch faithfulness: smoke with `MTG_BP_PUT_BATTLEFIELD=0` gave 243/243 PASS against the committed GT.** The hatch
  arm IS base play for every suite deck. For Elves it is not exact, because the provider bp_base blank is unhatched.
- A/B (hatch 0 vs 1, one pooled batch, seed 9600001, the suite's d5 row shape, arms innermost and alternated):
  `logs/elves/wavebp_ab_report.txt`, raw output `build/scratch/elves-wavebp/ab.{out,err}`. Providers are identical
  across arms. Every deck's net is <= 0 except elementals +0.0025 (1 better, 2 worse), pd +0.0025 (0/1) and pd2hg
  +0.0050 (0/1). The ms ratio (new/base) is landfall 1.40x, creature_giving 1.39x, cg2hg 1.40x, melira 1.34x, hydras
  1.32x, goblins 1.23x, jumbo 1.21x, pd 1.19x, galadriel 1.17x, shadowborn 1.15x, selesnyav2 1.11x and <= 1.07x for
  the rest. Ten slower games are listed in the report with their seeds (seed = 9600001 + gi).

## Remaining, in order
1. Base-binary unit subset: the base worktree `.claude/worktrees/wavebp-base` (detached at 4fc09f9b3) is being
   built with `MTG_BUILD_JOBS=14`. The log is `build/scratch/elves-wavebp/buildbase_j14.log`, which gets an `rc=` line.
   If it failed, rerun: `cd .claude/worktrees/wavebp-base; MTG_BUILD_JOBS=14 MTG_RSS_CAP_GB=20 flock
   /workspaces/MagicDeckTester/build/heavy.lock ./build.sh`. Then `bash scripts/box_lane.sh --slots 2 -- bash
   build/scratch/elves-wavebp/subset.sh base /workspaces/MagicDeckTester/.claude/worktrees/wavebp-base`. Diff the
   resulting `sub_base_*.log` failure names (`failnames.py`) against the new ones. There must be no new failures.
   (test_boars_fetch_rank failing under UNPRUNED is expected.) Run binaries as `build/Release/...` from inside the
   worktree, NEVER a copy in scratch: a copied binary resolves cards.json from the PRIMARY checkout and fails.
2. Check each slower game at `--depth 8 --budget-ms 0`: the 10 A/B games, plus the 9 smoke games in
   `wavebp_smoke_moved.txt` (smoke seed = 1001 + gi). Run new vs `MTG_BP_PUT_BATTLEFIELD=0` with the deck/profile,
   2HG `--starting-life 30 --opponent-heads 2` where the key is *2hg, and the creature_giving budget is 30.
   Use `scripts/box_lane.sh --slots 1`. Report recovered / not.
3. Delete the `MTG_BP_PUT_BATTLEFIELD` hatch in a final commit (coordinator decision 2: "delete, don't lever").
   Remove `BpPutBattlefieldEnabled()` (EngineFlags.h), the `BP_PUT_BATTLEFIELD` heurarm slot AND its name-table entry
   (HeuristicArm.h, both lists), and every `BpPutBattlefieldEnabled() &&` guard (AIEngine.cpp x4, TurnSolver.cpp x3).
   Also remove the "=0 -> as before" text from the comments and the cards.json Wave note (edit as text).
   `grep -rn BpPutBattlefield src test logs/elves/wavebp_*.py`. Rebuild with `MTG_BUILD_JOBS=14 MTG_RSS_CAP_GB=20 flock
   .../heavy.lock ./build.sh`, then rerun the plain units and the new subset.
4. `.claude/settings.local.json` is already byte-identical to the `.bak` (checked); nothing to restore.
5. Final report to the coordinator.

## Coordinator decisions (verbatim, from the brief)
1. Old-play arm: first prove the escape hatch is faithful -- run smoke with MTG_BP_PUT_BATTLEFIELD=0 and confirm every
   key's digest equals committed GT ... If it is, use hatch arms (=1 vs =0) in ONE pooled batch for the held-out A/B on
   each deck that moves ... report per-deck net win-turn delta, slower games, and check each slower game at --depth 8
   --budget-ms 0.
2. After the A/B, DELETE the MTG_BP_PUT_BATTLEFIELD hatch in a final commit ... Rebuild and re-run units after deleting it.
3. Do not narrow the trigger with a heuristic for Landfall/Selesnya cost. Report their ms/game before and after ... If you
   see a sound reason a given put cannot change the decision, propose it in the report; don't implement it.
4. Restore the worktree's .claude/settings.local.json from .../settings.local.json.bak before you finish.
- Later: "Do NOT rerun the full suite under MTG_UNPRUNED=1 or MTG_HUMAN_PLAY=1" -- the subset only, new AND base.
- Later: "Prefix every ./build.sh with MTG_BUILD_JOBS=14 from now on (this box, this session only)." ENOMEM is real RAM.
- Binding: everything heavy goes under `flock /workspaces/MagicDeckTester/build/heavy.lock`, with MTG_RSS_CAP_GB=20. No
  `timeout`. No pushes to the shared branch and no --accept. Never touch references/.

## Proposals for the report (not implemented)
- A DECLINED land search (`land_search_pins` front == kLandSearchPinMask) puts nothing, so marking that plan is pure
  variant waste. It is sound to unmark, but the pin-to-action mapping is plan-level, so this was left alone.
- A put land that enters TAPPED and has no static/triggered text adds no option this main beyond what the land count
  already priced in. That is a candidate sound no-op for Path / Search for Tomorrow / Rampant Growth etc., but it needs
  a proof per reader (domain, land-count scaling, landfall, already fired). Not implemented.
