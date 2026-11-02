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
Codex tools. A requirement for Claude itself (such as the Claude-play oracle)
still requires Claude; do not silently substitute another model.
