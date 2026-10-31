# Landing905 normal smoke/regression audit

Prepared against code `e63afece6c8a7d0d0081ccefd6a0dda2475b69da`, source
`c2c6d4a03a5c1df9cf72c55bd29940aacff41efc`, shared-branch base
`2c76c854e7ce593f129c0aced8f6854339dd00c2`. PGO build64102 is DONE0; root owns all engine launches. No engine/build was run to prepare this workflow.

The existing ground truth is internally consistent: `python3 test/check_gt_logs.py`
reports912consistent/0stale/0missing. Preserve the upstream accepted EDF/EDF2HG,
Landfall d5s3003 and MidrangeBurn changes. Historical Cyclone smoke differences
are investigation context, not a whitelist for accepting a new result.

## Run and collect

After root confirms final PGO is fresh and other heavy processes are closed, use
the ordinary harness unchanged, serial modes,32workers. No `--accept` yet.

```bash
cd /workspaces/MagicDeckTester/.claude/worktrees/cyclone-landing905
THREADS=32 bash test/regression.sh --smoke
python3 logs/cyclone-completion/prepared/audit_normal_regression.py \
  --worktree /workspaces/MagicDeckTester/.claude/worktrees/cyclone-landing905 \
  --mode smoke --baseline-ref 2c76c854e7ce593f129c0aced8f6854339dd00c2 \
  --expected-source c2c6d4a03a5c1df9cf72c55bd29940aacff41efc \
  --output /workspaces/MagicDeckTester/build/scratch/cyclone-finish/landing905-review/normal-audit/smoke

THREADS=32 bash test/regression.sh --regression
python3 logs/cyclone-completion/prepared/audit_normal_regression.py \
  --worktree /workspaces/MagicDeckTester/.claude/worktrees/cyclone-landing905 \
  --mode regression --baseline-ref 2c76c854e7ce593f129c0aced8f6854339dd00c2 \
  --expected-source c2c6d4a03a5c1df9cf72c55bd29940aacff41efc \
  --output /workspaces/MagicDeckTester/build/scratch/cyclone-finish/landing905-review/normal-audit/regression
```

An ordinary harness exit1 can mean a fingerprint difference requiring review;
collect its completed output before deciding. The collector fails closed without
the harness final Result line, any manifest summary, any expected GI, a matching
source pin, consistent aggregate/per-game folds, or the exact immutable old GT.
Its output path must be new. Changed raw `.wins` files, both-side hashes, original
manifest/jobs, batch output, diagnostics and binary hash are retained; baseline
is the explicit commit, never a moving `HEAD~1`. Do not rerun a mode over its
`test/logs/<mode>` outputs before preserving its artifacts and binary snapshot.

Normal harness behavior to retain:

- A snapshot `test/logs/<mode>/mtg.run` is the actual engine. Its `.meta`/`.diff`
  records provenance. Confirm binary SHA and PGO freshness against root's receipt.
- Every mode runs the scenario gate first. Regression additionally runs full
  strict reference replay and suboptimal references' require-beat gate. Do not
  suppress those gates with `VPC_SKIP`.
- Searched bottoming is ON, derived from depth; d0 stays the normal greedy control.
  Play rows omit explicit depth so each shipped `value_play` selects it; shallow
  rows use `ignore_play_profile`. Original jobs preserve this distinction.
- `audit_changed_games.py`, which the harness invokes at the end, **does run
  engine repros** through `explain_game.py`, capped at20. Do not invoke it as a
  supposedly read-only tool during PGO. The new collector never starts engines.

## Review obligations

`audit.json` lists **all** outcome/digest changes, split searched/d0, including
same-score play changes. `searched-slower.json` is the efficient causal-review
roster; each row binds original job, actual seed/globalGI and both raw digests.
Review every searched slower game. Inspect kept hand/bottoming, first physical
choice and draws before attributing a score change to pruning/payment/budget.
Review same-score changes for legal alternate lines, and sample the d0 changes.
Preserve raw digests; annotation-only findings do not normalize the engine gate.

`d8-recovery.UNSELECTED.json` contains proposals only, not authorization to launch.
It keeps the original policy/life/heads/assets, pins the exact actual seed and
globalGI, sets depth8/budget0/ignore_play_profile, and removes the old backstop's
2M-unit abandonment ceiling. Root selects a pooled follow-up only for unresolved
searched slower cases. Higher-budget recovery is evidence, not sole causality.
No full13k repetition follows mechanically from a normal-suite mismatch.

For an individual source-grounded comparison, root may later use:

```bash
python3 test/explain_game.py MODE KEY GI \
  --old-bin EXACT_BASELINE_BINARY --new-bin test/logs/MODE/mtg.run
```

That command runs engines. An old V5/C45/547 binary does **not** establish a
2c76baseline just because a particular key once matched; prove an exact old-side
reproduction or label baseline-trace attribution unresolved. Never accept merely
to manufacture a baseline snapshot. Final acceptance is through the normal
harness only after per-game review; user-owned option choices remain user-owned.

Pure helper checks (no engines):

```bash
python3 logs/cyclone-completion/prepared/test_audit_normal_regression.py
python3 test/check_gt_logs.py
```
