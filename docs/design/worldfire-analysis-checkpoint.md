# Worldfire analysis -- checkpoint (2026-11-21 ~20:15 UTC)

Owner: the session analyzing `decks/Worldfire/Worldfire.cod` (USER asked: "Please analyze the
Worldfire deck"). Worktree `/workspaces/MagicDeckTester/.claude/worktrees/worldfire`, branch
`wip/worldfire` (rebased on origin/phase-1-2-deck-analyzer bd5a2c93f). Ledger:
`docs/design/analysis-Worldfire.md`. Free-turn design (all USER rulings verbatim):
`docs/design/opponent-lockdown-free-turns.md`.

## Done
- Stage 1 coverage: 6 missing (Worldfire, Keldon Halberdier, Mystical Tutor, Personal Tutor,
  Overmaster, Mistrise Village); rest full.
- Stage 2 by integrator #1 (handed off at ~340K): WIP commit 6634f2bb0 -- six cards.json entries,
  `ResolveExileAllAndSetLife` (executor + rollout), off-suspend creature haste, own_death_live armed by
  Worldfire (fetch at life 1 is death in both worlds), `WorldfireProvider` (TutorSearchWidth 32,
  MTG_WORLDFIRE_TUTOR_WIDTH=0 -> 6), test/unit/test_worldfire.cpp, viewer INERT_PARAMS. Handoff:
  `logs/worldfire/integrator-handoff.md` (gitignored, on disk).
- Parent: clean `./build.sh` at 742c81b11 (9p "Cannot allocate memory" needed retries).
- 2-hourly session-only status cron cf1c0324 (`7 */2 * * *`).

- Stage 2 gates (integrator #2, b4a74b519): all pass; smoke byte-identical (235, play-changed=0);
  coverage 22/22 full; costs + fields clean; rules review clean. Sanity d5 b20 40 games: 39/40 won,
  avg 6.0 over wins; Worldfire cast 28/40, 23 won next turn; 2 casts with nothing suspended (gi28 T3
  blind cast -> Stage 5). Pre-existing: test_shadowborn_int9 Thrumming Stone fails at bd5a2c93f;
  unit_par.sh miscounts shards (exit 3 when green); provider_audit red on Landfall/Selesnya; suspend
  writes no JSON-log action.

- Windows CI 44900654317 (b4a74b519): success.
- USER rulings 2026-11-21 (verbatim in analysis-Worldfire.md): free-turn depth = no ply cost; the four
  deferrals APPROVED; INERT_PARAMS APPROVED; the tutors prune to missing combo pieces; Rift Bolt held for
  the combo.

## Prunes (committed 296d53c8d, built, test_worldfire 28/28)
- Tutor combo-piece prune + Rift Bolt hold + OffersCastNow/OffersSuspend hooks (MTG_WORLDFIRE_TUTOR_PIECES /
  MTG_WORLDFIRE_HOLD_BOLT, =0 off). Fix found by the unit test: a pending Halberdier waives the +1 Bolt-suspend
  mana in mana_short.
- Stage 4 baseline profile (pre-prune) in ledger 38f30b9e3; profile json still UNTRACKED -> re-run after the A/B.

## Prune A/B DONE (ledger 384880ad7, 625a8f818)
- ON beats OFF on both blocks (9100001: 5.734 vs 5.891; held-out 9200001: 5.760 vs 5.969, 19 better / 3 worse;
  slow games 3 vs 11). Proposed Bolt race-release (MTG_WORLDFIRE_BOLT_RACE) measured net worse -> OFF.
- Smoke byte-identical with the prunes (235, play-changed=0).

## Done since (2026-11-21 ~16:00 UTC)
- Stage 4 re-run WITH prunes: profile committed b67de3cf3 (COST_NEUTRAL, discard STATUS_QUO_OK, policy PASS).
  provider_audit: Worldfire clean (pre-existing reds only: Landfall, Selesnya).
- Suite rows (all three tiers + worldfire2hg canary + backstop 30401/30521, screened: b0 is DEGENERATE --
  most games hit the 2M cap) f01bff837; cost gate 2.06x Hinata PASS (suite_cost.json 70fc1c321).
- Land-play fix (agent #2) DONE: wip/opp-landplay 53eb009bc + b8d66c163 (scenarios name their opponent list,
  default White Knights). Cherry-picked here as 4668b69b7, 355e5282f. Its tiers: smoke 176/235, regression
  277/375 keys move; 32 better / 213 worse games, 199 of the worse vs life-gain lists (correct rules).

## Done since (~17:00 UTC)
- GT accepted smoke+regression for the land-play fix + Worldfire NEW keys: c81c9fd51 (1369 keys, consistent).
- Claude-play sweep DONE (16 games, ledger f2577e4a5): 0 legality flags; FOUND + FIXED the Irencrag Feat
  restrictor counting a SUSPEND as a cast (008f1147d, scenario worldfire_feat_suspend_bolt_kill; scratch
  worktree .claude/worktrees/wf-featfix, branch wip/wf-featfix -- remove later via safe_worktree_remove.sh).
  Deferred items: docs/design/worldfire-sweep-deferred.md.

## LANDED 42d819bc0 (2026-11-21 ~18:00 UTC)
- Rebased onto origin, GT re-accepted smoke + regression (1409 keys, check_gt_logs consistent), pushed via
  push_queue. Windows CI 44935787667: success. Feat-fix slower game gi52 recovers at b200 (40bd2d0c5);
  verify_deck --write-ledger GATE PASS (7d2067a9c). Those two docs commits ride along with the next landing.

## Running (~20:15 UTC)
- stage5b (heavy_run, build/scratch/worldfire/stage5b.sh): 5c2 leaf_tiebreak_check -> logs/worldfire/
  leaf_tiebreak.log (rc= line), then 5b depth sweep d3b10 / d5b20 / d5b200, 96 games, seed 9400001 ->
  build/scratch/worldfire/s5b/*.out + DONE.
- gi4 (seed 9300005) d8 b0 replay, PID 3629867 -> build/scratch/worldfire/gi4/{d8b0.out,DONE}.
- Free-turn chain1 (freeturn worktree, build/scratch/freeturn/chain1.sh, PID 3724929): build, units,
  scenarios, flags-OFF smoke -> build/scratch/freeturn/chain1.{out,DONE}. Agent #2 has finished; the parent
  owns the measurement (handoff logs/freeturn/handoff.md step 3; off arm = build/scratch/freeturn/offarm_parent).

## Next, in order
1. Read 5c2 (KEEP / NEUTRAL / NO SIGN -> re-run at suggested --blocks / OPT OUT -> GradesNoWinLeaf false) and
   5b monotonicity (d5b20 vs d3b10; b200 recovery = budget starvation, note in 5c); ledger.
2. gi4 d8 b0 result -> ledger (misplay candidate: Claude T5 vs search T6).
3. Free-turn: after chain1, MTG_FREE_TURNS=1 regression + Worldfire direct, report (a)-(d), doc Implementation
   section, push wip/freeturn (flags OFF; adoption is the USER's).
4. Stage 6 report; remove scratch worktrees wf-featfix + opp-landplay-base (check_references_safe first);
   CronDelete cf1c0324.

## Open USER questions (defaults taken)
- Search depth for free turns: default = no ply cost (match Extra Turns; its Q9 was never USER-answered).
- Integrator #1's: four PROVISIONAL deferrals (Overmaster / Mistrise uncounterable riders inert,
  Halberdier "may cast" -> cast, Worldfire LTB triggers not fired); INERT_PARAMS for the two Worldfire
  params; the TutorSearchWidth 32 hook (lossless, default ON).
