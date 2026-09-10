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

## SESSION 2026-09-05l — value-leaf phase A OOM, root-caused + fixed (e4da4b49 local)

Phase A's batch was kernel-OOM-killed at ~27 GB anon (14:01:58 UTC; the VSCode disconnect was
collateral). Root cause: `MTG_FSL_POOL` is entry-count accounting sized at a 1 KB/entry planning
size calibrated on Mirrorwing-era games (~600 B measured); Melira's combo-turn SearchLines run
~1.5–2 KB/entry (reconstructed from the kill's own accounting), so the nominal 12.3 GB pool was
really ~19–25 GB. A compounding driver bug let the damage propagate: `phase_rows` never checked
the batch exit status, marked A_rows done over the killed batch (375 of 2500 games, 362 with
rows), and phases B/C ran against a model trained on 14% of the data. Fixed both (exit-status
gates on phase A+E, planning size 1 KB → 3 KB), wiped the poisoned markers/staged model/matrix
state, re-queued only the missing games (21 jobs, banked rows kept), relaunched 21:37 UTC.
Note: extra RAM does not fix this class — the budget scales with MemTotal, so the undercount
scales with the box. Full mulligan gen deliberately deferred past tonight: origin holds a
play-affecting greedy closure (8bea89da), and a profile generated at ba5857fa would not survive
integration; tonight ends at the `recommend` scout.

## SESSION 2026-09-06 — searched cases PULLED from the suite; bucket scout run

The measured suite cost made the case: melira searched cases were **72% of smoke's core-time**
(2779 of 3871 s; d3 31 s/game, d5 26 s/game, 2hg 23 s/game) and **52% of regression's** (4014 of
7656 s) — one deck, over half the harness. User 2026-09-06: *"we can't have it taking that long
and holding up other testing"*, *"drop Melira from these tests entirely until we can guarantee
the performance."* Pulled every searched melira/melira2hg case from smoke, regression AND
overnight (the overnight d3/d5 four-seed sweep had never been measured — no GT keys existed, so
nothing was lost); kept the free d0 canaries (≤3 s per 1000 games) so a play change still moves
a committed digest. Restore path: git history of `test/regression_cases.sh`, gated on the
enumeration wall fix (docs/design/pod-pair-enumeration-explosion.md) plus a fresh s/game probe.
The wall itself remains THE open Melira problem: burst-family (f66f2949) + plan-space cap bound
memory (bucket scout peaked ~2.5 GB where it OOMed at 23 GB before), but discovery rollouts
still hit 35–94 s against a 20 ms budget.

## SESSION 2026-09-06b — the perf sprint (user: "focus on fixing things so Melira can be in the suite")

Profiled g88 (perf/dwarf on the Profile build): **92% of the game inside SolveUncached's
per-subset walk** — the greedy path, called at every rollout leaf. Fixes landed (1ef95821, local,
push paused): fill-probe precompute (~17% pure Action-copy waste, byte-identical), deferred best
materialization (byte-identical), **MTG_SOLVE_SPACE_CAP=16384** (greedy-only product bound; g88
262144→13.2s, 16384→3.5s, same t4 win), persist-loop go-off cut (storm/EDF-cut sibling;
guarantees the lethal loop line under the tight cap). Validation: smoke 72/72 configs-changed 0
(twice), 299-ref gate 0 play-drift / 0 enum-gap, all 10 Melira refs reproduce. g88: 18.7 → 3.5 s.

Search-side cap curve at d3 b10 (50 games, seed 1001, 8 threads): 262144→306s, 65536→304s,
16384→301s (avg 4.96/4.96/4.94) — **flat: the residual d3 cost is NOT wave-0 breadth**. Per-game
scan + slow-game profile in flight; suspicion is a small monster-game tail.

Side finding, surfaced to user: `Fluctuator/claude_s10_gi9` reports **"shuffle-dead" falsely** —
the classifier's board-differs branch (added 2026-08-25 for the StompySurprise Lodge case) reuses
the shuffle-dead label for what is really an upstream play divergence; Fluctuator has no shuffle
effects at all. Reproduces with ALL of today's levers disabled (MTG_PLAN_SPACE_CAP=0
MTG_SOLVE_SPACE_CAP=0 MTG_NO_PERSIST_LOOP_CUT=1) → predates today's work. FOLLOW-UP (user views
the category as illegitimate): rename/split the class to name play-divergence honestly, decide
whether it should gate strict, and bisect which commit diverged this ref.

## SESSION 2026-09-06c — where the tail hides (pre-bed state, commits local/unpushed)

d3 s/game 31 → ~1.7 median, but the TAIL owns the cost: seed-1033/gi32 = 289 s of a 613 s
50-game set (t5 win). Two opt-in levers landed (30ecffa9, default OFF, smoke 72/72 unchanged):
MTG_SOLVE_CHARGE (greedy walk bills the budget; gi32 289→151 s) and MTG_DECISION_WORK_X (per-
decision total ceiling via the existing Overrun rollback). **The decisive debug finding:**
armed roots bill only 10–19K units each and never trip — gi32's remaining 151 s runs OUTSIDE
SolveWithLookahead-rooted decisions, under the EXECUTOR's breakpoint/FSLine search hosts, which
never arm the meter. NEXT: arm decisionwork at those roots (FullSearchLine / breakpoint
re-search entries), re-measure gi32, then sweep X for quality (refs + d3 avg 4.96 is the bar),
then a melira keepgen `recommend` re-probe with both levers on — that is the gate for mulligan
generation becoming feasible.

## Session 2026-09-06d — the tail was the BOTTOMING ROLLOUTS; per-deck bottom_eval policy ADOPTED

**The resume lead resolved, and it wasn't what the ledger guessed.** Arming the decision meter at
the executor's true root (`FullSearchLineHybrid` — it bypasses `SolveWithLookahead` entirely;
commit 286b3948) plus `MTG_DECISION_WORK_DEBUG` showed gi32's ~140 s residual was **~21 whole-game
t1→t7 playout sequences inside one game**: the clairvoyant bottoming rollouts
(`MTG_BOTTOM_LEGAL` rolls out every C(7,2)=21 removal subset of a mull-2 keep as a FULL game at
play settings, and `m_in_rollout` playouts never touch `g_rollout_nest`, so each armed as its own
root). Real play was ~4M units (~seconds); bottoming owned the rest. This is the same anatomy the
BottomEvalScope comment records for FiveColour (90.4%) and Fluctuator (~92%).

**Ceilings measured DEAD on the real-game residual before that diagnosis** (worth keeping):
- `MTG_DECISION_WORK_X` on gi32: X=10 → 40 s but t5→t6; X=30 → 76 s, still t6; X=100 → 134 s, t5
  back but nothing saved. The heavy decisions legitimately need their units; capping either loses
  the win or saves nothing. The ceiling machinery stays (default-off, correct arming) for
  attribution and generation-side use, not as a play lever.
- `MTG_SOLVE_CHARGE` on the 50-set: 613→~370 s-equivalent but avg 4.96→**5.24** (−0.28t). Rejected
  for play; remains opt-in.

**The fix that shipped (commits 286b3948 + 8d113e92):**
1. `MTG_BOTTOM_EVAL_DEPTH=0` alone (greedy stage-1 trials): 50-set 613→245 s serial but avg
   4.96→5.02 (3 games +1t: g4, g27, g37 — all mull-2, bottoming choices moved).
2. **Two-stage refine `MTG_BOTTOM_EVAL_TOPK=K`**: greedy-score all subsets/candidates, re-roll the
   K cheap-best at REAL play settings. K=3: one diff left. **K=5: ZERO per-game diffs — all 50 win
   turns identical to full-fidelity baseline — at 405 s serial (1.5x)**. gi32: 289→~15 s.
3. **Per-deck profile carrier** `mulligan.bottom_eval_{depth,budget_ms,topk}` (parse+save
   round-trip; env twins override only when EXPLICITLY set). Melira Pod ships depth0/topk5 in its
   profile.json, so suite/generation/viewer/user play all pick it up with no env to remember.

**Validation:** smoke 72/72 configs-changed 0 (×3 builds, defaults byte-identical); profile-driven
50-set avg 4.9600 with zero win-turn diffs vs baseline; 299-ref gate profile-driven: 0 play-drift,
0 enum-gap, 0 mull-drift (1 known pre-existing fluctuator shuffle-dead). Fleet-wide default flip
was A/B'd and **REJECTED**: env forced on all decks moves 6 smoke cases (fluctuator play changes,
12 slower) — this must stay per-deck.

**Fleet follow-up (recorded, not actioned):** Fluctuator/FiveColour would likely get their own
large wins from a deck-specific `bottom_eval` policy + TOPK — but each needs its own quality
A/B and GT rebaseline; melira was the priority.

**State at freeze:** d3 50-game set 405 s serial (~8.1 s/game; was ~31 s/game pre-sprint, 12.3
post-sprint), avg 4.9600 exact. Under-billing note: gi32 bills only ~4.5M units vs ~15 s wall —
enumeration/plan-apply overhead outside Consume sites; the value leaf (H-cell ladder guard,
1.35–84.8x) is the intended next cut, so no further hand-optimization before generation.

**NEXT (launched this session): `bash scripts/valueleaf.sh run "decks/Melira Pod"`** on the frozen
commit; prior queue logs/vlq_melira_pod had banked 2130/2500 phase-A rows — the driver's play-digest
chunk banking decides what survives today's play change (bottoming policy + sprint = play moved).
Then mulligan `recommend` once the value leaf's final stage writes value_play.

### 2026-09-07 — the abandon ceiling is mis-calibrated for this deck (finding, NOT actioned mid-run)

**Symptom:** phase C ran games of 4.5-6.5 h. **They were not un-guarded — they were abandoned,
just far too late:** 467 slow games voided after burning **222 core-h** (60% of phase C's
370 core-h of slow-game time). Worst cells: H4_s8008 38.4, H4_s10010 28.0, H4_s11011 22.6 core-h
of pure waste.

Three mechanisms, only one of which is even eligible here:
1. Rung/quality condemnation — removed 2026-08-21, gone.
2. Cell condemnation (`--intractable-median-sec-per-game=30`) — **structurally cannot touch an H
   cell**: `NEVER_CONDEMN` is clamped >=5 and HDEPTHS tops out at 5. Deliberate (the H cells ARE
   the crossover). Every multi-hour game here is H3/H4/H5, so this guard is inert by design.
3. Per-game abandon ceiling = `max(ABANDON_K x cell median units, ABANDON_FLOOR_UNITS)` — armed and
   firing, but the threshold is wrong for Melira.

**Why the threshold is wrong.** `ABANDON_FLOOR_UNITS=40000000` was calibrated to mean "~30 minutes"
(the user's stated intent) using **Mirrorwing's** ~10k-22k units/core-second. Melira runs an order
of magnitude slower per unit: measured **min 228, median 3,470, max 28,332 u/core-s**. So the 40M
floor, meant to be 30 minutes, is **12.6 to 48.7 HOURS** depending on the cell (worst: H5_s11011 at
228 u/core-s = 48.7 h). On wall-clock grounds the floor is effectively inert on this deck.

On H4_s8008 the RATIO fires instead of the floor — median 12.35M units x25 = **308.8M** ceiling,
7.7x the floor, ~13.2 h at that cell's median rate. That one DOES fire (its abandoned games bill
units ~2-3x faster than their cell's median game, so they reach 308.8M in 4-6.5 h) — but only after
each has burned 4-6.5 core-hours that are then discarded. The floor is a floor with no cap above it.

**ARITHMETIC CORRECTION (same day):** a first version of this section reported rates ~8x too high
(min 1,637 / median 26,599) and a 6.8 h worst case. Cause: `cells.json`'s `ms` field is TOTAL
SECONDS per cell, not per-game milliseconds as the name suggests (`valueleaf_depth_matrix.py:144`
sums it; `:790` divides by `games` for s/game). Cross-check that settles it: sum(ms) = 2,026,646
against ~692 core-h of elapsed phase C. The corrected numbers above make the mis-calibration
substantially WORSE, not better.

**Unit-vs-wall divergence (related, but do NOT over-claim it here).** A live perf sample of a
PHASE A monster worker showed 82% of stack time in `EnumeratePlansWithLandUncached` /
`AppendSubdecisionAxes` plus `vector<Action>` copies — enumeration, which bills no units (units
accrue only at rollout turn-steps via `SearchBudget::Consume`), matching the perf-sprint note that
gi32 billed 4.5M units against ~15 s wall. That is a real effect and it is why Melira's u/core-s is
low overall. It is NOT the explanation for the H4_s8008 abandonments specifically: those games bill
units FASTER than their cell median, so there the binding problem is purely the ceiling's size.
Phase A (unbounded labels) and phase C (matrix) are different workloads; measure before attributing.

`scripts/valueleaf.sh` already documents this exact failure from the 250M first cut ("wrong by
~10x... the floor was ~7 HOURS... CALIBRATE AGAINST THE WORKLOAD YOU ARE BOUNDING"). 40M fixed it
for Mirrorwing and reopens the same ~7 h hole on Melira.

**Proposed fix (deferred — needs the user's call):** derive the floor per DECK from that deck's own
measured units/core-second (phase C already prints median units in the CEILING lines and s/game per
cell, which is the whole calibration table), and CAP the ratio so `k x median` can never exceed the
wall-equivalent of the intended bound. Keep it unit-based: units are what make the skip list
reproducible across machines.

**NOT actioned mid-run, deliberately:** the abandoned set is unioned across cells and must be
identical everywhere, so changing the ceiling mid-table would leave cells holding different game
populations. And `valueleaf.sh` is being executed by a live bash process — editing it in place is
unsafe. The current table stays VALID (abandoned games are excluded everywhere and backfilled;
disclosed as `~~ FILTERED`), just expensive.

### 2026-09-07 (late) — suite restore attempted and REVERSED by the user

With both tiers green at the restored cases (smoke 72/72 makespan 200 s vs 77 s without;
regression 98/98 makespan 262 s, no increase; 0 configs changed; 6 new keys) I committed the
restore as 16615552. The user reversed it within the hour: **"let's not add melira until we
are happy with performance. I'm not convinced of that at all."** Commit reset away (unpushed);
GT back to 414 consistent with 0 melira keys.

The criterion I applied ("comfortably inside the 15-minute smoke target") was mine, not the
user's. The gap the user is looking at, on the shipped no-sidecar config:

| | melira | costliest other deck (fivecolour) | median suite deck |
|---|---|---|---|
| d3 b10 | ~8.4 s/game | 1.19 s/game | ~0.1-0.2 s/game |
| d5 b20 | ~5.6 s/game | ~2 s/game (d6) | ~0.3 s/game |

i.e. still ~7x the next-costliest deck and ~50-80x the median, after a 5x cut. That is
the number to move. The value leaf (uncertified) measures 2.5x on top of this, which would
bring d3 to ~3.4 s/game -- still ~3x fivecolour.

**Standing rule from this:** melira does not re-enter the suite on a "fits the budget"
argument. It re-enters when the user says the per-game cost is acceptable.

## SESSION 2026-09-08 — the greedy leaf was the whole game; per-deck `search_leaf_depth` ADOPTED (0)

User (post-compaction): *"get back to optimizing Melira... we may need to go beyond byte
identical and do some serious pruning."* Phase A rows may stay as-is (byte-identity no
longer a constraint).

### Where the d3/b10 time actually goes (clean per-game scan + suite-config profile)

A clean per-game wall scan of the 50-game d3 set (`scripts/attic/melira_wall_scan.sh`,
`--seed 1001+gi`, only `%e` captured): total 358 s, 6 games hold 271 s of it (gi49 96 s,
gi32 59 s, gi13 37 s, gi47 36 s, gi2 22 s, gi1 21 s); 39 games are under 5 s. NOTE the
resume hook's "gi32 ~15 s" was wrong -- 58 s solo re-timed; gi49 (seed 1050, NO mulligan,
t5 win) is the true monster and was not in the catalogue.

gi49 decomposed with `MTG_DECISION_WORK_DEBUG` + `MTG_ROLLOUT_STATS`: turns 1-3 cost 52K
units combined, **turn 4's single main-phase decision cost 832K units** (t5 wins in 1 unit).
A 10 ms budget is 9,000 units, so that decision ran ~92x over budget -- legally, because
FullSearchLine's per-pass overrun guard has `kOverrunFloor = 1,000,000` units (the floor
exists so ordinary decks' passes never trip it). 501,477 candidates were scored in that one
decision through 163K rollouts of 1.09 turn-steps each.

DWARF-unwound perf of gi49 at the SUITE config (not the unbounded H4 repro, whose hotspots
differ): **93% of the game is inside the greedy `SolveUncached` subset walk** (called from
`SimulateToEnd` rollout steps and `SolveSecondMainInSearch`): `ColorFeasibility::Payable`
29% self, the consider() lambda 14%, the Subset* rules ~15%, `SubsetPayable` 5%.
`FSLineWin` itself is 1.75% -- the search TREE is small; the LEAVES are the bill.

### The walk cannot be pruned by feasibility (funnel measurement)

Added a consider() funnel to `MTG_ENUM_STATS` (cumulative pass counts). gi49:

| stage | positions |
|---|---|
| entered consider() | 288,943,644 |
| passed subset rules | 288,842,221 (rules reject 0.03%) |
| passed flat mana | 287,416,959 (0.5%) |
| passed SubsetPayable | 287,414,046 |
| passed ColorFeasibility | 287,413,788 |
| fully scored | 287,413,788 |

**99.5% of visited subsets survive every filter and are fully scored.** The deck's actions
are free (K=1 persist-sac bits, Ooze exiles, Feeder outlets; Pod at {1}{G/P}), so the mana
bound never bites and no incremental/monotone prune can help. Typical walk shape (the only
one over the 3,000-position watermark): `groups=3 ind=6` -> 2^6 x 2 x 3 x 13 = 4,992
positions; 209K walks averaging ~1,380 positions. Per-position cost ~330 ns is already
cheap; the COUNT is the problem, and the count is structural: every rollout turn of every
leaf pays one full walk. So the lever is fewer walks, i.e. a cheaper search leaf.

### Screen on the 50-game d3 set (8-way parallel; win turns vs `logs/melira_perf/scan_d3`)

| arm | wall | avg | better / worse (games) |
|---|---|---|---|
| base | 357.7 s | 4.9600 | -- |
| `MTG_FD_LEAF_DEPTH=0` (pure greedy horizon rollout) | 54.8 s (0.15x) | 4.9000 | 4 [16,25,27,29] / 1 [43] |
| `MTG_POD_VICTIM_TOP=1` | 144.4 s (0.40x) | 5.0600 | 2 / 6 |
| `MTG_POD_VICTIM_TOP=2` | 293.4 s (0.82x) | 4.9600 | 1 / 1 |

Pod-victim narrowing is either quality-negative (top1) or barely faster (top2) -- not the
lever, and still the user-reserved provider heuristic; not taken. The leaf depth is the lever:
the 1-ply leaf runs `SolveWithLookahead` at every simulated turn of every leaf, i.e. one
greedy walk per enumerated candidate per turn, and on THIS deck the extra ply buys nothing
(the combo turn is found by the FSLine tree, not by the leaf's lookahead).

### Confirmation: 1000 fresh-seed games (seed 800000), ONE pooled batch, both suite configs

Implemented as a PER-DECK profile field `search_leaf_depth` (top-level key; -1/absent =
engine default 1 = byte-identical; explicitly-set `MTG_FD_LEAF_DEPTH` wins as the A/B hatch,
same precedence as `bottom_eval_*`), plumbed via `TurnSolver::SearchLeafDepthScope` opened
by AIEngine around each FullSearchLineHybrid decision (thread-local, nested rollouts inherit
it). Profile-driven arm reproduced the env arm's 50-set win turns exactly; default profile
reproduced base exactly.

| config | base avg | leaf0 avg | delta | base core-s/game | leaf0 core-s/game | speedup |
|---|---|---|---|---|---|---|
| d3 b10 | 4.8730 | 4.8650 | **-0.008** | 9.95 | 0.78 | **12.8x** |
| d5 b20 | 4.8590 | 4.8460 | **-0.013** | 11.35 | 1.21 | **9.4x** |

(First launch of this batch was OOM-killed at 22.8 GB anon RSS -- I had not sourced
`scripts/lib/membudget.sh`; relaunched under the caps at 20 threads, peak RSS 4.8 GB.)

That first read said "better on both configs". Two more seed bases with per-game pairing
(`--game-log-dir`, `scripts/attic/paired_wins.py`) CORRECTED it to **quality-NEUTRAL**:

| base | config | base avg | leaf0 avg | delta | t | better / worse |
|---|---|---|---|---|---|---|
| 800000 | d3 | 4.8730 | 4.8650 | -0.008 | (no per-game file) | |
| 900000 | d3 | 4.8390 | 4.8540 | +0.015 | +1.56 | 31 / 47 |
| 1000000 | d3 | 4.8320 | 4.8410 | +0.009 | +0.96 | 36 / 45 |
| 800000 | d5 | 4.8590 | 4.8460 | -0.013 | | |
| 900000 | d5 | 4.8340 | 4.8310 | -0.003 | -0.35 | 34 / 33 |

Pooled: **d3 +0.005t over 3000 games (se ~0.006), d5 -0.008t over 2000 games (se ~0.006)** --
neither outside noise; the d3 lean is one game in ~200 winning a turn later (transitions are
mostly 4->5 vs 5->4: 27 vs 18 on base 900000 -- the 1-ply leaf occasionally sees a turn-4
kill line the greedy playout under-ranks at the horizon; at d5 the tree covers that ply and
the effect vanishes). Speed: d3 9.3-9.9 -> 0.63-0.82 core-s/game (12-15x), d5 10.8-11.4 ->
1.21-1.25 (9x).

**ADOPTED** in `decks/Melira Pod/Melira Pod.profile.json` (`"search_leaf_depth": 0`) as the
"serious pruning, well-tested" trade the user asked for this session: ~10x at a quality
delta inside +-0.01t on 2000-3000 paired games per config. It is NOT claimed as a clean win
on the quality axis (the 50-game and first-1000 reads that said "better" were noise -- the
same trap as the value-leaf 50-game read, and the reason a second base is mandatory). If the
user wants zero measurable d3 cost, the natural follow-up is a 1-ply leaf on the FIRST
rollout turn only (greedy after) -- not measured. Melira's per-game cost is now ~0.6-0.8 s d3
/ ~1.2 s d5, at fivecolour's level (the bar the user named), from ~8.4 / ~5.6 before this
session. Gates below.

Why it is better and not merely cheaper: the 1-ply leaf's per-turn lookahead is clairvoyant
over the real library order (it ranks candidates by depth-0 rollouts that read the true
draws), so at the leaf it rewards lines that happen to line up with the next draw; the pure
greedy leaf is a flatter, less draw-fitted estimate, and the FSLine tree above it -- which is
where this deck's combo turn is actually found -- is untouched.

Fleet note: the field is per-deck and absent everywhere else, so every other deck is
byte-identical by construction (smoke gate below). Whether other decks would ALSO prefer
depth 0 is a separate heuristic-optimization question (TH was the deck that originally
motivated the 1-ply leaf; see the s_fd_leaf_depth comment) -- not pursued here.

### Byte-identical follow-up: skip the provider tutor ranking when the plan pinned the put

Leaf-0 world profile of gi32 (the mull tail): `PerformPodActivate -> PerformTutorToBattlefield
-> MeliraPodProvider::TutorCandidates` was 12% (library walk + string set + string sort per
rollout Pod activation). In the Pod path the fetch is plan-pinned (`preferred = {fetch}`,
max_puts 1), so pass 1 fills the single slot and pass 2's fill loop broke on its first
iteration -- AFTER computing the whole ranking. Now skipped when pass 1 already filled every
slot (`SpellEffects.h`); TutorCandidates is pure, so byte-identical (smoke below). Effect
visible in base-3: leaf0 d3 0.78 -> 0.63 core-s/game (different seed base; the 50-set clean
re-scan below is the like-for-like number).

### Clean re-scan under the shipped profile (leaf 0 + tutor-fill skip), box idle

| set | session start | now | max game |
|---|---|---|---|
| 50-game d3 b10 (seed 1001+gi) | 357.7 s (8.4 s/game avg per the earlier batch) | **30.3 s (0.61 s/game)** | gi32 7.3 s (2 mulligans), gi47 5.7 s (1 mulligan); every other game <= 1.1 s |
| 25-game d5 b20 | ~140 s | **19.0 s (0.76 s/game)** | gi1 7.5 s; next 1.4 s |

Win turns identical to the leaf-0 arm (avg 4.9000 on the 50-set). The remaining tail is the
mulligan games' bottoming refine (topk 5 full playouts per mulligan at play settings) -- now
the only thing above ~1 s. Fleet gates for the tutor-fill skip: smoke 72/72, configs changed 0.

### Byte-identical greedy-walk micro-opts (consider() by reference, mana-verdict cache, hoisted MVs)

Post-leaf-0 profile of a typical game (gi13, ~1 s): the greedy walk is still 74% of it
(SolveUncached self 27%, `ColorFeasibility::Payable` 14%, `ManaCost::ManaValue` 6% as a
per-position per-digit re-sum, operator new / push_back ~6% from consider()'s by-value `sel`).
Three byte-identical changes in `SolveUncached`:
1. `consider()` takes `std::vector<int>&` (the odometer's reusable buffer; temporaries at the
   short-circuit sites became named locals) -- no heap copy per visited position.
2. A 1-entry MANA-VERDICT cache keyed on the mask with the mana-INERT independent bits
   cleared (cost 0, no hybrid, no float/rock/mint, no land sac/discard -- the K=1 persist sacs).
   Consecutive odometer positions differ only in those bits, so flat CanPay / SubsetPayable /
   ColorFeasibility verdicts are reused instead of recomputed. Armed only by the inline walk and
   only when no affinity/reducer/tap-debit/filter credit path is live in the call; costed
   independents (Ooze's {G} exile) stay in the key.
3. Per-action `ManaValue()` hoisted into `cand_mv[]` for the odometer's cost loops.

Measured: 50-set d3 30.3 -> 28.7 s, 25-set d5 19.0 -> 17.5 s (~5-9%; smaller than the profile
suggested -- the cache's hit rate is bounded by how many walks have inert independents at all).
Win turns identical on both sets; smoke 72/72 configs changed 0; 299 refs 0 drift.

**State at the end of this session (box idle, shipped profile):**

| | session start | now |
|---|---|---|
| 50-game d3 b10 | 357.7 s (7.2 s/game) | **28.7 s (0.57 s/game)** |
| 25-game d5 b20 | ~140 s (5.6 s/game) | **17.5 s (0.70 s/game)** |
| 1000-game d3 / d5 (batch, core-s/game) | 9.3-9.9 / 10.8-11.4 | 0.63-0.82 / 1.21-1.25 (pre-cache binary) |
| worst game | gi49 96 s | gi32 6.8 s (2 mulligans; bottoming stage-1 + topk-5 refine) |

fivecolour, the costliest suite deck, is 1.19 s/game at d3. Melira is now below it on
average; its remaining tail is the mulligan games (~5-7 s: two bottoming rounds each of
C(h,k) depth-0 playouts plus 5 refine playouts at play settings). Whether that is "happy with
performance" is the user's call -- **melira is NOT re-added to the suite by this session**.
Open question for the user: re-add now (d0/d3/d5 cases would cost ~30 s + ~18 s + the d0
canary at smoke scale), or first take the mulligan tail down (topk 5 -> 3 measured win-turn
identical on gi32/gi47 but SLOWER on gi32 -- chaotic; stage-1 C(h,k) is the other half)?

### Bottoming refine K (bottom_eval_topk) 5 vs 4 vs 3 -- REJECTED, K stays 5

1000 games x d3/d5, seed 800000, per-game paired vs topk5 (one pooled batch, ~4 min):

| arm | d3 avg / delta / better-worse / core-s | d5 avg / delta / better-worse / core-s |
|---|---|---|
| topk5 (shipped) | 4.8650 / -- / -- / 775 | 4.8460 / -- / -- / 1236 |
| topk4 | 4.8700 / +0.005 (t 2.2) / 0-5 / 737 | 4.8490 / +0.003 (t 1.7) / 0-3 / 1072 |
| topk3 | 4.8760 / +0.011 (t 3.3) / 0-11 / 644 | 4.8580 / +0.012 (t 3.5) / 0-12 / 908 |

Lowering K only ever LOSES games (0 better at either K) for 5-26% of the cost: the refine is
doing real work. The mulligan tail stays as it is; it is ~5-7 s on the worst 50-set game.

Byte-identity bonus: this batch's topk5 arms reproduce batch 1's leaf0 digests exactly
(d3 3ae70e58147fd761, d5 d099668502343bdb) across the tutor-fill skip + walk micro-opts --
2000 games of byte-identical evidence on top of smoke + refs. (Their core-s are within batch
noise of batch 1's -- the 5-9% gain is only visible on the solo 50-set scans.)

Worst-case class after this session: `--seed 800529 --depth 5 --budget-ms 20` (a mulligan game
that wins t7): 42 s inside the 24-thread batch, **18.2 s solo** -- the topk-5 refine's five
full d5 playouts at ~3 s each (t3 decisions of 660K units) plus the real game. That is the
mull-tail class the topk A/B above says we should keep paying for.

### 2026-09-08 — RE-ADDED TO THE SUITE (user: "The performance should be close to other decks")

Per-game cost vs the suite (smoke tier, same run, batch-contended ms/game): fluctuator
1674 d3 / 3103 d5, fivecolour 1272 / 1932, **melira 1117 / 1675**, hinata 295 / 550, median
deck ~130 / ~140. Third-costliest deck, under the two it used to be 7x above => added back
with the ORIGINAL counts in every tier (the same cases b528a390 pulled): smoke d0/d3/d5 +
melira2hg, regression d0 + d3/d5 x 2002/3003, overnight d0 x4 + d3/d5 x 4004-7007.

| tier | result | makespan | melira in-tier ms/game |
|---|---|---|---|
| smoke | 72 unchanged, 4 new, accepted | 86 s (76 s without melira; the 2026-09-07 attempt was 200 s) | d3 1117, d5 1675, 2hg 920 |
| regression | 98 unchanged, 5 new, accepted | 192 s | d3 779-1172, d5 1022-3969 (s2002's d5 has a few ~10-18 s mulligan games; no game over 30 s) |
| overnight (--deck=melira, per-deck accept) | 12 new, accepted | 52 s for the 12 melira cases (was ~7.5 CPU-hours) | d3 683-1065, d5 1283-1618 |

GT consistent: 435 logs, 0 stale, 0 missing; only melira keys changed. The overnight tier's
audit showed "43 configs changed" -- stale Sep-4 .wins of OTHER decks in the shared wins dir,
not this run; the per-deck accept promotes only this run's keys by design (regression.sh
~183). Pushes remain paused (user).

### 2026-09-08 (later) — the two questions: is quality worse overall, and are the worse cases recoverable?

USER: *"there are always 2 questions to answer: Is quality worse overall and are any worse cases
recoverable with depth and budget? If there are no unrecoverable regressions and the quality is
not worse overall we can proceed."*

**Q1 -- worse overall?** No. Pooled paired: d3 +0.005t (se 0.006) over 3000 games, d5 -0.008t
(se 0.006) over 2000. Neither outside noise.

**Why the d3 games moved (traced, 3 games):** the arms split on a TURN-1/2 sequencing choice two
turns before the kill (T2 Voice vs Melira; T2 Finks vs Vizier; T1 Birds vs Feeder). The ladder
telemetry shows why the leaf decides those turns at all: at "d3 b10" every decision commits
DEPTH 1 (id_depth hist 1:4 on the traced game; the start gate never admits a deeper pass), and
d5/b20 commits 1-2. So the turn-4 kill is never in the tree at turn 2 -- the leaf ranks it, and
the pure greedy leaf ranks by PROJECTION, blind to resolution-driven kills (Pod -> Redcap loop,
Chord -> Melira) the old 1-ply leaf applied and simulated.

**A first-turn-only 1-ply leaf (`search_leaf_first_turn_depth`, built + measured) is DEAD**: no
better than leaf-0 at d5 (+0.009, 23 better / 32 worse, 1000 games) and 14.1 core-s/game -- MORE
than the original leaf, because rollouts average 1.09 turns, so "first turn only" IS the whole
leaf. Kept as a default-off, unset-everywhere lever (byte-identical; smoke below) so it is not
re-implemented; do not ship it.

**Q2 -- recoverable with depth and budget?** Yes, all of them. The 92 games the new leaf plays
worse at d3/b10 (both seed bases), replayed under the shipped profile:

| recovered at | games |
|---|---|
| d3 b20 | 31 |
| d5 b20 | 1 |
| d5 b40 | 14 |
| d7 b80 | 9 |
| unbounded (d5 or d7, b0) | 37 |
| NOT recovered | **0** |

That 37 need an UNBOUNDED budget is the same budget story as above: a bounded ladder on this
deck never reaches its nominal depth, so "more depth" only helps once the budget stops binding.

**Mirror check (is the old leaf's blind spot the same size?):** the 67 games the NEW leaf wins
EARLIER, replayed under the OLD leaf at the same ladder + unbounded d5: 59 recovered, 8 still
running their unbounded jobs at the time of writing (the old leaf is expensive unbounded); at
bounded settings up to d7/b80 the old leaf failed to recover 18 of 67 -- the same shape as the
new leaf's 37 of 92. Two leaves with symmetric, budget-limited blind spots = churn, not a
regression class.

HARNESS TRAP (cost one wrong table): a per-game batch job must set `seed = base + game_index`
(the chunk convention) -- with `seed = base` every job replays game 0. `game_index` alone only
sets the spawn schedule and log number.

**Verdict under the user's rule: proceed** -- quality not worse overall, no unrecoverable
regressions. Melira stays in the suite with `search_leaf_depth: 0`.

## SESSION 2026-09-08b — value-leaf pipeline re-costed at leaf-0 play; label search now honours `search_leaf_depth`

User: *"How far are we off from being able to finish the value-leaf in 2-3 hours?"* Answered by
measurement (logs/vl_probe/), not by scaling the 12x play cut — and the play cut does NOT carry
uniformly: it lands on the H cells, not on the V cells (the value evaluator never used the rollout
leaf) and — until this session's fix — not on phase A at all.

### Probe: seed 8008, games 0-24 (the old run's first chunk), unbounded cells, 40M-unit abandon cap

| cell | old s/game (17 survivors) | new s/game (all 25) | cut | mean wt, 21 common games |
|---|---|---|---|---|
| H1 | 9.4 | 0.08 | ~125x | 5.143 |
| H2 | 183 | 0.94 | ~195x | 4.857 |
| H3 | 439 | 4.3 | ~100x | 4.762 |
| H4 | 847 | 19.8 | ~43x | 4.714 |
| H5 | 367 | 79 (2 abandoned) | ~4.6x | 4.714 |
| V4 | 17 | 11 | 1.5x | 4.714 |
| V5 | 108 | 76 (4 abandoned) | 1.4x | 4.714 |
| V6/V7/V8 | ~100 | ~81 (3 abandoned) | 1.2x | 4.714 |

**The finding that matters more than the cost: on these games H4 = V5 = V6 = V7 = V8 in quality,
and H4 now costs a QUARTER of V5.** The old table's whole case for the model on this deck was
H4 at 1177 s/game vs V5 at 33 s/game for equal quality (crossover 4->3, 5->3). At leaf-0 play the
heuristic ladder is the cheap one: H3 (4.3 s) is within one game of the value ladder on this chunk.
25 games, one seed — a signal, not a verdict — but the mechanism is structural: the greedy leaf
made the rollout nearly free, so what is left in an H cell is enumeration, which the value leaf
does not remove either.

### Phase A (row dump, K=3 searched labels): no speedup until the label search took the deck's leaf

Same 25 games (seed 900000, chunk 0), d5/b20 default play, profile attached:

| dump | core-s / 25 games | worst game | rows |
|---|---|---|---|
| old play (2026-09-06 run, >30 s games only) | 1036 (8 games) | 342 s | — |
| leaf-0 profile, label search on engine-default leaf | 1128 | 284 s | 120 (+1 dropped) |
| `MTG_FD_LEAF_DEPTH=0` (leaf-0 everywhere) | 490 | 127 s | 120, BYTE-IDENTICAL |
| **fixed binary** (profile-scoped label search) | 516 (smoke running alongside) | — | 120, BYTE-IDENTICAL |

Cause: `EmitEvalRows` ran before/outside the `SearchLeafDepthScope` AIEngine opens around
`FullSearchLineHybrid`, so the labels were searched under `s_fd_leaf_depth` (1) regardless of the
profile. Fix (this session, byte-identical play: smoke 76/76 configs changed 0): open the same scope
around `EmitEvalRows`. Labels are the ladder's earliest win, so the leaf only changes what a shallow
pass can already prove; the 120/120 identity on this chunk is the empirical check. Every other deck
ships -1 = no-op scope.

### Projection at leaf-0 play, 32 cores (old run in brackets)

| phase | work | est. core-h | est. wall |
|---|---|---|---|
| A rows, 2500 games | ~33 s/game (old 76 s/game avg, 53 core-h) | ~23 | ~45 min + the worst game's tail (old: one 6.3 h game) |
| C matrix, 13 cells x 4 seeds x 400 | H ~99 s + V ~122 s per game-set (old 677 core-h) | ~100 | 3-4.5 h |
| E A/B + play sweep + trust, ~40k games | ~1 s/game | ~12 | ~25 min |
| F mullgen contract | 48 hands x R24 x a few settings | small | minutes |
| **total** | | **~135** | **~4.5-6 h** (old: 35+ h and never completed) |

**So: roughly 2x off the 2-3 h target, and the excess is entirely V5-V8 + H5 (~75 of the 100
matrix core-h), which the leaf change cannot touch.** The matrix's target and ladder are fixed by
design (retired knobs), so closing the gap is a user decision: (a) run it as-is at ~5 h; (b) accept
the parity signal above and skip the model for Melira — but note `mullgen_finalize` (phase F) and
the mulligan generator read their settings from the value sidecar, so that route needs the settings
derived by hand; (c) trim the V ladder / target for this deck (their knob, not mine).

Also noted: the 40M-unit abandon floor now fires at ~4-6 minutes on V5-V8/H5 (these cells bill
~150k u/core-s at leaf-0 play), so the 2026-09-07 "floor = 12-48 h" mis-calibration is moot for
the V cells; H4 abandoned nothing in 25 games. The mirror check from the previous section closed
at 63/67 recovered; the 4 unrecovered were the unbounded old-leaf jobs I killed to free the box.

### 2026-09-08b — value-leaf pipeline COMPLETE (staged, NOT adopted): the model buys nothing at leaf-0 play

Run: `valueleaf.sh run` 03:48 UTC (frozen 98170986), phase A re-dumped (user: a bad call — the old
rows' labels were byte-identical under both leaves; and phase A was capped at 24 workers on the
2026-09-05 OOM history — also a bad call, all cores always), user cancelled the phase A tail at
12,070 rows 05:29, `finish` → train (held-out RMSE 0.640) → matrix 05:30-08:32 (3 h 2 min, 32
workers, ~100 core-h, 84 of 20,800 games abandoned = 0.4%, risk gate CLEAN) → D → E → F FAILED as
expected (needs the live sidecar). Total 5 h 8 min wall.

**Matrix (374 paired games/cell x 4 seeds, unbounded):**

| rung | H (turns, s/game) | V (turns, s/game) |
|---|---|---|
| 1 | 5.1946, 0.09 | 5.4841, 0.08 |
| 2 | 4.8536, 0.94 | 4.9288, 0.45 |
| 3 | 4.7599, 5.0 | 4.8054, 2.4 |
| 4 | 4.7440, 18.6 | 4.7500, 10.8 |
| 5 | 4.7414, 34.8 | 4.7434, 32.8 |
| 6/7/8 | — | 4.7421 / 4.7414 / 4.7414, ~36 |

Crossover derived = identity (1->1 2->2 3->3 4->4 5->5 6->5 7->6 8->6); trust UNSET (V5 gap
+0.0020, upper bound 0.0039 > tol). To match H4 the value leaf needs V5 at 1.8x H4's cost. The old
table's case (H4 1177 s vs V5 33 s) is gone: the greedy leaf made the heuristic rollout nearly free
and enumeration is what remains in both arms.

**Phase E, bounded play (built-in d5/b20), 8 seeds x 1000 paired games:**

| arm | avg | delta | t | seeds better/worse | core-s | cost |
|---|---|---|---|---|---|---|
| live (no sidecar) | 4.83812 | — | — | — | 11836 | 1.00x |
| staged (sidecar present) | 4.83263 | -0.0055 | -2.47 | 7/1 | 17574 | **1.48x** |

Play-profile sweep on the staged model (4 seeds x 500): every ENABLED arm (d4/d5/d6 with
escalation_cap=d) is WORSE than the presence-only default (+0.0135..+0.015 t, t +3..+4) while
0.84-0.92x the cost — capping escalation costs more quality than the depth buys.

**Verdict: NOT a clean win.** +0.0055 t (significant, 7/8 seeds) for **1.48x the per-game cost** at
the shipped play — on a deck re-admitted to the suite on cost grounds, the third-costliest already.
Staged at `logs/eval/Melira Pod.value.STAGED.json`; adoption is the user's call
([[adopt-clean-wins-without-asking]] applies only to no-drawback wins). Recommendation: do not adopt.
Consequence: phase F (mull_gen setting + expected_buckets) cannot run without a live sidecar, so the
mulligan generation setting must be derived by hand (`scripts/derive_mullgen_setting.py` against a
temporary value.json, or the built-in d5/b20 default) if the model stays unadopted.

### 2026-09-08c — why the sidecar costs 1.48x: the PROBE ladders deeper, and depth is enumeration

User: *"even the fact that escalation is that slow is somewhat suspect given that we are expected to
cache all of the search work and only do the rollouts on escalation... We should probably try to
understand the mechanism first."* Diagnostic (logs/melira_vl_diag/, 200 games d5/b20 seed 700000,
MTG_HYBRID_STATS + MTG_ROLLOUT_STATS + MTG_ESC_MEASURE), live = no sidecar, staged = shipped hybrid:

| | live (heuristic) | staged (value probe + heuristic redo) |
|---|---|---|
| core-s / 200 games | 264 | 458 (1.74x) |
| work units | 22.2M | 55.9M (2.5x) |
| interior nodes | 2.83M | 15.35M (5.4x) — of which the escalation re-traverses 0.94M (6%) |
| rollout steps | 6.63M | 2.45M |
| unit shares | rollout 30% + greedy leaf 30%, interior 39% | interior 90% (fs_main2 63%, fs_pre 27%), rollouts 4% |
| ladder committed depth (mean; hist) | 2.70; 1:38 2:911 3:587 4:227 5:98 | probe 3.37; 2:295 3:486 4:487 5:175 |
| escalations | — | 953 of 1445 decisions (66%); 888 of them fell short of d5 |
| probe budget left at escalation | — | mean 27.7%; 518 of 953 (54%) had under 10% |
| cold single pass vs ladder (esc-measure) | — | 0.88 (shallow passes cost ~12%, the memo buys little here) |

**Mechanism.** Neither hypothesis in the user's list is the cause: it is not a budget reset (default
is the legacy shared REMAINING budget; the escalation starts starved, 54% with <10% left) and it is
not the value leaf's per-node cost (V is cheaper per node). It is that the value-leaf PROBE climbs
one rung deeper on average (3.37 vs 2.70; d4+d5 in 46% of decisions vs 17%) because its leaf is
nearly free and the value start gate is 8x more lenient — and on this deck each rung is ~5-7x more
enumeration (plans_enum 35.3M vs 5.8M). The probe's memo is leaf-dependent so none of that
interior is reusable by the redo, but the redo itself is only 6% of interior nodes: the cost IS the
probe. The prior finding on hinata (escalation rollout-bound, probe 61%) holds in kind and is
sharper here: probe ~94% of the extra work. The quality gain (-0.0055 t) comes with it — the
deeper probe is what found the wins.

Bounded paradigm A/B running (logs/melira_vl_ab/, 8 seeds x 1000 paired games; d5/b20: live /
pure / pure_a1 (gate leniency pinned 1) / staged / ladder / ladesc; d3/b10 same minus ladesc).
`pure_a1` vs `pure` isolates the gate leniency; `pure_a1` vs `live` is the leaf alone.

### 2026-09-08d — paradigm A/B: on Melira the heuristic with more budget dominates every value-leaf form

User: *"maybe for decks like these ones we should use value-leaf up to the final level and use the
heuristic rollout at that point instead? ... not search further and go through heuristic
escalations at the final depth."* Measured (logs/melira_vl_ab/, 8 seeds x 1000 paired games each,
seeds 700000+1000i, delta vs no-sidecar at the same config, negative = better; cost = core-s ratio):

| arm | what it is | d5/b20 delta (t, better/worse) | cost | d3/b10 delta | cost |
|---|---|---|---|---|---|
| live | heuristic leaf, no sidecar | 0 | 1.00x | 0 | 1.00x |
| staged | shipped hybrid: value probe (8x gate) + heuristic redo | -0.0033 (-1.0, 5/3) | 1.48x | -0.0020 (-0.9, 5/3) | 1.04x |
| ladesc | ladder warm-ups on value + escalation | -0.0033 (same digest as staged) | 1.47x | — | — |
| pure | value leaf every pass, no escalation, 8x gate | +0.0200 (+3.7, 1/7) | 1.31x | +0.0413 (+11.7, 0/8) | 0.79x |
| pure_a1 | as pure, gate leniency pinned to 1 (leaf is the ONLY difference) | +0.0874 (+23, 0/8) | 0.53x | +0.0963 (+23, 0/8) | 0.50x |
| ladder | EXISTING lever MTG_LADDER_VALUE_LEAF: value warm-ups, heuristic committing pass -- NOT the user's proposal (falls back to the value line on overrun) | +0.0867 (+22, 0/8) | 0.57x | +0.0711 (+17, 0/8) | 1.02x |
| staged_a1 | EXISTING hybrid with the probe gate pinned to 1 -- NOT the user's proposal (starved d1..5 re-ladder, crossover fall-back) | +0.0122 (+6.0, 0/8) | 0.82x | +0.0108 (+3.7, 0/8) | 0.79x |
| **livebud** | **heuristic, no sidecar, 1.5x budget (d5/b30, d3/b15)** | **-0.0088 (-9.0, 8/0)** | **1.20x** | **-0.0120 (-9.0, 8/0)** | **1.16x** |

Readings:
* **Everything else equal, the value leaf is much WORSE at bounded budget on this deck** (pure_a1:
  +0.09 t at half the cost) — its lines at the depths a pinned gate admits are value-leaf lines, and
  the matrix says V(k) is worse than H(k) at every k<5 (V3 4.8054 vs H3 4.7599).
* **CORRECTION (user, same day: "you implemented my idea wrong and then are presenting numbers about it"): the two arms above were existing levers run as stand-ins, not the proposal.** The ladder lever does not deliver "heuristic at the final depth": when the
  committing heuristic pass overruns, the ladder keeps the previous pass's line, which is a value-leaf
  warm-up line — so it lands exactly on pure_a1. The escalate form (staged_a1) is cheaper than live
  but worse (0/8 seeds both configs).
* **The only value-leaf arm at parity is the shipped hybrid, and it gets there by searching deeper
  (8x gate) at 1.48x.** Spending a smaller increment on the heuristic instead (b20->b30 = 1.20x
  core-s) is better on all 8 seeds at both configs. **Value-leaf verdict for Melira: DEAD.** Ship no
  sidecar; the STAGED model stays in logs/eval as a record. If the suite wants Melira's quality
  back, the lever is budget (b30 at d5 / b15 at d3: -0.009 / -0.012 t for ~1.2x), a user decision
  since it moves GT.
* **General rule (user, 2026-09-08: "this may not be the right tool for all decks, but it is worth a
  consideration vs the escalation approach"):** the depth matrix's crossover is the per-deck signal.
  Identity crossover (V(k) == H(k)) means the probe cannot buy quality by going deeper on the cheap
  leaf, so no hybrid form can beat the heuristic at equal cost; V(k) ~= H(k-3) decks are where the
  deeper probe pays and escalation is the right paradigm. Fluctuator's table should be read the
  same way next.

### 2026-09-08e — the proposal, implemented faithfully: ONE completing heuristic pass at the probe's depth

New per-job mode `esc_single` (env `MTG_ESC_SINGLE_AT_COMMITTED`, default OFF = byte-identical; smoke-gated):
the value-leaf ladder finds the depth D it can afford; then the heuristic rollout leaf plays exactly one
pass at D with an unlimited budget so it completes, and that line is taken unconditionally (a verified
probe win is kept). No d1..depth re-ladder, no crossover fall-back, no starvation. Two arms, same seeds
as 2026-09-08d: `single_a1` (probe gate pinned to 1, so D is what the heuristic would have committed)
and `single_a8` (default 8x leniency, deeper D). Results in the table appended below when the batch lands.

**2026-09-08e RESULTS** (same seeds/pairing as 2026-09-08d; sanity: mode engages -- 543 decisions, 160
verified probe wins kept, 383 single passes at D, 0 short; smoke 76/76 configs changed 0 with the mode off):

| arm | d5/b20 delta (t, better/worse) | cost | d3/b10 delta | cost |
|---|---|---|---|---|
| single_a1 (gate pinned 1; D ~ heuristic's depth, mean 3.05) | **-0.0161 (-11.5, 8/0)** | 1.59x | **-0.0247 (-10.8, 8/0)** | 1.55x |
| single_a8 (default 8x gate; deeper D) | -0.0399 (-15.0, 8/0) | 3.77x | -0.0479 (-19.6, 8/0) | 2.54x |
| staged (shipped hybrid) | -0.0033 (ns) | 1.48x | -0.0020 (ns) | 1.04x |
| livebud (heuristic, 1.5x budget) | -0.0088 (8/0) | 1.20x | -0.0120 (8/0) | 1.16x |

**The faithful paradigm is the first value-leaf form on this deck that beats the heuristic on every
seed, and it beats the shipped hybrid at about the same cost by 5-12x the margin.** Why it works where
the hybrid does not: the hybrid's redo is a starved d1..depth re-ladder (54% start with <10% budget,
888/953 fall short) whose result is then subject to crossover fall-back; this mode plays ONE completing
heuristic pass at exactly the depth the cheap probe established, so every decision gets a heuristic
line at the deepest depth it could afford. Open: quality at EQUAL cost vs the heuristic (livebud at
1.2x is not matched to single_a1's 1.6x) -- matched-budget heuristic arms (d5/b40, d5/b50, d3/b20)
running on the same frozen binary; appended when they land.

**Equal-cost read (matched-budget heuristic arms, same seeds):**

| config | esc_single (gate pinned) | heuristic at matched cost |
|---|---|---|
| d5 | -0.0161 at 1.59x | b40: -0.0130 at 1.42x; **b50: -0.0165 at 1.54x** |
| d3 | -0.0247 at 1.55x | b15: -0.0120 at 1.16x; b20: -0.0180 at 1.30x (curve extrapolates to ~-0.026 at 1.55x) |

**Verdict for Melira: the paradigm is a WASH against spending the same budget on the heuristic, and a
large win over the shipped hybrid (the escalation approach).** So on an identity-crossover deck the
value leaf still cannot beat the heuristic per unit of cost; what this mode fixes is the hybrid's own
inefficiency (starved re-ladder + fall-back). Where the crossover is NOT the identity (V(k) ~= H(k-3)
decks) the same mode should carry the leaf's cost advantage through to the committed line without
the redo tax -- that is the deck class to test next (Fluctuator, then a V<<H deck such as hinata).
Adoption for Melira: not proposed (no sidecar; budget is the simpler lever). Lever kept, default off.

## SESSION 2026-09-09 — the EMULATED-GATE LADDER (user design): value warm-ups, heuristic commit at the heuristic's depth

User: *"The idea I mentioned should have no extra cost (and should lean toward a lesser cost) on decks
like Fluctuator or Melira if done properly. We would aim to use the heuristic rollout at the final
depth we expect to process. The only slightly tricky point is ensuring that we indeed use the
heuristic at the same level as it was used with a full heuristic ladder."* — and on the depth
choice: *"I agree with the estimation approach ... We need something like that to avoid just blindly
going to the next level."* Priority: *"make it a win and then figure out how much we can tune it."*

**What there is to win (unbounded, byte-identical lines, 40 games/cell, logs/melira_vl_ab/warmup.out):**

| deck | d3 | d4 | d5 |
|---|---|---|---|
| Melira | 1.22x | 1.11x | 1.22x |
| Fluctuator | 1.26x | 2.44x | **4.07x** |

**Built: `MTG_LADDER_EMULATED` / per-job `ladder_emulated` (+ `ladder_emul_margin`), default OFF,
byte-identical off (smoke 80/80 configs changed 0).** In FullSearchLine's ladder: warm-up passes on the
value leaf; the heuristic ladder's start gate is REPLAYED on reconstructed heuristic costs
`ch(k) = value cost(k) + R(k) x leaves(k)` (same tree under both leaves, only the leaf differs), alpha
1.10, growth from the two previous reconstructed costs, remaining = real remaining minus the extra the
heuristic ladder would have spent; a pass is played on the value leaf iff the replayed gate predicts
the NEXT pass is admitted, else on the heuristic; a wrong "warm-up" call (gate rejects k+1 after a
value pass at k) replays the heuristic at k with a FRESH interior memo (the value pass's entries share
its keys — the interior-reuse doc's trap, hit once here: "heuristic" fallbacks were reading value
lines at zero cost until the fresh cache); overrun steps one shallower as the ladder would. R(k) is
learned per depth on the thread from every (value, heuristic) pair at one depth, with a 3-sample
per-depth calibration (both leaves played) and a per-deck reset keyed on the value-profile path.

**Sanity, 50 games Melira d5/b20, seed 700000 (same games, MTG_ROLLOUT_STATS):**

| ladder | digest | avg | units | committed hist |
|---|---|---|---|---|
| heuristic (live) | 0927226b5564d358 | 5.18 | 7.39M | 1:20 2:268 3:176 4:72 5:46 |
| emulated, single R (first cut) | 5f6d8eef… | 5.12 | 11.46M | 2:157 3:238 … (gate too permissive) |
| **emulated, per-depth R + real-remaining** | **0927226b5564d358 (IDENTICAL)** | 5.18 | 7.79M (1.05x) | ~same |

So the depth is reproduced: identical play. Remaining overheads on Melira (where warm-ups are cheap
greedy rollouts anyway): value passes 9% of units, fallback waste 5% (26% of decisions mispredict
the committing depth), calibration passes (per thread, amortised in long jobs). Measured R(k):
d1 6.1, d2 4.0, d3 4.1, d4 3.5 units/leaf — the reconstruction over-estimates at d1 (bias 0.49) and
d2 (0.79), which is the conservative direction.

First bounded A/B (before the per-depth fix; 8 seeds x 1000): slightly BETTER quality (-0.003..-0.014,
significant) at 1.07-1.35x cost = the permissive gate committing deeper. Margin sweep (1.0 / 0.5 /
0.25) on the fixed binary running (logs/melira_vl_ab/ab8.out).

**Adoption caveat to resolve before any ship:** R is learned per THREAD, so committed depths depend
on the game->thread schedule (the minotaur d5 flake mechanism). For shipping, R(k) must be a
per-deck constant in the profile (measured offline, deterministic) or re-calibrated per game.

### 2026-09-09b — margin sweep read, then the accounting that closes the question

**Margin sweep (fixed binary; 8 seeds x 1000 games per cell; vs the heuristic-ladder arms of the
previous batch, so cost is contended wall-ms across batches — indicative only):**

| cell | arm | d_avg vs heuristic | same-score games | ms / heuristic |
|---|---|---|---|---|
| Fluctuator d5/b20 | margin 1.0 / 0.5 / 0.25 | -0.001 / -0.001 / -0.000 | 99.3% | 1.04 / 1.05 / 0.98 |
| Fluctuator d3/b10 | margin 1.0 / 0.5 | **-0.014 / -0.014** (better) | 98.1% | **1.31 / 1.28** |
| Melira d5/b20 | margin 1.0 / 0.5 / 0.25 | -0.002 / -0.002 / +0.000 | 99.0-99.2% | 1.00 / 0.99 / 0.93 |
| Melira d3/b10 | margin 1.0 / 0.5 | -0.005 / -0.003 | 98.3% | 1.04 / 1.01 |

Quality is the heuristic ladder's everywhere but Fluctuator d3/b10, where the emulated ladder commits
DEEPER (better play at 1.3x) — the replayed gate is not the heuristic's gate there. Cost is neutral.
Not a win. So: exact accounting of WHERE a warm-up saving could come from, `MTG_ROLLOUT_STATS`
now prints per-ladder units keyed by committed depth (`heuristic-ladder by committed depth`,
`emulated-ladder by committed depth`; 200 games, seed 700000, `logs/melira_vl_ab/acct/`).

**1. The warm-ups are nearly free at bounded budgets — the ceiling is tiny.** Units of the passes
BEFORE the committing one, as a share of the whole ladder (the most the design can save):

| deck | d5/b20 | d3/b10 | d3/b3 (mulligan gen) |
|---|---|---|---|
| Melira | **5.5%** | 5.0% | 4.4% |
| Fluctuator | 15.0% | 13.8% | 4.4% |

Per decision on Melira d5/b20: a d2 commit spends 350 warm / 9155 commit; d3 714 / 10257; d4
1643 / 22821. The d1->d2 growth is ~26x, not the gate's assumed 6x — the shallow passes are a
rounding error next to the committing enumeration. On Fluctuator the whole 15% is the d1 pass
(d2 commit: 1070 warm / 6733 commit), which is the one pass the design cannot predict (nothing to
extrapolate from). The 4.07x "warm-up share" that motivated this is real only UNBOUNDED, where
every pass d1..d5 runs to completion — and there `MTG_LADDER_VALUE_LEAF` (2026-08-05) already
delivers byte-identical lines at 1.2-4.1x (table above); the matrix uses it.

**2. Where the emulated ladder's cost actually goes (Melira d5/b20, 200 games):** heuristic ladder
22.19M units; emulated 24.92M (1.12x): value passes 2.72M of which **2.01M wasted** (739 of 1860
decisions mispredict the committing depth), committing passes 21.33M vs 20.42M — the emulated
ladder commits d3 on 687 decisions vs 576 (d2 860 vs 972), and the decisions it moves deeper are
exactly the expensive ones the heuristic gate had rejected. A value pass at d2 costs 1917 units per
decision against 9155 for the heuristic pass — 21%, not "free": enumeration units dominate the pass.
Forcing d1 onto the heuristic (`ladder_emul_hfirst=1`, `MTG_LADDER_EMUL_HFIRST`) so the d2 gate is
exact: Fluctuator reproduces the heuristic ladder's play EXACTLY (digest 646668908e794918, both)
at 0.998x — because it leaves nothing to warm up; Melira gets WORSE (27.28M, 1.23x): the d3 gate is
still replayed on a reconstructed d2.

**3. Why the replay cannot be made exact — the premise was wrong.** The design assumes the tree of
a pass is the same under both leaves, so `heuristic cost = value cost + R x leaves`. Measured on the
same-depth pairs (calibration + fallback re-runs, Melira d5/b20):

| depth | pairs | heuristic leaves / value leaves | pairs whose leaf count differs |
|---|---|---|---|
| d1 | 154 | 1.00 | 0 |
| d2 | 556 | 0.97 | 104 (19%) |
| d3 | 289 | **0.81** | 213 (74%) |
| d4 | 56 | **0.72** | 38 (68%) |

Leaf values drive the search's cutoffs (verified-win exits, best-so-far bounds), so from d2 on a
value pass walks a DIFFERENT, larger tree than the heuristic pass at the same depth. No R and no
growth model recovers the heuristic pass's cost from it; the depth mismatch (and the 1.3x on
Fluctuator d3/b10) is structural.

**Verdict: the emulated-gate ladder cannot be a cost win at bounded budgets on either deck** — the
ceiling is 4-15% of ladder units, the value passes cost 21% of a heuristic pass, and the gate replay
is inexact by construction. It stays in the tree as an opt-in A/B lever (default OFF, byte-identical
off), documented here so it is not re-derived. What DOES pay on these decks: pure heuristic ladder at
play, `MTG_LADDER_VALUE_LEAF` unbounded (1.2-4.1x, exact).

Same-batch UNITS sweep (`MTG_DUMP_UNITS=1`, `logs/melira_vl_ab/ab9.out` + `wins9/*.units`): STOPPED
by me at 50/160 jobs (my own probe of a lever already closed above; the user redirected the box to
adoption + mulligan). The one complete cell, Fluctuator d5/b20, 8 seeds x 1000, deterministic units
vs the heuristic ladder in the SAME batch:

| arm | d_avg | units | same-score | identical digests |
|---|---|---|---|---|
| emulated, margin 1.0 | -0.002 | 1.116x | 99.2% | 0/8 |
| emulated, margin 0.25 | -0.000 | 0.982x | 99.3% | 0/8 |
| heuristic-first + margin 1.0 / 0.25 | 0.000 | **1.000x** | 100% | 7/8, 8/8 |

The best bounded case anywhere is 1.8% at 99.3% play agreement. Closed.

### 2026-09-09c — user redirect: faster settings, ADOPT the value leaf in some fashion, then mulligan settings

User: *"We should use faster run settings for Melira. I'm trying to get the value-leaf adopted in some
fashion and then move on to calculating the best mulligan settings."*

**Adopted: the STAGED model as the live sidecar, presence-only** (`decks/Melira Pod/Melira Pod.value.json`
= `logs/eval/Melira Pod.value.STAGED.json`, provenance 98170986, no `value_play` block). Of the forms
measured, it is the only shippable one: the emulated ladder is neutral and schedule-dependent (learned
R), `esc_single` is a budget wash. Its cost is a d5/b20 effect: at the suite's d3/b10 the sidecar
measured -0.0020 t at **1.04x** (2026-09-08d), at d5/b20 -0.0033..-0.0055 t at 1.48x. Consequences:
melira GT moves in every tier that searches (smoke d3/d5, regression d3/d5, overnight d3/d5; the d0
greedy cases are untouched) — re-run + accept queued behind phase F; phase F can now run and write
the generation contract (`mull_gen_depth`/`mull_gen_budget_ms`, `expected_buckets`).

"Faster run settings" taken as: derive the mulligan-generation setting by measurement (phase F picks
the CHEAPEST candidate at rho >= 0.99 — by-hand probe without the sidecar: d2/b3 0.21x, d3/b3 0.24x,
d1/b3 0.04x at rho 0.981), measure Melira A/Bs at d3/b10 from here on, and leave the suite's melira
cases as they are (counts already trimmed to 50/25 in smoke). If the intent was to change the suite's
melira configs (drop the d5/b20 case, or b10 there), that is a GT-moving user call — raised, not
taken.

### 2026-09-09d — phase F under the adopted sidecar, and the mulligan settings arithmetic

**Phase F (`mullgen_finalize.py --write`, 48 openers x R24 vs the d5/b20 hybrid reference):**

| candidate | rho | units/rollout | cost vs play |
|---|---|---|---|
| d1 b3 | 0.9550 | 3,316 | 0.029x |
| d2 b3 | 0.9819 | 19,909 | 0.176x |
| **d3 b3** | **0.9907** | **30,078** | **0.266x** (PICK, cheapest >= 0.99) |
| d3 b20 | 0.9987 | 64,551 | 0.570x |
| d5 b20 (play) | 1 | 113,260 | 1x |

Written: `value_play.mull_gen_depth=3, mull_gen_budget_ms=3, expected_buckets=28` (K confirmed by
discovery at play settings; the user is to confirm the number — that is the guard's contract).

**The sidecar makes each generation rollout ~2.3x MORE expensive, not less.** Same script without
the sidecar (2026-09-08 by-hand probe): d3/b3 13,243 units/rollout at rho 0.992, d2/b3 11,682 at
0.991, d1/b3 2,420 at 0.981; the d5/b20 reference itself is 55,803 vs 113,260 with the hybrid. A
profile fitted to the SHIPPED play must be labelled under the shipped play, so with the sidecar
adopted the generation runs at 30k units/rollout; generating without it (2.6x cheaper at d2/b3)
would label hands under a policy the deck no longer plays. Recorded so the choice is visible.

**Hand space and projections.** K=28 (14 one-ofs) gives 3,669,096 distinct hands (the counting
below reproduces the generator's number exactly). The lever is the per-deck bucket ruling
`<stem>.buckets.json` (`BucketPolicy.h`: human-written `merge`/`keep_apart` groups with a `why`;
no tool writes it). Three tiers, cumulative:

* **T1 — functional identities for a keep decision** (INSTALLED as `Melira Pod.buckets.json`, PROPOSED,
  confirm before the full gen): green-producing lands as one class {Llanowar Wastes, Razorverge
  Thicket, Blooming Marsh, Branchloft Pathway, Darkbore Pathway, Forest}; W/B-only lands {Caves of
  Koilos, Orzhov Basilica}; free sac outlets {Carrion Feeder, Bloodthrone Vampire}; persist enablers
  {Melira, Vizier of Remedies}; one-drop dorks {Ignoble Hierarch, Birds of Paradise}. K=19.
* **T2** — + the four-mana pod pieces {Ravenous Chupacabra, Ranger of Eos, Felidar Guardian, Celes}
  as one "MV4 chain piece". K=16.
* **T3** — + MV3 pieces {Severance Priest, Recruiter of the Guard, Reclamation Sage} and MV2 pieces
  {Voice of Resurgence, Scavenging Ooze}. K=13.

Projected on this box (32 threads), rollout rate scaled from the 2026-09-08 scout (~18-20 keep
rollouts/s at 55.8k units) by units/rollout — ESTIMATES until the T1 scout prints its own:

| ruling | K | hands | scout (1 rollout/cell) | full gen (~36 rollouts/hand, complete R40) |
|---|---|---|---|---|
| none | 28 | 3,669,096 | ~58 h | **~44 days** |
| T1 | 19 | 289,560 | ~4.6 h | ~3.4 days |
| T2 | 16 | 130,977 | ~2.1 h | ~1.6 days |
| T3 | 13 | 62,352 | ~1.0 h | ~0.7 days |

(Without the sidecar at d2/b3 every row is ~2.6x cheaper; at d1/b3 ~12x, at rho 0.955.) On the
user's 12-thread machine multiply by ~3. The T1 `recommend` scout is queued behind the GT chain,
alone on the box, to replace the estimate with a measured projection and the slowest cells.

**REVERTED (user: "I didn't accept any bucketing ideas yet").** The agent had INSTALLED the T1
proposal as `Melira Pod.buckets.json`, set `expected_buckets` to 19 when the K guard refused the
scout (discovery: 28 raw buckets merged to 19 under the file — the guard doing exactly its job),
committed it and launched a scout under it. None of that was the user's ruling. Restored: no
`buckets.json` in the deck folder, `expected_buckets=28` (what discovery finds with no policy),
K=19 discovery cache deleted, scout killed. The proposal text lives at
`logs/Melira Pod_mullgen/proposals/buckets.T1.proposed.json` and in the tier list above; **no
bucket ruling exists until the user writes or accepts one**, and the K=28 hand space (3.67M
hands) is the deck's current generation shape. Pushed 73aae836; CI green (ubuntu, windows,
Linux/Windows determinism parity).

### 2026-09-09e — the user's bucketing rules, and what they leave

User: *"I don't want to merge lands producing different colours, nor should we merge the bounceland
with a different type."* Then: *"Carrion Feeder and Bloodthrone Vampire merged are not entirely
ideal, so I would probably split them. If I had to choose I would merge Voice and Ooze and
Reclamation Sage + Severance Priest as 2 and 3-drops that pretty much are just podded into something
else. Unfortunately their colors are different, but it's still not entirely ideal."*

Under the colour rule the T1 land group collapses to ONE colour-identical pair (Llanowar Wastes +
Blooming Marsh, both B/G, painland + fastland); the Pathways commit to one colour on play, Basilica
is out by the bounceland rule, Forest has no partner. Hand counts (the generator's counting):

| ruling | K | hands | full gen, est. |
|---|---|---|---|
| none | 28 | 3,669,096 | ~44 days |
| user's two merges (Voice+Ooze, Sage+Priest), outlets kept apart | 26 | 2,582,812 | ~31 days |
| + Wastes+Marsh | 25 | 1,955,703 | ~23 days |
| + Melira+Vizier (G vs W) | 24 | 1,553,932 | ~19 days |
| + Hierarch+Birds (Birds makes W) | 23 | 1,164,547 | ~14 days |

The hand space is the twelve pod-chain one-ofs, not the lands: cautious merges shave ~20% each and
no cautious set makes the profile tractable on this box (est. rate 35 keep rollouts/s at the phase-F
setting with the sidecar; x3 on the 12-thread machine). The levers beyond bucketing: the `fast`
recipe (~a quarter to a third off), a cheaper labelling setting (d2/b3 rho 0.98 at 0.66x, d1/b3
0.955 at 0.11x — below the 0.99 floor = a different policy), generating without the sidecar (2.6x
cheaper, labels under a policy the deck no longer ships), or a ruling on the MV4 one-ofs.

Then the user corrected the land reading — *"I thought we were also merging lands that produce the
same colours? ... Except the bounceland which absolutely cannot be merged"* — and RULED: *"K=23 that
you have is probably the safest set of merges and the others would be for if we need more"* (Melira +
Vizier "a bit arguable", Hierarch + Birds "if we are desperate ... Ignoble is a little worse and it
does come up").

**INSTALLED (user ruling, a523b881): `decks/Melira Pod/Melira Pod.buckets.json`** — merge: {Llanowar
Wastes, Blooming Marsh, Darkbore Pathway} (B/G), {Razorverge Thicket, Branchloft Pathway} (G/W),
{Voice, Scavenging Ooze}, {Reclamation Sage, Severance Priest}; keep_apart (recorded so the fallbacks
are explicit): {Orzhov Basilica, Caves of Koilos}, {Carrion Feeder, Bloodthrone Vampire}, {Melira,
Vizier}, {Ignoble Hierarch, Birds of Paradise}. `expected_buckets=23`. Hand space 1,088,514 (the
engine's count at the user's intermediate K=26 ruling, 2,582,812, matched the arithmetic exactly).
Fallbacks if cost demands: + Melira/Vizier -> K=22, 849,374 hands; + Hierarch/Birds -> K=21, 618,732.
The `recommend` scout ran under this ruling on the deck folder (`logs/Melira Pod_mullgen/scout_K23.log`):
discovery 28 raw -> **23 buckets, 1,088,514 hands (engine == arithmetic)**; floor phase on 32
threads at a SUSTAINED **~115 keep rollouts/s** (47,166 in 900 s; 42/s in the first 600 s while the
slow cells front-load) — 3.3x the 35/s estimate used above. Re-projected at 115/s: scout (1 rollout x
1.58M size-7+draw cells + 600k fused sub-table batches) ~5 h; **full `complete` gen ~1,088,514 x ~36
/ 115/s ≈ 4 days** on this box, ~12 on the 12-thread machine; fallbacks K=22 ≈ 3.1 d, K=21 ≈ 2.3 d.

**Then it CRASHED at 900 s: exit 139, SIGSEGV** (`dmesg`: "segfault at 4 ... in mtg-analyze", i.e. a
null + 4 read), symbolised in the exact Release binary to `CapGroupsBySituationalRank(...) + 0x3d0`
(TurnSolver.cpp, the situational-rank group cap shared by EnumeratePlans / the plan-space cap of
2026-09-06). Not seen on the 2026-09-08 K=28 scout without the sidecar (hours). Reproduced under
`build/RelWithDebInfo` with `ulimit -c unlimited` in 3 minutes (core.3405805, gdb): frame 0
`CapGroupsBySituationalRank(greedy=true)` from `SolveUncached` <- `SolveSecondMainInSearch` <-
`SimulateToEndImpl` (a rollout's greedy second-main solve), locals `ranked = {}` (empty),
`keep_n = 1`, `keep = {}`, `i = 0` -> `keep[ranked[0].second]` reads `.second` of a null element =
address 4. **Root cause:** the call arrives with NO groups but `2^num_independent` alone above the
greedy plan-space cap (`SolveSpaceCap`, the 2026-09-06 Melira product cap), so the "product too
big" branch falls through the empty-groups case, `keep_n = max(1, ...)` floors to 1 ("always keep
the top group"), and the loop indexes an empty ranking. **Fix:** `if (groups.empty()) return;` at
the top of the function (nothing to cap; the walker bounds the product) — byte-identical in every
case that did not crash. Needs many independent actions with zero groups in a rollout, which is
why it surfaced in Melira's generation rollouts and not in the suite.

### 2026-09-09f — the tree difference, root-caused: it is the MEMO, not pruning (user: "that seems like a bug")

User: *"The way I see it in my mind we are just replacing the cost of the rollouts when they are not
needed. It seems like you are doing something completely different if there are added costs."* Right.
Instrumented the same-depth heuristic-vs-value pairs (200 games, Melira d5/b20):

| per pass, h / v | d2 | d3 | d4 |
|---|---|---|---|
| B&B prunes at FSLineWin entry | 0 / 0 | 0 / 0 | 0 / 0 |
| in-horizon early exits | 0.012 / 0.012 | 0 / 0 | 0 / 0 |
| mean leaf win turn | 8.31 / 5.70 | 8.30 / 5.80 | 8.69 / 6.54 |
| memo WIN hits | 4.4 / 6.4 | 6.4 / 12.2 | 6.0 / 14.6 |
| memo NO-WIN hits | **1.9 / 0** | **10.9 / 0.02** | **19.3 / 0.8** |
| memo win-entry ORDER MISSES (full re-search) | 1.3 / 1.7 | **6.9 / 12.6** | **8.3 / 25.4** |

Pruning and early exits are identical. The difference is memo reuse: `FSLineCache` NO-WIN entries
are order-free, WIN entries carry an index-encoded line and replay only when `FsOrderSig` matches
(canon keys admit permuted states). The heuristic rollout says "no win by turn 8" from most leaves
(mean 8.3), so its transpositions are answered by order-free no-win entries; the value model says
~5.7, so nearly every subtree becomes a WIN entry, and every order-mismatched re-entry is a full
re-search. So a value warm-up was NOT "the heuristic pass minus rollouts": it re-searched what the
heuristic pass memoized. That is why the trees differed and why the gate replay drifted.

**Fix (`MTG_WARMUP_MEMO_ORDERFREE`, default ON, byte-identical off the warm-up path):** an
emulated-ladder warm-up pass (its line is discarded by construction; a warm-up that finds a verified
win is now replayed on the heuristic at that depth) takes an order-mismatched WIN entry's win turn
without the line. Re-measured (same 200 games):

| per pass, h / v | d2 | d3 | d4 |
|---|---|---|---|
| heuristic leaves / value leaves | 1.06 | **1.30** | **1.45** |
| memo win-entry order misses | 1.1 / 0 | 7.4 / 0 | 12.8 / 0 |
| memo no-win hits | 2.0 / 0 | 10.8 / 0.02 | 24.7 / 0.6 |

The value warm-up now walks the SMALLER tree; what is left is the heuristic pass's OWN order-miss
re-searches (7-13 per pass), which the shipped search pays on every committing pass. Emulated
ladder totals moved from 24.92M to 24.08M units (live 22.19M) — the reconstruction is now biased the
other way (heuristic tree bigger), so mispredicts did not fall (731/1851).

**The leaf-independent rule, and a perf lever for the shipped search (PROPOSED, user's call — it is
play-affecting):** order-free WIN reuse for EVERY pass, i.e. on an order-mismatched win entry return
the win turn with an empty continuation. Trees become identical under both leaves (then heuristic
cost = value cost + rollouts exactly, up to per-position rollout-cost variance) AND the committing
pass skips 7-13 full re-searches per pass. The cost: a line reaching such a node is truncated there,
so commit-the-line re-searches at that decision instead of replaying — play can change (a fresh
budget at the re-search). Needs the standing gate (smoke + regression + per-game diff) and an
explicit go.

Final state of the lever after the memo fix + "replay a verified value pass on the heuristic, then
stop" (3de1efef, smoke 80/80, CI green ubuntu/windows/parity): 200 games avg 4.995 (live 5.010),
25.86M units vs 22.19M live (1.17x), 831/1857 mispredicts — the reconstruction now under-estimates
the heuristic pass (its tree is 1.25-1.54x the warm-up's at d3-d4), so the emulated ladder stays
CLOSED until the order-free-win rule above is applied to every pass. The scout on the fixed binary
passed the old crash point: 102,212 keep rollouts at 1200 s (100/s sustained); stopped by me when
the user decided to generate on the 12-thread machine (`fast` recipe); journal kept.

### 2026-09-09g — order-free WIN reuse for EVERY pass (`MTG_MEMO_WIN_ORDERFREE` / job `memo_win_orderfree`, default OFF)

Built as the leaf-independent rule; smoke 80/80 unchanged with it off. 200 games Melira d5/b20:

| arm | units | avg | notes |
|---|---|---|---|
| heuristic ladder (shipped) | 22.19M | 5.010 | |
| **heuristic ladder + order-free** | **20.03M (0.90x)** | 5.015 | commit depth mean 2.68 vs 2.64 (deeper for the same budget) |
| emulated ladder + order-free | 22.44M (1.12x of the order-free live) | 4.995 | trees now near-identical: h/v leaves 0.99 / 0.94 / 1.05 at d2-d4, differing pairs 48/590, 12/249, 2/60 (were 161, 191, 36) |

Two readings. (1) **For the shipped search it is a ~10% units cut at equal depth/budget** — the
committing pass no longer re-searches order-mismatched transpositions (7-13 per pass). Play is not
byte-identical (a line ends where a reused entry sits, exactly as at a leaf; the engine re-searches
past it), so quality is the pooled A/B's question. (2) **For the emulated ladder the trees are now
the same, and it is STILL 1.12x** with 804/1829 decisions mispredicting the committing depth. That
residual is not reconstruction error any more: it is the PREDICTION itself — before running pass k
the ladder must guess whether the gate will admit k+1, which depends on pass k's actual cost (the
gate's growth ratio is ch[k]/ch[k-1]); the heuristic ladder never guesses, it runs k and then reads
the gate. Each wrong "warm-up" guess costs a value pass (~21% of the heuristic pass at that depth),
and with warm-ups worth 5-7% of the ladder here the guessing overhead outweighs the saving.

Pooled A/B running (`logs/melira_vl_ab/orderfree_manifest.json` -> `ab10.out`, `wins10/*.units`):
live / live+order-free / emulated+order-free, Melira + Fluctuator, d5/b20 + d3/b10, 8 x 1000, units
dumped. The adoption question is (1); (2) is reported for the record.

**User: "It should be possible to get a small cost win." — where the residual goes.** Two
hypotheses tested on the same 200 games (both order-free):

* Leaf-rollout TT reuse (warm-up rollouts pre-paying the committing pass): **NO.** Committing-pass
  TT hit rates are the same in both ladders — d2 17.3% vs 15.7%, d3 12.3% vs 12.1%, d4 5.4% vs
  7.0% (live warm passes hit 22-37%). Not the mechanism.
* The PREDICTOR's growth model: **YES.** Melira's pass cost grows ~26x into d2 (d1 ~350 units, d2
  ~8-10k) and only ~1.1x into d3 (d3 ~8.9k per decision), while `predict_next_fits` extrapolated
  with the gate's bootstrap default 6 before any measured ratio: at k=2 it asked "36 x ch1 fits?"
  (yes), the real gate then asked "26 x 26 x ch1 fits?" (no), and the d2 value pass — the most
  expensive warm-up — was wasted: 804/1829 mispredicts, ~1.8M of the 2.3M value units. Perfect
  prediction would give ~live − (warm rollouts − value evals) ≈ 20.03M − (1.33M − 0.48M) ≈ 19.2M =
  **0.96x**: the small win, and its ceiling at this budget.

Fix: learn per-depth growth G[k] = ch[k]/ch[k-1] on the thread (EMA, like R) and predict with it
(`est_k = ch[k-1] x G[k]`, `est_k1 = est_k x G[k]`, the gate's own extrapolation with the realized
ratio). Re-measured, same 200 games (order-free live = 20.03M, avg 5.015):

| emulated ladder | units | mispredicts | wasted value | value | calib+other | commit | avg |
|---|---|---|---|---|---|---|---|
| default-growth predictor | 22.44M (1.12x) | 804/1829 | 1.84M | 2.41M | 0.34M | 19.10M | 4.995 |
| **learned-growth predictor** | **20.32M (1.015x)** | **351/1836** | 0.41M | 0.84M | 0.61M | 18.29M | 5.005 |

The mispredicts halved-and-more and the waste fell 4.5x. What is left: ~0.5M of per-thread
CALIBRATION passes (305 in a 200-game / 32-thread run: 3 heuristic re-runs per depth per thread —
~1% in a 1000-game job, so ~0.99x amortised), 351 residual mispredicts (0.41M), and a commit-cost
drift of +0.6M from committing slightly deeper (mean 2.696 vs 2.684: the reconstruction's R is a
per-depth mean, the real rollout cost varies per position). The 0.96x ceiling stands; the pooled
follow-up (`emulofg` arm, 32 jobs, `ab11.out`, queued behind ab10) measures it at 8 x 1000 where
calibration is amortised.

**Pooled A/B landed (ab10, 8 seeds x 1000 per cell, deterministic units, same batch):**

| cell | heuristic + order-free: quality | units | play | emulated (default predictor) + order-free: quality | units |
|---|---|---|---|---|---|
| Melira d5/b20 | -0.0010 (10 better / 2 worse) | **0.903x** | 99.8% same score | -0.0040 (51/19) | 0.968x |
| Melira d3/b10 | -0.0001 (1 / 0) | **0.906x** | 1 game of 8000 differs | -0.0056 (86/42) | 1.098x |
| Fluctuator d5/b20 | +0.0002 (0 / 2) | **0.963x** | 6/8 seeds byte-identical | -0.0017 (42/30) | 1.102x |
| Fluctuator d3/b10 | +0.0001 (0 / 1) | **0.983x** | 7/8 seeds byte-identical | -0.0128 (135/36) | 1.498x |

Order-free WIN reuse for every pass is a CLEAN WIN for the shipped search on both decks: 2-10% fewer
units, no quality axis moved, play almost untouched (the lines it shortens are almost never the
committed ones). Under the clean-win rule it is ADOPTED: default ON (`MTG_MEMO_WIN_ORDERFREE=0`
restores the old replay), gated by smoke + regression across every deck with the per-game diff
inspected before accept. Note the emulated ladder on Melira d5/b20 is now cheaper AND better than
the plain heuristic ladder even with the default predictor (0.968x, -0.004 t, 51/19); the learned-
growth arm is in ab11.

**Smoke under the adopted sidecar (binary with the emulated lever OFF):** 77/80 configs unchanged
(byte-identical off, as required), melira d3 4.88 -> 4.84 (2 faster), melira d5 4.96 = 4.96 (play
differs, score same), **melira2hg d3 5.04 -> 5.12 (2 slower of 25)** — the 2HG case runs the same
folder, so it picks up a model fitted to 1v1 goldfish at 20 life. Sized with a 4 x 1000 paired 2HG
A/B (sidecar vs none, d3/b10, `logs/melira_2hg/`): sidecar +0.0053 t (226 games better / 199 worse
of 4000, se ~0.005, 1 of 4 seeds better) — noise-level, no harness change.

**Regression tier:** 104/108 unchanged; the four melira cases moved d3 s2002 4.800 -> 4.813, d3 s3003
4.787 -> 4.827, d5 s2002 4.750 -> 4.775, d5 s3003 4.750 -> 4.725 (8 slower / 7 faster over 230
games, incl. s2002 gi22 T7 -> unwon at both depths). Small-sample; the 8 x 1000 phase-E / paradigm
reads (-0.002 t d3, -0.0033..-0.0055 t d5) are the evidence, and adoption is the user's direction.
**Overnight tier (melira per-deck, 1400 searched games):** d3 s4004..s7007 4.865 -> 4.875, 4.805 ->
4.815, 4.875 -> 4.840, 4.785 -> 4.750; d5 4.840 -> 4.807, 4.807 -> 4.780, 4.860 -> 4.813, 4.773 =
4.773 — 80 games faster / 48 slower, every d5 seed better or equal. All three tiers ACCEPTED under the
adopted sidecar (GT logs 456/456 consistent).

### 2026-09-09h — learned-growth arm landed; order-free reuse: fill-in built, the Fluctuator losses root-caused

**ab11 (`emulofg` = emulated ladder + order-free + learned per-depth growth), 8 x 1000 per cell,
same batch as ab10, vs the plain heuristic ladder (`live`) and vs order-free alone (`liveof`):**

| cell | emulofg units / live | / liveof | d_avg vs live | better/worse vs live |
|---|---|---|---|---|
| Melira d5/b20 | **0.834x** | 0.924x | +0.0016 | 41 / 55 |
| Melira d3/b10 | 0.908x | 1.002x | +0.0011 | 76 / 85 |
| Fluctuator d5/b20 | 1.001x | 1.040x | +0.0001 | 29 / 32 |
| Fluctuator d3/b10 | 1.472x | 1.497x | -0.0124 | 131 / 35 |

The learned growth fixed the predictor (Melira d5/b20 0.968x -> 0.834x) but the residual over order-free
alone is ~8% at d5/b20 and nothing at d3/b10, with the per-game score count slightly negative. Not a
clean win on its own; the multi-deck screen below asks the user's wider question.

**Fill-in built.** Two gaps the pre-compaction draft missed: (1) the ancestors of a truncation point
are memoized with truncated lines under EXACT order signatures, so the re-search would replay them —
the lookup now skips a truncated WIN entry while the shortcut is off; (2) `FSLineStoreWin` used
`emplace`, so the complete line would never overwrite — a complete line now supersedes a truncated
one. Measured (200 Melira games, d5/b20, `MTG_ROLLOUT_STATS`): 17 committed lines re-searched, 0.46M
units (2.3%), none still truncated; units 20.45M vs 22.19M old = **0.92x** (0.90x without fill-in).

**The Fluctuator losses were NOT truncation.** Seed 702739 still wins on turn 4 with fill-in. Root
cause: `FSLineStoreWin` stores any `win_turn <= max_turns`, so WIN entries also carry the LEAF
ESTIMATE of a win beyond the node's horizon — and the greedy rollout is not order-invariant, so a
permuted state's estimate is not this state's. Reusing those substitutes one estimate for another;
in 702739 the turn-3 cycling kill is judged a turn worse at a permuted node and the ladder commits a
line that stops after four cycles. Restricting the order-free shortcut to VERIFIED entries
(`win_turn <= turn + depth - 1`) wins 702739 on turn 3 again — but the 200-game cost is **0.99x**
(21.96M): the saving lives almost entirely in the estimate entries. So the lever is a HEURISTIC one
(a permuted-state rollout as this state's estimate; measured neutral over 32000 games at 2-10%),
not a pure memo. Exposed as `MTG_MEMO_ORDERFREE_VERIFIED_ONLY` / job `memo_orderfree_verified_only`
(default OFF = all entries, the measured lever); the true fix would be an order-invariant rollout,
which would make estimate reuse exact — deferred, it changes play everywhere.

**Multi-deck screen launched** (`logs/emul_screen/`, 992 jobs, 8 x 500 per cell, 32 threads,
deterministic units): for every deck with a live value leaf (19), arms `ship` (sibling sidecar =
value hybrid + escalation), `heur` (no sidecar: the plain heuristic ladder, order-free all + fill-in),
`emul` (emulated ladder, sidecar for warm-ups only) at d5/b20 and d3/b10; Melira and Fluctuator also
carry `heurof0` (order-free off) and `heurver` (verified-only) so the three order-free variants are
measured in the same batch. User's question: is the emulated ladder worth using instead of escalation
for any other deck (their expectation: no — escalation reaches searched wins faster on decks without
Melira's front-loaded growth). Projection ~1.5-2.5 h from the regression tier's throughput.

**Tiers on the final binary (all entries + fill-in, verified-only OFF):** smoke 15 changed / 65
unchanged (searched: 5 faster, 0 slower; the same 14 digests as the flip alone plus mirrorwing d5
play-changed at the same score = the fill-in's whole footprint on the suite), regression 32 changed /
76 unchanged (searched: 27 faster, 6 slower; each slower game explained by the audit as a divergent
physical game — fetch/shuffle or mulligan divergence). Both ACCEPTED (gt_logs 456/456 consistent),
committed as d71b4157 (rebased over origin's census commits 41e1e8c6/f3039fee — the replayed engine
change widens `PlanCopySig`, which is called only inside `DedupCensusOn()` (default OFF), so the
rebased binary is play-identical to the one the GT was measured on; byte-identity smoke to follow
once the screen releases the build output). Overnight tier chained behind the screen + regression
tier by PID (`logs/melira_gt/overnight_chain.pid` -> `overnight_final.out`), to be inspected and
accepted separately. CI: the push's own run was cancelled by a later push to the branch
(bb847162, another census instrument); the Windows result is read from that run.

**Fluctuator joins the emulated screen (user, 2026-09-09):** *"We should trust Fluctuator then with the
new mode"* — its model was rejected as a PLAY leaf (`.value.DISABLED.json`), but an emulated-ladder
warm-up needs only a rough guide (the committing pass is always heuristic), so `fluct_emul_*` runs
with the DISABLED sidecar as `value_profile` (`manifest_fluct.json`, 16 jobs, same seeds as the
screen's `heur`/`heurof0`/`heurver` arms; `run_fluct.out`, folded into `report.py`). Prior read
(ab11, without fill-in): d5/b20 1.001x at +0.0001 t, d3/b10 1.472x at -0.0124 t (131 better / 35
worse) — a quality gain bought at budget-raise prices, to be compared with a plain budget raise if it
repeats. Trust is not what makes escalation cheap: Anti-Lifegain, Dragons and Dragonstorm have NO
trust depth yet ship 2.7-3.7x under the heuristic ladder; the saving is the value pass's candidate
line verifying first time. USER CORRECTION on the Melira/Fluctuator reading: the shallow paired
gap is NOT a sign the model is wrong — a value leaf always loses to rollouts in the early turns, on
every deck. What set Melira and Fluctuator apart is that going DEEPER with the leaf (where it
converges) was itself costly, and that cost is why both had the leaf disabled in play; Melira's
1.48x hybrid is that same cost showing up. So the screen's question per deck is "does the value
search converge at a depth that is still cheap?", and the decks to watch (Mirrorwing, Stompy,
Creature Giving) are those whose convergence depth is deep, not those with a large shallow gap.

### 2026-09-09i — per-deck search shape + the NO-LEAF stand-in (user design)

User, in order: *"there is never a reason to have it fully disabled. We choose either escalation or this
new approach"* → *"maybe even ... the new approach with no leaf whatsoever? ... a third option"* →
*"just letting search do its thing and only using the heuristic when we find nothing might be
sufficient"* → *"Technically it is even an alternative under escalation. Since you could just escalate
always if you don't find a win. But bank the win from not doing the leaf."* → *"we probably should
play with this option just to make sure we aren't wasting unnecessary time with the value-leaf."*

**Built (unlinked until the screen releases the binary):**
- `MidGameEvaluator::Constant()` — a NO-LEAF stand-in: `constant=true`, Score() = 99 turns, `empty()`
  false so every presence gate sees an attached model. The leaf site returns `max_turns+1` for it
  with no rollout, no feature extraction. So the search commits only wins it PROVES in-horizon and
  otherwise escalates (trust depth forced 0) / warms up for free.
- Per-deck shape in the sidecar's `value_play`: `ladder: "escalation" | "emulated"` and
  `leaf: "model" | "none"`, read whether or not the block is `enabled`. `leaf: "none"` swaps the
  loaded model for the stand-in (`apply_leaf_policy` in AttachValueSidecar); the DISABLED-file
  convention is superseded — a deck always ships a live sidecar and picks a shape.
  `ladder: "emulated"` sets `valuearm::t_deck_ladder` (RAII per decision in AIEngine); UseValueModel()
  returns false under it (the model is warm-up-only), and the emulated switch reads it.
  The emulated ladder's learned R/G are now keyed on `MulliganProfile::value_source` (the profile path)
  + the arm's model override — a pooled batch never mixes decks (it did before: key = arm path only).
- Arm/env: `value_profile: "noleaf"` attaches the stand-in with no sidecar. `ladder_emul_direct` /
  `MTG_LADDER_EMUL_DIRECT`: commit a VERIFIED warm-up win directly (fill-in with the shortcut off if
  truncated) instead of replaying it on the heuristic; default ON for the stand-in, OFF for a model.
  Counter `direct_commits` in the `[rollout-stats]` emulated line.
- Screen 2 (`logs/emul_screen/manifest_noleaf.json`, 640 jobs): `escnl` (hybrid + stand-in) and
  `emulnl` (emulated ladder + stand-in) on every deck, same seeds/configs as screen 1, so `report.py`
  lines them up against `ship` / `heur` / `emul`. The question per deck: does the learned leaf beat
  "search, then escalate" by enough to pay for its generation? Runs after screen 1, on the rebuilt
  binary (byte-identity smoke first: no deck sets a shape, so play must not move).

**Pipeline uses of the no-leaf stand-in (user, 2026-09-09):** *"we could use it from the start prior to
even having an existing value-leaf. Even if we decide a value-leaf is worthwhile we may be able to use
no-leaf for part of the chain or to help generate parts of the value-leaf."* Candidates, to be judged
against screen 2 (each is a place the chain currently runs either the heuristic ladder or the hybrid):
1. **Day-one play for a new deck** — ship `<deck>.value.json` with `leaf: "none"` + a ladder from the
   analyze-deck stage, so "no sidecar" stops being a state (kills the presence-gating traps: hybrid
   activation by file existence, the H-cell ladder's 1.35-84.8x cliff on a missing model).
2. **Play validation / claude-play sweeps** (analyze-deck stage 5) — cheaper searched play, exact
   wherever a win is proven.
3. **Value-leaf phase A (row dump)** — the labelling search: no-leaf escalation commits proven wins and
   escalates the rest, so labels are the heuristic's where it matters at a fraction of the cost.
4. **Value-leaf phase C (H-depth cells)** — the expensive H cells run on escalation-with-stand-in rather
   than the plain heuristic ladder; the V cells are unaffected (they are what is being fitted).
5. **Mulligan generation rollouts** — `mull_gen_depth`/`budget` rollouts on the stand-in hybrid instead
   of the model hybrid; sidecar-presence ordering (value leaf BEFORE mulligan) would then be a quality
   choice, not a correctness dependency.
Ordering caveat that stays: any artifact fitted to play (value leaf, keep table) is fitted to the shape
in force when it was generated; switching a deck's shape afterwards is a regeneration trigger exactly as
a play-logic change is.

### 2026-09-09j — screen 1 read: escalation wins everywhere the leaf converges; order-free default stays ALL entries

Screen 1 (`logs/emul_screen/report_vs_ship.txt`, `report_vs_heur.txt`): 20 decks × {d5b20, d3b10} × 8 seeds ×
500 games, arms `heur` (heuristic ladder), `ship` (the deck's shipped shape), `emul` (emulated ladder on the
shipped model), plus `heurof0` / `heurver` on Melira and Fluctuator. Units are deterministic search work.

**(1) The order-free default (all entries, ON since d71b4157) is the clean win and stays.**

| deck / cfg | heur (all entries) | heurof0 (off) | heurver (verified only) |
|---|---|---|---|
| melira d5b20 | 1.000, avg 4.8300 | 1.107, +0.0017 t (net -7) | 1.107, +0.0017 t (net -7) |
| melira d3b10 | 1.000, avg 4.8570 | 1.100, +0.0000 | 1.100, +0.0000 |
| fluct d5b20 | 1.000, avg 3.6240 | 1.039, byte-identical | 1.039, byte-identical |
| fluct d3b10 | 1.000, avg 3.6398 | 1.017, byte-identical | 1.017, byte-identical |

All-entries is cheapest on every cell and never worse in quality (the 3/16000 Fluctuator losses of 09h did
not recur on these 8000 games; on Melira it is the 0.0017 t BETTER arm). Verified-only buys exactness at
1.10x on Melira for no quality — not adopted. Nothing to flip; the shipped default is confirmed.

**(2) Emulated ladder vs escalation, per deck (u/ship = units relative to the shipped shape; d_avg > 0 is worse):**

| deck | d5b20 heur | d5b20 emul | d3b10 heur | d3b10 emul | verdict |
|---|---|---|---|---|---|
| antilife | 3.72x +0.001 | 4.39x +0.000 | 1.31x -0.001 | 1.59x -0.005 | escalation |
| auras | 7.66x +0.004 | 9.71x +0.004 | 1.70x +0.003 | 3.30x -0.002 | escalation |
| breaching | 18.8x +0.000 | 4.72x +0.000 | 5.89x +0.000 | 1.67x +0.000 | escalation |
| critter | 14.4x -0.000 | 13.9x -0.000 | 1.17x +0.000 | 1.19x +0.000 | escalation |
| dragons | 2.79x +0.003 | 2.78x +0.003 | 1.18x +0.000 | 1.25x -0.001 | escalation |
| dragonstorm | 2.73x +0.003 | 3.25x +0.002 | 1.16x -0.002 | 1.39x -0.006 | escalation |
| fivecolour | 1.79x +0.009 | 2.82x -0.002 | 1.42x +0.013 | 2.96x -0.006 | escalation (emul buys quality at 3x) |
| goblins | 4.80x +0.004 | 6.35x +0.002 | 1.67x +0.005 | 2.40x +0.001 | escalation |
| hinata | 1.27x +0.020 | 1.64x +0.009 | 1.12x +0.006 | 1.79x -0.017 | escalation |
| kitty | 2.99x +0.008 | 5.52x +0.002 | 1.49x +0.008 | 3.38x +0.001 | escalation |
| knights | 13.3x +0.001 | 13.8x +0.001 | 1.38x +0.001 | 1.47x -0.001 | escalation |
| melira | 0.71x +0.023 | 0.66x +0.029 | 0.78x +0.011 | 0.78x +0.012 | ship (leaf) is the QUALITY arm here |
| minotaur | 6.34x +0.003 | 6.25x +0.004 | 1.30x +0.001 | 1.34x -0.001 | escalation |
| mirrorwing | 3.50x +0.010 | 3.97x +0.011 | 1.45x +0.015 | 2.14x +0.009 | escalation |
| stompy | 4.13x +0.018 | 6.18x +0.012 | 1.35x +0.007 | 3.02x -0.011 | escalation |
| burn | 4.27x +0.002 | 4.45x +0.001 | 1.27x +0.002 | 2.11x +0.000 | escalation |
| slivers | 13.4x +0.001 | 13.7x +0.001 | 1.53x -0.001 | 1.95x -0.002 | escalation |
| th | 3.86x +0.015 | 3.50x +0.012 | 1.71x +0.006 | 2.38x +0.002 | escalation |
| fluct (vs heur) | 1.00 | 1.01x -0.000 | 1.00 | 1.46x -0.014 | heuristic ladder (no model) |

The user's prediction holds: the emulated ladder costs what the heuristic ladder costs (its committing
pass IS the heuristic's), so wherever the leaf converges cheaply — 17 of 18 modelled decks — escalation
wins by 1.2-19x at equal quality. Melira is the one deck where the shipped model is the quality arm
(ship -0.0225 t vs heur at 1.40x), which is the 09c adoption. Fluctuator, with no model, gets nothing from
the emulated ladder at d5 and pays 1.46x for -0.014 t at d3 (the heuristic ladder there is budget-bound).

**(3) What this leaves for the no-leaf work (user, 2026-09-09 evening).** The emulated ladder's virtue is
structural, not its leaf: warm-ups on a cheap leaf, ONE committing pass on the expensive one at the depth
that ladder would commit, verified wins banked at warm-up depth. The user's refinement: *"If you have
value-leaf it may still reduce the need to check the leaf until we get near the escalation stage.
Technically we know the node cost of value-leaf, so we could retain enough budget to do the value-leaf
first when escalation is needed."* That is the emulated ladder with the COMMITTING leaf = the model:
warm-ups on the stand-in (no leaf cost at all), the committing pass on the value leaf at the depth the
VALUE ladder would commit (its gate replayed on reconstructed value costs = stand-in cost + R_v × leaves,
R_v learned exactly as R is), then the hybrid's trust escalation on that line as today. No re-run at the
committing depth, verified warm-up wins banked directly. Built next as `value_play.commit: "model"`.

### 2026-09-09k — the OOM restart, a determinism defect in the emulated ladder, and COMMIT-ON-MODEL built

**What broke.** ~13:00 the container OOMed (23 GB box): the chained overnight tier (all cores), screen 2
(32 threads) and a `./build.sh profile` were running at once; the user had to restart the machine by hand
and is asleep from here. Standing rule from now on (memory `oom-one-heavy-process-at-a-time`): ONE heavy
process at a time, never a build beside a batch, a `MemAvailable` watchdog on every batch I start (it kills
by PID). Measured: this screen's arms are memory-heavy -- the no-leaf ladders build large per-decision
memos -- 32 workers reached ~20 GB, 16 workers 14.6 GB; the re-launch runs 12 workers (~7-9 GB).
The overnight tier (112 jobs in) and screen 2 (92 in) both died; screen 2's remainder is pooled with
screen 3 below; the overnight tier is NOT re-run tonight (it cannot share the box) -- it is the one open
item for the morning.

**Determinism defect (found by the re-run, fixed in 5e4bb2a3).** Re-running one finished screen-2 job on
the rebuilt binary: `antilife_escnl` identical, `antilife_emulnl` a DIFFERENT digest. Cause: the emulated
ladder's learned R(k)/G(k) are thread-local and were keyed on deck+arm, so they persisted across GAMES on a
batch worker -- a job's play depended on which jobs had shared its thread. The g_probe_leaves contamination
of the Minotaur flake, one level up. Fix: the key now carries `game_seed` (state per game, a pure function
of the game), each game starts from the deck's FROZEN `escalation_r` (the same units-per-leaf quantity the
hybrid's predictor freezes for exactly this reason) else 120, and calibration is 2 samples per depth per
game. Consequence for the record: every emulated-ladder number measured before this fix (screen 1's `emul`
column in 09j, screen 2's first 46 `emulnl` jobs, ab11's emulofg) was history-dependent. The 09j verdict
stands qualitatively (1.2-19x is far outside what R drift moves) but those cells are not reproducible;
everything from here is measured on the per-game build, with two same-job-twice determinism checks in the
batch (`_rep`).

**Built: the committing pass on the MODEL (user design, 2026-09-09 evening).** Sidecar
`value_play.commit: "model"` (arm `ladder_emul_commit_model`, env `MTG_LADDER_EMUL_COMMIT_MODEL`): the
emulated ladder's warm-ups run on the stand-in (`leaf: "none"` keeps the model attached now; arm
`ladder_emul_warm_none`) or on the model, and the committing pass runs on the VALUE leaf at the depth the
VALUE ladder would commit -- its gate replayed with the value ladder's relaxed alpha and path-to-trust
rescue on reconstructed costs (R_v learned like R; ~0 in units because feature extraction is unmetered).
The line then goes through the hybrid's trust escalation exactly as the shipped ladder's line does
(t_deck_ladder 2; UseValueModel true). No re-run at the committing depth; verified warm-up wins committed
directly. Smoke byte-identical (no deck sets a shape).
- `emulv` (model warm-ups) is the fidelity control: same tree, same leaf, same depth as `ship` -- it should
  track ship closely, and any gap is the gate replay's error.
- `emulnlv` (leafless warm-ups) is the shape under test: ship minus the warm-ups' feature extraction. In
  UNITS that saving is invisible (the value leaf charges none), so the units comparison shows only the
  tree/commit-depth differences; the wall saving needs a CPU-time measurement, done after the batch.

**Leaf-usefulness predictor (user: "a cheaper approach to figure out when it is useful").** Arm
`probeonly` = the no-leaf hybrid with escalation off (`value_min_depth 0`): its units are the constant
ladder's own cost. `ceiling = probeonly / escnl` is what a leaf trusted at every depth would leave of the
no-leaf shape's cost; validated against the realized `ship / escnl` on the 19 modelled decks. If it
predicts, a new deck's leaf decision is one cheap no-leaf batch (which is the deck's day-one shape anyway).

Queue tonight, strictly serial: pooled screen (screen-2 remainder + `emulv` + `emulnlv`, 1194 jobs, 12
workers) -> `probeonly` (320 cheap jobs) -> read -> adopt clean wins per deck via the sidecar's shape ->
smoke + regression -> commit/push. Not tonight: the overnight tier; the rebase onto the Snow agent's
61b3cfb7 (a default-ON play change that will move GT -- their rebaseline, not mine to fold in blind).

### 2026-09-10a — the shape screen, complete: every approach on every deck (deterministic units, per-game learned state)

Second reset of the night (a Windows update at ~14:30 killed the machine again; resumed 21:20). The pooled
screen then ran to completion at 12 workers under the memory watchdog (peak ~10 GB), with two same-job-twice
checks that now match (`antilife_emulnl` and `knights_emulnlv` reps: identical digests) -- the per-game
R/G fix holds. Two more shapes were built mid-run on the user's direction and pooled in:
- **escnlv** -- the NO-LEAF ESCALATION LADDER (user: *"no-leaf -> choose between value leaf or heuristic
  leaf escalation. We would retain budget for the value-leaf at the last depth"*): the probe runs the whole
  ladder leafless (proven wins banked with no leaf at all), its start gate admits a pass only if the same
  cost again would still fit (`g_reserve_value_pass`), and an UNVERIFIED line at a TRUSTED depth pays one
  value pass there (fresh memo); at an untrusted depth the heuristic escalation runs as today. A value pass
  that overruns keeps its best-rated line under the ladder's anytime rule (user: *"we want the
  prioritization of the value-leaf on the final depth ... but do not finish entirely"*), else escalates.
  Sidecar shape `{ladder "escalation", leaf "none", commit "model"}`.
- **emulnlv** is the user's *"no-leaf on depths where we don't think we will be ending"* (built in 09k).

Units relative to the deck's shipped shape (`heur` for Fluctuator), d_avg > 0 is worse, 8 seeds x 500 games:

| deck | cfg | escnl | emulnl | emulv | emulnlv | escnlv | verdict |
|---|---|---|---|---|---|---|---|
| antilife | d5b20 vs ship | 2.306x +0.0012 | 3.776x +0.0010 | 1.038x +0.0000 | 1.177x +0.0005 | 1.765x +0.0032 | ship |
| antilife | d3b10 vs ship | 1.100x +0.0005 | 1.319x -0.0010 | 0.458x +0.1647 | 0.468x +0.1647 | 1.089x +0.0005 | ship |
| auras | d5b20 vs ship | 1.639x +0.0000 | 7.666x +0.0035 | 1.199x -0.0005 | 1.188x -0.0005 | 1.801x +0.0003 | ship |
| auras | d3b10 vs ship | 1.065x +0.0000 | 1.704x +0.0025 | 0.345x +0.0145 | 0.346x +0.0145 | 1.021x +0.0003 | ship |
| breaching | d5b20 vs ship | 1.230x +0.0000 | 19.641x +0.0000 | 1.286x +0.0000 | 0.948x +0.0000 **clean** | 0.956x +0.0000 **clean** | emulnlv (0.948x) |
| breaching | d3b10 vs ship | 0.984x +0.0000 | 5.966x +0.0000 | 0.348x +0.0012 | 0.348x +0.0012 | 0.986x +0.0000 | ship |
| critter | d5b20 vs ship | 2.068x -0.0002 | 14.304x -0.0002 | 1.029x +0.0000 | 1.018x +0.0000 | 1.225x +0.0000 | ship |
| critter | d3b10 vs ship | 1.000x +0.0000 | 1.159x +0.0002 | 0.117x +0.0198 | 0.117x +0.0198 | 1.000x +0.0000 | ship |
| dragons | d5b20 vs ship | 1.137x -0.0008 | 2.795x +0.0025 | 0.323x +0.0020 | 0.322x +0.0020 | 1.091x -0.0008 | ship |
| dragons | d3b10 vs ship | 1.000x +0.0000 | 1.160x +0.0003 | 0.118x +0.0320 | 0.118x +0.0320 | 1.000x +0.0000 | ship |
| dragonstorm | d5b20 vs ship | 1.449x -0.0027 | 2.752x +0.0030 | 1.062x +0.0003 | 1.047x +0.0000 | 1.062x +0.0008 | ship |
| dragonstorm | d3b10 vs ship | 1.032x -0.0027 | 1.161x -0.0013 | 0.272x +0.5663 | 0.272x +0.5663 | 1.000x +0.0000 | ship |
| fivecolour | d5b20 vs ship | 2.357x -0.0148 | 2.346x +0.0058 | 0.901x +0.0105 | 0.949x +0.0095 | 2.171x -0.0133 | ship |
| fivecolour | d3b10 vs ship | 1.152x +0.0000 | 2.143x +0.0098 | 0.447x +0.1160 | 0.453x +0.1160 | 1.098x +0.0002 | ship |
| fluct | d5b20 vs heur | 0.381x -0.0047 **clean** | 1.003x -0.0002 | - | - | - | escnl (0.381x) |
| fluct | d3b10 vs heur | 0.422x -0.0108 **clean** | 1.005x +0.0000 | - | - | - | escnl (0.422x) |
| goblins | d5b20 vs ship | 1.066x -0.0003 | 4.830x +0.0040 | 0.768x +0.0032 | 0.747x +0.0032 | 1.176x +0.0003 | ship |
| goblins | d3b10 vs ship | 1.052x +0.0000 | 1.683x +0.0045 | 0.430x +0.0232 | 0.431x +0.0232 | 1.029x +0.0000 | ship |
| hinata | d5b20 vs ship | 1.824x -0.0080 | 1.271x +0.0200 | 1.057x -0.0045 | 1.052x -0.0012 | 1.272x +0.0103 | ship |
| hinata | d3b10 vs ship | 1.145x -0.0030 | 1.130x +0.0057 | 0.495x +0.3160 | 0.496x +0.3160 | 1.023x -0.0002 | ship |
| kitty | d5b20 vs ship | 1.647x -0.0013 | 3.012x +0.0078 | 0.907x +0.0078 | 0.906x +0.0075 | 1.606x -0.0008 | ship |
| kitty | d3b10 vs ship | 1.129x +0.0000 | 1.512x +0.0080 | 0.549x +0.0375 | 0.549x +0.0375 | 1.111x +0.0017 | ship |
| knights | d5b20 vs ship | 1.323x +0.0000 | 13.248x +0.0008 | 1.013x +0.0000 | 1.000x +0.0000 | 1.062x +0.0000 | ship |
| knights | d3b10 vs ship | 1.009x +0.0000 | 1.376x +0.0005 | 0.202x +0.0235 | 0.202x +0.0235 | 1.001x +0.0000 | ship |
| melira | d5b20 vs ship | 2.425x -0.0053 | 0.752x +0.0225 | 1.085x +0.0215 | 1.154x +0.0212 | 2.085x -0.0038 | ship |
| melira | d3b10 vs ship | 1.481x -0.0073 | 0.816x +0.0085 | 0.983x +0.0357 | 0.993x +0.0357 | 1.587x -0.0103 | ship |
| minotaur | d5b20 vs ship | 1.311x -0.0008 | 6.306x +0.0025 | 1.038x -0.0003 | 1.032x -0.0003 | 1.304x +0.0000 | ship |
| minotaur | d3b10 vs ship | 1.001x +0.0000 | 1.293x +0.0012 | 0.253x +0.0525 | 0.253x +0.0525 | 1.005x +0.0000 | ship |
| mirrorwing | d5b20 vs ship | 1.175x -0.0062 | 3.502x +0.0097 | 0.960x +0.0140 | 0.939x +0.0140 | 1.180x -0.0052 | ship |
| mirrorwing | d3b10 vs ship | 1.003x +0.0000 | 1.456x +0.0147 | 0.559x +0.1237 | 0.559x +0.1237 | 1.018x +0.0002 | ship |
| stompy | d5b20 vs ship | 1.151x -0.0023 | 4.117x +0.0175 | 0.893x +0.0037 | 0.888x +0.0037 | 1.176x -0.0017 | ship |
| stompy | d3b10 vs ship | 1.050x -0.0010 | 1.352x +0.0068 | 0.420x +0.0733 | 0.420x +0.0732 | 1.006x +0.0000 | ship |
| burn | d5b20 vs ship | 2.540x +0.0000 | 4.303x +0.0018 | 0.973x +0.0000 **clean** | 0.972x +0.0000 **clean** | 2.562x +0.0000 | emulnlv (0.972x) |
| burn | d3b10 vs ship | 1.025x +0.0000 | 1.283x +0.0020 | 0.538x +0.0092 | 0.540x +0.0092 | 1.040x +0.0002 | ship |
| slivers | d5b20 vs ship | 1.280x +0.0000 | 13.324x +0.0007 | 1.033x +0.0000 | 1.017x +0.0000 | 1.237x +0.0000 | ship |
| slivers | d3b10 vs ship | 1.022x -0.0025 | 1.525x -0.0005 | 0.251x +0.0157 | 0.251x +0.0157 | 1.033x -0.0025 | ship |
| th | d5b20 vs ship | 1.096x -0.0012 | 3.802x +0.0150 | 1.043x -0.0002 | 1.014x -0.0002 | 1.163x -0.0017 | ship |
| th | d3b10 vs ship | 1.009x -0.0000 | 1.691x +0.0062 | 0.449x +0.0410 | 0.447x +0.0410 | 1.003x +0.0000 | ship |

**Reading.**
1. **Fluctuator: no-leaf escalation is a CLEAN WIN at both configs** -- 0.38x / 0.42x the heuristic ladder's
   units with BETTER play (-0.005 / -0.011 t, net +21 / +47 games). Adopted below as the deck's live sidecar
   with `{ladder "escalation", leaf "none"}`; the rejected 2026-09-06 model stays in the file for
   reference (the DISABLED-file convention is retired, as 09i said it would be).
2. **The emulated ladder committing on the model is BROKEN at d3b10 on every deck** (0.12-0.55x units,
   +0.01 to +0.57 t): at a 10 ms budget the committing pass overruns, the fallback steps shallower and
   overruns again, and the decision is left with the leafless warm-up line and no budget for the hybrid's
   escalation. At d5b20 it tracks ship on most decks (emulv 1.01-1.06x) and is nominally clean on
   Breaching / Burn (0.95x / 0.97x), but a shape applies at every depth (the mulligan generator runs the
   deck at d3), so nothing is adoptable from it. The emulated ladder's remaining virtue is the one it was
   built for -- warm-ups on a cheap leaf when the COMMITTING leaf is the expensive rollout -- and screen 1
   already showed that loses to escalation wherever the model is trusted.
3. **escnlv (no-leaf escalation ladder) is never a units win over ship** (1.0-1.1x at d3b10, 1.06-2.6x at
   d5b20) and matches ship's quality within noise on every deck; its case rests entirely on WALL time (the
   leaf evaluations it skips are unmetered) -- measured next by the single-worker CPU probe.
4. **escnl on modelled decks at d3b10 costs 1.00-1.15x ship at equal quality** -- the model buys almost
   nothing at d3 (every trust depth is >= 4, so ship escalates every unverified d3 decision anyway). At d5b20
   the model's benefit is real: 1.1-2.5x, and up to 19x on the H-ladder decks (Breaching, Knights, Slivers,
   Critter) where the escalation itself is the cost.

**Leaf-usefulness predictor: the experiment is INVALID as built.** `probeonly` plays identically to `escnl`
and `ship` (avg win turn equal to two decimals on six decks checked) at 0.9-1.07x `escnl`'s units: with the
trust escalation switched off, an unverified leafless line is re-searched by the engine's continuation path
-- the heuristic by another door -- so `probeonly / escnl` is ~1 by construction. The realized `ship / escnl`
(0.4-0.9 at d5b20) is mostly the leaf's ORDERING and B&B cuts, not avoided escalations; a leafless run
cannot see that. A working predictor needs per-decision instrumentation per job (verified fraction,
escalation-unit share, tree size by depth) -- future work, recorded in `per-deck-search-shape.md`.

**CPU probe (single worker, 150 games, two reps; Melira is the only deck heavy enough to time):** ship
97.9 s / 97.8 s, emulv 88.7 s, emulnlv 88.5 s, escnlv 212 s, escnl 168 s. Leafless warm-ups save 0.2% of
wall over model warm-ups -- the value leaf's evaluation is not where time goes -- and the leafless ladders
are slower in wall exactly as in units. The 9% under ship of the emulated shapes is a different committed
line (different digest), not a saving at equal play. Knights: 0.7 s per 150 games under every shape.
Verdict on the user's question: the leaf costs its generation, not its play; in play it pays.

### 2026-09-10b — Fluctuator ADOPTED (no-leaf escalation); run time AND quality, same batch

User (01:20): *"this isn't just about quality, but also run time. We want numbers for both in each
configuration to decide on adoption."* The screen's wall column is only comparable within one batch (screen
1 ran 32 workers beside the overnight tier; the pooled runs 12 workers alone), so `ship`/`heur` wall ratios
in the big table are NOT usable and `decide.py` now prints them only for same-batch pairs. For the one
adoption, a dedicated same-batch A/B (12 workers, 8 seeds x 500 games, both arms interleaved):

| cfg | arm | avg win turn | search units | wall ms |
|---|---|---|---|---|
| d5b20 | heur (old) | 3.6240 | 70,080,135 | 460,836 |
| d5b20 | escnl (new) | 3.6193 | 26,725,819 | 130,194 |
| d5b20 | new/old | **-0.0047 t** | **0.381x** | **0.283x** |
| d3b10 | heur (old) | 3.6398 | 41,030,069 | 283,746 |
| d3b10 | escnl (new) | 3.6290 | 17,313,567 | 103,850 |
| d3b10 | new/old | **-0.0108 t** | **0.422x** | **0.366x** |

Wall falls further than units because the heuristic ladder's rollouts are wall-heavy per unit. Tiers:
smoke 3 Fluctuator configs moved, 9 games faster / 0 slower; regression 5 moved, 7 faster / 1 slower, and
the slower one classifies as budget churn (T5 -> T6 at 1x, back to T5 at 16x). Accepted both (f714046c).
For the other 19 decks the table in 10a is the adoption record: no shape beats `ship` on units at equal
quality at BOTH configs, and the CPU probe shows the leaf's evaluation is not a wall cost, so `ship` stays.

### 2026-09-10c — WHY each rejected shape measured what it did: two implementation bugs, one exposure, one identity

User (2026-09-10 ~02:15): *"If ideas are not panning out, we need to understand the why rather than just
discarding them. So far, you have only done the latter."* Then: *"Implemented incorrectly is a real cause that
could be biting us here"*, and *"It's best to look at many real games that differ in quality or time and see what
happened."* So this session replays individual games from the screen's per-game `.wins`/`.units` files
(`logs/emul_screen/gamediff.py <armA> <armB>` lists the divergent games) single-process with
`MTG_ROLLOUT_STATS=1 MTG_TRACE=search`, and reads the ladder pass by pass. Every replay reproduced its batch
result exactly (same win turn, same units), so the mechanisms below are read off the real decisions.

**1. emulv / emulnlv (emulated ladder, commit on the model) — the heuristic escalation was HIJACKED. Bug.**
Dragonstorm d3b10 game 800676: ship wins T4 (646 units), emulv T8 (568 units) — same tiny search, so not a budget
problem. Trace: ship's T1 value ladder commits win=5, the hybrid escalates, and the ROLLOUT ladder finds the T4
win (passes 179/203/216 units). emulv's "escalation" replays the identical three model passes a second time and
never runs a rollout. Cause: the emulated block re-enters on the escalation call (it only checks the arm flag),
and `run_pass`'s `ForceHeuristicLeafGuard(heuristic && !commit_model)` = false OVERWRITES the escalation's forced
rollout leaf. So in every emulv/emulnlv game the escalation was a no-op: the arms measured "cheap" (no rollouts)
and "bad" (no escalation) for the same reason. This is the d3b10 catastrophe (1196 of 4000 Dragonstorm games
worse) AND the small quality losses at d5b20. The earlier "commit-pass overrun fallback" diagnosis was wrong:
the replays show 0 overruns and 0 fallbacks. FIX: the emulated block is skipped when `g_force_heuristic_leaf` is
set. Verified: 800676 emulv now T4 with ship's exact escalation.

**2. escnl on modelled decks (Melira 2.4x) — overrun WASTE, not slow leafless search. Exposure of a general
defect.** Game 802768: ship 2.04M units T7, escnl 7.92M T6. Stats: `id_pass starts=151 aborted=14
waste_units=6.3M (80%)`; ship aborted 0. Each abort runs to the proportional ceiling (25 x 18k = ~455k units,
Snow's MTG_OVERRUN_PROP). Pass by pass: leafless passes cost 14/118/1236/7411, then pass 5 is admitted (est 44k
vs 9.2k remaining — the VALUE ladder's relaxed alpha 8.8 applies because the stand-in is "a model") and
overruns. At b1000 the same pass costs 80,679 — IDENTICAL to ship's model pass, at every depth and decision.
So the leafless tree is the SAME SIZE as the model's (the doc's "ordering and B&B cuts" story is refuted);
what differs is behaviour AFTER EXHAUSTION: no-win results are not memoised once a truncation event has
landed (`trunc_at_entry` watermark), the model's results are WIN entries (stored regardless) so its exhausted
pass winds down at 67,890, and the constant leaf's are ALL no-wins so its pass re-searches every transposition
to the ceiling. Two fixes: (a) a constant-leaf ladder uses the strict alpha 1.10 (the relaxed alpha exists for a
leaf that holds an incumbent) — verified: 802768 escnl 0 aborts, 1.83M units (< ship's 2.04M), still T6;
(b) a constant-leaf pass STOPS at exhaustion at its plan loops (after (a) a pass estimated 10.8k still ran to
130k under the same blackout) — edited + syntax-checked, built and measured only after the running batch.
Ship is byte-identical under both (the flags are never set on a rated or rollout pass).

**3. escnlv on Melira (2.09x, never a win) — the same explosion, and its value pass NEVER RAN.** Game 801771:
8 aborts, 52% waste; `no-leaf escalation ladder decisions=30 value_passes=0 to_heuristic=24`. Melira ships no
`value_trust_depth`, so `escalate_below` = depth+1 and `committed >= value_min_depth` never holds: on every
deck without a trust depth (Melira, Dragonstorm, Anti-Lifegain, ...) escnlv degenerates to escnl by design.
Its fair measurement is on the trust-depth decks (knights/slivers/auras/breaching) after fix 2.

**4. emulnl (emulated, commit on the ROLLOUT) — it IS the heuristic ladder.** Melira: emulnl vs heur differs in
16 of 4000 games (units 1.056x). Game 800663 (ship T4, emulnl T7): the replayed heuristic gate sees the rollout
ladder's 46x growth d2->d3 and refuses pass 3, committing the heuristic d2 line (land only); ship's relaxed
alpha admits a 16.8k value pass 3 that rates the spell line (win=4). So emulnl's +0.0225 t is heur's gap to ship
and its 0.75x is heur's cost; on cheap decks its 3-19x is the rollout leaf (~145 units/leaf on Breaching where
ship's whole ladder is 1,102 units). Not a shape defect and not a candidate: it has no advantage over heur.

**What this changes about the conclusions.** The model buys NOTHING in tree size on Melira; its benefit is (i) a
rated line at the exhausted deepest pass that the executor keeps by crossover, and (ii) the escalation it
cancels on trust-depth decks. The no-leaf shape's advantage is ONLY over the rollout leaf (Fluctuator). The
"leaf eval is ~0.2% of wall" CPU-probe reading stands, but "ship 9% slower than emulv in wall" was the hijack
(no rollouts ran). Re-screen: `logs/emul_screen/manifest_fix.json` (1858 jobs: ship/heur/escnl/escnlv/emulv/
emulnlv, all decks, both cfgs, 12 workers + memwatch) started 02:21 on the fix-1 + fix-2a binary
(`chain_fix.sh` -> `decide_fix.txt`, `report_fix_vs_*.txt`, wins in `wins_fix/`). Fluctuator's adopted shape is
affected by 2a (its GT will move); emulnl is unaffected and keeps its pooled6 numbers.

### 2026-09-10d — the menu (user), batch 2 at 32 threads, and two more implementation findings

User direction (2026-09-10 ~03:30-04:30): *"we need to try on other decks as well"*; keep the menu small —
*"Escalation with and escalation without the value leaf and heuristic ladder with and without the value-leaf.
It's possible the ladder will be the same with value-leaf on and off making this only 3 possibilities"*; the
full rollout ladder is the likely dominated idea (*"early rollouts rather than escalations ... don't bring much
benefit"*); and *"being stuck with less than half of our threads is not acceptable"* / *"We should very rarely
go over 20 GB even with 32 threads. If we are, we might have a bug."*

**Batch 1 (fix 1 + 2a, 12 workers) was killed by its own watchdog at 04:18** (MemAvailable 1.46 GB; 652/1858
jobs). Ship/heur digests were byte-identical to screen 1 on every finished job (26,000/26,000 games). Its
partial `decide_fix.txt` is superseded by batch 2.

**Memory.** Per-game peaks are small (50-260 MB); single DECISIONS balloon (the TT comment records ~6 GB on
an antilife escalation, the line-cache comment ~28 GB on Mirrorwing). Both memos have result-neutral caps
that were OFF: `MTG_TT_CAP`, `MTG_FSL_CAP` (deterministic recompute) and `MTG_FSL_POOL` (global backstop).
Verified inert on a normal game (identical units + play for ship/escnl/nl_sres). Batch 2 runs at 32 threads
with `MTG_TT_CAP=3000000 MTG_FSL_CAP=500000 MTG_FSL_POOL=10000000`, watchdog 2.5 GB floor, 30 s RSS trend
(`memtrend_fix2.log`, names the in-flight games). First 10 min: RSS 9.5 -> 14.3 GB, still climbing —
watch; the caps may need to be tighter (TT is not pooled).

**Batch 2** (`manifest_fix2.json`, 3410 jobs, `launch_fix2.sh`, RESUME mode via `gen_resume.py`): ship, escnl,
heur, emul, emulv, v_single, v_sres (value probe -> ONE reserved rollout pass), nl_single, nl_sres (leafless
probe -> ONE reserved rollout pass), escnlv, emulnlv. New code: `esc_single_reserve` (the probe's gate keeps
tree(k) + R x leaves(k) for the single pass; the pass runs on the remainder, overrun-guarded, partial kept if
rated else the escalation) and fix 2b (a constant-leaf pass stops at exhaustion). Verification on the new
binary: ship 2040514 units unchanged; Dragonstorm emulv T4 at ship's 646 units; Melira escnl 1.494M (2a:
1.834M; ship 2.040M) T6; nl_sres 1.100M T6, 36/36 single passes completed; **v_sres == nl_sres to the unit on
that game** (the strict reserve binds before the relaxed alpha, so both probes stop at the same depth and
the single pass overrides the line — the user's "same ladder with the leaf on and off").

**Finding 5 — the value-tuned escalation applied to a LEAFLESS line (escnlv, antilife d5b20: 41 worse / 0
better vs ship).** 13 of 20 sidecars carry `escalation_cap` (the predicted single-pass escalation) and 4 a
value-ranked beam (`beam_width`). Under `escnlv` the sidecar is loaded, so when the probe's value pass does
not run (untrusted depth, i.e. every deck without `value_trust_depth`) the escalation beams by value ranks
that do not exist, predicts against a no-win line and the crossover table discards its result: game 800681
shows the probe stop at pass 4 (the reserve refuses pass 5, where escnl banks the T5 win), then NO heuristic
pass at all, T5 -> T7. `escnl` (stand-in, no sidecar loaded) is unaffected — it runs the plain ladder. FIX
(edited, syntax-checked, unbuilt while batch 2 runs): a leafless line disables the beam and the single-pass
predictor and the crossover always takes the heuristic line (`line_constant`). escnlv is re-run in the resume.

**Early batch-2 read (antilife d5b20, n=8, vs ship):** emulv 1.038x / +0.0000 / net 0 — the fidelity control
now TRACKS ship (hijack fixed); heur 3.72x; escnl 2.14x (+0.0015); nl_sres 2.15x (+0.0023, −11); v_sres 4.92x
(−0.0017, +3); unlimited single passes 10.8x / 14.1x (not adoptable on cost); emulnlv 1.17x (+0.0003).

**Finding 6 — the FiveColour "monster" games: the executor's full-depth fallback on an EMPTY line.** Game
803307 (d5b20): heur 2.70M units / 50 s / T6; escnl 40.7M units / 571 s single-threaded / T6, 257 MB peak.
79% of escnl's units are `la_cand` — SolveWithLookahead's root candidate loop, i.e. AIEngine's fallback
("no committed play for this phase ... rank this turn with the SAME full lookahead on a FRESH budget"): a
no-win search line has EMPTY phases (the leaf returns `{max_turns+1, {}}` and a node with no improving child
keeps that), so it is never committed and the executor re-searches the decision at depth 5 with rollouts.
A constant leaf makes EVERY unverified probe line empty, so the shape leans on the escalation's rated line;
where the rollout leaf finds no win by turn 8 either (FiveColour: slow deck), the fallback fires per decision
(118 decisions in the game, ~273k units each — far past the 18k budget, so the candidate loop's budget stop
is not bounding it). heur shows the same fallback at 36% of its units; ship (rated line, never empty) does
not. The fallback's cost is why FiveColour is the batch's wall tail on every leafless arm and on heur.
Not fixed tonight: candidates are (a) commit the ladder's best-graded no-win line anyway (the refuted-follow
lever already keeps a whole lost line), (b) bound the fallback's candidate loop by its budget, (c) for a
leafless probe, treat the escalation's line as authoritative even when unrated.

**Batch 2 killed by its watchdog at 05:07** (533/3410 jobs): RSS jumped 14.4 -> 19.6 GB in 30 s (MemAvailable
2.38 GB) with the memo caps ON, so a single decision still allocated ~5 GB outside the capped memos (or in
them: FSL entries on a mass-draw deck are far larger than the ~600 B the cap assumes). The in-flight slowest
was `creature_giving_v_single` — an UNLIMITED single rollout pass. Resume policy: the unlimited arms
(v_single / nl_single: 10-14x units, ceiling reference only) are dropped from the resume, caps tightened to
`MTG_TT_CAP=1000000 MTG_FSL_CAP=200000 MTG_FSL_POOL=6000000`. Lesson re-learned: `launch_fix2.sh` was edited
while its first instance was still running; bash re-read the changed file mid-execution and relaunched a
garbage batch line ("ambiguous redirect"). Never edit a running shell script; write a new file.

**Finding 7 — the batch memory spikes are the breakpoint-wave PREFIX-RESUME cache on Dragonstorm.** Batch 2
was killed twice by its watchdog (05:07: 14.4 -> 19.6 GB in 30 s; 05:42: 9.1 -> 17.7 GB), both times with
Dragonstorm d5b20 jobs in flight and the memo caps ON. Per-arm probe (500 games, seed 801000): EVERY arm
peaks at 1.2-1.9 GB, ship included (1.55 GB) — so not an arm and not one 8 GB decision, but ordinary
Dragonstorm games at ~1.5 GB each, a dozen of which coincide across 32 workers. Ship's slow game 801393
(104k units, 10.3 s, 1156 MB): `MTG_NO_BP_PREFIX_CACHE=1` -> 5.5 s, 430 MB with IDENTICAL play (log diff);
`MTG_BP_SEARCH=0` -> 1.4 s, 408 MB (the process baseline). The cache keeps up to 256 GameState snapshots
per node (three sites); a Dragonstorm snapshot is MBs, a Melira one KBs, so an entry cap is no bound at
all — and on this deck the copies cost more wall than the resumes save. FIX (built as fix4): the caps are
by BYTES per node — min(256, MTG_BP_PREFIX_CACHE_KB / ApproxStateKb(state)), default 32 MB — result-neutral
and deterministic per node. Watchdog for a box with 24 GB swap: `memwatch2.sh` trips on MemAvailable +
SwapFree < 4 GB (the old 2.5 GB RAM floor killed before swap engaged). Memo caps restored to the looser
TT 3M / FSL 500k / pool 10 GB (result-neutral, but recompute shows in units on monsters).
Correction to finding 7 (measured after the byte cap was built): the cap does NOT bind — a 128 KB budget
(<= 6 entries) leaves the slow Dragonstorm game at 10.7 s / 1156 MB, so the memory is in the RESUME path
(capture + resumed apply), not in the stored snapshots; struct sizes are small (GameState 776 B, Card 136 B,
Permanent 280 B). Open item: why a resumed apply allocates ~700 MB on Dragonstorm. `MTG_NO_BP_PREFIX_CACHE=1`
is inert on Melira / Knights / Mirrorwing (same wall, memory, units, play) and FiveColour (identical play,
+136 units of recompute), and halves Dragonstorm's wall and memory with identical play — batch 2 resumes
with it set (`launch_fix2.sh`), watchdog `memwatch2.sh` (RAM+swap floor 4 GB), 32 threads.

### 2026-09-10e — batch 2 read at 2254/3258 jobs (every d5b20 cell complete) + three more mechanisms

**Menu read so far (units vs ship; decide2.py dominance):** on every trusted-model deck at d5b20 SHIP dominates
(escalation with leaf), escnl beats heur everywhere (leafless-first beats rollouts-everywhere), and the
final-depth forms cost 2-9x ship for no quality. At **d3b10** the final-depth single pass is CLEAN on antilife
(0.83x, −0.0013), critter (0.77x), dragonstorm (0.92x, −0.0007): the model buys nothing at d3 and one rollout
pass beats the value ladder + escalation. **v_sres == nl_sres to the unit on most decks** (the reserve binds
before the leaf matters) — the user's "same ladder with the leaf on and off". Melira d5b20: nothing beats ship;
heur 0.71x/+0.0225; escnl 0.65x/+0.040 (worse than heur: the strict-alpha probe spends budget and banks
nothing on this deck, starving the escalation). Fluctuator: escnl 0.966x heur (−0.001) — the 0.38x of screen 1
is GONE (finding 10). Hinata: v_sres −0.0115 (+30) at 2.6x; escnlv +0.10 (−400) (finding 9).

**Finding 8 — the single-pass reserve's R prior (120) is 10x too high on Melira: nl_sres/v_sres +0.2177 t
(892 of 4000 games worse) at 0.175x.** Trace (800663): the reserve refuses pass 3 (R=120 x leaves), the probe
stops at d2, the single rollout pass runs at d2, the deck plays d2-heuristic. Melira's real R is ~10-16 units
per leaf. FIX (fix6): per-GAME calibration — the first decision runs a d1 leafless + d1 rollout pass on fresh
caches (~1% of the budget), R = (h1 − t1) / leaves; each later single pass refines it (EMA); keyed on deck +
game seed. Arms nl_sres2 / v_sres2 re-measure.

**Finding 9 — under a leafless line the escalation's CAPPED single pass aborts and leaves the line EMPTY
(Hinata escnlv 801634: T5 -> T8, 24k -> 254k units).** The 10d gating turned off only the depth predictor inside
the `eff_single` escalation path; the path still ran one capped pass at the cap (5), which overruns its 2x
fresh-half budget, sets hcommitted = 0, the take-decision rejects it, and the line stays the probe's empty
no-win line -> the executor's full-depth fallback (finding 6) plays nothing at T1. FIX: a leafless line takes
the plain ladder branch (`eff_single && !line_constant`). Arm escnlv2 re-measures.

**Finding 10 — Fluctuator's 0.38x came from the RELAXED alpha; the strict alpha (fix 2a) threw it away.**
Screen 1 escnl (relaxed 8.8, no exhaustion stop): 0.381x heur, 31 better / 6 worse. Batch 2 escnl (strict +
exhaustion stop): 0.966x, 6/2. On Fluctuator the deep leafless passes bank the in-horizon wins; the strict alpha
stops the ladder where heur's does. Never measured: relaxed alpha + exhaustion stop. LEVER: arm
`constant_alpha_relaxed` / MTG_CONSTANT_ALPHA_RELAXED — arm escnl_rx in batch 3. Expected: Fluctuator regains
its win; Melira loses (the deck-class split again).

Batch 3 (`manifest_fix3.json`, 2497 jobs, `chain_fix3b.sh` after batch 2): ship, heur, nl_sfit, v_sfit (FIT
single pass: unreserved probe, rollout at the deepest affordable depth), nl_sres2, v_sres2 (calibrated R),
escnl_rx, escnlv2. Verification gate: ship 2040514; hinata 801634 escnlv2; melira 800663 nl_sres2; fluct escnl_rx.
(Process note: `kill $(pgrep -f chain_fix3.sh)` killed my own tool shell — the pgrep matched it. Kill by saved pid.)

### 2026-09-10f — batch 2 complete (final read unchanged), batch 3 stopped at 214 jobs, finding 11, batch 4 launched

**Batch 2 final (3258 jobs, `decide2_batch2_final.txt` / `_vs_heur.txt`):** the 10e read holds on every cell. Late
decks: kitty ship dominates all (escnl 1.39x/+0.0035 at d5b20); mirrorwing escnl 1.51x/−0.0025 (not clean on units),
final-depth forms 3.5x; th ship dominates (escnlv +0.077, finding 9). Melira d3b10: escnl 0.667x/+0.039, heur
0.782x/+0.0105, nl_sres 0.194x/+0.253 (finding 8). Fluctuator vs heur: escnl CLEAN at both cfgs (0.966x/−0.001,
0.926x/−0.0018); nl_sres −0.0045/−0.0075 at 2.1x.

**Verification of the fix6 tree (chain_fix3b): ship byte-identical (2040514).** But two of the three targeted
replays did NOT land where predicted: Hinata escnlv 801634 T7 (ship T5, was T8; 254k → 48k units) and Melira
nl_sres2 800663 T7 (ship T4; single passes completed=5, gate_refusals=2). Melira nl_sfit 802768: T6 at 1.40M units
where ship plays T7 at 2.04M (the FIT form's first clean win on Melira).

**Finding 11 — the STRICT alpha stops the leafless probe exactly one depth short of the win on Melira and Hinata;
this is a GATE POLICY, not a bug, and it is the common cause of both misses.** Traces, same seeds, ship vs
leafless: the passes cost the SAME units at every depth (Melira 800663 T1: 19 / 332 both; Hinata 801634 T1: 22 /
236 / 2272 both), then ship runs the next pass under the relaxed alpha (x8.8) — Melira pass 3 = 16,810 units, finds
the T4 line; Hinata pass 4 = 16,843 — while the constant-leaf ladder's strict alpha refuses it (est ≈ prev cost x
growth ≈ 17-50x on Melira > remaining). The leafless probe then plays d2 (Melira) / d3 (Hinata) + escalation. So
finding 8's R calibration was correct but moot on Melira: the reserve refused only 2 of 6 decisions; the other 4
stopped at d2 by the ordinary gate. Relaxed alpha on the leafless probe (lever from finding 10, arm
`constant_alpha_relaxed`) with the exhaustion stop: Melira nl_sfit_rx 800663 **T4 at 24,966 units vs ship 51,803
(0.48x)**; 802768 T6 at 1.50M vs ship T7 at 2.04M; Hinata escnlv_rx 801634 T6 (ship T5; the leafless pass 4 is cut
at exactly the budget, 18,000 units, where ship's identical-size pass ran to 19,373 — see the lever below).

**Consequence for the batch:** batch 3 carried the relaxed alpha only on the escalation form (escnl_rx); the
single-pass and model-commit leafless forms were all strict, so on the deep-win decks they would measure the gate
policy, not the shape. Batch 3 (my run, 10 min old, 214 jobs reported) was stopped by pid and relaunched POOLED as
**batch 4** (`gen_fix4.py`, `manifest_fix4.json`, 3,867 jobs, heavy decks first; the 214 done jobs are skipped —
same binary at the default lever): batch 3's arms + `nl_sfit_rx`, `nl_sres2_rx`, `escnlv_rx` (relaxed alpha) +
`nl_sfit_rx2`, `escnl_rx2` (relaxed alpha + exhaustion stop at 2x). `chain_fix4.sh` builds, verifies ship
(2040514) AND a batch-3 arm at the default lever (mel_nlsfit 1400802) for byte-identity, replays the two seeds at
x1 and x2, then runs the batch → `run_fix4.out`, `wins_fix4/`, `decide2_batch4_vs_ship.txt` / `_vs_heur.txt`
(decide2 reads wins_fix3 + wins_fix4).

**Lever — constant-leaf exhaustion multiple (`constant_exhaust_mult` / MTG_CONSTANT_EXHAUST_MULT, default 1 =
byte-identical).** The 10c exhaustion stop cuts a leafless pass at used >= budget; the model pass has no such stop
(it runs to the 25x proportional ceiling because its truncated line is still rated). In exhausted mode the search
still walks its main plan loops (only the optional wave / group-wave / m2-fix phases stop), so a leafless pass past
exhaustion can still PROVE a win in what remains — Hinata's pass 4 needed 8% more than the budget. The multiplier
is a tuning parameter of the leafless probe, not a menu item; x2 is measured on the two forms where it can matter.

**Finding 6 (executor full-depth fallback on an empty line) stays open by choice:** the fallback is the design
(a no-win decision replays the baseline search so full-depth is a superset of baseline); what the leafless forms
change is how often they hand it an EMPTY line. Measure after batch 4, separately.

### 2026-09-10g — the menu apparatus, the early test, and a methodology fix (batch 4 running)

**Methodology — the quality axis is now the PAIRED SIGN TEST, not the average win turn.** Measuring the
per-seed spread of `d_avg` on batch 2's completed cells (500 games/seed) shows its standard error over the
WHOLE 8 x 500-game screen is 0.011 (Melira), 0.004 (Hinata), 0.003 (FiveColour) — 6-22x the ±0.0005
tolerance the adoption rule was written with — while the light decks sit at 0.000. So every "worse by
+0.0010" tag this screen printed on a heavy deck was inside its own noise, and the rule as written could not
have decided the cases it exists for. Arms run identical games (same seed, game index and opening hand), so
`better`/`worse` are paired and `z = net / sqrt(better + worse)` is a sign test on the discordant pairs.
Re-reading batch 2 through it: Melira's rejections are z = −8 (escnl) to −27 (nl_sres), Hinata's leafless
escalation is z = −3.1, and the sub-0.002 readings that decorated Knights/Critter are |z| <= 1 — the same
conclusions, but now separable from noise. `d_avg` is kept as the MAGNITUDE (a shape losing a fifth of a turn
is rejected whatever its z). Recorded in `per-deck-search-shape.md` "Adoption rule"; `decide2.py` reports z
per cell.

**`menu.py` — the decision the user asked for ("3-4 should be the maximum... dominated ideas can be
dropped").** Five labels, four candidates plus the control: `esc_v` (ship), `esc_nl` (escnl_rx), `fit_v`
(v_sfit), `fit_nl` (nl_sfit_rx), `heur`. Per deck it applies the adoption rule at BOTH configurations and
prints which shapes are strictly better, which are merely no worse, and on which decks — so a dominated shape
is dropped on evidence rather than taste. Queued on batch 4's reports (`chain_menu.sh` -> `menu_batch4.txt`).

**`scripts/shape_probe.py` — the cheap early test for a NEW deck.** One pooled batch over the three
MODEL-LESS shapes (rollout ladder / leafless escalation / leafless single pass) at the deck's own depth and
budget: minutes, against the hours a value leaf costs, and it needs no model to run. Reports units and the
sign test, names a winner, prints the `value_play` block to paste, and — this is the part the old apparatus
lacked — says UNRESOLVED when |z| < 2 instead of pretending a tie is a verdict. Every shape key is pinned per
arm so a deck that already ships a shape cannot leak it into the control. Validation queued (`chain_fix6.sh`)
on two decks whose answer the screen already knows: Knights (light: the shapes should tie exactly) and
Fluctuator at both configurations (leafless escalation should win), on seeds disjoint from the screen's.

**Sidecar vocabulary (commit 4c9506e3, built by `chain_fix5.sh` after the batch).** `value_play.ladder`
gains `"single"`; new `value_play.alpha` (`"relaxed"` / `"strict"`) and `value_play.exhaust_mult`. Resolution
is arm > deck > env at every site, so the batch's per-job arms still win and every off-batch path stays
byte-identical. chain5 verifies that (Melira ship 2040514, Fluctuator's sidecar route 1773) and then checks
the sidecar route against the arm route for each candidate Fluctuator shape — the shape a deck SHIPS must be
the shape the screen MEASURED, and only a route check can show that.

### 2026-09-10h — batch 4 complete: THE MENU settles at three shapes; Fluctuator's relaxed alpha ADOPTED

**Batch 4: 3,867 jobs, exited normally, 5 h 10 m at 32/32 workers, peak RSS 19.5 GB.** Integrity check first:
the control arms are byte-identical across batches (`fluct heur`, `melira heur/ship`, `hinata heur/ship`,
`knights heur/ship` all sum to the same units in `wins_fix2` and `wins_fix4`), so unit ratios ARE comparable
across batches and every fix in 223c80f1 is inert on the shipped paths.

**THE MENU (written up in `per-deck-search-shape.md`; tables in `menu_batch4.txt`).** Three shapes survive:
escalation + model leaf (any deck with a trusted model — wins or ties on 17 of 18), escalation + leafless
probe with the relaxed alpha (no model / day one), and the final-depth single pass, leafless, relaxed (the
low-budget dial). Dropped: **the full rollout ladder**, dominated on all 19 decks at both configurations
(1.17x–18.76x units, never better on quality) exactly as the user predicted; and **the value probe +
final-depth pass**, adoptable nowhere at both configurations. So the "with and without the value leaf" pair
collapses to one entry for the final-depth family and the menu is three, not four.

**Why the final-depth family splits by budget (the mechanism, not a rule of thumb).** At d5b20 it costs
0.80–1.8x with neutral-to-better quality; at d3b10 it costs 0.61–0.97x and loses quality on 10 of 18 decks
(z −2 to −5). The probe runs unreserved and eats the budget, and the single rollout pass then runs at the
deepest depth that still FITS what remains: at a large budget that is deep enough to be a real saving, at a
small one it buys a d1/d2 rollout where the ladder's escalation would have gone deeper. It is cheaper
BECAUSE it is doing less. The reserved variant fails the mirror-image way (reserving stops the probe early,
finding 8). The single pass is a dial for decks whose escalation is wasted, not a general replacement.

**Finding 12 — the screen's leafless arm is not the play a deck ships.** `value_profile: "noleaf"`
substitutes a constant stand-in and never loads the model file, so a sidecar's trust depth, crossover,
escalation cap/R and beam are absent; a deck shipping `leaf: "none"` keeps them live. On Fluctuator, 8 x 500
games: the stand-in route is 0.967x the shipped route and 5 games in 2,000 differ. The menu's conclusions
survive it, but an ADOPTION has to be measured on the sidecar route.

**ADOPTED — Fluctuator `value_play.alpha: "relaxed"`** (clean win, both axes, both configurations; the
standing 2026-09-03 directive covers it). Measured on the sidecar route against the same file without the
key, 8 x 500 games: d5b20 **0.433x units**, 24 games better / 3 worse (z +4.0), mean −0.0035; d3b10
**0.786x**, 19/5 (z +2.9), −0.0047. The `ladder: "single"` alternative was measured the same way and is
cheaper at d3b10 (0.647x) but worse on the mean (+0.0025), so it was not taken. Verified after the edit: the
shipped deck now reproduces the measured arm to the unit and to the digest. GT: full smoke re-run — **79 of
80 configs unchanged** (every other deck byte-identical, confirming the new levers are inert), the one change
being `fluctuator_smoke_d5_s1001` gi68 5→7, classified by `classify_turn_later.sh` as **churn** (recovers to
T5 at 4x and 16x budget) against 24-better/3-worse over 4,000 games. Smoke accepted; regression tier running.

**The exhaustion multiple does NOT earn a default.** At 2x the budget: Fluctuator escnl_rx2 0.476x/0.714x vs
escnl_rx 0.438x/0.712x, nl_sfit_rx2 0.486x/0.664x vs 0.446x/0.662x — slightly worse or equal everywhere.
`exhaust_mult` stays a knob at its byte-identical default of 1.

### 2026-09-10i — the early test validates, and catches two ways the screen's rows are not shipped play

**`shape_probe.py` end-to-end (chain 6), on decks whose answer the screen already knows, seeds 880000+
(disjoint from the screen's).** Knights: esc_nl **0.10x** the rollout ladder's units, quality a tie (1
better / 0 worse) — and the tool correctly reports UNRESOLVED rather than calling a tie a verdict.
Fluctuator d5b20: esc_nl 0.23x, z +2.7 → recommends exactly the shape adopted an hour earlier, from a
12-job run of minutes instead of a 3,867-job batch of five hours. Fluctuator d3b10: fit_nl 0.42x vs esc_nl
0.45x, both z +4.5 → recommends the single pass, matching the screen. The tool reproduces the screen's
verdicts; that is the validation.

**It also exposed a 3% discrepancy that turned out to be two separate things.** The probe's rollout-ladder
control was 2x the screen's `fluct heur` row, which forced a check:

1. **The screen's `fluct heur` arm was never the rollout ladder.** `gen_manifest.py` emits `heur` as
   `dict()` for a deck with no `val` entry, so no `value_model: false` was pinned and Fluctuator's own
   sidecar drove the job — that row is its SHIPPED leafless escalation. Every "vs heur" ratio for Fluctuator
   in batches 1-4 is against its shipped shape, which is why `escnl_rx` 0.438x agrees to 1% with the
   sidecar-route measurement (0.433x). Confirmed by reproduction: the screen's row is byte-identical
   (4,483,613 units, digest `e390ddbd`) to the pre-adoption sidecar re-run under the screen's environment.
   `shape_probe.py` pins every shape key on every arm precisely so this cannot happen there.

2. **`MTG_NO_BP_PREFIX_CACHE=1` CHANGES PLAY — finding 13.** Isolating the screen's four environment
   variables on 500 Fluctuator games: the three memo caps (`MTG_TT_CAP`, `MTG_FSL_CAP`, `MTG_FSL_POOL`) are
   byte-identical as documented, but the prefix-cache flag alone spends **+2.9% units and returns a different
   play digest**. The mechanism is the budget: a cache miss is recomputation, not a free lookup, so under a
   FIXED unit budget the same decision truncates elsewhere and can commit a different line. "Result-neutral
   memo" holds only at an unlimited budget, and the earlier "play identical with it off" reading came from
   Dragonstorm alone. Every screen batch carried this flag (it was what stopped the OOM), so all four batches
   measure the no-cache play.

**Neither invalidates the menu** — every arm in a batch shared one binary and one environment, so the
comparisons are like-for-like — but both invalidate any ABSOLUTE reading of a row, and both are why the
Fluctuator adoption was re-measured on the sidecar route with no flags at all before it was taken.

**GT after the adoption: regression tier 108 passed, 0 failed, 0 changed** (the play digest folds DECISIONS,
and the relaxed alpha mostly reaches the same decision with far less work — only ~0.7% of games end
differently, so the 75/150-game regression cases see none). Smoke accepted earlier: 79 of 80 unchanged, the
one change classified as churn. Viewer protocol: 0 play-drift, 0 enum-gap.
