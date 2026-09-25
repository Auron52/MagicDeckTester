# The slow-rollout tail, and the greedy walk nothing ever charged

Status: **OPEN — deferred 2026-09-22 by the user, deliberately.** The diagnosis below is complete and
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
2. `MTG_FUNGUS_SHRINK_SAC_M2` — measured above, awaiting the user's adoption call.
3. **Closed, do not re-open:** enumeration-side exact dedup (1.0007x after the fingerprint fix);
   cache layout (0.65%, landed); the budget ceiling (1.33x, landed); `SweepDeadFadeTokens` batching
   (refuted — `OnCreatureDies` runs between erases).
