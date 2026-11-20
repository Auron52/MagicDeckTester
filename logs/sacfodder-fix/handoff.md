# Handoff: MTG_SAC_FODDER_MENU fix-up 3 (agent 3; stopped at ~306K on the coordinator's instruction)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/sacfodder-fix`, branch `wip/sacfodder-fix`.
Commits on top of b9d05f514 (previous handoff):
- `7fac7389e` code + tests: `MTG_SAC_FODDER_INERT_PRUNE` (exact dominance prune, card-data audited) and
  `MTG_SAC_FODDER_WAVE` (credited plans admitted in MTG_BP_DEV_SEQ's second pass only), both heurarm slots, DEFAULT
  ON under the still default-OFF lever. TEMP `MTG_SFM_DUMP` removed. Units 3720/3720 pass (build/scratch/claude_sacfodder_fix3/units2.log).
- the commit holding this file: docs/design/sac-fodder-menu-2026-11-18.md "Fix-up 3" section (the residue write-up).
Not pushed yet at the time of writing (wip branch may be pushed anytime; NEVER push phase-1-2-deck-analyzer).

## What was built
- CardDatabase.h/.cpp: per-card `fodder_inert`, `fodder_inert_outlet`, `fodder_board_audited`, `fodder_deck_audited`
  from raw cards.json key whitelists (pattern of walk_bound_audited).
- TurnSolver.cpp: `FodderPipsCover` / `FodderInertCast` / `FodderPruneAudited` beside `SacFodderMenuCtx`;
  `BuildSacFodderMenuCtx` zeroes dominated fresh bodies (single-colour outlet only) and marks `own_dominated` for a
  plan-cast inert outlet; `SacFodderMenuSubsetBodies` returns 0 when the outlet's own body is the only creditable one.
  EnumeratePlansCore, after the fodder walk gate: under `g_devseq_suppress > 0 && g_rollout_nest == 0 &&
  g_bp_enum_depth == 0` a credited plan is dropped and recorded as a dev-seq latent occurrence.
- ManaPayment.cpp/.h: `SacFodderInertPruneOn()`, `SacFodderWaveOn()`. HeuristicArm.h: two slots.
- test_sac_fodder_menu.cpp: audit classification test, dominance test ({Skirk, Lackey, Bolt} off two Mountains gone,
  back with Krenko on the board); two older tests pinned to INERT_PRUNE=0 (their fixtures are exactly dominated shapes).

## Measurements (dev seeds, play settings, x5000/deck vs lever off; build/scratch/claude_sacfodder_fix3/ab1,
compare with `python3 tools/cmp2.py ab1:off ab1:<arm>` from that dir)
| arm | Goblins f/s net | units | Fungus f/s net | units |
|---|---|---|---|---|
| on (prune+wave) | 1/3 +2 | x1.157 (cpu x1.201) | 1/2 +1 | x1.046 (cpu x1.056) |
| onnp (wave only) | 1/3 +2 | x1.162 | 1/2 +1 | x1.046 |
| onnw (prune only) | 1/4 +3 | x1.092 | 0/3 +3 | x1.037 |
Slower (on): Goblins 620594 4->5, 622286 3->4, 622411 3->4; Fungus 630865 4->5, 633295 4->5.
Faster: Goblins 621953 4->3; Fungus 633599 4->3.
=> BOTH decks still net > 0 after steps 1-2: per the brief (step 4) the line STOPS here; parent parks it and raises
it with the USER. No further variants.

Residue diagnosis (single-game repros, build/scratch/claude_sacfodder_fix3/rc1/play, arms off/on/nw, on with
MTG_DEV_SEQ_STATS=1): in every slower game `pass2_better=0` -- the second pass never took a line -- and only 2-4
decisions per game go through FullSearchLine's dev-seq two passes. So the losses come from lever effects the wave
does not stage: decisions not routed through the two-pass root; beyond-horizon playouts and breakpoint enumerations
(g_rollout_nest>0 / g_bp_enum_depth>0) still see credited plans (leaf values move); the apply's fodder order walk
(BuildPoSacFodder). 622286 diverges at T1 m1: off casts Goblin Lackey, on casts Aether Vial. The wave also adds units
(pass 2 spends what pass 1 leaves). The prune is sound but moves nothing measurable once the wave is on.

d8 b0 (`--depth 8 --budget-ms 0`, lever off vs on): Fungus 630865 4/4, 633295 4/4 -> both recover
(rc1/d8/results.txt). Goblins: first attempt failed (needs --ignore-play-profile); re-run is hold3 (below).

## Running job
- hold3: PID in build/scratch/claude_sacfodder_fix3/hold3.pid (the flock wrapper; it runs hold3.sh under
  build/scratch/claude_box.lock). DONE marker: build/scratch/claude_sacfodder_fix3/HOLD3.DONE (written by hold3.sh
  as its last line). Output: build/scratch/claude_sacfodder_fix3/rc1/d8g/results.txt plus per-run
  rc1/d8g/d<gi>_<arm>.out/.err.
- How to read it: results.txt has lines `d<gi> <arm> <avg turns>` for gi 620594, 622286, 622411 and arms off/on.
  A slower game RECOVERS when on <= off at d8 b0. A blank value means the run failed: read the .err (look for
  "Error:", RSS cap aborts) and fix the flags. Bar: every slower game must recover. This is information for the
  write-up only; the dev-seed net already fails the bar.

## Remaining, in order
1. When HOLD3.DONE appears, read rc1/d8g/results.txt as above and add the Goblins d8 b0 line to the "Fix-up 3"
   section of docs/design/sac-fodder-menu-2026-11-18.md. Commit to wip/sacfodder-fix and push that branch.
2. Report to the USER (through the parent): fix-up 3 does not clear the bar (numbers above); the line is parked.
   The open option (USER call, not to be tried without a ruling) is to stage the remaining lever effects too
   (playouts, bp enumerations, non-two-pass decision routes). Note its cost: the wave alone already raises units
   (Goblins x1.092 -> x1.157).
3. Not done (only if the USER revives the line): held-out A/B on fresh seeds, smoke+regression, the Fungus
   mana-clean gate counts off vs on (previous handoff step 6).

## USER rulings (verbatim, as given in the brief)
- "For 3 I would like us to fix this up. I can't think of a reason why this would reduce quality significantly. If
  anything it should be more quality at a slightly higher CPU cost."
- Never justify CPU cost by the suite's 3x headroom; cut the cost.
- Adoption bar: every slower game must recover at --depth 8 --budget-ms 0, and every deck net <= 0 vs lever OFF on a
  large held-out sample ("+1 is not neutral. 0 is neutral.").
- A budget may re-price work, never make a line unfindable at unlimited budget; a budget-staged later wave is the
  sanctioned shape.
- Brief step 4: "If after steps 1-2 the dev seeds are still net > 0 on either deck, STOP: write up the residue and
  report; the parent will park the line and raise it with the user. Do not try further variants beyond 1-2."

## Machine rules
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock for engine runs and builds; MTG_RSS_CAP_GB=40;
./build.sh only, ~200 s after edits; no timeout; waiters on PID (kill -0) or DONE markers; scratch under
build/scratch/claude_sacfodder_fix3/; never touch references/.
