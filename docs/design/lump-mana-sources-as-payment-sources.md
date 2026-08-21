# One-shot "lump" mana sources belong in the PAYMENT SOLVER, not the plan enumerator

2026-08-21. Design + sizing for moving Black Lotus (and Lotus Bloom, Treasures, storage lands) out of
the plan enumerator's per-colour fan and into the mana payment solver as a first-class source, with a
reservation doctrine for one-shot resources. **Design is the USER's** (2026-08-20/21); this doc records
it, the mechanism it fixes, and the measured ceiling. **Not built** — deferred per repo convention.

## 1. The defect

`ChosenFloatColorCandidates` (TurnSolver.cpp) emits **one `SacForMana` Action per candidate colour**,
and each becomes its own odometer group. Candidate colours are every colour with a pip in the active
player's hand / **library** / graveyard / battlefield. On a five-colour deck that is all five, so one
in-play Lotus is a **6-option odometer group** (5 colours + don't-use), multiplying everything the
enumerator does at that node.

The engine does not actually believe in these variants. The subset math credits the Lotus as
`ritual_float` **wild** (`a.ritual_float = sac_for_mana_amount; // credited as wild (Solve)`), and two
separate feasibility sites patch its mana back in as wild precisely because the action model hides it:

* `TurnSolver.cpp` ~4046 — *"AvailableManaPool deliberately does NOT count these — they are modelled as
  an ACTION (Kind::SacForMana), not a source — so without crediting them here the ceiling under-counts"*
* `TurnSolver.cpp` ~22334 (`BootstrapFeasibleMana`) — *"AvailableManaPool / ComputeAvailableColors omit
  them… so the affordability sim below would FALSE-REJECT a line they pay for"*

So the colour split is enumerated in the search, then thrown away as wild by every gate that has to
reason about it. **The colour of a mana source is a payment question, not a plan question** — which is
how every land in the engine is already treated.

### The Dragonstorm precedent, and why it does not generalise

`dragonstorm-float-colour-collapse.md` hit this exact fan-out and fixed it with a **provider heuristic**
(`RestrictSacColorsToHasteAndRed` / `ImpulseFloatColorRedOnly`): force RED unless a haste creature
castable this turn demands otherwise. Worth 25–180x on that deck's captured slow rollouts (a 31.7-minute
atom → ~11 s).

That shortcut existed only because Dragonstorm is red-primary. **FiveColour has no such shortcut** —
every colour is genuinely live — so the same structural defect has no heuristic patch available here.
Done properly, the source model **subsumes both Dragonstorm hooks**: an exact model replaces a
deck-specific heuristic, and those provider overrides can be deleted rather than duplicated per deck.

## 2. The design (USER)

> *"What prevents [Black Lotus] from being used like a land? Do we actually need those variants? We can
> instead just make it act similar to a depletion land and provide what the user needs. Though I should
> note that a 1-time-use source like this wants to be reserved when we don't need it."*

### 2a. Model it as a source

The flow matcher (`ManaPayment.cpp`, `BuildFlow`) currently has exactly two source shapes:

| shape | call | meaning |
|---|---|---|
| free choices | `add(mask, amt)` | `amt` mana, each independently chosen from `mask` |
| fixed bundle | `add_one_of_each(mask)` | one of EACH colour in `mask` (domain / Karoo) |

A Lotus is **neither**: it is *N mana, all of ONE colour chosen at solve time*. That is a **third shape
that does not exist yet** and is the one piece of real new machinery this needs. Note the two existing
gates already approximate it with the strictly-more-permissive `add(0x1F, 3)`; that approximation is
fine for a conservative gate but **must not** become the real payment (it would let one Lotus pay
`{W}{U}{B}`, which is illegal).

Once the shape exists, the 5 `SacForMana` colour variants and their odometer group are **deleted**, not
capped — the solver picks the colour from demand, the way it already does for every land.

### 2a-bis. The USER's decomposition (2026-08-21) — this is what collapses the implementation

> *"Lotus is well suited to cover either 1 wild + 2 colourless or all of a specific colour in a line."*

Lotus-as-`WWW` can pay `{W}{W}{W}`, `{W}{W}`+1 generic, `{W}`+2 generic, or 3 generic. The two shapes
that matter:

* **Shape A — 1 coloured pip + 2 GENERIC.** The common case. Critically, **the colour choice here is
  free and interchangeable**: if the Lotus only has to cover ONE pip, every one of the five variants
  does it equally. This is the sharpest statement yet of why the fan is redundant rather than merely
  expensive — in the dominant case the five branches ARE the same branch.
* **Shape B — 3 pips of one specific colour.** The concentrated case, and the ONLY case where the
  all-one-colour constraint actually binds.

**Implementation consequence: no general constraint solver is needed.** Model the Lotus as one pip
drawn from `mask` plus two GENERIC-ONLY units. That is exact everywhere except a cost demanding ≥2
same-colour pips off the Lotus alone, where shape B is offered as the alternative. Far smaller than
the general "N of one chosen colour" machinery described above, and it sits naturally on the existing
payment order (`ManaPayment.cpp` ~380: *"Pay coloured requirements first (most restrictive), then
generic"*, with generic paid via `pay(Color::Colorless, /*any=*/true)`).

**There is a ready-made primitive for the two generic units.** The engine already has a "{C} mode"
supply: filter lands tap `floating.Add(Color::Colorless, 1)` and line 141 notes *"{C} mode covers
generic/{C}"*, while generic pips are paid as `pay(Color::Colorless, /*any=*/true)` (~387). So shape A
can be expressed with machinery that exists.

**GENERIC is not COLOURLESS — the model must not conflate them.** The engine keeps them separate
(`ManaCost::generic` vs `ManaCost::colorless` for `{C}`, Card.h 74/80; `ManaPool::colorless` is its own
field and `CanPay` charges `max(0, cost.colorless - colorless)`). Black Lotus makes COLOURED mana, so
it pays generic `{2}` but can NEVER pay a `{C}` pip. FiveColour has no `{C}` costs (checked — the
distinction does not bind on this deck), but the shape is engine-wide and other decks can, so the two
generic units must be generic-payers, not colourless mana.

### 2b. Reserve one-shots — the lump doctrine

A "lump" source is all-or-nothing: tapping it produces a fixed quantity and any surplus is wasted
(floats, empties at end of turn). Lotus (3, one colour) and storage lands are both lumps — the storage
land bursts **all** its counters on a single tap (`ManaPayment.cpp`: *"a single tap now BURSTS ALL live
counters… the land is committed for the turn"*).

USER doctrine:

1. **Reserve the one we don't need**, in priority order **Lotus → Storage → Depletion** (most to least
   consequential to lose). Mana creatures sit **above depletion lands** — a land has no use but its
   mana, a creature also attacks/blocks.
2. **Waste is the trigger.** Reservation is strongest when the turn cannot use the whole lump — spending
   a 3-mana Lotus to pay 1 burns 2.
3. **Never spend a lump that cannot complete the payment when one that can is available.** USER's case:
   need 3, holding a Lotus and a storage land that *cannot* provide all 3 → use the Lotus. A partial
   lump is pure loss: it commits the resource **and** still leaves you needing another source. This
   corrects a naive "rank Lotus last", which would spend the storage land first — exactly the wasteful
   outcome.
4. Scope: **Dragonstorm and future decks**, not FiveColour (checked — FiveColour's 32-card list has no
   storage or depletion lands; its only lump is the conjured Lotus).

Half of this already exists and should be extended rather than rebuilt: `BatchPrepayMainCasts` has the
whole-turn *"leave out if you can"* reserve (`reserved_depl` for depletion lands, `reserved_crea` for
the beater and dorks), and `ManaSourceRank` already taps storage LAST.

### 2c. What is already covered (do not rebuild)

* **Faeburrow in main 1.** USER: *"Faeburrow Elder in the first main could be incorrect"* — tapping the
  vigilant scaler pre-combat forfeits the attack. Both halves exist: `BoardHasVigilantManaScalerAttacker`
  makes the cast-phase classifier answer *Both* instead of *Main1* (the FAEBURROW DOCTRINE, `bdc5ccd9`),
  and `AttackerReserveEnabled`'s "hold your beater" already reserves the best attacking mana source so it
  is not tapped for mana instead of swinging. An explicit Faeburrow slot in the reserve order is only
  warranted if measurement shows a residual gap.
* **Reserving scaling dorks to grow their output is moot on FiveColour.** USER: *"If garth didn't provide
  all 5 colors I might also have considered reserving all of the scaling dorks… as it is, this is not
  really necessary."* Garth One-Eye is a `{W}{U}{B}{R}{G}` permanent, so one Garth on board already puts
  all five colours in play and pins Faeburrow / Bloom Tender at maximum output. There is nothing to grow
  toward. (It is also part of why these boards are expensive: every dork tap is 5 mana into the payment
  space.)

### 2c-bis. WILD IS NOT ACCEPTABLE FOR THE LOTUS (USER, 2026-08-21) — and it is the sharpest argument

Black Lotus adds three mana of **any one** colour: all three must match. Crediting it as `wild` is
therefore wrong in a way that matters at the point of decision, not just cosmetically.

Traced in code: the colour variants of ONE Lotus are correctly mutually exclusive
(`sac_source_id` / `SubsetHasDuplicateSacSource`, TurnSolver.cpp ~3163) — one Lotus, one colour. But the
affordability credit is `credit.wild += a.ritual_float` regardless of which variant was selected. So:

> **The affordability model cannot tell the five variants apart.** All five credit an identical
> "3 wild". The search enumerates five branches that are INDISTINGUISHABLE to the very test that
> decides them, and the colour only becomes real at apply.

That is the strongest form of the argument in §1: the split is not merely expensive, it is five
branches the decision procedure cannot separate. It is also over-permissive in the dangerous
direction — a cost needing `{W}{U}` reads payable off a single Lotus under the wild credit, and
*every one* of the five variants reads payable, when none of them is.

**Not yet verified:** whether that produces a genuinely mis-committed line or only wasted branches.
The two feasibility sites (~4046, ~22334) are explicitly conservative gates with "contention… left to
the real payment", and the site at ~1937 is a stranding-avoidance helper where over-permissiveness is
harmless. The enumerator's `consider()` / `eval_and_push` path is the one that could mis-afford; it
needs instrumenting before any bug is claimed. Do not assert a defect on this reasoning alone — three
confident causal stories about this deck have already been refuted by short measurements.

Consequence for the design: the one-colour-lump shape is not only a perf change. It makes the
constraint EXACT at the point of decision, which neither `add(0x1F, amt)` nor the current five-variant
fan does.

## 3. Sizing the ceiling (measured, 2026-08-21, HEAD `552a76b5`)

Instrument: `MTG_SAC_COLOR_CAP` (value-carrying int, 0/unset = off = byte-identical) truncates the
float-colour fan to the N highest-demand colours. **A sizing instrument, never an adoption** — it is a
quality prune (it denies real colour choices), whereas the source model is exact. cap=1 leaves a
2-option group where the source model leaves none, so this **under-states** the design's ceiling.

Harness: `MTG_KEEP_REPLAY` on the 14 matched mid-tier slow hands from the stopped FAST run
(`logs/fc_garth/`), 2 passes, per-hand minima, alternating arms.

| set | wall BASE → CAP1 | speedup | odometer | per-hand median (range) |
|---|---|---|---|---|
| Garth openers (n=8) | 132.6 s → 82.9 s | **1.60x** | 1.46x | 1.50x (1.00–41.53) |
| non-Garth (n=6) | 64.3 s → 54.1 s | 1.19x | 1.15x | 1.00x (0.98–12.65) |
| **all (n=14)** | 196.9 s → 137.0 s | **1.44x** | 1.36x | 1.12x (0.98–41.53) |

**`win_turn` changed on 0 / 14 hands.** Even as a prune the fan is redundant in practice — which is the
argument that the exact source model costs nothing at all.

The aggregate understates what matters. The distribution is **bimodal**, the Dragonstorm shape exactly:

```
41.53x   18.0s -> 0.4s    Garth One-Eye; Faeburrow Elder; Bloom Tender; Birds of Paradise x4
12.65x   11.1s -> 0.9s    Godless Shrine; Deathrite Shaman; Breeding Pool; ...   (non-Garth opener!)
 1.92x   23.8s -> 12.4s
 1.78x   12.6s -> 7.1s
 ...rest 1.0-1.6x
```

Two hands collapse by 41.5x and 12.7x; the rest move modestly. **Wall (1.60x) exceeds odometer (1.46x)
and the atom collapses 41x against a 2.8x odometer drop — cost is superlinear in group size**, which is
why the tail collapses so much harder than the mean. The tail is where this deck's generation budget
goes: the stopped run put **30% of all compute (28.4 of 95 core-hours) into rollouts ≥30 s**.

Note the 12.65x hand is a **non-Garth opener** — Garth is drawn mid-rollout, so the effect tracks
*a Lotus existing on board*, not the opening hand.

## 4. Status

**AGREED SCOPE (USER, 2026-08-21), to build after the overnight rebaseline:**

| item | decision |
|---|---|
| §9 — Treasures spent before creatures | **BUILD**, and unconditionally (the "almost" and the override are both gone) |
| §2a — model the lump as a payment SOURCE, solved in the mana evaluation instead of enumerated as branching `SacForMana` actions | **BUILD** — this is what gets Treasures out of the odometer |
| §10 — treasure-reserve override hook (Gold Rush) | **REJECTED, do not build** — see the ⛔ block in §10 |

### Why §2a is smaller than §2a-bis implies — and why §9 is not separate work (2026-08-21)

USER: *"Treasures are tappable permanents."* Correct, and it corrects an agent claim made in the
same conversation that the payment solver "only sees tappable permanents, so a Treasure never
enters it". That reasoning was wrong. `TapForCostSharedOnce` iterates **every untapped controlled
permanent** and keeps those whose `produces` list covers the needed colour (`pay_produces` ->
`makes`). A Treasure is untapped and tappable and passes the loop fine.

It is excluded for a much narrower reason: **its `parameters` carry only `sac_for_mana_amount: 1`
and no `produces` entry at all**, so `pay_produces()` is empty, `makes` is false, and the scan
`continue`s. Its own `cards.json` oracle note says so outright — *"Modeled via the Lotus-Bloom
sac-for-mana machinery: sac_for_mana_amount=1, colour searched (chosen_float_color variants)."*
The tap is real in the rules; the ENGINE routes the whole ability through the plan enumerator
instead of the payment path.

So the work is a re-route, not a new concept:

1. Give the sac sources a `produces` (any colour for a Treasure) plus a marker that paying with
   them also costs a SACRIFICE, so `usable()` / `pay_produces()` admit them and `tap_source`
   sacrifices rather than merely tapping.
2. `ManaSourceRank` then orders them like every other source — and **§9 is literally their rank
   number**, ahead of the mana creatures. No separate ordering machinery, no new hook. This is why
   §9 cannot land before §2a: until the Treasure is a payment source there is no ordering step for
   it to be ordered *in*, because cracking it is a SEARCHED BRANCH rather than a payment choice.
3. Delete the `SacForMana` colour fan for `sac_for_mana_amount == 1`, which is what removes the
   `3^9 = 19,683` odometer blowup on a 9-Treasure Mirrorwing board.

**This also explains §8's measurement instead of leaving it a curiosity.** The action-level fold
failed on Treasures because it fixes a colour per source BEFORE total demand is known, while the
enumerated fan could coordinate across nine sources. A payment solver chooses the colour per PIP as
it pays, so it gets that coordination for free — the solver is the right home for precisely the
case the fold could not handle.

**The Lotus is NOT the easy half.** At `amount = 3` one sacrifice yields three mana at once, so it
is a lump, not a per-tap source, and it still needs the flow matcher's third shape (§2a-bis). The
Treasure at grain 1 is the part that drops straight into the existing tap path.

Audit of the shipped engine, 2026-08-21 (all three confirmed by reading the code, not the doc):
Treasures are **still a branching decision** (one `SacForMana` action per candidate colour per
source, each its own odometer group — the emitter's own comment prices a 9-Treasure Mirrorwing board
at `3^9 = 19,683` states); there is **no treasure-vs-creature spend ordering** anywhere
(`ManaSourceRank` only ranks TAPPED sources, and a Treasure never reaches it); and there is **no
treasure reserve** (`FungibleSacSourceCap` names Gold Rush only to justify DROPPING duplicates).
What is already right: `FirstUnpayablePos` credits `creates_treasures` as wild, base count only — a
deliberate under-credit in the safe direction.

* `MTG_SAC_COLOR_CAP` is a **temporary sizing instrument** and should be deleted once this record is
  signed off (coding-conventions rule 5). It is default-off and byte-identical.
* The source model + lump reservation is **not built**. Order of work if taken: (a) the one-colour-lump
  shape in `BuildFlow`, (b) delete the `SacForMana` colour fan, (c) extend the whole-turn reserve to
  lumps in the doctrine's priority order, (d) retire the two Dragonstorm provider hooks, (e) validate on
  Dragonstorm (where the heuristic it replaces was worth 25–180x) as well as FiveColour.
* Being exact rather than a prune, it should carry **no** quality cost — but it changes payment, so it
  needs the full A/B ladder, not a byte-identity check.


**Why that primitive is safe TODAY and a trap TOMORROW (checked 2026-08-21).** A scan of the whole
card pool: `{C}` appears in **12 entries, all on the PRODUCTION side** ("{T}: Add {C}" — Cavern of
Souls, Sliver Hive, Mutavault, Reliquary Tower, Cascade Bluffs, …) and in **ZERO costs**. `cost.colorless`
is dead code across every implemented deck, so borrowing `Color::Colorless` for the Lotus's two generic
units cannot currently mis-pay anything. USER, 2026-08-21: *"I'm not sure we have even added any
'colourless-specific' mana at this point."* Correct.

But the trap has a name and it is already in `decks/`: **implementing `EldraziDisplacerFlicker` is what
introduces the first `{C}` COSTS** (Eldrazi Displacer's `{1}{C}` ability, Thought-Knot Seer's `{3}{C}`).
None of those cards are in `cards.json` yet — the deck is a decklist awaiting implementation — so the
day it is implemented, a Lotus modelled as colourless mana would illegally pay a `{C}` pip. If shape A
reuses the `Color::Colorless` primitive, it MUST carry a generic-only marker (or an explicit assert
that `cost.colorless == 0`) rather than silently relying on there being no `{C}` demand.

## 5. The class splits by AMOUNT — and half of it needs no new machinery (2026-08-21)

The whole lump class is exactly three cards (`sac_for_mana_amount`):

| card | amount | reachable from | shape needed |
|---|---|---|---|
| Treasure Token | **1** | Jared Carthalion -6 (FiveColour); Gold Rush (Mirrorwing) | **`add(mask, 1)` — ALREADY EXISTS** |
| Black Lotus | 3 | Garth One-Eye conjure (FiveColour) | shape A/B (new) |
| Lotus Bloom | 3 | decklist (Dragonstorm) | shape A/B (new) |

**A Treasure is ONE mana of any colour, which is precisely the existing free-choice shape.** For
`amount == 1`, "one mana of any one colour" IS `wild` — so the subset math's `credit.wild +=
ritual_float` is not merely indistinguishable across the five variants (as it is for the Lotus), it is
**exactly correct**. The variants differ only in what is floated at apply. The per-colour fan is
therefore pure redundancy with no modelling justification whatsoever, and unlike the Lotus there is no
soundness question attached.

**The win is 6 options -> 2, not 6 -> 0.** Collapse the COLOUR fan (5 colours + don't-use = 6) to the
sac/don't-sac bit, with the colour resolved by the payment solver from demand. On the documented
Mirrorwing board of 9 untapped Treasures: `3^9 = 19,683` -> `2^9 = 512` at the deck's real colour
count (`6^9 = 10,077,696` -> 512 under the unpruned five, `claude-play-unprune-blowup.md`).

**Two things this is NOT (correcting an earlier overstatement in this doc's drafting):**

1. *Not free.* The flow SHAPE needs nothing new, but wiring a **tap-and-SACRIFICE** source into the
   payment solver is real work: the solver taps sources, and a sacrifice is a state change (the
   permanent leaves the battlefield), not a tap. That plumbing is the actual cost of this half.
2. *Not fully hideable.* The sac decision must stay in the search, because `Gold Rush` pumps
   `+2/+2 for each Treasure you control` — on Mirrorwing the treasure COUNT is load-bearing, so
   "should I sac this one" is a genuine plan-level choice. Only the COLOUR is irrelevant to Gold Rush,
   which is exactly the part being deleted.

No `{C}` exposure either — the mask is the five colours, so a Treasure can never pay a colourless pip.

Only **amount >= 2** needs the new one-colour-lump shape, i.e. Black Lotus and Lotus Bloom.

### The `{C}` guardrail is NOT on the critical path (USER, 2026-08-21)

The colourless exposure exists only in shape A's shortcut of modelling the surplus units as
`Color::Colorless`, which applies only to the amount>=2 lumps. USER: Displacer's `{C}` costs *"will
eventually be part of the project, though not in conjunction with lotuses"* — and that is right: Black
Lotus is Garth-conjure-only (FiveColour), Lotus Bloom is Dragonstorm, and neither will share a deck
with an Eldrazi shell. Treasure, the one lump that IS freely mixable with such a deck, needs no
colourless modelling at all.

So this is a tripwire, not a design constraint: keep a one-line assert (`cost.colorless == 0` on the
shape-A path) because the guarantee is a DECK-COMPOSITION invariant, and deck composition is the thing
that changes. Do not complicate the design for it.

### FiveColour attribution is unaffected

Treasures reach a FiveColour board only through Jared Carthalion's **-6 ultimate** (loyalty starts at
5, so two +1 activations first) — rarely reached in a goldfish that wins around turn 5. Consistent
with `Treasure Token` never appearing in the `MTG_BRANCH_STATS` driver table for these hands. The
Black Lotus attribution in §3 stands.

## 6. SCOPE: permanents only — Apex of Power is explicitly OUT (USER, 2026-08-21)

`ChosenFloatColorCandidates` has two callers: the `SacForMana` fan (~5480) and **Apex of Power's**
per-colour cast variants (~4822, `impulse_float_amount: 10`, *"add ten mana of any one color"*).

**Apex is out of scope.** USER: *"I would leave Apex alone for now, since it is a spell, not a normal
mana source."* That is the right line, not merely a deferral: Apex is a SPELL (`{7}{R}{R}{R}`) whose
float is a one-time resolution effect. There is no permanent on the battlefield for the payment solver
to tap or sacrifice, so the source model does not apply to it at all — its colour is a cast-time
choice, not a payment one. (Its float is also deliberately NOT credited as `ritual_float`; it
materialises only at resolution.)

Consequences:

* **`ChosenFloatColorCandidates` is NOT deleted.** It loses its `SacForMana` caller and continues to
  serve Apex.
* **Only ONE Dragonstorm hook retires.** `RestrictSacColorsToHasteAndRed` is the Lotus Bloom /
  `SacForMana` hook and goes with the fan. **`ImpulseFloatColorRedOnly` is Apex's and STAYS.** (An
  earlier draft of this doc said both would retire — wrong.)
* Scope is exactly the permanents carrying `sac_for_mana_amount`: **Treasure Token (1), Black Lotus
  (3), Lotus Bloom (3)**. Nothing else in the pool.

## 7. BUILT + MEASURED: the action-level fold (`MTG_SAC_COLOR_FOLD`, 2026-08-21)

**Staged first cut, deliberately NOT the source model.** The measured FiveColour win came from the
LOTUS fan, so a treasure-only first step would have won on Mirrorwing and left FiveColour tractability
— the reason this arc exists — untouched. So the fan is collapsed at the ACTION level for both amounts,
which captures the win with NO payment-solver surgery. The source model (and with it the reservation
doctrine in §2b) remains the follow-on.

**What it does.** `SacForMana` emits ONE colour-agnostic action (empty `chosen_float_color`) instead of
one per candidate colour. The colour is resolved at apply by `TurnSolver::SacFloatColorFor`, from the
plan's own remaining coloured demand, scored `min(demand[c], amount)` over the SAME candidate set the
enumerator would have fanned (so a provider collapse still binds). Non-empty `chosen_float_color` is
returned untouched, so legacy and recorded/replayed plans are byte-identical. One shared static on
`TurnSolver`, called by all five apply sites (3 in AIEngine, 2 in TurnSolver) -> executor/rollout
lockstep by construction.

**Rules note:** "{T}, Sacrifice: Add one mana of any color" is a MANA ABILITY (CR 605.1a) — no stack,
and activatable during cost payment (CR 601.2g). Resolving its colour from the demand it is paying is
the rules-faithful model; the enumerator guessing across N branches never was.

### Byte-identity gate (coding-conventions A3 pattern)

| arm | avg, 120 games seed 1001 d2/b1 |
|---|---|
| clean env | **5.0333** (= pre-change constant) |
| `MTG_SAC_COLOR_FOLD=0` | **5.0333** |
| `MTG_SAC_COLOR_FOLD=1` | **5.0333** (metric-inert on the proxy; wall 42.6 s -> 40.5 s) |

### Effect on the tail — the fold captures ~99% of the sized ceiling

14 matched slow hands, 2 passes, per-hand minima:

| set | cap=1 CEILING (§3) | **fold, MEASURED** | odometer |
|---|---|---|---|
| Garth openers (n=8) | 1.60x | **1.59x** | 1.46x |
| non-Garth (n=6) | 1.19x | **1.19x** | 1.15x |
| **all (n=14)** | 1.44x | **1.43x** | 1.36x |
| worst atom | 41.5x | **38.7x** (17.8 s -> 0.5 s) | |
| second atom | 12.7x | **14.9x** (11.3 s -> 0.8 s) | |

**`win_turn` changed on 0 / 14 hands.** The fold reaches the prune's ceiling WITHOUT being a prune: it
denies no colour, it picks one from demand. That is why it costs nothing on the metric where the cap
was a quality prune that merely happened not to bite.

### NOT YET ADOPTED — two gates

1. **Default is OFF.** Flipping it to default-ON is an adoption: it changes the plan space on every deck
   with a sac source (FiveColour, Dragonstorm, Mirrorwing), so it needs the regression suite + a GT
   rebaseline, not the byte-identity check above. It is metric-inert everywhere measured so far.
2. **`RolloutCfg` does NOT stamp this flag.** Running a generation with `MTG_SAC_COLOR_FOLD=1` in the
   ENV would produce sidecars that pool with unfolded ones as if identical — precisely the trap
   `fivecolour-mullgen-labeller-sweep.md` §4 warns about for the V-arm. **Do not launch a gen behind the
   env flag.** Either adopt it as the default (one engine behaviour, nothing to stamp) or stamp it.

## 8. ADOPTED, and MEASUREMENT INVERTED THE PLAN (2026-08-21)

`MTG_SAC_COLOR_FOLD` is **default ON (=0 reverts)**, **scoped to `sac_for_mana_amount >= 2`**.

### The inversion

§5 argued the TREASURE half was the safe one to land first ("no soundness question", `wild` exact at
amount 1) and the Lotus half was the risky one. **The regression suite says the opposite.** Folding
every sac source:

| | smoke (s1001) | regression (s2002/s3003) |
|---|---|---|
| mirrorwing d3 | +0.0266 | +0.0150 / +0.0150 |
| mirrorwing d5 | +0.0533 -> 0 after the sequential fix | +0.0300 / +0.0500 |
| dragonstorm, fivecolour | neutral-to-better | neutral-to-better |
| searched totals | slower=5 faster=1 | **slower=17 faster=3**, net **+0.102** |

4/4 mirrorwing searched keys worse across two independent seeds — systematic, not variance.
**Mirrorwing is the multi-Treasure deck** (up to 9 in play): resolving each one-mana source's colour by
a sequential demand-greedy cannot match what the enumerated fan finds across many sources. A single
3-mana Lotus has no combinatorial partner to coordinate with, which is exactly where the greedy is
sound. Scoping to amount>=2 restored every mirrorwing key to byte-identical.

Two defects were found and fixed by measurement along the way, both mine:

1. **O(library) scan at apply.** The first cut called `ChosenFloatColorCandidates` inside
   `SacFloatColorFor`, which walks hand + LIBRARY + graveyard + battlefield — once per sac action per
   PLAN APPLICATION. A Garth slow hand went from 12.5 s to **>166 s (13x SLOWER)**. Removed: `demand[c]
   > 0` already implies `c` is in the candidate set, so the intersection could never remove anything.
2. **Independent per-source argmax.** Each sac action resolved against the same undiminished demand, so
   every source picked the same colour. Now assignment is SEQUENTIAL over the plan's sac actions,
   charging each pick against the remaining demand (unfolded siblings charge their enumerated colour).
   Fixed mirrorwing d5 outright (+0.0533 -> 0); did not rescue d3, which is what forced the scope.

### Adoption evidence (scoped)

| | smoke | regression |
|---|---|---|
| result | 32 passed / 4 failed | 56 passed / 4 failed |
| dragonstorm d0 | **-0.0040 better** | **-0.0030 better** |
| dragonstorm d3 / d5 | 0 (digest-only) | 0 (digest-only) |
| fivecolour d0 | 0 (digest-only) | **-0.0010 better** |
| mirrorwing (all) | **PASS** (byte-identical) | **PASS** (byte-identical) |
| searched | — | **slower=0, faster=0** |
| d0 | — | **slower=0, faster=2** |

**Nothing worse anywhere; net -0.0040.** FiveColour gen-config perf retained at **1.24x** on the
120-game d2/b1 proxy (14.24 s -> 11.50 s, avg identical 5.1250) and 1.59x on the Garth slow-hand set.

GT rebaselined and accepted for **smoke + regression**; the diff touched exactly the 8 expected keys.
**Overnight GT is NOT yet rebaselined** — those dragonstorm/fivecolour keys will FAIL on the next
overnight run and need accepting then.

### Consequences for the remaining design

* **The treasure half belongs in the SOURCE model, not the action fold.** USER, 2026-08-21: *"when we
  have that many treasures it should probably be done very cheaply too. There isn't all that much to
  calculate, just pay the pip costs, use the rest for colorless and call it a day."* That is precisely
  what a payment solver does natively and what an action-level fold cannot express — it has to commit a
  colour per source before knowing the assignment. The measurement is the evidence: direct assignment
  needs the solver. §5's ordering therefore reverses — Lotus folds now, **Treasures wait for §2a**.
* **Mirrorwing INVERTS the reserve doctrine.** USER: *"Especially in Mirrorwing Treasures should be used
  before creatures."* §2b ranks one-shots as reserved hardest, but Mirrorwing's creatures are Zada /
  Mirrorwing COPY TARGETS and the scaling body for Fists of Flame / Ancestral Anger — worth far more
  than their mana — so the Treasure should be spent to keep the creature untapped. The doctrine is not
  "reserve the one-shot hardest" unconditionally; it is weighed against the creature's NON-MANA value,
  which is deck-shaped. Record this before implementing §2b's ordering.

## 9. Reserve doctrine, refined: TREASURES BEFORE CREATURES (USER, 2026-08-21)

USER: *"I think it's okay if treasures go before creatures almost in general, but we should have a
heuristic way to override that."* This supersedes §2b's flat "reserve the one-shot hardest" for the
treasure/creature pair.

> **UPDATE, later the same day — the "almost" is gone and so is the override.** The user rejected
> the override class outright (see the ⛔ block in §10): reserving Treasures means tapping the mana
> creatures, and in Mirrorwing those are the pump's own recipients, so the reserve disarms the body
> it is preserving the pump for. **Treasures are spent before creatures unconditionally.** This is
> the rule to implement — a flat ordering, no trigger scan, no per-deck hook.

### The ordering principle is EXACTNESS, not one-shot-ness (USER, 2026-08-21)

> *"part of the reason to put treasures before creatures and lotuses after is that treasures can be
> used exactly."*

This is the axis that actually orders the list, and it is separate from the alternative-use argument
below. Rank by **how much of the source's output the turn can consume**:

| source | grain | waste when spent | rank |
|---|---|---|---|
| **Treasure** | 1 mana, any colour | **none** -- you only tap it when you need >= 1 more | spend FIRST |
| **mana creature** | 1 per tap (N for a domain source) | none, but forfeits its non-mana use this turn | next |
| **Storage land** | ALL counters burst on tap | surplus counters, permanently | held |
| **Lotus** | 3 in one lump | 2 destroyed if used to pay 1 | spend LAST |

A Treasure is one-shot, yet it outranks a creature for spending — because **one-shot-ness only costs
you something when the lump exceeds the need**. At grain 1 there is no surplus to lose, ever.

This also unifies the storage-vs-Lotus case in §2b item 3 rather than leaving it a special case: the
Lotus was preferred there because it was the EXACT fit for a 3-cost while the storage land could not
cover it. Same rule both times — use the source the turn can consume exactly.

Two distinct kinds of waste are worth keeping separate, since they explain the ranking:

* **Consumed-resource waste** (Lotus, storage/depletion counters): spending destroys value
  permanently. This is what reservation is defending.
* **Tap waste** (a domain dork tapped for five colours when one was needed): wasteful this turn, but
  the dork untaps next turn, so nothing is permanently destroyed. Cheaper to get wrong.

### Why it generalises (the second axis: alternative use)

Reservation is about preserving ALTERNATIVE USE, and the two sources' alternative uses live on
different clocks:

* A **Treasure** held to a later turn is still worth exactly one mana. Its value is time-invariant and
  it has no non-mana use at all.
* A **mana creature** tapped this turn loses its attack/block, its copy-target role, its body for
  pumps — all of which are worth something THIS turn and are gone the moment it taps.

So the rule is **spend the source whose alternative use is lowest NOW**, and that is almost always the
Treasure. This is the same argument the engine already makes for mana rocks
(`BatchPrepayMainCasts`: *"A mana ROCK is deliberately NOT reserved: it has no other use, so holding it
would only make the solve fail and cost a second backtrack"*) — a Treasure is that argument plus
one-shot-ness, and one-shot-ness does not decay, so it does not outrank a creature's this-turn use.

Note this INVERTS §2b's ordering for this pair, and §2b should be read as governing the Lotus /
Storage / Depletion lumps, not as a blanket one-shot rule.

> ## ⛔ SUPERSEDED (USER, 2026-08-21): THE GOLD RUSH OVERRIDE IS REJECTED — DO NOT BUILD IT
>
> Everything from here to the end of §10 described a treasure-RESERVE override. The user rejected
> the whole class on a board argument this doc had missed, and the argument is decisive:
>
> *"I'm not really worried about a Gold Rush override as I wouldn't generally do that anyway. 2x
> Gold Rush is difficult not to win with in the first place, so tapping dorks (which receive the
> pumps) to do this is a bad idea in my opinion."*
>
> **The override is self-defeating.** Reserving Treasures for the count means paying with the mana
> CREATURES instead — and in Mirrorwing those creatures are the pump's own RECIPIENTS. A tapped
> creature cannot attack, so the preserved +2/+2 is bought by disarming the body it was preserved
> for. The reserve buys pump and spends the thing the pump exists to enable.
>
> The second premise was wrong too: a turn with two Gold Rushes is already overwhelmingly winning,
> so it is not a turn whose margin needs defending — optimising it is optimising a game that is
> already over.
>
> **Consequence:** §9's base rule stands UNCONDITIONALLY. Treasures are spent before creatures,
> full stop; there is no override hook, no `pump_per_treasure_power` scan, no per-deck reserve
> hook. §9 gets *simpler* than it was written, not more complex, and §2a no longer has a
> precondition to clear before it can fold Treasures.
>
> Kept below unedited as the record of what was considered and why it lost.

### The override trigger, and it is narrow

**A treasure-COUNT payoff castable this turn.** Scanned the whole card pool: exactly ONE card counts
Treasures — **`Gold Rush`** (`pump_per_treasure_power` / `pump_per_treasure_tough`: *"up to one target
creature gets +2/+2 for each Treasure you control"*), and it is in Mirrorwing. Sacrificing Treasures
for mana BEFORE casting it shrinks the pump, so on such a turn the Treasures must be held and the
creature tapped instead.

Detect it from PARAMS, never a card name: `pump_per_treasure_power > 0` (or any future
count-Treasures parameter) on a spell castable this turn. That keeps it a class rule, and it is the
same shape as the existing `RestrictSacColorsToHasteAndRed` "is an off-colour worth it THIS turn"
scan.

Second override class, cheap and worth including: **the creature has no alternative use this turn** —
summoning-sick with no haste, zero power, or no profitable attack — in which case tapping it is free
and the Treasure should be kept instead. A VIGILANT mana creature is the sub-case already covered
elsewhere: it can attack and still tap post-combat, so in main 2 tapping it costs nothing (§2c, the
Faeburrow doctrine).

### Why this matters beyond tap order

Gold Rush is independent evidence that **Mirrorwing's Treasures are not fungible mana** — their count
is load-bearing. That is a second reason (alongside the multi-source colour-assignment result in §8)
why Treasures resist the treatment that suits a Lotus, and it should be settled BEFORE the source
model (§2a) folds them: a solver that freely spends Treasures for mana will silently cost Gold Rush
its pump unless this override exists first.

## 10. The treasure-reserve OVERRIDE must be a general hook, not the Gold Rush special case

USER, 2026-08-21: *"it is true that Treasures can sometimes be something you want to reserve and there
is a use-case for doing so in some decks. So, it needs to be something we can override."*

So §9's ordering (Treasures spent first) is the BASE rule and must be overridable per deck, via a
`DecisionProvider` hook — the repo's standing pattern for deck-shaped judgement (adopt in the archetype
provider, never the root). The hook should be **state-driven where it can be**, so it fires on a card
CLASS rather than a deck name.

### The override classes, and which are live today

| class | trigger | status |
|---|---|---|
| **Treasure-COUNT payoff** | `pump_per_treasure_power > 0` on a spell castable this turn | **LIVE** — `Gold Rush`, in Mirrorwing (§9) |
| **Artifact-COUNT payoff** | a permanent/spell whose value scales with artifacts you control | **LATENT** — cards exist, no deck co-locates them with Treasures yet |
| **Colour-fixing reserve** | the Treasure is the ONLY source of a colour a later cast needs | **LIVE in principle** on any poorly-fixed deck |

Scanned the pool for the artifact-count class:

* **`All That Glitters`** — *"+1/+1 for each artifact and/or enchantment you control"*. A Treasure is an
  artifact, so sacrificing one shrinks the buff.
* **`Puresteel Paladin`** — Metalcraft (3+ artifacts) grants equip {0}. Sacrificing a Treasure can drop
  below three and cost free equips.

Neither of their decks (Auras / KittyEquipment) currently produces Treasures — Treasures come from
`Gold Rush` (Mirrorwing), `Jared Carthalion` -6 (FiveColour) and `Magma Opus`. So this class is a
LATENT trap of the same shape as the `{C}`/Eldrazi one in §5: harmless until a deck combines them, and
silent when it does. Implement the hook so the class is expressible now, even though only Gold Rush
fires today.

The colour-fixing case is the one with no card-parameter tell: it is a board question ("would spending
this Treasure for generic strand a coloured pip I still need?"), and is exactly the question the
payment solver is positioned to answer once §2a lands — another reason the Treasure half belongs in the
source model rather than the action fold.

## 11. The fold's colour greedy scored DEMAND when it should have scored SCARCITY (2026-08-21)

§8 adopted `MTG_SAC_COLOR_FOLD` on train seeds (smoke 32/4, regression 56/4, *"nothing worse
anywhere"*). **Held-out overnight then caught three reproducible Dragonstorm losses** — s6006
gi118 and gi148, T5 → T6, at BOTH d3 and d5, with identical kept hands and draws, and
`MTG_SAC_COLOR_FOLD=0` restored T5 on all three. This is the value of held-out seeds stated as
plainly as it gets: smoke and regression were both clean.

### The defect

`SacFloatColorFor` ranked the lump's colour by how much of the plan's demand it covers:

```cpp
const int score = std::min(remaining[i], amt);   // RAW demand
```

That is wrong whenever the rest of the board already covers that colour. Dragonstorm is exactly
that case: red is abundant there — Mountain, Unclaimed Territory, and three rituals that each
float RED **by design** (`ritual_float_color: "R"`, chosen precisely so red cannot pay the
off-colour {B}/{G} dragon pips). So the argmax piled Lotus Bloom onto red, the one colour already
paid for, instead of the off-colour pip nothing else could supply. Trace on the failing game:
**4940 red picks vs 437 black**.

### Why it cost whole turns

The unfolded fan emits one action per colour, so the search scores each and takes a **max over
colours**. The fold replaces that max with a single greedy pick. When the greedy picks the useless
colour, every Lotus line is valued below what the fan achieved and the search **steers away from
those lines entirely**. That is why the diff appears at T2 with **no Lotus anywhere in either
line** — the Lotus is never sacrificed in the committed line of any of the three games. The damage
was entirely in PROJECTION values.

### The fix — scarcity, not demand

Charge the demand against what is already available and rank by the SHORTFALL:

```cpp
const ManaPool avail = AvailableManaPool(state);
remaining[i] = std::max(0, demand[i] - supply[i]);
```

Same scarcity-first doctrine `ManaSourceRank` already applies to tap order (*"SPEND the least
flexible first so the flexible sources stay available"*), applied to the lump's colour. **Global —
no per-deck hook.** A `FoldsSacColorVariants()` provider hook was built first and REJECTED by the
user: *"I don't want to be fiddling with this on a deck to deck basis. It should be on everywhere
or off everywhere."* That was the right call — the deck-scoped version would have hidden a real
defect behind a config.

Result: all three Dragonstorm games back to T5 with the fold ON. Overnight searched-slower **6 → 3**,
and all three survivors are benign (two `churn`, one with divergent kept hands = different physical
games). **Zero same-draws regressions remain.** FiveColour is byte-identical at smoke — Garth
supplies all five colours, so shortfall-ranking and demand-ranking coincide there — and keeps its
speed win (1.18x, vs 1.24x before the added `AvailableManaPool` walk).

### Two candidate fixes that were built, measured, and DROPPED

* **`colors.size() > 1` guard** ("only fold where there is a fan to collapse"). Theory: Lotus Bloom
  is single-variant, so folding changed semantics for no gain. **Did not fix it** — Dragonstorm's
  fan is genuinely multi-colour. Reverted.
* **Projection lockstep in `FirstUnpayablePos`.** A folded action carries no pinned colour and
  Lotus/Lotus Bloom set no `ritual_float_color`, so `col` lands empty and `AddColorToPool` credits
  the lump as **WILD** — modelling three INDEPENDENT any-colour mana when a Lotus adds three of ONE
  colour. That is genuinely unsound in the optimistic direction, and it is **still latent**. But
  resolving it through `SacFloatColorFor` did NOT restore the games and costs an O(acts²) call in a
  projection loop, so it was reverted rather than shipped unmeasured. **Recorded here as a known
  latent unsoundness**; §2a removes it for free, since a payment source is credited as it pays.

### What this says about §2a

The root cause is structural: a fold must commit the colour BEFORE seeing how the line pays, so any
heuristic is guessing where the fan searched. The scarcity rule is a much better guess, not a
solution. §2a ends the guessing — a payment SOURCE assigns colour per pip AS it pays, which is the
max the fan approximated, at none of the fan's enumeration cost. Retire this greedy when §2a lands.

Diagnostic kept: `MTG_SAC_COLOR_TRACE=1` prints each resolution as
`[sac] T<n> <card> amt=<n> -> <colour>  demand W.. U.. B.. R.. G..  (FOLDED|pinned)`.
It is what found this, and it is the tool for the next deck that folds a lump.
