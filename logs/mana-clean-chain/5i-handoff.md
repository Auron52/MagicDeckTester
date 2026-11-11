# 5i handoff (agent 2026-11-11, stopped at the 250K context cap)

## Done
- `bb597608d` (local, NOT pushed): 5i plan-cache determinism fix.
  - Root cause (Gods): `GodsProvider::LoamTargetSearchCandidates` reads the FULL / guard search roots (Magus of
    the Bazaar, root graveyard land count) and `turncensus::t_probe`, none of which was in any plan-cache key;
    the Loam probe context (`g_loam_probe_offer`) was in no key either. Fix: `DecisionProvider::PlanCacheContextKey`
    (Gods returns the facts its hook reads through the shared `ReadGodsLoamCtx`), folded with the probe bit in
    `FoldPhysicalCards`.
  - `decisionwork::PricedExceeded` counts a budget read even with the ceiling disarmed (read guard soundness).
  - `MTG_BP_ENUM_VERIFY` re-derives MUTED and skips read-guard-refused hits (it used to perturb play).
- The local chain is REBASED onto origin (7e88d9fd5 + cd0368564): `dc8e94507 655729123 6ef494c19 674f6400b
  a87166c49 39f6ba18c bb597608d`. Conflicts: HeuristicArm.h (kept both HYDRAS_BUCKET_DISCARD and
  HASTE_HOST_DRAIN, in that order in enum AND names); regression_gt.txt header comment only.
  `check_gt_logs.py` consistent 1101/1101 after rebase; units 2833/2833 pass on the rebased build.
- Smoke on the rebased tree: 194 pass, only gods_smoke_d3_s1001 play-changed (score 5.36 same); the pre-5i
  binary (logs/snapshots/smoke-baseline, a6b6c0c8d) with MTG_PLAN_CACHE_KB=1 gives the new digest 37dbb911
  exactly -> explained. **Smoke ACCEPTED (uncommitted at handoff time, see commit below).**
- Measurements: Gods d3 s2002 + ET d3/d5 s2002 identical digest AND units at default / KB=1 / KB=65536 /
  --threads 32,8,4; MTG_BP_ENUM_VERIFY=3 CONTENT-DIFF 0 of 302,384 hits (was 1,543 Gods).

## Remaining, in order
1. **Integration/push of the whole local chain** (coordinator asked this agent to do it; it was not done):
   `python3 scripts/check_references_safe.py`; `git pull --rebase`; `./build.sh`; regression decision below;
   `python3 test/check_gt_logs.py`; push; background `gh run watch <id> --exit-status` (token via
   `git credential fill`, never printed). Report push hash + CI run id.
2. **Regression GT** (run `build/scratch/claude1110/5i/reg.log`, rebased bb597608d, Release): 299 pass, 3
   play-changed same score: gods d3 s2002 gi39 (explained, cold-cache digest 76f2637c), gods d5 s2002 gi39
   (9ea56514 -- confirm with `logs/snapshots/regression-baseline` + MTG_PLAN_CACHE_KB=1 on that job before
   accepting), extraturns d3 s2002 gi34 -> 639a950d (the ET flake; NOT this fix). Accept ONLY the two Gods rows
   (`--deck=gods` after a gods-only run, or a full run where ET happens to match GT); do not accept the ET row.
3. **ET pooled flake (still open):** extraturns d3 s2002 gi34 plays 639a950d instead of GT f1d9f642 in some
   full pooled regression runs (5k's run and this one; NOT in the 14:24 run nor in a direct rerun R1 of the
   same manifest/binary). Ruled out: plan pool size (single game invariant at MTG_PLAN_CACHE_KB 2..16384),
   tier-2 (refused=0), bp-cache content (verify 0), FSL (did not bind). Next suspects: thread_local state that
   survives `ClearPerGameCaches` between games of DIFFERENT decks on one worker; cross-thread inputs
   (GameWorkMeter `PublishTo`/`late_limit`). Repro idea: `--threads 1` manifests putting other decks' games
   before ET d3 gi34 (seed 2036, game_index 34, d3 b10, ignore_play_profile), bisect by deck.
4. Held-out A/B on Gods (base = pre-5i binary vs new, fresh seed block, d3 b10 and play depth) -- not run.
   Expect neutral: the fix makes play equal to the cold-cache result.
5. Mark 5i DONE/PARKED in build/scratch/claude1110/QUEUE.md (currently PARTIAL).

## USER rulings given in the brief (verbatim)
None specific to 5i. Brief-common rules apply (no timeout; one engine job at a time, MTG_RSS_CAP_GB=40;
NO-GREEDY; USER decisions never settled by an agent; push only after the hold, which ended 15:58 UTC).

## Running jobs
None. Scratch: `build/scratch/claude1110/5i/` (R1 = direct pooled rerun, reg.log, smoke.log, B_* arms).
