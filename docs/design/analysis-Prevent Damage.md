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
- **STAGE 5 DONE (see "Stage 5 — verification" below; fixes `6d3f212c`, script fix `09a12ad2`).**
  The deck FAILS the 5j 3x cost rule (~4.5x over) -> performance is the first goal before VL /
  mulligan. Remaining: 5d claude-play fan-out, §5i discard buckets, suite rows + GT (after cost).
- (historical) NEXT: Stage 3 coverage loop (already clean at I3 -- re-run to confirm) -> Stage 4 profile -> 4a provider audit -> Stage 5 (verify_deck,
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

## Stage 5 — verification (2026-09-27, Opus verifier; worktree `/tmp/pd-wt`)

Commits: **`6d3f212c`** (the Stage 5 fixes below). Every run pooled into ONE `mtg --batch` per
process-wide env setting, seeds disjoint from the suite (suite uses 1001 / 2002,3003 / 4004-7007 x
<=1000 games; Stage 5 used 12001, 13001, 14001, 20001, 21001, 1,000,000+). Scratch under
`logs/prevent_damage/stage5/` (gitignored).

### 1. `verify_deck.py` (before the fixes, HEAD ece1abbe)
| gate | result |
|---|---|
| coverage | PASS — 26 cards full (main + side) |
| card_costs | FAIL (did-not-run, not a mismatch) — 95 costs unverified, Scryfall 429. **None of the 26 Prevent Damage cards is among them** (all compared, all match). Re-run when the rate limit clears. |
| card_fields | FAIL — the ONE pre-existing `Basri, Tomorrow's Champion` `exert` keyword mismatch (not ours, recorded since I1) |
| clause_ledger | SKIP (by design) |
| regression_tiers / suite | FAIL — expected: the deck is not in the suite yet (a later step adds it; not added here) |
| viewer | PASS; viewer_wiring PASS (`sacrifice`) |
| mismatch | PASS — 0 nonconv / fd-diverge, seeds 7001/7002 x 60 |
| play_invariants | **FAIL, 8 violations -> FIXED (harness gaps, not engine bugs)** — see Fixes (c). Re-run: 8 games / 176 decisions, all hard invariants hold. |
| claude_sweep | SKIP — 5d not run by this verifier (it is a separate fan-out step) |
| discard_policy | **gate does not exist in this tree** (it lives in the main checkout's uncommitted WIP); the §5i bucket policy is a later step |

`audit_viewer_decisions.py ... --sideboard` (5h): **PASS**. Expected types dig / sacrifice / target;
Acidic Slime + Shriekmaw `target`, Timeless Witness `dig`, Dimir House Guard transmute all VERIFIED by
targeted seed-search. One advisory: House Guard's regeneration "Sacrifice a creature" (its disclosed
PROVISIONAL deferral). Bilbo's put multi-pick was not exercised (0 real activations anywhere).

### 2. 5a mismatch harnesses — PASS (zero lines, before AND after the fixes)
- `MTG_FLAG_NONCONV`, d3 b20, 2 x 250 games (s12001, s13001): **0 `[nonconv]`** (ece1abbe) and 0 again
  on `6d3f212c`. avg 5.664 / 5.684 -> 5.668 / 5.680; 487/500 won.
- `MTG_FULL_DEPTH + MTG_FD_ORACLE`, d5 b20, same seeds: **0 `[fd-diverge]`** before and after.
- `MTG_PD_STATS` on the post-fix runs: **0 real own deaths in 1,000 games.**
- Oddity recorded (not a defect): at s12001 the d3/b20 run and the d5/b20 full-depth run are
  DIGEST-IDENTICAL (same per-game units on the slow games too) — the extra depth changes nothing on
  that block; d3/b10 vs d5/b20 differ on only 7/300 games at s14001 (5b).

### 3. 5b depth sweep (s14001, one pooled batch, suite-like budgets)
| cell | games | won | loss-pen. avg | per-game (batch ms) |
|---|---|---|---|---|
| d0 | 1000 | 317 (31.7%) | 8.300 | 0.4 ms |
| d3 b10 | 300 | 290 | 5.867 | 8,929 ms |
| d5 b20 | 300 | 291 | 5.847 | 14,031 ms |

Monotonic (d0 << d3 <= d5). Clock plausible: d5 T4 7, T5 116, T6 120, T7 39, T8 9, unwon 9 — the
intended kill (engine online T3-4, drain T5-6) is the mode. Per game d3->d5: 6 faster, 1 slower
(gi142 T5->T6, a budget line-shift: identical at b200/b1000). **d0 is very weak (68% unwon)**: the
greedy rollout plays this deck badly (see Open items: 5i rollout-quality digest is warranted).

Every d5 unwon game read (logs `stage5/unwon/g*`), **no own deaths** (life > 0 in all nine):
- g3, g266 — kept two Reflecting Pools (a lone/only-Pool hand makes NO mana); g283 — Pool + Ancient
  Tomb (no coloured source). Keep-quality with the baseline profile; the mulligan stage's job.
- g11 — Tomb x2 + Forge, no G/B source: Earthquake the only castable card.
- g185, g262 — two-land stall T2-T6; the search chips with Rolling Earthquake X=1 while Tamanoa sits
  uncastable (the damage-race tie-break prefers burn now; see 5c2).
- g252, g289 — flood / no drain piece; symmetric self-burn (Earthquake, a second Manabarbs with no
  gain engine) to 1-3 life, then locked. Hopeless draws.
- **g196 — a MISPLAY, fixed:** T5 cast a SECOND Vito ({2}{B} + Spellshock 2 + Citadel 3 + Tomb 2 =
  7 life) into the legend rule, 8 -> 1 life, then could never cast again. Traced: pass and the
  duplicate tied on the graded no-win leaf (tb 15,000,000 both) and `plan.value` preferred casting;
  the greedy rollout itself casts the duplicate, so pricing our life at the leaf could not see it
  either (MTG_PD_LEAF_OWN_LIFE made no difference). Fix (b) below.

### 4. 5c budget starvation (19 slow/unwon d5 games x b20 / b200 / b1000, one batch)
b20 reproduces every original result. Of the nine T8 wins, four speed up with 10-50x budget (g167
8->7, g202 8->7, g234 8->7->6, g149 8->7 at b200 but back to 8 at b1000 — non-monotone churn); the
other five are unchanged. **All ten unwon games stay unwon at b1000** — they are mana screw / stall,
not starvation. Threshold: mild starvation on ~4/300 games, recovered by ~b200. Not a logic bug.

### 5. The PROVISIONAL levers — one pooled paired A/B (d5 b20 = play settings, s20001 + s21001 x 300)
arm minus base, per game, loss-penalised (negative = the arm is faster):

| lever (arm) | delta | t | arm slower / faster | CPU vs base | decision |
|---|---|---|---|---|---|
| `MTG_PD_DINA_LETHAL_GATE=0` | **+0.0400** | +2.85 | 41 / 17 | 1.06x | **KEEP ON** — the gate is confirmed on held-out seeds |
| `MTG_PD_SECOND_MAIN=0` | -0.0033 | -0.23 | 35 / 34 | **0.63x** | quality-neutral (as I2's n=200 probe); m2 costs **1.6x for nothing measurable**. Default left ON per 2c-bis — **PROVISIONAL, recommend OFF (user call)**; it is the first cost lever for the 3x gate |
| `MTG_PD_LEAF_OWN_LIFE=1` (new) | -0.0033 | -0.58 | 4 / 5 | 1.01x | neutral -> stays OFF (no sign; the rollout, not the leaf, was the problem) |
| `MTG_PD_DUP_LEGEND=0` (new, 2nd batch) | +0.0017 | +0.45 | 3 / 2 | 1.00x | keep ON (dominated-cast prune; fixes the g196 class) |
| `MTG_PD_SELF_LETHAL_GUARD=0` (new, 2nd batch) | +0.0050 | +0.83 | 4 / 3 | 1.00x | keep ON — **real own deaths 2 -> 0** |

Base after the fixes: 579/600 won, loss-penalised 5.810. The second batch's guard-OFF arm reproduced
the first batch's base digests exactly (the control that must match: nothing else moved).

### 6. Fixes (commit `6d3f212c`; rules skill consulted for the SBA-before-trigger ordering)
(a) **Suicide guard** (`MTG_PD_SELF_LETHAL_GUARD`, default ON, heurarm slot). s20001 gi72 (found as a
    regression of the dup-legend arm): T4 Manabarbs at 8 life -> 1, then at 1 life under Manabarbs
    EVERY cast is a suicide (the first barb resolves, SBA, before Tamanoa's gain — the card's own
    ruling). The greedy rollout took one each turn, so every leaf including `<pass>` read
    `kOwnDeath`, the tie-break went blind, and `plan.value` committed a REAL suicide (Rolling
    Earthquake X=5 at 1 life, T5). New `dmgev::FirstLandTapKills` (lower bound: 1-point first barb
    with a Tamanoa, all barbs without; Purity prevents; any untapped non-land mana source voids it;
    floating mana netted by the caller) drops such CAST plans at both subset enumerators through the
    new `DecisionProvider::GuardsSelfLethalPayment` hook (default false: every other deck
    byte-identical). `PreventDamageProvider::XCandidates` drops an Earthquake X >= our life unless
    Purity (the `PingAllSelfSafe` twin: X >= life is a loss, or a DRAW when it also kills the
    opponent — never a win). Not covered (disclosed): a Pyrohemia activation whose {R} land tap
    barbs us to death at life 2+ (the ping guard checks only the ping); pain from the tapped land
    itself; gains earlier in the same plan (not credited to the X cap).
(b) **Duplicate legend** (`MTG_PD_DUP_LEGEND`, default ON). Vito and Dina are `custom`, so the generic
    enter-inert whitelist never pruned a second copy. `PreventDamageProvider::OfferDuplicateLegendCast`
    offers it only when the CAST can pay — a damaging cast trigger (Spellshock) AND a gain engine
    (Tamanoa/Purity), on board or in hand and affordable together with the duplicate — or when the
    base entry/death-upside helper says so. (First draft counted an unaffordable Tamanoa in hand and
    re-opened the same misplay on T6; tightened by a mana bound.)
(c) **play_invariants false positives.** `tutor_etb` (every tutor cast since 2026-09-10) was not a
    known type; and the viewer's display collapse (`hide_bundle`: tutor/wish cast fan, pod fan,
    sac loops) legitimately leaves GAPS in real engine indices, which the checker read as
    "non-contiguous". `main.cpp` now emits `"plans_hidden": N` (capped mode only, only when N > 0;
    the uncapped reference checker never sees it) and the checker requires strictly increasing
    indices bounded by emitted + hidden. Any deck with Living Wish / pod / Chord would have tripped it.
(d) Levers/diagnostics: `PD_DINA_LETHAL_GATE` is now a heurarm slot (was a static env read, so it
    could not pool); `MTG_PD_LEAF_OWN_LIFE` (default OFF); `MTG_PD_STATS` prints `[pd-own-death]` per
    REAL own death (bottoming trial playouts also run through GameEngine — excluded via
    `g_real_resolution`) with the job's PD lever overrides; the solve trace prints the graded leaf
    `tb=`.
Tests: 4 new unit cases (X cap + Purity, FirstLandTapKills incl. an OPPONENT's Manabarbs and an
unarmed board, the enumerator offers no cast at 1 life under Manabarbs, the duplicate-Vito rule).
mtg-test 296/296; scenarios 117/117; **smoke 101/101 byte-identical, play-changed 0**;
`viewer_protocol_check --strict` unchanged (30 ok / 304 repaired / 0 play-drift / 0 enum-gap /
0 contract-fail; the one board-diverged ref is the pre-existing Snow one).

### 7. 5c2 horizon-honest tie-break — KEEP THE DEFAULT (ON)
`leaf_tiebreak_check.py --blocks 4 --games 500` at PLAY settings (d5/b20, the built-in default; no
value_play yet), on `6d3f212c`: **123 changed of 2,000 paired (6.15% binding), net -16 turns
(-0.008/game), 55 worse / 68 better** — half A -17, half B +1 (the halves disagree in size, not in a
way that would flip the verdict). The script first printed "0 changed of 0 paired — NO SIGN": its
`[win]` parser (`job=(\S+)`) could not match a deck stem containing a SPACE, so it compared nothing.
Fixed in **`09a12ad2`** (parse `job=(.+?) gi=`; a zero-paired comparison is now an ERROR, never NO
SIGN) and the same output re-parsed — no re-run needed. **Consequence for other decks:** every stem
with a space was exposed (Creature Giving, Melira Pod, Mirrorwing Dragon, Unpredictable Cyclone);
Melira Pod's ledger records "NO SIGN at 1,200 paired (0 changed)", which is almost certainly this bug
— its 5c2 is void and should be re-run. The stored-value failure mode the task warned about is
visible in individual games (g185/g262 chip with Earthquake X=1 before Tamanoa; g252/g289 burn their
own life) but the aggregate still favours the tie-break at this sample.

### 8. 5j suite cost — **FAIL the 3x rule by ~4.5x** (report only; no rows added)
`suite_gate.py --cost` has nothing to read (no suite cases), so the number is measured directly with
the same metric (`batch ms / games`, max over searched cells) from the 5b pooled batch (s14001):
**d3 b10 = 8,929 ms/game, d5 b20 = 14,031 ms/game.** Reference = fivecolour 1,033.46 ms/game ->
budget 3,100 ms/game. Prevent Damage is **13.6x the reference, ~4.5x over the budget.**
`MTG_PD_SECOND_MAIN=0` alone is 0.63x (-> ~8.9 s, still ~2.9x over). Per the 3x rule the deck must
NOT be added as-is and **performance becomes the first goal, before the value leaf and mulligan
profile** (both generators are blocked by `--require` anyway). User's call.

### 9. 5f perf — where the time goes (report only; no pruners added)
One slow d5/b20 game (s14013 gi12, 123 s, 2 mulligans) under `perf` (Profile build) and
`MTG_ROLLOUT_STATS`: units_total 2.43M over 141 searched decisions (~17k/decision — the b20 budget of
18k units IS respected), so the cost is **per-unit expense x decision count**, not a blow-up:
~50 µs per unit. The iterative-deepening ladder commits at **depth 1 on 95/141 decisions** (d2 36,
d3 7) — at b20 this deck's "d5" is effectively d1-d2, i.e. starved (consistent with 5c). Units split:
root candidates 35%, rollout steps 32%, greedy fallback 32%. Self-time is flat — BuildSimKey 4.5%,
CollectActions 4.3%, SolveUncached 4.1%, operator new 3.1%, EnumeratePlans 2.4%, then a long tail
(TT hashing, ApplyPlanDirect, ReflectedColors 1.3%, ColorFeasibility, FlushDamageEvents 1.1%,
GenericProvider::TutorCandidates 0.8%). The searched second main is ~15% inclusive. **No single
decision point explodes**: root candidate lists at T3 run 19-47 (Beseech twobrid x tutor width 20,
Living Wish's 13 names), T6 mostly 1-11. The width candidates for a future (A/B-gated) narrowing:
the tutor axis (width 20 x twobrid k=0..3 for Beseech), Rolling Earthquake X 1..max, Pyrohemia K
1..max. The cheapest measured lever is `PD_SECOND_MAIN=0` (0.63x, quality-neutral).
Bottoming: mulliganed games pay clairvoyant bottoming playouts at depth (Fluctuator's lesson) — not
separately quantified here.

### 10. 5e/5g heuristic mining (`mine_heuristics.sh`, d5 / b3000, all turns) — CANDIDATES ONLY
Cost is extreme for this deck (~50 CPU-s per mined decision; a 24-game probe was still grinding
after 13 min). Ran `GAMES=80 SEEDS="30001 31001"` and stopped it (my own run) at ~55 min with seed
30001 partly done: **374 decisions, ONE seed** — below the >=2-seed overfit guard, so every rule
below is LOW-CONFIDENCE. Nothing encoded (cast order is user-reviewed per deck).
- **ORDER (0-conflict):** Living Wish before Rolling Earthquake (16/0), GSZ before Earthquake (7/0,
  10 ties), Vito before Earthquake (6/0), Beseech before Earthquake (4/0), Beseech / Living Wish
  before Manabarbs (3/0 each). One shape: **tutor/assemble the engine, THEN sweep** (the sweeper is
  the payoff). Mixed: GSZ vs Living Wish (4/1), Manabarbs vs Earthquake (2/1) — leave to the search.
- **INCLUSION** (every card + = "not this turn" on average; read the split): Vito +0.02 (neutral),
  Living Wish +0.13 (11 help / 23 hurt), Beseech +0.26 (9/21), Tamanoa +0.28, GSZ +0.35, Faithmender
  +0.50, **Rolling Earthquake +0.59 (1 help / 73 hurt)**, Spellshock +0.69 (0 help), **Manabarbs
  +0.85 (1/49)**, Pyrohemia +0.89 (n=9). All "setup / leave to the search" per the table — but the
  Earthquake/Manabarbs/Spellshock rows are the stored-value signature (self-damage enablers and the
  sweeper are worth little before the gain engine is out), matching g185/g262/g252/g289. A candidate
  for a per-deck cast-order / hold review, not a gate.
- **LAND:** Reflecting Pool 1215, Ancient Tomb 1023, City of Brass 650, Battlefield Forge 551,
  Tarnished Citadel 505, Brushland 369, Grand Coliseum 223, Karplusan Forest 85 (earliest-win lines).
Artifacts: `logs/prevent_damage/stage5/mine_s30001_partial.ewins.jsonl`, `mine_report.txt`.

### 11. Outlier re-check on the fixed binary
s14001 gi142 (d3 T5 vs d5 T6 at every budget on ece1abbe — the one depth non-monotone game) now
wins **T5 at both d3/b10 and d5/b20** on `6d3f212c` (hand holds a second Vito, so the duplicate-legend prune is the likely mover; the
old T6 line was not re-traced). g196 is still unwon (it no longer burns 7 life on the duplicate; the hand is short of a
gain engine until T6 and the game is lost to the clock).

### 12. Open items / PROVISIONAL (surfaced, not blocking; defaults taken)
1. **3x cost gate FAILS (~4.5x over).** Performance is the first goal before VL / mulligan. First
   lever: `MTG_PD_SECOND_MAIN=0` (0.63x, quality-neutral at n=600) — **PROVISIONAL default left ON**
   per 2c-bis; recommend OFF (user call). Then the tutor width x twobrid axis, X/K ranges (A/B-gated).
2. d0 greedy is weak (31.7% won, LP 8.30 vs 5.85 searched) and the rollout IS the leaf: a 5i
   rollout-quality digest (`MTG_DIVERGENCE_LOG`) is warranted — the suicide/duplicate-legend fixes
   were both rollout-policy failures surfacing as root misplays.
3. Stored-value valuation: Earthquake as early burn and a second Manabarbs with no gain engine
   (g185/g262/g252/g289; miner INCLUSION rows). 5c2 still favours the tie-break in aggregate;
   `MTG_PD_LEAF_OWN_LIFE` measured neutral. A hold rule belongs in the user-reviewed cast-order pass.
4. Suicide guard gaps (disclosed in 6(a)): Pyrohemia's {R} tap barb at life 2+, own-land pain, gains
   earlier in the same plan.
5. Keep quality with the baseline profile: Reflecting-Pool-only / no-coloured-source keeps are the
   single largest unwon class (g3, g266, g283) — the mulligan stage (after the cost gate).
6. **Melira Pod's 5c2 NO SIGN is void** (the space-in-stem parser bug, fixed `09a12ad2`); re-run it.
   Mirrorwing Dragon / Creature Giving / Unpredictable Cyclone likewise if they ever ran the script.
7. Card-cost audit incomplete (95 cards 429'd, none of ours) — re-run `audit_card_costs.py` later.
8. 5d claude-play sweep and the §5i discard bucket policy: not run here (separate steps; the
   `discard_policy` gate is not in this tree). Regression-tier rows + GT: not added (separate step,
   and blocked by item 1).
9. mine_heuristics: one partial seed only — re-mine with a second seed before any cast-order work.

**Post-fix `verify_deck --no-network` (on `09a12ad2`):** coverage, viewer, viewer_wiring, mismatch,
**play_invariants PASS** (8 games / 176 decisions); card_fields FAIL (Basri, pre-existing);
regression_tiers / suite FAIL (expected, not added); card_costs / clause_ledger / claude_sweep SKIP.

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
10. (Stage 5) `MTG_PD_SECOND_MAIN` measured again: quality-neutral (-0.0033, t -0.23, n=600) at 0.63x
    CPU when OFF. Default kept ON (2c-bis); recommend OFF — especially since the deck fails the 3x cost
    gate. PROVISIONAL.
11. (Stage 5) New default-ON levers `MTG_PD_SELF_LETHAL_GUARD` and `MTG_PD_DUP_LEGEND` (rules-derived
    dominance prunes; own deaths 2 -> 0 per 600; both neutral-or-better on the metric). PROVISIONAL
    pending the user's review.
12. (Stage 5) 3x cost rule: FAIL (~14.0 s/game at d5/b20 vs the 3.1 s budget). Deck not added to the
    suite; optimisation is the next goal (user call on how far to push before VL / mulligan).

## Orchestrator log (post-Stage 5)

- 2026-09-27: `MTG_PD_SECOND_MAIN` flipped to DEFAULT OFF (ef26b03b) on the Stage 5 paired A/B
  (neutral quality, 1.6x CPU). PROVISIONAL for user review.
- **5j cost gate FAILS (~4.5x at d5/b20; ~2.9x over even with m2 off).** Per the skill: the deck
  is NOT added to any regression tier yet; getting it inside 3x of the reference is the FIRST
  GOAL, before any value leaf / mulligan work. Perf work queued after the claude-play sweep.
- 5d claude-play sweep LAUNCHED on ef26b03b: 16 Opus players, base seed 31001, gi 0..15
  (disjoint from suite and Stage 5 seeds). Results aggregate into `logs/prevent_damage/sweep/`.
- 2026-09-28: 5d sweep DONE and every confirmed flag fixed (`d4c38ed4`, `817df444`) -- see
  "## Claude-play sweep" below (pain-aware payment, one end-of-main sweep, viewer valve, own-death
  protocol, GSZ fail-to-find, labels; gi13 = b20 starvation, gi15 = cast order). CPU +15% at d5/b20
  (`MTG_PD_PAIN_PAY=0` hatch). Next: perf (the 5j gate), §5i discard buckets, suite rows + GT.

## Claude-play sweep
- commit: `ef26b03b`
- seeds: 31001 games: 16
- flags: 0 unresolved

16 Opus players (claude-play, `--reveal 6`), one per game index 0..15 of base seed 31001, run on
`ef26b03b` (m2 OFF). Results: `logs/prevent_damage/sweep/results.jsonl` (gitignored; gi0 has no result
line -- its player recorded only T5/T6 frames, and the orchestrator cited it under findings 1 and 2).
**The sweep ran BEFORE the fixes below, and they change play** (pain-aware payment in both worlds,
GSZ fail-to-find in autonomous play, one sweep per main in human play): the `claude_sweep` gate will
report the record as stale against HEAD. That is expected; a re-sweep on the fixed binary is the
natural next check, not a blocker.

Fixes: **`d4c38ed4`** (findings 1-6) and **`817df444`** (finding 1, per-attempt policy + line-aware /
flexibility-aware assignment, found by the gi15 investigation below).

| gi | ai_win | claude_win | flags -> resolution |
|---|---|---|---|
| 0 | -- | -- | (no result line) pain-blind payment + mid-main pain sweep -> **fixed** `d4c38ed4`/`817df444` |
| 1 | 6 | 6 | combat logged after opp dead (cosmetic); plans_hidden by design -> dismissed (display only) |
| 2 | 5 | 5 | **CONFIRMED** pain-blind payment, no gain engine (T3 Faithmender 17->13, min 17->14) -> **fixed** `d4c38ed4`: claude-play and autonomous now 17->14 (and T4 11 not 9); Dina sac labels identical (victim asked later) -> dismissed (by design) |
| 3 | 4 | 5 | combat after opp dead (cosmetic); sweep stops at won-lock -> dismissed (correct) |
| 4 | 6 | 6 | **CONFIRMED** viewer plan-cap valve dropped GSZ (114,264 raw positions / 27-plan menu) -> **fixed** `d4c38ed4`: 0 groups dropped, GSZ -> Tamanoa (X=3) offered, menu identical to `MTG_VIEWER_PLAN_CAP=0`; pain-blind payment -> fixed |
| 5 | 5 | 5 | Rolling Earthquake X missing from labels; collapsed Wish label -> X **fixed** `d4c38ed4` ("(X=n)"); collapse by design |
| 6 | 5 | 5 | combat after opp at 0 (cosmetic); payment-order (weak, Manabarbs timing) -> covered by the useful-mode policy (damaging mode first, cap-bounded); the per-land-tap "after Manabarbs" ordering is not modelled (the sweep taps leftovers after the casts) -- **PROVISIONAL**, surfaced below |
| 7 | 6 | 7 | **CONFIRMED** pain sweep fired after every applied plan in human play (Karplusan tapped before the tutored Quake) -> **fixed** `d4c38ed4`: the same line now wins T6 (= search); Beseech cost "{6}" -> **fixed** (`{2/B}{2/B}{2/B}`); pain-blind payment -> fixed |
| 8 | 5 | 5 | dup-legend prune by design; combat cosmetic; AI mulled a 7 Claude won with -> dismissed (mulligan stage, baseline profile) |
| 9 | 6 | 6 | **CONFIRMED x3**: (a) SEVERE Dina {B}{G} on two Citadels (3+3 at 5 life) -> own death -> **fixed** `d4c38ed4` (unit test pins the exact board with a control arm that MUST die); (b) pain-blind T3/T5 payments -> fixed; (c) extra frame after own death + Tamanoa gain logged before LOSE -> **fixed** `d4c38ed4` (0 extra frames; event log shows only the LOSE line) |
| 10 | 5 | 5 | dup-legend prune by design (PROVISIONAL) -> dismissed |
| 11 | -- | 6 | AI kept a colourless-only 6 and never won; Claude (clairvoyant) mulled to 5 -> dismissed (mulligan stage) |
| 12 | 5 | 5 | GSZ axis offered only find-X -> **fixed** `d4c38ed4` (fail-to-find: autonomous X=0 under Spellshock + gain engine, human every X) |
| 13 | 7 | 5 | search-misplay candidate -> **investigated: BUDGET STARVATION at b20** (below) |
| 14 | 5 | 5 | all correct; 31.6 s SLOW-GAME -> perf (the known 5j cost failure) |
| 15 | 7 | 5 | search-misplay candidate -> **investigated: CAST ORDER + budget** (below); the replay also exposed a stranding defect in the first cut of the payment fix -> **fixed** `817df444` |

N = 0: every CONFIRMED flag is fixed. gi13/gi15 were CANDIDATES; their classifications are
recorded below and neither is a play-correctness bug (gi15's cast-order item is the user-reviewed
cast-order pass's, and is surfaced there).

### Fixes (rules skill consulted: CR 601.2g-h / 605 / 704.3 / 704.5a / 701.19b)

1. **Pain-aware payment** (`dmgev::PainAwarePay`, `MTG_PD_PAIN_PAY` heurarm slot, default ON,
   PROVISIONAL). Root cause: both payers chose which source pays which pip pain-blind; the old
   `PayWithPain` only chose the MODE of a painland on a generic pip. Mana abilities resolve during
   casting and SBAs are not checked until priority (CR 601.2g-h, 704.3), so a payment's whole pain
   lands before any Tamanoa trigger -- the payment's total must stay under our life. Policy, per
   payment ATTEMPT (each rung of the reservation ladder): (a) never lethal when a survivable
   assignment exists (all-lethal -> the historical payment, which the self-lethal guard /
   `kOwnDeath` already own; the batch prepay and held rungs decline instead); (b) no gain engine ->
   the MINIMUM damage (pain + Manabarbs hits), exact via a DP over source modes
   (`PaymentDamageFloor`), which also picks, among equal damage, the assignment that keeps the WIDEST
   sources up and realises it with a hold; if that strands the rest of the line being applied
   (PlanTraits colour/total check) the historical assignment is taken when it survives; (c) gain
   engine -> damaging mode first, bounded by (a). Manabarbs timing needs nothing: the marks flush when
   the payment commits, before Manabarbs is on the battlefield. City of Brass's damage (a trigger, CR
   603) is counted as pre-SBA pain -- conservative for the bound. Unit tests: gi9 board (control arm
   dies), gi2 min-pain, Manabarbs counted, all-lethal still pays, exact floor, gi15 widest-kept.
2. **One pain sweep, at the true end of main 1** (`PainSweepDeferScope`). The human-play loop applies
   a phase plan by plan; `ApplyPlanDirect` swept after each. **Autonomous search checked: clean** --
   a breakpoint node's prefix apply returns at the pend (before the sweep) and inline continuations
   run inside the same `ApplyPlanDirect`. **The drip sweep (`TapDripLandsIfUseful`, Anti-Lifegain)
   has the same human-play defect** (it fires after every applied plan; `DripManaWantedLaterThisTurn`
   only sees the current hand) -- reported, NOT changed (other decks' references/GT). Unit test.
3. **Viewer valve bounds PAYABLE positions** (`viewerplancap::Estimate`: knapsack count over the
   enumerator's own all-ramp-credited mana bound; raw walk only at 64x). `--strict` unchanged. Unit test.
4. **Own death mid-main**: the human loop stops at once; the flush checks the SBA before logging a
   trigger (no "Tamanoa" line for a gain that never resolved).
5. **Labels**: "(X=n)" on X spells (summary only -- the structured `casts` list is unchanged, so
   reference matching is unaffected); twobrid renders `{2/B}` in hand JSON (tools/play shows the raw
   string and extracts only single-symbol pips, so it renders fine).
6. **GSZ fail-to-find** (`kTutorDeclineTarget`, CR 701.19b): `OfferFailToFindPut` (PD: Spellshock + a
   gain engine) offers X=0 to the search; human play every affordable X, own menu entry (never folded
   under a fetch). The engine skips the "then shuffle" on any empty search (pre-existing); GSZ's
   self-shuffle still shuffles.

### Verification (final binary = `817df444`)
- `./build.sh` clean; mtg-test 304/304; scenarios 117/117.
- **Smoke: 101/101 byte-identical, play-changed 0 (searched and d0)** -- run on `d4c38ed4` and again
  on `817df444`. Every change is gated to `dmg_events_armed` or human play.
- `viewer_protocol_check.py --strict`: 30 ok / 304 repaired / 0 play-drift / 0 enum-gap / 1
  board-diverged (the pre-existing Snow s4_gi3) / 10 mull-drift / 0 contract-fail -- identical to the
  Stage 5 record.
- Repros on the fixed binary: gi2 T3 17->14 (claude-play AND autonomous). gi4 / gi7 / gi9 replay
  the recorded lines with `MTG_PD_PAIN_PAY=0` (the fixed payer changes the life totals, so the
  recorded choice indices no longer reach the same frames with it on; the fixes under test are
  independent of it): gi4's T6 frame offers GSZ -> Tamanoa (X=3), nothing truncated, menu ==
  `MTG_VIEWER_PLAN_CAP=0`; gi7's line (choices remapped by summary for the new X labels / GSZ
  variants) keeps Karplusan untapped after the tutor and wins T6 (= the search); gi9's line dies at
  T6 and goes straight to CLAUDE_RESULT (0 extra frames, event log = the LOSE line only). With the
  payer ON, gi9's line reaches T6 at 11 life, not 5; the 5-life Citadel board is pinned by the unit
  test instead.
- **Deck sanity, 300 games d5/b20 s14001, paired per game (`MTG_DUMP_WINS`)**: baseline
  (`ef26b03b`, m2 OFF) avg 5.8200, 291/300 won; fixed 5.8167, 291/300 won; 13 changed: 7 faster,
  **6 slower (gi 32, 50, 88, 109, 202, 214)** -- every one reverts to the baseline win turn with
  `MTG_PD_PAIN_PAY=0` (the payer is the mover) and every one plays the baseline win turn at b200
  (32:5, 50:5, 88:5, 109:6, 202:7, 214:5): b20 budget churn from the changed rollout life accounting,
  not a line the payer cannot express. **Own deaths (`MTG_PD_STATS`): 0 before, 0 after.**
- **Cost**: batch CPU 3.12M ms vs 2.72M (**1.15x**) at d5/b20 -- the policy's extra attempts. The
  first cut was 5.3x; the DP floor (one capped attempt instead of a descending search whose failing
  proofs were exhaustive on six-colour boards), a lossless damage B&B in the DFS, and per-attempt
  holds brought it down (digests unchanged across the lossless steps). The 5j cost gate was already
  failing (~4.5x); this is +15% on top -- `MTG_PD_PAIN_PAY=0` is the one-binary hatch.

### gi13 / gi15 investigation (d5, budgets 20 / 200 / 1000 / 5000; handoffs via `--choices-then-auto`)
| game | baseline b20/b200/b1000 | fixed b20/b200/b1000/b5000 | Claude |
|---|---|---|---|
| gi13 (s31014) | 7 / 5 / 5 | 7 / 5 / 5 / 5 | 5 |
| gi15 (s31016) | 7 / 6 / 6 | **6** / 6 / 6 / 6 | 5 |

- **gi13 = BUDGET STARVATION at b20** (fixes did not change it). Claude's board handed to the search:
  from T2 (after T1 Reflecting Pool) b20 -> T7, b200 -> T5; from T3 (after Claude's T2 Living Wish ->
  Vito) T5 at both. The b20 search's T2 decision (Dina + Wish -> Faithmender) is the costly one; ten
  times the budget finds Claude's line. Same class as Stage 5c's mild starvation.
- **gi15 = CAST ORDER (+ budget)**; the fixes moved b20 from T7 to T6. From Claude's exact T5 board
  (lands for 7, Dina + Vito out, GSZ + Rolling Earthquake in hand) the search plays T6 at b20 AND b200:
  its line is GSZ + Spellshock, not the lethal GSZ -> Tamanoa then Rolling Earthquake X=2. With
  `MTG_SEARCH_ORDER=1` (cast-ORDER search) the same handoff finds T5, and the full game at b200 is T5
  (b20 still T7). GSZ and Rolling Earthquake tie at CastOrderRank 20, so the canonical order can
  resolve the sweeper BEFORE the tutored Tamanoa arrives -- no gain, no kill. This is the Stage 5
  miner's "GSZ before Earthquake (7/0)" row: a **user-reviewed cast-order item** (not encoded here).
  The replay also exposed that the FIRST cut of fix 1 stranded the Quake's {R} (GSZ paid by Citadel
  {C} + Coliseum {C} at equal pain to a Tomb): fixed in `817df444` (per-attempt floor over the rung's
  sources, widest-kept tie-break, line-aware fallback); a hand-played "GSZ alone" now leaves the {R}
  and the search then finds the T5 Quake.

### Open questions (surfaced, not blocking; defaults taken)
1. **Pain-aware payment for other painland decks?** The same cap would stop pain-blind payments in
   EDF / Angels / any painland list. Deliberately NOT extended (armed decks only). User call.
2. `MTG_PD_PAIN_PAY` default ON is **PROVISIONAL** (quality -1 turn / 300, 0 own deaths either way,
   +15% CPU). Adopt / reject on review.
3. Useful mode keeps the old generic-pip preference (damaging mode first); it does NOT yet prefer a
   painful source for a COLOURED pip, nor schedule lands "after Manabarbs" (gi6's weak note).
4. gi15's cast order (tutor-put engine piece before the sweeper): for the user-reviewed cast-order pass.
5. The drip sweep's human-play defect (fix 2) -- same shape, other decks; fix on sign-off.
- 2026-09-28: 5d sweep DONE (16 games, 0 unresolved after fixes d4c38ed4 + 817df444). NEXT, in order:
  (1) performance agent LAUNCHED on b36941f5 to pass the 5j 3x cost gate (sound certificate first,
  provider narrowings A/B'd; proposes suite rows but does not add them); (2) §5i bucket discard
  policy (analyzer verdict NO_RULE_CONSIDER_SEARCH, 14% label regret); (3) if the gate passes: all
  three tiers + GT accept (`bash test/regression.sh --<tier> --deck=<key>` then `--accept` with the
  SAME filter; `python3 test/check_gt_logs.py`); (4) final verify_deck + Stage 6 report + push decision.

## Performance (Stage 5j) — IN PROGRESS (perf agent, 2026-09-28, worktree `/tmp/pd-wt`)

Scratch: `logs/prevent_damage/perf/` (gitignored). Reference re-checked with `suite_gate.py --report`:
still **fivecolour 1,033.46 ms/game -> budget 3,100 ms/game** (snow 4,179 has no mulligan profile, so
it is not a reference).

### P1. Baseline at HEAD `87a8526e` (suite-shaped rows, suite seeds, 150 games each, one batch)
| row | avg (loss-pen.) | ms/game |
|---|---|---|
| d3 b10 s2002 | 5.8733 | 7,529 |
| d3 b10 s3003 | 5.6933 | 6,209 |
| d5 b20 s2002 | 5.8533 | **12,174** (worst) |
| d5 b20 s3003 | 5.6800 | 10,412 |

**3.9x over the budget.** `MTG_ROLLOUT_STATS` on that batch: **`[bottom-cost]` 3,728 CPU-s of 5,449
(68%) is London BOTTOMING** — 156 bottom decisions at **23.9 s each**: with no exhaustive bottom table
every legal removal subset plays a full clairvoyant game at play settings (FiveColour 90.4% and
Fluctuator ~92% before their tables — the same class). The remaining search: units_total 153.5M,
id-depth mean 1.83 (starved), units split rollout_step 36% / greedy_fallback 34% / la_cand 28%.

### P2. Bottoming-eval + leaf-depth levers (one pooled batch `ab1`, d5 b20 = play settings, held-out s40001 + s41001 x 300, per-arm profile copies; `base` reproduces the shipped profile's digest exactly)
| arm (profile key) | ms/game | x base | paired delta (loss-pen.) | t | worse / better |
|---|---|---|---|---|---|
| base | 11,334 | 1.00 | — | — | — |
| `bottom_eval_depth 0` (all-greedy bottoming rollouts) | 3,240 | **0.29** | **+0.1350** | +7.04 | 55 / 0 |
| `bottom_eval_depth 0, topk 3` | 5,818 | 0.51 | +0.0417 | +4.56 | 24 / 1 |
| `bottom_eval_depth 0, topk 5` (the Melira shape) | 7,572 | 0.67 | +0.0133 | +2.32 | 8 / 1 |
| `search_leaf_depth 0` (greedy horizon leaf; Melira ships it) | 7,197 | 0.64 | +0.0367 | +2.14 | 49 / 31 |

**All four REJECTED** (none is quality-neutral). The bottoming arms lose in ONE direction only
(55/0): the base bottomer plays every legal removal to the end CLAIRVOYANTLY at full play settings, so
it is an oracle on the true draws and any cheaper rollout can only match or lose to it. That is also
why this cost is TRANSITIONAL: the exhaustive keep/bottom table (mulligan stage) replaces these
rollouts outright (`AIEngine::BottomCards`' table path runs first) — every reference deck in the
gate's table is measured WITH its table. The `bottom_eval_depth 0` arm is a near-upper bound on
what removing the bottoming rollouts saves: **PD's play-only cost is ~3.2-3.5 s/game at d5 b20**
(≈1.05-1.15x the 3,100 budget), not 11-12 s.

### P3. Where the non-bottoming (play) cost goes
- **Play-only cost** (`MTG_BOTTOM_ROLLOUTS=0`, the suite-shaped rows of P1; heuristic bottoms, so the
  games differ from the shipped ones — a cost probe, not a quality arm): d5 b20 **3,224 / 3,095
  ms/game** (s2002 / s3003), d3 b10 2,360 / 2,367. So the play search alone sits at **~1.04x** the
  3,100 budget; the clairvoyant bottoming rollouts take it to 3.9x.
- The budget BINDS: ~15.5k units per searched decision at d5 b20 (id-depth mean 1.4-1.8), ~5.5
  searched decisions per game. Under a binding budget a **provider narrowing cannot lower cost** — it
  only redistributes the same units (deeper, narrower). The Stage 5 narrowing candidates (Beseech
  twobrid x tutor width, Earthquake X, Pyrohemia K, Wish width) are therefore QUALITY levers here, not
  cost levers, and were not pursued for the gate. The only cost levers are wall-per-unit and the
  number of searched decisions/rollouts.
- `perf` (Profile build, 12 play-only games, s40001): flat. Inclusive: SimulateToEnd (horizon leaf)
  84%, ApplyPlanDirect 51%, mana payment (TapForCostShared) 19%, PerformTutor 8% (of which the tutor
  CANDIDATE LIST 5.7% — rebuilt on every greedy tutor resolution in the rollouts), the pain-payer's
  floor DP (`PaymentDamageFloor`) 5.1% (4.2% self, the per-state div/mod decode), PainAwarePay
  wrapper 4.0%, CollectActions 10%, ShuffleByKey 2.3%.

### P4. Lossless BOTTOMING CUTOFF — built, verified byte-identical, NOT adopted (no measurable win)
Idea: every consumer of a bottoming candidate's score reads it only as "== the best", so a
candidate's rollout need only be played through the running-best turn (then reported max+1). Built
as `MTG_BOTTOM_CUTOFF` (heurarm slot; off under refine / blind-K / external chooser / trace). One
pooled batch, cut vs nocut, 1,800 paired games (d5 b20 s40001/s41001 x300, d3 b10 s2002/s3003 x150):
**per-game outcomes AND digests identical on all 1,800; the cutoff fired on 1,072 of 2,467 armed
rollouts** (so the equality had power). Cost: -1.4% / -0.4% (d5), -2.2% / +5.6% (d3) — inside run
noise. The cut turns are the cheap tail; nearly all bottoming cost is in candidates that TIE the best
and must be played in full. **Reverted** (no measurable benefit for the added surface).

### P5. Lossless per-unit work — ADOPTED (`6a8f09ba`)
Two byte-identical strength reductions from the P3 profile: (1) the tutor candidate list is built in
ONE walk — `GenericProvider::TutorCandidates` tracks distinct names by interned pointer instead of an
`unordered_set<string>`, and `PreventDamageProvider`'s nonlands-first order comes out of the same walk
(no per-name string `Lookup`); (2) `PaymentDamageFloor`'s DP decodes each state's mixed-radix digits
once per call instead of once per (source, state), and computes the successor index by difference.
Verification: PD 600 games at the P1 suite rows **per-game identical** (all four `.wins` files and
digests); smoke **101/101, play-changed 0**; mtg-test 304/304; scenarios 117/117. Cost: **-3.4% CPU on
PD play** (play-only, 3/3 interleaved reps vs a worktree build of the committed tree, 30.16 -> 29.14
CPU-s). Invisible in the with-bottoming totals (inside run noise).

### P6. `MTG_PD_PAIN_PAY` re-priced (play-only probe, s40001 + s41001 x 200, one batch)
ON 1,206,809 ms vs OFF 1,074,972 ms = **1.12x** (was 1.15x before P5's DP fix). Quality OFF-minus-ON
+0.025 / +0.015 (ON better, as recorded). `MTG_PD_PAY_STATS`: 30.1M policy calls, 17.6M harmful-mode
(14.65M on the exact DP path, 0 exact-retry failures), 12.5M useful-mode. What remains is the DP plus
the per-payment board scans (`PaymentPainSafe`, `CountTamanoa`, `SelfDamageUseful`, `PaymentDamage`),
i.e. ~12% of play — not a lever that moves the gate either way. Default stays ON (PROVISIONAL, as
recorded).

### P7. Certificate (`ProvenWinlessThisTurn`) — NOT BUILT, and why it cannot move the 5j gate
`WinlessCertificateActive` (TurnSolver) consults the hook only inside `LabelSearchScopeActive()` —
the value-leaf LABEL ladder and the unbounded depth-matrix cells — because pruning consumes no work
units and would change play under a budget. **The suite rows are budgeted play, so a certificate
cannot change `cost_per_game_ms` at all.** Its payoff is the VALUE-LEAF GENERATION (Fungus: 1.9x
label speedup), which is itself blocked by this gate. Deferred; `Certificate()` stays `NotAssessed`.
The sound shape when it is built (for the VL stage):
- **Tier 1 (cheap, likely the common fire):** no drain engine (Vito / Dina) on the battlefield AND
  none REACHABLE this turn — not in hand, and no tutor in hand that could fetch one (Beseech ->
  library MV <= lands, GSZ -> green creature MV <= X puts Dina onto the battlefield, Living Wish ->
  sideboard) — then the opponent's loss is bounded by combat (power of creatures that can attack NOW;
  nothing in the 75 grants haste — verify by NAME whitelist, the Fungus rule) + direct damage
  (Earthquake X and each Pyrohemia ping deal <= 1 opponent damage per mana, so <= this turn's total
  mana: untapped sources at their max yield, + floating, + one land drop at max yield).
- **Tier 2 (drain present):** additionally bound lifegain EVENTS (land taps x Manabarbs copies, casts
  x Spellshock copies, pain taps, Pyrohemia activations, Earthquake casts, combat with Vito's
  lifelink) x per-event gain (damage x 2^Faithmenders + Bilbos, counting ones fetchable this turn)
  x Vitos, + events x Dinas. Only worth it if Tier 1's fire rate is too low.
- Name whitelist over hand / battlefield / graveyard / sideboard; decline on any unknown card; audit
  with `MTG_WINLESS_AUDIT` before adoption.

### P8. VERDICT — the 5j gate is NOT met with sound, adopted levers
| state | worst suite row (d5 b20 s2002) | vs 3,100 budget |
|---|---|---|
| BEFORE (HEAD `87a8526e`) | 12,174 ms/game | 3.93x |
| AFTER (`6a8f09ba`, P5 adopted) | 12,331 ms/game (same games; run noise) | 3.98x |
| play-only (no bottoming rollouts; probe) | ~3,100-3,224 ms/game (P3; -3.4% after P5) | ~1.0x |

- **68% of the cost is clairvoyant London bottoming** (24 s per bottom decision: every legal removal
  played to the end at full play settings). No lossless reduction exists (P4: the cut-able tail is
  cheap; the cost is in candidates that tie the best). Every cheaper bottomer loses quality in ONE
  direction (P2: 55/0, 24/1, 8/1 worse/better) — it is an oracle on the true draws.
- **Every reference deck in the gate's table is measured WITH its exhaustive keep/bottom table**
  (`AIEngine::BottomCards`' table path runs before, and replaces, these rollouts) **and with its
  value leaf.** PD has neither, and both generators refuse a deck outside the suite — the gate as
  applied here compares PD's pre-artifact cost with peers' post-artifact cost. The play search alone
  is ~1.0x the budget, before the value leaf (which cut Giants to 0.14x).
- **What would be needed:** the mulligan (exhaustive keep/bottom) table removes the 68% outright;
  then ~1.0x remains, which the value leaf is expected to clear. Both require the USER to lift the
  gate for this deck (`MTG_ALLOW_UNTESTED_DECK=1` is user-only) or to rule that the gate measures
  post-artifact cost. **Default taken: nothing adopted that costs quality; deck NOT added to any
  tier.** (Alternative the user could choose instead: ship `bottom_eval_depth 0, topk 5` — 0.67x,
  +0.013 turns/game, t=2.3 — which still leaves ~8 s/game, 2.6x over; not recommended.)

### P9. PROPOSED suite rows (NOT added — for the step that adds them once the gate passes)
Key `pd` (`[pd]="decks/Prevent Damage/Prevent Damage.cod"` + `.profile.json`), shaped on melira (the
costliest peer family); `pd2hg` because Dina ("each opponent"), Rolling Earthquake / Pyrohemia ("each
player") and Vito ("target opponent") all read opponents / life. Probe costs at the CURRENT state
(with bottoming rollouts; 20 threads): d0 0.26 ms, d3 b10 5.8-7.5 s, d5 b20 8.3-12.2 s, 2HG d3
7.8 s, 2HG d5 8.5 s per game (2HG probes n=40-60).
| tier | rows | est. core-min now | est. after artifacts |
|---|---|---|---|
| smoke | `pd 0 1001 1000 0` · `pd 3 1001 25 10` · `pd 5 1001 15 20` · `pd2hg 3 1001 15 10` | ~8 | ~2.5 |
| regression | `pd 0 2002 1000 0` · `pd 3 2002 40 10` · `pd 3 3003 40 10` · `pd 5 2002 25 20` · `pd 5 3003 25 20` · `pd2hg 3 2002 25 10` | ~23 | ~7 |
| overnight | `pd 0 {4004,6006,8008,10010} 2000 0` · `pd 3 {4004..7007} 100 10` · `pd 5 {4004..7007} 75 20` · `pd2hg 5 {4004..7007} 50 20` | ~135 | ~42 |
Smoke's per-game tail (a 30-90 s SLOW-GAME is common at d5) matters more than its total: keep the d5
count low. "After artifacts" assumes the bottoming share disappears and play stays ~1.0x budget.

- 2026-09-28 (perf agent): 5j **NOT met** — see P8. 68% of cost is pre-table clairvoyant bottoming;
  play-only ~1.0x budget. Lossless P5 adopted (`6a8f09ba`, -3.4% play CPU). Open USER question: lift
  the gate for PD's mulligan table + value leaf (`MTG_ALLOW_UNTESTED_DECK=1`), or rule that 5j
  measures post-artifact cost. Rows proposed in P9, not added.
