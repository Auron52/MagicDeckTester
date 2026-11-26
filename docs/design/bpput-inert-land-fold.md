# Site 10 battlefield-put half: the inert-land fold (2026-11-26)

Branch `wip/bpput-cost`. Measurements: `logs/bpput/report.md`.

## The cost being reduced

The Genesis Wave fix (site 10's battlefield-put half, `TurnSolver::CastPutACardPermanent`) arms a deferred re-solve
after any cast that puts one of our CARD permanents onto the battlefield from outside the hand. `PlanOpensBreakpoint`
marks the put families so the continuation is searched. It is a completeness fix and stays. Its cost on held-out
seeds (units/game vs the pre-fix tree): creature_giving 1.455x, creature_giving2hg 1.415x, melira 1.33x, landfall
1.29x, hydras 1.19x. The new marks come from ordinary ramp spells (Rampant Growth, Search for Tomorrow, Nature's
Lore, Three Visits, Crop Rotation) and creature puts (Chord of Calling).

## The fold

A put LAND that no option this turn can read is not new material, so its cast arms nothing for it, exactly as when
the search whiffs. A put permanent is a fold CANDIDATE (`TurnSolver::PutLandFoldCandidate`) when it is:

* a basic, non-snow, non-creature land whose definition reads nothing (see readers below), and
* either TAPPED, or put by a CREDITED cast, i.e. the cast carried the land-put mana credit (`Action::rock_mana`,
  core/LandSearch.h "THE LAND-PUT MANA CREDIT").

It is INERT when it is a candidate and no reader is live (`TurnSolver::LandPutReaderLive`). When every new
permanent of the cast is inert, `CastPutACardPermanent` returns false. Both worlds call it with the cast's credit:
the rollout's `apply_one` rider `cast_put_credit`, and the executor's `cast_by_name` rider through
`rdb_bf_credited`.

The MARK half is `PlanLandPutFoldsInert`, called from PlanOpensBreakpoint's land clause. It folds only where the
arming provably folds, because a mark folded while the arming still fired would leave the continuation as an
unchallengeable canon default.

## The proof

The class's own no-op argument: a cast that put nothing leaves the continuation a board holding no object the plan
was enumerated without, so a base plan already carries whatever the re-solve could do. The fold extends this to a
cast that put an object L when no option can see L. In that case the continuation's option set, and each option's
legality, cost and effect, are those of the whiffed board, which the class already declines to arm.

An option can read a basic land permanent L only in these ways:

1. **Its mana.**
   * A tapped L produces nothing this turn unless something untaps it, and every untapper is a reader (2).
   * An untapped L is admitted only from a credited cast. The credit has already priced L's tap, global land-tap
     bonuses included, into every cast after the search in the plan, and its classes partition the picks. So
     "search, then spend L" is a base plan.
   * The mana sinks outside that funding order are the MANA readers, checked whenever L is untapped:
     * a suspend (the Suspend action runs in the pre-cast pass, ahead of the credited cast);
     * a mana-cost activation (equip with a mana cost, Steelbane Hydra's counter removal).
2. **Its presence as an object.** This covers:
   * a land, land-type or basic count;
   * L as a target, or as a sacrifice, bounce, blink or untap victim;
   * a land-entered-this-turn condition, or a landfall trigger;
   * a per-land mana bonus, a fastland's entry test, or a top-play permission.

   Each of these is a card param, a template or a landwalk keyword. `CardDefinition::bpput_land_reader` is derived
   at load from the card's JSON `parameters` KEYS against a reviewed whitelist (`BpPutLandReaderFromJson`,
   CardDatabase.cpp). It FAILS TOWARD READER: any key not on the list (including every param added later), any
   template other than custom / basic_land / vanilla_creature / mana_dork, a landwalk keyword, or a definition
   registered without JSON makes the card a reader. A false "reader" only declines a fold.
3. **The library.** A reader still in the library cannot act this turn unless it first arrives in a zone an option
   acts from. A non-reader can only move it there through a route that arms its own breakpoint: a draw (draw keys
   are not whitelisted, so a drawing card is a reader), a tutor to hand, or a put. Each such route's re-solve sees
   L in both worlds alike (whitelist property 2). So the arming does not scan the library.

`LandPutReaderLive` scans every zone an option acts from this turn:
* our hand, graveyard, staged cards and sideboard (companion or wish);
* the exile;
* every permanent on the battlefield, on both sides.

So when every new permanent is inert, the armed continuation reaches no state the unarmed tree does not. At an
unlimited budget nothing becomes unfindable. Under a finite budget the freed variants are budget spent elsewhere,
and that can move play.

### The whitelist's three properties (each key was reviewed for all three)

1. It never reads our battlefield lands in a way that decides what a plan can do.
2. It never moves a library card into an actionable zone, except through a route that arms (to hand, a put) or
   outside our main phase (an upkeep trigger).
3. It is not an activated ability with a mana cost. The three exceptions are marked as mana readers instead:
   `suspend_cost`, a non-zero `equip_cost_generic`, and `remove_counter_destroy_art_ench_cost`.

The list covers the keys held by the Hydras, Landfall, Creature Giving and Melira Pod lists, plus their mana and
creature bodies. `loyalty_abilities` is safe only when every effect is `beast_token_33` or `draw_greatest_power`.
Widening the list needs the same three-way review for each key.

### Why the mark implies the arming (`PlanLandPutFoldsInert`)

All of the following are evaluated on the plan-start state:

* The cast is a hard cast of an instant or sorcery that searches straight to the battlefield (nothing to hand, no
  ETB or attack search).
* Its land enters tapped, or the action carries the credit. This is the same value the riders hand the arming.
* Every pick of the search's own memo (`LandSearchMemoSlot`) is a candidate land, so whichever land comes is one.
* No reader is live now. The mana readers are included when the land comes untapped.
* No other action of the plan is a tutor or ETB tutor to hand, a put (Chord, Wave, Vial, Pod), a battlefield tutor,
  or a land search to hand.

At the arming, the zones then hold only:
* the plan-start cards;
* cards that arrived through an ARMED route, whose deferred re-solve already claimed the cast window (the put is
  then never asked);
* the land itself.

## What was NOT folded, and why

* **Creature puts** (Chord of Calling, Genesis Wave, Defense of the Heart): a creature is fodder, a Pod or sacrifice
  target, a convoke body and an attacker. In Melira every one of those is read by the deck (Pod, Carrion Feeder,
  Redcap). No proof is possible.
* **Uncredited untapped lands** (Crop Rotation's `tutor_land_to_battlefield`; a Nature's Lore whose picks include
  a non-basic, which withdraws the credit): the land is new mana that the plan was never priced with. That is the
  completeness case the fix exists for.
* **Snow basics:** snow readers were not reviewed.
* **Global land-tap bonuses (Vernal Bloom, Gauntlet of Power, Nissa):** these are readers. The credit includes a
  bonus that is live at enumeration, and `LandPutCrossCredit` covers a bonus the plan establishes. But a bonus cast
  by the continuation itself was not proven covered, so they stay readers.
* **Cultivate and other searches that also put a card in hand:** the hand half arms on the hand card anyway.
* **Fetch and land-search activations, Khalni, Myriad Landscape, Karoo lands:** these are readers by key. Karoo and
  sacrifice-a-land costs genuinely read L.
