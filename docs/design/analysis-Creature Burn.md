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

## CHECKPOINT (orchestrator, updated 2026-11-21 ~22:10 UTC) — resume here after compaction
**2026-11-22 ~07:50 UTC: the USER reloaded VSCode ~06:30-07:45; it killed every process (rebuild, 3 agents + their
builds, the viewer).** Restarted: viewer (primary, MDT_REFERENCE_BACKUP_DIR set; log build/scratch/cb-orch/viewer.log);
landing5/rebuild.sh (PID 5322, incremental build + unit + scen of HEAD fc57da098+, marker landing5/BUILT2). The lost
agents' uncommitted edits were committed as WIP (cb-scry d9982edbf UNBUILT, cb-unearth a313b8e48 UNBUILT) and three new
Opus agents resumed from those + bbgap-handoff.md. Next unchanged: BUILT2 -> join -> under hold: smoke, scoped
regression, accepts -> land.
**08:00 UTC: a SECOND restart. Both were the container's MEMORY cap** (USER: "Unfortunately I think we are hitting a
memory cap. I don't need the viewer restarted, but run things carefully" / "That is why both restarts happened"):
~24 cc1plus (my 12-job build + three 4-slot lane builds). NEW RULE: one build/batch at a time under
`flock build/.heavy.lock`, setsid nohup; viewer NOT restarted. Rebuild relaunched under the lock (PID 4334, marker
landing5/BUILT2). Agents resumed, told to touch nothing heavy until build/scratch/cb-orch/LANDED exists (I create it
after my last heavy landing run). USER rulings: (a) yes (b) yes (c) "This needs to stop reappearing. I say every time
that it must always be able to do nothing to end the phase." -> CLAUDE.md hard rule 295128c44, idle pass
unconditional at every root (cb-bbgap) (d) "staged cards should be counted because the idea is the opponent thinks you
may have burn in hand to finish them off. (it being staged instead does not change this reasoning)" -> both policies
count staged cards (cb-bbgap).
**08:44 UTC: JOINED, I HOLD THE LEASE; smoke RUNNING (landing6/smoke.sh PID 25933 under flock, keepalive running; status landing6/status, marker landing6/SMOKE_DONE; GT backup gtbak6).** Earlier resume point (08:25): rebuild PASSED (landing5 BUILD2/UNIT2 3895/SCEN2 608, binary build/Release =
src of eb56e245e). Rebased onto origin 4a7f65983 (box_lane.sh + CLAUDE.md only; src tree hash UNCHANGED, so the binary
stands). NEXT: `push_queue.py join` -> wait -> at my turn, under `push_queue.py hold --` and `flock build/.heavy.lock`,
MTG_PGO=0: fetch (if origin moved with src: rebase + MTG_BUILD_JOBS=4 rebuild), back up GT, `regression.sh --smoke` ->
expect ONLY creatureburn d3/d5 + creatureburn2hg (Magma Jet + Fireblast pair fold) and midrangeburn d3/d5(+2hg) (Magma
Jet) to move -> attribute, accept; scoped regression creatureburn,creatureburn2hg,midrangeburn,midrangeburn2hg -> classify
slower (classify_turn_later.sh) -> accept; check_gt_logs; viewer_protocol_check --strict --only Creature_Burn,Midrange_Burn
--threads 4; commit GT; `land`; CI watch (--interval 60, background); then `touch build/scratch/cb-orch/LANDED`
(the three agents wait on it before building). Viewer NOT running (USER: not needed).
DONE: Stages 1-5 + verify_deck GATE PASS; Browbeat fold DEFAULT OFF (measured); USER's 10 references committed;
LANDED 5a07c3df3 16:01 UTC (CI green). Since then, local (rebased onto origin 42d819bc0, unpushed):
* 2352c2241 manual tap-ping in human play (USER: "It seems like Cinder Pyromancer auto-taps in the viewer, but cannot
  be manually tapped. Let's leave the auto-tap, but add the option to manually tap."). Validated byte-identical
  (smoke creatureburn/2hg/boars/2hg play-changed 0; refs replay identical; menus grown 0). Primary binary rebuilt
  19:34 so the viewer offers it.
* 1d53617bd + 532aab341 Magma Jet cast_scry searched (MTG_CAST_SCRY_SEARCH, default ON) -- root cause of BOTH
  reference shortfalls (USER: "We should also make sure all of the references are matched if you haven't. Beyond seed
  9, which I expect to become invalid after the bounceland issue is fixed."): ref_bench 2/10 short -> 0/10;
  held-out A/B CB -0.060t (23 faster/0 slower), Midrange Burn -0.015t (7/1, the 1 recovers at d8 b0).
* OPPONENT KAROO: origin's ce5866171 (another session, same USER report) implements it: Karoo returns the oldest other
  non-Karoo land, else another Karoo, else itself; a Karoo with nothing to return is played after a real land.
  = the USER policy here ("leftmost" = oldest). Our wip/opp-karoo (c28b21f91) is SUPERSEDED, agent stopped.
ROUND 3 DONE 00:54 UTC 2026-11-22 (HEAD 532aab341): build/unit/scen PASS; smoke moved ONLY creatureburn d3/d5,
creatureburn2hg d5, midrangeburn d3/d5 (22 faster / 0 slower / 20 same-turn) -> ACCEPTED f95d5e065; gt_logs consistent;
ref_bench 0/20 short (creature_burn human 4.600 / search 4.500; midrange_burn 4.700 / 4.500). s9 under origin's Karoo
fix: opp T1 Haven, T2 Gruul Turf bouncing Haven, PoP hits 2 -- search STILL wins T4 (Bolt, PoP, Ball Lightning, then
Ball Lightning + Fireblast), = the reference's T4. Scoped regression ACCEPTED (33 faster; midrangeburn d5 s2002 gi65 4->5 = churn, recovers 4 at 4x/16x).
s9 reference PLAY-DRIFT under origin's Karoo fix (recorded T2: opp Gruul Turf + Haven unbounced, PoP 4) -- FIXED by
origin d0d5f8463 (opponent_land_entry stamp + --legacy-opponent-land-entry), another session. Rebased onto it
(2026-11-22 ~01:45 UTC). Round 4 PASSED 04:33 (smoke byte-identical 243/243; CB+MB refs 18 ok 2 repaired 0 drift).
But origin then moved to f9a59b8a7 (periodic overnight accept, burn damage order, Merfolk gate, ...) -> rebased again
04:45 UTC, DROPPING my two GT commits (regenerate under the rebased binary; backup/cb-pre-rebase4 holds them);
HeuristicArm.h conflict = both appended a slot (kept both). RUNNING: build_chain landing5 (PID 323234; build, unit,
scen; status landing5/status, marker landing5/BUILT) outside the lease. ADDED f7daf7049 (USER: fold Fireblast's Mountain pairs like the other
burn decks) after the build started -> after BUILT: incremental ./build.sh + unit again, then join. THEN: join; under `hold`: fetch (rebase +
incremental rebuild if origin moved), smoke -> expect ONLY Magma Jet decks to move -> accept; scoped regression
creatureburn,creatureburn2hg,midrangeburn,midrangeburn2hg (+ any other Magma Jet deck smoke moved) -> accept; check_gt_logs;
refs --only Creature_Burn,Midrange_Burn; land; CI watch.
USER RULINGS 2026-11-22 (verbatim): (1) scry width: "I don't like widths period because they provide either too many
or too few options. My preference is to fully open it up and prune with a heuristic as needed." (2) Burn scry: "Yes, we
should fix it. Ideally we would make it a relatively smart heuristic that keeps things based on impact and
castability. So, for example, you would often keep bolt because it can fit in to the curve wherever, but you may not
keep a 3-drop if you have too many even if you have enough mana. Also, we should certainly consider pruning orderings
of the top. Those should be something the heuristic can handle." (3) main-1 Browbeat gap: "I'm not sure how necessary
this is, but let's look into it before generating. I'm not in a rush to start the generation at this point." (5)
Browbeat: "Is this the primary reason why we are considering second main? Maybe we should actually make the opponent
take the visible combat damage into account when deciding on browbeat. It doesn't really make sense to exclude it."
AGENTS (Opus, background): scry -> brief logs/creature-burn/scry-brief.md, worktree cb-scry (wip/cb-scry), handoff
scry-handoff.md; Browbeat policy + gap -> brief logs/creature-burn/browbeat-gap-brief.md, worktree cb-bbgap
(wip/cb-bbgap), handoff bbgap-handoff.md. MULLIGAN GENERATION WAITS for both (USER: not in a rush).
USER 2026-11-22 (verbatim): (6) Hellspark unearth: "It's arguable, but I think its best to just fully implement this."
-> agent cb-unearth (brief logs/creature-burn/unearth-brief.md, handoff unearth-handoff.md). (7) "We don't re-open
these questions for any one change. Instead the regeneration queue handles these automatically based on drift." (8)
"Fast is fine. Generating overnight once on the machine is fine. The point is that we don't want to be constantly
regenerating it when developing or doing so when holding up the queue." + "(although if you do generate the overnight
at some point, I recommend pulling from origin first)". So: mullgen `fast`; overnight FULL-set expansion once at
adoption, after a fresh pull, never under the lease.
USER 2026-11-22: "If the pinger pings are visible they probably should. Yes, Vexing Devil should act the same way.
That's good if we don't need to work with the fold anymore." -> relayed to cb-bbgap: visible = combat to come + one ping
per READY tap pinger; same anticipation for Vexing Devil (shared helper; its decks join the A/B); fold lever deleted if
the main-2 trigger goes.
cb-bbgap agent 1 HANDED OFF (bbgap-handoff.md, WIP b372fd612 unbuilt); agent 2 continues. FINDINGS: the main-1 gap was a
FIXTURE artifact (scenario Champion echo unresolved + land drop pre-used) -- real residual: a search started MID-main
never offers "cast nothing more" (engine-wide). Browbeat's main-2 trigger STAYS (a post-combat haste body / Peaks
empties the hand without visible damage: threshold 8 -> 5, unreachable from main 1), fold lever kept. USER QUESTIONS:
(a) visible damage per creature = max(combat to come, ready ping)? (b) opponent assumes attack-all (a searched hold is
not anticipated) OK? (c) always offer "cast nothing" at a mid-turn search root (engine-wide play change)? (d) Vexing
Devil's held-card count includes staged cards (Browbeat excludes them) -- align?
NEXT after landing: s9 check (USER expects T5 with the opponent Karoo fix); Stage 6 report + 6a disclosure (items: fold
OFF verdict; main-1 attack-then-Browbeat gap; PEAKS_PLAIN_FOLD; Browbeat out of signature; extra Peaks positions; echo
census NO GAP; Pyromancer response ping / un-ping; 1-power pinger exemption; Peaks inert-class premise; PoP vs Peaks;
heavy unbudgeted tail; cert NotAssessed; PROVISIONAL Hellspark unearth; Browbeat SECOND-MAIN RELEVANT; scry width);
then the MULLIGAN PROFILE (USER: "After that we can try building the mulligan profile."; mullgen.sh run, alone on the
box, frozen commit). Timer 7229aaa7 (2-hourly :17); CronDelete at the end; check_references_safe at close.

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
    Toughest-first default CONFIRMED by the USER 2026-11-21: *"The opponent's toughest creature is good for searing
    blaze."* and *"(in Goldfishing. In a real game you would probably target the best creature you can kill)"*. So this ranking is for goldfish only; Phase 2 (a real
    opponent) should default to the best creature the burn can kill.
- dismissed: g2 T3, the frame's `legal_targets[].index` (object index) vs the reply `options[].index`. FALSE
  POSITIVE: the engine did what the reply said, and the note says "reply an option index". Every option-list
  `target` note now names `options[].index` vs `legal_targets[].index` explicitly, and so does `claude-play.md`
  (`c5f3b7610`, usability only).

## Stage 5c2 — horizon-honest tie-break check (2026-11-21)

`scripts/leaf_tiebreak_check.py` at the deck's PLAY settings (d5 b20, source=default), MTG_PGO=0, two runs pooled:

| run | seeds | paired | changed | net turns | worse | better |
|---|---|---|---|---|---|---|
| run 1 (`--blocks 12`) | 1000000..2100000 | 12000 | 6 | -8 | 0 | 6 |
| extension (`--seed-base 2200000 --blocks 12`) | 2200000..3300000 | 12000 | 12 | -5 | 4 | 8 |
| **pooled** | | **24000** | **18** | **-13** | **4** | **14** |

Every half of both runs is negative (helps). Binding rate 0.075%, so 18 changed games stays under the script's
20-game sign threshold even at 24 blocks: the lever almost never fires, and when it does it helps 14:4.
Gate 1 (aggregate): helps. Gate 2 (game by game): the 4 worse games (s2200000 gi267 5->6, s2300000 gi526 4->5,
s2400000 gi142 5->6, s2900000 gi272 6->7) re-run with the tie-break ON at d5 `--budget-ms 0`
(`build/scratch/cb-orch/gate2/`) all recover the base win turn (5, 4, 5, 6): recoverable, no line deleted.
**VERDICT: KEEP THE DEFAULT (ON). No opt-out.** Outputs: `logs/leaf_tiebreak/creatureburn{,-ext}.{json,out}`
(run 1's `.out` also copied to `build/scratch/cb-orch/tiebreak-run1.out`).

## Browbeat second-main cost (probe, 2026-11-21)

Browbeat (`opp_may_take_damage_instead_of_draw`) is this deck's only `GoldFishRunner::DeckUsesSecondMain` trigger,
so it opens a full searched post-combat main on EVERY turn. Priced with a never-landed probe binary
(worktree `.claude/worktrees/cb-m2probe`, `MTG_CB_PROBE_BROWBEAT_M2=0` drops the trigger), both arms on the same
binary, back to back, alone on the box, regression seed 1001 x 300 games per cell (`build/scratch/cb-orch/m2cost/`;
units reproduce exactly on a rerun):

| cell | arm | avg win turn | units | ms/game | changed games (ON better / worse) |
|---|---|---|---|---|---|
| d5 b20 | M2 ON (shipped) | 4.8467 | 7,154,532 | 3887 | 6 (3 / 3), net 0 |
| d5 b20 | M2 OFF | 4.8467 | 4,384,041 | 2101 | |
| d3 b10 | M2 ON (shipped) | 4.8567 | 4,875,796 | 2553 | 10 (5 / 5), net 0 |
| d3 b10 | M2 OFF | 4.8567 | 3,073,558 | 1341 | |

The every-turn main 2 costs **1.63x units at d5, 1.59x at d3** and buys **0.000 turns** on this sample. Dropping it
is NOT allowed as such: a main-2 Browbeat ("attack, then Browbeat" meets a lower-life choice) would become unfindable
at every budget. The sound shape is the per-turn restricted main (`RestrictedSecondMainLive`, the Giada/Omnitool
hook): open it only on turns a Browbeat is in hand post-combat. Being built as a follow-up; see the checkpoint.

### The per-turn fold: built, measured NOT an identity, ships DEFAULT OFF (2026-11-21, `1328b46b0`)

`MTG_CB_BROWBEAT_M2_RESTRICT` (heurarm `CB_BROWBEAT_M2_RESTRICT`): `CreatureBurnProvider::RestrictedSecondMainLive`
opens the full post-combat main only on turns a castable Browbeat is in hand after combat, and
`DeckUsesSecondMain` drops Browbeat's every-turn trigger for this provider (one reader for both halves). Held-out
A/B, one pooled batch, seeds 4000001.., 2000 paired games per arm per cell (scratch `build/scratch/cb-m2fold/`):

| cell | units ON/OFF | ms/game ON vs OFF | avg ON vs OFF | changed (same turn) | ON better / worse | net turns |
|---|---|---|---|---|---|---|
| d3 b10 | 0.692x | 1503 vs 2563 | 4.7965 vs 4.7920 | 553 (498) | 23 / 32 | +9 |
| d5 b20 | 0.641x | 2215 vs 3878 | 4.7925 vs 4.7815 | 585 (547) | 8 / 30 | +22 |

d5 b0 spot checks ON vs OFF: gi480 4 vs 5, gi189 6 vs 5, gi702 5 vs 4; gi1024 / gi61 / gi720 equal. Different win
turns at an unlimited budget, so it is not a re-pricing; d8 b0 recovery was not run (already fails net <= 0).
**Mechanism:** the soundness argument covers only the searched main 2 inside the window. `uses_second_main = false`
also (a) stands down `MainPhaseFilterActive`, changing main 1's menu shape, and (b) closes the rollout's / d0
runner's GREEDY per-turn main 2 beyond the horizon, which spends leftover mana (e.g. a Rift Bolt suspend after a
creature; d0 smoke 25 slower / 0 faster). An amended fold would keep both of those intact on closed turns.
Default tree byte-identical (creatureburn + 2hg smoke play-changed 0; `=1` smoke on the other burn decks + redelem
PASS). **USER item** (surfaced 2026-11-21): ship OFF as is, or fund the amended fold.

### Completeness gap: "attack, then Browbeat" is unfound from the MAIN-1 frame (2026-11-21, open)

Turn 5, opponent at 10, Keldon Champion untapped, hand Browbeat + an unplayable Mountain, three Lightning Bolts on
top, six Mountains. Line: attack (10 -> 7), main-2 Browbeat (7 <= 8 while holding -> declined), draw the Bolts, cast
three: win T5. From the main-1 frame the full-depth search does NOT find it on EITHER arm at d2, d3 or d5 with
budget 0 (commits a T6 line, "PROVEN NO-WIN" for the turn), although the executor's own main-2 solve does (scenario
`creatureburn_browbeat_m2_post_combat_browbeat`, which resumes at main 2). The one line the every-turn main 2 exists
for is unreachable from main 1 at any budget -- likely why that main 2 measured 0.000 turns. Suspected cause (not
confirmed): the nested post-combat main does not continue search after a draw. A CLAUDE.md COMPLETENESS defect,
pre-existing, not caused by the fold. Owed: root-cause + fix (follow-up engine task).

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-11-21)

`verify_deck.py decks/Creature Burn/Creature Burn.cod --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 19 cards full (missing=0, partial=0) |
| card_costs | PASS | yes | all mana costs match Scryfall (cost/cmc only) |
| card_fields | PASS | yes | 887 cards match snapshot (cost/PT/types/keywords); 18 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | PASS | yes | in all 3 suite tiers with GT (creatureburn) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 4 type(s) wired (emitter + GUI): echo, sacrifice, scry, target |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 8001] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/190 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |
| suite | PASS | yes | suite gate: Creature Burn is regression key `creatureburn` |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Browbeat: USER-ruled passive-opponent model of the opponent's choice (2026-11-20; ONE place: BrowbeatOpponentTakesDamage in SpellEffects.h, the analog of the USER's Vexing Devil ruling -- 'I would probably do the same thing as devil, which is take the damage unless it puts you within 3 life.' and 'No, your idea is good. (hand empty = take the damage unless lethal)'): the opponent takes the 5 damage iff its life (the shared team life in 2HG) is > 5 with our hand empty (unless lethal), or > 8 while we hold any card (it guesses from our hand SIZE, never its contents; Browbeat itself and staged exiled cards are not counted). Taken: 5 noncombat damage from Browbeat to the opponent (marks it dealt damage / lost life this turn) and NO cards drawn. Declined: we draw three (draw_spell draw 3; an over-draw loses, CR 104.3c). The choice is the OPPONENT's, so it is neither a search branch nor a viewer decision; executor and rollout resolve it through the same ResolveDamageInsteadOfDraw. Our OWN 'take 5' option is never used: it is dominated (no draw and no damage to the opponent). PRE-PING (Creature Burn batch 2): a cast of Browbeat in a deck with a tap pinger carries a searched sub-decision (Action::browbeat_flip, BrowbeatFlipPrePings): fire the fewest ready pings first so the opponent declines, or withhold the fewest Cinder Pyromancer response pings so it takes -- the outcome is binary in the life, so the base cast plus that one variant cover every reachable outcome at maximum damage. 'Target player draws' targets US: the passive opponent's library is never worth handing three cards. SECOND-MAIN RELEVANT (PROVISIONAL, surfaced to the USER): the choice reads the opponent's life at resolution, which combat changes, so the param is in GoldFishRunner::DeckUsesSecondMain and the cast classifies to either main.
- coverage deferral -- Flame Javelin: TWOBRID PIPS (the Beseech the Queen parse): each {2/R} is generic 2 plus ManaCost::twobrid metadata, so the mana value is 6 for every reader (CR 202.3f) and the card is red (CR 105.2). Which pips to pay with {R} is a SEARCHED branch (CollectActions' twobrid post-pass: one variant per coloured-pip count k = 0..3 -- {6}, {4}{R}, {2}{R}{R}, {R}{R}{R}). SOUND FOLD (MTG_TWOBRID_COLOUR_FOLD, default ON): when every unit of mana the cast could use is red (pool + floating all {R}, every permanent and every land in hand makes only {R}, nothing reads the mana spent) the fewest-mana variant dominates every other and is the only one emitted -- a mono-red Mountain board casts it for {R}{R}{R}; with any colourless / other-colour source the full fan is kept. The human plan menu always shows every payment. 4 damage to any target = direct_damage targeting any: the face, or a creature through the shared burn-target decision (a human picks on the board).
- coverage deferral -- Ball Lightning: Vanilla 6/1 with Trample + Haste (keywords) and endstep_sacrifice_self: at the beginning of EACH end step it is sacrificed through the shared SacrificePermanentAt (PerformEndStepSelfSacrifices, both end-step sites in lockstep; it dies, so dies/LTB watchers fire). Haste lets it attack the turn it is cast. Trample is inert vs the never-blocking goldfish opponent.
- coverage deferral -- Cinder Pyromancer: FULLY MODELED (Creature Burn batch 2, 2026-11-20). tap_ping_damage 1 + tap_ping_target player: as Goblin Fireslinger (the face; x1 in 2HG). cast_color_untap_self R: on every cast of a RED spell (FireOnCastTriggers -- a hand cast, Fireblast's alternative cost, Flame Javelin's twobrid cost, a Rift Bolt cast off suspend in the upkeep; suspending a card and Hellspark's unearth are not casts) a TAPPED Pyromancer untaps (the 'may' is always taken: untapped dominates vs the goldfish) and an UNTAPPED one able to {T} pings IN RESPONSE, then untaps (TapPingCastUntap: +1 damage, the same board). Each such response ping is recorded as WITHDRAWABLE (GameState::opt_pings) for Browbeat's flip, since not pinging was the other legal line. FOLDS (both worlds, shared with Zhur-Taa Druid's): the END-STEP SWEEP (PingDorkEndSweep -- every ready pinger pings first thing in our end step; nothing after the last main can use the body, so tapping weakly dominates) and the POST-COMBAT LETHAL FINISH (PingDorkFinish -- autonomous play taps them right after combat damage when their pings alone are lethal, so the kill is seen at combat). Timing is otherwise inert: the only card in the list that reads the opponent's life before the end step is Browbeat, and a Browbeat cast carries the searched PRE-PING sub-decision (Action::browbeat_flip, BrowbeatFlipPrePings: fire the fewest pings first so the opponent declines, or withhold the fewest Pyromancer response pings so it takes -- the outcome is binary, so two variants are complete). ATTACK: a 0/1 attacking deals 0 and forfeits its ping, so it is HELD (DecisionProvider::AttackWith, dominated) unless its attack would deal at least its ping (a pumped Pyromancer attacks). HUMAN PLAY: every ping asks the board-click `target` decision, players only.
- coverage deferral -- Goblin Fireslinger: FULLY MODELED (Creature Burn batch 2, 2026-11-20). tap_ping_damage 1 + tap_ping_target player: a free {T} ability (summoning sickness applies -- CanTapNow). 'Target player or planeswalker' is the opponent's FACE in autonomous play (no opposing planeswalker ever exists; pinging ourselves never helps) and ONE head in 2HG (x1 to the team's shared life). FOLDS (both worlds, shared with Zhur-Taa Druid's): the END-STEP SWEEP (PingDorkEndSweep -- every ready pinger pings first thing in our end step; nothing after the last main can use the body, so tapping weakly dominates) and the POST-COMBAT LETHAL FINISH (PingDorkFinish -- autonomous play taps them right after combat damage when their pings alone are lethal, so the kill is seen at combat). Timing is otherwise inert: the only card in the list that reads the opponent's life before the end step is Browbeat, and a Browbeat cast carries the searched PRE-PING sub-decision (Action::browbeat_flip, BrowbeatFlipPrePings: fire the fewest pings first so the opponent declines, or withhold the fewest Pyromancer response pings so it takes -- the outcome is binary, so two variants are complete). ATTACK: a 1/1 attacks for exactly its ping (the standing attack-all exemption). HUMAN PLAY: every ping asks the board-click `target` decision, players only.
- coverage deferral -- Hellspark Elemental: Trample + Haste (keywords; trample inert vs the never-blocking opponent) and endstep_sacrifice_self (sacrificed at the beginning of each end step, the Spark Elemental shape). UNEARTH (CR 702.84): unearth_cost {1}{R} -- Action::Kind::Unearth, an ACTIVATED ability from the GRAVEYARD at sorcery speed (the main-phase enumeration; empty stack), NOT a cast (no cast triggers, no storm/prowess, not a spell). One action per graveyard copy, interchangeable copies folded to a canonical prefix (equiv_tag), so a plan may unearth any number of copies it can pay for and the SEARCH decides whether and how many (never a heuristic pick); pre-combat its hasted power is stamped into the this-turn attack projection, post-combat it is priced -2 (no attack left, and it costs the card). Applied in the trailing pass after the plan's casts (a same-plan Metallic Mimic counts). ApplyUnearth (both worlds): the card leaves the graveyard and ENTERS through PutCardOntoBattlefield under GraveyardEnterScope (ETB watchers fire -- Metallic Mimic's +1/+1 counter if it named Elemental; Soulstoke's lord applies by live subtype) with Permanent::unearthed set and temp_haste ('It gains haste'). 'Exile it at the beginning of the next end step': the delayed trigger sweeps every unearthed permanent to EXILE at the top of both end-step sites (PerformEndStepUnearthExiles, lockstep), before its own sacrifice trigger -- and in either order it ends in exile. 'If it would leave the battlefield, exile it instead of putting it anywhere else' is a CR 614 REPLACEMENT (no stack), applied at the shared sacrifice chokepoint (SacrificePermanentAt -- its own end-step sacrifice, Soulstoke/Thunderkin delayed sacrifices, sac outlets; the other end-step delayed triggers run after the exile sweep and find it gone): the card goes to exile, the sacrifice still happened (sacrifice watchers fire) but it did NOT die (no dies/morbid triggers; LTB triggers fire). So an unearthed creature never returns to the graveyard (no second unearth, no later reanimation). The unearthed status is a PER-OBJECT flag (Permanent::unearthed) dropped on any zone change, so an effect that itself EXILES the creature and later returns it (champion) returns a new object no longer under unearth (Scryfall ruling). [PARTIAL/PROVISIONAL (user sign-off owed): the replacement is NOT routed through a destroy effect, the lethal-damage / toughness-0 state-based-action deaths, the legend rule, the symmetric creature sweeps or a bounce -- none of which can befall an unearthed creature in this list (the goldfish opponent never blocks or removes, and nothing here damages or shrinks our creatures); a deck that can kill or bounce an unearthed creature that way would need those sites routed.
- coverage deferral -- Keldon Champion: Every clause modelled. Haste is Keyword::Haste (it attacks the turn it lands). Echo {2}{R}{R} rides echo_cost -- the shared upkeep pay-or-sacrifice decision (Mogg War Marshal / Stingscourger precedent), SEARCHED by the echo axis (Plan::echo_choice, MTG_ECHO_AXIS) and surfaced to a human as the `echo` decision; 'came under your control since the beginning of your last upkeep' is the per-object Permanent::echo_resolved flag, so only the first upkeep after it enters asks. The ETB '3 damage to target player or planeswalker' is etb_damage_any 3 at the opponent's FACE: the passive opponent has no planeswalker and damage to ourselves never helps (the etb_damage_any precedent -- automatic, not asked of a human). x1 in 2HG: it is a single target, not 'each opponent'. Fired from the universal own-ETB cascade, so every enter route, executor and rollout alike. Not second-main relevant.
- coverage deferral -- Keldon Marauders: Every clause modelled. Vanishing is NOT in the keywords array: the engine's Keyword enum carries only keywords it reads, and vanishing is fully expressed by vanishing_counters 2 (the fading / devour convention). VANISHING (CR 702.63, new 2026-11-20): two TIME counters put AS IT ENTERS (Permanent::time_counters, at the universal enter cascade -- so a counter doubler doubles them, CR 614.1c), one removed at each of our upkeeps by PerformUpkeepVanishing (both worlds, beside fading), and sacrificed when the LAST comes off -- i.e. at the SECOND upkeep after it enters (2 -> 1, then 1 -> 0 and sacrificed), so cast on turn N it attacks on turn N+1 only. The sacrifice goes through the shared SacrificePermanentAt, so its leave trigger fires. 'Enters OR leaves' is ONE printed trigger with two conditions, modelled as two params: etb_damage_any 1 (the universal own-ETB cascade) and ltb_damage_any 1 (the shared FireLeavesBattlefieldTriggers -- any leave: the vanishing sacrifice, a sacrifice outlet, a death, a bounce). An upkeep sacrifice's 1 damage is dealt in the upkeep, so a lethal one wins that turn in both worlds. 'Target player or planeswalker' collapses to the opponent's FACE for both halves (no opponent planeswalker; damage to ourselves never helps -- the etb_damage_any precedent, not asked of a human); x1 in 2HG (a single target). No player decision anywhere: both vanishing steps are mandatory.
- coverage deferral -- Spark Elemental: Vanilla 3/1 with Trample + Haste (keywords) and endstep_sacrifice_self: at the beginning of EACH end step (ours and the opponent's notional one) it is sacrificed through the shared SacrificePermanentAt (PerformEndStepSelfSacrifices, both end-step sites in lockstep; it dies, so dies/LTB watchers fire). Haste lets it attack the turn it is cast (the search's this-turn attack projection stamps it via haste_attack_power). Trample is inert vs the never-blocking goldfish opponent.
- coverage deferral -- Vulshok Sorcerer: FULLY MODELED (Creature Burn batch 2, 2026-11-20). Haste is Keyword::Haste (it may attack or {T} the turn it lands). tap_ping_damage 1 + tap_ping_target any: 'any target' collapses to the opponent's FACE in autonomous play -- the passive goldfish has no creature or planeswalker, and damaging our own permanents or face never helps (the etb_damage_any precedent); x1 in 2HG. FOLDS (both worlds, shared with Zhur-Taa Druid's): the END-STEP SWEEP (PingDorkEndSweep -- every ready pinger pings first thing in our end step; nothing after the last main can use the body, so tapping weakly dominates) and the POST-COMBAT LETHAL FINISH (PingDorkFinish -- autonomous play taps them right after combat damage when their pings alone are lethal, so the kill is seen at combat). Timing is otherwise inert: the only card in the list that reads the opponent's life before the end step is Browbeat, and a Browbeat cast carries the searched PRE-PING sub-decision (Action::browbeat_flip, BrowbeatFlipPrePings: fire the fewest pings first so the opponent declines, or withhold the fewest Pyromancer response pings so it takes -- the outcome is binary, so two variants are complete). ATTACK: a 1/1 attacks for exactly its ping (the standing attack-all exemption). HUMAN PLAY: every ping asks the board-click `target` decision over the FULL legal set (default: the opponent's face).
- coverage deferral -- Teetering Peaks: FULLY MODELED (Creature Burn batch 3, 2026-11-20). enters_tapped + produces R + etb_target_creature_pump_power 2: the trigger resolves at BOTH land-entry sites (the land drop and an effect put -- ResolveLandEtbTargetPump), +2/+0 until end of turn through temp_power_bonus. MANDATORY target, any creature on either side; with no creature the trigger is removed (CR 603.3d) and the land just enters tapped. TARGET = a SEARCHED rank (Plan::landfall_target_choice) over sound-folded classes: identical creatures are one target, and an INERT class (an opponent's creature, a summoning-sick creature without haste, any creature once this turn's combat is over) is dropped while a creature that can still attack this turn exists (tapped is NOT inert: Cinder Pyromancer untaps on a red cast); pingers keep their own classes (a pumped Pyromancer attacks but loses its ping). PROVIDER PRUNE (CreatureBurnProvider, MTG_CB_PEAKS_PLAIN_FOLD, default ON): the plain non-pinger attackers are interchangeable under attack-all against the never-blocking goldfish, so one stands for all. POSITION: the land drop is also searched right after EACH haste-creature cast (Plan::land_slot k -- 'Ball Lightning, then Peaks' pumps the Ball Lightning) and after an unearth (the after-the-casts clone -- 'unearth Hellspark, then Peaks'); Peaks enters tapped, so a later position never costs mana. Disclosed: Price of Progress cast before vs after Peaks changes OUR life by 2 (inert in goldfish). HUMAN PLAY: the board-click `target` decision over the full legal set.
- allowlisted divergence -- Wanderwine Prophets [keywords]: Champion is modeled by champion_merfolk with linked exile, return, sacrifice if no champion, and searched human choice. The engine has no generic cham
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately 
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- allowlisted divergence -- Bloodline Keeper [keywords]: Transforming DFC (Bloodline Keeper // Lord of Lineage): Scryfall reports CARD-LEVEL keywords for both faces (Flying, Transform). Transform is the layo
- allowlisted divergence -- Lord of Lineage [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the layout's Transform; the back face carries no transform of its own (transform_fr
- allowlisted divergence -- Emrakul, the Aeons Torn [keywords]: 'Protection from spells that are one or more colors' is a real keyword but INERT here (PROVISIONAL, ledger Q12): nothing in a goldfish game targets, d
- oracle_text advisory -- Deeproot Pilgrimage: oracle_text diverges (similarity 0.55); scryfall='Whenever one or more nontoken Merfolk you control become tapped, create a 1/1 blue Merfolk creature token with
- oracle_text advisory -- Svyelun of Sea and Sky: oracle_text diverges (similarity 0.73); scryfall='Svyelun has indestructible as long as you control at least two other Merfolk.\nWhenever Svyelun attacks, draw 
- oracle_text advisory -- Wanderwine Prophets: oracle_text diverges (similarity 0.11); scryfall='Champion a Merfolk (When this enters, sacrifice it unless you exile another Merfolk you control. When this lea
- oracle_text advisory -- Decimator of the Provinces: oracle_text diverges (similarity 0.22); scryfall="Emerge {6}{G}{G}{G} (You may cast this spell by sacrificing a creature and paying the emerge cost reduced by t
- oracle_text advisory -- End-Raze Forerunners: oracle_text diverges (similarity 0.15); scryfall='Vigilance, trample, haste\nWhen this creature enters, other creatures you control get +2/+2 and gain vigilance
- oracle_text advisory -- Flinthoof Boar: oracle_text diverges (similarity 0.17); scryfall='This creature gets +1/+1 as long as you control a Mountain.\n{R}: This creature gains haste until end of turn.
- oracle_text advisory -- Herald of Ilharg: oracle_text diverges (similarity 0.24); scryfall='Trample\nWhenever you cast a creature spell, put two +1/+1 counters on this creature. If that spell has mana v
- oracle_text advisory -- Herald's Horn: oracle_text diverges (similarity 0.26); scryfall="As this artifact enters, choose a creature type.\nCreature spells you cast of the chosen type cost {1} less to
- oracle_text advisory -- Ilharg, the Raze-Boar: oracle_text diverges (similarity 0.21); scryfall="Trample\nWhenever Ilharg attacks, you may put a creature card from your hand onto the battlefield tapped and a
- oracle_text advisory -- Raggadragga, Goreguts Boss: oracle_text diverges (similarity 0.23); scryfall='Each creature you control with a mana ability gets +2/+2.\nWhenever a creature you control with a mana ability
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Goblin Guide: oracle_text diverges (similarity 0.60); scryfall="Haste\nWhenever this creature attacks, defending player reveals the top card of their library. If it's a land 
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.31); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.15); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.21); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.\n{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.35); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.\n{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.\n{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.\nYou may choose not to untap this land during your untap step.\nAt the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.\n{T}: Put a storage counter on this land.\n{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)\nThis land enters tapped.\nWhen this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.\n{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.\nSacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.27); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.\n{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.05); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Niv-Mizzet, the Firemind: oracle_text diverges (similarity 0.16); scryfall='Flying\nWhenever you draw a card, Niv-Mizzet deals 1 damage to any target.\n{T}: Draw a card.'
- oracle_text advisory -- Niv-Mizzet, Parun: oracle_text diverges (similarity 0.23); scryfall="This spell can't be countered.\nFlying\nWhenever you draw a card, Niv-Mizzet deals 1 damage to any target.\nWh
- oracle_text advisory -- Curiosity: oracle_text diverges (similarity 0.16); scryfall='Enchant creature\nWhenever enchanted creature deals damage to an opponent, you may draw a card.'
- oracle_text advisory -- Tandem Lookout: oracle_text diverges (similarity 0.38); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)\nWhen
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.30); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.45); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)\n{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.30); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.32); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.05); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.14); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.12); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.\nFerocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample\nSpells you cast cost {1} less to cast for each target.\nSpells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —\n• Tap X target permanents.\n• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying\n{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.\nSplice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste\nOther creatures you control have haste.\nWhenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste\nWhen Karrthus enters, gain control of all Dragons, then untap all Dragons.\nOther Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.23); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying\nWhenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.\nStorm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.\nIf this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.\nWhenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.33); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample.\nWhen this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.35); scryfall='Enchant creature with another Aura attached to it\nEnchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample.\nWhenever enchanted creature deals damage, yo
- oracle_text advisory -- Hyena Umbra: oracle_text diverges (similarity 0.52); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has first strike.\nUmbra armor (If enchanted creature woul
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)\nUmbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature\nEnchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature\nEnchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature\nEnchanted creature gets +1/+0 and has flying.\n{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)\nWhenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)\nEnchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)\nOther Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.\nGoblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.\n{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.\n{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.\nWhenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.46); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.24); scryfall='First strike\nWhen this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach\nWhen this creature enters, it deals 2 damage to any target.\nChannel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.\nWhen this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.\nWhenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.\nWhenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.\n{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.15); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.26); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.16); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.33); scryfall='Flying, first strike, trample, lifelink, haste\nAt the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste\nWhenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything\nIf Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance\nThis creature gets +1/+1 for each color among permanents you control.\n{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)\n{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)\nEquip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.\n−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.\n−2: Gain control of target creature.\n−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")\n+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.34); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.03); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying\n{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.21); scryfall="Choose five. You may choose the same mode more than once.\n• Target permanent phases out.\n• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying\nWhenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.\nChoose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.39); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.\nEquip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.\nEquip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.\n{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.\nWhenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.\nEquipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.\nEquip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Paradise Mantle: oracle_text diverges (similarity 0.10); scryfall='Equipped creature has "{T}: Add one mana of any color."\nEquip {1}'
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.\nEquip {3} ({3}: Attach to target creature you control. Equip 
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)\nEquip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.\nEquip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)\nEquip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.\nEquip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.\nEquipped creature gets +1/+0 and has haste.\nEquip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.\nEquip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.\nJump — During your turn, Cid has flying.\n{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.\nWhenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike\nWhenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone 
- oracle_text advisory -- Ardenn, Intrepid Archaeologist: oracle_text diverges (similarity 0.17); scryfall='At the beginning of combat on your turn, you may attach any number of Auras and Equipment you control to targe
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.\nRemove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.43); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.\nMetalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.30); scryfall='First strike\nBalan has double strike as long as two or more Equipment are attached to it.\n{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying\nWhenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.\n{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.03); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.41); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch\nWhen this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample\nWhen this creature enters, if your life total is less than 7, your life total becomes 7.\nAs long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste\nWhen this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample\nWhen this creature dies, create three 5/5 green Wurm creature tokens with trample.\nWhen Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample\nWhenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.\nSearch your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.12); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.\n{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.12); scryfall='First strike\n{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.\n{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.\nWhenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.25); scryfall='First strike\nOther Minotaurs you control have first strike.\nAs long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample\nOther Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.\n{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.21); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.\n{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample\nWhen Atsushi dies, choose one —\n• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.\nFlying, haste\n{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.\nWhenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.\nCreature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.\n{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\n{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste\nYou may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.\n{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.\n{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.\nWhenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.\n{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.\n{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.\n{T}: Add {C}.\n{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)\n{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.\n
- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying\nWhen this creature enters, untap up to two lands.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying\nWhen this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Cryptolith Rite: oracle_text diverges (similarity 0.05); scryfall='Creatures you control have "{T}: Add one mana of any color."'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land\nEnchanted land has shroud. (It can't be the target of spells or abilities.)\nWhenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.04); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.05); scryfall="This spell can't be countered.\n{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)\n{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)\nFlash\nFlying\n{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).\n{T}: Add {C}.\n{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.\nCascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample\nCascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)\nShuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.\n{T}: Add {R}.\n{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U}.\n{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.\n{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.\nCreatures you control can't have -1/-1 counters put on them.\nCreatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.\nPersist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.\nPersist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.\nSacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch\nWhen this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)\n{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying\nWhen this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.\nWhenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {W}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {B}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {R}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{1}, {T}: Add one mana of any color.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.\nCycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.\nCycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.\nCycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and 
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\nAs this artifact enters, choose a color.\n{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample\nAbominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)\nWhen this artifact enters, draw a card.\n{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash\nFlying\nWhen this creature enters, draw a card.\nThis creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.\nAt the beginning of your 
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying\nRimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying\n{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.11); scryfall='Protection from black and from red\nWhenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.12); scryfall="First strike, lifelink\nAscend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nAs long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.\nAs long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying\nLifelink (Damage dealt by this creature also causes you to gain that much life.)\nWhenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible\nAs long as your devotion to white is less than five, Heliod isn't a creature.\nWhenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.12); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.\n
- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, it fights up to one target creature you don't control.\nEnrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — The next red or 
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample\nWhen Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.11); scryfall='Flying\nFirst strike (This creature deals combat damage before creatures without first strike.)\nLifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.10); scryfall='Whenever an Angel you control enters, you gain 4 life.\nWhenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying\nWhenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 1 life.\nWhenever an Angel you control enters, you gain 1 life.\n{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying\nAt the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying\nWhen this creature enters, you may reveal a card you own named Legion Angel from outside the game and 
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.\n−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.11); scryfall='Flying, vigilance\nEach other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample\nWhenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.\n{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Yavimaya Sapherd: oracle_text diverges (similarity 0.06); scryfall='When this creature enters, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Deathbloom Thallid: oracle_text diverges (similarity 0.05); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- The Mycotyrant: oracle_text diverges (similarity 0.18); scryfall='Trample\nThe Mycotyrant\'s power and toughness are each equal to the number of creatures you control that are 
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."\nAt the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Spore Swarm: oracle_text diverges (similarity 0.06); scryfall='Create three 1/1 green Saproling creature tokens.'
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.13); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)\nCreate two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.15); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.\nIf one or more +
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.\nAs long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender\nAt the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters 
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.\nRemove three spore counters 
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance\nWhen this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying\nWhenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that 
- oracle_text advisory -- Risen Reef: oracle_text diverges (similarity 0.15); scryfall="Whenever this creature or another Elemental you control enters, look at the top card of your library. If it's 
- oracle_text advisory -- Flamekin Harbinger: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, you may search your library for an Elemental card, reveal it, then shuffle and put 
- oracle_text advisory -- Foundation Breaker: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, you may destroy target artifact or enchantment.\nEvoke {1}{G} (You may cast this sp
- oracle_text advisory -- Fulminator Mage: oracle_text diverges (similarity 0.07); scryfall='Sacrifice this creature: Destroy target nonbasic land.'
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample\nThis creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.\nWhenever this creature enters or attacks, it deals 3 damage 
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —\n• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.17); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Smokebraider: oracle_text diverges (similarity 0.12); scryfall='{T}: Add two mana in any combination of colors. Spend this mana only to cast Elemental spells or activate abil
- oracle_text advisory -- Incandescent Soulstoke: oracle_text diverges (similarity 0.10); scryfall='Other Elemental creatures you control get +1/+1.\n{1}{R}, {T}: You may put an Elemental creature card from you
- oracle_text advisory -- Fury: oracle_text diverges (similarity 0.13); scryfall='Double strike\nWhen this creature enters, it deals 4 damage divided as you choose among any number of target c
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste\nWhenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying\n{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}\nWhen this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.\n{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.\n{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nIf you would gain life, you 
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.\n{3}{B}{B}: Creatures you control gain lifelink 
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.\n{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.\n{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying\nIf noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.\n{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)\nSearch your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nSacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nWhen this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.\nEternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nWhen this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.\nFerocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Nourish: oracle_text diverges (similarity 0.03); scryfall='You gain 6 life.'
- oracle_text advisory -- Eternal Witness: oracle_text diverges (similarity 0.13); scryfall='When this creature enters, you may return target card from your graveyard to your hand.'
- oracle_text advisory -- Elvish Harbinger: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, you may search your library for an Elf card, reveal it, then shuffle and put that c
- oracle_text advisory -- Ezuri, Renegade Leader: oracle_text diverges (similarity 0.16); scryfall='{G}: Regenerate another target Elf.\n{2}{G}{G}{G}: Elf creatures you control get +3/+3 and gain trample until 
- oracle_text advisory -- Scholar of Athreos: oracle_text diverges (similarity 0.13); scryfall='{2}{B}: Each opponent loses 1 life. You gain life equal to the life lost this way.'
- oracle_text advisory -- Brindle Boar: oracle_text diverges (similarity 0.05); scryfall='Sacrifice this creature: You gain 4 life.'
- oracle_text advisory -- Weather the Storm: oracle_text diverges (similarity 0.14); scryfall='You gain 3 life.\nStorm (When you cast this spell, copy it for each spell cast before it this turn.)'
- oracle_text advisory -- Utopia Sprawl: oracle_text diverges (similarity 0.21); scryfall='Enchant Forest\nAs this Aura enters, choose a color.\nWhenever enchanted Forest is tapped for mana, its contro
- oracle_text advisory -- Crab Umbra: oracle_text diverges (similarity 0.16); scryfall='Enchant creature\n{2}{U}: Untap enchanted creature.\nUmbra armor (If enchanted creature would be destroyed, in
- oracle_text advisory -- Staff of Domination: oracle_text diverges (similarity 0.09); scryfall='{1}: Untap this artifact.\n{2}, {T}: You gain 1 life.\n{3}, {T}: Untap target creature.\n{4}, {T}: Tap target 
- oracle_text advisory -- Galvanic Alchemist: oracle_text diverges (similarity 0.18); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Seedcradle Witch: oracle_text diverges (similarity 0.08); scryfall='{2}{G}{W}: Target creature gets +3/+3 until end of turn. Untap that creature.'
- oracle_text advisory -- Trufflesnout: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, choose one —\n• Put a +1/+1 counter on this creature.\n• You gain 4 life.'
- oracle_text advisory -- Heroes' Reunion: oracle_text diverges (similarity 0.01); scryfall='Target player gains 7 life.'
- oracle_text advisory -- Rest for the Weary: oracle_text diverges (similarity 0.05); scryfall='Target player gains 4 life.\nLandfall — If you had a land enter the battlefield under your control this turn, 
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.03); scryfall='{T}: Add one mana of any color.\n{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.\n{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.12); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike\nNoncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.\nWhenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.\nSacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until 
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.\nWhenever General Kudro or another Human you control enters, exile target 
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash\n{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.22); scryfall='First strike\nWard—Pay 3 life.\nNightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.\n{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.\n{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Waterlogged Grove: oracle_text diverges (similarity 0.11); scryfall='{T}, Pay 1 life: Add {G} or {U}.\n{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Adelbert Steiner: oracle_text diverges (similarity 0.02); scryfall='Lifelink\nAdelbert Steiner gets +1/+1 for each Equipment you control.'
- oracle_text advisory -- Adaptive Omnitool: oracle_text diverges (similarity 0.07); scryfall='Equipped creature gets +1/+1 for each artifact you control.\nWhenever equipped creature attacks, look at the t
- oracle_text advisory -- Kíli the Resourceful: oracle_text diverges (similarity 0.07); scryfall='Storied (If you control three or more artifacts, legendaries, and/or Sagas, you have an enduring story for the
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature\nEnchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature\nEnchanted creature has flying.\nAura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.\nChannel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an 
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance\nWhenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature\nWhen this Aura enters, tap enchanted creature.\nEnchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature\nEnchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.06); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.38); scryfall='Enchant creature\nEnchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature\nEnchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.\n{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- oracle_text advisory -- Linvala, Shield of Sea Gate: oracle_text diverges (similarity 0.31); scryfall="Flying\nAt the beginning of combat on your turn, if you have a full party, choose target nonland permanent an 
- oracle_text advisory -- Troyan, Gutsy Explorer: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}{U}. Spend this mana only to cast spells with mana value 5 or greater or spells with {X} in their 
- oracle_text advisory -- Detention Sphere: oracle_text diverges (similarity 0.25); scryfall="When this enchantment enters, you may exile target nonland permanent not named Detention Sphere and all other 
- oracle_text advisory -- Steel of the Godhead: oracle_text diverges (similarity 0.31); scryfall="Enchant creature\nAs long as enchanted creature is white, it gets +1/+1 and has lifelink. (Damage dealt by the
- oracle_text advisory -- Auroral Procession: oracle_text diverges (similarity 0.07); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Reborn Hope: oracle_text diverges (similarity 0.10); scryfall='Return target multicolored card from your graveyard to your hand.'
- oracle_text advisory -- Rampaging Baloths: oracle_text diverges (similarity 0.14); scryfall='Trample\nLandfall — Whenever a land you control enters, create a 4/4 green Beast creature token.'
- oracle_text advisory -- Mossborn Hydra: oracle_text diverges (similarity 0.13); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)\nThis crea
- oracle_text advisory -- Scythecat Cub: oracle_text diverges (similarity 0.16); scryfall='Trample\nLandfall — Whenever a land you control enters, put a +1/+1 counter on target creature you control. If
- oracle_text advisory -- Scute Swarm: oracle_text diverges (similarity 0.15); scryfall="Landfall — Whenever a land you control enters, create a 1/1 green Insect creature token. If you control six or
- oracle_text advisory -- Moraug, Fury of Akoum: oracle_text diverges (similarity 0.17); scryfall="Each creature you control gets +1/+0 for each time it has attacked this turn.\nLandfall — Whenever a land you 
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.\nLandfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.\nWhenever 
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.\nLandfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.\nYou may play lands from the top of your library.\nLandfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.\nRemove three 
- oracle_text advisory -- Akoum Hellhound: oracle_text diverges (similarity 0.15); scryfall='Landfall — Whenever a land you control enters, this creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Brushfire Elemental: oracle_text diverges (similarity 0.26); scryfall="Haste\nThis creature can't be blocked by creatures with power 2 or less.\nLandfall — Whenever a land you contr
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.26); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.\nSuspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.\n{T}: Add {C}.\n{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample\nWhenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.\n{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.\n{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- oracle_text advisory -- Temple of Abandon: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Deceit: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Enlightenment: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malady: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malice: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Mystery: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Plenty: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Triumph: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Zhalfirin Void: oracle_text diverges (similarity 0.74); scryfall='When this land enters, scry 1. (Look at the top card of your library. You may put that card on the bottom.)\n{
- oracle_text advisory -- Crystal Grotto: oracle_text diverges (similarity 0.39); scryfall='When this land enters, scry 1.\n{T}: Add {C}.\n{1}, {T}: Add one mana of any color.'
- oracle_text advisory -- Rumble Arena: oracle_text diverges (similarity 0.44); scryfall='Vigilance\nWhen this land enters, scry 1. (Look at the top card of your library. You may put it on the bottom.
- oracle_text advisory -- The Grey Havens: oracle_text diverges (similarity 0.24); scryfall='When The Grey Havens enters, scry 1.\n{T}: Add {C}.\n{T}: Add one mana of any color among legendary creature c
- oracle_text advisory -- Galadriel of Lothlórien: oracle_text diverges (similarity 0.24); scryfall='Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3.\nWhene
- oracle_text advisory -- Clifftop Retreat: oracle_text diverges (similarity 0.37); scryfall='This land enters tapped unless you control a Mountain or a Plains.\n{T}: Add {R} or {W}.'
- oracle_text advisory -- Furycalm Snarl: oracle_text diverges (similarity 0.31); scryfall="As this land enters, you may reveal a Mountain or Plains card from your hand. If you don't, this land enters t
- oracle_text advisory -- Steelshaper's Gift: oracle_text diverges (similarity 0.18); scryfall='Search your library for an Equipment card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Swiftblade Vindicator: oracle_text diverges (similarity 0.27); scryfall="Double strike (This creature deals both first-strike and regular combat damage.)\nVigilance (Attacking doesn't
- oracle_text advisory -- Lizard Blades: oracle_text diverges (similarity 0.11); scryfall="Double strike\nEquipped creature has double strike.\nReconfigure {2} ({2}: Attach to target creature you contr
- oracle_text advisory -- Magnetic Theft: oracle_text diverges (similarity 0.04); scryfall="Attach target Equipment to target creature. (Control of the Equipment doesn't change.)"
- oracle_text advisory -- Forge Anew: oracle_text diverges (similarity 0.09); scryfall='When this enchantment enters, return target Equipment card from your graveyard to the battlefield.\nDuring you
- oracle_text advisory -- Raubahn, Bull of Ala Mhigo: oracle_text diverges (similarity 0.07); scryfall="Ward—Pay life equal to Raubahn's power.\nWhenever Raubahn attacks, attach up to one target Equipment you contr
- oracle_text advisory -- Speaker of the Heavens: oracle_text diverges (similarity 0.16); scryfall="Vigilance, lifelink (Attacking doesn't cause this creature to tap. Damage dealt by this creature also causes y
- oracle_text advisory -- Path to Exile: oracle_text diverges (similarity 0.12); scryfall='Exile target creature. Its controller may search their library for a basic land card, put that card onto the b
- oracle_text advisory -- Sulfur Falls: oracle_text diverges (similarity 0.27); scryfall='This land enters tapped unless you control an Island or a Mountain.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Gitaxian Probe: oracle_text diverges (similarity 0.16); scryfall="({U/P} can be paid with either {U} or 2 life.)\nLook at target player's hand.\nDraw a card."
- oracle_text advisory -- Generator Servant: oracle_text diverges (similarity 0.15); scryfall='{T}, Sacrifice this creature: Add {C}{C}. If any of that mana is spent on a creature spell, it gains haste unt
- oracle_text advisory -- Brainstorm: oracle_text diverges (similarity 0.07); scryfall='Draw three cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Legion Lieutenant: oracle_text diverges (similarity 0.11); scryfall='Other Vampires you control get +1/+1.'
- oracle_text advisory -- Vampire of the Dire Moon: oracle_text diverges (similarity 0.37); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nLifelink (Damage dealt by
- oracle_text advisory -- Falkenrath Pit Fighter: oracle_text diverges (similarity 0.11); scryfall='{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn.
- oracle_text advisory -- Stromkirk Condemned: oracle_text diverges (similarity 0.15); scryfall='Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn.'
- oracle_text advisory -- Voldaren Estate: oracle_text diverges (similarity 0.35); scryfall='{T}: Add {C}.\n{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.\n{5},
- oracle_text advisory -- Bloodghast: oracle_text diverges (similarity 0.19); scryfall="This creature can't block.\nThis creature has haste as long as an opponent has 10 or less life.\nLandfall — Wh
- oracle_text advisory -- Stromkirk Captain: oracle_text diverges (similarity 0.11); scryfall='First strike\nOther Vampire creatures you control get +1/+1 and have first strike.'
- oracle_text advisory -- Drana, Liberator of Malakir: oracle_text diverges (similarity 0.14); scryfall='Flying, first strike\nWhenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking cr
- oracle_text advisory -- Mavren Fein, Dusk Apostle: oracle_text diverges (similarity 0.14); scryfall='Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with life
- oracle_text advisory -- Vampire Socialite: oracle_text diverges (similarity 0.26); scryfall="Menace (This creature can't be blocked except by two or more creatures.)\nWhen this creature enters, if an opp
- oracle_text advisory -- Falkenrath Gorger: oracle_text diverges (similarity 0.27); scryfall="Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is equal to its
- oracle_text advisory -- Markov Baron: oracle_text diverges (similarity 0.26); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Asylum Visitor: oracle_text diverges (similarity 0.33); scryfall="At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and you lose 1 
- oracle_text advisory -- Captivating Vampire: oracle_text diverges (similarity 0.12); scryfall='Other Vampire creatures you control get +1/+1.\nTap five untapped Vampires you control: Gain control of target
- oracle_text advisory -- Bloodline Keeper: oracle_text diverges (similarity 0.12); scryfall='Flying\n{T}: Create a 2/2 black Vampire creature token with flying.\n{B}: Transform this creature. Activate on
- oracle_text advisory -- Lord of Lineage: oracle_text diverges (similarity 0.17); scryfall='Flying\nOther Vampire creatures you control get +2/+2.\n{T}: Create a 2/2 black Vampire creature token with fl
- oracle_text advisory -- Kessig Wolf Run: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {C}.\n{X}{R}{G}, {T}: Target creature gets +X/+0 and gains trample until end of turn.'
- oracle_text advisory -- Harrow: oracle_text diverges (similarity 0.19); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for up to two basic land card
- oracle_text advisory -- Explosive Vegetation: oracle_text diverges (similarity 0.29); scryfall='Search your library for up to two basic land cards, put them onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Nature's Lore: oracle_text diverges (similarity 0.19); scryfall='Search your library for a Forest card, put that card onto the battlefield, then shuffle.'
- oracle_text advisory -- Three Visits: oracle_text diverges (similarity 0.18); scryfall='Search your library for a Forest card, put it onto the battlefield, then shuffle.'
- oracle_text advisory -- Vernal Bloom: oracle_text diverges (similarity 0.14); scryfall='Whenever a Forest is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Gauntlet of Power: oracle_text diverges (similarity 0.19); scryfall='As this artifact enters, choose a color.\nCreatures of the chosen color get +1/+1.\nWhenever a basic land is t
- oracle_text advisory -- Nissa, Who Shakes the World: oracle_text diverges (similarity 0.25); scryfall='Whenever you tap a Forest for mana, add an additional {G}.\n+1: Put three +1/+1 counters on up to one target n
- oracle_text advisory -- Kalonian Hydra: oracle_text diverges (similarity 0.15); scryfall='Trample\nThis creature enters with four +1/+1 counters on it.\nWhenever this creature attacks, double the numb
- oracle_text advisory -- Garruk, Primal Hunter: oracle_text diverges (similarity 0.20); scryfall='+1: Create a 3/3 green Beast creature token.\n−3: Draw cards equal to the greatest power among creatures you c
- oracle_text advisory -- Primordial Hydra: oracle_text diverges (similarity 0.24); scryfall='This creature enters with X +1/+1 counters on it.\nAt the beginning of your upkeep, double the number of +1/+1
- oracle_text advisory -- Voracious Hydra: oracle_text diverges (similarity 0.21); scryfall="Trample\nThis creature enters with X +1/+1 counters on it.\nWhen this creature enters, choose one —\n• Double 
- oracle_text advisory -- Neverwinter Hydra: oracle_text diverges (similarity 0.29); scryfall='As this creature enters, roll X d6. It enters with a number of +1/+1 counters on it equal to the total of thos
- oracle_text advisory -- Steelbane Hydra: oracle_text diverges (similarity 0.16); scryfall='This creature enters with X +1/+1 counters on it.\n{2}{G}, Remove a +1/+1 counter from this creature: Destroy 
- oracle_text advisory -- Gargos, Vicious Watcher: oracle_text diverges (similarity 0.23); scryfall="Vigilance\nHydra spells you cast cost {4} less to cast.\nWhenever a creature you control becomes the target of
- oracle_text advisory -- Skyshroud Claim: oracle_text diverges (similarity 0.26); scryfall='Search your library for up to two Forest cards, put them onto the battlefield, then shuffle.'
- oracle_text advisory -- Martyr of Sands: oracle_text diverges (similarity 0.06); scryfall='{1}, Reveal X white cards from your hand, Sacrifice this creature: You gain three times X life.'
- oracle_text advisory -- Walking Ballista: oracle_text diverges (similarity 0.06); scryfall='This creature enters with X +1/+1 counters on it.\n{4}: Put a +1/+1 counter on this creature.\nRemove a +1/+1 
- oracle_text advisory -- Abiding Grace: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your end step, choose one —\n• You gain 1 life.\n• Return target creature card with mana v
- oracle_text advisory -- Emeria, the Sky Ruin: oracle_text diverges (similarity 0.19); scryfall='This land enters tapped.\nAt the beginning of your upkeep, if you control seven or more Plains, you may return
- oracle_text advisory -- Brainsurge: oracle_text diverges (similarity 0.09); scryfall='Draw four cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Street Wraith: oracle_text diverges (similarity 0.26); scryfall="Swampwalk (This creature can't be blocked as long as defending player controls a Swamp.)\nCycling—Pay 2 life. 
- oracle_text advisory -- Unpredictable Cyclone: oracle_text diverges (similarity 0.31); scryfall="If a cycling ability of another nonland card would cause you to draw a card, instead exile cards from the top 
- oracle_text advisory -- Curator of Mysteries: oracle_text diverges (similarity 0.16); scryfall='Flying\nWhenever you cycle or discard another card, scry 1.\nCycling {U} ({U}, Discard this card: Draw a card.
- oracle_text advisory -- Emrakul, the Aeons Torn: oracle_text diverges (similarity 0.17); scryfall="This spell can't be countered.\nWhen you cast this spell, take an extra turn after this one.\nFlying, protecti
- oracle_text advisory -- Ulamog, the Ceaseless Hunger: oracle_text diverges (similarity 0.15); scryfall='When you cast this spell, exile two target permanents.\nIndestructible\nWhenever Ulamog attacks, defending pla
- oracle_text advisory -- Ulamog, the Infinite Gyre: oracle_text diverges (similarity 0.31); scryfall='When you cast this spell, destroy target permanent.\nIndestructible\nAnnihilator 4 (Whenever this creature att
- oracle_text advisory -- Long-Term Plans: oracle_text diverges (similarity 0.10); scryfall='Search your library for a card, then shuffle and put that card third from the top.'
- oracle_text advisory -- Toxic Deluge: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, pay X life.\nAll creatures get -X/-X until end of turn.'
- oracle_text advisory -- Lord of Atlantis: oracle_text diverges (similarity 0.47); scryfall="Other Merfolk get +1/+1 and have islandwalk. (They can't be blocked as long as defending player controls an Is
- oracle_text advisory -- Master of the Pearl Trident: oracle_text diverges (similarity 0.63); scryfall="Other Merfolk creatures you control get +1/+1 and have islandwalk. (They can't be blocked as long as defending
- oracle_text advisory -- Vodalian Hexcatcher: oracle_text diverges (similarity 0.39); scryfall='Flash\nOther Merfolk you control get +1/+1.\nSacrifice a Merfolk: Counter target noncreature spell unless its 
- oracle_text advisory -- Phantasmal Image: oracle_text diverges (similarity 0.58); scryfall='You may have this creature enter as a copy of any creature on the battlefield, except it\'s an Illusion in add
- oracle_text advisory -- Cosi's Trickster: oracle_text diverges (similarity 0.38); scryfall='Whenever an opponent shuffles their library, you may put a +1/+1 counter on this creature.'
- oracle_text advisory -- Sink into Stupor: oracle_text diverges (similarity 0.58); scryfall="Return target spell or nonland permanent an opponent controls to its owner's hand."
- oracle_text advisory -- Dark Ritual: oracle_text diverges (similarity 0.04); scryfall='Add {B}{B}{B}.'
- oracle_text advisory -- Demonic Tutor: oracle_text diverges (similarity 0.21); scryfall='Search your library for a card, put that card into your hand, then shuffle.'
- oracle_text advisory -- Diabolic Intent: oracle_text diverges (similarity 0.25); scryfall='As an additional cost to cast this spell, sacrifice a creature.\nSearch your library for a card, put that card
- oracle_text advisory -- Culling the Weak: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, sacrifice a creature.\nAdd {B}{B}{B}{B}.'
- oracle_text advisory -- Lake of the Dead: oracle_text diverges (similarity 0.14); scryfall="If this land would enter, sacrifice a Swamp instead. If you do, put this land onto the battlefield. If you don
- oracle_text advisory -- Profane Tutor: oracle_text diverges (similarity 0.27); scryfall='Suspend 2—{1}{B} (Rather than cast this card from your hand, pay {1}{B} and exile it with two time counters on
- oracle_text advisory -- Shadowborn Apostle: oracle_text diverges (similarity 0.17); scryfall='A deck can have any number of cards named Shadowborn Apostle.\n{B}, Sacrifice six creatures named Shadowborn A
- oracle_text advisory -- Abhorrent Overlord: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhen this creature enters, create a number of 1/1 black Harpy creature tokens with flying equal to you
- oracle_text advisory -- Reaper from the Abyss: oracle_text diverges (similarity 0.15); scryfall='Flying\nMorbid — At the beginning of each end step, if a creature died this turn, destroy target non-Demon cre
- oracle_text advisory -- Sire of Insanity: oracle_text diverges (similarity 0.14); scryfall='At the beginning of each end step, each player discards their hand.'
- oracle_text advisory -- Jetmir, Nexus of Revels: oracle_text diverges (similarity 0.40); scryfall='Creatures you control get +1/+0 and have vigilance as long as you control three or more creatures.\nCreatures 
- oracle_text advisory -- Thrumming Stone: oracle_text diverges (similarity 0.17); scryfall='Spells you cast have ripple 4. (Whenever you cast a spell, you may reveal the top four cards of your library. 
- oracle_text advisory -- Chariot of Victory: oracle_text diverges (similarity 0.45); scryfall='Equipped creature has first strike, trample, and haste.\nEquip {1}'
- oracle_text advisory -- Voyaging Satyr: oracle_text diverges (similarity 0.13); scryfall='{T}: Untap target land.'
- oracle_text advisory -- Surrak and Goreclaw: oracle_text diverges (similarity 0.63); scryfall='Trample\nOther creatures you control have trample.\nWhenever another nontoken creature you control enters, put
- oracle_text advisory -- Nylea, God of the Hunt: oracle_text diverges (similarity 0.54); scryfall="Indestructible\nAs long as your devotion to green is less than five, Nylea isn't a creature. (Each {G} in the 
- oracle_text advisory -- Extraplanar Lens: oracle_text diverges (similarity 0.51); scryfall='Imprint — When this artifact enters, you may exile target land you control.\nWhenever a land with the same nam
- oracle_text advisory -- Horn of Greed: oracle_text diverges (similarity 0.27); scryfall='Whenever a player plays a land, that player draws a card.'
- oracle_text advisory -- Howling Mine: oracle_text diverges (similarity 0.37); scryfall="At the beginning of each player's draw step, if this artifact is untapped, that player draws an additional car
- oracle_text advisory -- Thraben Inspector: oracle_text diverges (similarity 0.26); scryfall='When this creature enters, investigate. (Create a Clue token. It\'s an artifact with "{2}, Sacrifice this toke
- oracle_text advisory -- Squadron Hawk: oracle_text diverges (similarity 0.19); scryfall='Flying\nWhen this creature enters, you may search your library for up to three cards named Squadron Hawk, reve
- oracle_text advisory -- Curious Farm Animals: oracle_text diverges (similarity 0.10); scryfall='When this creature dies, you gain 3 life.\n{2}, Sacrifice this creature: Destroy up to one target artifact or 
- oracle_text advisory -- Kami of False Hope: oracle_text diverges (similarity 0.09); scryfall='Sacrifice this creature: Prevent all combat damage that would be dealt this turn.'
- oracle_text advisory -- Enduring Innocence: oracle_text diverges (similarity 0.20); scryfall="Lifelink\nWhenever one or more other creatures you control with power 2 or less enter, draw a card. This abili
- oracle_text advisory -- Lurrus of the Dream-Den: oracle_text diverges (similarity 0.23); scryfall='Companion — Each permanent card in your starting deck has mana value 2 or less. (If this card is your chosen c
- oracle_text advisory -- Careful Study: oracle_text diverges (similarity 0.27); scryfall='Draw two cards, then discard two cards.'
- oracle_text advisory -- Faithless Looting: oracle_text diverges (similarity 0.59); scryfall='Draw two cards, then discard two cards.\nFlashback {2}{R} (You may cast this card from your graveyard for its 
- oracle_text advisory -- Purphoros, God of the Forge: oracle_text diverges (similarity 0.60); scryfall="Indestructible\nAs long as your devotion to red is less than five, Purphoros isn't a creature.\nWhenever anoth
- oracle_text advisory -- Mystic Retrieval: oracle_text diverges (similarity 0.49); scryfall='Return target instant or sorcery card from your graveyard to your hand.\nFlashback {2}{R} (You may cast this c
- oracle_text advisory -- Magus of the Bazaar: oracle_text diverges (similarity 0.22); scryfall='{T}: Draw two cards, then discard three cards.'
- oracle_text advisory -- Cephalid Coliseum: oracle_text diverges (similarity 0.62); scryfall='{T}: Add {U}. This land deals 1 damage to you.\nThreshold — {U}, {T}, Sacrifice this land: Target player draws
- oracle_text advisory -- Golgari Grave-Troll: oracle_text diverges (similarity 0.53); scryfall='This creature enters with a +1/+1 counter on it for each creature card in your graveyard.\n{1}, Remove a +1/+1
- oracle_text advisory -- Stinkweed Imp: oracle_text diverges (similarity 0.64); scryfall='Flying\nWhenever this creature deals combat damage to a creature, destroy that creature.\nDredge 5 (If you wou
- oracle_text advisory -- Life from the Loam: oracle_text diverges (similarity 0.68); scryfall='Return up to three target land cards from your graveyard to your hand.\nDredge 3 (If you would draw a card, yo
- oracle_text advisory -- Keranos, God of Storms: oracle_text diverges (similarity 0.58); scryfall="Indestructible\nAs long as your devotion to blue and red is less than seven, Keranos isn't a creature.\nReveal
- oracle_text advisory -- Mogis, God of Slaughter: oracle_text diverges (similarity 0.58); scryfall="Indestructible\nAs long as your devotion to black and red is less than seven, Mogis isn't a creature.\nAt the 
- oracle_text advisory -- Klothys, God of Destiny: oracle_text diverges (similarity 0.65); scryfall="Indestructible\nAs long as your devotion to red and green is less than seven, Klothys isn't a creature.\nAt th
- oracle_text advisory -- Ephara, God of the Polis: oracle_text diverges (similarity 0.57); scryfall="Indestructible\nAs long as your devotion to white and blue is less than seven, Ephara isn't a creature.\nAt th
- oracle_text advisory -- Phenax, God of Deception: oracle_text diverges (similarity 0.65); scryfall='Indestructible\nAs long as your devotion to blue and black is less than seven, Phenax isn\'t a creature.\nCrea
- oracle_text advisory -- Wild Defiance: oracle_text diverges (similarity 0.21); scryfall='Whenever a creature you control becomes the target of an instant or sorcery spell, that creature gets +3/+3 un
- oracle_text advisory -- Berserk: oracle_text diverges (similarity 0.25); scryfall='Cast this spell only before the combat damage step.\nTarget creature gains trample and gets +X/+0 until end of
- oracle_text advisory -- Temur Battle Rage: oracle_text diverges (similarity 0.33); scryfall='Target creature gains double strike until end of turn.\nFerocious — That creature also gains trample until end
- oracle_text advisory -- Might of Old Krosa: oracle_text diverges (similarity 0.33); scryfall='Target creature gets +2/+2 until end of turn. If you cast this spell during your main phase, that creature get
- oracle_text advisory -- Vines of Vastwood: oracle_text diverges (similarity 0.44); scryfall="Kicker {G} (You may pay an additional {G} as you cast this spell.)\nTarget creature can't be the target of spe
- oracle_text advisory -- Legion Leadership: oracle_text diverges (similarity 0.11); scryfall="Until end of turn, double target creature's power and it gains first strike."
- oracle_text advisory -- Spellskite: oracle_text diverges (similarity 0.18); scryfall='{U/P}: Change a target of target spell or ability to this creature. ({U/P} can be paid with either {U} or 2 li
- oracle_text advisory -- Blistering Firecat: oracle_text diverges (similarity 0.21); scryfall='Trample, haste\nAt the beginning of the end step, sacrifice this creature.\nMorph {R}{R} (You may cast this ca
- oracle_text advisory -- Hystrodon: oracle_text diverges (similarity 0.31); scryfall='Trample\nWhenever this creature deals combat damage to a player, you may draw a card.\nMorph {1}{G}{G} (You ma
- oracle_text advisory -- Den Protector: oracle_text diverges (similarity 0.37); scryfall="Creatures with power less than this creature's power can't block it.\nMegamorph {1}{G} (You may cast this card
- oracle_text advisory -- Fire-Lit Thicket: oracle_text diverges (similarity 0.36); scryfall='{T}: Add {C}.\n{R/G}, {T}: Add {R}{R}, {R}{G}, or {G}{G}.'
- oracle_text advisory -- Drogskol Captain: oracle_text diverges (similarity 0.25); scryfall="Flying\nOther Spirit creatures you control get +1/+1 and have hexproof. (They can't be the targets of spells o
- oracle_text advisory -- Supreme Phantom: oracle_text diverges (similarity 0.17); scryfall='Flying\nOther Spirits you control get +1/+1.'
- oracle_text advisory -- Shacklegeist: oracle_text diverges (similarity 0.17); scryfall="Flying\nThis creature can block only creatures with flying.\nTap two untapped Spirits you control: Tap target 
- oracle_text advisory -- Spirit of the Labyrinth: oracle_text diverges (similarity 0.08); scryfall="Each player can't draw more than one card each turn."
- oracle_text advisory -- Flooded Strand: oracle_text diverges (similarity 0.14); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Island card, put it onto the battlef
- oracle_text advisory -- Geist of Saint Traft: oracle_text diverges (similarity 0.23); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)\nWhenever Geist of
- oracle_text advisory -- Lingering Souls: oracle_text diverges (similarity 0.16); scryfall='Create two 1/1 white Spirit creature tokens with flying.\nFlashback {1}{B} (You may cast this card from your g
- oracle_text advisory -- Patrician Geist: oracle_text diverges (similarity 0.14); scryfall='Flying\nOther Spirits you control get +1/+1.\nSpells you cast from your graveyard cost {1} less to cast.'
- oracle_text advisory -- Mausoleum Wanderer: oracle_text diverges (similarity 0.19); scryfall="Flying\nWhenever another Spirit you control enters, this creature gets +1/+1 until end of turn.\nSacrifice thi
- oracle_text advisory -- Skyclave Apparition: oracle_text diverges (similarity 0.28); scryfall="When this creature enters, exile up to one target nonland, nontoken permanent you don't control with mana valu
- oracle_text advisory -- White Orchid Phantom: oracle_text diverges (similarity 0.21); scryfall='Flying, first strike\nWhen this creature enters, destroy up to one target nonbasic land. Its controller may se
- oracle_text advisory -- Usher of the Fallen: oracle_text diverges (similarity 0.09); scryfall='Boast — {1}{W}: Create a 1/1 white Human Warrior creature token. (Activate only if this creature attacked this
- oracle_text advisory -- Millicent, Restless Revenant: oracle_text diverges (similarity 0.16); scryfall='Affinity for Spirits (This spell costs {1} less to cast for each Spirit you control.)\nFlying\nWhenever Millic
- oracle_text advisory -- Zhur-Taa Druid: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {G}.\nWhenever you tap this creature for mana, it deals 1 damage to each opponent.'
- oracle_text advisory -- Spark Elemental: oracle_text diverges (similarity 0.46); scryfall="Trample, haste (This creature can deal excess combat damage to the player or planeswalker it's attacking. This
- oracle_text advisory -- Ball Lightning: oracle_text diverges (similarity 0.53); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)\nHaste (Th
- oracle_text advisory -- Flamekin Bladewhirl: oracle_text diverges (similarity 0.12); scryfall='As an additional cost to cast this spell, reveal an Elemental card from your hand or pay {3}.'
- oracle_text advisory -- Sunspine Lynx: oracle_text diverges (similarity 0.19); scryfall="Players can't gain life.\nDamage can't be prevented.\nWhen this creature enters, it deals damage to each playe
- oracle_text advisory -- Hellspark Elemental: oracle_text diverges (similarity 0.16); scryfall='Trample, haste\nAt the beginning of the end step, sacrifice this creature.\nUnearth {1}{R} ({1}{R}: Return thi
- oracle_text advisory -- Nova Chaser: oracle_text diverges (similarity 0.12); scryfall='Trample\nChampion an Elemental (When this enters, sacrifice it unless you exile another Elemental you control.
- oracle_text advisory -- Thunderkin Awakener: oracle_text diverges (similarity 0.17); scryfall="Haste\nWhenever this creature attacks, choose target Elemental creature card in your graveyard with toughness 
- oracle_text advisory -- Mogg Maniac: oracle_text diverges (similarity 0.31); scryfall='Whenever this creature is dealt damage, it deals that much damage to target opponent or planeswalker.'
- oracle_text advisory -- Spiteful Sliver: oracle_text diverges (similarity 0.17); scryfall='Sliver creatures you control have "Whenever this creature is dealt damage, it deals that much damage to target
- oracle_text advisory -- Nomads en-Kor: oracle_text diverges (similarity 0.11); scryfall='{0}: The next 1 damage that would be dealt to this creature this turn is dealt to target creature you control 
- oracle_text advisory -- Shaman en-Kor: oracle_text diverges (similarity 0.13); scryfall='{0}: The next 1 damage that would be dealt to this creature this turn is dealt to target creature you control 
- oracle_text advisory -- Stuffy Doll: oracle_text diverges (similarity 0.14); scryfall='Indestructible\nAs this creature enters, choose a player.\nWhenever this creature is dealt damage, it deals th
- oracle_text advisory -- Guilty Conscience: oracle_text diverges (similarity 0.13); scryfall='Enchant creature\nWhenever enchanted creature deals damage, this Aura deals that much damage to that creature.
- oracle_text advisory -- Screaming Nemesis: oracle_text diverges (similarity 0.27); scryfall="Haste\nWhenever this creature is dealt damage, it deals that much damage to any other target. If a player is d
- oracle_text advisory -- Blasphemous Act: oracle_text diverges (similarity 0.20); scryfall='This spell costs {1} less to cast for each creature on the battlefield.\nBlasphemous Act deals 13 damage to ea
- oracle_text advisory -- Chain Reaction: oracle_text diverges (similarity 0.41); scryfall='Chain Reaction deals X damage to each creature, where X is the number of creatures on the battlefield.'
- oracle_text advisory -- Loran's Escape: oracle_text diverges (similarity 0.10); scryfall='Target artifact or creature gains hexproof and indestructible until end of turn. Scry 1.'
- oracle_text advisory -- Worn Powerstone: oracle_text diverges (similarity 0.15); scryfall='This artifact enters tapped.\n{T}: Add {C}{C}.'
- oracle_text advisory -- Flooded Grove: oracle_text diverges (similarity 0.16); scryfall='{T}: Add {C}.\n{G/U}, {T}: Add {G}{G}, {G}{U}, or {U}{U}.'
- oracle_text advisory -- Hedge Maze: oracle_text diverges (similarity 0.22); scryfall='({T}: Add {G} or {U}.)\nThis land enters tapped.\nWhen this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Spawning Kraken: oracle_text diverges (similarity 0.20); scryfall='Whenever a Kraken, Leviathan, Octopus, or Serpent you control deals combat damage to a player, create a 9/9 bl
- oracle_text advisory -- Koma, World-Eater: oracle_text diverges (similarity 0.19); scryfall="This spell can't be countered.\nTrample, ward {4}\nWhenever Koma deals combat damage to a player, create four 
- oracle_text advisory -- Serpent of Yawning Depths: oracle_text diverges (similarity 0.29); scryfall="Krakens, Leviathans, Octopuses, and Serpents you control can't be blocked except by Krakens, Leviathans, Octop
- oracle_text advisory -- Lorthos, the Tidemaker: oracle_text diverges (similarity 0.24); scryfall="Whenever Lorthos attacks, you may pay {8}. If you do, tap up to eight target permanents. Those permanents don'
- oracle_text advisory -- Whelming Wave: oracle_text diverges (similarity 0.13); scryfall="Return all creatures to their owners' hands except for Krakens, Leviathans, Octopuses, and Serpents."
- oracle_text advisory -- Kiora Bests the Sea God: oracle_text diverges (similarity 0.23); scryfall="(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — Create an 8/8 bl
- oracle_text advisory -- Summon: Leviathan: oracle_text diverges (similarity 0.22); scryfall="(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — Return each crea
- oracle_text advisory -- Hullbreaker Horror: oracle_text diverges (similarity 0.12); scryfall="Flash\nThis spell can't be countered.\nWhenever you cast a spell, choose up to one —\n• Return target spell yo
- oracle_text advisory -- Brinelin, the Moon Kraken: oracle_text diverges (similarity 0.14); scryfall="When Brinelin enters and whenever you cast a spell with mana value 6 or greater, you may return target nonland
- oracle_text advisory -- Sin, Unending Cataclysm: oracle_text diverges (similarity 0.15); scryfall="Flying, trample\nAs Sin enters, remove all counters from any number of artifacts, creatures, and enchantments.
- oracle_text advisory -- Trench Gorger: oracle_text diverges (similarity 0.18); scryfall='Trample\nWhen this creature enters, you may search your library for any number of land cards, exile them, then
- oracle_text advisory -- Maze's End: oracle_text diverges (similarity 0.30); scryfall="This land enters tapped.\n{T}: Add {C}.\n{3}, {T}, Return this land to its owner's hand: Search your library f
- oracle_text advisory -- Azorius Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {W} or {U}.'
- oracle_text advisory -- Boros Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {R} or {W}.'
- oracle_text advisory -- Dimir Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U} or {B}.'
- oracle_text advisory -- Golgari Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {B} or {G}.'
- oracle_text advisory -- Gruul Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Izzet Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Orzhov Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {W} or {B}.'
- oracle_text advisory -- Rakdos Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {B} or {R}.'
- oracle_text advisory -- Selesnya Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.'
- oracle_text advisory -- Simic Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {G} or {U}.'
- oracle_text advisory -- Planar Nexus: oracle_text diverges (similarity 0.39); scryfall="This land is every nonbasic land type. (Nonbasic land types include Cave, Desert, Gate, Lair, Locus, Mine, Pow
- oracle_text advisory -- Guild Summit: oracle_text diverges (similarity 0.35); scryfall='When this enchantment enters, you may tap any number of untapped Gates you control. Draw a card for each Gate 
- oracle_text advisory -- Gates Ablaze: oracle_text diverges (similarity 0.28); scryfall='Gates Ablaze deals X damage to each creature, where X is the number of Gates you control.'
- oracle_text advisory -- Archdruid's Charm: oracle_text diverges (similarity 0.45); scryfall="Choose one —\n• Search your library for a creature or land card and reveal it. Put it onto the battlefield tap
- oracle_text advisory -- Gond Gate: oracle_text diverges (similarity 0.19); scryfall='Gates you control enter untapped.\n{T}: Add {C}.\n{T}: Add one mana of any color that a Gate you control could
- oracle_text advisory -- Exploration: oracle_text diverges (similarity 0.13); scryfall='You may play an additional land on each of your turns.'
- oracle_text advisory -- Burgeoning: oracle_text diverges (similarity 0.20); scryfall='Whenever an opponent plays a land, you may put a land card from your hand onto the battlefield.'
- oracle_text advisory -- Manabond: oracle_text diverges (similarity 0.32); scryfall='At the beginning of your end step, you may reveal your hand and put all land cards from it onto the battlefiel
- oracle_text advisory -- Amulet of Vigor: oracle_text diverges (similarity 0.10); scryfall='Whenever a permanent you control enters tapped, untap it.'
- oracle_text advisory -- Thran Portal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\nAs this land enters, choose a basic land
- oracle_text advisory -- Gateway Plaza: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped.\nWhen this land enters, sacrifice it unless you pay {1}.\n{T}: Add one mana of any co
- oracle_text advisory -- Sea Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than blue.\n{T}: Add {U} or one mana of th
- oracle_text advisory -- Manor Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than green.\n{T}: Add {G} or one mana of t
- oracle_text advisory -- Cliffgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than red.\n{T}: Add {R} or one mana of the
- oracle_text advisory -- Citadel Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than white.\n{T}: Add {W} or one mana of t
- oracle_text advisory -- Black Dragon Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than black.\n{T}: Add {B} or one mana of t
- oracle_text advisory -- The Black Gate: oracle_text diverges (similarity 0.37); scryfall="As The Black Gate enters, you may pay 3 life. If you don't, it enters tapped.\n{T}: Add {B}.\n{1}{B}, {T}: Cho
- oracle_text advisory -- Heap Gate: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.\n{1}, {T}: Add one mana of any color.\n{1}, {T}, Tap an untapped Gate you control: Create a Trea
- oracle_text advisory -- Baldur's Gate: oracle_text diverges (similarity 0.22); scryfall='{T}: Add {C}.\n{2}, {T}: Add X mana of any one color, where X is the number of other Gates you control.'
- oracle_text advisory -- Basilisk Gate: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{2}, {T}: Target creature gets +X/+X until end of turn, where X is the number of Gates you cont
- oracle_text advisory -- Talon Gates of Madara: oracle_text diverges (similarity 0.20); scryfall='When this land enters, up to one target creature phases out.\n{T}: Add {C}.\n{1}, {T}: Add one mana of any col
- oracle_text advisory -- Vexing Devil: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, any opponent may have it deal 4 damage to them. If a player does, sacrifice this cr
- oracle_text advisory -- Keldon Champion: oracle_text diverges (similarity 0.35); scryfall='Haste\nEcho {2}{R}{R} (At the beginning of your upkeep, if this came under your control since the beginning of
- oracle_text advisory -- Keldon Marauders: oracle_text diverges (similarity 0.26); scryfall='Vanishing 2 (This creature enters with two time counters on it. At the beginning of your upkeep, remove a time
- oracle_text advisory -- Flame Javelin: oracle_text diverges (similarity 0.18); scryfall="({2/R} can be paid with any two mana or with {R}. This card's mana value is 6.)\nFlame Javelin deals 4 damage 
- oracle_text advisory -- Browbeat: oracle_text diverges (similarity 0.09); scryfall='Any player may have Browbeat deal 5 damage to them. If no one does, target player draws three cards.'
- oracle_text advisory -- Goblin Fireslinger: oracle_text diverges (similarity 0.08); scryfall='{T}: This creature deals 1 damage to target player or planeswalker.'
- oracle_text advisory -- Vulshok Sorcerer: oracle_text diverges (similarity 0.06); scryfall='Haste\n{T}: This creature deals 1 damage to any target.'
- oracle_text advisory -- Cinder Pyromancer: oracle_text diverges (similarity 0.10); scryfall='{T}: This creature deals 1 damage to target player or planeswalker.\nWhenever you cast a red spell, you may un
- oracle_text advisory -- Teetering Peaks: oracle_text diverges (similarity 0.11); scryfall='This land enters tapped.\nWhen this land enters, target creature gets +2/+0 until end of turn.\n{T}: Add {R}.'
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit a478b813c (HEAD 05259f1531dd); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->
