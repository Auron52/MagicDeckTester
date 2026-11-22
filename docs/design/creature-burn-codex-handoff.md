# Creature Burn analyze-deck: handoff to Codex (2026-11-22 ~10:50 UTC)

This replaces the CHECKPOINT section of `docs/design/analysis-Creature Burn.md` (the history is kept there).
Start here. Read `CLAUDE.md` (it is authoritative for Codex too, via `AGENTS.md`). Before acting, check every
process and file named below, because some may have finished or moved on by the time you read this.

## Task
The USER asked: "Please analyze the creature burn deck." That means the full analyze-deck workflow
(`.claude/skills/analyze-deck.md`), Stages 1-6, ending with the Stage 6 report and its 6a disclosure. After that:
"After that we can try building the mulligan profile."
- **Done so far:** Stages 1-5 and the verify_deck gate.
- **Already landed:** `94cd1e06b` at 10:23 UTC 2026-11-22. CI run 45012242724 was still in progress at 10:45; check
  it with `gh run view 45012242724`, and if it is red, fix that first.
- **Not yet landed:** the work of three agents, sitting in worktrees (below). It must be integrated, validated and
  landed as ONE batch. Then the Stage 6 report, then the mulligan profile.

## Box rules (a 12-thread / ~10 GB laptop: the "slow box")
- **Memory:** two container restarts on 2026-11-22 were memory OOMs, caused by about 24 compilers running at once.
  - Run ONE heavy job (a build or a batch) at a time, under
    `flock /workspaces/MagicDeckTester/build/.heavy.lock`.
  - Builds use `MTG_BUILD_JOBS=4`. Eight jobs alone filled 10 of 10 GB.
  - Launch with `setsid nohup ... < /dev/null &`, then wait on the REAL PID or a DONE marker (never `pgrep -f`).
- **Push queue:** push only through `scripts/push_queue.py`.
  - On this box, join at most once every 4 h after the last `land`. The last land was 10:23 UTC, so the next join
    is at 14:23 UTC or later.
  - Under the lease: build Release only, `MTG_PGO=0`, smoke plus scoped regression, at most 1-2 h.
- **Never:** use `timeout`; commit `.vscode/settings.json` or `test/ref_bench.json`; touch `references/`.
- **The viewer is NOT running.** The USER said it does not need restarting. If you ever start it, use the primary
  checkout only, with `MDT_REFERENCE_BACKUP_DIR=/home/vscode/.claude/mdt-reference-backups`.
- **Mulligan generation** runs alone on the box, on a frozen commit.

## Heavy-lock queue at handoff (all setsid'd, so they survive the Claude session)
These run in order, one at a time. The Claude agents that queued them are gone: nobody is waiting on the results.
Nothing runs after the cb-scry build; the next step is yours.

1. **cb-unearth build** (flock PID 72807)
   - Log: `build/scratch/cb-unearth/build2.log`. It was at 50/367 at 10:45.
   - Output: `.claude/worktrees/cb-unearth/build/Release/` (a symlink onto `build/wt/cb-unearth`).
2. **cb-bbgap `build/scratch/cb-bbgap/chain1.sh`** (flock PID 72838)
   - Runs build, then unit, then scenarios.
   - Status: `build/scratch/cb-bbgap/status`. Marker: `CHAIN1_DONE`. Logs: `build4.log`, `unit.log`, `scen.log`.
3. **cb-scry build** (flock PID 73036)
   - Log: `build/scratch/cb-scry/build3.log`.

Each full build takes about 1-1.5 h on this box. If you would rather integrate first and build once, you may kill
queues 2 and 3. They are agent experiments, not USER-requested runs. Each worktree's A/B still needs its own binary,
though.

## The three worktrees
All three are WIP: never pushed, and none has a handoff file except bbgap's older one. Branches: `wip/cb-unearth`,
`wip/cb-bbgap`, `wip/cb-scry`. Briefs (gitignored, on the host drive): `logs/creature-burn/{unearth,browbeat-gap,scry}-brief.md`.

### cb-unearth (`.claude/worktrees/cb-unearth`)
- **Commits:** `a313b8e48`, then `16db95d19`, on top of the OLD base `dbf0f142c`. Needs a rebase.
- **USER ruling:** "It's arguable, but I think its best to just fully implement this."
- **What it does:** Hellspark Elemental's unearth "exile it instead" (CR 614 replacement) is applied on EVERY
  leave-the-battlefield path, in both the executor and the rollout:
  - destroy;
  - the lethal-damage and toughness-0 state-based actions;
  - the legend rule;
  - sweepers;
  - bounce.
  Sacrifice was already handled. New tests are in `test/unit/test_unearth_leave_paths.cpp`, and the
  `test_creature_burn_batch2.cpp` edits remove the old caveat test. Hellspark's PARTIAL/PROVISIONAL bracket note
  must be gone from cards.json; verify that.
- **Remaining:**
  1. The build, then serial `build/Release/mtg-test`.
  2. `MTG_BIN=... bash test/scenarios.sh`.
  3. Smoke scoped to the decks that play Hellspark: Creature Burn (+2HG) and Red Elementals. Expect
     byte-identical, since nothing in those lists destroys or bounces its own unearthed creature; explain any
     movement.
  4. A rules review against `.claude/skills/mtg-rules.md`.

### cb-bbgap (`.claude/worktrees/cb-bbgap`)
- **Commits:** `b372fd612`, then `67553aafd`, on top of the OLD base `dbf0f142c`. Needs a rebase.
- **Handoff (agent 1, partly stale):** `logs/creature-burn/bbgap-handoff.md`.
- **What it does (USER-ruled):**
  - **Anticipating opponent:** Browbeat (`BrowbeatOpponentTakesDamage`) and Vexing Devil judge against
    `OpponentAnticipatedLife` = life minus the visible damage still to come this turn. Per own creature, visible
    damage = max(combat damage to come, its ready tap ping). The opponent assumes we attack with everything.
    Staged cards count as held cards.
    - The shared helpers `OpponentVisibleDamageThisTurn` / `OpponentAnticipatedLife` are in SpellEffects.h.
    - The thresholds are unchanged: the opponent takes the damage iff anticipated life > 5 + (holding a card ? 3 : 0).
  - **The idle pass at EVERY search root:** a CLAUDE.md hard rule. `IdlePassMissing`; `MTG_FS_IDLE_NODE` and
    `MTG_FS_ROOT_IDLE_PASS` are DELETED, with no lever.
  - **New scenarios:** `creatureburn_browbeat_m1_anticipates_combat.json` and `creatureburn_midturn_root_pass.json`.
  - **Unit tests** in `test_creature_burn_batch1/2.cpp`, `test_needle_drop_vexing_devil.cpp` and `test_rfx_pipeline.cpp`.
- **The temporary lever `MTG_OPP_ANTICIPATE`** (heurarm `OPP_ANTICIPATE`, default ON; `=0` restores the old policy)
  still exists. It is ONLY for the A/B, and it must be DELETED before landing:
  - `OppAnticipateOn()` and the old-branch block in SpellEffects.h;
  - the `old_gate` in TurnSolver.cpp's Browbeat-flip post-pass;
  - the slot in HeuristicArm.h.
- **Remaining:**
  1. Check chain1's unit and scenario results, and fix any test-expectation slips.
  2. Run the pooled A/B, ONE batch, under the heavy lock:
     - Manifest: `build/scratch/cb-bbgap/ab/manifest.json`. 18 jobs, new vs old, covering Creature Burn,
       1 Drop Burn and Modern RW Burn at d5b20 and d3b10, plus 2HG at d5b20.
     - Run: `mtg --batch ... --threads $(nproc) --game-log-dir build/scratch/cb-bbgap/ab/logs`.
     - Compare with `python3 build/scratch/cb-bbgap/ab/compare.py build/scratch/cb-bbgap/ab/logs`.
     - Adoption bar: every slower game recovers at `--depth 8 --budget-ms 0`, and each deck's net is <= 0.
  3. `MTG_BIN=<bin> python3 scripts/ref_bench.py --deck creature_burn midrange_burn` must stay at 0/20 short. Add the
     1 Drop / Modern RW refs if they have any, since Vexing Devil moves.
  4. Delete the `MTG_OPP_ANTICIPATE` lever, rebuild, and run unit and scenarios again.
- **Findings for the report:**
  - **The main-1 "attack, then Browbeat" gap was a FIXTURE artifact:** the scenario's Champion echo was left
    unresolved and its land drop was pre-used.
    - The real residual was the missing idle pass at mid-turn roots. It is now fixed.
    - The idle-pass fix is ENGINE-WIDE. Expect it to move other decks in smoke. Validate it as a play change and
      accept the GT; it needs no A/B (CLAUDE.md).
  - **Browbeat's second-main trigger STAYS:** a haste body or Teetering Peaks played post-combat empties the hand
    without any visible damage (threshold 8 -> 5), and no main-1 order reproduces that.

### cb-scry (`.claude/worktrees/cb-scry`)
- **Commits:** `c6c3f2228`, then `e3aa05efc`, already rebased onto `94cd1e06b`.
- **USER rulings:**
  - "I don't like widths period because they provide either too many or too few options. My preference is to
    fully open it up and prune with a heuristic as needed."
  - "Ideally we would make it a relatively smart heuristic that keeps things based on impact and castability. So,
    for example, you would often keep bolt because it can fit in to the curve wherever, but you may not keep a
    3-drop if you have too many even if you have enough mana. Also, we should certainly consider pruning orderings
    of the top. Those should be something the heuristic can handle."
- **What it does:**
  - DELETES `MTG_SCRY_WIDTH` / `ScrySearchWidth`: every disposition of a look is enumerated, sized off the look count
    only.
  - Adds a provider ORDER prune for the kept-on-top cards.
  - Adds the smart Burn scry heuristic: `ScryKeepOnTop` / `ScryVerdict` in DecisionProviders.cpp, for the
    Creature Burn and Midrange Burn providers.
  - Tests: `test/unit/test_burn_scry.cpp`, plus `test_cast_scry_search.cpp`.
- **Remaining:**
  1. The build, unit tests, then scenarios.
  2. The pooled A/B:
     - Spec: `build/scratch/cb-scry/ab2/ab.json`. 10 jobs, Creature Burn and Midrange Burn, 500 games each.
     - Arms: base (binary `build/scratch/cb-scry/mtg-base`), W (width removal), then P, PF and PFO (the heuristic
       layers). Read the spec for each arm's exact env.
     - Compare with `build/scratch/cb-scry/ab2/cmp.py`. `MTG_PGO=0` on both arms. Same adoption bar as above.
  3. `ref_bench` at 0/20 short.
  4. Report units/ms per game: the fan grows to 5 at scry 2 and 16 at scry 3.
- **The width INVENTORY the USER asked for is DONE (report only):** `logs/creature-burn/width-inventory.md`.
  - Nine default-on caps are unfindable even at budget 0. The widest is the tutor width: `TutorSearchWidth`, base
    6, over library-order candidates.
  - Surface it in the Stage 6 report. Do not change those caps in this batch.

## Integration and landing (after all three pass)
1. Rebase all three onto the primary checkout's HEAD as ONE linear series. The primary has `14dd895d4` plus this
   doc, both local and unpushed.
   - Never merge.
   - `HeuristicArm.h` and `test/regression_gt.txt` will conflict. The worktrees carry no GT commits of their own,
     but the two old-base worktrees see GT diffs only because of their old base. Never hand-merge GT.
2. Build at 4 jobs under the lock. Then unit, `test/scenarios.sh`, and a check that no `MTG_OPP_ANTICIPATE`,
   `MTG_SCRY_WIDTH` or `MTG_FS_ROOT_IDLE_PASS` remains (`grep -r`).
3. At 14:23 UTC or later: `push_queue.py join`, then `push_queue.py wait` in the background. At your turn, under
   `push_queue.py hold --` and the lock, with `MTG_PGO=0`:
   1. Fetch, and rebase if origin moved (rebuild if src moved).
   2. Back up GT.
   3. Run `test/regression.sh --smoke` and attribute every moved deck. Expected movers: the Burn decks; Red
      Elementals only if unearth reached it; plus any deck the engine-wide idle pass moved. Then accept.
   4. Run a scoped `regression.sh --deck=<moved decks>`, then `test/classify_turn_later.sh regression` (churn is
      fine if it recovers at 4x/16x), then accept.
   5. Run `python3 test/check_gt_logs.py`.
   6. Run `test/viewer_protocol_check.py --strict --only Creature_Burn,Midrange_Burn --threads 4`.
   7. Commit GT, then `land`.
   8. Watch CI in the background with `gh run watch <id> --interval 60 --exit-status`.
   - If the moved set is too wide for 2 h: `requeue`, and say so.

## Then: the Stage 6 report and 6a disclosure (in the ledger and to the USER)
Items to cover:
- the Fireblast Mountain-pair fold (`MTG_CB_FIREBLAST_PAIR`, a sound fold, landed);
- the main-1 Browbeat gap diagnosis (a fixture artifact, plus the idle-pass fix);
- `PEAKS_PLAIN_FOLD`;
- Browbeat out of the archetype signature;
- the extra Peaks positions;
- the echo census (NO GAP);
- the Pyromancer response ping;
- the 1-power pinger exemption;
- the Peaks inert-class premise;
- PoP vs Peaks;
- the heavy unbudgeted tail;
- cert NotAssessed;
- Hellspark unearth (now full);
- Browbeat SECOND-MAIN RELEVANT, and why the trigger stays;
- the anticipating opponent (Browbeat and Vexing Devil);
- the scry width removal and the Burn scry heuristic;
- the width inventory;
- the GT moves, cards implemented, and profile info.

Regenerate the 6a block with `verify_deck.py` after the cards.json notes change.

## Then: the mulligan profile
- Run `bash scripts/mullgen.sh run "decks/Creature Burn" fast` (USER: "Fast is fine").
  - Alone on the box, on a frozen commit, after everything above has landed.
  - Long run: add a waiter, plus the 2-hourly status timer if it touches 12:30-20:00 UTC.
- Expand the deck's overnight rows ONCE at adoption, after `git pull` from origin, never under the lease. (USER:
  "Generating overnight once on the machine is fine ... (although if you do generate the overnight at some point, I
  recommend pulling from origin first)".)
- Do not ask the USER about regenerating keep tables after play changes: "the regeneration queue handles these
  automatically based on drift".

## Open USER questions
None. Every question so far has been answered, and the rulings are recorded above and in the ledger.

## Session close
Run `python3 scripts/check_references_safe.py`.
