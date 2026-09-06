# Analysis ledger — Snow

Deck: `decks/Snow/Snow.cod` (mainboard only; sideboard unreachable — no wish effects — and user
explicitly said to ignore it).

Status: **IN FLIGHT** (Stage 2 implementation COMPLETE + built; Stage 3 coverage CLEAN; unit
tests 70/70 + scenarios 72/72 PASS on the new binary; currently tuning the site-8 perf fix, then:
byte-identity smoke for existing decks → Stage 4 profile + provider audit → Stage 5 battery).

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

**Still to do**: byte-identity smoke (existing decks must not move — bp mask + fed-slots
refactor are the risky shared edits); scryfall_reference --update + audits (2d-bis); Stage 4
analyze_deck + provider_audit (expect Generic); Stage 5 battery (verify_deck.py, nonconv/
fd-diverge, multi-depth, 5c2 leaf_tiebreak_check, 5d claude-play sweep ~15-20 Sonnet agents,
5h audit_viewer_decisions sweep); DECISIONS.md rows (gyplay verb + dig-reuse notes); Stage 6
report w/ full deferral disclosure; LAST: forward-looking snow-mana model (USER directive 3).

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

## Open questions for the user (surfaced, not blocking)

1. `{S}` modelled as generic `{1}` (see above) — PROVISIONAL.
