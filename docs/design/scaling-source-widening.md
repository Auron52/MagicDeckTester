# Scaling mana sources: a same-turn permanent widens them, and the search cannot see it

**Status:** diagnosed and reproduced; fix specified below, stage 1 landed DEFAULT OFF
(`MTG_DOMAIN_WIDEN_GATE`, byte-identical). Fixture: `test/scenarios/fivecolour_domain_widen.json`.

**The user's question (2026-08-08):** *"the dorks on the field (Bloom Tender, Faeburrow Elder) can add
more mana based on what we play … sometimes the play is to play a rainbow permanent and then cast
another with one of these dorks. This is also something we should check if it is implemented
properly."*

**Answer: it is not.** The payment path is correct. The *search* never offers the line.

## The reproducer

`build/Release/mtg --scenario test/scenarios/fivecolour_domain_widen.json`

| zone | contents |
|---|---|
| battlefield | Faeburrow Elder (G,W), Bloom Tender (G), Breeding Pool, Jetmir's Garden |
| hand | Deathrite Shaman `{B/G}`, Cosmic Spider-Man `{W}{U}{B}{R}{G}` |

Lands are colourless permanents (CR 105.2c), so the domain is `{G,W}` — the two dorks are the only
coloured permanents. Each therefore taps for `{W}{G}`, and the board makes 6 mana with **no black**.

The line, which is exactly payable:

1. tap Bloom Tender for `{G}`, cast **Deathrite Shaman** `{B/G}`
2. Deathrite is black-green, so the domain becomes `{G,W,B}`
3. **Faeburrow Elder now taps `{W}{B}{G}`** — with Breeding Pool `{U}` and Jetmir's Garden `{R}`
   that is precisely `{W}{U}{B}{R}{G}`
4. cast **Cosmic Spider-Man**

The engine casts **nothing at all**. Three controls isolate it:

| arm | board | payoff cast? |
|---|---|---|
| A | Deathrite **already** on the battlefield | **yes** |
| C | control: add Overgrown Tomb so black exists without widening | **yes** |
| B | the reproducer — widening required | **no** |

A rules out the payment path: two widened dorks are tapped for `{W}{B}{G}` each without trouble, so
`DomainColors` and `EffectiveProduces` are right. C rules out a value preference for combat: given a
castable payoff the engine casts it rather than attacking. What is missing is only the *discovery*
that casting a permanent first unlocks the payoff.

`MTG_MANA_PRUNE=0` and `MTG_SEL_MANA_GATE=0` change nothing, so it is **not** the total-mana bound.

## Why — two state-only assumptions, both load-bearing

A scaling source's output is not a property of the board alone. "For each color among permanents you
control" is a function of the *selection*: a coloured permanent cast earlier in the same plan changes
it. Two places assume otherwise.

**1. `ComputeAvailableColors` (`src/ai/TurnSolver.cpp`).** Its own comment states the assumption —
*"State-only — it does not depend on which subset is being tested — so callers compute it ONCE before
the subset-enumeration loop"*. For a `domain_mana` source `EffectiveProduces` returns
`DomainColors(state, active)`, the colours on the battlefield *right now*. So `have[Black]` is false
and every subset containing the payoff is rejected as requiring a colour "no untapped source can
produce at all". Its soundness claim — *"it can never reject a plan the real payment would allow"* —
holds for static sources and fails for a scaling one.

**2. The flat pool `mana_ok` test, which rejects first.** `eff = pool` is built from
`AvailableManaPool(state)`, which credits each dork with its *current* domain. The subset
`{Deathrite, Spider-Man}` needs a `{B}` pip the pool does not contain, so it returns at the
`!mana_ok` line before the colour gate is consulted. **This is why stage 1 alone is inert** and ships
off: loosening the colour gate cannot rescue a subset the pool has already killed.

## The fix

Mirror the **mana-rock credit** that sits ten lines above the failure, which solves the identical
shape of problem — a same-turn cast whose mana is only creditable once the board can pay for the cast
itself (`if (sel_rock && pool.CanPay(rock_costs))`; *"a rock never funds its own cost"*).

Precompute once per enumeration (state-only, so it stays off the per-subset path):

* `domain_mask` — `DomainColors(state, active)` as a bitmask
* `live_dorks` — untapped `domain_mana` permanents with `CanTapNow`

Then per subset, when `live_dorks > 0`:

1. union the colours of every selected candidate that is a **permanent** (skip instants/sorceries —
   they never enter play) and is not already in `domain_mask` → `new_colors`
2. require the wideners to be payable from the **un-widened** pool (`pool.CanPay(widener_costs)`) —
   the rock rule, and what makes the credit conservative rather than optimistic
3. credit `eff`/`eff_nc` with `live_dorks` mana of **each** colour in `new_colors` — each dork gains
   exactly one mana of each newly-added colour

Inert whenever no `domain_mana` source is live, so every other deck is byte-identical.

**Where to apply it — `EnumeratePlans` first, not `Solve`.** The precedent is in the file: the
same-turn Ruby Medallion reducer credit is deliberately `EnumeratePlans`-only, because an optimistic
affordability hint is sound only when a rollout validates the line, and Solve's d0 greedy has no
rollout — crediting it there let the greedy commit a line the executor stranded on (smoke gi523
8→loss). This credit is stronger than the Medallion one (the widening is certain once the permanent
resolves, and step 2 makes it conservative), but it carries the same **ordering** obligation: the
executor must cast the widener before tapping the dork. That is unverified — arm A only proves a dork
widened *before* the turn taps correctly, not one widened *during* it. Verify the executor lockstep
before crediting `Solve`.

## Scope beyond `domain_mana`

`reflecting` (Reflecting Pool) has the same shape — its union depends on other lands, and a land
played this turn changes it. Not in any current deck, so it is untested and unfixed; the same credit
applies with lands-in-hand as the wideners.

The user also flagged the classes this generalises to, none implemented yet: **Elves** (Priest of
Titania) and **Defenders** (Overgrown Battlements) scale on creature counts rather than colours.
The mana-relevant rule for all of them is in `fivecolour-search-cost.md`: a scaling source can only
raise total mana when `n > c` (more scaling sources than the widener's cost) or when an untap effect
re-taps one (Wirewood Lodge), with the zero-cost case (Shield Sphere) as the degenerate `c = 0`.

## Cost note

This is a **correctness** item and is expected to make the search slightly *more* expensive: it keeps
subsets the gate currently rejects. It is not to be conflated with the emission-time prune in
`fivecolour-search-cost.md`, which is the opposite trade (drop never-payable lines, byte-identical).
The two interact: `BudgetCanGrow` must treat a live scaling source with `n > c` as a way the budget
can grow, or the prune would re-introduce this same bug from the other direction.

---

# Sibling gap: a dork CAST THIS TURN whose tap is unlocked THIS TURN (haste)

**Status:** diagnosed and reproduced 2026-08-09, not implemented. Distinct from the widening gap
above and NOT fixed by it — verified: `MTG_DOMAIN_WIDEN_GATE=1` changes nothing on this reproducer.

**The user's report (seed 3, hand-played artifact `logs/play/rejections/FiveColour_cod_s3_gi2_t3.json`):**
*"we should be able to reserve red and use it to cast Mana Cannons in conjunction with the hasty
Bloom Tender."*

## The line

Board (T3 pre-main, all untapped): Breeding Pool, Steam Vents, Deathrite Shaman, Faeburrow Elder.
Hand: Bloom Tender, Jetmir's Garden, Lightning Greaves, Mana Cannons, Nicol Bolas, Unite the Coalition.

    land=Jetmir's Garden ; cast=Bloom Tender ; cast=Lightning Greaves ; cast=Mana Cannons

1. Jetmir's Garden enters **tapped** — contributes nothing this turn.
2. Cast Bloom Tender `{1}{G}` and Lightning Greaves `{2}` off Faeburrow Elder (`{W}{B}{G}` — the
   domain is `{W,B,G}`: Faeburrow is G/W, Deathrite is B/G) plus Breeding Pool.
3. **Equip Lightning Greaves onto Bloom Tender for `{0}` → haste → Bloom Tender can tap**, adding
   `{W}{B}{G}`.
4. Cast Mana Cannons `{2}{R}`: `{R}` from Steam Vents, the two generic off Bloom Tender.

## Why the totals make this a REAL gap, not just a reservation one

Costs are `{1}{G}` + `{2}` + `{2}{R}` = **7 mana**. Without Bloom Tender's own tap the board makes at
most **5** (Breeding Pool 1 + Steam Vents 1 + Faeburrow 3; Deathrite is dead — its mana ability is
fuel-gated on a land in the graveyard and none is there). So reserving red is **necessary but not
sufficient**: the line cannot be paid at all unless the dork cast this turn taps this turn.

Reproduced: the engine casts Bloom Tender + Lightning Greaves, **never equips**, spends Steam Vents
(the only red source) on a generic pip, and never reaches Mana Cannons. `CheckLine` grades the
hand-played line `illegal: can't pay {2}{R} ... with the mana available this phase`, and the plan
menu offers `{Bloom Tender, Mana Cannons}` and `{Bloom Tender, Greaves, equip}` but never the
four-action line.

## The fix

The template is in the same function as the widening credit: the **same-turn mana-rock credit**
(`TurnSolver.cpp`, `if (sel_rock && pool.CanPay(rock_costs))` — "a rock never funds its own cost").
Rocks cast this turn ARE credited; a creature dork is not, correctly, because it is summoning-sick —
*unless this very plan gives it haste*.

Per subset, when the selection contains BOTH a mana dork cast from hand AND a haste-granting
equipment (`equip_grants_haste`) equipped onto that dork (or the dork has haste natively):

1. require the enabling casts (dork + equipment) to be payable from the **un-hasted** pool — the rock
   rule, and what keeps the credit conservative,
2. credit `eff`/`eff_nc` with the dork's tap output (its `PermanentManaYield` under the domain the
   board will have once it resolves — compose with the widening credit above),
3. gate on the equip actually targeting that dork, so it is inert for every other selection.

**`EnumeratePlans` only, never `Solve`** — same reason the doc gives above: an optimistic
affordability hint is sound only when a rollout validates the line, and Solve's d0 greedy has no
rollout. It carries the same ordering obligation on the executor (cast dork → equip → tap), which is
**unverified** and must be checked before any credit reaches `Solve`.

Colour **reservation** is a separate, additive requirement: even once the line is enumerated, the
payment must not spend the only red source on a generic pip. See `mana-source-reservation.md` —
whole-turn batch payment (`BatchPrepayMainCasts`) is the mechanism that should already handle it once
the subset is offered, so verify against it before adding anything new.
