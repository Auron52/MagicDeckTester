# Worldfire analysis -- checkpoint (2026-11-21 ~12:50 UTC)

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

## Running
- ACCEPT RUN (heavy_run, build/scratch/worldfire/accept_run.sh): regression.sh --both (logs/worldfire/
  tiers_both.log, BOTH_EXIT=) then --overnight --deck=worldfire,worldfire2hg (tiers_on_wf.log, ON_EXIT=);
  marker logs/worldfire/ACCEPT_RUN.DONE. Then: inspect, --both --accept, --overnight --deck=worldfire,
  worldfire2hg --accept, check_gt_logs, commit; message the free-turn agent.
- Claude-play sweep: 4 Opus agents x 4 games, seed 9300001 gi 0-15 (scratch build/scratch/worldfire/sweep/a1..a4).
  DO NOT rebuild this worktree while they run.
- Free-turn agent #2 in .claude/worktrees/freeturn: rebasing wip/freeturn onto wip/worldfire; waits for the
  accept before smoke; handoff2 at logs/freeturn/handoff2.md if it stops.

## Next, in order
1. (done) integrator #2.
2. Opponent land-play fixes (own measured commit): MDFC land faces played (`PlayableAsLand`),
   bouncelands bounce (oldest other land, else itself), Lake of the Dead sacrifices a Swamp. Smoke +
   regression: show which decks move; accept flow.
3. Free-turn rule behind flags (default OFF until USER adopts): screw hurdle (replace rightmost
   nonlands with the list's most common basic up to 2 lands; colour basic for the 4 no-basic lists;
   then replace one land if all need another land), flood hurdle (replace lands with placeholders up to
   2 nonlands, right end), lock check after the opponent's simulated turn, all 2HG heads, round counter
   not advanced, depth counts rounds (DepthAcrossTurn, like extra turns). Report Worldfire raw vs
   credited + free turns received by non-lock decks + search units.
4. Stage 4 baseline profile (`analyze_deck.py --no-rebuild`), provider audit.
5. Stage 4-bis/5j: all three regression tiers (3x Hinata cost gate `suite_gate.py --cost`), accept.
6. Stage 5: verify_deck.py, nonconv/fd-diverge, multi-depth, leaf_tiebreak_check (combo deck -- the
   damage-race tie-break may misprice stored value), claude-play sweep (Claude agents, ~15-20 games),
   viewer audit 5h, discard buckets 5i.
7. Stage 6 report. Push via push_queue (batch), CI watch.

## Open USER questions (defaults taken)
- Search depth for free turns: default = no ply cost (match Extra Turns; its Q9 was never USER-answered).
- Integrator #1's: four PROVISIONAL deferrals (Overmaster / Mistrise uncounterable riders inert,
  Halberdier "may cast" -> cast, Worldfire LTB triggers not fired); INERT_PARAMS for the two Worldfire
  params; the TutorSearchWidth 32 hook (lossless, default ON).
