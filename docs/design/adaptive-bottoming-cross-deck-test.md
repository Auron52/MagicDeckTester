# The adaptive-bottoming precision dial: deferred, and what to run instead

**Status: DEFERRED by the user, 2026-10-11.** Not a rejection and not a parking of the idea — a
re-ordering, plus a change in what the deliverable should be.

> **USER, 2026-10-11:** *"On the lossy change, I think that could be something to consider, but I
> would rather address it after all of the more lossless/dominance pruning changes are in.
> Realistically, it probably won't be necessary for this run, so the more useful part would be a test
> for the future to see how much this would help on a greater cross-section of decks. If we get lucky
> enough with the other optimizations we may even be able to run kitty both ways in the span of 36
> hours to have another real comparison?"*

Three separable instructions are in that paragraph, and they point at different work:

1. **Ordering.** The lossless / dominance prunes land first. The dial is not considered until they
   are in, because their saving changes whether the dial is needed at all.
2. **Scope.** The valuable artifact is not "should KittyEquipment v3 use it" — it is **how much this
   would help across a greater cross-section of decks**, as a standing result for future generations.
3. **The better experiment.** If the lossless work frees enough of the window, run the KittyEquipment
   v3 generation **both ways inside 36 h** and compare the real artifacts. That replaces a simulated
   regret with a measured one.

This document is the standing record so none of it depends on a session's memory.

---

## 1. What is already known, and the trap inside it

The lever is **adaptive bottoming** — the generator refines the decision-relevant bottoming sub-cells
toward a cap R while leaving clear cells at the floor — together with its two dials,
`MTG_KEEP_FLIP_EPS` (the refine-stop gate) and the cap R itself.

Measured **offline, with no rollouts**, by `MTG_KEEP_SIM_ADAPTIVE_BOTTOM=1`
(`src/analyzer/ExhaustiveKeep.cpp`, `RunAdaptiveBottomRegretSim`) on the **slivers_vial** banked raw:

| configuration | regret (turns) | sub-table rollouts saved | ⇒ off TOTAL generation |
|---|---|---|---|
| at kitty's own cap R=30 | **+0.0033 ± 0.0001t** | 61.1–34.0% | 31.7–17.6% |
| `flip_eps` 0.02 → 0.20 at R=30 | +0.0033 → +0.0031 (inside se) | 72.0–52.4% | 37.3–27.2% |

So at the cap this deck actually uses, adaptive bottoming costs about three thousandths of a turn and
saves between a sixth and a third of the whole generation, and **widening `flip_eps` is very nearly
free** — the regret moves by less than its own standard error while the saving grows substantially.

### The trap: the cap-R sweep must NOT be read as "a low R is cheap"

| raw | R=40 | R=20 | R=16 | R=16 + eps 0.20 |
|---|---|---|---|---|
| +0.0050 | +0.0038 | +0.0023 | +0.0016 | +0.0012 |

Regret **falls** as the cap falls. That is not a finding, it is an artefact of the estimand: the
metric is *adaptive versus full **at that R***, so a lower cap leaves less room for the two policies
to differ. Driving R down drives the measured regret toward zero while making **both** policies
worse in absolute terms, and the comparison cannot see that.

This is the user's own objection, confirmed:

> *"R being too low would be troublesome for the adaptive change, since it would reduce the impact."*

**Any future reading of this sweep must quote absolute policy quality at each R, not the adaptive-vs-full
delta at each R.** Otherwise the cheapest-looking configuration is simply the one with the least to lose.

### Raw R for slivers is 60

Recorded because it was misstated once in session: slivers' full-bottom raw is **R=60**, not R=100.

---

## 2. The cross-deck test (instruction 2) — ready to run, no rollouts

The simulator reads a pooled raw table and writes nothing. **26 decks already have a banked
`decks/<Deck>/<Deck>.keepmodel.exhaustive.raw.json.gz`**, so the cross-section the user asked for
needs no generation at all:

```
Angels, Anti-Lifegain, Auras, BreachingDragonstorm, Creature Giving, CritterLifegain, Dragons,
Dragonstorm, FiveColour, Fluctuator, Fungus, Giants, Goblins, Hinata2, KittyEquipment, Knights,
Melira Pod, Minotaur, Mirrorwing Dragon, Pirates, SelesnyaLifegain, StompySurprise, WhiteKnights,
burn, slivers_vial, treasure_hunt
```

### Shape of the run

Per deck, over a grid of (cap R, `flip_eps`), with `MTG_KEEP_SIM_TRIALS` high enough that the
Monte-Carlo floor-noise error is well below the effect (slivers used 128 and reported ±0.0001):

```
MTG_KEEP_SIM_ADAPTIVE_BOTTOM=1 MTG_KEEP_SIM_TRIALS=<n> \
MTG_KEEP_SIM_FLIP_EPS=<eps> MTG_KEEP_SIM_FLOOR=2 \
  <analyze-bin> decks/<Deck>/<Deck>.<ext> --cards-json src/cards/data/cards.json \
  --merge-keep decks/<Deck>/<Deck>.keepmodel.exhaustive.raw.json.gz
```

Four things to get right, each of which has bitten something in this repo before:

- **`sumsq` is a precondition, not an assumption.** The sim refuses without per-cell sumsq
  (`"pooled sidecars lack per-cell sumsq ... Nothing done."`). Part of the deliverable is the list of
  decks whose banked sidecars predate it — a deck that cannot be simulated must be reported as
  **un-run**, never silently dropped from the average.
- **It is a MEASUREMENT and must write nothing.** The block returns without producing a profile. Keep
  it that way: a keep table or value sidecar appearing beside a decklist *is* adoption in this repo
  (presence-gated), so a sim that wrote one would change play for that deck.
- **Do not average regret across decks and quote one number.** Decks differ in how much of their
  generation is bottoming at all (Snow: 59.6% of units; others far less), so the quantity the user
  asked about — how much this *helps* — is per deck `saving x (bottoming share)`, and the *risk* is
  per deck regret. Report the joint distribution, and name the worst deck.
- **Report it against each deck's own shipped cap**, not a common R. Decks ship different R
  (`fast` = 30, `complete` = 40), and the trap in §1 means a common-R table would rank decks by how
  little they had to lose.

### Why this is worth doing even if v3 never needs the dial

Every future deck's generation faces the same question, and today the only evidence is one deck. A
26-deck table turns "we think this is nearly free" into a result with a known worst case — and it
costs no rollouts, which is the entire reason the simulator was built.

---

## 3. The better experiment (instruction 3): run v3 both ways in one window

If the lossless prunes take KittyEquipment v3's generation far enough below the 36 h window that two
runs fit, then the right move is not to simulate the regret at all:

- **Arm A:** full bottoming, the shipped precision.
- **Arm B:** adaptive bottoming at the deck's cap, `flip_eps` as chosen.

Both produce a real keep table; the two tables are then compared the way keep tables are always
compared in this repo — by the in-game A/B, not by their own internal numbers. That is a *measured*
regret on the artifact that ships, and it retires the simulator's structural assumptions (Gaussian
draws from stored mean+variance, refined cells credited their true mean) rather than trusting them.

**Feasibility is a function of the lossless work and nothing else.** Two full-precision runs do not
fit a 36 h window today; whether they do later is exactly what the prune programme decides, so this
stays conditional and gets re-costed when those numbers land.

Three constraints carry over unchanged:

- **Generations are strictly serial and alone on the box.** Two arms means two sequential runs, never
  two concurrent ones — a contended generation measures nothing, and a keep table dropped beside a
  decklist mid-run changes the other arm's play.
- **Copy each journal aside before anything runs.** A generation journal is days of compute with one
  copy, and the recipe/R must match the journal's own header.
- **Bottoming itself is not the variable.** Bottoming is always on; the variable is only the
  *precision* with which the bottoming decision is estimated.

---

## 4. Who decides

Every dial here — cap R, `flip_eps`, `cutoff_P`, the bucket count, `MTG_DECISION_WORK_X` — is a
**precision/coverage tradeoff, and therefore the user's ruling, not an agent's**. That holds for
leaving one at its default exactly as much as for changing it. The simulator's job, and this
document's, is to put numbers in front of that decision; nothing here may be adopted on the strength
of a measurement alone.
