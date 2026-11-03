# Handoff: Dragons mana-clean fixes (branch wip/mc-dragons)

Task (from the coordinator): fix the mana defects the mana-clean gate found on Dragons
(`/workspaces/MagicDeckTester/logs/mana_clean/pass1-dragons/report.md`: 71,000 games, ENUM-GAP 1,691, APPLY-DROP
4,164, ORACLE-GAP 3,074), measure on a fresh 10k slice with both binaries, A/B the play change, unit test + scenario +
doc section. Do NOT accept GT and do NOT push to phase-1-2-deck-analyzer; `git push origin wip/mc-dragons` is allowed.
Full write-up: `docs/design/mana-completeness-oracle.md` section 14 (drafted; numbers below still to be added there).

Worktree: `/workspaces/MagicDeckTester/.claude/worktrees/agent-a9dab9fbcae918660`, branch `wip/mc-dragons`, based on
origin/phase-1-2-deck-analyzer 980616e55. Scratch: `/workspaces/MagicDeckTester/build/scratch/mcdragons/`. Helper
scripts (gitignored): `logs/mcdragons/{mk.py,gate.sh,batch.sh,repro.sh,abrep.py,scprobe.sh}`.

## Mechanisms (evidence)

1. **ORACLE-GAP, 2,880 of 3,074 -- the oracle was wrong.** Haven of the Spirit Dragon's rebuy ("{2}, {T}, Sacrifice")
   -- the oracle's frontier table offered the Haven's own {C} toward its {2}; the engine holds it (SelfTapSourceHoldBit),
   so the engine's "rebuy unpaid, {G} still floating" outcome was never reached. Fix: `manaoracle::AbilityTapSrcScope`
   set at both rebuy sites (`src/ai/TurnSolver.cpp:65748`, `src/ai/AIEngine.cpp:6536`), captured at the frontier
   (`src/ai/ManaPayment.cpp:8443`, `Hook::f_self_src` in ManaOracle.h), excluded in `BuildSources`
   (`src/ai/ManaOracle.cpp:921,970,2123`). Play reads none of it. Repro gi315 T3: ORACLE-GAP 5 -> 0.
2. **APPLY-DROP + ENUM-GAP -- per-cast payer strands the next cast's colour around Gruul Turf ({R}{G}).**
   {Mind Stone, Scourge}: Mind Stone's {2} took a Mountain; {Urza's Incubator, Dragon}: the Incubator's {3} took the
   Mountain and the Dragon (discounted to {R}{R}) met Gruul alone. ENUM-GAP is the same failure inside the enumerator's
   sequential walk (`SeqWalkFixedOrder`, gate `ef-walk-fail`). Fix: `MTG_LINE_REPAY` (DEFAULT ON, heurarm
   `LINE_REPAY`, `src/ai/EngineFlags.h:901`): every committed outermost payment pushes a record (digest + pre-board,
   `LineRepayRecord` `src/ai/ManaPayment.cpp:8048`, `LineRepayDigest` :7980); when a line's payment fails, the matching
   previous payment (depth <= 2) is re-made over the oracle's `MinimalPayments` until the failing one pays
   (`LineRepayTry` :8177; hook in `TapForCostShared` :8274 / :8304). Refuses across a draw (library size in digest),
   human play, tap-pref replay, oracle copies, armed boards, sacrifice/graveyard/drip/haste payments (barriers).
3. **Not fixed -- need-later APPLY-DROP (USER decision).** A continuation chose a cast AFTER a draw (top: T3
   {land=Gruul Turf}, Mind Stone's {1} draw paid with the floating {R}, post-draw Bolt has no {R}). Re-paying across
   the draw is clairvoyant; the lossless shape is a searched payment variant at a breakpoint opener (oracle doc
   section 2.3 / section 5 question 1). Surface to the USER; do not settle.

## Commits
- WIP commit on `wip/mc-dragons` (this handoff): all of the above + tests. Not yet pushed.

## Measurements

**Gate slice** (Dragons games 100000-109999, `MTG_MANA_ORACLE=2`; outputs `build/scratch/mcdragons/gate_{base,new}.{out,err}`):

| binary | ENUM-GAP | APPLY-DROP | ORACLE-GAP | total issues | census |
|---|---|---|---|---|---|
| base (980616e55, `basesrc/build/Release/mtg`) | 247 | 564 | 316 | 1,127 | depth1 440, depth2 124, off-menu 247, need-later 129, rollback-rescued 253/564 |
| new (lever on) | 1 | 142 | 28 | 171 | depth1 125, depth2 17, need-later 138, need-in-plan 4 |

Residue on the new binary: APPLY-DROP 138 need-later (mechanism 3) + 4 need-in-plan (seed 30102617 gi2617 T5
{Haven, Bolt, Lathliss}: census depth2 through a 4-decision chain, likely a barrier/zero-cost record in between --
not chased); ENUM-GAP 1 (seed 30101370 T4, 4-action plan with Greaves equip -- likely depth 3); ORACLE-GAP 28: 20 on
{Mind Stone + another cast} (pre-existing shape, nearest outcome "same core" with the other Mind Stone tapped -- the
engine's board appears among the oracle's options, cause not yet found) and 8 on {creature + Haven rebuy} with two
Havens (one Haven taps for the creature, the other is the rebuy source). A diagnostic now prints every outcome a gap
was judged against at level 3 (`gap-outcome` lines, `src/ai/ManaOracle.cpp:2892`); repro e.g.
`MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg decks/Dragons/Dragons.cod --profile decks/Dragons/Dragons.profile.json --games 1 --seed 30004558 --game-index 4558`.

**Identity**: base vs new with `MTG_LINE_REPAY` off, 6 decks x 100 games (`ab/ident_{base,new}.out`): byte-identical
digests and units.

**A/B (RUNNING)** `ab/ab1.json`, one pooled batch, per-job `MTG_LINE_REPAY` off/on: Dragons 6,000/arm (seed 1810001),
Dragons 2HG 6,000/arm (1820001, life 30, 2 heads), every other suite deck 60-300/arm (1830001). Interim (139 of 356
jobs): Dragons 6,000 paired -- 57 digests changed, 0 slower, 1 faster, units 1.0011; Bruna 150 paired: 0 slower / 1
faster; Angels, Fluctuator, Goblins, Soldiers, FiveColour byte-identical so far. Report:
`python3 logs/mcdragons/abrep.py` (prints per-deck paired/digest/slower/faster/net/units and the slower games).

**Unit tests**: `test/unit/test_line_repay.cpp` (3 cases, each shape fails with the lever off) + the Haven case at the
end of `test/unit/test_mana_oracle.cpp`: 4/4 pass. Full suite RUNNING. **Scenario**:
`test/scenarios/dragons_incubator_lathliss_gruul_repay.json` passes with the lever on, fails with it off
(`expect_log_sequence` T4 Incubator + Inferno). Full scenario suite RUNNING.

**A/B FINAL** (ab1 finished; full table `build/scratch/mcdragons/ab/ab1_report.txt`): Dragons 6,000 paired: 57 digests,
0 slower / 1 faster (net -1), units 1.0011. Dragons 2HG 6,000: 65 digests, 0 slower / 4 faster (net -4), units 1.0012.
Other decks that move (all screened at 60-300 games, so each needs >= 5,000/arm before adoption): NivMizzetCombo 150
(18 digests, 0/2, net -2, units 0.9975), SelesnyaLifegain 300 (14, 0/1), Bruna 150 (6, 0/1, units 1.0096), EDF 150
(10 digests, 1 slower / 1 faster, net 0, units 1.0165), AccomplishedAlchemist 150 (1 digest, 1 slower, net +1),
Pirates 300 (1, 0/1), Galadriel (2 digests, no turn change), FiveColour (2), Stompy (1). Every other suite deck
byte-identical. SLOWER GAMES to check at d8 b0 (both arms): alchemist seed 1830001 + gi 245 (7 -> 8, i.e. --seed
1830246 --game-index 245), edf seed 1830002 gi 1 (4 -> 5). EDF's slower game may share a cause with the
test_edf_goff_verify failure below.

## Jobs still running (none now -- all finished)
- A/B batch: PID 1832202 (`build/Release/mtg --batch .../ab/ab1.json --threads 8`), wrapper 1832200. Output
  `build/scratch/mcdragons/ab/ab1.out`, `ab1.err`, per-game `ab/ab1_w/*.wins`. NOTE: build/Release/mtg was relinked
  after it started (diagnostic print only, level >= 3); the running process keeps the old image.
- Full unit suite: FINISHED (`build/scratch/mcdragons/r/ut_all.txt`): 2,435 cases, 2,430 pass, **5 FAIL** -- must be
  resolved before landing. Read so far (not yet verified against a base mtg-test, which was never built):
  * `test_dragonstorm_followups.cpp:949,1001-1002` (MTG_LINE_SPARE_GENERIC_FIRST) and `test_niv_payment_holes.cpp:307-309`
    (Niv hole 2, MTG_RAMP_FEED_ROUTE): the failures are in the LEVER-OFF arms -- the line re-pay now rescues the drop
    those arms pin. Likely fix: pin `heurarm::LINE_REPAY` off in the off arm of each (the tests isolate their own lever).
  * `test_mana_oracle.cpp:242-245` (viewer line check, EDF seed 3000 T4): the engine's own apply now pays the line
    ("Training Grounds, then Eldrazi Displacer"), so `engine_full` is true where the test expects the drop. Likely the
    same: pin LINE_REPAY off for that case (it pins the historical payer, as its neighbours do), and consider adding a
    case that the search now pays it.
  * `test_edf_goff_verify.cpp:119-124` (EDF d0 unprojected go-off, ON arm): the go-off no longer kills on application
    (opponent at 19). NOT understood -- may be a real regression from the re-pay in EDF's blink loop (an untap effect
    between payments should already block it via the digest; check the ring/barrier logic and the d0 runner). Root-cause
    it before landing; run the test with LINE_REPAY off to confirm attribution.
- Scenario suite: FINISHED -- 345 passed, 0 failed, 47 ERROR. All 47 are `./build/Release/mtg: Permission denied`
  (the bruna_human_wings_swap... through cyclone_cycle_seed_red_float_legacy_misses block), from the mtg relink that
  ran in the middle of the suite -- not real results. Re-run those 47 (or the whole suite) on the current binary.
  Old line, kept for reference: PID 1866354 (`bash test/scenarios.sh`), output `build/scratch/mcdragons/r/sc_new.txt`
  (base comparison for any FAIL: `MTG_BIN=/workspaces/MagicDeckTester/build/scratch/mcdragons/basesrc/build/Release/mtg bash test/scenarios.sh`).

## Next steps
1. When ab1 finishes: `python3 logs/mcdragons/abrep.py`. For every slower game (deck, seed_base, game_index):
   per-game seed = seed_base + game_index; run both arms at `--depth 8 --budget-ms 0 --ignore-play-profile`
   (`MTG_LINE_REPAY=0` / `=1`; 2HG adds `MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2`), e.g.
   `MTG_LINE_REPAY=1 build/Release/mtg decks/Dragons/Dragons.cod --profile decks/Dragons/Dragons.profile.json --games 1 --seed <s> --game-index <gi> --depth 8 --budget-ms 0 --ignore-play-profile`.
   If not recovered, check clairvoyance with `MTG_NC_SEARCH=1`. Per-deck net must be <= 0. Any other deck whose
   digests move needs >= 5,000 held-out games per arm.
2. Check `ut_all.txt` (all must pass) and `sc_new.txt` (compare any FAIL against the base binary).
3. Fill section 14 of `docs/design/mana-completeness-oracle.md` with the gate table, A/B table, unit/scenario
   counts; delete `docs/design/mc-dragons-checkpoint.md` (superseded by this file) or keep one of the two.
4. Optional: chase the 28 ORACLE-GAPs with the `gap-outcome` diagnostic, and the 4 need-in-plan / 1 ENUM-GAP residue.
5. Amend/commit (end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`), `git push origin wip/mc-dragons`,
   watch CI (Windows) in the background. Report to the coordinator; surface mechanism 3 as a USER decision.
