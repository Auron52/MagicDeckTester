# COMBO OFF sweep — catalogue of failing states (phase 1: measure, do not fix)

**USER, 2026-09-11:** *"We should do a bunch of testing of Combo Off states and fix any case that
fails to go off."* And, on what the machinery is ultimately for: *"Especially as the intention is
to use the same logic to skip work for the search."*

This is the **phase-1 deliverable**: a re-runnable sweep, the classification of every state it
touched, and a per-mechanism write-up with a minimal repro fixture and the engine trace that names
the cause. **No engine change was made.** Phase 2 fixes the clusters.

The doctrine being measured against is the user's own, from Session 14: **display must be
AGGRESSIVE** (fire wherever the rule holds) and **execution must be EXACT** (a click wins). The two
are measured separately throughout, because they fail in opposite directions.

Harness: `bash test/combo_off_sweep.sh` (`--quick` for a gate-sized run, `--reuse-games` for the
re-run-after-a-fix path). Machine-readable results: `logs/combo_off_sweep/results.json`, keyed by a
**stable content-addressed state id**, so a post-fix sweep diffs cleanly against this one.
Repro fixtures: `test/combo_off/sweep/` — deliberately *not* `test/combo_off/`, so
`combo_off_check.sh` stays at its 10 green fixtures until fixes land.

---

## 1. What was swept

**1037 unique states**, three populations, every one put to the engine through
`TurnSolver::EnumerateMainPlans` under `MTG_HUMAN_PLAY=1` (the viewer's own entry point) and, when
a button appeared, re-applied through the public `ApplyPlan` under `ComboOffApplyPause` — the same
path a click runs.

| population | n | what it is |
|---|---:|---|
| `synthetic` | 226 | a combinatorial matrix over the rule table's own ingredients: outlet × untapper × {C}-source count × U/B × draw source × finisher placement × Training Grounds × Shivan Gorge × land yield, plus a tapped-{C} family for the `MTG_UNTAP_C_STARVED` reservation |
| `reference` | 260 | every main-phase frame of the user's 11 saved games under `references/EldraziDisplacerFlicker/` (309 decisions, deduped) — read-only, never written |
| `engine` | 551 | every main-phase frame of **59 autonomous games at the deck's shipped settings** (`--log-dir`, no `--depth`/`--budget-ms`, so the deck's own play policy governs) |

Three independent instruments answer each state: the **rule table** (`EldraziFlickerProvider::
ComboOffPossible`), the **trial apply** (`combo_off_verified`), and a **python oracle** in
`test/combo_off_sweep.py` that re-derives the loop's net mana, the 60-iteration bank and the
cheapest finish cost *from `src/cards/data/cards.json`* rather than from the engine — so it can
disagree with the rule table, which is what class (d) is.

### Classification

| class | meaning | n | % |
|---|---|---:|---:|
| **a** | offered, and the apply **wins** | **184** | 17.7% |
| **b** | offered, and the apply does **NOT** win — **EXECUTOR FAILURE** | **84** | 8.1% |
| **c** | not offered, and the position wins anyway — **MISSED OFFER** | **28** | 2.7% |
| **c′** | not offered, and the win is plain **combat damage** (not a combo) | 43 | 4.1% |
| **d** | offered and provably unwinnable — **RULE TOO LOOSE** | **0** | 0.0% |
| **e** | not offered, not winnable — correct | 698 | 67.3% |

Per population:

| population | a | b | c | c′ | e |
|---|---:|---:|---:|---:|---:|
| synthetic | 102 | 67 | 3 | 0 | 54 |
| reference | 74 | 11 | 6 | 19 | 150 |
| engine | 8 | 6 | 22 | 21 | 494 |

**Of the 268 states where the button appeared, 184 won and 84 did not — a 31.3% failure rate on
the click.** That is the user's complaint, quantified.

---

## 2. THE RESULT THAT MATTERS MOST: `combo_off_verified` is a perfect oracle

Across **all 1037 states and all three populations, with zero exceptions**:

```
population   verified  apply_win     n
  engine     False     False         6
  engine     True      True          8
  reference  False     False        11
  reference  True      True         74
  synthetic  False     False        67
  synthetic  True      True        102

VERIFIED but did NOT win : 0
UNVERIFIED but DID win   : 0
```

So the two halves of the button behave completely differently as predictors:

* **`combo_off_offered`** (the rule table) is right **68.7%** of the time (184/268).
* **`combo_off_verified`** (the trial apply) was right **100%** of the time (268/268).

This is the load-bearing fact for the search-shortcut plan in §5, and it is also why every failure
below is an *execution* story rather than a *promise* story: the engine already knows which offers
are real. It simply shows the others anyway, which is exactly what the user asked for.

### Per-rule and per-outlet success

| rule | offered | wins | win rate |
|---|---:|---:|---:|
| `DEPLOYED` | 117 | 101 | 86.3% |
| `WISH-DRAW` | 101 | 55 | 54.5% |
| `IN-HAND` | 31 | 26 | 83.9% |
| **`GORGE`** | **17** | **0** | **0.0%** |
| `WISH-NODRAW` | 1 | 1 | 100% |
| *(rule name empty)* | 1 | 1 | 100% |

| outlet the recognizer picked | offered | wins | win rate |
|---|---:|---:|---:|
| Eldrazi Displacer `{2}{C}` | 166 | 103 | 62.0% |
| Emiel the Blessed `{3}` | 102 | 81 | 79.4% |

**And one predicate separates the population perfectly:**

| loop colourless net per iteration | offered | wins | win rate |
|---|---:|---:|---:|
| `net_c <= 0` | 42 | **0** | **0.0%** |
| `net_c > 0` | 226 | 184 | 81.4% |

`RecogniseFlickerLoop` **already computes `loop.net_c`**, and `ComboOffPossible` never reads it.
See cluster C3.

Two predicates — `rule == GORGE` and `net_c <= 0` — between them cover **55 of the 84 failures
(65%)**; they overlap on only 4.

---

## 3. The failure clusters

Ordered by size. Every cluster names the engine trace that identified it and the repro fixture
that pins it. Each fixture is **expected to FAIL today** and is the phase-2 acceptance test.

Run one with:

```
./build/Release/mtg --scenario test/combo_off/sweep/<fixture>.json
MTG_EDF_GOFF_DEBUG=1 MTG_EDF_FINISH_STATS=1 ./build/Release/mtg --scenario <same> 2>&1 | grep -E 'edf-goff|finish'
```

### C3 — the go-off is sized in GENERIC mana while the finisher is paid in {C} PIPS (20 states)

**Repro:** `test/combo_off/sweep/sweep_4_c_pip_supply_not_sized.json`
**Populations:** synthetic 19, engine 1. **Offered by:** `DEPLOYED` / `IN-HAND` / `WISH-DRAW`.

The plan promises 14 blinks, the apply runs all 14, and the Essence Depleter drains exactly **nine
times**: opponent 20 → 11, then nothing.

```
[edf-goff] t5 ... net=3 refund=6 cost=3 drain=1/2 kmax=3 afford=3 n=14
hist [ability] Eldrazi Displacer: blinked Cloud of Faeries   (x14)
hist [damage]  Essence Depleter: opponent loses 1 life       (x9)
scenario: FAIL combo_off plan applied but did NOT win (opponent life 11, library 12)
```

On **generic** mana the sizing is correct: 14 × net 3 = 42 against the Depleter's 20 × `{1}{C}` =
40. On **colourless** it is not. Cloud of Faeries untaps two lands; the yield order takes Kitchen
(5) and the `MTG_UNTAP_C_STARVED` reservation forces the other slot onto a {C} land (1); Eldrazi
Displacer's `{2}{C}` then eats that one {C} every pass. The loop's colourless net is **zero**, so
the only {C} the drain ever sees is the board's initial supply.

**What the rule checks and what it does not.** `comborules::ColorlessSourceCount` counts {C}
*sources on the board* — three here, so both `C1` and `C2` hold — and `MTG_COMBO_OFF_BANKABLE`
prices the path in generic mana. Neither consults `loop.net_c`. A board can hold five colourless
sources and still feed a {C}-pip sink at net zero.

**Control, same board, one card swapped:** Emiel the Blessed (`{3}`, no {C} pip) instead of the
Displacer → `offered=1 verified=1`, 20 blinks, **wins**. This is the same Displacer-vs-Emiel divide
the committed fixtures `edf_co_4` (single {C}, ABSENT) and `edf_co_5` (two {C} + Drake, OFFERED)
pin at the *display* end. It is simply not pinned at the *execution* end.

**Two candidate repairs, and the choice is the user's.** Either size the go-off count by the {C}
pip supply as well as the mana (the `MTG_EDF_GOFF_C_ITERS` axis exists and does not bind on this
path), **or** require `loop.net_c > 0` in any rule whose finisher carries a {C} pip. The second
narrows the display, which under the aggressive-display doctrine is a ruling, not an agent's call.

### C2 — a Shivan Gorge on the battlefield starves the blink loop to death (17 states)

**Repro:** `test/combo_off/sweep/sweep_3_damage_sink_starves_loop.json`
**Populations:** synthetic 16, engine 1. **Offered by:** `GORGE` (and this is the whole of the
GORGE rule's 0.0% win rate).

Rule `GORGE` fires, the plan promises `blink Cloud of Faeries x20`, and the apply produces exactly
**one** event:

```
[edf-goff] t5 ... gorge=1/3 kmax=3 afford=3 n=20
scenario:   hist [damage] Shivan Gorge deals 1 to the opponent
scenario: combo_off history events=1 reveals=0
scenario: FAIL combo_off plan applied but did NOT win (opponent life 19, library 49)
```

**Zero blinks.** Isolated by removing one permanent, everything else held fixed:

| variant | blinks | pings |
|---|---:|---:|
| as authored | **0** | 1 |
| + a second {C} source (Adarkar Wastes) | 0 | 1 |
| + two more {C} sources | 2 | 1 |
| Emiel instead of Displacer (no {C} pip) | 0 | 1 |
| Peregrine Drake instead of Cloud (5 untaps, not 2) | 0 | 1 |
| **Shivan Gorge REMOVED, Infiltrator deployed instead** | **25** | — |

The identical loop runs twenty-five times with the damage sink off the board and zero times with it
on. Not a colour problem, not an untap-count problem: **the sink spend.**

**Mechanism.** `ApplyBlinkLoop` fires the damage sink *before* it pays the activation — deliberately
(`SpellEffects.h`: *"ORDER MATTERS. The sink fires BEFORE the tap-ahead: the tap-ahead would
otherwise tap the Gorge for its one {C} …"*):

```cpp
if (cash_sinks) { SpendSurplusOnDamageSinks(state, controller, c, pay); }
if (untaps > 0) { EtbUntapTapAheadIntoFloat(state, controller, untaps, 0, …); }
if (!pay(c))    { break; }          // k == 0, done == 0
```

`SpendSurplusOnDamageSinks` *does* guard itself — *"Never spend the loop's entry price: only fire if
the board could still pay BOTH this ability and the next iteration"* — but the guard is a **CanPay
projection over a pooled `AvailableManaPool`**:

```cpp
ManaPool have = AvailableManaPool(state, nullptr);
have.AddPool(state.floating_mana);
if (!have.CanPay(AddManaCosts(c, keep_payable))) { continue; }
```

A projection saying the two costs are *jointly* payable does not make the *real* payment leave the
blink payable. The Gorge's `{2}{R}` is paid first and taps the Gorge itself plus the
Fertile-Ground'd host — two of the board's three {C}-capable sources — before `{2}{C}` is ever
attempted. This is the repo's own **"generic pays from SURPLUS"** doctrine being violated: the
sink's generic half is paid out of scarce sources the pending activation has a pip on.

**Severity beyond the GORGE rule:** any board that merely *holds* a Shivan Gorge pays this,
whichever rule offered the button.

### C5 — the finish fires and runs out (14 states)

**Populations:** synthetic 3, reference 8, engine 3. Shares the C3 root on most instances.

Example `A_displacer_cloud_fin-wish-hand_lib12`: 10 blinks, the wish resolves and the finisher is
cast (`finish_cast: 2`), **two** cards are exiled, and the opponent's library goes 12 → 10.

This is the same colourless-supply wall as C3, seen through the deck-out sink instead of the drain:
`SpendSurplusOnExile` needs one {C} pip per card left in the library and needs them at once, which
is why `ApplyBlinkLoop` banks colourless for it (`want_hold_colorless`) — but a loop at `net_c <= 0`
has nothing to bank. Eight of the fourteen are reference frames, i.e. boards the user really sat in
front of.

### C1b — the draw-to-find route stops the loop before it starts (12 states)

**Repro:** `test/combo_off/sweep/sweep_2_draw_route_zero_blinks.json`
**Populations:** synthetic 10, reference 2. **Offered by:** `WISH-DRAW`.

Plan promises 31 blinks; the apply emits **zero events of any kind**. Isolated by moving one card,
board otherwise identical:

| where the Living Wish is | rule | blinks | result |
|---|---|---:|---|
| **LIBRARY** (this fixture) | `WISH-DRAW` | **0** | no win |
| HAND | `WISH-DRAW` | 10 | **wins** |
| library, + Dimensional Infiltrator in HAND | `IN-HAND` | 10 | **wins** |
| library, + Dimensional Infiltrator on BATTLEFIELD | `DEPLOYED` | 10 | **wins** |

The trigger is therefore exactly `!ComboFinisherReachable(...)` — which is what turns on
`ApplyBlinkLoop`'s `want_draw` *and* its draw-land untap promotion:

```cpp
bool want_draw = LoopDrawSinkOn() && !ComboFinisherReachable(state, controller);
…
if (cash_sinks && want_draw) { SpendSurplusOnDrawSinks(state, controller, c, pay); }
if (!pay(c))                 { break; }      // k == 0, done == 0
```

Same shape as C2, on the draw sink rather than the damage sink, and with the same projection-guard
weakness. The Session 14b lesson recurs from the other side: there the promotion happened and the
spend stood down; here the spend happens and eats the loop's entry price.

`MTG_EDF_LIB_ROUTE_COMBO_OFF=0`, `MTG_COMBO_OFF_DRAW_PROMOTE=0`, `MTG_HOLD_C_FOR_DEPLOY=0` and
`MTG_COMBO_OFF_EARLY_DEPLOY=0` were each tried and **none restores the loop** — so this is not one
of the existing levers being mis-set.

### C6 — the loop runs, the finisher is deployed, and it never activates once (11 states)

**Populations:** synthetic 11.

Example `A_displacer_cloud_tg_fin-wish-hand_lib49`: 21 blinks promised, **21 run**, `finish_cast: 2`
(the Living Wish resolved and the finisher was deployed) — and **zero** exile or drain events. The
opponent's library is untouched at 49.

So the whole chain up to the kill works and the kill itself never fires. On these boards
Training Grounds is in play, which reduces Dimensional Infiltrator's `{1}{C}` to `{C}`: one pure
colourless pip per activation, against a loop at `net_c == 0`. Same root as C3, and the sharpest
statement of it — under Training Grounds the finisher costs *nothing but* the pip the loop cannot
supply.

### C1a — the loop runs and the draw engine never fires, so the wish is never found (10 states)

**Repro:** `test/combo_off/sweep/sweep_1_draw_engine_never_draws.json`
**Populations:** synthetic 8, reference 1, engine 1. **Offered by:** `WISH-DRAW`.

Rule `WISH-DRAW`'s entire premise is the user's own sentence — *"Being able to draw repeatedly
through infinite mana and having one colourless and one blue or black mana source is sufficient"*.
The apply runs all 31 promised blinks and draws **nothing**:

```
31 x  hist [ability] Eldrazi Displacer: blinked Peregrine Drake (returns tapped)
[finish] … finish calls=62 hand=0 wish=0 no-mana=0 none-found=62 pay-fail=0
```

`none-found=62`: `ComboFinishFromHand` was called sixty-two times and never had a candidate,
because the Living Wish was still in the library the loop was supposed to dig through. **This is
the seed-6 shape the user reported** (*"the loop drew 13 cards and stopped although Living Wish was
still in the library"*) in its purest form — here it draws zero. One card away (a second
Conservatory) it becomes C1b and the loop stops entirely.

> **Note for phase 2:** C1a/C1b are the cluster another agent was already repairing when this sweep
> ran. The sweep's contribution is the scope: it is not one board, it is the **entire
> wish-from-library family** (46 of the 84 failures were offered by `WISH-DRAW`), and it has two
> distinct surface forms one card apart.

### Remainder (29 states)

With the GORGE and `net_c <= 0` sets removed, what is left splits as: *finish fired and ran out /
other* 11, *loop never ran (0 blinks)* 10, *draw engine never fired* 8. All three are the same
three mechanisms above appearing on boards the two headline predicates do not cover.

---

## 4. MISSED OFFERS — the rule is too tight in one nameable place

**28 states** were not offered on a board the autonomous engine then won that same turn. (A further
43 were excluded as **combat kills** — five power against five life is not a combo, and crediting
those would manufacture the class out of ordinary attacking.)

The winnability oracle is an **upper bound**: the turn engine steps into `turn` through an untap
and a draw the offer probe never sees. Every state records `untap_confound`; of the 22 engine-
population misses, 9 are `MAIN_2` frames whose lands the untap restores, and 13 are `MAIN_1`.

### M1 — `HasBlueOrBlackSource` ignores a land Aura's wild mana (its sibling `HasRedSource` does not)

Found on the user's own reference `claude_s1_gi0` turn 3 — the game the user **won** on turn 3.
Board: Emiel, two Peregrine Drakes, two Clouds, Aether Hub + Trace of Abundance + Wild Growth,
Conservatory + Trace of Abundance + Wild Growth, Mariposa + Wild Growth, **Living Wish in hand**.
`offered=0`. Add any single real blue source and the button appears:

| variant | offered | verified |
|---|---|---|
| as reconstructed | **0** | — |
| + Yavimaya Coast | 1 | 0 |
| + Adarkar Wastes | 1 | 0 |
| + Kitchen | 1 | **1 (wins)** |
| + 2 energy (Aether Hub's any-colour mode live) | 1 | **1 (wins)** |

The board already holds **two Trace of Abundance**, each of which adds *"one mana of any color"* to
its host's tap (`cards.json`: `land_aura_produces: []`, `land_aura_extra_mana: 1`). That is a blue
source. `comborules::HasBlueOrBlackSource` does not see it — and `comborules::HasRedSource`,
twenty lines above it in the same file, explicitly does:

```cpp
// A land Aura's "one mana of any color" rides its HOST's tap and is wild -- it is this
// deck's only real red (Fertile Ground, Trace of Abundance), so it counts here even though
// it can never count for {C}.
if (d->params.is_land_aura && d->params.land_aura_produces.empty()
    && d->params.land_aura_extra_mana > 0)
{ return true; }
```

The clause is simply missing from the blue/black predicate. `UB` gates rules 3, 4 and 5
(`IN-HAND`, `WISH-DRAW`, `WISH-NODRAW`), so on any board whose only blue is a Fertile Ground or a
Trace of Abundance, three of the five rules cannot fire at all. This is the aggressive-display
direction the user asked for, so it is a clean phase-2 fix.

### M2 — most of the rest is the reconstruction's own floor, not a rule defect

Checked individually, the remaining reference misses are boards where 5 of 8 permanents are tapped
and the enumerator finds only two plans at all (e.g. `R_claude_s2_gi1_15`, `plans=2`); adding a blue
source changes nothing. Those frames are mid-turn positions the user reached **with mana floating**,
and the scenario harness cannot stage a floating pool — the documented lower bound that
`combo_off_frames.py` carries too. They are recorded but should not be counted as rule defects.

### Class (d) — RULE TOO LOOSE: none found

The python oracle refuted **zero** offered states outright. The `MTG_COMBO_OFF_BANKABLE` arithmetic
added in Session 14c is doing its job: nothing in this sweep fired on a loop that provably cannot
bank its own path's cost. Every failure is an execution failure, not a false promise.

(The one *arithmetic* gap found is C3's: the bank is counted in generic mana only. That is not a
class-(d) refutation because the rule's own cheapest-reading convention is deliberate; it is
reported as a cluster and as a user ruling.)

---

## 5. THE SEARCH-SHORTCUT VIEW

The addendum: *"the intention is to use the same logic to skip work for the search."* The population
that governs that question is autonomous play at the deck's shipped settings — **551 states over 59
games** — and the answers are not encouraging for a *display-rule* shortcut, but are very
encouraging for a *verified* one.

```
states 551 over 59 games; offered 14 (2.5%), of those verified 8 (57.1%)
FALSE FIRE  (offered, arithmetic refutes) :  0   (0.00% of states)
MISSED FIRE (absent, engine wins that turn): 22  (3.99% of states)
games with any fire: 12/59
at FIRST fire: mean 0.17 further main-phase decisions and 0.17 further turns
               to the engine's own win
```

Read these four numbers together:

1. **A shortcut keyed on `combo_off_offered` would be WRONG 43% of the time** (6 of 14 fires in
   autonomous play do not win). Inside the search that is not a slowdown, it is a corrupted
   evaluation and a moved ground truth. **Do not key the shortcut on the display rule.**
2. **A shortcut keyed on `combo_off_verified` would have been right 8/8 here and 268/268 overall.**
   That is the only safe trigger on the evidence — but see the cost note below: `verified` is a
   *full trial go-off*, not a predicate.
3. **The shortcut as currently scoped would save almost nothing.** At the first state where any rule
   fires, the engine is on average **0.17 main-phase decisions and 0.17 turns** away from winning by
   itself. The table fires only once the kill is already immediate; the expensive part of the turn
   has already been searched by then. Widening the rules (M1, and whatever ruling C3 gets) is a
   prerequisite for the shortcut being worth anything, not an optional extra.
4. **Missed fires outnumber fires**: 22 absent-but-won against 14 fired (and 13 of the 22 are
   `MAIN_1` frames, i.e. not explained by the oracle's untap). The rule table currently sees roughly
   **two in five** of the states a shortcut would want.

### Is the rule evaluation cheap enough to run inside the search?

**The rule table itself: nearly, with two caveats.** Every ingredient predicate in `comborules` is
a single battlefield walk with a `LookupCached` per permanent — genuinely O(board). But two of them
are not:

* `WishReachesFinisher(s, c, draws)` walks the **entire library** (`for (const Card& l : ap.library)`)
  whenever the wish is not in hand and a draw source is live — ~50 `LookupCached` calls per
  evaluation.
* `CheapestFinishNeed(s, c, /*where=*/2)` walks the **library again** for the wish, then the
  sideboard, calling `BoardCanPayColors` per candidate.

Both are on the `WISH-*` path, which is the deck's main line, so they are hit constantly. Neither is
hard to hoist (a per-state "is a wish still in the library" flag maintained on draw/cast would do
it), but as written the table is O(board + 2·library), not O(board).

**The verify is emphatically not cheap, and it is the half that is actually reliable.** `combo_off_
verified` is a whole `ApplyPlanDirect` of the go-off: up to `FlickerMaxIterations` = 60 blink
iterations, each with an ETB untap, a tap-ahead and the sink spends, plus the finisher's activations
(up to ~50 library exiles). Session 14 measured its consequences directly and capped
`MTG_COMBO_OFF_TRIES` at 2 because of it (*"measured: 40 s per step at eight"*). Timing here put one
extra full apply on a 16-blink / 12-exile board at **≈2 ms** — against this deck's default
per-decision search budget of 20 ms, that is ~10% of an entire decision for **one** trial, at one
node.

**So the design question phase 2 inherits is:** the cheap half is the unreliable one and the
reliable half is the expensive one. Options worth measuring — none prejudged here — are (i) gate the
verify on the table so it runs only on the ~2.5% of states that fire, (ii) make the table itself
sound enough to trust by folding in `net_c` (C3) so `offered` approaches `verified`, or (iii) cache
the verify per (board-signature, turn) so a replayed prefix pays once.

---

## 6. Fidelity limits of this sweep (state them before trusting a number)

* **No floating mana.** `--scenario` cannot stage a mana pool, so every reconstructed frame is asked
  with an empty one. This makes the sweep a **lower bound on offers**: a button that appears anyway
  would have appeared in the real frame too. It is the main reason the reference population shows
  misses that are not rule defects (§4 M2).
* **The win probe untaps and draws.** It steps into `turn` through the untap and draw steps, so it
  is an **upper bound on winnability**. `untap_confound` is recorded per state; `MAIN_2` engine
  frames are the worst affected.
* **Library ORDER is synthetic.** Engine and reference states rebuild library *contents* from the
  decklist minus (hand + battlefield + graveyard + everything already cast and gone) — which is what
  makes `WISH-DRAW`'s library scan answerable at all — but the order is canonical, not the real
  shuffle. Rule `W` is order-independent; the *dig* in C1a is not, so C1a's exact draw counts are
  indicative rather than exact.
* **Exile is invisible in reconstruction.** A resolved Living Wish exiles itself, and the frame
  snapshots do not record the exile zone, so a rebuilt library can over-count. Such states are
  flagged `_lib_approx` and are excluded from any false-fire conclusion.
* **Energy is not reconstructed.** `Player::energy_counters` is a player resource and is not in the
  frame snapshot, so a reference/engine Aether Hub reads as `{C}`-only. This suppresses offers (it
  cannot create them), so it too is conservative — and §4 M1's `+2 energy` arm shows how much it can
  cost.
* **Combat kills are split out, not dropped** (`c_missed_offer_combat`, 43 states).
* One reference frame (`R_claude_s11_gi10_58`) carries `combo_off_verified` with an **empty
  `combo_off_rule`** — it wins, so it is not a defect, but the viewer's rule badge would render
  blank there. Minor, noted.

---

## 7. Phase-2 worklist, in the order the evidence supports

| # | cluster | states | kind | fixture |
|---|---|---:|---|---|
| 1 | **C2** damage sink starves the loop | 17 | executor bug (payment, not projection) | `sweep_3_damage_sink_starves_loop.json` |
| 2 | **C1a/C1b** draw-to-find route | 22 | executor bug (same shape, draw sink) — *in progress by another agent* | `sweep_1_draw_engine_never_draws.json`, `sweep_2_draw_route_zero_blinks.json` |
| 3 | **C3/C5/C6** {C} pip supply vs generic-mana sizing | 45 | needs a USER RULING (size the count, or narrow the rule) | `sweep_4_c_pip_supply_not_sized.json` |
| 4 | **M1** `HasBlueOrBlackSource` misses land-Aura wild mana | ≥1 confirmed, gates 3 of 5 rules | rule too tight — widening, so doctrine-aligned | reference `claude_s1_gi0` #37 |
| 5 | search-shortcut trigger: `verified`, never `offered` | — | design | §5 |

Re-run after each fix with `bash test/combo_off_sweep.sh --reuse-games` and diff
`logs/combo_off_sweep/results.json` on the stable state ids.

---

## 8. Open questions for the user (surfaced, not blocking — nothing waited on these)

1. **C3 is the big one and it is a doctrine question.** A board can hold three {C} sources and still
   feed a {C}-pip finisher at net zero, because Eldrazi Displacer eats one pip per pass.
   `loop.net_c <= 0` predicted failure on **42 of 42** offers. Do you want the display narrowed by
   `net_c > 0` (fewer buttons, all of them real), or the go-off count sized by the pip supply so the
   *execution* catches up (display unchanged, aggressive as you asked)? The second is more work and
   matches the stated doctrine better; nothing was changed either way.
2. **`GORGE` has never once executed** — 17 offers, 0 wins, and the cause (C2) hurts every board that
   merely holds a Shivan Gorge, not just the Gorge line. Should the fix restore the Gorge path, or is
   spending the loop's float on the Gorge the wrong behaviour inside a Combo Off apply in the first
   place?
3. **The search shortcut would currently save ~0.17 turns.** Is it worth pursuing before the rules
   are widened (M1 + whatever C3 becomes)? On today's table it fires in 12 of 59 games and only once
   the win is already one decision away.
4. Still open from Session 15/15b and untouched here: rules 2/3's `(D or E or C2)` rider is
   inference, not your words; rule 4 has no graveyard-Emiel guard; "bankable" vs raising
   `MTG_EDF_MAX_ITER`.


---

# PHASE 2 — the fixes, and what they moved

Phase 1 above is the measurement; this is what was done about it. Everything below was measured as
a **one-binary A/B on the same 1051 states**: the four mechanism levers off versus on, same build,
same mined games. That is the only honest before/after, and it is what the tables here report.

Measured on the rebased tree — this branch's four fixes sit on top of the seed-6 executor work
(`d671f568`, `bff3ef0b`), so the OFF arm already contains those.

## 2.0 The instrument that found all four: `MTG_EDF_LOOP_TRACE`

Default OFF, zero cost when off, and it never branches game logic. `[edf-goff]` prints the count a
go-off was **sized** at and `[finish]` counts the kill chain; between them sat `ApplyBlinkLoop`,
whose every break was silent. Phase 1 had 29 states whose plan promised a long chain and whose
apply ran **zero** blinks, and nothing could say which of the three breaks fired or why.

The trace prints, per iteration, the floating and available pools at four points — `enter`,
`post-damage-sink`, `post-draw-sink`, `post-tapahead` — and then `STOP at k=N: <reason>`. Every fix
below was found by reading one of its lines, and the two mistaken hypotheses it killed are recorded
in the code beside the fixes so they are not tried again.

## 2.1 C2 — the damage sink (rule GORGE: 17 offers, 0 wins)

**Two independent defects, both in the guard, both now fixed.**

**`MTG_COMBO_OFF_SINK_TRIAL`.** `SpendSurplusOnDamageSinks` guarded itself with a pooled
`ManaPool::CanPay(sink_cost + keep_payable)`. A pooled answer cannot see that one land serves one
of its modes (Brushland taps for `{C}` **or** `{G}`/`{W}`), nor that the sequential payment picks
greedily. So it passed, and the ping then took the board's last colourless source for a generic
pip:

```
k=4 enter             cost={2}{C} float{g1} avail{g5 c1 *3}
k=4 post-damage-sink  cost={2}{C} float{g5} avail{g5 c0 *0}
STOP at k=4: pay-failed
```

Four blinks of twenty. The guard is now a **real trial**: on a copy of the state it taps the sink,
pays the sink's cost through the loop's own payer, and requires the activation to pay as well. That
needed one new piece of plumbing — `StateManaPayer`, an optional state-taking payer that only the
COMBO OFF apply path supplies (both autonomous `ApplyBlinkLoop` call sites pass `nullptr`).
**Measured on that board: 4 blinks → 25.**

Two narrower repairs were tried first and **both measured inert**; both are recorded in the code.
(a) Reserving the {C}-capable sources across the spend through `g_plan_reserved_sources` —
reserve-then-fallback releases them again, and the sink's payment genuinely needs one. (b)
Re-running the *same* pooled projection after tapping the sink. (b) fixes a real bug on its own
terms — `{T}` is in the sink's cost, so a pool built before that tap credits mana the activation is
about to destroy, and Shivan Gorge taps for `{C}` — but it is not *this* bug: the guard still fired
at k=4 and the payment still stranded.

**`MTG_COMBO_OFF_GORGE_SLOT`.** A ping is once-per-untap, so `ApplyBlinkLoop` promotes the sink to
the front of the untap priority — and that slot is then **not** a yield land. `loop.refund` is the
top-`untaps` land yields with no such reservation, so on a two-untap board it credits Kitchen (5)
*and* a Conservatory (2) when the iteration really gets Kitchen (5) and the Gorge (1). With the
refund over-counted the branch sized `life` iterations on a board whose real per-iteration budget is
`5 + 1 − 3 (blink) − 3 (ping) = 0`.

| board | before | after |
|---|---|---|
| Cloud of Faeries (2 untaps), opp life 20 | 20 blinks, 10 pings, no kill | **not offered** |
| Cloud of Faeries, opp life 10 / 5 | 5 blinks, 3 pings, no kill | **offered, WINS** |
| Peregrine Drake (5 untaps), opp life 20 / 10 | wins | wins |

Same precedent as Session 14c's `MTG_COMBO_OFF_BANKABLE`: a rule the user wrote, right in **kind**
and unable to **count**. Across the sweep the effect is `GORGE 4/17 → 4/8` — **every win kept, nine
false offers withdrawn.**

Fixtures: `edf_co_12_gorge_two_untaps_absent.json` (correctly ABSENT, with the arithmetic in its
comment) and `edf_co_13_gorge_drake_wins.json` (offered and verified) — a negative and its positive
control, which is what makes the narrowing safe to ship.

## 2.2 C3 — the {C} pip supply (`net_c <= 0`: 39 offers, 0 wins)

**USER, writing rule 4:** *"the 1 colourless source works even when you have displacer out because
you can draw into Emiel and cast it if you can draw your deck."* The rule table believed that; the
executor never did it.

Eldrazi Displacer's blink is `{2}{C}` and Emiel the Blessed's is `{3}` — the same mana value, and
one of them spends a colourless **pip** every pass. Cloud of Faeries untaps two lands and the
`{C}`-starved reservation makes exactly one of them colourless, so the Displacer loop nets **zero**
colourless and a `{1}{C}` drain is fed only by the board's opening supply.

**`MTG_COMBO_OFF_OUTLET_SWITCH`** deploys a held pip-free outlet and switches the loop to it, under
four conditions that cannot all hold on a healthy board: the button is active, *this* outlet spends
a `{C}` pip, a **separate** `{C}`-pip sink is on the battlefield, and a pip-free outlet is in hand
whose colours the board can produce.

**`PipFreeOutletFromHandLive`** then makes the recognizer size the count on the **post-swap**
colourless net. Both halves are required, and the measurement says so exactly:

| arm | blinks | drains | result |
|---|---:|---:|---|
| no swap | 14 | 9 of 20 | no kill |
| swap only | 14 | **17** of 20 | no kill |
| swap + post-swap sizing | 20 | **20** | **WINS** |

The swap works perfectly and the loop still stops three drains short, because `c_iterations` was
gated on the *pre-swap* `net_c` of zero. Fixture: `edf_co_14_outlet_switch_to_emiel.json`.

**A death-recovery form was tried first and proved unreachable**, and the reason is the cluster's
real shape: the loop does **not** die. It runs its whole sized count and the *finisher* starves —
`pay(c)` never fails, so nothing downstream ever notices. That is recorded in the code.

**What is still open here.** `sweep_4` as authored has no Emiel anywhere — not on the battlefield,
not in hand, and not in the library (the synthetic matrix fills libraries with Forests). The user's
own route for that board is rule 4's *draw* into Emiel, so it waits on §2.5's defect. The swap it
needs is already in place and will fire on the drawn copy.

## 2.3 M1 — a land Aura's wild mana is a blue source (`MTG_COMBO_OFF_UB_AURA`)

`comborules::HasBlueOrBlackSource` walked `EffectiveProduces` only, while
`comborules::HasRedSource` **twenty lines above it in the same file** explicitly counted a land
Aura's *"one mana of any color"*. `UB` gates three of the five rules (IN-HAND, WISH-DRAW,
WISH-NODRAW), so a board whose only blue is a Fertile Ground or a Trace of Abundance could not fire
any of them. One helper now answers both colour questions.

Found on the user's own `references/EldraziDisplacerFlicker/claude_s1_gi0` **turn 3 — the turn they
won** — whose two Trace of Abundance are its only blue. Measured one card at a time, nothing else
changed:

| board | offered | verified |
|---|---|---|
| as reconstructed (before) | **0** | — |
| + Yavimaya Coast / + Adarkar Wastes | 1 | 0 |
| + Kitchen / + 2 energy (Aether Hub live) | 1 | **1 (wins)** |
| **as reconstructed (after the fix)** | **1** | **1 (wins)** |

The same clause must **not** be added to `ColorlessSourceCount`: a land Aura's wild can never pay a
`{C}` pip (`ManaPool` credits it as `wild`, deliberately not `wild_c`). Fixture:
`edf_co_15_ub_from_land_aura.json`.

## 2.4 The numbers — one binary, 1051 states, levers off vs on

| | OFF | ON |
|---|---:|---:|
| offered | 271 | **274** |
| **won** | 194 | **198** |
| offered but did not win | 77 | **76** |
| missed offers | 28 | **25** |
| correctly absent | 709 | 706 |

Per rule (wins / offers):

| rule | OFF | ON |
|---|---|---|
| `DEPLOYED` | 103/119 | 102/119 |
| `WISH-DRAW` | 59/102 | **64/113** |
| `IN-HAND` | 26/31 | **27/33** |
| **`GORGE`** | **4/17** | **4/8** |
| `WISH-NODRAW` | 1/1 | 1/1 |

Class transitions: **4 missed offers became wins**, 1 correct absence became a win, **4 failures
became correct absences**, and 6 correct absences became honest *unproven* offers — the
aggressive-display trade, working as the doctrine asks.

**Read the last row of that honestly.** The count of "offered but did not win" barely moves (77 →
76) because the fixes *remove* false offers while M1's widening *adds* unproven ones. That is not a
wash, because the two are not the same thing: `combo_off_verified` remains a **perfect oracle** —
198/198 verified offers won, 0 of 76 unverified did, zero exceptions in either direction across all
three populations. A verified offer has never once lied. An unverified one says so on the button
("not yet proven"), which is exactly what the user asked for.

Against the phase-1 baseline (this branch's fixes **plus** the seed-6 executor work):
**184 → 198 wins, 84 → 76 failures.**

## 2.5 STILL OPEN — the draw-to-find spend is unbudgeted (located, not fixed)

The coordinator's open question — *"with Living Wish 13 cards down the draw-to-find loop reaches 10
draws and stops even though +7 a pass against a 6-mana-per-card engine should reach it inside 60
iterations"* — is **located**, and it is the C2 defect one sink over.

Bisecting the wish's depth in `edf_co_11_seed6_t4_draw_the_deck.json`:

| wish depth | blinks | draws paid | wish cast | won |
|---:|---:|---:|---:|---|
| 1 (top) | 31 | 2 | 2 | yes |
| 2 | 37 | 4 | 2 | yes |
| 4 | 49 | 8 | 2 | yes |
| **6** | **6** | **10** | 0 | **no** |
| 9 / 11 / 12 / 13 | 6 | 10 | 0 | no |

**The numbers past the cliff are IDENTICAL at every depth**, which is the whole tell: the failure is
not depth-dependent at all. The loop always dies at iteration 6 having paid exactly 10 draws; when
the wish is within four cards the draws happen to reach it first, and past that they never do.
`MTG_EDF_LOOP_TRACE` shows the cause in one line:

```
k=6 enter             cost={C} float{c1} avail{g3 c1 *5}     <- nine mana and a {C}
k=6 post-damage-sink  cost={C} float{c1} avail{g3 c1 *5}     <- damage sink: no change
k=6 post-draw-sink    cost={C} float{}   avail{}             <- the draw sink took EVERYTHING
STOP at k=6: pay-failed
```

`SpendSurplusOnDrawSinks` spends the pool to zero without preserving the loop's next activation —
and that activation costs **one mana** here. Same shape as C2's damage sink, same guard weakness,
one call site below it.

**The fix is a three-line mirror of §2.1's and the plumbing is already landed**: `probe_pay` (the
`StateManaPayer`) is in scope at that exact call site, so the draw spend can take the same real
trial the damage spend now takes. Left unimplemented deliberately — `SpendSurplusOnDrawSinks` and
the `want_draw` route are another agent's active area, and the standing protocol is to report
rather than collide.

## 2.6 NOT TOUCHED — `BankableMana` and the floating pool

The s9_gi8 play-drift (`BankableMana` ignoring the floating pool: `max(0,net)*60 = 60 < ~104`, while
`60 + 80` banked clears it) is **not** touched by this branch. `BankableMana`, the `affords` gate,
`CheapestFinishNeed` and `ComboOffPossible` itself are all unmodified here — verified by diff. The
only `comborules` functions this branch changes are the colour predicates (`HasBlueOrBlackSource`,
`HasRedSource`, and the new shared `HasColorSource` / `LandAuraMakesAnyColor`), which sit adjacent
to `BankableMana` but do not read or write it. Safe to fix on the tip.

## 2.7 Flags added

Every one is default **ON** with `=0` as the opt-out, and every one is gated so autonomous play is
byte-identical by construction — `HumanPlayActive()` for the display/sizing side,
`ComboOffFinishActive()` (plus a null `probe_pay`) for the executor side.

| flag | what it gates |
|---|---|
| `MTG_COMBO_OFF_SINK_TRIAL` | the damage sink's real-trial guard (C2) |
| `MTG_COMBO_OFF_GORGE_SLOT` | reserving the untap slot the damage sink takes (C2) |
| `MTG_COMBO_OFF_OUTLET_SWITCH` | swapping to a pip-free outlet, and the post-swap sizing (C3) |
| `MTG_COMBO_OFF_UB_AURA` | a land Aura's wild counting as a blue/black source (M1) |
| `MTG_EDF_LOOP_TRACE` / `_N` | diagnosis only, default **OFF** |

## 2.8 Gates (rebased tree)

`./build.sh` clean; `scenarios.sh` **79/79**; `combo_off_check.sh` **15/15** (11 inherited + the 4
promoted here, every positive one verified); `regression.sh --smoke` **ALL PASS 73/73, 0 configs
changed** — byte-identical, which is the proof the gating holds.


## 2.9 C1a/C1b CLOSED — the draw sink gets the same real trial (`MTG_COMBO_OFF_DRAW_TRIAL`)

§2.5 above left this located and unfixed, as another agent's call site. It is fixed now, and it is
the C2 repair verbatim, one call site below.

`MTG_DRAW_GUARD_SELFTAP` had already corrected a real over-count in this guard -- it counted the
source's own yield, which the `{T}` half was about to spend -- and the guard still passed while the
payment still stranded. The bisection in §2.5 is what proved the remaining cause was the projection
itself and not the dig: **identical numbers at every wish depth past the cliff.**

So the guard becomes a real trial on a copy of the state, and it is applied to **both** halves of
"draw a card" so they cannot disagree — the `{T}` draw/investigate sources and `CrackCluesForCards`,
which carried the same pooled guard for its `{2}`.

| wish depth | before | after |
|---:|---|---|
| 1 / 2 / 4 | 31 / 37 / 49 blinks, WIN | unchanged |
| **6** | 6 blinks, no kill | **60 blinks, WIN** |
| **9 / 11 / 12** | 6 blinks, no kill | **60 blinks, WIN** |
| **13 — the user's real depth** | 6 blinks, no kill | **60 blinks, 26 draws, WIN** |

`[finish] guard-refused=12` on that board is the trial working as intended: it declines the draws
that would starve the loop and pays the 26 that would not. `edf_co_11_seed6_t4_draw_the_deck.json`
is now set to thirteen cards down — strictly subsuming the one-draw chain it pinned before — and
with `MTG_COMBO_OFF_DRAW_TRIAL=0` that same fixture does not win.

**One binary, 1051 states, this lever alone:**

| | OFF | ON |
|---|---:|---:|
| **won** | 198 | **206** |
| offered but did not win | 76 | **68** |
| offered | 274 | 274 |

**Nothing else moved** — identical offers per rule (DEPLOYED 119, WISH-DRAW 113, IN-HAND 33,
GORGE 8, WISH-NODRAW 1) — because this is purely an execution fix. It converts failures into wins
without touching the display at all, which is the cleanest shape a fix in this area can have.

Mechanism counts: `draw-to-find-starves-loop` 13 → **0**, `apply-did-nothing` 9 → **0**,
`loop-zero-iterations` 14 → **1**, `draw-loop-stopped-early` 22 → 8,
`damage-sink-starves-loop` 5 → **1**, `wish-never-cast` 13 → 8.

### Where the branch now stands

Against the phase-1 baseline of **184 wins / 84 failures**, and counting both agents' work on this
tip: **206 wins / 68 failures.** `combo_off_verified` remains a perfect oracle — 206/206 verified
offers won, 0 of 68 unverified did.

The residue is dominated by `loop-ran-finish-never-fired` (36) and `finish-fired-short` (31), both
of which sit on the `net_c <= 0` axis that §2.2's outlet swap only reaches when a pip-free outlet
is actually available. On the synthetic matrix it is not (the generator fills libraries with
Forests); on real boards it now can be, because the draw engine works.
