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

Provider: `WorldfireProvider : DeckProvider` (Certificate NotAssessed). Without it the deck rode
AntiLifegainProvider (fetchlands' `fetch_land_types` + the tutors' `tutor_to_top` set `anti`). Routed at
the top of `DetectDecisionProvider` on Worldfire's two params OR a creature with suspend; pinned in
`test_pirates_provider.cpp`'s routing table. NOT a pure empty derivation: ONE coverage hook,
`TutorSearchWidth() = 32` (`MTG_WORLDFIRE_TUTOR_WIDTH=0` gives the base 6), because Mystical Tutor's ~10
legal names exceed the base width of 6 taken in library order, so in some shuffles Worldfire itself was
unreachable at any budget. It ranks and drops nothing (Cyclone / Maze's End precedent). Default ON
(lossless); USER sign-off requested.

Gates (second integrator, 2026-11-21): units -- test_worldfire 6/6 cases (336 assertions); the
`test_pirates_provider` routing table gained its Worldfire row; the only other failure, shadowborn INT-9
"Thrumming Stone", is pre-existing (fails identically at bd5a2c93f). Coverage 22/22 `full`, missing [].
Cost audit: all 692 costed cards match Scryfall (685 in the full run plus the 7 it was rate-limited on,
re-run). Field audit: Overmaster + Mistrise Village added to `scryfall_reference.json`; offline diff rc 0,
hard fields clean for all six (oracle-text advisories only, from the bracket notes). provider_audit --check:
Worldfire is NOT AUDITED until it has a `.profile.json` (routing is pinned by the unit table); the rc 1 comes
only from the pre-existing Landfall / Selesnya_Lifegain red flags.

Step-4 review (mtg-rules.md), per card -- no findings:
- Worldfire: no target, no trigger; EXILE (not destroy), so indestructible is moot; oracle order kept
  (exile, then life); "becomes 1" is CR 119.5 (no damage; no lifegain/loss watcher in the pool); the spell
  is on the stack while it resolves and lands in the emptied graveyard (CR 608.2n); suspended cards are
  already in exile and keep their counters; tokens cease to exist.
- Keldon Halberdier: suspend is a special action, not a cast (so it stays legal after Irencrag Feat, unit
  test); counter removal is an upkeep trigger; the last-counter cast is a free cast; haste per CR 702.62a,
  never removed because control never changes in a goldfish.
- Mystical / Personal Tutor: search, reveal, shuffle, then put on top (tutor_to_top); instant timing for
  Mystical via the upkeep window, sorcery timing for Personal.
- Overmaster: draw 1; the uncounterable rider is inert (provisional deferral).
- Mistrise Village: check-land semantics on Mountain/Forest subtypes (Steam Vents counts); {T}: U; the
  {U},{T} rider is inert (provisional deferral).

Sanity (d5 b20, 40 games, seed 9100001, no profile): 39/40 won, avg 6.075 (unwon = 9), no crash or assert.
Worldfire cast in 28/40 games, 23 of them winning the next turn. Two casts had nothing suspended and no
mana source left: game 28 (T3, with no suspend card in reach; won T7 off a topdecked hard-cast Rift Bolt) and
game 14 (T7; unwon at max_turns 8). Whether the T3 cast is a misplay is a Stage-5 question. NOTE: the
game log JSON writes NO action for a suspend (pre-existing, true of Rift Bolt decks too), so a suspend is
visible only as a card leaving the hand.

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
