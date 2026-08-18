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

## Per-slot attribution: mostly unresolved -- SUPERSEDED, and WRONG

**Do not use this table.** It is kept only to record the error. It was computed under a `replace`
map that sent Expedite's slots to Fortifying Draught, when Impolite Entrance is Expedite's engine
clone -- so the slot labels describe bookkeeping, not card-for-card comparisons. The overnight
section below supersedes it with the corrected map, and reaches a DIFFERENT conclusion about which
substitutions carry the swap.

Conditioning on which swapped SLOT a game drew selects the same games in both arms (common shuffle,
aligned numbering), so this is a clean contrast. n = 40,000:

| slot | base -> trick | games | delta | se | t |
|---|---|---|---|---|---|
| 41-44 | Ancestral Anger -> Impolite Entrance | 23,775 | -0.0113 | 0.0072 | -1.56 |
| 47-49 | Twinflame -> Luxurious Libation | 19,384 | -0.0295 | 0.0083 | -3.54 |
| 54,55 | Expedite -> Fortifying Draught | 14,597 | +0.0048 | 0.0099 | +0.48 |
| 59,60 | Scale the Heights -> Fortifying Draught | 14,550 | -0.0156 | 0.0098 | -1.59 |

Read these against the -0.0142 library baseline, not against zero. (The conclusion drawn here at the
time -- that Twinflame -> Libation was the only slot with signal -- survived; the claim about the
Draught slots did not, because those slots were mislabelled. See the overnight section.)

## Overnight three-arm campaign (2026-08-18 night): the per-card answer

180,000 games (60,000 per arm, one seed, one shared apparatus), full per-game traces, plus a
DEDICATED isolated arm `libonly` = base with Twinflame x3 -> Luxurious Libation x3 (its own keep
table, 202,878 cells, 6.2 h).

| comparison | delta | se | t | diverged |
|---|---|---|---|---|
| base -> libonly (Twinflame->Libation ONLY) | **-0.0135** | 0.0027 | -5.06 | 17.8% |
| base -> trick (full 11-card swap) | **-0.0289** | 0.0038 | -7.62 | 34.5% |
| libonly -> trick (the other 8 cards) | -0.0154 | 0.0033 | -4.64 | 27.9% |

(The three sum exactly, but that is an arithmetic identity from measuring all arms on the same
games, not independent confirmation.)

### The replace map was wrong, and fixing it changed the per-card answer

User, 2026-08-18: *"why are we replacing Expedite, when the other spell is basically a direct
replacement"*. Correct -- Impolite Entrance IS Expedite's clone under this engine. The original map
sent Expedite's slots to Fortifying Draught, which made every per-slot attribution an artifact of
bookkeeping. Corrected map: Expedite->Entrance, Scale->Entrance, Anger->Draught, Twinflame->Libation.

That also embeds a PLACEBO inside the main comparison -- the Expedite->Entrance slot holds
engine-identical cards, so it must read zero. It reads **-0.0107 +/- 0.0066** (base vs trick) and
**-0.0084 +/- 0.0040** (libonly vs trick): consistently ~-0.01. That is the residual bias of the
slot-attribution method, MEASURED rather than assumed, and every slot estimate should be read net
of it.

| substitution | slot estimate | placebo-corrected | independent arm |
|---|---|---|---|
| Scale the Heights -> Impolite Entrance | -0.0920 +/- 0.0123 | **-0.081** | -- |
| Twinflame -> Luxurious Libation | -0.0462 +/- 0.0106 | **-0.036** | **-0.0329 +/- 0.0058** |
| Ancestral Anger -> Fortifying Draught | -0.0007 +/- 0.0090 | **+0.010** | -- |
| Expedite -> Impolite Entrance | -0.0107 +/- 0.0066 | 0 by construction | -- |

The placebo-corrected Twinflame->Libation (-0.036) lands on the dedicated arm's independent
measurement (-0.0329). Two methods, different apparatus, agreeing.

### Two results that invert the starting assumptions

**Fortifying Draught is not carrying the swap** -- Anger -> Draught is null (-0.0007 +/- 0.0090)
from base-vs-trick and +0.0150 +/- 0.0068 from libonly-vs-trick. **But do NOT read that null as
"Draught is a downgrade": it is an average across a SIGN CHANGE, and the conclusion drawn here at the
time was wrong.** See "The Draught null is a MIXTURE" below -- conditioned on Fists of Flame, Draught
wins by 0.134 turns in 34% of games and loses in the rest.

**The biggest single win is Scale the Heights -> Impolite Entrance** (-0.081 corrected, t = -7.49),
a substitution that only exists BECAUSE the map was corrected.

### Mechanism, from the traces

Among divergences with IDENTICAL opening hands and identical mulligan counts (pure play), Libation
beats Twinflame by deploying turns earlier:

```
gi=7774   base T8 -> libonly T4   base: T4 Twinflame({1}{R})
                                  lib : T2 Luxurious Libation({1}{G})
gi=14536  base T9 -> libonly T5   base: T7 Twinflame x2
                                  lib : T2 Libation({X}{G}); T4 Libation({5}{G})
gi=14562  base T9 -> libonly T5   base: never cast it
                                  lib : T2 Luxurious Libation({X}{G})
```

Libation is live at X=0 for one mana -- it still makes a 1/1 Citizen and still triggers the magnet
fan-out -- while Twinflame needs {1}{R} AND a board worth copying. In a deck that chains spells under
Zada, a one-mana body-maker that is never dead beats a stronger card you cannot cast yet.

### The undrawn-library effect scales with the size of the swap

The control (games where NEITHER arm drew a substituted card) was the worry in the first
measurement. Across the three pairs it tracks how much of the library changed:

| pair | cards changed | control delta |
|---|---|---|
| base vs libonly | 3 | **+0.0013 +/- 0.0015** (zero) |
| base vs trick | 11 | -0.0124 +/- 0.0025 |
| libonly vs trick | 8 | -0.0157 +/- 0.0016 |

With only 3 cards changed it VANISHES. So the effect is a genuine deck-composition effect on the
search's rollouts, not a fixed modelling bias attached to the new cards -- which is the reassuring
reading of the open question above, though it does not fully close it.

### The Draught null is a MIXTURE of two large opposite effects (resolved 2026-08-19)

User challenge: *"I'm still a bit suspicious of Fortifying Draught being bad. The card literally does
12 with three creatures and 20 with 4 creatures for G."* The arithmetic is right (copies resolve
sequentially, each gains 2 THEN reads the running total, so instance i pumps +2i: 2+4+6 = 12 at three
creatures, 2+4+6+8 = 20 at four), and the suspicion was justified. **Draught is not weak. The null is
an average over two large, opposite, individually-significant effects that cancel.**

The hidden variable is **Fists of Flame** (`pump_per_cards_drawn_power`, 4 copies in BOTH decks).
Ancestral Anger at a magnet does not draw one card, it draws one PER COPY -- and every one of those
feeds every later Fists instance, which draws first and then counts `cards_drawn_this_turn`. Anger's
payload is therefore a MULTIPLIER on Fists, while Draught's is a one-shot. Measured on hand-built
boards (Zada + 3 Goblin Instigator, no mana dorks, `test/scenarios/`):

| Fists of Flame available | Anger line | Draught line | difference |
|---|---|---|---|
| 0 | 10 dmg | **26 dmg** | Draught +16 |
| 1 | 40 dmg | 40 dmg | dead even |
| 2 | **86 dmg** | 70 dmg | Anger +16 |

**The crossover is at exactly one Fists of Flame, and the deck plays four.** Confirmed on the 60,000
real games, restricted to games drawing ONLY this slot (so no other substitution contaminates):

| Fists cast | games | base->trick delta | t | favours |
|---|---:|---:|---:|---|
| 0 | 2,709 | **-0.1336** | -8.35 | Draught |
| 1 | 3,506 | +0.0454 | +3.95 | Anger |
| 2 | 1,438 | +0.0654 | +3.18 | Anger |
| 3+ | 208 | +0.0288 | +0.48 | Anger |
| **ALL** | 7,861 | **-0.0131** | -1.52 | (the null) |

Reproduced on the independent `libonly` vs `trick` pair to three decimals (-0.1328 / +0.0425 /
+0.0624). **In its 34% of games, Draught is worth 0.134 turns -- six times the entire trick-suite
effect.** Reporting this slot as "null, Draught earns nothing" was averaging across a sign change.

Guarded by `test/scenarios/draught_magnet_escalation.json` (the escalating fan-out is exactly lethal
at 26) and `test/scenarios/anger_draws_feed_fists.json` (the draw multiplier is exactly lethal at 40).

**Deckbuilding implication: these two cards are complementary, not competing.** Splitting the slot
(some Anger, some Draught) is a screenable hypothesis that neither the 4/0 nor the 0/4 arm tested.

**A latent heuristic inconsistency found while chasing this, NOT the cause.** `EstimateCardValue`'s
d0 trick estimate scores an escalating counter as "~2 seen by a mid copy", in the units of that
counter. Gold Rush gains 1 Treasure per instance (constant right, estimate 18 at fanout 4); Draught
gains 2 LIFE per instance, so a mid copy sees ~4 -- it is scored 8 against a true 20, and against
Gold Rush's 18 despite the two having a mathematically IDENTICAL +2i curve. Probed at depths 0/1/2/5:
the search picked Draught correctly every time, so this did not produce the measured result. Recorded
as a candidate heuristic fix (measure per `heuristic-optimization.md`, do not adopt on reasoning).

### Artifacts

`logs/overnight/reports/`: `REPORT.md`, three TSVs cataloguing all 48,128 divergent games
(gi, seed, both win turns, first-differing turn, mulligans, slots drawn, casts with X), and three
case files with 68 side-by-side per-turn studies plus viewer repro commands.

### The deck this suggests, untested

Keep Ancestral Anger x4 and Expedite x2; take Impolite Entrance for Scale the Heights and Luxurious
Libation for Twinflame; play NO Fortifying Draught. That drops the one substitution measuring
nothing and keeps both that measure well.

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
