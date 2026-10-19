# Analysis ledger — LandfallElementals

Deck: `decks/LandfallElementals/LandfallElementals.cod` (R/G/u Elemental landfall tribal: 4 Akoum
Hellhound, 4 Brushfire Elemental, 4 Mossborn Hydra, 4 Risen Reef, 4 Flamekin Harbinger,
4 Smokebraider, 2 Fury, 1 Incandescent Soulstoke, 1 Fulminator Mage, 1 Foundation Breaker,
1 Metallic Mimic (30 creatures), 3 Aether Vial; 27 lands: 4 Secluded Courtyard, 4 Unclaimed
Territory, 4 Myriad Landscape, 4 Terramorphic Expanse, 1 Gruul Turf, 1 Fiery Islet, 1 Waterlogged
Grove, 4 Mountain, 3 Forest, 1 Island). Sideboard 2 Mulldrifter (unreachable: no wish -- not scanned). 60 cards.

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
| Smokebraider | lfe-fury | 2/3 | DONE `produces_any_combination` + `mana_only_subtype_abilities` (+ mana_only_subtype Elemental, creature_mana_only); payer/backtracker/flow oracle; `PayingAbilitySource` scope |
| Incandescent Soulstoke | lfe-fury | 2/3 | DONE lord + `tap_put_from_hand_*` riders (haste, `sac_at_end_step`); viewer `sfput` variant; opp-end-step timing PROVISIONAL (design doc) |
| Fury | lfe-fury | 3 | DONE double strike, `etb_damage_divided_creatures` (default no targets, fold), PITCH evoke `evoke_exile_color` searched per distinct red card; viewer `divide` (+allow_none) |

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

### lfe-fury report (2026-10-19, commits 91bf1268c 90dcd75ca)
Unit 1527/1528 (routing pin), scenarios 231/231 (+2 `lfe_soulstoke_put_fury_lethal`,
`lfe_smokebraider_pays_soulstoke`); 18 smoke rows on nine other decks byte-identical to GT.
Smokebraider: spell half = Elemental CREATURE spells only (Mimic in hand refused); ability half via a new
`PayingAbilitySource` scope set by `CreatureAbilityPayScope` at the put / ActivatePermAbility /
ActivatePump sites; surplus floats in the restricted reserve (`RestrictedFloatUsableNow`, generalising
`floating_bigspell_mana`); whole-turn prepay declines while such a source is live (`PP_SUBTYPE_MANA`).
Soulstoke: reuses Stoneforge's `PutFromHandAbility`; `Permanent::sac_at_end_step` swept by
`PerformEndStepDelayedSacrifices` (dies triggers fire); put stamps this-turn attack power (put Fury = 8);
`DetectDecisionProvider`'s Equipment signal now requires the put to name Equipment.
Fury: `ResolveEtbDividedCreatureDamage` from `FireOwnEtbTriggers`; provider hook
`DecisionProvider::EtbDividedDamageAlloc` (default: no targets unless an opponent-death payoff is out);
pitch evoke one variant per distinct red card (`Action::pitch_name/pitch_slot`, subset guard
kDupPitch); pitch evoke and a post-combat put rank strictly below passing (probe: wasted evokes 21 -> 0).
`sizeof(Permanent)` 304 -> 312.
PROVISIONAL (user sign-off): Smokebraider refuses a noncreature Elemental (Kindred) spell -- none in
cards.json; Soulstoke's instant-speed timing (`docs/design/opponent-end-step-window.md`: a between-turns
decision node, ~2-3 days, needs USER rulings on fan-out reach -- the deck is UNDER-rated where that line
is better); Fury -- planeswalkers not in the target set (none exist), ETB-before-evoke-sacrifice order
fixed (no card here cares). Inert-param classifications added to audit_viewer_decisions.py.
Side observations (not this deck): Shriekmaw / Reveillark evoke variants still carry full creature
value (the overvaluation fixed for Fury); possible Angels hole -- whole-turn prepay has no paying-spell
identity, so Giada's mana MAY pay a non-Angel in a batch (UNVERIFIED).
Integration (orchestrator, 2026-10-19): 5 conflict hunks, all independent additions (both evoke branches
in CollectActions split into two `else if`s; EtbOptionalDestroyTargetPick + EtbDividedDamageAlloc; the
two SpellEffects resolvers; INERT_PARAMS). `lfe` signature += Smokebraider's mana_only_subtype_abilities.
`LookTopEntrantDef` already covers `PutFromHandAbility` (card_name = the put card), so a Soulstoke put is
an entrant for Reef's look axis and the site-10 breakpoint clause.

## Stage 3 / 2d-bis / 4 (2026-10-19)
- Integrated tree (land + etb + fury + provider + seam tests): build clean, unit **1571/1571** (incl.
  4 cross-cluster seam cases in `test/unit/test_lfe_integration.cpp`), scenarios **234/234**.
- Stage 3 coverage re-run: missing [], all 22 cards `full`; every bracket note on the new cards is a
  description or one of the PROVISIONAL deferrals listed under each cluster report -- no Tier 1-3 gap.
- 2d-bis `audit_card_costs.py`: rc=0, 453/453 costed cards compared, all match Scryfall.
- Stage 4 profile (`--analyzer-seed 181018`): card scores 1000 games d5, no hand-score gate, default
  land window (1..5), `min_playable 0` (left as generated, the Minotaur / Mirrorwing precedent -- Q2),
  cost diagnostic NO_COST_INTERACTIONS. The analyzer printed a determinism WARNING (plan cache refused
  418 stores) during card scoring.

## Other decks: the two shared fixes (2026-10-19)
Integrated tree, all decks: smoke 121 unchanged / 7 changed (searched 0 slower, 5 faster; pirates,
soldiers, 2hg, critter d0); regression 174 unchanged / 4 changed (0 slower, 0 faster; play-changed at
the same score: pirates d3/d5 s3003, critter d3 s3003; critter d0). Exactly the decks lfe-etb predicted
(719f690c6 -> goblins/pirates/soldiers; 387ab019f -> critter/soldiers); goblins byte-identical.
Held-out paired A/B (base = the primary's binary, src c34f2d3 = origin 775aae82d; seeds 830000 d3 b10,
850000 d5 b20, 870000 2hg d3; 2,000 games per cell, 18,000 total): **8 faster / 0 slower, every deck's
aggregate <= 0** -- the no-greedy adoption bar is met. Smoke + regression GT ACCEPTED (full-mode, notes
carried forward), `check_gt_logs` 735 consistent / 0 stale. Overnight GT for those decks is left to the
periodic overnight run (CLAUDE.md overnight rule).
`audit_card_fields.py`: snapshot refreshed (the bulk --update hit 429s; Risen Reef, Waterlogged Grove
and 3 Temples re-fetched individually), rc=0, all 593 cards' hard fields match.

## Stage 4-bis / 5j — IN THE SUITE, all three tiers (2026-10-19)
Key `elementals` (+ smoke canary `elementals2hg`). Probe at Landfall's shape measured ~1.6 s/game worst,
so the rows were sized to the colossus gate shape + the overnight ENTRY set: smoke d0 1000 / d3 150 b10 /
d5 75 b20 / 2hg d3 25; regression d0 1000, d3 2x150, d5 2x75; overnight d0 4x2000, d3 4x400 b10, d5
4x300 b20. 21 NEW keys accepted with matching `--deck=` filters (only those keys + the header moved;
`check_gt_logs` 756 consistent). `suite_gate.py --measure`: **2,179 ms/game** (worst
`elementals_regression_d5_s2002`), 593 core-s, **0.73x galadriel (limit 3x) -> PASS**; `--require` ok.
The measurement re-ran the regression rows: all 5 byte-identical to the accepted GT (determinism).
Multi-depth (5b first read): d0 5.77-5.82, d3 4.59-4.67, d5 4.58-4.67 -- monotonic within noise
(d5 <= d3 on the overnight mean, 4.611 vs 4.635), and T4-T5 is the deck's realistic clock.
One SLOW-GAME: overnight d5 s4004 gi89 30.5 s (won T5) -- to inspect at 5c.

## Stage 5i — discard: evidence + AUTHORED PROPOSAL (USER-OWNED; not implemented)
Evidence (Stage 4, 400 games d3): **3 real cleanup sheds**, all ties (multi_optimal 1.0), verdict
STATUS_QUO_OK. The real-game prize is therefore tiny; the policy's value is doctrine + rollout
fidelity (the rollout always sheds heuristically, today by the generic max-MV rule, which sheds Fury
and Foundation Breaker first). PROPOSAL for the user (one index per shed, quota-first, net of board):
1. **LANDS** (land drops are also landfall triggers, so this deck keeps more than most): quota 2 while
   board lands < 5, else 1. Keep order inside: Myriad Landscape (3 land entries) > Terramorphic
   Expanse (2) > a land adding a colour the hand needs (R, then G, then U only with a Risen Reef in
   hand) > Gruul Turf (with a land to return) > creature-type lands > a basic of a covered colour.
2. **ACCELERATION**: Smokebraider while board mana < 5 and the hand holds an Elemental of MV >= 3;
   Aether Vial only on turns 1-2 (late, a Vial in hand is a dead card).
3. **THREATS** (catch-all, floor 2), rank: Mossborn Hydra > Brushfire Elemental > Risen Reef > Akoum
   Hellhound > Incandescent Soulstoke > Fury > Flamekin Harbinger > Metallic Mimic > Fulminator Mage >
   Foundation Breaker. STATE PROMOTIONS: Fury to the front while Soulstoke is on the battlefield (a
   put Fury = 8 damage); Flamekin Harbinger above Hellhound while a Risen Reef is out (its tutor goes to
   hand). DISTANCE-TO-PLAYABLE inside an over-full threat bucket: a Fury nowhere near 5 mana with no
   Soulstoke anywhere sheds first.
Would be `LandfallElementalsProvider::CleanupDiscardCandidates` behind `MTG_LFE_BUCKET_DISCARD`
(default ON once approved), validated by zero label regret + non-inferiority on all three tiers.

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
- Q2. `min_playable` is 0 in the generated profile (the skill suggests >= 1 for multi-colour decks; most
  decks ship 0 and Galadriel took 1 with its required piece). Mulligan territory -> yours. Default: as
  generated.
- Q3. Soulstoke's opponent-end-step timing: build the window (`docs/design/opponent-end-step-window.md`,
  ~2-3 days, three rulings needed) or ship main-phase-only? Default: main-phase only, PROVISIONAL.
- Q4. Sign off the PROVISIONAL deferrals listed in the three cluster reports above (Brushfire evasion,
  Hydra trample, Reef fixed orders / defaults, Breaker own-target narrowing, Fulminator ranking,
  Smokebraider Kindred refusal, Fury planeswalkers / trigger order) and the INERT_PARAMS rows.
- Q5. Harbinger/Reef trigger order reuses the `dig` modal (two card tiles, asked up front) rather than a
  new decision type -- OK?

## Approved deferrals
(none yet)
