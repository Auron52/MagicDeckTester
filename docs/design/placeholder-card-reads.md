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
| `MTG_PLACEHOLDER_MANA` (ON since `MTG_FLOAT_COMMIT_SEQ`; the ETB-untap site split out as `MTG_PLACEHOLDER_MANA_ETB`, OFF) | `SpellEffects.h:23903, 31338` | the float-colour commit weighed hand pips that were all 0 -> the first-listed colour always |
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
  * ~~`MTG_PLACEHOLDER_MANA`~~ (the ritual tap-ahead / Karoo tap-in-response read, `HandPipCommitColor`):
    UNBLOCKED and ON (2026-10-29) by the SEQUENCE-aware commit, `MTG_FLOAT_COMMIT_SEQ` (below, "MANA").
    `MTG_FLOAT_COMMIT_NET` stays OFF (inert under the sequence commit).
  * `MTG_PLACEHOLDER_MANA_ETB` (the ETB-untap tap-ahead's leftover-land read, split out of MANA 2026-10-29):
    EDF s1470011 gi10 is T3 -> T4 and stays T4 at d8 b0; a sequence rule picks the same colour there, so it is
    not what unblocks this site (below, "MANA" -> "EDF").
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

## MANA: the sequence-aware float commit (MTG_FLOAT_COMMIT_SEQ, 2026-10-29)

### Mechanism of the four games that did not recover (Hinata, seed 1350001)

Each game was replayed at play settings in four arms (`base` = SEQ off / MANA off = 513e27555's defaults;
`manaold` = MANA on with the pip-total commit; `on` = SEQ + MANA; `seqonly` = SEQ alone):

| gi | base | manaold | on | seqonly |
|---|---|---|---|---|
| 57 | 7 | 8 | 7 | 7 |
| 60 | 6 | 7 | 6 | 6 |
| 103 | 6 | 7 | 6 | 6 |
| 221 | 5 | 6 | 5 | 5 |

In all four the base arm's kill turn is a Reality Spasm line, and the Spasm's own `{U}{U}` is paid out of its
tap-ahead with every source tapped. The pip-total commit sends every choice land to `{R}` there, leaving one
blue-capable unit, so the Spasm cannot be paid and the kill line does not exist in that world at any depth: the
search sees it fail in every rollout and plays a different line turns earlier (gi57 skips its T1 land, gi221 plays
Mystic Monastery instead of Cascade Bluffs + Hinata on T4). `MTG_FLOAT_COMMIT_TRACE` shows the commit:

| gi | kill turn (base) | sources tapped ahead | hand at the Spasm | first-listed (base) | pip total (manaold) | sequence (on) |
|---|---|---|---|---|---|---|
| 57 | T7: Spasm, Spasm, Irencrag Feat, Crackle | Mountain, Monastery, Reflecting Pool, Ornithopter (wild) | 2 Spasm, Feat, Crackle, Soulfire, Remand, Hinata: R 9 / U 6 | Monastery U, Pool W -> `{W}{U}{R}*`, pays | Monastery R, Pool R -> `{R}{R}{R}*`, one blue unit | Pool U (the Spasm's 2nd U), Monastery R (Feat/Crackle) -> `{U}{R}{R}*`, pays; same at the 2nd Spasm |
| 60 | T6 main 2: Spasm, Crackle | Cascade Bluffs + Forbidden Orchard (wild) | Spasm, Crackle, Soulfire: R 5 / U 2 | Bluffs U -> `{U}*`, pays | Bluffs R -> `{R}*`, one blue unit | Bluffs U (t0), pays |
| 103 | T6: Spasm, Feat, Crackle | Boilerworks (U+R), Bluffs, Monastery | Spasm, Feat, Crackle, Soulfire, Distorting Wake: R 8 | Bluffs U, Monastery U -> `{U}{U}{U}{R}`, pays | Bluffs R, Monastery R -> `{U}{R}{R}{R}`: the trace's `pays-own=0` | Monastery U (t0), Bluffs R (t2) -> `{U}{U}{R}{R}`, pays |
| 221 | T5: Spasm, Feat, Crackle | Sol Ring, Monastery, Boilerworks, Bluffs, Reflecting Pool | Spasm, Feat, Crackle, Remand: R 5 / U 3 | Monastery U, Bluffs U, Pool W, pays | all three R -> `{U}{R}{R}{R}{R}{C}{C}` | Pool U (t0), Monastery R + Bluffs R (t2) -> `{U}{U}{R}{R}{R}{C}{C}`, pays |

Count of rollout Spasm tap-aheads whose own bill the float could not pay (`[float-hand] ... pays-own=0`, every
world, one game each): gi221 T5 37 (base) vs 3,288 (manaold); gi103 T6 1 vs 3,271; gi57 T7 648 vs 4,159. The
historical first-listed commit was right in these games by accident -- Monastery and Bluffs list `{U}` first --
and wrong whenever the first-listed colour is not the pending one (Reflecting Pool lists `{W}` first).

### The fix: commit the colours the rest of the line spends, in the order it spends them

`CommitFloatChoicesBySeq` (`src/core/SpellEffects.h`, beside `FloatSourceTapCommitted`). While a line is applying,
a tap-ahead first books every fixed-colour source and then commits its CHOICE sources (one unit of 2-4 colours:
Monastery, Bluffs' net unit, Reflecting Pool, a Signet) together, by matching them to the line's pips in this
priority:

* **tier 0** -- the PENDING cast's coloured pips (the Spasm's `{U}{U}`, paid before anything untaps), net of the
  fixed float and of whatever untapped sources the tap-ahead left (a bipartite match against their colours);
* **tier 1** -- the LATER casts' pips, in the order they will be cast, that neither the fixed float's leftover nor
  the board the step gives back (the sources the ritual untaps + the untapped ones tier 0 did not spend) can make;
* **tier 2** -- the later casts' other pips, in cast order. The board could pay them, but a float unit spent on a
  pip the line will pay frees a board source that keeps every colour it makes -- so among commits that all leave
  the line payable this is the one that keeps the most options. This is the deterministic tie-break the brief
  allows, nothing more;
* a source the line has no use for falls back to the historical hand-pip pick (`HandPipCommitColor`, unchanged:
  `MTG_PLACEHOLDER_MANA` / `MTG_FLOAT_COMMIT_NET` still apply there), committed after the matched ones.

The match is an augmenting-path matching run over the pips in that order, so a pip is covered whenever any
assignment of the sources covers it together with every earlier pip: a two-colour Bluffs is never spent on a pip
a three-colour Monastery could take while the Bluffs was the only cover of a later one.

**The line.** `g_line_seq` (`LineSeq`, SpellEffects.h) holds the applying line's unpaid casts -- exactly
`LineCastCostTotal`'s filter, so the same casts the line hold (`g_line_unpaid_cost`) holds -- with their pips and
action indices. It is bound by `LineSeqScope` at every site that binds the hold from an action list (the executor's
`TakeTurn`, `ApplyPlanDirect`, `SeqWalkFixedOrder`, `SubsetPayableAsExecutedImpl`, `OrderPrepaysWholeTurnImpl`),
an entry leaves beside every `SubManaCost(g_line_unpaid_cost, ...)` of a cast payment (`LineSeqPaid`: executor
`CastSpellFromHand`, rollout `apply_one`, the walk), the breakpoint prefix-resume snapshot carries it beside the
hold (`BpPrefixSnap::line_seq`, restored under `MTG_BP_RESUME_LINE_HOLD`), and the pay-probe memo's context
(`paymemo::Ctx`) includes it. The CAST ORDER of the later entries is asked for only when a commit has a choice to
make (`LineSeqLaterOrder`, ManaPayment.cpp): the entries' actions stable-sorted by `CastOrderLess` and arranged by
the deck (`ApplyDeckCastArrangement`) -- the two steps every apply site orders a cast section by; the walk's
entries are already in its fixed order. Plan order is NOT the cast order: Hinata's plans list Irencrag Feat /
Crackle before Reality Spasm, and the deck's settled order casts Spasm first.

**Lockstep.** The pending bill is the caller's live cost (`effective` in the executor, `ec` in `apply_one` and the
walk) and the commit reads only the board, the float and the bound sequence, so the executor, the rollout and the
payability walk commit identically. **No line** (`g_line_seq` null: real resolution off a plan, the d0 runner, any
caller that binds no hold) keeps the historical per-source commit exactly, and **`MTG_FLOAT_COMMIT_SEQ=0` never
binds a sequence**, so the lever off is byte-identical (verified: 8 decks x 30-150 games on seed 1440001, digests
and search units identical to 513e27555's binary).

**Scope.** The ritual tap-ahead (`RitualTapAheadIntoFloat`, Reality Spasm) and the Karoo tap-in-response
(`FloatKarooBouncedLand`: no pending cast, and the land leaves, so nothing is given back). The ETB-untap tap-ahead
(`EtbUntapTapAheadIntoFloat`, Peregrine Drake / Cloud of Faeries) is unchanged: its pending-pip override already
serves tier 0 autonomously (`MTG_TAPAHEAD_PENDING_PIP_AUTO`), and its leftover-land read is the split-out
`MTG_PLACEHOLDER_MANA_ETB` (OFF; see "EDF" below).

**Why a rule and not a branch.** The line is known at the commit -- it is the plan being applied, and the search
already branches over lines, cast orders and X. A colour no named cast spends cannot make the line pay where the
matched colour did not, so the only freedom left is among sources the line has no use for, which keep the
historical pick. A branch would multiply every Spasm apply by |colours|^|choice sources| to re-derive what the
matching knows. The commit stays a payment-time choice (the standing mana-payment exemption); it orders nothing
the search would otherwise have branched on.

**Diagnostics.** `MTG_FLOAT_COMMIT_TRACE=1` (executor) / `=2` (every world): one `[float-seq]` line per sequence
commit (pending bill, later casts in cast order, tier sizes, each source's colour and tier) and one `[float-hand]`
line per historical ritual commit with `pays-own=` (whether the ritual's own bill comes out of the float).

**Unit tests** (`test/unit/test_float_commit_seq.cpp`; `MTG_PLACEHOLDER_MANA` forced on, the new lever at its env
default, so `MTG_FLOAT_COMMIT_SEQ=0 mtg-test -tc="FloatCommitSeq*"` runs the same cases on the old commit and all
three FAIL there):
1. Hinata + Mountain + Mystic Monastery + Reflecting Pool, a red-heavy hand: Spasm's tap-ahead floats two blue
   (old: `{R}{R}{R}`); with no line bound the historical `{R}` commit is kept.
2. The same board, the line Lightning Bolt -> Reality Spasm -> Reality Spasm: the apply casts both Spasms (old:
   neither), and `OrderPaysSequentially` (the enumeration's walk) agrees.
3. A Karoo bounce of an untapped Monastery while the line still owes Hinata `{1}{U}{R}{W}` and no other source makes
   `{W}`: the float commits `{W}` (old: `{R}`; first-listed would be `{U}`).

### Measurement

Per-game paired, `base` = `MTG_FLOAT_COMMIT_SEQ` off + `MTG_PLACEHOLDER_MANA` off (the tree's shipped defaults
otherwise) vs `on` = both on; every job at its deck's play settings, `MTG_PGO=0`, `--threads 12`, one pooled
`--batch` per run. Slower / faster are games; "no win" counts as turn 99.

| Run | Tree | Seed | Scope | Result |
|---|---|---|---|---|
| S1 | 513e27555 + fix | 1450001 | 39 decks, 40,784 games (Hinata 1,000) | **0 slower / 3 faster** (Hinata 2, EDF 1), 44 moved, units x0.9999 |
| S1 arms | same | 1450001 | Hinata + the 10 Karoo / ETB-untap decks | SEQ alone: 0 / 2 (Hinata); +`MTG_FLOAT_COMMIT_NET`: 1 Hinata game moved vs `on`, 0 / 0 |
| F-pre | 513e27555 + fix | 1460001 | Hinata 4,000 | 5 slower / 18 faster (one no-win -> T8), units x1.0035; d8 b0: gi1790, 2987, 3174, 1442 RECOVER, gi1370 UNFINISHED (both arms still running after 2 h 19 m when the session ended) |
| F | rebased (3c729a5ae + fix, MANA one lever) | 1470001 | 39 decks, 51,744 games (Hinata 4,000) | **4 slower / 16 faster**, units x1.0004: Hinata 3 / 16 (d -13), every Hinata slower game RECOVERS at d8 b0 (gi282 5, gi1417 6, gi3445 7 in both arms); **EDF 1 / 0 -- gi10 does NOT recover** (d8 b0 base 3, on 4); 37 other decks 0 / 0 |

Reference replay (`test/viewer_protocol_check.py`, every reference, SEQ + MANA on vs off): identical
category for every reference on both trees (689 refs on the rebased tree: 267 ok, 421 repaired, 1 beaten, 0
play-drift / enum-gap / board-diverged). Byte-identity of the lever off: 8 decks on seed 1440001, digests and
units equal to 513e27555's binary. Unit suite: 2,144 / 2,144 on the rebased tree.

### EDF: why the ETB-untap site is split out (`MTG_PLACEHOLDER_MANA_ETB`, OFF)

F's one non-recovering game is EDF s1470011 gi10, and it is NOT the sequence commit: EDF's tap-aheads are the
ETB-untap kind (Cloud of Faeries, Peregrine Drake), which `MTG_FLOAT_COMMIT_SEQ` does not touch, and S1's SEQ-alone
arm left every EDF game byte-identical. It is MANA's second read site, `EtbUntapTapAheadIntoFloat`'s leftover-land
commit (after the pending-pip override). The base line (committed at T1, verified to T3) is T3: Fertile Ground,
Cloud of Faeries, Cloud of Faeries, Eldrazi Displacer, then the blink loop. Conservatory (`{G}`/`{W}`) is the only
choice land the Clouds tap ahead; first-listed it banks `{G}`, which nothing in the line spends, so Displacer's
`{W}` comes from tapping Conservatory later and Mariposa Military Base -- the board's only `{C}`, the blink's
`{2}{C}` -- stays up. With the hand's real pips it banks `{W}` (Displacer, Emiel) and the T1 search finds only a T4
line (`MTG_FD_TRACE`: `win=4`); d8 b0 stays T4.

A sequence rule does not fix it. An ETB-untap step serving the line's later pips in cast order (built and
measured, `build/scratch/floatseq/etbuntap_seq_step.patch`, not shipped) also banks `{W}` -- Displacer's pip is the
first later pip Conservatory can make -- and made gi10 T4 under SEQ alone too. The likely mechanism (NOT verified:
the executor never plays the line in that world) is downstream: with `{W}` floating, Displacer's remaining generic
is paid off Mariposa's `{C}`, and the loop's colourless is gone. So the site's blocker is the payer keeping the
line's `{C}` (the blink activation is not in the line hold unless the plan names an `ActivateBlink`), or a searched
commit at the ETB-untap tap-ahead -- not a pip total vs sequence question.

### Owed (the session ended before these ran)

1. **The final A/B of the shipped configuration on EDF**: SEQ + MANA (ritual / Karoo) on, `MTG_PLACEHOLDER_MANA_ETB`
   off, vs base, seed 1470001, 4,000 games (+ an `edf_onetb` arm with the ETB read on). Manifest ready:
   `build/scratch/floatseq/G/manifest.json` (also five identity jobs: the split binary's `on` arm against F's
   per-game digests on Hinata / Mirrorwing / Melira / Bruna / Landfall, which never run the ETB-untap site).
   Expected by construction: EDF moves only through a Karoo dual tapped in response (Azorius Chancery bounces).
2. F-pre's gi1370 at d8 b0 (pre-rebase tree; not slower on F's seed).
3. GT: the play change moves smoke / regression digests on Hinata, EDF, Melira, Mirrorwing (and any deck whose
   Karoo bounces a dual); accept at landing.
