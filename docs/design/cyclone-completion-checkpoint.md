# Cyclone completion checkpoint — 2026-10-30

User scope: finish outstanding changes in `origin/cyclone-analysis:docs/design/cyclone-branches-and-next-steps.md`, then re-evaluate. Do not start mulligan/value generation before that review.

Working branch/worktree: `cyclone-finish`, `.claude/worktrees/cyclone-finish`, branched from `origin/cyclone-int` (3892d2159), rebasing onto current phase-1-2 6b655b71f. Original backup branches untouched. Primary checkout has an existing unrelated `test/ref_bench.json` edit; preserve it.

Current stage: source frozen at `ce3431d90d7f7ee88b47a791ce61595740f907f0` against main930cc5c33. Release and fresh PGO builds complete;2349 units/384 scenarios and full699-reference strict protocol PASS. Full viewer line validation is still running serially. Memory6 finished468games; memory1 counterpart is running. After memory preflight, run final post-rebase smoke before trusting/adopting measurements, then launch prepared12542-game quality pool. No generation before final re-evaluation.

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
