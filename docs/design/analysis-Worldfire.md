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
`test_pirates_provider.cpp`'s routing table. NOT a pure empty derivation: ONE hook, `TutorCandidates`,
a USER-directed prune (2026-11-21: *"Mystical tutor should be heavily pruned and should look for missing
combo pieces. Worldfire, Suspend card (rift bolt) and acceleration (Seething Song or Irencrag) based on what
the current situation needs."* / *"(Same deal for Personal Tutor of course)"* / *"(except that Seething Song
is not a target)"* / *"And, to be clear, we can prune further than that based on what is in hand."*).
Kept: Worldfire while none in hand; a suspend threat while none suspended or in hand; acceleration while
no ritual in hand (acceleration only once every piece is present); the full list only if no piece is left
in the library. `MTG_WORLDFIRE_TUTOR_PIECES=0` gives the base list. It replaced integrator #1's width-32
coverage hook (`MTG_WORLDFIRE_TUTOR_WIDTH`, deleted).

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
- Overmaster: draw 1; the uncounterable rider is inert (approved deferral).
- Mistrise Village: check-land semantics on Mountain/Forest subtypes (Steam Vents counts); {T}: U; the
  {U},{T} rider is inert (approved deferral).

Sanity (d5 b20, 40 games, seed 9100001, no profile): 39/40 won, avg 6.075 (unwon = 9), no crash or assert.
Worldfire cast in 28/40 games, 23 of them winning the next turn. Two casts had nothing suspended and no
mana source left: game 28 (T3, with no suspend card in reach; won T7 off a topdecked hard-cast Rift Bolt) and
game 14 (T7; unwon at max_turns 8). Whether the T3 cast is a misplay is a Stage-5 question. NOTE: the
game log JSON writes NO action for a suspend (pre-existing, true of Rift Bolt decks too), so a suspend is
visible only as a card leaving the hand.

## Stage 4 -- baseline profile (2026-11-21, BEFORE the tutor/Bolt prunes; to be re-run after them)

- `analyze_deck.py --no-rebuild` rc 0: profile written (card_scores + mulligan defaults).
- Cost-aggregate diagnostic: COST_NEUTRAL.
- Discard analysis: NO_RULE_CONSIDER_SEARCH on only 4 labelled decisions (0.25 regret) -- too few to act on.
- provider_policy: PASS.
- Card scores: Keldon Halberdier +0.44 (1st copy), Lotus Bloom +0.35/+0.37, Seething Song +0.18/+0.17,
  Worldfire +0.05 then -0.66 for a 2nd copy, Rift Bolt -0.22/-0.28, Memory Lapse -0.44.
- Card-score games took 85-130 s each: watch the 3x-Hinata suite cost gate.

## Stage 4 -- re-run WITH the prunes (2026-11-21, the committed profile)

- rc 0. Cost diagnostic COST_NEUTRAL; discard STATUS_QUO_OK (ranking at or near the searched optimum);
  provider_policy PASS.
- Card scores (pre-prune -> now, first copy): Mystical Tutor +0.08 -> +0.28, Personal Tutor -0.02 -> +0.07,
  Worldfire +0.05 -> +0.12 (2nd copy -0.66 -> -0.18), Keldon Halberdier +0.44 -> +0.13, Lotus Bloom +0.35 ->
  +0.44, Seething Song +0.18 -> +0.19, Rift Bolt -0.22 -> -0.09.

## Tutor prune + Rift Bolt hold -- A/B (2026-11-21, d5 b20, profile attached, Release)

Prunes ON (USER rulings) vs OFF (`MTG_WORLDFIRE_TUTOR_PIECES=0 MTG_WORLDFIRE_HOLD_BOLT=0`), paired per game:

| block | games | ON avg | OFF avg | ON better | ON worse | slow games (>30 s) ON / OFF |
|---|---|---|---|---|---|---|
| seed 9100001 | 64 | 5.734 (64/64 won) | 5.891 (63/64) | 12 | 4 | 4 / 7 |
| seed 9200001 (held out) | 96 | 5.760 | 5.969 | 19 | 3 | 3 (124 s) / 11 (480 s) |

The 4 worse games of block 1, attributed by flipping one lever at a time and re-run at b200:
- gi 57 (Bolt hold at b20): recovers at b200 (4 = 4) -- budget noise (a T1 land-order difference).
- gi 27 (Bolt hold at b20): reverses at b200 (ON 6, OFF unwon).
- gi 18 (tutor prune): persists (6 vs 5). Both arms cast Worldfire with nothing suspended; OFF's Mystical
  Tutor fetched Personal Tutor (a non-piece, refused by the prune), and the different shuffle put a Rift
  Bolt on top for a T5 hard cast. A tutor-for-a-tutor line that wins on the known shuffle result, not a
  better plan.
- gi 8 (Bolt hold): persists (7 vs 6). Worldfire never reached the hand; OFF hard-cast Halberdier T3 and
  raced it with Rift Bolts as burn (T6). The hold allows a hard cast only when the Bolt alone is lethal.
  PROPOSED AMENDMENT (agent; default OFF, `MTG_WORLDFIRE_BOLT_RACE=1`): release the hold once we control a
  creature. MEASURED, NOT ADOPTED: same blocks, paired vs prunes ON -- block 1 5.719 vs 5.734 (1 better: gi 8,
  0 worse); held out 5.781 vs 5.760 (1 better, 3 worse: gi 16, 52, 82). Net ~+1 turn over 160 games, so it
  stays OFF and the USER's hold stands as shipped. (Race arm ran at --threads 4; block 1 differs from ON only
  at gi 8, so play is thread-invariant here.)

## USER rulings this analysis

- 2026-11-21: free-turn search depth -- *"I guess its potentially okay to keep it that way. The main cost
  lever is budget rather than depth anyway."* (no ply cost, like extra turns).
- 2026-11-21: the four deferrals (Overmaster / Mistrise riders inert, Halberdier always cast + first
  strike inert, Worldfire LTB triggers not fired) -- *"Yes, those are fine."* -> APPROVED.
- 2026-11-21: INERT_PARAMS for Worldfire's two params -- *"it is true that there are no choices associated
  with the Worldfire card beyond casting it or not."* -> APPROVED.
- 2026-11-21: the tutors' prune (see Provider below).

- 2026-11-21: opponent-lockdown FREE TURNS -- see `docs/design/opponent-lockdown-free-turns.md`
  (verbatim proposal + provisional defaults).

## Approved deferrals (USER 2026-11-21: *"Yes, those are fine."*; INERT_PARAMS: see rulings above)

- Overmaster: "The next instant or sorcery spell you cast this turn can't be countered" -- inert: the
  passive opponent casts no spells and our Memory Lapse is never cast (goldfish_inert).
- Mistrise Village: "{U}, {T}: The next spell you cast this turn can't be countered" -- inert for the same
  reason; activating it only spends mana and the tap.
- Keldon Halberdier: "you MAY cast it" off suspend collapsed to CAST (declining gains nothing; the Search
  for Tomorrow / Profane Tutor precedent). First strike inert (no blockers).
- Worldfire: leaves-the-battlefield triggers of exiled permanents are not fired (no such card in this pool).
- Viewer paperwork: `exile_all_permanents_hands_graveyards` / `each_player_life_becomes` added to
  `audit_viewer_decisions.py` INERT_PARAMS (no choice exists).

## Claude-play sweep
- commit: `70fc1c321` (binary; tutor/Bolt prunes + land-play fix, before the Feat-restrictor fix)
- seeds: 9300001 (seed = base+gi) games: 16 (gi 0-15), 4 Opus players x 4 games; benchmark d5 b200
- flags: 0 unresolved
- Results: Claude faster in 3 (gi 0: T6 vs T7, budget-bound -- the search finds T6 at b1000; gi 4: T5 vs T6;
  gi 11: T5 vs T6), equal in 13, never slower. No legality / rules / state flag.
- FIXED (search hole, found by gi 8 + gi 11): the Irencrag Feat restrictor counted a SUSPEND as a cast, so
  "suspend Rift Bolt + Irencrag Feat + Worldfire" was never enumerated at ANY budget (`--validate-line` ->
  legal_not_enumerated; b0 also misses it). `RestrictorCastsOf` now counts a Suspend as 0.
- Feat-fix tier result: only the 9 worldfire* keys move (dragonstorm, the other Feat + suspend deck, does not);
  searched 55 faster / 1 slower. The slower one (worldfire_regression_d5_s3003 gi52, 5->6 at b20) RECOVERS at
  b200 (new 5 = old 5): budget churn from the wider plan set. Landed in 42d819bc0 with the GT.
- OPEN (search misplay candidate, gi 4 / seed 9300005): from the same mulligan bottom the search wins T6 at d5
  b200 / b2000 and d8 b200; Claude suspends Halberdier T1 and wins T5 (handed the post-suspend board the search
  finds T5). d8 b0 pending.
- Deferred, no win-turn effect: `docs/design/worldfire-sweep-deferred.md` (identical-copy reorder fold, the
  upkeep win check, the Ponder shuffle label, suspend reporting/labels/log, the target-reply index trap).
