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
