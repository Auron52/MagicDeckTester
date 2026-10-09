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

## RESUME STATE (2026-09-28, after Stage 6)

- **All stages through Stage 6 are DONE on branch `prevent-damage-analysis` (local only, nothing pushed).**
  Read **"## Stage 6 — Report (2026-09-28)"** at the END of this ledger first: it has the results,
  the 6a disclosure and the consolidated **Decisions awaiting the user (D1-D16)**.
- Implementation: I1 `5bead6b1`, I2 `9c9186cd`, I3 `cdec4a37`. Stage 4 `ece1abbe`. Stage 5 fixes
  `6d3f212c`. m2 OFF `ef26b03b`. Sweep fixes `d4c38ed4` / `817df444`. Lossless perf `6a8f09ba`.
  §5i `e4524d71` + `dfa5a12e`.
- **2026-09-29: Living Wish ranking adopted** (`MTG_PD_WISH_RANK`, default ON, user doctrine). See
  "## Living Wish ranking" at the end (-0.0217 t/game, t -2.51).
- The blocker is **5j (3.98x the cost budget; 68% of it is pre-table clairvoyant bottoming)**. The
  deck is in no regression tier, and the keep-table / value-leaf generators refuse it until the user
  settles D1. Then run, serially: D7 (§5i adopt), then the mulligan table, then the value leaf, then
  the P9 tier rows + GT, then a claude-play re-sweep on HEAD (the recorded sweep is stale).

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
- commit: `b5a594e0`
- seeds: 61001..61016 games: 16
- flags: 0 unresolved

**Re-sweep on HEAD `b5a594e0` (2026-10-08, 16 Opus players, one SEED each, `--reveal 6`, benchmark =
the shipped d5/b20).** Results and replay helpers: `logs/pd_opt/sweep/` (gitignored). 11 games tie
the AI, 2 beat it, 0 lose to it; every player predicted every life total from cards.json and matched
the engine except where flagged.

| seed | AI | Claude | flags -> resolution |
|---|---|---|---|
| 61001 | 5 | 5 | **CONFIRMED** the pain-aware payer's rest-of-line check (`line_ok`) let an any-colour land cover every colour, so an X-spell-first payment stranded Tamanoa -> **fixed** `MTG_PD_LINE_OK_EXACT` (two more defects in the same check: phantom mana from non-mana permanents, and re-demanding already-paid casts) |
| 61002, 61007, 61011, 61012, 61016 | 5 | 5 | none (61011: a pre-Vito painful payment, see 61013) |
| 61003 | 6 | **5** | **search could not EXPRESS "cast Pyrohemia, then ping it in the same main"** (no site-9 variant: `CardHasPostEntryActivation` lacked `ping_all_cost`; not recovered at d8 b0) -> **fixed** `MTG_POST_ENTRY_PING_ALL` |
| 61004, 61009 | 7 | 7 | none |
| 61005 | 6 | 6 | **CONFIRMED** whole-turn batch prepay pays a multi-cast plan before its first cast resolves -> lost Tamanoa gains / drains -> **fixed** `MTG_PD_BATCH_TIMING`; **CONFIRMED** the apply path shuffled Green Sun's Zenith back as its template (`m_number 0`) -> the lookahead's library order differed from the game's -> **fixed** `MTG_GSZ_SHUFFLE_COPY_ID` (also the root cause of s108346 gi345, below) |
| 61006, 61008, 61010, 61015 | 6 | 6 | none (61008: a pre-Vito painful payment, see 61013) |
| 61013 | 6 | **5** | the batch-prepay defect again (fixed, above); the pain deferral did not hold City of Brass for a lone Vito/Dina -- REAL: a {C} capability bit (Reflecting Pool reflects {C}, City cannot) failed the colour-superset test -> **fixed** `MTG_PD_DEFER_C_GENERIC`; the T5 line needs Living Wish -> **Brushland**, which `MTG_PD_WISH_TRIM`'s "always Battlefield Forge" rule prunes -> a counterexample to the USER's doctrine (surfaced, not changed) |
| 61014 | 6 | **5** | none: the search's T1 land choice at b20; it finds T5 at b100 (budget, not inexpressible) |

### First sweep (`ef26b03b`, 2026-09-27)
- commit: `ef26b03b` · seeds: 31001 games: 16 · flags: 0 unresolved (after the fixes below)

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

## §5i discard policy (2026-09-28, implementation agent; worktree `/tmp/pd-wt`) — PROVISIONAL, awaiting USER REVIEW

**Implemented** `PreventDamageProvider::CleanupDiscardCandidates` from
`docs/design/prevent-damage-discard-policy-proposal.md`, section for section: GAIN / DRAIN / AMP / FUEL /
DIG / MANA buckets net of board, the interleaved ladder
`LAND > GAIN1 > LAND > DRAIN1 > LAND > DRAIN2 > LAND > DIG1 > FUEL1 > GAIN2 > AMP1 > LAND > FUEL2 > LAND`,
S0 dead (duplicate legend / target-less tutor / dead Reflecting Pool) > S1 NEG > S2 surplus lands >
S3 OTHER > S4 overflow (FUEL > DRAIN > AMP > DIG > GAIN, far first) > S5 kept tail. Params only, every
read via `LookupCached`, routed through `CleanupDiscardRankingWithOrder`. Gate `MTG_PD_BUCKET_DISCARD`
(`EnvOn(...,true)`, heurarm slot `PD_BUCKET_DISCARD`); `=0` = `GenericProvider::CleanupDiscardCandidates`.
No deviation from the proposal's rules; implementation choices where it was silent: a GSZ whose
reach has no GAIN/DRAIN piece prices X at the cheapest reachable card; an unknown/unclassified card
goes to OTHER; S0 order is legend, tutor, Pool (the proposal's a, b, c).

Also fixed the three stale notes the proposal flagged: Living Wish's and Vexing Shusher's cards.json
bracket notes were CORRECT for EldraziDisplacerFlicker (which runs both, with Essence Depleter /
Dimensional Infiltrator and two {C} sinks in its sideboard), so they were made deck-neutral (per-deck
statements) rather than deleted; `PreventDamageProvider`'s class comment now says width 20.
`oracle_text` is cosmetic (stripped from the card digest), so no play change.

### Verification
- **Unit tests** `test/unit/test_prevent_damage_discard.cpp`: the proposal's T1-T11 verbatim (full
  vectors for T1/T3/T4/T6/T8, idx0 + permutation for the rest) plus a Shriekmaw opp-creature case; every
  board also pins the generic idx0 (the control that must differ). T10: heurarm `PD_BUCKET_DISCARD=0`
  returns exactly `GenericProvider`'s ranking (idx0 Beseech) while ON returns Pyrohemia. 12/12 pass on
  the first build; full `mtg-test` green; `test/scenarios.sh` 117/117.
- **Firing census** (`MTG_SHED_STATS=1`, new `SHED POLICY` line — a provider-counted census of calls and
  of calls whose idx0 differs from the generic fallback), over the A/B batch below: **real sheds 49,
  rollout sheds 1,800,083** (1,605,284 at < 4 lands; every one at hand size 8, 1.00 shed/cleanup);
  policy calls **1,844,030, idx0 differs from generic on 1,580,150 (85.7%)**. The rule fires, and
  almost entirely inside the search (~37,000x real play). Shed cost 12.7 us/call. `MTG_DISCARD_SHED_VERIFY=1`
  on the same batch: 0 mismatches (but no multi-card cleanup occurred, so prefix stability — which the
  sink-conditional land target can in principle break — is untested; see review item 6).
- **Paired A/B at play settings** (d5 b20, profile attached, ONE pooled batch, 20 threads, heurarm flag
  per job; seeds **50001 / 51001 x 300**, disjoint from the suite and Stage 4/5/5j):

  | arm | s50001 | s51001 | CPU |
  |---|---|---|---|
  | ON (policy) | 5.7800 | 5.7600 | 6,517 s |
  | OFF (`=0`, generic) | 5.7767 | 5.7567 | 6,387 s (ON 1.02x) |

  Paired (loss scored 9): **delta +0.0033 turns/game (ON worse), t = +0.82, n = 600; 4 worse / 2
  better; 28 games changed play** (22 with the same result: 12 same win turn, 10 unwon in both). Non-inferior within
  noise — but the losses are NOT noise, see below.
- **Every changed game root-caused** (all 28 re-run with `--log-dir`, both arms; mulligans identical in
  all 28, so no keep-side cause):
  * **3 of the 4 worse games (s50001 gi283 5->6, s51001 gi160 8->unwon, gi290 5->6) are one mechanism:
    ON declines the TURN-1 LAND DROP** with lands in hand, goes to 8 cards and sheds Manabarbs at
    cleanup. The policy makes that line look free to the search: with no gain engine available (R2)
    Manabarbs is the worst card in the hand, and the greedy rollout that misplays Manabarbs scores the
    hand better without it — whereas under the generic fallback the same line would shed Beseech /
    a 4-drop, so the search plays the land. ON skipped a T1 land drop in **6** changed games (gi283,
    gi287, gi25, gi94, gi160, gi290) vs **1** for OFF (gi108, which both arms skip). This is a
    search/rollout interaction the policy AMPLIFIES, not a policy mis-ranking at a real cleanup.
  * The 4th worse game (s50001 gi0, 7->8) is a real T4 cleanup at two lands: OFF shed Beseech (MV 6),
    ON kept Beseech (DIG1 protects the only tutor even though it is FAR at 2 lands / 1 black source)
    and shed a second Manabarbs; ON then spent T7 on Beseech. A doctrine outcome (distance orders only
    inside a bucket), not a bug.
  * Better: gi108 (both skip T1; ON sheds Spellshock not Manabarbs, 7->6), gi156 (7->6).
- **Smoke** (`bash test/regression.sh --smoke`): **101/101 PASS, every case byte-identical
  (exp == got digest), 0 play-changed**; `check_gt_logs.py` consistent. (Prevent Damage is not in the
  suite yet, so the smoke is purely the "other decks unchanged" check.)
- **`discard_policy` gate** (`verify_deck.py gate_discard_policy`, read-only from the main checkout with
  `ROOT` pointed at this worktree; provider resolved by the engine): **PASS** — "PreventDamageProvider
  carries an authored bucket policy" (not MISSING / PATCH-ONLY / INHERITED); `discard_policy_audit`
  classifies it OK (control: Angels still MISSING). Its one disclosure is correct and deliberate: there
  is no `## Discard policy` section yet, because that section records the USER's confirmation, which
  has not happened. Write it on approval.

### Status: default ON, PROVISIONAL (user adopts)
Non-inferiority holds statistically (t = +0.82) and the doctrine is the reviewed proposal, so it ships
default ON per the 5i mandate — but the three T1-land-skip losses are a systematic mechanism, not
noise, so this is explicitly **not** an adoption claim. `MTG_PD_BUCKET_DISCARD=0` restores the old
behaviour exactly.

### Review items for the USER (proposal section 8, plus what the measurement added)
1. **NEW — the T1 land-skip interaction.** Options: (a) accept (net effect within noise); (b) make an
   unused land drop with a land in hand turn the held land into the first shed at cleanup (a policy-side
   deterrent: the decline-and-shed line then loses a land, so the search stops choosing it — a
   heuristic, it would need its own A/B); (c) treat it as a search issue (declining the land drop is a
   searched axis; the rollout's weakness with Manabarbs is what makes it look free — the Stage 5 "d0
   plays this deck badly" finding). Recommendation: (b) as a measured lever, since it is local to this
   deck and cheap to A/B.
2. FUEL placement (proposal Doubt 1): FUEL1 protected above GAIN2/AMP1; FUEL2 kept a second Manabarbs
   over Pyrohemia in gi0.
3. Pyrohemia last among fuel (Doubt 2) — gi0's OFF arm won with it.
4. Land target 5 / 6 with a sink (Doubt 3).
5. Colour cover before Ancient Tomb (Doubt 4); Reflecting Pool as the worst live land (Doubt 5).
6. Overflow precedence DRAIN backup below AMP (Doubt 6); tutor order Wish > GSZ > Beseech (Doubt 7) —
   and gi0 shows DIG1 protecting a FAR Beseech; a far-tutor exception is a candidate refinement.
7. Painful-land preference with a gain engine (Doubt 8) — not implemented, as proposed.
8. Prefix stability: the rule is declared stable (the default) but its land target depends on whether
   a sink is still in hand, so a multi-card cleanup that sheds the sink first could differ from
   re-asking. Unobserved so far (every measured cleanup shed exactly one card).
9. Timing (Doubt 10): adopt/reject BEFORE the value leaf and keep table are generated — both fit to the
   rollout policy this changes.

### Review item 1, option (b): `MTG_PD_SHED_UNPLAYED_LAND` (2026-09-28, worktree `/tmp/pd-wt`) — ADOPTED default ON, PROVISIONAL
**Rule.** Inside the 5i policy: at a cleanup (unstaged hand > 7) whose land drop went UNUSED
(`lands_played_this_turn < LandDropsAvailable()`) with a LIVE land in hand, the worst live land (land keep
order reversed) is shed FIRST, ahead of S0. A dead Reflecting Pool is not a live land (playing it gains
nothing; S0 sheds it anyway). Heurarm slot `PD_SHED_UNPLAYED_LAND`; `EnvOn(..., true)`; inert when
`MTG_PD_BUCKET_DISCARD=0`.
**Doctrine.** CR 514.1 cleanup discards to hand size; the rule only chooses WHICH card. This deck has
no landfall, no Karoo, and Manabarbs punishes TAPPING lands, not playing them, so "skip the drop, pitch
a spell" is weakly dominated by "play the land, keep the spell"; shedding the land makes the decline
cost exactly that land. **Visibility:** both cleanups read `lands_played_this_turn` BEFORE the
turn-start reset (GameEngine::CleanupStep precedes UntapStep; TurnSolver::SimulateEndAndStartNextTurn
sheds before its reset), through the one provider function, so executor and rollout agree. PD has no
searched-discard width (index 0 is the rollout's shed).
**Unit tests** (`test_prevent_damage_discard.cpp`): the proposal boards now set `lands_played_this_turn
= 1` (they are cleanups at which the drop was spent; T1-T11 unchanged). New: a T1 8-card board, drop
unused — fix OFF sheds a spell (control), fix ON sheds a land, remainder identical; inert when the drop
is spent, at 7 cards, and with the policy OFF (== generic); dead-Pool-only hand inert. 14/14.

**Paired A/B, ONE pooled batch** (d5 b20, profile attached, 20 threads, 12 jobs = 3 arms x 4 seed
sets x 300; heartbeat 20/20). The `nofix` and `off` digests on 50001/51001 are byte-identical to the
e4524d71 run (determinism check). Loss scored 9.

| arm | s50001 | s51001 | s52001 | s53001 | train | held-out | all 1200 |
|---|---|---|---|---|---|---|---|
| fix (policy ON + fix ON) | 5.7833 | 5.7600 | 5.7833 | 5.6633 | 5.7717 | **5.7233** | 5.7475 |
| nofix (policy ON, fix OFF) | 5.7800 | 5.7600 | 5.7833 | 5.6700 | 5.7700 | 5.7267 | 5.7483 |
| off (policy OFF) | 5.7767 | 5.7567 | 5.7767 | 5.6733 | 5.7667 | 5.7250 | 5.7458 |

| pair (+ = first worse) | train | held-out | all (n=1200) |
|---|---|---|---|
| fix - nofix | +0.0017 t=+0.33, 3w/3b | -0.0033 t=-1.42, 0w/2b | **-0.0008 t=-0.30, 3w/5b**, 41 play-changed |
| fix - off | +0.0050 t=+1.34, 4w/1b | -0.0017 t=-0.45, 2w/3b | **+0.0017 t=+0.63, 6w/4b**, 63 play-changed |
| nofix - off (the policy alone) | +0.0033 t=+0.82, 4w/2b | +0.0017 t=+0.58, 2w/1b | **+0.0025 t=+1.00, 6w/3b** |

**Land-drop skips** (a turn with a land in hand and no PLAY_LAND; from full per-game traces):

| arm | T1 skips (train+held-out) | ...of which ended in a cleanup shed | skips, any turn |
|---|---|---|---|
| fix | 9 + 4 = 13 | **1** | 42 |
| nofix | 11 + 6 = 17 | 6 | 43 |
| off | 5 + 8 = 13 | 4 | 37 |

**Target games.** s51001 gi160 (unwon -> 8) and gi290 (6 -> 5) RECOVER: with the fix the search plays
the T1 land. s50001 gi283 does NOT (6 in both ON arms, 5 OFF) — and the e4524d71 attribution of it was
WRONG: gi283 is ON THE PLAY, 7 cards at T1, **no cleanup at all**; its T1 skip cannot be reached by a
cleanup rule. s50001 gi0 (7 -> 8) is the known DIG1-protects-far-Beseech doctrine case (unchanged).

**Every worse game root-caused** (mulligans identical in ALL changed games, both pairs):
* fix vs nofix worse: **gi108 s50001 (6->7)** and **gi156 s51001 (6->8)** — the search skipped the drop
  ANYWAY and the fix then shed a real land (gi108: Coliseum T1 AND Brushland T2). The deterrent failed
  to deter. Root cause, from `MTG_TRACE=search,plans` on gi108 T1: `EnumeratePlansWithLand -> 5 plans`
  (4 lands + no-land), `pass=1 done win=9` — **no plan wins inside the horizon**, so every plan is a
  no-win leaf and the land plans TIE with the no-land plan; losing a land only matters if it moves the
  (non-existent) projected win. Neither `MTG_PD_LEAF_OWN_LIFE=0` nor `MTG_LEAF_TB_PERMS=1` changes the
  gi108 choice (both repro'd: still skips, 7), so it is not the own-life leaf term and not simply the
  permanent-count tie-break. gi243 s51001 (5->6): the fix arm casts a T2 Rolling Earthquake off a
  different rollout valuation — a search tie flip.
* fix vs off worse, additionally: gi283 (above), gi86 s52001 (T1 land CHOICE differs: Reflecting Pool
  vs Citadel), gi289 s52001 (T2 skip, no shed). All search tie flips driven by rollout-cleanup
  valuations, not a mis-ranked real cleanup.

**Decision: ADOPT default ON (PROVISIONAL, user review).** The bar was "non-inferior to policy-OFF and
>= policy-ON-without-fix": fix vs nofix -0.0008 (t=-0.30; held-out -0.0033, 0 worse / 2 better), fix vs
off +0.0017 (t=+0.63, inside noise — and smaller than the policy-alone's +0.0025). It cuts T1
skip-and-shed from 6 to 1. Its downside is real and bounded: when the search skips anyway (the
no-win-horizon tie), it costs a real land (gi108, gi156). `MTG_PD_SHED_UNPLAYED_LAND=0` hatch.

**The policy itself (for the USER; default NOT flipped).** Combined 1200 paired games, policy (nofix)
vs OFF: +0.0025 t=+1.00; held-out alone +0.0017 t=+0.58. **Not worse beyond noise — but there is no
evidence it is better either;** every point estimate leans slightly against it, and the adopted
combination (fix) vs OFF is +0.0017 t=+0.63. Whether a policy that is at best neutral should ship
default ON is the user's call.

**NEW follow-up (option (c), not done): the T1 land skip is a SEARCH behaviour present in every arm**
(13 of 1200 games even with the policy OFF). Mechanism: at T1 no plan wins inside the d5 horizon
(win=9), so the land/no-land plans tie and the no-land plan can be committed. The discard rule only
changes what the tie costs. A development-aware no-win tie-break that prices an unused land drop
(the Karoo-class idea behind `MTG_LEAF_TB_PERMS`, which alone did not fix gi108) is the real fix;
it needs its own investigation of why the tie survives TB_PERMS.

## Stage 6 — Report (2026-09-28)

Compiled on `dfa5a12e` (branch `prevent-damage-analysis`, local only, nothing pushed) from this ledger,
the final gate log `logs/prevent_damage/final/verify.log`, and the CODE: `PreventDamageProvider` in
`src/ai/DecisionProviders.{h,cpp}`, `GoldFishRunner::DeckUsesSecondMain`, `core/DamageEvents.h`,
the deck's `cards.json` entries (main + side) and `scripts/audit_viewer_decisions.py`. No build and
no runs were made for this report.

### 1. Cards implemented this run (21 new, 5 notes updated; 0 missing / 0 partial, main + side)

| tier | cards |
|---|---|
| Tier 1 (existing painland shape) | Battlefield Forge, Karplusan Forest, Tarnished Citadel, Grand Coliseum |
| Tier 2 (new params on existing templates) | Ancient Tomb (`tap_self_damage_any_mode`), Spellshock (`on_cast_trigger_any_mv`), Green Sun's Zenith (Chord + colour filter + self-shuffle, fail-to-find), Shriekmaw (ETB destroy + evoke), Acidic Slime (ETB destroy) |
| Tier 3 (new engine behaviour) | **Damage-event core** (own death as an SBA, lifegain replacement chain, per-event triggers; gated on `dmg_events_armed`): Manabarbs, Tamanoa, Purity, Rhox Faithmender, Bilbo (incl. the 111-life activation), Vito (drain + team lifelink), Dina (drain + sac-pump). Pyrohemia (new `PingAll` mode), Rolling Earthquake (X-each-creature-and-player), Beseech the Queen (**twobrid `{2/B}` parse**; it was misparsed as `{B}`), Timeless Witness (graveyard return + Eternalize), Dimir House Guard (Transmute) |
| Notes updated | City of Brass, Brushland (pain now matters), Living Wish and Vexing Shusher (made deck-neutral), Reflecting Pool (unchanged model) |
| Tier 4 accepted | **none yet.** Every deferral is PROVISIONAL until you sign it off (decision D10) |

### 2. Mulligan profile: the Stage 4 baseline ONLY
`decks/Prevent Damage/Prevent Damage.profile.json` (`ece1abbe`): `min_lands 1`, `max_lands 5`,
`stop_at 4`, `curve_check two_drop`, `bottom_order count_first`, **`hand_score_threshold -1e18`** (the
hand score never mulligans), **`min_color_sources {}`**, no `required_pieces`, no `value_play`. Card
scores (first copy): Tamanoa +0.37, Vito +0.30, Ancient Tomb +0.28, City of Brass +0.24, Living Wish
+0.24, Tarnished Citadel +0.21, Grand Coliseum +0.20, GSZ +0.17 ... Faithmender -0.26, Spellshock
-0.23, Manabarbs -0.21 / -0.45, Reflecting Pool (2nd) -0.45, Pyrohemia -0.45. **No keep table and no
value leaf**: both generators refuse a deck that is not in the suite, and 5j keeps it out (item 3).
Bad keeps are the largest unwon class: a lone Reflecting Pool or Pool + Tomb with no coloured source
(g3, g266, g283; sweep gi11).

### 3. Win rate / average win turn: a STANDALONE substitute (disclosed deviation)
The deck is in NO regression tier, so there is no suite run. It fails the 5j 3x cost rule; see P8.
Standalone numbers in its place ("LP" = loss-penalised average, loss scored 9):

| run | commit | games | won | avg win turn | LP |
|---|---|---|---|---|---|
| 5b depth sweep d0, s14001 | `ece1abbe` | 1000 | 317 (31.7%) | — | 8.300 |
| 5b d3 b10, s14001 | `ece1abbe` | 300 | 290 (96.7%) | — | 5.867 |
| 5b d5 b20, s14001 | `ece1abbe` | 300 | 291 (97.0%) | **5.75** (T4 7, T5 116, T6 120, T7 39, T8 9) | 5.847 |
| sweep-fix sanity d5 b20, s14001 | `817df444` | 300 | 291 (97.0%) | — | 5.817 |
| suite-shaped rows d5 b20, s2002 / s3003 | `87a8526e` | 150 + 150 | — | — | 5.853 / 5.680 |
| **latest paired A/B, `fix` arm (= HEAD behaviour)**, d5 b20, profile attached, s50001-53001 | `dfa5a12e` | 1200 | not recorded | — | **5.748** (held-out 5.723) |

Depth is monotonic (d0 << d3 <= d5). d0 is very weak because the greedy rollout plays this deck badly.
Cost at d5/b20 is ~12.3 s/game against a 3.1 s budget (3.98x): 68% of that is clairvoyant London
bottoming, and play alone is ~1.0x.

### 4. Verification (Stage 5)
- **Final `verify_deck`** (main-tree WIP gate script, `logs/prevent_damage/final/verify.log`):
  PASS for coverage (26 full), viewer, viewer_wiring (`sacrifice`), mismatch (7001/7002 x 60),
  play_invariants (8 games / 176 decisions), claude_sweep (0 unresolved) and **discard_policy**
  (authored buckets; its one disclosure is that `## Discard policy` gets written on your approval).
  **card_costs**: FAILED in the gate run only because it did not run (rc=2, Scryfall 429 on 96 fleet
  cards). **cost audit re-run: PASS**: `audit_card_costs.py --throttle 0.6` rc=0, "348 actually
  compared. All 348 compared mana costs match Scryfall"
  (`logs/prevent_damage/final/costaudit.log`). FAIL:
  **card_fields**: the pre-existing `Basri, Tomorrow's Champion` `exert` keyword, not in this deck.
  **regression_tiers**: the deck is in no tier. The WIP script has no such gate, but the branch's
  `verify_deck.py` fails it (Stage 5 record), which is blocked by 5j. Overall **GATE FAIL** until
  these are fixed or signed off in `## Approved deferrals`.
- **5a mismatch harnesses:** 0 `[nonconv]`, 0 `[fd-diverge]` (2 x 250 games, before and after the
  fixes). **0 real own deaths in 1,000 games.**
- **5b depth sweep:** monotonic (table above). All 9 unwon d5 games were read. One misplay (g196, a
  second Vito cast into the legend rule for 7 life) was **fixed** (`MTG_PD_DUP_LEGEND`). The rest are
  keep quality, mana screw or flood.
- **5c budget starvation:** mild. About 4 of 300 games speed up by b200, and all unwon games stay
  unwon at b1000. The iterative-deepening ladder commits at **depth 1 on 95 of 141 decisions**, so
  "d5 b20" is effectively d1-d2 for this deck.
- **5c2 horizon-honest tie-break:** keep ON (-0.008 turns/game, 55 worse / 68 better of 2,000). This
  run found and fixed a parser bug (`09a12ad2`) that makes **Melira Pod's recorded "NO SIGN" void**.
- **Stage 5 fixes (`6d3f212c`):** suicide guard (`MTG_PD_SELF_LETHAL_GUARD`: own deaths 2 -> 0 per
  600), duplicate-legend prune, and play_invariants harness gaps (`plans_hidden`), which any
  Wish/pod/Chord deck would also have tripped.
- **5d claude-play sweep:** 16 Opus players, s31001 gi 0-15, run on `ef26b03b`. 15 result lines
  (gi0 recorded frames only). **Every CONFIRMED flag was fixed** (`d4c38ed4`, `817df444`):
  - payment ignored pain when choosing sources, including one lethal Dina payment (gi9);
  - the pain sweep ran after every plan in human play;
  - the viewer plan cap dropped GSZ;
  - there was an extra frame after own death;
  - GSZ offered no fail-to-find;
  - labels were wrong: Beseech showed `{6}` and Earthquake had no X.

  gi13 is b20 starvation and gi15 is cast order (D9). Win turns on the 14 games both sides won:
  search 5.50, Claude 5.36. Claude was faster on 3 games (gi13, gi15, and gi11 where the AI never
  won) and slower on 2 (gi3, gi7), which was before the gi7 fix. **The record is STALE:** the sweep
  ran on `ef26b03b`, before the fixes and the §5i policy, and nothing tracks PD play by digest.
- **5h viewer surface:** `audit_viewer_decisions.py --sideboard` PASS. dig / sacrifice / target /
  transmute were verified by targeted seed search. Bilbo's put multi-pick was never exercised (0 real
  activations). Auto-resolved items are listed in 6a (V-rows).
- **§5i discard policy:** authored and default ON, PROVISIONAL. The policy alone vs OFF is +0.0025
  (t=+1.00, n=1200), i.e. neutral with every point estimate leaning slightly worse.
  `MTG_PD_SHED_UNPLAYED_LAND` cuts T1 skip-then-shed from 6 to 1.
- **Smoke:** 101/101 byte-identical, play-changed 0, after every engine commit. PD is not in the suite,
  so this only shows that other decks are unchanged.

### 5. Stage 6a — encoded heuristics & assumptions disclosure

**Routing:** `DetectDecisionProvider` returns `g_prevent_damage` FIRST, above every other branch. It
fires on Tamanoa ∨ Manabarbs ∨ Vito ∨ Dina params (not Faithmender/Bilbo). Without it the list would
ride AntiLifegain (Wish/Beseech `tutor_to_hand`) and trip `goblin` via Dina's outlet. It derives from
`DeckProvider`, and **every hook below is an override**. ⚠ The class comment in `DecisionProviders.h`
lists only 4 "hooks held"; the code overrides **13** (it has gone stale).

| # | assumption / heuristic | source | classification | cost / why safe (⚠ = worth your attention) |
|---|---|---|---|---|
| G1 | Passive goldfish opponent: never blocks, casts, taps lands, gains or prevents life | engine | global | Makes Fear/Flying/Deathtouch, the "reveal" halves and Manabarbs' opponent half inert |
| G2 | Clairvoyant search over a known (deterministically shuffled) library; London bottoming plays every legal removal to the end clairvoyantly | engine | global | ⚠ Bottoming is 68% of CPU, and it is an oracle on the true draws (P2: every cheaper bottomer is strictly worse) |
| G3 | **First main only.** `MTG_PD_SECOND_MAIN` defaults **OFF** (`ef26b03b`) and nothing else in the list sets `DeckUsesSecondMain` | `GoldFishRunner.cpp:122` | pruning (phase) | Measured neutral: -0.0033, t -0.23, n=600, at 0.63x CPU. ⚠ **Cannot express "attack, then ping/quake".** Vito is a 1/1, so a pre-combat Pyrohemia ping kills the lifelink attacker. Pyrohemia's bracket note justifies dropping the end-step window as "identical to a ping in main 2", **which no longer exists** |
| G4 | Activations trail casts within a main (`apply_trailing_activations`) | engine | pruning (order) | ⚠ With m2 OFF, "ping, then cast a creature" is inexpressible within the turn |
| G5 | Depth/budget: no `value_play`, so the built-in d5/b20 | profile | global | Starved: commits at depth 1 on 95 of 141 decisions. Mild starvation on ~4/300 games (5c, gi13) |
| G6 | Damage-event model + own death **gated to armed decks**. Every other deck still has no mid-turn own-death check | `StampDeckTraits` | global scope | Byte-identical elsewhere. Un-gating is D5 |
| G7 | Trigger order auto-taken: all gains, then each Manabarbs hit; Bilbo +1 before Faithmender x2 | `DamageEvents.h` | correctness shortcut | Weakly dominant (CR 603.3b / 616.1) |
| G8 | City of Brass and Purity TRIGGERS modelled as immediate damage / replacement | cards.json | card simplification | No observable window (no stack) |
| G9 | Baseline keep: `hand_score_threshold -1e18`, `min_color_sources {}`, `min_lands 1` | profile | heuristic (keep) | ⚠ It keeps colourless and Pool-only hands, the single largest unwon class. It is waiting on the mulligan stage, which 5j blocks |
| G10 | A hand Beseech is read at MV 6 by keep-model / land-play heuristics | cards.json | card simplification | Pessimistic only |
| P1 | `Certificate` NotAssessed | provider | none | No effect (P7: a certificate cannot move the budgeted 5j number) |
| P2 | `SelfDamageUseful` = a gain engine (Tamanoa/Purity) is out. It drives damaging-mode taps for generic pips and the end-of-main **pain sweep** (`TapPainSourcesIfUseful`) | provider + engine | correctness shortcut (**greedy mana policy**) | Allowed by the greedy-scope ruling. The engine bounds it by rules safety. Not A/B'd on its own (D12) |
| P3 | `ManaSourceRank`: with no gain engine, pain pushes a source back 5 per point | provider | ordering (cost lever) | `PainAwarePay` enforces the exact minimum anyway, so this only saves retries |
| P4 | `MTG_PD_PAIN_PAY` (ON): pain-aware source assignment per payment attempt. It is never lethal if a survivable assignment exists. With no gain engine it takes the exact minimum damage (DP); with one, damaging mode first | `DamageEvents.h` | correctness shortcut | +12% CPU, quality ON slightly better, 0 own deaths either way. Does not prefer a painful source for a COLOURED pip or schedule taps "after Manabarbs" (gi6, D3) |
| P5 | `GuardsSelfLethalPayment` (`MTG_PD_SELF_LETHAL_GUARD`, ON): drops CAST plans whose first land tap under Manabarbs kills us, and Earthquake X >= our life | provider | pruning (rules-derived) | Own deaths 2 -> 0. ⚠ **Life is read at ENUMERATION**, so a gain earlier in the same plan is not credited: a Tamanoa-then-big-quake line can be pruned. It also misses Pyrohemia's `{R}` barb at life >= 2 and a land's own pain (both unguarded, so a line is over-offered, never lost) |
| P6 | `OfferDuplicateLegendCast` (`MTG_PD_DUP_LEGEND`, ON): a 2nd Vito/Dina only when Spellshock + a gain engine are on board or affordable in hand, or the base upside helper says so | provider | pruning | Neutral (+0.0017, t 0.45). It misses nothing measurable: the duplicate dies to the legend rule |
| P7 | `TutorSearchWidth 20` | provider | coverage (not a narrowing) | All 19 library names + 13 sideboard names are reachable |
| P8 | `TutorCandidates`: nonlands first; **Living Wish re-ranked by the user's engine-role doctrine** (`MTG_PD_WISH_RANK`, ON since 2026-09-29: Tamanoa / Vito first by board state, Dina / Rhox backups, 2nd Tamanoa over Rhox unless {R}{G}{W} is tight, lands (colour-fixer first), the rest). Beseech keeps nonlands-first; GSZ has no consumer of this list | provider | ordering | Every name is still a searched variant. It sets the **rollout's / unpinned** Wish/Beseech pick, the 2nd tutor of a plan, and the human-play label/badge/grid. Wish ranking measured -0.0217 t/game (t -2.51, n=600). See "## Living Wish ranking" |
| P9 | `XCandidates` (Earthquake): drops X=0 unless Spellshock is out (plus P5's X cap) | provider | pruning | X=0 without Spellshock deals nothing, triggers nothing and costs a card: dominated, lossless. Human play is unpruned |
| P10 | `OfferFailToFindPut` (GSZ X=0 fail-to-find): only under Spellshock + a gain engine | provider | pruning (of an added option) | Without both, the cast only hurts us. Human play gets every X |
| P11 | `FodderSacUseful` (`MTG_PD_DINA_LETHAL_GATE`, ON): Dina sacrifices only when she can attack and an optimistic bound says THIS turn is lethal | provider | pruning (judgement) | Measured +0.040, t 2.85 (41 worse / 17 better with it off). ⚠ It misses a non-lethal set-up sac and a Purity-shuffle sac. Without it the search fed Tamanoa to Dina 28 times |
| P12 | `EtbDestroyTargetPick` (Shriekmaw/Slime): opponent creature first; never an engine piece or Manabarbs while anything else is legal; then tapped land, lowest MV | provider | correctness shortcut (resolution pick, not searched) | 0 real firings at n=200. ⚠ **It never lets Slime kill our own Manabarbs while a land is legal**, which could be right at low life with no gain engine |
| P13 | `CleanupDiscardCandidates`: the §5i bucket ladder (`MTG_PD_BUCKET_DISCARD`, ON) + `MTG_PD_SHED_UNPLAYED_LAND` (ON) | provider | correctness shortcut (index 0 = the shed; no searched discard width) | ⚠ It fires ~37,000x more in rollouts than in real play (idx0 differs from generic on 85.7% of 1.84M calls), so it shapes leaf values. Policy vs OFF: neutral, leaning worse (D7) |
| P14 | `NoWinLeafPricesOwnLife` (`MTG_PD_LEAF_OWN_LIFE`) | provider | default **OFF** | Measured neutral; inert |
| B1 | Inherited base rankings, **not searched**: `GyReturnToHandPick` (Witness unpinned: nonland, highest MV), `PutCreaturesFromLibraryPicks` (Bilbo: all creatures except a 2nd legend and self-destroying ETBs) | `DecisionProvider.h` | correctness shortcut | Reviewed, not measured. 0 real firings |
| E1 | Pyrohemia K = full 1..max; Earthquake X = 0..max (minus P9); `PingAllSelfSafe` never offers a self-lethal ping | engine | full search (+ rules prune) | Stage 5f narrowings were NOT pursued: under a binding budget they are quality levers, not cost levers |

**Card-modelling simplifications (bracket notes, verbatim gist; each is PROVISIONAL until D10):**
- **Inert:**
  - Manabarbs: `[PARTIAL: 'a player' -- only OUR taps are modelled ...]` and `[PARTIAL: ... tap-ahead helper ... flushed only at the next decision-boundary backstop ... none of those paths is reachable in this deck.]`
  - Beseech / Living Wish: `'reveal'`.
  - Spellshock: `[PARTIAL: the trigger resolves at cast time, ahead of the spell]`.
  - Purity: `[PARTIAL: modelled as a replacement where the card has a TRIGGER]`.
  - Rolling Earthquake: `'without horsemanship'`.
  - Pyrohemia: `[PARTIAL: ... instant-speed ... enumerated only in our main phases ... a ping there is identical to one in main 2.]`. ⚠ See G3.
  - Vexing Shusher: D1/D2, pre-existing.
  - Fear (House Guard, Shriekmaw), Flying (Purity), Deathtouch (Slime).
- **NOT inert:** **Dimir House Guard** `[PARTIAL -- PROVISIONAL DEFERRAL ... 'Sacrifice a creature: Regenerate this creature.' is NOT modelled. It is not inert here -- our OWN Pyrohemia, Rolling Earthquake and Shriekmaw can destroy this creature ...]`.

**Play-viewer auto-resolved decisions:**
- **Surfaced:** Earthquake X; Pyrohemia K (folded to 1, then re-prompt); GSZ every X incl. fail-to-find; Beseech twobrid k; Wish/Beseech names; Dina victim (`sacrifice`); Shriekmaw/Slime `target`; Witness `dig`; transmute chooser; Bilbo multi-pick (preselected, never exercised).
- **Auto-resolved:**
  - (V1) legend rule keeps the OLDEST, with no keep decision. This is weakly dominant: the older copy is not summoning-sick.
  - (V2) trigger/replacement order (G7), dominant.
  - (V3) the **end-of-main pain sweep** runs automatically in human play. This is a viewer-surfacing follow-up.
  - (V4) mana source assignment follows the pain-aware payer, as every deck's payment is automatic.
  - (V5) forced single targets are not prompted.
  - (V6) the Pyrohemia end-step window is not offered.
- **Gate coverage:** `verify_deck`'s viewer gate audits the MAIN zone only. The `--sideboard` pass was run by hand in Stage 5 (PASS).

### 6. Accepted deferrals
**None accepted yet.** Awaiting sign-off: the Dimir House Guard regeneration deferral (not inert), plus
the inert PARTIALs above (D10).

### 7. Suggested next steps
1. Settle D1 (the 5j route). Then run the mulligan (keep/bottom) table followed by the value leaf,
   strictly serially, AFTER D7 (both fit to the rollout policy §5i changes). Then add the three tiers +
   GT from P9 (`pd`, `pd2hg`).
2. Re-run the claude-play sweep on HEAD: the record predates every sweep fix and §5i. Then
   `verify_deck` again (card_costs now passes on the throttled re-run).
3. User-reviewed cast-order pass: tutor/assemble the engine before the sweeper (gi15; miner 16/0,
   7/0); hold Rolling Earthquake / a second Manabarbs without a gain engine. Re-mine with a second
   seed first: only one partial seed exists.
4. Investigate the T1 land-skip tie (no plan wins inside the horizon, so land and no-land plans tie;
   `MTG_LEAF_TB_PERMS` alone does not break it).
5. Re-run Melira Pod's 5c2 (void) and any other space-in-stem deck's.
6. A 5i rollout-quality digest (`MTG_DIVERGENCE_LOG`): d0 wins 31.7%, and both Stage 5 misplays
   were rollout-policy failures.

### Decisions awaiting the user (merged from Open questions, §12, the sweep, P8 and §5i; default taken in brackets)

| # | question | default taken | recommendation |
|---|---|---|---|
| D1 | **5j route (P8 options):** (1) lift the gate for PD (`MTG_ALLOW_UNTESTED_DECK=1`, user-only) to build the keep table + value leaf; (2) rule that 5j measures POST-artifact cost, as every reference deck is measured; (3) ship `bottom_eval_depth 0, topk 5` (0.67x, +0.013 t/game, still 2.6x over) | [nothing adopted; deck in no tier] | **(2), using (1) as its mechanism.** Not (3): it costs quality and still fails |
| D2 | `MTG_PD_SECOND_MAIN` default OFF | [OFF, PROVISIONAL] | Confirm OFF (neutral at 0.63x CPU). Note G3/G4: the post-combat lines it removes |
| D3 | `MTG_PD_PAIN_PAY` default ON (+12% CPU). Also: prefer painful sources for coloured pips / tap "after Manabarbs"? | [ON; not extended] | Adopt ON. The coloured-pip refinement is a low-priority follow-up |
| D4 | Extend pain-aware payment to other painland decks (EDF, Angels, ...)? | [armed decks only] | Measure first; it would move their GT |
| D5 | Make own death mid-turn a GLOBAL rule (`MTG_OWN_DEATH_ALL`)? | [gated to armed decks] | Measure over all tiers before deciding |
| D6 | Adopt the measured default-ON levers `MTG_PD_DINA_LETHAL_GATE`, `MTG_PD_SELF_LETHAL_GUARD`, `MTG_PD_DUP_LEGEND` | [ON, PROVISIONAL] | Adopt. Note P5's enumeration-time life read and P11's lethal-only scope |
| D7 | **§5i discard policy** default ON (neutral, leaning worse), plus `MTG_PD_SHED_UNPLAYED_LAND` ON. Review items: FUEL placement; Pyrohemia last among fuel; land target 5/6 with a sink; colour cover before Ancient Tomb; Pool as the worst live land; DRAIN backup below AMP; tutor order Wish > GSZ > Beseech, plus a far-Beseech exception (gi0); no painful-land preference; prefix stability untested; decide BEFORE the keep table / value leaf | [both ON, PROVISIONAL] | Your review (the doctrine is user-owned). On approval, write `## Discard policy` here (the gate asks for it) |
| D8 | T1 land-skip search tie: schedule the development-aware no-win tie-break investigation (option c)? | [not started] | Yes, after D1 |
| D9 | Cast order (user-owned): engine-before-sweeper (gi15), hold rules for Earthquake / a 2nd Manabarbs | [not encoded] | Review after a second mining seed |
| D10 | Sign off deferrals: **Dimir House Guard regeneration (NOT inert)**; the inert PARTIALs (Manabarbs opponent half and tap-ahead flush, reveal halves, Spellshock/Purity/City of Brass trigger collapse, horsemanship, Pyrohemia instant speed, Fear/Flying/Deathtouch) | [PROVISIONAL] | Approve. The House Guard shield is never worth a Tamanoa/Faithmender, and the card is never wished for |
| D11 | Unsearched rankings: `EtbDestroyTargetPick` (never kills our Manabarbs while a land is legal), `GyReturnToHandPick`, `PutCreaturesFromLibraryPicks`, nonland-first tutor order + width 20: keep, or full-search? | [keep] | Keep (0 real firings). Revisit if Slime/Shriekmaw ever fire |
| D12 | Self-damage greedy mana policy (`SelfDamageUseful` + end-of-main pain sweep): acceptable under the greedy-scope ruling? | [kept] | Accept. A/B the sweep if you want it measured on its own |
| D13 | Anti-Lifegain's drip sweep has the same human-play defect (fires after every plan). Fix it? | [not changed] | Fix: viewer-only, no autonomous play change |
| D14 | Viewer follow-ups: a legend-rule keep decision (V1); surface the pain sweep (V3) | [auto-resolved] | Low priority: both are dominant/automatic |
| D15 | Basri, Tomorrow's Champion `exert` card_fields mismatch (another deck's) | [untouched] | Fix in that deck's pass |
| D17 | **Keep rule (2026-09-29 overnight, "Cost-gate work, round 2" O2):** `curve_check none`, `min_lands 2`, `{R:1,G:1}`, `required_pieces` [Tamanoa, Vito, Rhox, Wish, GSZ], bottoming rollouts at `bottom_eval_budget_ms 1`. Better on every suite row (-0.07..-0.16), cost 3.8x -> 1.54x | [ADOPTED in the profile, PROVISIONAL] | Keep. Stricter lists score better only with the clairvoyant bottomer (blind re-run); `max_lands 4` buys ~-0.05 more for +20% CPU -- your call |
| D16 | Push `prevent-damage-analysis` (local only; smoke byte-identical on every engine commit)? | [not pushed] | Push (the cost audit is now clean). Watch Windows CI |

## Approved deferrals

- `regression_tiers:too_slow` and `suite:not_a_case` -- USER, 2026-09-28: *"Naturally it should not have
  the new deck in the regression test until it is optimized, but we can continue that when I get to it."*
  The deck stays OUT of all three tiers until the performance work (D1) brings it inside the 3x cost
  rule; the route for that optimisation (D1 options) is still open for the user's review.
  **RESOLVED 2026-10-08:** the deck is inside the 3x rule (see "## Suite admission" at the end) and is now
  in all three tiers with accepted GT. D1 is moot.

## Living Wish ranking (user doctrine 2026-09-29)

**USER, 2026-09-29:** *"our heuristics for Living Wish were particularly poor. Typically Tamanoa or
Vito are the best bet. In less common cases Dina or Rhox Faithmender are the backups. In goldfish
nothing else is relevant. Note that multiple Tamanoa is similar, but better than Tamanoa + Rhox
because it works better with Dina (two instances of lifegain). The only case Rhox is better is when
the right types of coloured mana are tight."*

**The defect.** `PreventDamageProvider::TutorCandidates` returned the sideboard in zone order, nonlands
first, so **Rhox Faithmender was always first**. The list's order is read by:
- the tutor axis's **base target**. `TurnSolver`'s post-dedup axis puts `cands[0]` on the base plan
  and adds one variant per other name (width 20).
- the **second tutor of a plan**. Only one tutor per variant is varied, so the other keeps the front.
- **every rollout / greedy resolution** below the root. `PerformTutor` falls back to
  `LiveTutorCandidates().front()` when no target is baked.
- **human play**: the plan label, the tutor chooser's grid order, and its badge. The badge is
  re-asked under `HumanPlaySuppress`.

Green Sun's Zenith does **not** consume this list. The Chord-class enumerator walks the library and
bakes every green target into its own variant. A trial `MTG_PD_GSZ_RANK` lever was **byte-identical
to OFF** on all 600 games of both seeds (digests equal), so it was deleted. Beseech the Queen is left
nonlands-first, because the doctrine says nothing about it.

**The ranking** (`PdRankEngineTutor`). Roles are read from params: GAIN `noncreature_damage_lifegain`
(Purity also counts as a gain already in reach), VITO `lifegain_target_opp_loses_that_much`, DINA
`lifegain_each_opp_loses`, AMP `lifegain_multiplier`. "Have" means on our battlefield OR in hand.
- **No gain engine:** Tamanoa, Vito, Dina, Rhox.
- **Gain engine but no drain:** Vito, Dina, Tamanoa, Rhox. Dina goes first when next turn's mana
  (board plus the best land in hand) is exactly 2.
- **Gain engine and a drain:** Tamanoa, Rhox, Dina, Vito. **Rhox goes first** only when no three
  DISTINCT lands (board + hand) cover {R}{G}{W} but {3}{W} is coverable. That is the doctrine's
  exception.
- **Legends already held:** a Vito or Dina already had sinks to the end of the engine group.
- **After the engine group:** lands, with a land that makes a colour the hand's engine cards (plus the
  top pick) need and no land of ours makes placed first. The user wished for Battlefield Forge twice
  for fixing. Then everything else, in zone order.

Names are used only to lay out the fixed sideboard in the tests. The ranking is `MTG_PD_WISH_RANK`
(`EnvOn(..., true)`) plus heurarm slot `PD_WISH_RANK`, and `=0` restores the old order exactly.

**Unit tests** (`test/unit/test_prevent_damage_wish.cpp`, 8 cases):
- one board per branch: W1 no-gain, W2 gain/no-drain, W2b 2-mana Dina, W3 second Tamanoa, W4 tight
  RGW gives Rhox, W5 legend duplicate sinks, W6 land fixer order;
- every board pins the OFF order, which is the control that must differ;
- a hatch case checks that `=0` gives the exact old list;
- Beseech and GSZ are lever-invariant.

Also run: full `mtg-test` green and `test/scenarios.sh` 117/117.

**Human-play replay.** Reordering the Wish grid would re-point every recorded `tutor_etb` index,
because the checker replayed them verbatim. `test/viewer_protocol_check.py` now **re-anchors a
recorded tutor pick by candidate NAME** when the name at the recorded index differs. That mirrors the
existing `options` re-anchor; fetching is by name, so this is the faithful replay. Checked on the
base binary: every non-PD reference classifies identically to the pre-change sweep.

**Paired A/B.** Play settings (d5 b20, profile attached; the profile has no `value_play`). ONE pooled
batch, 20 threads, heurarm flags per job. Held-out seeds **60001 / 61001 x 300**, disjoint from every
prior PD base. Heartbeat 20/20.

| arm | s60001 | s61001 | LP (n=600) | won | CPU |
|---|---|---|---|---|---|
| OFF (both levers 0) | 5.8200 | 5.7933 | 5.8067 | 582 | 6,305 s |
| **Wish ranking ON** | 5.7967 | 5.7733 | **5.7850** | 581 | 6,469 s (1.03x; the `both` arm, identical play, 1.00x) |
| GSZ ranking only | = OFF digests | = OFF digests | 5.8067 | 582 | inert, deleted |

- **Paired result, Wish ON minus OFF: delta -0.0217 turns/game, t = -2.51, 5 worse / 19 better.**
- **Real Living Wish fetches.** The Wish was cast 449 / 450 times per arm.
  - OFF fetched **Rhox 154**, Tamanoa 122, Vito 89, Brushland 60, Forge 8, Purity 6, Dina 5, other 5.
  - ON fetched **Tamanoa 217, Vito 109**, Brushland 63, Rhox 37, Forge 9, Dina 9, Purity 4, other 2.
  - So the root search was itself being pulled to Rhox by the Rhox-valued continuation.
- **Better games are the doctrine at work.** In s60001 gi8, OFF wished Rhox, Vito, Rhox, while ON
  wished Tamanoa and then Vito in one turn (the plan's 2nd Wish now gets the right front) and won a
  turn earlier. In s61001 gi1, OFF wished Rhox twice with Vito out and no gain engine, while ON wished
  Tamanoa and won a turn earlier.
- **All 5 worse games were root-caused.** Mulligans are identical in all 5. None of them loses at a
  Wish pick; each diverges before any Wish resolves:
  - s60001 gi104 and s61001 gi59 are the known **T1 land-skip tie** (D8). The perturbed rollout
    values tip a no-win tie toward skipping the T1 drop. T1 skip-then-shed happened in 3 ON games and
    2 OFF games.
  - s60001 gi119 and gi229 diverge on the T1 land choice (Forge vs Pool, Citadel vs Coliseum). Pain
    and sequencing drift follows.
  - s61001 gi10 diverges on the T2 choice (Earthquake X=2 vs GSZ).

  All five are early-turn rollout-valuation perturbations, not a mis-ranked fetch.

**Adopted default ON**: non-inferior (in fact better, t = -2.51) and doctrine-aligned.
- **Final binary:** `final_s60001` x 100 reproduces the ON arm's per-game digests exactly.
- **Smoke:** 101/101 PASS, configs changed 0, play-changed 0 (searched and d0). `check_gt_logs`
  consistent (590). PD is in no tier, so smoke only shows that other decks are unchanged.
- **Tests:** `mtg-test` green, scenarios 117/117.
- **`viewer_protocol_check.py --strict --only Prevent_Damage`** (new binary): 5 ok, 5 repaired, 0
  drift. All 10 references reproduce their recorded win turn. s3_gi2's Wish pick is re-anchored by
  name 12->4, the others are content-anchored plan indices.
- **`ref_bench --deck prevent_damage`:** the search column is **per-game identical before and after**
  (5.400; s2_gi1 still +1). The human column moved 5.800 -> 5.600 only because the user re-saved
  s10_gi9 (T7->T6) and s6_gi5 (T5->T4) mid-session (`b1538895`, `f93efa66`). It is now 1/10 short
  and 3 faster.

**s2_gi1 shortfall (ref_bench +1): NOT the Wish order; b20 starvation.** The reference hand-off
(`scripts/ref_handoff.py ... --turn 5`) starts from Tamanoa + Rhox + 4 lands, with GSZ, Wish,
Manabarbs and Pyrohemia in hand.
- At **b20 both arms** cast Pyrohemia on T5 and win on **T7**.
- At **b25 / 30 / 35 / 40 / 50 / 100 / 200 both arms** play GSZ X=3, then on T6 Wish into Vito and
  cast Vito, winning on **T6**.
- From `--turn 3`, b20 gives T7 in both arms.

So the Wish order is invariant here. The root sees Vito at every budget. The flip sits between b20
and b25 in both arms: the T5 decision commits before the GSZ-then-Wish line is priced (the 5c
starvation class, "commits at depth 1 on 95 of 141 decisions").

## Deferred: a black painland in the sideboard (USER 2026-09-29)

USER: "Realistically, I probably should have a black mana producing painland in the sideboard ... we can
sort that out later." A cast Living Wish already proves a green source, so the colours a fetched land
must add are W / R / B; Battlefield Forge covers R+W. Candidates: **Caves of Koilos** (W/B, implemented),
Sulfurous Springs (B/R, NOT implemented -- the standard painland shape), Llanowar Wastes (B/G,
implemented, but its G is redundant after the Wish). Likely swap: Brushland out. When adopted, the
Wish's one-land rule (MTG_PD_WISH_TRIM: Battlefield Forge by default) should take the black land when B
is the missing colour and R is not. Screen with `scripts/deck_compare.py` (shared apparatus).

## Living Wish / Green Sun's Zenith target narrowing (USER doctrine, 2026-09-29)

All paired at play settings (d5 b20, profile), held-out seeds 62001/63001/64001/65001 x 300, one pooled
batch per step; every control arm reproduced the previous step's digests EXACTLY.

| lever (default) | rule (USER's words in the code) | delta vs control | worse/better | CPU |
|---|---|---|---|---|
| `MTG_PD_WISH_ZENITH` (ON) | a held Zenith that can still find Tamanoa covers the gain role -> the Wish goes for Vito | -0.0025 (t -1.00) | 3 / 6 | ~1.00x |
| `MTG_PD_WISH_USEFUL` (ON) | the SEARCH's Wish targets: Tamanoa / Vito / Dina / Rhox + lands only (human play keeps all) | -0.0033 (t -0.82) | 7 / 11 | 0.88x |
| `MTG_PD_WISH_TRIM` (ON) | lands go when colours are covered + a land for this turn and next; ONE land at most, Battlefield Forge first (the Wish's own {G} proves green); a held Vito / Dina (hand OR battlefield) goes; Rhox goes when Tamanoa is fetchable and easy ({R}{G}{W} from distinct lands, no Ancient Tomb); Rhox ranks over a 2nd Tamanoa with an Ancient Tomb | -0.0008 vs the full list (t -0.16) | 14 / 16 | 0.84x |
| `MTG_PD_WISH_VITO_OVER_DINA` (**OFF**, measurement) | 1v1: no Dina while Vito is fetchable and not held | +0.0033 vs trim (t +0.59) | 20 / 17 | 0.80x |
| `MTG_PD_ZENITH_SKIP_DINA` (ON) | Zenith never fetches Dina while one is ours on the battlefield, or in hand and castable ({B}+{G} from distinct lands incl. one drop). New `PutPolicy::deck_bits` | -0.0025 (t -0.69) | 8 / 11 | 0.95x |

Stack (defaults) vs the full-list search: quality-neutral-or-better at ~0.80x CPU. Wish fetches with the
trim: Tamanoa 324, Vito 309, Rhox 159, Battlefield Forge 109, Dina 16, Brushland 3. NOTE for review:
the Ancient Tomb ordering roughly DOUBLED Rhox fetches (87 -> 159) -- quality-neutral, but a larger swing
than the doctrine may intend. VITO_OVER_DINA left OFF: slightly worse point estimate for 4% CPU -- USER call.
Unit tests: test/unit/test_prevent_damage_wish.cpp W1-W15, Z1-Z2 (each with a control that must differ).
Logs: logs/pd_wish/{abz,abu,abv,abzsd}.

## Cost-gate work, round 2 (2026-09-29 overnight, autonomous) -- 3.8x -> 1.54x, quality BETTER

USER: "continue a bit overnight ... optimize more ... so that this deck can be added to the regression
test." All A/Bs one pooled `mtg --batch` each at play settings (d5 b20 unless stated), fresh seeds per
round, paired per game (loss = 9). Scratch: `logs/pd_perf2/` (gitignored; `pair.py` there).

### O1. `MTG_BOTTOM_NAME_DEDUPE` -- ADOPTED default ON (`9ed41927`), lossless
The clairvoyant bottomer rolled out one full game per PHYSICAL card (count 1) or per removal mask
(count >= 2). Two copies of a name are the same removal, and this deck is mostly 4-ofs. Now one rollout
per distinct NAME / name-multiset; twins reuse the score. s66001+s67001 x300: **all 1,200 per-game
digests identical**, 249 rollouts reused (the equality has power), bottoming CPU -12% / -16%.
Smoke 101/101 byte-identical, play-changed 0 (fleet-wide default; no other deck moved).

### O2. The keep rule -- ADOPTED in the profile (PROVISIONAL, USER review: keep doctrine is yours)
Stage 4's baseline `curve_check two_drop` mulligans any 7 with no spell of MV <= 2 (only Living Wish,
Rolling Earthquake and GSZ qualify in the main), so Tamanoa + Manabarbs + lands went back -- and every
mulligan pays a clairvoyant bottoming (the 68% share). Rounds (each vs the arm named):

| round (seeds) | arm | delta | t | worse/better | CPU |
|---|---|---|---|---|---|
| 1 (68/69001) vs base | `curve_check none` | +0.162 | +4.65 | 72/31 | 0.43x |
| | none + `{R:1}` | +0.077 | +2.28 | 62/46 | 0.69x |
| | none + `{R:1}` + `min_lands 2` (ncR2) | -0.010 | -0.38 | 38/43 | 0.86x |
| 2 (70/71001) vs base | ncR2 | -0.060 | -2.04 | 38/53 | 0.75x |
| | ncR2 with `{R:1,G:1}` (ncRG2) | -0.085 | -2.72 | 38/61 | 0.84x |
| | ncR2 + `stop_at 5` / `6` | = ncR2 / +0.025 | | | 0.95x / 0.87x of ncR2 |
| | ncR2 + `bottom_eval_budget_ms` 10 / 5 | -0.002 vs ncR2 | | 2/3 | 0.74x / 0.65x of ncR2 |
| 3 (72/73001) | ncRG2 + budget 5 / 2 / 1 / depth3-b5 | IDENTICAL games to each other | | | 1.00 / 0.93 / 0.86 / 0.99 |
| 4 (80/81001) vs cand | + `required_pieces` [Tamanoa, Vito, Rhox, Wish, GSZ, Beseech] | -0.063 | -3.66 | 3/21 | 1.02x |
| | + `max_lands 4` | -0.052 | -2.19 | 25/39 | 1.21x |
| 5 (82/83001 + 2HG 84001) vs cand | required, **without Beseech** (rqNB) | -0.070 / 2HG -0.075 | -3.95 / -2.50 | 9/32, 2/11 | 1.11x / 1.01x |
| 6 (85/86001 + 2HG 87001) vs cand | rqNB | -0.067 / 2HG -0.090 | -3.29 / -2.70 | 16/40, 2/12 | 1.01x / 1.04x |
| | [Tamanoa, Vito, Wish, GSZ] | -0.092 / 2HG -0.130 | -3.81 / -3.39 | 24/53 | 1.12x / 1.04x |
| | [Tamanoa, Vito, Wish] | -0.105 / 2HG -0.170 | -3.52 / -3.74 | 47/82 | 1.27x / 1.08x |
| 6 again, **BLIND bottoming** (`MTG_NC_BLIND_BOTTOM=1`) | rqNB | **-0.048** | -2.47 | 15/35 | 1.15x |
| | [Tamanoa, Vito, Wish, GSZ] | -0.058 | -2.32 | 25/47 | 1.26x |
| | [Tamanoa, Vito, Wish] | -0.037 | -1.13 | 54/70 | 1.59x |

The blind re-run matters: the clairvoyant bottomer credits every EXTRA mulligan with an oracle bottom,
so stricter keep rules look better than they are. Without clairvoyance the stricter lists lose most of
their edge (and cost more), while rqNB keeps a clear gain. **Shipped (`cand` + rqNB):**
`curve_check none`, `min_lands 2`, `min_color_sources {R:1, G:1}`, `required_pieces` [Tamanoa, Vito,
Rhox Faithmender, Living Wish, Green Sun's Zenith], `bottom_eval_budget_ms 1` (bottoming rollouts at
900 units/decision -- b10..b1 played identical games; the bottom CHOICE is flat in budget).

### O3. `bottom_eval_units` / `MTG_BOTTOM_EVAL_UNITS` -- built, NOT adopted (default unset = byte-identical)
Below 1 virtual ms (s77001+s78001 x300, 2HG s79001 x150, vs 900 units): 600 +0.002 (1/0), 300 +0.005
(3/0), 150 +0.003 (2/0) at 0.83x, 50 +0.020 (9/0) at 0.76x; 2HG similar. Losses ONE-directional (the
oracle degrading). Even 50 units leaves 2HG d5 at ~3.4 s -- it cannot close the gate, so no quality was
spent on it. Kept as a per-deck profile key for the value-leaf / keep-table era.

### O4. Where the cost is now (probe, d5 b20 s2002 x150, shipped keep rule minus rqNB)
Bottoming rollouts ON vs `MTG_BOTTOM_ROLLOUTS=0`: 322 vs 209 user CPU-s, 3,867 vs 2,500 ms/game -> the
bottoming share is now **~35%** (was 68%); PLAY alone is ~2.5 s/game d5 (0.8x the budget), 2HG longer.
(`[bottom-cost]` reports summed WALL across threads, inflated by SMT contention -- it read 77%; compare
user CPU with the rollouts off instead.)

### O5. Gate numbers, final (one batch, base = the old profile, same binary; s2002/s3003 x150, 2HG x100)
| row | base ms/game | shipped ms/game | delta (loss-pen.) | t |
|---|---|---|---|---|
| d3 b10 | 5,208 | 3,691 | -0.067 | -1.43 |
| d5 b20 | 8,875 | 4,399 | -0.073 | -1.55 |
| 2HG d3 b10 | 5,250 | 3,947 | -0.160 | -2.03 |
| 2HG d5 b20 | 11,843 | **4,764** (worst) | -0.150 | -1.91 |
| d5 b20 held-out s88001 x300 | 8,870 | 4,693 | -0.130 | -2.81 |

**Worst row 4,764 ms/game = 1.54x the 3,100 budget (was 3.8-3.9x).** Better on every row. What is left
is ~1/3 bottoming (which the exhaustive keep/bottom table replaces outright) and horizon-rollout play
(which the value leaf replaces). No quality-neutral lever found closes the last 1.54x without those two
artifacts, and both generators are behind `MTG_ALLOW_UNTESTED_DECK=1` -- USER decision (D1, unchanged).

### O6. Play-side profile after O1-O2 (perf, Profile build, 40 x 2HG d5 b20, shipped profile)
Flat, as in P3: top self-time SolveUncached 2.9%, CollectActions 2.9%, PaymentDamageFloor 2.7%,
operator new 2.6%, BuildSimKey 2.5%, TapForCost lambda 2.1%, ReflectedColors 1.7%, ShuffleByKey 1.7%,
a string-keyed hash map 1.6%, GenericTutorList 1.6%, FlushDamageEvents 1.3%. No site worth a
strength reduction that could move a 1.54x gap; the horizon rollout (SimulateToEnd) is the play cost,
which is what the value leaf replaces. Stopped here.

### O7. Remaining cost + branching census (2026-09-29, shipped profile; units-based -- the box is CONTENDED, load ~32/24, so wall ms are pessimistic)
`MTG_DECISION_PROGRESS` (100 x d5 + 60 x 2HG d5, b20): bottoming = **45% of search units** (3,409 bot
decisions, mean 2,057 units vs the 900 cap -- the id ladder's depth-1 floor always completes); real
play 55%, 918 decisions, mean 10,095 units, **only 16% reach the 18k cap** (P3's "the budget binds, so
narrowing only redistributes" is WRONG for this deck now). Real units by turn: T1 19%, T2 19%, T3 26%,
T4 27%, T5 8%. id-depth 1 on 82% of decisions; rollouts are short (1.53 turn-steps/call); units:
rollout_step 37% + greedy_fallback (the horizon's per-turn greedy) 36% + la_cand 26%.
=> WIDTH, not depth, is the cost, and a narrower root saves cost almost proportionally (play AND bottoming).

`MTG_BF_CENSUS` (60 x d5): 62,113 decisions, 2.10M candidates, mean width 33.9, max 1,665. Mass:
**Beseech the Queen in 41%** (tutor axis x every combination of the other casts / land drop),
chosen X 23% (Rolling Earthquake 270k, GSZ 146k, Pyrohemia 67k), bp variants 17%.
`MTG_DEDUP_CENSUS`: post-apply state dups only 14.6% (1.2% from dropped casts); plan-signature dedupe
UNSOUND here (copy_FALSE 385,754). So no lossless dedupe lever.

Engine's actual picks (1,200 traced games, logs/pd_wish/abzsd): Beseech -> Earthquake 93, Vito 73, GSZ
69, Tamanoa 63, Ancient Tomb 41, Wish 25, Spellshock 22, Manabarbs 22, Citadel 16, Rhox 13, Pool 11,
Dina 11, City 8, Pyrohemia 6, Beseech 5, Karplusan 3, Forge 2, Coliseum 2, Brushland 1 (lands 84/486).
GSZ X: 3 -> 512, 2 -> 202, 0 -> 12. Earthquake X: 2 -> 522, 1 -> 429, 3..8 -> 68, 0 -> 6.

PROPOSED search-only cuts (not built; A/B pending, USER doctrine for 4): (1) Beseech lands -> ONE land
(which? it mostly fetches Ancient Tomb) ~-15% candidates; (2) drop GSZ X=0 (a clairvoyant reshuffle
in goldfish); (3) Earthquake X in {1, 2, lethal X, max} ~-5%; (4) optionally a Beseech doctrine list
like the Wish's. Also: re-measure the gate on a QUIET box (suite_gate --measure-all style).

### O8. Rolling Earthquake X doctrine (USER 2026-09-29) -- no-overkill + top-X ADOPTED default ON, PROVISIONAL

The user's rule, verbatim: *"for Rolling Earthquake X it makes no sense to do less than the toughness of
your creatures assuming you have remaining mana ... Killing them is usually bad ... if Vito dies, then he
cannot make the opponent lose life ... Naturally, if they have X life remaining, that might make sense."*
The **survival ceiling** is the largest X all of OUR creatures survive: lethal toughness minus the damage
already marked, minus 1. Indestructible creatures are ignored. Three levers:

| Lever | What it does | Status |
|---|---|---|
| `MTG_PD_QUAKE_NO_OVERKILL` (provider `XCandidates`) | No X above the ceiling, except the smallest X that is lethal to the opponent by itself (X x heads >= their life) | **ADOPTED default ON**, PROVISIONAL |
| `MTG_PD_QUAKE_TOP_X` (`EnumeratePlans` post-dedupe) | Among plans IDENTICAL except for the quake's X, keep only the largest X at or below the ceiling. The ceiling also counts the creatures this plan casts | **ADOPTED default ON**, PROVISIONAL |
| `MTG_PD_QUAKE_NO_UNDERSHOOT` (provider `XCandidates`) | No X below the ceiling, per card | **REFUTED**, default OFF |

**Round 1** (seeds 80001 + 81001 x300, 2HG 82001 x150; d5 b20; one pooled batch, so contention hits
every arm equally):

| Arm | Δ turns/game | t | worse / better | CPU |
|---|---|---|---|---|
| U (no undershoot) | +0.045 | +4.78 | 40 / 6 | 1.00x |
| O (no overkill) | -0.011 | -2.32 | 2 / 10 | 0.93x |
| UO (both) | +0.029 | +3.19 | 34 / 11 | 0.85x |

- **Why U is refuted.** In U's worse games the base cast **Vito + a quake in the same turn** (Vito +
  X=1 on 5 mana). X is fixed before the rest of the plan exists, so a per-card floor deletes the smaller X
  that PAYS for the other cast. That is the user's "assuming you have remaining mana" caveat. TOP_X is the
  plan-level form: a smaller X that funds another cast belongs to a different plan and survives.
- **O's 2 worse games** involve no overkill in the base, so they are search-path churn.

**Round 2** (held-out seeds 83001 + 84001 x300, 2HG 85001 x150):

| Arm | vs | Δ turns/game | t | worse / better | CPU |
|---|---|---|---|---|---|
| O | base | +0.005 | +0.85 | 13 / 9 | 0.98x |
| O + TOP_X | base | 0.000 | 0.00 | 12 / 12 | **0.94x** (2HG 0.90x) |
| O + TOP_X | O | -0.005 | -2.00 | 0 / 4 | 0.96x |

- **Chosen X** (quakes per 750 games): base 555, X>=3 in 33 of them. O+TOP_X 549, X>=3 in 13.
  Own-creature losses to our quake: 46 -> 33.
- **Two of O's 13 worse games are real counterexamples** to "never kill our creatures".
  - *Correction, 2026-09-29:* an earlier version of this section said four, counting two games where Dina
    "died". Dina is a **1/3** (as is Vito), so X=2 kills neither. The tally script used a hand-typed
    toughness table instead of `cards.json`. In those games (s83001 gi67, s84001 gi255) the arms had
    already diverged on turns 2-4, which is search churn.
  - The two real ones, s83001 gi169 and s84001 gi197, share a board: Tamanoa + Manabarbs + Spellshock
    (+ Rhox), and **no Vito or Dina**. The base quaked X=6 on T7 (killing Tamanoa), then X=8 on T8, and
    won. The O arm held X=3 twice to keep Tamanoa alive and **lost** (scored 9). X=6 is not lethal alone,
    so the lethal exception does not cover a two-quake kill.
  - Reading: without a drainer, Tamanoa only gains US life, so in a race trading it for a big X can be
    right. The user's reason ("if Vito dies he cannot make the opponent lose life") is about drainers.
- **Net:** the doctrine is right on balance. The O + TOP_X pair is quality-neutral at 0.94x CPU, which
  meets the adoption bar (neutral with upside) and is the user's rule.
- Both levers are gated on `x_damage_each_creature_and_player`, which only Rolling Earthquake carries, so
  no other deck moves (smoke). Unit tests: `test_prevent_damage_spells.cpp` "survival ceiling" and "TOP-X".

**USER QUESTION (non-blocking, default taken = strict ceiling over ALL our creatures):** should
Tamanoa count toward the ceiling only while a Vito or Dina is out (on the board or cast this plan)?
Both real counterexamples are drainer-less boards. The refinement is one more arm to measure.

## Pain deferral (USER report 2026-10-08, `references/Prevent_Damage/claude_s11_gi10.json` T5) -- `MTG_PD_PAIN_DEFER`, default ON, ADOPTED (USER 2026-10-09)

**The report.** *"See references/Prevent_Damage/claude_s11_gi10.json for an example of suboptimal mana
tapping. I had to use the manual-tap option ... My line was strictly better because the opponent lost 3
more life."* T5 (decision 15): Tamanoa + Vito out, 16 life vs 15, Rhox Faithmender ({3}{W}) cast off
Brushland x2, Grand Coliseum, Tarnished Citadel, Reflecting Pool.

**Reproduced (replay of the reference prefix, `MTG_TAPDBG`), not theorised.**

| payment | taps (`[tapdbg]`) | pain at x1 | opp after the cast | opp at T6 start | our life at T6 |
|---|---|---|---|---|---|
| auto (pain-first payer) | Brushland {W}, Brushland {G}, Coliseum {W}, Citadel {W} | 6 | 9 | 6 | 16 |
| user's manual taps | Pool {W}, Coliseum {W}, Brushland {W}, Brushland {W} | 3 | 12 | **3** | **19** |

**Root cause -- NOT pain avoidance.** The useful-mode payer (`PainAwarePay`, damaging mode first) already
took every painful mode it could: it spent MORE pain than the user. What it got wrong is WHEN: it spent the
Citadel's 3 before Rhox resolved (drains 3) and left the painless Pool, which the end-of-main-1 pain sweep
(`TapPainSourcesIfUseful`) can do nothing with. The user kept the Citadel; the sweep tapped it after Rhox:
3 damage -> Tamanoa gains 3 -> doubled to 6 -> Vito drains 6. This also answers open question 3 above:
a "painful source for a COLOURED pip" preference is moot while the sweep follows (every land is tapped
exactly once this main phase -- by a payment or by the sweep -- so which one pays changes only the timing),
and "schedule lands after Manabarbs" is the same timing question.

**The fix (`dmgev::DeferredPainPay` + `PaymentDeferralDP` + `PainDeferQuery`; full argument at the block in
`src/core/DamageEvents.h`).** Every damage event's value is monotone in the damage->lifegain->drain chain
read off params (`noncreature_damage_lifegain`, `prevent_noncombat_to_self_gain`, `lifegain_plus`,
`lifegain_multiplier`, `lifegain_target_opp_loses_that_much`, `lifegain_each_opp_loses`,
`land_tap_damage_each_player`). When the spell(s) a pain-useful payment funds will ADD to that chain (the
per-cast `PayingSpellCard`, or the whole-turn batch's casts), the payment minimises, over the sources it
taps, `Later - Now` (the sweep's value of the source on the post-resolution chain minus its value tapped
now), via an exact DP on the simple board, and holds the rest. Taken ONLY if, on the REALISED taps, it is
>= the pain-first payment in both the opponent's loss and our life (one strictly), its leftovers can make
everything the pain-first leftovers could (colour-superset matching), the whole board's pain potential
cannot kill us, the payment is in the pre-combat main of a deck with no second main (the sweep follows),
and the plan does nothing that can hit our own board first (`PlanTraits::own_board_hazard`). Otherwise the
historical payment, byte-identical. Not extended: harmful mode (the FIRST Tamanoa -- there the old payment's
lower pain is a real trade, not a dominance), Purity boards, non-simple boards.
- **Manabarbs (the user's second ask).** "Tap a land rather than a non-land source" is NOT a preference
  while the sweep follows: it taps every untapped land for its barbs anyway, so the choice is neutral.
  Under a PENDING Manabarbs (with a gain engine out) the same `Later - Now` makes held lands worth their
  post-resolution barbs, so the DP pays with the fewest land taps -- it falls out, but no list here has a
  non-land mana source, so the land-vs-non-land half is unreachable in this deck.
- **Own life.** Nothing in the list pays life; our life only matters as fuel (Spellshock, the Quake X
  ceiling, Bilbo's 111) and as the loss condition. With a gain engine out every hit nets >= 0 after its
  triggers, the Pareto check never takes less life, and the safety gate keeps every later payment and
  sweep tap survivable.

**Tests.** `test/unit/test_prevent_damage.cpp` "pain deferral" x6: the exact s11 board (lever off = 6/16,
on = Citadel held, opp -3 more, +3 life), no Vito (opp unchanged, +3 life), no amplifier (tap-for-tap
identical), post-combat (no sweep -> identical), own-board hazard (identical), casting Vito (strictly
better). Scenario `test/scenarios/pd_rhox_keeps_citadel_for_sweep.json` (19/6; lever off reproduces 16/9).

**Measurement (one pooled `--batch` per sample, both arms as a heurarm slot, same binary; held-out seeds).**

| cell | paired games | faster | slower | same-turn play changed | net turns | delta/game |
|---|---|---|---|---|---|---|
| d0 (s62001 x1000 + s82001 x4000) | 5,000 | 23 | **0** | 194 | **-29** | -0.0058 |
| d5/b20 = play settings (s52001 x1000 + s72001 x2000) | 3,000 | 12 | 7 | 151 | **-5** | -0.0017 |

Cost: d5 units/game 101,566 -> 101,387 (-0.2%). Every slower d5 game was re-run at b200 and b1000: six of
seven recover (equal turns, most with identical digests across arms -- budget line-shifts of a starved
search). The seventh, **s72434 gi433** (T5 -> T6 at b20/b200/b1000), is search-side: with the deferral in
the executor only it wins T5, with it in the search only T6; the search predicts T6 for its line and
realises T6 (no lockstep gap), it just ranks the T2 options differently under the new payment values and
misses the Wish -> Tamanoa -> Pyrohemia -> Vito + 2 pings line; from the T5 position both arms play
identically. d0 (no search) has no slower game in 5,000.

**Replay.** New recordings carry `"pain_defer": 1`; an unstamped reference replays with
`--legacy-pain-pay` (`test/viewer_protocol_check.py recording_rule_args`). All 11 Prevent_Damage
references replay with 0 drift/diverged/gap both WITH and WITHOUT the legacy flag; the full set (556) is
clean under the regression tier. The s11 reference's own manual taps pin its payment either way; the
auto-tapped replay of the same plan now reproduces the user's line (opp 3 / us 19 at T6).

**USER DECISION 2026-10-09: ADOPTED, ON.** USER: *"Yes, let's turn on Pain deferral then."* (Surfaced
2026-10-08 with ON as the default taken, including the one unrecovered searched game s72434 gi433.) It is a payment rule (the
standing mana-payment exemption), dominant per payment by construction, net faster on both cells.

## Suite admission (2026-10-08) -- the 3x rule PASSES; all three tiers + GT added

USER: "Let's take a look at optimizing Prevent Damage." Scratch: `logs/pd_opt/` (gitignored).

**The gate, measured fairly.** `test/suite_cost.json` was partly stale: Soldiers' 3,078 ms/game and
Selesnya's 1,844 were measured BEFORE their value leaf / keep table (both adopted since), and every
cached entry predates the PGO binary. So PD was measured in ONE pooled `--batch` (PGO at HEAD
`b1a5d710`, 20 threads) together with every searched regression case of all 28 decks that ship both
artifacts. The box carried ~20-25 cores of EXTERNAL load (load avg ~44), so every deck ran ~2x its cached
cost -- only the RATIOS are quoted:

| deck | worst searched case | ms/game in the batch | cached |
|---|---|---|---|
| **pd** | d5 b20 s3003 | **3,092** | -- |
| hinata (costliest both-artifact deck in the batch) | d3 s3003 | 2,328 | 594.6 |
| fivecolour | 2HG d3 s2002 | 1,690 | 1,033.5 |
| melira | d5 s2002 | 1,170 | 578.5 |
| selesnya | d3 s2002 | 318 | 1,843.6 (pre-keep-table) |
| soldiers | d3 s3003 | 173 | 3,077.8 (pre-leaf, pre-table) |

**PD = 1.33x hinata, 1.83x fivecolour -> inside 3x on either yardstick.** (Down from 3.8x on 09-29: the
keep rule, name-dedupe bottoming, the Quake / Wish / Zenith narrowing, and PGO.) Hinata reads 3.9x its
cached cost against ~2x for the rest -- two 34-41 s SLOW-GAMEs at d3 s3003; worth a look by whoever owns
it. The cached yardstick is now soldiers' stale 3,078, which inflates the 3x budget to 9,233 ms/game for
every deck that consults it (Bruna's re-admission among them) -- refresh with `suite_gate.py
--measure-all` on a quiet box.

**Rows added** (`test/regression_cases.sh`, keys `pd` / `pd2hg`): the P9 counts; overnight at the 2x gate
budgets the other decks use (d3 b20 / d5 b40). Makespans filtered to PD: smoke 15 s, regression 31 s,
overnight 104 s (~37 core-min). All three tiers clean; 556 references replay with 0 drift / 0 diverged /
0 enum-gap; 26 keys accepted, `check_gt_logs.py` consistent. `suite_gate --cost` records 3,649 ms/game
(the tier's own 25-game d5 s2002, same loaded box).

**Where the cost is now** (`MTG_TURN_CENSUS` + `MTG_BF_CENSUS` + `MTG_DEDUP_CENSUS`, 300 games d5 b20,
seeds 930000.., 20 single-threaded processes; 8,243 decisions, 32.6M units):
- **Clairvoyant London bottoming = 44.7% of units** (6,688 bottom decisions, 22/game, mean 2,176 units
  against the 900-unit budget -- the id ladder's depth-1 floor; 20% of these units are overrun-aborted
  passes the anytime rescue then commits). The exhaustive keep/bottom table replaces all of it.
- **Real play = 55.3%** (1,555 decisions, 5.2/game, mean 11,589 units, 18% over the 18k budget):
  `rollout_step` 30.5% + `greedy_fallback` 29.7% (the horizon rollout the value leaf replaces) + `la_cand`
  32.4% + `la_bp_wave` 6.1%.
- **Width: Beseech the Queen is in ~44% of all scored candidates** (`bf_action`), then Rolling Earthquake
  13%, Dina 12%, GSZ 10%. Chosen-X share 25.5% (Earthquake 97k, GSZ 80k). A T4 decision dump (s930065,
  252k units = 14x budget) shows the shape: 136 plans = 4 land drops x Beseech's twobrid variants
  (k=1/k=2) x ~18 library names.
- **Post-apply duplicates: 41-42% of scored candidates** reach a state a sibling already reached
  (`dedup_dup / dedup_seen`), 99% of them copy permutations, `nodrop`, non-bp. BUT `MTG_CAND_DEDUP`
  (skip the duplicate's rollout) is INERT here -- measured, 500 paired games d5 b20 (s95001/s96001 x200,
  2HG s97001 x100, two concurrent processes): **1 game moved (better), units -1.1%, CPU +1.8%**. The
  duplicates' rollouts are already served by the transposition table, so no lever there.

**Next (pipeline order):** claude-play re-sweep on HEAD (the recorded one predates every sweep fix and
§5i) -> value leaf -> keep table. The Beseech axis is the remaining width lever; a doctrine is the
USER's (see "Search width: the Beseech axis" below once measured).

## Search width: the Beseech axis -- `MTG_PD_BESEECH_USEFUL`, the USER's doctrine ADOPTED 2026-10-09 (default ON)

Beseech the Queen reaches every distinct library name with MV <= lands (~19), each a searched variant,
times the twobrid payment variants, at every ply: ~44% of all scored candidates. The draft applies the
Wish doctrine's SHAPE to it, by params (`PreventDamageProvider::TutorCandidates`): KEEP the engine
creatures (gain / drain / amp), the damage sources (Manabarbs, Spellshock, Rolling Earthquake,
Pyrohemia), the creature tutors (Living Wish, Green Sun's Zenith), the Ancient Tomb-shaped land, the
FUEL land (most self-damage per tap, untapped: Tarnished Citadel) and -- while a colour or next turn's
land drop is short -- ONE TEMPO land (most missing colours fixed, then most colours, untapped, least
pain). DROP another Beseech, a Vito / Dina already held, and the other lands. Human play keeps every
name. A MEASUREMENT lever until the USER rules on the doctrine.

| round (seeds; play settings + a d3 cell; one pooled batch each, both arms a heurarm slot) | paired games | delta (turns/game) | better / worse | units | CPU |
|---|---|---|---|---|---|
| 1, first cut (98001/99001 x300, 2HG 100001 x150, d3 101001 x200) | 950 | -0.0137 +/- 0.0048 | 17 / 4 | 0.79x | 0.74x |
| 2, first cut (103001/104001 x400, 2HG 105001 x200, d3 106001 x300) | 1,300 | -0.0092 +/- 0.0039 | 19 / 7 | 0.78x | 0.79x |
| 3, **land roles fixed** (107001/108001/109001 x400, 2HG 110001 x200, d3 111001 x300) | 1,700 | -0.0018 +/- 0.0035 | 11 / 6 | 0.82x | (box overloaded) |

- **Recovery (USER two-stage check) of every slower game.** Round 1: 4/4 (two at stage 1; gi235 at d8 b0;
  gi198's T5 is reproduced by neither arm at b200 / b1000 / d8 b0). Round 2: 5/7 recover; **two did not
  -- s104219 gi218 and s104261 gi260 -- and were REAL counterexamples to the first cut's land rule**: the
  exhaustive base Beseeched Reflecting Pool (the next land drop; the one-fixer slot had fallen to library
  order) and Tarnished Citadel (as FUEL: 3 self-damage per coloured tap = 3 Vito drain under Tamanoa).
  Root-caused and the ranking fixed (fuel + tempo roles; USER rule "counterexample -> fix the ranking");
  both then win T5 at d8 b0. Round 3: 5/6 recover (gi160 / gi350 / gi93 stage 1, gi226 / gi66 stage 2);
  s108346 gi345 (T5 -> unwon) is NOT the lever: the BASE also never wins at b100 / b1000 / d6 / d8 -- the
  Zenith lockstep defect below, fixed.
- OFF path: PD's three smoke rows reproduce the committed GT digests exactly in every round.
- **USER DECISION (2026-10-09):** *"I agree with that beseech idea. Engine creatures only and skip
  duplicates."* -- narrower than the draft above: the search offers Beseech ONLY the engine creatures
  (Tamanoa / Vito / Dina / Rhox Faithmender, by param role), and none we already have a copy of in hand
  or on our battlefield. Shipped as `MTG_PD_BESEECH_USEFUL` default ON (`PreventDamageProvider::
  TutorCandidates`; unit tests B1-B2 in `test/unit/test_prevent_damage_wish.cpp`). The draft's damage
  sources / tutors / land roles are deleted.

### The USER's rule, measured (2026-10-09; one pooled batch, every arm a heurarm flag set; `base` = the five-fix stack with Beseech unpruned; fresh seeds s130001/s131001 x600 d5 b20, 2HG s132001 x300, d3 b10 s133001 x400, d0 s134001 x3000; `logs/pd_opt/multi3/`)

| arm | delta vs base (all 4,900) | searched cells only (1,900) | better / worse (searched) | units |
|---|---|---|---|---|
| draft (2026-10-08) | -0.0039 +/- 0.0015 | -0.0054 | 16 / 4 | 0.82x |
| **USER: engine creatures only, skip every duplicate** | **-0.0278 +/- 0.0047** | +0.0016 (+3 turns: d3 +7, d5 0 / -2, 2HG -2) | 52 / 57 | **0.62x** |
| ...skip only a held LEGEND (Vito / Dina) | -0.0220 +/- 0.0045 | +0.0047 | 52 / 66 | 0.62x |
| ...all duplicates -> fall back to the duplicates (SHIPPED) | -0.0286 +/- 0.0047 | +0.0026 | 53 / 61 | 0.61x |

- The aggregate gain is the depth-0 runner (-0.046 t/game, 131 / 43): its greedy first pick is now an
  engine creature. Searched play is neutral within noise; work is 0.61-0.65x of the unpruned Beseech
  (the draft: 0.82x).
- "Skip duplicates" read literally (any engine creature already held) beats the legend-only reading
  (+0.0057 +/- 0.0020 for legend-only vs literal, p 0.01).
- The fallback when EVERY fetchable engine creature is a duplicate (the rule is silent): the duplicates
  (engine creatures stay the only targets) vs the whole library measured equal (-0.0008 +/- 0.0008,
  10 / 6). Shipped: the duplicates -- the narrower list. **USER review item** (default taken).

### Landing gate (2026-10-09, `4c08528f`; the five sweep fixes + the USER's Beseech rule; CI Linux + Windows + parity green)

Full smoke 114/118 and full regression 160/166 byte-identical -- only pd / pd2hg keys move; 556 references
replay clean (0 drift / diverged / enum-gap); scenarios 158/158; mtg-test 539/539. PD searched rows vs
the previous GT: overnight -16 turns (d3 -8 / 400, d5 -3 / 300, 2HG -5 / 200), regression +1, smoke 0;
every d0 row faster (~-0.04 t/game). Accepted deck-scoped with a recorded note in all three tiers.

### Recovery check of the USER's rule (two-stage, every slower searched game: multi3's 61 + the gate's 7)

| stage | recovered | of |
|---|---|---|
| 1: `--depth W --budget-ms 100` | 12 | 68 |
| 2: `--depth 8 --budget-ms 0` (the stage-1 failures) | 13 | 56 |
| **total** | **25** | **68** -- **43 do NOT recover** |

The rule does NOT meet the adoption bar's recovery clause (it is the USER's rule; reported, not
overridden). Why, from the games themselves: replaying the unrecovered games with Beseech UNPRUNED and
reading what it fetched (38 games the finisher amendment below does not fix, `logs/pd_opt/probe/`):
Rolling Earthquake 10, Ancient Tomb 10, an engine creature 7 (search-path noise), Green Sun's Zenith 5,
other lands 7, no Beseech 2. Earthquake was often fetched on T3 with nothing assembled yet (to hold the
finisher); Ancient Tomb is the deck's only acceleration. Every gate-tier slower game with an explained
line Beseeched in the old line -- most for Rolling Earthquake, cast the same turn or the next as the
kill.

### Amendment drafts (measured 2026-10-09; FIN_ALWAYS + TOMB ADOPTED by the USER the same day -- see the decisions below)

- `MTG_PD_BESEECH_FINISHER`: once a gain creature (Tamanoa) is on our battlefield and a drain creature
  (Vito / Dina) on it or in hand, the damage sources not held (Manabarbs, Spellshock, Rolling Earthquake,
  Pyrohemia) join the list after the engine creatures. Fresh seeds s140001/s141001 x600, 2HG s142001
  x300, d3 s143001 x400, d0 s144001 x3000 (`logs/pd_opt/recwish/`): **-0.0059 +/- 0.0018 overall (p 0.001);
  searched cells -0.0158 t/game (49 better / 19 worse), every searched cell better; units 1.00x.** At play
  settings it fixes 30 of the 68 slower games outright.
- `MTG_PD_WISH_LAND_CAST` (the Living Wish "always Battlefield Forge" counterexample, s61013): each
  sideboard land is scored by how many engine creatures in hand become colour-castable from DISTINCT
  lands (board + hand + it); the highest wins, Battlefield Forge keeps ties. Same batch: **0 of 4,900
  paired games changed win turn** (it fires rarely). On s61013 itself the Wish now takes Brushland (was
  Battlefield Forge), but the search casts the Wish on T3 after a T2 Ancient Tomb + Vito, so the game
  stays T6 -- the T5 line also needs the T2 Wish. Unit test W16.

**The amendments side by side** (one pooled batch, fresh seeds s150001/s151001 x600 d5 b20, 2HG s152001
x300, d3 b10 s153001 x400, d0 s154001 x3000; every arm vs Beseech UNPRUNED; `logs/pd_opt/multi4/`):

| arm | searched cells: turns vs unpruned (1,900 games) | all 4,900 | units vs unpruned |
|---|---|---|---|
| the USER's rule (shipped) | **+31 (+0.016 / game): d5 +13 / +13, d3 +6, 2HG -1** | -0.0090 +/- 0.0040 | 0.58x |
| + FINISHER (Tamanoa out + a drain had) | -4 (-0.002) | -0.0145 +/- 0.0039 | 0.59x |
| + damage sources ALWAYS (`MTG_PD_BESEECH_FIN_ALWAYS`) | **-15 (-0.008)** | -0.0165 +/- 0.0038 | 0.63x |
| + FINISHER + Ancient Tomb (`MTG_PD_BESEECH_TOMB`) | -10 (-0.005) | -0.0159 +/- 0.0038 | 0.60x |
| + FINISHER + Ancient Tomb + creature tutors (`MTG_PD_BESEECH_TUTORS`) | -11 (-0.006) | -0.0161 +/- 0.0038 | 0.64x |

- On these seeds the shipped rule is WORSE than unpruned in searched play (+0.016 t/game; multi3 had it
  neutral, +0.0016) -- better overall only through the depth-0 runner. Pooled over both batches
  (3,800 searched games): +34 turns, +0.009 t/game.
- FINISHER vs the shipped rule: -0.0055 +/- 0.0018 overall, every searched cell better (d5 -0.020
  p 0.035, d5 -0.022 p 0.001, d3 -0.023 p 0.049), units 1.00x. Replicates `recwish` (-0.0158 searched).
- ALWAYS vs FINISHER -0.0020 +/- 0.0013 (25 / 15, n.s.) for +7.8% units; Tomb vs FINISHER -0.0014 +/-
  0.0011 (n.s.); the tutors add nothing (-0.0002 vs Tomb).

**Recovery of each amendment** (two-stage, every searched game slower than UNPRUNED in the batch above;
`logs/pd_opt/rec4/`):

| arm | slower vs unpruned | stage 1 (d=W b100) | stage 2 (d8 b0) | NOT recovered |
|---|---|---|---|---|
| + FINISHER | 52 | 16 | 6 | 30 |
| + FINISHER + Ancient Tomb | 42 | 18 | 5 | 19 |
| + damage sources ALWAYS | 34 | 11 | 5 | 18 |

What unpruned Beseech fetched in ALWAYS's 18 unrecovered games: Green Sun's Zenith 6, Ancient Tomb 6,
lands 5 (Reflecting Pool 2, City of Brass 2, Tarnished Citadel 1), Tamanoa 1. So no narrowing tried
clears the recovery bar; reaching it needs roughly the 2026-10-08 draft's set (damage sources + Ancient
Tomb + the creature tutors + a fuel / tempo land) at ~0.82x units. The tutors slot measured nothing on
aggregate (-0.0002) and was deleted; FINISHER, FIN_ALWAYS and TOMB stay as default-OFF levers (unit tests
B3-B4).

**Third fresh-seed batch: damage sources ALWAYS + Ancient Tomb** (s160001/s161001 x600 d5 b20, 2HG
s162001 x300, d3 b10 s163001 x400, d0 s164001 x3000, vs UNPRUNED; `logs/pd_opt/multi5/`, recovery
`logs/pd_opt/rec5/`):

| arm | searched turns vs unpruned (1,900) | all 4,900 | units | slower | recovered (st.1 + st.2) | NOT recovered |
|---|---|---|---|---|---|---|
| the USER's rule (shipped) | **+46 (+0.024 / game; d5 p 0.003 / 0.019)** | -0.0104 | 0.59x | 89 | 16 + 8 | 65 |
| + FIN_ALWAYS | -3 (-0.002) | -0.0200 | 0.64x | 40 | 19 + 2 | 19 |
| + FIN_ALWAYS + TOMB | -9 (-0.005) | -0.0212 | 0.65x | 33 | 19 + 2 | 12 |

(s160001 gi406, a T7 base win, reaches only T8 at d8 b0 in all three arms -- the same game, digest-identical.)
FIN_ALWAYS vs the shipped rule: -0.0096 +/- 0.0020 overall, every searched cell better (d3 p 0.008,
d5 p < 0.001). TOMB on top: -0.0012 +/- 0.0008 (10 / 4) and a third fewer unrecovered games.

**Pooled verdict, three fresh-seed batches:** the shipped rule plays +80 turns over 5,700 searched games
(+0.014 t/game) slower than unpruned Beseech -- better overall only via the depth-0 runner. Engine
creatures + damage sources (+ Ancient Tomb), skip duplicates, is at least as good as unpruned in
searched play at ~0.65x units.

### Open USER decisions (Beseech / Wish, 2026-10-09; defaults taken in brackets)

1. ~~The Beseech rule's cost in searched play~~ -- **DECIDED 2026-10-09 (USER: "Okay, we could do the
   beseech rule you mentioned.")**: the doctrine is now "engine creatures, then the damage sources and an
   Ancient Tomb-shaped land; skip duplicates", folded into `MTG_PD_BESEECH_USEFUL` (default ON; the
   separate FINISHER / FIN_ALWAYS / TOMB levers are deleted -- FIN_ALWAYS subsumes FINISHER). Measured as
   the `finAT` arm above: searched -0.005 t/game vs unpruned at 0.65x units, 12 unrecovered of 33 (the
   remainder are Green Sun's Zenith and land fetches). Unit tests B1-B3. Gate + GT: see below.
2. The all-duplicates fallback: the duplicates vs the whole library measured equal. [the duplicates]
3. `MTG_PD_WISH_LAND_CAST` (the "always Battlefield Forge" counterexample): correct on s61013 (takes
   Brushland), 0 win-turn changes in 4,900 games. [OFF]
4. Twobrid `{2/B}` payment as a searched axis vs a payer choice -- with Beseech's list now 2-4 names the
   axis costs far less; recommendation: keep it searched (paying more mana with painful lands can be
   worth MORE drain, so the choice is not dominated). [searched]

### Landed (2026-10-09, `a31d01a8`)

The approved rule's gate: full smoke 114/118 and full regression 160/166 byte-identical (only pd / pd2hg
keys move), 556 references clean, mtg-test 549/549. Vs the engine-only rule's GT: overnight searched 31
faster / 11 slower (-20 turns), regression 3 faster / 1 slower. Rebased over upstream `fb831a13`
(restricted mana rank): the merged GT (upstream's keys + PD's) re-verified on the rebased binary -- smoke
118/118, PD regression 6/6, PD overnight 16/16, all byte-identical. **Cost: PD's worst regression row 65.2K
units/game vs Hinata's 61.0K (1.07x) -- with NO value leaf or keep table (Hinata has both).**

## References matched (2026-10-09; USER: "verify that the references are matched before generating any artifacts")

`scripts/ref_bench.py --deck prevent_damage` on `a31d01a8` (each hand-played reference's exact opening
hand via `--force-mulligan`, the shipped play policy): **0 / 11 short** -- the search equals the human's
win turn on 7 and is a turn faster on 4 (s3 gi2, s4 gi3, s7 gi6, s9 gi8); average 5.273 vs the human's
5.636. Artifact `logs/pd_opt/refbench/base.json`.

## Cast order and discard (USER request 2026-10-09: "I would also like to hear your suggestion for the discard heuristic and cast order")

Both are USER decisions; everything below is input to them. Every arm is a heurarm flag in ONE pooled batch
per round; fresh seeds; paired per game, losses scored 9.

| arm | what it changes vs the generic order (creatures 10, noncreatures 20, `on_cast_trigger_damage` 30) |
|---|---|
| `order` (`MTG_PD_CAST_ORDER`, full draft) | tutors first (GSZ 3, Living Wish 4, Beseech 5), then gain engine 10, Vito/Dina 11, Spellshock 12, Manabarbs 13, Rhox/amp 14, Rolling Earthquake 25, Pyrohemia 26 |
| `omin` (`MTG_PD_CAST_ORDER_MIN`, minimal) | Spellshock 30 -> 15 (after creatures, before the other noncreatures, so later spells trigger it); Rolling Earthquake 25 and Pyrohemia 26 (after the rest) |

**Round 1** (`logs/pd_opt/multi6/`, s170001+, 4,900 games): `order` +0.0016 t/game (d5 7 faster / 15
slower, d3 9 / 2). Mechanism: the rank drives the greedy rollout past the horizon, so the order changes
VALUATION, not just the cast sequence -- tutors-first makes rollouts spend T2/T3 mana on a tutor before the
engine. That is why the minimal arm keeps the generic tiers and fixes only the two clear defects.
Discard: the generic discard vs the §5i bucket policy moved 13 games, -0.0008 -- the choice barely matters.
The far-tutor discard exception (`MTG_PD_SHED_FAR_TUTOR`) fired in 2 of 4,900 games and was deleted.

**Round 2** (`logs/pd_opt/multi7/`, s180001/s181001 d5 b20 x600, 2HG s182001 x300, d3 b10 s183001 x400, d0
s184001 x3000):

| arm vs base | delta t/game | faster / slower | sign p | units |
|---|---|---|---|---|
| `omin` | **-0.0053 +/- 0.0022** (95% CI -0.0096..-0.0010) | 50 / 25 | 0.005 | 0.997x |
| `order` | +0.0006 +/- 0.0025 | 40 / 42 | 0.91 | 0.996x |

`omin` per cell: d5 s180001 -0.0067 (4/0), d5 s181001 -0.0083 (6/1), 2HG -0.0133 (5/1), d0 -0.0043
(31/19), d3 0.0000 (4/4). **Recovery (two-stage):** all 6 slower searched games recover at stage 1
(`--depth W --budget-ms 100`), so stage 2 had nothing to run (`logs/pd_opt/omin_verify/`). **References
under `omin`:** 0 / 11 short, identical win turns to base (5.273 vs the human's 5.636); one reference's
log differs (s11 gi10, same turn), so the flag is live (`logs/pd_opt/refbench/omin.json`).

**Recommendation (awaiting the USER):** adopt the minimal order (default ON, PD only); drop the full
draft; keep the §5i bucket discard policy as shipped. *(Superseded the same day: the §5i policy was
agent-authored, not the USER's; the USER then gave the discard doctrine below.)*

## Discard policy (USER doctrine, 2026-10-09) -- `MTG_PD_DISCARD_USER`, default ON

The USER, on the agent-authored §5i buckets: *"those buckets are not the greatest."* Their doctrine, verbatim:

- *"I agree with Gain and Drain, but Rhox should go under the Tamanoa bucket, but worse than the second
  Tamanoa. We should keep 2 if possible."*
- *"Tutors should be backups for missing pieces they can get and fill those places. So GSZ can be Tamanoa
  for 4 mana, which is worth keeping. Living Wish can get Vito or Dina, but is worse than just having a
  Vito in that bucket. Beseech is the worst tutor for most things, but can go in any open spot."*
- *"Typically Rolling Earthquake is the card to keep if we can keep a damage spell, but that area can be
  cut as needed for higher priority items."*
- *"For mana we should prioritize painlands and especially Tarnished Citadel as it is very powerful in
  this deck."*
- *"If lands need 4 spots we keep 2 Gain 1 Drain. If it needs 3 spots we keep 2 Gain 1 Drain and 1 spell.
  We should always have at least 4 mana between hand and board that we try to keep (possibly a bit more
  if Ancient Tomb and we need colours). Either way, no need to have more than 4 land. 1 Ancient Tomb + 3
  coloured lands that produce what we need should generally be sufficient for everything. That said,
  extra spots can be filled with more land for flexibility, after we have 2 gain 1 drain and 1 spell."*

**The ladder** (`PreventDamageProvider::CleanupDiscardCandidates`, the block "THE USER'S LADDER"):
`lands to 4 (hand + board) > GAIN1 > DRAIN1 > GAIN2 > SPELL1 > the extra lands > DRAIN2 > the rest`.

| place | filled by (net of board) | tutor backup, only when no real card fills it |
|---|---|---|
| lands to 4 | 3 coloured lands covering the kept spells' colours, then ONE Ancient Tomb; painful first: Tarnished Citadel (3) > the 1-damage lands (City of Brass, Brushland, Battlefield Forge, Karplusan Forest; Grand Coliseum after them, it enters tapped) > Reflecting Pool | -- |
| GAIN x2 | Tamanoa > Purity > Rhox Faithmender > Bilbo (2 minus the gain/amp permanents on board) | Green Sun's Zenith > Living Wish > Beseech |
| DRAIN x1 | Vito > Dina (open only with no drainer on board) | Living Wish > Green Sun's Zenith > Beseech |
| SPELL x1 | Rolling Earthquake > Manabarbs > Spellshock > Pyrohemia (passive fuel last with no gain engine reachable) | Beseech only |
| extra lands | every other land, same order | -- |
| DRAIN2 | the other drain name | -- |

The rest is shed first, before the kept cards in reverse: dead cards (a duplicate legend, a target-less
tutor, a dead Reflecting Pool), then the unwanted sideboard bodies, then spare fuel, spare tutors
(Beseech first), spare drains and gain pieces. The 09-28 unplayed-land rule
(`MTG_PD_SHED_UNPLAYED_LAND`) is unchanged: a skipped land drop still makes a land the first discard.

**Readings the agent chose (USER to confirm):**
1. "1 spell" = one damage spell; Beseech may stand in for it, Zenith and Wish cannot reach one.
2. "Backups for missing pieces": a tutor takes a place only when no real card fills it, so a real Rhox
   keeps the second GAIN place over a Zenith. The opposite reading (Zenith = "a Tamanoa for 4" outranks
   Rhox) was measured as a temporary slot and played IDENTICALLY in all 4,900 games (same digests, same
   units), so the slot was deleted.
3. GAIN1 > DRAIN1 > GAIN2: with one tutor and neither piece, it goes to the first GAIN place; the DRAIN
   place comes before the second GAIN place so a second tutor backs up the drain.
4. "Lands to 4" counts lands, not mana; the Tomb is the 4th land when 3 coloured lands are available.

Unit tests U1-U5 (`test/unit/test_prevent_damage_discard.cpp`); the 09-28 boards T1-T11 pin `=0`.

**Measured** (`logs/pd_opt/multi8/`, ONE pooled batch, fresh seeds s190001/s191001 d5 b20 x600, 2HG s192001
x300, d3 b10 s193001 x400, d0 s194001 x3000; paired, losses 9): the USER's ladder vs the 09-28 ladder
**-0.0010 +/- 0.0006 t/game, 3 faster / 0 slower** (d5 s190001 1/0, d0 2/0; every other cell
score-identical), units 1.0006x. Every job's digest differs, so the ladder is live; the discard rarely
decides a game in this deck (the 09-28 ladder vs the generic rule moved 13 of 4,900). Recovery: no slower
game, nothing to recover. **References:** 0 / 11 short, all 11 replays byte-identical to base
(`logs/pd_opt/refbench/user.json`).

## Engine fixes from the 2026-10-08 sweep (integration branch; all default ON, PD-only reach)

| flag | defect | fix | reach |
|---|---|---|---|
| `MTG_GSZ_SHUFFLE_COPY_ID` | apply path shuffled Green Sun's Zenith back as the definition's template (`m_number 0`); `Library::ShuffleByKey` ranks by `m_number`, so the lookahead's library differed from the game's after every Zenith. s108346 gi345: the search "drew" a T3 Zenith the game never dealt, committed a phantom T5 line and LOST at every budget above b20 | the cast copy (its `m_number`) goes in; recording stamp `gsz_copy_id`, `--legacy-gsz-shuffle` replays older references | only Green Sun's Zenith carries the param |
| `MTG_POST_ENTRY_PING_ALL` | site 9's fan-out predicate `CardHasPostEntryActivation` lacked `ping_all_cost`, so "cast Pyrohemia, then ping" was inexpressible at any depth (s61003) | one clause | only Pyrohemia carries the param |
| `MTG_PD_LINE_OK_EXACT` | the pain-aware payer's rest-of-line check: any-colour land counted toward every colour; untapped non-mana permanents counted as 1 mana; re-demanded already-paid casts (s61001) | colour-exact assignment (`ColorFeasibility::PayablePips`) over the line's true unpaid remainder | armed (damage-event) decks only = PD |
| `MTG_PD_BATCH_TIMING` | whole-turn batch prepay dealt a later cast's payment pain before an earlier amplifier (Tamanoa / Vito / Dina) resolved (s61005, s61013) | decline the batch for such plans; pay per cast | PD |
| `MTG_PD_DEFER_C_GENERIC` | the pain deferral's colour-superset test counted {C} as a colour, so City of Brass could never stand in for Reflecting Pool (s61013) | {C} is generic while no {C} pip is live | PD |

Isolated GSZ A/B (s112001/113001 x500, 2HG 114001 x200, d3 115001 x400): -0.0050 +/- 0.0025, 10 / 3,
units 1.00x; the 3 slower games recover (two at stage 1; gi4's base T5 is reproduced by neither arm at
d8 b0).

**Multi-arm A/B of the stack** (one pooled batch, every arm a full heurarm flag set, `base` = the
committed tree with every new lever OFF; held-out s116001/s117001 x400 d5 b20, 2HG s118001 x200, d3 b10
s119001 x300 = 1,300 paired games per arm; scratch `logs/pd_opt/multi1/`):

| arm | delta vs base (turns/game) | better / worse | units |
|---|---|---|---|
| GSZ copy id | -0.0077 +/- 0.0029 (p 0.012) | 10 / 1 | 1.00x |
| Pyrohemia ping | -0.0008 +/- 0.0008 | 1 / 0 | 1.00x |
| line_ok exact | 0.0000 (no game moved) | 0 / 0 | 1.00x |
| **all three (the new defaults)** | **-0.0085 +/- 0.0030 (p 0.006)** | 11 / 1 | 1.00x |
| all three + Beseech draft | **-0.0162 +/- 0.0041 (p < 0.001)** | 23 / 3 | **0.80x** |

Every slower game recovers under the USER two-stage check: s118001 gi79 (both arms) and s116001 gi112
at stage 1, s116001 gi152 at stage 2 (d8 b0 = 5).

**Batch timing + defer-{C}** (`MTG_PD_BATCH_TIMING`, `MTG_PD_DEFER_C_GENERIC`, both default ON, PD-only:
`dmg_events_armed`). On an armed board the whole-turn prepay now declines a multi-cast plan whose
permanent casts can only make pain worth more (at least one improving it, at least two paying mana) --
the casts pay one at a time through the shared per-cast payer; order-independent, so executor and
rollout stay in lockstep. The deferral ignores the {C} capability bit while no {C} pip is live this turn.
A/B on top of the three fixes above (one pooled batch; s120001/s121001 x400 d5, 2HG s122001 x200, d3
s123001 x300, d0 s124001 x3000 = 4,300 paired; `logs/pd_opt/multi2/`):

| arm (vs the three fixes) | delta (turns/game) | better / worse | units |
|---|---|---|---|
| + batch timing | -0.0098 +/- 0.0020 | 39 / 4 | 0.99x |
| + defer-{C} | -0.0016 +/- 0.0006 | 7 / 0 | 1.00x |
| **+ both (the shipped defaults)** | **-0.0119 +/- 0.0021** | 47 / 3 | 0.99x |
| + both + Beseech draft | -0.0135 +/- 0.0025 | 62 / 12 | 0.81x |

The one slower searched game of the shipped stack, s120001 gi192, recovers at d8 b0 (T5 = W; stage 1
gave T6); the two slower d0 games (gi332, gi1605) are downstream greedy choices (the lighter d0 bar).

Deferred (own doc): `docs/design/committed-line-replay-lockstep.md` -- a committed full-depth line is
replayed without checking the real hand still matches (the gi345 amplifier), and the same template-copy
pattern still feeds exile / graveyard on the apply path.
