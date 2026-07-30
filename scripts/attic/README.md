# scripts/attic/ — retired experiment drivers

One-off scripts from finished experiments, moved here (2026-07-30, backlog item A4) so that
`ls scripts/` shows only the ~13 tools that are part of a live workflow (the analyze-deck /
claude-play skills, the harness, `src/` references, and their script-to-script dependencies).

Nothing runnable references these: each was either cited only by the `docs/design/*.md`
write-up of an experiment whose outcome is already recorded, or referenced by nothing at all.
When a design doc cites `scripts/<name>`, the file now lives at `scripts/attic/<name>`
(same name, and `git log --follow` preserves its history).

Do not build new work on these — several predate the per-deck folder move, the numbering/CRN
shuffle fix, and the multi-config build tree, so their paths and flag conventions are stale.
If an old experiment needs re-running, prefer re-deriving the command from its design doc
against the current harness.
