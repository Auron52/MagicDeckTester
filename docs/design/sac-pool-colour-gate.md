# The sac-outlet pool's colour gate: 3 cards in 60 disabled a collapse on 100% of boards

> **SUPERSESSION WARNING (2026-10-01), read before adopting anything here.** The USER has redirected
> this at the better fix: *"I'm not worried about presumptively holding black. We should be able to
> just do it on demand. (i.e. sacrifice a saproling or tap a land)"* — which is
> `MTG_SAC_OUTLET_PAY` / `docs/design/sac-mana-outlet-as-deferred-source.md`. With that lever on,
> the mana outlet's searched actions are SUPPRESSED, so there is no float colour, no colour fan, no
> singleton gate and no speculative-black question — this whole document's narrowing becomes inert
> for Utopia Mycon. Its A/B is running now (`logs/victim_ab/pay_ab.json`).
>
> **UNCOMMITTED WORKING-TREE STATE:** `src/ai/TurnSolver.cpp` has `SacPoolTurnColorEnabled()` flipped
> to **default ON** (committed state is OFF). Deliberately left uncommitted pending that A/B: if the
> payment-source model adopts, REVERT the flip rather than ship two mechanisms for one problem. The
> flip is measurement-neutral for the running A/B because all three arms pin the flag per job.

**Status 2026-10-01: diagnosed, sized, and a first fix BUILT behind `MTG_SAC_POOL_TURN_COLOR`
(default OFF). It recovers ~56% of the lost opportunity. The remaining ~44% needs a different
change (§5).**

## 1. What went wrong, and why nothing reported it

`MTG_SAC_OUTLET_POOL` replaces the per-source POWERSET over N interchangeable sac-for-mana outlets
with a single COUNT axis. It was adopted 2026-09-23 at **2.29x** on the heaviest Fungus cell.

It has been **completely inert** on Fungus candidate-b ever since the list was revised.

The gate is in `CollectActions` (`src/ai/TurnSolver.cpp`):

```cpp
// THE COLOUR FAN MUST BE A SINGLETON. With several candidate colours the outcome
// depends on the colour MULTISET and not on the count alone ...
if (pool_cols.size() == 1)
```

and `pool_cols` for an any-colour outlet comes from `ChosenFloatColorCandidates`, which scans
**hand + LIBRARY + graveyard + battlefield**. That function answers *"could this colour ever be
wanted by this DECK"*.

Utopia Mycon's own card note predicted this exactly:

> "In this mono-green list that fan is the singleton {G}, so it costs exactly one action and no
> plan-space growth, **while staying honest if the deck ever splashes.**"

The deck splashed. Candidate-b's entire black component is **three cards out of sixty** —
1 Slimefoot, the Stowaway `{1}{B}{G}` and 2 Deathspore Thallid `{1}{B}` — against 37 copies with a
green pip. Because those three sit in the **library** for most of the game, the fan is `{G,B}` on
**every board, every turn, in every rollout**, and the singleton gate never passes.

**This was caused by the DECKLIST, not by any commit**, which is why no A/B, no digest and no gate
caught it: a pool going quiet is not an error, it is a silent fall-through to the per-source
powerset. It is also why Utopia Mycon · `SacForMana` is **44.0% of all candidate mass**
(1,021,086 of 2,321,995 in the `MTG_BF_CENSUS` attribution).

## 2. Sizing it before building anything

`MTG_SAC_POOL_PROBE` (default OFF, byte-identical when off) counts gate arrivals, how many are
blocked by the colour fan, and — by replaying **the same two tests the pooled path uses** — how many
of those would *otherwise* have pooled. That last number is the ceiling of any colour fix, measured
rather than guessed. 400 games, d3/b10, candidate-b:

```
gate reached = 4,425,133
blocked-by-colour = 4,425,133 (100.00%)
  of those WOULD-POOL = 1,658,433 (37.48% of reached)   avg family N = 2.12
  family sizes: N=2: 1,451,298   N=3: 207,135
  colour-fan width: always exactly 2  ({G,B})
```

**100.00%.** Not "often" — every single arrival.

### A correction to the headline number

This had been carried as "re-enable the 2.29x pool". **That figure does not transfer to this list,
and saying it would be overselling.** The 2.29x was measured where the family was size 8 covering
counts 1..8; here families are **N=2 (87.5%) and N=3 (12.5%)**, average 2.12. The collapse per
family is roughly `5^N -> |counts|+1`, i.e. ~25→3 at N=2 and ~125→4 at N=3 — real, but nowhere near
the N=8 case. Expect a modest win, not 2.29x.

## 3. The fix built: scope the fan to THIS TURN's sinks

`SacPoolTurnColorCandidates` asks the narrower question — *could the colour be wanted by a sink this
turn* — by scanning **hand + battlefield + graveyard and NOT the library**.

The argument: a floated colour **empties at end of turn**, so its only consumers are casts and
activations in this same turn. A colour with no pip demanded by anything reachable this turn can be
spent only against GENERIC, which the kept colour pays equally well. On that slice, dropping it is
dominance rather than preference.

Deliberately **scoped to the pool gate only** — the per-source path keeps the full fan, so Lotus
Bloom / Apex of Power and every non-pooling board are untouched.

### What it can lose, stated rather than glossed

A plan that floats `{B}` **speculatively** and then draws into the black card later in the same turn
(Psychotrope Thallid's draw). The float colour is committed when the plan is enumerated, while the
drawn card is still in the library. Narrow — one Psychotrope in the list, and the breakpoint
continuation *after* the draw re-enumerates with the card in hand, where this scan does see it — but
real. Hence **a heuristic narrowing at default OFF**, not a fold.

**No correctness hazard:** if a plan turns out to need the dropped colour, payment simply fails and
the plan is not emitted. Nothing is mis-executed.

**A pre-existing hole this inherits (does not introduce):** the demand scan reads each card's
mana_cost, so an activated ability whose *cost* carries a coloured pip is invisible to it.
`ChosenFloatColorCandidates` has the same hole. Inert on this deck — every Fungus sac/spore
activation costs no mana — but it would matter on a deck with coloured activation costs.

## 4. Effect on the gate

Same 400 games, d3/b10, probe on:

| | fan = deck-wide (shipped) | fan = this-turn (`MTG_SAC_POOL_TURN_COLOR=1`) |
|---|---|---|
| blocked by colour | **100.00%** | **43.96%** |
| still blocked AND would-pool | 37.48% of arrivals | 18.20% of arrivals |

So the narrowing recovers **~56%** of the blocked opportunity. The residual 44% is boards where a
black pip genuinely *is* in hand or on board, where the fan really is 2 and the count axis alone
cannot express the outcome.

Byte-identical with the flag off (digest `0f0baf5c772d76b0`, units `9528014`, both exact).

## 4b. What it buys

One pooled batch, 24 jobs, 5,200 games on candidate-b, paired blocks, two arms. `units` is the cost
metric because it is **deterministic** (the budget converts via `NODES_PER_VIRTUAL_MS`, it is not read
off the clock), so these numbers are immune to the shared box in a way wall clock is not.

| cell | games/arm | avg off | avg on | delta | units ratio |
|---|---|---|---|---|---|
| d0 | 2,000 | 6.3675 | 6.3675 | **+0.0000t** | n/a (no budget) |
| d3/b10 | 400 | 5.3150 | 5.3125 | **-0.0025t** | **0.97628x (-2.37%)** |
| d5/b20 | 200 | 5.3450 | 5.3500 | **+0.0050t** | **0.97161x (-2.84%)** |

Quality is flat — the d3 and d5 deltas are one game each, in opposite directions. Work falls
**2.4-2.8%**. At d0 the avg is identical to 4 dp while digests move in 2 of 4 blocks, i.e. the
narrowing fires and changes the line without changing the outcome.

### Held-out confirmation (fresh seeds, d3 90000+, d5 92000+)

| cell | games/arm | avg off | avg on | delta | units ratio | blocks cheaper |
|---|---|---|---|---|---|---|
| d3/b10 | 800 | 5.3325 | 5.3338 | **+0.0013t** | **0.92504x (-7.50%)** | **4 of 4** |
| d5/b20 | 600 | 5.3333 | 5.3333 | **+0.0000t** | **0.97665x (-2.34%)** | **4 of 4** |

d5 is **identical in all four blocks** to 4 dp over 600 games. d3 moves one game in 800. And the cost
win is not an outlier: **every one of the 8 blocks is cheaper**, per-block 0.8809x-0.9863x. The
held-out d3 figure (-7.50%) is larger than the training one (-2.37%), so the effect is if anything
under-stated above, not over-fitted.

## 4c. Verdict and the adoption call

Quality flat on 8,000 games across three depths; deterministic work down **2.3-7.5%** with **8 of 8
held-out blocks cheaper**. That matches the standard the comparable narrowings in this tree were
adopted on (`MTG_FADE_K_WINDOW`: "-0.004t at both depths, 2.04x/1.50x"; `MTG_SPORE_POP_ALL`:
"quality FLAT ... every labelling seed faster").

**Left at DEFAULT OFF, deliberately, and this is the recommendation to flip rather than a verdict
against it.** Three reasons it is the user's call and not an agent's: it is a **lossy** narrowing
(§3), not a sound fold, so the "collapse unconditionally" directive explicitly does *not* cover it;
flipping it MOVES Fungus GT and so needs a rebaseline sequenced against the other agent's in-flight
pushes; and it is a one-line change to `SacPoolTurnColorEnabled()` once approved.

## 5. The remaining 44% needs the COLOUR COMPOSITION axis, not a narrowing

For Utopia Mycon the ability is *"Sacrifice a Saproling: Add **one** mana of any color"* —
`amount == 1`, and **each activation chooses its colour independently**. So a pooled count-`c` action
floats `c` mana whose colours are independently choosable, and the honest typed representation is a
colour **multiset**, not a single letter.

That makes the sound (identity-preserving, not narrowing) form of the pool:

> emit one action per (count `c`, colour composition of `c`)

For `|cols| = 2` that is `c+1` compositions per count, so ~`maxc²/2` positions where the per-source
powerset costs `5^N`: at N=2, ~25 → ~5; at N=3, ~125 → ~9. Still a large collapse, and it covers the
mixed assignments the narrowing drops.

**Why it was not built here:** `Action` carries one `chosen_float_color` and one `ritual_float`, so a
composition needs a new field (or a multi-letter encoding of the existing `std::string`) plus matching
work in `AddChosenColorFloat`, the payment credit and the dedup signature. That is a structural
change across four sites, not a gate tweak.

**Do NOT reach for a wild float instead.** It is forbidden for a documented reason, in two places:
*"NOT wild -- wild could illegally pay a multicolour mix"* (`CardDatabase.h`) and *"never a wild
token in the pool, per the pools-hold-typed-mana-only doctrine, since floating mana is a truth claim
later payments read"* (Utopia Mycon's note). For an `amount == 1` outlet a wild float would in fact
be legal per-activation — but it would still violate the pool doctrine, so the composition axis is
the route, not wild.

## 6. Also affected

The cross-source axis is not Mycon-only. Every `distinct_physical_sources > 1` outlet in this list
reaches the same gate: **Saproling Burst, Vitaspore Thallid, Deathspore Thallid**.
