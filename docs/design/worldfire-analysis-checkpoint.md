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

## UNCOMMITTED in the worldfire worktree (NOT YET BUILT -- wait for the profile chain to exit first)
- `WorldfireProvider::TutorCandidates` + `TutorNeedsCandidate` (replaced TutorSearchWidth 32 /
  MTG_WORLDFIRE_TUTOR_WIDTH): Worldfire while none in hand; Rift Bolt while none in hand / suspended (a
  pending Halberdier makes it low priority: dropped when Worldfire or accel also kept); accel only while
  mana short (LooseManaCeiling + Lotus + ritual nets < 9 [+1 if threat missing]; under-count), no second
  Irencrag; nothing missing -> {} -> tutor not cast; no piece in library -> full list.
  MTG_WORLDFIRE_TUTOR_PIECES=0 off.
- New generic hooks `OffersCastNow` / `OffersSuspend` (DecisionProvider.h, default true) asked in
  TurnSolver.cpp CollectActions next to NeverCast and at the Suspend emitter. Worldfire: Rift Bolt hard
  cast only when lethal; suspend only if opp life <= 3 or (Worldfire in hand AND over-count mana >= 9 +
  suspend cost: LooseManaCeiling + 1 open land drop + Lotus + ritual nets). Halberdier never gated.
  MTG_WORLDFIRE_HOLD_BOLT=0 off.
- test/unit/test_worldfire.cpp: two new TEST_CASEs (tutor prune subcases; Bolt hold).
- Then: ./build.sh, units (per-shard rc), smoke byte-identity (new hooks default true -> expect 0 moved),
  sanity A/B prunes on vs off (d5 b20, seed 9100001, >=40 games each, now WITH the profile), commit.
- NOTE: the Stage 4 profile was generated BEFORE these prunes -> re-run analyze_deck after them.

## Running
- Stage 4 profile chain (heavy_run, PID 3393436): `logs/worldfire/stage4_profile.log`, last step =
  single-thread discard evidence (400 g d3). Profile json already written; cost diag COST_NEUTRAL.
  Card-score games 85-130 s each -> 3x Hinata cost gate is a concern.
- Land-play agent (Opus, ~214K ctx -> handoff soon) in `.claude/worktrees/opp-landplay`, branch
  wip/opp-landplay: told to build/smoke/regression WITHOUT the heavy lock (my chain held it 40 min on one
  core). Handoff file if it stops: `logs/opp-landplay/handoff.md` (in that worktree).
- heavy_run.sh gap: does not release the lock for a script chain on a single-thread step (note only).

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
