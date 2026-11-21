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
**MEASURED 2026-11-21 (`c5f3b7610`, 200 games, seeds 2002/3003 x100, play settings d5 b20): NO GAP.**
`[echo-axis] obligations=22115 covered=10910 inverted=5423 uncovered_in_window=8703 uncovered_beyond=2502 | real:
obligations=15 uncovered=0`; split `searched=0 playout=8703 constant_leaf_edge_fold=0`. Every in-window miss is in the
permitted playout beyond the horizon; no searched frame resolved an uncovered echo, and every real upkeep was covered.

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

## CHECKPOINT (orchestrator, updated 2026-11-21 ~00:45 UTC) — resume here after compaction

* Done (all local, NOTHING PUSHED yet): Stages 1-4, 4a provider, 4-bis rows in all 3 tiers + GT accepted
  (`a478b813c`), claude-play sweep (`3cf881139`; its one real flag fixed by `c5f3b7610`, ledger `57744efa4`),
  DECISIONS.md echo row (`690bded3b`), echo census NO GAP (`d89428098`), Scryfall subset refresh of all 19 deck cards
  (snapshot unchanged; hard fields all match). verify_deck `--no-sweep`: every gate PASS except claude_sweep, which
  should now pass (0 unresolved).
* RUNNING (my own probes, killable):
  1. Leaf tie-break EXTENSION, pid in `build/scratch/cb-orch/tiebreak-ext.pid`, log
     `build/scratch/cb-orch/tiebreak-ext.log` (EXIT line at end), batch out `logs/leaf_tiebreak/creatureburn-ext.out`.
     `--seed-base 2200000 --blocks 12`, MTG_PGO=0. Run 1 (seeds 1000000..2100000, saved in
     `build/scratch/cb-orch/tiebreak-run1.{log,out}`): 12000 paired, 6 changed, net -8 turns, 0 worse / 6 better,
     both halves -4 => NO SIGN (needs 20). Pool run1 + ext (24 blocks); if still < 20 changed, default ON stands
     ("a lever that never fires costs nothing"); a consistent sign that helps also means keep ON (no opt-out).
  2. Browbeat second-main PROBE build in worktree `.claude/worktrees/cb-m2probe` (detached HEAD 57744efa4 -- same src as HEAD, one
     uncommitted line in `GoldFishRunner::DeckUsesSecondMain`: `MTG_CB_PROBE_BROWBEAT_M2=0` drops Browbeat's
     trigger; NEVER land it), log `build/scratch/cb-orch/m2probe-build.log`, run via `scripts/box_lane.sh --slots 4`.
* NEXT, in order:
  1. When BOTH are done: one pooled batch ALONE on the box with the probe binary, both arms same binary
     (lever on vs off), d3 b10 and d5 b20, ~150 games/arm on regression seeds; report ms/game + avg win turn per
     arm to the USER (cost of Browbeat opening main 2; soundness says keep it ON). Then remove the probe worktree
     with `scripts/safe_worktree_remove.sh` (after `check_references_safe.py`).
  2. Pool the tie-break verdict; act (record in ledger; opt-out only if it measurably HURTS).
  3. `python3 scripts/verify_deck.py "decks/Creature Burn/Creature Burn.cod" --write-ledger` to a clean gate.
  4. Stage 6 report with the 6a disclosure + USER items: MTG_CB_PEAKS_PLAIN_FOLD default ON (provider prune, sound
     only under attack-all vs never-blocking goldfish); Browbeat dropped from the provider signature; extra Peaks
     land positions; Browbeat second-main cost; echo census result; Pyromancer human-play response ping +
     accounting un-ping; 1-power pinger attack-vs-ping exemption; Peaks inert-class premise (revisit with a
     Fling-type card); Price of Progress vs Peaks self-damage (inert); heavy unbudgeted tail; provider cert
     NotAssessed; the Blaze fix agent's open question (default prefers the TOUGHEST opponent creature; Midrange_Burn
     `claude_s10_gi9` label changes "1/1"->"2/2"; one-line change to keep list order).
  5. Push: `python3 scripts/push_queue.py join` once (slow box, <= once / 4 h); at the turn: rebase,
     `check_references_safe.py`, Release + unit + smoke (MTG_PGO=0) + `check_gt_logs.py` under `push_queue.py hold`,
     then `land`; watch CI in background `gh run watch <id> --interval 60 --exit-status`.
  6. CronDelete the status timer `b718d9a3` once done; `check_references_safe.py` at session close.
* Never commit `.vscode/settings.json` or `test/ref_bench.json` (not ours). Note: `build/PGO` is stale (a PGO build
  failed while the fix agent's edits sat in the primary tree); harness falls back to Release, byte-identical.
* Status timer: session-only cron `7 */2 * * *` (id b718d9a3).

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

## Stage 2 — Batch 3 — implemented (integrator 3 / 3b, 2026-11-20)

Commit `3bb70b590`. Card: **Teetering Peaks** (Scryfall re-fetched; oracle verbatim). Plus the deck's own provider
(`CreatureBurnProvider`, analyze-deck 4a) and the batch-2 viewer item (Browbeat pre-ping in the composer).

| Card | Tier | cards.json | C++ |
|---|---|---|---|
| Teetering Peaks | 3 | `basic_land`, `produces [R]`, `enters_tapped`, `etb_target_creature_pump_power 2` | NEW `CardParams::etb_target_creature_pump_power` / `_toughness`; `ResolveLandEtbTargetPump`; `GameState::deck_has_land_etb_target`; `TurnSolver::LandSlotCounts` |

C++ sites:
* **The trigger** (`ResolveLandEtbTargetPump`, SpellEffects.h): ONE resolver called from both land-entry sites,
  `PlayLandCore` (every land drop, both worlds) and `EnterLand` (an effect put), before landfall. Legal set = every
  creature on the battlefield, either side, minus shroud and an opponent's hexproof creature. Empty -> the trigger is
  removed (CR 603.3d) and the land just enters tapped. +2/+0 via `temp_power_bonus`.
* **Sound folds** (`LandEtbPumpReps`): identity (`LandfallTargetFoldKey`), and the INERT class (an opponent's
  creature, a summoning-sick non-haste creature, anything once `StealCombatOver`). Inert candidates are dropped while
  an attack-capable candidate exists; otherwise all are one class. Tapped is NOT inert (`CanAttackFull` asked as if
  untapped: Cinder Pyromancer untaps on a red cast). Pingers keep their own classes.
* **Searched target**: `Plan::landfall_target_choice` / `LandfallTargetPinFor` reused, emitted in
  `AppendSubdecisionAxes` on the `deck_has_land_etb_target` stamp (not `deck_has_landfall`) for a plan whose land
  carries the pump; width = board classes + counted casts/unearths before a positioned land, capped at 4.
* **Land position (missing capability closed)**: `TurnSolver::LandSlotCounts` counts a HASTE creature cast (hand,
  graveyard, Vial) for a pump land on a stamped deck, so "Ball Lightning, then Peaks" is enumerable. One predicate for
  the enumeration, `ApplyPlanDirect`'s slot hook and partitions, the executor's `exec_slot_hook` /
  `exec_cont_land_hook` / six `LandSlotWatchersFirst` callers, the human `landat=<k>` mapping (CheckLine) and the plan
  label. `LandOrderMatters` is untouched; the emission opens for `LandOrderMatters || deck_has_land_etb_target`.
  Every position k = 1..w is emitted (a lossless superset of the brief's k=1), plus the after-the-casts clone for a
  plan that UNEARTHS ("unearth Hellspark, then Peaks" was otherwise unfindable). Coordinator ruling: keep both (they
  add search options; sound).
* **Deck stamp** `deck_has_land_etb_target` (`StampDeckTraits`, default FALSE; a clear bit only withholds variants).
  `sizeof(GameState)` stays 1712 (padding hole after `deck_has_tap_ping`; verified by compile).

Provider (`CreatureBurnProvider`, DecisionProviders.h/.cpp): derives from `RedElementalsProvider` (the deck's
previous routing, via Hellspark Elemental's Elemental `unearth_cost`), so every inherited hook is byte-for-byte the
same. Signature OR over `tap_ping_damage`, `vanishing_counters`, `ltb_damage_any`, `etb_target_creature_pump_power`
(five cards). **Browbeat's `opp_may_take_damage_instead_of_draw` is NOT in it** (coordinator ruling: a generic red
burn staple that would capture other burn lists). Routed directly above `redelem`. `Certificate()` = NotAssessed.
One addition: `LandEtbPumpTargetOrder` puts plain attackers first, pingers after, and the provider prune
`MTG_CB_PEAKS_PLAIN_FOLD` (EnvOn default ON; off under human play or `MTG_UNPRUNE=peakstarget`) keeps one plain
class (+2 on any plain attacker is the same +2 under attack-all against the never-blocking goldfish). Routing pin in
`test_pirates_provider.cpp`: Creature Burn -> CreatureBurn.

Viewer: Bucket A `target` via `g_play_loyalty_chooser` over the FULL legal set (engine pick preselected).
`audit_viewer_decisions.py` MANIFEST `etb_target_creature_pump_power -> target`; `tools/play/DECISIONS.md` row.
**Batch-2 item closed:** CheckLine now emits a `preping` sub for Browbeat on a tap-pinger deck (both flip values),
so the composer's `cast=Browbeat` line asks the choose-variant dimension "Browbeat pre-ping". Before, the flip
variant shared the base's dedup signature and the human could not pick it. No `linebuild.js` token needed.

Tests: `test/unit/test_creature_burn_batch3.cpp` (8 cases: parse; 3 Mountains + Ball Lightning + Peaks -> the slot-1
plan exists, BL +2, projected 8 (opp 20 -> 12) vs land-first 6, no slot-1 plan without the stamp; `LandSlotCounts`
truth table; non-sick Fireslinger +2; only a sick Pyromancer -> one inert rep, no extra damage; no creature -> enters
tapped, trigger removed, and the `EnterLand` route pumps; fold classes + provider order/prune + pin ranks; axis
emission with/without the stamp and in main 2). Scenario `cb_teetering_peaks_after_ball_lightning_lethal` (opp 8,
3 Mountains, BL + Peaks -> T3 win only via the position axis).

Validation (Release, post-change):
* **Unit 3815/3815; scenarios 599/599.**
* `provider_audit.py --check` rc 0; routing output IDENTICAL before/after for every audited deck (no other deck
  holds the signature params). Creature Burn itself is "NOT AUDITED" (no `.profile.json` yet); the unit pin covers it.
* Coverage: **19/19 `full`.**
* Audits: `audit_card_costs.py` on Peaks compares nothing (a land has no mana cost; rc 2 "nothing compared" is the
  expected land result). `audit_card_fields.py --update --cards <Peaks subset>` added Peaks to
  `scryfall_reference.json` (887 cards; the file was re-sorted by key, content otherwise identical). Offline
  `audit_card_fields.py` rc 0 (oracle advisories = bracket notes). `audit_viewer_decisions.py --no-sweep` clean.
* **Smoke byte-identity (no `--accept`)** on redelem, redelem2hg, burn, burn2hg, onedropburn, onedropburn2hg,
  midrangeburn, midrangeburn2hg, landfall, boars, boars2hg, goblins, goblins2hg: **27/27 PASS, configs changed 0,
  play-changed 0** (searched and d0).

**Disclosed / surface to the USER:**
1. Price of Progress cast before vs after Peaks changes OUR life by 2 (Peaks is a nonbasic land); inert in goldfish.
2. INERT-class premise: nothing in the deck reads a creature's power outside combat damage. A Fling-type card added
   later must revisit the fold.
3. `MTG_CB_PEAKS_PLAIN_FOLD` is a provider prune, default ON (sound only under attack-all vs the never-blocking
   goldfish). The coordinator surfaces it to the USER.
4. Browbeat is kept out of the provider signature (generic staple).
5. Land positions: all k plus the unearth clone (beyond the brief's "first haste cast"), kept per coordinator ruling.

## Claude-play sweep
- commit: `a478b813c`
- seeds: 50001 games: 16 (8 Sonnet player agents x 2 games; every flag verified by an Opus agent).
  Disjoint from every suite seed. Games 0 and 2 were dealt the same hand (the two player agents read the seed
  convention differently), so 15 distinct deals. Results: `logs/creature-burn/claude-sweep-50001.json`.
- result: 16/16 completed; the player tied the search's win turn in every game (T4 x6, T5 x9, T8 x1); none faster.
- flags: 0 unresolved (1 real flag, FIXED in `c5f3b7610`)
  - g8 (seed 50009, game-index 8), REAL, RESOLVED, low severity, human-play surface only: every Searing Blaze plan in the
    main-phase menu is labelled "-> you + <own creature>" and the cast-time target prompt defaults to "You (face)",
    while the note claims "Default = the AI's pick". Accepting the defaults burns our own face and creature. The
    search's own line is right (opponent + its 6/6). Cause: `FixedBurnCandidates` lists Multi pins in battlefield
    order, human play collapses target-only variants to the first pin, and `PrepareFixedBurnPin` defaults to it.
    Pre-existing (shared with Burn and other Searing Blaze decks).
    FIXED `c5f3b7610` (human play only): `HumanPairedBurnDefault` ranks the default pin (opponent face, then
    opponent planeswalker, own face last; then the toughest creature, the one `PrunesPassiveBurnTargets` keeps),
    used for both cast-time prompts and the menu label under `HumanPlayActive()`. Option order unchanged, so
    recorded `--choices` replay as before. Unit test + scenario `creatureburn_human_blaze_default_opponent`
    (fails on the old binary). Smoke on the 8 burn decks: play-changed 0. References of Burn, Midrange_Burn,
    Modern_RW_Burn, 1_Drop_Burn (46) replay identically on both binaries (21 ok, 25 repaired as before, 0 drift).
- dismissed: g2 T3, the frame's `legal_targets[].index` (object index) vs the reply `options[].index`. FALSE
  POSITIVE: the engine did what the reply said, and the note says "reply an option index". Every option-list
  `target` note now names `options[].index` vs `legal_targets[].index` explicitly, and so does `claude-play.md`
  (`c5f3b7610`, usability only).
