# Fitting Lightstall Inquisitor, Lyra Archangel of Dawn and Remote Farm into Angels

**Status:** screening IN PROGRESS 2026-09-18. Cards are IMPLEMENTED and committed (`037bb8c5`);
**no decklist has been changed** — `decks/Angels/Angels.cod` is untouched and adoption is the
user's call. This doc is the record of what was measured and, more importantly, of what the
measurements CANNOT see.

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
| **Lightstall Inquisitor** `{W}` 2/1 Angel Wizard, Vigilance | ETB: each opponent exiles a card and may play it, taxed `{1}` | **Only the body.** The ETB is structurally inert (no opponent cast path) and vigilance is inert (the opponent never attacks). What is left is a ONE-MANA ANGEL, i.e. the cheapest possible lifegain trigger for Bishop of Wings / Righteous Valkyrie / Seraph Sanctuary / Giada. A screen that likes this card is liking the enabler, never the disruption. |
| **Remote Farm** (land) | enters tapped w/ 2 depletion counters; `{T}`, remove one: add `{W}{W}`; sacrifice at zero | **The upside only** — see below. |

## THE CENTRAL CAVEAT: two results are monotone to the edge of the tested range

Both `Remote Farm count` and `Lyra count` kept improving at every step out to the largest value
tested (8 Remote Farm; 6 Lyra). **A real cost curve bends. Monotone-to-the-boundary is the
signature of a cost the model never pays**, and in both cases the mechanism is the same and is
measured:

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

### Screen 3 — the life SLOPE, 20/40/60 life (`spec3_slope.json`) — IN FLIGHT

Launched 2026-09-18, log `logs/deckcmp/angels_newcards/screen3.log`. Arms: `lad4`, `lad6`,
`lad4_li2`, `lad4_rf2`, `lad4_rf6`, `rf6_only`. **How to read it:** the question is the SLOPE of
each arm's delta against starting life, because that is the closest available proxy for the cost
the sim cannot bill. An arm whose advantage GROWS with life (Lyra) is a real engine; one whose
advantage SHRINKS (Lightstall, and possibly high Remote Farm counts) is buying tempo the real game
will not pay for. If `lad6 − lad4` shrinks as life rises, that is the dead-legend cost becoming
visible and argues for the lower count.

## Standing conclusions

1. **Lyra, Archangel of Dawn is the real card. Adopt some number.** Confirmed on held-out seeds,
   robust in both modes, and it gets BETTER in longer games — the opposite of a tempo artifact.
   **4 copies is the well-supported count** (`lad4`, −0.138/−0.197). 5–6 measure better but run
   into the under-priced dead-legend cost; that is a judgement call, not a measurement.
2. **She is a 3-drop, so the user's "cut 5-drops" framing works better than expected** — `lad4`
   cuts FOUR five-drops (Thune 4→2, Lyra Dawnbringer 3→1), the top of the range guessed.
3. **Azorius Chancery does not earn its slot.** Negative in all three screens (−0.0048, −0.0083,
   −0.0078). Small but consistent. Cut it.
4. **Lightstall Inquisitor is marginal and mode-dependent.** Helps at 20 life, ~zero at 40. Its
   only real-game value (the ETB) is unmodelled, so the sim can neither credit nor debit it. This
   is a card to decide on judgement, not on this screen.
5. **Remote Farm: the sim wants an absurd number of them and cannot be trusted on the count.**
   Direction is real (the deck is mana-hungry and the burst genuinely accelerates it); the optimum
   is not measurable here.

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

**The one live apparatus caveat.** Aliasing Lyra into Archangel of Thune's bucket means the
mulligan treats a **three**-drop as a **five**-drop. That holds the keep policy fixed across arms
by construction, which is what ISOLATES the in-play difference — but it is precisely the case where
the new card would want a DIFFERENT keep policy, and the alias cannot see that. Given the effect
sizes (0.14–0.35 against a ~0.005–0.01 floor, a 14–40x margin) this will not flip a sign, but it
could move the optimal counts. The sanctioned fix is one table generated over the REAL candidate
decklists with **`MTG_KEEP_ARM_DECKS`** (per-bucket MAX across arms, every unreachable cell
dropped — Rule 0a's "union TABLE", never a union deck). **NOT YET RUN.**

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

## If a list is adopted

Screening produces a RANKING, not a shippable deck. An adopted combination owes its own artifacts
through `.claude/skills/mulligan-profile.md` (Angels generates a complete table in ~16 min) and its
own regression ground truth, and the predecessor list is archived per CLAUDE.md
(`decks/Angels/v1-<slug>/`, and its `references/` move with it).

## Related

* `.claude/skills/deck-screening.md` — the per-combination loop and the alias route.
* `docs/design/tokens-are-never-equip-hosts.md` — the equip fix that preceded this work.
* `docs/design/mullgen-play-settings-vs-d1b3.md` — the separate deferred mulligan-settings question.
