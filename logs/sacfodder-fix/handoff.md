# Handoff: MTG_SAC_FODDER_MENU fix-up (agent 2 of the fix-up, stopped at ~260K on the coordinator's instruction)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/sacfodder-fix`, branch `wip/sacfodder-fix`, rebased onto
origin/phase-1-2-deck-analyzer 93634781f. Commits (local + pushed to origin/wip/sacfodder-fix only):
- `c84a2a60f` previous agent's WIP (walk gate `MTG_SAC_FODDER_WALK_GATE`, `SacFodderSubsetOrderPays`, `BuildPoSacFodder`).
- `9e73eff2f` this agent: `MTG_SAC_FODDER_NEEDED_ONLY` (heurarm slot, default ON, read only under the lever): the fodder
  credit in EnumeratePlansCore is applied only when a mana gate refuses the subset without it (admit, un-credited
  CanPay, big-only/subtype-only splits, raw colour presence), and the walk gate runs on every credited plan.
  Also a TEMP diagnostic `MTG_SFM_DUMP=1` (prints `[sfm]` per offered credited plan; TurnSolver.cpp near
  `_ct.label = "OFFERED"`) -- REMOVE before adoption.
The lever itself (`MTG_SAC_FODDER_MENU`) is still DEFAULT OFF. Units pass at 9e73eff2f (rc=0). Smoke/regression NOT run.

## Mechanism (measured)
Every slower game at play settings is BUDGET CROWDING, not a wrong line or a phantom projection:
- `MTG_DECISION_PRICED_X=0` changes nothing on any of the 7 slower games (so the priced ceiling is not the binder; the
  unit SearchBudget is).
- Goblins gi 620470 T1 m1 (MTG_FS_ROOT_DUMP=1, MTG_FSW_TRACE=1 MTG_FSW_TURN=2; build/scratch/claude_sacfodder_fix2/rc1):
  OFF completes pass 3 and finds `Cavern: Goblin Lackey` win=3 (T2 child {Mountain; Skirk, Lackey}). ON spends pass 2
  on wider T2 menus (under T1 Skirk: 81 T2 nodes vs 42 -- new children {Mountain; Piledriver, Lackey, Lackey},
  {;Piledriver, Lackey}, all eating a FRESH Lackey beside the live Skirk; under T1 no-cast: {Mountain; Skirk,
  Piledriver(,Lackey..)} eating the fresh Skirk itself) plus the new root plans {Skirk + Lackey}; pass 3+ then search
  NO T2 nodes under Lackey (win=9) and the root commits {Skirk} (tie at win 4), then main 2 eats Skirk for the Lackey:
  the same board as OFF with Skirk wasted. 620594 is the same shape (T2 {Skirk} m1, m2 eats Skirk for Mogg War Marshal).
- Fungus 630658 diverges at T1 with no Mycon in hand (drawn T3): pure budget effect of the wider deep menus.
- Most of the new Goblins plans eat FRESH bodies (cast in the same plan) that have no ETB/cast value: such a plan is
  dominated by the plan without that body (cost(X) >= the 1 R it returns; Skirk only makes R). `[sfm]` dump over 16
  Goblins games: 23,091 credited offers, 9,322 `live=1 admit=1` (beside a live Skirk, needs a fresh body).

## Candidates tried (dev seeds Goblins gi 620000-624999, Fungus gi 630000-634999, x5000, vs lever OFF; play settings)
Directories under build/scratch/claude_sacfodder_fix2/ (compare: `python3 tools/cmp2.py ab0:off <dir>:on`).
CPU of ab1/ab2 is confounded (4-5 processes x 6-8 threads), use units.
| arm | Goblins f/s net | units | Fungus f/s net | units |
|---|---|---|---|---|
| ab0 on (c84a2a60f, walk gate on) | 2/4 +2 | x1.095 (cpu x1.187) | 0/3 +3 | x1.042 (cpu x1.062) |
| diag1: no credit beside a live outlet | 0/1 +1 | x1.050 | 0/2 +2 | x1.033 |
| diag2: no credit for inert fresh bodies (CastFromHand creature, no ETB tokens, MV>=per; outlet self kept) | 1/2 +1 | x1.072 | 0/3 +3 | x1.040 |
| diag3: diag2 + outlet's own body too | 1/2 +1 | x1.065 | 0/3 +3 | x1.040 |
| diag4: diag1 + diag3 | 0/1 +1 | x1.042 | 0/2 +2 | x1.033 |
| A: no land-Aura host keying for sacfod_credit | 2/4 +2 | x1.095 | 0/5 +5 (630182/630865 come back) | x1.036 |
| B: credit ONLY when sacfod_admit (lossy: drops colour-only fixes) | 1/4 +3 | x1.088 | 0/1 +1 | x1.027 |
| D: B + diag2 | 0/2 +2 | x1.069 | 0/1 +1 | x1.025 |
| E: B + diag4 | 0/1 +1 | x1.040 | 0/1 +1 | x1.021 |
| ab3 on = 9e73eff2f NEEDED_ONLY | 1/4 +3 | x1.091 (cpu x1.166) | 0/3 +3 | x1.037 (cpu x1.049) |
| ab3 onold = NEEDED_ONLY=0 | byte-identical to ab0 on | | | |
Persistent in EVERY arm: Goblins 620594 (4->5), Fungus 630658 (4->5). These come from the core capability (outlet cast
in the plan eating board bodies / tokens), i.e. the residual is budget noise of genuinely new plans.
NEEDED_ONLY barely moves Fungus (digest-only 50 -> 47) while B (admit-only) moves it to 19: the Fungus play changes
come from plans whose credit is needed for COLOUR (Utopia Mycon's any-colour mana), not from un-needed credits.
hold7 (dump of credited plans under NEEDED_ONLY, 32 Fungus / 16 Goblins games) -> dump2/; results appended below.

### hold7 result (dump2/, `[sfm]` under NEEDED_ONLY)
Credited offers: Goblins 16 games 23,091 -> 11,902 (all of `live=1 admit=0` gone; 9,322 `live=1 admit=1` and 2,548
`live=0 admit=1` remain -- these are the fresh-body / outlet-in-plan plans); Fungus 2,658 (16 games, old) -> 569 (32 games).
The remaining Fungus `admit=0` credits are NOT board-payable: pool < need (e.g. pool=2 need=5 {Utopia Mycon, Brightcap
Badger, Saproling Burst}); `admit` is false only because SacFodderBoardShort's upper bound counts Brightcap Badger's
stamped tap grant (rock_mana). So NEEDED_ONLY removed the un-needed credits correctly, and they were NOT what moved play:
the Fungus losses come from genuinely new plans (budget crowding), as in the Goblins traces.

## Remaining, in order
1. Decide NEEDED_ONLY: it is sound but buys ~nothing (Goblins +2 -> +3 is noise-level; units x1.095 -> x1.091). Keep or
   drop it; check whether the raw `have_colors` presence test over-credits (it ignores rock / pending-Aura colours) --
   use the widened presence (WidenHaveWithSubsetRocks) if the dump shows board-payable plans credited.
2. Implement the SOUND form of diag2 (fresh-body dominance): a body cast in the same plan and eaten in it is dominated
   by not casting it when it is INERT for the rest of the plan (no ETB / cast / dies trigger, no cost reduction, no
   mana or usable activated ability, no haste grant; outlet colour single and a matching pip in its cost so the freed
   mana is as good). Natural home: a load-time flag from the card's parameter KEYS (CardDatabase::BuildParamsFromJson,
   whitelist of combat-only keys: combat_damage_puts_subtype_from_hand, attack_pump_power_per_other_matching,
   subtypes_affected, power_bonus, tough_bonus, lord_excludes_self). Zero the body credit for inert fresh bodies in
   BuildSacFodderMenuCtx (both branches) AND keep the walk (PoSacFodderWalk) consistent. Expected (diag2): Goblins
   +2 -> +1, units -2%. It is a sound collapse (CLAUDE.md: collapse wasted search unconditionally).
3. The residual (620594, 630658) is budget crowding by genuinely new plans. Options for the USER: accept a +1-ish net on
   dev seeds (bar says no), or a budget-staged admission (sacfod plans in a later wave -- the sanctioned shape for a
   restriction, inert at unlimited budget). Do NOT gate them off.
4. d8 b0 replays of the slower games of the chosen candidate (previous agent: all recover for ab0-shape).
5. Held-out A/B on FRESH seeds (dev seeds 620000/630000 are now training seeds): e.g. Goblins gi 660000+, Fungus
   gi 670000+, x10000 each, one pooled batch (tools/mk_ab.py; flags must be heurarm slots).
6. Fungus mana-clean gate repro counts off vs on (logs/mana_clean/claude-queue-20261119h2/report.md repro lines, or
   `MTG_SAC_FODDER_MENU=$on python3 scripts/mana_oracle.py fungus --seed 30090000 --game-index 90000 --games 500
   --no-identity --bin build/Release/mtg --name sfm-fun-$on`); classify with build/scratch/claude_sacfodder_menu/cls.py.
7. Units, smoke + regression from the worktree with the lever ON (report configs changed; do NOT accept GT); update
   docs/design/sac-fodder-menu-2026-11-18.md; remove the TEMP MTG_SFM_DUMP block.

## Running jobs
None after hold7 (marker build/scratch/claude_sacfodder_fix2/HOLD7.DONE). Box lock: build/scratch/claude_box.lock.

## Tools (build/scratch/claude_sacfodder_fix2/tools/)
run1.sh (parallel single-game arms), mk_ab.py (manifest), cmp2.py (paired compare), gdiff.py (two game logs).

## Brief (verbatim)
You are taking over the MTG_SAC_FODDER_MENU "fix-up" in the MagicDeckTester repo (C++ MTG simulator). The previous agent stalled for six hours on a waiter watching the wrong path and was stopped at ~246K context without a handoff. The parent committed its tree as left. Read CLAUDE.md first (binding).

USER RULING (verbatim, 2026-11-19), the goal of this work: "For 3 I would like us to fix this up. I can't think of a reason why this would reduce quality significantly. If anything it should be more quality at a slightly higher CPU cost."
Context of "3": MTG_SAC_FODDER_MENU, the plan-menu mana gate counting sac-for-mana fodder for a sac outlet cast EARLIER IN THE SAME PLAN. It was measured as ENUM-GAP 705 -> 57, and every slower game recovered at d8 b0, but the held-out A/B had Goblins net +5 and Fungus net +3, failing the bar "every deck net <= 0". "Fix up" means: root-cause why Goblins/Fungus lose turns with the lever on, fix that, and bring it back for adoption (default ON) meeting the bar. If the bar cannot be met, report the numbers. The USER expects quality to go UP.

WHERE THINGS ARE:
- Worktree /workspaces/MagicDeckTester/.claude/worktrees/sacfodder-fix, branch wip/sacfodder-fix. HEAD 4ed00373b is the parent's WIP commit of the previous agent's uncommitted changes (ManaPayment.cpp/.h, TurnSolver.cpp, test_sac_fodder_menu.cpp, including a new "walk-gate" test). Units passed 3713/3713 on that build. Base 3e8a209bc is OLD: rebase onto origin/phase-1-2-deck-analyzer (93634781f or later) first and rebuild.
- Read `git show 4ed00373b` and git log on the branch to see what it changes.
- Design docs: docs/design/sac-fodder-menu-2026-11-18.md (the lever, A/B, slower games), docs/design/goblins-sac-mana-oracle-2026-11-18.md, docs/design/sac-fodder-created-in-the-same-line.md.
- The previous agent's scratch: /workspaces/MagicDeckTester/build/scratch/claude_sacfodder_fix/.
  - ab/, d8/, slice/ hold earlier A/B and d8 material.
  - rc/q1.log holds its last repro, games 620281 and 620470 with arms off / on / ongate0. 620281: off 3, on 3, ongate0 4. 620470: off 3, on 4, ongate0 4. So 620470 is a game the lever makes slower. Find out what "ongate0" was from rc/q1.sh and run1.sh.
- NEW EVIDENCE: the Fungus mana-clean gate just ran (logs/mana_clean/claude-queue-20261119h2/report.md in the primary checkout). It shows 978 ENUM-GAP of exactly this family, e.g. T3 m1 {Utopia Mycon + Saproling Burst} paid by eating a Saproling via Utopia Mycon cast in the same plan; repro lines are in the report. Use them to show the lever closes the gap, with flag counts off vs on on those seeds.

STEPS:
1. Rebase, build, and confirm units.
2. Root-cause the Goblins/Fungus slower games (620470 first). Name the mechanism: e.g. the wider menu crowding the budget, a wrong fodder choice, or a provider ordering. Fix it within the rules: no greedy pick inside the search window; heuristics only in a DecisionProvider and only to restrict/order. Keep added CPU small and report it; the USER said cut CPU cost, never justify it by headroom.
3. Held-out A/B on the decks that move: at least Goblins, Fungus, and any deck with sac-for-mana. The bar: every slower game recovers at --depth 8 --budget-ms 0, and every deck net <= 0.
4. Units, plus smoke + regression from your worktree; report per-deck changes for the parent to accept. Do NOT accept GT.
5. Update the design doc.

MACHINE RULES (strict):
- Box lock: every build and engine run goes under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock <cmd>`, with MTG_RSS_CAP_GB=40. The lock is shared and not FIFO.
- Inside a hold, use the whole machine: pooled `mtg --batch` with --threads $(nproc), or parallel processes whose caps sum to <= 36 GB. Batch single-game repros into ONE hold, in parallel. Never hold the lock with a single-threaded or open-ended job.
- ./build.sh only; wait ~200 s after edits before building.
- Scratch in /workspaces/MagicDeckTester/build/scratch/claude_sacfodder_fix2/.
- No `timeout`. Waiters key on a PID or a DONE marker, never `pgrep -f`, never polling turns. CHECK THE MARKER PATH YOU WAIT ON ACTUALLY MATCHES WHAT THE SCRIPT WRITES (that is how the last agent stalled).
- Rebase, never merge. Push only wip/sacfodder-fix; NEVER push phase-1-2-deck-analyzer. Never touch references/.

CONTEXT: read narrowly (grep, sed ranges, tail). At ~250K tokens, finish the step, commit, write logs/sacfodder-fix/handoff.md (done with hashes, remaining in order, this brief verbatim, running jobs with PIDs/markers, measurements) and stop. Never exceed 400K.

FINAL REPORT:
- mechanism and fix commits;
- the A/B per deck (faster/slower/net, CPU);
- d8 b0 results for slower games;
- the Fungus gate repro flag counts off vs on;
- units/smoke/regression with configs changed;
- your adoption recommendation.
