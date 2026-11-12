# Analysis ledger — Morph

Deck: `decks/Morph/Morph.cod` (R/G morph-pump aggro, 60 cards, no sideboard):
4 Blistering Firecat, 2 Hystrodon, 4 Den Protector, 1 Spellskite; 4 Birds of Paradise,
4 Ignoble Hierarch; 4 Berserk, 3 Temur Battle Rage, 4 Might of Old Krosa, 2 Vines of Vastwood,
4 Legion Leadership // Legion Stronghold (MDFC); 4 Wild Defiance; lands 7 Forest, 1 Mountain,
4 Stomping Ground, 4 Gruul Turf, 1 Rootbound Crag, 1 Fire-Lit Thicket, 2 Wooded Foothills.

The USER asked for the analysis on 2026-11-11 ("Please analyze the morph deck.").

Branch/worktree: `morph-analysis` @ `.claude/worktrees/morph` (cut from origin
`phase-1-2-deck-analyzer` 6b3dea976). The deck folder was COPIED from the primary checkout
(untracked there; original left in place). Scryfall JSONs: `build/scratch/morph/scry/` (primary
build volume).

**The plan (what the deck is trying to do).** Get a creature onto the battlefield early (a dork-
accelerated face-down morph for {3}, or a cheap Den Protector), then turn Blistering Firecat face up
for {R}{R} (a 7/1 trample haste that is sacrificed at end of turn — but a face-down Firecat that
entered last turn is not summoning sick, so the unmorph swings at once) and stack pump spells:
Berserk (+X/+0, X = power), Legion Leadership (double power, first strike), Temur Battle Rage
(double strike), Might of Old Krosa (+4/+4 in a main phase), Vines (+4/+4 kicked). Every targeting
instant on our creature also triggers Wild Defiance (+3/+3 each). Ignoble Hierarch's exalted adds
+1/+1 to a lone attacker.

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells, never blocks, never targets. Opponent-facing clauses
  (trample, Den Protector's "can't be blocked by smaller", Vines' "can't be the target of spells your
  opponents control", Spellskite's redirect, Legion Leadership's first strike) are goldfish-inert —
  model what is cheap to model, and every deferral is PROVISIONAL until the USER signs off.
  Self-binding clauses bind us: Firecat's end-step sacrifice, Berserk's end-step destroy, megamorph's
  +1/+1 counter, Den Protector's regrowth (OUR graveyard).
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces are SEARCHED, provider-narrowed (restrict/order only), or collapsed by a stated sound
  dominance fold, and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic); morph is an ENGINE mechanic usable by any card.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time; total threads <= 24.
- Agents: no `cd` in compound commands, no `rm` / `rm -rf`, no `timeout`. Direct `mtg` runs must
  pass `--cards-json <worktree>/src/cards/data/cards.json` (the primary's cards.json is otherwise
  loaded).
- Subagents never push the shared branch; over ~250K context they write a handoff and stop.

## Stage 1 — coverage (2026-11-11)
Missing (11): Blistering Firecat, Hystrodon, Den Protector, Spellskite, Berserk, Temur Battle Rage,
Might of Old Krosa, Vines of Vastwood, Legion Leadership // Legion Stronghold, Wild Defiance,
Fire-Lit Thicket. Present (8): Birds of Paradise, Ignoble Hierarch, Gruul Turf, Forest, Mountain,
Stomping Ground, Rootbound Crag, Wooded Foothills (re-verify against Scryfall in Stage 2).
Fetch-hold review required (Wooded Foothills; hold is modelled).

**No morph support exists in the engine** (no face-down casting, no turn-face-up action). That is the
Tier-3 centre of this analysis.

## Stage 2 — cards (in progress)
Research fan-out launched 2026-11-11: (R1) morph/megamorph engine design — Firecat, Hystrodon, Den
Protector; (R2) pumps + targeting triggers + remaining cards — Wild Defiance, Berserk, Temur Battle
Rage, Might of Old Krosa, Vines, Legion Leadership MDFC, Spellskite, Fire-Lit Thicket, and a
re-verify of the 8 present cards. Drafts: `logs/morph/research/*.md` (gitignored; decisions below are
the durable record).

### Research R2: pumps + other cards (done 2026-11-11; draft logs/morph/research/pumps.md)
- Re-verify of the 8 present cards: NO mismatch (Stomping Ground oracle text formatting only).
- All five pumps ride the existing searched-target family (`solo_target_trick`); every new payload
  goes into the ONE shared payload function (`ApplyTrickPayload`) that executor and rollout both call.
- **Wild Defiance — Tier 2.** No "becomes the target" trigger exists. New watcher in the shared
  payload, applied BEFORE the spell's own effect (so a later doubler doubles the +3/+3).
- **Berserk — Tier 2.** +X/+0 (X = target's real combat power at resolution); new end-step DESTROY
  flag iff it attacked; refused in a post-combat main.
- **Temur Battle Rage — Tier 2.** Sets the existing `temp_double_strike`; both combat paths already
  apply pumps/exalted across two damage steps.
- **Might of Old Krosa — Tier 2.** Model the main-phase "instead" branch (always +4/+4 today since
  no non-main cast path exists).
- **Vines of Vastwood — Tier 2 + ENGINE GAP:** the generic kicker emits ONE kicked cast with no
  target (TurnSolver.cpp ~40097) -> kicked Vines pumps nothing. Kicked casts must fan per target.
  Unkicked stays offered (it targets -> Wild Defiance).
- **Legion Leadership // Legion Stronghold — Tier 2.** Doubling as Berserk; back face needs an
  "always enters tapped" flag; {R/W} hybrid already handled.
- **Spellskite — Tier 1 body** (0/4 artifact creature) + the redirect (see below).
- **Fire-Lit Thicket — Tier 1**, Cascade Bluffs model.
- **ENGINE GAP — cast order:** solo_target_trick casts skip the rank ordering (ManaPayment.cpp ~4283)
  unless the provider opts in (`OrderOpaqueCastsByRank`), so pump-then-double vs double-then-pump is
  an accident of HAND ORDER today.
- **NOT INERT (corrects the standing decisions above):**
  1. Spellskite redirect: with Wild Defiance out, redirecting our own pump onto an attack-ready
     Spellskite triggers Defiance again (+3/+3 per Defiance per redirect, 2 life). PLAN: implement
     (an unapproved deferral of a non-inert clause is not allowed).
  2. Instant-speed doublers: Berserk / Legion Leadership cast AFTER the lone attacker's exalted
     trigger double the exalted bonus too; main-phase-only casting loses it. Needs a declare-attackers
     cast window (Tier 3). PLAN: count how often it binds, then build after the morph core.
- Proposals (USER-OWNED, not adopted): cast order creatures/flips -> Wild Defiance -> flat pumps ->
  TBR -> doublers (2(P+k) >= 2P+k); targeting folds: never pump a non-attacker; all non-Berserk pumps
  of a plan on ONE creature (exchange argument; t^n -> t); Berserk keeps its own target; unkicked
  Vines dominated unless a Defiance is out; lone-attacker vs alpha rule (needs A/B).
- Provisional inert deferrals: Berserk trample, TBR ferocious trample, Legion Leadership first
  strike, Vines' opponent-targeting protection, Defiance on opponents' spells.

### Research R1: morph / megamorph (done 2026-11-11; draft logs/morph/research/morph.md)
- **Tier 3, no Tier-4 part.** (CR text not in repo; megamorph is CR 702.37b — confirm in INT-2.)
- Data model: STAND-IN CARD, no new Permanent field. Each morph card gets a generated
  "<name> (Face-down)" CardDefinition (colourless nameless vanilla 2/2) at load — the bestow/kicker
  pattern. Every hash/dominance key already carries the name -> no sizeof(Permanent)/Dominance change.
  Flip reuses Bloodline Keeper's face-swap helper (keeps pumps, counters, sickness; CR 708.8).
- The real cost: leaving the battlefield (CR 708.9) — ~40 zone-push sites need RevealedCard(); SBA
  backstop + test-only MTG_FACEDOWN_AUDIT aborts on a missed site. LKI stays the face-down 2/2.
- Face-down cast: Action::face_down on CastRiders, both apply paths; flat {3}; ~14 dedup signatures get
  a face-down marker (PlanDomKey and BpCandFingerprint don't carry evoke either — fix both).
- Turn face up: PermAbilityMode::TurnFaceUp; megamorph counter via PutPlusCounters; Den Protector
  regrowth reuses ResolveEtbReturnGyToHand (Timeless Witness).
- Flip position fold (sound dominance, X-locking doublers): Firecat/Hystrodon flips offered at main
  start or right after their own face-down cast; Den Protector at "first" AND "after all casts"
  (complete given the regrown card opens a searched breakpoint). Combat / main-2 flips dominated.
- END-OF-TURN FLIP WINDOW (real decision): flip at the OPPONENT'S end step via BoundaryFan (Defense of
  the Heart pattern) — a Firecat flipped there misses its own sacrifice trigger and attacks next turn;
  leftover mana; Den Protector regrowing the Firecat sacrificed at our end step.
- New params: morph_cost, megamorph_cost, endstep_sacrifice_self, combat_damage_self_may_draw,
  turned_face_up_return_gy_to_hand (+ internal face_down_real_name).
- Second main: combat_damage_self_may_draw -> DeckUsesSecondMain (Curiosity draw hook).
- ROUTING: today Wooded Foothills routes this deck to AntiLifegainProvider (fetch hold OFF) -> one
  MorphProvider routed above it (INT-1 creates it; INT-2 extends the signature).
- Viewer: face-down cast = plan-variant flag; main-phase flip = plan verb; Den Protector target reuses
  `dig`; NEW decision type `face_up` at the opponent's end step; Hystrodon "may draw" folded.
- Provisional (USER sign-off): Den Protector evasion inert; trample inert; Hystrodon "may" folded to
  "draw iff library non-empty"; no combat flip; no flip in our end step / either upkeep; flat {3}.
- Risks: missed zone-push site; face-down vs hardcast signature collisions; an early flip stranding a
  colour; search cost (3x rule); value-leaf features blind to flip potential; copies of face-down
  creatures (none in deck).

## Stage 2 — integration plan (serial)
- INT-1 (running): the eight non-morph cards, kicker fan, Spellskite redirect, MorphProvider, cast-order
  dominance fold. Brief logs/morph/int1-brief.md.
- INT-2: morph per logs/morph/research/morph.md.

### INT-1 result (2026-11-11; branch morph-analysis, local)
- cards.json: Wild Defiance, Berserk, Temur Battle Rage, Might of Old Krosa, Vines of Vastwood, Legion
  Leadership // Legion Stronghold, Spellskite, Fire-Lit Thicket (costs verbatim from Scryfall; all "full"
  in --coverage-only; the 3 morph cards stay missing).
- Engine (all in the ONE shared trick payload / resolver unless noted): FireSpellTargetWatchers (Wild
  Defiance, before the spell's payload; deck stamp deck_has_spell_target_watch); trick_double_power (+X/+0
  off CombatPowerOf, after the watcher and the flat part); grants_temp_double_strike; mainphase_power_bonus
  ("instead" in a main phase); kicked_power_bonus on the "(Kicked)" face + KICKED TRICKS FAN PER TARGET (the
  generic kicker block now skips solo-target tricks -- no existing kicker card is one, so byte-identical);
  Berserk: Permanent::destroy_at_end_if_attacked swept by DestroyAttackedAtEndStep at BOTH end-step sites
  (deck stamp deck_tracks_attacks arms the attack count), cast_only_before_combat_damage refused in a
  post-combat main; mdfc_back_enters_tapped; ClassifyMainPhase / DeckFeedsCombat / EvalCard terms; the
  trick-target equivalence key carries the Berserk bit.
- SPELLSKITE REDIRECT built: Action/CastRiders/StackEntry::trick_redirect, one extra {U/P} pip
  (AddSpellRedirectPip at every cost recompute site; the phyrexian post-pass emits the 2-life twin), every
  plan/dedup key. Offered only with a target watcher on the battlefield or in hand, Spellskite able to
  attack, the original target able to attack, no magnet. Viewer: plan variant `verb: redirect`. KNOWN GAP:
  the LineSpec has no `redirect` key (CheckLine never matches a redirected plan to a hand line).
- MorphProvider (routed FIRST, above `anti`): derives from DeckProvider; keeps only the shared exalted-aware
  attack hold. Cast order = sound dominance tiers (permanents < additive tricks 21 < doublers/double strike
  22) with OrderOpaqueCastsByRank. Non-attacker trick-target fold (TrickTargetsAttackersOnly) ON, premises
  stated in code. Certificate NotAssessed.
- NOT built (USER-owned / lossy, listed for the USER): F2 one-target concentration, F3 unkicked-Vines
  dominance, F4 lone-attacker attack rule; declare-attackers cast window (INT-3).
- INT-3: declare-attackers cast window (doublers after exalted) — after measuring how often it binds.

### INT-2 result (2026-11-11; branch morph-analysis, local) -- PARTIAL, handoff logs/morph/int2-handoff.md
- cards.json: Blistering Firecat, Hystrodon, Den Protector (costs verbatim from Scryfall; all 19 cards "full" in
  --coverage-only). Params morph_cost / megamorph_cost (flat, one per card, THROWN otherwise),
  endstep_sacrifice_self, combat_damage_self_may_draw, turned_face_up_return_gy_to_hand; internal
  face_down_real_name. Keywords Morph/Megamorph stripped in audit_card_fields (kicker precedent, G8 default).
- FACE-DOWN FORM = a DB-synthesized "<name> (Face-down)" vanilla colourless 2/2 (built from nothing, so no
  real param leaks). No Permanent field; every name-keyed hash/dominance key tells it apart.
- CAST: Action/CastRiders::face_down (int8: 1 = face down for {3}; 2 = face down AND turned face up at once,
  {3} + the morph cost in ONE bill, flipped as its enter cascade completes -- the hand-source LEAD flip, so the
  batch prepay sees the whole cost). Def swap at both cast sites (apply_one, CastSpellFromHand); FaceDownCastCost
  is the one cost expression at every recompute site; every plan/dedup key (incl. PlanDomKey and
  BpCandFingerprint) carries it (#FD0/1/2 in both plan signatures -- lockstep check clean bar the old #BB).
  face_down 2 is FOLDED AWAY (sound) when the printed cost is pip-wise inside {3}+morph (Hystrodon).
- TURN FACE UP: PermAbilityMode::TurnFaceUp (LEAD: TurnSolver::ApplyFaceUpLeadActs before every cast, both
  worlds) and TurnFaceUpLate (TRAIL: the trailing pass). Position fold FaceUpLeadDominates (no turned-up
  trigger, face-up power >= 2 -> LEAD only; argued in SpellEffects.h). Den Protector: both positions, a
  hand-source TRAIL, and its regrowth SEARCHED (one variant per distinct graveyard name, + hand
  instants/sorceries for TRAIL; Witness shape; human = dig chooser). Megamorph counter via PutPlusCounters.
  Shared body TurnFaceDownPermanentUp (CR 708.8: same permanent, no ETB).
- Firecat: PerformEndStepSelfSacrifices at both of our end-step sites (face-up only). Hystrodon: DamageDrawsOwedBy
  clause (kMayDraw fold, PROVISIONAL) + draw_ping stamp + DeckUsesSecondMain (G4: main 2 ON for this deck).
- CR 708.9: RevealedCard at ~70 battlefield->zone sites; NormalizeFaceDownZoneCards backstop (executor SBA +
  rollout turn boundary) with the test-only MTG_FACEDOWN_AUDIT abort. deck_has_morph / deck_has_endstep_self_sac.
- Viewer: plan flags face_down 1/2, verb face_up + face_up_pos; board rows show the REAL name + face_down /
  megamorph / face_up_cost; composer +facedown / +facedownup. MorphProvider signature carries the morph params.
- NOT BUILT (owed, in order, see the handoff): the OPPONENT-END-STEP flip window (BoundaryFan axis + executor
  crossing node + face_up decision type + MTG_FACEUP_AXIS_STATS), the faceup= composer token / CheckLine verb,
  the TRAIL-regrowth breakpoint check (MTG_BP_PROBE), PlanHasDeadFaceUpPin, step 8 eval for flip potential.
- Gates (6e304038e): unit 2855/2855; scenarios 493/493; smoke play-changed=0 (searched + d0); audit_card_costs
  608/608; coverage 19/19 full. Pooled batch with MTG_FACEDOWN_AUDIT=1 (no abort): d0 6.68 (100 g), d3 b10
  4.40 (100 g), d5 b20 4.45 (60 g), audit d3 b10 4.395 (200 g). d0 is slow -- the greedy rarely flips (step 8).

## Approved deferrals
(none yet)

### INT-2b result (2026-11-12) -- the opponent-end-step flip window
- BUILT: morph.md step 7 -- the searched opponent-end-step flip (BoundaryFan face-up axis, executor crossing
  node, `face_up` viewer decision on all four sites, MTG_FACEUP_AXIS_STATS tripwire = 0 in searched frames on
  the 460-game pooled batch), the Hystrodon weak-dominance fold (G3 default), the `faceup=` composer token,
  and rule 6c (a hand-source TRAIL flip without a face-down cast -- an identity fold).
- Sanity clock (pooled, FACEDOWN_AUDIT on, no abort): d0 6.68 -> 6.68, d3 b10 4.40 -> 4.41, d5 b20 4.45 -> 4.48.
- OPEN (logs/morph/int2b-handoff.md): (A) at a finite budget no line holding a face-down Firecat to the
  opponent's end step is ever simulated (the fixture passes only at b0); (B) Den Protector's TRAIL regrowth of a
  card cast that section does not open a breakpoint (the section window diffs m_numbers) -- root cause found;
  (C) PlanHasDeadFaceUpPin deferred (not sound without a "can put a hand card in the graveyard" predicate);
  (D) d0 flip knowledge (step 8) not started.

### INT-2c result (2026-11-12) -- open findings A and B closed (commit 1ebdb3bb2)
- (A) ROOT CAUSE was not a budget gate: with the land drop already made, a main whose enumeration had any plan
  got NO pass (FSLineWin's idle node and SolveWithLookahead's pass were appended only to an EMPTY list), so the
  scenario's main 1 was {flip the Firecat} and the hold line was unenumerable at every budget and depth (d3 b0
  passed only via the root's no-win fallback + the executor's crossing node; d2 b0 failed). FIX:
  `FaceUpHoldPassMissing` appends the pass (with its sub-decision axes) in both hosts while the end-step flip
  axis is live and the list has no apply-empty plan -- adds a line, removes none. morph_eot_flip_firecat now runs
  at d3 b100 (passes d2/d3/d5 at b0..b100000), plus a d2 b0 twin.
- (B) FIX: `TurnSolver::SectionForgetHandExits` drops every section-snapshot number no longer in hand, in both
  worlds at the same point (after the casts and the Krenko taps, before the trailing pass); a card cast then
  regrown by the trailing-pass flip reads as new. Remand / buyback returns (inside one cast) stay old.
  morph_den_protector_regrow_pump.json activated (fails with MTG_BP_HAND_ENTRY=0, as it should).
- Gates: unit 2860/2860; scenarios 496/496; coverage 19/19 full; pooled batch (FACEDOWN_AUDIT on, 24 thr, same
  manifest as INT-2b) d0 6.68, d3 b10 4.41, d5 b20 4.4833, audit 4.395 no abort -- every digest IDENTICAL to INT-2b
  (fan crossings 333,815 -> 334,135, units +0.1%); SMOKE 195/195, play-changed=0 searched and d0.
- Still open: (C) PlanHasDeadFaceUpPin, (D) d0 flip knowledge (step 8).

### INT-2d result (2026-11-12) -- morph.md step 8: the beyond-horizon pump / flip policy
- DIAGNOSIS: the d0 gap (6.68 vs d3 4.41) was NOT mainly flip knowledge. The greedy (SolveUncached: the d0 runner
  and the playout past the horizon) (1) counted no trick in its this-turn win projection, so it never saw a pump
  kill, and cast every affordable trick for its EvalCard estimate (d0 s910000: 237 of 306 pumps on a non-lethal
  turn, 53 with no attack at all; the d3 search: 57 of 246); (2) aimed every target variant at the FIRST creature
  (equal evals, smallest-mask tie -- Legion Leadership + Berserk on a 0-power Spellskite beside a face-down
  Firecat). Face-down creatures DO attack and ARE legal pump targets at d0; the flip was already offered.
- BUILT (greedy walk only; never EnumeratePlans, so no searched frame moves; MorphProvider opts in through
  `DecisionProvider::GreedyHoldsCombatTricksForLethal`, MTG_MORPH_GREEDY_TRICK_HOLD=0 = the old greedy):
  * `GreedyTrickStackCredit` -- per recipient, the subset's LEAD flips (face-up power - 2, +1 megamorph), additive
    pumps (Might's main-phase +4, kicked Vines +4) each with the spell-target watchers' +N (Wild Defiance, on board
    or cast by the plan), then doublers, then double strike, then TRAIL flips; credited into the win projection.
    A mana-dork recipient gets no credit when the cost needs the dorks' mana.
  * `GreedyHeldTrick` -- a trick whose whole payload is this turn's combat, and a flip of a face that sacrifices
    itself at end step (Firecat), is HELD unless the subset projects lethal (the HoldCastUnlessLethal shape).
  * PLAYOUT HEURISTIC FOR THE USER (permitted greedy zone, named here because it is a cast policy): beyond the
    horizon the deck now holds its pumps and the Firecat flip for a projected kill turn.
- MEASURED AND DELETED: an end-step FRONT that leaves a Firecat face down (the menu still fanned it): d0 300 g
  5.0433 vs 5.0600 and 5.1000 vs 5.0967 on a second seed block -- noise, so no lever was kept.
- Known gap (not fixed, payment-side): the payer may tap a pumped 0-power dork that the lands could have spared
  (d0 s910020 T4). Denying dork recipients outright measured WORSE (100 g 5.04 -> 5.63).
- Clock (ONE pooled batch, same manifest/seeds as INT-2c, FACEDOWN_AUDIT on, 24 thr): d0 6.68 -> 5.04,
  d3 b10 4.41 -> 4.37, d5 b20 4.4833 -> 4.4333, audit d3 b10 200 g 4.395 -> 4.38, no abort. Census: searched-frame=0,
  real-end-step=2 -- both in the d0 job (depth 0: ChooseRealCrossingFaceUp returns at depth <= 0, the D0Runner
  greedy; a searched-only rerun reads 0). MTG_FACEUP_AXIS_STATS=2 now also prints each real-end-step window.
- Scenario test/scenarios/morph_d0_hold_pumps_for_lethal.json (d0; fails with MTG_MORPH_GREEDY_TRICK_HOLD=0).
- Value-leaf note (R9) stands: ExtractMidGameFeatures sees a face-down creature as a plain 2/2.

### INT-3 decision (2026-11-12) -- the declare-attackers cast window (D7 / Q2): MEASURED, PROVISIONALLY DEFERRED
- Census (`logs/morph/census/exalted_doubler.py` over the INT-2d d3 b10 game logs, 100 games, 211 attack turns):
  24 turns had a LONE attacker + >=1 Ignoble Hierarch + a main-1 Berserk / Legion Leadership. The exalted bonus a
  post-exalted doubler would add (E x (2^n - 1), x2 under Temur Battle Rage) would have turned a non-lethal attack
  lethal on **0 of 24** (most were already lethal; the non-lethal ones fell short by 7-17 against a gain of 1-6).
- So the window is not built. It stays a PROVISIONAL Tier-4 deferral, disclosed as UNDER-RATING (lower bound only:
  the search cannot see the window, so it never plans a line that needs it). Re-measure if a list adds exalted
  density or more doublers.

### Provider routing A/B (2026-11-12, HEAD 2c2dc16f7) -- MorphProvider vs the pre-INT-1 route (AntiLifegain)
- Same binary, same seeds (930000 block), two concurrent 12-thread batches; arm B pinned with MTG_PROVIDER_DECK to
  decks/Anti-Lifegain (the provider Wooded Foothills routed this list to before INT-1). Avg win turn:
  d0 600 g 5.1467 vs 7.2550; d3 b10 600 g 4.5617 vs 4.7200; d5 b20 400 g 4.5475 vs 4.7100 (units -8% / -9%).
  The d0 gap includes the Morph-only beyond-horizon trick hold (INT-2d); the searched gap is the cast-order fold,
  the non-attacker fold and dropping AL's fetch-hold-off / tutor / discard hooks. Output build/scratch/morph-provab/.
- MorphProvider stays (USER sign-off still collected: it replaced a route the deck rode before).

## Claude-play sweep
- commit: `f2df0a83f`
- seeds: 50000 games: 18
- driver: Claude agents (Sonnet players, one per game index 0..17 at seed 50000+gi; every flag adjudicated by an Opus
  verifier). Benchmark = the search at play settings (d5 b20).
- flags: 1 unresolved
  - REAL ENGINE BUG (g10, T2): a spell//land MDFC played as its land back face (Legion Stronghold) and bounced by
    Gruul Turf returns to hand as a land-only object -- the Legion Leadership front is lost (CR 712.8a: off the
    battlefield/stack a DFC has only its front face). Fix site: the MDFC back-face builder in CardDatabase.cpp
    should set `mdfc_front_name` so BounceKarooLand restores the front (and the other leave-battlefield paths).
  - dismissed (g17, T5): a self-defeating 4-spell plan payable only by tapping the lone attacker Birds -- a legal
    optimistic proposal the player chose; the shipped search does not pick it.
- win turns: Claude = search on 12 games; Claude faster on 4 (g0 4 vs 5, g3 3 vs 4, g5 7 vs 8, g15 4 vs 5) -> 5c
  budget-starvation check; Claude slower on 2 (g11, g12).
