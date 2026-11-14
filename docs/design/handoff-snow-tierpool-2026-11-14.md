# Handoff 2026-11-14: Snow continuation-pay package + tier pooling harness

This is the checkpoint for whichever agent (Claude or Codex) continues. Verify running processes before acting.

## 1. Snow package (branch `snow-contpay`, worktree `.claude/worktrees/snow-contpay`, LOCAL, not pushed)

- **Branch state:** rebased onto origin `bca756347` at 10:2x UTC. It carries 10 commits ending at `6ad20e507`. The pre-rebase backup is branch `backup/snow-contpay-pre-rebase`.
- **What it contains:**
  - `MTG_REPEAT_EXTRAS_RESERVE`: Owl K-repeat extras keep what later activations owe.
  - `MTG_EXEC_SITE8_CONT_INLINE`: the executor applies a site-8 continuation's activations inline.
  - `MTG_D0_SNOW_PUMP_CREDIT`: the d0 lethal check credits Rimefeather Owl's ice counters. It is default ON, and the USER may veto it.
  - `MTG_LINE_PAY_AUDIT` audit classes.
- **Evidence:**
  - Held-out A/B: every deck and axis is net <= 0. Ledger `docs/design/snow-continuation-pay.md` section 5.1; data in `build/scratch/snowcp/ab3/`.
  - Built, Release. Unit tests pass (3088/3088).
- **RUNNING:** `build/scratch/snowcp/run_tiers.sh` (PID 634677, detached), which runs the old harness's `--smoke` and then `--regression` in that worktree.
  - Logs: `build/scratch/snowcp/tier_{smoke,regression}.log`.
  - DONE marker: `build/scratch/snowcp/tiers.DONE` (one `mode rc= wall=` line per tier). The ETA is about 11:30 UTC.
- **Next steps:**
  1. Read both logs' per-game audit. Expected movement is in the Snow rows only. Any other deck moving needs a look first; a searched slower game must recover at `--depth 8 --budget-ms 0`.
  2. Accept from the inspected runs in the worktree: `bash test/regression.sh --smoke --accept`, then `bash test/regression.sh --accept`. These promote the existing runs; they do not re-run.
  3. Run `python3 test/check_gt_logs.py` and `python3 scripts/check_references_safe.py`.
  4. Commit the GT, fetch, and rebase if origin moved. If src or GT replays, rebuild and re-run the tiers.
  5. Push the shared branch `phase-1-2-deck-analyzer`. This session last pushed at 06:06 UTC, so the window has been open since 10:06.
  6. Start a CI watch in the background: `gh run watch <id> --interval 60 --exit-status`.
- **After landing:** re-test `MTG_RESCUE_TAP_SOURCE` with the re-weighted manifest in `build/scratch/snowcp/rts/` (rebuild the `mtg.ab` snapshot first). Adoption is the USER's call.

## 2. Tier pooling harness (branch `tier-pool`, worktree `.claude/worktrees/tier-pool`, LOCAL)

- **Commits:**
  - `f9dc6404d`: WIP. Jobserver tokens, a persistent `--interactive` replay child per reference, xargs-parallel scenarios, VPC beside the batch, and `--both` combined smoke+regression.
  - `f0457802e`: VPC stderr via pread.
  - `36d69fa43`: scenarios beside the batch, longest first.
  - `556cd8f43`: skill stage table and backstop comment.
- **Verified on one binary:**
  - All 525 `.wins` files are byte-identical, and units match (493,166,963).
  - All 837 reference verdicts are identical, stateless vs interactive.
  - Scenarios are identical (530/530).
  - Time outside the batch went from about 1,050 s to about 80 s for both tiers; paired smoke was about 34% faster.
- **Remaining before landing:**
  - Squash the WIP commits.
  - Exercise the scenario-failure kill, `SIDE_STAGES_SERIAL=1`, the no-mkfifo (Windows) fallback, and a combined run that includes overnight.
  - Count the interactive child's fallbacks to stateless replay.
  - Then push in a later 4-hour window.
- **Full handoff:** `.claude/worktrees/tier-pool/logs/tier-pool/handoff.md`. Measurements are in `build/scratch/tier-pool/`.

## 3. Open USER decisions

1. A veto of `MTG_D0_SNOW_PUMP_CREDIT` (default ON).
2. The Cyclone unbudgeted backstop row is screened stale. Seed 30033 hits the 2M-unit cap, but its comment says all six games finish. Re-screen it?
3. Adopting `MTG_RESCUE_TAP_SOURCE`, after its re-test.
4. Game-count trims, which were deferred until the pooling landed.
5. A queue idea, the USER's own: upgrade relatively cheap decks from `fast` to `complete`, predicting the cost from the fast run's journal. Regenerations happen ONLY via the artifact queue.

## 4. Morph (report done, nothing running)

- **Overnight 2026-11-14:** the slower Morph games have no play defect. They are all different kept hands (fast keep table); the fetch rule loses no line.
- **Owed, suite:** Morph's overnight rows are still the ENTRY set (d5 4x300 is under the 425 floor). Resize them at the next periodic overnight, with the table hidden for the resize accept. Martyr's rows look the same but were not checked.
- **Stale cost entry:** `test/suite_cost.json` still says Morph is 1.72x Hinata (measured before the keep table). It is about 0.46x now, measured in the same run as Hinata. Refill it with `suite_gate.py --measure-all`.
- **Owed:** a Morph claude-play sweep, because play changed after INT-8..17. Morph's value leaf is on HOLD until the weekend (USER).
