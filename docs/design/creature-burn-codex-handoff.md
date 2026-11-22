# Creature Burn analyze-deck: handoff to Codex (2026-11-22 ~10:50 UTC)

## Codex continuation (2026-11-22 ~21:50 UTC)

**23:15 UTC integration update:** The detached full smoke finished all 243 cases (206 matched the prior GT,
37 moved; `PREQUEUE_SMOKE_DONE=1` reflects expected GT drift). Blood Sun then landed as `249db2aa7` with
substantial `src/` and GT changes, invalidating that smoke as a final landing gate. Our 18 local commits were
rebased onto it as `ca844988c`; `HeuristicArm.h` keeps Blood Sun's slots and the final Burn scry slots, and
`TurnSolver.cpp` keeps the Revelation wave helpers while the temporary root-pass counter is removed. The user's
two local file edits were stashed and restored. We are push-queue head, with a detached 90-minute heartbeat in
`queue-hold-profile.log`. A four-job integrated Release build is running under `build/.heavy.lock`
(`postblood-build.log`, `POSTBLOOD_BUILD_DONE`). Next: unit/scenarios/reference/deck gates, a fresh full smoke
and per-game audit against the new Blood Sun GT, scoped regression, GT accept, and `land`. Requeue if the validation
exceeds the head window, then launch the fast profile after landing.

**23:33 UTC queue update:** Worldfire joined during the full integrated build. This session requeued behind it
to keep the head lease within the 1–2 hour validation window; the old heartbeat was stopped. The four-job build
continues locally (`POSTBLOOD_BUILD_DONE` still pending). A fresh `run-queue-wait-profile.sh` waiter is active;
the old `QUEUE_PROFILE_READY` marker was archived as `.previous`.

The previous full smoke was interrupted when its attached tool session stopped, and the queue lease passed to
Blood Sun. No user decision is pending. Rejoined the queue as number 1 behind Blood Sun; a detached waiter
tracks `build/scratch/cb-integrated/QUEUE_PROFILE_READY`. A detached prequeue full smoke is now running under
`build/.heavy.lock` (`prequeue-smoke.log`, `PREQUEUE_SMOKE_DONE`). Its binary is the committed Release source at
`0a522c2ae`; if origin changes before landing, compare `HEAD:src` and deck trees after rebase before trusting it.

The focused Burn `--smoke --regression` batch completed (42 cases, 10,526 games; `burn-gates.log`,
`BURN_GATES_DONE=1` because GT has not yet been accepted). All 610 scenarios, 940 strict reference replays, and
both suboptimal-reference games passed. `burn-classify.log` / `BURN_CLASSIFY_DONE=0` classify all searched slower
games. Both Creature Burn 2HG smoke slowdowns recover at 4x budget. Creature Burn's persistent smoke gi98 changes
the kept hand; it remains T5 at d8 b0. The persistent 1 Drop Burn slower games are the requested Vexing Devil
opponent-policy change (opponent declines four damage before a lethal attack), confirmed for representative
smoke gi87 and regression gi80 at d8 b0 (`deep-persist.log`, `DEEP_PERSIST_DONE=0`). The baseline+changed per-game
audit is in `burn-gates.log`. No GT is accepted or committed yet. Preserve the user modifications to
`.vscode/settings.json` and `test/ref_bench.json`.

Next: finish full smoke, audit every moved deck and classify searched slowdowns, then accept. Run the scoped
regression for any additional moved deck, accept, check GT logs, commit GT, land through `push_queue.py`, and launch
`bash scripts/mullgen.sh run "decks/Creature Burn" fast` alone under the heavy lock on the frozen landed commit.

## Codex continuation (2026-11-22 ~17:30 UTC)

No user decision is pending. The PO scry arm (pick plus kept order, firm default OFF) was adopted after the
500-paired-game follow-up and both slower Creature Burn seeds recovered at d8 b0. The final pre-rebase build
passed 3,917 unit tests and 610 scenarios; its 30-reference replay had zero shortfalls. The full deck gate
passed every local check but marked `card_costs` incomplete because three Scryfall reads timed out; all three
costs matched on direct retry, and `scripts/audit_card_costs.py` now retries transient timeouts.

The shared branch advanced substantially, so the 14 local Creature Burn commits were rebased onto
`c841cd59c` as HEAD `d083b566b`. The rebased source combined Worldfire's simultaneous-leave handler with
the unearth replacement and kept the independently removed tutor-width cap removed. A four-job Release build
is running under `build/.heavy.lock` (`rebased-build2.log`, marker `REBASED_BUILD2_DONE`); the engine has
linked, and unit test objects are compiling. The first rebase build exposed one missing closing brace in the
combined leave handlers; it was fixed and the second build has compiled that file. The queue turn was yielded
at 17:04 and rejoined behind Blood Sun; `queue-wait-rebased.log` / `QUEUE_REBASED_READY` track it.

Next: finish the build, run unit tests, scenarios and `verify_deck.py --write-ledger`; commit the current
SpellEffects brace, audit retry, and report updates (excluding `.vscode/settings.json` and `test/ref_bench.json`).
At the next queue head, fetch/rebase if needed, then smoke/scoped regression with changed-game audit, strict viewer
check, GT accept and `land`. After the Stage 6 report, generate the fast Creature Burn mulligan profile on the
frozen landed commit.

**17:49 UTC update:** The rebased engine linked and its 30 saved references replayed with 0/30 shortfalls
(`rebased-ref.log`). The full unit target is still compiling under the four-job build. The queue became ours
at 17:46 before validation was ready; it was promptly requeued at 17:48 behind Blood Sun. The new waiter is
`queue-wait-rebased2.log` / `QUEUE_REBASED2_READY`.

**18:44 UTC update:** The full rebased Release build completed (`REBASED_BUILD2_DONE=0`), then 4,044/4,044 unit
tests and 3,653,879 assertions passed (`REBASED_UNIT_DONE=0`), followed by 610/610 scenarios
(`REBASED_SCENARIOS_DONE=0`). The 30 saved references remain 0/30 short. The brace and transient Scryfall
timeout retry are committed as `4517c8a37`. `verify_deck.py --write-ledger` is running under the heavy lock
(`rebased-verify.log`, `REBASED_VERIFY_DONE`). The push queue has Blood Sun, then an urgent Beasts CI fix, then
this session.

**18:59 UTC update:** The rebased deck gate passed (`REBASED_VERIFY_DONE=0`). All 19 cards have full coverage;
Scryfall costs, viewer surface/wiring, mismatch, play invariants, suite, and provider policy are green. Its
Stage 6a block is regenerated in `analysis-Creature Burn.md`. The report now records the 4,044-unit/610-scenario
and 0/30-reference results. Commit the report and this handoff, then take the queue turn for smoke/scoped
regression, changed-game audit, strict viewer check, GT accept and land. The fast mulligan generation follows
on the frozen landed commit. No user question is pending.

## Codex continuation (2026-11-22 ~13:30 UTC)

The integrated source is committed through `e001eca40`. Release build, 3917 unit tests, 610 scenarios,
coverage, suite gate, and the 30-reference replay passed (0 shortfalls). The first landed batch's Windows CI
run 45012242724 succeeded. The pooled 28-job A/B completed with rc 0; results and `DONE` are in
`build/scratch/cb-integrated/ab-combined/`. The temporary `MTG_OPP_ANTICIPATE` and
`MTG_SCRY_LEGACY_WIDTH2` arms are removed in the current working tree. A full Release rebuild plus unit and
scenarios is running under the heavy lock as PID 119977 (`cleanup-build.log`, `cleanup-unit.log`,
`cleanup-scenarios.log`, `CLEANUP_DONE`). The corrected depth-8 scry replay is staged at
`build/scratch/cb-integrated/ab-deep/manifest.json` and must run after that build. An earlier 4-game replay is
INVALID because its one-game jobs omitted the game-index offset in `seed`; preserved under `ab-deep-invalid/`.
The Stage 6 report is drafted in the ledger, with the deep checks, landing and generated 6a block still pending.
No user decision is pending. The earliest next push-queue join remains 14:23 UTC.

**15:20 UTC update:** The clean build, 3917 unit tests, and 610 scenarios passed. The corrected deep replay found
PFO's firm verdict genuinely lossy: Creature Burn seeds 251 and 436 stayed T6 at d8 b0. The follow-up 500-game
per-deck PO arm (pick + kept order, firm OFF; `build/scratch/cb-integrated/ab-po/`) is -11 net turns for Creature
Burn and -5 for Midrange Burn against the old baseline. Its two slower Creature Burn seeds (8, 115) both recovered
T5 at d8 b0 (`ab-po-deep/`). `MTG_BURN_SCRY_FIRM` is now default OFF in the working tree; a final full build,
unit tests, and scenarios are running under the lock as PID 146902 (`final-build.log`, `final-unit.log`, `final-scenarios.log`,
`FINAL_DONE`). Joined the push queue at 14:23 as number 3, now number 2; `queue-wait.log` and `QUEUE_READY` track it.

## Codex continuation (2026-11-22 11:41 UTC)

The three WIP branches are now cherry-picked as one linear series on the primary branch. The old worktree build
queue below was stopped to avoid duplicate heavy builds. A direct-removal review found additional paths that
bypassed the unearth replacement; fixes and two tests are in the primary working tree, pending the unit run.
The first two integrated build attempts stopped on test fixture compile errors (`Library::resize`, a bit-field
assertion, and private SBA access); those fixtures are fixed. The incremental build is running as PID 89254,
with `build/scratch/cb-integrated/build4.log` and `BUILD4_DONE`. The linked engine passed the two new scenarios and a 30-reference replay (0 shortfalls); the
pooled A/B manifests are combined at `build/scratch/cb-integrated/ab-combined/manifest.json` (19,400 games).
All other remaining steps below still apply. The original process IDs in the next section are historical.

This replaces the CHECKPOINT section of `docs/design/analysis-Creature Burn.md` (the history is kept there).
Start here. Read `CLAUDE.md` (it is authoritative for Codex too, via `AGENTS.md`). Before acting, check every
process and file named below, because some may have finished or moved on by the time you read this.

## Task
The USER asked: "Please analyze the creature burn deck." That means the full analyze-deck workflow
(`.claude/skills/analyze-deck.md`), Stages 1-6, ending with the Stage 6 report and its 6a disclosure. After that:
"After that we can try building the mulligan profile."
- **Done so far:** Stages 1-5 and the verify_deck gate.
- **Already landed:** `94cd1e06b` at 10:23 UTC 2026-11-22. CI run 45012242724 finished GREEN (Windows build and checks: success).
  Nothing to fix.
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

## Heavy-lock queue at handoff (updated ~11:00 UTC; all detached in their own sessions)
These run in order, one at a time. The three Claude agents were STOPPED at ~11:00 UTC, so nobody is waiting on the
results and nothing runs after the last item; the next step is yours. (Two earlier queued entries, cb-bbgap's
flock 72838 and cb-scry's flock 73036, died while waiting for the lock and were relaunched as below.)

1. **cb-unearth build** (flock PID 72807, running)
   - Log: `build/scratch/cb-unearth/build2.log`. It was at 140/367 at ~11:00.
   - Output: `.claude/worktrees/cb-unearth/build/Release/` (a symlink onto `build/wt/cb-unearth`).
2. **cb-scry build** (flock PID 79353, waiting)
   - Log: `build/scratch/cb-scry/build4.log`.
3. **cb-bbgap `build/scratch/cb-bbgap/chain1.sh`** (flock PID 79554, waiting)
   - Runs build, then unit, then scenarios.
   - Status: `build/scratch/cb-bbgap/status`. Marker: `CHAIN1_DONE`. Logs: `build4.log`, `unit.log`, `scen.log`.

Each full build takes about 1-1.5 h on this box. If you would rather integrate first and build once, you may kill
items 2 and 3. They are agent experiments, not USER-requested runs. Each worktree's A/B still needs its own binary,
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
