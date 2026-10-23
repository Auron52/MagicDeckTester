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

## Stage 4a / 5 progress (2026-10-19)
- `provider_audit.py --check`: rc=0, LandfallElementals -> LandfallElementalsProvider (cert NotAssessed).
- SLOW-GAME overnight d5 s4004 gi89 (30.5 s in the pool, 15.8 s single-thread, won T5): TWO mulligans to
  five -> every candidate bottom played out as a full game (the known pre-keep-table cost). Observation
  (Q6): the baseline STATIC keep rule mulliganed a 5-land hand with Mossborn Hydra + Foundation Breaker
  (no 2-drop: `curve_check two_drop`), then a 1-land 7 -- mulligan policy is the USER's / the keep-table
  stage's.
- 5d claude-play sweep LAUNCHED: 20 Opus agents, seeds 61000-61019 (brief `logs/lfe/sweep_prompt.md`).
- 5e first pass RUNNING: overnight d3 rows (4x400 b10) with MTG_UNPRUNED=1 vs the accepted GT wins.

## Stage 5e — narrowing vs the unpruned oracle (2026-10-19)
- `MTG_UNPRUNED=1` (every gate) on the overnight d3 rows: RSS passed the 35 GB cap and the engine aborted
  itself (the guard held). Single-game bisect (seed 6083 gi77 d3 b10): pruned 1.3 s / 340 MB, all-gates
  29 s / 4.2 GB; one gate alone reproduces it -- **`searchorder`** (the cast-order permutation ORACLE,
  analysis-only, never shipped): this deck's many cheap Elementals x land slots x Reef look pins explode
  the permutation set, and the work is not charged to the budget's units (29 s at --budget-ms 10).
  Engine-cost finding for the oracle tool, not a shipped-play defect.
- Every gate EXCEPT searchorder, overnight d3 (1,600 games): 15 faster / 17 slower, net +0.0006/game.
  At b200 (20x): 14 of the 17 unpruned-slower games recover (budget dilution); 7 of the 15
  unpruned-faster games are found by SHIPPED play at b200; the other 8 are explained by ONE gate,
  **`dig`** (each of the 8 reproduces its faster win with MTG_UNPRUNE=dig alone).
- `MTG_UNPRUNE=dig` alone, overnight d3 + d5 (2,800 games): d3 13 faster / 12 slower (-0.0013/game),
  d5 11 / 11 (-0.0008), ~+5% units. Aggregate-NEUTRAL at the shipped budget, but the generic dig gate
  (`GenericProvider::ShouldConsiderDig` / `HasAnyDigSource`: when a line may crack Waterlogged Grove /
  Fiery Islet to draw) makes some lines unfindable even at 20x budget. -> Q7 (USER: a shared provider
  prune; the completeness rule vs the provider-prune licence).

## Convergence loop 1 — sweep fixes (2026-10-19)
Two fix agents in worktrees lfe-fix-pay / lfe-fix-enum, integrated onto lfe-analysis (clean cherry-picks):
**Payer / copy accounting (119895b07; flags B C D E G H I R):** Smokebraider pays first unless general
mana covers this payment plus what the rest of the line owes that Smokebraider cannot pay
(`RestrictedMayHoldForAttack` / `RestrictedIneligOwedLater`, ManaPayment.cpp, fed by PlanContext);
the restricted wild float goes to the pip the board cannot make (`SpendRestrictedWildTowardCost`);
non-Elemental costs are priced without Smokebraider or its float (`SubtypeOnlyCtx`); a Soulstoke put
is offered only when its {1}{R} is payable for that ability; `ComputeAvailableColors` counts the
restricted float; ONE per-plan copy guard over casts, Vial puts, Soulstoke puts, Fury pitches and
Remand riders; the put checks its card before paying (both worlds); stranded activations reported to
`dropped_casts`. Menu audit (every plan on every menu of the 8 flagged seeds): dropping plans 61 -> 8
of 5,852 (the 8 = land-slot + Reef boundary, then taken up by the enum fix). Agent smoke check: byte-
identical for bruna slivers knights minotaur pirates soldiers dragonstorm fungusv2 goblins dragons
hinata burn th; ANGELS moves (Giada; one d0 game faster), elementals moves.
**Enumeration (f42d64ab4, 7dd1acdc4, 260495d72, 7f095457e, 56a295a6b, 71aa23ca2):** (1) fetch-crack
positions before library-top readers (Risen Reef looks, top tutors): `ActionReadsLibraryTop` + reader
twins and pre-cast subset masks; root plans +24% on the 61002 crack turn, +63% on a two-crack board
with a Reef; the 61002 T4 kill is now found at the shipped d5 b20. (2) land-slot probe counts every
dropped cast and decides Reef plans per plan (`PositionPrefixProbeGuard`). (3) a Foundation Breaker
evoke nothing can read is cut (`PlanHasDeadOptionalEvoke`). (4) pump event text shows full P/T.
(5) `LandfallElementalsProvider` TutorSearchWidth admits every distinct Elemental name
(`MTG_LFE_TUTOR_WIDTH`, default ON -- Q8). (#16) LOCKSTEP fix: a breakpoint continuation's pitch evoke
recorded no exiled card, so the executor declined a Fury evoke the search had scored (gi28: committed
T4, realised T5); `docs/design/rollout-executor-lockstep.md` #16. Agent measurement (seed 4004, base =
accepted GT): d3 b10 4.6200 -> 4.5575 (width 6) -> 4.5200 (width 32; vs base 39 faster / 0 slower), d5
b20 4.5967 -> 4.5400 -> 4.5067 (28 faster / 2 slower, both recover at d8 b0); width 32 costs ~1.11x units.
Integrated tree: unit **1595/1595**, scenarios **237/237**; full smoke + regression running.

### Integrated tiers + held-out A/B on the loop-1 fixes (2026-10-19)
Full smoke: 125 unchanged / 7 changed; full regression 171 / 12. Moved: elementals (all rows faster),
angels (play changes at the same avg; d0 slightly faster), bruna_regression_d0_s2002 (same avg).
Searched SLOWER games: 9, ALL elementals -- 8 recover at 4x / 16x (`classify_turn_later.sh`; 2 end
BETTER than baseline), regression d3 s2002 gi112 persists at 16x but its draws diverge from T4 (a
shuffle) and it recovers to T4 at --depth 8 --budget-ms 0. Held-out A/B (base = primary binary src
c34f2d3; seeds 880000-910000): angels d3 2000 g 0/0, d5 1000 g 0/0, d0 4000 g 8 faster / 2 slower,
bruna d0 4000 g 0 / 1, angels 2HG d3 1000 g 0 / 1 = **gi308**: base T5 / new T6 at every budget incl. d8
b0. ROOT CAUSE (pay agent, VERIFIED from base's game log): base's T5 is ILLEGAL -- on T3 it cast
Lightning Greaves {2} + Bishop of Wings {W}{W} (two non-Angels, 4 mana) from Plains, Plains, Seraph
Sanctuary ({C}) and Giada ({W} for Angel spells only); the whole-turn batch prepay's creature-only
joint solve ran without the paying spell's identity and laundered Giada's mana. No legal T5 exists
(exhaustive claude-play menu search, 52,082 runs). So T6 is the CORRECT result: a correctness fix, not
a lost line. The prepay hole is closed in 8c684037b (`SubtypeBatchScope`, the Troyan big-spell scope's
twin; the A/B is byte-identical with it -- the enumeration already kept those batches out). The 3 d0
slowdowns are greedy picks among options that are now legal / correctly paid (angels gi632, gi1424 --
Giada now pays an Angel and does not attack; bruna gi1442 -- Mother of Runes newly castable off
Somberwald Sage's creature-only float). This confirms the 'possible Angels hole' the lfe-fury agent
flagged UNVERIFIED in Stage 2.
Final binary (8c684037b): unit 1596/1596, scenarios 237/237; tiers running.

## Rebased onto origin and PUSHED (2026-10-23)
USER 2026-10-22: pull the latest and push all of this to the shared branch; the open questions get
answered later, on a less busy machine that can implement the answers. The 26 local commits were rebased
over 430 upstream commits (pre-rebase branch kept as `lfe-analysis-prerebase-0f5af89de`, local only).
Overlaps that needed real merging, not a side pick:
- **Mossborn Hydra** landed upstream 2026-10-22 as a Landfall SCREENING candidate (same param names).
  Upstream's engine (`DoublePlusCounters`, the as-enters block + `EntersWithPreappliedScope`) and card
  entry are kept; this branch's duplicate entry and its second as-enters application are dropped.
- **Provider signature** narrowed to Risen Reef's `enters_look_top_land_or_hand` + Smokebraider's
  `mana_only_subtype_abilities` (both carried by no other card): `landfall_double_self_counters` would now
  pull a Landfall list that adopts Mossborn onto this provider.
- **Self-only sac victim** (d23208168): upstream fixed the same bug; its version is kept.
- **Duplicate-use filter**: upstream's compact path (`DupCompactReject`, `MTG_DUP_SRC_SKIP`) gave bit 8 to
  the Pod entry, which `kDupPitch` used; `kDupPitch` moved to bit 9 (`kDupCopies` to 10) and the compact
  path got the pitch clause (`DupPitchReject`, shared with the full filter). Without it the default path
  would have skipped the Fury pitch guard.
- **`sac_at_end_step`** became a 1-bit field beside Vampires' bit-fields, so `sizeof(Permanent)` stays 304.
- **Fast plan signature** (`PlanSignatureFast`, `MTG_PLAN_SIG_FAST`): the Fury `#EX` pitch token was
  missing there (two pitch evokes collapsed to one plan; test_lfe_fury caught it). Fixed; every upstream
  `*_VERIFY` diagnostic armed over 60 elementals d3 games: 0 mismatches.
- **CreatureAbilityPayScope(source card)** carried onto upstream's reworked Vampires / Alchemist ability
  apply sites (executor + rollout), and the put-from-hand hold onto upstream's ActivateUntap hold.
Rebased binary: unit 1995/1995, scenarios 314/314. Smoke: searched slower 0, faster 5, play-changed 8
(angels / pirates / soldiers, the same decks the shared fixes moved before); d0 faster 1. Regression:
searched slower 0 / faster 0 / play-changed 13 (same-score), d0 one +1 and one -1 in angels d0 (net 0,
the Giada mana fix; accepted with a note); 658 references replay (0 drift), both suboptimal references
still beaten. Smoke + regression GT accepted on this binary; gt_logs consistent (1002).
**Overnight:** no elementals overnight GT exists; per the repo rule it is the next periodic overnight
run's to accept. The 2026-10-19 classification of 49 slower overnight games compared against a baseline
this rebase superseded, so it is moot.

### HAND-OFF: what is still owed (for whichever machine picks this up)
1. The user's answers to Q1-Q9 below and the discard proposal (Stage 5i) -- implement what they decide.
2. Stage 5c2 `scripts/leaf_tiebreak_check.py`, 5h `scripts/audit_viewer_decisions.py` (with the sweep),
   full `scripts/verify_deck.py decks/LandfallElementals`.
3. Record the `## Claude-play sweep` section (20 games, seeds 61000-61019: 16 tie, 2 Claude faster
   (61002, 61016: plan-space gaps), 1 Claude slower (61003: the Smokebraider payer bug) -- every flag
   fixed in convergence loop 1 above), then a small confirmation sweep on fresh seeds.
4. Stage 6a disclosure, then the mulligan profile (USER order: profile -> mulligan -> value leaf).
5. The user's original `decks/LandfallElementals/LandfallElementals2.cod` (byte-identical to the shipped
   `LandfallElementals.cod` modulo CRLF) sits untracked in the primary checkout. USER 2026-10-22: the
   final name must not carry the 2 -- move that copy aside (never delete) once the primary is updated.

## Claude-play sweep
- commit: `100759123` (play-identical to `0513b9b65`: that commit adds an allocator retention cap and
  env-gated diagnostics only)
- seeds: first sweep 61000-61019 (20 games, 2026-10-19, pre-loop-1; every flag fixed in convergence
  loop 1). Confirmation sweep 2026-10-23: seeds 62000-62011 as `--seed 62000+gi --game-index gi`
  (12 games, Opus players; brief `logs/lfe/sweep2_prompt.md`). The first launch passed
  `--seed 62000 --game-index gi`, which replays ONE shuffle for every gi (the index only drives
  opponent spawns), so seed 62000 was played 6 times and counts once.
- flags: 2 unresolved
  * Smokebraider over-tap (62006 T3, 62007 T3): Mossborn Hydra {2}{G} is paid with Smokebraider's 2
    units AND two lands (Courtyard + Islet / Territory), 4 mana for a 3-mana spell, floating 1
    restricted unit. Smokebraider + one land pays it exactly. Legal but wasteful; in 62006 the extra
    tapped land makes the main-2 Flamekin Harbinger {R} uncastable. Win turn unchanged in both games.
    ManaPayment (the loop-1 `RestrictedMayHoldForAttack` / `RestrictedIneligOwedLater` path) -- OPEN.
    CAUSE: `restricted_hold_ok` (hold Smokebraider for its attack) is only re-tested when the line still
    owes mana (`line_owed > 0`); with nothing owed after this payment the hold stays on even when the
    general sources cannot cover the payment, so Smokebraider taps anyway but ranks last and pays the
    final pip only. TRIED AND REVERTED (2026-10-23): re-testing the hold on every payment (restricted
    pays first once it must tap) fixed 62007 but moved smoke elementals d0 gi480 (seed 1481) T5 -> T6:
    Risen Reef {1}{G}{U} from Waterlogged Grove / Secluded Courtyard / Smokebraider, then Akoum Hellhound
    {R}. The old payment (Grove + Courtyard + 1 Smokebraider unit) floated an any-colour unit that cast
    Hellhound; restricted-first paid Smokebraider 2 + Courtyard and left only Grove (G/U), no {R}. The
    DOMINANT payment is Smokebraider 2 + GROVE, keeping Courtyard (any colour for a creature, and an
    untapped land survives into main 2 where a float does not). So the fix is in the LAND choice once
    the restricted source must tap: spend both its units and keep the most flexible general source
    (needs-aware), not a rank flip. Searched rows were unmoved in score (8 play-changed, 0/0).
  * Fulminator Mage plans listed 7x on the 62006 T4 viewer menu (identical summaries, byte-identical
    boards): an unlabeled axis or real duplicates -- under investigation with the plan-fan explosion
    below.
- Results: 11 of 12 tie the d5 b20 search (T4 x7, T5 x4); 62011 Claude T5 vs search T6 -- the search
  mulligans a 1-land Gruul Turf 7 under the STATIC keep rule and finds T5 once handed the kept board
  (keep policy, Q6 / the keep table, not a search hole). No illegal plan, missing legal play or wrong
  state delta found. Also recurring, by design (d93492f1f): a human plan always cracks a fetch AFTER a
  Harbinger top-tutor (the crack-first order is search-only; the human orders it across frames).
- Decision types seen: mulligan, bottom, main_phase, tutor_etb (Terramorphic / Myriad), dig (Risen
  Reef look-top land), divide (Fury), bounce (Gruul Turf), vial_charge (Aether Vial), discard.

## Mulligan-profile blocker: memory (2026-10-23)
The generation-setting screen (`mullgen_finalize.py --write`) aborted twice at the 20 GB RSS cap on this
23.5 GB box: its d5 b20 hand scorer and keep-table discovery (d5 b20, 400 probes x 22 cards).
1. **Allocator retention (FIXED upstream, 88a979f07 `MTG_POOL_ALLOC_CAP_MB`, landed in parallel with
   this diagnosis).** `src/core/PoolAllocator.cpp` kept every freed <=512-byte block on its thread's
   free-list forever, so each worker held its own all-time high-water mark. With the pool off the same
   discovery peaks at 3.5 GB and completes (K=21, 293 s); an identical 64 MB/thread cap written here was
   smoke-verified byte-identical (149/149 unchanged) and dropped for upstream's at the rebase.
2. **Plan-fan explosion (OPEN).** The d5 b20 scorer still aborts with the cap: single main-1
   `EnumeratePlansWithLand` lists reach 100k-527k plans (up to 1.7 GB for ONE call) at turns 5-7, bf
   11-18, hand 6-8 (`MTG_ENUM_HIWATER_KB=100000`, axis breakdown): only ~300-2,800 BASE plans, multiplied
   by the Risen Reef look-top pins (~70% of plans), the land-search activation positions incl. pre-cast
   subsets (`land_search_act_slot`, ~80%), land_slot positions (~60%) and Harbinger tutor variants
   (~40%). The axes compose multiplicatively because the look-top fan and the act-slot twins both take
   tutor variants and position twins as bases (the 2026-10-19 loop-1 enumeration fix). A single slow
   discovery rollout (probe 102 + Risen Reef) is 12.5 s alone (173 s in the contended pool); its T4
   decision enumerated 156,707 plans over 513,816 odometer states for 60,873 billed units.

   **The monster game (single-game repro, play settings d5 b20, ONE thread):**
   `mtg decks/LandfallElementals/LandfallElementals.cod --profile ... --games 1 --seed 1100011 --game-index 11`
   reaches **18.9 GB RSS in 23 s** and dies at the default cap. A gdb snapshot at 6 GB: ONE main-1
   enumeration inside a rollout (FSLineWin -> SimulateToEnd -> RolloutPreCombatSolve ->
   SolveWithLookahead -> EnumeratePlansWithLand), turn 6, `all` = 1,028,168 plans after the
   act-slot twins, `extra` = 440,049 and growing in the land_search_pins fan (TurnSolver.cpp:74754),
   the look-top fan still to come. `MTG_ENUM_HIWATER_KB` is SILENT for it (it reports only after an
   enumeration completes). This game is what killed both 5c2 attempts (in flight in both aborts).

## Stage 5c2 / 5h / verify_deck / backstop screen (2026-10-23, this box: 23.5 GB, 32 cores)
- **5h viewer audit: PASS** (`audit_viewer_decisions.py`, 40 games from seed 9001): expected
  bounce/dig/divide/target/tutor_etb/vial_charge all surfaced (+ discard); oracle-text cross-check clean.
- **5c2 leaf tie-break: NOT RUN TO COMPLETION.** `leaf_tiebreak_check.py` (24,000 games at play
  settings d5 b20, 24 threads, RSS cap 14 GB) aborted at the cap after ~1,200 games -- the same
  plan-fan memory explosion as the mulligan blocker, here in ordinary play WITH mulligans. Slow games
  it logged (single-game repros, play settings): `--seed 1000162 --game-index 162` (58 s pooled, 11 s
  / 1.2 GB peak alone, 1.02M units), `--seed 1000467 --game-index 467`, `--seed 1000671 --game-index 671`.
  Re-run once the explosion is fixed (or at <= ~12 threads).
- **verify_deck.py (decklist path):** PASS coverage, card_costs, viewer, viewer_wiring, mismatch,
  play_invariants (8 games / 176 decisions), suite. SKIP card_fields (Scryfall snapshot missing 13 cards),
  clause_ledger. FAIL regression_tiers (overnight: no GT -- the next periodic overnight run accepts it;
  NOTE its d5 rows are 4x300 games at d5 b20, which this box may not survive at 32 threads until the
  explosion is fixed) and claude_sweep (the 2 open flags above).
- **Unbudgeted backstop rows (owed per the 2026-10-22 rule): NONE ADDED -- no block screens clean.**
  Budget-0 screen, 10 games x 8 bases (30001..30061, 30101..30161) x d3/d5, abandon_units 2,000,000,
  8 threads: EVERY d3 block abandons 1-4 games; d5 is clean only on 30001 / 30101 / 30141 / 30161 (max
  24 / 7 / 6 / 19 s). Worse, games ran 17-19 MINUTES without reaching the 2M-unit ceiling (30021 d3:
  1,035 s; d5: 1,141 s) -- the enumeration is not charged to the meter, so the backstop's own cap does
  not bound it. Same root cause as the memory explosion; screen again after it is fixed. (Picking a
  block to dodge it would hide exactly the defect the rows exist to catch.)

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
- Q1. ANSWERED (USER 2026-10-22): the final file has no 2 in its name -- `LandfallElementals.cod`; the
  untracked `...2.cod` in the primary is moved aside once the primary carries the tracked copy.
- Q2. `min_playable` is 0 in the generated profile (the skill suggests >= 1 for multi-colour decks; most
  decks ship 0 and Galadriel took 1 with its required piece). Mulligan territory -> yours. Default: as
  generated.
- Q3. Soulstoke's opponent-end-step timing: build the window (`docs/design/opponent-end-step-window.md`,
  ~2-3 days, three rulings needed) or ship main-phase-only? Default: main-phase only, PROVISIONAL.
- Q4. Sign off the PROVISIONAL deferrals listed in the three cluster reports above (Brushfire evasion,
  Hydra trample, Reef fixed orders / defaults, Breaker own-target narrowing, Fulminator ranking,
  Smokebraider Kindred refusal, Fury planeswalkers / trigger order) and the INERT_PARAMS rows.
- Q6. The baseline static keep rule mulligans e.g. 5 lands + Mossborn Hydra + Foundation Breaker (no
  2-drop). Fine to leave until the keep table (your mulligan stage), or do you want a static tweak first?
- Q7. The generic DIG gate is aggregate-neutral here (d3 -0.0013, d5 -0.0008 per game opened, ~+5%
  units) but hides 8/1,600 d3 lines even at 20x budget. Recommendation: open it for LandfallElementals
  (a deck-scoped provider override: more complete, measured neutral-to-better), and decide separately
  whether the GenericProvider gate should become budget-staged for every deck. Default: unchanged.
- Q8. Harbinger's tutor width: the provider now admits every distinct Elemental name (was the default
  6, which made Incandescent Soulstoke unfindable at any budget in sweep seed 61016). Measured d3 -0.0375,
  d5 -0.0333 on top of the other fixes, at ~1.11x units. ON by default (`MTG_LFE_TUTOR_WIDTH=0` restores
  6) -- keep it?
- Q9. Crack ordering is expressible only as "some cracks before all casts" (not between two specific
  later casts); in the SEARCH, land positions with a Reef look before the failing cast are still kept
  (conservative). OK as is?
- Q5. Harbinger/Reef trigger order reuses the `dig` modal (two card tiles, asked up front) rather than a
  new decision type -- OK?

## Approved deferrals
(none yet)
