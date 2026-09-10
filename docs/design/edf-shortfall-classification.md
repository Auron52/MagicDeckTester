# EldraziDisplacerFlicker: classification of the four +1 search-vs-human shortfalls

Companion to `docs/design/analysis-EldraziDisplacerFlicker.md` (the deck ledger). Read-only
analysis, 2026-09-10, branch `phase-1-2-deck-analyzer`.

**Provenance.** All numbers below were taken on the pre-existing `build/Release/mtg`, which
reproduces the fresh `ref_bench` win turns exactly at 20 ms (5 / 5 / 5 / 7). Concurrent work on
the box then rebuilt the binary mid-session (`543f540d`, viewer-only commits); **every ladder
cell was re-run on the rebuilt binary and came back with identical digests** — the whole table is
binary-invariant across that rebuild.

Fresh `ref_bench` (`test/ref_bench.json`, traces in `logs/ref_bench_cx8whs39/`) has EDF at
**human 4.5 / search 5.25 over 8 references, 5 shortfalls**. `claude_s1_gi0` (+2) is already
diagnosed in the ledger. This note classifies the other four, all **+1**:

| ref | human | search (shipped d5/20 ms) |
|---|---|---|
| `claude_s2_gi1`   | T4 | T5 |
| `claude_s5_gi4`   | T4 | T5 |
| `claude_s10_gi9`  | T4 | T5 |
| `claude_s11_gi10` | T6 | T7 |

## Verdict up front

**They do NOT share one mechanism — they split 3 / 1.**

* **s2, s5, s11 — (b) RANKING under a budget-starved tail estimate.** The human's plan is
  enumerated at the divergence node in every case; it loses on a tail estimate the search has
  no budget to compute. Raising only the virtual budget makes the search match the human turn:
  s2 and s5 at **100 ms**, s11 at **500 ms**. This is the class the ledger's Session 9 already
  named ("pure 20 ms budget starvation, the class the value leaf exists for"), and EDF still has
  no value sidecar.
* **s10 — (a) ENUMERATION / pricing.** Budget-**immune**: win turn 5 with a **byte-identical
  digest** (`ed4ef6bb450e3cf8`) at 20 / 100 / 500 / **2000 ms**. The missing construct is the
  same-main **bank → dig → wish → deploy → sink** chain: `FlickerGoOffCount` / `ScanHandSinks`
  will not price a finisher that is still in the *library* behind loop draws, so no loop is ever
  proposed and the apply's opportunistic mid-loop dig never gets a loop to ride. Same class as
  s1_gi0's T3 line and the generation campaign's "node 3" residual.

None of the four is (c) MULLIGAN/BOTTOM — `ref_bench` forces the recorded opening
(`force_mulligan` in the manifest), and s5's mulligan-to-6 + bottom-Brushland is reproduced
exactly. None is (d) MANA/PAYMENT: no line was found where the search held the cards and the
payment/tap order stranded it.

## Measurement: budget ladder (virtual ms, deterministic; `--batch`, d5 default)

Win turn; identical digests noted. `mull` forced to the reference's.

| ref | human | 20 ms | 100 ms | 500 ms | 2000 ms |
|---|---|---|---|---|---|
| s2_gi1   | 4 | 5 | **4** | **4** | — |
| s5_gi4   | 4 | 5 | **4** | **4** | — |
| s11_gi10 | 6 | 7 | 7 | **6** | **6** (digest = 500 ms) |
| s10_gi9  | 4 | 5 | 5 | 5 | 5 — **all four byte-identical** |
| s1_gi0 (context) | 3 | 5 | 5 | 5 | — (100 ms digest = 500 ms) |

`MTG_EDF_LIB_ROUTE=1` (the ledger's implemented-but-default-OFF library route) on s10 at 20 ms
and 500 ms: **byte-identical to the OFF arm** — the route never changes a decision here, matching
the ledger's own "the dig's setup fails `startable()`" finding.

---

## claude_s2_gi1 — divergence T2 — (b) RANKING (starved tail)

**Human T2** (viewer plan 37 of 87): `land=Yavimaya Coast; Wild Growth → Conservatory,
Eladamri's Call → Eldrazi Displacer`.
**Search T2**: `land=Yavimaya Coast; Wild Growth, Living Wish → Eldrazi Displacer`.

The two plans fetch the *same card*; the difference is **which tutor pays for it**. Eladamri's
Call searches the library (`tutor_types: ["Creature"]`); Living Wish searches the sideboard
(`wish_from_sideboard`), and the sideboard is the **only** place either win condition lives —
Essence Depleter and Dimensional Infiltrator are sideboard singletons. Spending the wish on a
piece the Call could have found leaves the search with no route to a sink at all.

**Enumeration is fine.** `MTG_FS_ROOT_DUMP=2`:

```
[fs-root] tail win=9 ... : Wild Growth + Eladamri's Call      (every scan)
[fs-root] tail win=5 ... : Living Wish + Wild Growth          (best scan)
```

The human's plan is scanned and priced at 9 (i.e. "no win inside the horizon"); the search takes
the 5. At T4 the search holds Displacer + Drake + Shivan Gorge and Eladamri's Call — no red
source on board (Trace of Abundance still in hand), no sink, no wish. It banks two blinks and
attacks; it needs T5 (draw a Wild Growth, cast Wild Growth + Trace, then 17 Shivan Gorge
activations off the loop) to kill.

**Human T4** (the line the search never prices): `land=Shivan Gorge; Peregrine Drake` →
`blink Drake x23` (pure banking) → `Living Wish → Dimensional Infiltrator` → cast it →
`blink Drake x50` → opponent decks out.

**At 100 ms the search matches T4 on a different line** — T1 Yavimaya + Wild Growth, T2
Conservatory + `Living Wish → Adarkar Wastes` (fetching a *land*), T3 Adarkar + Trace of
Abundance, T4 Shivan Gorge + Overgrowth + Drake + Eladamri's Call → Displacer, loop into Gorge.
So the T4 kill is inside the model's reach; only the 20 ms tail estimate is not.

**Covered by an existing documented gap:** yes — ledger Session 9, budget starvation.

## claude_s5_gi4 — divergence T1 (material at T2) — (b) RANKING (starved tail)

Mulligan is identical by construction (`force_mulligan "1:7"`, mulligan to 6 bottoming
Brushland #7) — **not (c)**.

**Human T1**: `land=Brushland`. **Search T1**: `land=Yavimaya Coast`.
**Human T2**: `land=Yavimaya Coast; Trace of Abundance → Brushland`. **Search T2**: `land=Mariposa
Military Base; cast nothing`.

The T1 land choice is what makes T2 possible. Trace of Abundance costs **{R/W}{G}** (hybrid).
Brushland produces `G/W/C`; Yavimaya Coast `G/U/C`; Mariposa Military Base `C` only. With
Yavimaya + MMB the search has **no {R} and no {W}**, so Trace is literally uncastable and its T2
is blank — a whole turn of ramp lost. (Playing the in-hand Brushland at T2 was also enumerated:
`MTG_FS_ROOT_DUMP=2` shows `tail win=6 ... : Trace of Abundance` alongside `tail win=5 ... :
Living Wish` / the empty plan; the Trace line loses 6-vs-5.)

**Enumeration is fine at T1 too.** `MTG_FS_ROOT_DUMP=1` shows the five land-only plans tailing
`6, 6, 6, 7, 9` — the human's Brushland sits inside the three-way tie at 6, and the true answer is
**4**. The estimate is simply not computed.

**At 100 ms**: T1 Brushland, T2 Yavimaya + Trace, T3 MMB + Trace + Trace + `Living Wish →
Dimensional Infiltrator`, T4 Conservatory + Peregrine Drake + Eldrazi Displacer + Infiltrator →
deck-out. **Win T4, matching the human.**

**Covered by an existing documented gap:** yes — ledger Session 9 lists s5 by name in the
100 ms-fixes-it set.

## claude_s10_gi9 — divergence T2 — **(a) ENUMERATION / pricing** (the structural one)

**Human T2** (plan 2 of 14): `land=Brushland; Fertile Ground → Brushland` — stacking it on the
Brushland that already carries T1's Wild Growth, making one land tap for 3.
**Search T2**: `land=Brushland; cast Eldrazi Displacer`.

`MTG_FS_ROOT_DUMP=2` — **both enumerated, both reach the same tail**, so the tie is decided by
plan value alone:

```
[fs-root] tail win=5 ... val=1200: Eldrazi Displacer
[fs-root] tail win=5 ... val=100 : Fertile Ground
```

`val` traces to the deck profile's `card_scores`: Eldrazi Displacer `+0.627`, Fertile Ground
`-0.173`. The search then compounds it at T3 (MMB + Fertile Ground + a **second** Eldrazi
Displacer — a redundant outlet) while the human plays Kitchen and one Displacer.

**The T4 consequence is exactly one mana.** Human T4 has 6 available (Brushland+WildGrowth+
FertileGround = 3, Brushland = 1, Kitchen = 1 *played T3, so untapped*, MMB = 1) and casts
`Training Grounds + Peregrine Drake` ({U} + {4}{U} = 6). Search T4 has 5 (Brushland+WildGrowth = 2,
Brushland = 1, MMB+FertileGround = 2, Kitchen played T4 and **enters tapped** = 0) and can only
cast the Drake. `MTG_FS_ROOT_DUMP=4` confirms `Training Grounds + Peregrine Drake` is **absent
from the search's plan set** — not a hole, just unaffordable:

```
[fs-root] tail win=5 ... val=400 : Peregrine Drake
[fs-root] tail win=9 ... val=100 : Training Grounds
[fs-root] tail win=9 ... val=101 : Training Grounds + Eldrazi Displacer(x1)
```

**But the tiebreak is not the shortfall's root cause, and the budget is not either.** 20 / 100 /
500 / 2000 ms all produce the *same game*, byte-identical. Even handed the human's T4 board, the
search cannot price that turn's kill, because of what the human's line actually is:

**Human T4** (from the reference, in order): `Training Grounds, Peregrine Drake` → **`blink
Peregrine Drake` x27** (pure banking, no win claimed) → `Mariposa Military Base: draw a card` →
`Kitchen: investigate` → `Clue Token: sacrifice: draw a card` (**this draws Living Wish**) →
`blink Peregrine Drake x9 — COMBO OFF: wins this turn` → `Living Wish → Essence Depleter` → cast
it → 20 × `{1}{C}: opponent loses 1 life`.

The recognizer's own verdict is the evidence: the viewer only prints **"COMBO OFF: wins this
turn"** *after* the dig has put Living Wish in hand. Before that it offers banking blinks only.
On the search's side that means `FlickerGoOffCount` → `ScanHandSinks` sees no sink on board
(no Shivan Gorge), none in hand, and none wishable (no wish in hand — it is in the **library**),
returns 0, and no loop is proposed. The apply *can* dig mid-loop (`SpendSurplusOnDrawSinks` →
`ComboFinishFromHand`), but only inside a loop the search already committed to.

**Classification: (a)**, missing construct = **same-main bank-then-dig-then-wish-then-deploy**
(the library-behind-loop-draws finisher). Secondary contributor: **(b)** the card-score tiebreak
at T2/T3 that spent the tempo which would have made the human's exact T4 affordable.

**Covered by an existing documented gap:** yes, and precisely — the ledger's
`MTG_EDF_LIB_ROUTE` (implemented, **default OFF**, 200-game A/B measured **negative**:
5.72→5.92 and 5.24→5.38) and the Session 9b closeout's "node 3" residual: *"investigate → crack
Clue → cast the DRAWN Living Wish → cast the FETCHED payload → loop … the SAME expressibility
class as the user's s1_gi0 T3 reference line."* Confirmed inert here: forcing the route on
changes nothing (byte-identical at 20 and 500 ms).

## claude_s11_gi10 — divergence T3 — (b) RANKING (total starvation)

**Human T3** (plan 6 of 40): `land=Brushland; Overgrowth → Conservatory, Fertile Ground →
Conservatory` — an aura chain: pay Overgrowth `{2}{G}` off the two Brushlands (one carrying
T2's Trace), then tap the now-3-mana Conservatory to pay Fertile Ground. By T4 the Conservatory
carries Overgrowth + 2 × Fertile Ground and taps for 5.
**Search T3**: `land=Brushland; cast Emiel the Blessed`.

`MTG_FS_ROOT_DUMP=3` — **every scanned plan tails to 8**, the max-turns wall:

```
[fs-root] tail win=8 ... val=1200: Emiel the Blessed
[fs-root] tail win=8 ... val=200 : Fertile Ground + Overgrowth
[fs-root] tail win=8 ... val=200 : Fertile Ground + Fertile Ground
[fs-root] tail win=8 ... val=100 : Fertile Ground   /  Overgrowth
[fs-root] tail win=8 ... val=0   : (nothing)
```

Identical tails across every plan is the starvation signature; with the search half blind the
decision falls entirely to `card_scores`, which again prefers the 4-mana creature (1200) to the
ramp package (200).

**At 500 ms the search matches T6** — and, notably, by *executing* the same mid-loop dig the
human used: T6 is `Brushland, Peregrine Drake` → blink loop → **`Mariposa Military Base: draw a
card` mid-loop, which draws the Living Wish that was not in hand at end of T5** → `Living Wish →
Essence Depleter` → drain. That is the cleanest available proof that the dig chain is
*executable*; what s10 lacks is only the **pricing** that makes the search propose a loop in the
first place.

**Covered by an existing documented gap:** yes — same budget-starvation class as s2/s5 (s11 is a
newer reference and was not in Session 9's triage list).

---

## Ranking by expected avg-win-turn value of fixing

Corpus arithmetic: 8 references, human 4.5, search 5.25, **gap 0.75 t** = 6 turns / 8 games
(s1 +2, s2/s5/s10/s11 +1 each).

1. **Value leaf / more effective search per unit wall — worth −0.375 t on this corpus (3 of the
   4 games), and it is ONE artifact.** s2, s5 and s11 all correct themselves with nothing but a
   bigger virtual budget (100 / 100 / 500 ms). EDF has no `.value.json` today, and the ledger
   already names the value leaf as the designed fix for exactly this regime. Highest value, and
   the work is already scoped (the Session 9b/10 generation-tractability campaign exists to make
   it affordable). **Caveat, stated plainly:** a budget ladder is a *proxy*. It shows the winning
   line is inside the model's reach given more search; it does not measure what a value leaf
   actually delivers at shipped wall clock.

2. **Bank-then-dig construction (library-behind-loop-draws finisher pricing) — worth −0.125 t
   from s10 alone, up to −0.25 t if it also unlocks s1_gi0's T3 (s1 is +2 and its T3 is the same
   class).** Budget-immune, so no amount of search buys it. But this is the expensive option: the
   route is already implemented and measured **negative** on a 200-game pooled A/B, and the
   ledger's own verdict is that reviving it needs a redesign (never displace the draw-land
   fallback; bank-then-deploy `startable()`), *"worth attempting only after the value leaf moves
   the deck's horizon."* Nothing here contradicts that ordering.

3. **Cheap, untested side lever: the `card_scores` tiebreak.** In **both** s10 (T2) and s11 (T3)
   the losing decision was a tie on tail broken purely by plan `val`, and both times it preferred
   a creature (Displacer / Emiel, val 1200) over a ramp package (Fertile Ground / Overgrowth,
   val 100–200). EDF's `card_scores` are the usual stale-artifact risk. This is a one-batch
   experiment (re-score, or damp the value term at equal tail) with a plausible −0.125 t and
   almost no engineering, so it is worth a screen *before* item 2 even though its ceiling is
   lower. It would not fix s10's turn on its own (the T4 kill still needs item 2), but it is the
   only lever here that costs an afternoon.

**Ordering: 1 → 3 → 2.**

## Reproductions

```
# budget ladder (pooled; virtual ms is deterministic, so contention does not change results)
#   jobs: {"deck": decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod,
#          "profile": .../EldraziDisplacerFlicker.profile.json, "games": 1,
#          "seed": S, "game_index": GI, "max_turns": 8, "force_mulligan": FM, "budget_ms": B}
#   s2 -> 2/1/"0:"   s5 -> 5/4/"1:7"   s10 -> 10/9/"0:"   s11 -> 11/10/"0:"
./build/Release/mtg --batch <manifest> --threads N --game-trace-dir logs/edf_shortfall

# root plan sets at the divergence nodes (single game, one thread)
MTG_FS_ROOT_DUMP=2 ./build/Release/mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
    --seed 2 --game-index 1 --games 1 --threads 1 --max-turns 8 --force-mulligan "0:"
#   ... =2 for s10 (seed 10 gi 9), =3 for s11 (seed 11 gi 10), =1 and =2 for s5 (seed 5 gi 4, "1:7")
#   ... =4 for s10's critical turn

# library-route arm: add "flags": {"MTG_EDF_LIB_ROUTE": true} to the job
```
