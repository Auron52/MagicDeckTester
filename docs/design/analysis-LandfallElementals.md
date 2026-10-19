# Analysis ledger — LandfallElementals

Deck: `decks/LandfallElementals/LandfallElementals.cod` (R/G/u Elemental landfall tribal: 4 Akoum
Hellhound, 4 Brushfire Elemental, 4 Mossborn Hydra, 4 Risen Reef, 4 Flamekin Harbinger,
4 Smokebraider, 2 Fury, 1 Incandescent Soulstoke, 1 Fulminator Mage, 1 Foundation Breaker,
1 Metallic Mimic, 3 Aether Vial; 22 lands: 4 Secluded Courtyard, 4 Unclaimed Territory, 4 Myriad
Landscape, 4 Terramorphic Expanse, 1 Gruul Turf, 1 Fiery Islet, 1 Waterlogged Grove, 4 Mountain,
3 Forest, 1 Island). Sideboard 2 Mulldrifter (unreachable: no wish -- not scanned). 60 cards.

The user dropped the list into the primary checkout as
`decks/LandfallElementals/LandfallElementals2.cod` and asked for an analysis on 2026-10-18. The list
was COPIED here as `LandfallElementals.cod` (folder-name stem, CRLF -> LF); the user's original stays
in the primary checkout untouched.

Branch/worktree: `lfe-analysis` @ `/workspaces/MagicDeckTester2/.claude/worktrees/lfe` (cut from
origin `775aae82d`). Implementation worktrees (branched from `lfe-analysis`): `lfe-land`, `lfe-etb`,
`lfe-fury` under `.claude/worktrees/`.

**The deck's engine.** Cheap landfall bodies (Hellhound 0/1 +2/+2, Brushfire 1/1 haste +2/+2, Mossborn
Hydra doubling its +1/+1 counters) pumped by EXTRA land entries: Terramorphic Expanse / Myriad
Landscape cracks, Gruul Turf's bounce-and-replay, and Risen Reef, which on every Elemental entering
puts a top-of-library land onto the battlefield tapped (else the card to hand). Flamekin Harbinger
tutors an Elemental to the top (ordered with a Reef trigger it is a tutor-to-hand). Smokebraider pays
Elemental spells and Elemental abilities; Incandescent Soulstoke is a lord and puts an Elemental from
hand into play with haste (Fury = a hasty 4/4 double striker). Aether Vial, Metallic Mimic (chooses
Elemental) and the two creature-type lands are already implemented.

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells, blocks or attacks; it has NO lands, artifacts or
  enchantments; in 8 of every 10 games it gets passive creatures on a fixed schedule
  (`GoldFishRunner::PopulateOpponentSpawns`) that exist only as removal targets. Opponent-facing
  clauses are therefore often inert, but self-binding clauses bind us. Model the full card;
  goldfish-inert deferrals are PROVISIONAL until the user signs off.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces are SEARCHED, provider-narrowed (restrict/order only), or collapsed by a stated sound
  dominance fold -- and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic); "choose a creature type" resolves from game state
  (`DominantCreatureSubtypeId`), never a literal.
- No unbudgeted-only changes; booleans via `EnvOn`; scope a change by where it is valid, never by what
  keeps GT still.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job at a time per agent; total threads <= 24 across the box (each implementation agent:
  `MTG_BUILD_JOBS=8`, runs <= 8 threads). Other sessions share this host.
- Agents: no `cd` in compound commands, no `rm` / `rm -rf` / `git checkout --`, no `timeout`.
- Every `mtg` run uses its OWN worktree as cwd (`env -C <wt>`): cards.json and decks are read at
  runtime.

## Stage 1 — coverage (2026-10-18)
Missing (11): Risen Reef, Akoum Hellhound, Brushfire Elemental, Flamekin Harbinger, Fulminator Mage,
Mossborn Hydra, Smokebraider, Fury, Incandescent Soulstoke, Foundation Breaker, Waterlogged Grove.
Present (11): Island, Mountain, Forest, Secluded Courtyard, Myriad Landscape, Terramorphic Expanse,
Aether Vial, Unclaimed Territory, Metallic Mimic, Gruul Turf, Fiery Islet. Their bracket notes are
re-checked for this deck at Stage 3 (the creature-type lands' "any creature" simplification is exact
here only if every creature is an Elemental -- Mimic becomes one via its chosen type, and its cost is
generic).

## Stage 2 — cards (in progress)
| card | cluster | tier | status |
|---|---|---|---|
| Akoum Hellhound | lfe-land | 2 | DONE `landfall_self_pump_power/_toughness` (FireLandfallTriggers via OnLandEntered) |
| Brushfire Elemental | lfe-land | 2 | DONE same + Haste; evasion PROVISIONAL inert |
| Mossborn Hydra | lfe-land | 2/3 | DONE `enters_with_plus_counters` + `landfall_double_self_counters`; trample PROVISIONAL inert |
| Waterlogged Grove | lfe-land | 1 | DONE Fiery Islet shape |
| Risen Reef | lfe-etb | 3 | DONE `enters_look_top_land_or_hand` + enters_watch_subtypes; land/hand SEARCHED (`Plan::look_top_pins`); viewer `dig` |
| Flamekin Harbinger | lfe-etb | 1 (+3 order) | DONE tutor_to_top Elemental; Harbinger/Reef trigger ORDER searched (generic ETB tutor-to-top x look-at-top watcher); viewer `dig` (trigger order) + `tutor_etb` |
| Foundation Breaker | lfe-etb | 2 | DONE `etb_destroy_artifact_enchantment_optional` + evoke_cost; decline own Vial/Mimic dominates; viewer `dig` (may destroy) |
| Fulminator Mage | lfe-etb | 2 | DONE hybrid; `sac_outlet_destroy_nonbasic_land` (search offers it only when a death pays); viewer `target` |
| Smokebraider | lfe-fury | | |
| Incandescent Soulstoke | lfe-fury | | |
| Fury | lfe-fury | | |

### lfe-land report (2026-10-19, commits d200ac87a d23951908 39eac6b4b)
Unit 1521/1522 (the 1 = the deck-folder count routing test, fixed by the Stage 4a routing row),
scenarios 231/231 (+2: `lfe_brushfire_cast_then_land_and_crack`, `lfe_hydra_cast_then_land_counters_persist`).
Pumps/doubling sit in the shared `OnLandEntered` chokepoint (every land-entry path). Play-neutral for
other decks (new params default off). PROVISIONAL deferrals: Brushfire's power<=2 blocking restriction,
Hydra's trample (goldfish never blocks); the four new params listed INERT in audit_viewer_decisions.py.
Integration notes: Soulstoke's put must reach `FireEtbWatchers` (Hydra's entry counter); Reef's land via
`EnterLand`; the pre-play "win this turn" estimate cannot see landfall (ordering only here); `EvalCard`
gives Hellhound 0 and no expected-landfall credit (d0 casts it anyway; a provider `ComboCardValue`
candidate).

### lfe-etb report (2026-10-19, commits c95839d40 caef6e9af 719f690c6 387ab019f)
Unit 1538/1539 (same routing-row pin), scenarios 230/230 (+1 `lfe_reef_land_to_hand_open_drop`).
Reef's trigger is collected during the enter cascade and resolved when the outermost
`FireOwnEtbTriggers` unwinds (depth counter), so battlefield indices held by callers stay valid; the
land enters via `EnterLand` (tapped, landfall, not a land drop); a card to hand opens a breakpoint
(new site-10 clause in `PlanOpensBreakpoint`). `Plan` +24 bytes (kPlanDomAssertedSize 496).
Two SHARED engine fixes kept (both are rule-mandated, not judgement): 719f690c6 the tutor-index axis
fans a creature PUT's ETB tutor (Aether Vial) instead of a greedy pick (can move goblins/pirates/
soldiers); 387ab019f a self-only sac outlet's victim is the source itself, it used to pick another
creature and do nothing (can move critter/soldiers). 48-game paired spot checks: 0 changed; the tiers
measure them for real.
PROVISIONAL (user sign-off): Reef -- the entrant's OTHER own-ETBs resolve before the Reef looks (fixed
order; nothing else in the pool touches the library top on entry); Reef triggers from non-plan entries
(e.g. a landfall token) take the provider default; mid-resolution triggers resolve after older pending
ones (not strict LIFO); >16 combined look/order variants fall back to one-at-a-time variation. Breaker
-- an own artifact-creature target whose death would pay is never chosen autonomously. Fulminator --
which land it destroys follows the fixed `EtbDestroyTargetPick` ranking (only reachable with a death
payoff). Trigger order reuses the `dig` modal rather than a new decision type (user may prefer one).
Integration notes: Soulstoke's put needs `LookTopEntrantDef` + `put_tutor` if it is a new Action kind;
Fury's evoke will touch the same `CollectActions` evoke branch; slow probe games are multi-mulligan
bottoming (no keep table yet); Reef widens search by design (an exact-count optimisation is possible).

## Prep notes (orchestrator, while the clusters run)
- **Routing hazards for Stage 4a** (`DetectDecisionProvider`, src/ai/DecisionProviders.cpp): Flamekin
  Harbinger's `tutor_to_top` is a term of the `anti` (AntiLifegain) signature (the Giant Harbinger
  misroute), Aether Vial's `upkeep_adds_charge` sets `vial`, `doubles_counters` sets `fungus` (lfe-land
  told not to reuse it for Mossborn Hydra), and the four Landfall payoff params set `landfall`. The deck
  needs its own `LandfallElementalsProvider` + a signature OR'd across several of its NEW params,
  routed above all of those; derive from `DeckProvider` unless a measured reason says otherwise.
- **Stage 3 re-checks of existing cards for this deck:**
  * Terramorphic Expanse / Myriad Landscape: the "instant-speed timing collapsed to the trailing pass of
    our main phase" PROVISIONAL note. Here the landfall payoffs are EOT pumps (Hellhound, Brushfire:
    want main 1, before combat) and Mossborn Hydra's permanent counter doubling, so a main-1 crack
    weakly dominates an opponent's-turn crack. That holds unless an opponent-end-step window is built
    for Soulstoke (lfe-fury is assessing), when Myriad's {2} would compete with Soulstoke's {1}{R}.
  * Gruul Turf: the Karoo bounce is a searched plan dimension narrowed by
    `DecisionProvider::BounceSearchCandidates`. In this deck an UNCRACKED Terramorphic / Myriad is a
    valuable return (replay = an extra landfall + a later crack). Check at 5e that the generic narrowing
    does not drop it (`MTG_UNPRUNE=karoobounce` oracle diff).
  * Aether Vial's bracket note says "AI heuristic: adds counters each upkeep until reaching the most
    common creature MV in hand". CHECKED: the charge is the provider hook `WantVialCharge` (hand-aware:
    hold while a creature at the current MV is in hand, else climb toward a bigger one, else pre-charge
    toward the deck's dominant MV); the searched fan `MTG_VIAL_AXIS` is opt-in, default OFF (USER
    2026-08-30 shipped it that way; AIEngine::DecideVialCharge, EngineFlags.h VialAxisEnabled). A
    provider prune -> disclose in 6a. With Risen Reef, a Vial put is an Elemental entering (Reef
    trigger). This deck's creature MVs: 1 x8, 2 x9, 3 x10, 4 x1, 5 x2.
  * Secluded Courtyard / Unclaimed Territory "any creature" simplification: exact here iff every creature
    is an Elemental (Mimic is a Shapeshifter in hand but costs only generic {2}).
- **Suite sizing reference:** 3x budget = 8,907 ms/game (galadriel 2,969 x 3, 2026-10-18 table).
  Landfall's entry shape: smoke d0 1000 / d3 30 b10 / d5 15 b20; regression d0 1000, d3 2x40 b10,
  d5 2x20 b20; overnight d0 4x2000, d3 4x250, d5 4x150.

## Open questions for the user (collected, never blocking)
- Q1. Deck file name: the shipped copy is `LandfallElementals.cod` (the folder's name). Your original
  `LandfallElementals2.cod` is still untracked in the primary checkout; once the tracked copy lands
  there, should the `...2.cod` be removed (the viewer would otherwise see two lists in one folder)?
  Default: leave it in place until you say.

## Approved deferrals
(none yet)
