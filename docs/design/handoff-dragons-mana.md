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

## Successor 2 status (2026-11-03, final)
- Commits: 65d6c2525 (blink-loop scope + untap count + 4 test rewrites), then the doc/handoff commit on top.
  Not pushed, GT not accepted.
- Unit 2435/2435, scenarios 392/392 (`build/scratch/mcdragons/r/{ut_fix2,sc_fix2}.txt`, binary `mtg.fix2`).
- A/B final = ab1 OFF vs ab2 ON (`bash logs/mcdragons/mkabx.sh`): table in mana-completeness-oracle.md section 14.
  Slower: Alchemist s1830246 gi245 (7->8), EDF s1830002 gi1 (4->5); both T4/T4 at d8 b0. Units on unchanged-play
  jobs x1.0003. Alchemist net +1 on 150 games -> the >= 5,000/arm held-out run decides it.
- NEXT 1: held-out >= 5,000 games/arm for Niv, SelesnyaV2, Bruna, Pirates, EDF, AccomplishedAlchemist (one pooled
  batch, THREADS <= 8 while the box is loaded, new seed base; Bruna ~2.4 s/game-thread).
- NEXT 2 (coordinator, 2026-11-03): the 138 need-later APPLY-DROPs. Claim: in every APPLY-DROP line of
  `build/scratch/mcdragons/gate_new.err` the dropped card is in hand= on the pre-plan board (e.g. seed 30100059 gi59
  T4: Bolt x2 in hand, Mind Stone's {1} draw paid with the floating {R}). (1) Verify the split across all 138:
  failing card in hand before the draw vs drawn in between. (2) Narrow LineRepayTry's draw barrier (the library size
  in LineRepayDigest): allow the re-pay across a draw when the failing cast's card was in hand before the draw; keep
  refusing when it was drawn. (3) Re-run the gate slice + the A/B with units. Optional: payer preference "spend the
  floating colour the hand needs least on a generic pip" (a payment-order change, same A/B). Only the drawn-card
  cases remain a USER question.

## Successor 3 (2026-11-03, stopped at the context cap) -- READ THIS FIRST
State: all code below is in WIP commit a56a429ef. BUILT (build1.log rc=0; `build/Release/mtg` and `mtg-test` carry the memo/DRAW strings), NOT TESTED -- start at Task 1 NEXT (1). A build of it is queued:
`buildq.sh` PID 2193594 (cwd = this worktree), log `/workspaces/MagicDeckTester/build/scratch/mcdragons3/build1.log`
(prints `rc=` when done; the queue was ~9 deep at 12:25 UTC). If that PID is gone without `rc=`, rebuild:
`cd <worktree> && bash /workspaces/MagicDeckTester/build/scratch/buildq.sh`. No other jobs of mine are running.
Scratch: `/workspaces/MagicDeckTester/build/scratch/mcdragons3/` (`mtg.cur` = binary of HEAD 3d3b067c1, i.e. the
pre-fix line re-pay; same bytes as `mcdragons/mtg.fix2`).

### Task 1 (held-out A/B) -- BLOCKED on a wall-time defect, now (hopefully) fixed in code
- FOUND: the line re-pay costs EDF ~20-40x WALL with units unchanged. ab1 (same batch, same box): EDF ms ON/OFF
  = 26x, units 1.0165; every other deck ~1x. Per game: EDF seed 1830038 gi 37 3,611 s (7,526 units), seed 1830138
  gi 137 2,737 s ON vs ~100 s OFF. Section 14's "units x1.0003" never saw it (the work is outside the units).
- perf (`mcdragons3/edfslow/perf3.data`): 99% in `manaoracle::MinimalPayments` under `LineRepaySearch`, reached from
  `EdfRangeAurasBehindUntappers` (EDF provider) -> `OrderPaysSequentially` -> `SeqWalkFixedOrder` inside
  `BpDeriveContinuationList`/`BpEnumEntryFor`. Inside MinimalPayments: `PayEnum::Verify`'s leftover-split recursion
  and `ExtendSurplusNow` (Aura'd lands = multi-unit taps; up to 4,096 verified probes per set-aside, per try).
- FIX (in the WIP commit, ManaPayment.cpp, lossless by construction, to be verified):
  (a) a decision-scoped memo of FAILED re-pays (`LineRepayMemoKey`: MidTurnStateKeyOf + re-pay digest + library
      size + both chain records' content hash (`LineRepayRec::content`, computed at record) + the bill + every
      payer thread-local I found + the heurarm arms; scope `TurnSolver::DecisionEpoch()`, cap 65,536, successes
      never memoised). `MTG_LINE_REPAY_MEMO_VERIFY=1` re-runs every hit; a disagreement counts `memo_mismatch`.
  (b) depth 2 checks the joint bill per option before recursing (`LineRepayChain::JointPays`, the same necessary
      condition the top level already used); the chain's record 2 is now found up front (needed by the key).
  (c) `MTG_LINE_REPAY_STATS=1` now prints searched d1/d2, MinimalPayments calls/options, inner-joint prunes,
      memo hits/mismatches, total ms, and a `[line-repay] slow try` line for any try > 100 ms.
- NEXT: (1) when built, copy the binary to `mcdragons3/mtg.new`. (2) Identity: run `mcdragons3/id/id.json` (ab2's ON
  jobs for the 6 movers + 2 Dragons jobs, renamed id_*) with `MTG_LINE_REPAY_DRAW=0 MTG_LINE_REPAY_MEMO_VERIFY=1
  MTG_LINE_REPAY_STATS=1 MTG_DUMP_WINS=1`, threads 8, `--game-log-dir mcdragons3/id/w`; per-game digests must equal
  `mcdragons/ab/ab2_w/on_*.wins` (same seeds; ab2 ran mtg.fix2) and memo_mismatch must be 0. (3) Wall: EDF seed
  1830138 gi 137 single game with the new binary must be ~100 s, not ~45 min (command in the edfslow dir:
  `cd <worktree> && MTG_LINE_REPAY_STATS=1 <bin> decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod --profile
  decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json --games 1 --seed 1830138 --game-index 137`). If
  the memo is not enough, the stats line says where the time is (d1 vs d2, options per call). (4) Then the held-out
  A/B: `python3 mcdragons3/ho/mk.py mcdragons3/ho/ho.json` (already generated: 1,100 jobs) -- SEED BLOCK 1840001
  (fresh: prior A/Bs used 1810001 / 1820001 / 1830001, the gate 30000000+); 5,000 games/arm for Niv, EDF,
  Alchemist, Bruna, Pirates, SelesnyaV2, Dragons, Dragons 2HG (Dragons added because the draw narrowing below moves
  it again), arms paired by (seed, game index), per-job MTG_LINE_REPAY off/on, ONE pooled batch, threads 8,
  MTG_DUMP_WINS=1 MTG_PGO=0 MTG_RSS_CAP_GB=43, `--game-log-dir mcdragons3/ho/w`; report with
  `python3 <worktree>/logs/mcdragons/abrep.py mcdragons3/ho ho` (expects ho.out and ho_w/; name the dirs to match).
  Compare WALL per deck too (ms on/off), not only units. Estimated ~36 core-h (~5-9 h at 8 threads on this box).
  2-hourly status timer per CLAUDE.md if it runs into 12:30-20:00 UTC.

### Task 2 (NEXT 2, the 138 need-later drops)
- Split (`mcdragons3/nl/split.py` over `mcdragons/gate_new.err`, by the dropped card's name in the pre-plan hand,
  net of the plan's own casts): 138 need-later = 115 in hand before the draw, 23 drawn mid-line (Lathliss,
  Atsushi, ...). 107 of the 115 have Mind Stone on the board: the draw is Mind Stone's crack ({1},{T},sacrifice).
- Code (WIP commit, TEMPORARY lever `MTG_LINE_REPAY_DRAW`, default ON, =0 restores the old barrier exactly; delete
  the lever before landing): library size moved out of `LineRepayDigest`; records keep `lib` and the hand's name
  hashes; `LineRepayFind` accepts a record across a draw only when `LineRepayAcrossDraw` holds (the failing spell's
  name in the record's hand, net of the copies that record and the chain's newer records paid for; activations
  refused).
- Expectation to CHECK, not assume: the Mind Stone crack sacrifices its source, so the battlefield shrinks and
  `LineRepayFind` skips that record (`s.battlefield.size() < r.n`) -- the draw barrier may not be the binding one
  for 107 of the 115. Measure: `bash mcdragons3/gate.sh <bin> new2 8` (gate slice, keeps CAPPED/UNMODELED lines
  too) and `... new2d0 8 MTG_LINE_REPAY_DRAW=0`; rerun split.py on each `.err`. Report count + mechanism; the 23
  drawn cases stay a USER question.

### Task 3 (remaining gate flags) -- two oracle-side fixes in the WIP commit (play-neutral), rest classified
- ORACLE-GAP x8 {creature + Haven rebuy}, two Havens (seed 30103105 T5): ORACLE defect. MinimalPayments dedupes
  results by MidTurnStateKeyOf, which on attachment-free boards forgets WHICH identical copy is tapped; the rebuy's
  own Haven (a plan source, keyed apart in BuildSources) then merged with its twin, the kept option tapped the
  rebuy's Haven and the engine's line was dropped. Fix: `ResultKey` (ManaOracle.cpp) folds each plan source's
  tapped state into the dedupe key (unchanged when t_plan_srcs is null, i.e. every play path).
- ORACLE-GAP x20 {Mind Stone + another cast} (seed 30100045 T4): ORACLE defect. The plan apply's dig (Mind Stone
  crack) taps the first untapped Mind Stone, then pays {1}, and untaps it on failure; the oracle's frontier did not
  know which copy's {T} it was, so the boards after tapping #29 or #30 read alike, the Pareto prune kept one, and the
  rollback then left the other copy up -- the engine's board was never reached. Fix: `AbilityTapSrcScope` around the
  dig's payment at the three sites (TurnSolver.cpp two, AIEngine.cpp one), so the frontier key carries the source.
- Both need the gate slice to confirm (expect ORACLE-GAP 28 -> ~0).
- ENUM-GAP x1 (seed 30101370 T4, {Mountain, Incubator, Atsushi, Greaves, equip}): ENGINE, a depth-3 chain
  (Incubator's {3} must float {R} off Gruul Turf and leave Sol Ring for Greaves); kLineRepayMaxDepth = 2. Raising it
  to 3 is the lossless fix but only after the memo has made the re-pay cheap; measure.
- need-in-plan x4 (seed 30102617 T5 {Haven, Bolt, Lathliss}, Haven's creature-only {R}): not yet chased.
- CAPPED (7 in the gate slice, 31 in pass1; now a gate FAIL): the oracle's per-plan apply cap
  (`MTG_MANA_ORACLE_NODES`, default 2000) -- a CHECKER limit. Next: rerun the capped plans with a larger cap and see
  whether they finish clean; then decide between a larger default and better pruning. UNMODELED: 0 on Dragons.

### Task 4 -- not started
- `src/ai/EngineFlags.h:896` comment and `src/ai/HeuristicArm.h:828` description say depth-1; the code is depth <= 2.
- `MTG_LINE_REPAY` stays (heurarm A/B arm, DEFAULT ON, like MTG_RAMP_FEED_ROUTE). Remove `MTG_LINE_REPAY_DRAW`
  (temporary). Keep or drop `MTG_LINE_REPAY_MEMO_VERIFY` (diagnostic verify gate, the enum-memo precedent) -- say
  which in its comment. Then units (`build/Release/mtg-test`), scenarios, `THREADS=8 bash test/regression.sh --smoke`
  (report moved rows, do not accept).

### USER questions to carry
- (RESOLVED, successor 4) The "23 need-later drops whose card was drawn mid-line" do not exist: `nl/split.py` split
  the hand on ", " and so broke every name containing a comma (Lathliss, Dragon Queen; Atsushi, the Blazing Sky).
  With the hand matched as a string (fixed in `mcdragons3/nl/split.py`), ALL 138 need-later drops on
  `gate_new.err` have the failing card in hand before the draw (115 non-comma names + 17 Atsushi + 6 Lathliss),
  so every one is a mistake-repair case under the USER ruling of 2026-11-03 (the rollback compensates for
  mistakes, never uses a drawn card; drawn cards stay behind the barrier -- LineRepayAcrossDraw already enforces
  it). There is therefore no hedge-rule candidate from this set.
- "Pay generic with colourless first" (USER ruling 2026-11-03: good EXCEPT EDF) ALREADY IS the shared default:
  GenericProvider's ManaSourceRankBase ranks a {C}-only source 5, ahead of every coloured land (StompySurprise s9
  T2, 2026-08-24; Sol Ring and Mind Stone fall on that rung). EDF's {C} holds (MTG_C_SOURCE_HOLD -> 59, and
  EldraziFlickerProvider's MTG_EDF_C_CONSERVE +40) are HUMAN-PLAY ONLY, so autonomous EDF also pays generic with
  {C} first today. An explicit EDF opt-out in autonomous play is a play change for EDF (EDF_C_CONSERVE's comment
  records that demoting {C} in the autonomous loop measured worse: edf_blink_loop_cashes_deckout t4 -> t5). Not
  built: USER question whether to A/B it.

## Successor 4 (2026-11-03) -- state
Scratch `/workspaces/MagicDeckTester/build/scratch/mcdragons4/`. Binary of a56a429ef: `mcdragons4/mtg.new`.
- Units (a56a429ef): 2,435/2,435 pass.
- Identity (`mcdragons4/id`, MEMO_VERIFY=1, DRAW=0, 1,600 games vs ab2's ON arm): 19/20 jobs byte-identical,
  memo_mismatch 0 over 213,929 failure-memo hits. ONE game moved: Bruna seed 1830069 gi68 (T6 both, digest differs).
  The new binary's ON game equals the OFF game; mtg.cur (pre-WIP) fires 220 re-pays (170 depth-2) on it, the WIP 60
  (10). Prime suspect: the WIP's per-option joint prune at depth 2 (lossy). Now behind a TEMP lever
  `MTG_LR_INNER_JOINT` (default OFF) for attribution: `mcdragons4/b68/runb.sh <bin> <tag> [ENV]` (1-game batch, prints
  the digest; ab2 ON = 86bc663936b9ccc5, OFF = 273ccb2f789b92f9).
- WALL (a56a429ef, `mcdragons4/wall/run.sh <bin> <tag>`, ON vs MTG_LINE_REPAY=0, concurrent, loaded box):
  EDF g137 (s1830138) 2,153 s vs 5.0 s; smoke63 (s1064 d5 b20) 4,368 s vs 4.1 s; Dragonstorm ab3 gi45 (s1930046)
  7.7 s vs 5.0 s. The failure memo does NOT fix it:
  * g137: 391 tries, EVERY one a depth-2 SUCCESS of ~6 s ({2}{W}{W} failing, chain {2}{W}/{2}{W}); successes were
    not memoised -> success memo added (re-make from the stored options; VERIFY compares options: ok_mismatch).
  * smoke63: 16,616 tries, 2,044 searches (d1 679, d2 1,365), only 19 fired; ~1.5 s per MinimalPayments call
    (3,265 calls, 110 options each; perf: PayEnum::Verify 14%, BuildSimKeyBase 19%, memmove 10%,
    LandTypeChangedToIsland 8%). Distinct keys, so no memo helps -> "freest board" precheck added: the failing bill
    paid by the REAL payer with the whole chain undone and not re-made; no pay -> no re-making pays (VERIFY counts
    max_mismatch). The raw joint check passes these because it does not see the real payer's holds.
  * With the inner joint prune OFF, smoke63 would also lose its 86,520 inner prunes (each a recursion with a
    MinimalPayments call) -- MEASURE before keeping it off.
- Build of the above queued: log `mcdragons4/build2.log` (target mtg + mtg-test).
- Next: id run again (VERIFY: memo/ok/max mismatches 0; gi68 back to 86bc...), wall run, then decide the inner prune
  (find why it is not necessary on gi68, or drop it), then the held-out A/B.
- The 23 "drawn" need-later drops do not exist (split.py comma bug; see USER questions above).
