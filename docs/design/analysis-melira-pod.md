# Analysis ledger — Melira Pod (`decks/Melira Pod/Melira Pod.cod`)

Status: **IN PROGRESS** (Stage 2 — research fan-out launched)
Started: 2026-09-04. Branch: `phase-1-2-deck-analyzer` (base HEAD 9062c552 at start).

## Deck shape

Persist-combo / toolbox: Melira, Sylvok Outcast or Vizier of Remedies + a persist
creature (Kitchen Finks, Murderous Redcap) + a free sac outlet (Carrion Feeder,
Bloodthrone Vampire) = unbounded loop (Redcap = unbounded damage → the goldfish
win; Finks = unbounded life + unbounded Feeder growth). Birthing Pod and Chord of
Calling are the tutors that assemble it; Reveillark/Felidar Guardian are value/
recursion pieces on the chain.

## Stage 1 — coverage

- 23 of 28 cards `missing`; 5 already full (Ignoble Hierarch, Razorverge Thicket,
  Birds of Paradise, Branchloft Pathway, Forest). No sideboard (not a wish deck).
- Engine infra present: blink/flicker combo (CardDatabase.h ~1640, flicker-combo.md),
  `sac_creature_outlet` (~1251), `tutor_to_battlefield` (579, 1473), Karoo bounce,
  MDFC land template, fastland (Razorverge full).
- Engine infra ABSENT: persist, -1/-1 counters + "can't have counters" statics,
  convoke, evoke, LTB (leaves-battlefield) triggers.

## Stage 2 — cards (research fan-out → serial integration)

| card | tier (draft) | status |
|---|---|---|
| Melira, Sylvok Outcast | — | researching |
| Vizier of Remedies | — | researching |
| Kitchen Finks | — | researching |
| Murderous Redcap | — | researching |
| Birthing Pod | — | researching |
| Chord of Calling | — | researching |
| Carrion Feeder | — | researching |
| Bloodthrone Vampire | — | researching |
| Reveillark | — | researching |
| Felidar Guardian | — | researching |
| Ranger of Eos | — | researching |
| Recruiter of the Guard | — | researching |
| Severance Priest | — | researching |
| Ravenous Chupacabra | — | researching |
| Reclamation Sage | — | researching |
| Voice of Resurgence | — | researching |
| Scavenging Ooze | — | researching |
| Celes, Rune Knight | — | researching |
| Blooming Marsh | — | researching |
| Darkbore Pathway | — | researching |
| Llanowar Wastes | — | researching |
| Caves of Koilos | — | researching |
| Orzhov Basilica | — | researching |

## Provider routing (Stage 4a — known risk, plan the fix at integration)

Carrion Feeder / Bloodthrone Vampire will carry `sac_creature_outlet`, which ALONE
sets the Goblin signature in `SelectDecisionProvider` (DecisionProviders.cpp ~9331)
→ the deck would misroute to `GoblinsProvider`, whose `DeferSacOutletPreCombat`
defers sac activations to a second main this deck doesn't have — i.e. it would
DELETE the combo's sac loop from the autonomous search (5th occurrence of the
documented misroute class: Mirrorwing, StompySurprise, Minotaur, Dragons).
**Integration must add a Melira-Pod signature routed ABOVE the goblin check**
(copy the Dragons block), OR'd across several deck-only gated params (persist /
minus-counter-prevention / pod-chain / convoke — whichever new params land), never
a single card's param. Routes to `GenericProvider` until a measured hook exists.

## Stage 2 research drafts (compact; integration notes)

### Carrion Feeder — Tier 2 (draft received)
`{B}` 1/1 Zombie. `sac_creature_outlet: true` + NEW param `sac_outlet_add_counter_to_self: 1`
(CardDatabase.h ~1271 alongside other `sac_outlet_*` payloads; apply in
`ApplySacCreatureOutlet` single + burst paths in SpellEffects.h ~5448+, re-locating
source by id after victim erase, fizzle if source gone; add eval term in
TurnSolver.cpp ~12016 valuing permanent growth). Uses existing
`Counter::Type::PlusOnePlusOne`. "Can't block" inert (opponent never attacks).
Viewer: bucket A — existing `sacrifice` decision type + `sacout=` plan verb.

### Bloodthrone Vampire — Tier 2 (draft received)
`{1}{B}` 1/1 Vampire. `sac_creature_outlet: true` + NEW params
`sac_outlet_self_pump_power: 2` / `sac_outlet_self_pump_toughness: 2` (until-EOT via
existing `temp_power_bonus`/`temp_tough_bonus`; distinct from `sacrifice_watch_pump_power`
which is a passive any-sac watcher, power-only). Viewer: bucket A.
Note: no mid-combat sac-for-pump conversion machinery exists (cf. firebreathing which
converts mana, not creatures) — pump is main-phase, decays at cleanup; disclose.

### Five lands — Tier 1, INTEGRATED into cards.json (data-only, zero C++)
Blooming Marsh (fastland, Razorverge idiom), Darkbore Pathway (MDFC, Branchloft
idiom), Llanowar Wastes + Caves of Koilos (painlands, Adarkar/Yavimaya idiom,
`tap_self_damage: 1`, {C} mode painless), Orzhov Basilica (Karoo, Izzet
Boilerworks idiom, existing `bounce` viewer decision). No deferrals.

### Persist cluster — Tier 3 design (draft received; the keystone)
Verbatim costs: Melira `{1}{G}` 2/2; Vizier `{1}{W}` 2/1; Kitchen Finks
`{1}{G/W}{G/W}` 3/2 Persist + ETB gain 2; Murderous Redcap `{2}{B/R}{B/R}` 2/2
Persist + ETB damage = its power to any target.
Key engine facts: `Counter::Type::MinusOneMinusOne` ALREADY exists (Permanent.h:7,
EffectivePower/Toughness subtract it, DomAxis, rendering) — no new counter state.
Design:
- `MinusCounterReplacement(state, controller, n)` in SpellEffects.h: Melira
  (`prevents_minus_counters`) → 0; Vizier (`reduces_minus_counters_by_one`) →
  max(0, n-1); else n. Single call site = persist return.
- Persist hook in `OnCreatureDies` (SpellEffects.h:3999) BEFORE the
  `reactions.empty()` early-out (Worldspine Wurm block is the line-for-line
  precedent): guard `!dead_was_token` && dead had 0 -1/-1 counters; return newest
  matching graveyard copy to battlefield with `MinusCounterReplacement(...,1)`
  counters; fire FireEtbWatchers + FireOwnEtbTriggers.
- ALL SIX death sites must pass the dead card's -1/-1 count + token flag:
  GameEngine.cpp:790 (executor SBA), SpellEffects.h:4999 (SacrificePermanentAt),
  :5458 (sac outlet), :12039 (sac-as-cost), AIEngine.cpp:1051 +
  TurnSolver.cpp:21225 (echo sac pair). Missing one = [fd-diverge].
- New params: `persist`, `etb_self_lifegain` (NOT widening land-only
  `etb_lifegain` — D5), `etb_damage_equals_power` (Redcap persisted 1/1 deals 1;
  keep `etb_damage_any: 2` printed value for valuation readers), the two static
  flags. Keyword::Persist added as explicitly-inert tag (Suspend/Splice idiom).
- THE LOOP: BOTH literal + recognizer (flicker shape, neither alone works):
  `ApplyPersistLoop(state, controller, outlet_id, persist_id, K)` sibling of
  ApplyBlinkLoop (SpellEffects.h:9124), shared by rollout ApplyPlanDirect +
  executor, breaks when iteration illegal, cap 60. Emission: extend sac-outlet
  Action block (TurnSolver.cpp:11960) with `sac_count=K`, explicit
  `sac_victim_id=persist_id` (bypass CanonicalSacVictim). Action cost = ONE
  activation (flicker trap #1).
  - Redcap kill: `MeliraPodProvider::ExtraLethalDamage` = min(K*live_power, cap).
  - Finks kill: NO lethal addend (life isn't a win) — literal pre-combat loop
    grows Carrion Feeder (needs the NEW `sac_outlet_add_counter_to_self` param),
    ordinary attacker projection sees the grown Feeder. K demand-driven.
- Loop recognition: Melira-or-Vizier + zero-counter persist creature + FREE
  outlet whose filter admits the victim.
- Provider: must NOT defer sac outlets pre-combat (Goblins hook is wrong here) —
  one more reason for the dedicated routing block.
- Plan-signature digest (TurnSolver.cpp ~24944): add the new params so plans
  differing in persist body/K stay distinguishable; verify K activations emit K
  `sacout=` tokens in plan labels.
- Deferrals (PROVISIONAL, need user sign-off): D1 poison-counter clause inert
  (nothing increments poison); D2 "opponents' creatures lose infect" inert (no
  opponent creatures in this deck's games); D6 Redcap "any target" collapses to
  face (no opponent permanents; Twinshot Sniper precedent); D8 +1/+1 vs -1/-1
  annihilation SBA (CR 704.5q) not modelled — nothing here puts +1/+1 on a
  persist body; disclosed on Finks entry.
Viewer: all bucket A (persist automatic; victim choice = existing `sacrifice`
type; K = main_phase plan variants).

### Celes, Rune Knight — Tier 3 (draft received)
`{1}{R}{W}{B}` 4/4 Legendary Human Wizard Knight. Two triggers, BOTH live:
1. ETB rummage: "discard any number, draw that many plus one" — NEW params
   `etb_discard_any_number` + `etb_discard_any_draw_bonus: 1`. On CAST: fan N on
   the existing `Action::etb_kx` axis (Terastodon vehicle). On PUT (Pod/Chord —
   the deck's real route; deck has NO red land, only Hierarch/Birds make {R}):
   resolution heuristic off CleanupDiscardCandidates, bounded by an
   `MTG_CELES_RUMMAGE_WORST` anti-arm. Viewer: existing `discard` type + new
   `"any_number"` context branch (0..hand, ok at 0) in main.cpp
   WriteDiscardDecisionJson ~1889 + index.html discardPanelHtml ~2458 +
   DECISIONS.md row + audit manifest.
2. Graveyard-enter watcher: "whenever one or more OTHER creatures you control
   enter, if any from a graveyard → +1/+1 counter on EACH creature you control"
   — NEW param `other_creature_gy_enter_team_counters: 1`. Needs a thread_local
   `g_enter_from_graveyard` RAII (set by persist return + Reveillark return —
   CROSS-CARD DEPENDENCY) + `g_enter_batch_id` so simultaneous entries
   (Reveillark returns 2) fire ONCE. FireEtbWatchers branch (~2853), Emiel
   push_back shape (~2921). This is a real second kill under the persist loop.
Second-main: none. Deferral (PROVISIONAL): "or was cast from a graveyard" not
modelled — zero reachable trigger (no flashback/escape/etc. in deck, no engine
cast-from-GY zone).

### ETB/GY utility quartet (draft received)
Environment fact (proved): opponent creature count is ALWAYS 0 in this deck (only
spawn sources are Forbidden Orchard / Hunted Phantasm / Varchild's, all Creature
Giving-only). Opponent casts nothing, owns no artifacts/enchantments.
- **Ravenous Chupacabra — Tier 2**: `{2}{B}{B}` 2/2. NEW `etb_destroy_opp_creature`
  (ETB analogue of Terror's `destroy_target_creature`; factor
  `DestroyLargestOppCreature` helper out of Terror branch SpellEffects.h:4223,
  call from FireOwnEtbTriggers; eval credit 0). Payoff provably 0 here but
  implemented faithfully+reusable. PROVISIONAL alt: Terror-style stub. Taking
  IMPLEMENT. Auditor: inert annotation row (never surfaces — no legal target).
- **Reclamation Sage — Tier 1 + deferral**: `{2}{G}` 2/1. ETB is OPTIONAL ("you
  may"); only legal target ever = our own Birthing Pod (strictly dominated).
  PROVISIONAL deferral (Disenchant-stub precedent, MTG_SKIP_INERT_LIFEGAIN
  argument). Viewer cost: human can't choose to pop own Pod — disclosed; promote
  to Tier 2 `etb_destroy_own_artifact_optional` if user wants parity. Taking DEFER.
- **Voice of Resurgence — Tier 3**: `{G}{W}` 2/2. Opponent-casts clause DEAD
  (disclosed). Dies-token via `dies_watch_includes_self` +
  `dies_trigger_creates_tokens: 1`, token 0/0 "0/0 Elemental Token" cards.json
  def-by-name (Eldrazi Spawn/Treasure precedent) with `power_equals_creature_count`
  + NEW `toughness_equals_creature_count` (+ new `DynamicBaseToughness` mirroring
  DynamicBasePower SpellEffects.h:6805; twin at Combat.cpp:89,
  TurnSolver.cpp:3464,3535, DecisionProviders.cpp:2873; GoldFishRunner.cpp:226,313
  feature gates; credit dies-token at TurnSolver.cpp:4867). Token colour
  unmodelled (dies_token has no colour arg) — inert here, disclosed.
- **Scavenging Ooze — Tier 3**: `{1}{G}` 2/2. NEW params `gy_exile_grow_cost {G}`
  / `gy_exile_grow_counters 1` / `gy_exile_grow_lifegain 1`. REPEATABLE (no {T} —
  can't reuse Deathrite's GraveyardExileAbility); NOT under
  MTG_SKIP_INERT_LIFEGAIN (counter = real clock, life scored 0); WHICH card
  exiled is SEARCHED, one Action per distinct GY name (Haven pattern) — exiling
  own creatures strips Reveillark targets. New Action::Kind::GraveyardExileGrow;
  the six GraveyardExileAbility sites (AIEngine.cpp:3705, main.cpp:298,912,994,
  1040,1072) are the known-miss checklist; GoldFishRunner.cpp:405 GyR mask.
  Viewer: plan-verb `verb:ooze` manifest row. Melira bans -1/-1 only (disclose).
All four: no second-main, no bucket-B viewer work.

### Birthing Pod — Tier 3 (draft received)
`{3}{G/P}` Artifact (MV 4). Phyrexian pip: parser already degrades `{G/P}`→`{G}`
with CORRECT MV (CardDatabase.cpp:411 fallback) — DEFER as green-only,
CONSERVATIVE direction (modelling "2 life" in a goldfish = near-free discount
that would flatter Pod). PROVISIONAL deferral.
Params: `pod_activation_cost "{1}{G}"`, `pod_mv_delta: 1` (the gate),
`pod_taps: true` + reuse `tutor_types`/`tutor_shuffle_after`.
- New `Action::Kind::ActivatePod` reusing sac_source_id/sac_victim_id/
  tutor_target. Enumerate beside blink block (~11600-11790); apply_one ~20200;
  plan_signature "POD#pod>victim:target" (~23920); cost switch ~12434; executor
  mirror AIEngine.cpp ~3694-3745 calling SHARED `PerformPodActivate`.
- `PerformPodActivate` order: tap Pod → read victim MV BEFORE removal → sac via
  shared cascade (factor `SacrificeCreatureAtIndex` out of
  PerformSacrificeCreatureCost ~12009 — persist/Feeder/Voice watchers fire
  BEFORE the search; persisted Finks is itself a legal fetch target, CR 601.2h)
  → `PerformTutorToBattlefield(..., require_mv=M+1)` (NEW param, default -1 =
  byte-identical for Dragonstorm/Natural Order) → shuffle.
- Enumeration: FULL (victim,target) cross product; lossless folds only:
  victim-equivalence key = name|MV|tapped|sick|P/T|counters|is_token (persist
  bodies with spent counters NOT equivalent); library names uniq; duplicate Pods
  canonical. NO chosen_x axis ({T} caps it). ALSO emit the empty-target variant
  (sac with no fetch = legal, death-as-payoff line) — dropping it violates the
  core invariant.
- Provider: `MeliraPodProvider` hooks PodVictimCandidates/PodFetchCandidates/
  DeferPodPreCombat (analogue of DeferSacOutletPreCombat), base = return all.
- SECOND MAIN MATTERS (attack with Finks/Redcap, sac it post-combat — bank
  damage + ladder): no is_pre_combat gate in enumerator; NOTE (orchestrator):
  `GoldFishRunner::DeckUsesSecondMain` must detect `pod_mv_delta` or the deck
  never gets a second main at all — add it at integration.
- Viewer: bucket A plan variants; plan_signature MUST key both ids (Gamble
  lesson); SummarizePlan labels via EnchantTargetName; add ActivatePod to
  main.cpp "activate": true board-thumb list (~1030-1050, the sixteenth-miss
  slot); pass target as `preferred` so the dragon modal stays suppressed
  (Natural Order double-ask desync precedent).
- Ladder verified unbroken 1→5 (Redcap is MV 4 not 3). Celes uncastable from
  hand (no red source) — Pod/Chord-only; flag in any Pod A/B report.
- Deferrals (PROVISIONAL): {G/P} green-only; same-phase multi-Pod chain (second
  main covers cross-phase chains; full chain needs a K-axis — defer until
  measured need).

### USER FEATURE REQUEST (2026-09-04, mid-run): "infinite life" separate output
User: "I actually might want an 'infinite life' separate output, since most
decks can't win when you have a massive amount of life. It's worth optimizing
for, only losing to taking them out that turn."
Design (chosen, PROVISIONAL — re-raised in closing report):
- Detect the Finks loop online: (Melira|Vizier) + persist creature with
  `etb_self_lifegain` and 0 -1/-1 counters + FREE sac outlet admitting it, at
  sorcery speed, legality-verified via ApplyPersistLoop (not pattern-matched).
- Counts as a WIN with `win_kind=infinite_life` at that turn; search optimizes
  min(kill, inf-life). Separate aggregates in output (counts + avg turn per
  kind). Off-switch `MTG_INFLIFE_WIN` (=0 → pure-kill arm for A/Bs). All gated
  on deck params → other decks byte-identical.

### Chord of Calling — Tier 3 (draft received)
`{X}{G}{G}{G}` Instant, Convoke. CRITICAL loader fact: `KeywordFromString`
THROWS on unknown keywords — must add `Keyword::Convoke` (inert-tag idiom) or
cards.json load hard-fails. Second critical: TurnSolver.cpp:8857 `has_x` block
`continue`s any non-DirectDamage X card — Chord is UNCASTABLE until
`tutor_to_battlefield_single` is carved out of that terminal continue.
Params: `convoke`, `tutor_to_battlefield_single`, `tutor_types: [Creature]`,
NEW `tutor_mv_max_is_x`, `tutor_shuffle_after`.
- Convoke = cast-time cost reduction with explicit tap set: shared
  `ConvokeBodies(state, ctrl, need_green, need_generic)` helper in
  SpellEffects.h = single source of truth (untapped own creatures, sickness
  irrelevant, live-mana dorks excluded by dominance ⇒ also prevents
  AvailableManaPool double-count; sick dorks ARE eligible). Reduction applied
  on `a.cost` at enumeration ⇒ ManaPayment untouched.
- Lossless collapses: (target, X=MV(target)) pairs only (X>MV strictly
  dominated — provider XCandidates override; Generic max_affordable is WRONG
  here); tap classes by (payment ability × opportunity cost) → counts not
  subsets; free bodies forced; greedy pip assignment optimal. Real searched
  choice: how many would-attack bodies to tap (+ over-tapping to free mana for
  another spell — provider returns small set).
- plan_signature: param-gated `#X<x>` + `#C<g>/<n>` on CastFromHand (today the
  autonomous dedup collapses X — MTG_SIG_X_AUDIT is in-tree evidence).
- MV filter must be applied IDENTICALLY in enumeration + resolution keyed off
  same chosen_x (g_scripted_tutor_choice index pin desyncs otherwise) — thread
  `mv_cap` into TutorCandidates/PerformTutorToBattlefield (legality, not
  narrowing).
- `DeckUsesSecondMain` extend on `params.convoke` (post-combat Chord on
  leftover bodies is a real distinct line) — merge with pod_mv_delta extension.
- Viewer: tutor target + X + convoke counts all bucket A plan variants;
  optional bucket-B specific-body chooser flagged as refinement, counts-only
  default (disclose in 6a).
- Deferrals (PROVISIONAL): opponent-turn/end-step casting — Tier 4, engine has
  NO opponent-turn priority window; NOT inert (real EOT-Chord loss), must be
  disclosed honestly. Cross-cast convoke body allocation between two Chords in
  one plan not separately searched (rare; disclose).

### Reveillark — Tier 3 (draft received)
`{4}{W}` 4/3 Flying, Evoke {5}{W}. LTB (not dies!) trigger: return up to two
creature cards PRINTED power ≤2 from GY to battlefield (13 of 18 deck creatures
qualify incl. Melira/Vizier/Redcap/Feeder — combo reassembly). Params
`ltb_return_creatures: 2`, `ltb_return_max_power: 2`, `evoke_cost: "{5}{W}"`.
- `FireLeavesBattlefieldTriggers` called from OnCreatureDies AND ApplyBlink
  exile half (Felidar flicker fires it — cross-card coupling #1).
- PRINTED power off LookupCached (persisted Finks 2/1 on board is a power-3
  CARD → illegal target — coupling #2 with persist).
- Factor `PutCardOntoBattlefield` out of PerformTutorToBattlefield put tail;
  new `PerformReturnFromGraveyardToBattlefield`.
- Evoke: Action::evoke variant beside bestow (mutually exclusive by hand_index);
  self-sac via SacrificePermanentAt routes into LTB for free. Evoke costs MORE
  than hard cast — kept as searched mode (only way to buy LTB with no outlet).
- Viewer: evoke = bucket A main_phase SubChoice; graveyard picks = BUCKET B new
  `revive` type (dragon multi-pick shape; dragon panel string hardcoded, can't
  reuse). ReviveChooser + RevealLogPause null + WriteReviveDecisionJson +
  revivePanelHtml + registry + manifest rows.
- Provider `ReviveCandidates` resolution-time pick (trigger fires on planless
  paths); MTG_UNPRUNE=revive fan for human play. PROVISIONAL: which-two is
  provider-picked not searched (C(n,2) blowup) — disclose 6a.
- Flying inert (Peregrine precedent). No second-main.

### Felidar Guardian — Tier 3 small (draft received)
`{3}{W}` 1/4. Param `etb_blink_permanent`. WIDEN CanApplyBlink/ApplyBlink
(SpellEffects.h 8615/8631) creature-only guards with `permanents_ok` default
false (Displacer/Emiel byte-identical). Best lines: untap tapped Birthing Pod
(2nd activation same turn — coupling #3: re-entered artifact immediately
tappable), untap land, reset Finks persist counter, flicker Reveillark (fires
LTB + returns). "You may" decline REAL (flickering Basilica is a downside) —
emit decline variant. Action::etb_blink_target variants on cast; resolution
chooser needed for Pod/Chord/Reveillark PUT entries (no plan action) — BUCKET B
new `flicker` type (BounceChooser signature, -1=decline, attach_host precedent)
+ provider FlickerTarget ranking. No deferrals (Chupacabra/Sage re-fires inert,
disclosed). No second-main (creatures never die in combat — opponent never
blocks).

### Tutor trio (draft received)
- **Ranger of Eos — Tier 3**: `{3}{W}` 3/2. NEW `tutor_max_mv: -1` default +
  `etb_tutor_hand_count: 2`. Pair pick = provider `TutorHandPutList`
  (Defense-of-the-Heart shape, one entry PER COPY), resolved at ETB (works on
  Pod/Chord put path where no cast variant exists). New shared
  `PerformEtbTutorToHandMulti` in SpellEffects.cpp; gate at ETB firing site
  (SpellEffects.h:3942) INSTEAD of PerformTutor; exclude count>1 from cast-time
  single-target axis (TurnSolver ~9250); ONE ShuffleAfterSearch for both (two
  would burn a search_count ordinal). Filter conjunct at THREE sites
  (SpellEffects.h:1136, :1151, DecisionProviders.cpp:238) — do NOT unify (they
  deliberately disagree on empty tutor_types semantics; Gamble). Viewer: reuse
  `sac_tutor` multi-pick; branch sacTutorPanelHtml wording (to-hand vs
  battlefield); manifest row. Pool here: Birds/Hierarch/Feeder.
- **Recruiter of the Guard — Tier 2**: `{2}{W}` 1/1. NEW `tutor_max_toughness:
  -1` default, same three filter sites, printed m_toughness. Everything else
  rides Goblin Matron machinery (cast=tutor_target axis; put=existing
  `tutor_etb` modal, -1 decline). Pool = all creatures except Reveillark/
  Felidar/Celes/Severance Priest; finds Ranger (chain line). Doc gap found:
  `tutor_etb` has no DECISIONS.md registry row — add one.
- **Severance Priest — Tier 1 + paired deferrals**: `{W}{B}{G}` 3/3 Djinn
  Cleric, Deathtouch (parsed, inert). PROVISIONAL deferral PAIR: ETB opponent
  hand-exile (opponent never casts; would force DeckTouchesOpponentZones
  opponent-library machinery for zero effect) + LTB Spirit token to OPPONENT
  (every firing is a gift; optimal line is decline = exactly what deferring
  models). Only WBG triple-pip card; Pod rung 3, NOT a Recruiter target.

## Integration roadmap (serial, build between stages)

I1. DONE — keywords Persist/Evoke/Convoke + all new CardParams + parses.
I2. DONE — persist core: MinusCounterReplacement + PutCardOntoBattlefield +
    OnCreatureDies(persist) with REQUIRED (was_token, minus_counters) args
    threaded through all 6 death sites; etb_self_lifegain; etb_damage_equals_
    power; sac-outlet self payloads. 6 cards in cards.json. Lands + persist
    cluster INTEGRATED.
I3. DONE — ApplyPersistLoop (SpellEffects.h, cap 60; discriminator sac_count>1
    && sac_victim_id!=0 at both dispatches); demand-driven persist bursts
    (Redcap lethal-K with direct_damage, Feeder growth-K) in the outlet
    enumeration; plan-signature already folds (source,victim,count);
    GameState::infinite_life_win mirroring opponent_decked (set EXECUTION-
    VERIFIED in ApplySacCreatureOutlet on a free-outlet clean-return
    persist+lifegain sac; folded into OpponentHasLost + Dominance;
    MTG_INFLIFE_WIN default ON, InfLifeWinEnabled in SpellEffects.h);
    RunResult.inf_life + games_won_inf_life; [win] kind=inflife; "infinite
    life :" summary line in main.cpp. MeliraPodProvider (Generic-inheriting,
    routing block ABOVE goblin — misroute verified live first: provider=Goblins
    gave ZERO outlet activations over 8 probe games).
    PROBE (24 City of Brass + Feeder/Redcap/Finks/Melira, d3 b300 s100 x8):
    avg 5.875 (pre-fix) → 4.375; 7/8 inflife wins T4; MTG_INFLIFE_WIN=0 arm
    wins the SAME turns via realized damage; 20 games nonconv=0 fd-diverge=0.
I4. DONE — TutorNumericFilterOk conjunct at 3 sites; PerformEtbTutorToHandMulti
    (SpellEffects.cpp; sac_tutor chooser reused for human play; ONE shuffle);
    TutorHandPutList root default; cast-axis exclusion for count>1; entries for
    Recruiter/Ranger/Severance Priest.
I5. DONE — ActivatePod kind (option-grouped per Pod, subset dup guard),
    PerformPodActivate (tap→sac via SacrificePermanentAt→require_mv put),
    kPodNoFetch sentinel variant, apply cases both worlds, POD# signature,
    main.cpp tag + activate-thumb, DeckUsesSecondMain(pod_mv_delta|convoke).
    Probe: Recruiter→Ranger fetch + "pod -> Murderous Redcap" verified in logs.
I6. DONE — Chord branch in the has_x block (was the Luxurious-Libation trap:
    uncastable otherwise): (target, X=MV(target)) axis × {free-convoke,
    free+attackers} arms; ClassifyConvokeBodies/ApplyConvokeTaps shared
    (live-mana dorks excluded by dominance, sick dorks eligible); cost reduced
    at emission (ManaPayment untouched); convoke_green/other Action fields
    (folded); #X#T#C signature gate; max_mv_cap threaded through
    PerformTutorToBattlefield at both resolution sites off the same chosen_x.
    Probe avg 4.5, Chord casts verified.
I7. DONE — FireLeavesBattlefieldTriggers (OnCreatureDies + ApplyBlink exile
    half), PerformReturnFromGraveyardToBattlefield (provider ReviveCandidates,
    default MV-desc; GyEnterBatchScope = one Celes event per Reveillark
    resolution), ApplyBlink/CanApplyBlink widened (permanents_ok, default
    false), Felidar cast variants (chosen_x = target m_number + decline; #F
    signature gate) + FireOwnEtbTriggers tail branch (LAST — ApplyBlink shifts
    indices) + MeliraPodProvider::FlickerTarget/ReviveCandidates rankings.
    EVOKE NOT MODELLED — PROVISIONAL deferral (dominated whenever any of the
    deck's 9 free outlets / untapped Pod exists; wiring self-sac through ~20
    cast sites judged not worth the risk tonight; keyword parsed inert).
I8. DONE — DynamicBaseToughness (+SBA site in GameEngine),
    DestroyLargestOppCreature + etb_destroy_opp_creature branch, Celes rummage
    (resolution heuristic: discard excess lands beyond 2; searched N axis =
    disclosed 5e refinement) + gy-enter team-counter watcher in
    FireEtbWatchers, GraveyardExileGrow enumeration (per-GY-name, repeatable,
    no option group). Entries: Voice, 0/0 Elemental Token def, Ooze,
    Chupacabra, Rec Sage (deferral), Celes.
    Coverage scanner taught persist/dies_* params satisfy the death-trigger
    heuristic (2 false-positive partials cleared).
I9. Viewer wiring pass (revive/flicker/any_number/sac_tutor wording/manifest/
    DECISIONS.md rows) + 2d-bis audits.
I10. Stage 3 coverage loop → Stage 4 profile + 4a → Stage 5 battery.

## Approved deferrals

(none yet — every proposed deferral is PROVISIONAL until user sign-off)

## Open questions surfaced (non-blocking)

(collected here and re-raised in the closing message)

## Verification verdicts (Stage 5)

(pending)
