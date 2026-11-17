# Sea Creatures analysis — resume checkpoint

Short resume note for the in-flight Sea Creatures `analyze-deck` run (Claude orchestrator session,
2026-11-16). The full state is the ledger `docs/design/analysis-Sea Creatures.md`.

## Where things live

* Worktree `/workspaces/MagicDeckTester/.claude/worktrees/sea-creatures`, branch
  `sea-creatures-analysis` (local commits only; NOT pushed; base `8cd0bbf23` (rebased 2026-11-16 from f92bb3b7c)). The primary checkout's
  `decks/Sea Creatures/` is untracked there (the user's drop); the worktree copy is committed.
* Scryfall JSON: `/workspaces/MagicDeckTester/build/scratch/sea/scry/`.
* Research drafts (gitignored): `logs/sea-research/*.md` in the worktree.

## Stage status

* Stage 1 coverage: DONE.
* Stage 2 research fan-out: DONE (6 drafts: mana, combat, sagas, bounce, sin, gorger; see ledger).
* Integration plan (serial, one Opus agent each, brief `logs/sea-research/INTEGRATOR-BRIEF.md`):
  I1 mana + SeaCreaturesProvider DONE (4eb5a3e10 core, 4cf605b4f Misty viewer-only hold, 8682648b6
  ledger); I2 combat IN FLIGHT (2026-11-17) -> I2 combat (Koma, Kraken, Serpent, Lorthos,
  Wave + shared bounce helper + Wave-skip prune) -> I3 sagas (Summon uses the bounce helper) -> I4
  bounce (Hullbreaker, Brinelin + decline prunes) -> I5 Sin -> I6 Gorger (exile-all >=6 lands prune).
* I1 movers: none from Misty (fetch_hold_human_only keeps autonomous on the old route). Creature Giving
  moves on ANY CardParams layout change and with batch job mix (pre-existing) -- separate investigation
  agent in worktree `.claude/worktrees/cg-jobmix` (detached at 8cd0bbf23, branch cg-jobmix-fix when it
  commits), findings to docs/design/creature-giving-jobmix-nondeterminism.md there.

## Next steps

1. Collect research drafts; record them in the ledger.
2. Serial integration, one agent per change set (simple lands/rock first, then Sagas, then triggers).
3. Stage 3 coverage re-run, Stage 4 profile + provider, 4-bis suite rows, Stage 5 verification, Stage 6.
