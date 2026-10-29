# Placeholder card reads (the 2026-10-29 audit)

## The defect class

Every hand / library / graveyard / exile / sideboard `Card` is built by `DeckLoader::MakePlaceholder`
from its NAME alone. Its own `m_mana_cost`, type / supertype / colour / keyword masks, `m_subtypes` and
P/T are EMPTY; the real characteristics live on the definition, `CardDatabase::LookupCached(c)->card`
(`ZoneCard`, `src/core/SpellEffects.h`). A battlefield `Permanent`'s card is not a placeholder: every
battlefield-entry path checked (73 `perm.card =` sites) copies `def.card`, falling back to the raw card
only when there is no DB entry (tokens), and a `StackEntry::source` is `def->card`. So a read of a
characteristic on a zone card that does not go through the definition reads `{0}` / "no type" / "no
subtypes", and the code built on it silently does something else.

## The audit (line numbers at 75aac228f)

Method: a scanner over `src/` for (1) direct reads `<zone>[i].FIELD`, (2) range-for / reference / copy
bindings of a zone card and their `X.FIELD` reads, (3) every function or lambda taking a `Card` that reads
a field off it, and all 402 call sites of those helpers; then each candidate read by hand.

### (a) Live bugs, fixed -- by lever

| Lever | Site | What the empty read did |
|---|---|---|
| `MTG_PLACEHOLDER_PROVIDER` (ON) | `DecisionProviders.cpp:26095-6, 26106, 26110` | Mirrorwing cleanup discard: magnet cast turn, tie-break and kept-enabler MV all 0 |
| | `DecisionProviders.cpp:3723, 3751` | Landfall cleanup discard: no hand legend ever legendary -- the legend cap and the dead-legend bucket never fired |
| | `DecisionProviders.cpp:31616` | Stompy cleanup discard: "one 1-mana dork" took the first dork of any cost |
| | `DecisionProviders.cpp:20856-59, 20885` | Prevent Damage quake X: hand creatures never counted; two-turn land never seen (both behind default-OFF levers) |
| `MTG_PLACEHOLDER_SEARCH` (ON) | `TurnSolver.cpp:3145` | M2DropLive: a staged land never opened the second-main drop |
| | `TurnSolver.cpp:42471`, `SpellEffects.h:20036` | a land arriving in combat never opened the dig / Omnitool second main |
| | `DecisionProviders.cpp:4268`, `TurnSolver.cpp:76607` | Karoo bounce: never a land in hand, so the replay fold always applied (over-collapsed candidates) |
| `MTG_PLACEHOLDER_SNOW` (ON) | `DecisionProviders.cpp:41553-56` | SnowCertGainBound found no snow permanent: an UNDER-stated gain bound, i.e. an unsound winless certificate |
| | `DecisionProviders.cpp:41876-79` | the certificate's rescue census (diagnostic counters only) |
| `MTG_PLACEHOLDER_ECHO` (OFF) | `DecisionProviders.cpp:15995` | Goblins echo "no gas": every hand cost `{0}`, so the clause fired only on a spell-less hand |
| `MTG_PLACEHOLDER_TECTONIC` (ON since 3b93a11f2's axis) | `SpellEffects.h:18566-67` | Tectonic Giant's default kept card (highest MV) tied every card at 0 -> lowest card number |
| `MTG_PLACEHOLDER_MANA` (OFF) | `SpellEffects.h:23903, 31338` | the float-colour commit weighed hand pips that were all 0 -> the first-listed colour always |
| `MTG_PLACEHOLDER_FEATURES` (OFF) | `KeepModel.cpp:407-410, 516-517` | ExtractMidGameFeatures: lands_in_hand / hand_creatures / lib_lands / lib_creatures counted only def-built cards; hand_castable_now counted a placeholder land as a castable MV-0 spell |
| | `KeepModel.cpp:71`, `KeepModelTrainer.cpp:189-190` | keep-model subtype_density always 0; the trainer never constructed the feature |
| no lever (non-play) | `AIEngine.cpp:407` | human cast-order error never showed the real cost |
| | `AIEngine.cpp:4408` | a land-drop census counter (diagnostic) |
| | `TurnSolver.cpp:60119` | a viewer-named Land's Edge discard (`le=<land>`) reported "no land in hand" |
| | `TurnSolver.cpp:99678` | the line checker (BoardActivationIllegalReason) graded a graveyard-exile line Illegal |

### (c) Unsure / deliberately left

* `TurnSolver.cpp:63266` -- the `MTG_LEGACY_SEARCH` frozen highest-MV cleanup shed (all MVs 0). Legacy mode
  only, a frozen baseline; left as it is.
* `DecisionProviders.cpp:35521` (SigCard), `TurnSolver.cpp:16694` (HandCardContentHash),
  `SpellEffects.h:37182` (PutBackClassKey) -- identity hashes over the raw fields. Never merge distinct
  cards; may under-fold a bounced copy (real masks) with a placeholder copy of the same card.

### (b) Safe

48 zone-field reads that already have a definition fallback (`def ? def->card.X : c.X`) or no effect, and
39 helper functions / lambdas reading `Card` fields (CardHasSubtype, CardMatchesTypeName,
EffectiveActivationCost, ComputeLordBonus, GyReturnTargetLegal, ...) whose 402 call sites all pass a
definition card, a battlefield card, a `ZoneCard`'d card or a synthesized token probe.

## Measurement (base 75aac228f; held-out seed 1350001 every suite deck, + 4,000 Goblins on 1360001)

Per group, alone ON vs everything OFF (slower / faster games; d = summed turn delta; no win = turn 99):

| Group | Result |
|---|---|
| SEARCH | 0 slower / 1 faster (melira) over 32,530 games; units +0.19% |
| SNOW | play-identical (797 Snow games) |
| PROVIDER, the parts kept ON | Mirrorwing 6 moved, Landfall 2 moved, Stompy / PD 0 moved: no slower game |
| ECHO (then part of PROVIDER) | Goblins 10 / 1; with MTG_GOBLIN_ECHO_LAND_DROP 4 / 1; with MTG_GOBLIN_ECHO_DRAW_LAND 1 / 0 |
| TECTONIC (then part of PROVIDER) | Giants 4 / 0 (d +4); with MTG_TECTONIC_KEEP_LAND 3 / 2 (one no-win became a T7 win) |
| MANA | Hinata 6 / 1 (d +5); with MTG_FLOAT_COMMIT_NET 5 / 2 (d +3) |
| FEATURES | 26 / 31 (landfall 15/9, creature_giving 9/9, auras 1/0, kitty 1/5, hinata 0/5, ...) |

d8 b0 (single-game, both arms, `ignore_play_profile`) of every slower game: all 26 FEATURES games and
Hinata gi148 / gi203 RECOVER. TWELVE DO NOT, and each is in a decision the search never branches on:

| Games | Mechanism |
|---|---|
| Giants gi103, 118, 421, 976 | The Tectonic keep axis (`TECTONIC_KEEP_AXIS`, TurnSolver's tectonic fan-out) is offered only when a Tectonic Giant is on the battlefield at the plan root (`modal_present` scans `state.battlefield`). A Giant CAST this turn, hasted by Lightning Greaves, attacks under the default alone; with the real MV, highest-MV keeps Stinkdrinker Daredevil over the Mountain the turn needs (gi103: no second-main land drop, no second Giant; T5 -> T6). |
| Goblins gob4k gi405, 3725; goblins gi640, 1042 | The echo pay/decline is never searched (below). Clause (b) decides at UPKEEP, before the land drop and the draw: gi640's T3 draw was the Mountain that would have cast Goblin Chieftain, and paying the echo had already tapped two of the three lands. |
| Hinata gi57, 60, 103, 221 | The float-colour commit is a payment-time choice with no searched alternative. Need-aware by pip TOTAL, it commits every tap-ahead Mystic Monastery / Cascade Bluffs to `{R}` (Soulfire, Crackle) at Reality Spasm's tap-ahead and starves the next Spasm's `{U}{U}`; the accidental first-listed colour was `{U}`. |

## The split (what ships, what waits)

* DEFAULT ON: `MTG_PLACEHOLDER_PROVIDER` (Mirrorwing / Landfall / Stompy / Prevent Damage),
  `MTG_PLACEHOLDER_SEARCH`, `MTG_PLACEHOLDER_SNOW`, the four non-play reads (no lever), and -- since the
  keep / mode axis covers a Giant the plan casts (below) -- `MTG_PLACEHOLDER_TECTONIC`, and -- since echo is
  searched (`MTG_ECHO_AXIS`, below) -- `MTG_PLACEHOLDER_ECHO` with `MTG_GOBLIN_ECHO_LAND_DROP` (own default ON,
  now live); `MTG_GOBLIN_ECHO_DRAW_LAND` stays OFF (not re-measured with the axis).
* DEFAULT OFF, each with what unblocks it:
  * ~~`MTG_PLACEHOLDER_ECHO`~~: UNBLOCKED and ON (2026-10-29), with `MTG_GOBLIN_ECHO_LAND_DROP` (own default
    ON, inert while the parent was off). Echo is now SEARCHED (`MTG_ECHO_AXIS`); the definition cost and the
    land-drop credit make PayEchoToKeep the branch ORDER. Measured 0 / 0 with the axis in (below).
  * ~~`MTG_PLACEHOLDER_TECTONIC`~~: UNBLOCKED and ON (2026-10-29). The keep/mode axis now fans out for a
    modal Giant the plan casts that can attack this turn (`MTG_TECTONIC_CAST_AXIS`) and for every attacking
    copy (`MTG_TECTONIC_ORDINALS`), commit 3b93a11f2. With the axis in (base 513e27555, held-out s2910001
    x 6,000 Giants and s2920001 x 3,000 Giants 2HG, per-game paired): PT ON vs OFF is **0 slower / 0
    faster** (Giants) and **0 slower / 1 faster** (2HG, -1 turn); units x0.999 / x0.990. d8 b0 of
    gi103 / 118 / 421 / 976: 5 / 5 / 7 / 5 with PT ON or OFF (PT ON without the axis: 6 / 6 / 8 / 6). The
    same seeds reproduce the original finding without the axis: PT ON vs OFF 9 slower / 4 faster.
    `MTG_TECTONIC_KEEP_LAND` stays OFF: on top of PT ON with the axis, 0 / 0 (Giants), 0 slower / 1 faster
    (2HG) -- no measurable effect.
  * `MTG_PLACEHOLDER_MANA` (+ `MTG_FLOAT_COMMIT_NET`): unblocked by a SEQUENCE-aware commit (the colours the
    rest of the applying line spends, in order), not a pip total.
  * `MTG_PLACEHOLDER_FEATURES`: a USER decision. The shipped value leaves and keep models were fitted on
    the placeholder values, so turning it on moves their inputs without a refit. Options: turn it on
    together with regenerating the leaves, or record the featurizer semantics in each sidecar so each
    deck reads the semantics its model was fitted on.

## NO-GREEDY finding: echo and the other upkeep picks are not searched

`DecisionProvider::PayEchoToKeep` is a one-answer provider pick in BOTH worlds: the executor at the real
upkeep (`AIEngine::ResolveEchoUpkeep`, from GameEngine's upkeep tail, plus an idempotent backstop at the top
of the pre-combat main) -- before the turn's search exists -- and the rollout at every simulated turn
boundary (`TurnSolver.cpp` `SimulateEndAndStartNextTurn`), which `FSLineTail` calls with depth remaining.
Its own comment says "the search need not branch echo". That is the in-window greedy shape the
NO-GREEDY rule forbids, and it is why no Goblins slower game above can recover at any depth. The same
shape: `DecisionProvider::PaysCumulativeUpkeep` (Varchild's War-Riders, `PerformUpkeepCumulativeGifts`,
same two sites); Mirri's Guile's upkeep reorder (`PerformUpkeepReorder`, a fixed heuristic order); and
Defense of the Heart's put list (`SacTutorPutList`).

**Update 2026-10-29: echo is now SEARCHED** (`MTG_ECHO_AXIS`, default ON; docs/design/searched-echo.md).
`PayEchoToKeep` only orders pay vs decline. On a held-out sample (seeds 1540001 / 1550001, 8,000 Goblins
games; 1560001, 4,000 goblins2hg), `MTG_PLACEHOLDER_ECHO=1` measured **0 better / 0 worse** against
`MTG_PLACEHOLDER_ECHO=0` with the axis on. Without the axis the same lever lost 1 / 7 and 0 / 5. The
witness games s1350001 gi640 and s1360001 gi405 / gi3725 recover at d8 b0 and at play settings. The
block on that lever is lifted, and it ships ON (a correct read that now meets the bar; the USER's echo
rule in `PayEchoToKeep` stays, as the branch order).

Cumulative upkeep stays a one-option prune: paying is weakly dominant for Creature Giving's card pool
against the passive opponent. The proof, and the conditions under which it breaks, are in that doc.
Mirri's Guile (no current deck) and Defense of the Heart's put list are listed there as next items.

## Re-measurement of the split (base 67e4d62e6 = MTG_ECHO_STAGED_GAS on f145caee2)

Fresh held-out seed 1370001, every suite deck (39), one pooled batch (`--threads 12`, MTG_PGO=0 both arms):
`base` = every PLACEHOLDER lever off vs `on` = the shipped defaults (PROVIDER / SEARCH / SNOW on; ECHO /
TECTONIC / MANA / FEATURES off), both pinned per job.

| | games | slower | faster | streams moved | units |
|---|---|---|---|---|---|
| all 39 decks | 48,020 | **0** | 1 (dragons, -1 turn) | 10 (dragons 1, edf 1, landfall 4, mirrorwing 4) | 1.0018 |

No slower game, so nothing to take to d8 b0; every deck's net is <= 0. Work moved where SEARCH now opens
what the placeholder read had closed (dragons 1.037, minotaur 1.019, mirrorwing 1.008, melira 1.007, bruna
1.006); 22 decks are unit-identical.
