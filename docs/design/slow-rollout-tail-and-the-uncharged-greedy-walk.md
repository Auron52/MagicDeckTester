# The slow-rollout tail, and the greedy walk nothing ever charged

**Status (2026-10-17): the 2026-09-22 deferral is SUPERSEDED by the rulings recorded further down.**
* **USER RULING 2026-09-24: fix the budget, not discovery.** The generation-side decision ceiling
  `MTG_DECISION_WORK_X=1000` was adopted 2026-09-25 and is wired into `scripts/mullgen.sh` only.
* **`MTG_FUNGUS_SHRINK_SAC_M2` ADOPTED, DEFAULT ON, in 694ddf6a (2026-09-25).** The USER's words:
  *"If it is a general win, then let's adopt."*
* **`MTG_SOLVE_CHARGE`**, the lever this header calls "not settled", is still default OFF
  (`TurnSolver.cpp`).

Later sections carry their own statuses. Read them rather than this header.

Original status (2026-09-22, SUPERSEDED): **OPEN — deferred 2026-09-22 by the user, deliberately.** The diagnosis below is complete and
the evidence is on disk. The fix is *not* settled: the obvious lever exists, is built, and is wired
into six hosts, but adopting it changes play, and the user's position is that it may not be right
as-is. Read this before touching either.

Background: `docs/design/fungus-token-search-cost.md` (the Fungus cost ledger),
`docs/design/adaptive-batched-keepgen.md` (the generator this bit).

---

## Symptom

Fungus mulligan generation, recipe `complete`, commit `5d84f3c6`, 24 cores to itself. After **5.8 h**
it had frozen **0 of 110,524** cells and was sitting at **2.88 of 24 cores** with the journal silent
for 89 minutes. It never entered the refine phase.

The proximate holder was a heavy tail of rollouts. At a **3 ms** budget:

| | |
|---|---|
| slow rollouts (>= 30 s) logged in 5.8 h | **205** |
| worst rollout that *completed* | **1,513,974 ms (25.2 min)** |
| rollouts still in flight when it was killed | 3, each **>= 81 min** — i.e. already 3.2x longer than anything that had ever finished |

Evidence archived at `logs/fungus_mullgen_stalled_5d84f3c6/` (journal, slow.log, gen.log).

Two distinct defects met here. **One is fixed; this doc is about the other.**

* *Generator* — the floor->refine phase transition had no work left to hand out once speculation hit
  its constant bound, so the box idled behind the stragglers. Fixed: see
  `docs/design/keepgen-precompute-filler.md`. That fix makes the machine stay busy; **it cannot make
  an unbounded rollout terminate.**
* *Engine* — the rollout tail itself. That is what follows.

---

## Root cause: `SearchBudget` never sees the greedy subset walk

`TurnSolver::SolveUncached` enumerates subsets of castable spells and scores each one in
`consider()`. `SearchBudget` counts **one unit per simulated turn-step**. A subset visit is not a
turn-step, so the walk is invisible to the budget.

Measured on the degenerate Fungus cell: **12,578,432 subsets scored** against a typical **3,120**
(**4,031x**), while `units_total` — the meter the budget actually reads — recorded **458,797**. The
budget was not exceeded. It was never consulted.

This is why the problem keeps coming back as a "new" bug: nothing is wrong with the budget, and
nothing is wrong with the enumeration. They are simply not connected.

### It is not a generation-only problem

The flag's own header records two prior sightings, both in ordinary play:
Melira keepgen discovery at **35-152 s rollouts against 20 ms budgets**, and suite game
**gi32/s1033 spending 289 s deciding a t5 win**. Any fix scoped to the generator would leave those.

---

## The lever already exists, and has never once been switched on

`MTG_SOLVE_CHARGE` (`src/ai/TurnSolver.cpp`, `GreedyChargeEnabled()` ~line 2227):

* Charges **one budget unit per `consider()` visit**; when the budget exhausts, the walk stops and
  **keeps best-so-far**. Combo/persist-loop cuts pre-seed lethal lines first, so kills stay found.
* **Deterministic** — unit-counted, no wall clock.
* Installed by every budget-holding host via `GreedyChargeGuard`: `SimulateToEndImpl`,
  `FSLineWin`/`FSLineTail`, `SolveSecondMainInSearch`, `SolveWithLookahead` — **six sites**.
* Exchange rate is already calibrated and argued: a subset visit costs ~1-2 us, a budget unit is
  ~1.1 us (900/virtual-ms), so 1:1 is the honest rate.

It shipped OFF with this plan:

> *"enable per-run (generation drivers; Melira probes) and flip the default only with a rebaseline."*

**Neither half happened.** `grep -rn MTG_SOLVE_CHARGE scripts/ src/analyzer/ test/` returns
**nothing** — no script, no analyzer path, no test ever sets it. The per-run half was never wired, so
the flag has been dead code for its entire life and the budget above has never been charged by
anything. Cross-reference the repo's own recurring lesson: a lever behind a default-off gate is dead
code, and the *reachability* is the defect, not the mechanism.

---

## Why this is NOT just "flip the default"

The original note's caution is real: **when the budget binds, rollout-leaf picks change and search
scores move with them.** Unlike a dedup over provably-equivalent work (e.g. `MTG_FUNGUS_SPORE_POOL`,
adopted the same day and avg-identical on 3,200 held-out games), this **truncates a real search**. It
can cost average win turn, and the cost is unmeasured.

Open questions a future session must answer before adopting anything:

1. **What does it cost?** Full gate — scenarios, smoke, regression, and a held-out sweep on fresh
   seeds. Smoke cannot answer it: several of its cases are 25-150 games, where one game is a 0.04
   swing. Size the measurement to the question.
2. **Is 1:1 the right rate?** It is calibrated on *mean* subset cost. The pathology is in the tail,
   where a visit may be far more expensive. A rate that is honest on average can still let a
   degenerate cell run long.
3. **Is a flat charge even the right shape?** It spends the budget uniformly. An alternative is a
   cap on *enumeration width* for the specific explosive structure (below), which would leave normal
   boards byte-identical instead of shaving every walk slightly.
4. **Generation vs play must agree.** Enabling it only in the generator would fit a keep table under
   an engine we do not ship — the generate-on-the-engine-you-ship rule. Whatever is chosen has to
   hold on both sides, or the artifact's play fingerprint is a lie.

---

## What actually explodes: the structure, not the deck

From the 205 slow rollouts, versus the base rate of the card appearing in a 7-card hand:

| in slow rollouts | observed | base rate | enrichment |
|---|---|---|---|
| Doubling Season | 143/205 (70%) | 39.9% | 1.7x |
| Utopia Mycon | 161/205 (79%) | 39.9% | 2.0x |
| **both** | **102/205 (50%)** | **14.5%** | **3.4x** |

In the worst 30 by duration, Doubling Season appears in **28**.

The mechanism is specific and worth stating, because it suggests a targeted fix: **Utopia Mycon is a
"sacrifice a Saproling" mana outlet, and Doubling Season doubles token production.** So the walk
enumerates subsets over a Saproling set whose size is itself being doubled — combinatorial growth in
a quantity another permanent inflates. Any deck pairing a token doubler with a token-sacrifice outlet
has this shape; Fungus is where we happened to meet it.

A width cap keyed on *that* structure (sacrifice-outlet subsets over interchangeable tokens) would be
narrower than a global charge and would leave every other board untouched. Note the repo's own
counter-lesson before assuming narrower is cheaper: a narrower gate once cost MORE, because precision
fragments the state space. Measure both.

---

## Reproducing it

```
# The degenerate cells, worst first (hand composition is on each line):
grep SLOW-ROLLOUT logs/fungus_mullgen_stalled_5d84f3c6/Fungus.keepmodel.exhaustive.raw.json.slow.log \
  | sed 's/^.*SLOW-ROLLOUT \([0-9]*\)ms/\1/' | sort -rn | head

# Worst single seed observed (size7, draw, r=4):
#   seed=1663341906637430961  hand: Doubling Season x2; Forest x1; Psychotrope Thallid x1; Utopia Mycon x3
```

`MTG_BF_CENSUS=1` reports `bf_scored greedy_subsets=` / `search_subsets=` (`bfcensus::g_subsets_scored`,
`src/ai/TurnSolver.cpp` ~line 202) — that is the counter the 12,578,432 figure came from, and it is
the right instrument for any candidate fix: a fix that does not move it has not touched the cause.

---

## Related, and still open

* **Mycoloth's devour axis (16x)** — a separate, already-identified Fungus search-cost item, untested
  against this. Devour eats Saprolings, so it plausibly shares the same explosive structure.
* **The spore pool's residual tail.** `MTG_FUNGUS_SPORE_POOL` (adopted 2026-09-22) cut the *frequency*
  of degenerate rollouts enough that the floor and sub-table passes completed for the first time — real
  progress — but the worst case went 22.1 min -> 25.2 min. It dedupes spore *sources*; it does nothing
  about the size of the Saproling set being subset-enumerated. Do not expect more from it.
* **A Fungus overnight regression tier** — Fungus is the heaviest deck in the suite and currently has
  no overnight coverage.

---

## DISCOVERY IS THE PHASE THAT BLOCKS — and it is the one with no backstop (2026-09-24)

New, and it changes where a fix should be aimed. Everything above measures the tail inside
*generation*, which is pooled, journalled and resumable — a long cell there costs throughput, not
progress. **Equivalence discovery is different: it is a barrier.** Nothing downstream starts until
the last probe lands, so its tail is wall-clock the operator sits through. USER, 2026-09-24:

> *"Discovery is the one phase that we actually wait for. It typically isn't too crazily long, so
> this has rarely bitten."*

And the reason it has rarely bitten is **not** that discovery is cheap — it is that discovery is
**cached** (`<stem>.keepmodel.gencache.json`, fingerprint-gated: `ExhaustiveKeep.cpp` ~744, "a hit
skips discovery"). A deck pays it once. A NEW list pays it in full, and there is nothing to stop it.

### Measured on Fungus candidate B (decks/Fungus/candidate-b-2026-09), `fast` recipe

400 probes x 22 candidates = 8,800 rollouts at play settings (d5/b20, from `BuiltinDefaultPlay` —
this list deliberately ships no value leaf):

| | |
|---|---|
| discovery wall | **31+ min** (still on its last 2 rollouts) |
| rollouts over 30 s | **73 of 8,800** |
| worst single rollout | **1,061.8 s — 17.7 minutes** (Doubling Season, probe 112) |
| next worst | 692.9 s / 687.7 s / 645.0 s (Utopia Mycon, Saproling Burst) |
| utilisation | **23.9/24 at 6% done -> 2.0/24 for the last two rollouts** |

The hands are the ones this document already fingered: `Doubling Season` and `Utopia Mycon` are the
exact pair in the "worst single seed observed" line above. Same structure, different phase.

### THE USER NAMED THE PRECISE GAP, and it is the one this document exists for

> *"We have a budget on discovery, though, since it is done at play settings. That said, in some of
> these cases the budget is not bounding things as well as it perhaps should."*

That is exactly right and it is this bug. Discovery **does** carry a budget — 20 virtual-ms — and it
does not bound: 20 virtual-ms produced a 17.7-minute rollout. The signature matches the 3 ms ->
25.2 min case recorded above, because the currency is the same: a unit is one simulated turn-step,
and the greedy subset walk inside a step bills nothing.

**There is additionally NO wall-clock backstop on this path.** `MTG_MAX_GAME_PREDICT_SEC` /
`MTG_MAX_GAME_WALL_SEC` (the value-leaf tail guard) live in `src/runner/BatchRunner.cpp` ONLY —
`mtg-analyze`'s keepgen route never sees them. So a discovery rollout today is bounded by a
currency that undercounts it and by nothing else. `MTG_SOLVE_CHARGE` remains wired into six
`GreedyChargeGuard` hosts and set by nothing in `scripts/`, `test/` or any manifest.

### Two candidate fixes, and they are NOT equally safe

**(a) Charge the greedy walk during discovery.** Fixes the cause. But it runs into open question 4
above ("generation vs play must agree"), and it changes scores, so it can MERGE candidates that are
not actually equivalent — a coarser partition, which is the dangerous direction: a hand then gets
labelled by a bucket it does not belong to.

**(b) A discovery-scoped WALL CAP with a conservative fallback.** If a discovery rollout exceeds N
seconds, abandon it and place the candidates in **separate** buckets. This is strictly safer than
(a), and the asymmetry is the whole argument:

* It can only ever produce a **FINER** partition. It never merges two candidates on the strength of
  a truncated comparison — it declines to merge. A finer partition costs downstream cells (more
  work in the phase that is pooled and resumable, i.e. the cheap place to spend) and cannot cause a
  hand to be scored by a bucket it does not match.
* Open question 4 bites (a) harder than (b). Point 4 is about the table's VALUES being fitted under
  an unshipped engine. Discovery does not produce values — the labels come from the generation
  rollouts at the `mull_gen_*` contract (d1/b3 here). Discovery produces the PARTITION. A
  conservative partition is not a claim about the shipped engine's scores, it is a refusal to make
  one.
* It is provably play-neutral by construction, because it would exist only on the discovery path.

**Neither is adopted, and (a) must not be flipped on by default** — the user reverted exactly that
on 2026-09-22 (*"I don't want to do this change in this session. Nor am I sure we want it as-is at
all."*). What (b) still needs before adoption: a chosen N, and confirmation of how far K moves on a
deck that trips it (candidate B is the natural subject, and its gencache now makes the comparison
cheap to re-run).

### USER RULING 2026-09-24 — fix the BUDGET, not discovery

The recommendation above (prefer (b), the discovery-scoped wall cap) is **superseded**. The user's
call, and the reasoning is better than the argument it replaces:

> *"It is a good point that on heavy decks that operation can be an issue. This might point more
> toward the need for properly bounding things by budget, though, rather than a need for a change
> to discovery."*

So discovery is the **symptom that made the defect visible**, not the thing to fix. A wall cap
scoped to discovery is a local patch: it would stop this one barrier from stranding the box while
leaving the currency broken everywhere else — the same 20-virtual-ms-buys-17.7-minutes failure would
still be live in play, in generation, and in every future phase that trusts a budget to mean
something. Bounding the budget properly repairs all of them at once, and it is the fix this document
was opened for.

That also re-weights the open questions above. Question 2 ("is 1:1 the right rate?") and question 3
("is a flat charge even the right shape?") become the load-bearing ones, because the target is no
longer "make discovery stop early" but "make a unit cost what it claims to cost". Question 4
(generation vs play must agree) stops being an obstacle and becomes an argument FOR this direction:
a budget that bounds on both sides is exactly what keeps the artifact's play fingerprint honest.

Discovery keeps ONE property worth remembering when the fix is measured: it is the only phase that
is a **barrier**, so it is the most sensitive test of whether a candidate bound actually works.
A fix that leaves one 17.7-minute rollout in 8,800 has not fixed it — 1 of 24 cores for the last
12 minutes of a 45-minute phase is what that residue looks like.

---

## SNOW, 2026-09-25: the undercount measured on a NON-degenerate deck, and the labeller's own verdict

Two gaps in the record above, both filled by a cancelled Snow mulligan generation.

### 1. The undercount is not a Fungus pathology -- it is the ordinary rollout

Everything above measures a *degenerate* cell (12,578,432 subsets against 458,797 units = 27.4 per
unit, a token doubler feeding a sacrifice outlet). That framing invites the reading that the defect
only bites explosive board structures. It does not. At Snow's shipped labeller settings, d2/b1, with
`MTG_BF_CENSUS=1 MTG_ROLLOUT_STATS=1` on 16 ordinary rollouts:

```
[score] depth=2 budget_ms=1 rollouts=16 work_units=434182 units_per_rollout=27136
[rollout-stats]   bf_scored greedy_subsets=2793865 search_subsets=170425
```

**6.43 greedy subset visits per charged unit** (6.83 with search subsets). No doubler, no sacrifice
outlet, no degenerate cell -- just Snow. At the 1:1 rate this document already calibrates, the budget
undercounts a typical Snow labeller rollout by **~7x**.

And on Snow the tail is NOT where the money is, which is worth recording next to the Fungus story:
689 rollouts over the 30 s threshold cost 9.16 core-h of a ~192 core-h run -- **4.8% of the work from
0.044% of the rollouts**. A tail cap buys ~5% here. The median rollout is the cost.

**Instrument gap, for the next reader:** `MTG_BF_CENSUS` alone prints nothing. It increments the
counters; the printer is `RolloutStatsReporter`'s destructor gated on `MTG_ROLLOUT_STATS`
(`TurnSolver.cpp:1367`). With the census alone the subset lines silently read **0**, which is
indistinguishable from "this deck has no greedy walk" -- it cost a wrong reading before it was caught.

### 2. Open question 1, answered for the LABELLER -- and it is not the Melira answer

Question 1 asks "what does it cost?" and the only datum was Melira's, on **play**: 613->370 s but avg
4.96->5.24, **-0.28 t**, rejected. A labeller is not judged on play quality. It is judged on whether
it RANKS hands like the shipped policy -- the criterion that made d2/b1 adoptable while shifting the
mean +0.056. That test had never been run for this flag.

200 paired openers from the real opening distribution, forced kept (the gen shape), R=30, held-out
seed 9201, Snow d2/b1, census on both arms (`logs/snowopt/label_charge_ab.py`):

| | base | + `MTG_SOLVE_CHARGE` | bar d2/b1 cleared |
|---|---|---|---|
| wall | 257.3 s | **169.8 s = 0.660x** | -- |
| rho | -- | 0.9968 | >= 0.9984 **MISS** |
| mean shift | -- | +0.0180 t | -- |
| dispersion sd | -- | 0.0288 | <= 0.030 pass |
| pairwise order agreement | -- | **10,685/10,685 = 100.0%** | 100.0% tie |

**1.52x faster for two of three fidelity criteria.** Not the clean sweep d2/b1 was, but nothing like
Melira's -0.28 t either -- because truncating the walk moves scores in a way that shifts hands
together rather than reordering them. Every one of 10,685 reference-separated pairs kept its order.

This does **not** license enabling it for generation: open question 4 stands (a table fitted under an
engine we do not ship), and the 2026-09-24 ruling deliberately chose the global budget repair over a
per-phase patch. What it does is price that repair. Snow's mulligan gen projects ~20-24 h; 1.52x takes
it to ~13-16 h, still over the ~8 h window, so the repair is necessary and not sufficient -- it needs
`MTG_FOLD_SEARCH_ODO` (1.13x) and a K reduction (~1.30x) stacked with it to approach a night.

Full context, including the cancelled run's state and the resumable 30 MB journal:
`docs/design/snow-generation-cost-2026-09-25.md` section 4c.
## MEASURED 2026-09-25 — the currency gap is 4 ORDERS OF MAGNITUDE, and bounding it buys 1.33x

The user's ruling above says to fix the budget. This section measures what that is actually worth,
on the real candidate-B keepgen workload, and the honest answer is **much less than the diagnosis
suggests**. Record it so nobody re-runs this hoping for a different number.

**Read this next to the Snow section above — they disagree about where the money is, and both are
right.** On Snow the >=30 s tail is **4.8%** of the run ("the median rollout is the cost"); on Fungus
candidate B it is **32.6%**. So *a tail ceiling is a deck-shaped lever*, worth ~5% on an ordinary
deck and ~1.3-1.5x on one with a token doubler feeding a sacrifice outlet. Neither number
generalises; measure the deck. The two sections also test different levers, and the difference is
what makes this one adoptable: Snow measured `MTG_SOLVE_CHARGE`, which moves scores and so runs into
open question 4 (a table fitted under an engine we do not ship). The ceiling below is chosen
*precisely at the point where it does not move them* — same play digest, same resumable journal —
so question 4 does not bite it.

### The right lever is NOT `MTG_SOLVE_CHARGE` — it is `MTG_DECISION_WORK_X`

`src/ai/DecisionWorkMeter.h`. Both are default-off dead levers, but they differ in shape, and the
difference is exactly the doc's open question 3 ("is a flat charge even the right shape?"):

* `MTG_SOLVE_CHARGE` bills **every** `consider()` visit 1:1, so it shaves **every** walk and moves
  every board's search. Melira measured it at **613→370 s but avg 4.96→5.24 (−0.28t)** — rejected
  for play (`docs/design/analysis-Melira Pod.md`), and the user reverted a default-flip 2026-09-22.
* `MTG_DECISION_WORK_X` is a **per-decision TOTAL ceiling** at `base_budget x X`. It is not a tax on
  every walk, it is a cap that binds only on the tail — and it **already bills the greedy subset
  walk**, through the `else if (decisionwork::Armed())` branch at `TurnSolver.cpp:23397`, with NO
  need to touch `MTG_SOLVE_CHARGE` at all. It also stops SOFTLY: the existing `Overrun` path makes
  iterative deepening commit the deepest completed pass, deterministically.

The Melira doc measured the ceiling DEAD **as a play lever** (X=10 lost the win, X=100 saved
nothing) and explicitly reserved the machinery "for attribution and **generation-side use**". This
is that generation-side use.

### How broken the currency is, measured (74,860 armed decisions, 2.96e9 units)

`MTG_DECISION_WORK_X=100000000 MTG_DECISION_WORK_DEBUG=1` on the real candidate-B floor pass —
a ceiling so high nothing trips, so the meter is pure instrumentation. Nominal budget **2,700
units** (3 ms x 900/virtual-ms):

| | units | vs budget |
|---|---|---|
| p50 | 1,482 | **0.5x** |
| p90 | 33,345 | 12.3x |
| p99 | 634,319 | 234.9x |
| p99.9 | 4,007,489 | 1,484.3x |
| **max** | **76,114,923** | **28,190.7x** |

**The median decision is comfortably UNDER budget.** The budget is not too tight and it is not
mis-set: it is simply not connected to the tail, exactly as the root-cause section says. One
decision in this sample spent 28,191 times its stated bound.

### What a ceiling buys, and the wall it hits

| ceiling | trips | units removed | speedup | play digest |
|---|---|---|---|---|
| X=10000 | 0.007% | 4.8% | 1.05x | unchanged |
| X=5000 | 0.025% | 8.7% | 1.10x | unchanged |
| X=2000 | 0.076% | 17.2% | 1.21x | unchanged |
| **X=1000** | **0.172%** | **24.6%** | **1.33x** | **unchanged — `e0ffdb608cd70b25`** |
| X=500 | 0.409% | 33.5% | 1.50x | **MOVES** -> `a46cec7fafa66603` |
| X=100 | 2.141% | 58.5% | 2.41x | **MOVES** -> `0cae3360bd3f19a3` |

**X=1000 is the boundary**, verified directly against the resume gate rather than argued: with the
banked journal in place, `MTG_DECISION_WORK_X=1000` prints
`RESUME(journal): reloaded 344625 cell-sides` and takes the discovery cache hit, while X=100 prints
`equivalence cache fingerprint MISMATCH -- re-discovering`. So at X=1000 the whole banked
generation survives; below it, nothing does.

That is the trap in this lever, stated plainly: **it is play-neutral precisely BECAUSE it almost
never fires.** The 64-game d1/b3 battery confirms it from the other side — its top-12 slowest
rollouts total 85.5 s at baseline and 79.6 s at X=1000, a ~7% move on games that are not
degenerate. The generation's hand distribution is far more skewed than the battery's, which is why
the unit counterfactual (24.6%) is worth more there than the battery suggests.

### Ranked against the AGGREGATE, which is the only ranking that counts

From the cancelled run's own log (`awk 'NR>1891'`, 23,102 s wall, ~149 core-hours of generation):

* **576 rollouts >= 30 s consumed 48.6 core-hours — 32.6% of ALL generation compute, from 0.084% of
  the 683,735 rollouts.**
* **8 rollouts (0.0012%) consumed 20.1 core-hours — 13.5% of the entire machine.** Worst single
  rollout **16,319 s against a 3 ms budget**.
* 7 of those 8 hands hold Doubling Season; Mycoloth is in 3 of the top 4. Same structure this
  document already fingered — a token doubler feeding a sacrifice outlet — now with devour on top.
  They are mana-light hands that never win early, so they play all 8 horizon turns on a huge board.

So even PERFECT elimination of the >=30 s tail is **1.48x**. The ceiling reaches 1.33x of that
without moving play at all, which is most of what is available from this direction.

### Where the other 4x is NOT

An aggregate `perf` profile of the real floor pass (`build/Profile`, 62k DWARF samples, all 24
threads — deliberately aggregate, because ranking by the worst cell bought 1.05x last round and a
stale wrong-cell profile bought 1.06x the round before) is **flat**. No symbol exceeds 8% self:

```
7.6% __memmove_avx (GameState clones)   6.5% CreateTokens        5.1% BuildSimKey
4.8% SimulateEndAndStartNextTurn        3.3% OnCreatureDies      ~12% the mana/payment family
28.4% SimulateEndAndStartNextTurn INCLUSIVE   13.0% SweepDeadFadeTokens INCLUSIVE
```

`SweepDeadFadeTokens` still shows 6.6% under it in `vector<Permanent>::erase`, but that erase
**cannot** be batched into a compaction pass: `OnCreatureDies` runs between erases and must observe
the board already changed. The byte-identical angle there is closed.

A flat profile with no hotspot is the signature of **work volume, not a bad function**. Candidate B
costs **0.786 core-s/rollout** against the shipped Fungus list's **0.197** at identical depth,
budget and horizon; strip the whole >=30 s tail and the remaining bulk is still **0.53** — 2.7x the
shipped list.

> **CORRECTION, same day.** The sentence that stood here — "that residue is real width, not waste" —
> was an inference from the flat profile, not a measurement, and it is **WRONG**. See the next
> section: profiling the shipped list on the SAME binary shows the two profiles have different
> *shapes*, and the residue is a cache-layout defect. The original 0.197 was also a cross-binary
> figure (it predates the board-width fixes), so the 4x ratio itself was never apples-to-apples.

### CONCLUSION — the per-rollout axis is nearly exhausted; the cell-count axis is not

Priced against the 23.86M-rollout projection (~9 days at 24 cores):

| lever | multiple | cost |
|---|---|---|
| `MTG_DECISION_WORK_X=1000` | **1.33x** | none — digest unchanged, journal + gencache preserved |
| X=100 | 2.41x | voids 344k cell-sides; needs scenarios+smoke+regression+held-out |
| perfect tail elimination | 1.48x (ceiling) | not reachable |
| bucket merge K=22 -> K=14 | **~12x on cells** | a mana-base decision, the user's to make |

**The engine axis tops out near 1.3–2.4x. The 5–20x the user is asking for is only in the bucket
count**, which `mullgen-cost-is-driven-by-bucket-count.md` prices and which is a decklist property,
not an engine one. Do not re-open the budget direction expecting more than this table.

### ADOPTED 2026-09-25 — generation-side only, gate green

`play_digest` is a 64-game behavioural sample, not a proof of byte-identity, so X=1000 was gated
before wiring:

* `test/scenarios.sh`: **103 passed / 0 failed** with the ceiling armed, and a line-by-line diff of
  every scenario's `win_turn` / `opponent_life` / `active_life` against the unarmed run is **empty**.
* `test/regression.sh --smoke`: **93 passed / 0 failed / 0 new, rc=0**, against committed ground
  truth — reproduced twice. The smoke fingerprint is `<avg>[/<play_digest>]` per case, so this is a
  per-case play-digest match, not just an aggregate one.

Wired into `scripts/mullgen.sh` as `MTG_DECISION_WORK_X="${MTG_DECISION_WORK_X:-1000}"` on the gen
invocation — overridable from the environment, and **generation-side only**. It must NOT become an
engine default: the Melira measurements are the standing reason (dead as a play lever, and the user
reverted a default-flip of the sibling lever on 2026-09-22).

Note what this does and does not claim. It does not make the budget honest — a decision may still
spend 999x its stated bound. It removes the top 0.17% that spend more than a thousand times it, and
that is 24.6% of all work.

---

## 2026-09-25 — the bulk gap is a CACHE-LAYOUT defect, not board width (user was right)

The section above concluded the per-rollout residue was inherent width. **That was wrong**, and the
user pushed back on exactly the right sentence: *"There must be something in how we are handling the
engine if it is that much slower than the original list."* There is. Here is the measurement.

### First, two errors in the comparison that produced "4x"

1. **It was cross-binary.** Candidate B's 0.786 core-s/rollout is the current binary; the shipped
   list's 0.197 comes from a run predating the board-width fixes. Ratios across binaries are exactly
   what `perf-ratios-are-scoped-to-deck-artifacts` warns about.
2. **A value-leaf confound was suspected and REFUTED.** `decks/Fungus/Fungus.value.json` carries a
   real 120-tree `eval_model` + `value_leaf_table`; candidate B's is metadata-only. Since a missing
   value leaf is priced at 1.35-84.8x, this looked decisive. It is not: stripping the model from the
   shipped sidecar (keeping `value_play` so depth/budget stay d1/b3) leaves the play digest at
   `8d83c48e723b26b7` and the 64-game battery at **2 s in both arms**. The leaf does not engage on
   this path. Do not re-run this A/B.

The battery does show the real gap cleanly, same settings, same binary: **shipped 2 s, candidate B
22 s — 11x.**

### The profiles have different SHAPES, not different magnitudes

Both `perf record -F 199` for 150 s at full saturation (24 threads), so self-% is comparable as
absolute CPU:

| symbol | shipped | candidate B | ratio |
|---|---|---|---|
| `OnCreatureDies` | 0.13% | 3.30% | **25x** |
| `GatherBoardSources` | 0.17% | 2.22% | **13x** |
| `CreateTokens` | 0.51% | 6.46% | **12.7x** |
| `__memmove` (GameState clones) | 1.44% | 7.56% | **5.3x** |
| `FireEtbWatchers` | 0.29% | 1.42% | 4.9x |
| `SweepDeadFadeTokens` | 0.00% | 0.90% | new (Saproling Burst) |
| `BuildSimKey` | 2.52% | 5.12% | 2.0x |
| `SolveUncached` consider lambda | **5.36%** | 2.49% | 0.46x |
| `EnumeratePlanPositions` | **3.98%** | 1.15% | 0.29x |

**Board mechanics: 7% of the shipped list, 32% of candidate B. The SEARCH share falls.** Candidate B
is not thinking harder — it is churning board state and thinking less.

### The cause, from the annotation rather than the symbol

`sizeof(Permanent) = 320` bytes (five cache lines; `Card` alone is 136). `perf annotate` on the two
hottest board walks:

```
GatherBoardSources:  29.94%  cmpb $0x0,0x101(%rbx)      <- ONE BYTE at offset 257
                      2.18%  lea  0x0(%rbp,%rbp,4),%rbx <- index*5, i.e. a 320-BYTE STRIDE
OnCreatureDies:      35.75%  mov  %r13,%rdi             <- LookupCached arg setup
                      1.19%  cmp  0x88(%r13),%eax       <- offset 136 = controller_index
```

**A board walk touches a fresh cache line per permanent to read one byte.** On a board of hundreds
of interchangeable Saprolings that is the whole cost. This is the identical signature recorded for
the `CreateToken` doubler fix (then 296 bytes, 47.88%+49.25% on two field loads) — the struct has
since grown to 320.

Candidate B reaches those boards because it plays 4x Doubling Season **and** 4x Saproling Burst,
4x Mycoloth, 4x Undercellar Myconid; the shipped list has 19 Forests, 2 Mycoloth and no Burst.
Same engine, and only one of the two lists makes the layout hurt.

### The levers, all byte-identical (so they PRESERVE the banked journal)

1. **Hot/cold split — the targeted one.** The hot walks read one or two small fields
   (`def_absent` at 0x101, `controller_index` at 0x88, `is_token`, `created_by_number`). A parallel
   array of those bytes makes a scan touch one cache line per ~64 permanents instead of one per
   permanent. Directly aimed at the measured 29.94% instruction.
2. **Shrink `Permanent`.** 320 -> ~160 B halves both the clone bytes (`__memmove`, 7.56%) and the
   stride. Much of the struct is `int` where `int16_t` would do, and `Permanent* attached_to` is
   documented in its own comment as **a dead stub**.
3. **Pool identical tokens onto a count axis.** The structural answer — hundreds of Saprolings that
   are genuinely interchangeable are stored as hundreds of 320-byte objects. This is the only lever
   in the 5-20x class. Precedent is good but NOT the same thing: `MTG_SAC_OUTLET_POOL` (adopted
   2026-09-23, 2.29x on the heaviest cell) pooled the *action*, not the *permanent*.

**Sizing, honestly:** (1)+(2) attack a 32% share and plausibly return ~1.3x; they are mechanical and
byte-identical. (3) is the one that could change the verdict, and it is a real design change.
None of this is implemented yet.

## 2026-09-25 (later) — the 22x LOCATED: one game in 24, and a 2.95x lever that is not free

> **CORRECTION to the section above.** It called the gap "a CACHE-LAYOUT defect". The layout defect
> was real and is fixed (`57a67598`), but it is worth **0.65%**, not the gap. Measured below. The
> section's diagnosis of *where* the cost is — board mechanics, not search — survives; its
> implication that fixing the layout would collect it does not.

### Where candidate B's cost actually is

Same 24 games, same seed / depth / budget (d1/b3), same profile shape, **same average win turn
(5.5417)**, one arm at a time on the box:

| list | wall | ratio |
|---|---|---|
| shipped Fungus | 1.8 s | 1x |
| candidate B | 40.7 s | **22.4x** |

And it is not spread over the 24 games. With `MTG_SLOW_GAME_MS=300`:

* **game 11 alone = 21.6 s = 53% of the whole job.** Games 11 + 10 + 17 = **80%**.
* game 11 burns **1,339,602 work units** against a typical ~25,000 — so the search visits ~53x more
  NODES, it is not merely slower per node.
* shipped Fungus's worst game in the same 24 is 357 ms.

Repro: `--seed 90011 --game-index 11 --games 1 --depth 1 --budget-ms 3 --ignore-play-profile`.

**What that game is.** It wins on turn 7, and the turn-7 board is **136 permanents of which 120 are
identical "1/1 Saproling Token"** and 8 more are identical "0/0 Saproling Token" — 94% of the board
is two duplicate groups (Saproling Burst + Doubling Season). `perf record` on it is **FLAT**: the top
symbol is `BuildSimKey` at 3.95%, nothing else over 3%. That is the signature of work VOLUME, and it
is why no micro-optimisation moves it.

`MTG_BF_CENSUS=1 MTG_ROLLOUT_STATS=1` on that game:

```
bf_census mean_width=57.1 max_width=896      68% of candidate mass at width >= 129
bf_shape  chosen_x=456524 share=0.817        <- 82% of all candidate mass is ONE axis
bf_src    Saproling Burst activations=393475 distinct_physical_sources=1
bf_scored greedy_subsets=10479713
```

**One physical Saproling Burst emitted 393,475 activation actions in one game.** Shipped Fungus runs
zero Saproling Burst and zero Undercellar Myconid; candidate B runs four of each. That is the 22x.

### The 2.36x that was not there — and the comparator hole behind it

`bf_width` on that game read `repeat_share=0.576 collapse=2.36x`: 58% of everything handed to the
scorer looked like the SAME plan twice, which reads as a free 2.36x in an enumeration-side dedup —
a skip this file's own comments already contemplate. **It was false**, and the thing that caught it
is the safety number the same census prints beside it:

```
dedup_exact repeats=321620 state_agreed=16046 exact_FALSE=305574
```

95% of the "repeats" reached a DIFFERENT post-apply state. Its header says that means the comparator
is blind, and it was: **`BpCandFingerprint` never folded `Action::devour_count`**, so "cast Mycoloth
devouring 0" and "cast Mycoloth devouring 3" were fingerprint-identical. Folding it (`b219b565`):

| | before | after |
|---|---|---|
| `repeat_share` | 0.576 | **0.00072** |
| `collapse` | 2.36x | **1.00072x** |
| `exact_FALSE` | 305,574 | **0** |

So there is no dedup prize, and a skip built on the old fingerprint would have silently deleted
305,574 genuinely different lines. It was also not only a census bug: that fingerprint keys the wave
walker's W0Len memo (`BpLenRecord` / `BpLenKey`), so the collision let a stillborn-slot length
learned under one devour count be reused under another. Latent — every gate is byte-identical — but
wrong.

**Method note worth keeping.** Two holes had already been patched by hand at that site (`rock_mana`,
`breakpoint_casts`) and this was the third. Guessing field-by-field failed twice here; what worked in
one run was (a) re-applying the same plan to two pristine copies of the same parent to prove the
apply was deterministic and therefore the comparator guilty, then (b) `ExactFalseActionDiff`, which
diffs all 62 `Action` fields against the twin plan sharing the fingerprint. That helper is kept
(census-only) so a fourth hole is READ OFF A RUN rather than reasoned about.

### The lever that does move it: `MTG_FUNGUS_SHRINK_SAC_M2` — 2.95x, and it is the USER's call

*(2026-10-17: the USER made that call. It was ADOPTED default ON in 694ddf6a, 2026-09-25.)*

Priced against the worst game, every default-OFF Fungus lever, one at a time:

| lever | game 11 |
|---|---|
| baseline | 21.92 s |
| `MTG_FUNGUS_DEVOUR_CANDS` | 21.43 s |
| `MTG_FUNGUS_SHRINK_SAC_PAYER` | 21.40 s |
| `MTG_SAC_AXIS` / `MTG_FUNGUS_SPORE_POOL` / `MTG_SAC_OUTLET_POOL` | 21.9–22.1 s |
| **`MTG_FUNGUS_SHRINK_SAC_M2`** | **6.80 s (3.22x)** |

This is the lever built TODAY from the user's own directive (*"we should move this into the second
main along with Mycoloth"*), left default OFF because a combined A/B "came back >=2.9x SLOWER and
could not say which half did it". **This is that attribution, and the sign is the other way for this
half.**

| block | arm | wall (serial, alone on the box) | avg win turn | games changed |
|---|---|---|---|---|
| tail block, 24 games seed 90000 | base | 22.60 / 21.97 s | 5.5417 | — |
| | **m2** | **7.51 / 7.46 s (2.95x)** | **5.5417** | 1 better (gi11 7->6), 1 worse (gi12 6->7) |
| general, 200 games seed 70000 | base | 23.8 / 23.5 s | 5.4200 | — |
| | m2 | 22.7 / 23.2 s (~1.02x) | 5.4150 | 3 of 200; paired t = **-0.58** |
| shipped Fungus, 200 games | m2 | — | 5.5750 | **digest IDENTICAL** — inert on the shipped list |

Game 11 itself: 22,518 ms -> 7,117 ms, units 1,339,602 -> 543,628.

**HELD-OUT PLAY VALIDATION (1,392 games, five fresh seeds, one pooled batch, both depths).** The
table above is the train block; this is the confirmation, and it is the part that changes the
verdict from "cost lever, play-neutral" to "cost lever that is also a small play WIN". Losses are
scored `max_turns+1 = 9`, the convention the batch's own `avg` uses (recovered exactly from six
independent jobs -- the raw `.wins` column is -1 for a loss and a naive mean of it is wrong by 0.1
here).

| regime | seed | n | base | m2 | diff | changed | better | worse |
|---|---|---|---|---|---|---|---|---|
| gen d1/b3 | 71000 | 200 | 5.5450 | 5.5200 | **-0.0250** | 5 | 5 | 0 |
| gen d1/b3 | 72000 | 200 | 5.4900 | 5.4700 | **-0.0200** | 10 | 7 | 3 |
| gen d1/b3 | 73000 | 200 | 5.5250 | 5.5150 | **-0.0100** | 2 | 2 | 0 |
| play d5/b20 | 74000 | 48 | 5.5625 | 5.5417 | **-0.0208** | 1 | 1 | 0 |
| play d5/b20 | 75000 | 48 | 5.3333 | 5.3125 | **-0.0208** | 1 | 1 | 0 |

Pooled generation regime: **t = -2.68** (n=600, mean -0.0183). Play regime: t = -1.42 (n=96, same
magnitude, smaller n). All held-out: **t = -3.00** (n=696). **Five of five held-out seeds favour the
lever, 15 games better against 2 worse**, and the train seed 70000 agrees, so 6 of 6 overall. Note
the two depths give the SAME effect size (-0.018 / -0.021) -- this is not a shallow-search artefact
that the real play depth would route around, which is the failure mode
`bound-the-axis-before-building-the-search` warns about.

**The shape is the point, and it is the right shape for generation.** The lever is ~1.0x on a median
game and ~3x on the tail — and the tail is where generation cost lives (576 rollouts >= 30 s were
32.6% of the whole candidate-B run). An average-based A/B will therefore always under-read it, which
is exactly how it came to be filed as "slower".

**THE COST, and it is why this is not an agent decision.** It MOVES the keepgen play digest:
`e0ffdb608cd70b25` -> `e0b7376940011349`. Adopting it **discards the 344,625 banked cell-sides**.
Contrast `MTG_DECISION_WORK_X=1000`, which was chosen precisely at the point where the digest does
not move. So the trade is: ~3x on the expensive rollouts, against restarting the generation.

### What is left, ranked

1. **The chosen-X axis on repeatable token-makers** — 82% of candidate mass, one Saproling Burst
   emitting 393,475 activations. Nothing has been built here. This is the only remaining lever in
   the 5–20x class and it is a real design change, not a tuning knob.
2. `MTG_FUNGUS_SHRINK_SAC_M2` — measured above, awaiting the user's adoption call. **[2026-10-17:
   ADOPTED default ON in 694ddf6a, 2026-09-25, on the USER's "If it is a general win, then let's
   adopt."]**
3. **Closed, do not re-open:** enumeration-side exact dedup (1.0007x after the fingerprint fix);
   cache layout (0.65%, landed); the budget ceiling (1.33x, landed); `SweepDeadFadeTokens` batching
   (refuted — `OnCreatureDies` runs between erases).

---

## 2026-10-03 — a 1.17x WAS still in the per-rollout axis: the soulbond scan was never gated

**This section amends the "the per-rollout axis is nearly exhausted" conclusion above.** That
conclusion is still right about the thing it measured — the BUDGET direction really is closed, and
the ranked table above stands. But it generalised from "the budget ceiling is out of room" and from
a FLAT aggregate profile to "the axis is out of room", and that was wrong. Re-profiling the same
floor pass on today's binary found a single symbol at **15.07% of all runtime, 7.4x the next one**.

### What the stale profile hid

The 2026-09-25 table in the cache-layout section is no longer the profile. Those board-mechanics
symbols have since been fixed and have fallen off (`CreateTokens` 6.46% -> <0.5%, `OnCreatureDies`
3.30% -> <0.5%, `GatherBoardSources` 2.22% -> <0.5%, `__memmove` 7.56% -> 0.95%). What the fixes
uncovered was `FireEtbWatchers`, which that table listed at a harmless 1.42%.

Method note, because it is the transferable part: **a conclusion of "flat profile, no hotspot" has a
shelf life.** It is a statement about one binary. Four board-width fixes later the distribution was
not flat at all, and nobody re-looked because the doc said not to.

### The defect

`SpellEffects.h`, the soulbond block of the enter cascade. Its comment read *"Gated on a soulbond
permanent actually being in play, so every other deck pays one bool read."* There was no such gate:
**the scan IS the presence test.** It walks the whole battlefield with a `LookupCached` per
permanent looking for `params.soulbond`, so a deck holding no soulbond card never takes the early
`break` and always pays the complete O(board) walk -- once per creature entering.

It survived two previous inventories of this exact family (`GameState.h` records them: the
`deck_has_subtype_enter_counters` pass, then the "AND A THIRD/FOURTH of the same shape" pass)
because it DOES have an entrant gate, `IsCreature() || is_animated`, which makes it read as one of
the cheap members. **That gate is vacuous on a token deck**, where every entrant is a creature. The
general lesson: an entrant gate is only a gate if the entrants are actually heterogeneous, and the
decks where these scans hurt are precisely the ones where they are not.

`perf annotate` on the loop, which is the same cache signature this document already recorded for
`GatherBoardSources`:

```
50.39%  cmpb $0x0,0x89(%r14)      <- def_absent, ONE BYTE at offset 137
24.15%  jne
 4.33%  add  $0x128,%r14          <- 296-byte stride = sizeof(Permanent)
```

### The fix and what it measured

`GameState::deck_has_soulbond`, stamped from the decklist in the GoldFishRunner deck scan,
default TRUE so an unstamped state (the scenario harness) keeps the full scan. **No lever** -- a
clear bit proves the loop cannot find anything, so this is dead-code elimination, not a narrowing,
and CLAUDE.md's "collapse wasted search unconditionally" applies. Restricted to `nctrl == 0`
because `core/OpponentDeck.h` is a fixed non-mirror list the stamp does not describe (the same
restriction, for the same reason, as the BuildSimKey graveyard gate `94667fa1`).

| | before | after |
|---|---|---|
| `FireEtbWatchers` | **15.07%** | **0.40%** |
| every other symbol | — | x1.18 renormalised |

**The renormalisation is the proof the work was DELETED rather than displaced**: removing 14.7% of
total time inflates every surviving share by 1/(1-0.147) = 1.172, and the measured spread was
1.16-1.22x with no symbol growing disproportionately. Had the cost merely moved, one would have.

Gates: `test/scenarios.sh` **118 passed / 0 failed**; `test/regression.sh --smoke` **107 passed /
0 failed, configs changed 0, play-changed 0** -- byte-identical, as the construction requires.

A `static_assert` in `Dominance.h` fired on `sizeof(GameState)` 832 -> 840 and is logged there:
DECK CONSTANT, stamped once and never rewritten, so there is nothing to fold into `Build()`.

### CORRECTION to the commit message: which candidate-b numbers this actually invalidates

`632fde7a`'s message says the "17.0x-vs-shipped **unit** decomposition" includes this defect. That
is WRONG and is corrected here, because it matters for which figures to re-measure.

**The enter cascade charges no units** -- there is no `Consume` / `ConsumeAt` / `gamework` site
anywhere in it (verified by inspection over the whole function). The soulbond scan was therefore
pure UNCHARGED wall, which is also why the full regression came back `configs changed 0`: units and
digests could not move.

So, of `candidate-b-cost-decomposed-and-refuted-levers`:

* **17.0x in charged units: STANDS.** Unaffected, byte-identical.
* **24x / 38x WALL, and the 11x battery figure (shipped 2 s vs candidate-b 22 s): INFLATED.** Both
  arms paid the dead scan, but candidate-b paid far more because its boards are far wider, so the
  RATIO is inflated, not just the absolutes.
* **The "2.9x more wall per charged unit" factor is exactly where this lived** -- that factor IS the
  measure of uncharged work per unit, so a large part of it was this one loop.

**That is the sharpest lesson available here.** The decomposition already carried this bug's
fingerprint, in a named factor, for two days: a deck spending 2.9x more wall per charged unit than
its sibling is a direct statement that something expensive is running outside the budget. It was
attributed to "the uncharged horizon leaf" and the search for the cause stopped there. When a
wall/unit ratio is anomalous, the uncharged work is not necessarily where you last found uncharged
work -- re-profile instead of re-using the previous answer.

### Where this leaves the ranking

~1.17x, and it does NOT change the strategic picture the conclusion above draws: the 5-20x class is
still only in the cell count and in pooling interchangeable tokens. What it changes is the advice
"do not re-open" -- that applies to the BUDGET lever specifically, not to re-profiling. **Re-profile
after any batch of fixes lands; a flat profile is a fact about a binary, not about a deck.**

---

# AMENDMENT 3 (2026-10-03): the 1.17x was an AVERAGE, and it understated the fix by ~2.5x

The section above closes with *"~1.17x, and it does NOT change the strategic picture"*. **Measurement
refutes that sentence.** On the workload that actually remained of the candidate-b K=17 generation,
the soulbond gate (`632fde7a`) is worth **2.4x-3.0x of WALL**, not 1.17x -- and it removed the
degenerate tail almost entirely.

## Why an average was the wrong statistic

The 15.07% -> 0.40% figure was a **profile share averaged over a floor pass**. But the defect's cost
is `O(entrants x board_width)`: it ran a full battlefield walk, with a `LookupCached` per permanent,
for every creature entering. On a Saproling/Doubling-Season board that is **quadratic in board
width**, so its cost is not spread evenly over rollouts -- it is concentrated in precisely the
rollouts whose boards get wide. Those are the degenerate cells that dominate the makespan.

An average over a mixed population therefore understates the fix exactly where the fix matters.
**When a defect's cost scales with a state dimension, the aggregate profile share is a lower bound on
its worst-case cost. Price it on the tail, not on the mean.**

## Measured, per-rollout (MTG_KEEP_REPLAY, byte-identical to the gen's own run_one)

| cell (K=17 labels) | in the generation | on the fixed binary | factor |
|---|---|---|---|
| `Wild Growth x1; Utopia Mycon x1; Peat Bog (+1) x1; Doubling Season x4` (size7 draw r=0) | **43,647,925 ms = 12.12 h** | **28,669 ms** | **1,523x** |
| `Vitaspore Thallid (+1) x1; Sol Ring x1; Doubling Season x3; Brightcap Badger (+2) x2` (size7 play r=1) | 42,249,670 ms = 11.74 h | >13 min, stopped unfinished | **>=54x, NOT closed** |

The second row is the honest half: **the tail is massively reduced but not uniformly eliminated.**
One cell went to 28.7 s; another is still minutes-to-unknown. `fungus-doubling-season-rollout-tail.md`
section 6 (a per-rollout bound by REACHABLE STATES, recording a truncation when it bites) is
therefore still OPEN and still worth building -- it is simply no longer the difference between a
feasible and an infeasible generation.

## Measured, aggregate -- matched on PROGRESS, which is the only fair comparison

A resume starts from the same journal, so "the original run's last N seconds" and "the resumed run's
first N seconds" are the same work at the same point. Same box, 24 threads, 20.8 cores busy on both.

| window | rollouts | rate |
|---|---|---|
| original run, final 2,100 s (62,408 -> 64,508 s) | 103,520 | 49.3/s |
| resumed run on the fixed binary, first 2,100 s | 252,928 | **120.4/s** |
| original run, final 1,500 s | 65,280 | 43.5/s |
| resumed run, first 1,500 s | 195,764 | **130.5/s** |

**2.4x on the 2,100 s window, 3.0x on the 1,500 s window.** The marginal rate decays inside a wave
in both runs (easy cells finish first), so the shorter window flatters the fix; 2.4x is the number to
plan with. Slow-rollout DENSITY is the corroborating signal and it is unambiguous: **1 per 66 s in
the original phase, 1 per 420 s on the fix, worst 151 s instead of 12.12 h.**

## A correction to this document's own tail accounting

An earlier reading of `Fungus.keepmodel.exhaustive.raw.json.slow.log` put the degenerate tail at
~73% of all compute. **That was wrong: the slow log is APPEND-MODE across runs**, so it mixed the
K=22 run's 3,450 slow rollouts (216.0 core-h, worst 7.14 h) with the K=17 run's. Split at the first
`(+N)` merged label -- which only the K=17 bucketing can produce -- the K=17 figures are:

* 424 slow keep-rollouts, **99.4 of the run's 430.1 core-h = 23.1%**
* top 20 = 78.8 core-h = 79.3% of the slow total, i.e. **18.3% of the whole run**
* worst 12.12 h, median slow rollout 41 s

So the tail was 23%, not 73%. **Any accounting over that file must be split by era first** -- and the
`(+N)` label is the discriminator that does it, because a merged bucket's label cannot exist before
the merge.

## Why the whole-run speedup exceeds the sum of its per-rollout parts

23% of compute in the tail cannot by itself yield 2.4x. The rest is a MAKESPAN effect: a 12-hour
rollout **parks one of 24 worker threads for 12 hours**, so the degenerate cells cost far more in
wall than in core-hours. Removing them returns threads to the pool. This is the same shape as the
`MTG_SAC_OUTLET_POOL` adoption, which moved CPU 1.07x against wall 1.32x. **On a pooled queue with a
heavy-tailed work distribution, core-hours understate the value of killing the tail; measure wall.**

## AMENDMENT 3b: after the fix, SIX instruments say nothing else is degenerate

Asked "is there anything else degenerate with the profile generation?", I ran every census this repo
has against the worst K=17 cell (the one that was 12.12 h and is now 28.8 s) and against the live
run. **Nothing else is degenerate.** Recording the numbers so this is not re-litigated.

| instrument | reading | verdict |
|---|---|---|
| `perf` on the phase that remains | flat: top symbol **3.48%**, biggest CATEGORY 17.3% (mana payment), 600 symbols | no hotspot |
| odometer (`MTG_BRANCH_SHAPE`) | 112,284 enum calls / 8,361,695 positions = **74 avg**; worst turn avg 174 | **not wide** |
| plan dedup | plans 2,371,928 -> deduped 2,293,928 = **3.3% duplicates** | no duplicate explosion |
| re-enumeration (`MTG_REENUM_CENSUS`) | **1.27x repeat**, a perfect memo removes **<=21.2%** (explicit UPPER bound) | matches `enum-memo`'s 2.8% real hit rate |
| board width | avg **15.1** permanents by turn 8 | not a token swarm |
| memory | RSS 8.5 GB against a ~35 GB cap | no refusal-driven recompute |
| worker scheduling | **24 of 24 worker threads in R**; only the 3 non-workers sleep | no wave-barrier starvation |

The process shows 19.8/24 cores of CPU rather than 24 because `loadavg` is **30.9 on a 24-CPU box** --
we share it with the WSL host. That is the environment, not the engine. (`cpu.max` = no quota.)

### What IS left, sized honestly -- all ~1.15x, none degenerate

* **Hoisting the shared-resource constraint into the enumeration.** The funnel prices it exactly:
  `SolveUncached` visits 25,050,943 with **15.36%** rejected after generating (dupSacSrc 5.96%,
  wasteSacMana 4.19%, overFodder 5.21%); `EnumeratePlans` visits 3,801,762 with **30.98%** rejected.
  The census prints its own caveat and it is the right one: *"that is the fraction of the walk a
  constraint-bounded enumeration could have skipped BEFORE generating, not the fraction of TIME it
  would save."* Lossless, worth doing under the collapse directive, **~1.15x not 2x.**
* **The residual heavy tail.** Live run worst 181 s against a ~0.5 s median. Section 6's
  reachable-states bound stays the right structural answer; see that doc's 6a.
* **`leaf-eval` ties 245,823 of 340,934 published = 72%** (life-equal 115,142; flips 6,825 = 2%). A
  QUALITY observation, not a speed defect: the evaluator cannot separate most positions the search
  distinguishes. Worth a look on its own merits, not as an optimization.

### The mana-side and two-stage censuses, read correctly

`MTG_ENUM_STATS` reports `mana-side combos raw 2,127,384` == `KEPT`, and "distinct mana only
1,044,383 (collapse **2.04x**)". **Do not bank that 2.04x** -- it is reached by DROPPING the
cards-spent identity, and two lines that spend different cards are different states
(`dedup-signature-must-carry-every-spell-axis`). The sound key is `distinct exact`, which collapses
**1.15x**.

Likewise the "two-stage gating potential ... 1.33x fewer" line is a **ceiling measurement for a
design that is already implemented** (the split materializes each side's lines; see the
degenerate-width guard `MTG_ODO_FLAT_FALLBACK`, default 1,000,000 lines). It is not unexploited
waste. And the guard is **not** tripping here: those counters are AGGREGATES over 634,308 calls, so
per node the payoff side is ~17 lines, not 11 M. Reading an aggregate as a per-node width is the
mistake to avoid.

---

# KITTYEQUIPMENT v3, 2026-10-10: a SECOND deck's tail, and what it changes about this document

This document's standing conclusion is *"the per-rollout axis is nearly exhausted; the cell-count
axis is not"*, with the ranked table topping out at 1.33–2.41x and the instruction **"do not re-open
the budget direction expecting more than this table."** That was measured on Fungus. KittyEquipment
v3 is the second deck to hit the same wall, and it **qualifies two of the claims above rather than
repeating them** — the shape of its tail is materially worse than Fungus's, and the mechanism it
bills to is different.

## The accounting, from the killed run's own slow log

`decks/KittyEquipment/v3-adelbert-gauntlets/*.raw.json.slow.log`, 9.5 h / 32 cores, R=30 (`fast`),
d1/b3, `MTG_DECISION_WORK_X=1000`:

| | Fungus candidate-b | **KittyEquipment v3** |
|---|---|---|
| rollouts >= 30 s | 576 | **6,132** |
| their cost | 48.6 core-h | **220.3 core-h** |
| share of ALL generation compute | 32.6% | **~72%** |
| share of rollouts they are | 0.084% | **~1.5%** |
| worst single rollout | 16,319 s | **3,405 s (56.7 min)** |
| ceiling from PERFECT tail elimination | 1.48x | **~3.6x** |

So on this deck the >= 30 s tail is not a 1.48x prize, it is most of the machine. The *worst single
rollout* is milder than Fungus's; the tail is far **broader**. That difference is what makes the
per-rollout axis worth re-opening here when it was closed there.

**The stall is separate from the cost, and it is worse.** The run's own monitor ended at
`roll7=217588 (0/s) ... frozen=0/106746 (0.0%)` — the size-7 pool fed ZERO rollouts for its last
hour while three rollouts of 56.7 / 40.3 / 36.3 minutes held the barrier, and not one of 106,746
cells had frozen in 9.5 h.

## THE KEEP GENERATOR ARMED NO PER-ROLLOUT BOUND AT ALL

Found by grep, not by inference: `gamework::Begin` has exactly four call sites, and only
`BatchRunner.cpp:1790` arms it. `analyzer/main.cpp:618` and `GoldFishRunner.cpp:949` both pass 0
(deliberately disarmed). `ExhaustiveKeep.cpp`'s `run_one` / `run_batch` never called it — so every
keep rollout in this repo's history ran with **no unit ceiling and no wall deadline**, which is the
whole explanation for a 56.7-minute rollout. `MTG_DECISION_WORK_X` bounds a *decision*, not a game.

A cap here is only meaningful because of `a28b566e`: the flag is set by `CheckDeadline` on a
4,096-unit stride and then has to be OBSERVED, and before that fix the greedy subset walk — the one
region burning these hours — polled nothing.

## A CAP IS A STALL GUARD, NOT A COST FIX

Exceedance counts over the 6,132 captured slow rollouts, with the full-run figure extrapolated by
the ~15.6x of the run that had not been done:

| cap | exceeded | CPU-h above the cap | extrapolated full run |
|---|---|---|---|
| 120 s | 1,799 | 91.1 | ~28,000 |
| 300 s | 548 | 40.0 | ~8,500 |
| 600 s | 151 | 15.6 | ~2,400 |
| 1,200 s | 30 | **3.2** | ~470 |
| 1,800 s | 7 | 1.1 | ~110 |
| 2,400 s | 3 | 0.4 | ~47 |

**At 1,200 s a cap reclaims 3.2 of 220 CPU-h.** The cost is the broad 30–300 s band (1,799 rollouts,
91 CPU-h), which no cap can remove without condemning thousands. Do not reach for a lower cap as a
performance lever.

It also follows that **no cap satisfies a 10-abandonment budget on this deck at the current engine
speed** — not even 2,400 s. That is the fail-out working as intended: its verdict is "optimise".

## USER RULING 2026-10-10 — leave it out, and fail out

> *"Why not just leave it out like we did for this run by manually cancelling it. That said, if this
> happens a significant number of times we error out. Maybe even if it is over something like 10-50
> rollouts."* ... *"We don't want more than 50 for sure and it might make sense to be even more
> aggressive, not allowing 10."*

Shipped as `MTG_KEEP_ROLLOUT_WALL_SEC` (default 1,200; `=0` disables) and
`MTG_KEEP_ABANDON_FAIL_AT` (default **10**, hard-clamped to **50**). Wall only, no predictive stage,
matching the user's phase-A ruling; with no unit ceiling stage 1 is inert by construction anyway.

Two different "leave it out"s, for a reason worth recording:

* **Sub-tables (`run_batch`)** leave out the single ROLLOUT. The accumulation is a plain sum, so a
  `kept` counter is enough, and `t.cnt += kept` instead of `(r1 - r0)` is what keeps a short sum from
  being divided by a full n — which would pull exactly the hardest hands' mean win-turn DOWN.
* **Size-7 (the continuous path)** leaves out the whole CELL-SIDE. There the fold's `c` is both the
  sample count and the index into `slot` (`SLOT(i, pd, c)`), and `slot` is sized to `r_max`: skipping
  one rollout mid-cell either stalls the prefix (the floor gate's `if (c < r0) any_below_floor = true`
  never clears, hanging the phase) or needs a second cursor, slack in the slot array and a new
  journal field to survive resume. That is surgery on the most delicate part of the generator for a
  sample the fail-out bounds at NINE. Zeroing the cell-side lands it in a state the pipeline already
  handles — an unsampled cell, which `recompute` skips and the model falls back to its prior for —
  and is strictly more conservative. **Resume limitation, stated not hidden:** poison is in-memory,
  so a resumed run re-admits those <= 9 cell-sides with a substituted value.

## MEASURING ON THE TAIL — and the two verdicts a MEDIAN bench got wrong

`MTG_KEEP_REPLAY_LIST` (this document's own instrument) replays captured slow rollouts
byte-identically to the gen's `run_one`. Two levers had been priced with `MTG_KEEP_BENCH`, which
stride-samples the ORDINARY floor — blind to the 1.5% of rollouts holding 72% of the compute
(`bench-n-decides-median-vs-tail`). Both were re-priced here on the identical 64 captured rollouts.

**`MTG_KEEP_BENCH=1` is load-bearing in that A/B, not an optimisation.** It makes the equivalence
cache skip the `play_digest` comparison, so every arm resolves the SAME 15 buckets in the same order.
Without it a lever that moves play re-discovers, the work index `w` shifts, the replay list's seed
check fails, and the arm silently replays a different and smaller set of rollouts. Both arms also pin
`MTG_PGO=0`: measured here, PGO is 1.10x faster than Release on this workload, and with `src` dirty
`harness_bin` switches between them on its own.

| lever | median bench (earlier) | **TAIL, 64 captured rollouts** | win turns moved |
|---|---|---|---|
| value leaf (v3's own) | 1.024x | **1.0x** (aggregate over the same cells) | — |
| `MTG_FOLD_SEARCH_ODO` | 0.956x | **0.942x** | **0 of 64**, `sum_wt` identical |

So the median bench was **not** misleading for either one: the leaf is not a tail fix, and the fold
is a real ~6% that the bulk number already had about right. The pre-registered suspicion that a
median bench had hidden a large tail win is **refuted** — worth recording, because it is the second
time on this deck that a plausible mechanism survived argument and died on measurement.

## WHERE THE TAIL'S UNITS ACTUALLY GO

`MTG_BF_CENSUS=1 MTG_ROLLOUT_STATS=1` over the 64 tail rollouts, `units_total=164,593,595`:

| bucket | units | share |
|---|---|---|
| **`equip_route`** | 53,591,450 | **32.6%** |
| `la_cand` | 30,245,915 | 18.4% |
| `fs_pre` | 25,539,904 | 15.5% |
| `rollout_step` | 25,168,091 | 15.3% |
| `greedy_fallback` | 20,655,302 | 12.5% |

and the branching shape:

```
bf_census    decisions=5,353,965  candidates=33,931,459  mean_width=6.34  max_width=2440
bf_scored    greedy_subsets=39,284,721   search_subsets=1,033,256,261        (26x)
bf_foldsite  search calls=6,954,270,539  from_odometer=0  with_tag=0  rejected=0
bf_foldsite  greedy calls=45,952,314     from_odometer=43,873,261
bf_width     raw=33,931,459  distinct_exact=30,340,037  collapse=1.125x
```

Three things follow.

1. **`equip_route` is the largest single bucket, at a third of the tail.** Unlike Fungus, where the
   cost was a token-doubler feeding a sacrifice outlet, this deck's tail bills to the equip-route
   DFS. So this document's "the engine axis tops out near 1.3–2.4x" was a statement about *Fungus's*
   mechanism; a different deck bills elsewhere and has to be re-measured, not inherited.
2. **The search fold site is called 6.95 BILLION times and folds nothing** (`from_odometer=0`),
   because `FoldSearchOdometerOptIn` is a per-deck opt-in that only Snow takes. That is what the
   0.942x arm above buys back.
3. **Width identities bound their own prize at 1.125x** on `la_cand` (18.4% of units), i.e. ~2%
   overall. Not the win, and this document's warning against reading `distinct_srcblind` as
   achievable applies unchanged.

## THE PRE-REGISTERED OBLIGATION THIS RUN TRIGGERED

`DecisionProviders.cpp`, above `NodeUnits()`:

> `!! BEFORE THE NEXT value-leaf OR mulligan GENERATION ON THIS DECK, RE-MEASURE THE WALL. The bound
> this constant exists to enforce is relaxed 4x ... a future regeneration must not inherit this
> number unchecked.`

`MTG_EQUIP_ROUTE_NODE_UNITS` is 2, lowered from 8 on the SCREENING/goldfish shape. The same comment
records that the GENERATION shapes were measured only at 1 and 8, with 1 "proven catastrophic"
there — so for generation the lever points **up**, and this is exactly the regeneration the note was
left for. Arms at 4 / 8 / 16 are measured in `logs/kitty_tail_diag/` by `ab_tail.sh`; the price is
the shape CLAUDE.md blesses (`ChargeDecisionUnits` returns on `budget == nullptr`, so it is provably
inert at an unlimited root and monotone in budget below it).

## THE FULL TAIL A/B, 2026-10-10 — nine arms, identical 64 captured rollouts

`logs/kitty_tail_diag/ab_tail.sh` + `compare_arms.sh`. Cells pinned (`MTG_KEEP_BENCH=1`), both arms
`MTG_PGO=0`, `MTG_DECISION_WORK_X=1000`. `changed` = rollouts whose win turn moved; `sum_wt` is the
aggregate label (299 everywhere except where noted).

| arm | tail cpu | vs base | worst rollout | win turns moved |
|---|---|---|---|---|
| base (committed defaults) | 9114.2 s | 1.000x | 263.3 s | — |
| `MTG_SOLVE_CHARGE_W=16` | 8984.7 | 0.986x | 249.8 | 0 |
| `MTG_EQUIP_ROUTE_NODE_UNITS=4` | 8913.0 | 0.978x | 261.7 | 0 |
| `=8` | 8767.6 | 0.962x | 256.1 | 0 |
| `=16` | 8770.0 | **0.962x (saturated)** | 251.7 | 0 |
| `MTG_FOLD_SEARCH_ODO=1` | 8586.8 | 0.942x | 247.5 | 0 |
| `MTG_FOLD_SEARCH_ODO=1` + `MTG_FOLD_VERIFY=1` | 8508.6 | 0.934x | 255.6 | 0 |
| `MTG_FOLD_SEARCH_ODO=1` + `MTG_SOLVE_CHARGE_W=16` | 8509.8 | 0.934x | 247.2 | 0 |
| **`MTG_FOLD_SEARCH_ODO=1` + `MTG_EQUIP_ROUTE_NODE_UNITS=8`** | **8337.1** | **0.915x** | **246.1** | **0** |
| `MTG_SOLVE_CHARGE_W=4` | 8232.3 | 0.903x | **329.5 (25% WORSE)** | **2** |

**The noise floor is ~1%**, established by accident and worth keeping: the `odo` and `odo_verify`
arms differ only by VERIFY's extra bookkeeping, yet the one doing MORE work came in 0.8% FASTER. So
read 0.962 vs 0.962 as "saturated", not as two measurements, and treat anything under ~1.5% as noise.

Four things this table settles.

1. **The best available is 0.915x, label-identical.** The two levers stack nearly additively (0.942 x
   0.962 = 0.906 predicted, 0.915 measured) and the stack also has the **best worst-case of any arm**
   — which matters more than the aggregate here, because the worst rollout is what holds the pool's
   barrier.
2. **`MTG_SOLVE_CHARGE_W` is NOT the lever it looks like.** W=4 is the cheapest aggregate on the
   board (0.903x) but it moves two labels and makes the worst rollout **25% worse**; and the curve is
   **non-monotonic** — W=16 is 0.986x, worse than W=4. A heavier charge exhausts the budget sooner and
   pushes work into a costlier fallback, the same displacement that flattens the equip price. A single
   measurement at one weight would have been actively misleading.
3. **The equip price saturates at 8.** 16 measures identical, so the knee is 8 and the whole available
   win from that direction is 3.8%.
4. **The engine axis on this deck is ~8.5%, not a multiple.** That is this document's original Fungus
   conclusion holding on a second deck, by a different mechanism. Nothing here changes feasibility:
   against a generation whose dominant phase is a multi-day bottoming refine, 0.915x is rounding.

**NONE OF IT IS ADOPTED, and the generation did NOT run under any of it.** A keep table is fitted to
the labeller's play, so generating under a lever the engine does not ship fits the table to a player
we do not play — the "generation-side rules" the user rejected outright. `MTG_FOLD_SEARCH_ODO` is the
strongest candidate (0.942x alone, 0.915x stacked, and `MTG_FOLD_VERIFY` reports
`recoverable=11,424,464  UNRECOVERABLE=0` on this deck, so the completeness bar is met), but the fleet
screen has kitty at 0.997 — not inert — so adoption changes play and owes a GT rebaseline plus a
held-out quality A/B. That is a user ruling, not an agent's.

**One gap the cap does not close:** discovery rollouts do not go through `run_one`/`run_batch`, so
`MTG_KEEP_ROLLOUT_WALL_SEC` does not bound them — a 33 s discovery rollout is visible in the policy
test's own log. That is the same unbackstopped phase this document flagged in 2026-09-24 and it is
still open.

## 2026-10-10 (late) — THE BOTTLENECK RELOCATED, and the four dials that size it

The run launched at committed defaults was killed by the user at 6.25 h with the honest projection
at **1.5–4 days**. What that run bought is the phase accounting no previous run had, and it moves the
target: **this generation's cost is not the per-rollout tail, it is the adaptive bottoming refine.**

### The phase timeline, measured

| phase | work | measured |
|---|---|---|
| 1. size-7 floor | 213,492 rollouts (106,746 cell-sides x r_floor 2) | **5.0 h** @ 11.9/s |
| 2. sub-table floor | 128,844 rollouts | **1.8 h** @ ~20/s |
| 3. adaptive bottoming waves | wave 1 = 50,136 tasks x 16 = **802,176 rollouts**; wave count unmeasured | **~20–28 h for wave 1 ALONE** |
| 4. size-7 refine | <= 106,746 x (r_max - 2), freeze-pruned | **never reached by any run** |

`roll7` stopping at exactly 213,492 is what identifies phase 1 as COMPLETE rather than stalled, and
`frozen=0/106746` is not a stall either -- freezing only happens in the refine phase, which waits on
`sub_converged`. `subwave=0x50063` decodes as zero waves COMPLETED with a last-wave size of 50,063:
the producer thread was parked inside the dispatch loop of its first wave, which is also why
`roll7` read `0/s`. The generator's own projection formula (`total_cells * 40 / floor_rate`) gives
**~160 h for `complete`, ~80 h for `fast`** on 171,168 cell-sides at 11.9 rollouts/s.

**So the per-rollout axis is on the critical path for the first 5 hours of a multi-day job.** The
nine-arm tail A/B above is still correct and still worth its 0.915x; it is simply not where the job
lives. Record this as the sharper form of this document's standing conclusion: *measure which PHASE
dominates before optimising the thing you can see.*

### The four dials, with the measurement behind each

`MTG_KEEP_FLIP_EPS` was priced without a second multi-hour run, by resuming the killed run's own
journal to its first wave (`MTG_FOLD_SEARCH_ODO=0` reproduced the pre-adoption digest
`3a5a92ae6f6e725c` exactly, 171,168 cell-sides reloaded) and reading the dry-run sweep:

| flip_eps | wave-1 tasks | vs default |
|---|---|---|
| **0.02 (default)** | **50,136** | 1.00x |
| 0.05 | 40,807 | 0.81x |
| 0.10 | 31,858 | **0.64x** |
| 0.20 | 22,752 | **0.45x** |

It governs BOTH halves -- the wave marking and, through `freeze_hit`, the size-7 refine. It only ever
affects NEAR-TIES: a cell is marked only when its decision is already statistically close, so by
construction the two options it is choosing between differ little in value.

`MTG_KEEP_ROLLOUTS` (cap R) is the lever on phase 4, whose ceiling is `(r_max - 2)` per cell-side.
**A wave's batch is `min(16, r_max - c)`, so cap R only shrinks the WAVES once it drops below 18** --
16 is the first value that moves both phases. R is in the journal's resume identity, so it cannot be
changed later without discarding the floor.

`MTG_KEEP_CUTOFF_P` / `_R` is a mass-weighted cell prune that **already existed, defaulted OFF, and
had never been used**. Computed exactly over this deck's 53,373 size-7 compositions, the hand mass is
extremely skewed:

| cells | share of cells | share of HANDS |
|---|---|---|
| 1,348 | 2.5% | 50% |
| 10,513 | 19.7% | 90% |
| 28,539 | 53.5% | 99% |

so a threshold of `P = 1.326e-06` drops **46.5% of the cells for 1% of the hands**.

`MTG_KEEP_SLOWEST_FIRST` (new, default ON, result-neutral) orders each wave's dispatch by the
generator's OWN measured per-cell cost. Every wave ends in a hard barrier
(`sub_wave_pending == 0` gates the next), so a wave's makespan is set by when its most expensive task
STARTS. Deliberately NOT applied to the size-7 floor: that phase is a barrier-free continuous queue
and is already well packed -- measured, it went 80% -> 100% in 13% of its elapsed time.

### Cost is PREDICTABLE from the hand on this deck, and one card explains it

Bucket enrichment in the 2,717 size-7 slow rollouts of the d2/b3 run, against each bucket's base rate
of appearing in a 7-card hand:

| bucket | in slow | base | enrichment |
|---|---|---|---|
| Skateboard | 45.1% | 11.7% | **3.86x** |
| Deconstruction Hammer | 40.0% | 11.7% | 3.43x |
| Dragonfire Blade | 37.3% | 11.7% | 3.20x |
| Shadowspear | 53.4% | 22.1% | 2.41x |
| Sram, Senior Edificer | 77.4% | 39.9% | 1.94x |
| Accorder's Shield (16 cards) | 32.4% | 90.1% | 0.36x |
| Ancient Den (12 lands) | 26.6% | 80.9% | 0.33x |
| **Puresteel Paladin** | **0.1%** | **39.9%** | **0.00x** |

**Puresteel Paladin grants metalcraft equip {0}, and a hand holding it is essentially never slow.**
With free equip the route's orderings collapse; without it the DFS must pay for each link, and the
expensive hands are the ones holding several DISTINCT one-of Equipment and no free-equip enabler.
That is the same *shape* as Fungus's token-doubler-plus-sac-outlet (a combinatorial quantity another
permanent inflates), reached by a different mechanism, and it is what makes a static cost predictor
feasible on this deck should one ever be wanted.

### Still open, and the right mechanism for next time

**`MTG_KEEP_PRUNE_SET` is the "don't re-run the multi-day deck from scratch" lever** and this round
could not use it: it is emitted by `RunKeepMerge` from a RAW sidecar, and a killed run leaves only a
journal (the final raw is written at the end; a fail-out now deliberately skips it). Its consume path
gates on bucket_fp/deck_fp but **deliberately NOT on play_digest or commit**, precisely so a re-run on
a new commit can skip last run's confident mulligans. Making a prune set derivable from a JOURNAL --
or checkpointing a raw mid-run -- would have saved this round's 6 h outright, and is the single
highest-value piece of plumbing this deck still wants.

Also unbuilt: `MTG_KEEP_SUB_CUTOFF_R`'s "decision-preserving" sub-table freeze exists only on the
prune-set EMIT path, not live, so it cannot help the dominant phase today. And `perf` does not work
in this container (`failed to write perf data: Bad address`), so the unit census remains the only
attribution instrument available here -- worth knowing before planning a profiling session.

# 2026-10-11 — LOSSLESS FIRST: the cost model was wrong, and what is actually available

USER RULING, 2026-10-11: *"Let's stop with the lossy changes until we have exhausted the lossless
options. We should be able to figure out a way to make this work without paying 60-80 hours. That is
obviously unacceptable. How high is K? Are there branches we can cut via heuristics? What other
optimizations are available. If we need to return to the lossy options I would rather test them out
on fast decks first to determine what they actually cost."*

This section is the answer, and it starts by retracting the measurement apparatus the previous
section was built on.

## THREE THINGS WERE WRONG WITH `ab_tail.sh`, and every ratio it produced inherits them

1. **IT RAN AT THE WRONG DEPTH.** The arms ran in a `d1` sandbox
   (`value_play.mull_gen_depth = 1`). The committed contract for this deck is **d2/b3**
   (`decks/KittyEquipment/v3-adelbert-gauntlets/KittyEquipment.value.json`, written by the value
   leaf's phase F). Every one of the nine arms was measured one depth below the job being sized.
2. **IT PRICED A PLATEAU, NOT A TAIL.** Its 64 captured rollouts all sit in a **100.0–104.8 s**
   band, mean 102.6 s. Against the full slow population (`n = 11,118`, `p50 = 67.7 s`,
   `p90 = 294 s`, `p99 = 973 s`, `max = 3,405 s`) that band is about **p64**. So it measured
   neither the median nor the tail. This is `memory:bench-n-decides-median-vs-tail` applied to the
   instrument built to avoid exactly that error.
3. **IT WAS LENGTH-BIASED BY CONSTRUCTION.** A list of logged-slow rollouts cannot estimate total
   generation cost, because total cost is (mean over ALL rollouts) × (count). The slow list's mean
   is a conditional mean and says nothing about the product.

The replacement is **`logs/kitty_tail_diag/ab2.sh`**: `MTG_KEEP_REPLAY_SAMPLE=N` draws N size-7
cell-sides *uniformly* over the size-7 work list and runs them through the gen's own `run_one` on
one pooled queue. Uniform-over-cell-sides **is** the floor phase's own distribution (every cell-side
gets `r_floor = 2`), so the sample mean is an unbiased estimator of per-rollout cost with the tail
represented in proportion. The draw is a fixed xorshift from a constant, so every arm replays the
identical rollouts — a **paired** comparison. It also has the prefix property: the first N draws of
an N' > N run are the N-run's draws.

`MTG_KEEP_BENCH=1` stays set and is load-bearing, not an optimisation: it makes the equivalence
cache skip the `play_digest` compare so every arm resolves the SAME 15 buckets and a `w` index means
the same cell on every arm. Checked against the source — it touches only that digest compare and the
bench path (which the replay returns before), so **it does not change `run_one`'s cost**.

**METRIC = sum ms, NOT units**, which inverts this repo's usual rule
(`memory:units-not-ms-on-a-contended-box`). The levers under test are *prices*: raising
`MTG_EQUIP_ROUTE_LATEMAIN_UNITS` raises units-per-link while lowering wall, so units and ms move in
opposite directions by construction. One arm at a time on an otherwise idle box with a fixed thread
count makes ms the honest axis. (A `suite_gate.py --cost` call — which MEASURES, spawning a
regression run — overlapped the first baseline for ~2 min and took load to 43.6 on 32 cores; that
baseline was discarded and the sweep restarted. A contaminated denominator biases every ratio
optimistically.)

## THE COST MODEL WAS OFF BECAUSE `roll7` DOES NOT COUNT EVERY ROLLOUT

`gen_prog.roll7` is documented as "size-7 ROLLOUTS enqueued (1 per kind-0 task)" and reaches exactly
213,492 = 106,746 cell-sides × `r_floor` 2. But the **kind −1 PRECOMPUTE** tasks run rollouts
"deliberately outside every accumulator: no fold_mtx, no in_flight, **no roll7**, no journal", and
the floor additionally speculates up to `cont_lookahead = 4` rollouts past `r0` per cell-side. So
the floor executes **~6 rollouts per cell-side, not 2** — roughly 640k, not 213,492.

Both corrections cancel for the floor's measured wall, so the phase timeline below is unaffected.
What it does kill is any projection of the form "rollouts × cost-per-rollout" built on the counted
number: that overstates cost-per-rollout by ~6–8x. The uniform sampler measures the *true*
per-rollout cost, and **a lever's RATIO transfers to every phase regardless of the count**, which is
why the ratio is the only thing taken from it.

## THE MEASURED PHASE TIMELINE (d1 run, the one whose floor completed)

Read off the monitor lines, which is a measurement rather than the generator's own `total_cells × 40
/ floor_rate` formula:

| phase | wall | rollouts (counted) | note |
|---|---|---|---|
| size-7 floor | 0 → 22,503 s (6.25 h) | 213,492 | `rollsub=0` throughout — all 32 threads on size-7 |
| sub-table floor | → 30,004 s (+2.1 h) | 160,844 | 64,422 fused batches, ~36 rollouts/s |
| **sub-refine wave 1** | 29,552 rollouts in 3,601 s | **801,008 needed** | ⇒ **27.1 h for wave 1 alone** |
| size-7 refine | never reached | — | freeze-pruned |

**Wave 1's 27 h is measured, not modelled**, and it is still the dominant phase. The apparent
"sub-refine rollouts are 2.6–5x dearer than sub-floor rollouts" is explained without any pathology:
the sub FLOOR covers all 64,422 sub-cells including the trivially cheap tiny hands
(`size6=21500 size5=7653 size4=2340 size3=589 size2=114 size1=15`), while the wave's 50,063 marked
cells concentrate in **size 6**, which costs nearly what size 7 does. The slow log's size mix agrees
(`size7=7802 size6=2802 size5=289 size4=22 size3=2 size2=2`).

**The box is NOT starved during the waves.** 32 workers × 16-rollout tasks at the observed
per-rollout cost predicts the observed 7–14 rollouts/s, so there is no scheduling win hiding in the
wave barrier. The one genuinely stranded window is the **serial marking pass between waves**
(`compute_sub_wave_tasks` enumerates subcompositions for every size-7 hand at every mull level, on
the producer thread — the code's own flip-sweep note says repeating it per wave "would stall feeding
for its duration on every wave"). That pass is read-only over independent size-7 hands, so it is
embarrassingly parallel and overlapping it is pure scheduling — the one piece of in-generator
plumbing worth building.

## THE TAIL'S MECHANISM: COST IS "GAMES THAT DO NOT WIN"

The slow size-6 hands all have one shape:

```
Remote Farm x1; Shadowspear x1; Skateboard x1; Sram, Senior Edificer x3
Remote Farm x2; Shadowspear x1; Sram, Senior Edificer x3
Remote Farm x1; Shadowspear x2; Sigarda's Aid x1; Skateboard x1; Sram x1
```

Land-light, Sram-heavy, **no Puresteel Paladin**. These hands cannot go off, so they play all
`max_turns = 8` turns and pay a full search at every decision, while a hand that goes off wins on
turn 3 and costs almost nothing. That is the same fact the enrichment table already reported from
the other end (Puresteel **0.00x**, Sram **1.94x**) — **cost is driven by turns played, and turns
played is driven by failure to win.** Any cut that helps must either make a non-winning game cheaper
per turn, or stop paying full price for a turn that cannot produce a win.

## THE LOSSLESS INVENTORY, with status

**`MTG_EQUIP_ROUTE_LATEMAIN_UNITS` — built, default-inert (1), NEVER MEASURED. The best candidate.**
`equip_route` is the largest single unit site on this deck (**32.6%** of units on the old tail list,
**26.3%** on the uniform d2 sample). This lever *prices* DFS links on frames after the turn's first
main instead of *refusing* them. Its own comment establishes soundness **by construction, not by
sample**: at an unlimited root `ChargeDecisionUnits` returns on `budget == nullptr` and the per-link
cut test requires `budget != nullptr`, so the price is never read and the walk is byte-identical. It
therefore satisfies "a budget may RE-PRICE work, it may not make a line UNFINDABLE" by construction,
which its gate-shaped twin `MTG_EQUIP_ROUTE_TOPMAIN` does not. TOPMAIN measured **0.879x at b20,
stacking to 0.714x**, and **−25 game-turns** (cheaper *and* better play). This is that saving with
the completeness objection removed.

**`MTG_METALCRAFT_EQUIP_HOIST` — measured −25.3% units on v1, default OFF. A USER decision.**
It extends the auto-equip collapse across the same-turn metalcraft flip: the collapse's freeness
test reads `EquipCostGenericNow` at *candidate* time, so on the go-off turn every equip is stamped
at its PRINTED cost, the collapse declines, and the odometer powersets a take/skip family every
member of which will cost {0} when paid. **Not a pure identity fold** — a hoisted equip rides every
subset without contributing to the mana term — so it is reported, never adopted by me. It is worth
measuring here because the v1 harm it was parked for came from **double strike** (Kor Duelist), it
disarms itself via `FrameHasDoubleStrikeSource`, and **v3 has no double-strike source**.

**The uncharged subset walk — `SolveChargeWeightOptIn`.** The census shows
`bf_scored search_subsets = 1,033,256,261` while `units.greedy_walk` **does not appear at all**
(sites with zero units are not printed). A billion scored subsets charging **zero** budget units is
the literal subject of this document's title. `MTG_SOLVE_CHARGE` is a per-deck opt-in
(`SnowProvider = 16`; Equipment has **not** opted in) and on Snow the uncharged walk let "63.6% of
decisions run past their budget and 36.4% past 10x it". Charging it is not a precision cut — it
makes the budget bound what it was calibrated on — and it is the sanctioned price shape (inert at
unlimited budget). Earlier readings on the wrong harness: `sc16` 0.986x, `sc4` 0.903x but with 2
labels moved and a 25% worse worst case. Re-measure on `ab2.sh`.

**`WinlessDevelop` — LOSSLESS, deck-agnostic, and WALLED OFF from budgeted play.** The
reachable-state closure collapses plans that reach the same end-of-turn state, keyed on the
engine's own order-exact 128-bit `BuildDedupKey`: "two lines sharing this key have identical
futures, so searching the second is a re-run of the first: **LOSSLESS up to a 128-bit collision**",
and "the mechanism here is deck-agnostic ... **any archetype that can prove a turn winless gets
this for free**". Two things stand between it and this deck:
  * **`EquipmentProvider` has no `ProvenWinlessThisTurn`.** Only Snow, Fungus and
    EldraziDisplacerFlicker implement the proof. For an equipment deck the proof is tractable and
    auditable (bound this turn's maximum attack damage given attackers, mana and attachable
    equipment) — but a false positive "silently converts a win into a loss", so it is a careful
    build, not a quick one.
  * **The scope latch is structural.** `WinlessDevelopActive` requires `UnbudgetedWorkScopeActive`,
    raised only where `m_budget_ms <= 0` (`AIEngine.cpp:2203`), and the keep rollout runs at
    budget 3. The gate is deliberate: skipping a rollout skips its `ConsumeAt` calls, so the
    deterministic unit count moves and with it every budget-derived decision and the regression
    fingerprint. "Budgeted play must therefore be UNREACHABLE, not 'measured unchanged'."
    Opening it is a play-changing engine project with a GT rebaseline attached.

**The bare winless certificate is NOT available, and that is correct.** `ProvenWinlessThisTurn`
applied at a horizon edge returns `{max_turns+1, {}}` — no win and **no actions**. That is exact for
an *earliest-win query* but a keep rollout must *develop* across turns to win at all, so a bare pass
corrupts it in exactly the measured way: "Snow seed 8014/gi 6 wins on turn 5 with the certificate
off and **turn 6 with it on**". Closed.

## K = 15, AND IT IS ALREADY THE PRODUCT OF AN EQUIVALENCE FOLD

K is **15**, giving `size7 = 53,373` compositions → 106,746 cell-sides, and 85,584 cells /
**171,168 cell-sides** across sizes 1–7. Those 15 classes are not a guess: bucket discovery probed
400 games at a 1% threshold and folded the deck's **19 distinct cards** down to 15, including all
**16 shields into one bucket**:

| bucket | n | members |
|---|---|---|
| 0 | 16 | Accorder's Shield, Cathar's Shield, Kite Shield, Spidersilk Net |
| 1 | 12 | Ancient Den, Plains |
| 3,4,8,9,10,14 | 4 each | Bone Saw; Colossus Hammer; Golem-Skin Gauntlets; Puresteel Paladin; Remote Farm; Sram |
| 11 | 2 | Shadowspear |
| 2,5,6,7,12,13 | 1 each | Adelbert Steiner; Deconstruction Hammer; Dragonfire Blade; Dwalin; Sigarda's Aid; Skateboard |

So **there is no unmerged equivalence left to harvest.** The obvious card-level dominance a human
notices first — Cathar's Shield ({0} cast, equip {1}, +0/+3) strictly dominating Kite Shield ({1}
cast, equip {2}, +0/+3) — is already inside bucket 0 and already collapsed. Lowering K further means
merging classes the prober found *not* equivalent, which is a coverage cut and a **user ruling**
(`memory:bucket-ruling-is-user-only`); it is not a lossless lever.

## A PROBLEM WITH THE ABANDON POLICY AS ARMED

The user's 2026-10-10 ruling (leave the rollout out; fail out somewhere in 10–50) assumed
abandonment is *exceptional*. At the wall this agent armed (`MTG_KEEP_ROLLOUT_WALL_SEC = 1200`) it
is not: **0.63% of the slow population exceeds 1200 s — 70 rollouts in this sample alone.** With
`MTG_KEEP_ABANDON_FAIL_AT` at 10 (code default) or even 50 (its cap), a full run would fail out
almost immediately and throw away the floor. The ruling is sound; the wall was set too low for it.
The wall is the agent's to set such that the user's count means what they intended, and
`p99 = 973 s` / `max = 3,405 s` say that threshold is **well above 1200 s**.

## TESTING THE LOSSY DIALS ON FAST DECKS, as the user asked

The user's instruction — *"I would rather test them out on fast decks first to determine what they
actually cost"* — is the right experiment and it is cheap, because what a lossy dial costs is a
property of the **table**, not of wall clock. Twenty decks already ship a
`keepmodel.exhaustive.profile.json`, so for a fast one (Melira, CritterLifegain, Pirates) a
regeneration at `flip_eps` 0.10/0.20, or at a lowered cap R, can be diffed against the SHIPPED table
label-by-label and value-by-value. That converts "flip_eps 0.10 accepts more risk of picking the
wrong bottoming line" from a sentence into a measured count of **how many cells changed label and by
how much value** — which is exactly what the user needs in order to rule on the dial for kitty, and
it costs hours on a fast deck instead of days on this one.

## 2026-10-11 RESULTS — LATEMAIN_UNITS IS REFUTED, and the right instrument for the lossy dials

### `MTG_EQUIP_ROUTE_LATEMAIN_UNITS` measured SLOWER. The "best candidate" call was wrong.

Paired, unbiased, N=20,000 uniform size-7 draws at d2/b3 on PGO, one arm at a time on an idle box
(`ab2.sh`):

| arm | sum core-ms | vs base | units_total |
|---|---|---|---|
| base | 44,165,907 | — | 168,023,829 |
| `LATEMAIN_UNITS=4` | 45,518,539 | **1.031x (SLOWER)** | 172,076,523 |

3.1% slower, which clears the ~1% noise floor. **The mechanism is already documented one function
up, and it should have been predicted rather than measured:** the equip route is a lethal DETECTOR
whose *success* fires the go-off cut and collapses the frame to one plan. `NodeUnits`' own adoption
note records exactly this for price 8 — "it runs out of links MID-detection, so it produces no
certificate, the cut does not fire, and the decision pays the FULL enumeration -- the worst case
available" (gi=371 t3: 70,077 → 18,006 units when the price went 8 → 4). Raising the *later-main*
price does the same thing to later-main frames. Pricing a detector higher starves the detection that
was going to pay for itself.

So "equip_route is 26-33% of units, therefore pricing it will help" was a bad inference: a big unit
share does not mean the work is wasted. On this deck the route's units are the cheapest work in the
run, because each success deletes an enumeration. Note also that `units_total` is not a clean work
axis for a re-pricing arm — the price multiplies units-per-link, so a +2.4% units reading alongside
a +3.1% ms reading means FEWER links each charged MORE. Only ms answers the question.

This does not touch `LATEMAIN_UNITS`' soundness argument (it remains inert at an unlimited root);
it simply does not buy anything here. `lm16`/`lm64` are expected to be worse for the same reason.

### THE LOSSY DIALS DO NOT NEED A REGENERATION — there is a validated offline simulator

`RunAdaptiveBottomRegretSim` (ExhaustiveKeep.cpp ~5411-5755, `MTG_KEEP_SIM_ADAPTIVE_BOTTOM`) prices
the adaptive-bottoming dials **offline from an existing full-bottom raw, with no rollouts**, and
reports **regret in TURNS with a standard error** alongside the rollout saving. Its own provenance:

> VALIDATION: slivers full-bottom raw -> +~0.006 (in-game was +0.0057)

i.e. it reproduces a real in-game A/B to three significant figures. Knobs: `MTG_KEEP_SIM_FLIP_EPS`
(the near-tie gate), **`MTG_KEEP_SIM_CAP`** (model a cheaper gen whose sub-tables only reach R=k —
the cap-R dial), `MTG_KEEP_SIM_FLOOR`, `MTG_KEEP_SIM_TRIALS`, `MTG_KEEP_SIM_SEED`. Invocation is the
merge path:

```
MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS=<raw.gz> MTG_KEEP_NO_WRITE=1 \
MTG_KEEP_SIM_ADAPTIVE_BOTTOM=1 MTG_KEEP_SIM_FLIP_EPS=<eps> MTG_KEEP_SIM_CAP=<R> \
  mtg-analyze <deck>.txt --cards-json src/cards/data/cards.json
```

It returns without writing a profile, and `MTG_KEEP_NO_WRITE=1` blanks both output paths as well, so
it cannot go near a journal. Driver: `logs/kitty_tail_diag/sim_lossy_dials.sh`.

### WHICH DECK, and why the obvious reason was the wrong one

USER: *"Probably a trusted deck (Slivers, etc.) would be the fastest for comparison"* and, crucially,
*"Though R being too low would be troublesome for the adaptive change, since it would reduce the
impact."* The R objection is correct and binding: a wave's batch is `min(r_batch 16, r_max - c)`, so
a low-R base leaves no room to refine and understates the dial.

But **slivers is fast in PLAY, not in GENERATION** — it runs at d5/b10, which makes it the third
most expensive table in the repo to rebuild. Relative regeneration cost (`cells x R x depth x
budget_ms`):

| deck | K | effective_R | cells | dep/bms | rel-cost |
|---|---|---|---|---|---|
| burn | 10 | 60 | 10,945 | 1/3 | **2.0M** |
| CritterLifegain | 13 | 40 | 28,997 | 1/3 | 3.5M |
| Giants / Fluctuator / Fungus | 13-14 | 30-40 | 36-55k | 1/3 | 4.3-5.0M |
| *kitty v3 target* | *15* | *30* | *85,584* | *2/3* | *15.4M* |
| slivers_vial | 10 | **100** | **7,758** | 5/10 | 38.8M |

**And then it does not matter, because nothing is regenerated.** For the OFFLINE sim the selection
criteria invert, and slivers wins all three:
  * it is the deck the simulator was **validated against**;
  * it has the **full-bottom raw** the sim requires (it refuses without per-cell `sumsq`);
  * its **R=100 is the highest in the repo** (burn 60, critter 40, kitty-`fast` 30), so every lower
    cap is modelled DOWNWARD from truth instead of extrapolated upward from a cramped base — which
    is the user's R objection answered by construction rather than by argument.

burn (R=60) and CritterLifegain (R=40) are the fallbacks if slivers' shipped raw turns out to be
adaptive rather than full-bottom, and both are in the suite with raw sidecars present.

The queued sweep prices `flip_eps` ∈ {0.02, 0.05, 0.10, 0.20} **at kitty's own cap R=30** (the
directly transferable reading), cap R ∈ {raw 100, 40, 20, 16} at the default gate, and the exact
**(eps 0.20, cap R 16)** corner this agent launched without approval on 2026-10-10.

---

# 2026-10-11, PART 2 — THE COST IS A CEILING, NOT A PRICE

The user, after the first round came back: *"There should also be lossless optimizations since we
have a high per-rollout cost. Not just those lossy ones. K=15 is quite reasonable. So we should not
have a super slow generation."* And then, naming the suspect: *"I strongly suspect the problems are
related to wide board branches, so we need to prune them."*

Both readings are right, and the second one is now measured rather than suspected.

## 1. The LATEMAIN family is refuted on every axis at once, monotonically

`MTG_EQUIP_ROUTE_LATEMAIN_UNITS`, N=20,000 paired uniform sample of size-7 cell-sides at the
shipped d2/b3, one arm at a time on an otherwise idle 32-core box:

| arm | core_s | vs base | max rollout | units | `sum_wt` | win turns moved |
|---|---|---|---|---|---|---|
| base | 44,155.9 | 1.000x | 1,071.9 s | 168,023,829 | 108,967 | — |
| lm4 | 45,508.6 | 1.031x | 1,204.2 s | 172,076,523 | 109,045 | 92 |
| lm16 | 45,021.9 | 1.020x | 1,209.9 s | 179,159,152 | 109,213 | 269 |
| lm64 | 46,915.6 | 1.062x | 1,203.8 s | 188,434,179 | 109,379 | 470 |

Slower, worse tail, more units, **and the deck wins later** — and those are one fact, not four. The
equip route is a lethal DETECTOR whose success fires the go-off cut and collapses the frame. Pricing
its links starves detection mid-flight, no certificate fires, and the decision pays the FULL
enumeration. Missed kills then *are* the cost, because a hand that goes off wins turn 3 for nothing
while one that does not pays all `max_turns = 8` turns at full price.

**The lesson, stated so it is not relearned: a large unit share does not mean the work is wasted.**
`equip_route` is 26–33% of units *because* each success deletes an enumeration. This was predictable
from the `NodeUnits` price-8 note one function up and should not have needed measuring.

## 2. CORRECTED — 63% of units sit in BUDGET-SATURATED decisions, not in a work-X ceiling

**The first version of this section was WRONG and is withdrawn in full.** It claimed the 63% was
`MTG_DECISION_WORK_X` being hit. It is not, and §1 of this very document already contained the
refutation, which is the part worth remembering: *"a ceiling so high nothing trips... Nominal budget
**2,700 units** (3 ms x 900/virtual-ms)"*, with the median decision at **0.5x budget**.

The arithmetic, which takes one line and was never done:

| | |
|---|---|
| `SearchBudget::NODES_PER_VIRTUAL_MS` | 900 |
| v3 generation budget (`mull_gen_budget_ms`) | 3 ms ⇒ `budget->Limit()` = **2,700 units** |
| `MTG_DECISION_WORK_X` (pinned by `mullgen.sh`) | 1000 ⇒ ceiling = `2700 * 1000` = **2,700,000 units** |
| measured d1 spend | **2,346 units/decision** = **0.087% of that ceiling** |

Three orders of magnitude of headroom at the mean. The ceiling cannot be what d1 is hitting.

### What d1 actually is

The same table, read correctly — and with `mch_lm16`, whose arm was never collected before:

| arm | d1 n | d1 units | **d1 u/dec** | d2 n | d2 units | **d2 u/dec** | units total | **core_s** |
|---|---|---|---|---|---|---|---|---|
| base | 44,981 | 105,532,422 | 2,346 | 113,126 | 36,111,217 | 319 | 168,023,829 | 44,155.9 |
| lm4 | 45,308 | 105,742,073 | 2,333 | 113,043 | 39,852,592 | 352 | 172,076,523 | 45,508.6 |
| lm16 | 45,817 | 105,447,607 | 2,301 | 112,947 | 47,181,839 | 417 | 179,159,152 | 45,021.9 |
| lm64 | 46,407 | 104,718,527 | 2,256 | 112,808 | 57,082,431 | 506 | 188,434,179 | 46,915.6 |
| **mch** | **40,858** | 89,892,729 | 2,200 | **119,202** | 31,135,520 | 261 | 146,761,558 | **15,894.2** |
| mch_lm16 | 42,019 | 91,701,318 | 2,182 | 118,853 | 40,961,793 | 344 | — | — |

**2,346 is 86.9% of the 2,700-unit budget.** That is the mechanism: a decision that cannot *complete*
a depth-2 pass spends essentially its whole budget trying, then iterative deepening commits depth 1.
A decision that *can* complete depth 2 does so in **319 units — 11.8% of budget**. The population is
bimodal, and the modes are "the frame fit" and "the frame did not".

So d1's per-decision cost is governed by the BUDGET, not by the menu width, and **narrowing a frame
cannot make a d1 decision cheaper** — it will spend its budget either way, just getting further into
the depth-2 pass before it stops. This is the opposite of what the withdrawn version concluded.

### The consequence, and it is the actionable one

**A width prune's only route to a cost win is to move decisions from d1-commit to d2-commit** — 2,346
units becomes 319, a factor of **7.3**. Not "narrow the frame a bit"; *narrow it until depth 2 fits*.

The target is therefore quantifiable, which the withdrawn version made impossible:

| wanted total cost | required d1 u/dec | cut needed |
|---|---|---|
| 0.75x | 1,999 | 15% |
| 0.60x | 1,438 | 39% |
| **0.50x** | **1,065** | **55%** |

### What the four pricing arms really did

Read as a d1/d2 *migration* the LATEMAIN family is refuted even more sharply than §1 says. Pricing
the route pushed decisions the WRONG WAY — d1 n 44,981 → 46,407 (**+1,426 decisions got worse
answers**) — while inflating the d2 decisions that survived by **+59%** (319 → 506 u/dec). Both
directions are losses, which is why every one of the four axes moved against it monotonically.

### And `mch` did the right thing by the wrong means

`mch` is the only arm that moved the split the GOOD way: d1 n **−9.2%** (44,981 → 40,858) and d2 n
**+5.4%** — about **4,100 decisions promoted from a budget-saturated depth-1 commit to a completed
depth-2 one**. That is the exact mechanism this section says is the only one available, and it is why
the arm was fast. Its 512 lost games were never a property of the mechanism; they came from the
hoist's freeness promise being an existential used as a universal (§5), which is repairable and is
repaired by `MTG_METALCRAFT_HOIST_EXACT`.

### A SECOND finding, and it changes which axis to judge on

`mch` is **0.874x on `units_total` and 0.360x on wall**. Wall fell **2.4x further than charged
units**. A large part of the real cost is therefore **not unit-charged at all** — which is the
uncharged greedy walk this document is named after, reached from a new direction.

**So `units_total` systematically UNDERSTATES a prune on this deck, and `ms` is the honest axis** —
which is what `ab2.sh` already argues for its own metric choice, on different grounds. A lever that
removes 1.6% of units may be removing several times that in wall. Conversely, do not convert a wall
win into a units claim or vice versa; quote both, as the table above now does.

`MTG_DECISION_WORK_X` is still worth what §1 measured it at — **0.172% of decisions tripped, 24.6%
of units removed, 1.33x, play digest UNCHANGED** — but that is a tail clip on the extreme, not the
explanation for the bulk. The screen's `wx250` / `wx2000` arms test it directly on this sample; if
all three bracket arms land together, X never binds here at all. X is a precision dial either way,
so it is a USER ruling and is reported, never adopted.

## 3. METALCRAFT_EQUIP_HOIST is 2.78x faster, and its quality cost is a SCORING error

| | base | mch |
|---|---|---|
| core_s | 44,155.9 | **15,894.2 (0.360x)** |
| max rollout | 1,071.9 s | **159.6 s** |
| units | 168,023,829 | 146,761,558 |
| d1 n / commit units | 44,981 / 105,532,422 | **40,858 / 89,892,729** |
| d2 n | 113,126 | **119,202** |
| `sum_wt` | 108,967 | **110,274 (+1,307 turns)** |
| win turns moved | — | 1,463 |

It moved ~4,100 decisions OFF the ceiling into completed searches — d1 n −9.2%, d2 n +5.4% — so the
search got *better*, and the labels still got worse by 0.065 turns/rollout. **That rules out
truncation as the cause** and confirms the mechanism its own slot documents: *"a hoisted equip rides
every subset without contributing to the mana term"*. The loss is confined to the plan's SCORE.

The tail collapse (1,072 s → 160 s) also dissolves the abandon-wall problem on its own: at a 160 s
maximum, no wall in the user's 10–50 fail-out range is near firing.

**This is a USER decision and is not adopted here** — it is not a pure identity fold.

### Why the obvious lossless fix does not exist, and what an exact one would require

`MetalcraftWillFreeEquips(state, cands)` counts board artifacts **plus every cast candidate in the
frame**, so it returns true whenever the frame *could* reach the threshold; the hoist then applies
to subsets casting none of those artifacts. The threshold comes from exactly one card in the v3
main 60 — **Puresteel Paladin, 3 artifacts**.

* **"Require metalcraft already live on the battlefield" gains nothing.** If the board already has
  3 artifacts and a Puresteel, `EquipCostGenericNow` already returns 0, `cost.ManaValue() == 0`, and
  the *plain* auto-equip collapse already fires. The hoist's entire value is the turn whose own
  casts FLIP metalcraft.
* **A per-subset threshold test is cheap** (board artifacts + artifacts cast in `sel`, accumulated
  while `sel` is built) **but is not sufficient on its own**: force-including a *paid* equip is not
  dominance, because it deletes the "don't equip" line from that subset. The original collapse
  excludes costed equips for exactly this reason.
* So an exact version must make the hoisted equip's inclusion **depend** on the artifact casts that
  flip the threshold, rather than ride every subset. `BuildEquipPieceDeps` already carries that
  shape — the free-cast hoist's note records it as *"the first collapse to erase a CAST group"*,
  stamping a dependent equip `{required, groups=0}`.

That is a real project touching odometer correctness, and it is deliberately NOT started before the
simple lossless prunes are measured: if the width prunes capture a good share of the 2.78x with no
scoring error at all, the complex exact hoist is unnecessary.

## 4. The width prunes under screen, and why the host axis comes first

The census says the equipment menu carries the work — `equip_route` 31.4% + `la_cand` 18.8% +
`greedy_fallback` 18.6% = **69% of units**, all three scaling with the WIDTH of that menu rather
than with any one price. The slow population says why the board gets wide: those hands are
land-light, Sram-heavy and hold no Puresteel in the opener, so they never go off, accumulate
equipment and play all 8 turns against an ever-widening menu. A wide board is both the cause of the
branching and the signature of a game that does not win.

The menu is (equipment × hosts) PAIRS and the odometer powersets take/skip over them, so **the host
width is an exponent on the equip axis, not a term beside it.** Collapsing it is the user's own
standing doctrine (*"There is no double-strike in the deck, so we can just choose one creature and
put all equipment on it"*), and for v3 the precondition is VERIFIED rather than assumed: Kor
Duelist — the only `double_strike_while_equipped` card near this list — is **sideboard only**, so
the main 60 has no double-strike source. Concentration is not merely neutral here either:
Golem-Skin Gauntlets grants +1/+0 **per Equipment attached to its own host** and the deck runs four,
so splitting equipment across hosts strictly REDUCES total power. The width-2 default was adopted on
a measured v1 case (gi=39, *"Greaves -> the Balan cast this same subset"*, which a width-1 haste
ranking never offers) and v3 plays neither Lightning Greaves nor Balan, so that justification does
not transfer.

Screened arms: `MTG_EQUIP_HOST_WIDTH=1`, `MTG_EQUIP_RIDER_WIDTH=1`, both; `MTG_FREE_CAST_HOIST`
(built, argued exactly mana-neutral, **never measured** — v3 holds twenty {0}-cost Equipment and
hand casts group by hand SLOT, so a mid-game hand powersets every one independently);
`MTG_EQUIP_COPY_XNAME` (built, never measured); and the three built today.

## 5. The two collapses built today, and the gap they close

**The search keys cards by NAME; the keep table folds them by BEHAVIOUR.** Cathar's Shield and
Accorder's Shield are byte-identical in `cards.json` — both `{0}`, both
`{is_equipment, equip_cost_generic:3, equip_tough_bonus:3, equip_grants_vigilance}`, same
types/subtypes/keywords — and v3 plays four of each. The deck's own equivalence discovery (400
probes, 1% threshold) puts **Accorder's + Cathar's + Kite Shield + Spidersilk Net in ONE class**. So
the mulligan generator folds those four names on measured evidence and then pays for a search that
branches four ways over them on every frame of every rollout it drives.

`CardDefinition::behaviour_identity` already existed with the right failure direction (digests the
whole entry minus `name` and `oracle_text`; an irrelevant key only *declines* a fold) but had ONE
consumer, `BuildFungibleEquipClasses` behind default-OFF `MTG_EQUIP_COPY_XNAME`.

* **`MTG_FOLD_XNAME`** routes it into `HandCardContentHash` (the hand-cast fold, default ON, −11.5%
  greedy subsets across all 40 suite decks; also the permanent and act-source halves) and into the
  route's `PurchaseKey`. Shared reader in `EngineFlags.h`. `ActionFoldSig` had to change too, and
  that is not cosmetic: the new tag puts the two cards in one class and condition 1 would then DROP
  it for differing name/`def` pointers, so the fold would silently stop firing rather than merge.
* **`MTG_EQUIP_EQCOST_DOMINANCE`** implements the user's rule — *"Same equip costs are the easiest
  to prioritize. The ones that do more can be always taken in that case... that is dominant just
  because the extra damage cannot be overridden by mana efficiency."* Bounded by the user's own
  counter-example: across UNEQUAL equip cost it is a real multi-turn allocation choice (*"we might
  have enough mana this turn for the shield and be able to equip the spidersilk next turn alongside
  other things. Mana efficiency takes priority."*) so nothing is pruned there. **Componentwise**,
  never through `rider_delta`'s weighted scalar — at a fixed host the double-strike flip and
  Kemba's upkeep token both cancel, so the order reduces to power / Jitte charges / lifelink.
* **`MTG_EQUIP_POWER_FIRST`** is the user's damage-per-mana ordering (*"prioritize paying equip
  costs for Gauntlets... hammer is so expensive to equip that the gauntlets will generally be what
  you choose"*). An ordering, never a prune, so it is inert at unlimited budget by construction.

Applied to the v3 equipment lattice, the user's equal-cost rule prunes this much:

| effective equip | candidates | verdict |
|---|---|---|
| 1 | Bone Saw +1/+0, Deconstruction Hammer +1/+1, Skateboard +1/+0 haste | Saw ≡ Dec. Hammer; Skateboard kept (haste is live) |
| 2 | **Gauntlets** (+1 per attached Equipment), Shadowspear +1/+1, Spidersilk Net +0/+2 | **Spidersilk pruned** — both others add power at the same price |
| 3 | **Dragonfire Blade** +2/+2, Cathar's / Accorder's / Kite +0/+3 | **all three shields pruned** when a Blade is available |
| 8 | Colossus Hammer +10/+10 | alone, no dominance applies |

Note this is NOT a repeat of the REFUTED `MTG_ZERO_POWER_EQUIP_FREE_ONLY`, which asserted the
shields are POOR — a projection whose exception was load-bearing (with three Gauntlets out, every
further Equipment is +3 power). The new rule asserts only that a strictly better equip at the SAME
PRICE is preferable, which has no exception.
