# Gods shared-branch integration handoff (2026-11-10)

The user asked to finish the Gods changes and push them to `phase-1-2-deck-analyzer`.
The integration checkout is `/workspaces/MagicDeckTester/.claude/worktrees/gods-integration-20261110`, branch `gods-integration-20261110`. It started at shared commit `9df2e007e` and has an **uncommitted merge** of `origin/gods-leaf-freeze-20261110` (`e6a16041b`). Merge conflicts have been resolved and staged with `git add -A`; check `git status` before committing. The primary checkout is dirty with unrelated agent edits; do not reset, clean, or commit them. The artifact queue was paused at the user's request and should remain paused.

The four feature commits contain the Gods play engine, ten references, adopted mulligan profile, value leaf, deck artifacts, regression rows, and analysis. All conflicts were resolved in the integration worktree, including shared turn-boundary effects alongside Gods dredge/Klothys/Keranos. A follow-up fix in `BoundaryFan::Next()` consumes the queued dredge base branch when advancing a different boundary axis; without it, Path and Shadowborn tests saw duplicate choices. `test_pirates_provider.cpp` now pins Gods routing. `test_path_to_exile.cpp` now checks the legal optional decline-search branch added by the feature. `test/viewer_protocol_check.py` handles sparse discard indices and old Magus upkeep references.

Verification on the corrected merge:

- `./build.sh` passed (`logs/gods_integration_rebuild3.log`; later `rebuild5.log` also passed after the Path test update).
- Full unit suite passed: 2,765 cases, 3,581,756 assertions (`logs/gods_integration_all_unit3.log`).
- Strict full viewer replay passed: 807 refs, 0 drift/gaps/divergences; both suboptimal references beaten (`logs/gods_integration_regression2.log`). Focused Gods replay also passed 10 refs.
- Corrected Gods regression batch: d0 8.7520 exact; d3 5.3800 same average with digest changes; standard play d5 5.2000 vs feature baseline 5.1800 (seed 2016 / game 14 T6→T7); 2HG play d5 5.2200 vs 5.2000 (seed 2041 / game 39 T5→T6). `bash test/classify_turn_later.sh regression` reran both at 4×/16× and both recovered their original turn at 4× (`logs/gods_integration_classify.log`). The corrected boundary fix left both d5 results unchanged, so the classification remains applicable. Standard game 14 recovers at budget 20 when `MTG_INWIN_CACHE=0`; 2HG game 39 has a separate budget/ranking cause. The full per-game audit identified these two slower games and eight equal-turn changed plays. No lost line was found.
- Corrected smoke standard: d0 8.7860 exact, d3 5.3600 exact, play d5 5.2200 same average but two equal-turn play digest changes (games 44 and 48). Corrected 2HG smoke d5 5.4000 exact. Logs `logs/gods_integration_smoke2.log`, `logs/gods_integration_smoke2_2hg.log`.

**Remaining before push:**

1. The measured regression baseline was accepted with a provenance note; standard smoke was rerun and its same-average play digest accepted. See `logs/gods_integration_regression_accept.log` and `logs/gods_integration_smoke_accept.log`. The known regression delta is +0.02 average turns per play row, +2 turn units across the two 50-game blocks; both slower lines recovered at 4× budget.
2. The integration result was appended to `docs/design/analysis-gods.md`. Stage all changes, run `git diff --cached --check`, and commit the merge. The remote shared branch advanced one doc-only commit, `587c6ed13`, since the integration base; fetch and merge it. Fetch again immediately before push, then push without force: `git push origin HEAD:refs/heads/phase-1-2-deck-analyzer`.
3. `python3 scripts/check_references_safe.py` was launched in session 33344; confirm its result before session close, per `CLAUDE.md`. Keep the artifact queue paused.

The uncommitted integration worktree is the sole place where the conflict resolution lives until the merge commit is made. Do not discard it.
