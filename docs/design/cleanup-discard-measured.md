# Cleanup discard: measured, and deliberately NOT searched

The cleanup discard (hand over its size limit — CR 514.1) was on the list of engine decisions that
pick among legal options by an arbitrary rule. It is now measured. **No searched axis is
warranted**, and one half of the rule turns out to be load-bearing rather than defective.

`MTG_TRACE=discard` instruments the decision (real resolutions only, gated on `g_real_resolution`,
so a searched run reports the discards the *game* made rather than the millions the search
imagined). `MTG_DISCARD_PICK` is the temporary A/B lever; `first` is the shipped rule.

## Where the decision actually happens

Per 400 d0 games per deck:

| deck | discard events | | deck | events |
|---|---|---|---|---|
| **treasure_hunt** | **336** | | burn | 3 |
| hinata | 36 | | auras | 3 |
| dragonstorm | 28 | | knights | 1 |
| antilife | 10 | | slivers, goblins | 0 |

It is reachable everywhere and exercised almost nowhere — effectively a Treasure Hunt decision, with
a thin tail in the two combo decks. (An earlier note that this rule "affects every deck" was wrong.)

Within TH the arbitrary tie-break is live in **100%** of discards:

* the **land branch** (`DiscardLandsFirst`) chooses among **4–22 lands**, median ~9, and takes the
  first in *hand order*;
* the **mv branch** is reached only when every land is staged, and is then a tie **100%** of the
  time, 5–16 wide — TH's eligible cards are all lands, all mana value 0, so "highest mana value"
  does not discriminate at all there;
* and the tied cards are genuinely different: 13 distinct land names — a Reliquary Tower, three
  cyclers, a sac-to-draw, two storage lands, two scry/surveil lands, four duals.

So the *frequency* case for searching this was strong. The leverage case is not.

## Two probes, because the obvious one proves nothing

`MTG_DISCARD_PICK=last` takes the **opposite end of the tied set** — same ranking, other end:

```
last vs first:  -0.0020 summed over 24 train cases, 6 moved, both directions
                every d5 case identical; one d3 case moved by +0.0040
```

That alone is weak evidence: *both* ends are arbitrary with respect to card quality, so two mediocre
rules agreeing says little. Hence `keeper`, which sheds the card scored **most useful**, on a
param-driven scale needing no deck knowledge: `no_max_hand_size` 3 (Reliquary Tower — the card that
ends this decision entirely) > cycling / sac-to-draw 2 > `etb_scry`/`etb_surveil` 1 > plain mana 0.

**An inert probe reads exactly like "no effect", so both arms were diffed on the actual
discarded-card distribution before any number was believed.** The first attempt at `keeper` used
`SituationalCardRank` and was inert: TH's `ScryKeepOnTop` answers `lands_in_play < 2` for a land,
the same verdict for *every* land, so the arm reproduced `first` exactly. The param-driven version
is live — it sheds cyclers and scry lands where the shipped rule sheds storage lands.

## Result, and the reattribution that matters

```
keeper vs first:  +0.1473 total   -- but it does NOT decompose the way the total suggests
   dragonstorm  +0.1239
   hinata       +0.0177
   th           +0.0057
```

Dragonstorm dominates a probe aimed at Treasure Hunt, which is the tell. **On any deck whose cards
all score 0, `keeper` does not shed the best card — it abandons the mana-value rule and sheds the
first eligible card in hand order.** Verified rather than inferred: every Dragonstorm discard under
`keeper` reports `best_mv=0` (33/33), against a spread of 6–9 under `first`.

So the number is real but measures something else, and the three findings are:

1. **The hand-order tie-break has no leverage.** −0.0020 across 24 cases; deeper search absorbs it
   entirely (every d5 case identical under `last`).
2. **Even the adversarial land choice has almost none** — for TH, the deck that makes 12× more
   discards than any other, deliberately shedding its most useful land costs **+0.0057**.
3. **The highest-mana-value rule IS load-bearing** — Dragonstorm loses **+0.1239** when it is
   effectively removed. It was on the "flagged defective" list; it is earning its keep, and the
   thing to avoid is weakening it.

(1) and (2) are two orders of magnitude below the axes that shipped alongside this work (tutor
−0.7015, Ponder −0.8467, ETB dig −0.0599, Lackey put −0.0499). A searched axis here would enumerate
a variant per candidate — 4–22 of them in the deck where it fires — to chase ≤0.006. It cannot repay
its cost, so it is not built. That is the same conclusion `MTG_LACKEY_RANK=low` reached for a
decision that *did* subsequently earn an axis; the probe is what distinguishes the two cases.

## Status

`MTG_DISCARD_PICK` stays until the outcome above has been signed off, then the `last`/`keeper` arms
are deleted per the coding-conventions rule on experiment levers. `MTG_TRACE=discard` is permanent —
it costs one cached bool test when the stream is off.
