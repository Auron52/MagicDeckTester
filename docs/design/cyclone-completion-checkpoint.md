# Cyclone completion checkpoint — 2026-10-31

User scope: finish outstanding changes in `origin/cyclone-analysis:docs/design/cyclone-branches-and-next-steps.md`, then re-evaluate. Do not start mulligan/value generation before that review.

Working branch/worktree: `cyclone-final`, `.claude/worktrees/cyclone-final`, based on main `85f4546d3`. Original backup branches and the intermediate finish/land/portable branches remain intact and pushed. Primary checkout has an existing unrelated `test/ref_bench.json` edit; preserve it.

Current stage (02:54 UTC): source frozen at `a7e4708006cef4c7bfc75c89638b2d9feb3eb1a1`, HEAD `ac10ec517`. Both paid-cycle context and continuation-segment replay fixes are committed. Release full units: 2,363 tests / 3,566,610 assertions PASS. Smoke: 150 PASS / 7 audited baseline FAIL / 0 NEW; all 157 outcomes/digests/work counts match source A. Fresh PGO+LTO is complete. Current viewer checks cover 709 strict references and 59 native suspend-affected references, with an explicit unchanged-path proof carrying 648 earlier native results. V1/V2 simulation queues were superseded by concrete replay defects; their partial results are diagnostic only. Regenerate fresh V3 inputs after PGO, complete the 13,142-game pooled comparisons and remaining memory/VERIFY/isolation/quiet-cost checks, then run regression and re-evaluate. No mulligan/value generation or proposal adoption before that review.

The dated milestones below are historical records; later entries supersede earlier progress and pending-work statements.

Independent reviews found extra required fixes:

- KEEP_AMEND reads library size/land count but CantripOrderKey omits them. Add both inputs with a same-board/different-library unit and rank-cache VERIFY run.
- Two-Eldrazi JUNK fix 0b8c27fbb appears right; add explicit k=1 and k=0 JUNK coverage (existing test is k=3; other k=0 test covers SETUP).
- Q61 SeedPaySplits incomplete for partial floating mana; special-source signatures may fold distinct capabilities, Cascade Bluffs colourless face omitted; all-generic guards incomplete. Current-main PayContextDigest must include new seed-payment holds. Payment review agent drafting corrective patch in build/scratch/cyclone-finish/q61.
- Q64 REV_CYCLERS rank 5 is before draws; proposed after-draw/before-Song order requires separate rank ~55 arm.
- Viewer branch has an unfixed wide-board valve regression: EDF claude_s5_gi4 T4 loses existing Mariposa group. Exclude added generic-cycle groups before old valve, append within caps. menu-prefix checker currently exits zero even on failure; repair before using as gate.
- Initial review found a fixed512MB/thread policy. It has since been removed; shared RAM-sized budgets remain. Broader floor/shared-overflow design stays a separate project.

Original pick queue (all integrated):

- prunes: a6766244f e672a8f73 cd8bd730b 9764ea0b8 0b8c27fbb (then test PB before flip)
- cost: 479bb66b6 76a733712
- castorder: 03baf1a73 9066f252d 6e887cbe5 b3f5abd88
- viewer: a89fd7ca2 dbe51e677
- docs from cyclone-analysis: a7ae8e4ce 66ded5bd7 5123a5425 f93a87ce5 (inspect parents/needed docs first)

Validation: unit/scenarios/smoke per group; full regression and viewer protocol/validate/menu-prefix; pooled PB/KEEP/SITU/SEED/blind and cast-order/Q64/payment A/B; memory t1/t6/t12 and tiny12 equality in wins/digests/units. Finish outstanding QC1/QC3/nested-exhaust measurements without deciding user-owned settings unilaterally. Final quiet-box bottoming-ON cost gate against same-run current Hinata. No MTG_BOTTOM_ROLLOUTS=0.

USER steering 2026-10-30: fixed 512MB/thread plan-cache policy is unwanted going forward. Prefer landing Cyclone before cache budgeting; do budgeting first only if necessary. Audit whether exact-key/hit-equals-miss fixes can retain existing shared budgets, then prove with tiny/global-pool thread invariance. Do not simply substitute another fixed quota. Current rebase conflict resolution also preserves main's meter-only census sites and enummemo::BodyScope around cached derivations; budget-units sum excludes only meter-only sites, not newly appended cache-hit site.

## Latest milestone (2026-10-30, after first rebased gates)

- Rebase+docs branch HEAD `2d96ac5dd`; 2,321/2,321 units and 3,564,244 assertions PASS. All 374 scenarios PASS.
- Initial smoke: 146 PASS / 7 FAIL / 4 NEW, batch 6m22s. Seven rows: AntiLifegain d0 digest only; Fluctuator d0 +.002; PD d0 -.003; Snow d0/d3/d5 digest only; Goblins2HG d5 +1 total turn, gi63 (seed1064, b60, T4->5). DO NOT ACCEPT yet. Prunes reviewer investigating these, including current-main baseline comparisons and recovery.
- Fresh current-main baseline `/workspaces/MagicDeckTester/build/Release/mtg` built at 6b655b71f. Initial Cyclone-integrator frozen binary: primary `build/scratch/cyclone-finish/rebase-baseline/mtg`; src/decks links point into worktree for data/default lookup. Smoke snapshot still at worktree test/logs/smoke/mtg.run until next smoke.
- Shared-cache patch now applied UNCOMMITTED (fixed plan/FSL quotas removed; original shared RAM budgets retained; exact keys/cachecharge and meter/residual guards retained). Three meter-only paths mark charging subsearch entry. Build session 85085, log primary build/scratch/cyclone-finish/shared-cache-build.log.
- DOTH+extra-turn regression test applied UNCOMMITTED, but EchoEdgeFoldHere exclusion deliberately not applied yet: first run focused test to demonstrate failure, then add `!s.deck_has_extra_turn` and rebuild. Also migrate the three newly introduced staged expiry reads to TurnSeq (DecisionProviders.cpp 2, GameEngine.cpp 1).
- Prepared fixes/scripts/reviews in primary build/scratch/cyclone-finish; unique drafts also copied to worktree logs/cyclone-completion/prepared (host drive). Q61 corrective+current-main memo patches, Q60 searched information slots, Q64 rank55 amendment, viewer valve/checker fix, JUNK k0/k1+KEEP cache tests all READY, UNTESTED, not applied. Original branches/picks still queued above.
- Run reference safety scanner passed all 26 worktrees: every uncommitted reference backed on origin/autosave. Primary unrelated test/ref_bench.json edit untouched.

## Pruning integration underway

- Shared quotas removal committed as `9db23420e`; existing units PASS. Extra-turn edge fold/expiry repair committed as `853c02e63`; regression failed before fix in both mains, passed 110 assertions after; full units 2,322/2,322 PASS.
- All five pruning picks now integrated: c877bf1fd / cbc64d70c / 13b722aeb / 8264e50d9 / 4b4625b26. Preserved Q36's seed-walk fold while updating CycleSeedAdmitted(state,...), preserved discard bucket code and appended distinct ledger sections.
- Additional local uncommitted fix: CantripOrderKey adds remaining library size and land count under KEEP_AMEND; new JUNK k0/k1 and density-key tests applied. Build session 97583, primary build/scratch/cyclone-finish/prunes-build.log (full header rebuild).
- Shared-cache smoke session 61772 uses frozen pre-pruning binary; still running. Output primary build/scratch/cyclone-finish/shared-cache-smoke.log. Compare final per-game outcomes against initial rebase smoke; expected same known differences. Do not start another smoke into its output directory until it completes.
- Initial smoke audit complete: all 15 changed Snow game fingerprints are reproduced EXACTLY by fresh current main (pre-existing GT drift, not Cyclone). Goblins2HG gi63 restores exact old T4/digest with MTG_CACHE_HIT_CHARGE=0, default recovers exact old T4/digest at d8 b0; b240 (4x) does NOT recover. Audit evidence primary build/scratch/cyclone-finish/smoke-audit/verdict.md. No GT accepted.
- Next: after pruning build, focused Cyclone tests, full units, smoke (after shared-cache smoke finishes); fix failures. Then serial cost picks, castorder picks + Q61/Q60/Q64 drafts, viewer picks + valve/checker draft. Measure on FINAL integrated tree, never stale old .wins. All new candidate levers remain OFF until their bar/USER decision; CYCLER_RULE is ON per existing adopted branch. PB_PLANS remains OFF until fixed PB A/B is finished.

## Cost picks and the discovered shared-plan overlap

- Pruning build finished; 81/81 focused cases and full 2324/2324 units PASS (3,564,480 assertions). Density-key/tests committed `0f769dee5`.
- Pruning smoke STOPPED at scenarios: 373/374; `cyclone_plans_two_eldrazi_emrakul_first` failed its Emrakul-first log sequence (still wins round3). SITU-off fixture copy PASSES. Diagnosis: fixed JUNK now equals best SETUP; single-role classifier labels it JUNK, then SITU ready discards it and substitutes inferior mirrored SETUP.
- Do NOT use blanket SETUP-before-JUNK: independent reviewer supplied counterexample with distinct best SETUP [Deluge,Emrakul] and fixed JUNK [Emrakul,Deluge]. That would lose a real JUNK option.
- Correct fix applied UNCOMMITTED: `DecisionProvider::kCantripPlanBoth=-3` (roles0/1), generic CantripPlanFilter selects each role's first/best candidate and folds identical selected winners. Cyclone marks overlapping candidate Both; SITU converts to surviving sole role before filtering. New overlap/SITU test + three filter-order property subcases; direct k0/k1 pair tests expect Both. Independent review confirms exact union-of-winners, no new cache inputs, fan cap2 sound. Details primary build/scratch/cyclone-finish/putback-overlap-review.md.
- Cost picks integrated: `1bb3a9fe7` / `0c9eb2c0f`. Preserve current-main BoundaryChooseByRollout while setting ko_rolled; keep meter-only sites excluded from budget-units totals.
- Superseded cost build session17319 was stopped by root (own build) after unsafe blanket relabel was caught. Corrected full build session95245 is live, log primary build/scratch/cyclone-finish/cost-build2.log. Run focused Cyclone + role-filter + leaf-kill-only tests; full units, failed scenario directly, then smoke. Last smoke session18028 finished exit1 before batch due the scenario above.
- Shared-cache smoke completed: EXACT equality vs initial rebased integrator in all 157 cases' played/avg/digest/total-units; 146pass7knownfails4new unchanged. Thus restored shared budgets introduce zero smoke drift. Pressure/thread proof still pending on final source.
- QC1/QC3 manifest generator ready with profile copies under output; primary build/scratch/cyclone-finish/qc-inputs/. QC1 sampler600 forced-seven d0/maxturn1 games is sampling only, NOT measured policy. After traces exist regenerate with --qc1-traces; selected paired d3 arms use normal policy vs curve_check:none (no forced hands). Separate no-cheap>=2lands historical cohort and one-land-only stratum. QC3 pooled480 games: (leaf,first)=(1,1),(0,1),(0,0), d3s127000x100b10 + plays128000x60b20. Updated helper copied to logs/cyclone-completion/prepared/.

## Cast/payment complete; viewer integration (latest)

- Q61 corrective source+floating allocation search committed `e1f2b895e`, including executor/rollout atomic payment holds, full payer context keys, floating-red missing-line fixture and OFF negative control. Default remains OFF for held-out measurement.
- Q64 middle cycler rank55 committed `3d99bc576`; existing rank5 diagnostic stays separate. Q60 four searched information orders committed `0e14954c2`, applying the funding ladder first. Funding-order regression added.
- Current cast gate: 98 focused cases / 8,020 assertions and all 2,344 units / 3,565,907 assertions PASS; all 378 scenarios PASS. Cast smoke 146 PASS / 7 known FAIL / 4 NEW, same as earlier source; detailed tuple comparison pending.
- QC1 sampler600 completed with traces: exactly28 hard-filter failures,381 already TwoDrop-pass,94 one-land-only,97 no-cheap>=2lands (70 with creature cycler). Sampling outcomes ignored. QC1 selected policy measurements and QC3 still pending.
- Viewer picks `dddb4f2f0`, `f07deed6a` integrated preserving current-main Chandra nested cast serialization, mana oracle/audit notes, ZoneCard display costs and board-specific payment reasons. Prepared valve and checker fix applied; Q62 funding-ladder label corrected. Release build session70633, log primary scratch/viewer-build.log. Before-viewer binary frozen at primary scratch/before-viewer/mtg.
- Fixed per-thread plan AND FSL quota policies were removed in `9db23420e`; shared RAM-sized pool policy retained. Broader cache budgeting is separate. This supersedes historical branch notes about512MB/thread, not a prerequisite implicitly adopted for Cyclone.

## Final review before optimized measurements

- Viewer before-review full units PASS:2344 cases /3565907 assertions. Frontend line-only checks PASS. Pin admission exception and Q61 fan census added; source-level final review also discovered failed explicit searched seed falling through to another heuristic cycle. Regression proved six failures before the guard (hand, graveyard, library, life and taps changed); guard now applied. No-seed playout remains covered as control. Final rebuild required.
- Viewer menu preservation/checker fix committed2bb51290d; Q61 fan-width and JUNK VERIFY observational reports4449e4a82. Separate failed-seed guard+test committed immediately afterward. Prepared source drafts are scratch only; applied implementations are tracked.
- Pre-freeze fetch found current main moved four commits to930cc5c33 (continuation-cache pin identity fold, Midrange Burn discard, Treasure Hunt artifact+GT). Rebase onto that current snapshot before final Release/PGO build and measurements. Primary unrelated ref_bench.json remains untouched.
- Final prepared pooled quality manifest:12542 games /1430 jobs /12 workers, including pruning12arms7200games, cast raw+blind3000, payment600, nested600, QC3 480, selected QC1 194, memory12normal168+300 shape. Other thread/pool variants remain independent process runs; quiet535-game cost gate separate. All require refreshed provenance and clean source plus matching PGO stamp before launch. No long simulations launched yet.

- Rebase onto930cc5c33 completed with no conflicts; pre-freeze GT consistency883/883. Corrected bounded pin fixture uses implemented Bone Saw/Paradise Mantle (original scratch Memnite/Ornithopter were absent); ordinary cap32 omits combined line, explicit Street Wraith pin restores it even though Cloud of Faeries is family front, kept positions30/32. New check is tracked in test/viewer_cycle_pin_check.py. Final Release rebuild92782 includes all source changes plus current main; stale-seed guard green proof pending this build.

## Frozen-source gates (current main930cc5c33)

Source tree `ce3431d90d7f7ee88b47a791ce61595740f907f0`, code/test HEADb925035e9. Final optimized Release build92782 complete. All2349 unit cases /3,566,068 assertions PASS. Stale searched-payment regression now41/41 PASS (six failures before). All384 scenarios PASS.

Serial viewer validation runner is active, owned by payment reviewer session98998; live state primary scratch/final-viewer/status.json. Cycle pin stage PASS. Remaining: EDF/cross-deck exact menu prefix, full strict protocol4threads, full validate, sampled viewer layers. No other heavy local job runs alongside it. Next: source-matching PGO, seal prepared12542-game primary pool, then outstanding process-only memory/iso92/VERIFY and quiet cost runs. Source proposal flags remain OFF pending proofs; no profile/value generation.

Shared-budget removal was rebased to `e9135d9ef`. Original9db23420e is a historical pre-rebase hash. No fixed per-thread plan or FSL quota remains.

## Optimized build and viewer status (23:10UTC)

- Backup branch pushed to origin/cyclone-finish; HEADae635995f before this checkpoint. LinuxCI41140760861 PASS including units/viewer/determinism; Windows still building. CI waiter56547 remains active.
- Viewer runner98998: cycle pinPASS; EDF72frames exact prefixPASS;15 cross-deck referencesPASS; full strict protocol699refsPASS (264ok,434repaired stale indices,1suboptimal beaten, zero drift/gaps/contract failures). Stage05 full line validation silently resolves697 proper refs serially before checking8285 recorded main decisions; stage06 sampled layers still queued. Payment reviewer owns waiter.
- PGO build99103 (final-pgo-build.log): instrumented build complete; stage2 trains whole suite197jobs/5513games with8workers; stage3 profile-use+LTO pending. This overlaps only the lightweight serial viewer checker, uses separate build/pgo-work and build/PGO, never changes Release/source.
- Ten reproducer helpers/records now tracked under logs/cyclone-completion/prepared. Memory reporter tightened to require actual tiny20MB pool pressure (positive refused stores), exact per-game wins/digests/units, and separate FSL refusal warnings.10 synthetic tests PASS.
- Planned order after PGO: already-owed memory6 and memory1 process runs before the12k main pool, to detect any default FSL binding/unit variance before hours of A/B collection; no additional or duplicated work. Final quiet timed cost gate remains separate after all other local work.

- PGO99103 completed successfully: all197 training jobs/5513games, fresh build/PGO/SRC_TREE equalsce3431d90d7f7ee88b47a791ce61595740f907f0. Memory6 preflight is now running via launcher session39156, engine PID1447027, output final-pgo-inputs/runs/memory_t6_normal/.468games include168 shared-reference games plus300 shape games. Peak observed RSS1.49GB; no timed cost inference while lightweight serial viewer validation continues. Memory1 follows, then memory-preflight comparison before12k pool.

## Shared-cache preflight underway (23:30UTC)

- Full CI41140760861 now PASS: Linux, Windows, and Linux/Windows determinism parity. Watch56547 complete. PGO99103 complete and source-matching; binary SHA795a713382ea7be04453327cfe64f99d3d9fdcaaf30a32a3bc13b9a8d17b09c3.
- Memory6 DONE0:468games,6jobs,23:14:03--23:23:47UTC. Shared plan budget1,027,328KB reached hiwater1,027,326KB, refused14stores, residual0KB; FSL did NOT bind. Peak RSS observed1.49GB. Output final-pgo-inputs/runs/memory_t6_normal.
- Memory1 running, launcher38584, engine1453228, output runs/memory_t1_normal. First completed24-game play block EXACTLY matches memory6 avg/digest/43,056,999units. Full168 shared-game comparison pending;300 t6-only shapes are compared later to primary12 output, not claimed covered by this preflight.
- New memory-preflight reporter requires all168 shared games plus the300 shape inventory and explicit FSL review if it binds.14 synthetic reporter tests PASS. Persistent scripts updated; helper/docs changes are pending a commit.
- Next: after memory1, report memory-preflight; run fresh-source PGO smoke (post second rebase), audit per-game changes before any GT acceptance. Initial7known mismatches are from the6b baseline; current main9368 continuation-key fold may alter them. Then main12542-game pool, iso9240salt process modes0/2, data-dependent symmetric blind/movers/recoveries, remaining memorytiny12/unbounded6, smallVERIFY, quiet final cost, full regression+audit and linear landing as appropriate.
- Viewer98998 still in full validate stage05 (~32min) with no failure emitted; serial reference alignment and successful validations are buffered. Stage06 sampled layers pending. Release/source hashes unchanged; only this lightweight checker overlaps heavy build/batch.

## Blocking fidelity repair (2026-10-31 shortly after00UTC)

- Memory1/6 preflight PASS: all168 shared per-game wins/digests/units exact; FSL0 binds;6-only300shapes inventoried. New post-rebase PGO smoke completed146PASS/7previouslyauditedFAIL/4NEW, no unexpected engine drift. Expected upstream MRange/TH changes only; noGTaccepted.
- Primary enlarged to13142games/1490jobs by restoring owed Q60DRAW arm (newfour searched information orders), preserving separateQ64rank5/rank55. All noncast/runtime files hash-unchanged. PBCYC reporting now compares liveCYCLER baseline; Myriad-only contrasts also added without moregames.
- Primary launcher39321 started23:52:46UTC, engine1487121; root sentSIGINT at23:58(~5m30) within allowedfirst10min defect window. STOPPED/-2 confirmed. Ordinary simulated FDseed107008/GI8 predictedT4 but actualT5, leaf_estnone. Old memory6 actualdigest/unit matches, notthread drift. Other FD warnings were blinded-future forecasts. All old results preserved+markedINVALIDATED-BY-FD107008.md; no aggregate/adoption verdicts.
- Two isolated PGO reproductions in scratch/fd107008 confirm. Initial hypothesis (not established): already-paid Curator seed hold may re-arm only in rollout due temporary same-name hit inHAND; executor hit is onSTACK. Rollout pays Remandgeneric fromMountain, leavesUforanothercycle/Emr; executor spendsblue and leavesMountain. Calendar/extra-turn counters are consistent; payment causes loss of extra turn.
- Focused physical-source regression was added and failed old engine exactlyone of50assertions (allzones/counts matched, landtap identities differ). Applied CycleSeedResolvedScope repair in ManaPayment.h plus executor/rollout/human sites: copycurrenttraits, clearonlymatching paidseeddef/pips/floor throughresolution; nested newPlanTraitsScope still supersedesit. Added nested-seed/pending-Lodge behavior regression. SourceUNCOMMITTED, greenbuild34708 running, logpaid-seed-green-build.log. Next: focused/fullunits,384scenarios, sameFDcase no-divergence, then freshPGO/newv2manifests/reruns. No source writes byreviewers.
- Before-fix PGO preserved at scratch/paid-seed-before-pgo/mtg withsrc/deckslinks.
- Viewer oldsource gates01--05PASS, fullvalidate697refs3,287accept/285choose/1unsupported/420nonhand skips/10baselinev1fail/0regression; mana2945yes/0NO/0unaudited. Stage06 sandboxedworkerfailedsocket/clientfixtures; rootunrestrictedretries bothPASS: saveparity18.474s, client274.526s. Manualtap5assertiondiagnostic leakage identicalbefore/final; Kittys31gi30twoalignment skips identicalbefore/final, actualprotocolWinT6passes. Interactiveparityand51sampleprotocolalsoPASS. No userreferencechanges/serverstart/permissionprompts.
- Originmain advanced to03688a120 via2Midrange-onlycommits. Newprovider Fireblast/Lavamancer hooks areguarded bycardparams absent inCyclone/Hinata/Kitty, so do not mutate thisfrozen experiment. Preserve pushedcyclone-finish history; laterlinearintegration viafreshunpublishedlandingbranch/cherry-pickifneeded, notrewritingpushedrefs.

- Follow-up: paid-seed clearing alone does NOT fix FD107008 or the physical-source test. Disabling existing MTG_SCARCE_COLOR_HOLD makes the focused test pass; disabling NeedsTap does not. Main rollout PlanTraits scope closes before root cycle while executor traits remain live, and site-4 continuations also lack a fresh scope. Existing-BP_TRACE-guarded actual payer diagnostics are applied temporarily; build44567 is in progress. No cause/fix claim until measured context and green reproducer.

## Measured cycle-context repair (00:33UTC)

The initial paid-seed-only explanation was incomplete. Actual BP-trace payer observers in
`fd107008/payctx-r1` show rollout Remand has `traits=null, mid=0`; executor has live
`mid=1, mana_casts=0`. Both have `seed=none, actmask=0` under the draft paid-seed repair.
The provider's existing scarce-colour rank therefore pays different physical lands:
rollout spends Steam Vents+Mountain; executor spends Steam Vents+Watery Grave.

Root now saves the exact original pre-cast traits, restores them only around the trailing
searched seed (including capture/resume snapshots), and installs the site-4 continuation's
own traits before precasts through its nested seed and activations. Consumed-seed clearing
remains necessary to prevent a temporary same-name free hit from resurrecting the old hold.
No heuristic/ranking change or cache policy change is part of this correction.

Focused source/payment tests PASS2/2,103assertions. Full units PASS2351/2351,
3,566,171assertions. Original ordinary seed107008/globalGI8 now predicts and executes T4
without FD/nonconv: per-game digest758163c5936e1ab8,1,099,992units, actual extraT4 attack25.
The searched legal line differs: Song+Song+Cyclone, cycle spareCyclone into secondCyclone,
cycleCurator into Emrakul, cycleWraith into Ulamog, then extra-turn attack. Diagnostic
payer patch has been removed; clean optimized rebuild46115 in progress. Prefix-resume
behavior regression is being added before committing/freshPGO. Previous v1 measurements
remain invalid as final-source baselines; v2 must use fresh directories and diagnostics.

- Prefix-resume and site-4 behavior regressions now PASS: focused4cases/209assertions.
  The site-4 ritual/cycle fixture also passes with scarce-colour ranking disabled; disabling
  activation-line holding too reproduces the stranded cycle (4failed assertions), so the
  test observes a real colored reservation rather than only counting spells/sources.
  Original full2351unit/384scenario gates preceded the two added regression cases; production
  repair is unchanged except the optional state outputs on the existing test-only resume hook.
  Next integration target is current mainca9132b9d (four new commits after930); preserve the
  pushed finish branch and replay onto a fresh landing branch without rebasing pushed commits.

## Recorded-cycle context follow-up (source WIP on cyclone-land)

Replay review found the root snapshot restore alone is incomplete: actual TakeTurn normally
replays root DigDraw records inside a fresh record-list scope. That list can omit the owner's
already-paid casts (e.g. mana_casts2 becomes0), changing the existing one-shot hold even with
mid_turn_casts correctly true. The low-level PerformDigFull test bypassed that wrapper.

Each recorded searched cycle now carries an immutable shared PlanTraits snapshot taken from
its owner before payment, plus the exact pump hold. Replay installs it around that cycle only;
its nested records regain their own context. The surrounding record-section scan ignores this
per-cycle metadata. Allocations happen only when recording an apply (including evaluated FSL
candidates, not merely the final committed line); enumerated actions remain null. New payload
heap is included conservatively in existing shared byte accounting; there is no new quota.
A generated-records→CommitPhasePlan→actual TakeTurn regression with two paid Bonesplitters,
Curator/Remand and a Treasure has an explicit metadata-removed control. Build91590 is underway;
AIEngine scan isolation and payload accounting were edited only after their TUs completed and
require a canonical follow-up build before trusting results. No v2 measurement launched yet.

## v2 source freeze gates (00:57UTC)

Canonical follow-up build30479 completed, including the record-scan isolation and recursive
new-payload accounting. All2360unit cases /3,566,447assertions PASS. All384scenarios PASS.
The five focused context/payment cases PASS262assertions, including the real TakeTurn
metadata-removed control. Original seed107008/globalGI8 remains a legal actual/predictedT4,
per-game758163c5936e1ab8 and1,099,992units, with zeroFD/nonconv. Source is integrated on
current mainca9132b9d via fresh cyclone-land; pushed cyclone-finish remains intact at4aaf2e47f.
GT consistency894/894, no stale/missing logs. New source-matching PGO and v2 viewer full sweeps
are next; PGO smoke must pass/audit before any v2 measurement is trusted. Previous v1 runtime
outcomes are not reused as v2 baseline/adoption evidence. No profile/value-leaf generation.

## v2 PGO / smoke / viewer progress (01:13UTC)

Fresh suite-trained PGO21320 DONE:197jobs/5513traininggames; SRC_TREE
`d11370b6daaaffe3206811dec9ef213ff1a1cac1`, binarySHA
`d74a112e70c640e34cb6d4758f64c9fb73004b8ea1826ce8c6e6ca2a924c8de3`.
PGO smoke8762 completed150PASS/7knownFAIL/0NEW,160s batch makespan. The seven
mismatches and their per-game fingerprints exactly retain the earlier audits; noGTaccepted.
Against preservedv1 smoke, only ten upstream FiveColour/Fungus/Midrange jobs change
outcomes/digests/units; all147otherjobs exact. All seven changed win/digest jobs match
current-main GT. Full difference is scratch/v2-smoke-v1-diff.json.

Newly enabled FD diagnostics found Niv-Mizzet s1082/globalGI81, predicted5/actual6,
provenT1/leaf_estnone. Isolated frozenv1PGO control reproduces the EXACT same FD,
actualdigestec2b6791edf31ecb and6682units. This is a pre-existing non-Cyclone issue,
not a new integration defect; evidence scratch/niv1082-old-control. Nonconv0.

New v2 inputs are sealed at13142games/1490jobs, no preparation warnings; only the
validated600 QC1 setup hands are reused (97selected), no old outcome baselines.
Memory6/1 now explicitly enable FD_ORACLE and NONCONV. Normal default shared RAM-sized
cache budgets remain; no per-thread quota exists. Starting fresh memory6 preflight.

V2 viewer runner52855: cyclepinPASS; EDF72frames/47grownPASS;15-ref menu prefix has
only two FiveColour menu differences isolated exactly to the four upstream mana-oracle
flags (turning those OFF restores all6frames). Both Cyclone references pass. Normal-default
full strict699PASS:264ok/434repaired/1beaten,0drift/gaps/contracts;697validate now running.
Root fixture30084 DONE allPASS: client313.982s, saveparity22.704s, review1.581s,
referencebackup6.405s. No persistent viewer was started and no user reference changed.

- Independent sealed-v2 input audit PASS34checks (all6castarms, livePBcontrol,468memory
  references with no jobflags, exactsource/PGOseals,97QC1hands,zerooutside-v2runtimepaths).
  Conditional blind/recovery launches must explicitly retain environment cleanup plus
  FD_ORACLE/NONCONV; their standalone pruning run plans only list DUMP_UNITS/SLOW_GAME_MS.
- Memory6 source preflight running53678, owner1562906/engine1562975 since01:13:44UTC.
  First79completedgames have0FD/0nonconv; observedVmHWM1,418,080KB. First24playreference
  avg5.2917/digesta653af4cfb6f7151,43,422,021units. No result-invariance claim until
  full168pair comparison against the separately rerun memory1 process.

## Shared-cache preflight passed; primary in progress (01:37UTC)

V2 memory6 DONE468games and memory1 DONE168games, both0FD/0nonconv. Strict preflight
report PASS: all168 paired per-game wins/digests/units EXACT,0differences/0problems,
FSLdidnotbind. Memory6 sharedplanbudget1,027,328KB reached1,027,325KB and refused4stores;
per-game plan-cache max858,347KB, already above the rejected512MiB quota. Poolpressure
was real, with neutral refusal reporting. Primary11958 started01:37:19UTC on frozen
sourceD113/PGOd74,13,142games/1490jobs. At450s:1010recordedgames,94blind forecasts,
0ordinaryCycloneFD/0nonconv/0ambiguousattention; observedVmHWM2,961,308KB.

CI41152959238: LinuxPASS; WindowscompileFAIL due five unportable __builtin_popcount calls
from imported upstreamb5412dbd8, not the cycle-context changes. Complete archive log is
scratch/final-v2-ci-failed-archive.log (individualjob Azure download failed twice; archive
fallback succeeded). Upstream already fixed exactly those five calls in26d381105 via
std::popcount; isolated persistent cyclone-portable worktree/branch934c57284 carries that
fix, source09dcd682405659b10ae780d29d1c06ebce54e11a, CI41155602357running. The active
measurement/viewer checkout stays frozenD113. The replacement has identical population-count
semantics on Linux; never relabel old provenance as the other tree. Final integration/cost/
regression must include the portability fix. BackupfinishCI41151364956PASS bothOS+parity.

Originmain has meanwhile advanced to85f4546d3 (portablecount fix, new Midrange references,
published replay-index preservation, multiple-suspend viewer fix, contractdocs). Preserve
pushed histories; integrate currentmain later on another fresh branch, assess applicability
of those changes to the measured autoplay pool, and run required final integration gates.
Do not change the frozen live source or apply proposal defaults while this run is active.

## New common replay bug; v2 comparison superseded (01:53UTC)

QC3leaf0 seed127012/globalGI12 exposed ordinary FD predicted5/actual6, provenT4,leaf_estnone.
Both sourceD113 and frozenv1CE reproduce it with identical actualJSON+121diagnosticlines;
current89312units/old89314. Concrete difference: extraT5 Brainsurge alone was simulated
under mid_turn_casts0, while end-of-main replay merged ordinal0Brainsurge with ordinal1
rootCuratorDig, inferred mid_turn_casts1, and tapped different lands. Actual then cannot
cycle for another extra turn. This is a real common engine-context bug, not an oracle label.

Primaryv2 agent-initiated composite A/B was stopped at982.34s, with partial outputs retained
and marked SUPERSEDED-BY-FD127012.md. The initial600s cancellation guard was too broad:
CLAUDE.md explicitly permits stopping an A/B the agent chose to launch at any age; the user
never requested this particular13142-game queue. No permission question was needed.
No partial aggregate/adoption evidence will be reused as final-source results.

Next: narrow catch-all fix to preserve remaining rec_bp_ord segments via existing replay_segment,
red/green physical-payment regression+exactFDcontrol, then fresh final integration on currentmain
(including its std::popcount portability fix). Live viewerA source stays untouched; it has now
completed699strict and697nativevalidate with exactlyv1's clean totals/mana2945/4pins; queued
crosschecks remain. Its two Kitty alignment skips are baseline limitations, not new regressions.

## Replay segments fixed on final integration (02:15UTC)

Active implementation checkout is now `.claude/worktrees/cyclone-final`, branchcyclone-final,
based on currentmain85f4546d3. Originalfinish/land/portable branches remain pushed intact.
The one integration conflict kept BOTH protocol helpers (published candidate-index remapping
and extra-turn `_tkey`); no GT conflicts. Current main already includes portablepopcount.

Two realTakeTurn regressions were added before the fix. Old behavior failed5/151assertions:
wrong physical BloodCrypt/Steam taps for consecutive and gapped ordinal sections, plus a
WirewoodLodge untap delayed until after the next section's Bonesplitter. Catch-all now invokes
the existing replay_segment machinery perremaining ordinal, including gaps, instead of merging
allremaining records into one inferred context. Green7focusedtests/413assertions PASS;
full2363units/3,566,610assertions PASS. ExactFD127012 nowactual/predictedT5,0FD/nonconv,
89312units. FD107008 stillactual/predictedT4,1,099,992units. Niv1082 stillits pre-existing
FD6vs5/6682units (general segment fix does not resolve that separate issue).

V2viewer finished all12stages:699strictPASS;697nativevalidatePASS withv1's exact3287accept,
285choose/1unsupported/420non-hand/10knownbaselinefail/0regression;mana2945/0lost/0unaudited;
4pinsPASS. Interactive/stateless parityPASS. Only two rawnonzero stages are fullyclassified
(upstreamFiveColourprefix change, byte-identical5manualtap trace failures); three reachability
SKIPs matchv1. Rootclient/save/review/refsafetyfixturesallPASS. Fullv2evidence is historical
sourceD113; final gates will cover currentmain's tennew Midrange refs and changed checker/UI.
Costreviewer is adding the minimal missing suspend-verb reconstruction to nativevalidator,
then targetingMidrange/Dragonstorm/Landfall suspend cases; autonomous segmentfix does notrun
in the nativehuman ApplyPlanDirect validator, so unchanged old coverage can be carried with
that explicit path proof. No profile/value generation or proposal-default adoption.

- Final integration Release smoke40304 completed150PASS/7knownFAIL/0NEW,312s batchmakespan.
  ALL157case outcomes/digests/units exactlyequalsourceA's PGO smoke (compiler timing excluded).
  No new changed game; prior7GTmismatch audits retain exact fingerprints. Only Niv's proven
  pre-existing FD remains; nonconv0. GT894/894consistent; candidate-remap unitsPASS; changed
  linebuild suitePASS includingtwo-suspend-copy cap. No GT accepted yet.
- Next composite scheduling front-loads all480 QC3 fidelity cases, preservingexactgames,
  flags/seeds/globalindices, so mode-specific defects surface before largeprunecollection.
  Newmain validator's suspend reconstruction is fixed;59native refs cover all18 old/new
  selectedsuspendactions, with648 unaffectedoldrefs carried by explicitpath/byte proof.

## Final source PGO and viewer gates (02:50 UTC)

- Final PGO+LTO completed whole-suite training (197 jobs / 5,513 games); source stamp
  `a7e4708006cef4c7bfc75c89638b2d9feb3eb1a1`, binary SHA256
  `c8fe5a6cf8fafc5d85a3e85dbf42aef506f1a482bf10bef09d1fcbdc848743d1`.
- Fresh V3 inputs are sealed at `build/scratch/cyclone-finish/v3/final-pgo-inputs`:
  13,142 games / 1,490 jobs; manifest SHA256
  `7072c81a61fae1eca716921949c12a28a1d0ffff10c31fc89a31e8ae42b07b8e`.
  Independent audit PASS36/36: no preparation warnings, semantic changes or old result paths.
- Small VERIFY PASS: 141,330,774 verified JUNK hits, 5,453,552 rank-cache hits,
  6,508 verified kill-only checks, no mismatch. Diagnostic exercise, not a timing A/B.
- Final viewer PASS: all709 strict references (274ok/434repaired/1beaten), zero drift,
  gaps or contract failures. Full linebuild4064 lines /707refs PASS; rendered suspend
  badge10checks PASS; published-index unit checks and capped-cycle pin PASS.
- Fresh native59 (Midrange10/Dragonstorm39/Landfall10):252accept/2choose/0unsupported,
  22non-hand skips/2known baseline limits/0REGRESSION;249mana-findable/0lost/0unaudited.
  These cover every18 selected suspend action; six old cast-signature baseline entries
  now resolve correctly through suspend reconstruction. Baseline file unchanged.
- Other648 native refs carry A's prior results only through the sealed reference/apparatus
  hashes and exact unchanged human/ApplyPlanDirect path proof. This is not a fresh707-native
  sweep. Filtered pins0/0 are expected; A's4/4 Bruna pin controls remain carried.
- Root-owned Midrange client fixture PASS:38rest points and exact undo; two existing
  unreachable seed16 fixture checks remain SKIP. Runtime report and count/provenance proof:
  `build/scratch/cyclone-finish/v3-viewer/REVIEW_CLASSIFICATION.md` and
  `classification_status.json`; all raw logs and guards preserved, no viewer jobs active.
- Reference-safety scan PASS across28 checkouts/worktrees; primary's unrelated
  `test/ref_bench.json` edit remains untouched. Linux/Windows CI and cross-platform determinism parity PASS (run41160289976).
- Normal6/1 memory preflight is running on final PGO. Completed normal6 reference cells
  and the100-game play shape match source A exactly in outcomes/digests/units so far.
  No final memory/adoption/cost conclusion before the remaining checks complete.

- Final normal6 memory completed468games, zeroFD/nonconv, exactwins/digests/units versus
  source A in all6 cells. Sharedplanbudget1,027,328KB reached1,027,327KB,8refusedstores,
  residual0; FSL did notbind. Per-game maximum858,347KB exceeds512MiB and confirms
  there is no fixedper-threadquota. Normal1 counterpart is nowrunning; no finalt12/tiny
  proof is claimed yet.

## V3 preflight closed; quality queue active (03:15 UTC)

- Final normal1/6 preflight PASS: all168 shared games exactly equal in win turn,
  digest and work units; noFD/nonconv or FSLbinding. Normal6's300 additional shape
  games also match source A exactly. Report: `v3/final-pgo-inputs/reports/memory-preflight-report.json`.
- Four final smoke controls completed/reviewed on the frozen PGO binary. Goblins2HG
  seed1064/GI63 atd8b0 recovers exact `63 4 13a9211a976e729a`; opening/mulligan/full
  turn arrays match the preserved old recovery. Three d0 sanity traces are plausible:
  empty-search Anti Mire; empty-search Fluctuator Tutor thenT8 double-Stinger lethal;
  PD own-life loss is correctly unwon, with the explicit sticky-loss sentinel.
- All157 final smoke results are byte-identical to source A. Snow's15 changed
  fingerprints each match prior fresh-main controls. PD has45 additional digest-only
  identities beyond the historical four-game whiff audit; no precise cause is claimed
  for those45. Final smoke audit and reviewed artifact hashes:
  `v3/final-smoke-audit.md`, `v3/regression-controls/final-controls.reviewed-provenance.json`.
  No GT has been accepted yet.
- PrimaryV3 began03:02:53UTC: launcher1663237, engine1663268,12workers, tool session60061.
  Runtime status/logs: `v3/final-pgo-inputs/runs/primary-quality/`.
  First heartbeat:12/12busy, RSS1.9GB. The whole480-game QC3 cohort completed by165s,
  zeroFD/nonconv/leafwarnings; all three leaf/first-turn modes exercised. At634s the
  pool had1650 completedgames; only blinded-cast forecast diagnostics, no ordinary
  FD/nonconv. These forecasts still require their paired ordinary-world review.
- Primary remains one13,142-game/1490-job queue. Independent reviewer monitors exact
  attribution; no adoption or whole-pool quality verdict from partial data. Conditional
  pruning manifests may be prepared once all720PhaseCjobs are complete; no follow-up
  engine runs overlap this pool. Then remaining tiny12/unbounded6/iso92, conditional
  blind/recovery proofs, full regression and quiet535-game bottoming-ON cost gate.
