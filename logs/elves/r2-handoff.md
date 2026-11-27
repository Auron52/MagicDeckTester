# Elves round 2 -- HANDOFF (context rule, 2026-11-27)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/elves-r2`, branch `wip/elves-r2` (from origin
f2e307fe3). Read `CLAUDE.md`, then `logs/elves/r2-notes.md` (what was built, USER rulings), then this file.
Subagents do NOT push the shared branch phase-1-2-deck-analyzer; push `wip/elves-r2` only.
Context rule: hand off at ~250K tokens, never exceed 400K.

## Done (commits on wip/elves-r2, NOT YET BUILT OR TESTED)

- `d70a0190e`: all code and tests for tasks 1-4 plus the tap order and the chosen census (details in r2-notes.md):
  - **Task 1, kick cap:** `MultikickCountCap()` in TurnSolver.cpp, the opponent's starting life via
    `gamesetup::StartingLife()`. `kMaxMultikick` 20 -> 40 in CardDatabase.h.
  - **Task 2, Wave cost:** `ElvesDiscardCost` prices Genesis Wave at X = 3 (cost 6).
  - **Task 3, land floor:** always on (`land_floor`), and the comment block above CleanupDiscardCandidates is reworded.
  - **TEMPORARY `MTG_ELVES_R2`:** default ON; =0 is origin behaviour for tasks 1-3.
  - **Task 4, hold-back modes:** `MTG_ELVES_HOLDBACK_MODE=band|three|two`, with heurarm twins
    `MTG_ELVES_HOLDBACK_THREE` / `MTG_ELVES_HOLDBACK_TWO`. The census line gains `inbetween=` and `mode=`.
  - **Tap order:** `MTG_ELVES_TAP_ORDER` (TEMPORARY, default OFF) drives `ElvesProvider::ManaSourceRank`.
    New field `PlanTraits::casts_scaled_sink`, set in ComputePlanTraits and mixed into PlanTraitsContentHash at
    bit 10 (a false value contributes 0, so the hash is unchanged).
  - **Chosen census:** `MTG_ELVES_CHOSEN_CENSUS=1` calls `ElvesChosenCensus`, hooked in AIEngine.cpp just before
    the StampM1Hand block at the plan-choice point.
  - **Tests:**
    - test/unit/test_elves.cpp: the kick-cap test (20 / 30 / R2=0) and two tap-order tests (the USER's board:
      Priest + 2 Llanowars + 2 Clancallers, Brigade squad x1 = 5 mana).
    - test/unit/test_elves_provider.cpp: the `three` / `two` test (7 Forests + Llanowar + Mystic + Priest; Wave
      band {4..9}, three {4,7,9}, two {7,9}; Warcaller band {0,2,3,4,5}, three {0,2,4,5}, two {0,4,5}), the Wave
      discard test, and the land floor at 4 lands updated with R2=0 checks.
- `748f4efb9`: census lambda renamed `stand_in` (MSVC defines a `near` macro); the tap-order test finds the cast in
  any action slot.

Not compiled. A first `-j2` box_lane build was started and then STOPPED by me (my own run), so nothing is running.

## Remaining, in order

1. **BUILD** (heavy.lock is FREE: the mulligan generation was OOM-killed at 10:06 and is paused until a memory fix
   lands):
   `cd /workspaces/MagicDeckTester/.claude/worktrees/elves-r2 && flock /workspaces/MagicDeckTester/build/heavy.lock env MTG_BUILD_JOBS=14 ./build.sh > logs/elves/r2-build2.log 2>&1; echo EXIT=$? >> logs/elves/r2-build2.log`
   (run_in_background). Check the exit code and the binary mtime; never trust `| tail`. Fix compile errors. Likely
   spots: the `realises` / `nearest_below` lambdas in ElvesScalingAdmit; `ElvesChosenCensus` (it uses
   `TurnSolver::EnumerateMainPlansExhaustive`, `LineCastCostTotal`, `heurarm::t_job_name`).
2. **UNITS:** `build/Release/mtg-test` (full), then `-tc="*Elves*"`.
   - Possible expectation slips: (a) the tap-order test `on` arm. If the prepay ladder
     (BatchPrepayMainCasts, not driven by the rank) or the per-cast reserve decides, the Priest may not be the one
     tapped. Find out which path pays; do not weaken the test. (b) the Warcaller sets in the three / two test. The
     unkicked K=0 cast is in the menu, and verify the enumerated top K really is 5.
   - Also run once each under `MTG_ELVES_HOLDBACK_MODE=three` and `=two` (env path), and under `MTG_UNPRUNED=1`.
3. **SINGLE-GAME CHECKS** (box_lane, `--threads 1`):
   - an Elves std game and a 2HG game with `MTG_ELVES_SCALING_CENSUS=1` (a census line with `inbetween=`);
   - `MTG_ELVES_SCALING_TRACE=<turn>` (the `between` tags);
   - `MTG_ELVES_CHOSEN_CENSUS=1` (`[elves-chosen]` lines);
   - a 2HG game where Warcaller K > 20 is offered.
4. **A/B + census, ONE heavy-lock hold** (6 arms x 3000 std + 3000 2HG = 36,000 jobs, ~16 core-h, ~30 min on the
   full box):
   ```
   S=/workspaces/MagicDeckTester/build/scratch/elves-r2/ab; mkdir -p $S
   cp build/Release/mtg $S/mtg            # the snapshot; run from the worktree (cards.json / providers from CWD)
   python3 logs/elves/r2_ab_manifest.py $S/ab.json 3000 3000
   python3 logs/elves/r2_census_manifest.py $S/cen_std.json 1000 std band
   python3 logs/elves/r2_census_manifest.py $S/cen_2hg.json 1000 2hg band
   python3 logs/elves/r2_census_manifest.py $S/cen_tap.json 1000 both twotap,bandtap
   bash logs/elves/r2_run.sh              # run_in_background; it flocks heavy.lock itself
   ```
   - **Markers** in $S: `ab.QUEUED`, `ab.STARTED`, `ab.DONE` (rc + time), `census.DONE`. Wait on the DONE markers or
     the PID, never `pgrep -f`.
   - **Heartbeat:** check the `[batch] heartbeat: N/M workers busy` line in `ab.err` within 10 minutes.
   - **If any window touches 12:30-20:00 UTC:** add a 2-hourly CronCreate timer (durable:false, owner named).
   - **Arms:** origin (R2=0), band, three, two, twotap (two + TAP_ORDER), bandtap (band + TAP_ORDER).
     Seeds 9610001+gi, game_index gi, d5 b20 with the profile; 2HG = life 30 + 2 heads.
5. **STATS:** `python3 logs/elves/r2_abstat.py $S/ab.out` gives per-format avg, net vs origin / band with paired se,
   faster/slower lists, digest differences, and units/game vs band.
   - **The price of the in-between branch:** units of three vs band, plus `inbetween` vs `plans` in the
     `[elves-scaling-census]` lines of cen_std.err / cen_2hg.err.
   - **Chosen census:** parse `[elves-chosen] job seed T pre sink count top hn h cat fixed_atk scaled_atk sick
     land_mana cost`. Report (a) how often the band's executed count is neither `top` nor `normal`; (b) the turn
     distribution of the categories for band vs twotap; (c) the tap-order frequency: pre=1, fixed_atk>=1,
     scaled_atk>=1, cost>land_mana.
   - **Caveat:** the census re-enumerates at the real state, so compare cen_* per-game results with the A/B band
     arm before trusting them.
6. **d8 b0 BAR:** `python3 logs/elves/r2_mk_d8.py $S/ab.out $S/d8.json` builds jobs for every game slower in band vs
   origin, and in three/two/twotap/bandtap vs band and vs origin, with the reference arm included. Run via
   `bash scripts/box_lane.sh --slots 2 -- build/scratch/elves-r2/ab/mtg --batch $S/d8.json --threads 2` (or under
   heavy.lock with full threads if it is free). A game recovers when the slower arm at d8 b0 wins no later than the
   reference arm at d5 b20.
7. **WRITE UP:**
   - Put the results in `logs/elves/r2-notes.md` and commit it (`git add -f`: logs/ is gitignored, the repo
     force-adds them).
   - If the in-between price is material (several % of Elves units, or a visible share of slow games), add a short
     design there: a board-feature lethal heuristic that admits only the hold-back counts consistent with the
     earliest lethal. Make it budget-staged (waves), not a hard gate, per "a budget may re-price, not remove". Do
     NOT build it. If the price is small, say plainly: no prune.
   - Then push wip/elves-r2 and report: hashes, units, the A/B table, d8 b0 verdicts, and the open USER decisions
     (band vs three / two; the tap order's default). Delete the temporary selectors only after the USER rules.

## Known approximations to disclose

- **top - ceil(h_normal / unit) can be one count too low.** When the top count leaves r spare mana, holding the
  fixed dorks back may still pay one more count. The admit function does not see the total mana.
- **The whole-turn prepay ignores the tap-order rank.** The hold/release ladder in TurnSolver::BatchPrepayMainCasts
  is not driven by the rank (at equal power it already releases the bigger producer, the Priest, first).
  ExecutorPayerOrder (the lethal projection) does not set casts_scaled_sink, and the Ezuri pump is not a sink.
- **The current tap order (origin) is the problem case.** GenericProvider::ManaSourceRank gives Priest / Archdruid
  66 (tap last) when the plan "casts scaler food", and Warcaller / Brigade are Elves, so a sink plan taps the
  Llanowars first. It gives 64 on an attack turn without food, else 65; the rank-61 comment at ~10091 is overridden.

## USER rulings (verbatim, 2026-11-27)

- **Q1:** "Hold all dorks back is okay, but not sufficient in my mind unless we have heuristically determined the
  right option in our situation. i.e. If just searching we should probably consider all, hold back normal dorks and
  hold back all dorks. If we add some heuristic logic to figure out the win turn we could potentially prune that
  further."
- "We could search the options in-between, but that seems quite costly." then "Okay, fair enough. If we don't need a
  prune then we don't need a prune. Though we should check how much this particular branch costs and make a
  decision on whether we perhaps do want a prune. My thought was a bit rough, so if we actually need to prune this
  then perhaps we should take the "heuristic calculation" approach to set things up for lethal as soon as possible
  instead."
- "This is also a tricky problem, because technically on earlier turns when we are setting up, you usually want to
  use the scaling elf mana last, to get more on board before you tap it. Then in late turns you want to work the
  other way around to leave multiple attackers."
- "That said, when using a scaling spell, (Joraga, Galadhrim, Genesis Wave) you usually would want to prioritize
  attackers or just pump all of the mana into the spell."
- "I see. So yeah, we should take control of the tap order a bit in the heuristic provider to make sure that we
  maintain whatever thing we want to maintain, whether that is the attackers or the scaling mana."
- "Usually on an earlier turn (like turns 2-3) you would focus on building your mana and keep the scaling back.
  After that you usually want to attack. But ideally how we handle this would depend more on our board than the
  turn."
- **Q4:** "I don't understand the Taunting Elf comment. Genesis Wave is technically MV 3, but realistically needs to
  be cast for 6 mana or preferably even more to be good. It shouldn't be counted as a cheap spell in the discard
  heuristic. Mana Elves should indeed be kept above spare lands. We ideally do want to play a land next turn,
  though, so keeping one is nice if we can manage it."
- **Q6:** earlier "I think Kick counts should be capped at lethal size of 20"; now "Yeah, we may want to scale it up
  for 2HG arguably."

## Coordinator instructions relayed (binding for this work)

- **Price, not default.** The `three` arm is the instrument for the PRICE of the in-between counts. Report it as
  "cost of the in-between branch", not as a candidate default. No lethal prune is built now; write the design only
  if the price is material.
- **Tap order.** Each plan states what it preserves:
  - PRESERVE SCALING (set-up / non-sink): scaled dorks last.
  - PRESERVE ATTACKERS (sink turn, pre-combat): lands, sick or non-attacking dorks, scaled dorks, then the
    attack-capable fixed dorks last.
  - ALL-IN: the order is immaterial.

  The attackers-first count must equal what the payer can raise with those attackers held. The selector stays
  default OFF; it is the USER's decision.
- **Board, not turn.** Never key the choice on the turn number. A heuristic may only ORDER options (or, if measured
  necessary, prune one) from BOARD features: the damage the dorks would add vs the opponent's remaining life, the
  growth of the scaled dorks, a sink in hand. Show the turn distribution of the chosen mode in the A/B games as a
  sanity check.
- **The USER's sink-turn candidate** is {top, attackers-first} with the matching tap orders (the `two` / `twotap`
  arms). Report how often the band's chosen count is neither of the two. The default stays the band.
