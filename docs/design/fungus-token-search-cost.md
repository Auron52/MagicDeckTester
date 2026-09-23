# Fungus: pathological per-game search cost on token boards

**Status: ROOT-CAUSED AND PARTLY FIXED (2026-09-17).** The first diagnosis below (board-size
scaling in `GameState` deep copies, remedy = fuse fungible Saproling tokens) was **WRONG**, and it
is left in place because the reasoning that produced it was sound and someone will otherwise
re-derive it. A `perf` profile — see **The actual cause**, further down — showed the deck was
spending **45% of a slow game counting Dragons it does not play**. Fixing that is a
one-signature change worth a measured **1.43x**. Read that section before acting on anything above
it.

**Found:** 2026-09-17, during the Fungus deck's Stage 5c2 `leaf_tiebreak_check` run. Surfaced by
the batch runner's own `SLOW-GAME` reporting, which is exactly the signal CLAUDE.md says to watch.

**Correctness is NOT affected.** Every Fungus gate is green: coverage clean, `nonconv` 0,
`fd-diverge` 0, claude-play sweep 16/16 with all flags resolved, smoke 80/0, regression 108/0,
`verify_deck.py` GATE PASS. This is purely a cost problem.

---

## The measurement

From one pooled 24,000-game run (12 seed blocks × 1000 games × 2 arms, `[batch] heartbeat` at
24/24 throughout, so this is not a scheduling artefact):

| metric | value |
|---|---|
| games over 30 s | **526** of 24,000 (~2.2%) |
| median slow game | 53.8 s |
| p90 | 195.8 s |
| **worst single game** | **1,446 s (24 min)** |
| total wall time inside slow games | **14.6 h** |

Slow games concentrate at **win turns 6-7** (444 of 526) and hit the `base` and `leaf` arms about
equally — so it is the deck's board, not the tie-break lever under test. The same game indices go
slow in both arms, which is itself confirmation that it is board-driven rather than lever-driven.

## Where the cost is

Counter-profiling build (`cmake -DMTG_PROFILE=ON -DCMAKE_BUILD_TYPE=Release`), same deck, one slow
game vs one fast game:

| | fast game (seed 9101 gi0, wins T5) | slow game (seed 1600607 gi607, wins T6) | ratio |
|---|---|---|---|
| search nodes | 12,467 | 968,430 | **78×** |
| wall | 74 ms | 333,909 ms | **4,483×** |
| **ms per node** | 0.006 | 0.345 | **58×** |
| GameState deep copies | 6,722 | 616,923 | 92× |
| decisions | 5 | 154 | 31× |
| TT hit rate | 9.5% | 7.5% | — |
| enum-memo hit rate | 0% (304 misses) | 6% (21,825 misses) | — |

**Both factors explode, and the per-node one is the surprising half.** A 78× node count on a longer
game is unremarkable; a **58× cost per node** is not. That is the signature of per-operation
board-size scaling — `GameState` deep copies and battlefield walks — rather than a plan explosion.

**It is NOT a plan explosion.** `MTG_BRANCH_STATS` on the slow game: 28,159 enumerations,
442,676 plans, **avg 15.7 plans per enumeration, max 207**. That is a small, healthy plan space.
`MTG_ENUM_HIWATER_KB=2048` printed nothing, which by that diagnostic's own contract means the cost
is *accumulation across the recursion*, not one giant enumeration.

**It is NOT memory.** Peak RSS on the slow game was 128 MB.

**It is NOT the real board.** At d0 the same game's battlefield tops out at **15 permanents**. The
big boards exist only inside *rollouts*, where the search explores Doubling Season / Mycoloth /
spore token-engine lines that real play never reaches. (The repo's standing lesson applies:
*rollout, not play, is the denominator*.)

This is the same class as the recorded clue-fusion incident — *"token floods, not units, ate the
wall"*, 67-69 permanent rollout boards, 807 s → 49 s — and the work-unit counters under-count it
there too.

## What was tried and REJECTED

**`MTG_BP_SEARCH=0` (disable mid-turn breakpoint re-solving).** On the single worst game it looked
excellent: **334 s → 178 s, a 1.88× speedup with the win turn unchanged** (6.0 either way).

**It does not generalise.** Over 100 games at play settings (d5, b200, seed 8801):

| arm | avg turns | wall |
|---|---|---|
| shipped (breakpoints on) | **5.6000** | 127.19 s |
| `MTG_BP_SEARCH=0` | 5.6100 | 125.70 s |

1.2% faster and a slightly *worse* average. **Rejected.** Recorded because the n=1 result was
genuinely persuasive and someone will re-derive it otherwise — this is the
"one run is not evidence" lesson in its natural habitat.

Note also that disabling breakpoints did **not** reduce the decision count (still 154), so the 31×
decision inflation on slow games has a different cause and is still unexplained. That is the
loose thread most worth pulling next.

## The actual cause (2026-09-17, `perf`)

**FIRST: `perf` WORKS IN THIS CONTAINER.** The earlier note that it "failed with Bad address" was
right about the symptom and wrong about the cause: it is the **workspace mount**, not the kernel.
Write the sample file to `/tmp` and use a software event and it just works — `gdb` is still blocked
by `ptrace_scope=1`, but nothing needed it.

```
perf record -e cpu-clock -F 499 --no-buildid -o /tmp/slow.perf.data -- \
    ./build/Profile/mtg decks/Fungus/Fungus.cod --profile decks/Fungus/Fungus.profile.json \
    --seed 1600607 --game-index 607 --games 1 --threads 1
perf report -i /tmp/slow.perf.data --stdio --no-children          # flat
# call graph: add  -g --call-graph dwarf,16384   (the Profile config is -O3 -g, no frame pointers)
```

Flat profile of the slow game (`--seed 1600607 --game-index 607`, 168K samples):

| symbol | self |
|---|---|
| `FireEtbWatchers` | 16.93% |
| `CardDatabase::LookupCached` | 14.75% |
| `CardHasSubtype` | 12.63% |
| `FireCreatureEnterWatchers` | 12.08% |
| `std::string::string(char const*, allocator const&)` | 11.86% |
| `__memmove_avx_unaligned_erms` | 6.38% |
| `__strlen_avx2` | 6.32% |
| `std::string::_M_dispose` | 3.09% |
| `RefreshDevotionCreatures` | 2.15% |

And with call graphs, the inclusive number that explains all of it:

```
SimulateEndAndStartNextTurn -> CreateToken -> CreateTokenOnce -> FireEtbWatchers   89.94%
   └─ CountControlledDragons                                                       45.14%
```

**A mono-green Fungus deck spent 45% of its search counting Dragons.** `FireEtbWatchers` calls
`CountControlledDragons` on EVERY permanent that enters the battlefield, unconditionally, before it
checks whether any permanent even has `dragon_ping_on_enter`. That function was:

```cpp
if (p.controller_index == controller && CardHasSubtype(p.card, "Dragon")) { ++n; }
```

and `CardHasSubtype` took `const std::string&`, so the **string literal constructed a heap
temporary per permanent per ETB** — malloc + strlen + copy + free, to answer a question about at
most four interned uint16 ids. Cost is O(enters x battlefield), which on a deck that makes dozens
of tokens a turn onto a 298-permanent rollout board is quadratic.

**This is what the "58x cost per node" actually was.** It is not `GameState` deep-copy scaling.
Note why no gate caught it: the answer is identical either way, so play is byte-identical and every
correctness test passes. Only a profile can see it.

### The fix, and what it bought

`CardHasSubtype` now takes `std::string_view` (no allocation; fixes all ~61 call sites at once,
since `const std::string&` converts implicitly), and `CountControlledDragons` compares a cached
interned id via the new `CardHasSubtypeId`.

Measured A/B, same game, single-threaded, idle box:

| arm | wall | win turn |
|---|---|---|
| before | 311.90 s | 6.0000 |
| after | **218.72 s** | 6.0000 |

**1.43x, with play byte-identical** (smoke digests unchanged).

### The rollout board census (`MTG_BOARD_CENSUS`)

The "missing measurement" this doc asked for now exists, in `src/ai/TurnSolver.cpp`
(`namespace boardcensus`), sampling the battlefield at the rollout turn-step. Counters only, so an
armed run is byte-identical; `MTG_BOARD_CENSUS_STRIDE` (default 64) keeps it affordable.

200 games at play settings: rollout boards reach **298 permanents** (real play tops out at 15),
with a single fungible class of **288**. Mean board is only 8.3 — the cost lives entirely in the
0.5% of steps above 25 permanents. It also reports the fusion prize in both cost currencies
(`linear_work xN` / `quadratic_work xN`) and which cards carry the collapsible mass; on Fungus that
is **Forest first, Saproling second**, which is itself a correction to the guess below.

## The second fix: per-GAME presence gates (and the general defect behind them)

After the Dragon fix the remaining profile was the same shape: `LookupCached` (14.9%) plus
`FireEtbWatchers` / `FireCreatureEnterWatchers` self time, all of it **repeated full-battlefield
walks per ETB**. Per entering permanent the cascade walked the board about five times
(`RefreshDevotionCreatures`, `RefreshCityBlessing` twice — once per player, `FireCreatureEnterWatchers`,
the Lathliss pass, the Scourge pass), each with a `LookupCached` per permanent, for mechanics the
deck does not contain.

**The gates that were supposed to prevent this DO NOT WORK.** `CardDatabase::HasQuestAnthem()`,
`HasTokenDoubler()` and `HasCounterDoubler()` are computed over `m_cards`, which is the **entire
387-card `cards.json`**, not the deck being played. Doubling Season and Beastmaster Ascension are
in that file, so all three are **unconditionally true in every run** and gate nothing. The comment
on `HasQuestAnthem` claiming "False for every deck but Fungus, so the scan is provably skipped" is
wrong, and should be corrected when those three are converted. (`MaxHandSizeAnthemMax` is different
and is fine — it is a runtime *bound* on hand size, not a presence flag.)

**The gate has to be per-GAME, stamped from the decklist.** That mechanism already existed and was
simply not used here: `GoldFishRunner::StampDeckTraits` computes `deck_reads_mv_cast` and
`deck_reads_endstep_lifegain` exactly this way. Three siblings now join them —
`deck_has_ascend`, `deck_has_devotion_creature`, `deck_has_dragon_ping` — each gating its scan.

Three properties make this safe rather than merely fast:

* **They default TRUE**, i.e. to the pre-existing behaviour. The scenario harness builds a
  `GameState` *without* stamping traits (documented at the end of `StampDeckTraits`), and an
  unstamped state must keep the old semantics. A flag wrongly true costs time; wrongly false
  drops a trigger. **Only ever err true.**
* **The scanned set is mainboard + sideboard** — the sideboard is reachable through a wish, and
  every token is either vanilla or a copy of something that came from those two (the passive
  opponent's scheduled spawns are plain 1/1s).
* **Garth One-Eye is an explicit escape hatch.** He materialises specific cards that need not be
  in any decklist, so a deck running him leaves all three gates open.

Measured on the same game, cumulative with the subtype fix:

| arm | wall | win turn |
|---|---|---|
| before both | 311.90 s | 6.0000 |
| + subtype fix | 218.72 s | 6.0000 |
| + presence gates | **120.84 s** | 6.0000 |

**2.58x overall, play byte-identical** (smoke 80/0, regression 108/0, 0 play-changed on both).

Unlike token fusion this helps every deck, not just this one, and it leaves a mechanism in place:
**the next cascade scan added should get a `deck_has_*` stamp rather than a `CardDatabase::Has*()`.**

## Candidate: de-duplicate TOKEN CREATION — as a deck heuristic, not a general rule

**User, 2026-09-17:** *"Cost-wise we should de-duplicate token creation. If you don't want a
saproling token from source x, you also don't need to evaluate the token from source y, assuming
both can create one."* And immediately after, the qualifier that decides where it goes:
*"(this example is potentially a heuristic for this deck, since it really is not a general rule)"*.

**That qualifier is the whole design.** The repo's core invariant is that only a deck/archetype
provider may narrow the search; a generic limiter is forbidden. And the general rule really is
false — two sources that both make "a 1/1 Saproling" are not interchangeable in general:

* the costs differ (three spore counters off a specific body, vs mana, vs a sacrifice), and which
  body paid decides what that body can do on a LATER turn;
* the sources' own residual state differs (a Thallid left at 0 spore counters vs one left at 2);
* one source may be doubled by Doubling Season and another not;
* a source may itself be an attacker, or a sac-outlet's fodder.

What IS true for this deck is narrower and worth measuring: across the Thallid family the bodies
are near-interchangeable, so the *decline* branch is being re-evaluated once per source.

**Existing machinery to build on rather than reinvent** (all in `src/ai/TurnSolver.cpp`):

* `FinalizeFoldTags` + the `g_fold_*` counters — collapses interchangeable actions into one class,
  but today it is offered `CastFromHand` candidates; the spore activation is an
  `ActivatePermAbility` (`PermAbilityMode::SporeSaproling`), so it is almost certainly not covered.
  **Check that first** — the fold may just need extending to the activation path.
* `MTG_CAND_DEDUP` / `MTG_DEDUP_CENSUS` — skips a candidate whose post-apply state an earlier
  sibling already reached. Measured at **64% of scored candidates on Snow**; default OFF because
  it was built for speed and did not deliver it there. The census is the cheap first measurement
  here: **run it on a Fungus slow game and read the dup rate before building anything.**
* `MTG_FOLD_VERIFY` — the recoverability verifier, and the reason this can be done safely at all:
  it asserts at runtime that every rejected selection has a legal, enumerable TWIN. That converts
  "is this lossy?" from an argument into a check. Both holes fixed in `4b589c0d` were twin
  failures it would have caught.

**Order of work:** census the duplication rate → confirm the fold does not already cover the
activation path → implement in a `FungusProvider` (which does not exist yet; see
`fungus-second-main-and-devour.md`, which needs one for the same reason) → measure per
`heuristic-optimization.md`, with `MTG_FOLD_VERIFY` armed.

## The direction that was guessed, and is now NOT the priority

The repo already has a USER-blessed doctrine for exactly this shape: **fuse fungible tokens for the
search** (`clue-fusion-search-shortcut`). Represent identical vanilla 1/1 Saproling tokens as one
`Permanent` carrying a stack count for search/rollout purposes, keeping the viewer per-token. That
attacks the 58× per-node term directly, which is the half a wider plan-prune cannot touch.

Two guardrails from the same doctrine:
* **Do NOT reach for a token cap.** That is a Tier 1-3 clause silently dropped, which the
  analyze-deck skill forbids outright.
* **Do NOT narrow with a generic limiter.** Per the core invariant, any narrowing belongs in a
  deck/archetype provider and must be measured against the unpruned arm.

Before building it, get the missing measurement: **what is the actual rollout board size
distribution on a slow game?** The real board is 15; the rollout board is unknown and is the number
that decides whether fusion is worth it. There is no existing instrument for it —
`MTG_ENUM_HIWATER_KB` reports `bf=` only when a *plan set* crosses a size threshold, which never
fires here.

## Practical consequence today

Fungus **cannot go into the regression suite at current cost** — `verify_deck.py` correctly warns
that nothing tracks the deck's play digest because it is not a suite case, but a 24-minute game
would blow the smoke (<15 min) and regression (<45 min) budgets on its own.

A value-leaf generation on this deck should also be costed against these numbers before being
started: the skill quotes tens of hours for a normal deck, and ~2% of this deck's games are
minutes-to-tens-of-minutes each.

---

## ROOT CAUSE #2, 2026-09-18: the enum memo was paying a deep copy on every hit (1.37x)

The Dragons fix above removed the largest per-node cost and thereby exposed the next one. A `perf`
profile of a straggler label game (`--seed 900193 --game-index 193`, one of the ten games phase A
could not finish in **two successive runs**, ~2.9 h each and still `[RUNNING]`) put a single symbol
at the top:

```
25.64%  std::vector<Action>::vector(std::vector<Action> const&)
 3.80%  TurnSolver::Plan::Plan(TurnSolver::Plan const&)
 2.67%  basic_string::_M_construct<char*>
 2.30%  std::vector<Action>::~vector()
```

A DWARF call graph named the path exactly:

```
FSLineWin -> FSLineTail -> ApplyPlanDirect -> EnumerateBreakpointPlans
  -> vector<Plan>::vector(const vector<Plan>&)     <- a COPY, not a move
    -> Plan::Plan(const Plan&) -> vector<Action>::vector(const&)
```

`EnumerateBreakpointPlans` was one line:

```cpp
return BpEnumEntryFor(state, is_pre_combat)->plans;   // returns BY VALUE
```

`BpEnumEntryFor` hands back a pointer into the thread_local enum memo, and returning `->plans` by
value **deep-copies the whole memoised list on every hit** — every `Plan`, its entire
`vector<Action>`, and a `std::string` inside each `Action` (`sizeof(Action) == 352`). The memo
exists to avoid re-deriving that list, and it was handing back most of the saving in copy cost.
This is why it hid behind the Dragons cost: both are per-node constants, and the profile only ever
shows the bigger one.

**The fix** (commit below) adds `EnumerateBreakpointPlansRef`, returning `const std::vector<Plan>&`,
and converts the three hot `ApplyPlanDirect` call sites. The by-value overload stays for callers
that need ownership.

**The lifetime contract is the whole risk, and it is documented on the declaration.** The reference
is invalidated by the next enumeration on the thread, via three distinct paths: the memo clears on
its count cap, it clears on exceeding the plancache byte budget, and when the cache is disabled (or
one entry exceeds the whole budget) the entry returned is a single `thread_local` scratch that the
next call overwrites. Each converted site was checked to read the list immediately and to call
nothing re-entrant — `PlanOpensBreakpoint`, `IsApplyEmptyPlan` and `BpCandFingerprint` are all
verified pure. The **executor** sites in `AIEngine` were deliberately left by-value: they run once
per committed decision, they are not hot, and that path is lockstep-critical.

**Measured** (matched Profile-config binaries, same games, same threads):

| path | before | after | speedup |
|---|---|---|---|
| label / row-dump (`MTG_EVAL_ROWS_K=3`, unbounded search) — time to 136 rows, 24 games | 141.4 s | 103.3 s | **1.37x** |
| play (depth 5, budget 20 ms) — 100 games seed 7001 | 63.0 s | 55.6 s | **1.13x** |

1.37x is what Amdahl predicts from a 25.6% site (1.34x), which is the check that the right thing
was removed. Play is **byte-identical**: smoke 83/0 with `play-changed=0`, scenarios 100/100, unit
2,634,451 assertions, and the A/B's own avg turn is 5.6800 on both arms.

After the fix the profile is **flat** — no symbol above 7.2% (`BuildSimKey` 7.2% + its lambda 2.0%
is now the largest single cost, and the next target if this deck needs more).

**What this does NOT fix.** It is a constant factor, and the ten straggler games are an
*unbounded-search* problem: the label path runs with no budget by design (a full-strength teacher
label), so a board that explodes combinatorially still explodes, 1.37x sooner. The remaining lever
for those specific games is still the Saproling-fusion work described above, or accepting them via
`valueleaf.sh finish`.

## THE MISSING MEASUREMENT, taken 2026-09-18: the fusion prize is x1.56 linear / x3.66 quadratic

`MTG_BOARD_CENSUS=1` over 23 Fungus label games (the completable members of the `901750` block;
`gi=12` excluded because it never finishes), 13,958 sampled rollout turn-steps at stride 64:

```
board: all_perms_mean=11.69  ours_mean=9.86  ours_max=364  all_max=365
ours:  creatures_mean=5.48   tokens_mean=2.15  token_share=0.218
FUSION: classes_mean=6.31  collapsed_mean=3.55  shrink=0.360  biggest_group=352  hosts_uniqued=4144
FUSION PRIZE: linear_work x1.56333  quadratic_work x3.66357
              (sum n 137,670 -> 88,062 ; sum n^2 2,207,262 -> 602,490)
```

**The rollout board really does reach 364 permanents** against a real board of 15, which is the
hypothesis this instrument was built to test, and it holds.

**The cost is in a tail so thin that mean board size is the wrong summary.** Boards of 97+ are
**0.07% of steps** but carry **23.9% of all quadratic work**; every bucket at 17+ carries 47.5% of
quadratic work between them. After fusion **every bucket above 16 is empty** — the tail is exactly
what fusion deletes.

**THE SURPRISE: the largest fungible class is FOREST, not Saprolings.**

```
collapsible_by_card  Forest=23775   1/1 Saproling Token=20669   Tukatongue Thallid=2062
                     Utopia Mycon=879   Thallid Shell-Dweller=577   Simic Growth Chamber=496
```

This doc (and the clue-fusion precedent) framed the work as *Saproling* fusion. The census says
basic Forests are 48% of all collapsible instances, more than the Saproling tokens. That matters
for sequencing, because **lands are the easier and safer half**: no summoning sickness, no combat
state, no counters, no P/T, and no lord/anthem interaction — the fields that make creature fusion
delicate. `hosts_uniqued=4144` is the guard already handling the one real complication here (a
Forest hosting Wild Growth is force-uniqued and cannot collapse).

**Recommendation:** do LAND fusion first as its own change. It is the larger share of the prize, it
touches none of the combat/lord machinery, and it can be measured against the unpruned arm on its
own before any creature-token work is attempted.

**What this does not change:** the prize is a per-node constant (a big one), not a bound on the
search. `gi=12` is stuck labelling **turn 2** — its real board is nearly empty, and the explosion is
entirely inside the label's full-game rollout. Fusion makes each node ~1.6-3.7x cheaper; it does not
make an unbounded search terminate.

## WHY PHASE A HANGS AND PHASE C DOES NOT: the abandon ceiling is not wired to phase A

Phase A of the value leaf stalled on this deck **twice**, both times on the same ten single-game
jobs, each burning ~2.9 h and still `[RUNNING]` when the run was stopped — permanently occupying
10 of 24 workers and producing no rows.

`scripts/valueleaf.sh` already has the mechanism that exists for precisely this. `BatchRunner`
supports a deterministic per-game work ceiling (`--abandon-units`, `--abandon-k`,
`--abandon-calib`, `--abandon-floor-units`), stated in work UNITS rather than wall clock so the
abandoned set is identical on every machine and across resumes. **It is passed only to the PHASE C
matrix invocation.** Phase A's batch call passes none of it, so a phase-A game has no ceiling of
any kind and runs until the process is killed.

The design note for that mechanism is itself written against a phase-A observation — it rejects a
250M floor because it *"would have let through the 6.41-hour game the phase-A heartbeat had already
recorded"*. So phase A's monsters were known; the guard just never got attached to it.

Abandoning is consistent with what phase A already accepts: rows dedupe on `(seed,turn)`, resume
queues only games with **zero** rows, and the script's own comment says losing late-turn rows is
*"a handful of rows against a hundred core-hours; `finish` exists precisely because fewer rows is
acceptable"*.

**Proposed change** (NOT made — it alters what training data a generation keeps, which is the
user's call):
* pass `--abandon-units <N>` to the phase-A batch. The ABSOLUTE cap is the right form here; the
  relative `--abandon-k` ceiling calibrates against a matrix *cell*, and phase A has no cells.
* `N` must be calibrated **against the label workload**, per that note's own warning
  ("CALIBRATE AGAINST THE WORKLOAD YOU ARE BOUNDING, not a convenient proxy"). Phase C's
  `ABANDON_FLOOR_UNITS=40000000` was calibrated for unbounded search **against a value leaf**;
  phase A is unbounded search with **no** leaf (it is generating the first model), so units per
  core-second differ and the number cannot be carried across. The batch reports per-game units only
  when abandonment is armed, so the calibration is: arm it with a deliberately huge cap on a Fungus
  label block, read the reported units, and set `N` to the user's stated policy
  (*"above 10 minutes... maybe even 30 minutes"*).

Without this, any Fungus value-leaf run needs `valueleaf.sh finish` to terminate, and phase A will
re-queue the same ten zero-row games on every resume.

---

## ROOT CAUSE #3, 2026-09-18: the label stragglers are a MISSING CERTIFICATE, not a cost problem

**User, 2026-09-18:** *"We should also consider reducing the branching factor as needed."* This
section is the measurement that request asked for, and it ends somewhere the two cost fixes above
could not reach.

### The branching census, on BOTH paths -- they are different machines

`MTG_BF_CENSUS` ("which effects are causing notable branching factors") plus `MTG_DEDUP_CENSUS` and
`MTG_ROLLOUT_STATS`. The play column is the known pathological game single-threaded
(`--seed 1600607 --game-index 607`); the label column is 8 games of the `901750` block at the phase-A
config (`MTG_EVAL_ROWS_K=3 MTG_EVAL_ROWS_ROLLOUT=0`, unbounded search).

| | play (d5/b20) | label (unbounded) |
|---|---|---|
| decisions / candidates | 19,422 / 344,619 | 19,644 / 315,225 |
| mean width / max width | 17.74 / 200 | 16.05 / 176 |
| interior_frac | 0.142 | **0.823** |
| `units.fs_pre` (one node per pre-combat plan applied) | 2.9% | **43.7%** |
| `units.la_cand` | 35.3% | 10.7% |
| `units.fs_bp_wave` | 0.6% | 18.7% |
| `units.rollout_step` | 21.2% | 13.4% |
| candidate dup rate (post-apply state) | **41.8%** | 18.6% |
| `copy_FALSE` (plan-signature dedup would delete these) | 67,725 | 101,527 |
| greedy subsets scored / search subsets | 25,790,445 / 354,473 | 6,231,944 / 2,268,409 |

**Do not carry a conclusion from one column to the other.** Play is a rollout-and-candidate machine;
the label is an interior-node machine (82% interior). `MTG_CAND_DEDUP`'s 41.8% prize is play-side
only -- its two call sites live in `SolveWithLookahead` and the label path never reaches them, which
that flag's own note already recorded.

**The dominant branching DIMENSION on the label path is the spore K-axis.** 44.5% of candidate mass
carries a `chosen_x`, and it is almost entirely `ActivatePermAbility` (kind 26) on the Thallid
family: `Thallid` 99,221 activations from **3 distinct physical sources**, `Thallid Shell-Dweller`
39,855 from **4**. Interchangeable sources cost 2^k selections to express k+1 distinct outcomes,
which is the exact defect the hand-cast half of `FinalizeFoldTags` was built for.

### Lever 1, BUILT: `MTG_FOLD_COUNTER_SOURCES` -- and it is nearly inert here

`PermIsPlainForFold` refused any permanent carrying spore/quest counters. Its stated reason is
right but proves a narrower thing than the clause implemented: *"a Thallid holding 2 spores and one
holding 5 are not interchangeable"* is an argument about UNEQUAL counts. Two Thallids on the SAME
count are as interchangeable as two Scrying Sheets. The arm moves that distinction into
`ActivationEquivTag` instead of using it to gate membership.

| | arm off | arm on |
|---|---|---|
| tagged actions | 2,362,673 | 2,816,289 |
| kept classes / members | 99,920 / 205,057 | 102,842 / 210,901 |
| `drop_src` (source emits >1 k) | 0 | **116,427** |
| `guard_reject` (greedy / search) | 401,563 / 12,004 | 482,591 / **12,004** |
| greedy subsets | 6,231,944 | 6,150,916 (-1.3%) |
| **`units_total`** | 2,910,912 | **2,910,912** |
| `MTG_FOLD_VERIFY` UNRECOVERABLE | 0 | **0** (recoverable 457,567) |
| wall, 8 label games, 2 reps | 11.28 / 11.58 s | 11.15 / 11.10 s |

It is **lossless** (the verifier is armed and non-vacuous) and **inert when off** (the control
reproduces `units_total` to the digit, and the counters are mixed into the tag only when the arm is
on). But read the two bold cells: `units_total` is **identical to the digit** and the SEARCH half of
`guard_reject` does not move at all -- every new rejection lands in the greedy odometer. The ~2.6%
wall is the uncounted greedy-subset saving and nothing else. Two structural reasons it cannot do
better here: `drop_src` kills 116,427 classes because a Thallid holding 6+ counters emits more than
one `k` and the fold's source condition (correctly) refuses it; and the searched enumeration does
not build its selections through the odometer the canonical-prefix guard rules.

**Default OFF.** Kept because it is a correct generalisation of a shipped fold and a deck with more
same-count counter sources would pay differently -- not because it helped this one.

### Lever 2, MEASURED: the general EOT closure is NOT dead here (it was on Snow)

`MTG_FSW_EOT_DEDUP=1` (census, byte-identical), same 8 games:

```
general nodes=44315  distinct end-states=300029  duplicate=58203 (16.2% of boundaries)
by elided work: duplicate free=52543 leaf=0 REAL=5660 | distinct free=209095 leaf=0 real=90934
                real-dup share=5.9%
```

`a3503192` measured this **DEAD on Snow** -- 4.5M duplicate boundaries, *every one* `free`, only
389 of 16,077,262 distinct boundaries leading to a real subtree. On Fungus **5,660 duplicates lead
to a real subtree, a 5.9% real-dup share**, against an `fs_pre` that is 43.7% of units. So the Snow
census verdict does not transfer.

**The COLLAPSE still does not pay, and mode 2 was run rather than assumed** (same 8 games):

| | mode 0 | mode 2 (collapse) |
|---|---|---|
| `units_total` | 2,910,912 | **2,910,912** |
| wall | 11.31 s | 11.39 s |
| labels | — | **identical** (row content; the dump ORDER varies with thread scheduling, so compare sorted -- a raw `cmp` reports a false difference) |
| its own `distinct` / `duplicate` / real-dup | 300,029 / 58,203 / 5,660 | **unchanged** |

Its own counters not moving is the tell. The closure fires at the end-of-turn boundary, i.e. AFTER
the plan has been applied, so the boundary count is fixed by the plan count and only the recursion
BELOW it can be elided -- and eliding it changes nothing, which points at that recursion already
being served by the transposition table. (Hypothesis, not measured: the cheap confirmation is a TT
hit-rate read across the two modes. It is consistent with `a3503192`'s Snow finding from the other
side.) **Not a lever on this deck either; the 5.9% census figure is a boundary statistic, not a
prize.** Also from the same run:
`[bp-waves] scored=572,671 rolled=289,220 dupstate=265,108 improved=0` -- the wave phase is 18.7% of
label units and improved **nothing** in 8 games (Snow: 133 of 158M, so this is the same shape, worse).

### THE STRAGGLER: what is actually happening, measured live

None of the above is what makes a game run 2.83 h. `MTG_WINLESS_STATS_EVERY=30` on the straggler
(`--seed 901762 --game-index 12 --games 1 --threads 1`) prints the search's position while it runs,
which is the only way in -- the box refuses gdb/perf attach and an exit-time counter never prints
for a game that never exits. After ~90 s:

```
=== [progress] at t7 cut=7 candidate 177/676 (max seen 960) ===
=== [progress] at t7 cut=7 candidate  37/332 (max seen 1100) ===
=== WINLESS CERT[m1]: checks=13212 fired=0 (0.0%) ===
=== WINLESS CERT scope: fsw nodes all=16006 label=15806 edge=13209
                      | plans all=873680 label=866773 edge=811799 ===
=== WINLESS SEED: tries=15809 wins=3 (0.0%) | edge tries=13212 wins=3 ===
=== WINLESS RESIDUAL: 13209 of 13212 edge nodes (100.0%) resolved by neither ===
=== LABEL WORK: ApplyPlanDirect calls=6762581 | fsw-plans=6703584 ===
```

Three numbers say the whole thing:

* **93.7% of every plan the search expands is at a HORIZON-EDGE node** (811,799 of 866,773). An edge
  node sits at `turn >= cutoff`: it cannot search deeper, so its only job is to answer "can I win
  THIS turn?"
* **Those nodes are 99.98% no-win** -- `WINLESS SEED` finds 3 wins in 15,809 tries.
* **The certificate that exists to answer exactly this question fires 0 times in 13,212 checks.**

So the search proves "no win on turn 7" by brute-force applying 300-1,100 plans at each of 13,209
nodes, one at a time, ~99.98% of the time to arrive at the answer a certificate would return in
microseconds. Node widths are still GROWING when sampled (960 -> 1,100), which is why the game has
no natural end.

### Why the certificate never fires: Fungus has no provider

`ProvenWinlessThisTurn` is a `DecisionProvider` hook whose generic implementation is
`return false` -- deliberately, and the contract note says why ("a sound generic bound has to prove
nothing else in this deck can move the opponent's life total or library, and `CardParams` carries
419 fields"). **Exactly two providers implement it: `EldraziFlickerProvider` and `SnowProvider`.**
Fungus has a recogniser (`DecisionProviders.cpp`, the six-card gated-param signature) but it routes
to `GenericProvider` -- the comment there says so outright: *"Fungus has no measured deck heuristic
to hold yet"*.

**That routing decision, made for the heuristics, is what costs the label path its tractability.**
The certificate is not a heuristic; it is a proof, and it is the only thing in the engine that can
turn a 1,100-wide edge node into O(1).

### What this retires, and what it promotes

* **RETIRED as the straggler fix: land/token fusion.** The recommendation two sections up ("do LAND
  fusion first") stands as a per-node cost win (x1.56 linear / x3.66 quadratic) and would help every
  Fungus game. It is **not** the straggler fix and must not be sold as one: a 3.7x cheaper node
  against a node count that is still growing at sample time buys 3.7x on an unbounded quantity. The
  same sentence was already written at the end of the census section; this measurement is the proof.
* **PROMOTED: `FungusProvider::ProvenWinlessThisTurn`.** This is now the top item for this deck, and
  it is the concrete answer to step 3.1 of `label-work-bounding-by-reachable-states.md` ("classify
  the tail before building anything") for Fungus: the tail is **no-win-by-8 edge nodes**, not late
  wins and not failed combos. Build against `SnowProvider::ProvenWinlessThisTurn` as the worked
  example. The bar is the hook's own: it may over-credit the player's reach and decline, never
  under-credit -- a false positive silently converts a win into a loss. `WINLESS SEED`'s 3 wins in
  15,809 tries are the audit set to check it against, and `MTG_WINLESS_WINDUMP=<n>` prints the
  winning plans so the certificate can be made to cover them by EXECUTION rather than approximation.
* **NOT a lever, run and refuted:** `MTG_FSW_EOT_DEDUP=2` (see the table above -- `units_total`
  identical to the digit, labels identical, its own counters unmoved). The 5.9% real-dup share is a
  boundary statistic; collapsing it buys nothing.

### Method note worth keeping

Two separate A/Bs in this section first read as "rows DIFFER" under `cmp`, and both were **thread
scheduling reordering the dump file**, not a behaviour change -- the row CONTENT was identical in
each. `MTG_DUMP_VALUE_ROWS` is written by all workers into one file with no ordering guarantee.
Compare label dumps **sorted**. A raw byte compare on a multi-threaded dump manufactures a play
change out of nothing, which is expensive in exactly the wrong direction: it makes a lossless
change look lossy and invites someone to "fix" a correct mechanism.

---

## BUILT, 2026-09-18: `FungusProvider::ProvenWinlessThisTurn`

The item promoted in the section above is now implemented (`src/ai/DecisionProviders.{h,cpp}`),
behind `MTG_FUNGUS_CERT` (**default OFF** -- see "The default question" below). This section records
what it proves, what it measured, and where the remaining looseness is, so the next tightening is
aimed rather than guessed.

### Why Fungus had no certificate at all

`ProvenWinlessThisTurn` is a `DecisionProvider` hook. The generic implementation is `return false`
-- "I cannot prove anything" -- and before this change only `EldraziFlickerProvider` and
`SnowProvider` overrode it. Fungus routed to `GenericProvider`:

```cpp
if (fungus)      { return g_generic; }   // before
if (fungus)      { return g_fungus; }    // after
```

That routing line was written for *heuristics* -- Fungus wanted none of Snow's play preferences, so
it took the generic provider and with it, silently, the generic **proof**. This is worth naming as a
trap for every future deck: **the provider table couples judgement and proof, and they have opposite
defaults.** Declining to supply a heuristic costs nothing; inheriting `return false` for a
certificate costs the entire horizon-edge saving. A deck that opts out of provider heuristics should
still be asked whether it can prove a winless turn.

### The contract, and why this deck is provable

The hook's bar is one-sided and unforgiving: it may **over-credit** the player's reach and decline,
but it may never **under-credit**, because a false positive silently converts a win into a loss.
Fungus is provable because four facts hold across its whole pool, each checked by reading
`src/cards/data/cards.json` rather than from memory:

1. **Combat is the only route to the opponent's life.** No burn, no drain, no mill kill.
2. **Nothing grants haste**, so this turn's attackers are exactly what `CanAttackFull` reports *now*
   -- a token created later this turn can never attack this turn.
3. **Nothing untaps**, so an already-tapped body is spent.
4. Only two effects can grow the team after the function looks: **Sporecrown Thallid** (the pool's
   only lord, `+1/+1` to other Fungi/Saprolings) and **Beastmaster Ascension** (`+5/+5` at seven
   quest counters, one counter per *declared attacker*, doubled by Doubling Season -- and the
   counters land in the declare-attackers step, so a wide enough attack switches the anthem on
   during its own combat).

So the bound is `base_damage + attackers x (lords + anthem)`, and the certificate fires only when
that is strictly below the opponent's life. Blockers are ignored (they only reduce damage), and
`lords` is applied to every attacker rather than only the Fungus/Saproling ones it really pumps --
both over-credits, the admissible direction.

### THE INVARIANT: the whitelist is by NAME, and it is load-bearing

`FungusCertKnownDef` checks the card's **name** against the fourteen mainboard cards, and every zone
that can reach play this turn is walked against it -- hand, battlefield (both sides), graveyard, and
the library when a draw outlet is live. An unrecognised card **declines**.

This is deliberate and must not be "improved" into a params-shaped test. The property it buys is
that **adding a card to the deck can make the labeller slower; it can never make it wrong.** A
params-shaped test would instead silently mis-price the first card whose reach is expressed by a
param the certificate does not read -- which is the exact failure mode that turns a proof into a
guess. The cost of the name list is that it must be edited when the decklist changes; that cost is
the point.

Two `d == nullptr` branches are load-bearing in the other direction: a definition-less permanent or
graveyard entry is one of **our own Saproling tokens**, not an unknown card. The same missing
definition that puts it on that branch is what guarantees it carries no params and hence no ability.

### Measured: 8 label games, `--threads 8`

| | `MTG_FUNGUS_CERT=0` | `MTG_FUNGUS_CERT=1` |
|---|---|---|
| `WINLESS CERT[m1]` | `checks=37253 fired=0` (**0.0%**) | `checks=37253 fired=30050` (**80.7%**) |
| `units_total` | 2,910,912 | 1,645,239 (**1.77x**) |
| wall, 2 reps | 11.28 s / 11.24 s | 7.35 s / 7.24 s (**1.55x**) |
| label rows | 47 | 47, **identical content**, avg 5.8750 both arms |
| `MTG_WINLESS_AUDIT` | -- | 30,050 certified nodes probed, **violations=0** |

`MTG_WINLESS_AUDIT` is the falsification harness, and it is the only number in this table that
speaks to *correctness*: it runs the win-seed even at nodes the certificate cut, so a seeded win
there is a recorded violation. Zero on 30,050 nodes is evidence, not proof -- the proof is the
contract above.

### Declines are ATTRIBUTED, because the last two tightenings came from the tally

`FungusWhy` tallies every decline by reason and `FungusCertReasonReport()` prints it at exit **and on
every `[progress]` tick** -- a straggler that never exits is exactly the game whose declines you need
to read. Both tightenings this buys were diagnosed, not guessed:

* `unknown-card=7,656` -- the largest class at first. `MTG_FUNGUS_CERT_TRACE` named it in one line:
  `1/1 Saproling Token`, in the **graveyard**. The deck sacrifices Saprolings all game, so its own
  dead tokens were being read as unrecognised cards. Fixed by the `d == nullptr` skip. **Now 0.**
* `library-reachable=17,004` -- my own blanket decline the moment a Psychotrope Thallid was on the
  battlefield, on the grounds that the library becomes reachable. Replaced by a **fodder-bounded
  library walk**: at most `fodder` cards can be drawn, so at most that many library lords can
  arrive. **Now 0.**

After both: `fired=36,611 combat-lethal=14,907`, with every other class at zero.

### Where the remaining looseness is -- the aimed next step

`combat-lethal` is the only surviving decline class, so the next tightening is entirely a question of
making the damage bound tighter without making it unsound. Three named over-credits, in the order
they are likely to bite:

1. **A Saproling is counted as an attacker AND as sac fodder simultaneously.** This is the sharpest
   one, and it is not a mana question. Both of the deck's outlets eat Saprolings
   (`sac_creature_requires_subtype: "Saproling"`), so a body spent to find a Beastmaster Ascension
   cannot also be attacking with it. The joint bound is "attack with `n - k` while drawing `k`",
   maximised over `k`, instead of today's "attack with `n` *and* draw `n`".
2. **`fodder` counts every creature, not just Saprolings.** The outlets require the subtype, so the
   deck's Fungi (Thallid, Sporesower, Utopia Mycon, Mycoloth, ...) are not fodder for either. Sound
   but loose, and cheap to fix.
3. **Hand cards are credited as cast for free.** The analogue of `SnowCertGainBound`. Note that
   Fungus makes this weaker than it looks: **Utopia Mycon** is "Sacrifice a Saproling: Add one mana
   of any color" with *no* mana cost, so every Saproling is also a mana, and a naive "untapped lands"
   bound would be badly wrong. A mana bound here must be a joint Saproling budget -- which is the
   same budget item 1 is about, so the two should be built together or not at all.

The straggler (`--seed 901762 --game-index 12`) is the case that shows why this matters: its fire
rate **falls from 80.7% to ~67% as the board widens**, and every one of its declines is
`combat-lethal`. Once the board is wide enough that seven attackers is automatic, the Ascension term
alone reads as lethal and the certificate stops paying. **That game is still enormous with the
certificate on** -- 53M `ApplyPlanDirect` calls and climbing at 19 minutes -- so the certificate
should be reported as a large constant-factor win on typical label games, **not** as the fix for the
worst straggler.

### The default question: RESOLVED, default ON (2026-09-18)

`MTG_FUNGUS_CERT` now defaults **ON**; `=0` remains the off switch, and with it off `FungusProvider`
is byte-for-byte `GenericProvider` again, which is what a future bisect wants.

The gate written in the first draft of this section was "~24 label games with `MTG_WINLESS_AUDIT`
armed and violations still zero". It was run on a **held-out** seed (31337), deliberately disjoint
from the 8-game block the two tightenings were tuned on -- tightenings diagnosed from a decline
tally are fitted to the block that produced the tally, so measuring the result on the same block
would be self-confirming.

| | 8-game tuning block | **24-game held out** | `SnowProvider` at adoption |
|---|---|---|---|
| label rows | 47 | **128, identical to arm-off** | 123 |
| fire rate | 80.7% | **85.8%** | -- |
| wall | 11.28 s -> 7.35 s (1.55x) | **179 s -> 94 s (1.90x)** | -- |
| `MTG_WINLESS_AUDIT` | 30,050 probed, 0 violations | **99,128 probed, 0 violations** | -- |

**The load-bearing number is 99,128 audited nodes, not the row count.** 128 rows is merely
*comparable* to Snow's 123, not wider; what makes this adoptable is that the falsification harness
ran the canonical go-off at every node the certificate cut and found no win, on 3.3x the nodes of
the block the tightenings were fitted to. The fire rate also rose on held-out data (80.7% -> 85.8%),
which is the opposite of the usual tuning story: the wider block holds more ordinary mid-game edge
nodes and proportionally fewer of the wide-board nodes that defeat the bound.

### METHOD TRAP, and it bit this very measurement: `MTG_DUMP_VALUE_ROWS` APPENDS

The first write-up of the table above said **222 rows**. It was wrong, and the way it was wrong is
worth more than the correction.

`MTG_DUMP_VALUE_ROWS` opens its file in **APPEND** mode, not truncate. Verified directly: write a
sentinel line into the target, run, and the sentinel is still there with a fresh `#` header
underneath it. Two of the arm files (`/tmp/w_off.rows`, `/tmp/w_on.rows`) were names reused from an
earlier 8-game A/B in the same session, so each already held 94 rows. 222 = **94 stale + 128 real**.

Why it did not corrupt the *conclusion*, and why that is luck rather than method: the 94 stale rows
were byte-identical in both arms (they came from a prior off/on pair that was itself identical), so
they cancelled in the compare. Filtering both files to the block's own seeds (31337..31360) leaves
128 vs 128, **SAME** -- and the rebuilt binary's arm-off run reproduces exactly those 128. The
comparison survived; the row COUNT, and the "wider base than Snow" claim built on it, did not.

Two rules fall out, and they compose with the sorted-dump note above:
1. **Always write a label dump to a fresh path** (or truncate it first). A reused `/tmp` name in a
   long session is a silent data-union.
2. **Sanity-check the dump's own seed column against the seeds you asked for.** One
   `awk '{print $(NF-1)}' | sort -u` would have caught this instantly -- the file claimed 32
   distinct seeds for a 24-game run. That check is cheaper than the compare it guards.

The same append behaviour explains the audit arm's "DIFFER": it wrote to a *fresh* name, so it holds
the clean 128 while the arm it was compared against held 222. Its 128 are byte-identical to the
arm-off 128. No row's content ever differed in any arm.

### The `combat-lethal` breakdown: MEASURED, and it refuted the ranked guess

`combat-lethal` is the only surviving decline class (33,057 against 115,541 fires on the held-out
block). The three over-credits listed above were *ranked by guess*. So instead of building the
top-ranked one, the decline was split by **which term actually carries it** (`FungusLethal`, printed
beside the reason tally under `MTG_WINLESS_STATS`) -- asking, for each decline, what the bound would
say with the more optimistic terms removed. The split is by ZONE, because that is the difference
between a real threat and a loose credit: on the battlefield the pump is already there; in hand it
still needs `{1}{G}`; in the library it needs a draw costing `{1}` **and** a Saproling **and** the
cast.

```
base-lethal=138  lord-board=238  lord-hand=16  lord-library=32386
anthem-battlefield=160  anthem-hand=105  anthem-library-only=14
```

**`lord-library` is 32,386 of 33,057 -- 98.0% of every remaining decline.** One credit, in one zone,
is the entire tail:

```cpp
lords_lib += std::min(lib_lords, fodder);   // <-- 98.0% of all declines live here
```

It credits every Sporecrown Thallid still in the **library** as drawn *and* cast for free. With four
copies that is **+4/+4 on every attacker**, conjured from cards nobody has seen, and it is applied to
a board where `base_damage` alone is not lethal in 99.6% of these nodes (`base-lethal` is 138).

**The guess this refutes is worth recording.** The doc's own ranked list put the joint Saproling
budget first and the library-*anthem* credit as the likely culprit. The measurement says
`anthem-library-only` is **14 nodes**, and all three anthem buckets together are 279 (0.8%). Had the
joint budget been built on the ranking, it would have been the most intricate and most dangerous
change of the three -- a certificate bug is silent -- aimed at under 1% of the problem. This is the
third time this session that instrumenting a decline class beat reasoning about it
(`unknown-card` -> dead Saproling tokens; `library-reachable` -> a blanket decline; now this).

### The tightening was MEASURED BEFORE BEING BUILT -- and the first two candidates died

Drawing and casting one library Sporecrown costs at minimum a Saproling plus `{1}` for the
Psychotrope draw plus `{2}` more for the cast: **3 mana and 1 Saproling per copy**, where today both
are free. That is the obvious fix, and `UntappedManaUpperBound` is the right helper to price it
(it already credits the sac-for-mana outlet, so Utopia Mycon turning Saprolings into mana needs no
Fungus special case). Rather than build it, it was added as a **what-if counter** -- re-run the bound
with the cap applied, record whether the node WOULD have certified, change no behaviour:

| what-if (none of these are applied) | would-fire | share of `lord-library` |
|---|---|---|
| library Sporecrown priced at `>= 3` mana | 2,025 | 6.3% |
| **CEILING**: library lords DELETED (unsound) | 2,064 | 6.4% |
| **JOINT CEILING**: library lords **and** anthem deleted (unsound) | **32,386** | **100.0%** |

Read those three rows together, because separately each one misleads:

* The mana bound buys 6.3%, and the **ceiling on its entire family is 6.4%** -- the cheap bound
  already captures 98.1% of the best any library-lord tightening could ever do. On its own that
  reads as "the certificate is at its ceiling; stop."
* The joint ceiling says that is **wrong**. Delete the anthem as well and **every single one** of the
  32,386 certifies. So these boards are NOT lethal on the bodies: `base_damage + attackers x
  (lords_board + lords_hand)` is short of the opponent's life **100% of the time**.

The two credits are **CO-CARRYING**: each independently clears the life total, so cutting either one
alone changes nothing because the other takes over. That is the whole explanation for the 6.4%
ceiling, and it is invisible to any measurement that varies one term at a time.

**A trap in the attribution itself, worth keeping.** The zone buckets classify by lords with the
anthem EXCLUDED, while the real bound includes it. So a node lands in `lord-library` whenever the
lords reach lethal -- even if the anthem reaches lethal too. `lord-library = 98.0%` therefore does
**not** mean "the library lord is the cause"; it means "the library lord is *a* sufficient cause".
Attribution by first-match over a sum of terms names one carrier and hides the rest.

### Where the Ascension actually is -- and why the last lever is a SAPROLING budget, not a mana one

```
FUNGUS lord-library, WHERE THE ASCENSION IS: board=29  hand-only=195  library-only=32162
```

**99.3% of the remaining declines rest on an Ascension that is still in the LIBRARY**, and only 29
nodes (0.09%) have a real one on the battlefield. So both co-carrying credits are library credits:
the certificate is conjuring *two different unseen cards* and then believing the board they imply.

That also explains why the mana bound failed, and names the one lever left. Mana is **not** the
scarce resource on these boards -- Utopia Mycon is "Sacrifice a Saproling: Add one mana of any
color" with no mana cost, so a wide token board *is* a large mana pool and a `/3` cap never binds.
The scarce resource is **Saprolings**, and the binding constraint is that they are spent twice over:

* every library card needs a **draw**, and every draw sacrifices a **Saproling** (plus `{1}`);
* every point of Mycon mana also sacrifices a **Saproling**;
* and every sacrificed Saproling is **one fewer attacker** -- which fights the Ascension's own
  precondition, because the anthem needs **seven declared attackers** to switch on.

So the deck cannot both dig out the Ascension and keep the board wide enough to turn it on, and the
certificate currently lets it do both for free. The remaining candidate is therefore the **joint
Saproling budget** -- maximise over `k` (Saprolings spent) of "attack with `n - k` while drawing and
casting with `k`", instead of today's "attack with `n` **and** draw `n`". It is the item the doc
originally ranked first, for the right mechanism but the wrong term.

**Bar for building it:** it is the most intricate change of the three and a certificate bug is
silent, so it needs its own careful soundness pass, a `MTG_WINLESS_AUDIT` run at least as wide as
the adoption run, and -- per the lesson above -- a what-if counter measuring the JOINT effect before
any behaviour changes. The prize is real and now quantified: the joint ceiling is 100% of 32,386
declines, which is 28% of all checks, i.e. a fire rate of ~85.8% -> ~99.8%.

## THE SAC-TO-DRAW CLOCK RULE: adjudicated 2026-09-18, and the predicate named the wrong creature

The user's rule for Psychotrope Thallid ("{1}, Sacrifice a Saproling: Draw a card"), in their words:

> Sacrifice this saproling for a card when the creature doesn't move up our clock and keep it when
> it does.

with the acceptance criterion *"this won't get perfect results with respect to tests but, as long as
we can determine that it is just clairvoyance, we can accept it"*, the method *"one way is simply to
analyze every loss"*, and the scope limit *"the mana case is a different one -- in that case we can
just let the search take over whether it is worthwhile"* (so Utopia Mycon's mana sac is untouched).

### Why the draw is the half worth taking off the search

Fungus runs **no shuffle effects** -- no card in the list carries a shuffle, tutor or fetch param --
so the library is a fixed permutation from setup and the search's simulated draws come from that
same permutation. The draw decision is therefore **structurally clairvoyant**. The mana sac is not:
its payoff is fully determined inside the plan ("this lets me cast X"), so the search can price it
honestly and should keep owning it. That split is the user's and it is the right one.

**No existing instrument can measure this deck's draw clairvoyance by decoupling.**
`MTG_SHUFFLE_SALT_SEARCH` salts mid-game RESHUFFLES only, of which this deck has none: 1,500 pooled
games across baseline + 4 salts returned identical digests on every arm. That is "nothing to salt",
not "no clairvoyance". A positive control on FiveColour/treasure_hunt also failed to move, which
surfaced a second and more general gap: **21 of 24 decks now carry a value sidecar, so the rollout
-- and with it every rollout-time shuffle -- is replaced by the O(1) evaluator.** The flag is far
narrower than its documentation implies. Measuring draw clairvoyance for a deck like this needs a
new instrument that salts the library order seen during EVALUATION, not just at reshuffles.

### The adjudication (5,000 paired games, seed 70000, one pooled batch)

|                | base (OFF) | clock (ON) |
|----------------|-----------|-----------|
| avg win turn   | 5.6406    | 5.6378    |
| unwon          | 19        | 19        |

`DIVERGENT 22 -- WORSE 4, BETTER 18`, unwon set identical. Every one of the four slower games was
replayed in both arms with `--log-dir` and read action by action (`scripts/win_divergence.py` emits
the repro commands; `--seed base+gi --game-index gi --games 1` is the only form that replays the
same game):

| game | base -> clock | what the base arm's sac-draws actually produced | clairvoyance? |
|---|---|---|---|
| `gi=616`  | T7 -> T8 | T7: sacced, drew **Beastmaster Ascension**, cast it, swung 28 -- won on the spot | **yes** |
| `gi=912`  | T5 -> T6 | T5: drew a **Forest**, never cast; the 22-damage kill owed it nothing | no |
| `gi=1280` | T6 -> T7 | T5 -> Forest, T6 -> Tukatongue (never cast); Beastmaster came off the NORMAL T6 draw | no |
| `gi=4880` | T7 -> T8 | T5: drew a **Forest** | no |

So it is **not** just clairvoyance -- one of four. And the other three share a signature that has
nothing to do with sacrificing: the clock arm **declines its turn-1 Utopia Mycon** (`gi=1280` plays
Essence Warden over it), leaving mana unspent and falling a turn behind before a Psychotrope is even
on the board. A decision-level rule cannot do that. The cause had to be upstream.

### The defect: `src` is the OUTLET, and the victim is chosen somewhere else

`DecisionProvider::FodderSacUseful(s, src, def)` passes the **outlet** permanent as `src`. The body
that dies is picked separately, a few lines earlier in `TurnSolver`, by `CanonicalSacVictim`, and
baked into `Action::sac_victim_id`.

The first version of the hook asked *"is SOME Saproling off the clock?"*. `CanonicalSacVictim` ranks
by **expendability** -- effective power, tokens first -- and every Saproling is an identical 1/1
token, so which one it returns is settled by the tie-break, not by whether it is attacking. The gate
therefore green-lit the sac on the strength of a summoning-sick Saproling sitting elsewhere on the
board while the engine went and spent an **attacking** one: the exact inversion of the rule.

Because the hook is consulted at **every node of every lookahead**, that inconsistency is not a
local misplay -- it perturbs the search's valuation of whole lines, which is why the damage surfaced
as a mis-valued turn-1 play rather than as a bad sacrifice. Fixed by recomputing the same victim
with the same arguments the call site uses and testing **that** body.

**The general lesson, and it is not Fungus-specific:** a heuristic that gates one decision must be
evaluated on the object the decision actually consumes. A provider hook that receives the *source*
of an action and reasons about the action's *target* is reasoning about a different question, and
the two agree often enough to look correct in a small sample. The 500-game run saw 2 divergences,
both favourable, and said nothing.

### A withdrawn claim

The 500-game run reported the rule at **1.71x less search work**. It does not replicate: at 5,000
games the same arm used **more** (28,448,468 vs 22,230,701 thread-ms, 1.28x the other way). Fungus
concentrates ~26% of its compute in ~11.5% of its games, so one pathological game moves these sums
by more than the effect. Neither number is a usable cost estimate and the speedup claim is
withdrawn rather than reversed. (Same shape as the repo's own one-run-t-stat lesson: +2.36 then
-2.07 on the same binary.)

### Sequencing consequence for the value leaf

The value leaf's model, depth matrix and crossover are all fitted to the play that ships, so **every
open Fungus play lever must be closed before the freeze is taken**, not after. Three were open on
2026-09-18: `MTG_FUNGUS_SAC_DRAW_CLOCK`, `MTG_SAC_OUTLET_PAY` (the held-out measurement in
`logs/sacpay_ab/holdout.json` turned out to be a manifest with no results in it -- it had never been
run) and `MTG_FORCE_USES_M2` (which could not ride a pooled batch at all until it was given a
heurarm slot: `GoldFishRunner.cpp` read it as a process-wide `static const bool`, so a process could
only ever BE one arm). They are measured 2x2x2 in ONE pooled batch rather than as three A/Bs,
because this repo has already been burned by per-lever readings that did not compose (+0.0201
alone, -0.0616 in combination).

---

# BRANCHING-FACTOR WORK, 2026-09-18: the user's three items

**User, 2026-09-18**, after reading the blow-up diagnosis above:

> "We should heuristic to remove the colour fan. This deck only uses green or perhaps just let the
> mana heuristic handle that. As I said before, we should de-duplicate the choice of which source a
> saproling comes from, the oldest entry that has 3 counters first. We can also try your idea, but I
> would also like the earlier points optimized."

and, clarifying the second item:

> "We should be able to fold entries with the same number of counters."
> "i.e. Heuristically, we know that the source of the saproling does not matter in this deck, so we
> can safely choose any of them that has enough counters to be the first one."

Three items. Item 1 needed no code, item 2 is the large win, item 3 is measured but not built.

## Item 1: the colour fan -- ALREADY HANDLED, nothing built

The user's own alternative ("or perhaps just let the mana heuristic handle that") is what is already
happening. `ChosenFloatColorCandidates` filters candidate colours by DEMAND -- coloured pips summed
over the active player's nonland cards in hand, library, graveyard and battlefield -- and that
filter is explicitly *not* a heuristic and is *not* lifted by `MTG_UNPRUNED(SacColor)`:

```cpp
// a colour no card in ANY of the active player's zones has a pip for cannot be spent on anything,
// so the variant that floats it is a dead branch, not a choice the search is being denied.
for (int c = 0; c < 5; ++c) { if (demand[c] > 0) { idx.push_back(c); } }
```

Every mainboard nonland card in this deck is mono-green, so `demand[]` can only ever have the G slot
set and the fan is the singleton `{G}`. Utopia Mycon's "add one mana of any color" therefore already
costs exactly one action.

**Proved by measurement, not by reading the comment** -- the comment on the emission site said so,
and this session had already caught one stale comment of exactly that kind ("Fungus ... routed to
`GenericProvider`", which `FungusProvider` had made false). The falsification is zero-code:
`MTG_SAC_COLOR_CAP=1` truncates the fan to one colour, so if the fan is already a singleton, capping
it must be byte-identical.

| | 400 games, seed 40000 |
|---|---|
| baseline | avg 5.6725 |
| `MTG_SAC_COLOR_CAP=1` | avg 5.6725 |
| per-game win turns (`MTG_DUMP_WINS`) | **400/400 identical** |

**Nothing to build.** Recorded because "restrict the any-colour fan" is an obvious-looking
optimisation that someone will propose again; the answer is that the demand filter got there first.

## Item 2: the spore-source pool -- BUILT, `MTG_FUNGUS_SPORE_POOL`

### What it does

Every interchangeable spore outlet becomes ONE POOL. The canonical (oldest) payer carries a single
k-axis whose maximum is the POOL's total capacity; every other member emits nothing; the apply twin
spends oldest-first, rolling over between bodies. So the enumeration carries **how many** Saprolings
to make and never **which body pays** -- collapsing 2^n source selections into the n+1 outcomes that
actually differ.

**The count axis survives intact**, which is the one thing that must not be lost: the card data is
explicit that K is a real searched axis and not a greedy max ("holding three counters until a
Doubling Season resolves turns one Saproling into two, and Mycoloth's devour wants the bodies on the
battlefield BEFORE it enters"). Pooling collapses the SOURCE only.

It is a HEURISTIC, on the user's explicit ruling, and not a proof: spending Thallid A's counters
rather than Thallid B's leaves a different DISTRIBUTION of residual counters. The total is
identical -- every pop costs the same three -- and the user has ruled the distribution does not
matter for this deck. That is exactly the deck-provider scope the repo reserves for narrowing, so it
lives behind `FungusProvider::FoldSporeSourceIdentity()` and is inert for every other deck.

Age is a safe canonical key for this specific ability: the activation has no {T} in its cost, so
summoning sickness never makes one body legal and another not (CR 302.6 restricts only {T}
abilities). `PermIsPlainForFold`'s usual manland caveat cannot arise here.

Pool membership is decided by a field-by-field payload compare (`SporePayloadsMatch`), not by card
name, so a list that ever gains an outlet minting something other than a 1/1 green Saproling simply
fails to pool and keeps its own axis, automatically.

### WHY THE SHIPPED LOSSLESS FOLD COULD NOT DO THIS

`MTG_FOLD_COUNTER_SOURCES` was built for exactly this shape and measured **inert** (`units_total`
identical to the digit). The two reasons are structural, and the pool sidesteps both rather than
fixing either:

1. **`FinalizeFoldTags` CONDITION 2 drops any class whose source emitted more than one action.** A
   Thallid holding six counters emits `k=1` AND `k=2`, so `src_cnt > 1` and the class dies. That is
   the whole of `drop_src=116,427`. The condition is correct for the case it was written for (an
   Aether Vial deploy colliding with a cast on the same hand slot); it is simply fatal here.
2. **The canonical-prefix guard is gated `from_odometer`**, and the searched enumeration never goes
   through the odometer -- which is why the SEARCH half of `guard_reject` did not move at all while
   the greedy half moved 81k.

Pooling at the EMISSION site needs neither: it emits one action per COUNT instead of one per
(source, count), so the powerset collapses before any guard is consulted.

### Measured

Inertness first, because a default-off lever that is not byte-identical is a bug regardless of its
prize:

| gate | result |
|---|---|
| lever OFF vs the pre-change binary, 400 games | **400/400 identical** |
| scenarios | 103 passed, 0 failed |
| unit | 114 cases, 2,634,478 assertions |

Then the effect. Branching census (`MTG_BF_CENSUS` + `MTG_ROLLOUT_STATS`, 6 games, single-threaded)
confirms the lever actually fires -- required, because an unchanged average has three causes and
"never ran" is one of them:

| | OFF | ON |
|---|---|---|
| candidates | 428,477 | 414,808 |
| mean width | 29.38 | 28.41 |
| **max width** | **360** | **227** |
| `chosen_x` mass | 123,933 (28.9%) | 109,358 (26.4%) |
| `Sporesower Thallid` activations | 52,006 | **26,869** (-48%) |
| `Thallid Shell-Dweller` activations | 2,161 | **1,367** (-37%) |

And the two paths, which do NOT pay the same:

| | games | `units_total` | wall | labels / win turns |
|---|---|---|---|---|
| play (d5/b20), seed 40000 | 400 | -- | -- | **400/400 identical** |
| label, seed 901750 | 8 | 1,708,948 -> 1,642,561 (1.04x) | 7.32 -> 6.29 s (1.16x) | 45 rows identical |
| label, seed 31337 (held out) | 24 | 7,143,918 -> **4,850,445** (**1.47x**) | 69.60 -> **26.73 s** (**2.60x**) | 129 rows identical |

**The gain scales with how hard the block is**, which is the right shape for this deck: 1.16x on the
easy 8-game block, **2.60x** on the wider held-out block. Wall improves faster than units because
the collapsed enumerations are also cheaper per node -- max width falls 360 -> 227, and the board-size
tail is what the per-node cost is made of (see the census section above).

**Play is metric-neutral and labels are unchanged on every block measured.** That is evidence, not
proof -- this is a heuristic and it narrows the search -- but it is the evidence the adoption bar asks
for, taken on a held-out seed rather than the block the lever was developed against.

METHOD NOTE, and it is the one this doc already records: label dumps were written to FRESH paths and
compared SORTED, and the dump's own seed column was checked (`24 distinct` for a 24-game run). The
append-mode trap that turned 128 rows into "222" is one reused `/tmp` name away at all times.

## Item 3: the joint Saproling budget -- ADOPTED, `MTG_FUNGUS_CERT_JOINT` DEFAULT ON

**Status corrected 2026-09-22.** This heading said "default OFF" long after `cd85196a` adopted
the lever (1.178x on the label path, labels identical). The reader is `EnvOn(..., true)`. It is
paid in, NOT available headroom -- which is exactly how a stale default reads when someone is
hunting for a speedup. `=0` reverts.

The user's "your idea": the certificate's remaining looseness. Per this doc's own standing rule --
and it has now killed three ranked guesses on this deck -- the candidate was instrumented as a
what-if counter BEFORE any behaviour change.

### The bound

Today's bound takes the maximum over a combination that cannot happen: it credits
`min(lib_lords, fodder)` library Sporecrowns as drawn AND keeps every one of those bodies attacking.
Each draw costs a Saproling, the outlets eat Saprolings, so a body spent digging is a body not
attacking -- which fights the Ascension's own precondition of seven DECLARED attackers.

So maximise over `k` (Saprolings spent) of "attack with what is left while drawing with `k`":

```
atk(k) = attackers - max(0, k - free_fodder)        free_fodder = max(0, fodder - attackers)
ld(k)  = lords_board + lords_hand + min(lib_lords, k)
an(k)  = ba_power  if reachable, seen (k>0 for a library-only Ascension), and ba_best + atk(k)*per >= threshold
combat = max over k of  base_damage + atk(k) * (ld(k) + an(k))
```

**Admissible by construction** -- `<=` today's bound at every node, so it can only ever fire more --
with two over-credits kept deliberately so it can never UNDER-credit: bodies that were never going
to attack (summoning-sick tokens, Shell-Dweller's defender) are spent FIRST and cost nothing, and
the attackers that are spent are assumed to have contributed 0 power to `base_damage`.

Soundness note on the obvious objection: a player could attack first and sacrifice afterwards,
keeping both the damage and the draw. That does not break the bound, because a card drawn after
combat cannot pump *this* turn's attack, and the certificate's question is only "can I win THIS
turn".

### Measured, and the variance is the headline

| block | would-fire | still-declines | share of combat-lethal |
|---|---|---|---|
| seed 901750, 8 games | 8,946 | 5,867 | **60.4%** |
| seed 31337, 24 games (held out) | 4,707 | 28,307 | **14.3%** |

**Both numbers are reported because the spread is the finding.** A 4x swing between blocks means
this candidate must not be sized from one of them -- which is the same "one run is not evidence"
lesson the `MTG_BP_SEARCH=0` entry in this doc already carries. For comparison, on the same
instrument the two earlier candidates scored 0.6% (library lord priced at >= 3 mana) and 0.6% (the
unsound CEILING of that whole family), so 14.3% is still an order of magnitude better than anything
previously proposed here, and it is an ADMISSIBLE bound rather than a ceiling.

### Built, and how the cost was taken out

The exhaustive sweep is O(fodder) per declining node and `fodder` reaches the hundreds on exactly
the boards this is for, so it is not shippable as written. It reduces to O(lib_lord_count):

* for `k <= free_fodder` the attacker count is CONSTANT and `ld` is non-decreasing but caps at
  `lib_lord_count`, so the maximum on that whole range sits at `min(free_fodder, lib_lord_count)`;
* past `free_fodder` the attackers fall by one per `k` while `ld` still caps, so only
  `[free_fodder, free_fodder + lib_lord_count]` can hold the maximum;
* `k in {0,1}` additionally covers the library-only-Ascension step in `seen`.

`lib_lord_count <= 4` in this list, so the shipped path evaluates ~7 values of `k`.

**The reduction is CHECKED, not argued.** A reduced set that missed the true maximum would make the
bound too small -- an UNDER-credit, the one direction this hook may never take -- so under
`MTG_WINLESS_STATS` every declining node also runs the exhaustive sweep and compares:
`FUNGUS JOINT reduced-vs-exhaustive MISMATCHES` must be 0. The what-if counter and the real bound
call the SAME lambda, so they cannot drift.

### The latent `lords_lib` soundness bug -- FIXED in the same change

Recorded in `label-work-bounding-by-reachable-states.md` section 6 and now closed. The original
expression capped a SUM OF POWER BONUSES with a draw COUNT:

```cpp
lords_lib += std::min(lib_lords, fodder);          // lib_lords = SUM of power_bonus
```

That is exact only while every lord in the pool is +1/+1 -- true of Sporecrown Thallid, so it has
never yet been wrong. Add a +2/+2 lord and it UNDER-credits: four copies give `lib_lords=8`, and
with `fodder=3` the cap yields 3 when three draws really fetch three lords worth +6. Under-crediting
is how a certificate certifies a node that is actually a win. Now:

```cpp
lords_lib += std::min(lib_lord_count, fodder) * lib_lord_bonus;
```

**Behaviour-neutral on this list** (every lord is +1/+1, so `min(4,f)*1 == min(4,f)`), which is why
it can ride along with a default-off lever without needing its own A/B.

### What adoption needed -- MET 2026-09-19, and the lever is now DEFAULT ON

The stated condition was `MTG_WINLESS_AUDIT` at least as wide as the certificate's own adoption run
(99,128 nodes, violations 0), because the hook's bar is one-sided and a false positive silently
converts a win into a loss. Run on seeds held out from both this section's tuning block (901750) and
the certificate's own adoption block (31337):

* **audit** -- 48 jobs x 4 games, seeds 61000-61191, pooled, `MTG_WINLESS_AUDIT=1`:
  **1,711,293 certified-winless nodes probed, violations=0** (17x the bar), and
  `FUNGUS JOINT reduced-vs-exhaustive MISMATCHES: 0`, so the O(lib_lord_count) reduction agreed with
  the exhaustive sweep at every declining node.
* **A/B** -- `test/fungus_cert_joint_ab.sh`, 32 games, seeds 62000-62031, both arms concurrent:
  ApplyPlanDirect **85,841,868 -> 72,843,492 = 1.178x**, wall 702 s -> 589 s, certificate fire rate
  66.9% -> 70.5%, and **LABELS IDENTICAL on all 185 rows**.

The label-identity half is the load-bearing one: an unsound tightening makes a label come back
LATER, which is the quality loss this repo does not trade for wall clock. Play cannot move -- the
hook is consulted only where the search is unbounded. **Note the two blocks' 4x spread in
would-fire share (60.4% / 14.3%) did NOT predict the delivered speedup**; the realised 1.178x sits
below even the pessimistic block, which is the expected direction (a node that stops being searched
is not a node whose cost was average) and one more reason to size from a measurement rather than
from a what-if counter.

---

## THE CERTIFICATE WAS UNSOUND, and the audit found it: the anthem does not stack in the bound

**Found 2026-09-18, while trying to widen the joint budget's audit past its adoption bar.** This is
the most important result on this page, and it is a defect in code that had already SHIPPED
(`MTG_FUNGUS_CERT`, default ON since earlier the same day).

### How it surfaced

The joint-budget audit on the block it was developed against (seed 31337, 94,512 certified nodes)
reported **violations=0**. Pushing to a wider, held-out block to clear the doc's own adoption bar
(99,128 nodes) turned up **4 violations in 434,817 nodes** on seed 52000.

Isolating the two levers showed they owned none of it:

| config | probes | violations |
|---|---|---|
| **JOINT=0 POOL=0 (the SHIPPED default)** | 454,185 | **4** |
| JOINT=1 POOL=0 | 458,432 | 4 |
| JOINT=0 POOL=1 | 430,670 | 4 |
| JOINT=1 POOL=1 | 434,817 | 4 |

Identical count in every arm: **pre-existing, in shipped code**, and nothing to do with the work
that found it.

### The bug

```cpp
ba_power = std::max(ba_power, q.quest_anthem_power);   // <-- ONE copy's worth, however many are out
```

Beastmaster Ascension reads *"As long as **this** enchantment has seven or more quest counters on
it, creatures you control get +5/+5."* Two copies at threshold are two independent continuous
effects -- **+10/+10**, not +5/+5. All four violation boards held TWO Ascensions, and the arithmetic
is exact. From the dump:

```
VIOLATION t6: opp_life=15 creatures=2
bf=[Tukatongue Thallid, Simic Growth Chamber, Forest, Beastmaster Ascension, Forest,
    Thallid, Beastmaster Ascension, Forest, Doubling Season, Forest]
```

Certificate: `base_damage(2) + attackers(2) * (lords 0 + anthem 5) = 12 < 15` -> certifies "cannot
win this turn". Reality with both Ascensions online: `2 * (1 + 10) = 22 >= 15` -> **wins**.

That is an UNDER-credit, the one direction the hook's contract forbids: *"it may over-credit the
player's reach and decline, but it may never under-credit, because a false positive silently
converts a win into a loss."*

### The fix

Count the copies and test each on its OWN counters, because the counters are per-copy too -- a fresh
Ascension and one sitting on six do not come online together:

```
ready  = (battlefield copies with own_counters + gain >= threshold)
       + (hand + library copies, only if gain >= threshold -- they enter with NO counters)
anthem = ready * ba_power
```

`ba_threshold` also changed from a MAX to a MIN across copies. With one anthem card in the pool the
two are the same number, but a max-threshold would make a second anthem card harder to switch on
than it really is -- the inadmissible direction again, latent in exactly the way the `lords_lib` sum
was.

The term is now a lambda of the attacker count (`anthem_for`) because the joint budget re-evaluates
it at several attacker counts, and the two must not drift.

### Measured

| | before | after |
|---|---|---|
| audit violations (seed 52000, 48 games) | **4** | **0** |
| certified nodes probed | 454,185 | 453,526 |
| fire rate | 92.9% | **92.9%** |
| seed 31337 block: checks / fired / units | 108,650 / 92,239 / 7,143,918 | **identical** |
| seed 31337 labels | 129 rows | **identical** |

**The fix is surgical and essentially free.** It changes only the nodes that were actually wrong:
seed 31337 had no violations and is bit-identical afterwards (same checks, same fired, same
units_total, same 129 labels), while seed 52000 goes 4 -> 0 at an unchanged fire rate.

### What this says about the method

* **The narrow audit passed.** 94,512 nodes on the tuning block found nothing; 454,185 on a
  held-out block found four. The adoption bar ("at least as wide as the adoption run") is doing real
  work, and the held-out part of it is the half that mattered -- the certificate's own adoption run
  had 99,128 nodes and also missed this.
* **Every gate the deck has was green while this was live.** Smoke, regression, scenarios and the
  unit suite all pass with the bug in place, because the certificate is label-scoped and cannot
  change play -- it corrupts TRAINING LABELS silently. `MTG_WINLESS_AUDIT` is the only instrument in
  the repo that can see it. Any future certificate work should run it wide and held-out, not just
  wide.
* **Two of the three soundness defects found today are the same shape**: a per-copy quantity folded
  with `max` or summed-then-capped, where the copies really add. `ba_power` (max over Ascensions)
  and `lords_lib` (sum of bonuses capped by a draw count). When a bound aggregates over copies,
  state explicitly whether the effect stacks.

### Consequence for the value leaf

Phase A rows generated before this fix were labelled by a certificate that could declare a winnable
turn unwinnable. The 14,007 rows currently banked are therefore suspect wherever a board held two
Ascensions. The observed rate is low (4 nodes in 454k on one seed, 0 on another) and a label is only
wrong if a violation lands on a ROW's own root rather than an interior node -- but the honest
position is that the banked rows predate a soundness fix, and a regeneration on the fixed engine is
the clean route. That is the user's call, not an agent's; it is recorded here rather than acted on.

---

## ROOT CAUSE #3, 2026-09-19: the cost has MOVED -- it is the plan enumerator now, not the cascade

The 2026-09-17/18 fixes worked, and the profile that proves it also invalidates the standing plan.
Going into this session the hypothesis was "more of the same": `SimulateEndAndStartNextTurn` still
holds **twelve ungated full-battlefield walks per simulated turn-step**, each with a `LookupCached`
per permanent, for mechanics the deck does not contain (`no_max_hand_size`, `storage_land`,
`upkeep_adds_charge`, the upkeep-token block, `echo_cost`, `AdvanceSagas`,
`SpawnForbiddenOrchardTokensTurnStart`, `PerformUpkeepSacTutor`, `PerformUpkeepSlumber`,
`PerformUpkeepReorder`, `PerformUpkeepSporeCounters`, `PerformEndStepLifegainTokens`). Twelve
`deck_has_*` stamps were drafted.

**A profile stopped that before it was built.** Two slow census games (`--seed 1400125
--game-index 125` and `--seed 1600066 --game-index 66`, ~1.39M and 1.37M units, `build/Profile`,
`perf record -e cpu-clock -F 499`, output under `/tmp`):

| symbol | g125 self |
|---|---|
| `SolveUncached::consider` (the per-subset callback) | 12.17% |
| `ColorFeasibility::Payable` | 8.80% |
| `EnumeratePlanPositions` | 5.29% |
| `TapForCostSharedOnce` (+ its two lambdas) | 6.11% |
| `SubsetHasUnbackedEtbGift` | 3.36% |
| `ManaPool::CanPayFlat` | 2.87% |
| `SubsetHasDuplicateSacSource` | 2.84% |
| `SubsetOversubscribesSacFodder` | 2.55% |
| `SubsetPayable` | 2.33% |
| ... 8 more `Subset*` filters | ~7% |
| `CardDatabase::LookupCached` | **1.87%** |
| `RefreshDevotionCreatures` / `FireEtbWatchers` / `CardHasSubtype` | **absent** |

`LookupCached` was 14.75% before the cascade fixes and is 1.87% now; `FireEtbWatchers` (16.93%) and
`CardHasSubtype` (12.63%) have left the profile entirely. **The twelve remaining board walks are
together worth single-digit percent, and were not worth twelve gates.** Roughly **60% of a slow game
is now plan enumeration and per-subset filtering.**

### The defect class survived the move: the guard is still inside the loop

`SolveUncached::consider` (and its lockstep twin `EnumeratePlans::eval_and_push`) runs **seventeen
`Subset*` rejection filters per enumerated subset**. Every one of them is commented "inert for every
deck without X" -- but *inert* here means **returns false**, not **is skipped**. Each still walks
`sel`, dereferencing a cold `CardParams` per selected action, to rediscover that the deck has no
Aria of Flame / no splice card / no phyrexian pip. This is exactly the shape that made the Dragon
count 45% of a game, one level up the call stack.

Three fixes went in, all **byte-identical by construction** and none needing a lever:

* **`SubsetHasUnbackedEtbGift` called `RemedyActive` FIRST** -- a full battlefield walk with a
  `LookupCached` per permanent -- *before* the cheap `sel` scan that decides the answer for every
  deck but Anti-Lifegain. Once per enumerated subset, on a board reaching 364 permanents. The
  predicate is a conjunction of pure tests, so the order is free; the board walk now sits last and is
  unreachable without a gift. `DecisionUnpruned` deliberately did **not** move below the loop: it
  carries a gate-reachability probe side effect, and it is hoisted instead so the probe can only
  over-report a gate as reachable, never under-report one as dead.
* **`SubsetOversubscribesSacFodder` allocated before it early-outed.** It built a
  `vector<pair<string,int>>` (with a `std::string` copy per outlet, and a `ControlledDefByNumber`
  board scan per candidate sac action) and only then tested `outlets < 2`. A necessary-condition
  prepass now counts candidate sac actions -- no allocation, no `CardDefinition` read, no
  battlefield touch -- and returns early when there are fewer than two, since `outlets` is a strict
  subset of them. This is the one filter in the chain Fungus genuinely runs (Utopia Mycon is a
  creature-sac outlet), which is precisely why the cheap half had to come first.
* **`PerformEndStepLifegainTokens` scanned the board before reading its own intervening-if.** Every
  trigger it collects is worded "if you gained life this turn", so a turn that gained none fires
  nothing regardless of the board. The counter read is hoisted above the scan; the CR 603.4 snapshot
  property is unaffected (still one read, still before anything is created).

### What they bought, and the honest size of it

Seven tail games, both arms launched concurrently, `units` and win turn asserted identical on every
one (`test/subset_filter_ab.sh`):

| seed | units | cpu before | cpu after | speedup |
|---|---|---|---|---|
| 1600930 | 753,162 | 129.0 s | 122.1 s | 1.056x |
| 1700036 | 716,507 | 78.7 s | 75.1 s | 1.047x |
| 2000564 | 519,181 | 68.9 s | 67.1 s | 1.027x |
| 1200328 | 905,676 | 75.7 s | 72.3 s | 1.047x |
| 1700914 | 1,072,828 | 88.4 s | 84.6 s | 1.046x |
| 1300295 | 133,650 | 82.9 s | 74.9 s | 1.107x |
| 1100891 | 920,628 | 95.3 s | 94.9 s | 1.004x |
| **total** | | **618.8 s** | **591.0 s** | **1.047x** |

**Byte-identity: confirmed -- units and win turn identical on all seven games.** That is the claim
that matters; it is exact, not statistical.

**Cost: 1.047x, and the FIRST measurement of it was wrong in a way worth recording.** Run in wall
clock the same seven games came back 1.064x aggregate but with **one game going the wrong way
(0.948x)** -- on a box at loadavg 45 (a 24-worker census plus the A/B's own 14 processes). Switching
the currency to `task-clock:u` (process CPU time, which does not charge a run for the time it sat
descheduled) put **all seven games on the right side**, 1.004x to 1.107x, and moved the aggregate
only slightly, to 1.047x. The aggregate was roughly right by luck; the per-game signal -- the part
that tells you whether the change ever hurts -- was pure scheduler noise. This is the same
correction `test/etb_gate_ab.sh` already documents, and it should have been the currency from the
start.

### Where the remaining headroom is

This is a ~4.7% fix inside a ~60% region, and the region is now the whole game.

Worth noting what it does **not** touch: `ColorFeasibility::Payable` (8.80%) and `CanPayFlat`
(2.87%) are genuine payability work on subsets that really are candidates. Those need either
memoisation or fewer subsets -- and *fewer subsets* is what the spore pool below actually delivers.

## THE GENERAL FORM: `SubsetFilterPre`, and it is worth 1.136x

The three fixes above each short-circuited ONE filter. The generalisation is to stop calling a
filter at all when it cannot possibly fire, and it is worth almost three times as much.

**The summary.** `SubsetFilterPre` is seventeen bools, one per filter (or per filter family),
computed in ONE pass over `cands` before the walk starts, at both sites --
`SolveUncached` (just after the `any_ritual` / `any_rock` scans it sits beside) and
`EnumeratePlans` (after both aura injectors have appended, because the summary's whole claim is
that it saw every member of `cands`). Each call site becomes `if (pre.<bit> && Filter(...))`.

**Why it is sound, in one sentence:** `sel` holds indices INTO `cands`, `cands` is fixed for the
whole enumeration, and every filter's `true` requires at least one SELECTED candidate carrying some
property -- so if no candidate anywhere in `cands` carries it, the filter's answer is a foregone
`false`. Each bit is deliberately WEAKER than its filter (it reads only `Action` fields -- never the
board, never pair structure, never a `CardDefinition` the filter would re-resolve), so the summary
can only err toward RUNNING a filter that would have returned false. Defaults are all TRUE, i.e.
"run every filter", so a default-constructed summary is exactly the old behaviour.

**Two details that are easy to get wrong, both handled:**

* `RenumberFoldOrds()` mutates `cands` AFTER the summary is built -- but it only ever CLEARS an
  `equiv_tag` or renumbers ords inside an existing class, never creates a nonzero tag. The
  `dup_source` bit reads `equiv_tag != 0`, so building early is a superset of the post-renumber
  truth: the safe direction.
* Three default-off diagnostics count filter ENTRIES rather than outcomes -- the gate-reachability
  probe (`DecisionUnpruned` fires inside two of these filters), `strandedstats::g_calls`, and
  `bfcensus::g_fold_guard_seen`. To them a skipped call is an entry that vanished, and the probe is
  the sharp one: a gate with no live callsite gets dropped from a sweep as provably dead. So when
  any of the three is armed the summary comes back all-true. This is what `GateProbeArmed()` (new,
  in `DecisionProviders.h`) exists for.

**Two more hoists in the same change**, both of the same shape -- work that is constant for an
enumeration being redone per subset:

* `ResolveProvider(state).PrunesAcceleratorWithoutPayoff()` is a VIRTUAL call on a per-deck provider
  and was dispatched once per enumerated subset at both sites. Hoisted to a `const bool`.
* `VerifyFoldRecoverable()`'s own first line is `if (!FoldVerifyOn() ...) return;` -- but reaching
  that line still meant a call into a large un-inlinable function plus a thread-local read, once per
  fold REJECTION. On Fungus, where interchangeable sources make every enumeration fold, the two
  constprop clones of that no-op were **1.29% of a slow game**. The flag now gates the call site.

**Measured: 1.136x CPU time** over the same seven tail games, every game positive
(1.066x-1.217x), units AND win turn identical on all seven:

| seed | units | cpu before | cpu after | speedup |
|---|---|---|---|---|
| 1100891 | 920,628 | 61.1 s | 57.3 s | 1.066x |
| 1200328 | 901,945 | 49.0 s | 42.4 s | 1.157x |
| 1300295 | 133,650 | 54.7 s | 47.0 s | 1.163x |
| 1600930 | 753,162 | 81.2 s | 66.8 s | **1.217x** |
| 1700036 | 716,507 | 53.0 s | 45.8 s | 1.157x |
| 1700914 | 1,072,828 | 58.1 s | 54.2 s | 1.071x |
| 2000564 | 519,181 | 46.6 s | 41.7 s | 1.116x |
| **total** | | **403.7 s** | **355.2 s** | **1.136x** |

## ROUND 3: the same defect one level down, and the first thing measurement REFUSED

With the bits in, the three filters left on the Fungus profile were exactly the three the deck can
genuinely trip -- no bit can skip them, because Utopia Mycon really is a creature-sac outlet:
`SubsetHasDuplicateSacSource` 7.29%, `SubsetWastesCreatureSacMana` 4.87%,
`SubsetOversubscribesSacFodder` 3.96%.

**Two of the three still had a battlefield walk INSIDE the subset loop.** Both resolved a selected
action's `sac_source_id` to its controlled `CardDefinition` (a board scan plus a `LookupCached`)
once per enumerated subset, to answer a question that cannot change while the enumeration runs --
the board is frozen. `SubsetFilterPre::sac_src_def` resolves it once per candidate, and
`board_persist` hoists the same filter's persist scan for the same reason. `SubsetWastesCreatureSacMana`
also got the necessary-condition prepass its sibling already had: it summed a `ManaValue` per
selected action before learning whether the subset contained a sac-for-mana at all.

**And one idea was built, measured, and thrown away.** `SubsetHasDuplicateSacSource` is eight
independent clauses; a clause mask (which of them has a candidate that could fire it) is sound by
the same necessary-condition argument and looked obviously worth it at 7.29%. Measured: **7.29% ->
7.30%, i.e. nothing.** Fungus trips the live clauses, and the dead ones cost a predicted branch on
an already-loaded field. It was reverted rather than shipped -- a no-op abstraction threaded through
a hot correctness-critical guard is exactly the complexity that should not land.

**What round 3 is worth: about 1%, and the profile is the primary evidence, not the A/B.**

| evidence | before | after |
|---|---|---|
| `SubsetOversubscribesSacFodder` (profile share) | 3.96% | **2.33%** |
| `SubsetWastesCreatureSacMana` (profile share) | 4.87% | **4.55%** |
| A/B, 7 games, 14 processes on 24 cores | | **1.009x** (2 of 7 negative) |
| A/B, 16 games, 32 processes on 24 cores | | **1.008x** (6 of 16 negative) |

All three agree on ~1%, and the profile -- one process, quiet box -- is the only one of them that
can resolve an effect that small.

**OVERSUBSCRIPTION WIDENS THE PER-GAME SPREAD EVEN IN `task-clock`, which is a second measurement
lesson on top of the first.** At 14 processes on 24 cores the per-game ratios sit in a tight band
(round 2: 1.066x-1.217x; round 3: 0.998x-1.035x). Running 32 processes on the same 24 cores blew
that band out to **0.943x-1.080x** for the identical change. `task-clock` fixes the scheduler
charging a process for time it sat DESCHEDULED; it does not fix a contended core doing less work per
on-CPU second (shared cache, memory bandwidth, SMT). So: keep the concurrent-arms design, and keep
the arm count at or under the core count -- otherwise the harness stops being able to see a small
effect at all, which is precisely when you most need it to.

### A unit count is NOT a cross-run fingerprint, and this run is the evidence

Seed 1200328 reads 901,945 units in the table above and read **905,676** in the 1.047x table
earlier on the same day. Same game, same `--threads 1`, one game per process. Tested directly:
the older binary reproduces the *quiet-box* number (so it is not the code), the pre-rebase
`cards.json` reproduces it too (so it is not the card data), fourteen concurrent copies of the game
all report it (so it is not self-concurrency), and `MTG_MEM_BUDGET_MB` at 2000 and 8000 both report
it (so it is not cache sizing). The one thing true of the earlier run and nothing since: a 24-worker
census batch was sharing the box.

Not reproduced on demand, so no mechanism is claimed here. What matters is the consequence, and it
is recorded in `test/subset_filter_ab.sh`'s header: **units are comparable between the two ARMS of
one paired run -- which is exactly what that harness asserts, because it launches both arms
together -- and are NOT comparable across runs taken under different box conditions.** Note this
sits outside the model in `docs/design/batch-run-to-run-nondeterminism.md`, which attributes
divergence to thread-carried state growing with run position and reports `--threads 1` as 0/50 on
units; a one-game process has no predecessor at all.

## The spore-source pool, measured (2026-09-19): 24,000-game paired census

`MTG_FUNGUS_SPORE_POOL` shipped default OFF and had never been measured at play settings. Twelve
blocks x 1000 games x {poolOFF, poolON}, d5/20 virtual-ms, one pooled batch (24/24 workers
throughout), read with `test/fungus_slow_census.py`:

(Numbers below are the FINAL read, after all 24,000 games landed. An earlier revision of this
section quoted a partial read taken while ~480 games were still draining -- 11,521 pairs, wall tail
12.98 h -> 5.50 h, ms/unit 55.2x -> 38.9x. The ratios barely moved; the absolute tail hours did,
because the stragglers are by definition the most expensive games and they were missing from both
arms unevenly. Quote the table below, not that one.)

| | poolOFF | poolON |
|---|---|---|
| avg win turn | 5.5875 (n=12,000) | **5.5861** (n=12,000) |
| total units | 1,419,037,161 | 1,356,113,905 |
| median units | 38,910 | 38,113 |
| p99 units | 1,178,171 | **1,101,086** |
| max units (one game) | 3,863,768 | **2,520,642** |
| wall tail >= 30 s | 538 games (4.5%), **17.69 h** | 304 games (2.5%), **8.11 h** |
| ms/unit vs the 0.00111 contract | 65.8x | **44.7x** |

Paired over all 12,000 games: **1.046x in units**, cheaper on 7,154 games, more expensive on 516,
equal on 4,330. The tail is where it pays -- the >=30 s wall tail is cut to 46% of its hours, and the
single worst game in the census drops from 3.86M units to 2.52M.

**It is not play-neutral**: win turn differs on 13 of 12,000 paired games. The average moves the
right way (5.5861 vs 5.5875, lower is better) but that difference is far inside the noise of a
12-block sample, so the honest statement is *play-indifferent on average, cheaper in the tail*.
Because it is a provider-owned narrowing rather than a reordering, adopting it is the
`heuristic-optimization.md` flow -- this census is the train half, and a held-out confirm on fresh
seeds is still owed.

**Do not compare this census's wall tail to the earlier one** (`logs/leaf_tiebreak/check.out`: base
9.75 h, leaf 9.14 h). The two ran under different contention and the older one recorded no units, so
there is no common currency between them. Within this census both arms shared the box, which is what
makes the poolOFF-vs-poolON comparison sound.

---

# WHY THE PHASE-A TAIL GAMES TAKE FIVE HOURS (2026-09-19)

The value-leaf run for this deck has never finished. Its last attempt died at ~7 h with
**3 of 24 workers busy (12%)**, one game at **6.91 h**. The question this section answers is the
user's: *what makes that game so slow, and what can we do about it?*

## The four tail games, named

`logs/vlq_fungus/rows.batch.log` carries the repro line for every game over the slow threshold. The
four that owned the makespan:

| repro | wall | win turn |
|---|---|---|
| `--seed 900738 --game-index 238` | 19,968 s (5.55 h) | 7 |
| `--seed 901762 --game-index 12` | 19,870 s (5.52 h) | 8 |
| `--seed 901839 --game-index 89` | 17,408 s (4.84 h) | 8 |
| `--seed 900915 --game-index 165` | 14,352 s (3.99 h) | 6 |

**They are the LONG games.** Win turns 7/8/8/6 against a deck average of 5.52-5.66. That is the
whole correlation: a game that wins late has more real turns to label, and each of its labels has a
deeper horizon left to refute.

For scale, the *worst* game in the 24,000-game PLAY census is seed 1700120 at 3.86 M units / 226 s.
The label path's worst is **~88x that**. The gap is not the deck getting harder; it is what phase A
asks for.

## What phase A asks for

`scripts/valueleaf.sh` phase_rows runs the binary with `MTG_DUMP_VALUE_ROWS`, `MTG_EVAL_ROWS_K=3`,
`MTG_EVAL_ROWS_ROLLOUT=0`. In `AIEngine.cpp` that means: at **every real pre-combat main**, inline
and synchronously, run `EnumerateEarliestWins` **K=3 times** under reshuffled libraries, each an
**UNBOUNDED exact search to the turn-8 cap**. Play is budgeted (d5/b20); the label is not budgeted
at all. So an 8-turn game pays ~24 unbounded searches where play paid 8 budgeted ones.

## Where the time actually goes, measured on the straggler

`--seed 901762 --game-index 12` re-run on HEAD `f2431f77` with `MTG_DECISION_PROGRESS=1
MTG_WINLESS_STATS=1 MTG_WINLESS_STATS_EVERY=60`:

```
[dprog] t1 LABEL ms=376 work=43039            <- turn 1's whole label: 376 ms
=== LABEL WORK: ApplyPlanDirect calls=19238336 | ladder-pass=122 fsw-plans=19184254 ===
=== WINLESS CERT[m1]: checks=23238 fired=17932 (77.2%) ===
=== WINLESS CERT scope: plans all=909490 label=902583 edge=805007 ===
=== WINLESS RESIDUAL: 5303 of 23238 edge nodes (22.8%) resolved by neither ===
=== [progress] at t7 cut=7 candidate 50/340 (max seen 1192) ===
```

Read it in order:

1. **One turn is not the problem; one PASS is.** Turn 1's label costs 376 ms. The next turn's label
   was still running, alone, after minutes -- 19.2 M plan applications and climbing.
2. **99.7% of that work is `fsw-plans`** (19,184,254 of 19,238,336). Those are plans applied one at
   a time at a **horizon-edge node** -- a node whose only question is *"can I win THIS turn?"*.
   `ladder-pass=122` says this is not a ladder climbing many rungs; it is a handful of passes
   grinding an enormous plan set.
3. **The node fan-out is 300-1,192 candidates**, and the search walks them individually.
4. **The certificate already earns its keep**: it refutes 77-84% of edge nodes outright. This is
   `FungusProvider` (e413fff8), and note it was ALREADY IN the frozen binary `8ac753c4` -- so the
   5.5-hour games are what remains *after* a 1.90x win, not before it.
5. **The residual 22.8% is the cost**, because the surviving nodes are the wide ones.

## The residual has ONE cause, and it is nearly pure

`FungusCertReasonReport` on the same run:

```
FUNGUS WINLESS CERT reasons: fired=21142 combat-lethal=10671
combat-lethal breakdown: base-lethal=2 lord-board=28 lord-library=9982
                         anthem-battlefield=1 anthem-library-only=658
```

**`lord-library` is 93.5% of every decline.** The certificate cannot certify the turn winless
because it must assume a Sporecrown Thallid could be DRAWN out of the library and pump the team to
lethal. Everything else -- lords on board, anthems, base lethal -- is rounding error.

So the cost structure of a five-hour game is, end to end:

> long game -> many real turns -> x3 reshuffles -> unbounded ladder -> thousands of horizon-edge
> nodes -> ~23% of them survive the certificate, essentially all for `lord-library` -> each survivor
> applies 300-1,192 plans one at a time.

## Why the existing state-dedup does not help

`MTG_LABEL_LADDER_DEDUP` collapses candidates with identical post-apply states -- the user's own
"which states can be reached" doctrine. On this game it reports `searched=93 inherited=0`,
**0.0%**. That is not a bug: it dedups at the LADDER ROOT, and a token deck's root candidates
genuinely have distinct boards. The 99.7% of work sits at the horizon EDGE, where no state
collapse is applied at all. That gap is exactly item 4 of
`label-work-bounding-by-reachable-states.md`, and it remains unbuilt.

`LABEL EDGE TAIL: elided=913419 continuations` shows the one edge-side optimisation that does fire
-- but it elides the *continuation* after the apply, not the apply itself, and the apply is the cost.

## What to do about it

Ordered by evidence behind them, not by appeal.

### 1. `MTG_FUNGUS_CERT_JOINT` -- built, and its adoption bar is now MET

The joint Saproling budget prices the token pool once instead of letting it both attack and be
eaten for cards. It bites directly on the lord terms, which is where 93.5% of the declines are. It
was held at default OFF for one stated reason: *"`MTG_WINLESS_AUDIT` at least as wide as the
certificate's own adoption run (99,128 nodes, violations 0)"*.

Run 2026-09-19 -- 48 jobs x 4 games, held-out seeds 61000-61191, pooled, `MTG_WINLESS_AUDIT=1`:

```
=== WINLESS AUDIT: probed 1711293 certified-winless nodes with the canonical go-off, violations=0 ===
=== FUNGUS JOINT reduced-vs-exhaustive MISMATCHES: 0 (must be 0) ===
```

**1,711,293 nodes, zero violations -- 17x the stated bar**, and the O(lib_lord_count) reduction
agreed with the exhaustive sweep at every declining node. The condition the doc set for adoption is
satisfied on its own terms.

### 2. Levers that exist but were NOT in the frozen binary

The banked rows were produced at `8ac753c4`. Since then: this session's three filter commits
(~1.20x on tail games), and `MTG_FUNGUS_SPORE_POOL` (**1.47x units / 2.60x wall on the LABEL
path**, default OFF, never yet used in a generation). The pool is the single biggest available
multiplier on phase A -- and it is not play-neutral (13 of 12,000 census win turns move), so turning
it on for a generation changes the play the leaf is fitted to. That is a decision, not a free win.

### 3. The one that would actually change the shape: bound the library term by the TOP of the library

This is a proposal, not a measurement. `lords_lib` is computed by walking the WHOLE library:

```cpp
for (const Card& c : ap.library) { ... if (IsLordPermanent(*d)) { ++lib_lord_count; ... } }
lords_lib += std::min(lib_lord_count, fodder) * lib_lord_bonus;
```

With 4 Sporecrowns in the list and `fodder` usually >= 4 on a token board, the `min` almost never
binds, so the bound is effectively *"all four library Sporecrowns arrive this turn"*. But draws come
off the TOP in order, and the deck cannot reorder: verified against `cards.json`, the only library
interaction in all 14 mainboard cards is Psychotrope Thallid's `sac_outlet_draw` -- **no shuffle, no
tutor, no scry, no surveil**. `library.front()` is the top (`SpellEffects.h`). So only the top
`max_draws` cards are reachable this turn, and on most boards none of them is a Sporecrown.

**The soundness precondition, which is the whole difficulty.** `fodder` counts creatures ALREADY on
the battlefield, and the comment calls it "an upper bound on DRAWS" -- but on this deck it is not
one. Two mechanisms regenerate fodder during the turn, both verified in `cards.json`:

* **Spore counters.** Every Thallid carries `spore_saproling_cost: 3`, `spore_creates_tokens: 1`, so
  a creature sitting on 3+ counters makes a fresh Saproling without consuming a body.
* **Tukatongue Thallid replaces itself.** `dies_watch_includes_self: true`,
  `dies_trigger_creates_tokens: 1` -- sacrificing it to the draw outlet returns a Saproling, so that
  draw cost NO net fodder. With Doubling Season (`doubles_tokens: true`) it returns *two*, and the
  pool grows.

Today's bound does not depend on `fodder` being a true ceiling -- with 4 Sporecrowns and `fodder`
usually >= 4 the `min` does not bind -- so this is latent rather than live. **A top-of-library bound
WOULD depend on it**: a lord sitting at position `max_draws + 1` that is nevertheless drawable is an
UNDER-credit, the one direction this hook may never take. So the implementation is not
`min(library.size(), fodder)`; it is a deliberately generous

```
max_draws = fodder
          + SUM over permanents floor(spore_counters / spore_saproling_cost) * spore_creates_tokens * (doubling ? 2 : 1)
          + (dies-token creatures on board) * dies_trigger_creates_tokens * (doubling ? 2 : 1)
```

every term rounded UP, and then `top_n = min(library.size(), max_draws)`. Mana is a further real
bound on draws (each activation costs {1}) and is deliberately ignored here, exactly as the current
code ignores it -- ignoring a limit is the safe direction.

Even generously, `max_draws` lands in the high single digits against a ~30-card library, so the
term collapses from "all four Sporecrowns arrive" to "is one of the top ~8 cards a Sporecrown" --
usually no. Given `lord-library` is 93.5% of all declines, this is the largest identified lever on
the tail. It must clear a `MTG_WINLESS_AUDIT` at least as wide as the 1.7 M-node run above before it
ships, and it should ship behind its own flag so a bisect can separate it from the joint budget.

### 4. The makespan is a separate problem, and no per-unit win fixes it

Even at 2x, a queue whose tail is one 5.5-hour game still ends with 3 of 24 cores busy. The
structural observation is that **a phase-A game is not an atomic unit of work**: it is ~8 real turns
x K=3 independent label searches, computed inline in one thread because `EmitEvalRows` is called
synchronously from the play loop. The play half is negligible (turn 1's real decision: 115 ms).
Sharding the queue at the (game, turn) level rather than the game level would let a monster game's
24 independent labels spread across the box. That is a driver+engine change, not a flag, and it is
the only item here that attacks 12% utilisation rather than per-game cost.

## A correction to this document's own record

Section 5 of `label-work-bounding-by-reachable-states.md` states the certificate fires **0 times**
for Fungus because the deck routes to `GenericProvider`. That was true when written and is now
**stale**: `FungusProvider` (e413fff8, 2026-09-18) implements `ProvenWinlessThisTurn`, and the
measured fire rate on the straggler is **77-84%**. The 93.7%-of-plans-at-edge-nodes figure quoted
there was measured on the pre-certificate binary; the post-certificate equivalent is 88.5%
(805,007 of 909,490), because the certificate removes whole nodes but the survivors are the widest.

---

## WHY THERE ARE SO MANY PLANS IN THE FIRST PLACE (2026-09-19)

The sections above establish that the cost is plans applied one at a time at horizon-edge nodes.
This one asks where the plans come from. Measured, not reasoned: `MTG_FS_ROOT_DUMP=6` on the
straggler (`--seed 901762 --game-index 12`), play path.

**Read the dump carefully -- 320 dumped lines are ELEVEN separate enumerations, not one node.**
(Boundaries are visible because `pre` is value-ordered, so `val` rising marks a new enumeration.)
The largest single enumeration is **56 plans**. Quoting 320 as one node's fan-out would be wrong.

**Those 56 plans are six decisions.** The complete set of distinct spell contents at that node:

```
Utopia Mycon + Thallid Shell-Dweller(x1) + Thallid Shell-Dweller(x1)
Utopia Mycon + Thallid Shell-Dweller(x1)
Utopia Mycon
Thallid Shell-Dweller(x1) + Thallid Shell-Dweller(x1)
Thallid Shell-Dweller(x1)
(nothing)
```

| axis | factor | running | what it is |
|---|---|---|---|
| spell content | 6 | 6 | the actual decision |
| land timing | x2 | 12 | `land=Forest` vs `land=(defer)` |
| breakpoint continuation | x2.5 | 30 | `bp_choice` = -1 / 0 / 1 / 2097152, one variant per continuation |
| identical in all printed fields | x1.9 | 56 | 56 plans, 30 distinct lines |

**A 9.3x inflation over the real choice set**, and every one of the 56 is applied to a board at a
node asking only "does any of these win this turn?".

**The x1.9 is NOT established as waste.** `FsPlanText` does not print WHICH interchangeable
permanent a plan used -- which Thallid activated, which land was tapped, which token was
sacrificed -- so those may be genuinely distinct plans separated by a choice between objects that
are identical in every respect that matters. That is precisely the class `MTG_FUNGUS_SPORE_POOL`
folds, and it is why that lever is worth 1.47x units / 2.60x wall on the label path. Anyone
attacking this number should start by re-dumping with the source identity included rather than
assuming the duplicates are free.

**Why the label path sees 300-1,192 and not 56.** Same structure, wider board. By turns 7-8 there
are many more Saprolings and many more Thallids sitting on 3+ spore counters, so the base set grows
and the same multipliers ride on top. The 300-1,192 figures come from the `[progress]` instrument
during the label search (`candidate 50/340 (max seen 1192)`), not from this play-path dump.

### How this composes with the certificate

The two attack different factors of the same product, which is why neither alone is sufficient:

* the **certificate** (the adopted joint budget, and the proposed top-of-library bound) stops the
  search ENTERING the node -- all 56 go unapplied;
* the **spore pool** shrinks the list inside nodes the certificate could not refute.

The land-defer x2 and breakpoint x2.5 are untouched by either, and both are *searched rather than
narrowed* by deliberate design (`bp_choice` "pins a breakpoint continuation ... searched rather
than narrowed"). On a deck whose real decision set is six wide they are most of the plan count, so
they are the obvious next question -- but changing them is a QUALITY decision the repo has already
taken once, not an oversight to clean up.

---

# THE SPORE POOL, MEASURED ON THE MULLIGAN PATH (2026-09-22): 2.08x

The owed held-out confirm for `MTG_FUNGUS_SPORE_POOL` arrived from an unplanned direction: the
mulligan `recommend` scout, run twice on a quiet box, pool OFF then ON. This is a genuinely held-out
workload -- the lever was developed against the LABEL path and the play census, never against keep
rollouts -- and it is the workload that actually gated a decision.

| | pool OFF | pool ON | ratio |
|---|---|---|---|
| projected COMPLETE (full bottom, R40) | 33.7 h | **16.2 h** | 2.08x |
| projected FAST (adaptive, R30) | 16.8 h | **8.1 h** | 2.07x |
| the scout's own floor-pass wall | 3,030 s | **1,458 s** | 2.08x |

Three independent measures agreeing to within 1% is the useful part: the projection is derived from
measured rollout rates, so its agreeing with the scout's own wall says the model and the clock see
the same thing.

## Why the mulligan path is where this lever pays most

`decks/Fungus/Fungus.keepmodel.exhaustive.raw.json.slow.log`, pool OFF: **250 slow keep-rollouts,
6.58 core-h, worst single rollout 1,324 s -- against a 3 ms budget (441,000x over).**

| card | in N of 250 | % |
|---|---|---|
| **Utopia Mycon** | 214 | **86%** |
| Psychotrope Thallid | 130 | 52% |
| Doubling Season | 123 | 49% |
| Thallid Shell-Dweller | 119 | 48% |
| Sporesower Thallid | 114 | 46% |

Interchangeable spore BODIES per slow hand -- the `2^n` axis the pool collapses to `n+1`:

| bodies | hands | share | selections |
|---|---|---|---|
| 3 | 47 | 18.8% | 8 -> 4 |
| 4 | 77 | 30.8% | 16 -> 5 |
| 5 | 59 | 23.6% | 32 -> 6 |
| 6 | 30 | 12.0% | 64 -> 7 |
| 7 | 5 | 2.0% | 128 -> 8 |

**68.4% of slow keep-rollouts hold four or more spore bodies.** A keep rollout starts from a kept
seven, so spore-dense hands are over-represented relative to a mid-game board -- which is exactly why
this path pays more than play (2.08x) and about as much as the label path (2.60x).

Doubling Season is in half of them, compounding the COUNT axis on top of the SOURCE axis. The pool
collapses only the source; the count axis is preserved deliberately, and this data is the reason that
distinction matters rather than being pedantry.

## Status

Still DEFAULT OFF. The `heuristic-optimization.md` adoption decision is the user's and has not been
made. What the evidence now covers: cost on three separate workloads (label 2.60x, play tail 46% of
hours, mulligan 2.08x) and play-neutrality on two (400/400 identical digests at d5/b20; 13 of 12,000
paired win turns differ in the 24,000-game census, average moving the right way but inside noise).

The 2026-09-22 `fast` generation was run WITH the lever on, because at 16.8 h the pool-OFF gen did
not fit the window at all. If the lever is ultimately rejected, that table was fitted under an engine
0.1% different from the one that ships -- far inside the table's own noise, but it is a real caveat
and it is recorded here rather than discovered later.

---

# Round 5 (2026-09-22): the payability block, and the devour axis under it

**Trigger.** The USER, on being shown that `ColorFeasibility::Payable` was 19.9% of a slow Fungus
game: *"Payability is the limitation on a mono-colored deck? That suggests we have a bug of some
sort."* That read was right, and it is the reason this round exists.

Fungus is mono-green. Every spell in the list is generic + `{G}`; the only sources are Forest,
Simic Growth Chamber, Wild Growth and Utopia Mycon. There is no colour to get wrong.

## What the funnel said

`MTG_ENUM_STATS=1` on the worst game in the regression tier
(`--seed 2085 --game-index 83 --depth 3 --budget-ms 10`):

```
entered                    : 453,588,641
  passed subset rules      : 413,362,363
  passed flat mana         : 413,362,363     <- 0 rejections
  passed SubsetPayable     : 413,362,363     <- 0 rejections
  passed ColorFeasibility  : 413,362,346     <- 17 rejections, out of 413 MILLION
```

Against a `perf` profile in which the payability family -- `Payable` 19.9%, `CanPayFlat` 5.5%,
`SubsetPayable` 5.5%, `SequencedRitualCredit` 3.2%, `AddCostCarryingHybrids` 3.0%,
`CreditFixedColorSac` 1.2%, `CanPay` 1.0%, `PoolCredit` 0.8% -- was **36.6% of the game**.

Roughly a third of the run, to reject seventeen subsets.

## Defect 1: `usable` is armed by the SOURCE side alone

`BuildColorFeasibility` sets `usable` from `has_multi` -- *does the board hold a multi-colour
source?* Fungus holds two (Simic Growth Chamber, Utopia Mycon's "any colour"), so the test arms.
Nothing ever asks the other half of the question: **can the DEMAND side make it bite?** The test
exists to catch two differently-coloured pips competing for one dual. With every pip the same
colour there is no competition to find, and the Hall scan walks all 31 colour subsets to discover
that.

**Fix (shipped, byte-identical).** Only a **union of demand masks** can bind. For any set `S`, let
`S'` be the union of the masks contained in `S`: every mask inside `S` is inside `S'` and vice
versa, so `need(S') == need(S)` exactly, while `have()` is non-decreasing in `S` -- adding a colour
adds `cover[]` and `cred[]`, and the producer deduction can grow by at most the cover gain
(`max(0,x+d) - max(0,x) <= d`), leaving the credit gain. So `need(S) > have(S)` implies
`need(S') > have(S')`, and scanning the unions returns the same verdict on the same subsets.

A mono-colour demand set has `ndm == 1`: **one check instead of 31.** Gated at `ndm <= 3` so the
closure is never larger than the scan it replaces; wider demand sets keep the flat walk.

## Defect 2: the test re-reads a 384-byte struct per candidate per subset

The scan fix alone bought only 4-6%, which said the scan was not where the time went. `perf
annotate` put **14% of the whole function on one instruction** -- the branch on `a.kind`, i.e. the
load. `sizeof(Action) == 384` (`shl $0x7` + `lea (%rax,%rax,2)` in the addressing), and `Payable`
strides that vector at a random index, touching three cache lines per candidate, 453 million times.
It is a memory problem, not an arithmetic one.

**Fix (shipped, byte-identical).** `ColorDemandIndex` (`ManaPayment.h`) hoists everything `Payable`
reads off an `Action` -- pips, producer mana value, vial/noncreature/producer flags -- into parallel
arrays of 4/4/1 bytes, built once per enumeration next to `BuildColorFeasibility`. For a
30-candidate board the whole index is ~270 bytes and stays resident. The `uniform` case (all
coloured demand in one shared colour, no hybrids -- every mono-colour deck, and a two-colour one
whose cheap half is on board) then reduces the entire test to two tight array walks and one
comparison. When it does not hold the index is left unset and the general path runs unchanged.

## Measured

Three slowest regression games, single-threaded, idle box, paired:

| game | before | + scan fix | + demand hoist | net |
|---|---|---|---|---|
| `seed 2085 gi83` | 188.11 s | 180.45 s | **137.09 s** | **-27.1%** |
| `seed 2031 gi29` | 69.16 s | 64.68 s | **46.11 s** | **-33.3%** |
| `seed 3095 gi92` | 28.71 s | 28.30 s | **22.92 s** | **-20.2%** |

Byte-identical everywhere: unit counts unchanged per game, and smoke **90/0 ALL PASS with 0 configs
changed and 0 play-changed** across all 22 decks. Smoke makespan 112s -> 103s, so the gain is not
Fungus-only -- any deck whose demand set is mono-colour on a given board takes the fast path.

## THE REMAINING MULTIPLIER: the devour axis

An `MTG_ODOM_SHAPE` instrument (temporary, reverted) over gi83 found the cost is one repeated
odometer shape:

```
[odom] pos=16384 m=26 num_ind=3 ngroups=8 sizes=1,1,1,1,15,1,1,1
 grp4: Mycoloth x15, all (kind=CastFromHand, tag0, ord0)
```

48,908 calls of this shape carry **458.7M of the game's 508M odometer positions**. The group of 15
is **Mycoloth's devour axis** -- one candidate per number of creatures devoured -- and that digit
multiplies the whole rest of the odometer by 16. Strip it to cast/don't-cast and the shape is 2,048
positions instead of 16,384.

**This explains the shape of the tail that nothing else did.** gi29 and gi83 run near-identical
d3 unit counts (428,070 vs 423,477) for 2.2x the wall time -- gi83's cost is per-node, not
node-count. The devour axis is exactly a per-node cost that **scales with board width**, and Fungus
is a deck whose whole plan is to go wide. That is why the expensive games are the wide ones.

**It is not free to narrow, and must not be narrowed by assumption.** Devour `k` is a genuinely
distinct outcome (Mycoloth enters `(4+2k)/(4+2k)` and makes `2k` Saprolings per upkeep), the
victims come out of the same fodder pool Utopia Mycon sacrifices for mana, and Beastmaster Ascension
counts *attacking* bodies -- so "devour everything" is not obviously right even in a goldfish. This
is a search-restriction question in the sense of `heuristic-optimization.md`: it changes play, so it
needs a measured A/B on train seeds and a held-out confirm, and it belongs in a deck provider, not
the root. Two shapes worth measuring:

1. **Bound the axis by reachable states** rather than by victim count -- the `labeller-no-lossy-
   restrictions` lesson. Devouring `k` fungible Saprolings is one state per `k`, which is already
   the minimum; the question is whether the *ends and a midpoint* separate outcomes enough.
2. **Make the axis payoff-only.** Devour costs no mana, so the mana verdict is identical across all
   16 digit values -- but the fodder predicates (`SubsetOversubscribesSacFodder`,
   `SubsetWastesCreatureSacMana`) do read it, so this needs the interaction checked before any
   verdict can be cached across the digit. If it holds it is byte-identical and worth ~16x on the
   payability half of this board.

Related: `fungus-second-main-and-devour.md` (devour's *semantics* and the main-phase placement the
user steered), which is the other half of this card's story.

---

# Round 6 (2026-09-22): the MULLIGAN slow games, and a Treasure check walking a Saproling board

**Trigger.** The USER: *"let's pick up the fungus deck optimizations, especially for the slow games
in the mulligan profile. I would like to start with ones that are lossless before considering
anything else including changes related to budgeting."* So: byte-identical work only, and measured
on the mulligan path rather than the regression tier.

## The mulligan slow games are a DIFFERENT SHAPE from `gi83`, and the funnel says so

The cancelled `complete` gen left 205 streamed slow rollouts in
`decks/Fungus/Fungus.keepmodel.exhaustive.raw.json.slow.log` -- **23,172 s of rollout time, worst
single rollout 1,514 s (25 min)**. Ranked by time, **Doubling Season is in almost every one of the
top entries** (`Doubling Season x3; Forest x2; Psychotrope Thallid x1; Wild Growth x1` and the
like). That is the atom: Doubling Season doubles the Saproling count, so these are the WIDEST
boards the deck ever reaches.

`MTG_KEEP_REPLAY` replays one of them exactly (README in `test/slow_repro/`); the capture line's
`seed=` reproduces byte-for-byte, which is how every number below is paired.

The funnel (`MTG_ENUM_STATS=1`) on that rollout is **not** Round 5's funnel:

```
entered                    : 40,966,303
  passed subset rules      : 34,618,576
  passed flat mana         : 33,552,920
  passed SubsetPayable     : 33,552,920     <- 0 rejections
  passed ColorFeasibility  : 33,546,800     <- 6,120 rejections
  survivors (fully scored) : 26,751,857     <- 65% of everything entered
```

Round 5's `gi83` spent a third of the game rejecting 17 subsets out of 413 million. Here **almost
nothing is rejected at all** -- 65% of enumerated subsets are scored in full. The cost is not a
funnel that fails to cut; it is the **per-subset scoring work itself**, paid 26.7 million times.
Optimising the payability funnel further would have been optimising the wrong path, which is why
this round re-profiled instead of continuing Round 5's list.

## The defect: `FreshMintSpendableNow` walks the whole battlefield, per scored subset, on a deck with no Treasures

`perf` (Profile build, the replayed rollout):

```
11.56%  consider() lambda
 5.12%  EnumeratePlanPositions
 3.37%  FreshMintSpendableNow          <-- Fungus's list contains no Treasure at all
 3.28%  ManaPool::CanPayFlat
 2.85%  SubsetHasDuplicateSacSource
 2.60%  CardDatabase::LookupCached  (+0.93% a second clone)
 2.04%  ColorFeasibility::Payable      <-- 19.9% before Round 5
 ...
 0.97%  WidenHaveWithSubsetRocks       <-- Fungus's list contains no mana rock either
```

Per-source-line attribution puts `SpellEffects.h:18433 / 18447 / 18448` -- the loop bodies of
`CopyMagnetLive` and `HeroismCopiesLive` -- at **2.3%**, with the `CardDatabase.h:2673-2682`
`LookupCached` they each call adding **~2.8%** on top.

The mint-credit block in both subset walkers opens with a walk of `sel` for a minting candidate and
then computes

```cpp
const bool spendable = mint_exact
    ? (FreshMintSpendableNow(state, state.active_player_index) || hand_magnet || ...)
    : (!FreshHoldActive() || CopyMagnetLive(state, state.active_player_index));
if (sel_mint && minted > 0 && pool.CanPay(mint_costs) && spendable) { ... }
```

`spendable` is computed **before** anything tests `sel_mint`, and `FreshMintSpendableNow` ->
`CopyMagnetLive` / `HeroismCopiesLive` **scan the entire battlefield with a `LookupCached` per
permanent**. On a deck that mints nothing, `sel_mint` is false on every subset and that scan decides
nothing, every time.

**It costs most exactly where it can help least.** The scan is O(board width), so it is cheapest on
an empty board and most expensive on a wide one -- and a wide board is the state a token deck spends
its long games in. That is why this surfaced on the mulligan tail specifically and not in Round 5's
profile: these are the Doubling Season games.

`WidenHaveWithSubsetRocks` is the same class one order of magnitude smaller, plus a second defect:
its result feeds only the `SubsetPayable` call on the next line, which is guarded by
`!mc_hit && (mana_ok || s_rescued_color_gate)` -- so on every mana-cache hit the widen ran for a
value that was immediately discarded.

## The fix (shipped, byte-identical)

1. **`SubsetFilterPre::mint`** -- one more bit in the per-enumeration summary Round 2 built, set by
   exactly the block's own entry test (`def->params.creates_treasures > 0`). False makes `sel_mint`
   unreachable, so skipping the whole block is byte-identical by construction. Both walkers gated
   (`consider`, `eval_and_push`) -- lockstep, as their comments require. The summary's
   instruments-disarm rule carries over unchanged: armed instruments get an all-true summary and
   see every callsite they saw before.
2. **`WidenHaveWithSubsetRocks`** -- moved INSIDE its guard, and given the callers' existing
   `any_rock` per-enumeration scan as `any_rock_cand`. With no rock candidate every `rm.Total() <=
   0`, so the loop's verdict is a foregone `return have`.

Note which shape the fix takes: **not** a lazy `spendable`. A lazy `spendable` would have removed
the board scan but left the per-subset `sel` walk. The `pre` bit removes both, and it removes them
for every deck in the repo that holds no Treasure, not just this one.

## Measured

`MTG_KEEP_REPLAY` of `Doubling Season x1; Simic Growth Chamber x1; Thallid x1; Thallid Shell-Dweller
x2; Utopia Mycon x2` (draw, r=1), interleaved base/new x3 on a quiet box:

| arm | run 1 | run 2 | run 3 | mean |
|---|---|---|---|---|
| base | 16,318 ms | 16,296 ms | 16,220 ms | 16,278 ms |
| new  | 14,018 ms | 13,854 ms | 13,783 ms | **13,885 ms** |

**-14.7%.** Identity: `win_turn=7` on all six runs, and `enum-memo hits=31 misses=14556` /
`solve-memo hits=36499 misses=238460 clears=3` identical in every arm -- same nodes, same memo
traffic, same answer. Smoke **93 passed / 0 failed, 0 configs changed, 0 play-changed** across all
22 decks, Mirrorwing (the deck that actually mints) included. `test/scenarios.sh` 103/103.

The win is larger than the 3.37%+0.97% the flat profile attributes to the two symbols, because
deleting `CopyMagnetLive` also deletes its `LookupCached` calls and the cache pressure of walking
the battlefield vector once per scored subset.

## Method note worth keeping

The first timing of this rollout came back at **37.4 s** for what the paired A/B then measured at
16.3 s on the same binary and the same seed -- a 2.3x swing with nothing changed but the hour. This
box is a WSL2 guest and its `loadavg` is the HOST's, so an un-paired before/after here would have
"measured" anything you liked. Every number above is base-and-new interleaved, in one command, on
one quiet box.

## Two things measurement REFUSED in this round

**1. The win is not one number -- it scales with BOARD WIDTH, and ACROSS THE TAIL IT IS -6.1%.**
The -14.7% probe is the best hand, not the typical one. Eight captured slow rollouts spanning the
tail, replayed base/new **strictly serially on a quiet box** (load ~1.1, one process at a time):

```
gen=  214725  base=  92499  new=  90231    -2.5%
gen=  132604  base=  49760  new=  50084    +0.7%
gen=  109667  base=  39742  new=  40495    +1.9%
gen=  105091  base=  53930  new=  44298   -17.9%
gen=   91713  base=  40813  new=  38068    -6.7%
gen=   84083  base=  43557  new=  39361    -9.6%
gen=   75405  base=  24935  new=  25188    +1.0%
gen=   63483  base=  32055  new=  26648   -16.9%
TOTAL          base= 377291  new= 354373    -6.1%
```

**-6.1% is the number to quote for the gen**, because a gen's cost is the SUM and the sum is what
the makespan sees. The spread is the real finding: -18% to -17% on three hands and nothing
distinguishable from zero on three others. A second hand measured separately (`Doubling Season x1;
Essence Warden x1; Forest x1; Thallid Shell-Dweller x1; Tukatongue Thallid x1; Wild Growth x2`,
play r=0) came back -5.8%, right on the tail-wide figure.

The three small POSITIVE readings are NOT a slowdown -- a byte-identical change cannot cost time --
they are the single-run noise floor, which even on a quiet box is about +-2% (the same probe re-run
five times within one arm spanned 13,727-13,937 ms). Quote the TOTAL, or the range; never the best
hand, and never a single run per arm.

Why some hands gain nothing: the deleted work is `O(board width) x (scored subsets)`, so it pays
only where the odometer walk dominates. A rollout that spends its time in `FSLineWin` /
`ApplyPlanDirect` instead (20.5% and 12.4% inclusive on the profiled hand) barely notices.

**2. Folding `SubsetHasDuplicateSacSource`'s seven tail walks into one bought NOTHING -- reverted.**
It was the obvious next target (3.43% after the mint fix, the largest filter left) and the obvious
defect shape: seven separate `for (b = a+1; ...)` loops, each re-reading `cands[sel[b]].kind` at a
random 384-byte stride, walking the tail twice whenever `a` is both a free cast and a clause-owning
kind. The restructure -- one walk per `a`, `a`'s fields hoisted -- is byte-identical (the function
is a disjunction over pairs, so pair order cannot change the bool).

Six interleaved rounds on the quiet probe:

```
before: 13937  13875  13903  13754  13727     (min 13727)
after:  13848  13848  13814  13788  13778     (min 13778)
```

A **3-2 split with every difference under 0.7%.** The larger hand disagreed in the other direction
by 2.6%, i.e. noise both ways. **Reverted** -- churn in this file is not free, and the repo's bar is
a strict improvement.

**And a second candidate refused BEFORE it was built: the accumulation-loop SoA hoist.** The head of
`consider()` sums ~10 fields per selected candidate out of a 384-byte `Action`, 34.6 M times -- the
exact shape `ColorDemandIndex` fixed in Round 5 for `Payable`, and sitting inside the biggest
self-time symbol left (the `consider()` lambda, 11.29%). **The pattern does not transfer, and
`TurnSolver.h` says so in one look:** `Payable`'s fields were scattered across the struct, but the
valuation scalars here -- `eval`, `direct_damage`, `is_noncreature`, `card_mv`,
`vial_attack_power`, `haste_attack_power`, `haste_prowess` -- are declared CONTIGUOUSLY at
TurnSolver.h:482-493 and already share a cache line. A packed row takes the per-candidate footprint
from ~3 lines to ~2, not from 3 to 1, and it is Ir-neutral by construction so callgrind could not
adjudicate it either. **Check the target struct's LAYOUT before assuming a hoist will pay** --
"reads N scattered fields" is a claim about declaration order, and it is cheap to verify.

**What the negative result TELLS the next agent**, which is why it is written down: the filter's
3.43% is NOT the redundant tail walks. `sel` is small enough on these boards that the walks are
short and the candidate array (~26 x 384 B = 10 KB) stays warm in L1 between calls. So the cost is
the per-CALL overhead and the work that is proportional to `|sel|` itself -- which means the lever
that would actually move it is **calling it less often**, not making each call leaner. Do not retry
the stride hoist here.

**A measurement note that cost an hour:** `perf stat -e instructions:u` reports `<not supported>` on
this box -- WSL2 exposes no hardware PMU -- so there is no contention-immune counter available for
an A/B. `task-clock:u` sums every thread (the gen's discovery phase is parallel), so it does not
isolate a replayed rollout either. Wall time with many interleaved repetitions, compared on the
MINIMUM, is the only *wall-clock* instrument this box offers.

**CORRECTION, same session:** that last sentence is wrong in the way that matters, and it was
already refuted inside this repo before it was written. **`valgrind --tool=callgrind` is available
here and its Ir count is deterministic and load-immune** -- `SubsetPayableWithFilters`' own comment
in TurnSolver.cpp says so explicitly ("wall clock on this host drifted ~2x within one session, which
is enough to make a neutral change read as a 1.9x win... callgrind's Ir is deterministic and
load-immune"), and `docs/design/accelerant-ordering-and-self-funding.md`,
`analysis-EldraziDisplacerFlicker.md` and `code-pruning-and-refactor-backlog.md` all A/B with it.
The cost is ~90x wall, so it wants a SMALL probe, not a 14 s one. The right reading of this section
is therefore: no PMU, wall clock untrustworthy under host load -- **so reach for callgrind Ir**, not
for more repetitions.

**And a second one, because it produced a result that is physically impossible.** A tail sweep over
eight slow hands was first run the obvious way -- both arms of all eight hands launched at once, 16
processes on 24 cores. It reported the byte-identical change as **SLOWER on three of the eight
hands** (+0.6%, +2.0%, +3.3%) and -3.3% overall. None of that is real: each replay's discovery phase
is itself multithreaded (~1.5 CPUs), so 16 of them saturated the box, and the two arms of a pair did
not overlap in time, so they did not share conditions. Re-run strictly serially the same hands
moved by tens of percent (`Doubling Season x4; Utopia Mycon x1; Wild Growth x2`: 84.9 s parallel ->
49.2 s serial). **A parallel fan-out is the right shape for a WORK queue and the wrong shape for a
TIMING A/B** -- the repo's pooling rule is about throughput, not about measurement.

That serial re-run was then abandoned too, and this is worth knowing: it began returning a
byte-identical arm at **+68%** while `uptime` showed **load average 23 against 1.7 CPUs of guest
process**. The load is the HOST's; nothing inside the container can see what is causing it, and
nothing inside the container can measure through it. **Check `uptime` against your own `%CPU` before
trusting any wall number here** -- if the gap is large, the box is not yours and the measurement is
not real. The numbers in the table above were all taken while that gap was small.


## Two more measurements, for the next agent's shortlist

**The canonical-prefix fold is 97.2% of ALL subset-rule rejections, and it is NOT worth moving.**
`MTG_BF_CENSUS=1 MTG_ROLLOUT_STATS=1` on the probe rollout:

```
bf_foldsite greedy calls=40,966,303 from_odometer=40,966,303 with_tag=16,070,503 rejected=6,172,592
```

Total rule rejections are 40,966,303 - 34,618,576 = 6,347,727, so **every other filter in that
20-clause chain combined rejects 175,135 subsets (2.8%)**. The fold alone rejects 6.17 M -- 15.1% of
everything the odometer generates is an arrangement of interchangeable sources that is provably a
duplicate. `IndependentAccelPrefixViolated` is the precedent for collapsing such arrangements at the
odometer instead (2^L -> L+1), and unlike that one this collapse would be byte-identical, because
the fold ALREADY rejects exactly the non-prefix set.

**It is still not worth building**, and the reason is the ordering of the chain: on Fungus every
filter ahead of the fold is switched off by a `SubsetFilterPre` bit, so the fold is effectively the
FIRST test that runs. A skipped position therefore saves only the cheap head of `consider()` -- the
`sel` push_backs, the sort, one `SubsetHasDuplicateSacSource` call -- which prices out at roughly
1-2%, under the repo's noise bar. Size the prize from WHERE in the chain the rejection happens, not
from how many rejections there are. (If the meters ever matter: both `MTG_SOLVE_CHARGE` and
`decisionwork` bill at `consider()` ENTRY, before the filters, so an odometer-level collapse is
byte-identical only while they are disarmed -- which is the default and every shipped run.)

**`SubsetPayableWithFilters` is 4.39% inclusive and it is REAL WORK, not a mint-style defect.** It
looked like one: it copies the board per call, and on Fungus it is armed by `PendingLandAuraColorMask`
-- a HAND fact (Wild Growth in hand), true for a whole enumeration regardless of what any individual
subset does. A counter (now a permanent funnel line under `MTG_ENUM_STATS`) settles it:

```
[rescue] SubsetPayableWithFilters calls=1695688  rescued=630032  subset-casts-aura=1567110
```

**37.2% of calls rescue a subset the flat pool rejected**, and 92.4% are on subsets that genuinely
cast the pending Wild Growth -- so the tightest available per-subset precondition would remove 7.6%
of the calls, about 0.3% of the rollout. Leave it alone. The mint block and this one look identical
from the outside (board-scanning work armed by a board/hand fact); what separates them is whether
the work ever changes an answer, and only a counter can say.
---

# Round 7 (2026-09-22): WHY the greedy walk is expensive — a powerset over interchangeable mana outlets

**USER, opening this round:** *"Why is the greedy walk so expensive in the first place. That is
where I would start."* … *"Rather than just cutting the cost."*

That reframing is the whole round. `MTG_SOLVE_CHARGE`
(`slow-rollout-tail-and-the-uncharged-greedy-walk.md`) treats the walk's cost as legitimate and
rations it. The question here is whether it is legitimate at all. **It is not.** On the slow
mulligan cells the walk spends most of its positions enumerating a distinction that does not exist.

## The measurement that shows it

`MTG_ENUM_STATS=1 MTG_ENUM_STATS_MIN=1000` on the replay of a 30 s slow rollout
(`size6 draw r=39`, hand `Psychotrope Thallid x1; Thallid Shell-Dweller x1; Utopia Mycon x4`).
`ReportEnumBound` prints the odometer's shape — `bound = 2^|independent| * Π(1+|group|)` — whenever
it crosses an escalating watermark. The last three:

```
bound=2.05e+03 groups=8 ind=3  … (k7 Utopia Mycon) x3
bound=1.23e+04 groups=7 ind=6  … (k7 Utopia Mycon) x6
bound=4.92e+04 groups=6 ind=8  … [g5 k26 Utopia Mycon] (k7 Utopia Mycon) x8
```

**Every independent bit is `k7 Utopia Mycon`** — `Action::Kind::SacForMana`, the *"Sacrifice a
Saproling: Add one mana of any color"* outlet. Eight of them, from **four** physical Mycons (one
single-sac + one demand-driven burst each). Eight independent bits is **2^8 = 256 positions**.

The last line is the finding in one line, because it shows **both abilities of the same card side
by side**:

| | emitted as | positions |
|---|---|---|
| `[g5 k26 Utopia Mycon]` — the spore pop | ONE group of 5 | 6 |
| `(k7 Utopia Mycon) x8` — the mana outlet | 8 INDEPENDENT bits | 256 |

Same card, same board, same enumeration. One ability is pooled; the other is a powerset.

## Why the outlet is a powerset, and why that is redundant

`ActionFoldSig` folds a non-hand-cast on every field **except** `sac_source_id` — exactly the axis
that distinguishes two copies of one outlet. So the four Mycons' single-sac actions differ in
nothing the fold looks at. They ought to collapse. Two independent reasons they do not:

1. **The sac-outlet block never tags at all.** Only two sites in `TurnSolver.cpp` call
   `ActivationEquivTag` (~17731 and ~17847) and **both are the spore-pop path**. The mana-outlet
   emission (~18090-18260) assigns no `equiv_tag`, so its actions cannot enter a fold class.
2. **Even tagged, `FinalizeFoldTags` CONDITION 2 would drop them.** It kills any class whose source
   emitted more than one action, counting *every* action sharing the source key, tagged or not. A
   Utopia Mycon emits a spore pop *and* a single-sac *and* a burst — always > 1 — so the class dies
   by construction. This is the same wall `MTG_FOLD_COUNTER_SOURCES` hit (it *"moved `units_total` by
   ZERO … drop_src=116,427"`, recorded in `EngineFlags.h`).

**The general fold structurally cannot reach this.** That is not a bug in the fold; it is why the
spore half was fixed by *pooling at the emission site* (`MTG_FUNGUS_SPORE_POOL`, 12.6M -> 3.3M
subsets) rather than by the fold.

### And the redundancy here is an IDENTITY, not a heuristic

This is the part that makes it worth fixing losslessly. The spore pool needed a **user ruling**
because it is genuinely approximate: popping Thallid A's counters rather than B's leaves a different
*residual distribution* of counters, and a later turn could in principle care.

The mana outlet has no such residue. The activation
* **does not tap the source** (`a.cost = ManaCost{}`; *"The source stays"*),
* **does not touch its counters** or any other field of it,
* takes its victim from `CanonicalSacVictim`, which returns the **same** Saproling for every Mycon.

So sacrificing a Saproling through Mycon #1 versus Mycon #3 leaves **literally the same game
state**. The 2^8 walk is not exploring 256 futures; it is visiting one future up to 256 times.
`k` interchangeable sources cost `2^k` selections to express `k+1` outcomes — which is precisely
what this file's own `EngineFlags.h` note already says about the spore half.

A sharper way to put it: because the ability never exhausts its source, **four Mycons are not four
resources, they are one resource**. The enumerator is using the four physical copies as a *unary
repetition counter* — which is both wasteful (2^4 arrangements for 5 counts) and, separately,
*inexpressive*: with one Mycon on the board the single-sac path can never represent more than one
activation, no matter how many Saprolings are available.

## Sizing the axis

`MTG_SAC_OUTLET_PAY=1` (below) deletes this axis outright, so it bounds the prize. Replayed on the
same cell, census armed on both arms:

```
census armed  MTG_SAC_OUTLET_PAY=0   win_turn=8   replay elapsed=39,063 ms
census armed  MTG_SAC_OUTLET_PAY=1   win_turn=8   replay elapsed=   201 ms
CLEAN         MTG_SAC_OUTLET_PAY=0   win_turn=8   replay elapsed=31,554 ms
CLEAN         MTG_SAC_OUTLET_PAY=1   win_turn=8   replay elapsed=   205 ms     -> 154x
```

The clean pair (no census — arming it forces `SubsetFilterPre` all-true and inflates both arms) is
**31,554 ms -> 205 ms, 154x, at an unchanged win turn**, on one cell. **Treat it as an upper bound
on the axis, not as a shipped number** — it is one hand, and the lever that produced it is not a
ship path (below). Note also that the subset counters move the *opposite* way
(`greedy_subsets` 13.8M -> 110.5M), which is
a process-wide artifact: the replay is ~30 s of a ~298 s process and **discovery dominates the
counters**. Only `[replay] DONE elapsed=` isolates the rollout.

## The two candidate fixes, and why the lossless one is not built

**(a) `MTG_SAC_OUTLET_PAY` — built 2026-09-18, DEFAULT OFF, A/B never run.**
`docs/design/sac-mana-outlet-as-deferred-source.md`. Moves the outlet's fodder to the mana-payment
side as a last-ranked source, deleting the branching axis entirely. It is the user's own proposal
(2026-09-17) and it is **a model change, not a lossless one** — user: *"this is a cost change that
we are aiming to not cost any quality"*, and *any win-turn regression is disqualifying*.
**It is also incomplete**: its own §"STILL MISSING" records that the *plan-added fodder credit* is
unimplemented at ENUMERATION time, so a subset whose mana comes from a body the same subset creates
scores unpayable and is never offered — which is exactly the Doubling-Season-off-an-activated-
Saproling line these hands are built on. Do not read the 194x above as this lever being ready.

**(b) Pool the outlet at the emission site — BUILT AND REFUTED, see the next section.** The same move the spore pool
made, one ability over, with a stronger argument behind it (identity, not heuristic). Emit, in place
of one single-sac action per source, **one action per activation COUNT** `j = 1..N` over the `N`
interchangeable live outlets, all carrying the oldest outlet's `sac_source_id` so
`ActivationFamilyKey` buckets them into ONE mutually-exclusive group. That reproduces exactly
today's reachable count set `{0..N}` while costing `N+1` positions instead of `2^N`.

Three things to get right when building it:
* **Gate on a singleton colour fan.** With a wide `ChosenFloatColorCandidates` the outcome depends
  on the colour *multiset*, not just the count, so pooling is only an identity when the fan is one
  colour (it is `{G}` on Fungus). Outside that, leave today's emission.
* **The bursts pool too, but do not lose their combinations.** Today's `N` bursts are separate
  independent bits, so `{burst_1, burst_2}` is a reachable `2k`-sac outcome. Pool to the set of
  reachable TOTALS, not to a single burst.
* **Digest will move, outcomes must not.** Like the spore pool, this changes *which body pays*, so
  `play-changed` is the wrong bar; the bar is avg win turn over a held-out sweep, plus
  `MTG_FOLD_VERIFY`-style reasoning that every collapsed arrangement has a twin.
* **REQUIRE THE CANONICAL VICTIMS TO AGREE — the identity argument is not universal.**
  `CanonicalSacVictim` takes the *source's* id, so an outlet that can eat **itself or its own kind**
  may pick a different victim per source. Utopia Mycon is safe by type (it requires `Saproling` and
  is a Fungus, so it is never a legal victim and all four copies return the same Saproling), but
  **Skirk Prospector is a Goblin that sacrifices Goblins** — two Skirks are *not* trivially
  interchangeable, because eating Skirk A is a different board from eating Skirk B. Pool only when
  the per-source canonical victims are equal; otherwise this stops being an identity and becomes the
  same kind of heuristic the spore pool needed a ruling for.

## What this says about `MTG_SOLVE_CHARGE`

It does not refute charging the walk — an unbudgeted loop should still be bounded. But it reorders
the work. The charge rations a walk whose positions are **mostly duplicates**; calibrating an
exchange rate against duplicate work prices the wrong thing, and truncating it trades real quality
to avoid work that never needed doing. Collapse the redundancy first, then decide what the residual
walk is worth.

---

# Round 7b (2026-09-23): the count-pool was BUILT, and MEASUREMENT REFUTED IT

> **THE REFUTATION DID NOT HOLD — see Round 8.** The 7 lost games below were measured against a
> baseline that floats mana for a sacrifice it never performs. Read 7b/7c for the mechanisms
> (both are still accurate about what the code does); do not read the verdict.

`MTG_SAC_OUTLET_POOL` / `heurarm::SAC_OUTLET_POOL`, **DEFAULT OFF and byte-identical** (smoke 93/93,
`configs changed: 0`, `play-changed=0`, scenarios 103/103). Kept, off, as the reproducible A/B for
the next attempt — the same status `MTG_SAC_OUTLET_PAY` and `MTG_FOLD_COUNTER_SOURCES` hold.

**It does exactly what Round 7 predicted to the ENUMERATION, and it is a large win on cost.** The
independent Mycon bits collapse into one group, the worst odometer shape falls **4.92e+04 -> 1.54e+03
(32x)**, and the slow-cell replay goes **31,554 ms -> 181 ms (174x)** at an unchanged win turn:

```
[enum-stats] bound=1.54e+03 groups=10 ind=0 ... [g1 k26 Utopia Mycon] [g2 k7 Utopia Mycon]
```

**And it loses games.** Fungus d0, 1000 games, vs committed GT — **7 win turns changed, EVERY ONE
WORSE, three of them to a LOSS**; avg **6.0950 -> 6.1040**. Nothing improved:

```
gi120: 8 -> loss    gi177: 8 -> loss    gi978: 7 -> loss
gi340: 7 -> 8       gi506: 6 -> 7       gi798: 7 -> 8      gi811: 6 -> 8
```

A strictly one-directional change is the signature of **deleted reachable lines**, not of a
re-ordering. So the Round 7 identity argument is TRUE ABOUT THE SOURCE and FALSE ABOUT THE COUNT.

## What was ruled out, by measurement rather than argument

* **The `V` cap — ruled out.** The first cut capped the count at the plan-START fodder, which is
  wrong (a subset may sacrifice Saprolings THE SAME SUBSET CREATES — that is why
  `SubsetOversubscribesSacFodder` bails out entirely when a co-selected action can add a matching
  creature). Removing the cap was necessary and **changed nothing**: the same 7 games still lost.
* **Mutual exclusivity — ruled out.** `MTG_SAC_OUTLET_POOL_GROUP=0` keeps the pooled counts as
  independent bits instead of one group. **The same 7 games still lost** (avg 6.1030). So grouping
  the counts is not what costs the lines, and the grouping half is sound on its own.

## What is left, and it is the lesson

The remaining difference is the **action SHAPE**: a count-`c` action performs all `c` sacrifices
**atomically** inside `ApplySacForMana`'s burst loop, whereas today's `c` separate activations each
apply at their own position in the plan's action order — with token-CREATING actions able to run
between them. On this deck the fodder is manufactured mid-plan (spore pops, doubled by Doubling
Season), so a lump sac that runs before its fodder exists simply finds `vid < 0` and floats less
mana than the plan was scored on.

**AN ACTIVATION COUNT IS NOT A FREE-STANDING QUANTITY.** It is only separable from the plan when the
fodder it consumes is already on the battlefield. That is the same hole
`sac-mana-outlet-as-deferred-source.md` records as its own STILL-MISSING stage 2 — two different
designs, one underlying fact, and it is now measured rather than predicted.

**For the next attempt.** Do not re-derive the flat count axis; it is refuted. What the evidence
still supports is the narrower half: the **source** axis is genuinely redundant (N untapped outlets
resolving one canonical victim are one resource) while the **count** axis is not. A fix that keeps
`c` separate single-sac actions — so plan order and mid-plan fodder creation survive — but stops the
`N` physical copies each contributing their own, would need those `c` actions to be distinguishable
positions without being `2^N` arrangements. That is a different mechanism from this one, and the
`bf_width` census (`copyaxis_share=0.403`, `collapse=1.76x`) is the instrument that bounds it.

---

# Round 7c (2026-09-23): the count-pool's losses are a GUARD leak, not a property of counts

> **PARTLY RIGHT — see Round 8.** The guard leak is real and is now fixed
> (`MTG_SAC_FODDER_RESERVE`), but fixing it did NOT recover gi120, because the baseline's turn-8
> win is bought with phantom mana. The guard was half the story; the executor was the other half.

**This CORRECTS Round 7b's conclusion.** 7b said "an activation count is not a free-standing
quantity" and blamed plan ORDER. That named the wrong cause. **USER:** *"it might still be worth
looking at why this is causing so much trouble. That doesn't seem to make much sense."* It doesn't,
and the game says so.

## The failing game, traced

`fungus_smoke_d0` gi120 — repro needs BOTH flags (`--seed base+gi`, see
`single-game-repro-needs-game-index`) plus the manifest's `--ignore-play-profile`:

```
./build/Release/mtg decks/Fungus/Fungus.cod --profile decks/Fungus/Fungus.profile.json \
  --cards-json src/cards/data/cards.json --games 1 --seed 1121 --game-index 120 \
  --depth 0 --budget-ms 0 --threads 1 --ignore-play-profile        # OFF 8, ON 9 (loss)
```

The game log diff is NOT a preference change. At turn 5 the OFF arm plays Simic Growth Chamber and
**casts Mycoloth `{3}{G}{G}`**; the ON arm plays the land and **casts nothing**. `MTG_SAC_TRACE=1`
says why:

```
[sac] T5 burst 1/2 src=Utopia Mycon vid=1000 (1/1 Saproling Token) bf=8
[sac] T5 burst 2/2 src=Utopia Mycon vid=-1 ((none)) bf=7
```

The pooled `count=2` action floated **1** mana, not 2, with one Saproling on board. Mycoloth came up
short and the turn was wasted. **Control: the OFF arm emits no burst at all** — that count is
reachable today only by co-selecting two single-sacs.

## Why nothing rejected it: `SubsetOversubscribesSacFodder` is inert on this deck

Three independent bail-outs, each sound on its own terms, and pooling has to clear all three:

1. **`sac_actions < 2` / `outlets < 2`** — *"one outlet can never oversubscribe itself"*. True only
   while one outlet means ONE activation; a pooled action is one outlet standing for up to `N+N*k`.
   **Fixed** (`pooled_multi`).
2. **`plan_can_add` counts the SAC ACTION'S OWN CARD.** Utopia Mycon both makes and eats Saprolings,
   so its `spore_token_subtypes` made the bail-out fire on every Mycon plan. **Fixed** (a sac
   activation creates nothing), and **it did not fix the game**, because:
3. **Mycoloth's `upkeep_token_subtypes` is `['Saproling']`** — so *casting Mycoloth* still makes
   `plan_can_add` true, even though those tokens arrive NEXT UPKEEP. Not fixed, and not fixable
   locally: the predicate is "this card can ever make a matching token", by design.

The guard's own comment states the intent — *"Being conservative here is the safe direction: a
missed reject leaves the pre-existing (documented, executor/rollout-shared) apply-time degradation
exactly as it was"*. **So the guard is deliberately permissive, and on Fungus it is effectively
inert: it can never reject a Mycon plan for over-promising fodder.**

## The actual lesson

Over-promising fodder is **already happening today** — it is just benign, because each single-sac
action degrades independently and costs **1 mana**. Collapsing N activations into one action turns
that into a **k-mana** shortfall that strands the cast the whole plan was built around. The pool did
not introduce the unsoundness; **it changed a self-limiting failure into a catastrophic one, against
a guard that was never built to be precise.**

So the collapse is still right. What it needs is not a better count SET but a real supply model:
the pooled count must be bounded by fodder the plan can actually have **this turn** = board fodder
plus tokens co-selected actions create **before** the sac. That is exactly the *plan-added fodder
credit* `sac-mana-outlet-as-deferred-source.md` lists as its own missing stage 2 — **the same
blocker, now reached from the opposite direction and with a traced failing game to test against.**
Build that first; both this pool and `MTG_SAC_OUTLET_PAY` unblock behind it.

**Do not spend more on the count SET.** With the V cap, the capped set is too small (gi120's count=2
is unreachable and Mycoloth still misses); without it, the set is right and the guard cannot police
it. Neither end works until supply is modelled.

---

# Round 8 (2026-09-23): the baseline was being PAID for over-promising, and that is what the pool "lost"

**This corrects the VERDICT of Rounds 7b and 7c.** Both measured the count-pool against
`fungus gi120`'s turn-8 baseline. That baseline win is powered by **one green mana the engine
conjures out of a sacrifice that never happens**. The pool did not delete a line; it declined to
cheat, and the harness scored that as a regression.

**USER, 2026-09-23** — the directive that produced this round:
> *"I suppose if we are over-promising fodder then those fodder need to be reserved. And if that
> then makes the line unviable we skip it."*

That is right, and it needs **both** halves. Reserving at enumeration is useless while the executor
pays out on a premise that failed.

## The defect: `ApplySacForMana` floats the mana before it knows the cost can be paid

`src/core/SpellEffects.h`, the single-sac path. The order is:

```cpp
AddChosenColorFloat(state, color, amount);      // <-- the mana exists NOW
...
if (victim_id != 0 && skirk) {
    ...                                          // find the baked victim
    return;                                      // not found -> sacrifice no-ops. Mana stays.
}
```

The stale-victim comment already states the intent — *"A missing REAL-card victim is a failed plan
premise ... and must NO-OP"* — but only the **sacrifice** no-ops. `amount` mana is already in the
pool and pays for a spell. The activation's cost is *"Sacrifice a Saproling"* (CR 601.2h): with no
legal victim the ability was never activated, so it produces nothing.

**Two of the three sibling paths in this same file already get it right**, which is what makes this
a defect rather than a modelling choice:

| path | order | correct? |
|---|---|---|
| `ApplySacForMana`, multi-sac burst (`count > 1`) | `if (vid < 0) break;` **precedes** the float | yes |
| `ApplySacCreatureOutlet` (value outlets) | `if (!found) { return; }` **precedes** the payload | yes |
| `ApplySacForMana`, single-sac (`count == 1`) | floats first, resolves victim second | **no** |

## gi120, re-traced: the turn-8 win is bought with phantom mana

`MTG_EXEC_TAP_TRACE=1` on the OFF arm, turn 5 — three Forests untapped, Simic Growth Chamber entered
tapped, and Mycoloth costs `{3}{G}{G}`:

```
[exec-tap] T5 before Mycoloth: float{w0 u0 b0 r0 g2 c0 *0} untapped: Thallid Forest Utopia Mycon Forest Utopia Mycon Forest
[exec-tap] T5 Mycoloth tapped: Forest Forest Forest   float{...g0...}
```

**`g2` floating, from two Utopia Mycon single-sacs, against ONE Saproling on the board.** The first
ate it; the second found nothing, floated `{G}` anyway, and that phantom green is the fifth mana.
The board confirms the arithmetic: four creatures before, one after — one Saproling sacrificed,
three devoured (6 upkeep tokens = 3 x devour 2), and the second Mycon still standing having "paid"
nothing.

Set `MTG_SAC_NO_PHANTOM_FLOAT=1` on the **OFF** arm and gi120 goes `8 -> 9` on its own. So:

* Round 7b's headline — *"7 win turns changed, EVERY ONE WORSE, three to a LOSS"* — was measured
  against a baseline cashing free mana on exactly this deck.
* Round 7c's cause (the guard's three bail-outs) was **real and worth fixing**, but it was not the
  whole story: with the guard fixed, gi120 still lost, because the honest line genuinely cannot cast
  Mycoloth on turn 5. Only the baseline could, and only by cheating.
* The pooled burst path was **never the unsound half**. It is the half that already checked.

## What was built

Three levers, all **default OFF**, OFF arm byte-identical (scenarios 103/103, smoke 93/93,
`configs changed: 0`, `play-changed=0`).

**1. `MTG_SAC_NO_PHANTOM_FLOAT`** (`SpellEffects.h`) — resolve the victim first, float only if one
was found. Every activation that HAS a victim is byte-identical: same float, same victim, same
order. Only the failed premise changes, and it changes to nothing happening.

**2. `MTG_SAC_FODDER_RESERVE`** / `heurarm::SAC_FODDER_RESERVE` (`SubsetOversubscribesSacFodder`) —
the reservation ledger. The old guard answers an EXISTENCE question (*"could this plan conceivably
make a matching body?"*) and allows on yes; this answers a SUPPLY question, and every activation
reserves one body against it. **Every credit is either exactly countable or unbounded** — there is
no estimate in between, and an uncountable credit keeps today's allow, so the arm can only ever
remove a bail-out where the quantity is certain:

| credit | value | why |
|---|---|---|
| `upkeep_token_subtypes` | **0** | those tokens arrive at the NEXT upkeep; this main phase is past it. **Mycoloth is the card that made the whole guard inert on Fungus.** |
| `spore_token_subtypes` on a non-pop action | **0** | the spore ability is its own enumerated action; a freshly cast Thallid holds no counters |
| spore pop (`AbilityMode::SporeSaproling`) | `chosen_x * spore_creates_tokens` | the action states its own yield; with `MTG_FUNGUS_SPORE_POOL` this is the pool's whole capacity |
| casting a matching creature | **1** | exactly one body |
| `dies` / `sac_outlet` / `etb_created` / `tap` / `cast` / `attack` token subtypes | **unbounded** | genuinely replenishable this turn, or the count is not local — keep allowing |

Plus a cross-consumer check: **devour is a second consumer of the same bodies** (Mycoloth, CR
702.81 — `Action::devour_count` is a searched axis on the cast), so a plan can promise the same lone
Saproling to an outlet and to devour. Every subtype pool is a subset of *"any creature you
control"*, so `total consumption <= total bodies` is a necessary condition for the whole plan.
Separable as `MTG_SAC_FODDER_RESERVE_DEVOUR=0` for attribution.

`MTG_FODDER_TRACE=1` prints each reject with its filter, supply, credit and demand. On gi120 it
prints exactly seven, all of the form `filt=Saproling sup=1 cr=0 dem=2` — the guard is now precise,
not permissive.

**3. `MTG_SAC_OUTLET_POOL`** — unchanged from Round 7b, re-measured on an honest baseline.

## Measured: smoke, five arms, per-cell avg win turn

**Only 4 of 93 cells move under ANY arm.** Every other deck in the suite is untouched — the phantom
float is a Fungus-only phenomenon here (Goblins' Skirk Prospector never strands a victim in these
games), which is itself worth knowing.

| cell | OFF (GT) | POOL only | NP only | NP+RES | **NP+RES+POOL** |
|---|---|---|---|---|---|
| `fungus_smoke_d0` | 6.0950 | +0.0090 | +0.0080 | +0.0030 | **+0.0030** |
| `fungus_smoke_d3` | 5.6400 | +0.0067 | +0.0067 | +0.0067 | **+0.0000** |
| `goblins_smoke_d0` | 4.0680 | +0.0010 | 0 | 0 | **+0.0010** |
| `goblins_smoke_d3` | 3.6467 | 0 | 0 | 0 | **0** |
| `fungus_smoke_d5` | 5.6267 | 0 | 0 | 0 | **0** |

Reading it, searched tiers first (per the user's standing note that d0 is the side-note):

* **The full stack is EXACTLY the ground-truth baseline at `fungus d3`, `fungus d5` and
  `goblins d3`** — and it gets there while removing a free-mana bug. Every *partial* arm is worse at
  d3 than the full one; the three changes only land together.
* At `fungus d0` the honest cost is **+0.0030**, less than half of either the pool alone (+0.0090)
  or the phantom fix alone (+0.0080). The reservation is what pays that back.
* `goblins d0` +0.0010 is one game and belongs to the pool's own churn, unchanged by the other two.

Structurally, over 200 fungus d0 games the worst odometer shape falls **4.61e+03 -> 2.02e+03** and
`ind` goes to **0** — the independent Mycon bits are gone, which was Round 7's whole claim. (The
**31,554 ms -> 181 ms / 174x** slow-cell figure is Round 7b's, measured on the pool alone; it has
not been re-run with the reserve on.)

## What this round actually settles

1. **The user's instinct held through three rounds of contrary measurement.** *"I still see no
   reason to not collapse those abilities"* — there wasn't one. The collapse was being judged
   against a baseline that had an extra mana the collapse refused to fake.
2. **A guard that is "conservative in the safe direction" can be conservative in the WRONG
   direction.** `SubsetOversubscribesSacFodder`'s own comment argued a missed reject only *"leaves
   the pre-existing apply-time degradation exactly as it was"*. That is true for a VALUE outlet
   (Psychotrope Thallid loses its draw) and **false for a MANA outlet**, which gains a mana. The
   degradation was not neutral, so permissiveness was not safe.
3. **Measure the failing arm against an honest baseline, not against ground truth.** GT encodes the
   engine's current behaviour including its bugs. Two rounds were spent explaining why a correct
   change looked worse than an incorrect one.

## ADOPTED 2026-09-23 — all three DEFAULT ON, both GT tiers re-accepted

**USER:** *"Assuming this is notably faster let's move to adopt."* It is, so they are. Each keeps a
`=0` hatch (`MTG_SAC_NO_PHANTOM_FLOAT=0` is the only way to reproduce a pre-2026-09-23 ground
truth). GT rebaselined: smoke 93 keys, regression 129 keys, `check_gt_logs.py` 506 consistent / 0
stale.

### The speed, measured for the adoption (12 threads, interleaved arms, two reps)

| fungus cell | OFF | ON | |
|---|---|---|---|
| **d3 s2002 (200 g)** | 25.10 / 25.15 s | **11.18 / 10.97 s** | **2.29x** |
| d5 s2002 (100 g) | 19.32 / 19.51 s | 18.32 / 17.80 s | 1.10x |
| d5 s3003 (100 g) | 12.49 / 12.16 s | 12.18 / 11.77 s | 1.03x |
| d3 s3003 (200 g) | 11.29 / 11.20 s | 11.44 / 11.10 s | 1.01x |
| d0 s2002 (1000 g) | 0.05 s | 0.07 s | free either way |

**1.32x across the whole fungus searched block, and the win is concentrated exactly where Round 7
said it would be** — one cell halves while the rest are flat, because the powerset only explodes on
particular hands. CPU (`user`) moves just 1.07x against wall 1.32x: this is killing the makespan
TAIL, which is what matters on the suite's heaviest deck.

### Held-out quality (regression tier, seeds 2002/3003, disjoint from smoke's 1001)

Games-weighted: **searched (d3+d5) = +0.0 turn-units over 1700 games**; d0 = **−1.0** over 2000
(i.e. one game better). Only three distinct hands move at searched depth — `fungus d3 s2002 gi127`
7→6 and `goblins d3 s3003 gi22` 5→4 better, `fungus gi24` 5→6 worse (counted twice, it sits in both
the d3 and d5 s3003 cells).

**`gi24` is `gi120`'s defect at searched depth, not a deleted line.** Draws are IDENTICAL between
the arms, and the old line is bought with phantom mana:

```
[exec-tap] T4 before Mycoloth: float{... g4 ...} untapped: 1/1 Creature Thallid Forest Utopia Mycon Tukatongue Thallid Utopia Mycon Utopia Mycon Utopia Mycon
[exec-tap] T4 Mycoloth tapped: Forest   float{... g0 ...}
```

Four Utopia Mycons float `{G}{G}{G}{G}` against the **one** Saproling the turn's Thallid pop
created — Mycon eats Saprolings only, so **three of that four are conjured**, and Mycoloth's
`{3}{G}{G}` is paid one Forest plus four phantom-heavy green. So every searched-depth win-turn
"regression" traced across both tiers is the baseline losing illegal mana.

### Re-measured after the rebase onto the wave-0 + payment changes

The adoption landed on top of `9109ff6c` / `43bbed27`, whose own GT accept had moved the baseline,
so both tiers were re-run and re-accepted on the COMBINED binary (GT is a measurement, not a file to
merge). Smoke's delta was unchanged. The regression tier moved slightly, to **searched +1.0
turn-unit over 1700 games** (d0 unchanged at −1.0 over 2000), and surfaced a **third** instance of
the same defect:

* `fungus gi74` at d5 s2002, **8 → loss** — the maximal slowdown, and a hand the *old* GT had as a
  loss too until the upstream changes won it. Each of the three levers flips it **on its own**, so
  the win was knife-edge. It is also phantom-funded: at T7 before Mycoloth the float is `{G}{G}{G}`
  while the untapped board is `1/1 Creature, Tukatongue Thallid, Forest, Forest, Thallid
  Shell-Dweller, Utopia Mycon, Thallid Shell-Dweller, Utopia Mycon` — **two Mycons, ONE Saproling**,
  so at most one sacrifice is legal and **two of the three green are conjured**.
* `fungus gi127` 7→6 and `goblins gi22` 5→4 still improve; `fungus gi24` 5→6 as above.

**So all three searched-depth win-turn regressions across both tiers — gi120, gi24, gi74 — are the
baseline losing mana it never paid for.** None is a deleted line.

**METHOD TRAP, recorded because it produced a wrong answer.** `test/classify_turn_later.sh` re-runs
each slower game at 4x/16x budget **in the caller's environment**. Run it without the arm's env
vars set and it re-measures the BASELINE, not the arm: it reported `gi24` as *"churn (recovers to 5:
4x=5 16x=5)"*, which is simply the old behaviour winning on turn 5 again. With the levers actually
on it reports `PERSISTS (4x=6 16x=6)`, which is correct — and the real explanation is the phantom
float above, not churn. **Classify with the arm engaged, or you are classifying the thing you are
comparing against.**
* **The reservation is order-blind.** It credits a co-selected spore pop whether or not the pop
  precedes the sac in the plan's action order. That over-credits, which is the safe direction and
  matches today's behaviour, but it is the obvious next tightening.
* `MTG_SAC_OUTLET_PAY` has still never been A/B'd. Its blocker (the *plan-added fodder credit*,
  `sac-mana-outlet-as-deferred-source.md` stage 2) is what the reservation ledger now partially
  implements — a credit for bodies the plan itself creates — so that lever is closer than the doc
  says.
