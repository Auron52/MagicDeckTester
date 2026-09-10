# Analysis ledger — Snow

Deck: `decks/Snow/Snow.cod` (mainboard only; sideboard unreachable — no wish effects — and user
explicitly said to ignore it).

Status: **COMPLETE THROUGH STAGE 6** (see the pipeline-state block below, which is authoritative).
Remaining, deliberately user-initiated: the value leaf and the mulligan profile. Snow is not yet
in the regression suite.

> The old status line here said "IN FLIGHT ... currently tuning the site-8 perf fix". That was
> stale by two days: the site-8 fix landed and the pipeline ran to Stage 6. Kept as a note because
> a stale status line at the top of a ledger is read as current and this one misdirected a later
> session into re-opening finished work.

## Implementation state (2026-09-06, for resumption after compaction)

Everything below is IMPLEMENTED and COMPILES (Release build clean):

- **cards.json**: all 17 mainboard entries appended (JSON validated; coverage-only reports no
  missing/partials). Bracket notes carry every PROVISIONAL deferral.
- **New CardParams** (+ parse in CardDatabase.cpp): pt_equals_snow_permanents_you_control /
  _on_battlefield, damage_equals_snow_permanents, ice_counter_cost, ice_counters_are_snow,
  ice_counters_dont_untap, snow_enter_scry, upkeep_snow_threshold + upkeep_sac_* token spec
  (power/toughness/subtypes/keywords/legendary), gy_play_cost/_requires_supertype/
  _permanent_only/_enters_tapped, tap_draw_requires_top_supertype, filter_no_free_colorless,
  etb_self_draw, etb_tap_opp_creature.
- **SpellEffects.h**: IsSnowPermanent-family (IceGrantsSnow/AnyIceCounters/SnowPermanentCount,
  CardHasSupertypeNamed); DynamicBasePower/Toughness snow arms; TapLargestOppCreature +
  FireOwnEtbTriggers dispatch (etb_tap_opp_creature, etb_self_draw); FireSnowEnterWatchers
  (called from FireEtbWatchers top + LandPlay tail w/ re-find-by-number); PerformUpkeepSlumber
  (+ GameEngine upkeep + rollout SimulateEndAndStartNextTurn lockstep calls); CreateToken
  Indestructible keyword + legendary set-after-create + EnforceLegendRule; gated TapDraw
  resolution (put-in-hand NOT a draw, dig-chooser decline, non-snow top stays);
  PermAbilityMode::IceCounter (label, apply w/ dig-chooser target + dont-untap-aware heuristic,
  SpendRepeatActivations arm + useful cap); ApplyGraveyardPlayAbility + GyPlayTargetLegal
  (probe/commit split); CreatureBurnDamage snow arm; Astrolabe no-free gating in
  UnconditionalProduces / AnyColorFilterFedSlots (split arithmetic, old formula byte-identical
  at Fn=0) / AddSourceToPool / SourceMaxNet / HasUntappedRampFeeder.
- **TurnSolver.cpp**: BpSiteMask widened to 9 bits, site 8 UNCONDITIONAL (0x100 forced — same-
  turn playability is correctness, USER 2026-09-06); PlanOpensBreakpoint site-8 clause; rollout
  ActivatePermAbility site-8 re-solve (playability gate + chain-depth-1 cap, occurrence always
  counted for bp_at lockstep); autonomous whiff-pruning of gated TapDraw enumeration (top must
  be snow — lossless dominated-action removal, humans keep the offer); IceCounter ModeSpec row +
  useful-K cap; Skred dmg + FindBurnKillTarget routing; leaf dyn estimate snow arms;
  BoardHasScalingAttacker + Main1 classifier + plan-signature "nfc"/GYP# folds; Kaldring
  enumeration (Haven clone, land gated on drop open) + apply + ActivationFamilyKey +
  CheckLine gyplay= (validation, declaration, matching); Slumber EvalCard branch
  (upkeep_snow_threshold distance scaling); ice-lock untap gate (rollout mirror).
- **GameEngine.cpp**: ice-lock untap gate; PerformUpkeepSlumber call; is_draw_spell +=
  etb_self_draw.
- **AIEngine.cpp**: executor site-8 twin (same playability gate; resolve_draw_breakpoint);
  Skred FindBurnKillTarget arm; Kaldring executor apply + LogAbility.
- **KeepModel.cpp**: etb_self_draw in cards_drawn + draw_engine.
- **main.cpp**: gyplay verb (parse, emit, tag, non-cast list, activate list).
- **scripts**: audit_card_costs/fields.py MDFC face-by-name selection (Kaldring fix);
  audit_viewer_decisions.py MANIFEST (tap_draw_requires_top_supertype→dig, snow_enter_scry→scry,
  ice_counter_cost→dig, gy_play_cost→main_phase), BOARD_ACTIVATIONS (gyplay verb, ice activate),
  INERT_PARAMS (all remaining new params + any_color_filter pre-existing gap + FIXED stale
  Chupacabra no-legal-target claim).

**Perf saga (site 8)**: first run had 306s games at d3/b200 — 521k greedy re-solves/game.
Fix 1 (playability gate, both worlds): 166k/34s. Fix 2 (chain-depth cap + autonomous whiff
pruning): 190k/50s — WORSE (whiff-prune raised the hit rate; sequential activations all fire).
Fix 3 (IN TREE, REBUILD + RE-MEASURE IS THE RESUME POINT): the rollout greedy fallback no
longer runs a full Solve — a found LAND is played directly via a mini land_to_play plan; a
found NONLAND waits for the playout's next simulated turn. Searched bp variants (W=2 default)
and the executor's committed re-solve keep the FULL same-turn continuation, so the USER's
same-turn-playability requirement holds where it matters (real play + searched lines); only
greedy playout SCORING is slightly pessimistic on nonland finds. Resume: `./build.sh`, re-time
`--seed 44 --game-index 2 --games 1 -d3 -b200` (was 50s; want low seconds), then the 10-game
run, then continue the Still-to-do list below.

**Pipeline state (2026-09-06 end of autonomous window): COMPLETE through Stage 6.**
Byte-identity ✓ (smoke 73/73 + regression 99/99 at every step, incl. after the Astrolabe fix
and the {S} model); Scryfall audits ✓ (fully green after real {S}); Stage 4 profile ✓ +
provider Generic ✓; Stage 5 ✓ — verify_deck GATE PASS (coverage/costs/fields/viewer/wiring/
mismatch(no nonconv, no fd-diverge, 2 seeds x 60 games)/play_invariants/claude_sweep), multi-
depth sanity d0 6.72 > d3 6.12 = d5 6.12 loss-penalized (monotone, d3>=d0 in every paired
game, d5 identical to d3 at b200), 5c2 leaf_tiebreak_check run (verdict recorded below),
claude-play sweep 16/16 ties 0 unresolved flags, real-play coverage of every mechanic
(Augur x56 / Sheets x8 / Owl ice x2 / Kaldring gyplay x1 / Marit Lage token + sacrifice +
legend rule across 175 d0 games); DECISIONS.md ✓; snow-mana model ✓ (section above); CI ✓
(Linux + Windows + determinism parity green on both pushes).
**NOT done, deliberately (user-initiated later per policy): mulligan-profile generation and
the value leaf.** Also open: Snow is NOT in the regression suite yet — its slow games
(worst ~5-15 min single games at d3/d5 b200 pre-value-leaf, horizon-rollout volume s90, NOT
site 8) make adding it a shared-budget sizing decision for the user.
**5c2 leaf tie-break check (24,000 games / 12,000 paired at play settings): OPT OUT** — the
horizon tie-break RAISES Snow's average on both halves (+5/+3, net +8 turns; 114 binding games,
0.950%). The stored-value failure mode fits exactly (snow-permanent build-up prices at zero on
an opponent-life proxy). Adopted: **SnowProvider** (Generic + `GradesNoWinLeaf() -> false`, the
deck's first measured hook), routed on an OR of four cards' snow-only params (Treefolk CDA /
Slumber threshold / Owl grants-snow / gated look) — deliberately NOT on Supertype::Snow itself
(snow basics are the splashable-staple class). provider_audit: Snow -> Snow, intended. Full
record in docs/design/horizon-honest-leaf.md §4c.
**5i discard analysis (analyze_deck's evidence stage, 400 games d3/b10): STATUS_QUO_OK** —
"the current ranking is at or near the searched optimum on this deck." Per-card regret 0.0
everywhere except Ice-Fang Coatl 0.091 and Abominable Treefolk 0.154 (both tiny). No bucket
policy work needed.
**5e/5f status**: Snow rides the GENERIC provider — no deck narrowing heuristics exist, so 5e
has nothing to verify (and the 16/16 claude-play ties say the un-narrowed search already plays
optimally). The 5f PERF GATE is NOT yet cleared for mulligan-gen feasibility: the deck's cost
is horizon-rollout volume (s90 ~1.2M leaf solves in the worst game), whose designated remedy
is the VALUE LEAF (1.35–84.8x per value-leaf.md), user-initiated. If post-leaf cost is still
infeasible, 5f pruning proposals (look/ice enumeration width) are the next lever — grounded in
the 5e oracle method, unpruned A/B mandatory.

## Stage 1 — Coverage

All 17 mainboard cards `missing` (fresh deck). Sideboard `reachable: false` (correct; no wish).

## Shared engine design (orchestrator, provisional)

- Engine already has `Supertype::Snow` (parsed from `supertypes` in cards.json). No `{S}` mana,
  no ice counters, no snow-count effects yet.
- **PROVISIONAL deferral (needs user sign-off): `{S}` pips modelled as generic `{1}`.**
  Why inert here: every mana source in this deck is a snow permanent (snow basics, Rimewood
  Falls, Highland Weald, Scrying Sheets, Coldsteel Heart, Boreal Druid, Arcum's Astrolabe), so
  "one mana from a snow source" == "one mana" for this deck. Bracket-noted on every entry with
  an {S} cost. Revisit if a non-snow mana source ever joins a deck using these cards.
- Snow-permanent COUNTING (Abominable Treefolk P/T, Marit Lage's Slumber threshold, Ice-Fang
  deathtouch) is implemented faithfully off `Supertype::Snow` battlefield scans.

## Stage 2 — Cards (drafts pending)

| card | tier | status |
|---|---|---|
| Skred | 2 | draft ready (direct_damage + damage_equals_snow_permanents; shared CountSnowPermanents helper in SpellEffects.h; NOT snow itself, no {S}; goldfish-inert removal — expect ~0 card_score, that is correct) |
| Rimefeather Owl | 3 | draft ready (GLOBAL-count CDA both axes; ice_counter_cost "{2}" activated ability via PermAbilityMode::IceCounter, K capped by non-snow permanents; ice_counters_are_snow layer-4 static evaluated LIVE, never baked into masks) |
| Scrying Sheets | 2 | draft ready (tap_draw_cost "{2}" + NEW tap_draw_requires_top_supertype "Snow" — Mariposa Military Base precedent; LookupCached mandatory for top-card supertype; trailing-pass same-turn-unplayable deferral PROVISIONAL) |
| Snow-Covered Island/Forest/Mountain, Rimewood Falls, Highland Weald | 1 | draft ready (basic_land template; Rimewood Falls + Highland Weald enters_tapped; no cpp) |
| Coldsteel Heart | 1 | draft ready (custom mana_rock WUBRG + enters_tapped; ETB colour-lock simplified per Cavern/Unclaimed precedent — over-permissive, disclose) |
| Boreal Druid | 1 | draft ready (mana_dork produces [C]; no cpp) |
| Abominable Treefolk | 2 | draft ready (power/toughness_equals_snow_permanents CDA via DynamicBasePower/Toughness + etb_tap_opp_creature; untap rider provably unreachable — PROVISIONAL deferral; leaf power estimate + BoardHasScalingAttacker + feeds_combat wiring needed) |
| Arcum's Astrolabe | 3 | draft ready (etb_draw NEW param via FireOwnEtbTriggers; any_color_filter + filter_no_free_colorless NEW — Capital City's flag bakes a free {C} mode Astrolabe lacks, ~9 engine sites to gate; KeepModel cards_drawn wiring; audit script needs any_color_filter mapping added) |
| Frost Augur | 3 | draft ready (SHARED tap_look_top_snow_cost "{1}" mechanism w/ Scrying Sheets; Action::Kind::ActivateLookTopSnow; CanTapNow gate — first {T} ability on a non-dork creature; non-snow top STAYS on top; decline via dig chooser) |
| Ice-Fang Coatl | 2 | draft ready (etb_self_draw NEW param in FireOwnEtbTriggers — own_creature_enters_draw is a WATCHER, wrong; Flash/Flying/conditional deathtouch inert deferrals PROVISIONAL; GameEngine is_draw_spell snapshot predicate fix) |
| Kaldring, the Rimestaff | 3 | draft ready (MDFC BACK FACE of Jorn, God of Winter! gy_play_* params, Action::Kind::GraveyardPlayAbility + Plan land_from_graveyard slot; Jorn face deferral PROVISIONAL — decklist names Kaldring; audit scripts hard-fail on card_faces[0], must select matching face) |
| Marit Lage's Slumber | 3 | draft ready (snow_enter_scry watcher — MUST also hook LandPlay.cpp tail, lands don't route FireEtbWatchers; upkeep_snow_threshold 10 sac→20/20 legendary token via PerformUpkeepSacTutor clone; CreateToken needs Indestructible + legendary; EvalCard enchantment blindness; rollout SimulateEndAndStartNextTurn lockstep) |
| Rimescale Dragon | 1 | draft ready (vanilla 5/5 flier; {2}{S} tap+ice ability AND its paired don't-untap static DEFERRED TOGETHER as inert — PROVISIONAL; note castability {5}{R}{R} is tight, 1-of will be noise) |

## Integration notes (cross-card contracts)

- **`SnowPermanentCount(state, controller)` is written ONCE** in `SpellEffects.h` (beside
  `CreatureCount`); readers: Skred damage, Abominable Treefolk CDA, Marit Lage's Slumber
  threshold, Rimefeather Owl (per its draft), Ice-Fang Coatl deathtouch condition.
- **Every snow card's entry MUST carry `"supertypes": ["Snow"]`** (basics: `["Basic","Snow"]`).
  A missed supertype silently shrinks the Treefolk/Owl and stalls the Slumber threshold.
- Skred is NOT snow and has no {S} — plain `{R}`.
- Fix stale `INERT_PARAMS` string for `etb_destroy_opp_creature` in
  `scripts/audit_viewer_decisions.py` (claims "no legal target"; contradicted by the 2026-09-05
  correction — spawns exist in 8/10 game indices) in the same pass.
- Treefolk needs: leaf power estimate arm (TurnSolver ~3900), `BoardHasScalingAttacker`,
  Main1 classifier, GoldFishRunner feeds_combat lists ×2.
- **Ice counters (corrected premise):** Rimefeather Owl's P/T counts SNOW PERMANENTS, not ice
  counters; ice counters matter only via the Owl's own static (iced permanents become snow).
  Every permanent this deck controls is already snow — the ONLY non-snow permanent possible is
  the Marit Lage token. Owl-ices-Marit-Lage = +1 snow; Rimescale's static is the only cost of
  that line → **defer or implement the Owl ice ability + Dragon static as a PAIR, never one
  alone** (Dragon draft defers both; align the Owl draft at integration).
- **Agent-disagreement to fix in bracket notes:** Rimescale's draft claims "opponent controls
  no creatures"; Skred/Treefolk drafts correctly note harness spawns exist in 8/10 game
  indices. Inertness conclusion unchanged (opponent tapped state is never read; spawns never
  attack/block) — reword the Dragon's note at integration to the unobservability argument.

- **CRITICAL — `{S}` in any cards.json cost string parses to ZERO** (`ManaCostFromString`
  recognises no `{S}`; the stoi throw is swallowed). Never write `{S}` in a cost field: write
  the generic total instead (Astrolabe cast `{1}`, Frost Augur activation `{1}`, Scrying
  Sheets activation `{2}`, Owl ice `{2}`).
- **CDA scope params**: Owl counts snow permanents ON THE BATTLEFIELD (global); Treefolk
  counts YOU CONTROL. Two distinct params; both set BOTH axes (toughness must be wired or the
  SBA kills the 0/0 on entry).
- **If the Owl ice ability ships, Rimescale's "iced creatures don't untap" static ships with
  it** (otherwise icing own permanents is free upside the real card punishes). Untap-step gate
  on own creatures with ice_counters>0 while a source is out; icing "useful" cap = count of
  non-snow permanents (own permanents are all snow already).
- Ice ability targets follow the loyalty doctrine: search stays narrow, full rules-legal set
  offered at resolution under HumanPlayActive.

- **Kaldring is the MDFC back face of Jorn, God of Winter.** Decklist explicitly names
  Kaldring, so the orchestrator's call (PROVISIONAL, surfaced to user): implement the Kaldring
  face only; the Jorn front face ({2}{G} 3/3, untap-snow-on-attack — would flip the deck
  second-main-relevant) is a PROVISIONAL deferral, NOT silently dropped. Full nonland-MDFC
  face-choice support is the follow-up if the user wants Jorn searchable.
- **audit_card_costs.py / audit_card_fields.py hardcode `card_faces[0]`** — will hard-fail
  diffing Kaldring against Jorn's cost/types/PT. Fix: select the face whose name matches the
  cards.json entry. Required regardless of the Jorn decision.
- **Slumber's snow-enter scry watcher must hook BOTH FireEtbWatchers AND LandPlay.cpp's ETB
  tail** (param-gated FireSnowEnterWatchers, NOT a blanket FireEtbWatchers call from PlayLand
  — byte-identity risk). ~21 of the deck's snow permanents are lands.
- Snow-count helper naming: agents proposed `SnowPermanentCount` / `CountControlledSnowPermanents`
  / `IsSnowPermanent` — integrator picks ONE (a predicate + a count, sited by CreatureCount),
  with the ice-grant-aware variant from the Owl draft (two-pass, early-out on zero ice counters).
- Ice-Fang: do NOT list Deathtouch in keywords (would grant unconditionally); supertype Snow is
  load-bearing even though its own deathtouch clause is inert.
- Kaldring graveyard predicates MUST route ZoneCard/LookupCached (placeholder masks are empty).
- Legend rule: CreateToken gets `legendary` bool; upkeep block calls EnforceLegendRule; Kaldring
  gy-play path calls EnforceLegendRule (4x Slumber is legendary).

## USER directives (mid-run, 2026-09-06)

1. **Sheets/Augur found card MUST be playable the same turn** — the trailing-pass deferral is
   REJECTED. Wire the activation through the mid-turn re-solve (breakpoint) machinery.
2. **Reveal-and-put-into-hand is NOT a draw** — no cards_drawn_this_turn increment, no draw
   watchers. Standing convention from now on.
3. **Snow mana gets a real forward-looking model** ({S} payable only from snow-source mana,
   correct under mixed manabases) — implemented LAST, after the deck converges. The generic
   collapse stays until then (exact for this deck: all sources snow), bracket notes retained.

## Integrator reconciliations (orchestrator decisions, all 13 drafts in)

1. **Frost Augur vs Scrying Sheets — ONE mechanism, the Sheets route.** Reuse existing
   `tap_draw_cost` (Mariposa `PermAbilityMode::TapDraw`) + new `tap_draw_requires_top_supertype:
   "Snow"`; Augur = `"{1}"`, Sheets = `"{2}"`. Augur's proposed new Action kind is dropped. Add
   a summoning-sickness gate (`CanTapNow`) for CREATURE sources of perm abilities if the
   enumeration lacks one (verify in code — first {T}-activation creature). Wire the "may
   reveal" decline through the existing dig chooser at the shared TapDraw site, gated on the
   new param (Mariposa unaffected; chooser null in search = byte-identical).
2. **Ice-Fang `etb_self_draw` vs Astrolabe `etb_draw` — same mechanic, ONE param:
   `etb_self_draw` (int)**, fired from FireOwnEtbTriggers; both cards use it.
3. **Owl ice ability ships ⇒ Rimescale's static ships (the pairing rule):** new
   `ice_counters_dont_untap` (bool) on Rimescale; UntapStep + rollout mirror skip own
   permanents with ice_counters>0 while a source is out. Rimescale's ACTIVATED ability stays
   deferred (unobservable). Ice enumeration useful-cap = non-snow permanents only.
4. **Snow density corrected: 56/60 snow; the only non-snow cards are 4x Skred** (plain
   Instant). {S} ≡ {1} is EXACT for this deck (all mana sources snow) — verified by three
   agents independently.
5. Snow helpers: ONE `IsSnowPermanent(state, perm)` predicate (ice-grant-aware, two-pass
   early-out) + `SnowPermanentCount(state, controller /* -1 = battlefield */)` count.

## Approved deferrals

(none yet — all deferrals PROVISIONAL until user signs off)

## Claude-play sweep
- commit: `2c98725b` (the Snow implementation commit; the one flag's fix landed immediately after)
- seeds: 9000 games: 16 (game-indices 0–15, disjoint from suite seeds 1001/2002/3003/4004–7007)
- flags: 0 unresolved
- Result: **16/16 ties** — claude_win = ai_win = 5 in every game; zero misplay candidates.
- One unique CONFIRMED bug, independently found by 6 of 16 agents (gi 0, 2, 4, 10, 11, 14):
  the enumerator pool credited a FED Arcum's Astrolabe (`filter_no_free_colorless`) a full
  `++pool.wild` — but its fed mode is 1-in/1-out, so the feeder's unit was double-counted
  (+1 per fed copy, scaling to the +4 the param's own note warns about). Symptom: unpayable
  plans offered (mv-7 casts off 6 real sources) and re-offered after each `dropped_casts`
  no-op; nothing illegal ever resolved (payment is atomic). FIXED same day: `wild_phantom`
  subset on ManaPool (the `wild_c` pattern) + an amount precheck in `CanPayFlat` that
  subtracts phantom units from the payable amount while keeping the wild for colour
  deficits — provably one-directional (can refuse phantom offers, can never hide a legal
  cast), and provably byte-identical for every deck with no such filter (phantom=0 makes
  the precheck implied by the existing deficit checks). Verified: unit 70/70 + scenarios
  72/72 + smoke/regression suites on the fixed binary.
- Repeated verified-clean observations across the 16 games: snow-enter scry fired on all
  three wiring paths (own enter / FireEtbWatchers / LandPlay tail); Treefolk CDA exact at
  every combat; upkeep threshold correctly silent below 10; ETB draws, enters-tapped,
  summoning sickness, legality of every offered plan (modulo the fixed flag) all exact.
- Coverage gaps the sweep could NOT reach (T5 Treefolk beatdown always ends the game
  first): the Slumber upkeep sacrifice → Marit Lage token (+ legend rule), Scrying
  Sheets / Frost Augur gated look (site 8), Rimefeather Owl ice counters, Kaldring
  graveyard-play. Covered instead by the d0/d3/d5 depth-sweep game logs (slower wins
  reach the upkeep) + targeted repros — see the depth-sweep section.

## Snow-mana model — IMPLEMENTED 2026-09-06 (was the "design" below; kept for rationale)

Shipped exactly as designed, with one correction found by measurement:
- `ManaCost::snow_pips` (baked into generic — every flat reader unchanged), parsed from `{S}`
  (which previously parsed to NOTHING); cards.json now carries the REAL costs (Astrolabe `{S}`,
  Augur `{S}`, Sheets `{1}{S}`, Owl ice `{1}{S}`) — the Scryfall hard mismatch is cleared.
- `ManaPool::snow_units` subset (wild_c pattern) maintained at the AddSourceToPool choke point
  (def-card masks, so pending-rock projections avoid the placeholder-mask trap); `CanPayFlat`
  requires `snow_pips <= snow_units` (necessary-condition, enumerate-optimistic doctrine).
- Payer (`TapForCostSharedOnce`): **strict snow payment is scoped to MIXED manabases only**
  (`g_snow_pay_strict`: cost has {S} pips AND some source the payer controls is not snow).
  Under strict: {S} pips settle first among generic pips restricted to snow producers (the
  `usable()` choke point covers scarcity/legacy/filter paths; a fed snow filter's output is
  snow per CR 106.4b regardless of feeder), floating may not pay them (no provenance), and the
  snow-blind backtracker fallbacks are skipped (pessimistic-safe). On an all-snow board every
  restriction stands down — **byte-identity by construction**, which the first cut got wrong
  (a blanket floating-hold moved seed-42 gi9 from wt 6 to 7; the strict scope fixed it).
- **Evidence**: same binary, old-vs-new cards.json, Snow 10 games d3/b200 seed 42 —
  win-turn IDENTICAL (avg 6.0000 both). Unit 70/70, scenarios 72/72. Mixed-manabase probe
  (Snow.cod with 8 Island + 3 Mountain swapped in): Astrolabe off a lone plain Island is
  REFUSED by the payer (`dropped_casts`, board untouched) and RESOLVES off a snow Forest;
  the mixed deck functions at d0 (7.36) and d3 (6.17) — slower than pure snow, as losing
  11 snow permanents should be.
- **Disclosures**: (1) the ENUMERATOR may still offer an {S} cast a mixed board cannot pay
  (an amount-only afford gate upstream of ManaPool::CanPay) — doctrine-compliant (never hides,
  payer exact, refusal disclosed via dropped_casts), same class as pre-existing colour optimism.
  (2) On an all-snow board, ritual/spell-produced floating would be assumed snow (no current
  deck mixes rituals with {S} costs). (3) A generic cost REDUCER cannot distinguish the baked
  {S} from real generic (no current deck combines them). (4) Under strict, the skipped
  backtracker can fail a filter-chain payment a cleverer assignment could make (pessimistic,
  never illegal).

## Snow-mana model design (USER directive 3 — implement LAST)

Forward-looking {S}: payable only by mana from snow SOURCES, correct for mixed manabases.
Follows the repo's hybrid/phyrexian/wild_c precedent — flat readers byte-identical, metadata
only CONSTRAINS payment:

- **ManaCost**: each {S} pip bakes into `generic` (ManaValue and every flat reader unchanged)
  and increments a new `uint8_t snow_pips`. Parse in ManaCostFromString ({S} currently parses
  to NOTHING — the silent-zero trap).
- **ManaPool**: per-bucket snow SUBSET counts (`snow_white … snow_colorless, snow_wild`),
  maintained where pools are built (BuildAvailableMana: source is snow iff the battlefield
  permanent's card `HasSupertype(Supertype::Snow)` — real masks on battlefield) and carried
  through AddPool/floating retention. Subsets, not new supply — every existing reader sees
  identical values (the wild_c argument).
- **Affordability/payment**: a single greedy `snow_pips <= snow_any` check is WRONG (cost
  {W}{S} vs pool {snow W, non-snow G} — the one snow unit cannot pay both pips). Exact and
  cheap instead: enumerate the supplying BUCKET per {S} pip (<= 7 choices per pip; every real
  snow card has exactly one {S}, support 2 like phyrexian), decrement that bucket + its snow
  subset, flat-check the remainder. Sites: ManaPool::CanPay, TapForCostShared/Direct (must
  TAP an actual snow source for the pip; prefer non-snow sources for non-snow pips), greedy
  ManaPayment, the SpellEffects.cpp flow oracle.
- **cards.json**: restore real costs — Astrolabe cast `{S}`, Augur activation `{S}`, Sheets
  `{1}{S}`, Owl ice `{1}{S}`, Rimescale ice `{2}{S}` — clearing the one Scryfall hard mismatch.
- **Verification**: this deck's manabase is 100% snow, so `snow_any == Total()` always and
  every check degenerates to the flat one — byte-identity EXPECTED for Snow, and `snow_pips=0`
  everywhere else makes other decks provably untouched. Prove both: Snow 10-game digest match
  + smoke suite.

## Perf characterisation (2026-09-08, measured — the 5f gate's evidence)

Measured at Snow's REAL shipped settings. **Snow has no `value_play` block, so it resolves to the
built-in default d5 / 20 virtual-ms** (`[play] depth=5 budget=20ms source=default`) — NOT the
`d3/d5 b200` gate cells the "worst ~5-15 min single games" note above came from. Any future
perf claim about this deck must say which of the two it means.

**Cost vs the rest of the repo** (300 Snow games + 100 each comparator, ONE pooled batch,
`MTG_SLOW_GAME_MS=1`, seeds 5500001+):

| deck | mean | median | p90 | p99 | max |
|---|---|---|---|---|---|
| **snow** | **4,888 ms** | 1,276 | 10,325 | 72,673 | **124,312** |
| hinata | 777 | 450 | 2,236 | 5,513 | 5,513 |
| kitty | 220 | 51 | 620 | 3,598 | 3,598 |
| fluct | 217 | 170 | 434 | 846 | 846 |
| dstorm | 148 | 6 | 121 | 4,576 | 4,576 |
| goblins | 28 | 4 | 49 | 1,179 | 1,179 |

**6.3x the next-worst deck on the mean, and tail-dominated: the top 12 of 300 games are 41% of all
Snow time.** Snow was the entire remaining tail of the 800-game batch.

**There is no hotspot.** `perf` (cpu-clock, Profile build) on a representative 7 s game: top self-time
symbol is `Action::Action` at 2.79%, nothing above 3%, 82% of samples land in "other". The win has to
come from doing LESS WORK, not from optimising a function.

**Where the units go** (`MTG_ROLLOUT_STATS`, 300 games vs Hinata 100):

| site | Snow | Hinata |
|---|---|---|
| la_cand | **33.5%** | 6.8% |
| rollout_step | **25.0%** | 5.6% |
| greedy_fallback | **24.2%** | 4.8% |
| la_bp_wave | **16.2%** | 0.8% |
| fs_main2 + fs_pre | 0.8% | 79.1% |

Snow burns **3.2x the units per game** (170,742 vs 53,264) and spends them somewhere completely
different: the LOOKAHEAD sites (la_cand + la_bp_wave ≈ 50%) and rollout+greedy (≈ 49%), where Hinata
is 79% full-solve. Two consequences:

- **`rollout_step + greedy_fallback ≈ 49%` is exactly what the VALUE LEAF replaces.** That is the
  quantified case for the 5f remedy, and it is the reason to run the leaf before reaching for
  anything else.
- **`la_bp_wave` at 20x Hinata's share is the price of the site-8 same-turn-playability directive**
  (USER 2026-09-06, "Sheets/Augur found card MUST be playable the same turn"). The directive is not
  in question; it now has a number attached, which is what a future 5f pruning proposal has to beat.

**The extreme tail has a separate, named cause.** The 124 s worst game (gi=224) spent
**1,001,374 of its 1,778,424 units (56%) inside ONE ABORTED iterative-deepening pass**. The overrun
guard is `max(kOverrunBeta * budget->Limit(), kOverrunFloor)` = `max(2 * 18,000, 1,000,000)` — at
Snow's 20 virtual-ms budget the **FIXED 1,000,000-unit floor swamps the intended 2x-of-budget
ceiling by 55x**, so a runaway pass burns 55 decision-budgets before it is cut. The floor only stops
binding above a ~556 ms budget, i.e. never in play.
**But it is a TAIL fix, not a general one, and the first read of this was wrong:** deck-wide there
was exactly **1 aborted pass in 300 games** — `waste_share = 1.95%` of units. Worth doing for
generation makespan and for the p99; NOT the deck's cost. (Any change here is an engine-wide
constant and needs the full suite, not a Snow-only argument.)

This is Step 1 of `anytime-search-budget-prediction.md`, which asks for exactly this measurement
("we do not currently know how often the cutoff actually fires mid-line, or how much time it
wastes") and says to measure before touching the predictor. Now measured, on one deck.

**Step 2, the floor sweep (2026-09-08).** `MTG_OVERRUN_FLOOR` exposes the constant; 300 games per
arm at play settings, each arm alone on the box:

| floor | avg | total_s | mean | p95 | p99 | max | movers vs control |
|---|---|---|---|---|---|---|---|
| 1,000,000 (shipped) | 6.0867 | 1557.8 | 5193 | 23,627 | 72,802 | 86,641 | — |
| 200,000 | 6.0900 | 1480.1 | 4934 | 21,398 | **52,436** | 85,279 | +0/−1 |
| 100,000 | 6.0900 | 1511.7 | 5039 | 22,032 | 54,656 | 80,110 | +1/−2 |
| 50,000 | 6.0900 | **1928.0** | 6427 | 25,606 | 66,006 | 110,421 | +1/−2 |

- It is a **TAIL lever**: 200k buys **p99 −28%** (72.8 s → 52.4 s) for −5% mean. That is the number
  generation MAKESPAN cares about, and the mean barely moves because only ~1 pass in 300 games
  aborts at all.
- **The curve turns.** 50k is **+24% WORSE on wall** than the shipped floor — cutting passes that
  would have completed just pays for the same work again a level shallower. A lower guard is not
  monotonically cheaper; assume that and you adopt a regression.
- **Cross-deck safe:** smoke at 200k is **0 configs changed / 73 unchanged** — no suite deck ever
  reaches the floor, so an engine-wide constant is Snow-scoped in practice.
**HELD-OUT CONFIRMATION (1,200 games, seeds 5600001+ — disjoint from the 300-game sweep above):**

| arm | avg | paired movers | total | mean | p95 | p99 | max |
|---|---|---|---|---|---|---|---|
| control | 6.0867 | — | 10,776.9 s | 8981 | 41,077 | 107,835 | 268,444 |
| 200,000 | 6.0867 | **0 better / 0 worse** | 10,427.1 s | 8689 | 38,489 | 98,601 | 244,871 |

**Quality is EXACTLY neutral on held-out: not one game of 1,200 changes win turn** (the digest still
moves, so play changes and outcomes do not). The +0.0033 seen on the train set was that single game
and it does not reproduce — which is the whole reason the bar asks for a held-out sample.

Cost side, stated honestly: **−3.2% wall, p99 −8.6%, max −8.8%.** The train set's headline
"p99 −28%" does NOT survive the larger sample — 1,200 games have a much longer tail (max 268 s vs
86 s), so the guard clips a smaller fraction of it. This is a ~3% lever, not a step change.

**Meets the adoption bar** (neutral-at-play-settings on a large sample, with upside) and is
cross-deck inert (smoke 0 configs changed). **Still NOT adopted — that is the user's call**, and
the honest pitch is "3% and a slightly shorter tail", not a fix for Snow's cost. Snow's cost is
structural: the 49% of units in rollout+greedy that the VALUE LEAF replaces.

## Profile regenerated 2026-09-09, and what the attempt measured by accident

The profile was refit after this session's play changes (Scrying Sheets tap-hold, Coldsteel Heart's
ETB colour lock, the overrun ceiling) on frozen commit `49ab7e85` / src `1233dc76`, seed pinned
20260909. All 17 cards moved; the structure (same cards, same breakpoint counts) did not. The
notable mover is **Coldsteel Heart's second breakpoint, +0.0379 -> -0.2065** — i.e. once the Heart
is modelled as a colour-LOCKED source, the second copy is worth markedly less, which is the
direction the model change predicts.

**SNOW AT UNLIMITED BUDGET IS INTRACTABLE, and now there are numbers for it.** `analyze_deck.py`'s
optional reframe A/B diagnostic runs 200 games per condition at `depth=3, budget=0ms` — unlimited.
In ~25 minutes on 24 cores it did not finish its FIRST condition, and its slow-game tail escalated
monotonically:

| ms | 36,783 | 63,370 | 110,086 | 129,232 | 187,549 | 304,856 | 314,066 | 477,913 | 573,054 | 734,012 | 1,144,721 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| gi | 28 | 14 | 19 | 13 | 31 | 22 | 43 | 25 | 6 | 11 | 21 |
| win turn | 5 | 6 | 5 | 6 | 5 | 6 | 6 | 6 | 6 | 6 | **7** |

Twelve games over the 30 s threshold inside the first ~44 game-indices, worst **19 minutes for one
game**, and cost rises with win turn (the 5s cluster low, the lone wt=7 at the top). Repro for the
worst: `--seed 90022 --game-index 21 --games 1 --depth 3` with `--ignore-play-profile`.

Two consequences worth carrying:
- **This is the user's stated rationale for the anti-truncation work, measured on this deck** — an
  unlimited-budget run over many games reliably hits degenerate cases and becomes unboundable. It is
  the concrete case for a LARGE FINITE budget that truncates almost nothing (see
  `anytime-search-budget-prediction.md`).
- **Run the profile regen with `--no-cost-diagnostic` on Snow.** The diagnostic is informational,
  the profile is written before it starts, and on this deck it will occupy the whole box for an
  unbounded time. The run above was stopped for exactly that reason; the profile is unaffected.

## Value leaf ATTEMPTED and DEFERRED 2026-09-09 — the degenerate tail makes phase A unaffordable

`bash scripts/valueleaf.sh run decks/Snow` was started on frozen commit `a26d4632` / src
`1233dc76` / play fingerprint `3b366eed8cf0` (queue `logs/vlq_snow`) and **cancelled by the user
after ~2 h 40 m** because it would have owned the box for days. This is a measured verdict on the
deck, not a pipeline failure — the run behaved exactly as designed.

**What phase A actually achieved** (2500-game target, 10 pooled jobs, one queue, ~23/24 cores busy
throughout — utilisation was never the problem):

| elapsed | games finished | rows | workers blocked |
|---|---|---|---|
| 1 h 58 m | **140 / 2500** | 779 | **23 of 24**, each on a single game running **1.8 h+** |
| at cancel (~2 h 40 m) | **171 / 2500** | 949 | — |

~47 core-hours bought 140 games ⇒ **≈0.34 core-h/game ⇒ ≥35 h for phase A alone**, and that is the
optimistic read: it credits the 23 unfinished tail games as free when they had already consumed
~41 core-h between them. Instantaneous throughput at cancel was ≈0 — the pool had exhausted the
cheap games and every worker was grinding the tail. Phase C (the H×V matrix) is normally the larger
phase, so the full pipeline is multiple days, not an overnight.

**Root cause is this deck's degenerate tail, compounded.** Phase A plays at shipped settings but
attaches K=3 SEARCHED labels per position, which multiplies cost precisely on the games that were
already worst. It is the same tail measured two hours earlier by the analyzer's unlimited-budget
diagnostic (worst single game **19 minutes**, section above).

**There is no supported lever for this.** `--abandon-units` (the per-game work ceiling) is a
**phase C** flag on `valueleaf_depth_matrix.py`, is documented as uncalibrated and not for
production runs, and **would not touch phase A**. The pipeline is deliberately knob-free, so the
honest position is: *Snow's degenerate games must be made tractable before a value leaf is
affordable here.* That is the prerequisite work, and it is also the higher-value work — the same
tail is what makes every Snow measurement expensive.

**State preserved for a resume** (nothing needs redoing): freeze intact, phase 0 marked done, and
949 rows over 171 games on disk in `logs/vlq_snow`. Rows dedupe on `(seed, turn)`, so
`bash scripts/valueleaf.sh run decks/Snow` RESUMES rather than restarts. Do **not** use `finish`
(accept-rows-as-final) at this row count — 171 games is far too thin to train on.

## Optimisation attempt 2026-09-09 — candidate duplication measured; the cheap win is not available

Target chosen from the perf characterisation above: with the value leaf deferred, `rollout_step +
greedy_fallback` (49%) is unreachable, leaving the LOOKAHEAD sites. `la_cand` is the largest single
site (34%) and charges **one unit per candidate scored**, so that share IS candidate count.

**Finding 1 — 64% of scored candidates are redundant.** A census (`MTG_DEDUP_CENSUS`, default off,
counts only) over 60 games: `seen=4,226,520 dup=2,702,295` — a candidate whose post-apply state an
earlier sibling of the same pass already reached, whose rollout therefore recomputes a result
already on the books. The skip for this already existed at both candidate sites but was bundled
behind `MTG_COST_REFRAME`, an unrelated cost relaxation nothing ships. `MTG_CAND_DEDUP` unbundles it
(`a5b02c66`).

**Finding 2 — it is a QUALITY lever, not a speed one.** Adopted nothing; measured everything:

| gate | result |
|---|---|
| regression tier | slower=0 **faster=5** play-changed=19 |
| smoke | slower=0 **faster=7** play-changed=8 |
| Snow 300 @ play settings | avg **6.0833 unchanged**, units 49,617,752 → 47,326,981 (−4.6%) |
| Snow 300 **total cost** | **NOT RESOLVED** — see below |

**The total-cost question is open, and an earlier "wall-neutral" claim here was withdrawn.** Wall is
unreliable on this box (it is shared with other agents), so the arms were re-measured on process CPU
time (user+sys), which counts the hashing that units cannot see and is far less contention-sensitive
than elapsed. Four interleaved reps per arm:

| arm | median | min–max | within-arm spread |
|---|---|---|---|
| control | 1821 s | 1643–1909 | **16.2%** |
| dedup | 1837 s | 1765–1925 | 9.1% |

The spread *within* each arm swamps the 0.8% *between* them. So the honest statement is that units
say −4.6% while total cost is unresolved — not that the change is neutral. Settling it needs a
low-noise instrument (single-threaded deterministic runs, or instruction counts), not more reps of
the same kind. It ships DEFAULT OFF: it clears the adoption bar on quality, there is no evidence it
does the job it was built for, and adopting it would move 17 GT keys for an unrelated benefit.

**Finding 3 — a metric caveat that generalises.** Units and wall disagree here, and *units are the
flattering one*. `units_total` counts SEARCH work; this change trades search work for **hashing**
(`BuildDedupKey` on every candidate), which the unit counters cannot see. The saved rollouts and the
added hashing nearly cancel. Do not price a change that adds NON-SEARCH work in units alone — this
is the mirror image of the wall-vs-units trap, pointing the other way.

**Finding 4 — the real prize is measured UNAVAILABLE, twice.** The win worth having is a skip that
lands *before* the `GameState` copy and `ApplyPlanDirect`, not just before the rollout — and most
duplicates look like pure copy permutations (same cards, same modes, different `Action::hand_index`;
Snow runs multiples of Coldsteel Heart, Scrying Sheets and the snow basics), which are recognisable
from the plan alone. That would skip ~60% of candidates outright. It is unsound:

| signature | copy_perm | **copy_FALSE** |
|---|---|---|
| narrow (kind, name, x, alt, sac_land, dig_sac, discard, splice, float) | 2,531,634 | 949,427 |
| widened: every mode-bearing Action field + `bp_choice`/`searched_order`/`atk_dork_release` | 1,808,436 | **853,079 (32%)** |

`copy_FALSE` = candidates sharing a signature with an earlier sibling that land on a **different**
post-apply state. Widening the signature barely moved it: the plan does not determine the state. A
signature skip would delete ~a third of genuinely distinct lines — a lossy prune, refused by the
standing no-lossy-truncation bar however a suite scores it. Re-check `copy_FALSE` before anyone
retries this.

**Where Snow's cost therefore still sits.** Unchanged from the characterisation above: no hotspot,
cost spread across candidate volume (state copy + apply per candidate), rollouts, and the
`la_bp_wave` price of the site-8 same-turn-playability directive. The deck's structural remedy is
still the value leaf, which is itself blocked by the degenerate tail (section above) — so the tail
remains the thing to attack, and it is now blocking two separate lines of work.

## Branching-factor census 2026-09-09 — WHERE the width is, and the one lever worth building

`MTG_BF_CENSUS` (default off, counts only) answers "which effects cause notable branching factors".
60 Snow games: **57,734 decisions, 4,370,356 candidates, mean width 75.7, max width 2,688.**

**The mass sits in wide decisions**, so a lever that only touches narrow ones is worthless:

| width | decisions | candidate mass | share |
|---|---|---|---|
| 1–32 | 24,606 (43%) | 329,605 | 7.5% |
| 33–64 | 10,453 | 494,297 | 11.3% |
| 65–128 | 14,608 | 1,376,951 | **31.5%** |
| 129–256 | 5,211 | 912,408 | 20.9% |
| 257–512 | 2,161 | 783,386 | 17.9% |
| 513+ | **695 (1.2%)** | 473,709 | **10.8%** |

Decisions ≥257 wide are **4.9% of decisions but 28.7% of the mass**.

**What generates it: the DIG SOURCES, and it is a copy-count problem.**

| card | activation actions | distinct physical sources |
|---|---|---|
| Scrying Sheets | 2,940,620 | **4** |
| Frost Augur | 2,383,807 | **4** |
| Rimefeather Owl | 63,434 | 2 |

5.32M activation actions across 4.37M candidates — more than one per candidate. `play_land`,
`dig_draw`, `searched_order` and `alt_cost` contribute **zero**; `bp_variant` rides along on 42.2%
(the site-8 same-turn-playability re-solve, as the perf section predicted).

*(Read `chosen_x` here carefully: `PermAbilityTaps(TapDraw)` is true, so the K-axis at the
`counts` block is skipped and `chosen_x` is always 1. Its 78.5% share means "most candidates contain
a dig activation", NOT that an X range is being enumerated. An earlier reading of this census made
that mistake.)*

**THE LEVER: fold interchangeable activation SOURCES into a COUNT.** Each untapped Scrying Sheets is
emitted as its own `ActivatePermAbility` action, so subset enumeration explores which *copies* to
tap. With 4 Sheets + 4 Augurs that is up to 2^4 x 2^4 = **256 dig-only combinations**, of which only
**5 x 5 = 25** are distinct decisions — "activate K Sheets and J Augurs". Everything else is a copy
permutation, which is why the dedup census sees 64% duplicates. A fold would be a **~10x cut in the
dig dimension**, on the deck's dominant branching axis.

It is sound in principle *and has an in-repo precedent*: the ETB-blink target fold already collapses
targets by an equivalence key — "same name + tapped-state + sick-state + counter count are
interchangeable in every modelled respect" (`TurnSolver.cpp`, the `etb_blink_permanent` block),
described there as **lossless dominated-action removal, not heuristic narrowing**. Four untapped
copies of one Sheets are interchangeable by exactly that standard; what must be preserved is HOW
MANY are activated, not WHICH.

Two things to verify before building it, because the earlier plan-signature attempt failed on
exactly this ground: (1) the fold must keep the count axis, since one look at the top card differs
genuinely from two; (2) `copy_FALSE` in the dedup census says the post-apply state key distinguishes
`m_number`, so a folded run will NOT be byte-identical — it must be judged on the suite's
slower/faster verdict and the play-settings average, not on digest identity.

## Hand-cast fold + a LOSSY hole in the shipped activation fold (2026-09-09, overnight)

Follow-on to the interchangeable-activation-source fold (`61b3cfb7`). Two separable pieces landed
here; they are deliberately shipped with different defaults.

### 1. The activation fold as shipped was LOSSY. Fixed, default ON, byte-identical.

The canonical-prefix rule keeps the k earliest members of an equivalence class and rejects every
other arrangement. That is a faithful canonicalisation exactly when the members are interchangeable
one-for-one. It stops being one as soon as a single SOURCE contributes two tagged actions -- two
ability modes, or two `chosen_x` counts:

```
copy1 k=1 (idx0)  copy1 k=2 (idx1)  copy2 k=1 (idx2)  copy2 k=2 (idx3)
{idx0, idx3} "copy1 at k=1, copy2 at k=2" -> idx3's class predecessor idx1 unselected: REJECT
{idx1, idx2} its mirror                    -> idx2's class predecessor idx0 unselected: REJECT
```

...so the MIXED arrangement is unreachable **at any budget** -- a lossy truncation, which the
standing bar forbids. **This was LIVE on Snow**, not hypothetical: `Rimefeather Owl`'s
`{2}{S}: put an ice counter` has no `{T}`, so the K-count axis survives and each of the 2 Owls
emits several tagged actions. The new accounting counter reads **47,702 class-drops per 60 games**
from exactly this shape.

The fix is two conditions, both *checked* rather than assumed, in `FinalizeFoldTags`:
* **one tagged action per SOURCE, counted across classes** -- a source with a real choice between
  classes cannot be canonicalised by a per-class prefix, so every class it touches un-folds;
* **every class member identical in every Action field** but its source key (`ActionFoldSig`,
  deliberately exhaustive over all 49 fields).

Plus `RenumberFoldOrds`, which re-derives the ordinals over the candidates the enumeration can
ACTUALLY select. `CapGroupsBySituationalRank` and `DropRitualGroupsIfNoPayoff` remove whole groups
between `CollectActions` and the odometer; if the removed member happened to be ord 0, the
surviving ord 1 would have been rejected with no predecessor left to select, and "cast one copy"
would have gone unreachable in that enumeration.

**Smoke 80/80 ALL PASS, zero churn** -- the lost arrangements never mattered to an outcome on the
current suite, which is precisely why nothing caught this.

### 2. The hand-cast fold: ADOPTED DEFAULT-ON 2026-09-10 (-11.5% greedy work across 40 decks)

`MTG_FOLD_HAND_CASTS=1`. Same canonical prefix, applied to duplicate cards in hand -- the census put
`cast_from_hand` at 77.4% of candidate mass against the activation slice already folded.

| Snow 60 @ play, deterministic counters | fold off | fold on | delta |
|---|---|---|---|
| greedy subsets scored | 72,474,369 | 61,068,943 | **-15.7%** |
| search subsets scored | 6,203,369 | 6,181,471 | -0.35% |
| avg turn-to-win | 5.9833 | 5.9833 | identical |
| play digest | 4d0ae0ea43f9ba15 | 4d0ae0ea43f9ba15 | identical |
| `units_total` | 11,811,112 | 11,811,070 | **-0.0004%** |

Suite quality, audited per game (`audit_changed_games.py`):

| | keys changed | averages moved | searched | d0 |
|---|---|---|---|---|
| smoke | 3 of 80 | 0 | slower=0 faster=0 play-changed=0 | slower=0 |
| regression | 3 of 108 | 1 (fivecolour d0 5.8360 -> **5.8350**, better) | slower=0 faster=0 play-changed=0 | slower=0 **faster=1** |

So: **nothing worse anywhere, one game strictly better, and the searched depths are untouched
entirely.** The residue is 6 keys of d0 play-digest churn at identical scores. It is still default
OFF because adopting it rebaselines ground truth, which is the deck owner's call.

**`units_total` IS BLIND TO THIS CHANGE, AND THAT IS THE REUSABLE LESSON.** `la_cand` charges one
unit per candidate scored *at a top-level search decision*; the greedy rollout leaf -- which is what
this fold actually shrinks -- is not unit-counted at all. The first measurement read "units
identical, candidate census identical" while the subset guard was rejecting 456,201 extra subsets
per 5 games. This is the MIRROR of the trap recorded for the candidate dedup (there, units
*flattered* a change that added hashing): units can equally **understate a change to zero**. The
counters `bf_scored greedy_subsets/search_subsets` were added to settle it deterministically --
unlike wall or CPU they cannot be moved by a contended box, and this box could not resolve it: three
interleaved CPU reps gave -7.0%, +27.7%, -2.8% with a within-arm spread of ~49%.

### 2b. Adoption: measured on every suite deck, then adopted on the USER's call

USER 2026-09-09: *"This sounds entirely positive, so it should be tested for performance and quality
on other decks and then adopted if that holds up."* It held up.

**Performance -- all 40 suite decks/2HG variants, one searched-depth case each, deterministic
subset counters:** every deck got cheaper, none got slower, and the **play digest was unchanged on
all 40**.

| | greedy subsets scored |
|---|---|
| total, 40 decks | 120,445,840 -> 106,617,520 (**-11.5%**) |
| best | hinata2hg **-33.7%**, fluctuator -24.7%, hinata -24.2%, dragonstorm2hg -20.6% |
| worst | kitty -4.0% |
| Snow (not in the suite) | -15.7% |

Hinata is on record as BUDGET-STARVED, so a quarter of its greedy enumeration is real headroom.

**Quality -- three disjoint seed sets, audited per game.** smoke (s1001) 3 of 80 keys, 0 averages
moved; regression (s2002/s3003) 3 of 108 keys, 1 average moved and it is BETTER (fivecolour d0
5.8360 -> 5.8350); **searched depths slower=0 faster=0 play-changed=0**, d0 slower=0 **faster=1**.

**Ground truth rebaselined: exactly 6 keys, all d0** -- five digest-only, one improved. Accepted from
the inspected runs via `regression.sh --smoke --accept` / `--accept`, `check_gt_logs.py` clean at
456.

### 2c. Summoning sickness is an ORDERING concern, not a membership one

USER 2026-09-09: *"we shouldn't need to check the 'entered this turn' unless they are creatures or
the ability makes them a creature ... Scrying Sheets really doesn't care"*, then the stronger form
that was implemented: *"we could also deduplicate to use the oldest copy (which would avoid the
manland summoning sick problem). For that problem, the older one is strictly better."*

The original fold refused to fold anything that entered this turn. That is now dropped, and age is
handled by the canonical ORDER instead -- for free, because activations are emitted walking
`state.battlefield`, which is entry order, so ord 0 IS the oldest copy.

The two cases need DIFFERENT arguments, and the user corrected me for conflating them:

* **Already a creature** (Frost Augur): a summoning-sick one never emits a `{T}` activation at all,
  because the emission sites gate on `CanTapNow` / `!p.CanTap()`. It cannot join the class.
* **Not yet a creature** (a manland; and Scrying Sheets, which never becomes one): `CanTap()`
  returns true unconditionally for a non-creature, so **that gate does nothing here** -- both copies
  are candidates, and as the user put it, *"they may still be summoning sick after the activation."*
  What makes it safe is **dominance, not legality**: sickness is MONOTONE IN AGE. If the newer copy
  is not sick the older one is not either, and the older can be usable when the newer is not, so the
  older is never worse. The "everything else equal" premise is not assumed -- it is exactly what the
  rest of `PermIsPlainForFold` enforces.

Measured contribution on Snow: **~0** on its own (greedy subsets 72,474,369 -> 72,471,495, digest
identical) -- at most one land enters per turn, so the strict form excluded at most one copy. It is
byte-identical across all 188 suite configs. It is in because the strict form was reasoning the
engine does not need, not because it paid for itself.

### 3. TWO ways the canonical prefix was unsound, both found by root-causing ONE changed game

The first cut of the hand fold cost `knights_regression_d0_s2002` gi497 a turn-4 kill. Root-causing
that single game (`explain_game.py`, then a per-reject trace) found two independent holes -- and the
FIRST of them was already present in the SHIPPED activation fold.

**(a) The prefix rule is a statement about a POWERSET, and not every caller is one.** It drops a
non-canonical arrangement because an equivalent twin is enumerated alongside it. That premise holds
for the odometer and for nothing else: the greedy also feeds `consider` hand-CONSTRUCTED lines --
the lethal combo, the Dragonstorm/Apex go-off, the persist loop, the attack-only subset -- each a
single specific selection with no twin generated anywhere. Applying the rule there deletes the line
outright. Fixed with consume-once provenance (`foldsel::Take()`): the walker marks its emit, the
receiving `consider`/`eval_and_push` takes and clears it, so a constructed line evaluated inside a
nested rollout cannot inherit its caller's value.

**(b) A class member's SOURCE must offer nothing but that one action -- counting UNTAGGED actions
too.** The fatal selection was `{Marshal of Zhalfir cast from slot 1, Marshal of Zhalfir VIALED from
slot 0}` -- two Marshals, exact lethal. Its canonical twin is `{cast from slot 0, vial from slot 0}`,
which is **the same hand slot twice** and therefore never enumerated. An Aether Vial deploy is a
second action on a hand slot and is not a `CastFromHand`, so the original form of the condition --
which counted only TAGGED actions per source -- could not see it. The count now spans every action
sharing the source key, tagged or not, because that key is what `PlanGroupKey` buckets on and
same-group actions are mutually exclusive by construction.

**The method is the transferable part.** The aggregate said "net 0.0000, one better one worse" --
which reads like noise and would have been accepted as such. Six rejects existed in the entire
losing game, five harmless and one fatal, so nothing short of dumping the rejected selection **with
the names of its class members** would have shown it. The standing "root-cause every worse game" bar
is what turned a plausible-looking wash into two real defects, one of them in already-shipped
default-ON code.

### The hand-ORDER objection, checked (it was already written down in the tree)

`BuildFungibleEquipClasses` -- the pre-existing group-level fungible-copy collapse for Equipment
(`MTG_EQUIP_COPY_COLLAPSE`, also default off, also for digest reasons) -- carried a comment
objecting to exactly this: *"Hand CASTS are not collapsed here even though two copies of one card
in hand are equally fungible: casting from a different hand slot leaves a different hand ORDER,
which a later discard or reveal can read. That case needs its own argument, not this one."*

The objection is real and is narrower than it reads. Removing the copy at slot i rather than slot j
leaves the same hand MULTISET but a different SEQUENCE -- `[A,B,X,C,X,D]` becomes `[A,B,C,X,D]` or
`[A,B,X,C,D]` -- so it bites for any rule that reads a hand POSITION *blind to what is in it*. The
engine has exactly one such read, `AIEngine::ChooseDiscard`'s `if (heur < 0) { return &ap.hand[0]; }`,
and **it is unreachable**: `CleanupDiscardRanking` returns early only on an empty hand, so a hand at
the 8-card limit always produces at least one candidate. Every other discard/reveal path ranks by
CONTENT (mana value, required-piece protection, spare-copy banding) and breaks ties by index, so a
tie between two identical copies names a card of identical CONTENT either way.

What genuinely can differ is the surviving copy's `m_number`. That is not a leak in the argument --
it IS what "interchangeable copies" means -- but it is why the fold moves play digests: 10 of 80
smoke keys, every average identical, and across all 188 suite configs slower=0 / faster=0. The
comment has been updated in place rather than left contradicting the code.

Two further consequences of the same m_number point, already handled: the fold is disabled under
`HumanPlayActive()` (a saved `references/` game replays by card number, and the viewer must keep
offering the human every copy), and `m1_hand` membership -- the order-condemnation snapshot, which
is keyed by number -- joins the equivalence tag so a condemned copy never folds with a fresh one.

### The trap that cost the most time here

The first cut of the hand-card test asked "is this card in its PRINTED form", comparing each hand
card against its `CardDatabase` definition. It rejected **767,406 of 767,406** hand casts: cards
outside the battlefield are DeckLoader PLACEHOLDERS carrying a name and nothing else, so a
placeholder never equals its own printed definition. The fold silently did nothing and every gate
still passed -- the same shape as the Coldsteel Heart `chosen_color` no-op recorded above, and the
same placeholder-vs-definition confusion. The replacement compares the two CARDS to each other
(`HandCardContentHash`, exhaustive over Card's 15 fields), which needs no reference form at all.
A second, subtler instance of the same class: `ActionFoldSig` initially hashed `sac_source_id`,
which is the very field two interchangeable copies differ in -- every activation class failed the
identity check and the fold stopped firing, detected only because the branching census read back
the unfolded mean width (75.96) to four significant figures.

## Open questions for the user (surfaced, not blocking)

1. ~~`{S}` modelled as generic `{1}`~~ — **CLOSED 2026-09-06** by the real snow-mana model
   (`b1551194`, section above). The line survived here after being resolved; do not re-raise it.
2. **The nine PROVISIONAL card deferrals still have no sign-off** ("Approved deferrals: none yet").
   Eight are inert by construction; **Coldsteel Heart is the one that is not** — see below.
3. ~~**Coldsteel Heart does not ask for a colour**~~ — **IMPLEMENTED 2026-09-08.** USER ruling:
   "We should probably implement Coldsteel Heart. In the search we can heuristically pick either
   Green or Blue depending on what our other sources are producing ... You choose whatever you
   have less of in hand and board." Shipped as `CardParams::etb_choose_color` +
   `Permanent::chosen_color`, resolved at entry as an as-enters REPLACEMENT effect (CR 614, no
   stack, ahead of every ETB trigger), read through `EffectiveProducesFor` /
   `ProducesForPayment(perm)`. Heuristic, NOT searched (a five-way branch per rock would multiply
   the plan space at the site this deck can least afford). The human is asked in the viewer
   (`choose_color` decision). Off-switch `MTG_ETB_COLOR_LOCK=0`; telemetry `MTG_ETB_COLOR_STATS`.
   **APPROVED DEFERRAL (phase 2): Red is excluded from the candidate set.** USER: "Technically the
   deck has red, but there is no point in worrying about that until Skred is active." Skred is
   goldfish-inert, so its 4 real {R} pips are demand for a spell that cannot matter; when Skred
   becomes live, DELETE `SnowProvider::EtbChosenColor` so the deck falls back to the generic
   demand rule rather than growing a third case.
   **THE BUG THIS ALMOST SHIPPED WITH, because it is the reusable lesson.** The first build chose
   a colour **zero times in 248,645 entries** and every gate still passed — unit 74/74, scenarios
   74/74 (including two new ones written specifically for this feature), and a 300-game A/B that
   came back byte-identical. Cause: `EtbChosenColorFrom` read a hand `Card`'s own `m_mana_cost`,
   which is a PLACEHOLDER on a zone handle (the real cost needs `LookupCached`) — so every demand
   count was 0 and the heuristic returned -1 every time. The fixtures all STAGE `chosen_color`
   explicitly and so never exercised the entry path at all. **"The feature does nothing on this
   deck" and "the feature never runs" are indistinguishable from an unchanged average**, which is
   the exact rationale `MTG_REFLOAT_STATS` was created for; `MTG_ETB_COLOR_STATS` now tells them
   apart in one run (U=97,111 G=47,965 on three games). Any future param-gated feature whose A/B
   comes back byte-identical must prove its code RAN before that result is believed.

   The original report, kept for context (USER, hand-playing references 2026-09-08: "that
   is kind of an issue"): The ETB "choose a color, locked forever" is unmodelled, so each of the
   4 copies taps for ANY of WUBRG every turn instead of one colour fixed at ETB. This is the one
   deferral that is **over-permissive in the engine's favour** — a real Snow deck's Hearts are
   locked and it has colour-screw risk this engine never faces, in a 3-colour (U/G/R) deck running
   4 copies. It inflates the deck's numbers, and it is a MODELING bug (rules-arbitrated), not a
   heuristic to tune.
   **Scope, measured honestly:** there is NO per-permanent chosen-colour machinery anywhere in the
   engine. `EffectiveProduces(state, controller, def)` is keyed on the DEFINITION, with ~78 call
   sites, so a locked colour needs a `Permanent` field, an ETB decision (searched or heuristic),
   viewer prompting, and per-permanent context threaded through the mana spine — carrying the
   "grep every raw `produces` read" hazard recorded in `any-color-filter-and-raw-produces-reads`.
   A real project, not a patch. Precedent for the current simplification: Cavern of Souls,
   Unclaimed Territory, Secluded Courtyard.
   **Ordering consequence:** fixing it CHANGES PLAY, so a value leaf generated before the fix must
   be regenerated after it.
4. **Snow is still not in the regression suite** — a shared-budget sizing call, and an expensive
   one at 4.9 s/game mean.
