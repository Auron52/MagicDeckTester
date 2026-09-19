# Fitting Lightstall Inquisitor, Lyra Archangel of Dawn and Remote Farm into Angels

**Status:** screening COMPLETE 2026-09-18, one recommendation, held out and confirmed. Cards are
IMPLEMENTED and committed (`037bb8c5`); **no decklist has been changed** — `decks/Angels/Angels.cod`
is untouched and adoption is the user's call. This doc is the record of what was measured and, more
importantly, of what the measurements CANNOT see.

**The answer, in one line:** adopt **4 Lyra, Archangel of Dawn** (cutting all 3 Lyra Dawnbringer and
1 Archangel of Thune), **4 Remote Farm** and the Azorius Chancery's slot, and — the finding nobody
was looking for — **raise Giada, Font of Hope 2 → 4**. Worth **−0.303 / −0.326 / −0.369** turns at
20 / 30(2HG) / 40 life over 80,000 held-out games per format. Full list under
*THE RECOMMENDED LIST* below.

**Scale:** **8,230,000 paired games** across nine screens and two held-out confirmations (counted
from the `N arms x M games` line of each log, not estimated), three formats, five disjoint seed
blocks, all arms deck-construction legal.

**User framing (2026-09-18):** *"figuring out how to fit in the new cards … I'm guessing we'll want
to drop at least 1 or 2 if not 3-4 5-drops"*; Swords and Unexpectedly are **not** candidates
(*"the removal is a big help in real games"*); Legion Angel *"kind of a handy card in a drawn-out
fight (something we don't deal with in goldfish)"*; Remote Farm to be settled **first**;
*"I suppose we should test under both modes"*; *"there aren't that many new cards, so we should be
able to evaluate most possibilities."*

## The cards, and what the sim can and cannot see

| card | what it is | what the sim sees |
|---|---|---|
| **Lyra, Archangel of Dawn** `{2}{W}` 3/3 Legendary Angel Knight, Flying | "Whenever you gain life, put a +1/+1 counter on each Angel you control" — Archangel of Thune narrowed to Angels | **Nearly everything.** Fully modelled. In this deck every creature but Bishop of Wings is an Angel, so she is close to a 3-mana Thune. NOT a 5-drop: she is a **three**-drop. |
| **Lightstall Inquisitor** `{W}` 2/1 Angel Wizard, Vigilance | ETB: each opponent exiles a card from hand and may play it, taxed `{1}`, lands tapped | **Only the body.** The ETB is structurally inert (no opponent cast path) and vigilance is inert (the opponent never attacks). What is left is a ONE-MANA ANGEL, i.e. the cheapest possible lifegain trigger for Bishop of Wings / Righteous Valkyrie / Seraph Sanctuary / Giada. A screen that likes this card is liking the enabler, never the rider — and the rider is a small tax, not disruption. |
| **Remote Farm** (land) | enters tapped w/ 2 depletion counters; `{T}`, remove one: add `{W}{W}`; sacrifice at zero | **The upside only** — see below. |

## LEGALITY BOUNDS THE SEARCH — and screens 1–3 crossed it

**Nothing in this toolchain enforces the four-copy rule.** `deck_compare.py` validates that an arm
is 60 cards; it does not validate that the 60 are legal. Checked in `cards.json`:

| card | `supertypes` | copy cap |
|---|---|---|
| Plains | `["Basic"]` | unlimited |
| **Remote Farm** | `None` — a *nonbasic* land | **4** |
| **Lightstall Inquisitor** | `None` | **4** |
| **Lyra, Archangel of Dawn** | `["Legendary"]` | **4** |
| Legion Angel | `None` | **4** — already at cap (1 main + 3 side) |

So **`rf6`, `rf8`, `lad5` and `lad6` are deck-construction-illegal and cannot be adopted at any
measured value.** They were the top arms of screens 1–3. Every arm from screen 4 onward is checked
against this table before it is run.

This also *partly dissolves* the caveat below: the legal maximum for both Lyra and Remote Farm is 4,
so "monotone out to 6 or 8" is no longer a question anyone has to answer. What remains is whether
the shape is monotone *within* 0–4, which screen 4's ladders measure directly.

## THE CENTRAL CAVEAT: two results are monotone to the edge of the tested range

Both `Remote Farm count` and `Lyra count` kept improving at every step out to the largest value
tested (8 Remote Farm; 6 Lyra) — **both past the legal cap of 4**. **A real cost curve bends.
Monotone-to-the-boundary is the signature of a cost the model never pays**, and in both cases the
mechanism is the same and is measured:

* **Remote Farm.** Of the copies that reach the battlefield, **only 7% ever spend both counters and
  sacrifice themselves; 82% are tapped exactly once.** The deck wins around turn 5.3, so Remote
  Farm behaves as *a tapped Plains with a Dark Ritual stapled on* and its printed drawback is
  billed in 1 game in 14.
* **Lyra count.** 6 copies of a LEGENDARY 3-drop measured better than 4. The legend rule IS
  modelled (a second copy is genuinely dead), but the games end before you draw one.

**This was tested, and the naive fix does NOT work.** Raising `max_turns` 8 → 20 moved every delta
by ≤0.0009 (base 5.2904 → 5.2946), because only ~0.3% of games reach turn 8 at all. The harness cap
was never the binding constraint — the deck's own clock is. No horizon setting fixes that; it needs
a longer *game*, which is why `starting_life` became the second mode.

**Do not let the sim pick either count.** The sign is trustworthy; the magnitude and especially the
optimum are not.

## Mode 2: `starting_life`, not `max_turns`

`deck_compare.py` takes a per-arm `formats` axis. `{"std": {"starting_life": 20}, "long":
{"starting_life": 40}}` runs every combination under each and compares it to ITS OWN base. Compare
DELTAS across formats, never LEVELS (a higher life total is slower for reasons unrelated to any
card). The keep table and value leaf were fitted at 20 life; that is symmetric across arms, so the
delta holds.

## Results

All 20,000 paired games per cell, d5/20ms, ONE pooled batch, shared apparatus, provider pinned
`Angels`. Negative = faster.

### Screen 1 — 13 arms, the combination space (`spec1.json`)

| arm | Δ @20 life | Δ @40 life | read |
|---|---|---|---|
| `lad4` Thune 4→2, LyraDB 3→1, **+4 Lyra** | −0.1376 | **−0.1953** | best in long |
| `mix33` Thune 4→1, LyraDB 3→0, Legion→0, +3 Lyra +3 Lightstall | −0.1825 | −0.1744 | best in std |
| `mix31` +3 Lyra +1 Lightstall | −0.1338 | −0.1686 | |
| `lad3` / `lad2_db` / `mix22` | −0.109 … −0.128 | −0.132 … −0.133 | |
| `lad2` Thune 4→2, +2 Lyra | −0.0772 | −0.0941 | |
| `legion0_lad1` cut Legion Angel for 1 Lyra | −0.0324 | −0.0593 | **discount — see below** |
| `swords2_li1` cut 1 Swords for 1 Lightstall | −0.0493 | −0.0573 | **discount — see below** |
| `li3_db` LyraDB 3→0, +3 Lightstall | −0.0726 | −0.0548 | |
| `li4` +4 Lightstall | −0.0873 | −0.0266 | **collapses** |
| `li2` +2 Lightstall | −0.0508 | −0.0071 (t=−2.2) | **collapses to nothing** |

### `lad4` confirmed on held-out seeds (`--confirm lad4`)

| block | std | long |
|---|---|---|
| screen (seed 980000) | −0.1376 | −0.1953 |
| held out (seed 1480000) | −0.1383 | −0.1994 |
| shrinkage | −0.0006 (t=−0.13) | −0.0041 (t=−0.73) |
| **pooled, 40,000 games** | **−0.1379** | **−0.1974** |

No selection bias. Paired format difference-of-differences: **long − std = −0.0612 ± 0.0033,
t = −18.83.** Lyra is worth significantly MORE the longer the game runs.

### Screen 2 — built on `lad4` (`spec2.json`)

| arm | Δ @20 | Δ @40 |
|---|---|---|
| `lad4` | −0.1384 | −0.1974 |
| `lad4_nochanc` (also cut Azorius Chancery) | −0.1467 | −0.2052 |
| `lad5` / `lad6` | −0.1632 / −0.1840 | −0.2359 / −0.2534 |
| `lad4_li2` | −0.1973 | −0.2212 |
| `lad4_rf2` / `rf4` / `rf6` | −0.2123 / −0.2698 / −0.3221 | −0.2605 / −0.3072 / −0.3488 |
| `lad4_rf4_li2` | −0.3211 | −0.3222 |

### Screen 3 — the life SLOPE, 20/40/60 life (`spec3_slope.json`) — LANDED

21 cells × 20,000 games, `max_turns` 18, log `logs/deckcmp/angels_newcards/screen3.log`. The
question is the SLOPE of each arm's delta against starting life — the closest available proxy for
the cost the sim cannot bill. An arm whose advantage GROWS with life is a real engine; one whose
advantage SHRINKS is buying tempo the longer game will not pay for.

| arm | Δ @20 | Δ @40 | Δ @60 | **slope (L60−L20)** | t | read |
|---|---|---|---|---|---|---|
| `lad4` | −0.1406 | −0.2024 | −0.2076 | **−0.0669** | −18.42 | grows — real engine |
| `lad6` *(illegal)* | −0.1845 | −0.2547 | −0.2439 | −0.0594 | −13.75 | grows |
| `lad4_rf2` | −0.2139 | −0.2591 | −0.2368 | −0.0229 | −5.30 | grows, then flattens |
| `lad4_li2` | −0.1985 | −0.2212 | −0.1797 | **+0.0188** | **+4.31** | **SIGN FLIP** |
| `lad4_rf6` *(illegal)* | −0.3229 | −0.3460 | −0.2858 | **+0.0370** | +7.45 | decays |
| `rf6_only` *(illegal)* | −0.1555 | −0.1580 | **−0.0730** | **+0.0825** | **+19.80** | **collapses by half** |

**This is the bend the earlier screens could not produce, and it is unambiguous.**

* **`rf6_only` loses half its value between 40 and 60 life** (−0.158 → −0.073, t = +19.80). That is
  the depletion-sacrifice bill finally arriving: given a long enough game, the deck *does* run its
  Remote Farms out and sacrifice them. The cost is real, the sim can price it, and 20 life simply
  is not long enough to see it. Remote Farm is a **tempo land**, correctly valued only against a
  stated game length.
* **`lad4_li2` flips sign.** At 20 life adding 2 Lightstall beats `lad4` (−0.1985 vs −0.1406); at
  60 life it is *worse* than `lad4` (−0.1797 vs −0.2076). Lightstall Inquisitor does not merely
  fade in long games — it **costs** you them.
* **Lyra holds up across the whole range** (−0.1406 → −0.2024 → −0.2076). The gain plateaus between
  40 and 60 but never reverses. This is the one card here with no length caveat at all.

### Screen 4 — the main list-settling screen, ALL ARMS LEGAL (`spec4_main.json`)

18 arms + base × 3 formats (`std` 20 / `2hg` 30+2 heads / `long` 40) × **30,000** games,
`max_turns` 18, one pooled batch, 32/32 workers throughout. Fall-through 0.39% worst-arm
(≈0.00025t of one-sided bias — three orders of magnitude under the effects below).
Log `logs/deckcmp/angels_newcards/screen4.log`.

**A — which big slots to cut for 4 Lyra ArchDawn** (24 lands, no Chancery). The answer is clean and
monotone in all three formats, and the ordering *widens* with game length:

| arm | std | 2hg | long |
|---|---|---|---|
| `a_t3d0` Thune 3, Dawnbringer 0 | **−0.1516** | **−0.1991** | **−0.2196** |
| `a_t2d1` Thune 2, Dawnbringer 1 | −0.1477 | −0.1949 | −0.2048 |
| `a_t1d2` Thune 1, Dawnbringer 2 | −0.1456 | −0.1890 | −0.1919 |
| `a_t0d3` Thune 0, Dawnbringer 3 | −0.1399 | −0.1791 | −0.1693 |

**Cut Lyra Dawnbringer, keep Archangel of Thune.** Thune's lifegain→team-counter trigger is the
deck's engine; Dawnbringer is a lord whose flying and first strike are both structurally inert here,
so only the +1/+1 and lifelink survive.

**B — Remote Farm ladder** (on `a_t2d1`). Monotone to the legal cap of 4 in every format; the
marginal copy decays but never turns:

| arm | std | 2hg | long |
|---|---|---|---|
| rf0 | −0.1477 | −0.1949 | −0.2048 |
| `b_rf1` | −0.1817 | −0.2219 | −0.2320 |
| `b_rf2` | −0.2138 | −0.2432 | −0.2569 |
| `b_rf3` | −0.2424 | −0.2628 | −0.2787 |
| `b_rf4` | **−0.2692** | **−0.2843** | **−0.2996** |

**D — Giada, Font of Hope. The screen's one genuinely new finding.**

| arm | std | 2hg | long | slope (long−std) | t |
|---|---|---|---|---|---|
| `b_rf4` Giada 2 / YV 4 | −0.2692 | −0.2843 | −0.2996 | −0.0304 | −9.16 |
| `d_giada3` Giada 3 / YV 3 | −0.2903 | −0.3171 | −0.3382 | −0.0479 | −13.73 |
| `d_giada4` Giada 4 / YV 2 | −0.2987 | **−0.3294** | **−0.3521** | **−0.0534** | −14.71 |

Best *legal, actionable* arm in both 2hg and long. Mechanism: Giada's clause 2 is a CR 614
replacement that sizes up every other entering Angel, and Righteous Valkyrie gains life equal to
**that creature's live toughness** — so a bigger entrant is more life, which is more Thune / Lyra
ArchDawn counter events. She is an engine multiplier, not a curve filler.
**The dead-legend worry points the other way here:** a legend's redundant-copy cost should bite
*harder* in longer games (more cards seen), yet Giada 4's advantage *grows* with length. That is
evidence against the concern, not for it.

**E — land count** (on rf4): 23 (−0.2784 / −0.2843 / −0.3007) ≈ 24 (−0.2692 / −0.2843 / −0.2996)
> 25 (−0.2629 / −0.2763 / −0.2949). **Do not go to 25.**

**C — Lightstall, and why its group is confounded.** `c_li1/2/3` buy Lightstall by cutting *more*
Archangel of Thune, so they move two axes at once. Their slopes tell the story anyway: `c_li1`
−0.0049, `c_li2` +0.0076, `c_li3` **+0.0349** — the more Lightstall, the more the arm's advantage
is a short-game artifact. At `2hg` the ladder has already flattened (`c_li2` −0.3093 vs `c_li3`
−0.3089).

### Screen 5 — the finals, group winners CROSSED (`spec5_finals.json`)

13 arms + base × 3 formats × **40,000** games. Screen 4 settled four axes separately; nothing had
tested them together. Reference arm `v_a` = Thune 3 / Dawnbringer 0 / **LAD 4** / Giada 4 / YV 2 /
Chancery 0 / Plains 16 / **Remote Farm 4** (24 lands).
Log `logs/deckcmp/angels_newcards/screen5.log`.

| arm (trade vs `v_a`) | std | 2hg | long | vs `v_a` (std / 2hg / long) |
|---|---|---|---|---|
| **`v_a`** | **−0.3017** | **−0.3268** | **−0.3716** | — |
| `v_b` Giada 3 / YV 3 | −0.2915 | −0.3126 | −0.3522 | +0.010 / +0.014 / +0.019 |
| `v_t2d1` Thune 2 / Dawnbringer 1 | −0.3014 | −0.3268 | −0.3584 | +0.000 / 0.000 / +0.013 |
| `v_serra3` Serra 3, YV 1 | −0.2906 | −0.3146 | −0.3537 | +0.011 / +0.012 / +0.018 |
| `v_sanc3` Sanctuary 3, Plains 17 | −0.2692 | −0.2900 | −0.3214 | +0.033 / +0.037 / +0.050 |
| `v_rf2` Remote Farm 2 | −0.2473 | −0.2913 | −0.3300 | +0.054 / +0.036 / +0.042 |
| `v_rf0` Remote Farm 0 | −0.1844 | −0.2509 | −0.2778 | +0.117 / +0.076 / +0.094 |
| `v_yv0` YV 0, Lightstall 2 | −0.3337 | −0.3390 | −0.3807 | −0.032 / −0.012 / −0.009 |
| `v_land23` 23 lands, Lightstall 1 | −0.3246 | −0.3408 | −0.3836 | −0.023 / −0.014 / −0.012 |
| `v_li1` Thune 2, Lightstall 1 | −0.3339 | −0.3411 | −0.3775 | −0.032 / −0.014 / −0.006 |
| `v_li2` Thune 1, Lightstall 2 | −0.3611 | −0.3501 | −0.3735 | −0.059 / −0.023 / −0.002 |
| `v_nogreaves` cut Greaves, +1 Lightstall | −0.3355 | −0.3570 | −0.4091 | −0.034 / −0.030 / −0.038 |
| **`z_swords2_li1` — BIAS YARDSTICK, NOT ADOPTABLE** | −0.3524 | −0.3758 | −0.4273 | **−0.051 / −0.049 / −0.056** |

**New answers:** Seraph Sanctuary stays at **4** (cutting one for a Plains is clearly worse, despite
the {C}); Serra stays at **2**; Giada 4 > Giada 3 confirmed on the full candidate, gap widening with
length; Remote Farm 4 ≫ 2 ≫ 0 in every format.

### THE YARDSTICK ARM CHANGED AN ANSWER — this is why it was carried

`z_swords2_li1` cuts ONE Swords to Plowshares and measures **−0.05t**. Nothing is being bought: the
goldfish has no creature worth exiling and the lifegain rider needs Tainted Remedy. **That −0.05 is
the price this apparatus pays an arm for discarding a card it cannot model** — and it is LARGER
than most of the real effects in this table.

Apply it to `v_nogreaves`. Cutting Lightning Greaves measured better in all three formats
(−0.034 / −0.030 / −0.038) with a good slope — it looks adoptable. But its margin sits **below the
yardstick**, and Greaves' unmodelled half (shroud, i.e. protection from removal) is the *same class
of blindness* as Swords'. Haste is modelled and real; shroud is worth zero against an opponent that
never interacts. **Not adopted.** Without a deliberate yardstick arm in the screen this would have
read as a clean win.

## Standing conclusions

1. **Lyra, Archangel of Dawn is the real card. Adopt some number.** Confirmed on held-out seeds,
   robust in both modes, and it gets BETTER in longer games — the opposite of a tempo artifact.
   **4 copies is the well-supported count** (`lad4`, −0.138/−0.197). 5–6 measure better but run
   into the under-priced dead-legend cost; that is a judgement call, not a measurement.
2. **She is a 3-drop, so the user's "cut 5-drops" framing works better than expected** — `lad4`
   cuts FOUR five-drops (Thune 4→2, Lyra Dawnbringer 3→1), the top of the range guessed.
3. **Azorius Chancery does not earn its slot.** Negative in all three screens (−0.0048, −0.0083,
   −0.0078). Small but consistent. Cut it.
4. **Lightstall Inquisitor is a short-game card that turns NEGATIVE in long games.** Helps at 20
   life, ~zero at 40, and at 60 life `lad4_li2` is measurably *worse* than `lad4` (slope +0.0188,
   t = +4.31). Include it only for a list aimed at short games.

   **On the unmodelled ETB, stated precisely** (an earlier draft of this doc called it "its only
   real-game value", which is sloppier than the card deserves). Oracle: *each opponent exiles a card
   from their hand and may play that card for as long as it remains exiled; each spell cast this way
   costs {1} more; each land played this way enters tapped.* The opponent does not lose the card — so
   this is not disruption. What it is, is a small **tax**: {1} more on that card, and a tapped land
   if it is a land, against a small gift (the card dodges hand-size and discard). Net mildly in our
   favour, and small either way. It does not rescue Lightstall, because what the screen measures and
   rejects is the *body's* tempo contribution decaying to zero by 40–60 life — a separate thing from
   the rider, and the larger one.
5. **Remote Farm is a tempo land whose cost the sim CAN price — given a long enough game.**
   `rf6_only` loses half its value between 40 and 60 life (t = +19.80). Direction is real at 20
   life; the card is progressively worse the longer the format. Capped at 4 by legality anyway.
6. **`opponent_heads` is structurally inert for this deck.** Every read site of
   `gamesetup::OpponentHeads()` is gated on a param (`etb_damage_each_opponent`,
   `tap_opponent_lifegain`, `alt_lifegain_each_player`, `attack_creates_tokens`,
   `tap_damage_each_opponent`, X-spell face targeting) and **no card in the Angels 60 or its
   sideboard carries any of them**. So 2HG here reduces exactly to `starting_life: 30`, i.e. a
   50%-longer race. Righteous Valkyrie's anthem is `StartingLife() + 7`, so it needs the same
   *gain* (7 life) at 30 as at 20 — the deck's key threshold is format-invariant. Resplendent
   Angel's 5-life trigger is an absolute per-turn amount, likewise invariant.

## Two arms whose numbers must NOT be acted on

* **`swords2_li1` (cut a Swords for a Lightstall) measures −0.05/−0.06 and that is meaningless.**
  Swords to Plowshares is near-inert against a passive goldfish — there is no opponent creature
  worth exiling and its lifegain rider needs Tainted Remedy. The sim charges **nothing** for
  cutting removal. The user had already excluded Swords; the number agrees only by accident.
* **`legion0_lad1` (cut Legion Angel).** The engine DOES model the 4-deep wish chain as card
  advantage, so this is not blind — but it cannot model the attrition war that makes the chain
  matter, which is exactly the user's stated reason for keeping it.

## Apparatus

Shipped Angels keep table (K=14, R=40), with the three introduced cards **aliased** into host
buckets via `scripts/alias_card_into_bucket.py` — the sanctioned no-regeneration route:

| introduced card | host bucket | why it is safe |
|---|---|---|
| Remote Farm | `Plains` (19) | land-for-land; bucket ≫ 7 so the cap cannot overflow |
| Lightstall Inquisitor | `Lightning Greaves` (+Swords/Unexpectedly, 7) | bucket already at cap 7, so it can never overflow |
| Lyra, Archangel of Dawn | `Archangel of Thune` (4) | the bucket the slots come from |

Measured composition fall-through: **≤0.0076% of hands** on every arm, far under the 1%
`max_fallback`, so the shipped table is kept on every arm and coverage is effectively exact.

**The one live apparatus caveat — and it is now MEASURED to be real, not merely suspected.**
Aliasing Lyra into Archangel of Thune's bucket means the mulligan treats a **three**-drop as a
**five**-drop. That holds the keep policy fixed across arms by construction, which is what ISOLATES
the in-play difference — but it is precisely the case where the new card would want a DIFFERENT
keep policy, and the alias cannot see that.

**Equivalence discovery run on the union pool returns K=17, not 14, and the three extra buckets are
exactly the three aliased cards** — Remote Farm is NOT equivalent to Plains, Lyra ArchDawn is NOT
equivalent to Archangel of Thune, Lightstall is NOT equivalent to the Greaves/Swords bucket. The
alias is demonstrably wrong rather than unverified. (`armtable/Angels.keepmodel.gencache.json`.)

**Which way it biases, card by card** — this is the part that matters for reading the results:

| alias | what the mulligan therefore believes | direction of the bias |
|---|---|---|
| Lyra ArchDawn → Archangel of Thune | the arm's hand is more top-heavy than it is, so it mulligans hands it should keep | **AGAINST** the Lyra arms — the measured Lyra effect is a FLOOR |
| Remote Farm → Plains | a tapped depletion land counts as an untapped Plains, so land-light hands get over-kept | **FOR** the Remote Farm arms |
| Lightstall → Greaves/Swords | a 1-drop Angel counted as a non-creature | ambiguous, and Lightstall is not in the recommendation |

So the headline recommendation (adopt Lyra, cut Dawnbringer, raise Giada) sits on the **conservative**
side of the distortion, and **the Remote Farm count is the least-secure part of it**.

**Attempted fix, and why it was abandoned.** One table over the REAL candidate decklists with
`MTG_KEEP_ARM_DECKS` (Rule 0a's "union TABLE", never a union deck) — 10 arms, 190,062 reachable
cells of a 280,514 envelope (25.1% dropped as unreachable). It ran at 815 rollouts/s rather than the
~4,000/s the shipped table managed, because **hands holding 3+ Remote Farm roll out 5–6 s each**
(depletion counters multiply the decision tree). Projection from the live monitor: the fused
sub-table phase alone needed **3.6 h**, total 4 h+. Killed at 19 min; its journal is preserved at
`armtable/Angels.keepmodel.exhaustive.raw.json.journal` and the run is resumable.
Replaced by `--with-floor`, which brackets the same question per-arm at R=10 — see below.

### THE BRACKET (`spec6_floor_2hg.json --with-floor v_a,v_rf2`) — and it REFUTED one of my predictions

`--with-floor` generates a throwaway R=10 table for **each arm's own 60** and re-measures the same
paired games under it. `v_a`'s bracket table therefore buckets Lyra ArchDawn, Giada and Remote Farm
**correctly** — which is exactly the alias distortion, priced. (It needed a single-format spec: a
multi-format spec refuses a bracket, correctly, because a floor measured at one starting life does
not bound another's. Run at 2hg, the format the user asked about.)

| | `v_a` | `v_rf2` |
|---|---|---|
| under the shared (aliased) table | −0.3238 | −0.2875 |
| under each arm's OWN R=10 table | **−0.3897** | **−0.3420** |
| apparatus bias | −0.0659 (t=−8.89) | −0.0545 (t=−7.60) |
| floor = \|bias\|+2se | 0.0808 | 0.0688 |
| **effect / floor** | **4.01x** | **4.18x** |

Both clear the 3x screening bar. But the **nulls** are the real content — a bracket only worries when
the two nulls *differ*, and here they differ in a specific, informative way:

```
per-arm nulls, own table vs the shared one (positive = the OWN table plays WEAKER)
  base      +0.01815      <- base gains nothing from a worse copy of its own table
  v_a       -0.04780      <- v_a plays BETTER on its own table, despite R=10
  v_rf2     -0.03635
```

The shared aliased table **handicaps the candidate arms**, and does so *despite* their bracket
tables being R=10 (which the skill measures as ~0.032t of pure quality loss on its own). Correcting
the bucketing makes `v_a` look **better**, −0.3238 → −0.3897.

**What this confirms, and what it refutes.**
* **Confirmed:** the Lyra alias (a 3-drop bucketed as a 5-drop) biases *against* the Lyra arms. The
  headline numbers in this doc are a **floor**, not an inflation.
* **REFUTED — my own prediction.** I reasoned above that aliasing Remote Farm onto Plains would bias
  *for* the Remote Farm arms (a tapped depletion land being keep-scored as an untapped Plains, so
  land-light hands get over-kept). The measurement says otherwise: under the corrected apparatus
  `v_a` beats `v_rf2` by **0.0477**, *wider* than the 0.0363 the shared table showed. The 4-Remote-
  Farm count is not an artefact of the alias; if anything the alias understates it. The prediction
  table above is left in place deliberately — it was a reasonable mechanism and it was wrong.

### Two structural claims, closed by digest probe rather than argument

Both are byte-identical over 4,000 games (every game outcome, not just the mean):

| claim | probe | result |
|---|---|---|
| `opponent_heads` is inert for Angels | base deck, life 30, heads 1 vs 2 | **IDENTICAL** — so "2HG" for this deck is exactly `starting_life: 30` |
| the adopted 3-card sideboard plays like the shipped 5-card one | same 60, side 5 vs side 3, at life 20 AND life 30 | **IDENTICAL** at both — every measurement transfers |

The sideboard probe was not idle: Legion Angel's wish is name-pinned so the two extra cards can
never be fetched, but sideboard SIZE is folded into the search's state key (`Dominance.h`), so it
was reachable in principle. It isn't.

## Engine work this required (committed, `037bb8c5`)

`lifegain_counters_subtypes` — a RECIPIENT subtype filter on
`lifegain_each_own_creature_counters` (where `enters_watch_subtypes` filters the ENTERING
creature). Empty = every creature, which is what keeps Archangel of Thune byte-identical.
Smoke **83/83, play-changed=0**; 5 new unit tests in `test_angels_tribal.cpp` (114 pass, was 109).

**A defect an adversarial review caught before it shipped, worth remembering.** The first pass
dropped the whole-team term for any narrowed watcher at three loyalty-EV sites, justified as
"Ajani's, CritterLifegain's, no deck pairs them with a narrowed watcher". The third site is
**Serra the Benevolent's −3**, and Angels runs 2 Serra — so it would have under-ranked this deck's
strongest loyalty line exactly when Lyra is out, biasing the screen against the card being
screened. A 28-cell screen already running against it was killed rather than read. Replaced with
real recipient counting plus a subtype test on the token each ability is about to create (Serra's
4/4 **Angel** counts for an Angel-narrowed watcher; Ajani's 2/2 **Cat** does not).

Known, deliberately not fixed: `FireLifegainWatchers` gates on `IsCreature()` alone, so an animated
land takes no counter from Archangel of Thune even though animation grants all creature types. Real
pre-existing rules gap; fixing it moves `slivers_vial`'s GT, so it owes its own measurement.

## Engine defect that invalidates the legend-count arms (fixed, `9134e4e3`)

**Screens 1–9 ran on an engine that refused to cast a second Giada or a second Lyra**, so every
number above comparing MORE copies of a legend against FEWER is a **lower bound on the
higher-count arm**. The user's challenge to the 4-ofs is what surfaced it.

`OfferDuplicateLegendCast` prunes a duplicate legendary cast on the reasoning that the legend rule
kills the new copy on resolution, so the cast is a **tie** that costs a card and a turn's mana. That
holds only when nothing reads the entry or the death, and it was guarded by a template whitelist
(`VanillaCreature` / `LordEffect`). The whitelist answers the wrong question: the template describes
the **card**, but whether a duplicate is a tie depends on the **board around it**.

In this deck it is never a tie. A second Lyra or Giada still fires every Angel-enter watcher before
it dies — Bishop of Wings gains 4, Righteous Valkyrie gains its toughness, Seraph Sanctuary gains 1,
Youthful Valkyrie takes a counter, Giada adds an as-enters counter — each of those a separate
life-gain **event** (CR 119.10) that Archangel of Thune and Lyra Archangel of Dawn read. And the
legend-rule death is *itself* Bishop of Wings' "an Angel you control dies", i.e. a Spirit token.
`cards.json` already said so on Lyra Dawnbringer: *"Casting a redundant Lyra is a real line here,
not a no-op; do not score copies 2 and 3 as dead."* The engine was scoring them as dead anyway.
Confirmed with `MTG_TRACE=legend`: **33 duplicate-Dawnbringer events blocked per 1500 Angels games**
(and 8 duplicate-Lathliss per 800 Dragons games).

The fix vetoes the prune by asking the board rather than the template, and is strictly an
improvement where it changed anything: across smoke + regression, **0 slower / 7 faster / 57
play-changed at searched depths, and byte-identical at autonomous d0**. Windows + Linux +
determinism parity all green.

**Consequence for this document.** Screen 10 was killed mid-flight rather than read, because it was
measuring exactly the arms the defect biased. Screen 11 re-runs the legend counts *and* Lightstall on
the fixed engine. Nothing in the *core* recommendation is known to move — `v_a` already runs the
higher legend counts, so the defect ran **against** it — but the margins between `v_a` and the
lower-count arms should be treated as unsettled until screen 11 lands.

### Screen 7 — the RANKING held out, not just the winner (`spec7_rank_heldout.json`)

`--confirm` validates one arm against base. The recommendation rests on the ORDER of six arms, so
the order is what needed holding out: 6 arms × 3 formats × 40,000 games on seed **1500000**,
disjoint from the screen's 1200000 and the confirm's 1700000.

| arm | std (screen → held-out) | 2hg | long |
|---|---|---|---|
| `v_a` | −0.3017 → **−0.3071** | −0.3268 → **−0.3303** | −0.3716 → **−0.3730** |
| `v_b` | −0.2915 → −0.2977 | −0.3126 → −0.3166 | −0.3522 → −0.3570 |
| `v_t2d1` | −0.3014 → −0.3067 | −0.3268 → −0.3269 | −0.3584 → −0.3566 |
| `v_li1` | −0.3339 → −0.3382 | −0.3411 → −0.3444 | −0.3775 → −0.3761 |
| `v_rf2` | −0.2473 → −0.2517 | −0.2913 → −0.2939 | −0.3300 → −0.3278 |
| `v_yv0` | −0.3337 → −0.3362 | −0.3390 → −0.3373 | −0.3807 → −0.3781 |

**Every arm reproduces within ~0.006 and the ordering is identical in all three formats.** The three
load-bearing margins survive: Giada 4 > 3 (0.0094 / 0.0137 / 0.0160), Dawnbringer 0 ≥ 1
(0.0004 / 0.0034 / 0.0164, growing with length as screen 4 predicted), Remote Farm 4 > 2
(0.0554 / 0.0364 / 0.0452).

### Screen 8 — is `v_a` LOCALLY optimal? (`spec8_local.json`)

10 arms + `v_a` × 3 formats × 40,000 games, seed 1600000. Every arm is `v_a` with exactly **one**
single-card trade, so each row is a clean 1-for-1. Margins are **vs `v_a`**; positive = worse.

| trade | std | 2hg | long | verdict |
|---|---|---|---|---|
| `w_solring0` cut Sol Ring, +1 YV | **+0.1041** | +0.0973 | +0.1041 | Sol Ring is enormous. Keep. |
| `w_rvalk3` Righteous Valkyrie 4→3, +1 YV | **+0.0365** | +0.0528 | +0.0647 | 4 is firmly right. |
| `w_thune4` Thune 3→4, −1 YV | +0.0172 | +0.0108 | +0.0026 | 3 Thune confirmed. |
| `w_serra3` Serra 2→3, −1 YV | +0.0101 | +0.0121 | +0.0149 | 2 Serra confirmed. |
| `w_land25` Plains 16→17, −1 YV | +0.0090 | +0.0084 | +0.0104 | 24 lands ≥ 25. |
| `w_resp3` Resplendent 4→3, +1 YV | −0.0092 | −0.0035 | +0.0048 | wash |
| `w_land23` Plains 16→15, +1 YV | −0.0079 | −0.0116 | −0.0093 | **fully modelled — tested** |
| `w_thune2` Thune 3→2, +1 YV | −0.0174 | −0.0114 | −0.0006 | **fully modelled — tested** |
| `w_bishop3` Bishop 4→3, +1 YV | −0.0140 | −0.0129 | −0.0118 | under-modelled, see below |
| `w_serra1` Serra 2→1, +1 YV | −0.0118 | −0.0177 | −0.0176 | under-modelled, see below |

The two sanity arms land hard, which is what gives the small rows credibility: this screen can
resolve a real effect when there is one.

**The interesting pattern: four trades beat `v_a`, and every one of them buys a THIRD Youthful
Valkyrie.** That is not a contradiction of `v_a`'s Giada 4 / YV 2 — screens 4, 5 and 7 all say
Giada > YV. Put together, the implied ordering is **Giada > Youthful Valkyrie > {the 3rd Thune, the
2nd Serra, the 4th Bishop, the 24th land}**. So the ideal is Giada 4 *and* YV 3, paid for elsewhere.

**Two of the four are disqualified by the yardstick discipline, not by their numbers:**
* **Serra the Benevolent** — her −6 emblem is a *provable no-op* in this matchup (a damage floor on
  a life total nothing can reduce). Cutting her is flattered exactly the way cutting Greaves was.
* **Bishop of Wings** — the lifegain half is modelled, but the "whenever an Angel you control dies,
  make a 1/1 Spirit" half is **near-inert against an opponent that never interacts**: Angels
  essentially do not die in a goldfish. Cutting Bishop is flattered by the half the sim cannot run.

Both margins (0.012–0.018) are far under the −0.05t yardstick, so they are **not adoptable** on this
evidence. The other two — paying with a Plains or the third Thune — are fully modelled on both
sides, so they got their own held-out block.

### Screen 9 — the two fully-modelled refinements, held out (`spec9_refine.json`)

3 arms × 3 formats × **60,000** games, seed 1800000, disjoint from every prior block. Margins vs
`v_a`; pairwise se ≈ 0.0019–0.0023.

| trade | std (screen 8 → held out) | 2hg | long |
|---|---|---|---|
| `w_thune2` Thune 3→2, YV 2→3 | −0.0174 → **−0.0151** (t≈−7.9) | −0.0114 → **−0.0064** (t≈−3.2) | −0.0006 → **−0.0017** (t≈−0.7) |
| `w_land23` Plains 16→15, YV 2→3 | −0.0079 → **−0.0031** (t≈−1.6) | −0.0116 → **−0.0040** (t≈−2.0) | −0.0093 → **−0.0058** (t≈−2.5) |

**`w_land23` shrank by ~60% on held-out seeds** — the textbook selection-bias signature on a small
effect in a 10-arm screen, and the reason a trade never gets adopted off its discovery block.
It is noise; 24 lands stands.

**`w_thune2` is real but it is a 20-life-only tweak.** It holds at std (−0.0151), halves at 2HG, and
is indistinguishable from zero by 40 life. **Not taken.** The recommendation has to serve 2HG and
long as well, and buying 0.015 turns at 20 life for nothing at 40 is not a trade worth making the
default. Recorded here with its size and its decay so the option is on the record if someone wants
a dedicated 20-life build.

**Net: `v_a` is locally optimal** — to within ~0.015t at 20 life and ~0.006t at 30–40 life, against
single-card trades in every direction, with the two sanity arms (Sol Ring, Righteous Valkyrie)
confirming the screen resolves real effects when they exist.

### Screen 11 — legend counts AND Lightstall, on the FIXED engine (`spec11_legends_lightstall.json`)

17 arms + base × 3 formats × 40,000 games (2.04 M games), seed 2100000, after the duplicate-legend
prune fix. Deltas are **paired per game index against `v_a`**; negative is faster. This screen
answers the user's challenge to the 4-ofs, prices their Lyra Dawnbringer preference, and re-measures
the card the yardstick error had wrongly excluded.

| arm | std | 2HG | long | reading |
|---|---|---|---|---|
| **(L) cut a legend** | | | | |
| `l3_yv3` Lyra ArchDawn 4→3 | **+0.0129** | +0.0171 | +0.0244 | worse |
| `g3_yv3` Giada 4→3 | +0.0101 | +0.0153 | +0.0191 | worse |
| `l3_g3_yv4` both →3 | +0.0239 | +0.0311 | +0.0432 | worse |
| `g2_yv4` Giada →2 | +0.0304 | +0.0428 | +0.0532 | worse |
| **(L) Lyra Dawnbringer** | | | | |
| **`d1_thune2` 1 Dawnbringer, Thune 3→2** | **−0.0015** (t −1.0) | **−0.0027** (t −1.7) | +0.0103 | **free at 20 and 30 life** |
| `d1_yv1` paid from Youthful Valkyrie | +0.0134 | +0.0038 | +0.0118 | costs |
| `d1_lad3` paid from Lyra ArchDawn | +0.0297 | +0.0230 | +0.0357 | expensive |
| `d1_giada3` paid from Giada | +0.0280 | +0.0253 | +0.0339 | expensive |
| `d2_l3_g3_yv2` two Dawnbringer | +0.0604 | +0.0544 | +0.0767 | very expensive |
| **(I) Lightstall Inquisitor** | | | | |
| `i1_yv1` 1, from Youthful Valkyrie | −0.0195 | −0.0088 | −0.0075 | better |
| `i1_thune2` 1, from Thune | −0.0321 | −0.0139 | −0.0055 | better |
| `i2_yv0` 2 | −0.0309 | −0.0121 | −0.0083 | better |
| `i2_thune2_yv1` 2 | −0.0481 | −0.0204 | −0.0090 | better |
| **`i3_thune2_yv0` 3** | **−0.0558** (t −20) | **−0.0216** | −0.0060 | **best arm in the screen** |
| `i1_plains15` 1, from the 24th land | −0.0188 | −0.0078 | −0.0068 | better |

**The 4-ofs survived the challenge, and by a WIDER margin than before the fix.** Every arm that cuts
a legend count is worse in all three formats, and *the cost of cutting grows with game length* —
which is the mechanism the prune fix restored. A redundant legend is not a blank here: it is a
Bishop-of-Wings 4 life, a Righteous Valkyrie gain, a Seraph Sanctuary gain, a Youthful Valkyrie
counter and a Giada counter on the way in, plus a Spirit token on the way out, and a longer game
cashes more of them. The user's suspicion was well-founded as a question; the answer is that the
counts were if anything *under*-supported by the old engine, not over-supported by it.

**The Dawnbringer preference has a price, and the price is zero if the right slot pays.** Funding
1 Lyra Dawnbringer out of the **third Archangel of Thune** measures −0.0015 (t = −1.0) at 20 life and
−0.0027 (t = −1.7) at 30 — neither distinguishable from zero — against +0.0103 at 40. Funding it out
of Lyra ArchDawn or Giada instead costs ~20× more. Since the user's stated reason is life total as a
defensive resource on big boards — something a goldfish *cannot* price at all — a swap that is free
on the measurable axis and positive on an unmeasurable one is a straightforward take.

**Lightstall is good, and the internal control separates the card from the slot.** Cutting a Thune
for a 5-mana Dawnbringer is ~neutral (−0.0015); cutting a Thune for a 1-mana Lightstall is −0.0321.
The ~0.031 difference is the *card*, not the Thune cut — so this is not the "expensive to cut ⇒ the
donor was the effect" confound. The effect decays sharply with game length (−0.056 → −0.022 →
−0.006), exactly as a cheap enabler should: it buys early trigger density, which matters least when
there is plenty of time.

**Two things screen 11 left open**, both handled in screen 12 (seed 2200000, held out):
1. **Lightstall is monotone to the edge of the tested range** — 3 was the most tested and 3 won.
   This document's own central caveat says that is not a maximum. Screen 12 takes it to four.
2. **The two winning edits were never combined.** The only combined arm (`d1_i1_yv0`, −0.0022 std)
   paid for the Dawnbringer out of Youthful Valkyrie rather than the Thune that made it free.

## THE RECOMMENDED LIST (`v_a`)

Held out on disjoint seeds with no measurable shrinkage. Pooled over **80,000 games per format**:

| format | Δ avg win turn vs the shipped list | shrinkage screen → held-out |
|---|---|---|
| std (20 life) | **−0.3032** | −0.0030 (t = −0.60) |
| **2HG (30 life, 2 heads)** | **−0.3259** | +0.0018 (t = +0.33) |
| long (40 life) | **−0.3692** | +0.0048 (t = +0.82) |

```
CREATURES (24)                        LANDS (24)
  4 Lyra, Archangel of Dawn   NEW      16 Plains          (19 -> 16)
  3 Archangel of Thune        (4 -> 3)  4 Seraph Sanctuary (unchanged -- 4 is right, tested)
  0 Lyra Dawnbringer          (3 -> 0)  4 Remote Farm      NEW
  4 Giada, Font of Hope       (2 -> 4)  0 Azorius Chancery (1 -> 0)
  2 Youthful Valkyrie         (4 -> 2)
  4 Righteous Valkyrie                 OTHER (12)
  4 Resplendent Angel                   2 Serra the Benevolent  (2 is right, tested)
  4 Bishop of Wings                     1 Sol Ring
  1 Legion Angel                        1 Lightning Greaves   <- KEPT, see the yardstick
                                        3 Swords to Plowshares
SIDEBOARD (3)                           3 Unexpectedly Absent
  3 Legion Angel   <- the wish pool, and nothing else
```

**The sideboard must be rebuilt, and this is not cosmetic.** `decks/Angels/Angels.cod` currently
sides `1 Lyra, Archangel of Dawn + 3 Legion Angel + 1 Lightstall Inquisitor`, where the Lyra and the
Lightstall are the user's *introduction slots*. Mainboarding 4 Lyra with one still in the side is
**5 copies — illegal**. Legion Angel's wish is `wish_requires_name`-pinned to its own name, so the
pool is exactly the 3 Legion Angels and every other sideboard slot is mechanically inert here.

**The flex slot — and a correction, because the yardstick was applied to it BACKWARDS.**
`2 Youthful Valkyrie → 2 Lightstall Inquisitor` (`v_yv0`) measures **−0.032 at 20 life, −0.012 at
2HG, −0.009 at 40 life**. A previous revision of this section ruled Lightstall out by declaring
those margins "below the −0.05t yardstick". **That was wrong, and it contradicted the very next
paragraph of this document.**

The yardstick prices **cutting** a card the sim cannot model, because in that direction the
apparatus charges *nothing* for the loss and so flatters the arm doing the cutting. It says nothing
about an arm that **adds** one. And neither Lightstall swap cuts an under-modelled card: `v_yv0`
cuts Youthful Valkyrie and `v_li1` cuts Archangel of Thune, both fully modelled. So the gate simply
does not apply here, in either direction, and the margins are not apparatus inflation.

What *is* true about Lightstall is different and narrower. Its ETB is not a partly-modelled rider
with a residual — `cards.json` records it as **structurally absent**: `DeckTouchesOpponentZones`
keys only on `exile_opponent_top_cost`, which nothing in this deck has, so no opponent hand is ever
dealt and there is no direction in which the exile could move a goldfish result. What the screen
therefore measures is the honest remainder: **a one-mana Angel body**, the cheapest possible trigger
for Bishop of Wings, Righteous Valkyrie, Seraph Sanctuary and Giada. In a real game the ETB is a
genuine *two-sided* effect — it strips a card but hands the opponent a taxed copy of it — and this
sim cannot price either side. That makes the number neither a floor nor a ceiling for real play; it
makes it a measurement of the **enabler only**, and whether the disruption half is worth anything is
a judgement that lives outside this apparatus.

So the honest status is: **the exclusion was not earned, and Lightstall is back in scope.**
Screens 1–9 also measured it on an engine that under-rated every Angel-heavy arm (see *Engine defect
that invalidates the legend-count arms* below). It is re-measured properly in screen 11.

**Where the yardstick does NOT apply, and why the core recommendation survives it.** The yardstick
prices *cutting a card the sim cannot model*. It is the wrong comparator for a comparison where both
arms hold the same card SET and differ only in counts of well-modelled cards — there the relevant
guards are the se and the measured apparatus floor. That is the case for every load-bearing claim
here: Giada 4 vs 3 (`v_a` vs `v_b`, −0.0194 ± 0.0027, t = −7.29), Remote Farm 4 vs 2 (`v_a` vs
`v_rf2`, 0.0477 under corrected per-arm tables), and Dawnbringer 0 vs 1 (`v_a` vs `v_t2d1`). None of
those trades an unmodelled card for anything.

### What was NOT changed, and why that took a deliberate experiment

`v_nogreaves` (cut Lightning Greaves for a Lightstall) measured better in **all three** formats
(−0.034 / −0.030 / −0.038) with a good slope. It is not in the list. Its margin is *below* the
measured bias yardstick (−0.05t for cutting one Swords to Plowshares), and Greaves' unmodelled half
— shroud, i.e. protection from removal — is the same class of blindness as Swords'. Haste is
modelled and real; shroud is worth exactly zero against an opponent that never interacts.

## If a list is adopted

Screening produces a RANKING, not a shippable deck. Adoption is the USER'S call; nothing here has
been written to `decks/`. An adopted combination owes:

1. Its own mulligan table via `.claude/skills/mulligan-profile.md` (Angels generated a complete one
   in ~16 min — but budget MORE here: Remote Farm hands roll out 5–6 s each, and the R=10 bracket
   table for `v_a` alone took ~45 min).
2. **A bucket ruling.** The candidate's own 60 discovers **K=14**, but the union pool discovers
   **K=17**. `value_play.expected_buckets` is a USER ruling, never an agent's — it is currently 14
   in `decks/Angels/Angels.value.json` and has NOT been touched (the 17 was set only on a throwaway
   screening copy under `logs/`).
3. Its own regression ground truth, plus — noted while checking the 2HG convention — **Angels has no
   `angels2hg` case** in `test/regression_cases.sh`, though 19 other decks do. It was added
   2026-09-18 and the 2HG gate was never backfilled.
4. Archival of the predecessor per CLAUDE.md (`decks/Angels/v1-<slug>/`, and its `references/` move
   with it).

## Related

* `.claude/skills/deck-screening.md` — the per-combination loop and the alias route.
* `docs/design/tokens-are-never-equip-hosts.md` — the equip fix that preceded this work.
* `docs/design/mullgen-play-settings-vs-d1b3.md` — the separate deferred mulligan-settings question.
