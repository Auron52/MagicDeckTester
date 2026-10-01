# Fungus: candidate wins for mulligan-profile GENERATION (and general usage)

> **CORRECTION, 2026-10-01 — THIS DOC'S FIRST DRAFT USED THE WRONG LIST.** USER: *"we should be
> looking at the new fungus list not the old one."* The NEW list is
> **`decks/Fungus/candidate-b-2026-09/Fungus.cod`** and it is a near-total rebuild, not a tweak:
> Sol Ring, 4 Mycoloth, 4 Undercellar Myconid, 4 Saproling Burst, Slimefoot, Deathspore/Vitaspore
> Thallid, Concordant Crossroads, Brightcap Badger, Shroofus Sproutsire — and a completely different
> mana base of **4 Forest / 4 Secluded Courtyard / 4 Peat Bog / 4 Hickory Woodlot / 4 Blooming
> Marsh** against the old 19 Forest + 3 Simic Growth Chamber.
>
> **Two consequences.** (1) The sections below marked OLD LIST are measured on the wrong deck and are
> kept only as the contrast. (2) **The land-aura host heuristic is NOT inert after all** — Secluded
> Courtyard, Peat Bog and Hickory Woodlot are *exactly* the three cards the user's rule names, and all
> three are in this list. I previously reported the rule as inert on Fungus; that was true of the OLD
> list only, where every land is green and non-depletion. On the new list every class A/B/C/D row
> fires. `MTG_LAND_AURA_HOST_PICK` should be re-measured here before anything else about it is
> believed.
>
> The new list is also far more expensive: at d3, 100 games, it runs **415,548 enumeration calls /
> 17.26 M odometer / 4.19 M plans**, against the old list's 201,894 / 4.26 M / 0.94 M over *200*
> games — roughly **8.7x the calls and 17x the plans per game**.

2026-10-01. Written because the overnight window is for generation, and the question asked was
*"look for any other wins we can consider for the mulligan profile generation ... or for general
usage ... Of the Fungus deck."*

**LANE NOTE: breakpoint continuations are another agent's work tonight and are excluded here.** The
`put_in_hand` finding below is recorded as a HANDOFF, not as something to implement from this side.

## The frame: Fungus's cost has moved entirely into generation

* **The regression block is no longer expensive.** The whole Fungus *overnight* tier — d0 x4 @2000,
  d3 x4 @400, d5 x4 @300, ~10,800 games — now runs in **30 s of batch makespan**. The d3 s2002 cell
  is 23.4 s CPU for 200 games against the 507 s its own comment used to claim.
* **Generation is where the hours are.** `decks/Fungus/Fungus.keepmodel.exhaustive.raw.json.slow.log`
  records **275 rollouts over 30 s, totalling 15.7 HOURS**, median **57 s**, max **7,350 s = 2 h 02 m
  for a single keep rollout**.
* That matches the memory already on file: *"Fungus candidate B: 576 rollouts >= 30 s = 32.6% of all
  generation compute. Snow: the >= 30 s tail is 4.8%."* **The tail cap is a deck-shaped lever and
  Fungus has the worst tail in the repo.**

## NEW, measured today: WHAT the tail is made of

Census of the 275 slow rollouts, weighting each card by the milliseconds of the rollouts it appears
in (`inSlow%` = share of slow rollouts containing it):

| card | in slow hands | share of slow ms | copies in deck | per-copy signal |
|---|---:|---:|---:|---|
| **Doubling Season** | 81.1% | **94.4%** | 4 | ~2x a 4-of's base rate |
| Forest | 70.2% | 82.0% | 19 | — |
| **Psychotrope Thallid** | **45.1%** | **66.0%** | **1** | **~4x — the most over-represented card in the deck** |
| Simic Growth Chamber | 57.1% | 61.1% | 3 | high |
| Utopia Mycon | 57.8% | 42.1% | 4 | — |
| Wild Growth | 39.3% | 37.1% | 2 | ~2x |
| Mycoloth | 29.1% | 25.2% | 2 | ~1.5x |

A 1-of is in roughly 11.7% of opening 7s; **Psychotrope Thallid is in 45.1% of the slow ones.** And
the five slowest rollouts all contain Doubling Season, four of five contain Psychotrope:

```
7350 s  Doubling Season x1; Forest x2; Mycoloth x1; Psychotrope Thallid x1; Simic Growth Chamber x2
4525 s  Doubling Season x1; Forest x2; Psychotrope Thallid x1; Utopia Mycon x2
4277 s  Doubling Season x3; Forest x1; Psychotrope Thallid x1; Simic Growth Chamber x1; Utopia Mycon x1
3028 s  Doubling Season x2; Forest x2; Simic Growth Chamber x3
2821 s  Doubling Season x3; Psychotrope Thallid x1; Simic Growth Chamber x2
```

**Why these hands and not the hands the suite plays.** Shipped Fungus wins on turn 5.4 and the
regression cells stop at turn 8, so normal play never builds a wide board. Mulligan generation
deliberately rolls out hands a player would never keep — `Doubling Season x3` is three 5-drops — and
those games DRAG, which is what widens the board and explodes the menus. **The suite structurally
cannot see the regime generation pays for.** Attempts to reproduce it from the goldfish path failed:
the unwon d3 game (`--seed 2128 --game-index 126`) costs 0.10 s and raising `--max-turns` to 20
changes nothing, because the deck still wins on turn 9.

## THE NEW LIST: which turns are particularly slow, and what is on them

USER: *"it would be best to look at particularly slow turns"* / *"analyze which turns are particularly
slow."*

### Per-turn, NEW list, CURRENT binary (d3 b10, 100 games, `--threads 1`)

| turn | calls | sum_odo | plans | dedup | avg_odo | avg board |
|---|---:|---:|---:|---:|---:|---:|
| 3 | 28,809 | 696,423 | 119,211 | 98,579 | 24.2 | 4.9 |
| 4 | 79,607 | 1,889,682 | 495,420 | 422,414 | 23.7 | 7.1 |
| **5** | **156,792** | 4,378,310 | **1,334,377** | 1,217,701 | 27.9 | 9.4 |
| 6 | 73,429 | 2,949,305 | 741,598 | 708,010 | 40.2 | 11.8 |
| 7 | 28,190 | 1,700,616 | 451,591 | 433,881 | 60.3 | 12.0 |
| **8** | 38,039 | **5,417,591** | 1,029,671 | **1,024,735** | **142.4** | 11.7 |
| | 415,548 | 17,255,406 | 4,188,689 | 3,919,483 | | |

**Two different slow turns, for two different reasons:**

* **Turn 5 is slow by VOLUME** — 37.7% of all calls and 31.9% of all plans, at an ordinary width
  (avg_odo 27.9). It is simply where the most decisions happen.
* **Turn 8 is slow by WIDTH and prunes essentially not at all** — avg_odo **142.4**, 3.5–6x every
  turn before it, 31.4% of the whole odometer from 9.2% of the calls, and dedup removes
  **0.5%** (1,029,671 → 1,024,735). Turn 8 is the horizon edge (`max_turns=8`), which matches
  `fungus-value-leaf-status.md`'s finding that **99.7% of this deck's evaluations sit at the horizon
  edge**. The engine builds a million plans on the last turn it will ever look at, and almost none of
  them are duplicates it can drop.

**REGIME CAVEAT:** the §"The real turns" table in `fungus-slow-rollout-diagnosis-2026-09-30.md` is the
KEEP-REPLAY regime and reports turn 6 owning 73.7% of the odometer with turn 8 owning 53.7% of plans.
That is a different workload from this goldfish run and the two must not be compared cell-for-cell.
What survives both: **turn 8 is a blowup, and its dedup rate is ~0 in both.**

### What the heaviest turn-8 decisions are made of — and the win

All three heaviest decisions in the run are turn 8, and all three have the same shape:

```
HEAVY rank=1 odo=3200 turn=8 board=15 visits=1255 plans=1119 dedup=1119 reenumerated=4
  BOARD lands=4 perms=Utopia Mycon,Concordant Crossroads,Utopia Mycon,Psychotrope Thallid,Saproling Burst
  GROUP size=4 Utopia Mycon [sacForMana:colour=G:victim=1005 | colour=B:victim=1005 | sacN=2:colour=G | sacN=2:colour=B]
  GROUP size=4 Utopia Mycon [ ... BYTE-IDENTICAL ... ]
  + 6 size-1 groups
```

Two things that were not true of the old list:

1. **The colour fan is now 2, not 1.** Utopia Mycon adds one mana of ANY colour, resolved through
   `ChosenFloatColorCandidates`. The card's own note says *"In this mono-green deck the fan returns
   the singleton {G}, so it costs exactly one action and no plan-space growth — but it stays honest
   if the deck splashes."* **The deck now splashes** (Blooming Marsh, Peat Bog, Slimefoot,
   Deathspore Thallid), so the fan returns {G, B} and every Mycon group doubles.
2. **THE POOLING IS PER PHYSICAL SOURCE, so two Utopia Mycons do not pool with each other.**
   `CollectMultiVariantSacSources` matches on `s.id == a.sac_source_id`, so each Mycon gets its own
   group and its own `x5` in the odometer: **(1+4)² = 25 slots.**

   **But two Utopia Mycons are perfectly interchangeable.** The ability is *"Sacrifice a Saproling:
   Add one mana of any color"* — no `{T}`, no self-sacrifice, so the Mycon is not consumed and
   activating A twice reaches exactly the state of activating A once and B once. The real axis is
   *"sacrifice k Saprolings total, producing a multiset of colours"*: for k ≤ 2 over {G, B} that is
   **6 outcomes, against 25 enumerated** — and it grows as (1+4)^n in the number of Mycons on board,
   on a deck running four of them.

   This is the SAME defect the within-source count pool fixed at **2.29x** (`MTG_SAC_OUTLET_POOL`,
   adopted 2026-09-23) — one level out. It is a **sound identity fold**, not a heuristic: the sources
   are indistinguishable, so no line is lost. **This is the most promising concrete win found, it is
   in the sac-outlet lane rather than the breakpoint lane, and it lands on the heaviest turn of the
   list that is actually being generated.**

   Related open item already on file: doc item 7, *"two co-selected creature-sac outlets, or conclude
   the reservation guard is play-inert"*. Note both groups above name **the same `victim=1005`**, and
   `dupSacSrc=0` — the duplicate-source filter does not fire because the SOURCES differ; only
   `overFodder` (40 rejects of 1,255 visits) stands between co-selecting both and double-sacrificing
   one Saproling.

### The new list's generation tail: 213.8 hours

`decks/Fungus/candidate-b-2026-09/Fungus.keepmodel.exhaustive.raw.json.slow.log` — **3,923 slow
rollouts, 213.8 HOURS**, median **38.9 s**, max **25,713 s = 7 h 08 m for ONE rollout** (the old
list: 275 rollouts, 15.7 h, max 2 h 02 m — so **13.6x the tail**).

| phase | count | hours | share |
|---|---:|---:|---:|
| **keep-rollout** | 3,338 | **201.90** | **94.4%** |
| discovery | 492 | 10.73 | 5.0% |
| play-digest | 93 | 1.16 | 0.5% |

**All five slowest rollouts contain Psychotrope Thallid** — still a 1-of — and three of five contain
Secluded Courtyard:

```
25713 s  Psychotrope Thallid x1; Doubling Season x2; Bloom...
22263 s  Secluded Courtyard x2; Psychotrope Thallid x1; F...
20781 s  Psychotrope Thallid x1; Peat Bog x1; Forest x1; D...
16319 s  Secluded Courtyard x1; Psychotrope Thallid x1; M...
15283 s  Secluded Courtyard x1; Psychotrope Thallid x1; Fo...
```

`keep-rollout` at 94.4% means the tail is the rollouts themselves, not discovery or the play-digest
battery — so a per-rollout lever is the one that pays.

## THE BIGGEST FINDING: the 2.29x sac-outlet pool is SILENTLY OFF on the new list

**Not from any code change — from the decklist.** `MTG_SAC_OUTLET_POOL` collapses N interchangeable
mana outlets onto one COUNT axis (adopted 2026-09-23, measured **2.29x on the heaviest Fungus cell**).
Its emission site carries an explicit gate:

```cpp
// THE COLOUR FAN MUST BE A SINGLETON. With several candidate colours the outcome
// depends on the colour MULTISET and not on the count alone, so a count axis would
// not be the same enumeration. Those boards stay on the per-source path.
if (pool_cols.size() == 1)
```

The OLD list is mono-green, so `ChosenFloatColorCandidates` returns `{G}` and the pool fires. **The
NEW list plays black** (Blooming Marsh, Peat Bog, Slimefoot, Deathspore Thallid), so the fan is
`{G, B}`, the gate fails, and **every Utopia Mycon drops back onto the per-source path.**

Measured directly, by rendering `sac_source_id` and a `:POOLED` marker into the heavy dump:

```
OLD list, mono-green   GROUP size=8 Utopia Mycon [sacForMana:colour=G:src=58:POOLED:victim=1002
                                                 | sacN=2 | sacN=3 | ... ]   -- ONE group, counts 1..8
                       POOLED markers in run: 3
NEW list, splashes B   GROUP size=4 Utopia Mycon [...:src=54:victim=1000 | ...]
                       GROUP size=4 Utopia Mycon [...:src=55:victim=1000 | ...]   -- one group PER SOURCE
                       POOLED markers in run: 0
```

With `distinct_physical_sources=4` for Utopia Mycon, the odometer takes `(1+4)^4` where one pooled
count axis would do. That is why **Utopia Mycon · SacForMana is 44.0% of all candidate mass**
(1,021,086 of 2,321,995). **Nothing reports that the pool stopped firing** — it simply does not,
which is why this survived a deck rebuild unnoticed. The card's own note even predicted it: *"In this
mono-green deck the fan returns the singleton {G} ... but it stays honest if the deck splashes."*

### The fix the user specified: TWO heuristics

USER: *"The mana sac should just be used as a mana source"* and *"We should have a heuristic for this."*

**1. THE FLOAT COLOUR — this is what re-enables the pool.** Choose the colour by DEMAND instead of
fanning it, so `pool_cols` is a singleton again. **But note it is a NARROWING, not an identity fold:**
the fan exists because pools hold typed mana only and a wild token could illegally pay a multicolour
mix, so the colour is deliberately enumerated rather than pinned. Pinning it can drop a line where the
other colour was needed later in the same turn. So this is GT-moving and needs its A/B — unlike the
Wild Growth fold, which was byte-identical.

**2. THE VICTIM — a 3-class ranking, and it is a MODELLING rule, not just a tie-break.** USER, with the
correction that Shroofus is *last* rather than excluded: *"The only different saprolings would be
Shroofus Sproutsire (should be kept), 1/1 saprolings and Saproling Burst saprolings. Most of the time
we should prioritize 1/1 saprolings unless the Saproling Burst saprolings are the same size or smaller.
(or 1 P/T larger in the second main)"* / *"I don't mean never shroofus, but Shroofus should be the last
to go."*

The card text is what makes this ordering correct:

* **Saproling Burst tokens** — *"This token's power and toughness are each equal to the number of fade
  counters on Saproling Burst"*, and *"When this enchantment leaves the battlefield, destroy all tokens
  created with it."* Burst has Fading 7, losing a counter per upkeep, so these tokens **shrink every
  turn and are doomed when Burst fades out.** That is why their SIZE decides: a small one is nearly
  free fodder, a large one is a real attacker worth keeping while Burst lives. (The `3x 0/0 Saproling
  Token` on the heavy boards is this — Burst at zero counters.)
* **Shroofus Sproutsire** — a 1/1 **Saproling** itself, so legal fodder, but it is the deck's
  exponential engine (*"Whenever a Saproling you control deals combat damage to a player, create that
  many 1/1 green Saproling creature tokens"*). Hence last, not forbidden.

Resulting sacrifice order (first to go → last):

1. **Saproling Burst tokens at P/T <= 1/1** (first main), or **<= 2/2 in the second main** — doomed,
   and in the second main they will not attack again this turn anyway.
2. **Plain 1/1 Saproling tokens.**
3. **Saproling Burst tokens larger than that** — keep as attackers while Burst lives.
4. **Shroofus Sproutsire** — last.

This belongs in `CanonicalSacVictim`, which is also the function `victims_agree` calls per pool
member — so a deterministic shared ranking keeps every Mycon agreeing and keeps the pool eligible.
**On the boards measured the victims ALREADY agreed** (both `src=54` and `src=55` chose `victim=1000`),
so the victim rule is a PLAY-QUALITY win plus insurance; the colour fan is what actually blocks pooling
today. Those two should not be conflated when either is measured.

## Candidate wins, most promising first

### A. A TARGETED tail cap, not a bigger global `MTG_DECISION_WORK_X`

Already priced and closed as a global lever: `X=1000` is **1.33x** and digest-preserving (ADOPTED in
`scripts/mullgen.sh`); `X=100` is **2.41x** but moves the play digest, so it is a quality question.
The ceiling is *disconnected* from the tail rather than mis-set — p50 is 0.5x budget, p99 is 235x,
p99.9 is 1,484x, max **28,191x**.

**The new information is that the tail is IDENTIFIABLE FROM THE HAND before the rollout runs**
(Doubling Season present, especially with Psychotrope / multiple Doubling Seasons). That admits a
cap aimed at the cells that need it instead of a global constant that must trade digest fidelity
everywhere. Not designed here; the measurement that would justify it is the per-cell cost
distribution keyed on hand composition, which the slow log already half-provides.

### B. Psychotrope's sac-for-draw — CHECKED AND RULED OUT as a width win (but see the quality note)

Utopia Mycon's `SacForMana` outlet was collapsed onto a count axis — one action per activation COUNT
rather than a powerset over interchangeable Saprolings — and that was measured at **2.29x on the
heaviest Fungus cell** (`MTG_SAC_OUTLET_POOL`, adopted 2026-09-23). Psychotrope's outlet is a
**different action kind** (`Action::Kind::SacCreatureOutlet` with `sac_outlet_draw`, not
`SacForMana`), and `CollectMultiVariantSacSources` is documented as grouping *"a `SacForMana` source
only when variants differ in COLOUR"*.

**The pooling gap is REAL: it does not cover the draw outlet.** But it turns out to be harmless, for
two reasons found at the emission site:

* **No victim fan.** The multi-sac burst sets `b.sac_victim_id = 0` — *"unused for a burst; victims
  are chosen canonically at apply"* — so there is no powerset over interchangeable Saprolings. The
  defect the Mycon pool fixed is not present here.
* **No burst at all for this outlet.** `k = (sd->params.sac_outlet_damage == 0) ? 0 : V;` — a
  DRAIN-ONLY burst is suppressed, and the comment notes that is *"every Fungus outlet"*. Psychotrope
  has `sac_outlet_damage == 0`, so `k = 0` and no multi-sac action is emitted.

**So Psychotrope's menu width is ONE action per activation.** Count-pooling it would collapse nothing.
**This candidate is closed** — which is itself useful, because it means the cost cannot be menu width
and must be the per-draw BREAKPOINT CHAIN: the only way to sac five Saprolings for five cards is five
activations across five breakpoints. That routes the whole Psychotrope question into the breakpoint
lane, i.e. the handoff below.

**Quality note for whoever owns that lane:** because the drain-only burst is suppressed, "sacrifice
five Saprolings, draw five" is **not expressible as a single plan** — it exists only as a chain of
breakpoint continuations. If that chain is budget- or rank-limited, deep draw lines may be
unreachable at any budget, which is the "missing arm is silent" asymmetry pointing the other way.

### C2. The host HEURISTIC, re-measured on the land base it was written for — modest

Now that the right list is in hand, `MTG_LAND_AURA_HOST_PICK` was A/B'd on it (d3 b10, 100 games,
`--threads 1`, user CPU): **109.74 s off → 107.69 s on = 1.019x, avg win turn IDENTICAL at 5.4400.**
One pair only, so treat the ratio as indicative; the quality-neutrality is the useful part. So even on
4 Secluded Courtyard / 4 Peat Bog / 4 Hickory Woodlot — the exact shape the rule was stated for — it
is a ~2% effect. Worth having under the collapse doctrine, not worth a GT re-accept on its own.

### C. The land-aura FOLD's generation value is unmeasured and should exceed its play value

`MTG_LAND_AURA_HOST_FOLD` shipped today at **1.026x on the d3 cell** (see
`land-aura-host-decision.md`). That number is the *turn-5 regime floor*, because the fold's reach is
the LAND COUNT: hosts = lands in play, so it collapses `x5 -> x2` on a 4-land turn-4 board and
`x11 -> x2` on a 10-land board. Generation's long games reach the latter. Wild Growth is in **39.3%
of slow rollouts and 37.1% of slow generation ms**, so the axis is demonstrably present in the tail.
Measuring it there needs the gencache (stale, ~18 min to rebuild) and so was not done tonight.

### D. Two-stage gating of the subset walk (1.19x, carried forward)

At overnight scale the funnel reads **51,999,110 subset visits**, of which the three shared-resource
predicates reject **24.07%** (`dupSacSrc` 11.94%, `wasteSacMana` 4.34%, `overFodder` 3.79%). A
constraint-bounded walk would skip those before generating rather than after. Sized at **1.19x fewer
visits** in `fungus-slow-rollout-diagnosis-2026-09-30.md` §2g — and note that visits have twice been
shown to bound cost without predicting it, so this needs a profile before it is built.

## HANDOFF (do not implement from this side): breakpoints do NOT emit only new options

The user's 2026-09-19 ruling is that a breakpoint opens *"exactly when there are new options to
consider"*. Measured at overnight scale on Fungus with `MTG_BP_PROBE=1`:

```
BP CONTINUATION ENUMS: 3,448,739 total
put_in_hand      total=3,273,470  empty=3,144,371  searched=129,099  ( 3.9% searched)
                 empty split: untarget=2,489,690  overrun=654,681 (dup=112,532)
post_entry_act   total=1,596,800  empty=  400,711  searched=1,196,089 (74.9% searched)
                 empty split: overrun=371,380 (dup=25,075)  untarget=29,331
deferred_cantrip total=   53,302  empty=      565  searched= 52,737  (98.9% searched)
```

**`put_in_hand` arms 3.27 M times and 96.1% are empty; 2,489,690 of them — 76% of all arms — are
`untarget`, i.e. the card that entered hand has no legal use.** It is Psychotrope's site: the
arming clause's own comment says the sac-for-draw was added because *"the census measured this as
100% of Fungus's hand entries and the deck holds no other draw at all"*.

**Sizing honestly: disabling the whole class measured only ~1.02x** (d3 cell, 20.53 s -> 20.05–20.24 s
user CPU, avg turns identical; and ~1.04x at `--max-turns 14`). An `untarget` arm is individually
cheap because its continuation finds nothing and exits. So the count is NOT a speedup of that size,
and the `post_entry_act` over-arming the earlier audit predicted (Utopia Mycon cast with no Saproling)
shows up as only `untarget=29,331`.

**Where the prize plausibly is — UNVERIFIED, flagged as a hypothesis:** empty arms still consume
breakpoint RANKS. Wave 0 indexes continuations as `cands[0..W-1]` under a budget cutoff, and the
engine's own note says reached continuations are *"being mis-RANKED, not merely mis-afforded"*. If
96% of arms are empty they occupy slots real continuations need, making this a **quality** prize
(better continuations inside the same budget) rather than a wall prize. Someone should confirm
whether an empty arm actually takes a rank before anyone prices it.

## Also settled today

* **EldraziDisplacerFlicker is not suite-eligible.** Timed directly (no suite cases exist, so
  `suite_gate.py --cost` cannot read it): **3,649 ms/game at d3, 5,371 at d5**, against a 3x bar of
  ~3,100 (3 x fivecolour's 1,033). Dropped as a prerequisite; it was blocking
  `MTG_LAND_AURA_HOST_PICK` and `MTG_LAND_AURA_FOLD_KAROO`, which therefore stay default-off and
  unmeasurable for now.
* The overnight *tier* comparison is against **Sep-17 ground truth** (`configs changed: 213`,
  `no-run-dir: 69`, play-changed counts spanning decks that never ran). Its 4 Fungus failures —
  largest 5.3575 -> 5.3600 — predate today; smoke and regression, which have current GT, are
  101/101 and 140/140 at `play-changed=0`.
