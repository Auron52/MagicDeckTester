# Scaling mana sources: a same-turn permanent widens them, and the search cannot see it

**Status: FIXED 2026-08-10, DEFAULT ON** (`MTG_DOMAIN_WIDEN=0` restores the gap). Fixture
`test/scenarios/fivecolour_domain_widen.json` now asserts `validate_line` **accept** and fails with
the flag off. See "Resolution" at the bottom for the implementation, the measurement, and the one
thing the spec below did not predict.

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

The user also flagged the classes this generalises to: **Elves** (Priest of Titania) and
**Defenders** (Overgrown Battlements) scale on creature counts rather than colours. The Elf
class is now IMPLEMENTED (2026-08-20, `MTG_DORK_GROWTH` + the Wirewood Lodge untap-burst
`MTG_UNTAP_BURST`; fixtures `test/scenarios/stompy_dork_growth.json` /
`stompy_lodge_burst.json`) — see the addendum in `analysis-StompySurprise.md` for the
credit/bound/order pieces and the measurement. Defenders remain unimplemented (no deck).
The mana-relevant rule for all of them is in `fivecolour-search-cost.md`: a scaling source can only
raise total mana when `n > c` (more scaling sources than the widener's cost) or when an untap effect
re-taps one (Wirewood Lodge), with the zero-cost case (Shield Sphere) as the degenerate `c = 0`.

## Cost note

This is a **correctness** item and is expected to make the search slightly *more* expensive: it keeps
subsets the gate currently rejects. It is not to be conflated with the emission-time prune in
`fivecolour-search-cost.md`, which is the opposite trade (drop never-payable lines, byte-identical).
The two interact: `BudgetCanGrow` must treat a live scaling source with `n > c` as a way the budget
can grow, or the prune would re-introduce this same bug from the other direction.

## Resolution (2026-08-10) — the credit, and the hybrid pip that blocked it

Implemented as specified: `ScalingWidenScan` computes the state-only half once per enumeration
(`domain_mask`, plus `live` = untapped scaling sources that can tap now and whose mana the pool
already holds), and per subset `SubsetWidensDomainBy` unions the colours the selection's PERMANENT
casts add. When the wideners are payable from the **un-widened** pool, `eff`/`eff_nc` gain `live`
mana of each newly-added colour. `live == 0` skips the per-subset path entirely, so every deck
without a scaling source is byte-identical — confirmed: smoke 30/30 and regression 50/50 with
**0 play-changed**.

The two halves ship under ONE flag. `MTG_DOMAIN_WIDEN_GATE` is gone: a gate-only flag could never be
turned on to any effect (this file's own §"why" explains that the flat pool rejects the subset first),
so it was a switch with no reachable ON state. `MTG_DOMAIN_WIDEN` now controls the colour gate and
the pool credit together.

### The part the spec missed: a hybrid pip bakes into its FIRST colour

The credit fired and changed nothing, because step 2 — "require the wideners to be payable from the
un-widened pool" — was summing their costs field by field. A hybrid pip is stored baked into its
first colour (`Card.h`), and the metadata that lets `ManaPool::CanPay` try the OTHER colour lives
outside those flat ints. So the sum of Deathrite Shaman's `{B/G}` demanded **black** — the very
colour the un-widened board cannot make, which is the whole point of the reproducer — and
`pool.CanPay(widener_costs)` was false for the one subset the fix exists to rescue.

`AddCostCarryingHybrids` carries the pairs while the fixed 4-entry array has room and leaves the
surplus baked, which only ever demands a specific colour: conservative, never permissive. Note the
sibling accumulations (`rock_costs`, `enable_costs`) still sum flat; that is safe in the same
direction (they can only under-credit) but it means a hybrid-costed rock or equip piece would
silently miss its credit too.

This is the third time in this area that a hybrid pip's first-colour baking has produced a
"the code is right but the line is never offered" bug — `SubsetPayable` and `CheckLine`'s colour gate
were the first two (`92c7ce0`). **Any new flat read of a `ManaCost`'s colour ints is suspect by
default.**

### Measured

Play, 3 x 500 games at d3/b10, both arms on ONE binary (seeds 4200000 / 90001 / 555000-held-out):

| arm | 4200000 | 90001 | 555000 | sum | cost |
|---|---|---|---|---|---|
| widening OFF | 5.0780 | 5.0400 | 5.0720 | 15.1900 | 3,405.9 core-s |
| **widening ON (adopted)** | 5.0800 | 5.0420 | 5.0720 | 15.1940 | 3,432.7 core-s (**+0.8%**) |

**Per-game, the reach is 6 games in 1,500** (`--game-log-dir`, unwon scored as `max_turns + 1`):
2 faster, 4 slower, 1,494 unchanged. The +0.004 IS those six games — at n=6 the direction carries no
information, so the honest reading is *no measurable quality effect either way*, at +0.8% cost.

**Adopted as a correctness fix, not as a quality win.** The engine was failing to enumerate a legal,
strictly better line; the fixture goes `illegal` -> `accept`, and with `max_turns` raised to 9 the
executor plays it for real — Cosmic Spider-Man lands on T6 instead of T7, which is what lets
Progenitus follow on T7 (opponent to -15 instead of -1). That the effect is rare in goldfish games is
a statement about how often this board shape comes up, not about whether the line exists.

### The executor ordering question is answered

The spec flagged it as unverified: *"the executor must cast the widener before tapping the dork"*.
It does, with no new machinery — the cast order is ascending cost, which puts a 1-mana widener ahead
of the payoff it unlocks. Verified end to end on the fixture at `max_turns: 9` (the T6 line is
`cast Deathrite Shaman {B/G}` then `cast Cosmic Spider-Man {W}{U}{B}{R}{G}`), not merely by
`validate_line`. The credit is still `EnumeratePlans`-only per the Medallion precedent; `Solve`'s d0
greedy is untouched.

### Known optimism, deliberate

The credit gives `live` mana of each new colour even when one of those sources is itself tapped to
PAY for the widener (in the fixture Bloom Tender pays for Deathrite, so the true gain is one black,
not two). That is the specified behaviour and it is sound in the same way the rock and haste-dork
credits are: an optimistic affordability HINT that the rollout validates, offered only where a
rollout exists to validate it.

---

# Sibling gap: a dork CAST THIS TURN whose tap is unlocked THIS TURN (haste)

**Status: FIXED 2026-08-09** (default on, `MTG_NO_HASTE_DORK_CREDIT=1` restores the old behaviour).
Fixture: `test/scenarios/fivecolour_haste_dork_mana.json` — asserts `validate_line` **accept**, and
**fails** with the flag off. Distinct from the widening gap above and NOT fixed by it — verified:
`MTG_DOMAIN_WIDEN_GATE=1` changes nothing on this reproducer.

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

## The fix — THREE parts, not one

The credit alone does nothing. Each of these three was independently load-bearing; with any one
missing the line is still not playable, and the first and third are the ones the original diagnosis
underestimated.

**1. The enumeration credit** (`EnumeratePlans`, `HasteUnlockedManaOf` + the `any_haste_dork` block).
Its template is the **same-turn mana-rock credit** ten lines above (`if (sel_rock &&
pool.CanPay(rock_costs))` — "a rock never funds its own cost"). Rocks cast this turn ARE credited; a
creature dork is not, correctly, because it is summoning-sick — *unless this very plan gives it
haste*. Per subset, when the selection contains an `equip_grants_haste` Equip whose host is a
currently-untappable mana dork:

* require the ENABLERS — the equip activation plus whichever of the two pieces the subset casts — to
  be payable from the **un-hasted** pool. That is the rock rule, and it is what keeps the credit
  conservative: the unlocked mana can never fund the unlock.
* credit `eff`/`eff_nc` with the host's tap output (`AddSourceToPool`, so a domain dork composes with
  the widening credit above).

Both host locations are covered — the dork cast by this same subset, and one already on the
battlefield but summoning-sick (cast pre-combat, equipped post-combat) — because the Equip
enumeration only ever offers a `fresh` host. A host that can already tap is skipped: it is in the
pool already, and crediting it would double-count (reachable via a haste *lord*, which the equip
predicate does not test).

**`EnumeratePlans` only, never `Solve`** — an optimistic affordability hint is sound only where a
rollout validates the line, and Solve's d0 greedy has none (the Medallion precedent).

**2. The total-mana bound** (`ManaPruneBound`'s new `extra_credit` addend). *This is the part the
original spec missed.* The scalar bound is `pool.Total()` plus ritual/rock float, and the odometer
rejects any position costing more than it **before `consider()` ever runs** — so the 7-mana subset
died at the bound and part 1 never saw it. The credit and the bound have to move together. The
addend is an upper bound on the turn's mana, so it can only loosen the prune; `Solve` passes 0 and
is byte-identical. The selection-exact gate (`MTG_SEL_MANA_GATE`, default off) models ritual/rock
float only, so it bails to the scalar path when an unlock is present rather than silently
re-introducing the bug from the other direction.

**3. The execution side — ordering AND colour reservation.** Enumerating the plan is not enough; the
executor stranded it two different ways.

*Ordering* (`TurnSolver::ApplyManaUnlockEquips`): equips are applied in a **trailing pass after all
the main casts**, so the haste arrived after the cast that needed it. The unlock equip now fires the
moment both its pieces are on the battlefield — called before the casts and again after each one, by
the rollout (`ApplyPlanDirect`) and the executor (`AIEngine::TakeTurn`) through that one function, so
the two stay in lockstep. It is self-gating on "the host is a still-locked mana source", so an equip
onto a beater (Greaves → Maelstrom Archangel) is untouched and still fires in the trailing pass; that
pass skips an already-attached pair so the cost is never paid twice.

*Colour reservation* (`TurnSolver::ManaUnlockColorReserve` + `PlanSourceReserveScope`) — the "reserve
red" half of the user's report. With the ordering fixed the line was STILL stranded: the per-cast
greedy paid the enablers' generic pips with **Steam Vents, the only red source**, so `{R}` was gone
before the hasted Bloom Tender ever tapped, and the dork's own `{W}{B}{G}` cannot substitute. The
plan-scoped reservation holds such a source untapped until the unlock fires, riding the payment
path's existing reserve-then-fallback (`ReservableSpecialMask`), so over-reserving can never make a
cost unpayable. The rule is deliberately narrow: reserve a colour's sources only when the untapped
count is `<=` the pips the payoff still needs after subtracting what the dork itself makes — i.e.
only when the colour is genuinely scarce and every one of those sources is load-bearing.

`BatchPrepayMainCasts` is **not** the mechanism here, contrary to the note this section previously
carried. It declines outright on this turn: it solves the turn's whole combined cost against the
board, and before the unlock that cost (7) exceeds what the board makes (5). The reservation is
therefore additive to it, not covered by it. See `mana-source-reservation.md`.

## Harness note: scenario fixtures had no card identity

Two harness gaps had to be closed before this could be regression-guarded at all, and both affect
every future fixture:

* `--scenario` built each card from the *definition*, whose `Card::m_number` is 0 — so every card on
  a fixture board shared number 0, and the Equip enumeration's "never equip an Equipment to itself"
  guard (equipment number == host number) rejected **every** pair. No equip line existed in any
  scenario fixture. Cards are now numbered in construction order.
* A fixture could only assert a win turn, which structurally cannot see this bug: against a passive
  goldfish Lightning Greaves is worth nothing, so the search legitimately prefers the cheaper
  `Bloom Tender + Mana Cannons` line and plays the same turn either way. The value of the fix is that
  the line is *available* to the human. `--scenario` therefore now supports `validate_line` +
  `expect_verdict`, running `TurnSolver::CheckLine` on the authored board — the same path the play
  viewer uses.
