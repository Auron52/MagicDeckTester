# Analysis ledger -- Worldfire

Deck: `decks/Worldfire/Worldfire.cod` (60 cards, U/R). Started 2026-11-21.

Plan: suspend a finisher (Rift Bolt, Keldon Halberdier) and/or Lotus Bloom, ramp to {6}{R}{R}{R}
(Seething Song, Irencrag Feat, Lotus Bloom, Mercadian Bazaar storage, Sandstone Needle /
Saprazzan Skerry depletion lands), dig with Brainstorm / Ponder / Preordain / Overmaster and the
tutors (Mystical, Personal), cast Worldfire: every permanent, hand and graveyard is exiled and both
life totals become 1; the suspended finisher (in exile, untouched) kills next turn.

## Stage 1 -- coverage (2026-11-21)

Missing (6): Worldfire, Mystical Tutor, Keldon Halberdier, Personal Tutor, Overmaster,
Mistrise Village. Every other card `full`. Fetch-hold review required (Scalding Tarn, Flooded
Strand; hold modelled).

| card | Scryfall cost | proposed tier | notes |
|---|---|---|---|
| Worldfire | {6}{R}{R}{R} sorcery | 3 | new effect: exile all permanents, all hands, all graveyards; each player's life becomes 1. Suspended cards (exile) survive; floating mana survives. |
| Keldon Halberdier | {4}{R} 4/1 first strike | 2/3 | first creature with suspend (4 -- {R}); off-suspend creature has haste (CR 702.62a). First strike inert (no blockers). |
| Mystical Tutor | {U} instant | 1 | tutor_to_top Instant/Sorcery, shuffle; upkeep cast as Worldly Tutor. |
| Personal Tutor | {U} sorcery | 1 | tutor_to_top Sorcery, shuffle. |
| Overmaster | {R} sorcery | 1 | draw 1; "next instant/sorcery can't be countered" inert (passive opponent). |
| Mistrise Village | land | 1 | U, enters tapped unless you control a Mountain or a Forest (checkland_subtypes); {U},{T} uncounterable rider inert. |

Memory Lapse is a faithful dead card (no opponent spells).

## Stage 2 -- implementation (2026-11-21)

| card | tier | template + params | engine | viewer |
|---|---|---|---|---|
| Worldfire | 3 | custom; `exile_all_permanents_hands_graveyards`, `each_player_life_becomes: 1` (new) | `ResolveExileAllAndSetLife` (SpellEffects.h), called last before the graveyard move in `EffectHandler::ResolveImpl` and `TurnSolver` apply_one (lockstep); arms `own_death_live` (GoldFishRunner::StampDeckTraits) | no choice (INERT_PARAMS) |
| Keldon Halberdier | 2 | vanilla_creature; `suspend_time_counters: 4`, `suspend_cost: {R}` | CastOffSuspend gives a CREATURE cast off suspend Haste (CR 702.62a), the single arrival path of both worlds | A: suspend timing = plan variant |
| Mystical Tutor | 1 | custom; `tutor_to_top`, `tutor_types` [Instant, Sorcery] | generic upkeep instant-tutor window (UpkeepTutorOptions) | A: tutor target (plan variant + upkeep prompt) |
| Personal Tutor | 1 | custom; `tutor_to_top`, `tutor_types` [Sorcery] | -- | A: tutor target |
| Overmaster | 1 | draw_spell; `draw: 1` | -- | none |
| Mistrise Village | 1 | basic_land; `produces` [U], `checkland_subtypes` [Mountain, Forest] | -- | land entry (A) |

How Worldfire treats each zone: every permanent on both sides is exiled as its printed card (tokens and
opponent spawns cease to exist); every card in every hand (ours, `players[1].hand`, 2HG `opponent2_hand`)
and both graveyards is exiled into `GameState::exile`; the rollout's merged impulse cards
(`m_is_staged`) stay -- they are in exile already. Untouched: `suspended_cards` (time counters kept),
`staged_cards`, the shared exile, libraries, floating mana. Worldfire itself is on the stack while it
resolves and lands in the emptied graveyard. Life: both totals (2HG: the team pool) become 1;
`opponent_lost_life_this_turn` is set. Leaves-the-battlefield triggers are not fired (none in the pool).

Life payment at 1: fetchlands pay 1 life at life 1 (the enumerator only refuses `life < cost`), and the
executor already loses at the turn's end (`GameEngine::PlayOutFrom`, `HasLost`), but the rollout saw our
death only on `own_death_live` decks. Worldfire now arms it, so both worlds end the game and the search
avoids the payment. Shocks need `life > 2` (never paid at 1); the passive opponent never pays life at
`life <= cost`.

Tests: `test/unit/test_worldfire.cpp` (executor vs rollout post-Worldfire state; Rift Bolt suspended this
turn kills at the next upkeep; exile / staged / floating mana / 2HG second hand; own-death arming; Halberdier
haste and its rollout attack; suspend after Irencrag).

Provider: `WorldfireProvider : DeckProvider`, empty (Certificate NotAssessed). Without it the deck rode
AntiLifegainProvider (fetchlands' `fetch_land_types` + the tutors' `tutor_to_top` set `anti`). Routed at
the top of `DetectDecisionProvider` on Worldfire's two params OR a creature with suspend.

## USER rulings this analysis

- 2026-11-21: opponent-lockdown FREE TURNS -- see `docs/design/opponent-lockdown-free-turns.md`
  (verbatim proposal + provisional defaults).

## Approved deferrals

(none yet)

## Provisional deferrals (await USER sign-off)

- Overmaster: "The next instant or sorcery spell you cast this turn can't be countered" -- inert: the
  passive opponent casts no spells and our Memory Lapse is never cast (goldfish_inert).
- Mistrise Village: "{U}, {T}: The next spell you cast this turn can't be countered" -- inert for the same
  reason; activating it only spends mana and the tap.
- Keldon Halberdier: "you MAY cast it" off suspend collapsed to CAST (declining gains nothing; the Search
  for Tomorrow / Profane Tutor precedent). First strike inert (no blockers).
- Worldfire: leaves-the-battlefield triggers of exiled permanents are not fired (no such card in this pool).
- Viewer paperwork: `exile_all_permanents_hands_graveyards` / `each_player_life_becomes` added to
  `audit_viewer_decisions.py` INERT_PARAMS (no choice exists).
