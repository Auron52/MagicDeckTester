# A dropped cast can strand a co-selected EQUIP (a second "not benign" drop class)

**Status:** open, NOT fixed. Found 2026-08-20 on KittyEquipment; the mechanism is deck-independent.
**Reproducer:** `./build/Release/mtg decks/KittyEquipment/KittyEquipment.cod --seed 500560 --games 1
--depth 3 --threads 1 --log-dir <dir>` — reproduces exactly, single-threaded.
**Greppable signature:** an ability string `equip -> #<number>` (a raw card number instead of a host
name) in a game JSON. `AIEngine::bf_name()` falls back to `#N` precisely when no permanent with that
number is on the battlefield.

## This is NOT a missing guard, and it is NOT the optimistic enumeration being wrong

Two things that look like the bug are both fine, and it is worth recording why so nobody re-chases
them:

* `SubsetHasStrandedEquip` (TurnSolver.cpp) already rejects an Equip whose equipment OR host is in
  hand and not cast by the same subset. It is correct and complete — it checks `sac_source_id` and
  `sac_victim_id` through the same `on_bf || cast_here` predicate. **It passed here, correctly: the
  subset DID contain the host's cast.**
* Dropping an unpayable cast at apply time is DELIBERATE. `GameLogger.h`'s affordability-audit note
  states it plainly: enumeration is optimistic on purpose, "over-crediting is safe *because the
  search discards unpayable lines*", and a suite run drops thousands.

Measured on this deck (`MTG_AFFORD_AUDIT=1`, 300 games, d3): **33 of 4,006 executed casts (0.82%)
are planned and then silently dropped as unpayable** — Kor Duelist 25, Kemba 5, Stoneforge 2,
Puresteel Paladin 1. Every one is **colour-short, none total-short**, i.e. the flat wild-pool
approximation crediting Sol Ring's `{C}{C}` toward a `{W}` cost. Cast ORDER cannot fix a colour
shortfall (see `exact-mana-enumeration.md`).

## What IS the defect

`GameLogger.h` already names the exception to "drops are benign":

> What is NOT benign is dropping a same-turn MANA ACCELERANT: the plan committed to a ritual/rock
> precisely to fund a later spell, so losing it strands the payoff.

**A dropped CREATURE cast that a later Equip in the same plan was co-selected against is the same
kind of exception, and the detector does not cover it.** The audit reports `STRANDED accelerants=0`
for this deck — correctly, since no ritual or rock was stranded — while the equip stranding goes
uncounted.

The sequence, from the reproducer (seed 500560, turn 2 main 1):

1. enumeration approves `{cast Puresteel Paladin, equip Bonesplitter -> Puresteel Paladin}`;
   `SubsetHasStrandedEquip` passes because the cast is present;
2. at apply, Puresteel Paladin (`{W}{W}`) cannot be paid — one Plains was spent on Sol Ring, leaving
   one white source and Sol Ring's two colourless — so `CastSpellFromHand` returns and the cast is
   skipped;
3. the plan continues to the Equip. `TapForCost` runs FIRST and pays Bonesplitter's equip `{1}`
   (metalcraft is off — Puresteel Paladin being exactly the card still in hand);
4. `ApplyEquip` finds `host_ok == false` and returns without attaching;
5. the log line is emitted regardless, so the game log claims an attach that never happened, and the
   board snapshot afterwards shows nothing attached.

Card #48 is Puresteel Paladin, in the opening hand and never cast; at that moment there is no
creature on the battlefield at all.

Observed rate of the visible symptom: **2 of 1,270 equips (0.16%), 2 games in 600**, identical in
the control, order and park arms.

## What to do, in the order that pays

1. ~~Extend the STRANDED detector to equips.~~ **DONE** (measurement only, byte-identical, no
   rebaseline). `MTG_AFFORD_AUDIT=1` now prints a second STRANDED line beside the accelerant one:

   ```
   AFFORD_AUDIT  real drops: STRANDED equips=1   (an Equip whose co-selected HOST cast was
                                                  dropped: it still PAID and then no-opped)
   AFFORD_AUDIT    Bonesplitter -> #48                      x1
   ```

   That is the reproducer above, caught with one env var instead of a 600-game log sweep, on any
   deck. Implemented as a per-thread list of dropped card NUMBERS (an Equip carries
   `sac_victim_id`, and a deck holds several copies of a card), reset per plan application and
   consulted at the Equip branch BEFORE `TapForCost` pays. Every call site is behind
   `AffordAuditOn()`, so with the audit off nothing is computed: verified byte-identical
   (`base.train 123c0bdaaf6ffe5d`, `park.train 6a558d4d77e2241f`), smoke 36/36 and regression 60/60
   green.
   **It found a live instance on another deck immediately.** FiveColour + Dragons, 150 games each
   at each deck's own play policy: `STRANDED equips=1`, `Lightning Greaves -> #4`. So this is not a
   KittyEquipment quirk — it is a property of the shared executor path, confirmed on a second deck
   the first time the instrument was pointed at one.

   Two things worth noticing in that same run, neither chased here: 90 of 4,300 executed casts
   (2.1%) were dropped, and unlike KittyEquipment **10 of them are TOTAL-short** (Birds of Paradise
   5, Lightning Greaves 5) rather than colour-short. Total-short is precisely the class the audit
   note says a cast ORDER can strand or save — the Dragonstorm defect — so FiveColour has live
   instances of the order-fixable class worth a look on its own terms.

2. Only then consider the apply-side guard: require the host on the battlefield BEFORE `TapForCost`,
   and log only after a successful attach. **Both halves move the play digest** — `LogAbility` folds
   into it (`FoldStr("A")` runs before the `m_digest_only` early-out), so even the log-only half
   rebaselines every equipment-carrying deck (KittyEquipment, FiveColour, Mirrorwing) for a defect
   firing in 1 game in 300. Expected effect on avg win turn is far below measurement resolution, so
   the fix would be unverifiable rather than verified — and this repo has three separate cases where
   an obviously-correct mana-accuracy fix measurably LOST.
3. The real upstream question is the colour approximation itself (0.82% of casts dropped, 100%
   colour-short). That is `exact-mana-enumeration.md`'s territory and much bigger than this symptom.
