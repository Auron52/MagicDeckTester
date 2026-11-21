# Deferred findings from the Worldfire claude-play sweep (2026-11-21)

Source: the Stage 5d sweep of `decks/Worldfire/Worldfire.cod` (4 Opus players x 4 games, seed 9300001
gi 0-15, d5 b200 benchmark). The one search hole it found -- the Irencrag Feat restrictor counting a
SUSPEND as a cast, so "suspend Rift Bolt + Irencrag Feat + Worldfire" was never enumerated at any budget --
is FIXED (`RestrictorCastsOf`, `src/ai/TurnSolver.cpp`). Everything below changes no win turn and is
deferred, each item self-contained.

## 1. Identical copies are separate Ponder / scry candidates (wasted search)

`EnumerateTopDispositions` (`src/core/SpellEffects.h`) permutes card INDICES, so a look holding two copies of
one card (Seething Song, Seething Song, Overmaster) lists each distinct ORDER twice. `TopDispositionCandidates`
-- the SEARCH's candidate list -- is built from it, so the search branches on both (a sound identity fold:
library copies carry no per-object state). Scry/surveil subsets have the same duplication.

Fix: skip an option whose (name sequence kept on top, shuffle flag) was already emitted. HAZARD: claude-play
reveal indices and the scripted top pin (`g_scripted_top_choice`) index this list, so every saved reference
game whose recorded look contains duplicate copies replays a different option. Before landing: replay every
reference (`ref` sweep / verify gate), remap any shifted index in the replay checker, or version the menu.

## 2. The game does not end when a player loses in the upkeep or draw step

`GameEngine::RunTurnFrom` (`src/core/GameEngine.cpp`) calls `CheckWinCondition` after main 1 and after combat,
not after `UpkeepStep` / the draw. A suspended Rift Bolt that puts the opponent to -2 in our upkeep still
runs main 1 (claude-play asks for a main-phase decision; the benchmark plays a land). CR 704.5a: the game
ends at once. The recorded win turn is right, so outcomes do not move; digests may (the extra main's actions).
Fix in BOTH worlds (the rollout twin `TurnSolver::SimulateEndAndStartNextTurn`), then smoke/regression accept.
A main-1 kill got the same fix after an earlier sweep.

## 3. Ponder "Shuffle them away" is labelled "to the bottom"

The shuffle branch of `ReorderTopOrShuffle` passes the looked-at cards as `bottomed`, and `RevealDisposition`
(`src/core/GameLogger.cpp`) prints "to the bottom". The library IS shuffled. Viewer / game-log label only.

## 4. A skipped suspend is not reported in `dropped_casts`

When a plan's suspend cannot be paid at execution (no red source), the executor skips it legally, but neither
the plan's `drops` nor `dropped_casts` names it (seeds 9300009 T2, 9300015 T2). Protocol reporting only.

## 5. Plan labels list a suspend after Worldfire, but it runs first

The executor suspends before the Worldfire cast (correct: Worldfire would exile the hand), but the plan label
keeps rank order ("Song, Song, Worldfire, Rift Bolt (suspend)"). Misleading in the viewer; no illegal play.

## 6. The JSON game log omits suspends and some off-main actions

No suspend action type in `GameLogger`; off-suspend arrivals, upkeep Rift Bolt damage, an upkeep Mystical
Tutor cast and fetch cracks are missing from `turns[]`, and suspended cards appear in no zone. The claude-play
`events` stream has them all. (Also noted by the Stage 2 integrator.)

## 7. Target-decision replies index the array, not the entry's `index` field (protocol trap)

In a claude-play `type:"target"` decision each `legal_targets` entry carries an `"index"` (the player or
battlefield index), but the reply is the entry's POSITION in the array (`src/main.cpp` ~7930-8010). Rift Bolt
off suspend lists `[{index:1, Opponent}, {index:0, You}]`; replying 1 bolts yourself. Plans use the opposite
convention (reply = the `index` field). Fix: reply by the `index` field, or rename the field (and update
`.claude/skills/claude-play.md` and the viewer client together). Repro: seed 9300006, choices
`1,27,-1,-1,8,4,4,1,1,3,-1,-1,6,-1,-1,14,-1,-1,27,7,-1,-1,0,1`.

## 8. Plan `drops` ignore the Feat restriction and Worldfire's hand exile

The drop annotation catches some ordering cases but not a cast refused by Irencrag Feat's one-more-spell
limit (seed 9300007 T4 plan "Irencrag Feat, Seething Song, Worldfire": Worldfire silently not cast, no
`dropped_casts`, and the next frame re-offers "cast: Worldfire", which silently does nothing), nor casts
listed after Worldfire exiles the hand (seed 9300006 T5; there `dropped_reasons` IS reported). Reporting only.
