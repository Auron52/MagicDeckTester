# Worldfire analysis -- checkpoint (2026-11-21 ~11:15 UTC)

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

## Running
- Integrator #2 (Opus, background): handoff steps 2-9 -- unit tests, coverage, cost audit, field audit
  (Overmaster + Mistrise missing from scryfall_reference.json after 429s), provider_audit --check,
  40-game sanity run (self-destruct Worldfire? crash after mid-plan Worldfire?), smoke byte-identity
  (no accept), mtg-rules Step-4 review, commit.

## Next, in order
1. Read integrator #2's report; relay to USER; fix/loop if a gate failed.
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
