# Knights copy-count campaign — WhiteKnights (mono-white) and Knights (R/W)

**User request, 2026-09-26:** *"I would like to start analyzing which cards in the deck (and in the
other deck) should change number of copies. I would like to include Remote Farm in the test first,
then move on to creatures. … I would like to leave this deck mono-white for the time being and likely
going forward as well."*

Scope read: copy-count screening (`deck-screening.md`) over the two Knight decks' **existing** pools,
plus one introduced card (Remote Farm). **WhiteKnights stays mono-white** — no arm in this campaign
adds a non-white source or a non-white spell, and that constraint is standing, not per-screen.
"The other deck" is read as `decks/Knights/` (the R/W list that shares this one's Knight core); say so
if a third list was meant.

This is the **per-combination** loop, not adoption. A combination that wins here still owes its own
mulligan profile, value leaf and regression ground truth before it ships.

---

## The apparatus, and why each choice is forced

| | WhiteKnights | Knights |
|---|---|---|
| keep table | K=14, 42,271 cells, R=40 | K=13, 43,038 cells, R=60 |
| `hand_score_threshold` | `-1e18` → **NO_GATE**, `card_scores` unreachable | **0.2453 → LIVE gate** |
| `value_play` | `{expected_buckets: 14}` only → no adopted policy, so the screen runs the **engine default d5/b20**, which is also its d5 suite cell | `target_depth 5, budget_ms 20, escalation_cap 5` |
| play settings used | d5 / 20 ms | d5 / 20 ms |

**Remote Farm is aliased into the Plains bucket, not generated for.** It is an introduced card, so the
shipped table does not bucket it, and `Decide`/`DecideBottom` would return `present=false` for any hand
holding it — a silent one-sided fall-through to the static keep on the only arm that plays the card.
The approved route (`deck-screening.md`, user directive 2026-09-02) is
`scripts/alias_card_into_bucket.py`, which adds the name to the bucket of the card it replaces. K is
unchanged and nothing is generated.

Measured, that is exact rather than approximate: because every Remote Farm arm trades Plains 1-for-1,
the land bucket's total is constant, so the arm's composition space is **identical** to base's and the
fall-through rate is **0.0000% on all four arms** (`logs/wk_screen/fbrate.py`).

* **The assumption this route makes, stated plainly:** the mulligan decision treats Remote Farm *as* a
  Plains. That holds keep/bottom policy fixed across arms by construction, which is what isolates the
  in-play difference — but it cannot see that a tapped, self-sacrificing double-land might want a
  *different* keep policy. If the thesis were "this land changes which hands you keep", this route
  would not measure it.
* **Both sidecars are copied next to the aliased table.** The engine resolves sibling models
  directory-relative off the *profile* path, so a scratch profile without its `.value.json` silently
  detaches the value leaf — worth 1.35–84.8x.
* **`"pool_table": false`** on every spec: a union table is banned (Rule 0a) and with the alias in place
  nothing should ever want one. The flag makes that impossible rather than unlikely.

**`max_fallback: 0.02` on the creature specs, from the measured number.** Cuts never overflow; a
*raise* past the table's enumerated cap makes untabled hands reachable on that arm alone. Measured:
Benalish Marshal 2→3 is 0.1023%, 2→4 is 0.3876%, Hero 3→4 is 0.0072%, and the two 1-of→2-of raises
(Silverblade Paladin, Adeline) are **1.1864% / 1.1936%** — just over the 1% default, which would have
sent the whole screen to a pool table. At ~0.0007 t of one-sided bias that is a tenth of the measured
apparatus floor, so raising the budget is the right call and dropping the table (~0.063 t on *both*
arms, ~22x wall) would be far worse.

**Three formats, per arm** (`std` 20 life / `2hg` 30 life + 2 heads / `long` 40 life), `max_turns 18`,
each compared to **its own** format's base. This follows the Angels campaign, where the ordering of
arms *widened* with game length and one arm outright **sign-flipped** between 20 and 60 life. Ranking
on 20-life speed alone would have adopted that arm. Compare deltas across formats, never levels.

---

## Why these arms — the evidence each hypothesis rests on

The `card_scores` marginals in each profile are a **hand-score fit against a passive goldfish**, not a
win-turn contribution. They are used here only to *choose what to measure*.

### WhiteKnights (`logs/wk_screen/cr_whiteknights.json`)

The ledger (`analysis-WhiteKnights.md` §2, reading 3) already names this screen as the required
follow-up: *"Several second copies price negative (Dauntless Bodyguard −0.202, Worthy Knight −0.125,
Venerable Knight −0.080) … the right follow-up is `deck-screening.md`, not a hand edit — and per the
cut-ladder lesson it should test **adding** a third/fourth copy of an expensive-to-cut card too, not
only cuts."*

Cut candidates, by marginal score: Valiant Knight −0.123 (its `{3}{W}{W}` activation measured
nearly unaffordable), Acclaimed Contender −0.115, Aether Vial −0.083, Hero of Bladehold −0.064,
Knight Exemplar −0.045, and the negative second copies above.

The sink is **Benalish Marshal** on group A — a `{W}{W}{W}` anthem the deck runs only 2 of, which is
exactly the card a mono-white constraint makes affordable and a splashed list cannot. Group B holds the
cut fixed (Valiant Knight → 0) and varies the sink instead, so "what is the free slot worth to each
candidate" is answered independently of "which card should leave". Group C moves the land count, which
cannot be tested alone: a 60-card deck makes every cut also an add.

`a_lg0_bm3` cuts **Lightning Greaves**, on two independent grounds: the reference bench found the
engine correctly declining it (*"Greaves is `{2}` — the whole turn — and adds no power; its shroud is
inert vs a passive opponent and its haste only pays on a creature cast that same turn"*), and the
Angels campaign measured cutting Greaves better in all three formats.

### Knights (`logs/wk_screen/cr_knights.json`)

Two signals here are much louder than anything in WhiteKnights:

* **`Aether Vial` is the worst card in either deck** — −0.113 for the first copy and **−0.364** for the
  second, at 4 copies. Laddered 4→3→2→0.
* **`Plains` prices NEGATIVE on every copy** (−0.002 / −0.083 / −0.121) while all three nonbasics price
  positive, because Plains cannot cast Inspiring Veteran or Haytham Kenway in an R/W deck. So the Vial
  slots are laddered into Plains *and* the Plains count is moved in both directions.

Knights is nearly all 4-ofs, so its only legal raises are Haytham Kenway 3→4, Adeline 1→2, and Plains.
That is why Haytham is the default sink — see the caveat below.

---

## What the measurement CANNOT say (state these with any result)

1. **Do not read "cut the interaction".** Swords to Plowshares (−0.257) and Unexpectedly Absent
   (−0.229) are the worst-scoring cards in WhiteKnights because the passive opponent presents nothing
   to target. No arm in this campaign cuts them, and no future arm should on this evidence.
2. **Aether Vial's real value is partly unmodelled.** The engine models the charge/deploy heuristic,
   but not flashing in creatures to dodge sorcery-speed answers or countermagic. A Vial cut is
   therefore *flattered* here. The 4th copy's −0.364 is still a very large in-model signal.
3. **Haytham Kenway is the most under-modelled sink.** Its protection and ETB exile are both inert in
   goldfishing (the opponent's creatures never block or attack). What remains — the +2/+2 Knight lord
   and the legend rule at 3 copies — *is* modelled, so arms that add Haytham are under-credited
   relative to real play, not over-credited. That is the safe direction for a cut and the wrong one
   for an add: read an arm that *adds* Haytham as a lower bound.
4. **Battle cry is the most goldfish-inflated number in the WhiteKnights ledger** (0.20 t), per the
   user: *"Accorder Paladin is notably better in goldfishing than in real play"* — a 3/1's toughness is
   free against an opponent that never blocks. Accorder Paladin is already at 4 and no arm moves it,
   but any conclusion that leans on battle-cry density inherits that bias.
5. **Acclaimed Contender's one residue is provably inert here** ("legendary artifact" is a
   supertype+type pair `etb_dig_subtypes` cannot express; neither deck holds one), so Contender cut
   arms are NOT flattered by a modelling gap. Its `Knight/Aura/Equipment` filter was widened this
   session for exactly this reason.
6. **A screen's `base` level is not the deck's strength.** The paired shuffle moves every opening hand.
   Quote deltas, never levels, and never diff a level against the ledger's 4.320.

---

## Run order and state

| # | screen | spec | state |
|---|---|---|---|
| 1 | Remote Farm, WhiteKnights (rf1–rf4) | `logs/wk_screen/rf_whiteknights.json` | preflight OK, 0.0000% fall-through |
| 2 | Remote Farm, Knights (rf1–rf4) | `logs/wk_screen/rf_knights.json` | preflight OK, 0.0000% fall-through |
| 3 | creature/count, WhiteKnights (14 arms) | `logs/wk_screen/cr_whiteknights.json` | preflight OK |
| 4 | creature/count, Knights (11 arms) | `logs/wk_screen/cr_knights.json` | preflight OK |

Each is ONE pooled `mtg --batch` (arms × formats in one queue, never a wave per arm or per format).
Screens run **serially** — two concurrent screens would halve the box and corrupt each other's
per-arm ms/game, which is the figure used to size the next run.

**Floor brackets.** `--floor` is available and free for **cut-only** arms (the default bracket
re-weights the shipped R=40/R=60 raw to the arm's counts — deterministic, 1.5 s, no generation). It is
**unavailable** for any Remote Farm arm (an introduced card has no cells to reweight) and for the raise
arms (a bucket past the source grid), where it would fall back to *generating* a table — which the
approved route forbids. So: bracket the cut arms, and report the Remote Farm floor as **unmeasured**,
noting only that the alias makes every arm share one table over an identical composition space, which
bounds the asymmetry by construction rather than by measurement.

**The winner of any multi-arm screen is selection-biased** — confirm it with
`--confirm <tag>` on the disjoint held-out block before recommending anything.

## Open questions for the user (surfaced, not blocking)

1. **Ranking weights.** The Angels campaign ranked arms late-weighted — `long` 0.5 / `2hg` 0.3 /
   `std` 0.2, *"not by 20-life speed"* (user, 2026-09-19). Applied here by default; all three per-format
   deltas are reported raw so it can be re-weighted.
2. **Do both lists stay?** `Knights` and `WhiteKnights` share a Knight core and the ledger's open
   question 4 already asks this. Optimising copy counts in both assumes both stay.
3. **Knights' sideboard is full of main-deck candidates** — Hero of Bladehold, 2 Accorder Paladin,
   Kinsbaile Cavalier, Student of Warfare, Valiant Knight. Promoting any is an *introduced card* for
   that list and needs its own alias; it is out of scope for screens 1–4 and would be screen 5.
