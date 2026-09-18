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
