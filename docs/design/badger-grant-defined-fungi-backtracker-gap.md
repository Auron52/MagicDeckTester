# Brightcap Badger's grant: three gaps, and the hold-ladder defect the first fix exposed (2026-10-29)

Status: FIXED behind four default-ON levers -- see "Measurement" for the A/B and what is owed.
(Found 2026-10-28 while implementing Paradise Mantle; deferred unmeasured. Investigated and fixed 2026-10-29.)

Brightcap Badger: "Each Fungus and Saproling you control has '{T}: Add {G}.'" `ManaDefOf` (SpellEffects.h)
documents the two populations it reaches: definition-less Saproling TOKENS, and Fungus / Saproling CARDS
that have a CardDefinition but no mana ability (in the shipped Fungus list: 4 Sporecrown Thallid, 2 Thallid,
Shroofus Sproutsire, Utopia Mycon -- 8 of 60 cards).

## Gap B -- the backtracker and ManaCacheKey skipped the second population (`MTG_GRANT_DEFINED_PAY`)

* `TapForCostBacktrackWorker`'s top-level source list (`SpellEffects.cpp`, the `s_src_cands_buf` build)
  admitted a granted body only on the `!d` (token) branch; a defined Fungus fell to the
  `BasicLand || ManaDork || mana_rock || IsPaySacSource` test and was skipped.
* `ManaCacheKey`: the same shape, and its own comment says it "must move in LOCKSTEP with the payer's
  predicate".
* The greedy payer (`ManaPayment.cpp`) and the bounds (`LooseManaCeiling`, `UntappedManaUpperBound`)
  always credited both populations, so the three payment layers disagreed about the same board.

Pinned by `test/unit/test_badger_grant_sites.cpp`: Forest + Thallid / Sporecrown Thallid / Shroofus +
Badger pays {G}{G} through the pool and the greedy payer, and failed in `TapForCostBacktrack` (6 of 6
checks fail at `=0`). Fix: both sites use `GrantReaches` / `GrantFaceFor` (the Paradise Mantle branch
generalised); uncapped, since these are distinct cards, not interchangeable tokens.

The mana oracle does NOT see gap B: the menu gates use the bounds, which were already right. It shows in
the backtracker-driven paths -- above all the whole-turn joint prepay (`BatchPrepayMainCasts`).

## What the gap-B fix exposed: the prepay's hold ladder (`MTG_RESERVE_GRANTED_BODIES`, `MTG_HOLD_PARTIAL_DEPL`)

The first A/B of gap B alone (22,000 held-out fungusv3 games, seed 970001+) was neutral when searched
(d3 2 faster / 1 slower, d5 0 / 1, 2HG 0 / 0) but **d0 was 2 faster / 11 slower** (+0.0022). Root cause,
fungusv3 d0 seed 988063 gi62 T5 (`MTG_PREPAY_HOLD_TRACE=1`): Beastmaster Ascension + Doubling Season, 8
mana, off Ancient Tomb, three Forests, Hickory Woodlot ({G}{G} per depletion counter), Undercellar Myconid,
Sporecrown Thallid and two Saprolings. The ladder's first rung holds every reservable source -- the
Myconid (the only creature in the hold class) and the Woodlot. Before the fix that rung could not pay and
the next one spent the Woodlot; with the backtracker able to tap Sporecrown, rung 0 PAID by tapping
Sporecrown and both Saprolings, keeping the Woodlot's counter. Kill T5 -> T6. Two defects:

1. **Granted bodies were never in the creature hold class.** `DorkReserve` keyed on
   `CardTemplate::ManaDork`; a token has no definition and a Thallid is not a dork. Fix: an attack-capable
   body the grant reaches is held exactly like a dork (`MTG_RESERVE_GRANTED_BODIES`). This was already
   live for the Saproling TOKENS before gap B; gap B made it reachable for the Fungus cards.
2. **The partial creature rungs were unreachable on any board with a depletion land.** They hold every
   depletion land and sat AFTER the `[depletion]` last-resort rung, whose hold is a subset of each of
   theirs -- so whenever `[depletion]` failed they failed too, and when it paid it had spent EVERY creature
   to keep the counter, the reverse of the ladder's stated "creatures outrank a depletion counter". Fix
   (`MTG_HOLD_PARTIAL_DEPL`): the partial steps come ahead of the last resorts, each as a PAIR -- the
   remaining creatures with the depletion lands held (same bodies spent, the counter kept), then the same
   creatures with them released. On the T5 board: hold three of the four creatures, spend the Woodlot.
   This one is NOT Badger-specific: it moves every deck with depletion lands and mana creatures (Angels and
   Bruna: Remote Farm; Mirrorwing: Sandstone Needle; Fungus: Hickory Woodlot).

## Gap A -- a plan that casts Badger could not count on Badger's own grant (`MTG_GRANT_CAST_CREDIT`)

Mana oracle, fungusv3 30 games seed 1100001 (`logs/mana_oracle/badger-eg1/`): 11 ENUM-GAP. Three are this
gap -- e.g. seed 1100024 gi23 T3 `{Forest, Saproling Burst, Brightcap Badger}`: cast the Badger, then tap
four Saprolings already on the board for the Burst. Nothing credited the grant to the casts after the
Badger, so the menu priced the Saprolings at zero. The other eight are the existing "Concordant
Crossroads' blanket haste does not unlock the plan's own creatures" row of
`mana-completeness-oracle.md` (three of them are Badger-granted Fungi cast that same turn) -- still open.

Fix, in the Corsair Captain ETB-Treasure shape: `CollectActions` stamps a Badger cast's
`Action::rock_mana` with one {G} per body already on the board that the grant would unlock
(`SamePlanGrantCredit`, SpellEffects.h: untapped, able to tap now, not already a mana source; zero when a
grant is already live). Riding `rock_mana` gives it the rock's discipline everywhere -- "a rock never funds
its own cost" (the board must pay the Badger first), the payable-order walk's producer credit
(`CreditCastOutput` hoists the Badger ahead of what it funds) -- and the scratch walks lay the Badger on
their board so the grant goes live for the later payments (`SubsetPayableWithFiltersImpl`'s producer
pass; `SubsetPayableSequential` already placed every cast permanent). `SpwfProvablySupplyShort` treats a
credited Badger as a producer it cannot price. Known over-credit: two Badgers in one plan are credited
twice (the real walks are exact, so this can only put a PHANTOM on the menu).

## Measurement (2026-10-29)

One pooled batch, 58,400 held-out games, seed 1500001+, per-job `flags` arms on one binary
(`build/scratch/badger-ab2/`; unwon = 9; per game, faster / slower vs all four levers off):

| arm | fungus d0 (3000) | d3 b10 (3000) | d5 play (3000) | 2HG d5 (1000) |
|---|---|---|---|---|
| gap B alone | 1 / 4, +0.0010 | 0 / 1, +0.0003 | 0 / 0 | 0 / 0 |
| B + held bodies + partial-depletion pairs | 17 / 1, -0.0053 | 3 / 2, -0.0003 | 1 / 0, -0.0003 | 0 / 0 |
| all four (+ gap A) | 82 / 40, -0.0137 | 37 / 11, -0.0087 | 19 / 2, -0.0057 | 14 / 1, -0.0130 |

The other decks the ladder fix reaches, all four levers on: Angels d0 + d5 (4000): 0 games moved. Bruna
d0 (1000) 2 / 0, d5 (200) 0 moved. Mirrorwing d0 (2000) 2 / 0, d5 (2000) 17 digests moved, 0 win turns.

All 14 searched slower games (11 d3, 2 d5, 1 2HG) re-played at `--depth 8 --budget-ms 0` under both arms:
every one ties the base arm at the base's win turn. The d0 slower games under gap A are the d0 greedy
runner taking the newly visible line -- fungusv3 d0 seed 1506091 gi90 T5: Wild Growth, Badger, then
Doubling Season off four Saprolings' granted {G}, where the base attacked with them (5 -> 6); the searched
tiers weigh the same option and come out ahead.

Unit suite 2149 / 2149. Smoke + regression GT accepted for the moved decks (smoke 7 keys, regression
14; searched 1 slower in smoke -- fungusv3 d5 gi54 4 -> 5, budget churn, T4 at b100 and d8 b0 -- and 0
slower / 7 faster in regression). Every other deck byte-identical. The overnight GT is left to the periodic run.
