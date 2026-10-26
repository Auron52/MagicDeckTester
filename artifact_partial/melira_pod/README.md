# Melira Pod keep-table regeneration, PAUSED by the USER 2026-10-26 22:21 UTC

Artifact refresh queue job `melira_pod` (worker artifact-queue-d7d6a5, MagicDeckTester box),
`mullgen.sh run "decks/Melira Pod" fast` frozen at 60f525f17, stopped after 27 h (launched 2026-10-25 19:16 UTC):
all 600,866 sub-table batches done, size-7 floor part-way (6.58M size-7 rollouts, 0 of 1,576,162 cell-sides
frozen), size-7 refine not started. The USER cancelled it on the ETA (~21-25 h more: single rollouts of 6-8 h on
Carrion Feeder / Murderous Redcap / Kitchen Finks hands held the tail). Journal header: K=23 R=30 depth=3
budget_ms=3 max_mull=6 seed_base=1000000 play_digest b2c25a26e00f1eba commit 60f525f17+dirty.

This branch is a BACKUP copy. The live state is in the worker's worktree on this box
(`.claude/worktrees/artifact-queue/.claude/worktrees/artifact-worker-artifact-queue-d7d6a5`, journal beside the
decklist + inflight.json in `logs/artifact_queue/artifact-queue-d7d6a5/`); `python3 scripts/artifact_queue.py start`
there resumes it. To resume elsewhere: check out 60f525f17, gunzip the journal to
`decks/Melira Pod/Melira Pod.keepmodel.exhaustive.raw.json.journal`, and run `mullgen.sh run "decks/Melira Pod" fast`.
