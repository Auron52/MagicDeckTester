# sb-lakeprof handoff (2026-11-04)

Worktree `.claude/worktrees/sb-lakeprof`, branch `sb-lakeprof` (local, NOT pushed). Scratch:
`/workspaces/MagicDeckTester2/build/scratch/sb-lakeprof/`.

## Done
- 48448ddad: MTG_IDLE_CRACK_FOLD (sound identity fold of land sac-for-mana cracks) + MTG_PROFANE_RESOLVE_FAN
  (Profane fetch decided by BoundaryFan at the upkeep crossing; Plan::suspend_tutor_choice ->
  GameState::scripted_suspend_tutor). Unit tests test/unit/test_shadowborn_lakeprof.cpp.
- 24774b850: fold only on full menus (wall), census refinement, docs/design/sac-for-mana-on-demand.md (USER Lotus
  ruling write-up, deferred), cards.json bracket notes.
- Unit 2567 pass; scenarios 401/401; A/B (ab2/) both arm -0.009 t -2.33, 10 faster 2 slower (gi427, gi842 recover
  at d8 b0); s7 b20 still T6 (units -7.8%).
- Smoke (smoke.final.log): moved rows shadowborn d3/d5/2hg d3 (d5 4.96->4.94, 2 faster), breaching d3/d5/2hg d5
  (same score, play digest). Regression (regr.log): shadowborn d3/d5 s2002 same score, d5 s3003 4.8667->4.8400
  (gi15 8->5, gi43 6->5); breaching x3 same score; UNEXPLAINED: th d5 s2002 (same score), hinata d0 (gi60 -1->8,
  faster), bruna d0 (same score) -- th d5 reproduces with BOTH levers off (offprobe.log), so it is not the levers:
  either a non-lever change (struct/key) or pre-existing GT drift. Base binary being built in
  `.claude/worktrees/sb-lakeprof-base` (detached b379c41b4; log basebuild.log, BRC= marker) to attribute.

## Remaining, in order
1. Finish attribution: run th d5 s2002 / hinata d0 s2002 / bruna d0 s2002 with the BASE binary (regression.sh
   --deck=<d> using MTG_BIN or harness_bin override; or a manifest) and compare digests to GT and to ours.
2. `MTG_BIN=<new binary> python3 scripts/ref_bench.py --deck shadowborn_apostle` (and base binary).
3. Paste build/scratch/sb-lakeprof/ledger_section.md (replace SMOKE_REGR with the smoke/regression deltas) at the end
   of docs/design/analysis-Shadowborn Apostle.md; commit.
4. Remove the scratch base worktree with scripts/safe_worktree_remove.sh after check_references_safe.py.
Do NOT --accept GT. Do NOT push.

## USER rulings (verbatim)
- "For 2 I agree that it should not be sacrificing the land unless we need it. We should sort that out if possible."
- "The card that the tutor fetches should not be staged upfront particularily for Profane Tutor. For other things
  that happen immediately it potentially could be, but I think we have been inconsistent about this, which isn't
  great."
- "If it doesn't find that relatively straightforward line at a decent budget we shold probably dig to see if there
  is a way to reduce the cost so that it can."
- "It should be land -> Dark Ritual -> other spells. Lotus bloom should be sacrificed on demand by the mana system.
  not a direct activation. Land drop should happen main 1 unless we searched it up in main 2."
