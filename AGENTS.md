# Repository instructions

Read `CLAUDE.md` in the repository root before working in this repository and follow
its repository instructions. It is the shared, authoritative instruction file for
both Claude Code and Codex.

The authoritative skill documents are `.claude/skills/*.md`. Read the applicable
source document before following a workflow. Codex discovery entrypoints under
`.agents/skills/` point to those files; they do not duplicate the workflows.
After adding, renaming, or updating a skill's introduction, run
`python3 scripts/sync_codex_skills.py` and include the entrypoint changes with it.

Claude-specific tool names describe capabilities: use the equivalent available
Codex tools. The external-play correctness sweep (the historically named
`--claude-play` protocol) is driven by agents of the SAME type as the parent
(USER 2026-11-11: "whatever the parent agent is should spawn the same type of
agents to do the play sweep"): a Codex session spawns Codex agents and records
the sweep as Codex-play; a Claude session spawns Claude agents. The user switches
between the two when one runs low on budget. An explicit user request for a
specific model still requires that model; do not silently substitute another.
