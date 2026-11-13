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

### INT-11 result (2026-11-12) -- rebased onto origin/phase-1-2-deck-analyzer 9ba5851a1; GT re-established
The 27 morph-analysis commits are replayed onto `9ba5851a1`, which adds 45 origin commits: payer and mana-oracle item-6
fixes, `MTG_PAY_DOOMED_BODY_FIRST`, and the 2026-11-12 overnight accept `fec16d7d2`. The tip before the rebase is
`d7cfab7bb`. `range-diff` shows `=` for every commit except five, and those five changed only in conflict resolution:
- INT-2: origin's sac-draw re-find, with our `RevealedCard` wrap kept.
- INT-8: the HeuristicArm slot lists, as a union (origin's slots first, then ours).
- The three GT commits: origin's non-morph keys and gt_logs, with our morph keys and notes kept.
`suite_cost.json` keeps origin's pinned hinata entry. The morph entry is rescaled onto it with a note: 1.722x hinata, as
measured.

Gates on the rebased tree: mtg-test 2905/2905, scenarios 502/502, coverage-only 0 missing.

**Smoke (FULL, THREADS=24):** 196 pass. The 3 that moved are mirrorwing d3/d5 and mirrorwing2hg 4.68 -> 4.64. Searched:
0 slower, 2 faster. d0 is unchanged.

**Regression (FULL):** 312 pass. The 4 that moved are mirrorwing d3/d5 x2 and extraturns_regression_d5_s3003 (gi36: same
score, line tie). Searched: 0 slower, 2 faster, 24 play-changed. d0 is unchanged. Viewer protocol: 0 play-drift, and the
REQUIRE-BEAT check passes 2/2.

**Every morph/morph2hg key PASSES unchanged.** Origin's payer and oracle changes do not move Morph.

**Causation.** An origin-only build (scratch worktree `int11-origin`) reproduces origin's GT on every moved key, so all of
the moves are ours: the INT-5 `#TT` dedup fix for mirrorwing, and INT-2c `SectionForgetHandExits` for extraturns (bisected
in INT-7). The moved digests are identical to the pre-rebase GT. The bar is met trivially because no game is slower.
Accepted with `--deck=mirrorwing,mirrorwing2hg` (smoke) and `--deck=mirrorwing,extraturns` (regression).
`check_gt_logs.py` reports 1146 consistent, 0 stale. Against origin, `regression_gt.txt` differs only in the morph keys,
these 7 mirrorwing/extraturns keys, and the notes. The OVERNIGHT tier was not run.

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

## Mulligan profile + viewer morph support (2026-11-13)

- **Keep table ADOPTED** (27eb15869): `mullgen.sh run decks/Morph fast`, K=18 (bucket policy
  `Morph.buckets.json`: Wooded Foothills + Stomping Ground merged, Birds / Ignoble kept apart, USER
  2026-11-12), Phase F setting d1 b3. Validation PASSED 2026-11-13T03:08Z: keep (exhaustive vs static)
  -0.2488t (4/4 seeds, t -28.6), bottoming (blind exhaustive vs lookahead, confounded) -0.0805t (4/4, t -12.9).
- **Viewer morph support** (af2296167): face-down cast badge ({3}), face-down-and-flip badge where the
  engine offers it, Den Protector trailing flip badge, click a face-down creature to queue "turn face up";
  LineSpec `facedown=` / `facedownup=` (enforced like `kicked=`: absent = hard cast); CheckLine prices a
  declared face-down cast at {3}. Gaps: CheckLine does not charge the face-up cost (pre-existing); a
  hand-badge flip matches by name when a face-up and face-down copy coexist; no in-combat flip window.
- **GT accept (smoke + regression) on af2296167**: ONLY morph / morph2hg moved (every other deck
  byte-identical => the viewer change is inert for AI play). Smoke: d0 5.008->4.738, d3 4.34->4.02,
  d5 4.24->4.09, 2HG d5 4.68->4.52. Regression: d0 4.958->4.773, d3 4.1467->4.08, d5 s2002 4.18->4.0933,
  d5 s3003 4.4133->4.1867, 2HG d5 4.60->4.72 (slower), budget-0 backstops d3/d5 4.00->4.33 (3 games;
  smoke rows, not quality). Expected for a mulligan adoption (different kept hands); the keep table's
  own held-out validation is the quality verdict.
- **Value leaf: ON HOLD** (USER 2026-11-13: "let's hold on the value-leaf until I have had a chance to
  make some references. We'll finish the mulligan profile, however.").

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-11-13)

`verify_deck.py decks/Morph/Morph.cod --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 19 cards full (missing=0, partial=0) |
| card_costs | PASS | yes | all mana costs match Scryfall (cost/cmc only) |
| card_fields | PASS | yes | 766 cards match snapshot (cost/PT/types/keywords); 17 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | PASS | yes | in all 3 suite tiers with GT (morph) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 5 type(s) wired (emitter + GUI): bounce, dig, face_up, land_entry, tutor_etb |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 8001] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/184 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |
| suite | PASS | yes | suite gate: Morph is regression key `morph` |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Blistering Firecat: MORPH (CR 702.37): morph_cost -- a face-down cast variant for {3} (a colourless nameless 2/2 with no abilities: the DB-synthesized "Blistering Firecat (Face-down)" face) beside the hard cast; the search decides. Turning face up is a special action (CR 116.2b) offered in the main phase BEFORE the plan's casts (a sound dominance fold: power-locking pumps such as Berserk / Legion Leadership want the 7 base first) (or, cast from hand, at its own face-down cast's resolution). At the OPPONENT'S end step the flip is a searched turn-boundary axis (BoundaryFan; the viewer's `face_up` prompt): a Firecat turned up there, after that end step began, has missed its sacrifice trigger (CR 603.2) and survives into our turn, paid from the mana left untapped. Same permanent after the flip (CR 708.8): pumps, counters and summoning-sick status persist; a Firecat that entered face down last turn attacks at once, and one cast face down and turned up this turn has haste. endstep_sacrifice_self: at the beginning of each end step, ONLY while face up (a face-down Firecat has no abilities). Trample is inert vs the never-blocking goldfish opponent. PROVISIONAL (USER sign-off owed): no flip during combat or in an upkeep / our own end step -- dominated by the main-phase and opponent-end-step windows.
- coverage deferral -- Hystrodon: MORPH: morph_cost, as Blistering Firecat. At the OPPONENT'S end step a payable face-down Hystrodon is always flipped when the rest of the flip set leaves the mana (a SOUND dominance fold, EotFaceUpDominates: face up it is a 3/4 with no drawback, the 2/2's every capability, and that mana has no other use there). combat_damage_self_may_draw 1: one draw per combat-damage event to the opponent (two with double strike), through the shared Curiosity/Lookout combat-damage draw hook; a face-down Hystrodon has no ability and does not draw. 'You may' is folded to 'draw iff the library is non-empty' (PROVISIONAL dominance fold, the Curiosity precedent; USER sign-off owed). Turns the deck's searched second main on (DeckUsesSecondMain): a card drawn in combat can be cast after it. Trample is inert vs the never-blocking goldfish opponent.
- coverage deferral -- Vines of Vastwood: solo_target_trick (searched target; viewer board click) + KICKER: both modes are cast variants sharing one hand slot, one PER TARGET, and the SEARCH picks -- unkicked {G} (its only goldfish effect is the TARGETING, which triggers Wild Defiance) vs the DB-synthesized 'Vines of Vastwood (Kicked)' face at {G}{G} (an additional cost, CR 601.2b) carrying kicked_power_bonus/_tough 4 as its +4/+4. PARTIAL/PROVISIONAL: 'can't be the target of spells or abilities your opponents control' NOT modelled -- inert, the passive opponent never targets anything.
- coverage deferral -- Den Protector: MEGAMORPH (CR 702.37b): megamorph_cost -- face-down cast for {3} as Blistering Firecat; turned face up for {1}{G}, putting a +1/+1 counter on it as it turns (PutPlusCounters, doubler-aware). Its flip is offered both BEFORE and AFTER the plan's casts (the graveyard at the flip decides what returns). Also flippable at the OPPONENT'S end step (a searched turn-boundary axis, each regrowth target searched): there it can return a Firecat sacrificed at our end step. turned_face_up_return_gy_to_hand: WHICH card is SEARCHED -- one variant per distinct graveyard name (plus instants/sorceries the same plan casts first), surfaced to a human as the dig chooser at resolution (the Timeless Witness path); an empty graveyard removes the trigger (CR 603.3d). PROVISIONAL deferral (USER sign-off owed): 'Creatures with power less than this creature's power can't block it' is not modelled -- an evasion clause, inert vs the goldfish opponent, which controls no blockers and never blocks.
- coverage deferral -- Temur Battle Rage: solo_target_trick (searched target; viewer board click). grants_temp_double_strike -> Permanent::temp_double_strike, read only by CreatureHasDoubleStrike, so the CR 510.4 two-step split in Combat.cpp (executor and rollout) and the search's attack projection all see it; every pump and the exalted bonus are dealt in both steps. Cleared at cleanup. PARTIAL/PROVISIONAL: the FEROCIOUS trample grant is NOT modelled -- inert, the passive opponent never blocks (no blocker path; Keyword::Trample has no reader), so the condition is not evaluated either. Instant speed collapsed to the main phase: inert for this card (double strike multiplies whatever power exists at damage).
- coverage deferral -- Berserk: solo_target_trick: the target is a SEARCHED plan variant (viewer: board-click target). trick_double_power: +X/+0 with X = the target's combat power AS THE SPELL RESOLVES (CombatPowerOf: printed + temp + counters + lords + Auras + Equipment), read AFTER any Wild Defiance trigger (which resolves first) and locked there -- pumps resolving later are not doubled. trick_destroy_at_end_if_attacked: a delayed trigger (Permanent::destroy_at_end_if_attacked) swept at the beginning of the end step in both worlds -- DESTROYED (not sacrificed: indestructible survives, no sacrifice watchers) iff the creature was declared as an attacker this turn; a body that already left is not found (CR 603.7c). cast_only_before_combat_damage: never offered in a post-combat main. PARTIAL/PROVISIONAL: 'gains trample' NOT modelled -- inert, the passive opponent never blocks (no blocker path; Keyword::Trample has no reader). PARTIAL/PROVISIONAL: instant speed is collapsed to the main phase; NOT fully inert -- cast after a lone attacker's exalted trigger resolves, X would include the exalted bonus (see the Morph ledger, D7).
- coverage deferral -- Wild Defiance: spell_target_watch_pump_power/_tough 3: a WATCHER read for every OWN-creature recipient of an instant/sorcery solo-target trick inside the ONE shared payload applier (ApplyTrickPayload: executor EffectHandler + rollout apply_one, lockstep), applied BEFORE that spell's own payload (the trigger goes on the stack above the spell and resolves first, CR 603.3) -- so a later doubler (Berserk, Legion Leadership) doubles the +3/+3. One +3/+3 per Wild Defiance we control per targeting (each copy is its own trigger); a spell COPY that targets the creature counts too, and a Spellskite redirect is a SECOND 'becomes the target' event (the redirected-to creature triggers again). Mandatory, untargeted, no choice -> no viewer decision. PARTIAL/PROVISIONAL: (1) spells cast by the OPPONENT are collapsed away -- inert, the passive opponent never casts; (2) only solo-target tricks fire it -- a pump_spell-template instant (Invigorate) does not; no such card shares a list with Wild Defiance.
- coverage deferral -- Gruul Turf: Karoo bounce land: enters tapped, makes 2 mana ({R}{G}: ONE OF EACH colour per tap, never two of either -- ManaPayment's Karoo bundle), and on ETB returns one of your lands to hand (BounceKarooLand prefers a tapped land; the replay cost is the real tempo cost). The Izzet Boilerworks pattern.
- coverage deferral -- Might of Old Krosa: solo_target_trick (searched target; viewer board click). power_bonus/tough_bonus 2 = the base; mainphase_power_bonus/_tough 4 REPLACE it ('instead') when the spell resolves in a main phase (GameState::phase -- the cast and the resolution share the phase in this engine). Every cast this engine can make is a main-phase cast, so the +2/+2 branch is currently unreachable; it is modelled so a future combat-time cast window prices it correctly. Pumped after a Wild Defiance trigger; a later doubler (Berserk, Legion Leadership) doubles it.
- coverage deferral -- Fire-Lit Thicket: Filter land, the Cascade Bluffs model (is_filter): fed one {R} or {G} from another source it yields two mana of {R}/{G} in any split; without a feeder it taps for {C}. Colour fixing, not ramp.
- coverage deferral -- Rootbound Crag: Check land: enters untapped iff a controlled land has subtype Mountain or Forest (checkland_subtypes, evaluated in the shared LandWouldEnterTapped predicate so enumeration pricing and the real drop agree).
- coverage deferral -- Ignoble Hierarch: Exalted is ENGINE-MODELLED (Keyword::Exalted -> CountExalted: +1/+1 per Exalted permanent when exactly one creature attacks, applied at the shared combat sites). Tri-colour dork via produces B/R/G.
- coverage deferral -- Spellskite: 0/4 artifact creature body: a legal pump target and attacker. redirects_spell_target_to_self: the redirect is modelled for OUR solo-target tricks -- a searched cast VARIANT of a trick aimed at another own creature A (Action::trick_redirect = this Spellskite), costing one extra {U/P} pip (the phyrexian post-pass emits the pay-2-life twin). At resolution A 'became the target' (Wild Defiance fires for A), then Spellskite does (CR 115.7 -- Defiance fires again for Spellskite), and the spell's payload lands on Spellskite. Offered only where it can differ from targeting Spellskite directly: a Wild Defiance on the battlefield, Spellskite able to attack this turn, and A able to attack (a pump on a non-attacker is worth nothing). PARTIAL/PROVISIONAL: redirecting an OPPONENT's spell or ability is not modelled -- inert, the passive opponent casts and activates nothing; redirecting our own non-trick targeted spells/abilities is not modelled -- no such card shares a list with Spellskite today.
- coverage deferral -- Wooded Foothills: REAL battlefield fetchland: its entry and searched activation are separate decisions; the activation pays 1 life and sacrifices this land as costs, then searches Mountain or Forest and shuffles. It cannot tap for mana. Instant-speed activation is collapsed to our searched main-phase activation slots.
- coverage deferral -- Legion Leadership: Modal double-faced SPELL//LAND (the Turntimber Symbiosis shape): the back face is DB-synthesized from mdfc_back_* and offered by land enumeration as the card's only land face; mdfc_back_enters_tapped -> it ALWAYS enters tapped; it taps for {R} or {W}. FRONT: solo_target_trick (searched target; viewer board click); trick_double_power = 'double its power' == +X/+0 where X is its combat power as the spell resolves (CR 701.10), read AFTER any Wild Defiance trigger. Hybrid {R/W} payable by either colour (the shared hybrid machinery). PARTIAL/PROVISIONAL: 'gains first strike' NOT modelled -- inert in this deck: first strike only changes WHEN damage is dealt, nothing between the two damage steps here changes an outcome, and with double strike it is redundant. PARTIAL/PROVISIONAL: instant speed collapsed to the main phase -- NOT fully inert, doubling after a lone attacker's exalted trigger resolves would include the exalted bonus (see the Morph ledger, D7).
- fetch hold review -- Wooded Foothills (hold modelled); record the provider timing policy in the deck analysis
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
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
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
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.\n{T}: Add {C}.\n{2}, {T}: Choose a color. Add an amount of 
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
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.26); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.17); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
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
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.08); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying\n{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.35); scryfall="Choose five. You may choose the same mode more than once.\n• Target permanent phases out.\n• Target player dra
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
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.\n{R/W}: This creature gains f
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
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.18); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Smokebraider: oracle_text diverges (similarity 0.12); scryfall='{T}: Add two mana in any combination of colors. Spend this mana only to cast Elemental spells or activate abil
- oracle_text advisory -- Incandescent Soulstoke: oracle_text diverges (similarity 0.17); scryfall='Other Elemental creatures you control get +1/+1.\n{1}{R}, {T}: You may put an Elemental creature card from you
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
- oracle_text advisory -- Berserk: oracle_text diverges (similarity 0.28); scryfall='Cast this spell only before the combat damage step.\nTarget creature gains trample and gets +X/+0 until end of
- oracle_text advisory -- Temur Battle Rage: oracle_text diverges (similarity 0.33); scryfall='Target creature gains double strike until end of turn.\nFerocious — That creature also gains trample until end
- oracle_text advisory -- Might of Old Krosa: oracle_text diverges (similarity 0.33); scryfall='Target creature gets +2/+2 until end of turn. If you cast this spell during your main phase, that creature get
- oracle_text advisory -- Vines of Vastwood: oracle_text diverges (similarity 0.44); scryfall="Kicker {G} (You may pay an additional {G} as you cast this spell.)\nTarget creature can't be the target of spe
- oracle_text advisory -- Legion Leadership: oracle_text diverges (similarity 0.13); scryfall="Until end of turn, double target creature's power and it gains first strike."
- oracle_text advisory -- Spellskite: oracle_text diverges (similarity 0.18); scryfall='{U/P}: Change a target of target spell or ability to this creature. ({U/P} can be paid with either {U} or 2 li
- oracle_text advisory -- Blistering Firecat: oracle_text diverges (similarity 0.21); scryfall='Trample, haste\nAt the beginning of the end step, sacrifice this creature.\nMorph {R}{R} (You may cast this ca
- oracle_text advisory -- Hystrodon: oracle_text diverges (similarity 0.31); scryfall='Trample\nWhenever this creature deals combat damage to a player, you may draw a card.\nMorph {1}{G}{G} (You ma
- oracle_text advisory -- Den Protector: oracle_text diverges (similarity 0.37); scryfall="Creatures with power less than this creature's power can't block it.\nMegamorph {1}{G} (You may cast this card
- oracle_text advisory -- Fire-Lit Thicket: oracle_text diverges (similarity 0.36); scryfall='{T}: Add {C}.\n{R/G}, {T}: Add {R}{R}, {R}{G}, or {G}{G}.'
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit f2df0a83f (HEAD 639d00bfc13f); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->
