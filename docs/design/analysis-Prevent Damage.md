# Analysis ledger — Prevent Damage

Deck: `decks/Prevent Damage/Prevent Damage.cod` (Cockatrice). Started 2026-09-27.
Work branch: `prevent-damage-analysis` (worktree `/tmp/pd-wt`, based on origin `a21b790f`). It is kept
off the main checkout because that tree holds another agent's unpushed discard-policy WIP in the
same files (`cards.json`, `DecisionProviders.*`).

## What the deck does

Manabarbs / Spellshock / painlands / Ancient Tomb / City of Brass hurt us on every tap and cast.
**Tamanoa** ("whenever a noncreature source you control deals damage, you gain that much life")
turns each of those into lifegain. **Rhox Faithmender** doubles it (Bilbo, from the sideboard, adds
+1), and **Vito** (opponent loses that much) / **Dina** (each opponent loses 1 per event) turn the
lifegain into the win. Rolling Earthquake and Pyrohemia are both burn and a Tamanoa trigger,
because they hit every creature and every player. Tutors: Beseech the Queen, Green Sun's Zenith, and
4 Living Wish into a 13-card sideboard. The sideboard is therefore IN SCOPE.

## Stage 1 — coverage (2026-09-27)

21 missing: Manabarbs, Battlefield Forge, Ancient Tomb, Tamanoa, Pyrohemia, Tarnished Citadel,
Grand Coliseum, Karplusan Forest, Beseech the Queen, Rhox Faithmender, Spellshock, Rolling
Earthquake, Green Sun's Zenith, Vito, Dina; sideboard: Purity, Dimir House Guard, Shriekmaw,
Timeless Witness, Bilbo, Acidic Slime. Present and full: City of Brass, Brushland, Reflecting Pool,
Living Wish, Vexing Shusher.

### Engine gaps found before implementation

1. **No own-death check.** Our life can go negative with no consequence. The Tamanoa ruling makes
   this load-bearing: "if a noncreature source you control deals damage to you that drops your life
   total to 0 or less, you'll lose the game before Tamanoa's ability can resolve."
2. **Self-damage is raw `life -=`** at ~5 separate sites (ManaPayment.cpp, SpellEffects.cpp x2,
   SpellEffects.h, solver replays). It is not a damage EVENT with a source, so nothing can trigger
   on it.
3. **No lifegain replacement** (Faithmender x2, Bilbo +1). The shared `GainLife` hook +
   `FireLifegainWatchers` (once per event, CR 119.10) exists and is the right place.
4. **Twobrid `{2/B}` unsupported** (Card.h: "{2/W} remains unsupported").

### Event-granularity rulings (Scryfall, drive Dina/Bilbo math)

- Tamanoa triggers ONCE per damage event by a source, for the TOTAL dealt to all recipients
  (Pyrohemia activation, Rolling Earthquake = one trigger each). Each Pyrohemia activation and
  each Manabarbs trigger is its own event.
- Faithmender: multiplicative (two = x4). Bilbo: +1 per event, once per event.
- Dina: 1 life per lifegain EVENT regardless of amount. Vito: that much.

## RESUME STATE (2026-09-27, before compaction)

- Stage 2 research DONE: four Opus notes in `logs/prevent_damage/research/` (gitignored, in the
  worktree): `core_design.md` (THE blueprint), `lands_manabarbs.md`, `spells.md`,
  `creatures_sideboard.md`. Scryfall JSON in `logs/prevent_damage/scryfall/`.
- Integration is SERIAL, three phases, one Opus integrator agent each (shared files):
  * **I1 DONE, committed locally 6a36b5d4 (smoke byte-identical, 268/268 unit, 111/111 scenarios):** core damage-event/own-death/lifegain engine (gated on
    `dmg_events_armed`), Faithmender/Bilbo replacements, Vito/Dina watchers, Purity prevention,
    Tamanoa, Manabarbs, 5 lands (Ancient Tomb `tap_self_damage_any_mode`), `SelfDamageUseful` +
    pain sweep, `PreventDamageProvider` + routing, scenario tests, smoke byte-identity. It writes
    its results into the Stage 2 section below. If it is not reported there, check `git status` /
    `git diff --stat` in /tmp/pd-wt to see how far it got before re-launching.
  * **I2 DONE, committed locally (see Phase I2 below; smoke byte-identical, 281/281 unit, 114/114
    scenarios, sanity 194/200 avg 5.87, 0 self-deaths):** Spellshock, Pyrohemia (PingAll mode; PermAbilityTaps trap; end-step sac; K
    widening), Rolling Earthquake (X 0..max, not {max}), Beseech the Queen (twobrid, phyrexian-style;
    `{2/B}` is MISPARSED as `{B}` today), Green Sun's Zenith (Chord + colour filter + self-shuffle),
    `DeckUsesSecondMain` for Pyrohemia/Earthquake (m2 cost 4.25x on record -> give it an A/B lever).
    Blueprint: `spells.md`.
  * **I3 DONE, committed locally (see Phase I3 below; smoke byte-identical, 292/292 unit, 116/116
    scenarios, coverage 0 missing / 0 partial main+side, sanity 194/200 avg-win 5.856):** Vito team
    lifelink, Dina sac-pump (+ lethal-gated provider judgement), Shriekmaw (ETB + evoke), Acidic
    Slime, Timeless Witness (searched gy return + eternalize), Dimir House Guard (transmute;
    regeneration PROVISIONAL deferral), Bilbo 111-life activation, viewer rows. Blueprint:
    `creatures_sideboard.md`.
- NEXT: Stage 3 coverage loop (already clean at I3 -- re-run to confirm) -> Stage 4 profile -> 4a provider audit -> Stage 5 (verify_deck,
  harnesses, depth sweep, 5c2 leaf tie-break, 5d claude-play fan-out (Opus), 5h viewer, 5i BUCKET
  discard policy — gate `discard_policy` is in the MAIN tree's uncommitted WIP, not upstream) ->
  add to all three regression tiers with GT -> Stage 6 report.
- Branch `prevent-damage-analysis` is local only (nothing pushed). I1 6a36b5d4, I2 0e62da55, I3 = the commit directly on top of 0e62da55
  on top. Next: Stage 3/4/5 (the Stage 2 implementation is complete).

## Stage 2 — implementation

### Phase I1 — the damage-event core (DONE 2026-09-27, uncommitted in /tmp/pd-wt)

**Files.** New `src/core/DamageEvents.h` (the whole event model, heavily commented — read its header
first). Touched: `CardDatabase.{h,cpp}` (params), `GameState.h` (two deck-constant stamps +
`OpponentHasLost`), `Permanent.h` (`mana_tap_mark`), `SpellEffects.h` (include, `GainLife`
replacement, Vito/Dina watchers, drip-sweep + `DripLandAnyPipColor` arming, new
`TapPainSourcesIfUseful`), `SpellEffects.cpp` (backtracker `activate()` marks + undo, C-hoist skip,
Ancient Tomb any-mode, mana-cache per-tap marks + armed key fold + pay-with-pain latch),
`ManaPayment.cpp` (`TapSourceIntoFloat`, filter-land taps, `PermPaySnap`, outermost-payment
flush), `TurnSolver.cpp` (batch-prepay flush, `ApplyPlanDirect` backstop + pain sweep, rollout
own-death exits, `leafeval::kOwnDeath`, land signature `pa`), `AIEngine.cpp` (pain sweep at both
executor drip-sweep sites), `GameEngine.cpp` (own-death turn exits, main-phase backstop),
`GoldFishRunner.cpp` (`StampDeckTraits` arms the model), `Dominance.h` (boundary assertion + size
log), `DecisionProvider.h` (`SelfDamageUseful` hook), `DecisionProviders.{h,cpp}`
(`PreventDamageProvider` + routing), `scripts/audit_viewer_decisions.py` (8 INERT rows),
`test/unit/test_prevent_damage.cpp` (new, in CMake), `test/unit/test_pirates_provider.cpp` (routing
table row), six `test/scenarios/pd_*.json`.

**Arming.** `GameState::dmg_events_armed` is stamped in `GoldFishRunner::StampDeckTraits` — the ONE
function every entry point calls (SetupGame → runner / batch / analyzer / keep generators /
claude-play; the scenario harness and the cast-order report call it directly) — iff main OR
sideboard carries `noncreature_damage_lifegain` (Tamanoa), `land_tap_damage_each_player`
(Manabarbs), `prevent_noncombat_to_self_gain` (Purity), `lifegain_multiplier > 1` (Faithmender) or
`lifegain_plus` (Bilbo). `own_death_live` = armed || `MTG_OWN_DEATH_ALL=1` (measurement-only,
default OFF). Every change is behind one of the two, or behind a param no other deck carries.

**The model (see `DamageEvents.h`).**
1. A mana tap on an armed board takes its pain off our life at once (unless Purity prevents it) and
   RECORDS its triggers on the permanent (`Permanent::mana_tap_mark`: land-tap bit for Manabarbs,
   prevented bit for Purity, pain amount for Tamanoa). Marks ride every existing rollback for free
   (`PermPaySnap` gained the field; whole-battlefield snapshots copy it; the backtracker's per-node
   undo restores it) and the mana cache stores/replays them per tap.
2. `dmgev::FlushDamageEvents` turns marks into triggers once a payment COMMITS: outermost
   `TapForCostShared` success (nesting counter — the hybrid wrapper re-enters), the batch-prepay
   commit, and each tap of the drip / pain sweeps. Order: SBA first (the Tamanoa ruling — lethal
   pain loses before the trigger resolves), then every lifegain event (one per Tamanoa per pain
   event; one per Purity prevention), then every Manabarbs hit as its OWN `DealDamageEvent` (which
   triggers Tamanoa and resolves before the next barb), with an SBA check after each event.
3. Lifegain replacement in `GainLife`: `(a + #Bilbo) * Faithmender-product` (CR 616.1: +1 first is
   always at least as good). Vito (`lifegain_target_opp_loses_that_much`, replaced amount, one
   head) and Dina (`lifegain_each_opp_loses`, per event × heads) in `FireLifegainWatchers`; the
   `g_tap_speculating` early-out is kept and is correct — gains only ever happen at a flush, never
   inside the backtracker's speculation scope.
4. Own death: a loss PARKS our life at `dmgev::kLostLife` (-2^28) so no later gain revives us; the
   WON-LOCK makes damage to us a no-op once the opponent has lost; `OpponentHasLost` under
   `own_death_live` = raw ∧ our life > 0 (so both-at-0 in one event is a DRAW, CR 104.4a). Executor:
   `RunTurnFrom` returns after a main phase that killed us; `PlayOutFrom` then reads HasLost (-1).
   Rollout: `SimulateToEndImpl` exits at the loop head / after main 1 / after main 2 with
   `max_turns+1` and publishes `leafeval::kOwnDeath` (a valid tie-break quantity worse than every
   real one) — NEVER a Quantity for a suicide line.
5. `DealDamageEvent(src_ctrl, src_is_creature, combat, to_self, to_opp, to_creatures_total, src,
   tamanoa_lki)` is the entry point I2 routes Spellshock / Pyrohemia / Rolling Earthquake through
   (Purity prevention → immediate gain; Tamanoa once per event for the total; `tamanoa_lki` for a
   sweeper that kills Tamanoa in the same event).

**Params (all new, all gated).** `tap_self_damage_any_mode` (Ancient Tomb; also folded into the
land signature as `pa`), `noncreature_damage_lifegain`, `land_tap_damage_each_player`,
`prevent_noncombat_to_self_gain`, `lifegain_multiplier`, `lifegain_plus`,
`lifegain_target_opp_loses_that_much`, `lifegain_each_opp_loses`.

**Flags.** `MTG_DMG_EVENT_VERIFY=1` (diagnostic): the decision-boundary backstop flushes
(`ApplyPlanDirect` entry, `GameEngine::MainPhase` end, pain-sweep entry) ABORT if they find a mark —
i.e. a missing flush site. `MTG_OWN_DEATH_ALL=1` (measurement-only, default OFF): own-death
handling for every deck (NOT the event model) — the basis for the open question on un-gating it.

**Cards (cards.json, bracket notes on each).** Manabarbs, Battlefield Forge, Karplusan Forest,
Tarnished Citadel, Grand Coliseum (Tier 1 painland shape; Coliseum `enters_tapped`), Ancient Tomb,
Tamanoa, Rhox Faithmender, Vito (trigger), Dina (trigger), Bilbo (replacement), Purity (prevention +
existing `graveyard_replace_shuffle_library`). City of Brass's note updated (its "life loss is
inert" claim no longer holds in this deck; its printed TRIGGER is modelled as immediate damage —
disclosed). Costs/P/T/oracle hand-verified against today's Scryfall JSON for all 12 (the live cost
audit 429'd on five of them — see Verification).

**Provider.** `PreventDamageProvider : DeckProvider`, routed FIRST in `DetectDecisionProvider` on
Tamanoa ∨ Manabarbs ∨ Vito ∨ Dina params (Faithmender/Bilbo deliberately excluded — a lifegain
doubler is what a CritterLifegain/Angels list would add). Without it the list rides AntiLifegain
(Living Wish / Beseech `tutor_to_hand`) and, once Dina's outlet lands in I3, would trip `goblin`.
Hooks: `Certificate()` NotAssessed (body names what a proof must bound); `SelfDamageUseful` = a gain
engine (Tamanoa or Purity) is out — **PROVISIONAL judgement hook, measure in Stage 5**; the engine
adds the rules-safety bound (`dmgev::PaymentPainSafe`: even if every pain land still tappable this
payment hurt, the pain alone must not reach 0 before the triggers — evaluated on the reconstructed
start-of-payment state so it cannot flip mid-payment and is safe to key the mana cache on);
`TutorSearchWidth 16` — a COVERAGE fix (Living Wish's 13-name sideboard; base 6 would leave seven
names unreachable), same argument as Pirates' 9 / EDF's 8. Routing test table + a
one-card-cut-survival test added.

**Pain mode + sweep (a GREEDY MANA POLICY — allowed under the greedy-scope ruling; FLAGGED FOR THE
USER).** `DripLandAnyPipColor` and the backtracker's {C}-first hoist read `dmgev::PayWithPain`
(provider hook ∧ payment safety), so a generic pip on a painland/Citadel/Coliseum takes the damaging
mode when useful. `TapPainSourcesIfUseful` runs at end of main 1 beside `TapDripLandsIfUseful` at
its three lockstep sites (`ApplyPlanDirect`, `AIEngine::TakeTurn`, the claude-play branch): it taps
each remaining land one at a time (damaging mode when useful; painless {C} otherwise, for Manabarbs
only), flushing after each, skipping any tap whose pain (or a barb) could kill us before the gains,
and keeping lands when `uses_second_main` and a hand card could still want them. It is an automatic
action in human play too (like the drip sweep) — a VIEWER-SURFACING question for later, not a play
correctness one.

**Tests.** `test/unit/test_prevent_damage.cpp` — 10 cases / 126 assertions: Tamanoa on coloured vs
painless tap (both providers); Faithmender ×2, ×2×2 = ×4, Bilbo+Faithmender = (a+1)·2; Vito drains
the replaced amount, Dina per event; two Manabarbs taps = two events = two Dina drains (and pure
damage without Tamanoa); lethal self-damage with Tamanoa = LOSS, sticky (later gain / later opponent
death is not a win); Purity prevents and gains, Tamanoa does NOT trigger (also for a Manabarbs hit);
Ancient Tomb 2 on its {C}{C} tap; both to 0 = DRAW vs opponent-first = WIN + won-lock; unarmed board
= legacy model; routing + cut-survival. Scenarios (full engine, resumed at main 1, sick creatures so
only the sweep acts): `pd_sweep_tamanoa_faithmender_vito` (22/16), `pd_manabarbs_dina_per_event`
(20/18), `pd_purity_prevents_no_tamanoa` (21/19), `pd_ancient_tomb_two_damage` (22/16),
`pd_bilbo_two_faithmenders` (27/12), `pd_never_pays_itself_dead` (at 1 life with Manabarbs the
sweep taps nothing). The forced-lethal and draw pins are unit tests because the engine (correctly)
never chooses those lines in a playout.

**Verification (2026-09-27, final binary).**
- `./build.sh` clean (no new warnings surfaced); `sizeof(Permanent)` 296 and `sizeof(GameState)`
  832 unchanged (both new fields landed in padding — classified in `Dominance.h`'s log anyway).
- `mtg-test`: 268/268 cases, 2,638,471 assertions pass (incl. the 10 new PD cases and the routing
  table row). `test/scenarios.sh`: 111/111 pass (incl. the 6 new `pd_*`).
- **Smoke byte-identity: 101 passed, 0 failed, 0 new; play-changed=0 (searched and d0)** — run
  twice, the second time after the `kOwnDeath` change (makespan 3m21s). Nothing accepted.
- Deck sanity (real `.cod`, I2/I3 cards dead in hand), 200 games, d3/b20, seed 1000,
  `MTG_DMG_EVENT_VERIFY=1` (no backstop ever found an unflushed mark): 141/200 won by T8, avg win
  turn 6.56 (T5 11, T6 64, T7 42, T8 24), 0 self-deaths. (Before `kOwnDeath`: 140/200 and 2
  self-deaths — the finding that motivated it.) The drain engine visibly works in the logs
  (end-of-main sweeps draining 5-9 via Tamanoa + Vito; Manabarbs taps drained by Dina).
- `provider_audit.py --check` rc=0; Prevent Damage is listed NOT AUDITED (no profile yet — Stage 4);
  its routing is pinned by the unit test instead.
- `audit_viewer_decisions.py --no-sweep`: no unmapped param; the only oracle flag is Dina's
  sacrifice (I3, disclosed).
- Costs: live `audit_card_costs.py` run twice, rc=2 both times (Scryfall 429 on the tail of the list —
  95 NOT COMPARED, none mismatched). Manabarbs and Tamanoa were live-compared OK; Rhox Faithmender,
  Vito, Dina, Bilbo, Purity were rate-limited in both runs and are verified instead by (a) the
  `--update` field snapshot, which fetched all 12 new cards fresh from Scryfall today and whose
  offline diff checks mana_cost as a HARD field, and (b) a by-hand diff against the research
  agent's Scryfall JSON (`logs/prevent_damage/scryfall/`). All 12 match (cost, P/T, oracle prefix).
- `audit_card_fields.py --update` (447-card snapshot; 112 others 429'd and kept their old entries)
  then offline diff: rc=1 on ONE hard mismatch that is NOT ours — `Basri, Tomorrow's Champion`
  keywords local=[] vs scryfall=['exert'] (Basri and Gideon were missing from the snapshot before,
  i.e. that deck's `card_fields` gate already failed closed; the update just made the real gap
  visible). No hard mismatch on any Prevent Damage card; their oracle-similarity advisories are the
  bracket notes.

**Deviations from `core_design.md` (and why).**
- NO `sba_outcome` / `pend_gain_*` / `pend_barbs` GameState fields. Pending triggers live on the
  PERMANENT (`mana_tap_mark`) and loss is encoded by parking our life at `kLostLife`. Reason: every
  one of the ~25 existing snapshot/restore sites already restores the battlefield and the life
  totals, so marks and losses roll back for free; new GameState fields would have needed every
  site taught about them (the design's own top risk: "missed restore site"). Same reason the mana
  cache stores a per-tap mark (`ManaCacheTap::dmg_mark`) instead of aggregate pend deltas — and the
  per-tap form is what keeps Dina's per-EVENT count exact on a cache hit.
- The ~15 `ActivePlayer().life <= 0` plan-loop guards were NOT rewritten to `SelfHasLost`: with the
  parked-life encoding they already mean "we lost", and rewriting them would have folded
  `player_lost_on_draw` into guards on every deck (not byte-identical). `SelfHasLost` is used at the
  new sites.
- No Dominance / BuildDedupKey fold: `life_self` is already a dominance axis (MORE dominates) and
  `BuildSimKey` folds both lives, so a dead line (kLostLife) can never dominate or merge with a live
  one. `AtCleanBoundary` refuses a state carrying a mark.
- ADDED `leafeval::kOwnDeath`: the design's "publish kInvalid for a suicide line" was NOT enough —
  the root ranking falls back to `plan.value` when either side is kInvalid, so a suicide line TIED a
  live no-win line and won on plan.value. Measured: 2 of 200 d3 sanity games cast a third Manabarbs
  into their own death. kOwnDeath is a valid quantity worse than every real one; after the fix,
  0 of 200.
- The tap-ahead pain exclusions (`SpellEffects.h` Ritual/ETB-untap tap-ahead, `TurnSolver`
  CheckLine tap-ahead) and the EDF combo route's costly→{C} rule (`DecisionProviders.cpp` TapLand)
  were NOT flipped: none is reachable in this deck (no rituals, no ETB untappers, no combo route).
  Their taps would still be recorded and resolved at the next backstop, not before the spell. Noted
  on Manabarbs' card text.

**Deferrals / PROVISIONAL (carry to I2/I3 and Stage 6a).**
- TODO I3: Vito's `{3}{B}{B}` team lifelink; Dina's `{1}, sac another: +X/+0`; Bilbo's 111-life
  activation. Each bracket-noted on its card as `[PARTIAL -- TODO PHASE I3, PROVISIONAL]`, NOT as
  inert.
- TODO I2: route Spellshock / Pyrohemia / Rolling Earthquake through `dmgev::DealDamageEvent`
  (`PerformDamageAllCreatures` must pass `tamanoa_lki` and flush after its creature prune so a dead
  Vito does not drain). Generic noncreature burn (`EffectHandler::ResolveDirectDamage` + rollout
  twins) and Crackle/Soulfire self-hits remain unrouted (no card here uses them).
- PROVISIONAL: `SelfDamageUseful` judgement and the end-of-main pain sweep (greedy mana policy) —
  both need a Stage 5 A/B before they are more than "rules-safe and plausible".
- Disclosed: City of Brass's / Purity's printed TRIGGERS modelled as immediate (no observable
  window in a stackless engine); the trigger ORDER (all gains, then each barb) is the weakly
  dominant CR 603.3b order, auto-taken; own-death viewer display shows the parked life
  (-268435456) after a loss; `MTG_DMG_EVENT_VERIFY` would abort on a legitimate human pre-tap that
  is never paid for (diagnostic only — do not run it under the viewer).
- The deck runs today with its I2/I3 cards unimplemented (the loader keeps them as dead cards; no
  temporary list was needed).

### Phase I2 — the deck's spells (DONE 2026-09-27, committed locally in /tmp/pd-wt)

**Scope.** Spellshock, Pyrohemia, Rolling Earthquake, Beseech the Queen, Green Sun's Zenith, the
second-main lever, viewer manifest rows, cards.json entries. Blueprint `spells.md`.

**Files.** `Card.h` (twobrid metadata in ManaCost PADDING -- `sizeof` Card/Permanent/GameState
unchanged, Dominance.h pins held; `PayTwobridWithColor`), `CardDatabase.{h,cpp}` (7 params, `{2/X}`
parse, twobrid colour), `Permanent.h` (`PermAbilityMode::PingAll`, appended last; in the
`PermAbilityTaps` non-tapping list), `SpellEffects.h` (Spellshock armed route in
`FireOnCastTriggers`; `PerformDamageEachCreatureAndPlayer`; `PerformEndStepNoCreatureSacrifice`;
`PingAllSelfSafe`; PingAll in `ApplyPermAbility` / `PermAbilityLabel` / `SpendRepeatActivations` cost
chain + life cap + per-activation guard; `TutorLandCapOk`/`TutorLandCapSlack`;
`ShuffleSelfIntoLibrary`), `SpellEffects.cpp` (`LiveTutorCandidates` -- the resolution-time land cap
for `PerformTutor` and the human chooser), `DamageEvents.h` (`CastTriggerBill`; header note),
`EffectHandler.cpp` (Earthquake resolution; GSZ self-shuffle), `GameEngine.cpp` (end-step sac),
`TurnSolver.{h,cpp}` (`Action::twobrid_colored` + threading via `cast_twobrid_colored`; twobrid
post-pass; Earthquake X branch; GSZ colour conjunct in the Chord branch; PingAll in the three ModeSpec
tables + eval/direct_damage + self-lethal skip; plan-signature `#W`/`#Q`; cost/action FoldMix
(folded only when present); armed subset cast-trigger bill at both sites; rollout Earthquake/GSZ
twins; end-step sac twin; SamePlan verify field), `AIEngine.{h,cpp}` (`m_pending_twobrid`,
consume-once), `DecisionProvider.h` (PingAll K = full 1..max), `DecisionProviders.{h,cpp}` (Generic
X range for the sweeper; `PreventDamageProvider::XCandidates` / `TutorCandidates`, width 20),
`HeuristicArm.h` (`PD_SECOND_MAIN` slot), `GoldFishRunner.cpp` (`DeckUsesSecondMain`), `main.cpp`
(twobrid label / collapse key / JSON), `test/viewer_protocol_check.py` (`twobrid_colored` in
`action_sig`, defaulted), `scripts/audit_viewer_decisions.py` (7 rows), cards.json (5 new; Tamanoa
and Purity notes corrected), `scryfall_reference.json` (+5 snapshot entries), `test/unit/test_prevent_damage_spells.cpp` (new, in CMake), three
`test/scenarios/pd_*.json`.

**Params (all new).** `on_cast_trigger_any_mv` (loader widens `on_cast_trigger_max_mv` to
`CardParams::kOnCastAnyMv`, so every Eidolon reader sees Spellshock), `ping_all_cost`,
`ping_all_amount`, `endstep_sac_if_no_creatures`, `x_damage_each_creature_and_player`,
`tutor_max_mv_is_lands`, `shuffles_self_into_library_on_resolve`.

**Flags.** `MTG_PD_SECOND_MAIN` (DEFAULT ON, heurarm slot `PD_SECOND_MAIN`, `EnvOn(...,true)`):
Pyrohemia / Rolling Earthquake open the searched second main. **PROVISIONAL** -- see the probe below.

**The model, per card.**
- *Spellshock*: on an armed board each trigger is `dmgev::DealDamageEvent` from a noncreature source
  we control (Purity prevents; Tamanoa gains; SBA before the gain). One event per Spellshock per cast.
  The subset enumerators' "a plan that kills us via its own cast triggers" bill was the plain SUM --
  on an armed board it is now exact per event (`CastTriggerBill`: largest single hit with Tamanoa out,
  0 with Purity, the sum otherwise; unarmed = the sum, byte-identical).
- *Pyrohemia / Rolling Earthquake*: `PerformDamageEachCreatureAndPlayer` = Tamanoa count read
  BEFORE the damage (lki) -> shared `PerformDamageAllCreatures` (deaths, detach, OnCreatureDies) ->
  `DealDamageEvent(to_self=amt, to_opp=amt*heads, to_creatures_total=amt*creatures, lki)`. So a
  Vito/Dina/Faithmender killed by the event neither drains nor doubles, a Tamanoa killed by it still
  triggers for the total, our death is checked before the gain, both-to-0 is a DRAW. Pyrohemia K:
  generic `ManaSinkActivationCounts` returns 1..max for PingAll (the cap of 3 no longer applies to
  it); human play folds K to 1 and re-prompts (the mana-sink convention). A self-lethal ping is never
  offered and never applied (`PingAllSelfSafe`: the single hit vs our life; Purity = always safe).
  Earthquake X: a dedicated enumeration branch; GenericProvider returns 0..max for the param (its
  {max} rule was wrong here); `PreventDamageProvider::XCandidates` drops X = 0 unless a damaging cast
  trigger (Spellshock) is out -- found in the first 20-game probe, where the search cast X = 0 on T1
  for nothing (a card + {R} + Citadel pain). Human play / unpruned keep X = 0.
- *End-step sacrifice*: `PerformEndStepNoCreatureSacrifice` from both end-step sites after the
  token trigger; ANY creature (opponent spawns included) keeps it. Early-outs at the first creature.
- *Beseech*: `{2/B}` parsed as generic 2 + twobrid metadata (was `{B}`, MV 3); no other card in
  cards.json has a `{2/X}` pip (checked). Decision space: base (6 generic) + k = 1..3 coloured variants
  (`#W<k>` in the signature). Rollout leaf: the base is REPLACED by the cheapest variant the pool can
  pay. Tutor: `tutor_types []` = any card (GenericProvider already read empty as "no restriction";
  the AntiLifegain heuristic path in `SpellEffects.h::TutorCandidates` still reads empty as "no
  match" but is not on this deck's route -- left untouched, byte-identity). Land cap exact at
  resolution (`LiveTutorCandidates`, and `PerformTutor` clears an illegal baked target and re-picks);
  enumeration gives +1 slack when the land drop is open and a land is in hand. Provider orders
  nonlands first and `TutorSearchWidth` 16 -> 20 so all 19 distinct names are reachable.
- *GSZ*: the Chord X branch now applies `tutor_color` (empty for Chord -> byte-identical);
  `ShuffleSelfIntoLibrary` from `EffectHandler::MoveToGraveyard` and apply_one (second shuffle, keyed
  on the next search ordinal in both worlds).

**Tests.** `test_prevent_damage_spells.cpp` -- 13 cases: Spellshock + Tamanoa (+Vito) gain, two
Spellshocks = two events, lethal at 2 life, `CastTriggerBill`; Pyrohemia one event per activation
(Dina drains per activation; the 3rd ping kills Dina before its gain -> no drain), self-lethal ping
never applied, K = full range, `SpendRepeatActivations` pays {R} each, end-step sac (fires on an
empty board, an opponent creature keeps it); Earthquake X=3 kills Vito before the gain (no drain),
X=4 kills Tamanoa which still triggers (x Faithmender), lethal to both = DRAW, X range + X=0 rule +
enumerator emits X 1..3 on four lands; Beseech MV 6 / black / all generic, payable {B}{B}{B},
{2}{B}{B}, six generic, the enumerator emits k = 0..3, land cap (+1 slack, resolution re-pick);
GSZ offers Dina at X=2, never Vito, and goes back into the library. Scenarios:
`pd_pyrohemia_dina_per_activation` (26/16), `pd_earthquake_x_is_searched` (26/10 -- the search picks
X=2 over the max X=3 that would kill Vito before the drain), `pd_beseech_paid_bbb` (20/18 on exactly
three B sources).

**Verification (final binary).**
- `./build.sh` clean (no warnings). `mtg-test` 281/281 cases, 2,638,713 assertions. `scenarios.sh`
  114/114.
- Smoke: 101 passed, 0 failed, 0 new, play-changed = 0 (searched and d0), run twice (the second on
  the final binary). Nothing accepted.
- Deck sanity (real `.cod`, I3 cards still partial), 200 games d3/b20 seed 1000,
  `MTG_DMG_EVENT_VERIFY=1`: **194/200 won, avg win turn 5.87** (T4 5, T5 67, T6 82, T7 29, T8 11;
  loss-penalised 5.96) vs I1's 141/200 / 6.56. **0 self-deaths** (min life > 0 in every game), no
  aborts. Casts: Earthquake 186 (X: 0x8 -- all with Spellshock out -- 1x78, 2x76, 3x12, 4-6x12),
  Living Wish 147, Vito 136, GSZ 134, Tamanoa 131, Beseech 81, Dina 76, Spellshock 76, Pyrohemia 20
  (54 pings). Logs read: s1036 Beseech {B}{B}{B} -> GSZ, GSZ X=3 Tamanoa, then T7 two pings with Vito
  out (K=2, not 3 -- the third would kill Vito first) take the opponent 18 -> 2; s1013 Beseech for
  Living Wish, Earthquake X=1 with Tamanoa + Dina; s1002 GSZ X=2 Dina under Spellshock + Vito
  (16 -> 3 in one main). 20 SLOW-GAMEs (30-101 s) -- see the m2 probe.
- **m2 probe (my own experiment, one pooled `--batch`, 200 games each, d3/b20, s3000):**
  ON avg 5.760 / 2,299 s CPU; OFF avg 5.735 / 1,317 s CPU -> the searched second main costs **1.75x**
  for **no measurable gain** (+0.025, inside the noise at n=200). Default left ON (the 2c-bis
  mandate + the task's documented default); **PROVISIONAL -- Stage 5 must A/B it paired on held-out
  seeds and decide** (heurarm slot, so both arms pool into one batch).
- Costs: targeted `audit_card_costs.py` on the five new cards: all 5 compared, all match. Full run:
  248 compared all match, 96 NOT COMPARED (Scryfall 429), rc=2 -- none of the five among them.
  Hand-checked against `logs/prevent_damage/scryfall/*.json` too (cost, type, oracle verbatim).
- `audit_viewer_decisions.py --no-sweep`: no unmapped param; the only oracle flag is Dina's sacrifice
  (I3).
- `test/viewer_protocol_check.py --strict --threads 20` (the regression-mode reference gate, run
  because `main.cpp`'s plan labels / collapse key and the checker's `action_sig` changed): 345 refs,
  0 play-drift, 0 enum-gap, 0 contract-fail (30 ok, 304 repaired, 10 mull-drift). The ONE
  board-diverged ref (`Snow/claude_s4_gi3`, not gating) reproduces identically on the I1 commit's
  binary -- pre-existing, not ours.
- `audit_card_fields.py --update` (full: 120 cards 429'd and kept their entries; Spellshock /
  Pyrohemia / Rolling Earthquake among them, so re-fetched with `--update --cards <the five>` after a
  pause -- all five now in the snapshot, additions only) then offline diff: 452 checked, rc=1 on the
  ONE pre-existing hard mismatch (Basri, Tomorrow's Champion `exert`, not ours), nothing unfetched;
  the five new cards' oracle advisories are their bracket notes.

**Deviations from `spells.md`.**
- No `twobrid_colored` positional parameter on `apply_one` / `CastSpellFromHand`: threaded as a
  consume-once side channel (`cast_twobrid_colored` / `m_pending_twobrid`, the devour idiom), set at
  every call site that sets the devour count (7 rollout, 12 executor), so no 20-argument call changed.
- Twobrid fields live in ManaCost padding (a straight append grew Card and tripped the Dominance.h
  `sizeof(Permanent)` pin).
- `PingAllSelfSafe` also guards the FIRST activation inside `ApplyPermAbility` (both worlds), not just
  the enumeration and the repeat loop.
- Pyrohemia's K has NO provider narrowing yet (the generic full range is live) and Earthquake's X
  only the X=0 rule -- both are Stage 5f candidates if the cost matters.

**PROVISIONAL / flagged for Stage 5 and 6a.**
- `MTG_PD_SECOND_MAIN` default ON (1.75x for +0.025 in the probe).
- `TutorSearchWidth` 20 and the nonland-first Beseech/Living Wish ordering (a coverage bound; a
  real ranking is a 5e/5f question). Living Wish's base target changes with it (nonlands first).
- Activations trail casts within one main (`apply_trailing_activations`), so "ping, THEN cast the
  creature" is only expressible across the two mains -- one more reason the m2 lever matters.
- The search casts Rolling Earthquake as plain burn with no Tamanoa out (45 of 186 casts) -- a hold
  vs burn judgement for the cast-order/provider review, not a rules issue.
- Disclosed: horsemanship inert; Pyrohemia's instant-speed / end-step window collapsed to our mains;
  a hand Beseech read by keep-model / land-play heuristics is priced at MV 6 (pessimistic only).
- The Basri, Tomorrow's Champion `exert` keyword mismatch in `audit_card_fields.py` is pre-existing
  and not ours.

### Phase I3 — the creatures' abilities + the sideboard (DONE 2026-09-27, committed locally)

**Scope.** Vito's team lifelink, Dina's sac-pump, Bilbo's 111-life activation (built -- the recorded
user decision), Shriekmaw, Acidic Slime, Timeless Witness, Dimir House Guard, Purity route check,
legend rule, viewer surfacing, keyword tags. Blueprint `creatures_sideboard.md`.

**Params (all new, all gated).** `team_pump_grants_lifelink` (Vito, riding `team_pump_cost`/
`team_pump_power 0` -- ActivatePump mode 2, the Valiant Knight shape, NOT a new PermAbilityMode:
see CardDatabase.h's note on why the mode enum is the fragile route), `sac_outlet_self_pump_power_from_victim`
(Dina, on top of `sac_creature_outlet`/`sac_creature_cost {1}`/`sac_outlet_excludes_self`),
`etb_destroy_nonartifact_nonblack` (Shriekmaw; + existing `evoke_cost`),
`etb_destroy_artifact_enchantment_land` (Acidic Slime), `etb_return_gy_to_hand` + `eternalize_cost`
(Timeless Witness), `transmute_cost` (Dimir House Guard), `life_gated_put_creatures_cost` +
`activate_min_life` (Bilbo). Non-JSON: `CardParams::tutor_exact_mv` (Transmute's synthesized search).
Keywords `Fear`, `Transmute`, `Eternalize` added to the enum as INERT tags (appended; Scryfall-faithful).

**Files.** `CardDatabase.{h,cpp}`, `Card.h` (3 keywords; the Regenerate note corrected -- our own
sweepers CAN destroy our creatures now), `Permanent.h` (`PermAbilityMode::LifeGatedPutCreatures`,
appended, a {T} mode), `SubtypeRegistry.h` (`Zombie` runtime literal), `SpellEffects.h`
(`PdStats` firing counters; `EtbDestroyTargetLegal`/`ResolveEtbDestroyMandatory`/
`ResolveEtbReturnGyToHand` + their `FireOwnEtbTriggers` calls; Dina LKI pump in
`ApplySacCreatureOutlet`; `ChooseSacOutletVictimIndex` honours `excludes_self`; `CanonicalSacVictim`
self-guard; Vito in `ApplyActivatePump` mode 2; `TransmuteSearchParams`/`ApplyTransmute`;
`ApplyEternalize`; Bilbo in `PermAbilitySourceLive` (life gate) / `PermAbilityLabel` /
`ApplyPermAbility`; `PerformTutorToBattlefield(..., ask_human_despite_pin)`; `tutor_exact_mv` in
`TutorNumericFilterOk`), `TurnSolver.{h,cpp}` (`Action::Kind::Eternalize` appended + `LineSpec::
eternalizes`; enumeration: Vito beneficiary gate, Dina per-victim fan, Shriekmaw evoke gate,
Transmute (Channel kind) fan, Eternalize, Witness per-gy-name post-pass, Bilbo in the three mode
tables + `BpActivationAbilityUnambiguous`; signatures `#G`, `CHANNEL#..>target`, `ETERNALIZE#`;
rollout apply; `IsTrailingActivation`; CheckLine `eternalize=`), `AIEngine.cpp` (executor twins,
labels), `DecisionProvider.h` (`EtbDestroyTargetPick`, `GyReturnToHandPick`,
`PutCreaturesFromLibraryPicks` -- deliberate default rankings), `DecisionProviders.{h,cpp}`
(`PreventDamageProvider::FodderSacUseful` lethal gate, `EtbDestroyTargetPick` engine-last ranking),
`GoldFishRunner.cpp` (`DeckGraveyardReaders` scans the SIDEBOARD for the Witness's two params only),
`main.cpp` (labels, `transmute`/`eternalize` JSON, `eternalize_gy` state list, `eternalize=` verb),
`tools/play/{index.html,linebuild.js,DECISIONS.md}`, `scripts/audit_viewer_decisions.py`, cards.json
(4 new + Vito/Dina/Bilbo/Purity notes), `scryfall_reference.json` (+4 snapshot entries),
`test/unit/test_prevent_damage_creatures.cpp` (new, in CMake), two `test/scenarios/pd_*.json`.

**The model, per card.**
- *Vito*: temp_lifelink on every creature we control AT RESOLUTION (CR 611.2c), K capped at 1; offered
  only while an own attack-eligible creature lacks lifelink (lossless; keeps a no-op out of the human
  menu). Each lifelink attacker = its own `GainLife` event in `ResolveCombatDamage` (existing, per the
  Vito/Dina/Bilbo rulings) -> Faithmender/Bilbo replacement -> Vito "that much" + Dina 1. Combat
  damage by a creature, so Tamanoa never triggers on it.
- *Dina*: X = victim's LAST-KNOWN power (effective + lord bonus, floored 0 -- the Flinger expression),
  read before both erases; the victim dies through `OnCreatureDies` (Purity shuffles). SEARCHED victim:
  one variant per distinct (name, power, token) -- the canonical weakest-body pick would minimise X;
  repeatable within a turn. Human: every variant + the `sacrifice` board-pick (the source now excluded).
- *Shriekmaw*: mandatory target over both sides (CR 603.3d): an opponent spawn (colourless) when
  present, else one of OUR nonblack creatures, else removed. Evoke {1}{B}: the Reveillark mechanism,
  gate widened for ETB-destroy cards (Reveillark untouched), always offered (a Spellshock trigger).
- *Acidic Slime*: always destroys one of OURS (the goldfish has no artifact/enchantment/land).
- Target pick for both = `EtbDestroyTargetPick` (PD: opponent first; never Tamanoa/Faithmender/Purity/
  Bilbo/Vito/Dina/Manabarbs/Spellshock/Pyrohemia while anything else is legal; then tapped land, lowest
  MV, oldest). A RESOLUTION pick, NOT searched -- PROVISIONAL; human: full legal set via the
  loyalty-chooser `target` shape (not prompted when forced).
- *Timeless Witness*: gy return searched on the cast path (one variant per distinct gy name + the
  unpinned base = provider pick on the live gy, the only route to a card that hits the gy earlier in the
  same plan); human play gets the dig chooser at resolution instead. Eternalize {5}{G}{G}: a new
  graveyard activation kind; exile, then a token copy from the definition with black-only / 4/4 / no
  mana cost / +Zombie written on the token's own Card; its ETB fires again (unpinned).
- *Dimir House Guard*: Transmute rides the Channel kind (from-hand, discard as cost) -> PerformTutor
  over synthesized params (to hand, shuffle, exact MV 4 -- Manabarbs / Faithmender / Pyrohemia here);
  one variant per legal name (autonomous), one target-less action + the tutor chooser (human).
  Trailing activation, so the found card is castable next main. Regeneration: PROVISIONAL deferral
  (open question 2; bracket-noted with the reasoning).
- *Bilbo*: `LifeGatedPutCreatures` -- {T} (sickness applies), life >= 111 checked when activating
  (before costs, CR 602.5b) by `PermAbilitySourceLive`, shared by enumeration and both applies; exile
  Bilbo (cost), search, put, shuffle exactly once even if nothing is put, then the legend rule. WHICH
  creatures ("any number") = `PutCreaturesFromLibraryPicks`: all creature cards EXCEPT a second copy of
  a legend we control/already put and a creature whose mandatory ETB would destroy our own permanent
  (Acidic Slime; Shriekmaw with no opponent creature). Human: the `dragon` multi-pick, provider pick
  preselected. `MTG_PD_STATS=1` prints firing counters (bilbo_activation etc.) at exit.
- *Purity*: every death route verified to reach the shuffle (sweepers, Dina sac, Shriekmaw destroy via
  `OnCreatureDies`; cleanup discard via `MaybeReplaceGraveyardWithLibraryShuffle`); unit-tested for
  the Dina and Shriekmaw routes.
- *Legend rule* (2x Vito / 2x Dina / Bilbo): existing `EnforceLegendRule` keeps the OLDEST; there is
  still NO viewer legend-keep decision -- disclosed.

**Provider judgement found by measurement (PROVISIONAL, `MTG_PD_DINA_LETHAL_GATE`, default ON).**
The first I3 sanity run (attack-only gate) went 193/200, avg-win 5.891, loss-penalised 6.00 (vs I2
194 / 5.866 / 5.96), 27 outcome changes (8 faster / 18 slower / 1 lost) and +24% CPU. Root cause: the
search sacrificed **Tamanoa 28x and Rhox Faithmender 8x** for a one-turn Dina pump; the 35 games with
a Dina sac were 8 turns slower in total than the same seeds in I2 (the leaf prices the pump's damage
now, not the gain engine's damage later -- a valuation horizon, confirmed not budget: the lost-lethal
game 6 is identical at 5x budget). Fix: the MeliraPod "lethal exception" shape -- offer the sac only
when an optimistic bound on this turn's damage (ready power + the mana-limited largest pumps, x the
Vito/Faithmender drain multiplier, + Dina per attacker) reaches the opponent's life. After it: every
inferred Dina sac lands on its game's winning turn.

**Tests.** `test_prevent_damage_creatures.cpp` -- 11 cases: Vito grant (own only, incl. Vito) +
per-attacker drains (opp 20 -> 9); grant offered once, not when no attacker lacks lifelink; Dina +6
off Purity (LKI) and Purity back in the library (one shuffle); per-victim fan, never herself, gated by
sickness and by the lethal gate; Shriekmaw: spawn first / Shusher over Tamanoa / Purity shuffles /
all-black board = nothing; evoke variant {1}{B}; Slime: tapped land over Manabarbs, Manabarbs when
sole; Witness: pinned / provider / empty gy / per-name fan; eternalize (exiled, token 4/4 black-only
MV0 Zombie Shaman, re-triggers, not a Shriekmaw target); Transmute (MV-4 only, found to hand, self to
gy, one shuffle, enumerator fan); Bilbo (gate 110/111, exile, 2 Tamanoa + 1 Dina, no 2nd Vito, no
Slime, one shuffle). Scenarios: `pd_vito_team_lifelink` (24/9), `pd_dina_sac_purity_pump` (opp at 7:
sac Purity, 7/3 Dina wins T4).

**Verification (final binary).**
- `./build.sh` clean. `mtg-test` 292/292 cases, 2,639,203 assertions. `scenarios.sh` 116/116.
- Coverage (`analyze_deck.py --coverage-only`): 0 missing; all 26 names `full`, main AND side (13 SB
  names reachable via Living Wish). Bracket notes only for disclosed inert/deferred clauses (list in
  6a below).
- Smoke: 101 passed, 0 failed, 0 new, play-changed = 0 (searched and d0), run twice (the second on the
  final binary, after the lethal gate). Nothing accepted.
- `viewer_protocol_check.py --strict --threads 20`: 345 refs, 30 ok / 304 repaired / 0 play-drift /
  0 enum-gap / 10 mull-drift / 0 contract-fail; the ONE board-diverged ref (`Snow/claude_s4_gi3`) is
  the pre-existing one I2 recorded.
- `audit_viewer_decisions.py --no-sweep`: main zone clean; with the new opt-in `--sideboard` (the .cod
  side zone -- this is a wish deck): no unmapped param; one advisory, Dimir House Guard's regeneration
  "Sacrifice a creature" (its PROVISIONAL deferral note).
- Deck sanity, 200 games d3/b20 s1000, `MTG_DMG_EVENT_VERIFY=1` (no abort), final binary:
  **194/200 won, avg-win 5.856, loss-penalised 5.95** (I2: 194 / 5.866 / 5.96); 6 outcome changes
  (4 faster: 46, 182, 183, 185 -- 182/185 via the Vito grant; 2 slower: 6 -- a T5 Earthquake X=0 drain
  spent the T6 lethal Earthquake, same at 5x budget, a valuation call; 66 -- a T3 Spellshock-vs-Vito
  divergence). CPU 19m39s user vs I2 17m18s (+14%). Deterministic (a rerun reproduces the unwon list and
  per-game units). Real firings: Dina pump 34 (every one on a winning turn), Vito grant 4 (all on
  winning turns); Shriekmaw / Slime / Witness / eternalize / transmute / Bilbo 0 in real play (each
  thousands of times in rollouts; Bilbo 18 rollout firings).
- **Living Wish fetches** (I3): Rhox Faithmender 46, Vito 35, Tamanoa 35, Brushland 20, Vexing Shusher 3,
  Purity 3, Dina 2, Battlefield Forge 2 -- sensible (the engine pieces; a land when short); none of the
  new SB cards is ever wished for, which matches their value here (Slime is net-negative, Shriekmaw
  kills a body the goldfish never uses, Witness / House Guard are slow).
- Costs: targeted `audit_card_costs.py` on the 8 new/edited cards: all 8 compared, all match.
  `audit_card_fields.py --update` (the 8 cards) + offline diff: 456 checked, rc=1 on the ONE
  pre-existing hard mismatch (Basri, Tomorrow's Champion `exert`, not ours); the new cards' P/T,
  types and keywords match; their oracle advisories are the bracket notes.

**Deviations from `creatures_sideboard.md`.**
- Vito rides ActivatePump mode 2 (`team_pump_grants_lifelink`), not a new `GrantLifelinkTeam`
  PermAbilityMode -- the Valiant Knight precedent, and the repo's own note that the mode enum's
  per-mode tables fail invisibly.
- Transmute rides the Channel kind/verb rather than a new verb (same from-hand shape); Eternalize got
  its own kind + verb + graveyard click (no existing shape activates from the graveyard without a
  board source).
- Witness/Transmute human play defers the pick to a resolution chooser instead of named plan variants
  (the `HumanPlayDefersTutorTarget` convention -- no CheckLine sub needed).
- Bilbo's "any number" is a provider pick + human multi-pick, not searched (disclosed).

**PROVISIONAL / flagged for Stage 5 and 6a.**
- `MTG_PD_DINA_LETHAL_GATE` (default ON) -- the measured Dina fix; A/B it paired in Stage 5.
- `EtbDestroyTargetPick` (Shriekmaw/Slime) and `GyReturnToHandPick` / `PutCreaturesFromLibraryPicks`
  are reviewed rankings, not searched and not measured (the cards never fire in real play at n=200).
- Dimir House Guard regeneration: PROVISIONAL deferral (open question 2).
- Inert (goldfish never blocks/casts): Fear (House Guard, Shriekmaw), Deathtouch (Slime), Flying
  (Purity); "can't be countered" (Shusher, pre-existing).
- The executor's game log does not record sac-outlet activations (pre-existing, every outlet deck);
  the Dina counts above are inferred from board diffs + `MTG_PD_STATS`.
- Legend rule: no viewer keep decision (keeps the oldest).
- `audit_viewer_decisions.py` gained `--sideboard` (opt-in so no other deck moves); `verify_deck`'s
  viewer gate still audits the main zone only -- Stage 5 should run it with `--sideboard` for this deck.

## Open questions / provisional decisions (surfaced to user, not blocking)

1. Own-death mid-turn as a GLOBAL rules fix? Default taken: GATED to armed decks; measure with
   `MTG_OWN_DEATH_ALL=1` over all tiers before deciding.
2. Dimir House Guard regeneration: PROVISIONAL deferral (shield must be paid before the sweeper
   resolves, sacrificing Tamanoa/Faithmender forfeits the very trigger the sweep is for). Bracket-noted
   on the card in I3; still awaiting sign-off.
3. Bilbo's 111-life activation: BUILD it (not deferred).
4. End-of-main voluntary pain sweep (`TapPainSourcesIfUseful`) is a greedy mana policy — within
   the greedy-scope ruling, flagged for the user.
5. Inert goldfish keywords (Fear, Flying, Deathtouch) — disclose; opponent never blocks.
6. Pyrohemia / Rolling Earthquake open the searched second main (`MTG_PD_SECOND_MAIN`, default ON).
   The I2 probe measured 1.75x CPU for +0.025 avg (noise) at n=200 -- default kept ON per 2c-bis;
   Stage 5 decides on a paired held-out A/B. PROVISIONAL.
7. Beseech / Living Wish tutor axis width 20 + nonland-first ordering. PROVISIONAL (coverage, not a
   measured ranking).
8. Dina's sac-pump is offered to the autonomous search only when an optimistic bound says it can
   close the game THIS turn (`MTG_PD_DINA_LETHAL_GATE`, default ON; the MeliraPod precedent). Without
   it the search fed Tamanoa/Faithmender to Dina (measured, Phase I3). PROVISIONAL -- Stage 5 A/B.
9. Shriekmaw / Acidic Slime own-side victim, Timeless Witness unpinned return, Bilbo's "any number"
   put: provider rankings (not searched), reviewed not measured -- none fires in real play at n=200.
   PROVISIONAL.
