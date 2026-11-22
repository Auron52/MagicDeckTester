# Worldfire analysis -- checkpoint (2026-11-21 ~22:30 UTC)

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

## Done since (~22:30 UTC)
- 5c2 tie-break KEEP; 5b monotone; 5c budget-bound at d5 b20 (d5 b200 17 better / 0 worse) -- ledger 2ecb24d7b.
- Scratch worktrees wf-featfix, opp-landplay-base removed (references verified safe).
- Free-turn (wip/freeturn 2d373428c + d35211372, pushed): flags-OFF smoke 243/243 byte-identical, scenarios
  605/605 both flag states; flag-ON regression measured (ledger 4a7ff87ea). Worldfire credited ~1 turn, raw
  rounds slower. Pre-adoption holes: Creature Burn ref claude_s9_gi8 drifts under the flag; steal_tap_count lock
  trait (Vampires 3x units); passive opponent's oldest-first cleanup hands Burn / Extra Turns / RW Burn free
  turns.

## Done since (~23:00 UTC)
- Budget curve d5 b20/50/100/200, 200 held-out games (ledger 5a3c9d2be); gi53 wins T6 at d8 b0 (548ab8969).
- Windows CI 44952777971 (wip/freeturn d35211372): success.
- Bucket design for the cantrips + discard (USER request + 12 USER amendments, verbatim):
  docs/design/worldfire-discard-buckets.md (latest 36f17f841).
- Free-turn follow-up agent stopped at 269K: handoff /workspaces/MagicDeckTester/.claude/worktrees/freeturn/logs/freeturn/handoff2.md
  (b861f7160 measured; 3abd94208 cleanup-floor WIP UNBUILT; its 5 USER questions are in its report / handoff).
- FOUND: my landing 42d819bc0 shipped a reference play-drift (Creature_Burn/claude_s9_gi8; tiers_land.log said
  REGRESSION DETECTED). Every session's regression tier inherits it.

- Reference fix LANDED d0d5f8463 (join --urgent), Windows CI 44965126799 success.
- Free-turn pass 2 DONE: wip/freeturn 23588e8a8 (rebased on d0d5f8463; cleanup floor replaces lands-first;
  no trait=0 free turns; Vampires / Sea Creatures units 1.000). Flags default OFF; USER questions in the report.
- Found: scripts/unit_par.sh --first/--last does nothing (every shard runs all 3889 cases) -- deferred tooling fix.

## Running
- URGENT ref-fix agent: ports only the opponent_land_entry stamp + --legacy-opponent-land-entry replay from
  b861f7160 onto origin in worktree .claude/worktrees/refland (branch wip/refland); validates; parent lands with
  `push_queue.py join --urgent` (fix ONLY in that landing).
- Cantrip/discard bucket agent in the worldfire worktree (flags default OFF; A/B 9100001 x64 + 9200001 x96).
- gi4 (seed 9300005) d8 b0 replay, PID 3629867 (6h+) -> build/scratch/worldfire/gi4/{d8b0.out,DONE}.

## Next, in order
1. Land wip/refland urgently once validated (join --urgent; land; CI watch).
2. (done) free-turn pass 2.
3. Relay the bucket agent's A/B; surface the bucket doc's open questions.
4. gi4 d8 b0 DONE (2026-11-22): RECOVERS -- the search itself suspends Halberdier T1, Worldfire T4, wins T5
   (Claude's line). Add to the ledger's sweep OPEN line once the bucket agent's ledger edit is committed (do not
   `commit -a` while it has a dirty tree).
5. Stage 6 report; CronDelete cf1c0324 when everything is reported.

## Open USER questions (defaults taken)
- Search depth for free turns: default = no ply cost (match Extra Turns; its Q9 was never USER-answered).
- Integrator #1's: four PROVISIONAL deferrals (Overmaster / Mistrise uncounterable riders inert,
  Halberdier "may cast" -> cast, Worldfire LTB triggers not fired); INERT_PARAMS for the two Worldfire
  params; the TutorSearchWidth 32 hook (lossless, default ON).
