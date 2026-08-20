# Metalcraft enumeration credit — the pricing fix was necessary but NOT sufficient

**Status:** built behind `MTG_METALCRAFT_CREDIT` (default OFF), measured 2026-08-20.
**Deck:** KittyEquipment only — `metalcraft_equip_zero_artifacts` is carried by exactly one card in
`cards.json` (Puresteel Paladin) and by exactly one deck, and KittyEquipment is **not** in the
regression tiers. So this lever cannot move any suite deck's ground truth.

## The gap this closes

The claude-play sweep (ledger stage 5d) found ONE systematic difference between the search and an
informed human on this deck: in 2 of 16 games the human won a full turn faster by casting 2–3
artifacts to flip metalcraft ON mid-turn and then equipping Colossus Hammer for `{0}`. The ledger
recorded the diagnosis as *"the search's one-shot enumeration prices equips at start-of-phase
artifact count ({8}, unaffordable) so the line is never offered"*, and the deferred fix as
*"price equips at post-subset artifact count"*.

That diagnosis is right about the pricing and **wrong about it being the barrier.**

## Two mechanisms, and only fixing both changes anything

**1. The per-subset price (`SameTurnMetalcraftEquipCredit`).** The Equip candidate block bakes
`EquipCostGenericNow(state)` into `Action::cost` — the artifact count as it stands at enumeration.
The credit forgives that generic when the subset's own artifact casts reach the reducer's threshold.
This is the Ruby Medallion precedent (`SameTurnReducerGenericCredit`), applied in `EnumeratePlans`
only, never at Solve's d0 leaf, per the law recorded on `LeafReducerCreditEnabled`: optimism is
sound exactly where a rollout validates the line. It is sounder than the Medallion credit it copies,
because the ORDER it assumes is guaranteed — casts are applied before the trailing equip pass in
both worlds, so the artifact really is on the battlefield when the equip is paid.

**2. The odometer's scalar mana bound (`ManaPruneBound`).** This is the one the ledger missed.
Before `consider()` prices a subset, the odometer skips any position whose summed
`cost.ManaValue()` exceeds a scalar ceiling — and that sum charges the Hammer its printed `{8}`.
The position never reaches the code that would forgive it.

`ManaPruneBound` documents this exact hazard on itself ("any FUTURE cost reducer that is credited
per-subset in `consider()` rather than baked into `a.cost`"), and the haste-dork unlock already
carries the matching `extra_credit` addend for the same reason. Metalcraft needed the same addend:
the total generic of the Equip candidates, an upper bound on what the credit can forgive, which is
the safe direction (raising a ceiling can only prune LESS).

## The measurement that separated them

With the price fixed and the bound left alone, on the two claude-play reproducer games
(`--seed 70014` / `--seed 70015`, d5):

| | seed 70014 | seed 70015 |
|---|---|---|
| enumerations offering a Colossus Hammer equip | 4,092 of 4,092 | 4,487 of 4,487 |
| ...its group surviving the breadth cap | 4,092 | 4,487 |
| subsets `consider()` saw holding it | **0** of 89,492 | **0** of 75,762 |
| win turn | 5 (unchanged) | 5 (unchanged) |

The candidate was offered in every single enumeration, survived every cap, and appeared in not one
enumerated subset. That is what pointed at a filter upstream of pricing.

With the bound addend as well, the same two games:

| | seed 70014 | seed 70015 |
|---|---|---|
| subsets holding a Hammer equip | 32,506 | 48,714 |
| ...surviving the legality rejections | 6,665 | 15,355 |
| ...passing the affordability gate | 1,302 | 360 |
| **win turn** | **4** (was 5) | **4** (was 5) |

and the lines that pass are verbatim the human's: `Colossus Hammer{1} + Shadowspear{1} +
EQUIP Colossus Hammer{8}`. **Both games now match the pilot the search was losing to.**

## Wider effect

A d3 A/B over 300 paired games (train 300001, held-out 900001) with the PRICE fix alone was
digest-identical in every game — 0 of 300 changed play at all. The credit did fire there (186
subsets rescued per 20 games) but only on the cheap shape `Sol Ring{1} + EQUIP Bonesplitter{1}`,
which never changed a decision. That is the honest reading of the price half on its own: live,
correct, and worth nothing without the bound.

With both halves, paired d3, 150 games per block:

| block | control | arm | delta | se | t | faster | slower | plays differ |
|---|---|---|---|---|---|---|---|---|
| train (300001) | 4.9667 | 4.8533 | **-0.1133** | 0.0260 | -4.36 | 17 | **0** | 51 |
| hold (900001) | 4.9600 | 4.8200 | **-0.1400** | 0.0300 | -4.67 | 22 | 1 | 58 |

## Where the effect comes from, and where the one regression comes from

Every held-out game was logged in both arms and bucketed by WHAT changed — the final mulligan
decision (attempt kept + bottomed pair), the main-phase action stream, or nothing:

| bucket | games | turns saved | per game | faster | slower |
|---|---|---|---|---|---|
| main-phase PLAY changed | 56 | 22 | **+0.1467** | **22** | **0** |
| MULLIGAN decision changed | 2 | -1 | -0.0067 | 0 | 1 |
| identical | 92 | 0 | 0 | 0 | 0 |

**The entire win is play, 22 faster and 0 slower.** The only regression in 300 games is a bottoming
side-effect: held-out gi=105 (seed 900106), where both arms face the same 7 after two mulligans and
must bottom 2 —

| | bottomed | keeps | result |
|---|---|---|---|
| control | Puresteel Paladin x2 | Sol Ring, Balan, Jitte, 2 Plains | T1 Sol Ring + Jitte -> **T4** |
| credit on | **Balan + Sol Ring** | Paladin x2, Jitte, 2 Plains | nothing castable T1 -> **T5** |

With metalcraft lines visible, the bottoming rollouts value the enabler highly enough to bottom Sol
Ring, the deck's best card by `card_scores` (+0.59). Main-phase play never diverges in that game;
the turn is lost at the bottoming choice, and extra depth does not rescue it (d5 also T5), which
fits a value shift rather than a budget artifact. The other mulligan flip (gi=35) is turn-neutral.

**Why this is bounded, not a lurking hazard.** `mana_bound` is used ONLY as a skip test before
`consider()` (checked at every use site), so loosening it can only admit positions the real
affordability model then judges — it cannot admit an unpayable plan and cannot distort a score. And
at T1 of gi=105 there are no creatures on the battlefield and none castable off one Plains, so there
are no equip candidates and the scan cannot even arm: the T1 plan space is identical in both arms,
and the divergence is purely in what the rollouts project for later turns.

Gating the credit OFF inside the mulligan/bottoming rollouts would buy back that -0.0067, and is
deliberately NOT done: two hands is not evidence, and a mulligan model that deliberately mis-models
the play it is choosing for is the worse failure.

## Instrument

`MTG_METALCRAFT_STATS=1` prints, at exit, the funnel that made the diagnosis possible:
enumerations where the scan armed / subsets credited / subsets RESCUED (the gate's verdict flipped)
/ enumerations offering an equip priced `>= {3}` / its presence in the odometer before and after the
group cap / subsets `consider()` built holding one / how many survived legality / how many passed
the gate. It also prints the first few rescued and expensive subsets by name.

The distinction between adjacent columns is the whole method: a COUNT of "credit computed" cannot
tell an inert lever from a live one, and "live" cannot tell a lever that rescues the wrong shape
(Bonesplitter) from one that rescues the shape that matters (Hammer).

## Lesson worth carrying

A pricing fix is only reachable if the position survives the PRUNE that runs before pricing. When a
credit measures completely inert — not "small", but *zero games changed* — check whether the
subsets it is supposed to rescue are ever built, before concluding the credit is worthless. Here the
answer was 0 of 89,492, and the two-line addend that fixed it was already documented as a
maintenance obligation on the function that needed it.
