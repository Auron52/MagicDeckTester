# `MTG_LAZY_LEAF` is NOT answer-identical at H5, and H5 is the value-leaf crossover reference

**Status: CONFIRMED on WhiteKnights across all four matrix seeds (0 better / 141 worse in 800 paired
games). NOT yet checked on another deck. Needs a decision before the next value-leaf generation,
because it biases the adoption verdict IN THE LEAF'S FAVOUR.**

Found 2026-09-26 while measuring what a value-leaf generation would cost for WhiteKnights — i.e. found
by accident, which is why it is written down rather than left in a session log.

## The claim in the code

`scripts/valueleaf.sh` arms the lever on **every** H cell of **every** deck's matrix (phase C) and
describes it like this:

```
lazy leaf: ARMED on the H arm (MTG_LAZY_LEAF=1, set per-cell by the driver). One leafless
  full-depth pass before the ladder; answer-identical (per-game win turns verified), worth
  -10.4% at d4 / -36.8% at d5 in units on Snow. A MEDIAN-game lever -- the tail is the
  backstop's job, not this one. Self-disables on any budgeted cell.
```

Two parts of that are load-bearing: **"answer-identical (per-game win turns verified)"**, and
**"self-disables on any budgeted cell"** — the latter means the lever is live *precisely and only* in
the matrix's unbudgeted H cells, which is the measurement the generator's whole verdict rests on.

## The measurement

`scripts/wk_matrix_h_cost.py` — H1..H5, unbounded (`budget_ms 0`), value OFF, no sidecar (phase C's
own conditions), 200 games per cell, `MTG_LAZY_LEAF` as a per-job heurarm flag so both arms ride one
pooled batch. Paired per game index.

| cell | identical digest | same win turn | better | worse | d_avg | units ratio |
|---|---|---|---|---|---|---|
| H1 | 200/200 | 200/200 | 0 | 0 | +0.0000 | 1.024 |
| H2 | 200/200 | 200/200 | 0 | 0 | +0.0000 | 1.017 |
| H3 | 200/200 | 200/200 | 0 | 0 | +0.0000 | 0.968 |
| H4 | 200/200 | 200/200 | 0 | 0 | +0.0000 | 0.794 |
| **H5** | **158/200** | **161/200** | **0** | **39** | **+0.1950** | **0.307** |

**H1–H4 are exactly answer-identical, as documented. H5 is not.** Reproduced on all four of the
matrix's own seeds (`logs/wk_hcost/h5_seeds.txt`):

| seed | identical | same win turn | better | worse | d_avg |
|---|---|---|---|---|---|
| 8008 | 158/200 | 161/200 | 0 | 39 | +0.1950 |
| 9009 | 161/200 | 166/200 | 0 | 34 | +0.1700 |
| 10010 | 160/200 | 164/200 | 0 | 36 | +0.1800 |
| 11011 | 165/200 | 168/200 | 0 | 32 | +0.1600 |
| **pooled** | | | **0** | **141** | **~+0.18** |

**0 better / 141 worse over 800 games.** One-sided, reproducible on every seed, and the effect is
~+0.18 turns — roughly **90x** this repo's usual equivalence tolerance of 0.002. It is not sampling
noise and it is not a tail artifact: the lever is described as a median-game lever and the damage
shows up in ~18% of games.

## Why this matters more than a 0.18-turn measurement error

From the value-leaf skill, on why the H ladder stops at 5:

> H5 is the **ESCALATION CAP** — the strongest fallback the runtime can ever take — so "trust the
> leaf" means "the leaf matches H5", and nothing deeper can inform a decision the runtime cannot make.

So **H5 is the reference every V cell is compared against**, and `value_fallback_crossover` /
`value_trust_depth` are derived from that comparison. Inflating H5's loss-penalized win turn by +0.18
makes the heuristic ladder look **worse than it is**, which makes the value leaf look **better than it
is**. The bias therefore runs in the direction of *trusting and adopting a leaf*, on every deck whose
matrix was built with the lever armed.

That is the opposite of a conservative error. A generation that wrongly says "the leaf matches H5"
ships a model that is then load-bearing in play.

## What is NOT established

* **Whether it generalises.** The quoted verification was on **Snow**; this is WhiteKnights. The lever
  may well be answer-identical there and broken here. But it is armed for all decks, so "it holds on
  the deck it was verified on" is not a safe default.
* **The mechanism.** Not diagnosed. The shape of the result is suggestive — clean at H1–H4, broken only
  at H5, which is also the escalation cap — so the likely area is the interaction between the leafless
  full-depth probe and the cap/commit logic at the deepest rung, rather than the probe itself. The
  units ratio collapsing to **0.307** only at H5 (against 0.79–1.02 elsewhere) says the lever is
  skipping most of the work at that rung, not trimming it.
* **Whether any shipped sidecar is actually wrong because of it.** That needs re-running the affected
  decks' H5 cells with the lever off and re-deriving their crossovers. Every deck with a
  `value_leaf_table` is a candidate.

## Suggested handling

1. **Before the next generation, disarm it at H5** (or entirely) and take the ~1.35x cost on that one
   cell. For a cheap deck this is free; the measured H5 cost difference here is 0.33 vs 0.10 core-h.
2. **Then diagnose** why the deepest rung differs, since a correct lazy leaf is worth real money on
   expensive decks (the Snow figures are not small).
3. **Then decide** whether any existing `value_leaf_table` needs its H5 row re-measured. That is a
   fleet-wide question and a user call, not an agent's.

## Note on how this was found, because the route is reusable

The question being asked was only *"how expensive is a value-leaf generation for this deck?"* —
measured by replicating phase C's H cells directly. The anomaly that exposed the defect was a **cost
number that was too good**: H5 came back ~600x cheaper than H1 while playing *worse*. A deeper search
cannot be both cheaper and worse, so the measurement was wrong or the engine was. Isolating it needed
four controls (`scripts/wk_h5_anomaly.sh`: lazy on/off × sidecar present/absent), which ruled out the
deck's own adopted shape and left the lever.

The first, discarded reading was that the newly-adopted `leaf: none` sidecar was responsible — it was
the recently-changed thing, and it *was* affecting cost (7x cheaper at equal play). Attributing the
anomaly to the most recent change would have produced a confident, wrong retraction of a good
adoption.
