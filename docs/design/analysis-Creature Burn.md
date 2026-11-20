# Analysis ledger — Creature Burn

Deck: `decks/Creature Burn/Creature Burn.cod` (60 cards, mono-R: 18 Mountain, 2 Teetering Peaks, 1 Browbeat,
2 Rift Bolt, 1 Flame Javelin, 3 Fireblast, 4 Lightning Bolt, 4 Magma Jet, 2 Price of Progress, 2 Searing Blaze,
4 Ball Lightning, 2 Cinder Pyromancer, 2 Figure of Destiny, 4 Goblin Fireslinger, 2 Hellspark Elemental,
2 Keldon Champion, 1 Keldon Marauders, 2 Spark Elemental, 2 Vulshok Sorcerer).

Started 2026-11-20 on `phase-1-2-deck-analyzer` @ 4f0ee3a94 (12-thread / 10 GB box).

## Stage 1 — coverage

* Missing (8): Browbeat, Flame Javelin, Cinder Pyromancer, Goblin Fireslinger, Keldon Champion,
  Keldon Marauders, Vulshok Sorcerer, Teetering Peaks.
* All other 11 cards `full`; bracket notes on Ball Lightning / Spark Elemental / Hellspark Elemental are the
  existing accepted trample-inert notes (shared with other burn decks).
* No sideboard; no fetchlands.

## Stage 2 — cards

(in progress — per-card research fan-out, 4 agents)

## Stage 2 — Batch 1 — implemented (integrator 1, 2026-11-20)

Cards: Keldon Champion, Keldon Marauders, Flame Javelin, Browbeat. Every clause modelled; Scryfall re-fetched
for each (mana_cost / oracle pasted verbatim).

| Card | Tier | cards.json | C++ |
|---|---|---|---|
| Keldon Champion | 1 | `custom`, `echo_cost {2}{R}{R}`, `etb_damage_any 3`, keywords `[Haste]` | none (echo is the searched echo axis; ETB face damage the existing cascade) |
| Keldon Marauders | 3 | `custom`, `vanishing_counters 2`, `etb_damage_any 1`, `ltb_damage_any 1` | NEW `CardParams::vanishing_counters` / `ltb_damage_any`, `Permanent::time_counters`, `PerformUpkeepVanishing` |
| Flame Javelin | 1 + sound fold | `direct_damage`, `damage 4`, `targeting any` (twobrid parse exists) | NEW `MTG_TWOBRID_COLOUR_FOLD` (default ON) in the CollectActions twobrid post-pass; `MTG_EMIT_PRUNE` exempts twobrid |
| Browbeat | 2 | `draw_spell`, `draw 3`, `opp_may_take_damage_instead_of_draw 5` | NEW param, `BrowbeatOpponentTakesDamage` (policy, ONE place) + `ResolveDamageInsteadOfDraw` (both worlds) |

C++ sites (all gated on the new params / a nonzero time counter, so other decks are untouched):
* **Vanishing** — counters put AS IT ENTERS at the universal enter cascade (`FireEtbWatchers`, beside
  `enters_with_plus_counters`, doubled by `DoublerShift`) and in `ApplyEntersWithOnPut` for simultaneous
  multi-puts (+ the three multi-put shift-detection loops). `PerformUpkeepVanishing` (SpellEffects.h, beside
  `PerformUpkeepFading`): remove one per upkeep, sacrifice through `SacrificePermanentAt` when the LAST comes
  off (Vanishing 2 = sacrificed at the 2nd upkeep); called at BOTH upkeep sites (`GameEngine::UpkeepTail`,
  `TurnSolver::SimulateEndAndStartNextTurn`). `time_counters` folded, NONZERO-gated, into every key that carries
  `fade_counters` (TurnSolver BuildSimKey Mix / signature string / fold-class refusal + tag / equip fold refusal /
  land-class plain test / permanent equality / turn-shape mix / transposition key; Dominance exact-match
  `mfold`; SpellEffects plain-permanent test + fungibility key + `ForEachDedicatedCounterField`;
  DecisionProviders state vector + combo-route key), the viewer badge (`main.cpp` + a `.cnt.time` style), the
  game-log snapshot, and the `--scenario` loader (`time_counters`). **`sizeof(Permanent)` 328 -> 336** (no pad
  hole left; Dominance.h note + assert updated).
* **LTB damage** — `FireLeavesBattlefieldTriggers` (every leave route that calls it, both worlds):
  `DealPermanentAbilityDamage` at the face inside a `NoncombatTriggerScope`.
* **Twobrid fold** — when the turn pool + floating mana is a single colour (no `{C}`, no wild), every own
  permanent (`EffectiveProducesFor`) and every land in hand makes only that colour, no land Aura, and nothing
  reads mana spent (`cast_trigger_untap_pump_target_min_spent`), the fewest-mana variant (ties: most coloured
  pips) dominates and is the only one emitted. Decision space only; human play unfolded. Prevent Damage
  (Beseech) never qualifies (all multi-colour / `{C}` lands).
* **Browbeat** — `ResolveDamageInsteadOfDraw` at the top of `EffectHandler::ResolveDrawSpell` and the rollout's
  `apply_one` DrawSpell branch: taken -> `DealPermanentAbilityDamage` 5 at the face and `n = 0` (the draw
  breakpoints still run, with the hand unchanged, so the worlds stay in lockstep). Held-card count excludes
  STAGED cards (the rollout keeps them in the hand vector). `EvalCard` policy-aware; `ClassifyMainPhase` ->
  `Both`; spectacle credit `else if` (held = true) beside the Devil's; `GoldFishRunner::DeckUsesSecondMain`.
* Scripts: `vanishing` in `MODELED_ELSEWHERE_KEYWORDS`; `vanishing_counters`, `ltb_damage_any`,
  `opp_may_take_damage_instead_of_draw` in the viewer auditor's no-decision manifest.

Tests: `test/unit/test_creature_burn_batch1.cpp` (19 cases: parse; Champion ETB both worlds; Marauders enter
counters + ping both worlds, upkeep sequence via the shared upkeep and via the rollout's turn boundary, no-counter
case, upkeep-lethal leave in the rollout, sac-outlet leave, multi-put pass; Javelin parse, RRR rollout cast, fold
= `{3}` on Mountains (and with a Mountain in hand), full fan with an Ancient Tomb on the board or in hand;
Browbeat parse + second-main, take at 20, empty hand 5 declines / 6 takes, holding 8 declines / 9 takes, staged
not held, 2HG team life x1). Scenarios `cb_flame_javelin_bolt_rrr_lethal` (4 Mountains, Javelin RRR + Bolt =
7, T4 win) and `cb_keldon_marauders_vanishing_upkeep_kill` (the executor's upkeep leave kill, T4, opponent ends on
exactly 0).

**PROVISIONAL (USER-owned, surfaced, not decided by the integrator):**
1. **Browbeat opponent policy A — USER RULING 2026-11-20** (no longer provisional). Verbatim: *"I would
   probably do the same thing as devil, which is take the damage unless it puts you within 3 life."* and,
   clarifying: *"So, you take it if you are at 9 or more life."* then *"No, your idea is good. (hand empty = take
   the damage unless lethal)"* => policy A exactly as implemented in batch 1: while we hold any card the opponent
   takes the 5 iff its life (team life in 2HG) >= 9; with our hand empty it takes unless lethal (life >= 6).
2. **Browbeat opens the second main** (`DeckUsesSecondMain`) — the post-combat cast meets a different
   opponent choice. Its cost (d3/d5 ms/game with and without, `MTG_FORCE_USES_M2`-style) is still to be measured
   once the deck plays.

Known risk carried from the research: Keldon Champion's echo payment created by applying a plan at a
single-main boundary (`BoundaryFan(state, /*with_echo=*/false)`) is not branched -> measure with
`MTG_ECHO_AXIS_STATS=1` once the deck plays.

## Stage 2 — Batch 2 — implemented (integrator 2, 2026-11-20)

Cards: Goblin Fireslinger, Vulshok Sorcerer, Cinder Pyromancer, plus the Browbeat PRE-PING sub-decision (the
[ORCH] soundness amendment). Every clause modelled; Scryfall re-fetched for each (mana_cost / oracle pasted
verbatim).

| Card | Tier | cards.json | C++ |
|---|---|---|---|
| Goblin Fireslinger | 2 | `vanilla_creature`, `tap_ping_damage 1`, `tap_ping_target player` | NEW `CardParams::tap_ping_damage` / `tap_ping_any_target` |
| Vulshok Sorcerer | 2 | `vanilla_creature`, `tap_ping_damage 1`, `tap_ping_target any`, keywords `[Haste]` | (same) |
| Cinder Pyromancer | 3 | `vanilla_creature`, `tap_ping_damage 1`, `tap_ping_target player`, `cast_color_untap_self R` | NEW `CardParams::cast_color_untap_self`, `TapPingCastUntap`, `GameState::opt_pings` |
| (Browbeat) | 3 | unchanged | NEW `Action::browbeat_flip` searched variant, `BrowbeatFlipPrePings` |

`tap_ping_target` is a DEDICATED targeting key (never `targeting`, which ~29 readers treat as a spell target);
it only shapes the human `target` decision (players only vs the full set). Parse errors on a bad value.

C++ sites (all gated on the new params or on `GameState::deck_has_tap_ping`, so other decks are untouched):
* **The two whole-turn folds** (Zhur-Taa Druid's, SpellEffects.h): `TapReadyPingDorks` now also taps every READY
  tap pinger (`TapPingerReady`: own, untapped, `CanTapNow`) through `ResolveTapPing` (taps it, then
  `DealPermanentAbilityDamage` at the face inside a `NoncombatTriggerScope`; x1 in 2HG -- "target player" is one
  head; human play asks the `target` decision, players only via a definition copy with `targeting = Player`
  for "target player or planeswalker"). The Druid branch is tested FIRST at each battlefield index and is
  unchanged, so a Druid deck walks and taps exactly as before. `PingDorkEndSweep` (first thing in our end step,
  `GameEngine::EndStep` / `TurnSolver::SimulateEndAndStartNextTurn`) and `PingDorkFinish` (right after combat
  damage, `GameEngine::CombatPhase` / `SimulateOneCombat`; its reach adds the pingers' x1 pings) are the
  unchanged lockstep call sites.
* **Cinder Pyromancer's untap** (`TapPingCastUntap`, called at the end of `FireOnCastTriggers` -- every cast site
  in both worlds: hand cast, Fireblast's alternative cost, Javelin's twobrid cost, a cast off suspend; the
  suspend special action and Hellspark's unearth are not casts). The flag is collected inside the existing
  battlefield loop (no extra walk for any deck). Tapped copy -> untaps (the "may" always taken: untapped
  dominates vs the goldfish). Untapped able copy -> pings IN RESPONSE, then untaps (+1 damage, same board), and
  the ping is recorded as WITHDRAWABLE (`GameState::opt_pings`, turn-gated by `opt_pings_turn`, read via
  `OptPingsLive()`; folded value-/turn-gated into `BuildSimKeyBase`; dead at every turn boundary, so not in the
  Dominance key -- it compares only end-of-turn states).
* **The attack hold** (`DecisionProvider::AttackWith`, shared by every declaration and projection): a pinger whose
  `CombatPowerOf` is below its ping and whose attack has no non-power value stays home (a 0/1 Pyromancer --
  attacking taps it for 0 and forfeits the end-step ping: dominated). A 1/1 pinger attacks for exactly its ping
  (the standing attack-all exemption decides it); a Teetering-Peaks-pumped Pyromancer attacks.
* `ConvokeBodyTapAbilityLive`: a live tap pinger is a costly convoke body.
* **Deck stamp** `GameState::deck_has_tap_ping` (`StampDeckTraits`, default FALSE: a clear bit only withholds
  the flip variant and the opt-ping record).

### The Browbeat pre-ping sub-decision -- design and soundness argument

Browbeat's PROVISIONAL opponent policy (`BrowbeatOpponentTakesDamage`) reads the opponent's LIFE as it resolves,
so the pingers' folds (which fix WHEN their damage lands) would make two lines unfindable at any budget: "ping
first, so the opponent declines" and "do not ping in response to the Pyromancer's untap trigger, so the opponent
takes". Re-check of the whole 19-card list: **Browbeat is the only card that reads the opponent's life (or damage
dealt) before the end step** -- Fireblast (sacrifice alt cost), Searing Blaze (landfall, not life), Price of
Progress (nonbasic lands), Rift Bolt, Lightning Bolt, Magma Jet, Flame Javelin, Keldon Champion / Marauders, Ball
Lightning, Spark / Hellspark Elemental, Figure of Destiny, Teetering Peaks and the pingers read nothing of the
kind. (The engine's own mid-turn lethal checks are not game rules; a ping that would be lethal is fired by the
post-combat finish, and the end-step sweep wins the same turn.)

Cheaper formulation than "k = 0..ready pre-pings", and sound: the policy's outcome is BINARY in the life
(take iff life > T, T = 5 + (we hold a card ? 3 : 0); pings change neither the hand nor T). So the search needs
only ONE extra variant per Browbeat cast, `Action::browbeat_flip = 1` = "the OTHER outcome at the least cost"
(`BrowbeatFlipPrePings`, applied inside `ResolveDamageInsteadOfDraw` just before the opponent chooses, both
worlds -- the executor via `CastRiders` -> `StackEntry::browbeat_flip` -> `EffectHandler::ResolveDrawSpell`, the
rollout via `apply_one`'s riders, the breakpoint replay record included):
* base outcome TAKE -> fire the FEWEST ready pings that bring life to <= T (life - T of them). A ping fired now
  rather than at the end step loses nothing, except a pinger that could still attack this turn, so they fire in
  ascending combat power (a 0-power Pyromancer first) -- the min-loss order. Too few -> nothing fires.
* base outcome DECLINE -> withdraw the FEWEST withdrawable Pyromancer response pings (T + 1 - life of them, at
  most `opt_pings`): exactly the world in which the Pyromancer did not ping in response to those earlier red
  casts (its board is identical either way -- it untaps regardless). Too few -> nothing. Not used while the
  damage-event layer or the noncombat-damage trackers are armed (their records cannot be withdrawn; no such deck
  has a pinger).
Any other number k of pre-pings yields one of the same two outcomes with no more total damage (total ping damage
is fixed: an unfired ping fires at the end step; a withdrawn one is lost, and the flip withdraws the fewest), so
it is dominated -- a sound fold, not a pick. A flip that cannot change the outcome is a copy of its base, which
the post-apply dedup absorbs. The variant is emitted by a `CollectActions` post-pass (decision space only:
searched collect, executor, human play -- the human picks it as a `main_phase` plan variant tagged "pre-ping";
a rollout leaf keeps the base, the beyond-horizon playout), only when `deck_has_tap_ping` and a pinger is on our
battlefield or in our hand. It shares the base's group key (mutually exclusive) and is folded, set-gated, into
every action / plan signature (`ActionFoldSig`, plan fingerprint, `#BB` in both plan signatures, the subset
substitution key, the action diff and equality, `main.cpp`'s viewer identity keys, tag and JSON
`browbeat_flip`).

Residual, disclosed (not a loss of this fold): an ATTACKING 1-power pinger deals its damage in combat, before a
main-2 Browbeat, exactly like every other attacker; whether to attack at all is the standing attack-all
exemption, unchanged by this batch.

## CHECKPOINT (orchestrator, 2026-11-20 ~12:00 UTC) — resume here after compaction

* Done: Stage 1; Stage 2 batch 1 committed `0f09f66c6` (+ ledger `447af13b9`, Scryfall snapshot `28c9221fb`).
  Browbeat policy A is USER-ruled (see item 1 above).
* RUNNING: integrator 2 (subagent) — Goblin Fireslinger, Vulshok Sorcerer, Cinder Pyromancer + the Browbeat
  pre-ping sub-decision; relabel Browbeat note/comment to USER-ruled (no behaviour change). Drafts:
  `logs/creature-burn/stage2-drafts.md` (Batch 2, incl. the [ORCH] soundness amendment). Handoff, if any:
  `logs/creature-burn/integrator2-handoff.md`. It commits locally, never pushes.
* NEXT, in order:
  1. When integrator 2 has committed: `git pull --rebase` onto origin (USER 2026-11-20: origin landed the
     slow-reference fix `08b13f602`, pay-refine node cap; ~40 commits incl. GT accepts) -> `./build.sh` ->
     unit + scenarios -> `python3 test/check_gt_logs.py`.
  2. Integrator 3: Teetering Peaks (ETB target pump axis + land-position-after-haste-cast) + the deck's OWN
     provider `CreatureBurnProvider`. NOTE: today the deck routes to **RedElementalsProvider** (Hellspark's
     unearth_cost; pinned in test_pirates_provider.cpp) — derive from that per analyze-deck 4a, signature OR-ed
     over the new params, `provider_audit.py --check`.
  3. Stage 3 coverage (all full) -> Stage 4 `analyze_deck.py` profile -> 4-bis suite rows (all 3 tiers; copy the
     onedropburn shape; `suite_gate.py --cost` vs Hinata 3x) -> Stage 5 (`verify_deck.py`, nonconv/fd-diverge,
     multi-depth, leaf tie-break, claude-play sweep ~15-20 games, viewer audit 5h, discard buckets 5i).
  4. Measure + surface: Browbeat's DeckUsesSecondMain cost (d3/d5 ms/game with vs without);
     Keldon Champion echo boundary gap (`MTG_ECHO_AXIS_STATS=1`).
* Status timer: session-only cron `7 */2 * * *` (fires 12:37, 14:37 UTC...). Push: slow 12-thread box — join the
  push queue at most once per 4 h; subagents never push.

Browbeat policy relabel (USER ruling 2026-11-20, relayed by the coordinator; NO behaviour change): the code comment
and the cards.json bracket note now read USER-ruled, quoting *"I would probably do the same thing as devil, which
is take the damage unless it puts you within 3 life."* and *"No, your idea is good. (hand empty = take the damage
unless lethal)"* -- policy A exactly as batch 1 (take iff life > 5 + (holding ? 3 : 0)). The play-event strings
dropped their "[PROVISIONAL policy A]" suffix; `BrowbeatTakeThreshold(amount, holding)` names the same value.

Scripts / viewer: `audit_viewer_decisions.py` MANIFEST `tap_ping_damage -> target`, INERT_PARAMS `tap_ping_target`
and `cast_color_untap_self`, the oracle cross-check counts a tap pinger as modelling `target` (static audit clean);
`tools/play/DECISIONS.md` rows for the pinger `target` reuse and the Browbeat `main_phase` "pre-ping" variant.

Tests: `test/unit/test_creature_burn_batch2.cpp` (12 cases: parse; end-step sweep -- ready / summoning sick /
haste, nothing twice; 2HG x1 per ping; post-combat finish only when lethal; Pyromancer two red casts = two response
pings + one end-step ping in BOTH worlds with `opt_pings` 2; summoning-sick copy pings nothing, tapped copy just
untaps; a cast off suspend (Rift Bolt) triggers it and a non-red spell does not; the 0-power attack hold, a 1/1
pinger attacks, a pumped Pyromancer attacks; Browbeat flip base TAKE -> one ping first -> DECLINE in both worlds,
the second pinger still pings at the end step; base DECLINE -> withhold the response ping -> TAKE in both worlds;
no-flip-exists cases equal the base; emission only with the stamp and a live pinger). Scenarios
`cb_cinder_pyromancer_response_ping_lethal` (opp 5, Bolt + response ping + held Pyromancer's finish = T3 win) and
`cb_cinder_pyromancer_held_finish_lethal` (opp 2, Fireslinger attacks, held Pyromancer's finish = T3 win). Test
seam `MtgTestSeam::CastBrowbeatFlipFromHand`. **Unit 3775/3775, scenarios 593/593** on the post-change Release
build.

Audits: `audit_card_costs.py` on the batch's cards (3 + Browbeat) rc 0, 4/4 compared; `audit_card_fields.py
--update` merged the three into `scryfall_reference.json`; offline diff rc 0 (oracle advisories = bracket notes).
Coverage: all three `full`; only Teetering Peaks missing.

**Byte-identity of other decks: NOT YET RUN** (integrator handed off on the context rule). Owed: `bash
test/regression.sh --smoke --deck=boars,pd,onedropburn,burn,landfall,hinata,redelem,lhc,midrangeburn,goblins`,
play-changed must be 0 (pre-change binary kept at `build/scratch/cb2/mtg.base`). Expected 0 by construction: every
new path is gated on `tap_ping_damage` / `cast_color_untap_self` / `deck_has_tap_ping` / a nonzero
`browbeat_flip`, the Druid branch of `TapReadyPingDorks` is tested first and unchanged, and sizeof(GameState) is
unchanged (1712).

**PROVISIONAL / disclosed (surface to the USER):**
1. Human play: Cinder Pyromancer's response ping is always fired (with its `target` decision); a human withholds
   it only through the Browbeat "pre-ping" plan variant, whose withdraw is an accounting un-ping (life restored).
   The 5h viewer check of that variant (and whether `tools/play/linebuild.js` needs a composer token) is owed.
2. A 1-power pinger attacking vs pinging is the standing attack-all exemption (equal damage); its combat damage
   before a main-2 Browbeat is the same as any attacker's.
3. `BrowbeatFlipPrePings` assumes 1-damage pingers (all three cards) and does not withdraw while the damage-event
   layer or the noncombat-damage trackers are armed (no deck with a pinger arms them).
