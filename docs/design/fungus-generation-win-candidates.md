# Fungus: candidate wins for mulligan-profile GENERATION (and general usage)

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

### C. The land-aura fold's GENERATION value is unmeasured and should exceed its play value

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
