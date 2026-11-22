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
  finds T5). RECOVERS at d8 b0 (2026-11-22, ~8 h one core): the search itself suspends Halberdier T1, casts Worldfire T4 and wins T5 -- Claude's line; a reach/budget miss, not a search hole.
- Deferred, no win-turn effect: `docs/design/worldfire-sweep-deferred.md` (identical-copy reorder fold, the
  upkeep win check, the Ponder shuffle label, suspend reporting/labels/log, the target-reply index trap).

## Stage 5b / 5c -- depth sweep and leaf tie-break (2026-11-21, committed profile, Release/PGO)
- **5c2 leaf_tiebreak_check (`--tag worldfire`, 12000 paired games at play settings): KEEP THE DEFAULT (ON).**
  Net -21 turns (half A -11, half B -10, same sign in both halves); 303 better / 283 worse; binds in 4.9% of
  games. The damage-race tie-break helps this combo deck slightly; no opt-out.
- **5b depth sweep, seed 9400001, 96 games, paired per game (`MTG_DUMP_WINS`):**

  | setting | avg (unwon = max_turns+1) | vs d5 b20 |
  |---|---|---|
  | d3 b10 | 5.531 | 2 better / 2 worse |
  | d5 b20 (play) | 5.531 | -- |
  | d5 b200 | 5.344 | 17 better / **0 worse** |

  Monotone: no setting is worse than a cheaper one in aggregate, and b200 never loses a game to b20. The one
  unwon game (gi53, seed 9400054) is unwon at every setting above, but **wins T6 at d8 b0** (replay 2026-11-21,
  `--seed 9400054 --game-index 53`, ~50 min on one core): a search-reach loss, not a deck limit. d3 b10 vs d5 b20 is a tie (gi35 / gi60 slower at
  d5 b20, two others faster), so depth 5 buys nothing at 20 ms.
- **5c budget starvation: YES at the play setting.** d5 b200 is 0.19 turns/game faster with zero losers, so the
  20 ms play budget leaves wins unfound (the sweep's gi0 and the Feat-fix gi52 tell the same story). This is a
  SETTINGS question for the USER (the depth matrix / play budget), not a search change: raising the budget
  roughly 10x the per-game cost (worldfire is 2.06x Hinata at b20 today).

## Budget curve at d5 (2026-11-21, held-out seed 9500001, 200 games, paired, profile attached, BEFORE the cantrip buckets)
One pooled `--batch` (800 jobs, 32/32 workers). Raw: `build/scratch/worldfire/budget/`; summary `logs/worldfire/budget_curve.txt`.

| budget | avg win turn | vs b20 better / worse | wall s/game | units/game |
|---|---|---|---|---|
| b20 (play) | 5.595 | -- | 11.8 | 0.57M |
| b50 | 5.560 | 9 / 2 | 23.9 | 0.95M |
| b100 | 5.530 | 17 / 4 | 36.3 | 1.38M |
| b200 | 5.485 | 26 / 4 | 61.5 | 2.13M |

Monotone in aggregate; each worse game is a single +1 turn (gi 23, 29, 76, 93, 97, 193 across the arms), not
attributed. b200 buys 0.11 turns/game at ~3.7x units. A SETTINGS question for the USER; re-measure after the
cantrip buckets (`docs/design/worldfire-discard-buckets.md`), which change both the cost and what each budget finds.

## Cantrip keep order (bucketed discard + Brainstorm / Ponder / Preordain) -- built 2026-11-21/22, ADOPTED (DEFAULT ON) 2026-11-22

Design: `docs/design/worldfire-discard-buckets.md` (the parent's draft and the USER's amendments 1-12). USER
request (verbatim): *"It seems pretty clear we are going to need prunes for Brainstorm, Ponder and Preordain.
Probably similar to those in Cyclone, but a bit simpler because we don't need to worry about our topdeck."* --
*"But essentially what we'll need to design is the discard heuristic to do this well. ... It should be bucketed
based on the combo and mana needs."* Discard buckets are USER-owned (analyze-deck 5i); the buckets are the USER's
design (amendments 1-12). The levers were built OFF and turned ON on 2026-11-22 (see "Adoption" below) as a
better-and-cheaper provider prune under the USER's standing clean-win approval (parent's decision). Code: `WorldfireKeepRank::Of` and the consumers in `WorldfireProvider`
(`src/ai/DecisionProviders.cpp`); heurarm slots `WORLDFIRE_*`; tests: `test/unit/test_worldfire.cpp` ("worldfire
keep order: ...", 11 cases).

**Census first** (MTG_CANTRIP_ATTRIB, 12 games, seed 9100001, d5 b20, profile, levers off): of 13.9M search
units, **65% were spent under Brainstorm put-back variants** (mean menu width 30, p90 43, max 90) and **30% under
Ponder variants** (width 7: six orders + the shuffle). Preordain's scry is NOT searched (width 5, 152,886
resolutions, all by the provider default). So the put-back fan is the cost, Ponder second, and a Preordain rule is
a pure play change that removes no search.

### The rule in plain words: one ranking, four consumers
Rank each card in hand (for Ponder and Preordain, the looked-at cards too). Quotas are filled first:
1. **Go-off turn T\*.** This is the first turn from now (offsets 0..4) on which Worldfire, a finisher and the mana are
   all ready. Every wait is counted:
   - Mystical Tutor brings its card on the next draw. Personal Tutor brings it next turn, or this turn if a cantrip in
     hand can draw it, paying both {U}.
   - A Rift Bolt is suspended ON T\* and needs one more {R}.
   - A Halberdier must land the turn after T\*. A pending one pins T\* to its arrival minus 1. One in hand allows
     T\* at its suspend turn + 3 or later.
   - Mana is 9, plus 1 for a Rift Bolt suspended that turn. It comes from:
     - the board (depletion lands 2, Bazaar its counters + its charging turns);
     - one land drop per turn (a tapped land counts from the next turn);
     - Blooms on the battlefield or arriving off suspend;
     - rituals at their net (Song +2 each, one Feat +3).

   Nothing by now+4 is "far". At cleanup this turn is over.
2. **COMBO** (1): the first Worldfire. With none, the best live tutor in hand (Mystical > Personal) fills it, ranked
   just below a real Worldfire. A 2nd Worldfire is DEAD.
3. **FINISHER** (1): the option giving the earliest T\*. Ties go pending Bolt > pending Halberdier > Halberdier in
   hand > Bolt in hand > a second tutor.
   - The first finisher of the OTHER type is BACKUP, the last overflow card shed.
   - A same-type 2nd Rift Bolt is DEAD.
   - A further Halberdier is BACKUP.
4. **MANA**:
   - **Lotus Bloom:** only if the go-off WITHOUT the pool's Blooms (T0) is later than the Bloom's arrival, or far. A
     2nd only if T\* is still far. Any other Bloom is the first card shed after DEAD.
   - **Lands:** the drops through T\*. One extra land is BACKUP.
   - **Rituals:** the fewest (Song, then Feat, then a spare tutor) that reach T\*'s need. One extra is BACKUP. A 2nd
     Feat is DEAD. A spare tutor with the mana covered is DEAD.
   - **Far:** all mana is quota.
   - **Inside MANA**, best first:
     1. colour sources: red to 1 (Song / a Rift Bolt suspend), or 3 (Feat, or Worldfire's RRR with no ritual kept);
        blue to 1 always. Needle counts RR, Skerry UU, Bazaar R. Steam Vents and the fetches count as either colour,
        not both.
     2. the next land drop.
     3. castable acceleration: Song with 3 incl. R before it; Feat with 4 incl. RRR or after a Song; a kept Bloom.
     4. a 2nd blue source while 2+ blue spells are held.
     5. further drops.
     6. acceleration castable only with those further drops.
   - **Mercadian Bazaar** keeps its place only on our turns 1-2 (`kWorldfireBazaarEarlyTurns`). Later it is the last
     land, unless T\* is far.
5. **DIG** (Brainstorm > Ponder > Preordain > Overmaster; tutors are not dig):
   - while a piece is missing, 1 + the missing pieces, at least 2. A piece filled only by a tutor counts as missing;
   - 1 once both pieces are in hand;
   - 0 once the mana is also covered;
   - dig beyond the quota is overflow, kept above surplus mana.
6. **DEAD**: Memory Lapse, the duplicates, and every tutor when nothing is missing.

**Keep order, best first.**
- **A piece GENUINELY missing** (no card, no tutor): COMBO > FINISHER > dig slot 1 > the next land drop > dig slot 2 >
  the rest of MANA > further dig.
- **Otherwise:** COMBO > FINISHER > MANA > dig. A dig kept only to draw a tutored card ranks below the go-off mana.
- **Then the overflow:**
  1. BACKUP (other-type finisher > extra land > extra ritual > 2nd Halberdier);
  2. dig beyond quota;
  3. rituals beyond;
  4. lands beyond;
  5. a Bloom near the combo turn;
  6. DEAD (tutor > duplicate > Memory Lapse).
- **Land keep order:** Needle > Steam Vents > Scalding Tarn > Flooded Strand > Mountain > Bazaar > Skerry > Island >
  Mistrise. A tapped land drops below every untapped one when no drop remains before T\*.
- **The look:** a looked-at card only takes a slot the hand has not filled.

**Consumers** (each its own lever, all DEFAULT ON since 2026-11-22; `=0` turns each off):
- `MTG_WORLDFIRE_BUCKET_DISCARD`: the cleanup sheds the lowest card. One index, re-ranked per shed.
- `MTG_WORLDFIRE_BRAINSTORM_RULE`: put back the two lowest, the higher of them on top. The searched put-back fan is
  that one pick (`_BRAINSTORM_SINGLE`, on under the rule; =0 keeps the rule as the fan's order only).
- `MTG_WORLDFIRE_PONDER_RULE`: the three go back in rank order. Keep-in-that-order vs shuffle stays SEARCHED (USER
  amendment 9), so the fan goes from 7 to 2. `_PONDER_SINGLE` (OFF) lets the rank decide the shuffle too.
- `MTG_WORLDFIRE_PREORDAIN_RULE`: keep a card iff it fills a still-unfilled quota, best on top; bottom the rest.
  - `_AGGRESSIVE` (on under the rule): with a piece genuinely missing, judged on the hand and board, keep only that
    piece or one tutor for it.
  - `_AGGR_DIG` (on): dig is bottomed too.
  - `_AGGR_FINISHER` (on): a missing finisher triggers it, not only a missing Worldfire.
- `MTG_WORLDFIRE_ORDER_DEADLINE` (ON since 2026-11-22): an agent proposal, see the attribution below.
- Experiment arms that stay OFF: `_PONDER_SINGLE`, `_AGGR_DIG_BLUE`, `MTG_WORLDFIRE_BAZAAR_T1`.

### A/B -- d5 b20, profile attached, paired per game, ONE pooled batch (1600 jobs, arms innermost), Release
`on` = the four consumers with their default sub-levers; `off` = none. Units / wall are the arm's summed games;
"slow" = games over 30 s.

| block | arm | avg | better / worse vs off | net turns | units | wall | slow games |
|---|---|---|---|---|---|---|---|
| 9100001 x64 | off | 5.547 | -- | -- | 32.2M | 811 s | 5 (392 s) |
| | **on** | **5.453** | **7 / 1** | **-6** | **9.4M (0.29x)** | **324 s (0.40x)** | 3 (155 s) |
| 9200001 x96 (held out) | off | 5.479 | -- | -- | 37.6M | 1007 s | 8 (466 s) |
| | **on** | **5.417** | **15 / 10** | **-6** | **10.7M (0.28x)** | **338 s (0.34x)** | 0 |

All 160 games won in every arm. **ON is a clean win on both blocks: better (net -6 and -6) and about 3.5x cheaper.**
It shipped OFF at first, because the buckets are the USER's (parent's instruction); adopted 2026-11-22, below.

Per-consumer (each consumer alone; and `on` with one sub-lever flipped), net turns vs off, block a / held out:

| arm | net | units (a / h) | reading |
|---|---|---|---|
| Brainstorm rule alone | -3 / -6 | 9.6M / 10.4M | carries nearly all the cost cut and most of the gain |
| Ponder rule alone | -2 / -2 | 27.2M / 32.8M | small cut (fan 7 -> 2) |
| Preordain rule alone | +2 / -5 | 32.7M / 36.4M | unsearched: a play change only, noisy |
| discard alone | -1 / 0 | 32.6M / 36.9M | inert in effect |
| on, `_BRAINSTORM_SINGLE=0` (order only) | -3 / +1 | 28.5M / 35.1M | the one-option prune is what buys both |
| on, `_PONDER_SINGLE=1` | +1 / -4 | 8.0M / 9.4M | worse than keep/shuffle searched: keep the USER's shape |
| on, `_AGGR_DIG=0` | -6 / -8 | 9.4M / 10.5M | slightly better than the default (a dig on top kept) |
| on, `_AGGR_FINISHER=0` | -7 / -7 | 9.3M / 10.7M | slightly better than the default (Worldfire-only trigger) |

### Attribution of the 11 worse games (ON slower than OFF), each lever flipped, at b20 and b200
10 of 11 **recover at b200** (ON = OFF): a gi37; h gi2, gi11, gi25, gi29, gi35, gi62, gi63, gi64, gi82. That is
budget churn. **h gi31 persists** (b200: ON 6, OFF 5). Flipping only the Ponder rule recovers it at both budgets; at
b20 that arm is byte-identical to OFF (same digest).

Root cause:
- The unpruned arm cracks Scalding Tarn before its T1 Ponder, sees Rift Bolt / Lotus Bloom / Seething Song, and puts
  **the Bloom on top**. It suspends the Bloom on T1, the Bloom arrives on T4, and Worldfire goes off on T4.
- The rank puts Rift Bolt (the missing finisher) above the Bloom. But Rift Bolt is only needed on the go-off turn,
  while the Bloom's three-turn suspend wait means a Bloom drawn a turn late delays the go-off a turn.
- **PROPOSED AMENDMENT (agent, `MTG_WORLDFIRE_ORDER_DEADLINE`, default OFF):** the cards Ponder and Preordain put back
  are ordered by WHEN the plan needs each in hand, quota cards first:
  - a go-off Bloom, the dig and the next land drop: now;
  - a Halberdier: T\*-3;
  - Worldfire, a Rift Bolt and the rituals: by T\*.

  The keep value breaks ties. A unit test pins the gi31 state.

### Deadline-order arm on the same build (ab2, 320 jobs)
`on` re-run reproduces the first run exactly (5.453 / 5.417, same units): play is deterministic.

| block | `on` + `MTG_WORLDFIRE_ORDER_DEADLINE` vs `on` |
|---|---|
| 9100001 | identical, 0 / 0 |
| 9200001 | 2 better (gi31, gi64) / 1 worse (gi56), net -1 |

### FINAL ARM: the USER's answers folded in (ab3, 800 jobs, one pooled batch)
The answers folded in:
- **Halberdier:** each finisher option has a LATEST go-off turn. The earliest go-off wins, ties go to the earlier
  kill. "9 before 10" makes the Halberdier the finisher.
- **Dig fill:** Brainstorm > Ponder only with junk in hand and a shuffle at hand.
- **Mana:** no extra land or extra ritual in BACKUP ("never more than 10").
- **Tapped lands:** a tapped land is overflow only when its drop would be T\* itself.
- **Mystical Tutor:** its {U} is free with a non-depletion blue source in play; otherwise it costs +1 on T\* (an upkeep
  cast).
- **New experiment arms (OFF):** `MTG_WORLDFIRE_PREORDAIN_AGGR_DIG_BLUE` keeps a dig on top with 2+ spare blue, no other
  blue spell and no go-off this turn. `MTG_WORLDFIRE_BAZAAR_T1` makes "early" turn 1 only.

| block | arm | avg | better / worse vs off | net | units | wall | slow (>30 s) |
|---|---|---|---|---|---|---|---|
| 9100001 x64 | off | 5.547 | -- | -- | 32.2M | 753 s | 5 (337 s) |
| | **on** | **5.469** | 7 / 2 | **-5** | **8.9M (0.28x)** | 226 s | 0 |
| | on + deadline | 5.453 | 8 / 2 | -6 | 8.8M | 210 s | 0 |
| | on + dig-blue | 5.469 | 7 / 2 | -5 | 8.9M | 213 s | 0 |
| | on + Bazaar T1 | 5.469 | 7 / 2 | -5 | 8.9M | 208 s | 0 |
| 9200001 x96 | off | 5.479 | -- | -- | 37.6M | 812 s | 4 (235 s) |
| | **on** | **5.365** | 17 / 7 | **-11** | **10.6M (0.28x)** | 281 s | 0 |
| | on + deadline | 5.354 | 16 / 5 | -12 | 10.5M | 270 s | 0 |
| | on + dig-blue | 5.354 | 17 / 6 | -12 | 10.6M | 267 s | 0 |
| | on + Bazaar T1 | 5.365 | 17 / 7 | -11 | 10.6M | 249 s | 0 |

`off` reproduces the first batch exactly. The final rule set is better than the first cut on the held-out block (-11
vs -6) and as cheap. Bazaar turn-1 never binds on these seeds. Deadline order and dig-blue each add one turn on the
held-out block.

Attribution of the final arm's 9 worse games, each lever flipped, at b20 and b200:
- **8 recover at b200** (ON = OFF): a gi2, gi37; h gi11, gi35, gi63, gi64, gi82, gi85.
- **h gi31 persists** (b200: ON 6, OFF 5). It is the Ponder ORDER again: flipping only the Ponder rule recovers it,
  and so does the proposed deadline order, at b20 and at b200.
- No worse game is the discard, the Brainstorm rule or the Preordain rule.

### Adoption (2026-11-22): ON + ORDER_DEADLINE, defaults flipped
USER rulings (2026-11-22, verbatim, relayed by the parent):
- 1a, the adoption bar: *"Obviously if I designed the rules and they work well (i.e. improvements in average quality
  and no unrecoverable that cannot be shown to be clairvoyance) then we should turn them on."*
- 1b, the experiment arms (`_AGGR_DIG_BLUE`, `_BAZAAR_T1`, `_AGGR_DIG=0`, `_AGGR_FINISHER=0`): *"If there is no
  difference I would keep the rule as designed. These are worth considering only if we see them perform better in
  testing. If the difference is small we should try a larger test set to separate them."* They stay at the designed
  defaults (`_AGGR_DIG` and `_AGGR_FINISHER` ON; `_AGGR_DIG_BLUE`, `_BAZAAR_T1`, `_PONDER_SINGLE` OFF). A follow-up
  agent runs the larger test.
- 1c1-1c3 (rule refinements): a follow-up agent A/Bs them against this adopted baseline. No rule changed here.

**Attribution, ON + deadline vs OFF, final build** (ab3 at b20; attr2 at b200; the same binary byte-for-byte, so
the b200 runs already in attr2 are the re-runs). 7 games are slower than OFF at b20; **all 7 recover at b200**, so
none needed a d8 b0 run and none is a clairvoyance question:

| block | gi | b20 on+DL / off | b200 on+DL / off | verdict |
|---|---|---|---|---|
| 9100001 | 2 | 6 / 5 | 5 / 5 | recovers (budget churn) |
| 9100001 | 37 | 6 / 5 | 5 / 5 | recovers |
| 9200001 | 11 | 6 / 5 | 5 / 5 | recovers |
| 9200001 | 35 | 7 / 6 | 6 / 6 | recovers |
| 9200001 | 63 | 6 / 5 | 5 / 5 | recovers |
| 9200001 | 82 | 5 / 4 | 4 / 4 | recovers |
| 9200001 | 85 | 6 / 5 | 5 / 5 | recovers |

h gi31 (the persisting game of the plain ON arm) is faster under deadline: b200 on+DL 5 = OFF 5. h gi56, worse under
deadline in ab2 (first build), is not worse on the final build.

**Defaults now ON:** `MTG_WORLDFIRE_BUCKET_DISCARD`, `_BRAINSTORM_RULE`, `_PONDER_RULE`, `_PREORDAIN_RULE`,
`_ORDER_DEADLINE` (each `EnvOn(..., true)`, `=0` turns it off); sub-levers at their under-the-rule defaults.
Unit tests pin the OFF paths with explicit `=0` arms and check the defaults.

**Tiers with the defaults ON** (`regression.sh --both`, one pooled batch, Release): scenarios 605/605; units as with
flags OFF (one known pre-existing failure, `test_shadowborn_int9.cpp:184`, plus unit_par's shard-count `rc=3`).
**Only worldfire keys moved** (smoke 239/243 pass, regression 384/390, every FAIL a worldfire key), accepted
(`--deck=worldfire,worldfire2hg`), `check_gt_logs.py` consistent:

| key | old | new |
|---|---|---|
| worldfire_smoke d0 / d3 / d5 | 8.366 / 5.600 / 5.725 | 8.139 / 5.800 / 5.600 |
| worldfire2hg_smoke d5 | 5.700 | 5.450 |
| worldfire_regression d0 / d3 / d5 s2002 / d5 s3003 | 8.344 / 5.533 / 5.567 / 5.483 | 8.150 / 5.533 / 5.483 / 5.367 |
| worldfire2hg_regression d5 | 5.733 | 5.733 |

The regression tier's other FAIL is the strict reference check: `Creature_Burn/claude_s9_gi8` play-drift. It is
NOT this change (the pre-change binary drifts identically). It is fixed upstream by d0d5f8463 (the
`opponent_land_entry` stamp), which is past this branch's merge base, so it clears on rebase.

**The tier's 18 searched-slower games** (each re-run ON vs OFF on the same binary, OFF = the five flags =0; scratch
`build/scratch/worldfire/adopt/tierattr*`):
- 13 recover at b200 (ON = OFF or ON faster).
- 5 persist at b200, which is 3 physical games:
  - seed 1004: smoke gi3 at std d3, std d5 and 2HG d5;
  - seed 3046: s3003 gi43;
  - seed 3057: s3003 gi54.
- At d8 b0, seeds 1004 (std and 2HG) and 3046 **recover** (ON 5 = OFF 5). At b200 both had ON suspend Lotus Bloom on
  T2 instead of T1, a budget miss.
- **Seed 3057 (gi54) persists at d8 b0** (ON 5, OFF 4). Only the Ponder rule's flip recovers it at b200.
  - Mechanism: OFF opens Skerry, which lets it Ponder twice on T2. The second Ponder SHUFFLES away Preordain /
    Flooded Strand / Ponder and draws Seething Song, then Worldfire, and goes off T3.
  - **Evidence of clairvoyance:** a blind ensemble (`blind_salt` 0-7, d5 b200, both arms) TIES: OFF 5.5
    (5,5,5,6,7,5,6,5) vs ON 5.5 (5,5,7,6,6,5,5,5). The blind OFF search never reaches T4, so OFF's T4 win needs the
    shuffle outcome it could not know.
  - Under the USER's bar (1a) gi54 is clairvoyance and does not block.
  - A blind d8 b0 ensemble was started and killed as degenerate: 1 of 16 games done in 2.3 h, the ON salt 0 game won
    T5. Blind arms are never d8-gated (USER 2026-11-21).

### Validation (flags OFF)
- **Units:** every shard passes. `unit_par.sh` exits 3 for its known shard-count bug. The Worldfire cases now number
  22.
- **Scenarios:** 605/605.
- **Smoke (first cut): 243/243 byte-identical.** No key moved, worldfire included. The refactor of the tutor census is
  byte-identical. GT not accepted.

## Free-turn rule (MTG_FREE_TURNS, default OFF -- adoption is the USER's) -- measured 2026-11-21
Branch `wip/freeturn` (2d373428c implementation, d35211372 doc; spec + measurement in
`docs/design/opponent-lockdown-free-turns.md`). Flags OFF: smoke 243/243 byte-identical, scenarios 605/605.
Flag ON, regression tier:

| key | off | on (credited) | on (raw rounds) |
|---|---|---|---|
| worldfire d5 s2002 | 5.567 | 4.500 | 6.467 |
| worldfire d5 s3003 | 5.483 | 4.300 | 6.550 |
| worldfire d3 s2002 | 5.533 | 4.533 | 6.800 |
| worldfire2hg d5 s2002 | 5.733 | 4.633 | 5.933 |

Direct 64 games d5 b20 seed 9100001: off 5.547, on 4.422. Every Worldfire d5 game takes at least one free turn.
Raw rounds get SLOWER (nothing tie-breaks toward fewer taken turns at an equal credited turn; one game took 10).
Follow-ups in progress before the USER can adopt: a Creature Burn reference drifts under the flag, the
`steal_tap_count` lock trait (Vampires pays 3x units for a lock it cannot make), and the passive opponent's
oldest-first cleanup handing non-lock burn decks a free turn.

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-11-21)

`verify_deck.py decks/Worldfire/Worldfire.cod --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 22 cards full (missing=0, partial=0) |
| card_costs | PASS | yes | all mana costs match Scryfall (cost/cmc only) |
| card_fields | PASS | yes | 893 cards match snapshot (cost/PT/types/keywords); 18 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | PASS | yes | in all 3 suite tiers with GT (worldfire) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 6 type(s) wired (emitter + GUI): dig, land_entry, reorder, scry, target, tutor_etb |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 8001] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/344 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |
| suite | PASS | yes | suite gate: Worldfire is regression key `worldfire` |
| provider_policy | PASS | yes | every expected provider policy is implemented (or none applies) |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Worldfire: Two clauses, two params, ONE shared resolver (ResolveExileAllAndSetLife, SpellEffects.h) at the instant/sorcery resolution site of both worlds (EffectHandler::ResolveImpl / TurnSolver apply_one), last before the spell moves to the graveyard -- so Worldfire, on the stack while it resolves, is not exiled by itself and lands in the emptied graveyard (CR 608.2n). EXILE (exile_all_permanents_hands_graveyards): every permanent on both sides (our lands with storage/depletion counters, a Lotus Bloom in play, creatures; the opponent's lands and scheduled spawns) goes to the shared exile as its printed card; tokens cease to exist; every card in every hand (ours, the opponent's, 2HG's second head) and both graveyards is exiled. Cards ALREADY in exile are untouched: suspended cards keep their time counters (a Rift Bolt suspended this turn deals 3 at our next upkeep), impulse-staged cards stay castable. Floating mana survives (the pool empties only at the end of a step). Leaves-the-battlefield triggers are not fired (no permanent in this pool has one). LIFE (each_player_life_becomes 1): each player's life total becomes 1 (CR 119.5; the opposing 2HG team's one shared pool becomes 1); the opponent's loss marks it as having lost life this turn. Our own death is armed for the deck (own_death_live): at 1 life a fetchland's 1-life payment kills us, in the search exactly as in the executor.
- coverage deferral -- Lotus Bloom: Lotus Bloom has NO normal mana cost -- it can ONLY enter via SUSPEND, never be hard-cast. SUSPEND (suspend_time_counters:3): a {0} main-phase action (Action::Kind::Suspend) exiles it from hand with three time counters; a counter is removed at each of your upkeeps and when the last is removed it is CAST off suspend into play (arrive_turn = suspend_turn + 3, i.e. suspend T1 -> arrive T4). The off-suspend cast is routed through the shared CastOffSuspend site (the single home for Dragonstorm's future storm increment, so an off-suspend arrival will count +1 storm); paying {0} to suspend and sacrificing for mana are NOT casts and add no storm. WHEN to suspend is a real SEARCH decision (the multi-turn rollout brings it into play and sees the future mana). SAC-FOR-MANA (sac_for_mana_amount:3): ON DEMAND, never a plan action (USER 2026-11-04: 'Lotus bloom should be sacrificed on demand by the mana system. not a direct activation.'; IsOnDemandSacDef, docs/design/sac-for-mana-on-demand.md). The shared payer (TapForCostShared) taps + SACRIFICES it only when the cost being paid cannot be paid without it, floating THREE mana of ONE color into the turn-scoped reserve (state.floating_mana.<color>), NOT wild (wild could illegally pay a multicolor {B}{R}{G} mix); the color is the payer's choice (the pip the cost is shortest of, then the rest of the line's pips, the hand's, WUBRG), and the surplus floats for the phase's later payments. The gates credit it as three wild (OnDemandSacSupplyNow), the conservative side for a necessary condition. Modelled lockstep: executor (GameEngine::UpkeepStep arrival + the shared payer) / rollout (SimulateEndAndStartNextTurn arrival + the shared payer). 'Any target' collapse: none (it produces mana, no target). VIEWER 5h TODO: suspend timing surfaces as a plan variant (Bucket A); the sac COLOR is a new small chooser (Bucket B) -- recorded, not wired.
- coverage deferral -- Memory Lapse: Goldfish-inert: the passive opponent casts no spells, so Memory Lapse has no legal target and is never cast. A faithful dead draw.
- coverage deferral -- Seething Song: Burst-mana RITUAL: pay {2}{R} (3), add {R}{R}{R}{R}{R} = GROSS 5 RED to the turn-scoped reserve (state.floating_mana.red), net +2. Modelled via ritual_floating_mana:5 + ritual_float_color:R (colored-float variant so the red can't pay the off-color {B}/{G} pips of the Dragonstorm-only dragons; Irencrag Feat now carries it too, and Reality Spasm's untap-refloat is coloured by the sources it untaps). Instant speed is inert in the single-player goldfish. No drawback, so no max_casts_after.
- coverage deferral -- Mercadian Bazaar: STORAGE-COUNTER BATTERY (storage_land, storage_charge_mode:tap; NOT a depletion land -- never sacrificed). Shares the same engine mechanic as Dwarven Hold. CHARGE: '{T}: Put a storage counter' (its charge tap makes no other mana) is modelled as +1 storage counter at the END of any turn the land is left UNTAPPED (idle) -- weakly dominant (an idle Mercadian never wants to sit un-charged), reproducing the literal +1-per-idle-turn accumulation and earliest useful burst (turn played + 2) EXACTLY. BURST: identical to Dwarven Hold -- an untapped charged storage land is a VARIABLE red source; one tap PARTIAL-bursts only the payment's remaining shortfall (the rest persist for a later turn), search-driven via plan choice, tapped LAST, reserved when unneeded, NOT sacrificed. Lockstep executor/rollout/planner; gated on storage_land -> byte-identical for non-storage decks.
- coverage deferral -- Mystical Tutor: tutor_to_top + tutor_types (Instant, Sorcery): PerformTutor shuffles, then puts the card on top. Searched target (the deck provider's TutorCandidates; every legal name in human play). As an INSTANT tutor-to-top the search also considers a paid UPKEEP cast before the draw (GameEngine::UpkeepTutorOptions, the Worldly Tutor route), so the fetched card is drawn that turn; human play offers the same upkeep window. 'Reveal' is inert (nothing reads reveals).
- coverage deferral -- Ponder: Modelled as cast_reorder 3 then draw 1 (NOT scry -- Ponder cannot bottom cards): keep all three on top in a chosen order and draw the first, OR shuffle the library and draw fresh (ReorderTopOrShuffle, shared by the executor and the rollout; the shuffle is the deterministic ShuffleAfterSearch, so the search knows the post-shuffle library -- the engine-wide known-library simplification). The DISPOSITION is SEARCHED: the Ponder order axis (TurnSolver PonderOrderAxis, under MTG_PONDER_AXIS, default ON) fans one plan variant per disposition -- every order of the looked-at cards plus the shuffle, 3! + 1 = 7 with three cards (TopDispositionCandidates; index 0 is the provider's default = the base plan, its duplicate dropped) -- pinned through g_scripted_reorder_choice and consumed at the reorder itself, so an earlier scry look in the same plan cannot eat it. ONE Ponder is varied per plan; a second Ponder in the same plan resolves by the provider's default disposition (generic: wanted cards first by ScryKeepOnTop, shuffle only when none is wanted). A deck provider may narrow the fan only by its own rule -- ReorderKeepShuffleOnly (keep-in-the-rule's-order vs shuffle) or ReorderSingleCandidate (the rule's pick alone); the generic provider does neither. HUMAN PLAY: no plan fan; the `reorder` decision (g_play_top_chooser) at resolution offers every order and the shuffle.
- coverage deferral -- Scalding Tarn: Resolved by PerformFetch: pulls a chosen library land (FetchCandidates colour heuristic), that land enters resolving its own shock/tapped choice, pay 1 life, fetchland to graveyard. Keeps a W/U/B/R/G `produces` ONLY so a copy in HAND counts as a flexible colour source for fixing heuristics (FiveColour's Island/Mountain-typed targets -- Steam Vents, Stomping Ground, Blood Crypt, Breeding Pool, Jetmir's Garden, Zagoth Triome, basic Island/Mountain -- reach all five colours); it never taps for mana. Outside Mill it searches on play; Mill may hold it on the battlefield for a later activation. 'Then shuffle' IS modelled: PerformFetch ends with ShuffleAfterSearch (CR 701.19; deterministic and lockstep in both worlds, ON by default -- MTG_NO_SEARCH_SHUFFLE is the opt-out), so the fetched land thins the library and cards a Brainstorm-style put-back left on top are shuffled away; the search sees the post-shuffle order (the engine-wide known-library simplification). A fail-to-find (no Island or Mountain card left, or the chosen target already gone) pays the life and STILL shuffles (CR 701.19b; USER 2026-10-27: 'Scalding Tarn sacrificing and finding nothing should indeed allow for a shuffle' -- WhiffShuffle, SpellEffects.h; before that date a whiff returned before the shuffle).
- coverage deferral -- Keldon Halberdier: Hard cast {4}{R}: a 4/1 first-striker. SUSPEND (suspend_time_counters 4, suspend_cost {R}): a main-phase Action::Kind::Suspend paying {R} through the shared ApplySuspend (NOT a cast, so it stays legal after Irencrag Feat's 'only one more spell'); suspended on turn T it is cast off suspend at the T+4 upkeep through the shared CastOffSuspend (a CAST: storm +1, and Irencrag's restriction can strand it), entering with HASTE (CR 702.62a -- the first creature with suspend; the keyword is set on the permanent at the single arrival path, so it attacks the turn it arrives in both worlds). Suspended cards survive Worldfire. 'You MAY cast it' is collapsed to CAST [PROVISIONAL
- coverage deferral -- Brainstorm: draw_spell draw 3 + put_back_from_hand 2: the shared ResolvePutBackFromHand (SpellEffects.h) runs INSIDE the resolution in both worlds (EffectHandler::ResolveDrawSpell, TurnSolver apply_one's DrawSpell branch), right after the three draws and before anything else -- the draw breakpoint, and every 'whenever you draw' trigger, which waits for the spell to finish resolving (CR 603.3) and so finds the two cards on top. WHICH two and in WHAT order is SEARCHED (Plan::putback_choice): one plan variant per candidate of PutBackCandidates -- every distinct ordered selection of hand cards, identical copies folded (identity class: same card, same staged/impulse state, same side of the live m1_hand snapshot), PROVIDER-ORDERED by DecisionProvider::PutBackRanking (default: the deck's cleanup shed order; candidate 0 = the two most expendable, the less expendable on top) and NOT width-capped. The fan width is an upper bound on the resolution's candidates (the predicted post-draw hand; a same-plan shuffle/tutor uses the class-blind bound); a too-wide index clamps onto the last candidate -- a sibling's exact apply, declined before it runs by the put-back fold (PutBackFoldMemo, MTG_PUTBACK_FOLD). Inside a breakpoint continuation the pin rides ContPins. ONE put-back is pinned per plan; a second Brainstorm in the same plan takes the provider default (the Ponder axis's shape). HUMAN PLAY: the `dig` chooser is asked twice ('Brainstorm (put back: top)', then '(put back: second)'); mandatory, -1 takes the default. INSTANT TIMING (PROVISIONAL fold): cast only in a main phase with an empty stack. Against a passive opponent nothing happens between windows except our own steps: an upkeep (pre-draw) Brainstorm is weakly dominated by the main-phase one (the main-phase cast can reproduce its exact hand and library -- put back the would-be draw step card and the same second card, in the same order -- and also sees one more card); an end-of-turn/opponent's-turn cast equals a cast at the end of our own turn, i.e. a post-combat main, which this deck does not play (main-2 placement is a USER question); combat adds information only through a combat-damage draw trigger (Curiosity on an attacker, a Tandem Lookout pair), where a post-combat Brainstorm could differ -- but with those pieces out the Niv-Mizzet loop is already live. 'In response' windows gain nothing (our own spells only).
- coverage deferral -- Flooded Strand: REAL battlefield fetchland (the Arid Mesa / Wooded Foothills model): its entry and its searched activation are separate decisions -- it may be held on the battlefield and cracked in a later main, and (the engine; SpiritsProvider DECLINES the hold by default since 2026-11-14 -- USER: "we should drop the fetch hold. It isn't helpful here." -- MTG_SPIRITS_NO_FETCH_HOLD=0 restores it) where a provider keeps the hold, the crack-on-play variants are offered BESIDE it, one per target, so a Strand played this turn can find a land that pays for this main's casts (FetchCrackOnPlayOffered). A Strand already HELD on the battlefield may be cracked FIRST to pay for a cast in the same main: the crack-first plans are priced on the post-crack board, one per pick (AppendHeldFetchCrackFirstPlans; docs/design/held-fetch-crack-then-cast-hole.md, FIXED 2026-11-13). The activation pays 1 life and sacrifices this land as costs, then searches a Plains or Island card (in the Spirits list: Plains, Island, Hallowed Fountain, Godless Shrine = Plains Swamp, Watery Grave = Island Swamp; the pick is searched, LandSearchPicks) and shuffles. The fetched shock land resolves its own pay-2-or-tapped choice as it enters. It cannot tap for mana. Instant-speed activation is collapsed to our searched main-phase activation slots (against the passive opponent a crack at the opponent's end step is equivalent to one in our next main before anything is cast, minus the draw information, so the main-phase slot weakly dominates).
- coverage deferral -- Personal Tutor: tutor_to_top + tutor_types (Sorcery): PerformTutor shuffles, then puts the card on top. Searched target (the deck provider's TutorCandidates; every legal name in human play). A SORCERY, so main phase only (no upkeep window). 'Reveal' is inert (nothing reads reveals).
- coverage deferral -- Overmaster: draw_spell draw 1. The can't-be-countered rider is goldfish-INERT [PROVISIONAL deferral
- coverage deferral -- Irencrag Feat: A burst-mana RITUAL: pay {1}{R}{R}{R} (4), add seven {R} to the turn-scoped reserve (state.floating_mana.red) -- net +3 mana and a red fix. Modelled via ritual_floating_mana:7 + ritual_float_color:R; the planner credits the SAME 7 it floats so the predicted combo and the executed one stay in lockstep. The COLOUR is load-bearing: with the default (empty = WILD) those seven paid ANY pip, so Hinata {1}{U}{R}{W} was castable off a mono-red board with no white or blue source at all -- the user's 'Hinata is played without having the right colours' report. Red mana pays red and generic pips only. DRAWBACK 'you can cast only one more spell this turn' modelled via max_casts_after:1 -- after Irencrag resolves the active player may cast at most ONE more spell, so the only legal combo shape is (other rituals/setup) -> Irencrag -> Crackle (Crackle = the one more spell). Without this the search could illegally chain Irencrag -> Reality Spasm -> Crackle. Needs no Hinata (no {X} to discount).
- coverage deferral -- Mistrise Village: Check land: enters untapped iff a controlled land has subtype Mountain or Forest (checkland_subtypes, the shared LandWouldEnterTapped predicate -- Steam Vents is a Mountain). Taps for {U}. The {U},{T} can't-be-countered activation is goldfish-INERT [PROVISIONAL deferral
- fetch hold review -- Scalding Tarn (hold modelled), Flooded Strand (hold modelled); record the provider timing policy in the deck analysis
- allowlisted divergence -- Wanderwine Prophets [keywords]: Champion is modeled by champion_merfolk with linked exile, return, sacrifice if no champion, and searched human choice. The engine has no generic cham
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately 
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- allowlisted divergence -- Bloodline Keeper [keywords]: Transforming DFC (Bloodline Keeper // Lord of Lineage): Scryfall reports CARD-LEVEL keywords for both faces (Flying, Transform). Transform is the layo
- allowlisted divergence -- Lord of Lineage [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the layout's Transform; the back face carries no transform of its own (transform_fr
- allowlisted divergence -- Emrakul, the Aeons Torn [keywords]: 'Protection from spells that are one or more colors' is a real keyword but INERT here (PROVISIONAL, ledger Q12): nothing in a goldfish game targets, d
- oracle_text advisory -- Deeproot Pilgrimage: oracle_text diverges (similarity 0.55); scryfall='Whenever one or more nontoken Merfolk you control become tapped, create a 1/1 blue Merfolk creature token with
- oracle_text advisory -- Svyelun of Sea and Sky: oracle_text diverges (similarity 0.73); scryfall='Svyelun has indestructible as long as you control at least two other Merfolk.\nWhenever Svyelun attacks, draw 
- oracle_text advisory -- Wanderwine Prophets: oracle_text diverges (similarity 0.11); scryfall='Champion a Merfolk (When this enters, sacrifice it unless you exile another Merfolk you control. When this lea
- oracle_text advisory -- Decimator of the Provinces: oracle_text diverges (similarity 0.22); scryfall="Emerge {6}{G}{G}{G} (You may cast this spell by sacrificing a creature and paying the emerge cost reduced by t
- oracle_text advisory -- End-Raze Forerunners: oracle_text diverges (similarity 0.15); scryfall='Vigilance, trample, haste\nWhen this creature enters, other creatures you control get +2/+2 and gain vigilance
- oracle_text advisory -- Flinthoof Boar: oracle_text diverges (similarity 0.17); scryfall='This creature gets +1/+1 as long as you control a Mountain.\n{R}: This creature gains haste until end of turn.
- oracle_text advisory -- Herald of Ilharg: oracle_text diverges (similarity 0.24); scryfall='Trample\nWhenever you cast a creature spell, put two +1/+1 counters on this creature. If that spell has mana v
- oracle_text advisory -- Herald's Horn: oracle_text diverges (similarity 0.26); scryfall="As this artifact enters, choose a creature type.\nCreature spells you cast of the chosen type cost {1} less to
- oracle_text advisory -- Ilharg, the Raze-Boar: oracle_text diverges (similarity 0.21); scryfall="Trample\nWhenever Ilharg attacks, you may put a creature card from your hand onto the battlefield tapped and a
- oracle_text advisory -- Raggadragga, Goreguts Boss: oracle_text diverges (similarity 0.23); scryfall='Each creature you control with a mana ability gets +2/+2.\nWhenever a creature you control with a mana ability
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Goblin Guide: oracle_text diverges (similarity 0.60); scryfall="Haste\nWhenever this creature attacks, defending player reveals the top card of their library. If it's a land 
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.31); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.15); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.21); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.\n{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.35); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.\n{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.\n{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.\nYou may choose not to untap this land during your untap step.\nAt the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.\n{T}: Put a storage counter on this land.\n{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)\nThis land enters tapped.\nWhen this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.\n{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.\nSacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.27); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.\n{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.05); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Niv-Mizzet, the Firemind: oracle_text diverges (similarity 0.16); scryfall='Flying\nWhenever you draw a card, Niv-Mizzet deals 1 damage to any target.\n{T}: Draw a card.'
- oracle_text advisory -- Niv-Mizzet, Parun: oracle_text diverges (similarity 0.23); scryfall="This spell can't be countered.\nFlying\nWhenever you draw a card, Niv-Mizzet deals 1 damage to any target.\nWh
- oracle_text advisory -- Curiosity: oracle_text diverges (similarity 0.16); scryfall='Enchant creature\nWhenever enchanted creature deals damage to an opponent, you may draw a card.'
- oracle_text advisory -- Tandem Lookout: oracle_text diverges (similarity 0.38); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)\nWhen
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.30); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.45); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)\n{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.30); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.32); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.05); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.14); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.12); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.\nFerocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample\nSpells you cast cost {1} less to cast for each target.\nSpells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —\n• Tap X target permanents.\n• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying\n{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.\nSplice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste\nOther creatures you control have haste.\nWhenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste\nWhen Karrthus enters, gain control of all Dragons, then untap all Dragons.\nOther Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.23); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying\nWhenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.\nStorm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.\nIf this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.\nWhenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.33); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample.\nWhen this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.35); scryfall='Enchant creature with another Aura attached to it\nEnchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample.\nWhenever enchanted creature deals damage, yo
- oracle_text advisory -- Hyena Umbra: oracle_text diverges (similarity 0.52); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has first strike.\nUmbra armor (If enchanted creature woul
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)\nUmbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature\nEnchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature\nEnchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature\nEnchanted creature gets +1/+0 and has flying.\n{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)\nWhenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)\nEnchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)\nOther Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.\nGoblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.\n{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.\n{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.\nWhenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.46); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.24); scryfall='First strike\nWhen this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach\nWhen this creature enters, it deals 2 damage to any target.\nChannel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.\nWhen this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.\nWhenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.\nWhenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.\n{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.15); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.26); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.16); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.33); scryfall='Flying, first strike, trample, lifelink, haste\nAt the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste\nWhenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything\nIf Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance\nThis creature gets +1/+1 for each color among permanents you control.\n{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)\n{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)\nEquip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.\n−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.\n−2: Gain control of target creature.\n−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")\n+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.34); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.03); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying\n{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.21); scryfall="Choose five. You may choose the same mode more than once.\n• Target permanent phases out.\n• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying\nWhenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.\nChoose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.39); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.\nEquip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.\nEquip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.\n{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.\nWhenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.\nEquipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.\nEquip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Paradise Mantle: oracle_text diverges (similarity 0.10); scryfall='Equipped creature has "{T}: Add one mana of any color."\nEquip {1}'
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.\nEquip {3} ({3}: Attach to target creature you control. Equip 
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)\nEquip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.\nEquip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)\nEquip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.\nEquip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.\nEquipped creature gets +1/+0 and has haste.\nEquip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.\nEquip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.\nJump — During your turn, Cid has flying.\n{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.\nWhenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike\nWhenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone 
- oracle_text advisory -- Ardenn, Intrepid Archaeologist: oracle_text diverges (similarity 0.17); scryfall='At the beginning of combat on your turn, you may attach any number of Auras and Equipment you control to targe
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.\nRemove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.43); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.\nMetalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.30); scryfall='First strike\nBalan has double strike as long as two or more Equipment are attached to it.\n{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying\nWhenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.\n{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.03); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.41); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch\nWhen this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample\nWhen this creature enters, if your life total is less than 7, your life total becomes 7.\nAs long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste\nWhen this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample\nWhen this creature dies, create three 5/5 green Wurm creature tokens with trample.\nWhen Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample\nWhenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.\nSearch your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.12); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.\n{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.12); scryfall='First strike\n{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.\n{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.\nWhenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.25); scryfall='First strike\nOther Minotaurs you control have first strike.\nAs long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample\nOther Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.\n{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.21); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.\n{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample\nWhen Atsushi dies, choose one —\n• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.\nFlying, haste\n{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.\nWhenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.\nCreature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.\n{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\n{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste\nYou may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.\n{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.\n{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.\nWhenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.\n{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.\n{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.\n{T}: Add {C}.\n{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)\n{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.\n
- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying\nWhen this creature enters, untap up to two lands.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying\nWhen this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Cryptolith Rite: oracle_text diverges (similarity 0.05); scryfall='Creatures you control have "{T}: Add one mana of any color."'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land\nEnchanted land has shroud. (It can't be the target of spells or abilities.)\nWhenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.04); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.05); scryfall="This spell can't be countered.\n{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)\n{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)\nFlash\nFlying\n{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).\n{T}: Add {C}.\n{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.\nCascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample\nCascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)\nShuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.\n{T}: Add {R}.\n{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U}.\n{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.\n{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.\nCreatures you control can't have -1/-1 counters put on them.\nCreatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.\nPersist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.\nPersist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.\nSacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch\nWhen this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)\n{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying\nWhen this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.\nWhenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {W}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {B}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {R}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{1}, {T}: Add one mana of any color.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.\nCycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.\nCycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.\nCycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and 
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\nAs this artifact enters, choose a color.\n{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample\nAbominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)\nWhen this artifact enters, draw a card.\n{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash\nFlying\nWhen this creature enters, draw a card.\nThis creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.\nAt the beginning of your 
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying\nRimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying\n{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.11); scryfall='Protection from black and from red\nWhenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.12); scryfall="First strike, lifelink\nAscend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nAs long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.\nAs long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying\nLifelink (Damage dealt by this creature also causes you to gain that much life.)\nWhenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible\nAs long as your devotion to white is less than five, Heliod isn't a creature.\nWhenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.12); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.\n
- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, it fights up to one target creature you don't control.\nEnrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — The next red or 
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample\nWhen Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.11); scryfall='Flying\nFirst strike (This creature deals combat damage before creatures without first strike.)\nLifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.10); scryfall='Whenever an Angel you control enters, you gain 4 life.\nWhenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying\nWhenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 1 life.\nWhenever an Angel you control enters, you gain 1 life.\n{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying\nAt the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying\nWhen this creature enters, you may reveal a card you own named Legion Angel from outside the game and 
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.\n−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.11); scryfall='Flying, vigilance\nEach other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample\nWhenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.\n{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Yavimaya Sapherd: oracle_text diverges (similarity 0.06); scryfall='When this creature enters, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Deathbloom Thallid: oracle_text diverges (similarity 0.05); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- The Mycotyrant: oracle_text diverges (similarity 0.18); scryfall='Trample\nThe Mycotyrant\'s power and toughness are each equal to the number of creatures you control that are 
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."\nAt the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Spore Swarm: oracle_text diverges (similarity 0.06); scryfall='Create three 1/1 green Saproling creature tokens.'
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.13); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)\nCreate two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.15); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.\nIf one or more +
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.\nAs long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender\nAt the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters 
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.\nRemove three spore counters 
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance\nWhen this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying\nWhenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that 
- oracle_text advisory -- Risen Reef: oracle_text diverges (similarity 0.15); scryfall="Whenever this creature or another Elemental you control enters, look at the top card of your library. If it's 
- oracle_text advisory -- Flamekin Harbinger: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, you may search your library for an Elemental card, reveal it, then shuffle and put 
- oracle_text advisory -- Foundation Breaker: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, you may destroy target artifact or enchantment.\nEvoke {1}{G} (You may cast this sp
- oracle_text advisory -- Fulminator Mage: oracle_text diverges (similarity 0.07); scryfall='Sacrifice this creature: Destroy target nonbasic land.'
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample\nThis creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.\nWhenever this creature enters or attacks, it deals 3 damage 
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —\n• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.17); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Smokebraider: oracle_text diverges (similarity 0.12); scryfall='{T}: Add two mana in any combination of colors. Spend this mana only to cast Elemental spells or activate abil
- oracle_text advisory -- Incandescent Soulstoke: oracle_text diverges (similarity 0.10); scryfall='Other Elemental creatures you control get +1/+1.\n{1}{R}, {T}: You may put an Elemental creature card from you
- oracle_text advisory -- Fury: oracle_text diverges (similarity 0.13); scryfall='Double strike\nWhen this creature enters, it deals 4 damage divided as you choose among any number of target c
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste\nWhenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying\n{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}\nWhen this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.\n{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.\n{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nIf you would gain life, you 
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.\n{3}{B}{B}: Creatures you control gain lifelink 
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.\n{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.\n{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying\nIf noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.\n{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)\nSearch your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nSacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nWhen this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.\nEternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nWhen this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.\nFerocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Nourish: oracle_text diverges (similarity 0.03); scryfall='You gain 6 life.'
- oracle_text advisory -- Eternal Witness: oracle_text diverges (similarity 0.13); scryfall='When this creature enters, you may return target card from your graveyard to your hand.'
- oracle_text advisory -- Elvish Harbinger: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, you may search your library for an Elf card, reveal it, then shuffle and put that c
- oracle_text advisory -- Ezuri, Renegade Leader: oracle_text diverges (similarity 0.16); scryfall='{G}: Regenerate another target Elf.\n{2}{G}{G}{G}: Elf creatures you control get +3/+3 and gain trample until 
- oracle_text advisory -- Scholar of Athreos: oracle_text diverges (similarity 0.13); scryfall='{2}{B}: Each opponent loses 1 life. You gain life equal to the life lost this way.'
- oracle_text advisory -- Brindle Boar: oracle_text diverges (similarity 0.05); scryfall='Sacrifice this creature: You gain 4 life.'
- oracle_text advisory -- Weather the Storm: oracle_text diverges (similarity 0.14); scryfall='You gain 3 life.\nStorm (When you cast this spell, copy it for each spell cast before it this turn.)'
- oracle_text advisory -- Utopia Sprawl: oracle_text diverges (similarity 0.21); scryfall='Enchant Forest\nAs this Aura enters, choose a color.\nWhenever enchanted Forest is tapped for mana, its contro
- oracle_text advisory -- Crab Umbra: oracle_text diverges (similarity 0.16); scryfall='Enchant creature\n{2}{U}: Untap enchanted creature.\nUmbra armor (If enchanted creature would be destroyed, in
- oracle_text advisory -- Staff of Domination: oracle_text diverges (similarity 0.09); scryfall='{1}: Untap this artifact.\n{2}, {T}: You gain 1 life.\n{3}, {T}: Untap target creature.\n{4}, {T}: Tap target 
- oracle_text advisory -- Galvanic Alchemist: oracle_text diverges (similarity 0.18); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Seedcradle Witch: oracle_text diverges (similarity 0.08); scryfall='{2}{G}{W}: Target creature gets +3/+3 until end of turn. Untap that creature.'
- oracle_text advisory -- Trufflesnout: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, choose one —\n• Put a +1/+1 counter on this creature.\n• You gain 4 life.'
- oracle_text advisory -- Heroes' Reunion: oracle_text diverges (similarity 0.01); scryfall='Target player gains 7 life.'
- oracle_text advisory -- Rest for the Weary: oracle_text diverges (similarity 0.05); scryfall='Target player gains 4 life.\nLandfall — If you had a land enter the battlefield under your control this turn, 
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.03); scryfall='{T}: Add one mana of any color.\n{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.\n{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.12); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike\nNoncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.\nWhenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.\nSacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until 
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.\nWhenever General Kudro or another Human you control enters, exile target 
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash\n{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.22); scryfall='First strike\nWard—Pay 3 life.\nNightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.\n{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.\n{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Waterlogged Grove: oracle_text diverges (similarity 0.11); scryfall='{T}, Pay 1 life: Add {G} or {U}.\n{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Adelbert Steiner: oracle_text diverges (similarity 0.02); scryfall='Lifelink\nAdelbert Steiner gets +1/+1 for each Equipment you control.'
- oracle_text advisory -- Adaptive Omnitool: oracle_text diverges (similarity 0.07); scryfall='Equipped creature gets +1/+1 for each artifact you control.\nWhenever equipped creature attacks, look at the t
- oracle_text advisory -- Kíli the Resourceful: oracle_text diverges (similarity 0.07); scryfall='Storied (If you control three or more artifacts, legendaries, and/or Sagas, you have an enduring story for the
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature\nEnchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature\nEnchanted creature has flying.\nAura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.\nChannel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an 
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance\nWhenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature\nWhen this Aura enters, tap enchanted creature.\nEnchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature\nEnchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.06); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.38); scryfall='Enchant creature\nEnchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature\nEnchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.\n{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- oracle_text advisory -- Linvala, Shield of Sea Gate: oracle_text diverges (similarity 0.31); scryfall="Flying\nAt the beginning of combat on your turn, if you have a full party, choose target nonland permanent an 
- oracle_text advisory -- Troyan, Gutsy Explorer: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}{U}. Spend this mana only to cast spells with mana value 5 or greater or spells with {X} in their 
- oracle_text advisory -- Detention Sphere: oracle_text diverges (similarity 0.25); scryfall="When this enchantment enters, you may exile target nonland permanent not named Detention Sphere and all other 
- oracle_text advisory -- Steel of the Godhead: oracle_text diverges (similarity 0.31); scryfall="Enchant creature\nAs long as enchanted creature is white, it gets +1/+1 and has lifelink. (Damage dealt by the
- oracle_text advisory -- Auroral Procession: oracle_text diverges (similarity 0.07); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Reborn Hope: oracle_text diverges (similarity 0.10); scryfall='Return target multicolored card from your graveyard to your hand.'
- oracle_text advisory -- Rampaging Baloths: oracle_text diverges (similarity 0.14); scryfall='Trample\nLandfall — Whenever a land you control enters, create a 4/4 green Beast creature token.'
- oracle_text advisory -- Mossborn Hydra: oracle_text diverges (similarity 0.13); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)\nThis crea
- oracle_text advisory -- Scythecat Cub: oracle_text diverges (similarity 0.16); scryfall='Trample\nLandfall — Whenever a land you control enters, put a +1/+1 counter on target creature you control. If
- oracle_text advisory -- Scute Swarm: oracle_text diverges (similarity 0.15); scryfall="Landfall — Whenever a land you control enters, create a 1/1 green Insect creature token. If you control six or
- oracle_text advisory -- Moraug, Fury of Akoum: oracle_text diverges (similarity 0.17); scryfall="Each creature you control gets +1/+0 for each time it has attacked this turn.\nLandfall — Whenever a land you 
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.\nLandfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.\nWhenever 
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.\nLandfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.\nYou may play lands from the top of your library.\nLandfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.\nRemove three 
- oracle_text advisory -- Akoum Hellhound: oracle_text diverges (similarity 0.15); scryfall='Landfall — Whenever a land you control enters, this creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Brushfire Elemental: oracle_text diverges (similarity 0.26); scryfall="Haste\nThis creature can't be blocked by creatures with power 2 or less.\nLandfall — Whenever a land you contr
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.26); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.\nSuspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.\n{T}: Add {C}.\n{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample\nWhenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.\n{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.\n{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- oracle_text advisory -- Temple of Abandon: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Deceit: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Enlightenment: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malady: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malice: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Mystery: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Plenty: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Triumph: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Zhalfirin Void: oracle_text diverges (similarity 0.74); scryfall='When this land enters, scry 1. (Look at the top card of your library. You may put that card on the bottom.)\n{
- oracle_text advisory -- Crystal Grotto: oracle_text diverges (similarity 0.39); scryfall='When this land enters, scry 1.\n{T}: Add {C}.\n{1}, {T}: Add one mana of any color.'
- oracle_text advisory -- Rumble Arena: oracle_text diverges (similarity 0.44); scryfall='Vigilance\nWhen this land enters, scry 1. (Look at the top card of your library. You may put it on the bottom.
- oracle_text advisory -- The Grey Havens: oracle_text diverges (similarity 0.24); scryfall='When The Grey Havens enters, scry 1.\n{T}: Add {C}.\n{T}: Add one mana of any color among legendary creature c
- oracle_text advisory -- Galadriel of Lothlórien: oracle_text diverges (similarity 0.24); scryfall='Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3.\nWhene
- oracle_text advisory -- Clifftop Retreat: oracle_text diverges (similarity 0.37); scryfall='This land enters tapped unless you control a Mountain or a Plains.\n{T}: Add {R} or {W}.'
- oracle_text advisory -- Furycalm Snarl: oracle_text diverges (similarity 0.31); scryfall="As this land enters, you may reveal a Mountain or Plains card from your hand. If you don't, this land enters t
- oracle_text advisory -- Steelshaper's Gift: oracle_text diverges (similarity 0.18); scryfall='Search your library for an Equipment card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Swiftblade Vindicator: oracle_text diverges (similarity 0.27); scryfall="Double strike (This creature deals both first-strike and regular combat damage.)\nVigilance (Attacking doesn't
- oracle_text advisory -- Lizard Blades: oracle_text diverges (similarity 0.11); scryfall="Double strike\nEquipped creature has double strike.\nReconfigure {2} ({2}: Attach to target creature you contr
- oracle_text advisory -- Magnetic Theft: oracle_text diverges (similarity 0.04); scryfall="Attach target Equipment to target creature. (Control of the Equipment doesn't change.)"
- oracle_text advisory -- Forge Anew: oracle_text diverges (similarity 0.09); scryfall='When this enchantment enters, return target Equipment card from your graveyard to the battlefield.\nDuring you
- oracle_text advisory -- Raubahn, Bull of Ala Mhigo: oracle_text diverges (similarity 0.07); scryfall="Ward—Pay life equal to Raubahn's power.\nWhenever Raubahn attacks, attach up to one target Equipment you contr
- oracle_text advisory -- Speaker of the Heavens: oracle_text diverges (similarity 0.16); scryfall="Vigilance, lifelink (Attacking doesn't cause this creature to tap. Damage dealt by this creature also causes y
- oracle_text advisory -- Path to Exile: oracle_text diverges (similarity 0.12); scryfall='Exile target creature. Its controller may search their library for a basic land card, put that card onto the b
- oracle_text advisory -- Sulfur Falls: oracle_text diverges (similarity 0.27); scryfall='This land enters tapped unless you control an Island or a Mountain.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Gitaxian Probe: oracle_text diverges (similarity 0.16); scryfall="({U/P} can be paid with either {U} or 2 life.)\nLook at target player's hand.\nDraw a card."
- oracle_text advisory -- Generator Servant: oracle_text diverges (similarity 0.15); scryfall='{T}, Sacrifice this creature: Add {C}{C}. If any of that mana is spent on a creature spell, it gains haste unt
- oracle_text advisory -- Brainstorm: oracle_text diverges (similarity 0.07); scryfall='Draw three cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Legion Lieutenant: oracle_text diverges (similarity 0.11); scryfall='Other Vampires you control get +1/+1.'
- oracle_text advisory -- Vampire of the Dire Moon: oracle_text diverges (similarity 0.37); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nLifelink (Damage dealt by
- oracle_text advisory -- Falkenrath Pit Fighter: oracle_text diverges (similarity 0.11); scryfall='{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn.
- oracle_text advisory -- Stromkirk Condemned: oracle_text diverges (similarity 0.15); scryfall='Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn.'
- oracle_text advisory -- Voldaren Estate: oracle_text diverges (similarity 0.35); scryfall='{T}: Add {C}.\n{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.\n{5},
- oracle_text advisory -- Bloodghast: oracle_text diverges (similarity 0.19); scryfall="This creature can't block.\nThis creature has haste as long as an opponent has 10 or less life.\nLandfall — Wh
- oracle_text advisory -- Stromkirk Captain: oracle_text diverges (similarity 0.11); scryfall='First strike\nOther Vampire creatures you control get +1/+1 and have first strike.'
- oracle_text advisory -- Drana, Liberator of Malakir: oracle_text diverges (similarity 0.14); scryfall='Flying, first strike\nWhenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking cr
- oracle_text advisory -- Mavren Fein, Dusk Apostle: oracle_text diverges (similarity 0.14); scryfall='Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with life
- oracle_text advisory -- Vampire Socialite: oracle_text diverges (similarity 0.26); scryfall="Menace (This creature can't be blocked except by two or more creatures.)\nWhen this creature enters, if an opp
- oracle_text advisory -- Falkenrath Gorger: oracle_text diverges (similarity 0.27); scryfall="Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is equal to its
- oracle_text advisory -- Markov Baron: oracle_text diverges (similarity 0.26); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Asylum Visitor: oracle_text diverges (similarity 0.33); scryfall="At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and you lose 1 
- oracle_text advisory -- Captivating Vampire: oracle_text diverges (similarity 0.12); scryfall='Other Vampire creatures you control get +1/+1.\nTap five untapped Vampires you control: Gain control of target
- oracle_text advisory -- Bloodline Keeper: oracle_text diverges (similarity 0.12); scryfall='Flying\n{T}: Create a 2/2 black Vampire creature token with flying.\n{B}: Transform this creature. Activate on
- oracle_text advisory -- Lord of Lineage: oracle_text diverges (similarity 0.17); scryfall='Flying\nOther Vampire creatures you control get +2/+2.\n{T}: Create a 2/2 black Vampire creature token with fl
- oracle_text advisory -- Kessig Wolf Run: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {C}.\n{X}{R}{G}, {T}: Target creature gets +X/+0 and gains trample until end of turn.'
- oracle_text advisory -- Harrow: oracle_text diverges (similarity 0.19); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for up to two basic land card
- oracle_text advisory -- Explosive Vegetation: oracle_text diverges (similarity 0.29); scryfall='Search your library for up to two basic land cards, put them onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Nature's Lore: oracle_text diverges (similarity 0.19); scryfall='Search your library for a Forest card, put that card onto the battlefield, then shuffle.'
- oracle_text advisory -- Three Visits: oracle_text diverges (similarity 0.18); scryfall='Search your library for a Forest card, put it onto the battlefield, then shuffle.'
- oracle_text advisory -- Vernal Bloom: oracle_text diverges (similarity 0.14); scryfall='Whenever a Forest is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Gauntlet of Power: oracle_text diverges (similarity 0.19); scryfall='As this artifact enters, choose a color.\nCreatures of the chosen color get +1/+1.\nWhenever a basic land is t
- oracle_text advisory -- Nissa, Who Shakes the World: oracle_text diverges (similarity 0.25); scryfall='Whenever you tap a Forest for mana, add an additional {G}.\n+1: Put three +1/+1 counters on up to one target n
- oracle_text advisory -- Kalonian Hydra: oracle_text diverges (similarity 0.15); scryfall='Trample\nThis creature enters with four +1/+1 counters on it.\nWhenever this creature attacks, double the numb
- oracle_text advisory -- Garruk, Primal Hunter: oracle_text diverges (similarity 0.20); scryfall='+1: Create a 3/3 green Beast creature token.\n−3: Draw cards equal to the greatest power among creatures you c
- oracle_text advisory -- Primordial Hydra: oracle_text diverges (similarity 0.24); scryfall='This creature enters with X +1/+1 counters on it.\nAt the beginning of your upkeep, double the number of +1/+1
- oracle_text advisory -- Voracious Hydra: oracle_text diverges (similarity 0.21); scryfall="Trample\nThis creature enters with X +1/+1 counters on it.\nWhen this creature enters, choose one —\n• Double 
- oracle_text advisory -- Neverwinter Hydra: oracle_text diverges (similarity 0.29); scryfall='As this creature enters, roll X d6. It enters with a number of +1/+1 counters on it equal to the total of thos
- oracle_text advisory -- Steelbane Hydra: oracle_text diverges (similarity 0.16); scryfall='This creature enters with X +1/+1 counters on it.\n{2}{G}, Remove a +1/+1 counter from this creature: Destroy 
- oracle_text advisory -- Gargos, Vicious Watcher: oracle_text diverges (similarity 0.23); scryfall="Vigilance\nHydra spells you cast cost {4} less to cast.\nWhenever a creature you control becomes the target of
- oracle_text advisory -- Skyshroud Claim: oracle_text diverges (similarity 0.26); scryfall='Search your library for up to two Forest cards, put them onto the battlefield, then shuffle.'
- oracle_text advisory -- Martyr of Sands: oracle_text diverges (similarity 0.06); scryfall='{1}, Reveal X white cards from your hand, Sacrifice this creature: You gain three times X life.'
- oracle_text advisory -- Walking Ballista: oracle_text diverges (similarity 0.06); scryfall='This creature enters with X +1/+1 counters on it.\n{4}: Put a +1/+1 counter on this creature.\nRemove a +1/+1 
- oracle_text advisory -- Abiding Grace: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your end step, choose one —\n• You gain 1 life.\n• Return target creature card with mana v
- oracle_text advisory -- Emeria, the Sky Ruin: oracle_text diverges (similarity 0.19); scryfall='This land enters tapped.\nAt the beginning of your upkeep, if you control seven or more Plains, you may return
- oracle_text advisory -- Brainsurge: oracle_text diverges (similarity 0.09); scryfall='Draw four cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Street Wraith: oracle_text diverges (similarity 0.26); scryfall="Swampwalk (This creature can't be blocked as long as defending player controls a Swamp.)\nCycling—Pay 2 life. 
- oracle_text advisory -- Unpredictable Cyclone: oracle_text diverges (similarity 0.31); scryfall="If a cycling ability of another nonland card would cause you to draw a card, instead exile cards from the top 
- oracle_text advisory -- Curator of Mysteries: oracle_text diverges (similarity 0.16); scryfall='Flying\nWhenever you cycle or discard another card, scry 1.\nCycling {U} ({U}, Discard this card: Draw a card.
- oracle_text advisory -- Emrakul, the Aeons Torn: oracle_text diverges (similarity 0.17); scryfall="This spell can't be countered.\nWhen you cast this spell, take an extra turn after this one.\nFlying, protecti
- oracle_text advisory -- Ulamog, the Ceaseless Hunger: oracle_text diverges (similarity 0.15); scryfall='When you cast this spell, exile two target permanents.\nIndestructible\nWhenever Ulamog attacks, defending pla
- oracle_text advisory -- Ulamog, the Infinite Gyre: oracle_text diverges (similarity 0.31); scryfall='When you cast this spell, destroy target permanent.\nIndestructible\nAnnihilator 4 (Whenever this creature att
- oracle_text advisory -- Long-Term Plans: oracle_text diverges (similarity 0.10); scryfall='Search your library for a card, then shuffle and put that card third from the top.'
- oracle_text advisory -- Toxic Deluge: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, pay X life.\nAll creatures get -X/-X until end of turn.'
- oracle_text advisory -- Lord of Atlantis: oracle_text diverges (similarity 0.47); scryfall="Other Merfolk get +1/+1 and have islandwalk. (They can't be blocked as long as defending player controls an Is
- oracle_text advisory -- Master of the Pearl Trident: oracle_text diverges (similarity 0.63); scryfall="Other Merfolk creatures you control get +1/+1 and have islandwalk. (They can't be blocked as long as defending
- oracle_text advisory -- Vodalian Hexcatcher: oracle_text diverges (similarity 0.39); scryfall='Flash\nOther Merfolk you control get +1/+1.\nSacrifice a Merfolk: Counter target noncreature spell unless its 
- oracle_text advisory -- Phantasmal Image: oracle_text diverges (similarity 0.58); scryfall='You may have this creature enter as a copy of any creature on the battlefield, except it\'s an Illusion in add
- oracle_text advisory -- Cosi's Trickster: oracle_text diverges (similarity 0.38); scryfall='Whenever an opponent shuffles their library, you may put a +1/+1 counter on this creature.'
- oracle_text advisory -- Sink into Stupor: oracle_text diverges (similarity 0.58); scryfall="Return target spell or nonland permanent an opponent controls to its owner's hand."
- oracle_text advisory -- Dark Ritual: oracle_text diverges (similarity 0.04); scryfall='Add {B}{B}{B}.'
- oracle_text advisory -- Demonic Tutor: oracle_text diverges (similarity 0.21); scryfall='Search your library for a card, put that card into your hand, then shuffle.'
- oracle_text advisory -- Diabolic Intent: oracle_text diverges (similarity 0.25); scryfall='As an additional cost to cast this spell, sacrifice a creature.\nSearch your library for a card, put that card
- oracle_text advisory -- Culling the Weak: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, sacrifice a creature.\nAdd {B}{B}{B}{B}.'
- oracle_text advisory -- Lake of the Dead: oracle_text diverges (similarity 0.14); scryfall="If this land would enter, sacrifice a Swamp instead. If you do, put this land onto the battlefield. If you don
- oracle_text advisory -- Profane Tutor: oracle_text diverges (similarity 0.27); scryfall='Suspend 2—{1}{B} (Rather than cast this card from your hand, pay {1}{B} and exile it with two time counters on
- oracle_text advisory -- Shadowborn Apostle: oracle_text diverges (similarity 0.17); scryfall='A deck can have any number of cards named Shadowborn Apostle.\n{B}, Sacrifice six creatures named Shadowborn A
- oracle_text advisory -- Abhorrent Overlord: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhen this creature enters, create a number of 1/1 black Harpy creature tokens with flying equal to you
- oracle_text advisory -- Reaper from the Abyss: oracle_text diverges (similarity 0.15); scryfall='Flying\nMorbid — At the beginning of each end step, if a creature died this turn, destroy target non-Demon cre
- oracle_text advisory -- Sire of Insanity: oracle_text diverges (similarity 0.14); scryfall='At the beginning of each end step, each player discards their hand.'
- oracle_text advisory -- Jetmir, Nexus of Revels: oracle_text diverges (similarity 0.40); scryfall='Creatures you control get +1/+0 and have vigilance as long as you control three or more creatures.\nCreatures 
- oracle_text advisory -- Thrumming Stone: oracle_text diverges (similarity 0.17); scryfall='Spells you cast have ripple 4. (Whenever you cast a spell, you may reveal the top four cards of your library. 
- oracle_text advisory -- Chariot of Victory: oracle_text diverges (similarity 0.45); scryfall='Equipped creature has first strike, trample, and haste.\nEquip {1}'
- oracle_text advisory -- Voyaging Satyr: oracle_text diverges (similarity 0.13); scryfall='{T}: Untap target land.'
- oracle_text advisory -- Surrak and Goreclaw: oracle_text diverges (similarity 0.63); scryfall='Trample\nOther creatures you control have trample.\nWhenever another nontoken creature you control enters, put
- oracle_text advisory -- Nylea, God of the Hunt: oracle_text diverges (similarity 0.54); scryfall="Indestructible\nAs long as your devotion to green is less than five, Nylea isn't a creature. (Each {G} in the 
- oracle_text advisory -- Extraplanar Lens: oracle_text diverges (similarity 0.51); scryfall='Imprint — When this artifact enters, you may exile target land you control.\nWhenever a land with the same nam
- oracle_text advisory -- Horn of Greed: oracle_text diverges (similarity 0.27); scryfall='Whenever a player plays a land, that player draws a card.'
- oracle_text advisory -- Howling Mine: oracle_text diverges (similarity 0.37); scryfall="At the beginning of each player's draw step, if this artifact is untapped, that player draws an additional car
- oracle_text advisory -- Thraben Inspector: oracle_text diverges (similarity 0.26); scryfall='When this creature enters, investigate. (Create a Clue token. It\'s an artifact with "{2}, Sacrifice this toke
- oracle_text advisory -- Squadron Hawk: oracle_text diverges (similarity 0.19); scryfall='Flying\nWhen this creature enters, you may search your library for up to three cards named Squadron Hawk, reve
- oracle_text advisory -- Curious Farm Animals: oracle_text diverges (similarity 0.10); scryfall='When this creature dies, you gain 3 life.\n{2}, Sacrifice this creature: Destroy up to one target artifact or 
- oracle_text advisory -- Kami of False Hope: oracle_text diverges (similarity 0.09); scryfall='Sacrifice this creature: Prevent all combat damage that would be dealt this turn.'
- oracle_text advisory -- Enduring Innocence: oracle_text diverges (similarity 0.20); scryfall="Lifelink\nWhenever one or more other creatures you control with power 2 or less enter, draw a card. This abili
- oracle_text advisory -- Lurrus of the Dream-Den: oracle_text diverges (similarity 0.23); scryfall='Companion — Each permanent card in your starting deck has mana value 2 or less. (If this card is your chosen c
- oracle_text advisory -- Careful Study: oracle_text diverges (similarity 0.27); scryfall='Draw two cards, then discard two cards.'
- oracle_text advisory -- Faithless Looting: oracle_text diverges (similarity 0.59); scryfall='Draw two cards, then discard two cards.\nFlashback {2}{R} (You may cast this card from your graveyard for its 
- oracle_text advisory -- Purphoros, God of the Forge: oracle_text diverges (similarity 0.60); scryfall="Indestructible\nAs long as your devotion to red is less than five, Purphoros isn't a creature.\nWhenever anoth
- oracle_text advisory -- Mystic Retrieval: oracle_text diverges (similarity 0.49); scryfall='Return target instant or sorcery card from your graveyard to your hand.\nFlashback {2}{R} (You may cast this c
- oracle_text advisory -- Magus of the Bazaar: oracle_text diverges (similarity 0.22); scryfall='{T}: Draw two cards, then discard three cards.'
- oracle_text advisory -- Cephalid Coliseum: oracle_text diverges (similarity 0.62); scryfall='{T}: Add {U}. This land deals 1 damage to you.\nThreshold — {U}, {T}, Sacrifice this land: Target player draws
- oracle_text advisory -- Golgari Grave-Troll: oracle_text diverges (similarity 0.53); scryfall='This creature enters with a +1/+1 counter on it for each creature card in your graveyard.\n{1}, Remove a +1/+1
- oracle_text advisory -- Stinkweed Imp: oracle_text diverges (similarity 0.64); scryfall='Flying\nWhenever this creature deals combat damage to a creature, destroy that creature.\nDredge 5 (If you wou
- oracle_text advisory -- Life from the Loam: oracle_text diverges (similarity 0.68); scryfall='Return up to three target land cards from your graveyard to your hand.\nDredge 3 (If you would draw a card, yo
- oracle_text advisory -- Keranos, God of Storms: oracle_text diverges (similarity 0.58); scryfall="Indestructible\nAs long as your devotion to blue and red is less than seven, Keranos isn't a creature.\nReveal
- oracle_text advisory -- Mogis, God of Slaughter: oracle_text diverges (similarity 0.58); scryfall="Indestructible\nAs long as your devotion to black and red is less than seven, Mogis isn't a creature.\nAt the 
- oracle_text advisory -- Klothys, God of Destiny: oracle_text diverges (similarity 0.65); scryfall="Indestructible\nAs long as your devotion to red and green is less than seven, Klothys isn't a creature.\nAt th
- oracle_text advisory -- Ephara, God of the Polis: oracle_text diverges (similarity 0.57); scryfall="Indestructible\nAs long as your devotion to white and blue is less than seven, Ephara isn't a creature.\nAt th
- oracle_text advisory -- Phenax, God of Deception: oracle_text diverges (similarity 0.65); scryfall='Indestructible\nAs long as your devotion to blue and black is less than seven, Phenax isn\'t a creature.\nCrea
- oracle_text advisory -- Wild Defiance: oracle_text diverges (similarity 0.21); scryfall='Whenever a creature you control becomes the target of an instant or sorcery spell, that creature gets +3/+3 un
- oracle_text advisory -- Berserk: oracle_text diverges (similarity 0.25); scryfall='Cast this spell only before the combat damage step.\nTarget creature gains trample and gets +X/+0 until end of
- oracle_text advisory -- Temur Battle Rage: oracle_text diverges (similarity 0.33); scryfall='Target creature gains double strike until end of turn.\nFerocious — That creature also gains trample until end
- oracle_text advisory -- Might of Old Krosa: oracle_text diverges (similarity 0.33); scryfall='Target creature gets +2/+2 until end of turn. If you cast this spell during your main phase, that creature get
- oracle_text advisory -- Vines of Vastwood: oracle_text diverges (similarity 0.44); scryfall="Kicker {G} (You may pay an additional {G} as you cast this spell.)\nTarget creature can't be the target of spe
- oracle_text advisory -- Legion Leadership: oracle_text diverges (similarity 0.11); scryfall="Until end of turn, double target creature's power and it gains first strike."
- oracle_text advisory -- Spellskite: oracle_text diverges (similarity 0.18); scryfall='{U/P}: Change a target of target spell or ability to this creature. ({U/P} can be paid with either {U} or 2 li
- oracle_text advisory -- Blistering Firecat: oracle_text diverges (similarity 0.21); scryfall='Trample, haste\nAt the beginning of the end step, sacrifice this creature.\nMorph {R}{R} (You may cast this ca
- oracle_text advisory -- Hystrodon: oracle_text diverges (similarity 0.31); scryfall='Trample\nWhenever this creature deals combat damage to a player, you may draw a card.\nMorph {1}{G}{G} (You ma
- oracle_text advisory -- Den Protector: oracle_text diverges (similarity 0.37); scryfall="Creatures with power less than this creature's power can't block it.\nMegamorph {1}{G} (You may cast this card
- oracle_text advisory -- Fire-Lit Thicket: oracle_text diverges (similarity 0.36); scryfall='{T}: Add {C}.\n{R/G}, {T}: Add {R}{R}, {R}{G}, or {G}{G}.'
- oracle_text advisory -- Drogskol Captain: oracle_text diverges (similarity 0.25); scryfall="Flying\nOther Spirit creatures you control get +1/+1 and have hexproof. (They can't be the targets of spells o
- oracle_text advisory -- Supreme Phantom: oracle_text diverges (similarity 0.17); scryfall='Flying\nOther Spirits you control get +1/+1.'
- oracle_text advisory -- Shacklegeist: oracle_text diverges (similarity 0.17); scryfall="Flying\nThis creature can block only creatures with flying.\nTap two untapped Spirits you control: Tap target 
- oracle_text advisory -- Spirit of the Labyrinth: oracle_text diverges (similarity 0.08); scryfall="Each player can't draw more than one card each turn."
- oracle_text advisory -- Flooded Strand: oracle_text diverges (similarity 0.14); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Island card, put it onto the battlef
- oracle_text advisory -- Geist of Saint Traft: oracle_text diverges (similarity 0.23); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)\nWhenever Geist of
- oracle_text advisory -- Lingering Souls: oracle_text diverges (similarity 0.16); scryfall='Create two 1/1 white Spirit creature tokens with flying.\nFlashback {1}{B} (You may cast this card from your g
- oracle_text advisory -- Patrician Geist: oracle_text diverges (similarity 0.14); scryfall='Flying\nOther Spirits you control get +1/+1.\nSpells you cast from your graveyard cost {1} less to cast.'
- oracle_text advisory -- Mausoleum Wanderer: oracle_text diverges (similarity 0.19); scryfall="Flying\nWhenever another Spirit you control enters, this creature gets +1/+1 until end of turn.\nSacrifice thi
- oracle_text advisory -- Skyclave Apparition: oracle_text diverges (similarity 0.28); scryfall="When this creature enters, exile up to one target nonland, nontoken permanent you don't control with mana valu
- oracle_text advisory -- White Orchid Phantom: oracle_text diverges (similarity 0.21); scryfall='Flying, first strike\nWhen this creature enters, destroy up to one target nonbasic land. Its controller may se
- oracle_text advisory -- Usher of the Fallen: oracle_text diverges (similarity 0.09); scryfall='Boast — {1}{W}: Create a 1/1 white Human Warrior creature token. (Activate only if this creature attacked this
- oracle_text advisory -- Millicent, Restless Revenant: oracle_text diverges (similarity 0.16); scryfall='Affinity for Spirits (This spell costs {1} less to cast for each Spirit you control.)\nFlying\nWhenever Millic
- oracle_text advisory -- Zhur-Taa Druid: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {G}.\nWhenever you tap this creature for mana, it deals 1 damage to each opponent.'
- oracle_text advisory -- Spark Elemental: oracle_text diverges (similarity 0.46); scryfall="Trample, haste (This creature can deal excess combat damage to the player or planeswalker it's attacking. This
- oracle_text advisory -- Ball Lightning: oracle_text diverges (similarity 0.53); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)\nHaste (Th
- oracle_text advisory -- Flamekin Bladewhirl: oracle_text diverges (similarity 0.12); scryfall='As an additional cost to cast this spell, reveal an Elemental card from your hand or pay {3}.'
- oracle_text advisory -- Sunspine Lynx: oracle_text diverges (similarity 0.19); scryfall="Players can't gain life.\nDamage can't be prevented.\nWhen this creature enters, it deals damage to each playe
- oracle_text advisory -- Hellspark Elemental: oracle_text diverges (similarity 0.16); scryfall='Trample, haste\nAt the beginning of the end step, sacrifice this creature.\nUnearth {1}{R} ({1}{R}: Return thi
- oracle_text advisory -- Nova Chaser: oracle_text diverges (similarity 0.12); scryfall='Trample\nChampion an Elemental (When this enters, sacrifice it unless you exile another Elemental you control.
- oracle_text advisory -- Thunderkin Awakener: oracle_text diverges (similarity 0.17); scryfall="Haste\nWhenever this creature attacks, choose target Elemental creature card in your graveyard with toughness 
- oracle_text advisory -- Mogg Maniac: oracle_text diverges (similarity 0.31); scryfall='Whenever this creature is dealt damage, it deals that much damage to target opponent or planeswalker.'
- oracle_text advisory -- Spiteful Sliver: oracle_text diverges (similarity 0.17); scryfall='Sliver creatures you control have "Whenever this creature is dealt damage, it deals that much damage to target
- oracle_text advisory -- Nomads en-Kor: oracle_text diverges (similarity 0.11); scryfall='{0}: The next 1 damage that would be dealt to this creature this turn is dealt to target creature you control 
- oracle_text advisory -- Shaman en-Kor: oracle_text diverges (similarity 0.13); scryfall='{0}: The next 1 damage that would be dealt to this creature this turn is dealt to target creature you control 
- oracle_text advisory -- Stuffy Doll: oracle_text diverges (similarity 0.14); scryfall='Indestructible\nAs this creature enters, choose a player.\nWhenever this creature is dealt damage, it deals th
- oracle_text advisory -- Guilty Conscience: oracle_text diverges (similarity 0.13); scryfall='Enchant creature\nWhenever enchanted creature deals damage, this Aura deals that much damage to that creature.
- oracle_text advisory -- Screaming Nemesis: oracle_text diverges (similarity 0.27); scryfall="Haste\nWhenever this creature is dealt damage, it deals that much damage to any other target. If a player is d
- oracle_text advisory -- Blasphemous Act: oracle_text diverges (similarity 0.20); scryfall='This spell costs {1} less to cast for each creature on the battlefield.\nBlasphemous Act deals 13 damage to ea
- oracle_text advisory -- Chain Reaction: oracle_text diverges (similarity 0.41); scryfall='Chain Reaction deals X damage to each creature, where X is the number of creatures on the battlefield.'
- oracle_text advisory -- Loran's Escape: oracle_text diverges (similarity 0.10); scryfall='Target artifact or creature gains hexproof and indestructible until end of turn. Scry 1.'
- oracle_text advisory -- Worn Powerstone: oracle_text diverges (similarity 0.15); scryfall='This artifact enters tapped.\n{T}: Add {C}{C}.'
- oracle_text advisory -- Flooded Grove: oracle_text diverges (similarity 0.16); scryfall='{T}: Add {C}.\n{G/U}, {T}: Add {G}{G}, {G}{U}, or {U}{U}.'
- oracle_text advisory -- Hedge Maze: oracle_text diverges (similarity 0.22); scryfall='({T}: Add {G} or {U}.)\nThis land enters tapped.\nWhen this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Spawning Kraken: oracle_text diverges (similarity 0.20); scryfall='Whenever a Kraken, Leviathan, Octopus, or Serpent you control deals combat damage to a player, create a 9/9 bl
- oracle_text advisory -- Koma, World-Eater: oracle_text diverges (similarity 0.19); scryfall="This spell can't be countered.\nTrample, ward {4}\nWhenever Koma deals combat damage to a player, create four 
- oracle_text advisory -- Serpent of Yawning Depths: oracle_text diverges (similarity 0.29); scryfall="Krakens, Leviathans, Octopuses, and Serpents you control can't be blocked except by Krakens, Leviathans, Octop
- oracle_text advisory -- Lorthos, the Tidemaker: oracle_text diverges (similarity 0.24); scryfall="Whenever Lorthos attacks, you may pay {8}. If you do, tap up to eight target permanents. Those permanents don'
- oracle_text advisory -- Whelming Wave: oracle_text diverges (similarity 0.13); scryfall="Return all creatures to their owners' hands except for Krakens, Leviathans, Octopuses, and Serpents."
- oracle_text advisory -- Kiora Bests the Sea God: oracle_text diverges (similarity 0.23); scryfall="(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — Create an 8/8 bl
- oracle_text advisory -- Summon: Leviathan: oracle_text diverges (similarity 0.22); scryfall="(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — Return each crea
- oracle_text advisory -- Hullbreaker Horror: oracle_text diverges (similarity 0.12); scryfall="Flash\nThis spell can't be countered.\nWhenever you cast a spell, choose up to one —\n• Return target spell yo
- oracle_text advisory -- Brinelin, the Moon Kraken: oracle_text diverges (similarity 0.14); scryfall="When Brinelin enters and whenever you cast a spell with mana value 6 or greater, you may return target nonland
- oracle_text advisory -- Sin, Unending Cataclysm: oracle_text diverges (similarity 0.15); scryfall="Flying, trample\nAs Sin enters, remove all counters from any number of artifacts, creatures, and enchantments.
- oracle_text advisory -- Trench Gorger: oracle_text diverges (similarity 0.18); scryfall='Trample\nWhen this creature enters, you may search your library for any number of land cards, exile them, then
- oracle_text advisory -- Maze's End: oracle_text diverges (similarity 0.30); scryfall="This land enters tapped.\n{T}: Add {C}.\n{3}, {T}, Return this land to its owner's hand: Search your library f
- oracle_text advisory -- Azorius Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {W} or {U}.'
- oracle_text advisory -- Boros Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {R} or {W}.'
- oracle_text advisory -- Dimir Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U} or {B}.'
- oracle_text advisory -- Golgari Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {B} or {G}.'
- oracle_text advisory -- Gruul Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Izzet Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Orzhov Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {W} or {B}.'
- oracle_text advisory -- Rakdos Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {B} or {R}.'
- oracle_text advisory -- Selesnya Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.'
- oracle_text advisory -- Simic Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {G} or {U}.'
- oracle_text advisory -- Planar Nexus: oracle_text diverges (similarity 0.39); scryfall="This land is every nonbasic land type. (Nonbasic land types include Cave, Desert, Gate, Lair, Locus, Mine, Pow
- oracle_text advisory -- Guild Summit: oracle_text diverges (similarity 0.35); scryfall='When this enchantment enters, you may tap any number of untapped Gates you control. Draw a card for each Gate 
- oracle_text advisory -- Gates Ablaze: oracle_text diverges (similarity 0.28); scryfall='Gates Ablaze deals X damage to each creature, where X is the number of Gates you control.'
- oracle_text advisory -- Archdruid's Charm: oracle_text diverges (similarity 0.45); scryfall="Choose one —\n• Search your library for a creature or land card and reveal it. Put it onto the battlefield tap
- oracle_text advisory -- Gond Gate: oracle_text diverges (similarity 0.19); scryfall='Gates you control enter untapped.\n{T}: Add {C}.\n{T}: Add one mana of any color that a Gate you control could
- oracle_text advisory -- Exploration: oracle_text diverges (similarity 0.13); scryfall='You may play an additional land on each of your turns.'
- oracle_text advisory -- Burgeoning: oracle_text diverges (similarity 0.20); scryfall='Whenever an opponent plays a land, you may put a land card from your hand onto the battlefield.'
- oracle_text advisory -- Manabond: oracle_text diverges (similarity 0.32); scryfall='At the beginning of your end step, you may reveal your hand and put all land cards from it onto the battlefiel
- oracle_text advisory -- Amulet of Vigor: oracle_text diverges (similarity 0.10); scryfall='Whenever a permanent you control enters tapped, untap it.'
- oracle_text advisory -- Thran Portal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\nAs this land enters, choose a basic land
- oracle_text advisory -- Gateway Plaza: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped.\nWhen this land enters, sacrifice it unless you pay {1}.\n{T}: Add one mana of any co
- oracle_text advisory -- Sea Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than blue.\n{T}: Add {U} or one mana of th
- oracle_text advisory -- Manor Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than green.\n{T}: Add {G} or one mana of t
- oracle_text advisory -- Cliffgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than red.\n{T}: Add {R} or one mana of the
- oracle_text advisory -- Citadel Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than white.\n{T}: Add {W} or one mana of t
- oracle_text advisory -- Black Dragon Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than black.\n{T}: Add {B} or one mana of t
- oracle_text advisory -- The Black Gate: oracle_text diverges (similarity 0.37); scryfall="As The Black Gate enters, you may pay 3 life. If you don't, it enters tapped.\n{T}: Add {B}.\n{1}{B}, {T}: Cho
- oracle_text advisory -- Heap Gate: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.\n{1}, {T}: Add one mana of any color.\n{1}, {T}, Tap an untapped Gate you control: Create a Trea
- oracle_text advisory -- Baldur's Gate: oracle_text diverges (similarity 0.22); scryfall='{T}: Add {C}.\n{2}, {T}: Add X mana of any one color, where X is the number of other Gates you control.'
- oracle_text advisory -- Basilisk Gate: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{2}, {T}: Target creature gets +X/+X until end of turn, where X is the number of Gates you cont
- oracle_text advisory -- Talon Gates of Madara: oracle_text diverges (similarity 0.20); scryfall='When this land enters, up to one target creature phases out.\n{T}: Add {C}.\n{1}, {T}: Add one mana of any col
- oracle_text advisory -- Vexing Devil: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, any opponent may have it deal 4 damage to them. If a player does, sacrifice this cr
- oracle_text advisory -- Keldon Champion: oracle_text diverges (similarity 0.35); scryfall='Haste\nEcho {2}{R}{R} (At the beginning of your upkeep, if this came under your control since the beginning of
- oracle_text advisory -- Keldon Marauders: oracle_text diverges (similarity 0.26); scryfall='Vanishing 2 (This creature enters with two time counters on it. At the beginning of your upkeep, remove a time
- oracle_text advisory -- Flame Javelin: oracle_text diverges (similarity 0.18); scryfall="({2/R} can be paid with any two mana or with {R}. This card's mana value is 6.)\nFlame Javelin deals 4 damage 
- oracle_text advisory -- Browbeat: oracle_text diverges (similarity 0.08); scryfall='Any player may have Browbeat deal 5 damage to them. If no one does, target player draws three cards.'
- oracle_text advisory -- Goblin Fireslinger: oracle_text diverges (similarity 0.08); scryfall='{T}: This creature deals 1 damage to target player or planeswalker.'
- oracle_text advisory -- Vulshok Sorcerer: oracle_text diverges (similarity 0.06); scryfall='Haste\n{T}: This creature deals 1 damage to any target.'
- oracle_text advisory -- Cinder Pyromancer: oracle_text diverges (similarity 0.10); scryfall='{T}: This creature deals 1 damage to target player or planeswalker.\nWhenever you cast a red spell, you may un
- oracle_text advisory -- Teetering Peaks: oracle_text diverges (similarity 0.11); scryfall='This land enters tapped.\nWhen this land enters, target creature gets +2/+0 until end of turn.\n{T}: Add {R}.'
- oracle_text advisory -- Worldfire: oracle_text diverges (similarity 0.13); scryfall="Exile all permanents. Exile all cards from all hands and graveyards. Each player's life total becomes 1."
- oracle_text advisory -- Keldon Halberdier: oracle_text diverges (similarity 0.39); scryfall='First strike\nSuspend 4—{R} (Rather than cast this card from your hand, you may pay {R} and exile it with four
- oracle_text advisory -- Mystical Tutor: oracle_text diverges (similarity 0.31); scryfall='Search your library for an instant or sorcery card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Personal Tutor: oracle_text diverges (similarity 0.40); scryfall='Search your library for a sorcery card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Overmaster: oracle_text diverges (similarity 0.38); scryfall="The next instant or sorcery spell you cast this turn can't be countered.\nDraw a card."
- oracle_text advisory -- Mistrise Village: oracle_text diverges (similarity 0.41); scryfall="This land enters tapped unless you control a Mountain or a Forest.\n{T}: Add {U}.\n{U}, {T}: The next spell yo
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- play advisory -- seed 7001 gi 0: phantom plan re-offered at turn 3: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Rift Bolt', 'Seething Song', 'Seething Song'] (Irencrag Feat costs 
- play advisory -- seed 7001 gi 0: phantom plan re-offered at turn 3: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Rift Bolt', 'Seething Song', 'Seething Song'] (Irencrag Feat costs 
- play advisory -- seed 7001 gi 0: phantom plan re-offered at turn 4: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Seething Song', 'Seething Song'] (Irencrag Feat costs {1}{R}{R}{R};
- play advisory -- seed 7001 gi 0: phantom plan re-offered at turn 4: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Seething Song', 'Seething Song'] (Irencrag Feat costs {1}{R}{R}{R};
- play advisory -- seed 7001 gi 0: repeated main-phase menu at turn 5: driver passed a repeatable action cycle
- play advisory -- seed 7001 gi 0: phantom plan re-offered at turn 6: dropped ['Keldon Halberdier', 'Seething Song', 'Seething Song'] (Keldon Halberdier costs {4}{R}; only 2 mana availa
- play advisory -- seed 7001 gi 0: phantom plan re-offered at turn 6: dropped ['Keldon Halberdier', 'Seething Song', 'Seething Song'] (Keldon Halberdier costs {4}{R}; only 2 mana availa
- play advisory -- seed 7001 gi 0: phantom plan re-offered at turn 7: dropped ['Seething Song', 'Seething Song'] (Seething Song costs {2}{R}; only 1 mana available; Seething Song costs 
- play advisory -- seed 7001 gi 0: phantom plan re-offered at turn 7: dropped ['Seething Song', 'Seething Song'] (Seething Song costs {2}{R}; only 1 mana available; Seething Song costs 
- play advisory -- seed 7001 gi 1: phantom plan re-offered at turn 3: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Rift Bolt', 'Seething Song', 'Seething Song'] (Irencrag Feat costs 
- play advisory -- seed 7001 gi 1: phantom plan re-offered at turn 3: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Rift Bolt', 'Seething Song', 'Seething Song'] (Irencrag Feat costs 
- play advisory -- seed 7001 gi 1: phantom plan re-offered at turn 4: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Seething Song', 'Seething Song'] (Irencrag Feat costs {1}{R}{R}{R};
- play advisory -- seed 7001 gi 1: phantom plan re-offered at turn 4: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Seething Song', 'Seething Song'] (Irencrag Feat costs {1}{R}{R}{R};
- play advisory -- seed 7001 gi 1: repeated main-phase menu at turn 5: driver passed a repeatable action cycle
- play advisory -- seed 7001 gi 1: phantom plan re-offered at turn 6: dropped ['Keldon Halberdier', 'Seething Song', 'Seething Song'] (Keldon Halberdier costs {4}{R}; only 2 mana availa
- play advisory -- seed 7001 gi 1: phantom plan re-offered at turn 6: dropped ['Keldon Halberdier', 'Seething Song', 'Seething Song'] (Keldon Halberdier costs {4}{R}; only 2 mana availa
- play advisory -- seed 7001 gi 1: phantom plan re-offered at turn 7: dropped ['Seething Song', 'Seething Song'] (Seething Song costs {2}{R}; only 1 mana available; Seething Song costs 
- play advisory -- seed 7001 gi 1: phantom plan re-offered at turn 7: dropped ['Seething Song', 'Seething Song'] (Seething Song costs {2}{R}; only 1 mana available; Seething Song costs 
- play advisory -- seed 7001 gi 2: phantom plan re-offered at turn 3: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Rift Bolt', 'Seething Song', 'Seething Song'] (Irencrag Feat costs 
- play advisory -- seed 7001 gi 2: phantom plan re-offered at turn 3: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Rift Bolt', 'Seething Song', 'Seething Song'] (Irencrag Feat costs 
- play advisory -- seed 7001 gi 2: phantom plan re-offered at turn 4: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Seething Song', 'Seething Song'] (Irencrag Feat costs {1}{R}{R}{R};
- play advisory -- seed 7001 gi 2: phantom plan re-offered at turn 4: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Seething Song', 'Seething Song'] (Irencrag Feat costs {1}{R}{R}{R};
- play advisory -- seed 7001 gi 2: repeated main-phase menu at turn 5: driver passed a repeatable action cycle
- play advisory -- seed 7001 gi 2: phantom plan re-offered at turn 6: dropped ['Keldon Halberdier', 'Seething Song', 'Seething Song'] (Keldon Halberdier costs {4}{R}; only 2 mana availa
- play advisory -- seed 7001 gi 2: phantom plan re-offered at turn 6: dropped ['Keldon Halberdier', 'Seething Song', 'Seething Song'] (Keldon Halberdier costs {4}{R}; only 2 mana availa
- play advisory -- seed 7001 gi 2: phantom plan re-offered at turn 7: dropped ['Seething Song', 'Seething Song'] (Seething Song costs {2}{R}; only 1 mana available; Seething Song costs 
- play advisory -- seed 7001 gi 2: phantom plan re-offered at turn 7: dropped ['Seething Song', 'Seething Song'] (Seething Song costs {2}{R}; only 1 mana available; Seething Song costs 
- play advisory -- seed 7001 gi 3: phantom plan re-offered at turn 3: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Rift Bolt', 'Seething Song', 'Seething Song'] (Irencrag Feat costs 
- play advisory -- seed 7001 gi 3: phantom plan re-offered at turn 3: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Rift Bolt', 'Seething Song', 'Seething Song'] (Irencrag Feat costs 
- play advisory -- seed 7001 gi 3: phantom plan re-offered at turn 4: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Seething Song', 'Seething Song'] (Irencrag Feat costs {1}{R}{R}{R};
- play advisory -- seed 7001 gi 3: phantom plan re-offered at turn 4: dropped ['Irencrag Feat', 'Keldon Halberdier', 'Seething Song', 'Seething Song'] (Irencrag Feat costs {1}{R}{R}{R};
- play advisory -- seed 7001 gi 3: repeated main-phase menu at turn 5: driver passed a repeatable action cycle
- play advisory -- seed 7001 gi 3: phantom plan re-offered at turn 6: dropped ['Keldon Halberdier', 'Seething Song', 'Seething Song'] (Keldon Halberdier costs {4}{R}; only 2 mana availa
- play advisory -- seed 7001 gi 3: phantom plan re-offered at turn 6: dropped ['Keldon Halberdier', 'Seething Song', 'Seething Song'] (Keldon Halberdier costs {4}{R}; only 2 mana availa
- play advisory -- seed 7001 gi 3: phantom plan re-offered at turn 7: dropped ['Seething Song', 'Seething Song'] (Seething Song costs {2}{R}; only 1 mana available; Seething Song costs 
- play advisory -- seed 7001 gi 3: phantom plan re-offered at turn 7: dropped ['Seething Song', 'Seething Song'] (Seething Song costs {2}{R}; only 1 mana available; Seething Song costs 
- claude_sweep recorded at commit 70fc1c321 (HEAD 40bd2d0c5433); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->
