# The Karoo drop's searched position (MTG_KAROO_POSITION)

Status: **BUILT 2026-10-22, lever default ON**, held-out A/B below. USER-requested fix:
*"We should allow the sanctuary to be played at the end of the phase at latest"* -- the end of the
phase is the LATEST point a Karoo may be played, and every earlier point must be reachable.

## The defect

A Karoo / bounce land (Selesnya Sanctuary: "This land enters tapped. When this land enters, return a
land you control to its owner's hand.") was ALWAYS played after the plan's casts
(`karoo_deferred`, both worlds). That is a good default -- the casts tap the lands first, so the
mandatory bounce returns a land already spent -- but it is a fixed placement, not a rule: a land drop
is legal whenever the player has priority with an empty stack in a main phase. The case that exposed
it: Rest for the Weary ({1}{W} instant, "gain 4; Landfall -- 8 instead if a land entered under your
control this turn") could never get its 8 BEFORE combat on a turn whose only land entry is a
Sanctuary, at any budget -- and in SelesnyaLifegain the pre-combat life feeds Blossoming Bogbeast's
attack pump (+X/+X, X = life gained this turn).

## The model

* **Encoding.** `Plan::land_slot <= -2` on a Karoo plan (`TurnSolver::KarooSlotBefore(r, every)`): the
  drop right BEFORE the r-th dispatched cast-section action (hand cast / graveyard cast / Vial put) that
  `TurnSolver::KarooSlotCounts` under the rule carried in the value -- every action (`-64-r`, human play
  and `MTG_UNPRUNE=landpick`) or only the observers of a land entering (`-1-r`, autonomous search). The
  rule rides the value so a plan means one point whatever mode applies it. `land_slot 0` on a Karoo plan
  stays the after-the-casts default; an unreached r falls back to it (never lost).
* **Enumeration.** Each Karoo plan is cloned once per counted action, after every other plan (ties keep
  the historical line). Not inside a breakpoint continuation (`bp_play_searched_land` plays a Karoo
  first there already). Rides `MTG_NO_KAROO_DEFER`.
* **Applies (lockstep).** `ApplyPlanDirect`'s `karoo_pre_hook` and `AIEngine::TakeTurn`'s
  `exec_karoo_pre_hook` run BEFORE each dispatch, at every site `slot_hook` / `exec_slot_hook` run
  after; one body per world (`play_deferred_karoo` / `exec_play_deferred_karoo`) plays the drop at the
  position or at the end, with the searched bounce pin, the bounce-fold probe and the held land Auras.
* **The float.** An untapped returned land is tapped for mana in response (`MTG_BOUNCE_UNTAPPED_FIRST`,
  `docs/design/karoo-tap-in-response.md`), so "tap the Forest, play the Sanctuary returning it, cast
  Rest with the floating {G}" is exactly the line realised.
* **Identity.** Every site that folds `land_slot` by value (dedup / odometer hashes, `SamePlan`, the
  census `Pland` key, `plan_signature`, the human menu keys) distinguishes the clones for free; the
  value-gated plan fingerprint gains a `KSLOT` term. The clones are `PlanDupSkippable` (an inert
  position lands on its base plan's state and costs one apply, not a rollout). The searched bounce's
  width bound uses the distinct-name bound for a positioned Karoo.
* **Human play.** `land=<Karoo>;landat=<k>` (k casts queued before it, 0 = before every cast); a plain
  `land=` / `landafter=` / `landat=all` Karoo is the default. The decision JSON publishes
  `karoo_before`; `tools/play/linebuild.js` encodes a Karoo's queue position as its timing when offered.

## The prune (DecisionProvider::KarooEntryObserver) and why it is sound

Moving the drop from after the casts to before cast c changes only (1) whether a land has entered when
c resolves, (2) whether the landfall triggers the Karoo fires have resolved before c, and (3) that the
bounce victim leaves earlier. (3) never helps: the victim is chosen from the same lands, an untapped one
floats its mana, and a land on the battlefield pays anything its floated mana could. So an action is an
observer iff it reads the land-entry CONDITION (`landfall_damage`, `cast_lifegain_landfall`) or the
deck holds a landfall TRIGGER (`deck_has_landfall`; every cast, since its payload may feed the cast).
Disclosed boundary: a cast that reads our hand sees the returned land earlier.

`LandfallProvider` narrows the trigger half to nothing: no cast in that list reads a landfall payload
at resolution (card-by-card argument in `DecisionProviders.cpp`). Measured before that fold: 3000
held-out std games, 0 better / 3 worse (+1 each, budget spent on the clones), units x1.010.

## Measurements

**Suite.** Smoke (binary e5c71fff8, before the Landfall fold): 136/136 PASS, 0 configs changed, 0 plays
changed. Every shipped Karoo deck holds no observer (no Rest for the Weary / Searing Blaze), and Landfall
-- the only Karoo deck with a landfall trigger -- is byte-identical after the fold, so the shipped suite
cannot move. Scenarios 235/235, unit 1706/1706.

**Held-out paired A/B** (`MTG_KAROO_POSITION` off = base vs on = pos, both arms in one pooled batch per
binary, fresh seeds 73xxxxx, shipped play settings, loss scored 9; `test/paired_arms.py`):

| row | games | better | worse | delta (turns) | units |
|---|---|---|---|---|---|
| vrest (SelesnyaLifegain screen list, 1 Rest, 2 Sanctuary), std | 6000 | 0 | 0 | +0.0000 +/- 0.0000 | x1.0028 |
| vrest, 2HG | 6000 | 4 | 0 | -0.0007 +/- 0.0003 | x1.0022 |
| SelesnyaLifegain as shipped, std | 500 | 0 | 0 | +0.0000 (byte-identical) | x1.0000 |
| SelesnyaLifegain as shipped, 2HG | 500 | 0 | 0 | +0.0000 (byte-identical) | x1.0000 |
| Landfall, std -- BEFORE the provider fold | 3000 | 0 | 3 | +0.0010 +/- 0.0006 | x1.0103 |
| Landfall, 2HG -- BEFORE the provider fold | 3000 | 0 | 5 | +0.0017 +/- 0.0007 | x1.0031 |
| Landfall, std / 2HG -- WITH the fold (70c9569cd) | 1000 / 1000 | 0 | 0 | byte-identical (wins, digests, units) | x1.0000 |
| SENSITIVITY: vrest with its 3 Feed the Clan -> Rest (4 Rest), std | 3000 | 2 | 0 | -0.0007 +/- 0.0005 | x1.0052 |
| SENSITIVITY: the same 4-Rest list, 2HG | 3000 | 7 | 0 | -0.0023 +/- 0.0009 | x1.0032 |

Across every Rest row: 13 better, 0 worse. The 4-Rest list is not a candidate list -- it exists only to
see the mechanism at a frequency the 1-Rest screen list cannot show, on the same apparatus.

The four vrest 2HG games that improved are the mechanism: e.g. gi 1367 (seed 7311367), T4 hand
Selesnya Sanctuary + Rest for the Weary with Nykthos Paragon out -- ON plays the Sanctuary, then Rest
for 8 (the Paragon's counters land before combat) and wins T4; OFF casts Genesis Wave, drops the
Sanctuary after the casts and wins T5. The eight Landfall slower games (+1 each, no faster game) were
search budget spent on Turf clones that cannot win, which is what the fold removes.
