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
