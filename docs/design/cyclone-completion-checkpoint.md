# Cyclone completion checkpoint — 2026-10-30

User scope: finish outstanding changes in `origin/cyclone-analysis:docs/design/cyclone-branches-and-next-steps.md`, then re-evaluate. Do not start mulligan/value generation before that review.

Working branch/worktree: `cyclone-finish`, `.claude/worktrees/cyclone-finish`, branched from `origin/cyclone-int` (3892d2159), rebasing onto current phase-1-2 6b655b71f. Original backup branches untouched. Primary checkout has an existing unrelated `test/ref_bench.json` edit; preserve it.

Current stage: rebase complete, HEAD 6c8a68c70. Skipped obsolete GT-only f586cb53b; retained current-main GT as one consistent measurement (883 rows, zero stale/missing). Resolved engine/viewer conflicts preserving both changes, including BoundaryFan extra-turn depth restoration and meter-only census accounting. First post-rebase Release build is running, log primary build/scratch/cyclone-finish/rebase-build.log. Unit/scenarios/smoke pending before queued picks. Build only via build.sh; all recreatable output under primary build/scratch/cyclone-finish. Prior machine's scratch manifests/binaries are absent: reconstruct from tracked ledgers, rerun paired baselines on current integrated source.

Independent reviews found extra required fixes:

- KEEP_AMEND reads library size/land count but CantripOrderKey omits them. Add both inputs with a same-board/different-library unit and rank-cache VERIFY run.
- Two-Eldrazi JUNK fix 0b8c27fbb appears right; add explicit k=1 and k=0 JUNK coverage (existing test is k=3; other k=0 test covers SETUP).
- Q61 SeedPaySplits incomplete for partial floating mana; special-source signatures may fold distinct capabilities, Cascade Bluffs colourless face omitted; all-generic guards incomplete. Current-main PayContextDigest must include new seed-payment holds. Payment review agent drafting corrective patch in build/scratch/cyclone-finish/q61.
- Q64 REV_CYCLERS rank 5 is before draws; proposed after-draw/before-Song order requires separate rank ~55 arm.
- Viewer branch has an unfixed wide-board valve regression: EDF claude_s5_gi4 T4 loses existing Mariposa group. Exclude added generic-cycle groups before old valve, append within caps. menu-prefix checker currently exits zero even on failure; repair before using as gate.
- Cache design remains interim 512MB/thread; broader floor+shared-overflow work belongs to cache audit, not a claim of completion here.

Queued picks after rebase:

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
