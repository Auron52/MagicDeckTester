# Auto-resolved target picks the search cannot see past

**Status:** audit complete, nothing built. Measurement gated on core availability.
**Origin:** generalised from BUG 7 (Oko `elk_transform`), which the USER found.

## The lens

The Oko bug was not "the heuristic ranks the targets badly". It was:

> The engine could not express the line at all, so the card *looked* useless,
> and a provider had been hand-tuned around that false appearance.

`elk_transform` was hardcoded to own Food tokens on the written premise that
"Elking a real creature is strictly worse". That premise is false — CR 302.6 ties
summoning sickness to how long you have **controlled** a permanent, not to how long
it has been a creature, so Elking an artifact or a spare dork yields a 3/3 that
attacks immediately. Because the line was inexpressible, `FiveColourProvider`
name-keyed Oko to Main2, which then looked like a search tie-break problem in the
6->7 win-turn family.

**The rule this produces:** before attributing a family to a tie-break, to churn, or
to "architectural" causes, ask *can the engine even express the line?* Two of this
arc's real bugs were sitting behind a wrong answer to that question.

## The class

Applying the lens to every `[bracket note]` in `cards.json` that admits a choice
narrowing (`AUTO-RESOLVED`, `NARROWED`, `NOT OFFERED`, `NOT ENUMERATED`) leaves
**five** live instances. All share one shape: a *target* is chosen by a fixed rule
inside the resolver, so the search sees exactly one line and cannot price the
alternatives.

(Cards flagged only `INERT` were excluded — those are riders that genuinely do
nothing against a passive, permanent-less, hand-less opponent. That is a faithful
modelling decision, not a narrowing.)

### 1. Nicol Bolas +3 — `destroy_own_noncreature` (FiveColour) — HIGHEST SUSPICION

Pick order in `SpellEffects.h`: **first own LAND in battlefield order**, else first
own noncreature non-walker.

Two defects, and the second is not merely a ranking complaint:

* **The comment says "most replaceable" but selects a land.** A land is the *least*
  replaceable noncreature permanent in a five-colour manabase. FiveColour puts Food
  tokens (Oko +2) and Treasure tokens (Jared -6) on the battlefield; a spent Food is
  far cheaper to destroy than a dual. The scan `break`s on the first land, so a
  token later in battlefield order is never reachable.
* **Among lands it takes the earliest-played**, which is typically the key fixer.
* **Consequence: Bolas is effectively a 6-mana blank.** His only other ability is
  -9, unreachable from a starting loyalty of 5 without two +3 activations. If +3 is
  correctly priced as "destroy your best land", the search declines it — so the
  ultimate is unreachable *by construction*, and a six-mana card does nothing. The
  inexpressible line is not a marginal upgrade; it is the card's whole plan.

### 2. Jared Carthalion -3 — `counters_up_to_two` (FiveColour)

Auto-resolves to the two highest-colour-count own creatures. The stated proof —
"max-total provably optimal vs a never-blocking opponent" — is correct **for total
power added**, and would be the whole story if the metric were board strength.

The metric is turn-to-win. The pick is blind to **summoning sickness**: a freshly
cast five-colour creature absorbs both activations for +5 power and **zero damage
this turn**, while a two-colour attacker that could have converted +2 into two
points of face damage is skipped. Whenever lethal is in reach this turn, max-total
is the wrong objective and the correct line is inexpressible.

Bounded fix shape: emit **two** candidate resolutions — max-total and
max-damage-this-turn (attack-eligible only) — and let the search price them. Two
branches, not `C(N,2)`.

### 3. Light-Paws, Emperor's Voice — aura fetch target (Auras)

Self-flagged in its own note: "the fetch target is a heuristic pick (highest power
contribution) -- disclosed as a narrowed target, **not yet a search decision**."
Honest, and the same shape as Oko.

### 4. Twinflame strive extras — `solo_target_trick` (Mirrorwing)

Extra targets resolve to **highest printed power**. Against no blockers that
maximises copied damage — but it is blind to **ETB value**: the note itself records
that a copied Goblin Instigator re-fires its ETB (CR 707.4). Copying a low-power
creature with a strong ETB can beat copying a high-power vanilla, and that line
cannot be reached.

(The same note's "zero-target casts are not offered" is *correct* pruning, not a
narrowing — CR 601.2c.)

### 5. Regrowth `return_target_from_graveyard` (Garth One-Eye only)

Auto-resolves to highest mana value. Highest MV is not the same as most useful: a
cheap spell castable this turn can beat an uncastable seven-drop. Rarest of the
five (reachable only off Garth), so lowest priority.

## Why nothing is built yet

The Oko fix was justified by a **concrete failing game** (the fivecolour 6->7
family), not by inspection. None of the five above has that evidence yet. The
honest next step is to measure how often each ability is *activated at all* in real
games before spending base-play risk on it — several of these cards are expensive
and late (Bolas is six mana in a deck that often wins around turn 6), so a
theoretically-inexpressible line may simply never come up.

**Blocked on:** free cores. Measuring requires a run, and doing it alongside another
batch risks CPU oversubscription, which is a known determinism hazard in this repo.

**Measurement plan:** one pooled `mtg --batch` over FiveColour + Auras + Mirrorwing
with game logs, then count activations of each ability from the logs (the loyalty
descriptions are already formatted in `TurnSolver.cpp`). Anything never activated is
documentation-only and should be left alone; anything activated with real frequency
earns a searched-target fix in the Oko mould.

## The trap any fix must avoid

Widening a candidate set **requires** the eval to distinguish the new candidates.
When Oko's targets were widened with a flat per-target eval, greedy d0 got
measurably **worse** (6.1850 -> 6.1940): the engine Elked a mana dork as readily as
a Food, and "loses all abilities" silently killed the mana. Charging the dork-ramp
cost fixed it (-> 6.1760). Expect the same failure mode for Bolas (destroying a land
vs a Food is a huge eval gap) and for Jared.
