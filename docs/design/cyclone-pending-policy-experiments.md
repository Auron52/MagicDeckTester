# Unpredictable Cyclone: pending policy experiments

Status: **stopped by user priority, not completed or rejected** (2026-10-31).

The user prioritized finishing integration on `phase-1-2-deck-analyzer`, followed by weekend mulligan-table and value-leaf generation. The agent-chosen exploratory follow-up pool was stopped to make that work possible. These optional policy experiments do not block landing the existing defaults with their own current-source validation. This document makes no policy adoption or rejection decision and does not authorize a push to `main`.

No further exploratory engine process, reversed-order isolation run, or result-alias installation was launched after the stop. The engine lease was released. The unfinished work below remains pending; its interruption is neither a failed quality result nor evidence against the user's proposed policies.

## Frozen experiment and actual stop

This evidence belongs to the old **Current8b** source and apparatus. It must not be relabeled as a measurement of Landing138 or any later integration.

| Item | Frozen identity |
| --- | --- |
| Source tree (`HEAD:src`) | `8b0bc5ea046ce748c456f7e4aceef4ea11807aa5` |
| PGO binary SHA-256 | `2371bc0e0adc3daf2fe65f2fc42139146aaae11e0d9627cf2f1e75d276d630b6` |
| Selected manifest SHA-256 | `712b5dcdcb563af3e2eb7ec9d72dfdb29f3a466a2ccc09c18de3a360ee1c7498` |
| Sealed launch-plan SHA-256 | `7f646dc9faf022dd9eb634a9de83bfdc6e1817fac8715a09ca0914f5d249d4af` |
| Historical worker count | 24; recorded historical apparatus, not the rule for new normal launches |
| Actual stop | `2026-10-31T10:07:36.636083+00:00` |
| Engine / wrapper exit | Engine `-2` (SIGINT); wrapper `254` |
| Recorded process state | `STOPPED`; no success `DONE` exists |

The frozen checkout is `/workspaces/MagicDeckTester/.claude/worktrees/cyclone-current`. The binary is `/workspaces/MagicDeckTester/build/wt/cyclone-current/PGO/mtg`. The sealed plan binds the deck, profile, sidecars, cards data, complete job fields, environment, cleanup, and argv. Preserve those bindings rather than reconstructing an apparently equivalent deck or profile.

Artifact paths below are relative to the durable campaign root:

```text
/workspaces/MagicDeckTester/build/scratch/cyclone-finish/
```

The selected manifest and plan are `v5/selected-followups-final/pooled-followups.json` and `shared-mode0-plan.json`. Actual outputs are under `v5/selected-followups-final/runs/shared-mode0/`: `batch.log`, `batch.err`, `wins/` (both `.wins` and `.units`), `traces/`, `environment.json`, `argv.json`, `status.json`, and `STOPPED`. All were preserved in place.

The pool used explicit process settings `MTG_SEARCH_BLIND_UNKNOWN=0`, `MTG_SEARCH_BLIND_SALT=0`, and `MTG_DIAG_PB_T1_ORDER=0`, in addition to the sealed observation flags. A nonnegative per-job `blind_salt` still activates blinded search despite the process fallback being zero. Ordinary controls omit the active job salt. `MTG_SEARCH_BLIND_UNKNOWN` is a process setting, not a registered per-job heuristic flag. Reversed iso92 uses a separate process with diagnostic mode `2`; it cannot be merged with mode `0` by job JSON alone.

## What actually completed

Exactly **2,439 of 5,986 singleton jobs** have completed stdout rows, matching wins/digests, nonnegative units, and complete physical traces whose identities and results were checked. **3,547 jobs lack completion.** No extra wins, units, or trace files existed without a corresponding completed stdout row at closure.

| Selected work | Completed | Pending / limitation |
| --- | ---: | --- |
| Fresh ordinary diagnostic controls | **1,025 / 1,025** | Complete individual control evidence; zero ordinary FD or NONCONV |
| Conditional symmetric four-salt ensemble | **1,410 / 4,916** | 3,506 missing; no complete ensemble aggregate |
| Selected QP5 deep checks | **4 / 8 games**, two complete pairs | Four games in two pairs missing |
| GI8 CO + REV_ELDRAZI candidate | **0 / 1** | Candidate pending; its existing primary CO control is retained |
| iso92 normal order | **0 / 40 logical jobs** | No normal aliases installed; none of the four intended conditional aliases completed |
| iso92 reversed order | **0 / 40** | Separate mode-2 process never launched |

The 5,986-job pool contains 1,025 ordinary controls, 4,916 conditional jobs, one GI8 candidate, 36 fresh normal-iso jobs, and eight selected deep jobs. Four of normal iso92's 40 logical jobs were exact conditional duplicates, hence 36 additional jobs rather than another 40. The separate reversed 40 were not part of that pool.

The completed ordinary controls are whole-game controls for the original blinded diagnostic records. They preserve arm, profile, depth, budget, actual seed, global game index, life/heads settings, and other policy fields while disabling search blinding. They may mulligan and choose differently from their blinded counterparts: they are **not an identical-plan replay proof**, and do not clear the previously identified site9 `NoReplay` boundary by themselves.

The two closed deep pairs are:

| Case | Original raw BASE / arm | Deep BASE / arm (`d8`, `b0`) | Exact interpretation |
| --- | --- | --- | --- |
| SITU, original d3, GI57 | T6 / T7 | T6 / T6 | Recovers the original raw control turn and closes the paired deep gap |
| SITU, original d3, GI254 | T6 / T7 | T5 / T6 | Recovers the original raw control turn; the paired deep gap remains +1 |

Do not describe GI254 simply as “unrecovered”: those two criteria differ. SITU from d5 GI53 and SEED from d5 GI27 remain pending, both arms of each pair. These selected cases do not prove recovery for the other slower games or justify adoption. The unselected 590-game general deep catalog, 1,068-game budget catalog, 518-game prunes recovery catalog, and whole 124-game QP5 catalog were not launched wholesale.

The complete earlier V5 primary (13,142 games / 1,490 chunks), its quality report, and its completed memory checks remain separate historical evidence under `v5/final-pgo-inputs/`. Their completion does not complete this stopped follow-up ensemble, and neither campaign's cost or outcomes automatically carry to a newer source.

## Diagnostics and the unresolved frozen warning

The stopped pool contains **767 blinded FD records and one blinded NONCONV**, zero ordinary diagnostic records, and zero raw byte/interleaving ambiguities. These are diagnostic counts, not a policy-quality aggregate.

The frozen NONCONV says:

```text
[nonconv] seed=110193 turn=7 verified_win_now=8 EXCEEDS earlier verified_win=7 proven_at_turn=6
```

Its original line includes a hand and plan but omits job, global game index, and salt. The exact candidate universe is 32 positively blinded d5 singleton jobs: eight distinct full policy contexts at GI193, each with salts 1–4. Twenty-four candidate traces closed. Only `pbcyc_d5_gi193_bs3` (`followup_ca62e278222d9bd1`) matches the observable ordered T7 warning hand among those 24. Its actual T7 sequence is Island20 → Myriad28 activation → Brainstorm4 → Ponder33 shuffle → Curator12; the game is unwon at the end of turn 8. Eight BASE/SITU candidate traces are absent after the stop. Attribution therefore remains incomplete; adjacency in an interleaved process log does not supply the missing job identity.

All eight corresponding ordinary primary policy contexts exist and were checked by strict seed/GI and complete policy/apparatus equality. That coverage is useful context, not identical-plan proof or automatic exoneration. Blinded forecast limitations and a concrete execution-fidelity defect must not be conflated.

Subsequent landing work adds root-context capture and `job`, `gi`, and effective `blind_salt` attribution to new NONCONV logs, and makes **every NONCONV require reporter attention**, including blinded warnings. The reporter update has 31 passing tests. Those improvements support better future diagnosis; they do not retroactively relabel, uniquely attribute, or resolve this frozen warning. The historical reporter, raw records, and strict raw-byte guards remain preserved.

## Evidence ledger

All paths in this table are beneath the campaign root above. The before-stop snapshots are retained unchanged; the later harvest binds the final stopped files and every independently closed result.

| Evidence | Path / SHA-256 |
| --- | --- |
| Ordinary 1,025 closure proof, captured before stop | `v5/selected-followups-final/ordinary1025-closed-milestone.json` — `40f5eb2c428bb1e770e53b609968ec2c7b143c3dcfca2d2202e0cdbcc24e461b` |
| Strict ordinary-primary context coverage for positive-salt jobs, before stop | `v5/selected-followups-final/conditional-primary-ordinary-coverage.json` — `8474db81fb9db272be5eed7be515f5f26ae8d862ed976dceee628444bd4158c4` |
| First NONCONV inventory and exact raw bytes, before stop | `v5/selected-followups-final/monitor-captures/20261031T095629.732898Z/inventory.json` — `da1217e40d87ef4c84e9ede40684975e019f78ce5cf30becca4e7261fce5a6d5` |
| Candidate/physical-trace review, before stop | `v5/selected-followups-final/nonconv110193/capture-202610311000015925890000.json` — `eb820e5c7f52596b032d47c7a416abb1b4e80caa273c4b47f9636de7ce1cd32a` |
| Final closed-result and raw-output harvest | `v5/selected-followups-final/USER-PRIORITY-STOP-HARVEST.json` — `f85b2817ece209aec051c1032d4640d55c4ec1997ba53b2a627846bc3195634d` |
| Final raw stdout | `v5/selected-followups-final/runs/shared-mode0/batch.log` — `b09145fa180e57e6783de0a4d8c26a4bf5d74d4947203ae801a5abfbc84b8ffe` |
| Final raw stderr | `v5/selected-followups-final/runs/shared-mode0/batch.err` — `7fdbf0ae71c26ec0b70f4d4809f4db760eab574f4e20f464c79d90740f3567a3` |

The harvest includes full per-result hashes and binds `status.json`/`STOPPED`, argv, and the actual environment. `CLOSURE-CHECKS.json` records engine/watcher closure, lease release, absence of result aliases, and the passing reference-safety check. `USER-PRIORITY-STOP.md` is the short handoff. Detailed deep-case evidence is under `v5/final-prunes/proofs-qp5-selected/deep-review/`; frozen NONCONV captures are under `v5/selected-followups-final/nonconv110193/`.

Logical input mappings are in `v5/selected-followups-final/input-job-aliases.json` and `control-map.json`. The original optional manifests remain under `v5/final-prunes/blind/`, `proofs-qp5-selected/`, and `iso92/`. The GI8 candidate and exact completed primary control alias are under `v5-analysis/cast-root-cause/GI8-normal-tier-attribution.PROPOSED.json` and `GI8-normal-control-alias.json`. The superseded two-arm GI8 proposal is history only: do not rerun its duplicate primary control.

## If this optional research is resumed

Resumption needs a deliberate scheduling decision after the prioritized integration/generation work. Nothing in this document starts a process or makes the pending experiments a landing gate.

1. Verify the frozen source, PGO binary, selected manifest, original process modes, and every apparatus hash against the sealed plan and stop harvest. Keep the original raw directories immutable. If the frozen binary or apparatus is unavailable or changed, report that fact; do not substitute a newer source into the old experiment.
2. Derive **only missing singleton jobs** from the strict completed inventory. Require the completed stdout row, exact GI wins/digest and units, full trace identity/result, and recorded hashes before treating a job as already done. Preserve all other job fields and the actual seed/global-GI pairing. Do not rerun the complete pool or duplicate the 1,025 completed ordinary controls. Retain the existing GI8 primary control alias.
3. Use a fresh output directory and a newly sealed missing-job manifest with explicit provenance back to each original job. Never overwrite stopped files, fabricate a success `DONE`, or mix new-source outcomes into the frozen result set. Current normal-launch concurrency follows the user's maximum-CPU directive (32 on this machine); record the actual count and runtime context, and do not carry historical timing or memory comparisons across that change.
4. Keep mode-0 normal iso92 and mode-2 reversed iso92 distinct. Any later logical alias must verify complete context, closed results, units, full physical traces, and provenance. No alias was installed in the stopped run. An incomplete or zero-eligible isolation sample is not a positive decision proof.
5. Recompute a full ensemble report only once its exact required jobs are complete. Preserve the NONCONV attention and incomplete-attribution caveat until actual evidence resolves them. Whole-game blind comparisons remain conditional experiments, not forced same-state choices or deck-wide effects. Adoption and rejection remain user-owned decisions.
