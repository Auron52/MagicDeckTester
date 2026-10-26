# Hinata2 keep-table regeneration, PAUSED by the USER 2026-10-26 21:56 UTC

Artifact refresh queue job `hinata2` (worker MagicDeckTester2-76d2cc), `mullgen.sh run decks/Hinata2 fast`
frozen at ae41aaad0 (src tree 0505eadb5), stopped after 45 h: size-7 floor and all 347,116 sub-table batches
done, sub-refine wave 1 (227,301 batches) part-way, size-7 refine not started. Journal header: K=20 R=30
depth=1 budget_ms=3 seed_base=1000000 play_digest 79871cfd49865d33.

This branch is a BACKUP copy. The live state is in the worker's worktree on the MagicDeckTester2 box
(`.claude/worktrees/artifact-worker-MagicDeckTester2-76d2cc`, journal beside the decklist + inflight.json in
`logs/artifact_queue/MagicDeckTester2-76d2cc/`); `python3 scripts/artifact_queue.py start` there resumes it.
To resume elsewhere: check out ae41aaad0, gunzip the journal to
`decks/Hinata2/Hinata2.keepmodel.exhaustive.raw.json.journal`, and run `mullgen.sh run decks/Hinata2 fast`.
