# Elves round 3 -- HANDOFF (context rule, 2026-11-27 ~12:50 UTC)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/elves-r2`, branch `wip/elves-r2` (pushed). Read `CLAUDE.md`,
then `logs/elves/r3-notes.md` (round 3, all measurements), then `logs/elves/r2-handoff.md` (round-2 rulings). Push
`wip/elves-r2` only, never the shared branch phase-1-2-deck-analyzer. Context rule: hand off at ~250K, never exceed 400K.

## Done
- `c3c8981da` (pushed): the round-3 tree plus notes and scripts.
  - Task 1 FIX: `ElvesSinkReached` (DecisionProviders.cpp). PlanTraits gains `scaled_sink_name / scaled_sink_hand0 /
    scaled_sink_count` (filled only when MTG_ELVES_TAP_ORDER is on; hashed only when count > 0). The reader
    `ElvesTapOrderOn()` moved to EngineFlags.h. Two unit tests on built boards (test_elves.cpp, end of file).
  - Task 4 tidy: MTG_ELVES_R2 deleted; MTG_ELVES_HOLDBACK_MODE + heurarm ELVES_HOLDBACK_THREE/_TWO + the three/two
    branches + the in-between counting deleted (USER ruling); the temporary MTG_ELVES_TAP_WHOLEPLAN slot deleted.
- Tasks 2, 3 and the d8 b0 bar are DONE and MEASURED on snapshot `build/scratch/elves-r3/ab/mtg` (sha1 fc83c4c8). That is
  the fix tree BEFORE the hold-back / WHOLEPLAN deletion (it still had both selectors, default off/band).

## Measurements (bandtapfix = band + MTG_ELVES_TAP_ORDER with the fix)
| fmt | net vs band (se) | faster / slower | units x band |
|---|---|---|---|
| std | -0.0067 (0.0015) | 20 / 0 | 0.9689 |
| 2HG | -0.0130 (0.0021) | 39 / 0 | 0.9727 |
- Old bandtap reproduced (digest-identical to round 2): std -0.0063 (0.0015) 20 / 1, 2HG -0.0123 (0.0023) 42 / 5.
- **d8 b0 OWED: none.** bandtapfix has no slower game vs band; the bar is met.
- 2HG 75, 202, 428, 2162 and std 2176: bandtapfix == band action for action; same mechanism as 2176 (pre-sink tap order).

## Running jobs at handoff
1. **Final tidy rebuild: FINISHED after the handoff, EXIT=0, 0 errors** (build/Release/mtg 13:11:03, mtg-test 13:17:39 UTC; the tidy tree at c3c8981da; logs/docs commits since are not built and need no rebuild). Originally: waiting on heavy.lock behind elves-mem2's taskA. flock PID 2821122, log
   `logs/elves/r3-build3.log` (ends `EXIT=<rc>`). Check the exit code and the mtime of build/Release/mtg and mtg-test;
   never trust `| tail`. If it was killed or failed, rerun:
   `flock /workspaces/MagicDeckTester/build/heavy.lock env MTG_BUILD_JOBS=12 ./build.sh > logs/elves/r3-build3.log 2>&1; echo EXIT=$? >> logs/elves/r3-build3.log`
   (run_in_background).
2. **Informational d8 b0** (NOT the bar) -- FINISHED after the handoff: 0 of 4 recover (see r3-notes.md section 4).
   Root-causing one of std 449 / 2HG 786, 1226, 2371 is an OPTIONAL follow-up. Originally: 8 jobs, the 4 games where bandtapfix is slower than the OLD bandtap (std 449;
   2HG 786, 1226, 2371). box_lane PID 2825391, output `build/scratch/elves-r3/ab/d8x.out` / `d8x.err`. Verdict:
   `python3 logs/elves/r3_d8_verdict.py build/scratch/elves-r3/ab/ab.out build/scratch/elves-r3/ab/d8x.out bandtapfix bandtap`
   (paths under /workspaces/MagicDeckTester/). Its jobs carry MTG_ELVES_TAP_WHOLEPLAN, which only the SNAPSHOT binary
   reads (the tidy tree deleted it), so always rerun it with the snapshot.

## Remaining, in order
1. After the rebuild: run the full units (`build/Release/mtg-test`), then `-tc="*Elves*,*elves*,Joraga*"`, plus once
   under `MTG_ELVES_TAP_ORDER=1` and once under `MTG_UNPRUNED=1` (box_lane --slots 2). Fix anything red. Expected slips:
   none in play (the deletions are inert at the defaults), but test_elves_provider.cpp's rewritten band test (formerly
   the three/two test) is new code.
2. **Inertness check of the tidy binary:** replay ~200 jobs of `build/scratch/elves-r3/ab/ab.json` (band and bandtapfix
   arms only; drop every job whose flags include MTG_ELVES_TAP_WHOLEPLAN) on the new build. Digests must equal ab.out.
3. Amend `logs/elves/r3-notes.md` section 5 to "built, units N/N, digests identical"; record the informational d8
   verdicts. Commit and push wip/elves-r2 (messages end `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`).
4. Report to the coordinator: hashes, units, the A/B table, the d8 verdict (none owed), root causes, and the
   tap-order recommendation (below). Do NOT adopt it: MTG_ELVES_TAP_ORDER stays default OFF until the USER rules.

## Recommendation for the USER (not adopted)
- Turn MTG_ELVES_TAP_ORDER ON (with the round-3 fix) as the Elves default.
- It is better in both formats (std -0.0067, 2HG -0.0130, each > 4 se) and cheaper (about 3% fewer units). It has
  ZERO slower games in 6,000, so the d8 b0 bar is met trivially.
- It is the USER's own rule: scaling mana is kept while setting up, and attackers are preserved from the sink's
  payment on. It is keyed on the plan, never the turn number.

## USER rulings (verbatim)
- Round 3: "Okay, that's fine. We'll leave it." (band vs three/two -> band stays; delete the selector).
- Round 3, ACCEPTED for AFTER this round (same function ElvesScalingAdmit; NOT implemented): admit a second scaled sink
  in one plan when a scaled source grown by the first sink's bodies pays for it (Brigade 814: the first Brigade's Elves
  grow Marwyn, whose tap pays the second Brigade).
- Tap order (round 2): "we should take control of the tap order a bit in the heuristic provider to make sure that we
  maintain whatever thing we want to maintain, whether that is the attackers or the scaling mana"; "Usually on an
  earlier turn (like turns 2-3) you would focus on building your mana and keep the scaling back. After that you usually
  want to attack. But ideally how we handle this would depend more on our board than the turn."; "when using a scaling
  spell, (Joraga, Galadhrim, Genesis Wave) you usually would want to prioritize attackers or just pump all of the mana
  into the spell."; "This is also a tricky problem, because technically on earlier turns when we are setting up, you
  usually want to use the scaling elf mana last, to get more on board before you tap it. Then in late turns you want to
  work the other way around to leave multiple attackers."
- Every earlier ruling: `logs/elves/r2-handoff.md` "USER rulings".

## Disclosed gaps
- A breakpoint continuation rebuilds traits over the REMAINING actions. Once the sink is cast it is not in them, so
  payments after it in such a continuation fall back to the scaling order (round 2's per-plan rank had the same
  property).
- Carried from round 2: BatchPrepayMainCasts' hold/release ladder and ExecutorPayerOrder do not read the rank, and the
  Ezuri pump is not a sink. The prepay declines on plans that cast a mana producer, so Elves set-up turns go through the
  per-cast walk, where the rank does apply.
