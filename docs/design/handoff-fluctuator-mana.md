# Handoff: Fluctuator mana-clean APPLY-DROPs (branch `wip/mc-fluctuator`, worktree `.claude/worktrees/agent-af77bef86492f9b03`)

Handed off 2026-11-03 ~11:35 UTC (context cap). NOT pushed; GT NOT accepted (the parent session lands it).
Write-up: `docs/design/mana-completeness-oracle.md` section 13.14 (line ~1439), renumbered 13.14 on the 2026-11-04 rebase (origin holds Auras 13.11, Slivers 13.12, Kitty 13.13).

## STATE 2026-11-03 ~19:30 UTC (agent 3) -- ALL OWED ITEMS DONE; ready to land (not pushed, GT not accepted)

* Commits on c4b7ea18d: engine bottom-cut fix (own commit) / mana(fluctuator) route + hold-up (with the exact cost
  cut) / mana-oracle CAPPED escalation / docs. Levers MTG_PROVIDER_HOLD_UP, MTG_BOTTOM_XTURN_FIX and the refusal
  trace are deleted. Final binary = `build/scratch/mcf/mtg.final4` (units 2437/2437; identical digests/units to the
  hold-up-on arm on 6,000 Fluctuator games). Smoke ran on `mtg.final3` (same play; the cut is exact): `c3/smoke.log`.
* Results and the open USER question (the hold-up's ~+10% Fluctuator CPU): doc 13.14.
* Pre-squash tip kept as branch `backup/mc-fluctuator-presquash-8421e3051`.

## STATE 2026-11-03 ~18:20 UTC (agent 3)

* DONE: Cyclone fix A/B (net -374 / 2HG -67; 7 slower all recover d8b0; units -3.9% / -3.2%), repros, slice
  (on 1 APPLY-DROP gi100666 by the USER's rule; off 2; CAPPED 0; 8 UNMODELED/arm in ONE game seed 30101162, a
  checker defect: scripted Capital City tap fed with the Massif's {R}), hold A/B (net -3 / -2, 0 slower, units
  +0.24% / -0.24%, CPU +16.5% / +19%), units 2437/2437 on final3, smoke on final3 (c4b7ea18d's 12 rows + cyclone
  d3/d5/2hg + fluctuator d0/d3/d5 moved, all better or same, no slower). Doc 13.14 written (one PLACEHOLDER: the
  hold-up cost line).
* IN PROGRESS: the hold-up's CPU. UNCOMMITTED src (ManaPayment.cpp): exact cuts -- amount prefilter, one shared
  trial (final4: identical digests/units on 6000 games; CPU +13.3% -> +10.7% vs off, `c3/cost/`), and an in-place
  solve of the last target (final5, build queued, log `build_final5.log`). Next: final5 identity + cost run
  (`c3/cost/on.json`, arms final5 / final3 / hold3 MTG_PROVIDER_HOLD_UP=0), units on final5, amend 0b4757b94 with
  the cut, fill PHU_COST_PLACEHOLDER, docs commit.

## STATE 2026-11-03 ~15:00 UTC (agent 3)

* SQUASHED on c4b7ea18d (backup of the pre-squash tip: branch `backup/mc-fluctuator-presquash-8421e3051`):
  61329bf4e engine: bottom-trial turn cut plays an owed extra turn (its OWN commit); 0b4757b94 mana(fluctuator): route +
  provider hold-up; 8c7623c54 mana-oracle: CAPPED escalation 20000 always on (env = number override only). Docs pending.
* Levers DELETED in the working tree and in those commits: MTG_PROVIDER_HOLD_UP, MTG_BOTTOM_XTURN_FIX (heurarm slots
  too), MTG_MO_REFUSE_TRACE + Hook::tap_detail. Final build queued -> `build/scratch/mcf/mtg.final3`
  (+ `mtg-test.final3`), log `build_final.log`.
* `mtg.hold3` built (14:47) = 8421e3051 src, levers still in (both default ON). Repros on it (`h3/repros.log`):
  4350 0, 11214 0, 104058 0 (DROP-COVERED=4), 100666 APPLY-DROP=1 with the Tutor rule on AND off (hold2, and
  hold3 with MTG_PROVIDER_HOLD_UP=0). Why: the drop is at T4 plan {Fluctuator}, the payment of Fluctuator's own {2}
  -- a Fluctuator is in hand, so the USER's condition (no Fluctuator in hand or play) keeps the {W} hold off. By design.
* Owed: Cyclone readout when PID 2247805 ends (ab4/xt.*); then on hold3 the slice (run_slice2.sh, MTG_MO_REFUSE_TRACE=1,
  slice3.json) and the Fluctuator A/B (ab4/hold.json); units + smoke (THREADS=8) on final3; doc 13.14; docs commit.

## STATE 2026-11-03 ~13:30 UTC (agent 2; commits 035f36d38, 919f1f733, 5e3c01820 on 25ffdd010)

* Extra-turn bottom cut fix: `GameEngine::PlayOutFrom` stops when `turn > stop || (turn == stop && pending_extra_turns
  == 0)`; TEMP lever `MTG_BOTTOM_XTURN_FIX` (heurarm slot BOTTOM_XTURN_FIX, EngineFlags.h) -- REMOVE before landing;
  unit test in test_cyclone_phase2.cpp. Lands as ITS OWN commit (coordinator).
* Hold-up: Tutor {W} entry LAST, only with no Fluctuator in hand OR on battlefield (USER ruling); unit test at end of
  test_fluctuator_discard.cpp. ProviderHoldUpHold handles the white pip.
* Oracle: CAPPED plan re-realized under `MTG_MANA_ORACLE_NODES_RETRY` (20000) -- 7 CAPPED Fluctuator games (seed
  30000000+gi: 651 2139 2391 2655 2670 2772 2802) all finish at 20000, wall ~unchanged (`build/scratch/mcf/cap/`).
  Refusal trace (TEMP `MTG_MO_REFUSE_TRACE`, Hook::tap_detail) -- 0 refusals in 3,000 games gi0-2999 on hold2; the
  pass1 522 set-aside refusals do not reproduce on current src. Remove the trace before landing.
* Repros on mtg.hold2: 4350/11214/104058 -> 0 APPLY-DROP; 100666 stays 1 (needs hold3 = Tutor entry).
* Cyclone slower games (seed 1810001+gi; base / binding / fix on / binding cut=0 / base cut=0 / binding d8b0):
  74: 6/7/7/7/6/6; 514: 5/6/6/6/5/5; 962: 5/6/6/6/5/(running); 1491: 5/6/6/6/5/4; 2278: 5/6/5/5/5/5.
  Only 2278 is the bottom-cut mechanism, and the fix restores it.
* gi962 at d8 b0 (binding): T5 -> RECOVERS. All five Cyclone slower games recover at d8 b0.
* ab2 FINAL (base 980616e55 vs c4b7ea18d+route; seed 1810001+gi): Cyclone n=3000 net +3, slower 5 (74 514 962 1491
  2278), faster 2, 27/120 digests moved, units +0.067%, CPU ms 72.34M vs 69.55M (+4.0%, contended box).
  Fluctuator 4000 / Galadriel 1000 / Snow 300: net 0, units +0.000 / +0.001 / -0.186%. Report: `ab2/all.err`,
  `ab2/all.out` via tools/ab_report.py + digest_cmp.py.
* Tutor hold: USER ruling -- {W} only when Tutor in hand AND no Fluctuator in hand AND none on the battlefield
  (rationale quoted in the code comment). Commits 5e3c01820, 3942d0ff0.
* Running: `build/scratch/mcf/ab4/xt_all.json` on mtg.hold2 (PID 2247805; Cyclone fix-on 3000 seed 1810001 to pair
  with ab2 r arm = fix-off, verified identical on 50 games; + cyclone2hg on/off 600 seed 2010001). hold3 build chain
  PID 2203520 -> mtg.hold3. Then: repros, `tools/run_slice2.sh` (MTG_MO_REFUSE_TRACE=1) with slice3.json,
  Fluctuator A/B `ab4/hold.json` (4000 + 2HG 2000; MTG_DUMP_WINS=1, --threads 8), delete both levers + trace,
  units, smoke, squash (xturn fix = its OWN commit), doc 13.14.
* Cyclone fix A/B readout: `cat ab2/r.err ab4/xt.err` with r's on_cyclone renamed off_cyclone (sed) vs xt's
  on_cyclone; manifest = ab4/xt_cy.json with an off copy of each job (or use ab2/cy.json names). cyclone2hg pairs
  are on/off inside ab4/xt.err (manifest ab4/xt_2hg.json).

## Commits (on c4b7ea18d, the Auras binding)

* 1cf2ca754 the rebased fix (route + temp flag), e17ee5066 handoff, eb0b7cfad checkpoint (route flag REMOVED, gi205 doc),
  then the hold-up WIP commits (temp lever `MTG_PROVIDER_HOLD_UP` STILL IN) and this handoff. Pre-rebase original:
  branch `wip/mc-fluctuator-prerebase` (a6b591e26). Squash into one commit before landing.

## Mechanisms

1. Continuation paid under the OUTER line hold: OWNED BY c4b7ea18d (`cont_line_armed` / `ExecBindContinuationLine` /
   `Action::rec_line_hold`). This branch's own copy (`MTG_CONT_LINE_HOLD`, `ContinuationLineHold`) is DELETED; c4b7ea18d
   covers every site it did, plus the replay keeps the stamp for the whole section and stamps CastFromGraveyard.
2. Any-colour filter fed by the source the line needs: `AnyFilterFeedRouteHold` src/ai/ManaPayment.cpp:7920 (now via the
   shared `HoldOutFor` :7858), called in `TapForCostShared`. NO flag (removed). Lossless/ordering only.
3. Generic pip off the line's colour (30081285): fixed by c4b7ea18d's binding alone.
4. Pre-draw payment for a post-draw cast (the 2 residual flags): USER's suggested deck hold, IN PROGRESS (below).

## Verified (rebased build, `build/scratch/mcf/mtg.rb1` = flagged; `mtg.final` = route flag removed, identical digests+units)

* Units 2435/2435 (both builds). Slice gi 100000-104999, `MTG_MANA_ORACLE=2` (`build/scratch/mcf/slice_rb1.*`):
  c4b7ea18d alone 51 APPLY-DROP (the same 51 as base) -> this branch 2 (gi104058 Unearth, gi100666 Enlightened Tutor;
  both mechanism 4). 1 of 20 job digests moved. Repros 3033/9815/4938/81285 -> 0; 4350/11214 stay 1.
* Route A/B (c4b7ea18d alone vs route; seed 1810001+gi; `build/scratch/mcf/ab2/r.*`, manifest r_ab.json): Fluctuator 4000,
  Galadriel 1000, Snow 300: 0 slower, 0 faster, 0 digests changed; units +0.000% / +0.001% / -0.186%. In the doc.
* Cyclone gi205 (seed 1710206, T5 -> T6; first A/B): from c4b7ea18d's binding (route cannot fire in Cyclone). RECOVERS
  at d8 b0 (T5 on all builds). ROOT-CAUSED (doc 13.14): mulligan bottom trials; the binding improves the bottom-Emrakul
  trial T7 -> T6, the strict bottom turn cut then stops the bottom-Gyre trial before the EXTRA turn (Emrakul's cast
  trigger) it wins in, round 5 (`GameEngine::PlayOutFrom` src/core/GameEngine.cpp:~180). Pre-existing defect;
  `MTG_BOTTOM_TURN_CUT=0` -> T5 both. Proposed sound fix (not done): stop only when `turn_number > stop ||
  (turn_number == stop && pending_extra_turns == 0)`.
* Smoke: NOT yet run on the final binary (pre-rebase smoke and c4b7ea18d's `build/scratch/auras/smoke.log` have the
  same 12 failing rows; only edf_d5's digest differed between them).

## Running jobs (leave them)

* PID 1955066: `mtg.rb1 --batch ab2/manifest_r.json --threads 4` (on_cyclone 3000, seed 1810001+gi; route jobs done).
* PID 1955207: `mtg.base --batch ab2/manifest_b.json --threads 4` (off_cyclone 3000 on base 980616e55); ~1,800/3,000
  at 11:30 UTC, ~3 h left. Output `build/scratch/mcf/ab2/{r,b}.{out,err}`.
  Report: `cat ab2/r.err ab2/b.err > ab2/all.err; python3 tools/ab_report.py ab2/all.err ab2/cy.json`; units:
  `cat ab2/r.out ab2/b.out > ab2/all.out; python3 tools/digest_cmp.py ab2/all.out` (cyclone = base vs c4b7ea18d+route,
  == c4b7ea18d on Cyclone). Bar: net <= 0, units within ~1%.
* My queued build (buildq, PID ~2139078, background task): hold-up v2 -> `build/scratch/mcf/build_hold2.log`, then
  it copies the binary to `build/scratch/mcf/mtg.hold2` and prints the four repro class lines (task output only).

## The USER's deck hold (for the 2 residual flags) -- IN PROGRESS

* Code (committed WIP): `DecisionProvider::PaymentHoldUp` src/ai/DecisionProvider.h:2956 (ordered list, default none);
  `FluctuatorProvider::PaymentHoldUp` src/ai/DecisionProviders.cpp:28060 = [{1}{B}{R} if Unearth AND Stinger in hand,
  {B} if Unearth, {1}{R} if Drannith Stinger]; `ProviderHoldUpHold` ManaPayment.cpp:7896, called at :8138 after the
  route: greedy trial, then hold ONE trial source out if that keeps rest-of-line + hold payable (preference, never a
  refusal). TEMP lever `MTG_PROVIDER_HOLD_UP` (heurarm slot PROVIDER_HOLD_UP, EngineFlags.h:898) -- REMOVE before landing.
* v1 (single combined hold, `mtg.hold`): repros 30004350 and 30011214 -> 0 (lever off: 1 each). Slice gi104058 and
  gi100666 NOT cleared: gi104058's T4 plan holds both cards and {2}+{1}{B}{R} is not jointly payable (hence v2's
  ordered list); gi100666 needs {W} for Enlightened Tutor, which is not on the USER's list (report; adding Tutor is a
  one-line provider change, the USER's call).
* Next: when mtg.hold2 exists: repros (seed:gi 30004350:4350 30011214:11214 30104058:104058 30100666:100666, oracle 2);
  slice `bash tools/run_slice.sh build/scratch/mcf/mtg.hold2 build/scratch/mcf/slice_hold 4 build/scratch/mcf/slice3.json`
  (on/off arms, count `APPLY-DROP .*job=on_`); A/B `MTG_DUMP_WINS=1 mtg.hold2 --batch build/scratch/mcf/ab3/hold.json
  --threads 4` (Fluctuator 4000/arm, seed 1910001+gi; ab_report.py + digest_cmp.py). Bar: net <= 0, slower games recover
  at d8 b0, units delta reported. Then remove the lever, unit tests, smoke (THREADS<=8) vs auras/smoke.log, fill 13.14.

## Also owed (now checker fix work)

* Pass1 Fluctuator run (`logs/mana_clean/pass1-fluctuator/`): 531 UNMODELED plans "scripted payment refused" and 822
  CAPPED plans -- not classified here; treat as oracle/checker fixes.
* Builds only via `bash build/scratch/buildq.sh`; runs THREADS <= 8 total while the load is high.

## Update (after handoff): hold-up v2 built (`build/scratch/mcf/mtg.hold2`, log `build_hold2.log`, exit 0)

Repros, oracle level 2: 30004350 -> 0, 30011214 -> 0, 30104058 (slice gi104058, Unearth) -> 0 APPLY-DROP (the
ordered list fixed it), 30100666 (gi100666, Enlightened Tutor {W}) -> still 1 (not on the USER's list). Units, the
slice (slice3.json) and the Fluctuator A/B (ab3/hold.json) on mtg.hold2 are still owed.

## Update: Cyclone A/B interim (base arm DONE, rebased arm PID 1955066 still running)

2,563 paired of 3,000: on 5.0737 vs base 5.0726, net +3 (5 slower, 2 faster), 23 of 101 job digests moved, units
+0.056%. Net is POSITIVE so far: this is c4b7ea18d's binding on Cyclone (the route cannot fire there). Owed: finish
the pairing (`cat ab2/r.err ab2/b.err > ab2/all.err; python3 tools/ab_report.py ab2/all.err ab2/cy.json`), check every
slower game at `--depth 8 --budget-ms 0 --ignore-play-profile` on both binaries (mtg.base vs mtg.rb1), and check
whether they share gi205's mechanism (mulligan bottom trial + the bottom turn cut skipping an Emrakul extra turn:
`MTG_BOTTOM_TRIAL_TRACE=1`, and `MTG_BOTTOM_TURN_CUT=0` on both arms). If they do, the turn-cut repair in 13.14 is the fix.
