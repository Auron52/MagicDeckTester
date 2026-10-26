# FiveColour keep-table regeneration, PAUSED by the USER 2026-10-26 22:23 UTC

Artifact refresh queue job `fivecolour` (worker artifact-queue-tool-647f24), `mullgen.sh run decks/FiveColour fast`
frozen at 60f525f17 (src tree aba41d81c), started 2026-10-25 14:21 UTC, stopped after ~32 h: size-7 floor and
every sub-table wave done (19.2M sub rollouts, 6 waves converged), size-7 refine part-way -- 2,697,625 of
3,955,796 cells frozen (68.1%), 11.9M size-7 rollouts. Journal header: K=27 R=30 depth=2 budget_ms=3
max_mull=6 seed_base=1000000 play_digest 5d5d032a4043742e bucket_fp 11891638426085921358 (the incumbent's
buckets, re-used via MTG_KEEP_BUCKETS_FROM).

This branch is a BACKUP copy. The live state is on the primary box: the worker's frozen worktree
`.claude/worktrees/artifact-queue-tool/.claude/worktrees/artifact-worker-artifact-queue-tool-647f24` (journal
beside the decklist, plus a `.PAUSE-copy-20261026`) and `inflight.json` in
`.claude/worktrees/artifact-queue-tool/logs/artifact_queue/artifact-queue-tool-647f24/`. The claim is `paused`,
so no other worker takes it; `python3 scripts/artifact_queue.py start` in that tool checkout resumes it.

To resume elsewhere: check out 60f525f17; gunzip the incumbent `decks/FiveColour/FiveColour.keepmodel.exhaustive.
profile.json.gz` to the plain `.profile.json` (mullgen tests the plain name for an incumbent; do NOT unpack the
raw -- that path is the generator's floor checkpoint); gunzip this journal to
`decks/FiveColour/FiveColour.keepmodel.exhaustive.raw.json.journal`; run `bash scripts/mullgen.sh run
decks/FiveColour fast`. Check the journal header against the run's settings before launching (CLAUDE.md "A
GENERATION JOURNAL IS ...").
