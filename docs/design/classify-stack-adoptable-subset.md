# The classify stack: a four-lever subset is adoptable; two levers are not

**Status:** regression tier MEASURED; held-out overnight validation running.
**Goal (USER, 2026-08-16):** move the searched second main and searched breakpoint
logic toward adoptability.

## Result

Regression tier, each config vs the levers-off GT, per-GAME weighted (26,300 games):

| config | mean | net turns | better/worse | win->loss | loss->win |
|---|---|---|---|---|---|
| all six | -0.0008 | -22 | 137 / 100 | 11 | 5 |
| minus SEARCH_SECOND_MAIN | +0.0001 | +3 | 138 / 127 | 10 | 8 |
| minus PHASE_CLASSIFY | -0.0013 | -33 | 108 / 62 | 6 | 5 |
| **minus BOTH (four levers)** | **-0.0021** | **-54** | **115 / 55** | **4** | **8** |

**The adoptable subset is `MTG_MAIN2_DROP` + `MTG_ACQ_RESOLVE` + `MTG_BP_SITES=63` +
`MTG_DOUBT_MAIN2`.** It is 2.6x the full stack's mean, halves the worse-games, and is
the first config where more losses become wins than wins become losses.

Per deck, four levers: antilife **-0.0024**, burn **-0.0067**, creature_giving
**-0.0086**, hinata **-0.0112**, fivecolour **+0.0044** (the only regression left).

`MTG_BP_SITES=63` -- the searched breakpoint sites -- is IN the good set.
`MTG_DOUBT_MAIN2` is inert here by construction: it is read inside
`ClassifyMainPhase` (`TurnSolver.cpp:3332`), which does not run with
`MTG_PHASE_CLASSIFY` off. Keep it in the set only for symmetry; it changes nothing.

## Why the two rejected levers fail

### `MTG_PHASE_CLASSIFY` -- a PRUNE that deletes the land drop (hinata, +0.1331 alone)

Alone on hinata: 11 better / 198 worse, mean **+0.1331**, 12 win->losses. It is the
only lever whose removal flips hinata from regression to improvement.

**Not truncation -- a prune.** All ten d3 win->loss games still lose at
`--budget-ms 0`, and `MTG_UNPRUNE=mainphase` restores the base result exactly. The
line is a candidate that was never emitted.

**Mechanism.** `GoldFishRunner::DeckFeedsCombat` is FALSE for Hinata (no
haste/prowess/pump/lord/equipment/anthem/power_bonus card in the deck). With
`deck_feeds_combat=false`, `ClassifyMainPhase` sends `DrawSpell` (Ponder, Preordain,
Expressive Iteration) to `MP::Main2` and main 1 collapses to land-drops-only. But
**this deck's cantrips are what FIND the turn's land**, and the land drop is
main-1-only unless `Main2DropEnabled`. Deferring the cantrip past combat therefore
does not delay the land -- it destroys the drop outright.

Exemplar `hinata_regression_d3_s2002 gi=188` (seed 2190, `--depth 3 --budget-ms 10
--ignore-play-profile`, deck `decks/Hinata2/`): identical mulligan-to-4 with exactly
one land. Base T2 casts Ponder -> draws Forbidden Orchard -> plays it -> casts Sol
Ring, makes a land drop every turn T1-T6 and wins T6. Under the lever main 1 is
empty, Ponder slips to T3 MAIN_2, and the deck makes **3 land drops in 8 turns**,
never casts Hinata, and leaves the opponent on 20.

This is a missing dependency edge, in the same family as the card dependency map: a
draw spell ENABLES the land drop, and the classifier does not know it. That is why
`MTG_MAIN2_DROP` is the biggest in-stack mitigator (leave-one-out +0.0600) -- it
restores a legal main-2 drop -- and the code already anticipated the hazard at
`TurnSolver.cpp:14176` ("a Main2-partitioned deck draws into lands in main 2 and must
be able to play them, USER 2026-08-14, hinata gi=99"). The pair still loses gi=188
(6 -> 7), so the drop alone does not fully repair it.

**If this lever is revived:** classify a draw spell as Main1/Both whenever the land
drop is still open, independent of `deck_feeds_combat`. Do not rely on
`MTG_MAIN2_DROP` to cover it.

### `MTG_SEARCH_SECOND_MAIN` -- truncation, and pure downside alone

Alone: 2 games better / 8 worse across ALL decks -- no deck gains. Budget sweep on
antilife (1,100 games per budget) shows the harm decaying monotonically to zero:
+0.0082 (b10), +0.0055 (b20), +0.0018 (b40), 0.0000 (b80), 0.0000 (b160). At b80+ it
changes nothing at all, so given room it simply reproduces the greedy second main.

See [[searched-second-main-adoptability]] for the exemplar (antilife gi=245: unbacked
Aria of Flame twice, opponent 20->30->40) and for the REFUTED greedy-floor attempt.

## The levers are NOT separable

Dropping SEARCH_SECOND_MAIN alone made hinata **three times worse** (+0.0131 ->
+0.0425): it had been masking PHASE_CLASSIFY's damage. Single-lever attribution is
necessary but not sufficient -- always re-measure the actual candidate config.

## Next

* Held-out overnight validation. Overnight GT is stale (predates the Bolas fix
  `881b517` and the drip-sweep fix `6169dbd`), so a BASE overnight run is going first
  to refresh it; the four-lever config must then be validated against that.
* fivecolour's +0.0044 is the last regression in the good set and is the remaining
  work before this is clean.

---

## HELD-OUT VALIDATION (overnight seeds, 2026-08-16) — it holds

Four-lever subset (`MTG_MAIN2_DROP` + `MTG_ACQ_RESOLVE` + `MTG_BP_SITES=63` +
`MTG_DOUBT_MAIN2`) vs the REFRESHED overnight base GT (`0cb6807`, which already
contains the Bolas and drip-sweep fixes so the comparison is not confounded).
205,600 games on seeds disjoint from the tuning tier:

| deck | better | worse | mean | win->loss | loss->win |
|---|---|---|---|---|---|
| goblins | 304 | 66 | **-0.0135** | 0 | 11 |
| hinata | 325 | 165 | **-0.0105** | 13 | 30 |
| burn | 135 | 0 | **-0.0067** | 0 | 2 |
| creature_giving | 65 | 7 | -0.0042 | 0 | 1 |
| antilife | 28 | 29 | +0.0001 | 1 | 3 |
| th | 10 | 25 | +0.0006 | 0 | 2 |
| fivecolour | 20 | 45 | +0.0021 | 0 | 1 |
| **TOTAL** | **887** | **337** | **-0.0028** | **14** | **50** |

**Held-out (-0.0028) is BETTER than train (-0.0021)** — the subset generalises
rather than degrading, which is the failure mode that usually kills a tuned config.
Better:worse 2.6:1; **50 losses become wins against 14 wins becoming losses**.

Two decks invisible on the regression tier are among the biggest winners: goblins
(-0.0135, 11 losses converted) and hinata (-0.0105, 30 converted) — hinata being the
deck that blocked the full stack. Burn is 135 better / 0 worse, again with no game
harmed at all.

### The remaining regressions (a work list, not a footnote)

`logs/stack4/holdout_worse.txt` has all 337 with repro seeds. The three
still-positive decks are tiny (fivecolour +0.0021, th +0.0006, antilife +0.0001 —
antilife is 28 better / 29 worse, i.e. noise). The worst individual games are
hinata's, and they cluster on one shape:

```
hinata_overnight_d3_s4004 gi=5   5->8   (seed 4009)
hinata_overnight_d3_s4004 gi=58  6->9   (seed 4062)
hinata_overnight_d5_s5005 gi=34  5->8   (seed 5039)
hinata_overnight_d5_s5005 gi=94  5->8   (seed 5099)
hinata_overnight_d5_s7007 gi=165 5->8   (seed 7172)
hinata_overnight_d5_s7007 gi=220 5->8   (seed 7227)
hinata_overnight_d5_s7007 gi=255 5->8   (seed 7262)
```

Repeated **5 -> 8** on hinata at both d3 and d5 is a family, not churn, and is worth
digging even though hinata is net -0.0105 overall. Note hinata also has the most
win->loss (13) *and* by far the most loss->win (30), so the deck is genuinely more
volatile under the subset rather than uniformly better.

### Status

Measured, validated held-out, and NOT adopted. All four remain default-off.
**Turning them on is the USER's call.**
