# Mirrorwing trick suite vs base suite: the measurement, and how it was verified

**Status:** measured 2026-08-18. Screening result, NOT an adoption (adoption goes through
`mulligan-profile.md` + `value-leaf.md`). See [[shared-apparatus-cost-mitigations]] for the
apparatus work this rests on and [[divergence-analysis-step]] for the divergence method.

## The swap

| out (base) | in (trick) |
|---|---|
| Ancestral Anger x4 | Impolite Entrance x4 |
| Twinflame x3 | Luxurious Libation x3 |
| Expedite x2 + Scale the Heights x2 | Fortifying Draught x4 |

11 of 60 cards. Slot pairing via `deck_compare`'s `replace` map, verified: all 13 shared card names
keep identical numbers and each replacement inherits the departing card's exact slots.

## Result

**The trick suite wins 0.0214 +/- 0.0035 turns sooner (t = -6.13), clearing its measured apparatus
floor of 0.0079 by 2.7x.**

| seed set | n | base | trick | delta | se | t | diverged |
|---|---|---|---|---|---|---|---|
| train 960000 | 40,000 | 5.0667 | 5.0460 | -0.0208 | 0.0050 | -4.19 | 36.7% |
| held-out 970000 | 40,000 | 5.0658 | 5.0438 | -0.0221 | 0.0049 | -4.49 | 36.4% |
| combined | 80,000 | | | **-0.0214** | 0.0035 | -6.13 | |

Apparatus: one shared cell-by-arm store (`keepstore.py`), 19 union buckets, 286,714 cells at H=7 of
which 60,794 (21.2%) shared and equal-weight pooled; base downsampled KEEP-ONLY to R=10 to match
trick's generation.

## Why the number is believable — the verification stack

Each of these was capable of failing, and two earlier versions of this measurement DID fail them.

1. **Placebo with a known-zero ground truth. The strongest single check.** Expedite and Impolite
   Entrance are provably identical to this engine -- equivalence discovery merges them into one
   bucket, and their parameters match (trample unmodelled because the goldfish never blocks;
   sorcery-vs-instant unmodelled because casts resolve in MAIN_1). Swapping Expedite x2 ->
   Impolite Entrance x2 must therefore measure EXACTLY zero. Measured over 40,000 games:

   ```
   base    mean win turn  5.066750
   placebo mean win turn  5.066750
   paired delta           +0.000000  (all zero)
   identical win turn     40,000/40,000 = 100.0000%
   diverged               0 (0.0000%)
   ```

   Every game byte-identical in outcome. This verifies numbering, pairing, the shared apparatus,
   and that the provider's name-keyed shed list does not leak card identity -- all at once.
   **Any future change to the screening path should re-run this.**

2. **Seed-perturbation null.** The store's downsample seed must not change the answer, so how much
   it does IS the floor: delta(0x5eed1234) = -0.0208 vs delta(0xA5A5C0DE) = -0.0216, |bias| =
   0.0009. Per-game churn from the seed alone: base 6.5%, trick 1.4% (asymmetric because only base
   is downsampled; trick feels it only through shared cells).

3. **Held-out seeds.** Train -0.0208, held-out -0.0221, gap 0.0013.

4. **The user's invariant: same indexed cards => same win turn.** 0 of 230 (0.00%) and 0 of 151
   (0.00%) in two independent samples.

5. **Apparatus disagreement, on hands containing NO swapped card** (both arms seeing identical
   cards, so any difference is the table alone): keep 13.74% -> 0.98%, bottoming 56.88% -> 0.30%.
   The ~1% residual is legitimate -- each arm's D_opt threshold differs (4.99556 vs 4.94963 on the
   play) because the DECKS differ.

## What the effect is actually made of — and the open risk

Splitting the 40,000 by whether a swapped card was ever drawn:

```
no swapped card drawn    3,669 games   delta -0.0142
swapped card drawn      36,331 games   delta -0.0215
```

**The advantage is roughly uniform whether or not the new cards are drawn.** Among the 3,669
control games, 98.06% have IDENTICAL opening hands and 99.26% identical mulligan counts, yet 2.95%
still end on different win turns. The only remaining difference is the UNDRAWN LIBRARY, which the
search's rollouts draw from: the trick deck's contents change how the AI evaluates plans even in
games where not one swapped card is seen.

The placebo proves this is not a harness artifact (an identical deck gives 0.0000% divergence). So
it is a real deck effect. **But it is the signature that engine over-valuation would also produce.**
If the rollouts over-rate Draught/Libation/Entrance -- Libation's `{X}` fill being the obvious
candidate, made more aggressive the same day -- the search would be systematically optimistic for
trick everywhere, which is exactly this shape. A supporting hint, not proof: both tables are
optimistic against realised play, but trick's more so (predicted-vs-realised gap 0.2621 vs 0.2375,
a 0.0246 difference, the same order as the effect).

**This is the open question, and it is a MODELLING question, not a measurement one.** The
measurement is verified; whether the engine values the new cards correctly is not.

## Per-slot attribution: mostly unresolved

Conditioning on which swapped SLOT a game drew selects the same games in both arms (common shuffle,
aligned numbering), so this is a clean contrast. n = 40,000:

| slot | base -> trick | games | delta | se | t |
|---|---|---|---|---|---|
| 41-44 | Ancestral Anger -> Impolite Entrance | 23,775 | -0.0113 | 0.0072 | -1.56 |
| 47-49 | Twinflame -> Luxurious Libation | 19,384 | -0.0295 | 0.0083 | -3.54 |
| 54,55 | Expedite -> Fortifying Draught | 14,597 | +0.0048 | 0.0099 | +0.48 |
| 59,60 | Scale the Heights -> Fortifying Draught | 14,550 | -0.0156 | 0.0098 | -1.59 |

Read these against the -0.0142 library baseline, not against zero. Doing so leaves Twinflame ->
Luxurious Libation as the only slot carrying real signal, and both Fortifying Draught slots at or
above baseline. **The user's prior was that Draught -- "very powerful" -- would carry the swap; this
does not support that**, but the per-slot standard errors are 0.007-0.010 against differences of
that same size, so it is suggestive at best.

## Caveats to carry into any adoption decision

- **Impolite Entrance's trample is NOT modelled** (the goldfish never blocks), so the trick arm is
  measured WITHOUT one of its real merits. The true effect is plausibly larger than 0.0214.
- **Luxurious Libation's token colour is not modelled** (no card in this deck reads colour, so
  believed inert).
- **The trick keep table predates commit ca4f0b4e** (the user's shed ranking). Digest checked: the
  shed change does not move d2/b3 rollout play, so the table is current -- but the measuring binary
  includes it while the table's rollouts do not.
- This is a SCREEN. 0.0214 turns is small in absolute terms; whether it justifies the swap is the
  user's call, and adoption requires the full per-deck artifact route.
