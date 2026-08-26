# StompySurprise: MTG_TOP_RESOLVE measured, and all 31 regressions root-caused

**Status: measured 2026-08-25, adoption NOT yet proposed.** Self-contained. Two results: the first
real measurement of `MTG_TOP_RESOLVE`, and an exhaustive per-game root cause of every regression it
and the cast order produce.

## 1. The measurement

Four arms, one pooled batch, paired on seed, at PLAY SETTINGS (mode 2 — `value_play`, depth 6,
budget 20; the deck's profile locks `target_depth=6`, so `--depth` must be OMITTED and the suite's
d5 cell is a gate cell, not play settings). 2,000 games per arm per block.

| arm | hold | train | combined |
|---|---|---|---|
| `MTG_STOMPY_ORDER` | 17 better : 7 worse (t −2.04) | 19 : 9 (t −1.89) | 36 : 16 |
| `MTG_TOP_RESOLVE` | 20 : 8 (t −2.34) | 17 : 10 (t −1.46) | 37 : 18 |
| **both** | **31 : 15 (t −2.43)** | **28 : 16 (t −1.90)** | **59 : 31** |

Avg deltas: order −0.0050/−0.0050, top −0.0065/−0.0040, both −0.0085/−0.0065.

**`MTG_TOP_RESOLVE` helps on its own, and had never been measured before.** The combination is the
best arm on BOTH blocks, which is what the design predicted: the order's rank-15 Turntimber /
rank-19 Natural Order placements were authored assuming the reset exists, so
`EngineFlags.h` is right that measuring the order without it "measures half a design".

**This voids the recorded Stompy gate-2 verdict** ("cast order is a hard prune, 37 deleted wins"),
which was measured without `MTG_TOP_RESOLVE`.

## 2. Every regression root-caused (USER directive)

> *"Can you also root-source every case that got worse before passing them on? I don't want to waste
> the other agent's time if they aren't related to the payment processing."*
> *"We should be doing this exhaustively anyway."* — USER, 2026-08-25

All 31 games where the combined arm is worse were reproduced individually and classified at the
first divergent turn (`test/tools/kitty_ab/rc_classify.py`).

| root cause | n | owner |
|---|---|---|
| different cast CHOICE | 17 | the levers |
| MULLIGAN divergence (pre-play) | 7 | neither — see below |
| same casts, sequencing/activation differs | 6 | the levers (cast order) |
| cast TIMING choice (deferred) | 1 | the levers |
| **PAYMENT / mana tap order** | **0** | — |

Cross-tabulated against which lever reproduces each one: TOP_RESOLVE 16, cast order 10, combination
only 3, both independently 2.

### The answer to the hand-off question: nothing goes to the payment owner

**Zero of the 31 involves a different mana payment.** In every "same casts, different sequencing"
case the `manaPaid` strings are IDENTICAL between arms (`{G}`, `{1}{G}`, `{4}{G}{G}{G}`) — the
payment engine produced the same payments and only the cast sequence moved. The previously recorded
"~16 of 26 were the MANA TAP ORDER" split was an INFERENCE and is wrong; this is measurement.

### Two traps this exercise exposed

* **A batch game reproduces as `--seed (base+gi)` AND `--game-index gi`.** Dropping `--game-index`
  silently produces a *different* game that still looks plausible — same deck, same seed, a sensible
  line — and it invalidated an entire first pass of this analysis (2 of the first 3 "payment
  candidates" were repro artefacts). `rc_classify.py --verify` now checks every reproduced log
  against the batch `.wins` entry and REFUSES to classify on a mismatch.
* **7 of 31 "regressions" are not play regressions at all.** The kept hands differ, because
  bottoming consults the search and a search lever therefore changes which cards go to the bottom.
  Attributing a play decision in those games is meaningless. This also means a fraction of the
  aggregate delta on any search lever is mulligan movement, not play movement.
* **"Missing cast + mana available" is NOT a payment failure** if the card is cast later. The single
  case that survived the naive test (train_1412, `MTG_TOP_RESOLVE`) turned out to cast the "missing"
  Worldly Tutor TWICE two turns later: the lever delayed it, costing a turn of ramp. The naive test
  would have handed a lever's own valuation change to the payment owner.

## 3. TOP_RESOLVE's own regressions: one shape, and it is NOT payment

The 18 games where `MTG_TOP_RESOLVE` ALONE is worse were reproduced and classified separately
(the section above analysed the combined arm). 13 are a different cast choice, 3 are mulligan
divergence, 1 is sequencing, 1 is deferred timing.

**10 of the 13 cast-choice regressions have the arm casting an EXTRA top-of-library card the
baseline did not**: Worldly Tutor x6, Mirri's Guile x2, Call of the Wild x1 (plus the deferred-timing
case, also Worldly Tutor). The lever makes a tutor more attractive because casting it arms a
re-solve, and the search sometimes buys that with mana it needed elsewhere.

### Not horizon, not budget

| setting | regressions remaining of 18 |
|---|---|
| d6 / b20 (play) | 18 |
| d8 / b10000 | 11 |
| d8 / b100000 | 11 (identical results) |

A 10x budget changes nothing, so the 11 survivors are neither horizon-truncated nor budget-cut.

### The mechanism, pinned (hold gi=1600, seed 901601, d8/b100k)

Both arms have the same 4 creatures on board at T3 and the same hand. The tap states say it all:

* **base** taps 3 Forests for 3 spells, leaves Llanowar Elves and Elvish Archdruid UNTAPPED, and
  attacks with both for **4** (opp 20 -> 16).
* **arm** casts a SECOND Worldly Tutor and pays for it by tapping 3 Forests **+ a Llanowar Elves**.
  That Llanowar cannot then attack, so the swing is **2** (opp 20 -> 18).

Two damage at T3 compounds: base kills on T4 (16 -> -1), the arm reaches only 8 and needs T5.

**The payment layer is not at fault, and this is worth stating explicitly.** The arm needed 4 mana
and had 3 lands, so the fourth spell costs an attacker NO MATTER WHICH source is tapped -- tapping
Elvish Archdruid instead yields all 4 mana from one permanent but loses ITS attack, the same 2
damage. There is no tap assignment that avoids the cost. The defect is the DECISION to cast the
fourth spell, which is the search's, not the payment solver's.

### The machinery to fix it already exists, and one deck uses it

`AvailableManaPoolNoAttackers` (`src/ai/ManaPayment.cpp`) is exactly this guard -- the pool minus
creature sources whose tap would cost a real attack (`CanAttackFull` + effective power > 0). It has
exactly TWO callers, both in the FiveColour provider ("CONDEMN-DIG REFINEMENT 2026-08-19: Main1 only
when payable WITHOUT tapping an attacker"). Nothing else in the engine prices an attack into a cast
decision. The obvious candidate fix -- gate a SPECULATIVE cast (a tutor, a dig) on the
no-attackers pool -- is untested and is the natural next experiment.

## 4. What is NOT settled

* **Adoption is not proposed yet.** 59 : 31 combined is a real signal, but the 7 mulligan-divergence
  games mean the play-only effect is smaller than the raw counts suggest, and the regressions are
  concentrated in TOP_RESOLVE (16 of 31). Worth understanding the 12 "different cast CHOICE /
  TOP_RESOLVE" games as a group before shipping — they may share one shape, the way the
  KittyEquipment condemnation losses were all one card.
* **Cost is unmeasured here.** Wall ms in the batch is contended and worthless; a `cost.py` pass on
  the `.units` files is still to do.
* **Gate 2 (the deleted-wins re-derivation) still needs running** under the combined arm, now that
  TOP_RESOLVE is known to matter.
