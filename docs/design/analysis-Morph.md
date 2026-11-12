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

### INT-4 result (2026-11-12, commit `01be1014d`) -- the verify_deck + claude-play sweep items
1. **MDFC back face leaves the battlefield as its front (CR 712.8a)** -- the sweep's g10 bug; see the sweep section
   below. Generic: `RevealedCard` maps a played back face (Legion Stronghold, Soporific Springs, Turntimber,
   Boulderloft / Slitherbore Pathway) to its front at every battlefield -> zone push, both worlds; BounceKarooLand's
   private Pathway block folded into it.
   SMOKE (not accepted): every non-Morph config BYTE-IDENTICAL (195/199, incl. the MDFC decks Auras, Melira, Merfolk,
   Selesnya v2, Stompy -- none bounces or kills a back-face land in those seeds). Morph moves: d0 5.110 -> 5.109
   (gi888 T5 -> T4), d3 4.58 = 4.58, d5 4.49 = 4.49, 2HG d5 5.28 -> 5.24 (gi8 T6 -> T5); slower = 0 in every
   config; 10 searched + 5 d0 games change play at the same score, every explained one holds both Gruul Turf and
   Legion Leadership (the search now sees a bounced Stronghold come back as a castable pump).
2. **viewer `face_up` HARD MISS was a FORCING GAP in the auditor, not the emitter.** The prompt needs a face-down
   permanent of ours AND its morph cost payable from untapped mana at the opponent's end step; the developing line
   (longest plan) hard-casts or taps out, so the window never opened. `pick_face_up_window`
   (scripts/audit_viewer_decisions.py): with a face-down target on board choose a cast-nothing plan (land drop
   kept), else the cheapest face-down cast of the target; `face_up` is attributed to its OPTION names (source is
   "Opponent's end step"). `--verify-card`: Firecat VERIFIED gi0, Hystrodon gi1, Den Protector gi6 (face_up + dig),
   source_confirmed on all three.
3. **viewer_wiring `dig`**: the Den Protector reuse row in tools/play/DECISIONS.md had its emitter/GUI cells shifted
   (the provider's `GyReturnToHandPick` in the emitter column). The regrowth IS wired (`ResolveEtbReturnGyToHand` ->
   `g_play_dig_chooser`); the row now names `WriteDigDecisionJson` / `digPanelHtml`.
4. **play_invariants advisories**: a "<X> (Face-down)" / "(Kicked)" / "(Bestowed)" cast is a cast variant of hand
   card X (`SYNTH_FACE_SUFFIXES`, scripts/play_invariants.py).

### INT-5/6/7 result (2026-11-12) -- the #TT dedup fix, the USER's largest-attacker rule, the Spellskite twin; rebased + GT accepted
Commits (after the rebase onto origin/phase-1-2-deck-analyzer cc24d1362): `8128e0dd4` INT-5 (engine dedup fix: the
solo-target trick variant keyed `#TT` in both plan-signature builders), `240c33075` INT-6 (the USER's largest-attacker
rule, cast-time sentinel, trail-flip look-ahead), `dbe31df40` INT-7 (USER rulings 5 + 6, below). Pre-rebase hashes
aad2a29a9 / ee5ada3e0.

**USER rulings (verbatim, 2026-11-12).**
1. "The largest attacking creature should be the pump target always. We should have a heuristic for this."
2. "I think we should plan which we are going to flip in the plan."
3. "Even though technically it doesn't use the stack, we may as well." / 3b. "That way our heuristic can use this information."
4. "Or the heuristic can look at whether it is flipped in the plan if that occurs later, but yes, you have the option of doing the flips earlier if you need to."
5. "Yes, let's drop spellskites ability heuristically unless Wild Defiance is active. If it is active we should keep it for the potential double pump."
6. "The important thing if we are pumping this way is that Spellskite should not be the target even if it is the largest creature. The reason being that we cannot get the benefit of redirecting."

**Rulings 5 + 6 as built (INT-7).** The sentinel carries the {U/P} redirect twin only while a target watcher
(spell_target_watch_pump_*, generic) is on our battlefield OR IN HAND (a hand Wild Defiance the plan casts first resolves
before the trick, because permanents go first; one the plan never casts leaves the twin as the plain cast at {U/P} more,
which is dominated), and only when a non-redirector recipient exists. The twin's ORIGINAL target is the largest attacker
EXCLUDING every redirector (`FindBestOwnAttacker(..., exclude_redirector)`, `redirects_spell_target_to_self`): its
Defiance trigger fires on the cast, then the payload goes to Spellskite with Spellskite's own trigger. The plain variant
is unchanged (largest, Spellskite counted). The beyond-horizon credit (GreedyTrickStackCredit) does the same. Human play
and MTG_UNPRUNED / tricktarget are unchanged. Unit tests: no Defiance gives no twin; Defiance in hand or active gives the
twin; with Spellskite the only creature there is none; the twin resolves with both triggers; with Spellskite LARGEST the
twin's original target is the next-largest.

**Held-out A/B** (seeds 60000+, play settings d5 b20 for Morph and value_play d6 for Mirrorwing, one pooled batch per
binary, 24 threads). A = committed tree f64f2eae7, B = dedup fix only, C = dedup + largest rule (ee5ada3e0),
C' = C + rulings 5/6 on the rebased tree (dbe31df40). "f/s" = faster/slower games vs A, sum = summed turn delta vs A
(an unwon game counts as 9).

| job (games) | A avg | B avg (f/s, sum) | C avg (f/s, sum) | C' avg (f/s, sum) | units B / C / C' vs A |
|---|---|---|---|---|---|
| morph (1000) | 4.5030 | 4.2570 (229/1, -246) | 4.2550 (233/3, -248) | 4.2550 (233/3, -248) | 0.712 / 0.623 / 0.623 |
| morph2hg (300) | 5.3067 | 4.9067 (100/2, -120) | 4.9067 (100/1, -120) | 4.9033 (101/1, -121) | 0.740 / 0.627 / 0.631 |
| mirrorwing (600) | 4.1683 | 4.1550 (8/0, -8) | = B (byte-identical) | not run (no Spellskite) | 1.096 / 1.096 |
| mirrorwing2hg (200) | 4.6050 | 4.5800 (5/0, -5) | = B (byte-identical) | not run | 1.121 / 1.121 |

Whole-batch wall (all four jobs, uncontended): A 287 s, B 185 s, C 157 s. The C' (215 s) and C_rb (368 s) Morph-only
walls ran on a host loaded by another container, so units are the cost figure for those two. Two separations:
C_rb (C's code on the rebased tree) vs C is byte-identical on all 1300 Morph games, so the rebase does not move the A/B.
C' vs C_rb (rulings 5/6 alone): morph 2 games play-changed at the same score; morph2hg 5 play-changed, 1 faster,
0 slower; units +0.0% / +0.6%. Mirrorwing C == B confirms the rule binary equals the dedup binary there (no Morph
provider). Mirrorwing's +10-12% units come from the dedup fix (targets are now distinct plans). Mirrorwing is 0.024x
Hinata, so this is immaterial to the cost gate.

**d8 b0 recovery** (ONE pooled batch, final binary dbe31df40; play-setting controls reproduced arm C's digests exactly).
Every slower game reaches its old turn or better: A/B morph gi116 (C vs B) T4, gi154 T5, gi259 T4, gi852 T5;
morph2hg gi183 T5, gi242 T5, gi254 (B vs A) T4, gi258 T5; smoke morph d0 gi399 (T6 -> T7) T5; regression morph d0
gi213 (T4 -> T5) T4, gi828 / gi932 (T6 -> T7 pre-rebase) T4. The same 12 games give the same turns on the
pre-ruling-5 binary.

**Cost gate:** not triggered. Morph per-game units FELL to 0.62x the parent (suite_cost was 1.72x Hinata).

**Rebase** onto origin/phase-1-2-deck-analyzer (17 commits ahead of merge base 6b3dea976). There was one conflict,
test/regression_gt.txt in 6c572d7b2 (the Morph suite entry): origin's side was taken for every row and the header, and
our Morph rows were kept as rows (they exist only on our side). Code merged cleanly, and the diff-of-diffs (merge base ->
old HEAD vs origin -> new HEAD, excluding GT) is identical, at 17,343 lines. check_gt_logs consistent. Post-rebase build:
mtg-test 2872/2872, scenarios 502/502.

**Smoke + regression (full tiers, dbe31df40).** Smoke 192 pass / 7 fail, regression 307 / 9; searched slower 0 in both;
d0 slower 1 each (gi399 / gi213, recovered above). Movers vs origin's GT:
- morph smoke d0 5.110 -> 5.053, d3 4.58 -> 4.36, d5 4.49 -> 4.24, 2HG 5.28 -> 4.80; regression d0 5.056 -> 5.005,
  d3 4.5867 -> 4.1733, d5 s2002 4.5467 -> 4.1867, d5 s3003 4.58 -> 4.4533, 2HG 5.20 -> 4.6267 (all searched keys 0 slower).
- mirrorwing smoke d3 / d5 unchanged score (1 / 2 games play-changed), 2HG 4.68 -> 4.64; regression d3 4.3067 -> 4.2933,
  d5 s2002 4.1867 -> 4.1822, d5 s3003 = (8 play-changed): the dedup fix.
- extraturns_regression_d5_s3003: 1 game (gi36), score T4 unchanged. OURS, not drift: origin's binary reproduces origin's
  GT on every extraturns regression key. Bisected (rebased commits) to `5d55fe9f6` INT-2c, and 5d55fe9f6 with only its two
  `SectionForgetHandExits` calls removed reproduces origin's digest. So it is part B, the generic "section window
  forgets cast cards" fix, not the morph-gated hold pass. The effect is a same-score tie on the winning extra turn:
  our line casts Extraplanar Lens + Expropriate + Nexus of Fate and sacrifices Myriad Landscape before the Ulamog
  attack, and origin's plays the land and attacks. Both win the same turn.
- Martyr: no longer moves. The stale keys INT-6 saw were upstream drift (origin re-accepted Martyr in 0425e34e4 /
  01a3adbc1), and the rebase took origin's GT.

**GT ACCEPTED** (bar met: 0 searched slower unrecovered at d8 b0; every engine-moved deck net faster on the held-out
A/B): smoke `--deck=morph,morph2hg,mirrorwing,mirrorwing2hg`, regression the same plus `extraturns`. check_gt_logs
consistent 1131; the GT diff holds only those keys plus the header and the two notes. OVERNIGHT is left for the periodic
overnight run (the morph / mirrorwing / extraturns overnight keys will move there).

**Open USER questions (unanswered; the rule stands as built):**
- Temur Battle Rage onto a creature that already has double strike: "largest" (x2 DS score) picks it again, so a second
  TBR in one turn adds nothing. Amend to "largest creature without double strike" for a DS-granting trick?
- Tie-break is battlefield order (oldest first). OK?
- (Ruling 5 implementation detail) the twin is also kept when Wild Defiance is only IN HAND (the plan may cast it first).
  The alternative is battlefield-only, which would miss a Defiance cast earlier in the same plan.

### INT-8 result (2026-11-12) -- USER rulings R7-R14: Vines kicked-only, the R8 cast order, Den Protector's inline flip, grouped Spellskite redirects, Berserk first, watcher-in-plan, second TBR elsewhere, non-mana tie-break
Commit `264360a7c` (code + unit tests), then GT accept + this section.

**USER rulings (verbatim, 2026-11-12).**
- R7: "Vines should only be cast with Kicker heuristically."
- R7b: "Sorry, except for with Wild Defiance."
- R8: "We should also work on the order. Wild Defiance goes early, firecat and most other morphs can go next doublers near the end and den protectors morph goes last targeting doublers like Berserk or Legion Leadership preferably, otherwise another pump like Might of Old Krosa or Vines. Berserk is clearly the best option, Legion Leadership or Might of old Krosa might depend on remaining mana. Arguably Den Protector megamorph might want to go before Temur Battle Rage so that other spells can get the benefit from it. If we are protectoring for Might of Old Krosa it probably wants to go before Legion Leadership as well. Please attempt to fill in the rest of the gaps and present it to me."
- R8b: "So the idea is fixed pumps go earlier and doubling pumps go late."
- R8c: "Whether we get them from hand or from Den protector's megamorph."
- R9: "Spellskite Redirects unfortunately need to be considered after each redirectable spell or perhaps as just a group like \"Redirect all Pump Spells\", keeping in mind that doublers are fair targets as well. True, we probably should take Den Protector's megamorph counter into account for doublers if we can afford to do so. Sometimes we need the spell first so we can return it. The berserk vs not is an easy question to answer. If we can find lethal we don't need to search it, I guess it's true we could just rank and test it first, though. Berserk very easily finds lethal lines in my experience, but it does it a little other help. But yes, it is rare to cast Berserk without lethal."
- R10: "Double Berserk is almost always lethal. Hence Den Protector for Berserk is pretty much always the best play."
- "Never pump a creature that can't attack this turn." (USER-APPROVED.)
- R11-R13: "It should be disallowed heuristically if no Wild Defiance ends up in the plan. Yes, Temur Battle Rage 2 should target another non-zero power creature. (after pumps) Ties can go to the oldest if the creatures do not differ in ability to tap for mana. We should prefer non-mana-producing targets."
- R14: "Keeping in mind that the sizing is done relative to the morphed body if morph is used."

**As built** (all MorphProvider, generic/param-driven; each lever an EnvOn + HeuristicArm slot, DEFAULT ON; off under human play
and MTG_UNPRUNED / tricktarget):
- A `MTG_MORPH_VINES_KICKED_ONLY` -- TrickKickModes: a kicker solo-target trick emits only its kicked variant unless a
  target watcher is on the battlefield or in hand (the ruling-5 test); R11 then requires the watcher on board or CAST IN
  THE SUBSET for the unkicked variant. No affordable kicker + no Defiance = no cast.
- B `MTG_MORPH_CAST_ORDER_R8` -- CastOrderRank: watcher 4 < every other permanent (generic 10/20; LEAD flips already run
  before every cast) < fixed pumps 21 (Might, Vines kicked or not) < Temur Battle Rage 22 < doublers 23 (Berserk, LL).
  A Den Protector regrowth recast takes its card's slot (the breakpoint continuation re-sorts by rank).
- C `MTG_MORPH_DEN_FLIP_INLINE` -- a NAMED trail flip (TurnFaceUpLate + tutor_target) runs right after the cast that puts its
  target in the graveyard: TurnSolver::FaceUpInlineTrailDue, dispatched through each world's own trailing body from the
  top-level cast-section hook (ApplyPlanDirect slot_hook / AIEngine exec_slot_hook, lockstep), and SectionForgetRegrown
  keeps the regrown card "new" for the section's hand-entry breakpoint (the recast). A battlefield Den's trail flip naming
  a card ALREADY in the graveyard is no longer emitted (its LEAD twin flips before every pump). Whether to flip and what to
  return stay searched. The look-ahead (PlanTrailFlipsFor) still sizes Den's face-up body for every pick before the flip;
  after it the body is face up and the entry goes inert. LIMIT: a flip inside a breakpoint CONTINUATION's own plan keeps
  the trailing position (top-level section only).
- D `MTG_MORPH_REDIRECT_GROUP` -- SubsetHasMissingTrickTarget: a subset holding any redirect twin must redirect EVERY
  cast-time-target trick it casts (doublers included); mixed subsets are pruned. LIFE RULE: each redirect pays 2 life
  ({U/P}, no blue); a group whose life sum would leave us at 0 or below is not offered -- the section falls back to "no
  redirect" (never a partial group). Per-section, so a continuation is its own group. Not harder than per-spell twins:
  the twins already exist per trick, so the group is one subset filter.
- E `MTG_MORPH_BERSERK_FIRST` -- an ORDER only: Plan::move_front (PlanMoveOrderFront: casts a Berserk-shape doubler =
  trick_double_power + trick_destroy_at_end_if_attacked, or flips Den Protector naming one) is a MoveOrderLess tier under
  wins_this_turn / pump_waste / atk_forfeit and above value; and FaceUpRegrowOrder orders Den's named variants: Berserk,
  then Legion Leadership vs Might by mana (LL first when >= 2 mana remain after the {1}{G} flip and the largest attacker's
  power > 4, else Might), kicked Vines, Temur Battle Rage (after the creatures if the largest already has double strike),
  Firecat, land. Nothing is removed. Plan::move_front sits in a padding hole (sizeof(Plan) unchanged, 656).
- R11 `MTG_MORPH_WATCHER_IN_PLAN` -- a subset with a redirect twin or an unkicked kicker trick needs the watcher on the
  battlefield or cast in that subset (it ranks 4, before every trick); a Defiance left in hand voids both.
- R12 `MTG_MORPH_TBR_OTHER` -- FindBestOwnAttackerForTrick: a double-strike grant goes on the best-ranked attacker WITHOUT
  double strike at combat and with combat power > 0 (after pumps and the plan's flips). With none it falls back to the
  plain pick, a no-op the search's not-cast sibling dominates -- so in effect no cast. Same rule in the playout / d0
  credit (GreedyTrickStackCredit).
- R13 `MTG_MORPH_TIE_NONMANA` + R14 -- the ranking key is (combat-time size, produces-mana-at-combat, battlefield order):
  ties prefer a non-mana body, then the oldest. "Produces mana" = the ManaDork template on the COMBAT-TIME face
  (CombatTimeDef: the real face when a trail flip is pending, else the permanent as it stands -- a face-down creature has
  no abilities, so no mana). Applied in the cast-time resolver, the redirect original target (exclude_redirector), R12,
  and the playout credit.
- ENGINE FIX found by the D unit test: SubsetPhyrexianDominated did not restore the redirect's {U/P} pip when it rebuilt a
  life-paid variant's full cost, so EVERY life-paid redirect twin read as "payable in mana instead" and was pruned as
  dominated -- in a deck with no blue only a Birds-paid redirect ever survived. Fixed (the pip is restored for
  trick_redirect). This is in every arm below, including alloff.

**Gates.** ./build.sh OK; mtg-test 2883/2883 (11 new INT-8 cases: A, B rank, C inline +14 vs trailing 13, C no GY trail
variant, D group + life, E front + regrow order, R11, R12, R13/R14 with one flipped and one unflipped face-down body; the
INT-6 "kicked + not" Vines expectation is now "kicked only" by R7); scenarios 502/502 -- the four morph_trick_target_* are
unchanged (T3 / T7 / T4 / T4, and identical opponent/active life to the 3f147d491 binary).

**Held-out A/B** (seeds 60000+ / 62000+, morph 1000 + morph2hg 300 at play settings d5 b20, 24 threads). ONE pooled batch
per binary: base = 3f147d491 (`build/scratch/int8/mtg.base`), new = 264360a7c with 10 arms (all on, each lever off alone,
all off) via manifest `flags`. f/s = faster/slower vs base; sum = summed turn delta (unwon = 9); units vs base.

| arm | morph avg | f/s | sum | units | morph2hg avg | f/s | sum | units |
|---|---|---|---|---|---|---|---|---|
| base 3f147d491 | 4.2550 | | | 1 | 4.9033 | | | 1 |
| **all on** | 4.2490 | 7/2 | -6 | 1.039 | 4.9000 | 3/2 | -1 | 0.987 |
| A off (Vines) | 4.2490 | 7/2 | -6 | 1.040 | 4.9000 | 3/2 | -1 | 0.988 |
| B off (R8 order) | 4.2500 | 6/2 | -5 | 1.042 | 4.9067 | 1/2 | +1 | 0.994 |
| C off (inline flip) | 4.2490 | 7/2 | -6 | 1.039 | 4.9000 | 3/2 | -1 | 0.986 |
| D off (group) | 4.2490 | 7/2 | -6 | 1.039 | 4.9000 | 3/2 | -1 | 0.989 |
| E off (Berserk first) | 4.2470 | 7/0 | -8 | 0.959 | 4.8967 | 3/1 | -2 | 0.918 |
| R11 off | 4.2490 | 7/2 | -6 | 1.059 | 4.9000 | 3/2 | -1 | 1.010 |
| R12 off | 4.2490 | 7/2 | -6 | 1.039 | 4.9000 | 3/2 | -1 | 0.987 |
| R13 off | 4.2490 | 7/2 | -6 | 1.038 | 4.9000 | 3/2 | -1 | 0.987 |
| all off | 4.2520 | 2/0 | -3 | 1.001 | 4.9033 | 0/0 | 0 | 1.001 |

Reading: every deck net faster (morph -6, morph2hg -1). "All off" is the base plus the Phyrexian-pip fix (2 morph games
faster, 2HG identical score): the isolation holds. Lever A's games are byte-identical to "on" (an unkicked Vines was
never the chosen line); C, D, R12, R13 each move play in a handful of games at the same score; R11 saves ~2% units; B is
worth 1 game on morph and 2 on 2HG. **E is the only lever that costs**: every slower game of the "on" arm (morph gi245,
gi289; 2HG gi174, gi258) disappears with E off, and E adds ~8% units on morph / ~7% on 2HG.

**d8 b0 recovery** (one pooled batch, new binary): morph gi245 T5 -> T4 (base T4), gi289 T5 -> T4 (base T4), 2HG gi174
T7 -> T5 (base T6), gi258 T7 -> T5 (base T6). All recover. Smoke and regression (below) had 0 slower games.

**Root cause of E's slower games (gi245, single-game repro at play settings, MTG_MORPH_BERSERK_FIRST=1 vs 0).** The games
diverge at T2: with E on, the plan casts Berserk on T2 (two Ignoble Hierarchs + Berserk, on a board of mana dorks:
nothing is lethal and the Berserk doubles a 0-1 power body that then dies to its own end-step destroy); with E off it
plays Gruul Turf and keeps the Berserk, winning T4 instead of T5. Mechanism: the front tier sits ABOVE `value` in
MoveOrderLess, and among horizon-TIED plans the first-verified plan commits -- so "try Berserk first" also acts as a
TIE-BREAK in favour of casting Berserk whenever the leaf cannot tell the lines apart, which on a non-lethal turn is
exactly the waste R9 warns about ("it is rare to cast Berserk without lethal"). The extra units come from the same place:
the fronted Berserk plans are rarely the best, so the B&B cutoff arrives later.
**Proposed amendment (not built; the USER's call):** front a Berserk plan only when it is projected lethal
(`plan.move_front = wins && PlanMoveOrderFront(...)`) -- "If we can find lethal we don't need to search it ... we could
just rank and test it first" -- i.e. Berserk first WITHIN the lethal tier, and no preference otherwise. The Den
regrowth ORDER (FaceUpRegrowOrder) is unaffected by this (it orders value-tied variants of one flip).

**Smoke (full tier, 264360a7c):** only Morph moved -- morph d0 5.0530 -> 5.0470, d3 4.3600 -> 4.3400, d5 4.2400 =
(play-changed), morph2hg d5 4.8000 = (play-changed); searched 0 slower / 1 faster, d0 0 slower / 4 faster.
**Regression (--deck=morph,morph2hg):** morph d0 5.0050 -> 4.9930, d3 4.1733 -> 4.1867, d5 s2002 4.1867 -> 4.1933, d5 s3003
4.4533 -> 4.4200, 2HG 4.6267 = (play-changed); searched 3 slower (d3 gi18 T4->T5, d5 s2002 gi18 T4->T5, d5 s3003 gi6
T7->T8) / 4 faster; d0 0 slower / 7 faster; references clean. **GT:** not accepted on its own -- INT-9 re-ran both
tiers on top of it (see INT-9 result).

**Open USER questions (unanswered; the rules stand as built):**
- E (Berserk first) is the only INT-8 lever that measures worse (2 slower morph games, 2 slower 2HG games at b20, +8%
  units; all recover at d8 b0). Root cause above: the front tier doubles as a tie-break for a non-lethal Berserk.
  Amend to "Berserk first within the projected-lethal plans only"?
- R12 with no eligible second target: the DS grant falls back to the plain pick (a no-op), so the not-cast sibling
  dominates -- in effect "no cast", but the cast is not removed from the menu. OK, or remove it at enumeration?
- R11 for unkicked Vines: the variant is emitted while Defiance is in hand and pruned per subset unless the subset casts
  Defiance. OK?
- C limit: Den Protector's flip inside a breakpoint CONTINUATION plan keeps the trailing position (top-level section
  only). Extend?
- Lever A changed no game in 1300 (an unkicked Vines was never the chosen line): it is effectively a pure prune.
- Previous (now answered by R12/R13): the second-TBR target and the tie-break.

### INT-9 result (2026-11-12) -- INT-8 recovery, the E lethal-only amendment (measured, NOT adopted), the USER's Wooded Foothills prune
Commit `000e53516` (code + unit tests). Scratch: build/scratch/int9/.

**USER ruling (verbatim, 2026-11-12):** "Let's look for other prunes as needed, including for the fetchland Wooded
Foothills. That should be relatively straightforward as goldfish can just always get stomping ground."

**1. Recovery of INT-8's three slower regression games (d8 b0, INT-8 binary 264360a7c, one pooled batch).**
d3 s2002 gi18 T5 -> **T4** (base T4); d5 s2002 gi18 T5 -> **T4** (base T4); d5 s3003 gi6 T8 -> **T7** (base T7; 61 min,
58.5M units). All recover. Lever-by-lever single-game repro at play settings (each INT-8 flag off alone, all off): on
all three games **only `MTG_MORPH_BERSERK_FIRST=0` (and all-off) restores the base turn**; every other lever alone is
byte-identical to "on".

**Root cause of gi18 (the missed T4 triple Berserk) = lever E.** Hand and draws are identical. With E on, T3 casts the
second Birds of Paradise beside Den Protector + Gruul Turf (E off keeps it and plays the Forest on T4). At T3 a Berserk is in hand
and nothing is lethal (opp 20). E's front tier puts every Berserk plan ahead of the value order, so the b10 budget goes
on non-lethal Berserk plans and a different, value-tied plan commits. At T4 the lethal line is Forest, Berserk, flip
Den Protector to regrow Berserk, Berserk, Berserk. It is lethal only through Den's regrowth CONTINUATION, so it is not
in the projected-lethal (`wins_this_turn`) tier, and a second Birds adds Berserk targets that widen the fronted non-lethal tier. The b10
search commits Hystrodon's flip + Gruul Turf and wins T5. Budget confirms it: E on wins T5 at d3 b10/b20 and T4 at
b40/b100 (at b40 the T3 choice already reverts). This is the INT-8 gi245 mechanism (the front tier acting as a
tie-break / budget sink on a non-lethal turn). gi6 is the same: with E on, T3 casts a non-lethal Berserk (opp 20 -> 14),
and the T6 double Berserk is then one short. **E-lethal-only fixes gi18** (T4 at d3 b10).

**2. The E amendment arm (`MTG_MORPH_BERSERK_FIRST_LETHAL_ONLY`, DEFAULT OFF -- E stays as ruled).**
`PlanMoveOrderFront` now receives the plan's `wins_this_turn` projection; with the arm on, the Berserk front tier applies
only to plans projected lethal ("If we can find lethal we don't need to search it, I guess it's true we could just rank
and test it first"). FaceUpRegrowOrder (Den's regrowth order) is unchanged in every arm.

**3. The Wooded Foothills prune (`MTG_MORPH_FETCH_RULE`, DEFAULT ON).** MorphProvider: `ConsiderFetchHold` false (the
fetch resolves on play); `FetchCandidates` puts the pick producing the most colours first (Stomping Ground; read from
`params.produces`), else the generic library order; `FetchSearchCandidates` stays the one-option default. Human play,
MTG_UNPRUNED (UnprunedGate::Fetch) and life <= 3 keep the hold and the full fan; that is engine-side and unchanged
(FetchResolvesOnPlay, the TurnSolver fetch fan).
Hold review (verify_deck's fetch_hold_review disclosure). Checked against the list and cards.json: no landfall, no
fetch, sacrifice or graveyard-count payoff, and no library manipulation. Den Protector's regrowth can only gain from a cracked
fetch in the graveyard. The goldfish value of the hold is therefore none, apart from one shape the A/B surfaced (gi496,
below): a HELD Foothills puts a land on the battlefield WITHOUT shuffling. The search reads the real library, so this
lets it keep a known good deal, and Gruul Turf can bounce the held fetch back to hand. That is deal-selection
(clairvoyance), not goldfish quality: a shuffle does not change the expected draws.
Life: fetch 1 + shock 2. The shock's pay-life rule (life > 2) and the INT-8 redirect-group life rule (never a group that
would leave us at 0 or below) both read the current life, so neither can be overdrawn. In practice the most one game pays is a
few points. A redirect costs 2 per pip, and at 17 life after one fetch+shock a group still needs 8 redirects to hit 0.

**Held-out A/B** (seeds 60000+ morph 1000 / 62000+ morph2hg 300, play settings d5 b20, ONE pooled batch, 23 threads;
base = INT-8's 3f147d491 arm, reused). f/s and sum vs base (unwon = 9); units vs base. "vs on" = vs the all-on arm.

| arm | morph avg | f/s | sum | units | vs on f/s, units | morph2hg avg | f/s | sum | units | vs on f/s, units |
|---|---|---|---|---|---|---|---|---|---|---|
| base 3f147d491 | 4.2550 | | | 1 | | 4.9033 | | | 1 | |
| **all on** (E as ruled + fetch rule) | 4.2260 | 35/7 | -29 | 0.838 | -- | 4.8567 | 16/3 | -14 | 0.852 | -- |
| E-lethal-only | 4.2240 | 35/5 | -31 | 0.794 | 2/0, 0.947 | 4.8600 | 16/4 | -13 | 0.796 | 0/1, 0.934 |
| E off | 4.2240 | 35/5 | -31 | 0.792 | 2/0, 0.946 | 4.8600 | 16/4 | -13 | 0.801 | 1/2, 0.940 |
| fetch off (= INT-8 "on", digest-identical) | 4.2490 | 7/2 | -6 | 1.039 | 5/28, 1.240 | 4.9000 | 3/2 | -1 | 0.987 | 3/14, 1.158 |

Reading:
- The **fetch rule** is the big win: morph -23 games-turns (28 faster / 5 slower vs fetch-off), 2HG -13 (14/3), and
  **0.81x / 0.86x units**. The fetch-off arm reproduces INT-8's "on" digests exactly (11eb55a7 / e0afd0af), so the
  isolation holds and the PlanMoveOrderFront signature change is neutral.
- **E-lethal-only and E off score the same**: morph -31 vs ruled -29, 2HG -13 vs -14, so -44 vs -43 combined. E-lethal-only
  costs about 5-7% fewer units than the ruled E. It removes the gi245 / gi289 slower games (as E off does) and gi18. Its
  one 2HG game slower than ruled (gi72 T5 -> T6) recovers at d8 b0 (T5 = base). The lethal-only front tier almost never
  fires: it changes 126 games vs on, E off changes 130. A Berserk lethal is already in the wins tier, or it is lethal only
  through a regrowth continuation that the projection does not see.

**d8 b0 recovery of the A/B's slower-vs-base games** (one pooled batch, new binary, all-on unless noted):
morph gi245 T4, gi289 T4, gi951 T4 (base 4) -- recover; 2HG gi25 T5, gi222 T5 (base 5), gi72 (lethal-only) T5,
gi185 (E off) T5 -- recover. **NOT recovered: morph gi186 T5, gi193 T5, gi532 T5 (base 4), gi496 T6 (base 5); 2HG gi146
T6 (base 5).** Every one is a fetch-rule game; with the rule off each is at its base turn at b20.
- gi186, gi193, gi532: **draws diverge** right after the fetch, because the shuffle runs on a different library (Stomping
  Ground fetched vs a basic, or a fetch cracked on a different turn). These are different physical games: fetch-shuffle variance, not a lost line.
- gi496: draws are identical through T5, a genuine line difference. Fetch off: T1 Foothills HELD (no mana), T2 Gruul
  Turf bounces it, T4 Foothills cracked, win T5. Rule on: the search plays NO land on T1 or T2 rather than crack the fetch
  (a crack shuffles away the known good draws), T3 Legion Stronghold, T4 Foothills, win T6 -- also T6 at d8 b0.
  The lost option is "a land on the battlefield without a shuffle". Its only value to a deal-reading search is keeping the
  known draws (see the hold review above).
- 2HG gi146: not inspected for draws (the 2HG CLI repro flags differ); same shape expected.

**Smoke (FULL tier, 000e53516; THREADS=23):** only Morph moved (4 configs): morph d0 5.0530 -> 5.0080, d3 4.3600 -> 4.3400,
d5 4.2400 = (play-changed), morph2hg d5 4.8000 -> 4.7200. Searched slower 0 / faster 3; d0 slower 7 / faster 45.
**Regression (FULL tier):** only Morph moved (7 configs): morph d0 5.0050 -> 4.9590, d3 4.1733 -> 4.1467, d5 s2002
4.1867 -> 4.1800, d5 s3003 4.4533 -> 4.4133, morph2hg 4.6267 -> 4.6000, and both 30001 unlimited backstops 4.0000 =
(play-changed). Searched slower 4 / faster 13; d0 slower 10 / faster 48. Slower: d3 + d5 s2002 gi18 T4->T5 and d5 s3003 gi6
T7->T8 (the E games above, line changes with identical draws); 2HG gi2 T4->T5 (draws diverge at T3 -- fetch variance).
References: 336 ok / 490 repaired / 1 beaten / 0 play-drift / 0 shuffle-dead / 0 board-diverged / 0 enum-gap /
0 mull-drift / 0 contract-fail (827); REQUIRE-BEAT 2/2. Every deck's tier aggregate is net faster.
**d8 b0 on the regression slowers (new binary 000e53516, one pooled batch):** gi18 T4 (base 4), gi6 T7 (base 7; 29 min,
58.5M units, digest identical to the INT-8 binary's) -- recover. E-lethal-only at play settings also restores both (gi18
T4 at d3 b10 and d5 b20, gi6 T7). **2HG gi2 T5 (base T4) does NOT recover**: draws diverge after the fetch shuffle.
The fetch-OFF twins of every unrecovered A/B game reach their base turn at d8 b0 (gi186 4, gi193 4, gi532 4, gi496 5,
2HG gi146 5). The gap is the rule's different deal (or, for gi496, the held fetch), not search truncation.

**GT: ACCEPTED by the parent in bd9a665b8 (see the update below).** INT-9 left it unaccepted because the bar ("every slower game must recover at d8 b0") is not met: six fetch-rule games do not
recover. Five are fetch-shuffle variance (draws diverge) and one (gi496) is the no-shuffle hold. Everything else is
clean: only Morph moved, every aggregate is net faster, and references are clean. The smoke and regression results envs from these
runs (test/logs/smoke, test/logs/regression in the worktree) are intact. If the USER / parent accepts the variance reading:
`bash test/regression.sh --smoke --accept --deck=morph,morph2hg`, `bash test/regression.sh --accept --deck=morph,morph2hg`,
`python3 test/check_gt_logs.py`, then diff test/regression_gt.txt (morph / morph2hg keys only).

**Open USER questions (INT-9; the rules stand as built):**
- E: adopt the lethal-only amendment? Recommended. It scores the same as E off (-44 vs ruled -43 games-turns over 1300
  held-out games), costs 5-7% fewer units, and fixes all five E-caused slower games (A/B gi245/289, regression gi18 x2, gi6). The USER's "rank and
  test it first" survives for projected-lethal plans. The honest caveat: it almost never fires, so in effect it is E off.
- Fetch rule: accept the six unrecovered games as fetch variance + the no-shuffle hold (gi496)? The rule is -36
  games-turns and ~0.83x units on the held-out set.

### INT-10 result (2026-11-12) -- the branching census ("what decisions are slow?"), two "combat is over" folds, the Den LEAD pin fold
Commit `bb6829c35` (code + unit tests). Scratch: build/scratch/int10/ (b0/, cen_b0/, cen_d5b20/, tag*/, ab/, srec/). Instrument added (print-only, default off): a
per-action TAG table in MTG_BRANCH_STATS (TurnSolver branchstats::g_by_tag -- kind + face-down mode + flip position +
regrowth pin + kicker + redirect, plus copy-twin / two-flip / main-2 probes), so the census can say WHICH options the
returned plans carry.

**USER rulings (verbatim, 2026-11-12):** "Let's look for other prunes as needed, including for the fetchland Wooded
Foothills. That should be relatively straightforward as goldfish can just always get stomping ground." and "Okay, what
decisions are slow? 'Never pump a creature that can't attack this turn.' That makes sense."

**1. Budget-0 screen (HEAD 3bd89f38f; seeds 30001+, 80 games each, abandon_units 2e6, MTG_SLOW_GAME_MS=5000).**
d3: 77 played, 3 capped (gi34, gi46, gi58); d5: 76 played, 4 capped (gi34, gi46, gi55, gi58); gi25 ends just under the
cap at both (1.87M / 1.88M units). 7/160 capped (was 9/80 at suite entry). 13 distinct games over 5 s; every slow game
is slow at BOTH depths, so the cost is the board, not the depth.

**2. Census of the worst five b0 games (d5, single-threaded, MTG_BRANCH_STATS / MTG_BRANCH_SHAPE / MTG_CAND_CENSUS).**
Two shapes:
- LONG STALLED games (gi34, gi58): no win by T8; every enumeration is narrow (avg odometer 2-4: "flat: 1-3 optional
  casts"), 625-690k EnumeratePlans calls, units la_cand 35-38% / rollout_step 21-25% / greedy_fallback 19-23%. Nothing
  unreasonable branches: an unlimited d5 search re-solved over eight turns of a hand that does not win.
- MORPH-CARD games (gi25, gi46, gi55): the biggest option groups are Den Protector / Blistering Firecat -- size 3 per
  morph card in hand (hard / face-down / face-down-and-up) and Den's flips x regrowth names; on the hot turn avg odometer
  27-46, max 1024-1280, but the subset rules cut it ~10x before plans are built (plans per call 1.4-2.2) and
  la_cand is 40-70% of units. MTG_CAND_CENSUS: candidates are distinct states (collapse 1.00-1.05x).

**3. The play-setting census (d5 b20, 192 games, seeds 920000+, MTG_TURN_CENSUS single-threaded x24 + MTG_BRANCH_STATS).**
1420 decisions; the slowest 10% hold 40% of units (median 2,925 units, max 79,058 = 4.4x budget; none past 5x). Units by
site: la_cand 51%, rollout_step 14%, fs_main2 14%, greedy_fallback 12%, fs_pre 6%. **77% of all units are in mulligan
bottom-TRIAL games** (census probe=1): with no keep table yet, every mulligan plays each candidate bottom out as a full
game at play settings. Real play is 23%, concentrated on T1 (13.4%) and T2 (6.9%) -- whole-line searches through the d5
horizon (top real decision: T2, 76k units, 48k candidates scored). 2.48M EnumeratePlans calls, odometer 231M, 3.56M
plans returned (the subset rules cut ~65x before a plan is built).

**4. Which options the returned plans carry (the TAG table; d5 b20, 3.56M plans; b0 gi55 1.24M plans).**

| decision point | width / count | share of returned plans | reasonable? |
|---|---|---|---|
| main 2 (any plan) | 1.83M plans, 2.5 per call | 51% | yes (Hystrodon draws; G4 main 2 ON) |
| **main 2 casting a solo-target trick** (Might, Vines, Legion Leadership, TBR; Berserk is illegal there) | 419k plans | **11.7%** (gi55 b0: 11.6%) | **NO -- no creature can attack after combat: pruned (a)** |
| **main 2 face-up Blistering Firecat** (hard cast, face-down-and-up, or flip) | 101k plans | **2.8%** | **NO -- sacrificed at this end step without attacking: pruned (b)** |
| Den Protector LEAD flip, UNPINNED base beside its named variants | 83k plans | 2.3% | duplicate (the pin resolves to one named variant): folded (c) |
| morph card in hand: hard / face-down / face-down-and-up | group of 3 per card; b0 hot-turn odometer 27-46 avg | -- | yes (real choices: colour, a body now, a flip later) |
| Den regrowth names x LEAD / TRAIL position | 2-9 per Den | ~10% (named + pin) | yes (searched per the USER's rulings) |
| two hand copies cast with DIFFERENT variants (the copy twin the hand-cast fold leaves unfolded) | 4k plans | 0.1% | negligible -- no fold |
| two flips of one face-down card name | 3k plans | 0.1% | negligible |
| opponent-end-step flip fan | 236k crossings / 192 games | -- | yes (searched axis, MTG_FACEUP_AXIS_STATS searched-frame 0) |

**5. As built (all three DEFAULT ON; (a) and (b) are sound dominance folds in the spirit of the USER's "Never pump a creature that can't
attack this turn."; each an EnvOn + HeuristicArm slot, off under human play and MTG_UNPRUNED / tricktarget).**
- (a) `MTG_MORPH_NO_POSTCOMBAT_TRICK` -- MorphProvider::TrickTargetCandidates: once this turn's combat is over
  (StealCombatOver: post-combat main or end step; no additional combat queued) a solo-target trick gets the "no
  recipient" sentinel, so no variant. Proof: every trick payload here (+N/+N, power doubling, double strike, first
  strike, Wild Defiance's +3/+3) is until-end-of-turn combat value nothing in the list reads after combat (goldfish: no
  blocks, no damage to our creatures; Berserk is already illegal after combat damage); casting only spends mana and
  moves the card hand -> graveyard, which Den Protector's regrowth can at best undo. Before this, the provider's
  recipient test (TrickRecipientCanAttackThisTurn -> CanAttackFull) had no phase term, so an UNTAPPED non-attacker (a
  dork held back for exalted, Spellskite) counted as "can attack" in main 2.
- (b) `MTG_MORPH_NO_POSTCOMBAT_SACUP` -- new hook DecisionProvider::EndStepSacFaceUpDeadAfterCombat (default false),
  applied by a post-pass at the end of TurnSolver CollectActions (before the fold tags) for a face-down-capable deck:
  after combat, a FACE-UP entry of an end-step-sacrifice creature -- the hard cast, the face-down-and-up cast, or
  turning a face-down copy face up -- is dropped. Blistering Firecat then cannot attack, is sacrificed at this end
  step, and lands in the graveyard; not doing it keeps the card in hand (or the face-down 2/2, still flippable at the
  opponent's end step or next turn) and the mana. The face-down cast (face_down 1) stays, and the opponent-end-step flip
  window is a separate axis (untouched).
- Unit tests: "morph INT-10: no solo-target trick is offered after combat" (pre-combat: offered; post-combat: none;
  lever off: offered; a deck with an extra combat: offered), "morph INT-10: no face-up Blistering Firecat entry after
  combat (the face-down cast stays)" (hard / FD+up / flip present pre-combat and with the lever off, gone post-combat,
  the FD cast kept).
- (c) `MTG_FACEUP_LEAD_PIN_FOLD` -- ENGINE identity fold in EmitFaceUpActions (morph decks only: it needs a
  turned_face_up_return_gy_to_hand card): a LEAD flip emits no UNPINNED base once its named regrowth variants are
  emitted. The LEAD runs before every cast, so its graveyard is the pre-plan graveyard (less what an earlier LEAD
  regrew), and every distinct name there has a named variant; the base resolves to the provider's pick among exactly
  those names, and a stale named pin falls back to the same pick -- so the base is a duplicate. The TRAIL base stays (a
  different position). Unit test "morph INT-10: Den Protector's unpinned LEAD flip is folded into its named variants".

**Proposals (NOT built; judgement calls or not worth a build tonight).**
- P2. Den Protector's named TRAIL variants for a hand instant the section can no longer cast (after combat every trick
  is now unoffered, so a TRAIL naming one is a stale pin = a duplicate of the TRAIL base). Small; an identity fold.
- P3. The two-copy twin (two hand copies of one morph card cast with different variants: the hand-cast fold's
  CONDITION 2 leaves multi-variant slots unfolded) is 0.1% of plans -- not worth an engine change.
- P4. Blistering Firecat hard cast vs face-down-and-up in main 1 when both are payable: the same permanent for one more
  mana, but NOT a sound fold (the hard cast needs a third red the combined bill does not, so the leftover pools are
  incomparable). Judgement -- would need a USER ruling and an A/B.
- P5. 77% of the play-setting units are mulligan bottom TRIALS. Not a branching problem: the keep table the mulligan
  generation is about to produce removes them from play (and from the value-leaf stage after it).
- Still open from INT-8/9: E lethal-only (recommended), R12's no-second-target TBR at enumeration, R11 for unkicked
  Vines, C inside continuations, the fetch-rule variance games.

**6. Held-out A/B -- (a) + (b)** (seeds 70000+ morph 1000 / 72000+ morph2hg 300, play settings d5 b20, ONE pooled batch,
23 threads, per-job flags; base = HEAD 3bd89f38f `build/scratch/int10/mtg.head`, same seeds, its own batch). f/s and sum
vs base (unwon = 9); units vs base.

| arm | morph avg | f/s | sum | units | morph2hg avg | f/s | sum | units |
|---|---|---|---|---|---|---|---|---|
| base 3bd89f38f | 4.2620 | | | 1 | 4.8333 | | | 1 |
| **on** (a + b) | 4.2570 | 6/2 | -5 | **0.872** | 4.8233 | 3/0 | -3 | **0.834** |
| (a) off | 4.2620 | 0/0 | 0 | 0.975 | 4.8300 | 1/0 | -1 | 0.966 |
| (b) off | 4.2570 | 6/2 | -5 | 0.893 | 4.8233 | 3/0 | -3 | 0.870 |
| both off | 4.2620 | 0/0 | 0 | 1.000 | 4.8333 | 0/0 | 0 | 1.000 |

Both-off reproduces the base digests exactly (ff01c5d9 / c7855700): the isolation holds. (a) carries the score and ~10% of
the units; (b) is score-neutral (play changes in 2 + 3 games) and worth 2-4% of the units. Slower games of "on":
morph gi61 (T2: a NON-LETHAL Berserk is cast -- the INT-8/9 lever-E tie-break mechanism, reached through a different
budget allocation; draws identical through T1) and gi445 (T1 land choice differs -- Stomping Ground vs a cracked Wooded
Foothills -- so the draws diverge after the fetch shuffle: fetch variance).

**7. Final binary (a + b + c, commit `bb6829c35`) on the same A/B.** Play-IDENTICAL to (a + b) on both decks (digests
844b371a / e248fae9), units -0.05% (morph) / -0.09% (2HG): (c) only removes duplicate candidates. Its "c off" arm
reproduces the (a + b) arm exactly (digest and units). So the final A/B vs base is the (a + b) row above.

**d8 b0 recovery (final binary, one pooled batch):** morph gi61 T4 (base T4), gi445 T4 (base T4) -- both recover.

**Budget-0 re-screen with the final binary** (same block: seeds 30001+, 80 games, cap 2e6): d3 **80/80 played, 0 capped**
(was 3: gi34, gi46, gi58 now finish), avg 4.1250; d5 77 played, **3 capped** (gi34, gi55, gi58; gi46 now finishes),
avg 4.1039. 3/160 capped, was 7/160. The remaining caps are the shapes of section 2: two stalled no-win hands (gi34,
gi58) and one long Den Protector turn (gi55) -- neither is a branching defect left in the census.

**Smoke (FULL tier, `bb6829c35`, THREADS=21):** only Morph moved (4 configs): morph d0 5.0080 = (play-changed 1),
d3 4.3400 = (play-changed), d5 4.2400 = (play-changed), morph2hg d5 4.7200 -> 4.6800. Searched 1 slower / 2 faster /
10 play-changed at the same score; d0 0 slower / 0 faster / 1 play-changed. The slower game, morph2hg gi5 T6 -> T7, has
identical hand and draws (a line change at T2-T3: an early non-lethal Temur Battle Rage); at d8 b0 it is T6 (base T6) --
recovers (repro at b20 reproduces T7). Scenarios 502/502; mtg-test 2887/2887.

**Regression (FULL tier, `bb6829c35`, THREADS=22):** only Morph moved (5 configs): morph d0 4.9590 -> 4.9580, d3
4.1467 = , d5 s2002 4.1800 = , d5 s3003 4.4133 = , morph2hg 4.6000 = (all play-changed); both 30001 unbudgeted backstop
rows unchanged. Searched 0 slower / 0 faster / 15 play-changed; d0 0 slower / 1 faster / 2 play-changed. References:
336 ok / 490 repaired / 1 beaten / 0 play-drift / 0 shuffle-dead / 0 board-diverged / 0 enum-gap / 0 mull-drift /
0 contract-fail (827); REQUIRE-BEAT 2/2. Every deck's aggregate is net <= 0 (only morph decks moved, each faster or equal).

**GT: ACCEPTED** (the bar is met: every slower game -- A/B gi61, gi445, smoke 2HG gi5 -- recovers at d8 b0, and every
deck is net <= 0): `regression.sh --smoke --accept-with-regressions=... --deck=morph,morph2hg`, then the regression tier
the same way; `check_gt_logs.py` 1131 consistent / 0 stale; the regression_gt.txt diff is morph / morph2hg keys only.
OVERNIGHT not re-run (USER: occasional, not per change).

**For the USER (decisions stated, rules stand as built):**
- (a) and (b) are presented as SOUND dominance folds (combat is over -> no attack -> the payload is dead), not
  judgement prunes, so they are default ON; (a) is the direct extension of "Never pump a creature that can't attack
  this turn." to main 2. Say if you want either off.
- (c) is an engine identity fold, play-identical on the held-out set.
- Open from INT-8/9 still: E lethal-only, R12, R11, C in continuations, the fetch-rule variance games.

## Claude-play sweep
- commit: `f2df0a83f`
- seeds: 50000 games: 18
- driver: Claude agents (Sonnet players, one per game index 0..17 at seed 50000+gi; every flag adjudicated by an Opus
  verifier). Benchmark = the search at play settings (d5 b20).
- flags: 0 unresolved
  - RESOLVED in `01be1014d` (INT-4) -- REAL ENGINE BUG (g10, T2): a spell//land MDFC played as its land back face
    (Legion Stronghold) and bounced by Gruul Turf returned to hand as a land-only object -- the Legion Leadership
    front was lost (CR 712.8a: off the battlefield/stack a DFC has only its front face). Root cause: the
    spell//land back-face synthesis in CardDatabase.cpp replaced the copied params with a clean `bp` and dropped
    `mdfc_front_name` (the Pathway path kept it). Fix: `bp.mdfc_front_name` is set, and `RevealedCard`
    (SpellEffects.h) -- the wrapper every battlefield -> hand / graveyard / exile / library push already goes
    through, executor and rollout -- now maps any played back face to its front, so the fix is not bounce-only.
    Pinned by the unit test `mdfc: a played BACK face leaves the battlefield as its FRONT` and the scenario
    `morph_mdfc_bounced_stronghold_returns_leadership` (turn-4 kill exists only if the bounce returns Leadership).
  - dismissed (g17, T5): a self-defeating 4-spell plan payable only by tapping the lone attacker Birds -- a legal
    optimistic proposal the player chose; the shipped search does not pick it.
- win turns: Claude = search on 12 games; Claude faster on 4 (g0 4 vs 5, g3 3 vs 4, g5 7 vs 8, g15 4 vs 5) -> 5c
  budget-starvation check; Claude slower on 2 (g11, g12).

**INT-9 GT update (parent, 2026-11-12).** Smoke + regression accepted for morph/morph2hg in `bd9a665b8`, with
an `accepted-with-regressions` note. The six unrecovered fetch-rule games were classified under the USER's
prune-adoption bar (better overall, faster, every lost line recognizable): A/B morph gi186/193/532 and
regression 2HG gi2 diverge in library order after the crack; 2HG gi146 (checked by the parent: identical T1-T2,
T3 lands Stomping Ground from hand so Foothills cracks a turn later, and the T4 draw differs: Forest vs Den
Protector) is the same; gi496 is the search holding an uncracked fetch so it does not shuffle away a library top
it can see, i.e. clairvoyance. Berserk-first stays AS RULED pending the USER's answer on the lethal-only amendment.
