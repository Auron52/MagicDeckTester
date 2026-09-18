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
to a real subtree, a 5.9% real-dup share**, against an `fs_pre` that is 43.7% of units. The Snow
verdict does not transfer, and mode 2 is worth an A/B on this deck. Also from the same run:
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
* **Also promoted, cheaper:** an A/B of `MTG_FSW_EOT_DEDUP=2` on Fungus (5.9% real-dup share, and
  lossless by the same argument that made the winless-develop closure sound).
