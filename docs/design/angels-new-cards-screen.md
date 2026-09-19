# Fitting Lightstall Inquisitor, Lyra Archangel of Dawn and Remote Farm into Angels

**Status:** screening COMPLETE 2026-09-19, one recommendation, replicated on four disjoint seed
blocks. Cards are IMPLEMENTED and committed (`037bb8c5`), and an engine defect that invalidated the
first nine screens' legend-count arms is fixed and committed (`9134e4e3`). **No decklist has been
changed** — `decks/Angels/Angels.cod` is untouched and adoption is the user's call. This doc is the
record of what was measured and, more importantly, of what the measurements CANNOT see.

**The answer, in one line:** adopt **4 Lyra, Archangel of Dawn** (cutting all 3 Lyra Dawnbringer),
**4 Giada, Font of Hope** (up from 2 — the finding nobody was looking for), **4 Remote Farm** and the
Azorius Chancery's slot, **3 Lightstall Inquisitor**, and cut **Archangel of Thune 4 → 1**. Worth
**−0.384 / −0.366 / −0.386** turns at 20 / 30(2HG) / 40 life, replicated across seed blocks with a
largest drift of 0.0045t. Full list under *THE RECOMMENDED LIST* below.

**Scale:** **16,250,000 paired games** across fourteen screens (counted from each spec's own
`arms × formats × games`, not estimated), three formats, **fourteen disjoint seed blocks**, every
arm's **mainboard** deck-construction legal.

> **A correction to that last clause, because an earlier revision claimed simply "all arms
> deck-construction legal".** `deck_compare` carries the base `.cod`'s **sideboard** onto every arm —
> it must, because a wish deck needs it — and that side is `1 Lyra, Archangel of Dawn + 3 Legion
> Angel + 1 Lightstall Inquisitor`, unchanged in all fourteen screens. So an arm with 4 maindeck Lyra
> Archangel of Dawn is at **5 copies** counting the side, and likewise any arm at 4 Lightstall. My
> spec generators assert `<= 4` on the **mainboard only**, which is where the gap came from.
> **The measurement is unaffected and the comparisons stand:** Legion Angel's wish is
> `wish_requires_name`-pinned to its own name at both enumeration (`TutorNumericFilterOk`) and
> resolution (`PerformTutor`), so the sided Lyra and Lightstall are **unreachable** — mechanically
> inert, and identically inert in every arm. What it affects is the list a human would physically
> build, which is why *THE RECOMMENDED LIST* rebuilds the side to the 3 Legion Angels and nothing
> else. Future spec generators should assert main + side.
>
> **USER RESOLUTION (2026-09-19):** *"They are not actually in the sideboard. Those can be dropped. I
> put them there for consideration only."* So the sided Lyra and Lightstall were never a sideboard —
> they were the user's *introduction slots*, a note-to-self that these two cards wanted testing. The
> real sideboard is the 3 Legion Angels, the adopted list is therefore fully legal at main + side, and
> the only live lesson is the assertion gap in the generators.

**The two corrections this document had to make about itself**, both worth reading before the
numbers, because each one reversed a conclusion:

1. **Lightstall Inquisitor was excluded by a backwards application of this doc's own bias
   yardstick.** The yardstick prices *cutting* a card the sim under-models; every Lightstall arm
   *added* one and cut fully-modelled cards. See *THE RECOMMENDED LIST*.
2. **Screen 13's "the land axis is closed at 23" was an over-claim.** The 21–24 range spans 0.0042t —
   flat, not an optimum — and six of the deck's sixty cards are never cast at all, which biases the
   sim's land optimum *low*. See *The land count is NOT resolved by this apparatus*.

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
4. ~~**Lightstall Inquisitor is a short-game card that turns NEGATIVE in long games.** Helps at 20
   life, ~zero at 40, and at 60 life `lad4_li2` is measurably *worse* than `lad4` (slope +0.0188,
   t = +4.31). Include it only for a list aimed at short games.~~
   **SUPERSEDED by screens 11–14 — 3 Lightstall Inquisitor is in the recommended list.** Two
   independent reasons the original reading does not survive:
   * It was measured on the **pre-`9134e4e3` engine**, which pruned duplicate-legend casts and so
     systematically under-rated every Angel-heavy arm — and the Lightstall arms are the
     Angel-heaviest in the campaign, because a 1-mana Angel body is the cheapest possible way to
     add Angel count.
   * It generalised a **2-copy** arm at **60 life** to all counts at all lengths. Re-measured at 3
     copies, `L_t1_yv2` is negative in **all three** shipped formats including 40 life (−0.0138,
     t = −3.8) and replicated across four blocks with no shrinkage.

   What survives from the original is the *shape* of the observation, and it still matters: the
   card's value does decay with game length (`long/std` = 0.18), so it is a tempo card and not a
   bomb. That is an argument about how many, not whether.

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
| Lightstall → Greaves/Swords | a 1-drop Angel counted as a non-creature | ambiguous — and note this row said "Lightstall is not in the recommendation" until screens 11–14 put 3 copies in it, so the ambiguity is now load-bearing rather than moot |

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

**What screens 11–14 then showed, resolving that.** The prediction was right about direction and
understated the size. The 4-ofs did not merely survive: their margin over the 3-of arms **widened**,
and cutting either legend to 3 now turns *positive* (worse than `v_a`) at 40 life, which it did not
before. The defect's cost was concentrated exactly where the mechanism says it should be — in long
games, where a redundant legend's trigger chain and its legend-rule Spirit token get cashed more
often. One conclusion did move, and it moved because of this fix: **Lightstall Inquisitor**, whose
arms are the Angel-heaviest in the campaign and were therefore the most under-rated (standing
conclusion 4, now struck).

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

> **Forward reference, because this axis then flip-flopped.** Screens 12–14 re-tested 23 lands inside
> the Lightstall frame and it came back better *without* shrinking, three blocks running. So the land
> count has now failed a held-out test once (here, at 24→23 in the `v_a` frame) and passed one three
> times (there, in the `L` frame). That is the profile of an effect **too small for this apparatus to
> resolve**, which is exactly where *The land count is NOT resolved by this apparatus* lands — and it
> is why the decision is made on the never-cast-removal density argument instead of on a t-statistic.

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

### Screen 12 — the finals, held out on disjoint seeds (`spec12_finals.json`)

14 arms + base × 3 formats × 40,000 games, seed 2200000. Deltas vs `v_a`, negative is faster.

| arm | std | 2HG | long |
|---|---|---|---|
| `i3_t2_yv0` — screen 11's winner, **held out** | **−0.0572** (was −0.0558) | −0.0203 | −0.0125 |
| `i2_t2_yv1` — held out | −0.0437 (was −0.0481) | −0.0191 | −0.0107 |
| `i4_t1_yv0` Lightstall 4, Thune 1 | **−0.0785** (t −26) | −0.0255 | −0.0043 |
| `i4_t2_yv0_lad3` 4th from Lyra ArchDawn | −0.0488 | +0.0002 | +0.0162 |
| `i4_t2_yv0_g3` 4th from Giada | −0.0408 | +0.0040 | +0.0182 |
| `i3_t1_yv1` Thune 1 | −0.0684 | −0.0267 | −0.0057 |
| `i3_t3_yv0_g3` **control:** keep Thune 3, cut Giada | −0.0200 | +0.0095 | +0.0123 |
| `d1_i3_t1_yv0` Dawnbringer + 3 Lightstall | −0.0530 | −0.0180 | +0.0025 |
| `d2_i2_t1_yv0` two Dawnbringer | −0.0198 | −0.0011 | +0.0233 |
| **`i3_t2_yv1_p15` 3 Lightstall, Thune 2, 23 lands** | **−0.0631** | **−0.0280** | **−0.0214** |

**The Lightstall effect replicated with no shrinkage.** `i3_t2_yv0` came back **−0.0572** against a
screen-11 value of −0.0558 on a disjoint seed block. That is the test that killed three of six
adoptable-looking trades in an earlier campaign; this one passed it cleanly.

**The control arm earned its place.** `i3_t3_yv0_g3` keeps Archangel of Thune at 3 and cuts a Giada
instead: −0.0200 / +0.0095 / +0.0123, far worse than cutting Thune, and *worse than `v_a`* in two
formats. So Thune is the right donor and the legends are not — the same answer the legend-count arms
gave from the other direction. The 4th Lightstall confirms it again: funded from Thune it is the best
std arm (−0.0785), funded from Lyra ArchDawn or Giada it is worse than taking only three.

**A preference's price depends on what it displaces.** In screen 11 the Dawnbringer displaced the
third Archangel of Thune and was free (−0.0015). Here the marginal slot is a *Lightstall*, so
`d1_i3_t1_yv0` (−0.0530) sits ~0.026 behind `i4_t1_yv0` (−0.0785) at 20 life. Neither number is
wrong; they answer different questions, and the second is the one that matters once Lightstall is in
the list.

**One arm is top-3 in every format, and it is the one that cut a land.** `i3_t2_yv1_p15` (3
Lightstall, Thune 2, **23 lands**) is 3rd at 20 life, 1st at 30 and 1st at 40 — and at 40 life it is
nearly twice the next arm. Everything else that wins at 20 life fades by 40.

### Screen 13 — converge: both open axes CLOSED (`spec13_converge.json`)

14 arms + base × 3 formats × 40,000 games, seed 2300000. Deltas vs `v_a`.

| arm | std | 2HG | long |
|---|---|---|---|
| `L` = 3 Lightstall, Thune 2, 23 lands — **3rd held-out block** | −0.0593 (−0.0631, −) | −0.0271 | −0.0140 |
| `i4_t1_yv0` — held out | −0.0789 (was −0.0785) | −0.0283 | **+0.0000** |
| **`L_t0_yv2` Thune 0, Lightstall 4, 23 lands** | **−0.0954** (t −30) | **−0.0373** | −0.0011 |
| `t0_yv1_p16` Thune 0 at 24 lands | −0.0899 | −0.0257 | +0.0124 |
| `L_p14` 22 lands | −0.0582 | −0.0260 | **−0.0158** |
| `L_p13` 21 lands | −0.0551 | −0.0215 | −0.0099 |
| **`L_t1_yv2` Thune 1, Lightstall 3, 23 lands** | −0.0756 | −0.0349 | −0.0144 |
| `L_i4_t1` Thune 1, Lightstall 4 | −0.0844 | −0.0351 | −0.0102 |
| `L_lad3_yv2` Lyra ArchDawn 4→3 | −0.0422 | −0.0099 | **+0.0103** |
| `L_g3_yv2` Giada 4→3 | −0.0394 | −0.0046 | **+0.0105** |
| `L_d1_t1` + 1 Lyra Dawnbringer | −0.0586 | −0.0227 | −0.0009 |

**The land axis has an interior maximum — it is not another edge.** 23 lands ≈ 22 > 21 in every
format (−0.0593 / −0.0582 / −0.0551 at 20 life). That axis is closed at **23**.

**The Thune axis is closed by arithmetic.** Thune kept improving to 0, and 0 is the floor.
`L_t0_yv2` is the biggest number in this entire campaign at 20 life (−0.0954) and at 30 (−0.0373) —
**and it is worth nothing at all at 40 life (−0.0011).** That split is the blind spot showing itself
on the instrument: the advantage of replacing bombs with one-drops is entirely an advantage of
*speed*, and it evaporates exactly as the game gets long enough for card quality to matter.

**THE 4-OFS HELD FOR A THIRD TIME, in the frame the list has actually moved to.** Cutting Lyra
ArchDawn to 3 costs 0.017 / 0.017 / 0.024; cutting Giada to 3 costs 0.020 / 0.023 / 0.025 — and both
turn *positive* (worse than `v_a`) at 40 life. Screens 11, 12 and 13 now agree on independent seed
blocks: **the legend counts are right at 4, and the user's suspicion, while a good question, is
answered in the negative.**

**The Dawnbringer's price settles at ~0.013–0.017t.** `L_d1_t1` vs `L_t1_yv2` costs +0.0170 std,
+0.0122 2HG, +0.0135 long. Not free as it was against the third Thune, but small and stable.

### Screen 14 — the finalists head-to-head, and nothing shrank (`spec14_confirm.json`)

8 arms + base × 3 formats × 40,000 games, seed **2400000** — a fourth seed block, disjoint from
screens 11–13. Deltas vs `v_a`; `long/std` is the ratio of the 40-life delta to the 20-life delta.

| arm | std | 2HG | long | long/std |
|---|---|---|---|---|
| **`L_t1_yv2`** Thune 1, LI 3, YV 2, 23 lands | −0.0771 (t −26.5) | −0.0375 | **−0.0138** | 0.18 |
| `L` Thune 2, LI 3, YV 1, 23 lands | −0.0631 | −0.0299 | −0.0185 | 0.29 |
| `L_i4_t1` Thune 1, LI 4, YV 1, 23 lands | −0.0848 | −0.0380 | −0.0120 | 0.14 |
| `L_t0_yv2` Thune 0, LI 4, YV 2, 23 lands | **−0.0977** | −0.0419 | −0.0021 | **0.02** |
| `L_d1_t1` + 1 Lyra Dawnbringer | −0.0596 | −0.0295 | +0.0001 | −0.00 |
| `L_t0_p14` Thune 0, LI 4, YV 3, **22 lands** | **−0.1024** | **−0.0480** | −0.0113 | 0.11 |
| `L_t1_yv3_p14` Thune 1, LI 3, YV 3, **22 lands** | −0.0809 | −0.0408 | **−0.0190** | 0.23 |

**Every replicated arm came back with essentially zero shrinkage — this is the campaign's strongest
evidence that the screen is not fitting noise.** Across a *fourth* independent seed block:

| arm | screen 13 std | screen 14 std | screen 13 long | screen 14 long |
|---|---|---|---|---|
| `L_t1_yv2` | −0.0756 | −0.0771 | −0.0144 | −0.0138 |
| `L_i4_t1` | −0.0844 | −0.0848 | −0.0102 | −0.0120 |
| `L_t0_yv2` | −0.0954 | −0.0977 | −0.0011 | −0.0021 |
| `L` | −0.0593 | −0.0631 | −0.0140 | −0.0185 |
| `L_d1_t1` | −0.0586 | −0.0596 | −0.0009 | +0.0001 |

Largest drift 0.0045t, no sign changes, no reversals of rank. Contrast the earlier campaign where
held-out re-measurement killed 3 of 6 adoptable-looking trades: these are not that kind of effect.

**KEEPING ONE ARCHANGEL OF THUNE IS THE WHOLE DIFFERENCE, and it separates cleanly from the land
count.** Hold lands fixed at 23 and vary only Thune: Thune 1 (`L_t1_yv2`) is **−0.0138** at 40 life
while Thune 0 (`L_t0_yv2`) is **−0.0021**. Hold lands at 24 (screen 13) and the same ordering
survives, shifted: Thune 1 (`i4_t1_yv0`) +0.0000 vs Thune 0 (`t0_yv1_p16`) +0.0124. So ~0.012t of
long-game value rides on the *first* Archangel of Thune specifically, at any land count. That is the
one dial where the goldfish's known bias runs in the *conservative* direction — it under-rates the
bomb — so this margin is a floor, not a ceiling.

**`L_t0_p14` is the campaign maximum and should be trusted least of all.** Thune 0 at 22 lands is
−0.1024 / −0.0480 at 20 and 30 life, the biggest numbers ever measured here, and its `long/std` is
**0.11** — it is the purest expression of "trade bombs and lands for one-drops", which is exactly the
edit the apparatus is least qualified to judge (below, and again in the section after it).

### The land count is NOT resolved by this apparatus, and the bias runs toward MORE lands

Screen 13 claimed *"the land axis has an interior maximum… that axis is closed at 23."* **That was an
over-claim and this section retracts it.** The measured spread across 21–23 lands is
−0.0593 / −0.0582 / −0.0551 at 20 life: a total range of **0.0042t**, an order of magnitude *below*
the campaign's own −0.05t bias floor. Flat is not an optimum. 24 lands sits in the same band
(`i3_t2_yv0`, −0.0572, adjacent block).

**And this axis has already failed a held-out test once.** Screen 5's `w_land23` — the same 24→23
cut, in the `v_a` frame — **shrank by ~60% on held-out seeds** and was written off as noise, with
"24 lands stands" recorded as the conclusion. Screens 12–14 then found 23 better three blocks
running without shrinking. An effect that fails replication in one frame and passes it three times
in another, at a magnitude below the bias floor in both, is not an effect this apparatus can
resolve. Decide it on grounds the apparatus *can* be audited on — which is what follows.

At 40 life the axis does show something — 22–23 lands beats 24 by ~0.015t — but that is the format
where flooding is *most* exposed, and there is a specific, verified reason the sim floods more than
the real deck does:

**Six of the deck's sixty cards contribute nothing to the measurement, and they are all mana sinks in
real play.** Verified directly (300 logged games on the shipped list, `logs/probe_blanks/`): across
**1,315 `CAST_SPELL` actions, `Swords to Plowshares` and `Unexpectedly Absent` were cast zero
times.** They appear only as `DRAW` events (133 of them), and at **42.3% of turn-ends** at least one
is sitting stranded in hand.

This is not a card-data gap — both are fully implemented, and the AI's refusal is *correct inside the
model*. The engine says so itself:

* `src/core/Card.h:87` — *"the opponent never blocks, casts, or removes — see Combat.cpp, which has
  no blocker path at all."*
* `src/ai/DecisionProviders.cpp:14098` — *"Swords to Plowshares / Unexpectedly Absent LAST, and
  Main2. HONEST BRACKET: they are inert here only because this goldfish has no blocking — in a real
  game Swords on a blocker is precisely an attack-enabler."*

(An earlier revision of this document said the goldfish has "no opponent permanents". **That is
wrong.** `GoldFishRunner::PopulateOpponentSpawns` materialises a per-game-index blocker pattern, so
the opponent *does* control creatures — 1/1 through 6/6 bare P/T tokens. They are scenery: legal
targets that never block. Swords on one removes nothing that mattered *and hands the opponent life
equal to its power*, so declining it is right.)

**The consequence for the land count is directional and unavoidable.** The sim's live deck is ~54
cards, of which 24 are lands — **44%**. The real deck's live 60 cards at 24 lands is **40%**. The sim
therefore experiences more flooding than real play at any given land count, and its preference for
cutting lands is inflated by that margin. **The sim's land optimum is a lower bound on the real
deck's.** Cutting to 22 on its word is the one direction the evidence cannot support.

### ⚠ The caveat that governs how screens 11–14 should be read

**Every direction winning here does the same thing: it trades five-mana bombs and lands for one-mana
bodies.** That is precisely the edit this apparatus is least qualified to judge. The goldfish has no
sweeper, no opposing removal and no card-quality pressure, so four 1-mana 2/1 Angels that merely
have to *resolve* to trigger Bishop of Wings look strictly better than an Archangel of Thune that
would win a real game by itself. The repo has recorded this blind spot before — the sim **overstates
all-in cheap deploy** and **understates interaction** — and this is a textbook instance of it.

What the measurement does establish, and establishes solidly: **within a goldfish race, Lightstall
Inquisitor is a real card in this deck and Archangel of Thune is the most expendable slot.** What it
cannot establish is how far to take that, because the first sweeper or removal spell prices the
difference and the sim never shows one. The numbers below are reported as measured; the judgement
about how much of the Thune-to-Lightstall trade to actually make is the user's, and the honest
recommendation is deliberately more conservative than the maximum the screen points at.

**The `long/std` column is the instrument auditing itself, and it is the single most useful number in
the campaign.** An arm whose gain is real deck quality keeps some of it when the game has to go long;
an arm whose gain is speed collapses toward zero. `L_t0_p14` and `L_t0_yv2` — the two biggest numbers
ever measured here — score **0.11** and **0.02**. `L_t1_yv2` scores **0.18** and `L_t1_yv3_p14`
**0.23**. Ranking by raw std delta ranks by how thoroughly an arm exploits the blind spot.

### Screen 15 — the legend question asked PROPERLY, and two of my answers overturned

18 arms + base × 3 formats × 40,000 games, seed **2500000**. Reference is the user's constrained
list. **This is the first screen in the campaign that cut a legend into a card worth playing.**

**THE CONFOUND THIS SCREEN EXISTS TO FIX.** Every legend-cut arm in screens 11–14 paid for the cut
with a Youthful Valkyrie or an Archangel of Thune:

| screen | arms | donor |
|---|---|---|
| 11 | `l3_yv3`, `g3_yv3`, `l3_g3_yv4`, `g2_yv4` | Youthful Valkyrie |
| 13 | `L_lad3_yv2`, `L_g3_yv2` | Youthful Valkyrie |
| 12 | `i4_t2_yv0_lad3`, `i4_t2_yv0_g3` | **Archangel of Thune** |

Those are the two cards this same campaign calls most expendable. So "the 4-ofs are confirmed on
four independent seed blocks" only ever established **the 4th legend beats the worst card in the
deck**. Screen 12's arms are doubly confounded — they cut a legend *and* add a Thune, and adding a
Thune is independently bad. This is the failure mode `isolate-the-axis-dont-difference-decks`
already warned about, walked into anyway.

**Against a GOOD donor the 4-ofs hold, and the case gets STRONGER with game length** (positive =
worse):

| trade | std | 2HG | long |
|---|---|---|---|
| 4th Giada → **4th Lightstall** | +0.0168 (t +6.8) | +0.0275 (t +9.9) | **+0.0350 (t +11.1)** |
| 4th Lyra ArchDawn → **4th Lightstall** | +0.0125 (t +8.7) | +0.0213 (t +13.8) | **+0.0311 (t +17.3)** |
| 4th Giada → 2nd Archangel of Thune | +0.0418 | +0.0399 | +0.0356 |
| Giada back to **2** (+ LI 4, Thune 2) | +0.0785 | +0.0818 | **+0.0809** |

The cost of cutting a legend **grows** from 20 to 40 life — the opposite of the Thune-cut and
land-cut numbers, which shrank to nothing. Under a late-weighted ranking the 4-ofs get *more*
justified, not less.

**Lightstall's 4th copy is a 20-life-only gain.** vs a land: −0.0101 (t −3.9) / −0.0035 (t −1.3) /
−0.0008 (t −0.2). vs a Youthful Valkyrie: −0.0073 (t −4.7) / +0.0006 / +0.0008. Never worse, so 4 is
defensible — but it has **1v1 proof and no 2HG proof**. Lightstall looked strong in screens 11–14
because it was replacing *Archangel of Thune*; with Thune pinned at 1 there is nothing good left for
it to replace.

**The Swords floor, measured in this exact frame: −0.0347 / −0.0432 / −0.0541** — and *largest at 40
life*, because a longer game draws more dead cards. Cutting a never-cast card is manufactured gain;
that number cannot be lined up against any other arm's.

**At 40 life `v_a` and the user's list are the same deck** (−0.0026, t −0.58). Every remaining
argument here is worth ±0.03t against a ~0.37t win already banked.

### ⚠ THE APPARATUS IS MIS-BUCKETED — which makes the land and Lightstall answers PROVISIONAL

Raised by the user, 2026-09-19: *"The card type changes should ideally generate a new mulligan
profile… otherwise it is too biased."* They were right, and it is measurable.

Screens 1–16 all share ONE keep table (`alias/`). Its buckets assert **two false equivalences**:

```
buckets[0] = [Lightning Greaves, Lightstall Inquisitor, Swords to Plowshares, Unexpectedly Absent]
buckets[1] = [Archangel of Thune, Lyra, Archangel of Dawn]
```

A 1-mana 2/1 Angel — the cheapest trigger for the whole lifegain engine — is treated as
interchangeable with a **Swords that is never cast**, and the 3-drop is treated as the 5-drop.
**Bucket discovery on the new list returns K=16 where that table has 14: the two lumpings, counted.**
(Individual arms differ: `U` discovers K=15.) The table is also stale — built at commit `15ae9593`,
before `9134e4e3` changed searched play.

A replacement table generated on a new-cards list (`logs/angels_w`, every candidate at ≥1 so all get
bucketed) took **16 minutes** and passed validation: keep **−0.1903t**, bottoming **−0.0789t**, 16/16
seeds each. **Its buckets are singletons** — Lightstall and Lyra ArchDawn separated.

**Status of the results above under this defect:**

* **Probably safe — the legend 4-ofs.** The arms differ by one card and the effect (t +11…+17 at 40
  life) dwarfs any plausible table bias.
* **PROVISIONAL — the Lightstall 4-vs-3 answer**, which is precisely the mis-bucketed card.
* **PROVISIONAL — the entire land ladder**, including the "don't cut lands" reversal, because a
  shared table is a hidden prior on the mana:spell ratio (`coverage-is-not-calibration`).

The user's framing of the fix is the one adopted: *"we are not stuck with one profile. We can always
run a test on multiple to help figure out how much the results could be biased… even without
generating a mulligan profile for every potential option."* `--with-floor U,d1_li3,p13_yv2` with
`"bracket": "generate"` gives each arm its OWN table and pools every cell in one batch, so the gap
between an arm's delta under the shared table and under its own table **is** the bias.

**Three tooling traps found while doing this, all worth knowing:**

1. **`expected_buckets` is a USER ruling and `keepgen` will refuse without it.** On a K mismatch it
   errors and instructs you to set `expected_buckets`. Do not. **Unset it** — K as discovered — which
   declines to install a policy rather than inventing one.
2. **`--preflight`'s fall-through % is wrong for a table not built on the spec's `base` deck.** It
   computes `enum_cells(buckets, counts)` against the base, so a per-arm table that is *complete for
   its own deck* reads as 65–81% fall-through. Verify against the table's own `entries[].comp`
   maxima before believing it.
3. **A spec's `replace` map only drives card NUMBERING** (`deck_compare.py:625`), not the keep table.
   The bucketing — and therefore the bias — lives entirely in the table itself.

## THE RECOMMENDED LIST (`L_t1_yv2`)

> **SUPERSEDED IN PART, 2026-09-19.** The section below predates screen 15 and the user's own
> deckbuilding rulings (1 Lyra Dawnbringer, ≥1 Archangel of Thune, 4 Lightstall, 2 Swords, ≥21
> lands, keep Serra). Its **legend counts stand and are now better supported**; its **land count and
> Lightstall count are provisional** pending the apparatus-bias bracket above.

**Read the size of the decision before reading the decision.** The core change — 4 Lyra Archangel of
Dawn, 4 Giada, 4 Remote Farm, no Azorius Chancery, no Lyra Dawnbringer, Archangel of Thune cut down —
is worth **−0.38 to −0.39t** against the shipped list and is not in dispute: it replicated across four
independent seed blocks in all three formats. Everything argued over below moves **0.02–0.04t**, i.e.
under 10% of the win already banked. Any of the finalists captures ~95% of the available gain, so
this is a choice to make once and not agonise over.

Pooled deltas vs the shipped `decks/Angels/Angels.cod`, 40,000 games per format per block,
replicated on seeds 2300000 **and** 2400000:

| format | Δ avg win turn vs the shipped list | vs the previous recommendation (`v_a`) | drift s13 → s14 |
|---|---|---|---|
| std (20 life) | **−0.3838** | −0.0771 (t = −26.5) | −0.0015 |
| **2HG (30 life, 2 heads)** | **−0.3663** | −0.0375 (t = −11.9) | −0.0026 |
| long (40 life) | **−0.3861** | −0.0138 (t = −3.8) | +0.0006 |

(Drift is signed, not absolute: **negative means the effect grew** on the held-out block. Two of the
three grew. Nothing shrank by more than 0.0006t, which is the point — a selection-bias artefact
shrinks toward zero on new seeds and these did not.)

```
CREATURES (27)                        LANDS (23)
  4 Lyra, Archangel of Dawn   NEW      15 Plains          (19 -> 15)
  4 Giada, Font of Hope       (2 -> 4)  4 Seraph Sanctuary (unchanged -- 4 is right, tested)
  3 Lightstall Inquisitor     NEW       4 Remote Farm      NEW
  1 Archangel of Thune        (4 -> 1)  0 Azorius Chancery (1 -> 0)
  0 Lyra Dawnbringer          (3 -> 0)
  2 Youthful Valkyrie         (4 -> 2) OTHER (10)
  4 Righteous Valkyrie                  2 Serra the Benevolent  (2 is right, tested)
  4 Resplendent Angel                   1 Sol Ring
  4 Bishop of Wings                     1 Lightning Greaves   <- KEPT, see the yardstick
  1 Legion Angel                        3 Swords to Plowshares
                                        3 Unexpectedly Absent
SIDEBOARD (3)
  3 Legion Angel   <- the wish pool, and nothing else
```

27 + 23 + 10 = 60, no nonbasic above 4 copies. Verified mechanically against
`spec14_confirm.json`'s own `L_t1_yv2` combination rather than hand-counted — the previous revision
of this block mis-stated its category totals as 24/24/12 when `v_a`'s actual split was 26/24/10.

### Why this arm and not the bigger number

`L_t0_p14` measures **−0.1024** against `v_a` at 20 life — 33% better than the arm recommended here —
and it is declined deliberately. Two reasons, both measured rather than asserted:

1. **Its gain does not survive a long game.** `long/std` = 0.11, and the related `L_t0_yv2` is 0.02.
   Cutting Archangel of Thune to zero is worth ~0.10t at 20 life and **nothing at all** at 40. The
   ~0.012t that rides on keeping the *first* Thune is stable at both 23 and 24 lands (above), and it
   is the one dial where the apparatus errs conservatively — a goldfish with no blockers, no sweeper
   and no removal cannot price a 5-mana bomb, so that margin is a floor.
2. **It goes to 22 lands, the one direction the evidence cannot support.** The land axis is flat to
   within 0.0042t across 21–24, and the deck's six interaction spells are cast **zero times in 1,315
   casts**, which inflates the sim's effective land density from 40% to 44% and biases its land
   optimum *low*. See *The land count is NOT resolved by this apparatus* above.

`L_t1_yv3_p14` (−0.0809 / −0.0408 / **−0.0190**, `long/std` 0.23) is the strongest counter-argument in
the set: it edges `L_t1_yv2` in all three formats and posts the campaign's best 40-life number. It is
still declined, on one ground only — it buys that edge by cutting the 23rd land for a third Youthful
Valkyrie, and no single format clears significance head-to-head (t = −1.58 / −1.18 / −1.68). A
~0.004t effect at t ≈ 1.5 is precisely the size that held-out re-measurement has killed before. **If
the user disagrees with the land-density argument, this is the arm to take instead** — it is one card
different and it was the better arm on every axis measured.

### The three questions the user asked, answered

* **"Suspicious of keeping 4-ofs for 2 Legendary cards."** Answered in the negative, on **four**
  independent seed blocks. Cutting Lyra Archangel of Dawn or Giada to 3 costs 0.017–0.025t and turns
  *positive* (worse than `v_a`) at 40 life. The suspicion was well-founded against the old engine,
  which was pruning the duplicate cast outright; with that defect fixed a redundant legend is a
  Bishop/Righteous/Seraph/Youthful/Giada trigger chain plus a Spirit token from its own legend-rule
  death. **Keep both at 4.**
* **"I also like keeping at least a 1-of for Lyra Dawnbringer."** It costs **+0.0138t at 40 life**
  and +0.0175 at 20 (`L_d1_t1` vs `L_t1_yv2`, screen 14) — small, stable, and *not* free as it was
  when it displaced the third Archangel of Thune. **This apparatus cannot evaluate the reason given
  for wanting it.** The stated reason is life total as a defensive resource on a wide board; the
  goldfish's opponent never attacks, never blocks, never removes, so gained life is worth exactly
  zero here except where a card reads it (Righteous Valkyrie's threshold). The measured 0.014t is the
  *whole* cost and none of the benefit. This is a preference the numbers should not overrule.
* **"Lightstall Inquisitor didn't make the cut at all?"** It should have, and the exclusion was a
  backwards application of this document's own bias yardstick (see the correction below). 3 copies,
  replicated across three blocks with no shrinkage. **In the list.**

**The sideboard must be rebuilt, and this is not cosmetic.** `decks/Angels/Angels.cod` currently
sides `1 Lyra, Archangel of Dawn + 3 Legion Angel + 1 Lightstall Inquisitor`, where the Lyra and the
Lightstall are the user's *introduction slots*. Mainboarding 4 Lyra with one still in the side is
**5 copies — illegal**, so that Lyra must come out of the side regardless of which arm is adopted.
The sided Lightstall is different: 3 main + 1 side = 4, which is legal, but it is also pointless —
Legion Angel's wish is `wish_requires_name`-pinned to its own name, so the reachable pool is exactly
the 3 Legion Angels and **every other sideboard slot is mechanically inert here.** Hence the
recommended side is the 3 Legion Angels and nothing else.

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

**And the yardstick's own meaning is now verified rather than assumed, which makes it stronger.** The
300-game probe above found Swords to Plowshares cast **zero times in 1,315 casts** — it is a total
blank in this apparatus. So "cut one Swords for a live card" is, mechanically, *replace one dead card
with one real one*, and that edit measured **−0.05t**. That is exactly what a bias floor should be:
the score the instrument hands out for an edit whose entire measured benefit comes from the sim's
inability to use the card it cut. Any arm that gains less than ~0.05t by cutting an under-modelled
card has gained nothing that will survive contact with a real opponent. The recommended list cuts no
such card — Swords 3, Unexpectedly Absent 3 and Lightning Greaves 1 are **unchanged in every arm in
the campaign**, so the yardstick never bites on it.

## If a list is adopted

Screening produces a RANKING, not a shippable deck. Adoption is the USER'S call; nothing here has
been written to `decks/`. An adopted combination owes:

1. Its own mulligan table via `.claude/skills/mulligan-profile.md` (Angels generated a complete one
   in ~16 min — but budget MORE here: Remote Farm hands roll out 5–6 s each, and the R=10 bracket
   table for `v_a` alone took ~45 min). **`L_t1_yv2` will want more than `v_a` did**: it holds three
   1-mana Angels, so the keep/bottom frontier moves and the shared screening table — which aliased
   Lightstall onto Lightning Greaves/Swords, i.e. onto a *non-creature* — is biased in a direction
   this campaign never resolved (see the alias-bias table above). That alias is harmless for a
   ranking where every arm shares it; it is NOT harmless for the shipped table.
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
