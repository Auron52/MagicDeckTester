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
Environment fact (~~proved~~ **RETRACTED 2026-09-05c**): the claim below missed the
runner-level goldfish spawn table (`PopulateOpponentSpawns`, creatures in 8 of 10 game
indices) — see SESSION 2026-09-05c for the corrected consequences (behaviour was already
right; only the justifications change). Original text: opponent creature count is ALWAYS 0
in this deck (only spawn sources are Forbidden Orchard / Hunted Phantasm / Varchild's, all
Creature Giving-only). Opponent casts nothing, owns no artifacts/enchantments — the
casts/artifacts half still holds.
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

**REDESIGNED ON USER REVIEW (2026-09-05) — the win-kind design above is
superseded.** User: going infinite does NOT count as a win ("I wouldn't count
going infinite fully as a win, but it should be reported separately as a
number ... any time you go infinite you are winning at latest next turn, since
the sacrifice effects all make massive creatures"; "We should report both";
"Often the kill turn is the same as the infinite life turn"; and the search
"should still prioritize cases that gain infinite life, if they can't find a
win ... it is a good tiebreaker"). Shipped design:
- `GameState::infinite_life_win` (bool win) → `inf_life_turn` (int, -1=never;
  proof turn). REMOVED from `OpponentHasLost` — kill/deck-out are the only
  wins again, everywhere (search, executor, fd-oracle revert via the one
  predicate); the game plays on after the proof and the loop-grown outlet body
  (Feeder counters / Bloodthrone pump ×60 cap) delivers the actual kill,
  usually the same turn or the next.
- Search prioritization = a DOMINANT term in the existing no-win leaf grade
  (`leafeval::Quantity`, default-ON MTG_LEAF_GRADE_NOWIN channel): a leaf that
  proved the loop outranks every life/board grade, earlier proof first; can
  never outrank a real in-horizon win. Lever `MTG_INFLIFE_TB` (default ON, =0
  measurement arm). `MTG_INFLIFE_WIN` is REMOVED (no setting makes it a win).
- Reporting both: `went infinite : N of G games, avg turn X [not a win; of
  those, K converted the kill, avg kill turn Y]`; `[win]` dump gains `ilt=`.
  Dominance fold updated (inf_life_turn folds when stamped).
- MEASURED (16-game benchmark, seed 9200, play settings): 16/16 still win,
  avg 5.0625 (vs 4.50 when the proof turn itself was credited as the win —
  the shift IS the +0/+1 conversion cost). 6 of 16 went infinite, avg proof
  T3.83, all 6 converted (avg kill T4.67; one same-turn, rest +1 — exactly
  the user's "winning at latest next turn"). Fewer games take the loop than
  under the win design (6 vs 9): with kills the sole objective the search
  loops only when it is on the kill path — correct, not a regression.
  MTG_INFLIFE_TB=0 arm byte-identical at this sample (deck wins in-horizon,
  so the no-win grade rarely binds; the lever exists for games with no
  in-horizon win). Smoke tier run to confirm all other decks byte-identical
  (OpponentHasLost is core).

### USER TUTOR POLICY (2026-09-05) — combo-aware to-hand tutor heuristics
User spec: "You only want missing combo pieces, tutors for combo pieces and
more rarely something that can be podded for more combo pieces when pod is
active. ... Ranger always should get a sacrifice creature though the second
choice is less crucial and could be a dork. Recruiter should get a combo piece
that you are missing." Third case "only necessary when you are really
creature-light or have duplicates at a specific mana cost. For example extra
persist creatures, Voice of Resurgence are good options as needed to fuel
birthing pod." Also named the canonical lines: pod Finks→Ranger, pod the
persisted Finks again→Redcap next turn; Voice pods into Finks/Recruiter and
its death token pods into a 1-drop.
Shipped (MTG_POD_TUTOR_RANK, default ON; =0 generic base):
- `MeliraPodProvider::TutorHandPutList` (Ranger, resolution-time, NEVER
  searched → the ranking IS the decision): first free-sac-outlet copy, then
  mana dorks (tmpl==ManaDork), then spare outlet copies, then rest.
- `MeliraPodProvider::TutorCandidates` (Recruiter's searched axis + base
  pick): missing-role first (enabler +100 > free outlet +80 > persist +60,
  missing = absent from battlefield AND hand), then Pod-fuel +40 (Pod on
  battlefield && a missing piece sits at candidate MV+1 in library), then
  cheaper-first, name-total-order. Pod/Chord enumerations deliberately NOT
  narrowed (search-primary; perf fine at ~0.74 s/game, ENUM_STATS clean).
Measured: s9200×16 byte-identical both arms; s3100×20 ON 5.55 vs OFF 5.60
(one game faster, one more loop proven, no regression anywhere). Log-verified
(36 games): Ranger fetched Feeder+Birds in 7/7 casts; Recruiter fetched the
missing piece both times it resolved (Melira g1, Redcap g24); consecutive-turn
Pod chains Finks→Redcap present. Smoke tier byte-identical (provider-scoped).

### USER PUT-NARROWING (2026-09-05) — Pod/Chord fetch axes cut to the whitelist
USER: "I don't think 0.74 seconds a game is good. I would cut the pod/chord
enumerations as well. ... there are actually useless cards for goldfish. We
should narrow them down to just the useful options." Plus the definitive tier
map: "Sacrifice creature for 1, Melira or Voice or sac creature on 2. Finks +
Recruiter on 3. Redcap + Ranger + Celes on 4" / "persist creatures on 3 and 4,
Melira effects on 2 and 4, sacrifice creatures on 1 and 2 + a tutor on 3 (and
a tutor for just 1-drops on 4)". Two corrections folded in mid-build: Celes IS
a combo piece (gy-enter team counters × every loop iteration = a Redcap-class
kill payload → always useful), and the MV4 "Melira effect" read as FELIDAR
(flicker resets the -1/-1 counter — enabler-class backup, missing-gated;
INTERPRETATION SURFACED to the user, not confirmed).
Shipped (MTG_POD_PUT_NARROW, default ON; =0 unnarrowed arm):
- `DecisionProvider::PutTargetPolicy/PutTargetOk` — two-step, ALLOCATION-FREE
  contract (v1 returned a name whitelist built per call: library scan + three
  hash sets × >250k CollectActions in the gi9 repro — the policy computation
  cost more than the narrowing saved; the header documents the lesson).
  Default narrow=false → every other deck byte-identical; the Pod + Chord
  enumeration sites share one policy so the two put-tutors cannot drift.
  Victims, the no-fetch sentinel, and the lossless folds are untouched.
- Whitelist by params: free outlet + persist always; Celes-class
  (other_creature_gy_enter_team_counters) always; enabler-class (prevents/
  reduces counters, etb_blink_permanent=Felidar) while no enabler assembled;
  tutors (tutor_to_hand) + diggers (etb_discard_any_number) while any piece
  missing; Voice-class fuel (dies_trigger_creates_tokens) while a Pod is out.
  Dorks/Scooze/Chupacabra/Rec Sage/Severance/Reveillark never listed.
MEASURED (d5 b20 = the deck's bare-run defaults):
- Quality: s9200×16 ON 5.0000 vs OFF 5.0625 (better); s3100×20 5.55 = 5.55.
- CPU: total across both sets 99.5s ON vs 152.3s OFF (−35%); s3100 alone
  −46%; s9200 flat.
- TAIL GAME gi9 (s9200): ON 18.8s/T5 vs OFF 6.2s/T6 — 6.5× the rollouts
  (276k vs 43k) but a TURN FASTER; the trajectory diverges to a richer,
  costlier, better line. Budget-independent (persists at d3/b10). Hotspot =
  ordinary work (ColorFeasibility::Payable 18%). Known trade, not a defect.
- Budget saturation: b50/b100 buy nothing (identical avgs); the d3/b10 cell
  matches d5/b20 quality on both seed sets at −25/−32% CPU — a candidate
  cheaper default, NOT adopted (36-game sample; needs a wider sweep).
- The real sub-second-per-game route remains the value-leaf stage (1.35–84.8x
  per value-leaf.md), which is user-initiated policy.

### USER CORRECTION (2026-09-05): Celes is the deck's SECOND MELIRA EFFECT — and
### that exposed a missing CR 704.5r rule
User: "Celes is primarily a Melira replacement that draws. Pumping your board
is a secondary effect ... (basically just like a bonus) ... Since +1/+1
counters cancel out -1/-1 counters this works." Mechanism: the persist return
itself fires her gy-enter trigger; the +1/+1 lands on the returning body and
annihilates its own -1/-1 — one body loops clean with NO counter-prevention
replacement. This also resolves the tier map: "Melira effects on 2 and 4" =
Melira/Vizier and CELES (the earlier Felidar reading was wrong; Felidar
removed from the put whitelist).
ENGINE GAP FOUND AND FIXED: counter ANNIHILATION (CR 704.5r) was not
implemented anywhere — counters coexisted as separate entries, P/T netted them
but MinusCountersOn read the raw -1/-1, so persist legality saw a dirty body
and the Celes loop stalled after one iteration. Shipped:
- `AnnihilateCounters` (SpellEffects.h, beside MinusCountersOn): min(+1/+1,
  -1/-1) removed from both; called eagerly at the gy-enter watcher (the only
  site in the pool that puts both types on one body; the helper's comment
  binds future counter sites to call it). No-op unless both types coexist →
  byte-identical for every other deck.
- `GyEnterCleanerActive` + the persist-burst enumeration gate extended:
  loop-closers are now (MinusCounterReplacement==0) OR a Celes-class watcher
  (per-victim re-check excludes the victim per "other creatures").
- Inflife detection needed NO code change (it scans post-event state); its
  comment now names both closer routes.
- Provider: NotePodRoles counts Celes-class as have_prev (BATTLEFIELD only —
  from hand her triggers do nothing and she is essentially uncastable here);
  PodMissingRoleScore ranks her enabler-class; PutTargetOk drops
  etb_blink_permanent (Felidar) and documents her primary role.
MEASURED: s3100×20 5.55 → 5.45 (one game a turn faster with the Celes route
available); s9200×16 unchanged (5.0000, same 7 loops). Smoke for other-deck
byte-identity + CI recorded with the commit.

### SLOW-GAME AUDIT (2026-09-05, USER: "issues if the win turn is above 5")
Convoke verified WORKING end-to-end (g11 T5: Chord chosenX=2, manaPaid {1}{G}{G},
Feeder+Finks tapped as the convoke bodies; 18 casts across 29 chord-holding
games of 52 logged). The real >T5 leaks were expendability ties:
1. FEEDER SELF-SAC FOR NOTHING (T1, alone on board): CanonicalSacVictim
   returned the source, whose +1/+1-to-self payload lands on the body that
   just died — a flat-leaf value tie preferred the "busy" plan. FIX: a
   self-directed-payload outlet is never offered as its own victim
   (param-gated; external-payload self-sacs like Siege-Gang stay legal).
2. MELIRA AS FODDER: the power-based expendability rank sacked the 2/2
   enabler over a 3/3 Ooze (Feeder fodder), and battlefield-order Pod
   emission committed pod-away-Melira at an equal-score tie over the
   same-MV Ooze. FIX: shared SacExpendabilityRank (factored from
   CanonicalSacVictim) gains a combo-enabler defer tier (+5000, above
   lords: prevents/reduces counters, Celes-class) and the Pod victim loop
   emits in that rank order (ordering only — every victim still emitted,
   folds stay lossless). NOTE (user): spawns don't block — creatures
   "dying in combat" was never a thing; every disappearance was a sac.
3. Chord held all game while missing 2 pieces (g8): user ruled this
   acceptable ("you want to get the one you are missing"; clairvoyance
   caveat acknowledged, not a concern). Chord→Celes noted as strong with a
   junk-heavy hand when affordable — already whitelisted; her rummage digs.
MEASURED after 1+2: s9200×16 5.0000 → 4.9375 (g15 T6→T5, no game worse);
s3100×20 5.45 unchanged.
OPEN (deferred, disclosed): the win-turn-tie inflife preference — between
two lines that BOTH win on turn N, nothing prefers the one that also goes
infinite (the tiebreak only grades no-win leaves); g15's old Pod#2-over-
activation choice was this class before the ordering fix masked it.

### USER FODDER RULE (2026-09-05): no sacs to Feeder until the combo is active
User: "there is essentially nothing you want to sacrifice to carrion feeder
until the combo is active. I suppose the only exception would be if the
sacrifice gives us lethal." Shipped as `DecisionProvider::FodderSacUseful`
(default true = byte-identical everywhere; the DeferSacOutletPreCombat hook
precedent, incl. the human-play carve-out) gating ONLY the canonical K=1
fodder sac of SELF-payload-only outlets (Feeder counter, Bloodthrone pump);
the persist-loop variants and lethal-K bursts are emitted separately and
never gated, so no loop or kill line is lost. MeliraPod rule: allow when the
loop closes (Melira/Vizier/Celes active) or a static lethal check passes
(ready attack power + payload × spare non-attacker bodies ≥ opp life — the
gi14 lethal was two TAPPED Hierarchs into Feeder for the last two points).
Lever MTG_POD_FODDER_GATE (default ON; =0 ungated arm).
MEASURED: 36-game sets moved ±1 game either way (gi14's exact-lethal race
became infinite-T5/kill-T6 — the gated line is the policy-correct one); the
deciding sample, s5000×100 fresh seeds: ON 5.13 vs OFF 5.12 — neutral within
noise, with more infinite-life play ON (33 vs 31 loops). Adopted per the
user's explicit rule.

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
    life :" summary line in main.cpp. [The win-kind half of this record is
    SUPERSEDED by the 2026-09-05 user-review redesign above: inf_life_turn,
    not a win, tiebreak via leafeval + MTG_INFLIFE_TB.]
    MeliraPodProvider (Generic-inheriting,
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

## Claude-play sweep
- commit: `8f712107` (played pre-fix; every confirmed flag fixed in the follow-up
  commit and re-verified — see resolutions below)
- seeds: 9200 (+gi) games: 16
- flags: 0 unresolved
- Results: 16/16 games completed. 11 exact win-turn parity with the d5/b200
  benchmark; 5 Claude-slower games (gi 2,3,8,9,11,15) — all but one caused by
  the convoke bug below; none faster (expected vs the clairvoyant search).
  Benchmark: avg 4.5625 (10 inflife avg 4.0 / 6 kill avg 5.5).
- CONFIRMED → FIXED:
  1. Convoke Chord dropped at execution (5 agents, ~9 repros): enumeration
     emitted the convoke-REDUCED cost but every recompute site (executor
     CastSpellFromHand, rollout apply_one, sequential-payability sim,
     split-turn accounting) re-priced the FULL X cost → mana-legal casts
     tapped their bodies then dropped ("dropped_casts"), and the autonomous
     search self-filtered convoke arms (Chord underrated). FIX: shared
     ApplyConvokeReduction applied at every site off the committed
     convoke_green/other counts (threaded through cast_by_name /
     CastSpellFromHand / apply_one).
  2. ApplyPersistLoop over-executed past opponent death (gi5: 18 extra
     iterations to -72 life). FIX: OpponentHasLost break each iteration
     (the SpendSurplusOnDrain guard).
  3. Cosmetic: creature-sac decision note said "the land to sacrifice"
     (shared WriteBounceDecisionJson). FIX: noun derived from the options.
  4. Cosmetic: persist bursts rendered "sac 18 creatures" on a 4-creature
     board. FIX: "loop <victim> xN (persist)" label.
  5. Cosmetic: Pod puts event-tagged kind:"dragonstorm". FIX: kind follows
     the source.
- Post-fix re-benchmark (same 16 games): avg 4.5625 → 4.5000. gi=11 7→5 (the
  search now takes convoke-Chord lines — the direct fix payoff); gi=9 5→6 at
  b200, EXPLAINED as a recoverable budget shift (b600/b2000 both restore the
  T5 inflife win monotonically; the widened convoke plan space dilutes the
  fixed budget on this one game) — the acceptable "search-budget line-shift"
  category, not a structural deletion.
- DISMISSED (verified non-bugs, with reasons):
  * Win checked at step boundaries (in-turn decisions continue after the
    win event; outcome/turn unaffected) — engine design, all decks.
  * Avoidable painland pings (2 agents) — tap-order treats self-damage as
    free vs a passive opponent; zero win-turn impact; noted as a global
    heuristic-quality candidate, not deck work.
  * Manual-sac heuristic_default badge points at the expendability rank
    (dork) not the combo body — the autonomous search uses its own explicit
    persist action (benchmark won); badge-only, 6a note.
  * Self-sac outlet plan offered with no other creature — legal enumeration,
    never chosen by the search; possible 5f prune.
  * "0/0 Elemental Token" display name — the CDA math was verified correct
    by the agent (token attacked for 7 with 7 creatures); name is the token
    def-by-name convention.
  * Same-plan entrants not Pod-sac candidates — the disclosed once-per-phase
    enumeration limit (see Pod deferrals).

## Approved deferrals

(none yet — every proposed deferral is PROVISIONAL until user sign-off)

## Open questions surfaced (non-blocking)

(collected here and re-raised in the closing message)

## Verification verdicts (Stage 5)

- 2d-bis: card_fields PASS (327 cards; 1 finding fixed — Pod cost now verbatim
  {3}{G/P}, parser does the documented collapse); cost audit via field audit
  (cost-audit 429s were transients; Celes hand-verified {1}{R}{W}{B} 4/4).
- 4a: provider_audit → Melira Pod → MeliraPod ✓ (no suspects).
- verify_deck (--no-network): coverage PASS (28/28 full), card_fields PASS,
  mismatch PASS (0 nonconv / 0 fd-diverge, seeds 7001+7002 × 60 games),
  play_invariants PASS (8 games / 132 decisions), viewer self-guard FIXED
  (manifest/inert rows for all new params; static auditor now rc=0),
  claude_sweep IN FLIGHT (16 Sonnet agents, seed 9200+gi / gi 0-15, commit
  8f712107; benchmark: 16/16 wins avg 4.5625 — 10 inflife avg 4.0, 6 kill 5.5).
- 5b multi-depth (s3100 × 20, b400): d0 8.0 / d3 5.30 / d5 5.30 — monotonic,
  plausible combo clock. d0 wins mostly by beatdown (expected, no search).
- 5c: no budget starvation signal (d3=d5).
- 5c2 leaf_tiebreak_check: NO SIGN at 1,200 paired games (0 changed — the
  tie-break never fires: the deck wins T3-5, deep inside the horizon). Default
  (GradesNoWinLeaf ON) kept per the script's own rule for an unbindable lever.
  FOLLOW-UP: the full-size run (12 blocks × 1000, ~24k games) was SIGKILLed
  (likely OOM at this pool size); re-confirm at 16 blocks when convenient.
- POST-FIX battery (commit e293a813, fixed engine + regenerated profile):
  verify_deck GATE PASS — coverage 28/28 full, card_fields PASS, viewer PASS
  (self-guard + FULL surface sweep clean), viewer_wiring PASS (bounce +
  sac_tutor), mismatch PASS (0 nonconv / 0 fd-diverge, 7001+7002 × 60),
  play_invariants PASS (8 games/136 decisions), claude_sweep PASS (recorded,
  0 unresolved). Multi-depth re-run (s3100 × 20 b400): d0 7.35 / d3 5.10 /
  d5 5.10 — monotone, plausible. CI green on e293a813 incl. Windows +
  determinism parity.
- VIEWER FOLLOW-UPS (bucket-B wiring deliberately deferred, disclosed in 6a —
  the full auditor surface sweep passes because these choices are mapped with
  their auto-resolution disclosed): `revive` chooser (Reveillark LTB picks —
  provider ReviveCandidates auto-resolves), `flicker` chooser for PUT-path
  Felidar entries (provider FlickerTarget auto-resolves; cast-path targets ARE
  human-pickable plan variants), Celes `discard` any_number context (heuristic
  N = excess lands), CheckLine verbs for ActivatePod/GraveyardExileGrow
  (reference-replay of those actions; no references exist yet), DECISIONS.md
  rows for the above.

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-09-04)

`verify_deck.py decks/Melira Pod/Melira Pod.cod --no-network --no-sweep --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 28 cards full (missing=0, partial=0) |
| card_costs | SKIP | yes | skipped (--no-network) |
| card_fields | PASS | yes | 327 cards match snapshot (cost/PT/types/keywords); 8 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| viewer | PASS | yes | self-guard + surface (static, --no-sweep) clean |
| viewer_wiring | PASS | yes | 2 type(s) wired (emitter + GUI): bounce, sac_tutor |
| mismatch | SKIP | yes | skipped (--no-sweep) |
| play_invariants | SKIP | yes | skipped (--no-sweep) |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Melira, Sylvok Outcast: Clause 2 is the live one: a replacement read by MinusCounterReplacement() at the single site that puts -1/-1 counters (the persist return) -- with Melira out a persist creature returns CLEAN and the loop never spends itself. Clause 1 inert: Player::poison_counters exists but nothing in the engine ever increments it (no infect/poison source implemented, opponent never attacks). Clause 3 inert: the passive opponent controls no creatures in this deck's games (the only opponent-creature spawn params live in Creature Giving).
- coverage deferral -- Kitchen Finks: Hybrid pips are real either-colour pips. Lifegain via etb_self_lifegain (a CREATURE ETB -- deliberately NOT the land-only etb_lifegain). Persist via the persist param (OnCreatureDies return through MinusCounterReplacement); the Keyword::Persist tag is an inert tag, Suspend/Splice idiom. Known gap, disclosed: the +1/+1 / -1/-1 annihilation SBA (CR 704.5q) is not modelled -- nothing in this deck ever puts +1/+1 counters on a persist body.
- coverage deferral -- Murderous Redcap: "Any target" collapses to the opponent's face (etb_damage_any precedent, provably optimal vs the passive opponent -- no opponent permanent exists in this deck's games). etb_damage_any 2 is the PRINTED-power value so every valuation reader keeps working; etb_damage_equals_power substitutes the entering permanent's live EffectivePower() at resolution, so a persisted 1/1 Redcap deals 1, not 2.
- coverage deferral -- Birthing Pod: PHYREXIAN MANA UNMODELLED, PROVISIONAL: costs are stored VERBATIM ({3}{G/P} / {1}{G/P}) and ManaCostFromString itself collapses {G/P} to {G} with the CORRECT MV 4 (its documented fallback). Green-only is the CONSERVATIVE side on purpose -- vs a passive goldfish opponent life is not a pressured resource, so modelling 'or 2 life' would make the pip nearly free and FLATTER the card; revisit only if the {G} pip measurably gates Pod turns. ACTIVATION (Action::Kind::ActivatePod -> PerformPodActivate): cost pod_activation_cost + {T} (pod_taps) + sacrifice a chosen creature through the SHARED death cascade (CR 601.2h -- costs paid before resolution, so a sacrificed Kitchen Finks persists back BEFORE the search and its card is not in the graveyard); victim MV read before it leaves; fetch = library creature with MV exactly victim+pod_mv_delta via PerformTutorToBattlefield(require_mv), full ETB cascade, then shuffle. 'Only as a sorcery' needs no gate (actions are enumerated in the mains only). The (victim, fetch) pair is a fully-searched axis: victims fold by a death-equivalence key, library names dedup, and the no-fetch variant '(no fetch)' is always emitted (the death itself can be the payoff). Second main enabled via DeckUsesSecondMain(pod_mv_delta): attack with a persist body, sac it to Pod post-combat.
- coverage deferral -- Ignoble Hierarch: Exalted is ENGINE-MODELLED (Keyword::Exalted -> CountExalted: +1/+1 per Exalted permanent when exactly one creature attacks, applied at the shared combat sites). Tri-colour dork via produces B/R/G.
- coverage deferral -- Reveillark: LTB (leaves-the-battlefield) trigger, NOT a dies watcher: fires on ANY leave -- every death site (sacrificed to Carrion Feeder / Birthing Pod, combat) via OnCreatureDies AND the exile half of a Felidar Guardian flicker via ApplyBlink -> FireLeavesBattlefieldTriggers. 'Power 2 or less' reads the PRINTED card power in the graveyard (a persisted Kitchen Finks is a 2/1 on the battlefield but a power-3 CARD in the yard -- never a legal target). 13 of the deck's 18 creatures qualify, incl. Melira, Vizier, Redcap, Carrion Feeder -- one trigger can rebuild the kill. WHICH two = MeliraPodProvider::ReviveCandidates (resolution-time: the trigger fires on paths no plan action carries; missing loop pieces first, then MV desc), human `revive` chooser planned in the viewer pass. Flying inert (passive opponent never blocks; Peregrine Drake precedent). [PARTIAL, PROVISIONAL: EVOKE is NOT modelled as a cast mode. WHY: {5}{W} evoke costs MORE than the {4}{W} hard cast, so it is strictly dominated whenever ANY free sac outlet (4 Carrion Feeder, 1 Bloodthrone) or untapped Birthing Pod is available -- hard-cast + sac buys the same LTB cheaper; its only live line is LTB-now with ZERO outlets, a narrow corner, while wiring evoke's self-sac through both worlds' cast paths touches ~20 hot-path sites. Deferred pending user sign-off; the keyword tag is parsed and inert.
- coverage deferral -- Voice of Resurgence: PARTIAL: the opponent-casts clause is DEAD -- the passive goldfish opponent never casts a spell (players[1
- coverage deferral -- Reclamation Sage: PARTIAL, PROVISIONAL: the ETB is not modelled. WHY inert -- the trigger is OPTIONAL ('you may', declined at resolution) and the passive opponent controls no artifacts or enchantments ever, so the ONLY legal target in this deck is our own Birthing Pod, which is strictly dominated to destroy; modelling it would add a permanently-declined, strictly-dominated action to the plan space (the MTG_SKIP_INERT_LIFEGAIN precedent; follows the user-approved Disenchant stub). Viewer cost, disclosed: a human cannot choose to destroy their own Pod -- promote to a Tier-2 param with a human-play-gated enumeration if the user wants parity.
- coverage deferral -- Ranger of Eos: ETB MULTI-tutor to HAND: etb_tutor_hand_count 2 + tutor_types + NEW tutor_max_mv 1 + tutor_shuffle_after. WHICH two = DecisionProvider::TutorHandPutList (ordered, one entry per library COPY -- 'two Carrion Feeders' is legal), resolved ONCE at the ETB on both the cast and the Pod/Chord put path (no cast-time axis), human-overridable via the sac_tutor multi-pick chooser. ONE shuffle after both leave the library. Legal pool in this deck: Birds of Paradise, Ignoble Hierarch, Carrion Feeder. 'Up to two' is always taken at the full count when available (fetching fewer is legal but never right here; disclosed).
- coverage deferral -- Ravenous Chupacabra: etb_destroy_opp_creature: the ETB analogue of destroy_target_creature (Terror), pick = largest opponent creature (DestroyLargestOppCreature, shared both worlds). PAYOFF IS PROVABLY 0 IN THIS DECK: the only opponent-creature spawn params in the engine (Forbidden Orchard / Hunted Phantasm / Varchild's) all live in Creature Giving, so the trigger never has a legal target and never fires here. Implemented faithfully + reusable rather than stubbed (the Terror-stub alternative was declined -- see the analysis ledger); carries no eval credit.
- coverage deferral -- Carrion Feeder: cant_block inert -- the passive goldfish opponent never attacks, so we never block. Free sac outlet (no mana cost, no {T}): sac_creature_outlet with the NEW sac_outlet_add_counter_to_self payload -- PERMANENT +1/+1 growth (Counter::PlusOnePlusOne), which under the Kitchen Finks persist loop is the deck's combat wincon.
- coverage deferral -- Recruiter of the Guard: ETB tutor to HAND on the Goblin Matron / Stoneforge machinery (tutor_to_hand + tutor_types + tutor_shuffle_after), narrowed by the NEW tutor_max_toughness filter -- PRINTED toughness off the CardDefinition (no continuous effect applies to a card in a library). WHICH creature: on a cast, the searched tutor_target plan axis; on a Birthing Pod / Chord PUT, the provider's front pick with the human tutor_etb chooser override (-1 declines the optional search). 'Reveal it' unobservable (nothing reads reveals).
- coverage deferral -- Felidar Guardian: ONE-SHOT ETB flicker (etb_blink_permanent) -- structurally unlike the repeatable activated blink_cost (Displacer/Emiel); re-usable only by re-entering Felidar itself (Pod / Chord / a Reveillark return). Reuses the ApplyBlink primitive WIDENED to any PERMANENT (permanents_ok): the deck's best targets are non-creatures -- a tapped Birthing Pod returns UNTAPPED and can be activated a second time that turn, a tapped land returns untapped as +1 mana. The return is a NEW OBJECT (CR 400.7): a persisted Kitchen Finks comes back with NO -1/-1 counter, a flickered Reveillark fires its LTB on the exile half, every flickered creature is summoning-sick again. 'You may' is a REAL decline (flickering Orzhov Basilica re-fires its land-bounce, a downside) -- a decline variant is emitted alongside the per-target cast variants (chosen_x carries the target m_number); PUT entries resolve via MeliraPodProvider::FlickerTarget (tapped Pod > spent persist body > Reveillark > tapped land > Finks > tutors > decline), human `flicker` chooser planned in the viewer pass. Re-firing Ravenous Chupacabra / Reclamation Sage is inert: the passive opponent controls no creatures, artifacts or enchantments.
- coverage deferral -- Darkbore Pathway: Modal double-faced LAND: play EITHER Darkbore ({B}) OR Slitherbore ({G}); the chosen face enters untapped and taps for its one colour, committing to one colour like the real card. In hand it counts as its FRONT colour ({B}) for mulligan/colour eval (minor disclosed simplification); the played battlefield face is exact.
- coverage deferral -- Branchloft Pathway: Modal double-faced LAND: play EITHER Branchloft ({G}) OR Boulderloft ({W}); the chosen face enters untapped and taps for its one colour, committing to one colour like the real card. In hand it counts as its FRONT colour ({G}) for mulligan/colour eval (minor disclosed simplification); the played battlefield face is exact.
- coverage deferral -- Scavenging Ooze: gy_exile_grow_cost {G}: a REPEATABLE activated ability (no {T} -- unlike Deathrite's gy_exile_* modes), N activations per turn bounded by green mana and graveyard size (Action::Kind::GraveyardExileGrow). 'A graveyard' collapses to OURS (the passive opponent's is always empty; disclosed). NOT under the MTG_SKIP_INERT_LIFEGAIN cut: the +1/+1 counter is a real clock; the 1 life is modelled but scored 0. WHICH card is exiled is a SEARCHED choice, one action per distinct graveyard card NAME (Haven of the Spirit Dragon pattern) -- exiling our own creature cards strips Reveillark's LTB targets, so it is not fungible. Melira bans -1/-1 counters only; +1/+1 growth is unaffected. Exiled cards are simply removed (nothing reads exile in goldfish).
- coverage deferral -- Vizier of Remedies: Modelled FAITHFULLY as n -> max(0, n-1) in MinusCounterReplacement, not as a Melira-equivalent boolean: it differs for n >= 2. For persist (n=1) both yield 0, which is why either card enables the loop.
- coverage deferral -- Llanowar Wastes: Painland, both modes modelled -- see Adarkar Wastes.
- coverage deferral -- Orzhov Basilica: Karoo bounce land: enters tapped, makes 2 mana ({W}{B}, modelled as wild like other duals), and on ETB returns one of your lands to hand (BounceKarooLand prefers a tapped land so no mana is lost this turn; the returned land must be replayed, the real tempo cost).
- coverage deferral -- Chord of Calling: CONVOKE modelled as a cast-time COST REDUCTION with an explicit tap set (Action convoke_green/convoke_other; ClassifyConvokeBodies is the single source of truth, shared by enumeration and both apply worlds): bodies tapped BEFORE the reduced cost is paid, summoning sickness IRRELEVANT (no {T} symbol), a green body pays a {G} pip or {1}, others {1} only; mana dorks with a LIVE mana tap are excluded by dominance (their tap yields a superset -- also prevents AvailableManaPool double-count), summoning-sick dorks ARE eligible; free (cannot-attack / 0-power) bodies tapped first, then a free+attackers arm (the real trade: damage now vs the fetch). X AXIS: one variant per (distinct library creature name, X = its MV) -- X > MV(target) is strictly dominated -- with X, target and tap counts all in the plan signature. FETCH: tutor_to_battlefield_single + tutor_mv_max_is_x (target MV <= chosen X, the cap threaded identically through enumeration and resolution), full ETB cascade on the put creature, then shuffle. [PARTIAL: instant speed collapses to YOUR MAIN PHASES -- the engine has no opponent-turn priority window, so the classic end-of-opponent's-turn Chord (fetched body untap-ready on your turn, mana spent on their turn) is NOT modelled. A real, non-inert loss, deferred as Tier 4 engine infrastructure; PROVISIONAL pending user sign-off. Both YOUR mains are modelled (DeckUsesSecondMain fires on convoke).
- coverage deferral -- Severance Priest: Deathtouch parsed (Keyword::Deathtouch) but structurally inert -- the passive opponent never blocks or attacks. PARTIAL, PROVISIONAL (user sign-off pending): clauses 2 and 3 are DEFERRED as a PAIR. Clause 2 (hand exile) -- the passive opponent never casts anything, so removing a card from its hand cannot change any outcome; its only live consequence is ARMING clause 3, which hands the OPPONENT a Spirit token. Clause 3 -- an opponent body in a model where opponent creatures never act, and this deck sacs the Priest constantly (Carrion Feeder / Pod), so every firing would be a small gift. The exile is OPTIONAL ('you may'), so the optimal line vs this opponent is to DECLINE -- which is exactly what modelling neither clause produces: the deferral reproduces optimal play rather than approximating it.
- coverage deferral -- Caves of Koilos: Painland, both modes modelled -- see Adarkar Wastes.
- coverage deferral -- Bloodthrone Vampire: Free sac outlet: sac_creature_outlet with the NEW sac_outlet_self_pump payload -- +2/+2 until end of turn (temp_power/tough_bonus, decays at cleanup). Distinct from sacrifice_watch_pump_power (Priest of Gix), a passive any-sacrifice watcher, power-only; this fires only on the outlet's OWN activation.
- coverage deferral -- Celes, Rune Knight: ETB RUMMAGE: etb_discard_any_number + etb_discard_any_draw_bonus 1 -- N chosen by a RESOLUTION heuristic on both the cast and the Pod/Chord put path (the deck's real route: NO red land exists, only Hierarch/Birds make {R}, so Celes is essentially uncastable from hand and enters via Pod/Chord): discard the hand's excess lands beyond two; N=0 still draws 1, never a downside. A searched cast-time N axis is a disclosed refinement (5e). GRAVEYARD-ENTER WATCHER: other_creature_gy_enter_team_counters 1 -- fires from FireEtbWatchers when the entering OTHER creature came from a graveyard (persist returns, Reveillark's LTB returns set GraveyardEnterScope), +1/+1 counter on EVERY creature we control; simultaneous entries fire ONCE (Reveillark's two-card return = one event, GyEnterBatchScope). A real second kill: under the persist loop each iteration pumps the team. [PARTIAL: the 'or was cast from a graveyard' half is NOT modelled; WHY inert -- no card in this deck has any cast-from-graveyard route and the engine has no such zone for creatures; zero reachable trigger. PROVISIONAL pending sign-off.
- card_costs SKIPPED (--no-network) -- Scryfall cost/cmc reality-diff not run
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Glorybringer [keywords]: 'Exert' is a real keyword but its use is OPTIONAL and provably worthless here: exerting costs the next untap step (so Glorybringer cannot attack the f
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.56); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.75); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.75); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.44); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.\n{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.71); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.\n{T}: You may put a creature c
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
- oracle_text advisory -- Worthy Knight: oracle_text diverges (similarity 0.45); scryfall='Whenever you cast a Knight spell, create a 1/1 white Human creature token.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.77); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.41); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)\n{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.44); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.43); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.55); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.11); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.58); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.\nFerocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample\nSpells you cast cost {1} less to cast for each target.\nSpells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
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
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
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
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.60); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample.\nWhen this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.58); scryfall='Enchant creature with another Aura attached to it\nEnchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample.\nWhenever enchanted creature deals damage, yo
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
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.08); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.40); scryfall='First strike\nWhen this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach\nWhen this creature enters, it deals 2 damage to any target.\nChannel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.\n{T}: Add {C}.\n{2}, {T}: Choose a color. Add an amount of 
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.\nWhen this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.\nWhenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.\nWhenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.25); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.34); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.45); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.\n{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.28); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.47); scryfall='Flying, first strike, trample, lifelink, haste\nAt the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste\nWhenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything\nIf Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance\nThis creature gets +1/+1 for each color among permanents you control.\n{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)\n{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.24); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)\nEquip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.\n−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.\n−2: Gain control of target creature.\n−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")\n+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.23); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying\n{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.46); scryfall="Choose five. You may choose the same mode more than once.\n• Target permanent phases out.\n• Target player dra
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
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.43); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.\nEquip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.\nEquip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.\n{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.\nWhenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.\nEquipped creature gets +3/+0 and h
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.\nRemove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.49); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.\nMetalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.37); scryfall='First strike\nBalan has double strike as long as two or more Equipment are attached to it.\n{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying\nWhenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.\n{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.12); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.39); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
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
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.24); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.\n{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.17); scryfall='First strike\n{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.\n{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.\nWhenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.35); scryfall='First strike\nOther Minotaurs you control have first strike.\nAs long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample\nOther Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.\n{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.28); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.\n{2}, Sacrifice another creatu
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
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land\nEnchanted land has shroud. (It can't be the target of spells or abilities.)\nWhenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.22); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.06); scryfall="This spell can't be countered.\n{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)\n{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)\nFlash\nFlying\n{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.09); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
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
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.\nCreatures you control can't have -1/-1 counters put on them.\nCreatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.\nPersist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.\nPersist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.\nSacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch\nWhen this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.29); scryfall="({G/P} can be paid with either {G} or 2 life.)\n{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.25); scryfall="Flying\nWhen this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.33); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.\nWhenever one or more othe
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- viewer SWEEP SKIPPED (--no-sweep) -- decision surfacing not runtime-verified
- mismatch SKIPPED (--no-sweep) -- nonconv/fd-diverge not exercised
- play_invariants SKIPPED (--no-sweep) -- claude-play protocol determinism/integrity/progress not exercised
- claude_sweep recorded at commit 8f712107 (HEAD e293a8135755); re-run if play changed since (NOTE: Melira Pod is NOT a regression case, so NO digest tracks its play -- nothing will tell you when this record goes stale. Re-run the sweep on judgement, or add the deck to the suite).

<!-- verify_deck:end -->

## SESSION 2026-09-05b — viewer play-testing feedback round 1

### Phyrexian mana IMPLEMENTED (user REJECTED the {G/P} green-only deferral)
User, from viewer seed 1: "I am not given the option to use phyrexian mana to play pod and
activate it T3. That is a key way to use pod." / "Ah, we can't defer that for sure." / "the
only major one here seems to be the phyrexian mana one." The green-only collapse was never
signed off (PROVISIONAL, collected overnight) and real play refuted its premise: the point of
{G/P} is not affording the pip, it is that 2 life FREES A SOURCE (T3: cast Pod {3}+2 life AND
activate {1}+2 life off 4 sources — impossible green-only).

Design (the convoke idiom, end to end):
- `ManaCost::phyrexian_count/phyrexian_color[2]` (Card.h): colour baked flat (MV/readers
  byte-identical), metadata only ADDS variants. Deliberately NOT in hybrid_pair — the life side
  is not a colour. `StripPhyrexianForLife(k)` removes k pips. Parser: `{C/P}` in
  ManaCostFromString (CardDatabase.cpp); `MTG_NO_PHYREXIAN=1` = old collapse (A/B hatch).
- Mana-vs-life is a SEARCH BRANCH, not a payment preference: CollectActions' phyrexian
  post-pass (runs LAST, after all filters) emits one variant per life-paid pip count for
  CastFromHand + ActivatePod, cost pre-stripped + `Action::phyrexian_life` (=2/pip), life-gated
  at emission AND apply (never pays to 0; > not >=). Variants share the base's group keys ->
  mutually exclusive; `plan_signature` gets gated `#P<life>` tags on both kinds (the bestow
  lesson).
- Recompute sites all strip in lockstep: rollout apply_one, executor CastSpellFromHand
  (threaded through cast_by_name, 9 sites), BatchPrepayMainCasts, the condemn stamper,
  SubsetPayableSequential. Life deducted after the mana half commits (CR 601.2h; nothing
  triggers on life payment here). Rollout + executor ActivatePod pay sites gate life first
  (no mutation), then deduct.
- Viewer: plan-list tag suffix "(pay N life)" + `phyrexian_life` key in the plan JSON.
  DISCLOSED GAP: CheckLine (--validate-line) prices full mana — a hand-built pay-life line
  reads unpayable; fix queued with the viewer items.
- DISCLOSED: the greedy (d0/leaf fallback) path never pays life — phyrexian is a searched
  feature; a greedy Pod cast pays {G} when affordable, is uncastable when only life would work.

Perf: seed 1 (the deck's slow game, 10s baseline) went 70s — the un-pruned fan tripled
solve-memo misses. Added `SubsetPhyrexianDominated` (MTG_PHY_DOMPRUNE, default ON): reject a
life-paid subset whose full-mana bill the PLAIN pool covers (weak dominance, exact vs this
apparatus — the mana twin has more leftover mana and more life). 70s -> 43s; s5000x100 A/B
in flight at time of writing (quality + aggregate cost).

Verified: unit tests 64/64; seed-1 log shows T3 `Birthing Pod manaPaid={3}` (+2 life) AND a
second Hierarch the freed pip paid for, Pod->Redcap T4, Pod->Celes T5, win T5.

### Deferral review round 1 (user, live)
- {G/P} green-only: REJECTED -> implemented (above).
- Chord opponent-turn/end-step window: "I don't think Chord end-step matters" -> deferral
  ACCEPTED (stands, disclosed).
- Multi-Pod: user "if we have multiple pods out we should allow their activation". VERIFIED
  allowed today: exclusivity is per sac_source_id (the {T}), two Pods = two families, both
  activate in one phase; Felidar untap re-enables a spent Pod. The only gap is the same-phase
  CHAIN (Pod#2 saccing Pod#1's fetch — victim lists snapshot pre-fetch); cross-phase chains
  work via the second main. Chain stays deferred (needs measured need).
- Reveillark EVOKE: user — "should be modelled for cases where we have sacrificed low-mana
  critters in the past... should be available." -> QUEUED to implement.
- Reveillark which-two: user — "should be default searched, but overriding with a provider is
  fine. You want to focus on having the combo first." -> QUEUED: searched axis, provider order
  as the rollout/fallback pick.
- "Opponent creature count provably 0": user challenged ("they can have spawns"). Re-derived:
  the only in-sim route is Severance Priest's Spirit, gated behind the OPTIONAL ETB exile we
  always decline — so the claim holds autonomously, but a human cannot take the exile in the
  viewer (parity gap, queued). Question surfaced to user: which spawn source did they mean?
- Darkbore front-face colour eval: user — "suspect, but I guess it will be overridden by the
  proper mulligan profile." Stands as-is.
- QUEUE after gameplay items (user): viewer bucket-B choosers (revive, flicker, Severance
  exile, Rec Sage self-Pod, CheckLine #P/verbs), then the equal-win-turn inf-life preference.

### EVOKE implemented (a618893c)
Second CastFromHand variant sharing hand_index (bestow idiom), pays evoke_cost {5}{W}
(added to Reveillark's params), self-sacs after the enter cascade via the SHARED
SacrificePermanentAt in BOTH worlds -> LTB fires as on any leave. Emission gated on a
printed power<=2 creature card in the graveyard (else dominated by the hard cast).
Cost swapped at every recompute site (the convoke/phyrexian lockstep list); #E0/#E1 sig
tags; viewer "(evoke)" + JSON key. Proof scenario melira_evoke_reveillark.json (only the
evoke line is lethal on the turn): PASS; suite 63/63, smoke 68/68 byte-identical,
benchmarks unchanged (0 evoke fires in 100 autonomous games -- corner line, as designed).

### Equal-win-turn inf-life preference implemented (closes the OPEN item)
User-queued. `leafeval::t_inf` publishes the winning rollout's end-state inf_life_turn at
SimulateToEndImpl's three win exits; the ROOT ranking loop prefers the inf-proven line on
equal-win-turn ties (above plan.value, below win turn; MTG_INFLIFE_WINTIE default ON).
SCOPE (deliberate, disclosed): root ties only -- the interior FSLine early exits/cutoffs stop
at the first horizon win and breaking them to surface interior ties is a search-cost trade this
does not justify; TT cache hits also lose the tag (bare-int table; deterministic per run).
The `better` chain restructure is provably identical when no candidate carries a stamp -> every
other deck byte-identical by construction (smoke 68/68). Measured s5000x100: 4.96 vs 4.97 avg,
inf 34 vs 33, inf turn 4.06 vs 4.09 -- directionally right on every axis, no drawback ->
adopted per the clean-win rule.

## SESSION 2026-09-05c — pod-chain breakpoint (site 7) + the spawn premise corrected

### "Spawns" resolved: the goldfish opponent's scheduled creatures
The surfaced question ("which spawn source did you mean?") is answered: the USER meant the
goldfish opponent's SCHEDULED spawns — `GoldFishRunner::PopulateOpponentSpawns`'s 10-game
pattern cycle, which materialises passive opponent creatures at fixed turns in **8 of every
10 game indices** (patterns 2–9; only indices 0–1 are pure goldfish). The session-b
re-derivation ("only in-sim route is Severance Priest's Spirit") searched card params and
missed the runner-level table entirely. Consequences re-derived:
- **Ravenous Chupacabra**: the "trigger provably never fires" claim was WRONG — spawns are
  real battlefield Permanents (GameEngine upkeep materialisation) and
  `DestroyLargestOppCreature` scans the battlefield, so the ETB fires and kills the largest
  spawn in any spawn-pattern game. Behaviour was already correct (implemented faithfully,
  never stubbed); only the justification changes: payoff stays ~0 because spawns never
  attack or block. Stale comments fixed (SpellEffects.h helper header, CardDatabase.h param).
- **Murderous Redcap collapse-to-face**: "no opponent permanent exists" was wrong, but face
  remains STRICTLY optimal vs never-acting spawns (damage to face progresses the win; damage
  to an inert body does nothing). Verdict stands on the corrected ground. Note the human
  cannot aim Redcap at a spawn in the viewer — same class as the Chupacabra tie-pick; both
  are ~0-payoff choices vs inert bodies, parity-gap-noted, not queued.
- **Melira clause 3** (opponent creatures lose infect): spawns are plain P/T tokens with no
  keywords — still inert, justification updated.
- **Severance Priest decline-optimal**: unchanged (a gifted Spirit is as inert as a spawn).

### Pod-chain BREAKPOINT (site 7) — "Pod #2 sacs Pod #1's fetch in the same phase"
USER: "still a relevant line. Maybe we should have a breakpoint in this case?" Built exactly
that: the fetch resolving is a mid-phase event that creates a new actionable (the fetched
creature as a sac victim), i.e. the post-breakpoint-search class. **Site 7** (bit 7 of
MTG_BP_SITES; default mask 0x77 -> 0xF7) opens in ApplyPlanDirect's trailing activation pass
right after a successful PerformPodActivate with a REAL fetch, gated on
`PodChainAnotherActivatablePod` (a second untapped pod_mv_delta source) — the common one-Pod
board pays nothing. Continuation = searched (`bp_choice` indexing the shared
EnumerateBreakpointPlans list, which re-collects at the post-fetch state where the fetch IS a
victim) or greedy Solve fallback; unlike sites 0/1 the fallback plays NO static land (nothing
was drawn). Both trailing activation passes (ApplyPlanDirect + the executor's) were
lambda-ified (recursive std::function) so the continuation's ACTIVATIONS apply — that is the
chain itself — and a continuation Pod activation re-enters the site (nesting via bp_at,
bounded by pod taps). Executor twin in AIEngine's ActivatePod branch: same gates, same
class-gated bp_seen counting (`TurnSolver::PodBreakpointClassOn` accessor), searched
continuation from the SAME list, precasts + clean-order casts + recursive trailing apply.
Wave-0 fan: `PlanOpensBreakpoint` marks plans holding a real-fetch ActivatePod when the
pre-apply battlefield holds >= 2 activatable Pods.
- **Ordering constraint (disclosed)**: the site counts between the inline cast sites and the
  deferred classes in both worlds; consistent today because no deck mixes pod sources with
  deferred-class cards (Melira plays no cantrip/trick/equipment/dig/staging card). Reconcile
  before such a deck exists.
- **Human play**: the auto-continuation is OFF under HumanPlayActive in both worlds — the
  human owns the rest of the phase. ~~The human-side same-phase chain remains the KNOWN parity
  gap (one plan pick per phase, no re-poll after a pod activation); cross-phase chaining via
  the second main still works for humans. Queued, unchanged.~~ **RETRACTED 2026-09-05e —
  measured EXPRESSIBLE.** The "one plan pick per phase" premise predates the always-prompt
  segment loop (MTG_PLAY_SEGMENT_ALWAYS, adopted 2026-09-04): the main phase re-prompts with a
  FRESH EnumerateMainPlans after every committed line, so all three shapes were verified live
  on Melira s1/gi0 via the stateless protocol: (1) same-main activation of a just-cast Pod —
  9 activation-only plans offered on the re-prompt right after the Pod cast (T3); (2) re-poll
  after an activation — always happens; (3) THE chain, Pod #2 saccing Pod #1's same-phase
  fetch — after committing "Pod: sac Finks → Redcap", the re-prompt offered 28 plans with
  "Birthing Pod: sac Murderous Redcap → …" (T5, two-pod steer). Site-7 wave-0 fan plans also
  put two-activation single lines in the human menu (victims snapshot pre-fetch there, so the
  fetch-chained victim still needs the segment route). Every earlier "0 chain plans"
  observation in the verification traced to mana genuinely being spent (all sources tapped /
  the last dork summoning-sick or sacced as the pod victim) — enumeration was never the gap.
- **Proof scenario** `melira_pod_chain.json`: 3 Pods, lone Carrion Feeder, opponent at 2,
  only damage in the position is Redcap's ETB 2 at the top of an MV 1->2->3->4 ladder — a
  three-step climb that CANNOT split across two mains (the second same-phase step needs the
  just-fetched victim). Default: PASS, realising the full triple chain in MAIN_1
  (Feeder->Vizier, Vizier->Finks, Finks->Redcap, ETB lethal). MTG_BP_SITES=119 (site 7
  masked; NOTE atoi cannot parse "0x77" — use decimal): FAIL, ladder caps at Finks. The
  first scenario draft (2 Pods, 2-step ladder) passed WITHOUT the site via main1+main2 —
  the cross-phase route really does cover every 2-step chain, which is why the site's value
  is the >= 3-step turn and the main-2 chain (a fetch made IN main 2 was equally unreachable).
- **Measured (s5000x100)**: ON 4.9500 avg / 35 infinite (conv 4.686) vs OFF 4.9600 / 34
  (conv 4.677); CPU 12m05s vs 12m05s user (FLAT), wall 50.1s vs 54.4s, same five SLOW-GAMEs
  either arm. MTG_BP_PROBE: site 7 hit 1,093,162 times / 100 games, 17.6% searched, 18,880
  on committed lines (~189/game). Small quality gain at zero measured cost -> **ADOPTED
  default-ON per the clean-win rule**; suite untouched by construction (no other deck has a
  pod source; smoke 68/68 byte-identical, unit 64/64, scenarios 64/64 incl. the new one).

### Seed-1 T3 cast+activate — the dominance prune had a hole (USER report, fixed)
USER (viewer, seed 1): "I can't play Birthing pod and activate it. On turn 3." Root cause:
`SubsetPhyrexianDominated` prunes a life-paid subset whenever the plain pool covers the full
bill — sound only if every use of the freed mana is expressible as another subset of the same
enumeration. The ACTIVATION of a Pod the subset is CASTING is not expressible (the pod loop
scans the battlefield; the Pod is still in hand at collect), so "land + Pod {3}+2 life, hold
the 4th source" was pruned everywhere the full bill was payable — plans 18–21 (Pod pay-life +
Hierarch, all four sources dead) and 29–30 (plain Pod) were all that survived, and the human
had no committable line reaching the activation. FIX 1: a subset casting a permanent with an
activation mana cost (pod_activation_cost) keeps its life twin. The enabler plan now appears
and, committed, the second-main decision offers the full pay-life activation fan (sac Finks →
Redcap etc.) — the user's exact T3 line is playable in the viewer (cast main 1, activate main
2, same turn; same-MAIN activation of a just-cast Pod remains inexpressible, value-identical
in goldfish).

FIX 2 (engine valuation), measured through three forms:
- Rollouts fully blind (fix 1 alone): seed 1 spends all four T3 sources on Pod+Hierarch and
  activates T4 — the enabler plan exists but rollouts can't see the main-2 payoff, so it
  always loses the root comparison.
- Activation twin un-gated for rollouts: seed 1 casts Pod T2 and fetches Redcap T3, but
  re-creates the original blowup shape (s5000x100: 11 SLOW-GAMEs / >7 min wall vs 5 / 50s).
  KILLED and reverted.
- SHIPPED: the TIGHT-POOL twin — rollouts get the activation life twin only where the full
  bill is unpayable from the current pool (exactly where the twin is the difference between
  the activation existing and not). Pool computed once, phyrexian-actions-only.
Final s5000x100: 4.9800 avg / 36 inf (conv 4.778) vs 4.9500 / 35 (conv 4.686) at 12m24s vs
12m05s user (+2.6%), wall 80.7s vs 50.1s — the tail is one game (gi=24: 36s -> 80s, the
prune-exception + tight-pool fan on a grindy pod board); SLOW-GAME count unchanged at 5.
Seed 1 win turn unchanged (T5) in all forms — the engine's T3 dork-vs-hold choice is an
equal-win-turn judgment it now makes SIGHTED. NOT a clean win (avg +0.03 noise-band, CPU
+2.6%, one tail game 2.2x): committed locally, PUSH HELD at user request ("Let's hold off on
pushing for a bit") — the trade-off is the user's to accept. Suite: smoke 68/68
byte-identical (fan gated on phyrexian_count / pod casts), unit 64/64, scenarios 64/64.

### Live play-test round 2 (2026-09-05): reference #1 promoted; three human-surface fixes
- **references/Melira_Pod/claude_s1_gi0.json PROMOTED + COMMITTED** (user: "can be marked as
  an actual reference") -- a T4 win, one turn faster than the search's own seed-1 game.
  Same-day viewer_protocol_check row added (fifth same-day-row incident; dir underscores vs
  the deck folder's space, the Creature_Giving shape).
- **"Not enough green for Chord" root-caused as THREE stacked defects**, from the user's saved
  s1 t4 rejection (verdict said "{X} unsupported"; the truth was one green short):
  1. `SubsetPhyrexianDominated` pruned the bare pay-life activation in HUMAN decision lists --
     a human builds a phase as several lines, so "pay life now, Chord next line" is real and
     invisible to the subset's dominance claim. CheckLine's {G/P} choose dimension collapsed
     to "pay mana" alone; the human literally could not pick pay-life on the declared line.
     FIX: the prune stands down under HumanPlayActive (rollouts keep it via HumanPlaySuppress).
  2. Even paying life, the activation's generic {1} TAPPED THE FOREST with Boulderloft (W) and
     Darkbore (B) idle -- all three tie at mono rank 10 and battlefield order decided. FIX:
     human-play-only demand tiebreak in the scarcity greedy (MTG_HUMAN_TAP_DEMAND, default ON):
     among EQUAL-rank sources paying a GENERIC pip, spend the highest hand-demand SURPLUS
     (supply minus hand pips per colour; costs read via the DEFINITION -- zone Cards carry no
     cost, the first build read all-zero demand). Coloured pips and the rank ladder untouched;
     autonomous play and GT byte-identical.
  3. The {X} stage-2 bail claimed "v1 cannot validate {X}" -- misleading (stage 1 matches {X}
     lines fine, fanning X and tutor-target as choose dimensions; the bail only fires when NO
     plan casts the spell). Reworded to the honest diagnosis (same misdirection class as the
     retired tutor bail).
  End-to-end verified on the user's exact line: pay-life activation (Darkbore pays the {1}),
  Forest survives, `cast=Chord of Calling` returns a choose fan (X=3 Recruiter/Finks etc.).
- **s6 gi5 t4 rejection ("11x sacout=Carrion Feeder" -- THE COMBO KILL) fixed**: repeat
  outlets enumerate as demand-driven loop bursts (x8 lethal damage / x14 lethal growth), so
  the human's click-count could only ever match the K's demand computed. FIX: loop-count fold
  in CheckLine's sacout match (exact first; on mismatch, bend only counts on names the plan
  loops, both sides >= 2) + a "loops xK <victim>" choose sub so K is an explicit pick, never
  a silent deviation. Verified: the 11-sacout line resolves to x8-vs-x14, committing x8
  realises the full persist loop (15 -> -1, Feeder swings for 9) -- win turn 4.
- Battery at the final state: unit 69/69, scenarios 72/72, viewer_protocol_check --strict
  0 play-drift / 0 contract-fail (284 refs incl. the new Melira one), smoke 68/68
  byte-identical (all three fixes are human-surface-only by construction).

### References resurrected (USER directive: "there should be no way for them to be dead")
The s1_gi0/s3_gi2 shuffle-dead verdicts were WRONG, and the user's framing was exactly right:
the shuffle is seed-deterministic and the lines were still enumerable -- the checker's repair
was the broken part. Root cause: `find_plan` matches a recorded pick by SUMMARY, and the
summary hides X, the Chord tutor target and the pod victim. s1's recorded T4 plan ("Chord +
pod pay-life") had ELEVEN identical-summary twins in the live (twin-widened) enumeration;
hits[0] realised a Chord fetching the wrong creature, the pod half stranded, the board
diverged, and the checker blamed the shuffle. FIX: action-payload narrowing in find_plan --
identical-summary hits narrow by the recorded plan's full `actions` signature (card, x,
tutor_target, phyrexian_life, pod_victim, verb, ...), which the reference already records.
RESULT: all 5 Melira refs green (s1 repaired -> T4 win as recorded; s3 repaired -> T5), and
the fix resurrected two OTHER decks' refs too (suite shuffle-dead 3 -> 1, repaired 263 ->
265, still 0 play-drift / 0 contract-fail on 284). The one remaining shuffle-dead
(FiveColour/claude_s1_gi0, hand genuinely differs -- Progenitus never drawn) PREDATES today
and is a real draw divergence, not this class; candidate for its own investigation.
NOTE for future emission changes: a plan list that grows (the phyrexian twin widening pushed
the recorded pick out of the 200-plan display window) is survivable ONLY because the checker
runs uncapped (MTG_PLAY_PLANS_CAP=0) and now matches full payloads -- keep both.

## SESSION 2026-09-05e — the last "shuffle-dead" ref, and the chain parity gap closed by measurement

### FiveColour/claude_s1_gi0: NOT shuffle-dead either (the user's "no excuse" standard, vindicated again)
The previous session flagged this as "hand genuinely differs -- Progenitus never drawn, a real
draw divergence". Traced this session: **also wrong, and also a checker artifact.** The
step-by-step replay shows every fetch matching the recording (Overgrown Tomb, Steam Vents,
Godless Shrine) and Progenitus drawn on T3 exactly as recorded. The real story: Maelstrom
Archangel's free-cast charge moved from a post-main #FREE plan variant to its own combat-time
`free_cast` frame, which this Aug-13 reference predates. The walk answers that inserted frame
from RECORDED INTENT (`free_cast_intent` reads the donor T4 post-main pick) and free-cast
Progenitus early -- correct! -- but nothing marked the donor post-main frame as consumed, so
on reaching it the plan was gone, the hand "differed" (the card is on the BATTLEFIELD, not
undrawn), and the classifier mis-blamed a reshuffle. FIX (same shape as the replicate
`honoured` set): `freecast_done` tracks what intent-driven free casts made per turn; a donor
frame that is nothing but those casts (land=none) is satisfied-early -> passed, not declared
dead. The ref now replays `repaired` to its exact recorded terminal (won=True, win_turn=5).
**Suite: 13 ok / 266 repaired / 0 play-drift / 0 SHUFFLE-DEAD / 0 enum-gap / 5 mull-drift /
0 contract-fail (284 refs).** Commit 63919bf1.

- The 5 remaining mull-drift are ALL Mirrorwing_Dragon s1-s5: the DECKLIST changed under them
  (3faf5c76 shipped the Anger-4/Oracle-3 list; the old list was archived as v2). Different 60
  cards -> different numbering -> different shuffles; "the recorded game no longer occurs" is
  literally true and neither the checker nor the engine is at fault. References are user-owned
  and commit-only, so whether to archive them beside the v2 list is the USER's call (surfaced).

### Human-side same-phase pod chain: RESOLVED-STALE (see the retraction in SESSION 2026-09-05c)
Verified live on Melira s1/gi0 through the stateless protocol: the always-prompt segment loop
already expresses (1) same-main activation of a just-cast Pod, (2) re-poll after an
activation, and (3) Pod #2 saccing Pod #1's same-phase fetch (28 such plans offered on the
re-prompt after committing "sac Finks -> Redcap"). No code change; the 2026-09-05c "one plan
pick per phase" premise predated the 2026-09-04 segment-always adoption. Remaining Melira
queue after this: Rec Sage self-Pod + Severance exile human choosers (both decline-optimal
deferrals awaiting sign-off), regression-suite membership + value-leaf/mulligan stages
(user-initiated), optional re-save of repaired refs (user-owned).

## SESSION 2026-09-05f — live play-test round 3: the persist-loop surface + the pod/chord resolution flow

User reports, all fixed and verified this session:

1. **Seed-6 "8x/14x Redcap" dialogs + messed-up history.** The committed plan was a BUNDLE --
   "sac 1 -> 2 damage, loop Redcap x8 -> 16 damage, loop Redcap x14" in ONE line (the loop K's
   are demand-computed ALTERNATIVES: lethal-by-damage vs lethal-by-growth) -- and each loop
   iteration re-asked the victim through the sacrifice dialog (8 identical dialogs), while the
   history showed only "returned (Persist) -> 2 damage" per iteration with NO sacrifice event.
   FIXES: (a) ApplyPersistLoop nulls the sacrifice chooser for the loop (victim + K are already
   explicit plan picks); (b) ApplySacCreatureOutlet emits a "sacrificed" event, so an iteration
   reads sac -> return -> ping; (c) bundle plans are hidden from the DISPLAY (never from
   enumeration -- the search's GT-measured plan space and recorded references, which replay by
   REAL index through the uncapped checker, are untouched; the chosen-extra emission still
   records a picked bundle).

2. **Fetch whitelists are search-only (USER: "they should be offered to the user").** The
   MTG_POD_PUT_NARROW whitelist now stands down under HumanPlayActive at both put-tutor sites
   (Pod + Chord); rollouts (HumanPlaySuppress) keep the narrowed search fan. Humans see every
   legal target (Scooze/Voice/Bloodthrone etc. reappeared in the fan and the pickers).

3. **Pod resolution flow (USER: "sacrifice chosen on the board, then the picker").**
   PerformPodActivate now runs the human flow at RESOLUTION: victim via the existing
   `sacrifice` board-click (source = the Pod), then the fetch via the `tutor_etb` picker built
   from the LIVE library at chosen-victim MV+1, unnarrowed, -1 = no fetch. DEFAULTS are the
   plan's baked values, so all 6 references replay losslessly through the checker's
   engine-default answers (verified: s1-s6 green, same outcomes). The menu display-collapses
   the (victim, fetch) fan to one representative per (rest-of-plan, Pod, pay mode) --
   Melira T4: 689 plans -> the pod cross-product gone; residue is real axes (cast order,
   MDFC faces, pay modes).

4. **Seed-7 Chord "forced a 1-drop, no picker" + summaries.** Three parts: (a) plan summaries
   now SHOW the baked fetch and X ("Chord of Calling -> Melira, Sylvok Outcast (X=2)") -- the
   invisible-fetch twins were the same summary-opacity class the checker hit; (b) Chord-class
   X-capped tutors (tutor_mv_max_is_x) re-ask the fetch at resolution via tutor_etb (baked
   default, -1 declines and STANDS); (c) the chord target axis display-collapses per X.
   Verified end-to-end on the user's seed-7 T3 state: commit "Chord (X=2)" -> picker offers
   all 8 MV<=2 creatures -> Melira enters T3.
   **KNOWN REMAINING GAP (disclosed): the one-line "Ignoble + Chord X=2 convoking the new
   Ignoble" is still not enumerated** -- convoke eligibility is computed against the pre-cast
   board, so the combined plan caps at X=1. The two-step flow works TODAY (commit Ignoble,
   re-prompt, Chord X=2 -> Melira -- verified), and X-at-resolution with live convoke recount
   is the designed follow-up if the user wants the single-line version.

All engine changes are human-surface-only by construction (choosers/event sink null in
autonomous, search, and rollout play). Battery: unit SUCCESS, scenarios 72/72, all 6 Melira
refs green; protocol + validate sweeps and smoke byte-identity recorded below on completion.

## SESSION 2026-09-05g — round 4: the one-line convoke X, the double dialog, the wrong noun

Three user reports on the chord-Melira flow, all fixed:

1. **"Doing them in the same plan it only gave me the 1-drop option."** The combined
   "dork + Chord" plan bakes X=1 (enumeration classifies convoke against the PRE-cast board), so
   the fetch picker capped at MV<=1. FIX -- SPARE-CONVOKE X EXTENSION at resolution: the picker
   now offers targets up to baked-X + (untapped convoke-eligible bodies, per the shared
   ClassifyConvokeBodies -- the just-cast summoning-sick dork qualifies), and picking k above the
   baked X taps k spare bodies, free-first, with a "Convoke -- tapped k more creature(s) to raise
   X" event. The downstream put cap is raised to the chosen MV (the first build tapped the bodies
   and then silently whiffed the put -- matches_types still filtered at the old cap). Verified:
   one-line "Ignoble Hierarch + Chord" -> picker offers all 8 MV<=2 -> Melira ENTERS, correct
   taps. Defaults never exceed the baked X, so references replay without extra taps.
   (This CLOSES the "one-line convoke X" gap disclosed in session 5f.)

2. **"It popped up two dialogs rather than one."** The queue-time choose fan still fanned the
   tutor TARGET (and the pod victim), which the resolution picker then asked again. FIX: those
   axes are dropped from CheckLine's sub fan (resolution_tutor: ActivatePod + tutor_mv_max_is_x
   casts); variants differing only there now share a sig and collapse. A bare "cast=Chord"
   validates to a choose fan of X ONLY (X=1 vs X=2 -- a real payment difference: X-via-mana taps
   a land, X-via-spare-convoke taps a body), then ONE picker asks the creature. The pod-victim
   sub is deleted outright (the board-click picks by battlefield index, superseding the " #k"
   disambiguation the sub existed for).

3. **"The text for sacrificing a creature says 'sacrifice a land'."** Frontend: the viewer's
   `sacrifice` panel hardcoded the land wording (it predates the creature-outlet/Pod reuse; the
   ENGINE note was already noun-derived since 2026-09-04). The panel now derives the noun from
   the options against the board (is_land by perm idx): land / creature / permanent.

Battery: unit SUCCESS, scenarios 72/72, protocol sweep 0 drift / 0 gaps / 0 contract (286 refs
incl. the user's new s6+s7 games, both committed), validate-line 0 REGRESSION (286), smoke 68/68
byte-identical, viewer sample checks PASS.

## SESSION 2026-09-05h — greedy-solve audit (USER: "ensure there are no greedy solves done in the middle of search")

Measured with the standing MTG_M2_YIELD_STATS apparatus (s5000 x 100 games, production settings,
the calibrated post-canon engine). **The searched part is CLEAN by the adopted 2026-09-02
sound-recipe standard** (canon continuation, MTG_BP_CANON_CONT default-ON):

- **Executor REAL main-phase greedy decisions: NONE.** Zero ROOT-kind fallbacks (nothing in the
  committed decision's own enumeration falls to greedy).
- **Executor breakpoint fallbacks: 20 / 100 games -- ALL base-class, 0 MISMATCH** (new cause
  split added to the probe this session: base = the committed plan carries no searched
  continuation, so the scoring rollout ran the IDENTICAL greedy Solve at the identical state
  through the twin applier -> realized == scored; MISMATCH = a searched continuation the
  executor could not replay -> would be a divergence, and reads zero).
- **Site 7 (the pod-chain breakpoint, built after the canon dossier): 1.88M greedy Solves per
  100 games, 99.0% in PLAIN ROLLOUTS** -- the class the user explicitly accepted ("rollouts
  being greedy is fine... I can always increase depth and budget to rely on them less. That is
  not true for the searched part"). Residue: rollout+rec 1.0%, overrun 0.4%, nested 0.04% --
  all rollout-side; canon fires/enums cover the captured applies.
- **Site 90 (9.7M): the horizon-leaf base case** -- the search's designed evaluator, not a
  mid-search fallback. The route to shrink it is the deck's VALUE LEAF (user-initiated stage,
  not yet run for Melira); until then every horizon evaluation is a greedy playout by design.

Conclusion: no greedy DECISION contaminates the searched structure for Melira Pod; greedy
survives only where the user's ruling accepts it (playouts) plus a provably-consistent base-arm
executor residue. The probe's new (base vs MISMATCH) split is permanent apparatus -- if MISMATCH
ever reads nonzero on a future audit, that is a real scored-vs-realized divergence to chase.

## SESSION 2026-09-05i — the xK loop dialog removed; loop targeting collapsed to the board

USER: "remove the weird x9/x17 Murderous Redcap dialog... doesn't make sense to the user and
seems redundant" + "we could possibly allow the targeting to be collapsed in this case."
It IS redundant: the demand-driven K's (lethal-by-damage vs lethal-by-growth) are the ENGINE's
alternatives for one job, the loop breaks the moment the opponent is dead, and a free outlet's
extra clean-return iterations cost nothing -- either K realizes the same game.

Shipped (the Pod-victim pattern, third application):
- The "loops xK <victim>" SubChoice is DELETED from the choose fan; loop variants of one outlet
  share a sig and collapse (rank-best representative carries the defaults).
- The menu display-collapses loop entries per outlet (key "S<source>loop"): seed-6 T4 now shows
  ONE "Carrion Feeder: loop Murderous Redcap x8 (persist) -> 16 damage" entry (was: x8 twin +
  x17 twin + bundles).
- ApplyPersistLoop asks WHICH creature loops ONCE, on the board, before the first iteration
  (default = the plan's baked victim -> predating references replay losslessly); the
  per-iteration suppression stays. K stays the plan's bake, bounded by break-at-lethal and the
  legality break.
Verified end-to-end on seed-6 T4: commit the one loop entry -> one sacrifice board-click
(default Redcap) -> loop runs to the kill (opp -12), history interleaved. All 10 references
green (s8/s9/s10 committed this session -- user's new games); protocol sweep 0 drift / 0 gaps
(287 refs), validate-line 0 REGRESSION, unit SUCCESS, scenarios 72/72, smoke 68/68
byte-identical (enumeration untouched -- display + choose-fan + resolution only).

## SESSION 2026-09-05j — engine vs the user's 10 reference games (USER: "do we at least match?")

Autonomous engine (production settings: profile-attached, d5/b20ms) on the SAME seed+game-index
as each user-played reference (MTG_DUMP_WINS per-game):

| seed/gi | user | engine | delta |
|---------|------|--------|-------|
| s1/g0   | 4    | 5      | engine 1 SLOWER |
| s2/g1   | 5    | 4      | engine 1 faster |
| s3/g2   | 5    | 4      | engine 1 faster |
| s4/g3   | 5    | 5 (inf T4) | match |
| s5/g4   | 4    | 6      | engine 2 SLOWER |
| s6/g5   | 4    | 6      | engine 2 SLOWER |
| s7/g6   | 5    | 5 (inf T5) | match |
| s8/g7   | 3    | 5      | engine 2 SLOWER |
| s9/g8   | 6    | 5      | engine 1 faster |
| s10/g9  | 4    | 5 (inf T5) | engine 1 SLOWER |

**Engine: 3 faster, 2 match, 5 SLOWER (three by 2 turns). Averages: user 4.5, engine 5.0.**
The answer to "do we at least match" is NO on half the set. The big gaps (s5/s6/s8, +2 each)
are the aggressive persist-combo kills -- s6 is the Feeder+Redcap loop kill the user executed
T4, s8 a T3 kill. NOT YET INVESTIGATED (compaction requested): the standing route is
per-game line comparison (explain_game / claude-play on the gap games) to classify each miss as
search-depth/budget vs heuristic vs modelling, then the heuristic-optimization loop for
anything systematic. Caveat: user games may include forced mulligans / side-channel steering
the autonomous engine decides differently -- same shuffle, whole-game comparison.

## SESSION 2026-09-05k — the "loses 5 of 10" table was WRONG; the two real gaps diagnosed and FIXED

**CORRECTION to 5j: the comparison above measured the WRONG physical games.** A viewer
reference saved at (seed s, game-index g) replays as `--seed s --game-index g --games 1`;
the 5j run used the BATCH repro shape (`--seed s --games g+1`, grep gi=g), whose per-game
shuffle seed is base+gi — a DIFFERENT shuffle for every g>0. Only the s1/g0 row compared
like with like. The two repro schemes are both real; they index different worlds:

* batch-run game gi  ->  `--seed base+gi --games 1`   (the batch-game-repro-seed rule)
* viewer reference   ->  `--seed s --game-index g --games 1`   (exactly as saved)

Re-measured on the correct games (opening hands verified identical to each ref's attempt-0
hand), PRE-fix engine vs user: **1 faster (s3), 7 match, 2 slower by 1 turn (s1, s10)** —
user avg 4.5, engine 4.6. Not 5-of-10-with-+2s; the +2 rows were shuffle artifacts.

### The two real gaps: one blindness, two faces

Both misses were the same enumeration hole — **an action whose enabler arrives mid-plan is
invisible at CollectActions**, which scans the battlefield:

* **s1 (user T4, engine was T5):** the user's T3 is "cast Pod {3}+2 life, hold the 4th
  source, activate {1}+2 life the same main" (sac Finks -> Celes). ActivatePod was only ever
  emitted for battlefield Pods, so cast-and-activate could not be ONE plan; it survived only
  as the fragile hold-a-source + main-2 route (the 5d tight-pool twin), which the root never
  ranked first. Probes: budget/depth up to d7/b2000 never found T4; MTG_POD_PUT_NARROW=0
  found T4 on this shuffle only by dodging the pairing (Pod landed T2) — the whitelist was a
  red herring, left untouched.
* **s10 (user T4, engine was T5):** the user's T4 is "cast Melira #2 AND grow-loop Kitchen
  Finks x17 (Feeder -> ~20 power), then attack". The loop variants were gated on the closer
  being active AT COLLECT, and the loop cannot defer to main 2 because the lethal attack
  sits between the mains — the plan was unenumerable at any depth/budget (verified). Engine
  played the identical parts a turn late (closer cast M1, loop M2, kill T5).

### The fix: cast-and-activate / cast-and-loop pairing (MTG_POD_HAND_PAIR, default ON)

All in the candidate space — no breakpoint, no bp_seen accounting, no executor drift:

1. **Hand-Pod ActivatePod emission** (TurnSolver CollectActions): the pod emission body is
   shared by a lambda; battlefield sources unchanged, plus one emission per distinct hand
   Pod NAME, gated on the pool covering cast+activation with every phyrexian pip life-paid
   (the true mana floor). The cast lands in the cast pass, the activation in the TRAILING
   pass, which runs after it in both worlds — pairing in one subset is sound by ordering.
2. **Closer-castable loop emission**: the persist-loop gate becomes closer ACTIVE or closer
   CLASS card in hand (Melira/Vizier/Celes params, matching NotePodRoles).
3. **Two subset rules** (SubsetHasStrandedPodActivation / SubsetHasUnclosedPersistLoop, the
   SubsetHasStrandedEquip pattern, lockstep in both walkers): a hand-Pod activation without
   its co-cast, or a persist loop with no closer active-or-cast, is rejected — no stranded
   pay-then-no-op ever reaches scoring or a human menu.
4. **ResolvePodSourceId** (SpellEffects.h, shared): the cast applies BY NAME, so the copy
   that materialises may differ from the hand copy the action named; both trailing apply
   sites (rollout/leaf + executor twin) re-resolve the source by name identically.

### Results (all at production defaults d5/b20)

| game | user | engine BEFORE | engine AFTER |
|------|------|---------------|--------------|
| s1   | 4    | 5             | **4** |
| s10  | 4    | 5             | **4** |
| s3   | 5    | 4             | 4 (still faster) |
| other 7 | — | match         | match (unchanged) |

**Engine now matches or beats the user on all 10 references** (1 faster, 9 match; engine
avg 4.4 vs user 4.5). The viewer offers the user's exact T3 line as one plan
("cast: Birthing Pod (pay 2 life), Birthing Pod: sac Melira -> Kitchen Finks (pay 2 life)").

Validation: unit SUCCESS; scenarios 72/72; protocol sweep 0 play-drift / 0 shuffle-dead /
0 enum-gap (289 refs; 5 known Mirrorwing mull-drift); validate-line 0 REGRESSION; smoke
68/68 byte-identical (the pairing is param-gated — no other deck has pod_mv_delta or
closer/persist params). Benchmark s5000x100: **avg 4.83 vs 4.98 OFF (-0.15t), inf 37 vs 36,
CPU 920s vs 818s user (+12.5%, Melira-only; wall flat at ~1:25 on 32 threads)**. Adopted
default-ON per the user's direct request to fix these losses; `MTG_POD_HAND_PAIR=0` is the
A/B hatch.

Residual: none of the 10 references now shows an engine deficit. s9 (user 6, engine 6) and
s2 (5/5) are matches, not wins — no action. The put whitelist (MTG_POD_PUT_NARROW) stays as
shipped; the 5j-era suspicion against it is closed as a wrong-shuffle artifact.
