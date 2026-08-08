# FiveColour search cost — why it is 25× the suite, and what to do before registering it

**Status: PARTLY RESOLVED.** Section 6 (provider misroute) FIXED + adopted; section 7 (tooling gap)
CLOSED. Sections 3-5 (the mana backtracker and the straggler tail) remain OPEN and still block
registering FiveColour in the tiers -- but the cost is down ~40% from where this started. See
"Where it stands now" at the bottom for the current numbers and the refuted levers.
Measured on 840ba15 and later, single-thread throughout.

## 1. The measurement

Apples-to-apples: every deck at `--depth 3 --budget-ms 10 --games 100 --seed 2002
--ignore-play-profile --lookahead-bottoming --threads 1`, run serially (no contention).

| deck | s/game | vs burn |
|---|---|---|
| burn | 0.021 | 1× |
| knights | 0.027 | 1.3× |
| auras | 0.035 | 1.7× |
| slivers | 0.035 | 1.7× |
| dragonstorm | 0.040 | 1.9× |
| th | 0.042 | 2.0× |
| antilife | 0.048 | 2.3× |
| goblins | 0.111 | 5.3× |
| hinata | 0.160 | 7.6× |
| creature_giving | 0.222 | 10.6× |
| **fivecolour** | **5.578** | **265×** |

FiveColour is **25× the next-heaviest deck**. It is also strongly seed-dependent: the same
config at seed 1001 is 1.81 s/game, so seed 2002 is ~3× worse than seed 1001.

## 2. It is NOT the branching factor

The obvious hypothesis is wrong. `MTG_BRANCH_STATS=1`, 6 games each:

| deck | EnumeratePlans calls | sum_odo | s/game |
|---|---|---|---|
| creature_giving | 25,194 | **1,552,046** | 0.222 |
| fivecolour | 33,328 | **1,474,960** | 5.578 |
| hinata | 17,394 | 379,998 | 0.160 |
| burn | 7,444 | 132,202 | 0.021 |

**creature_giving enumerates MORE odometer positions than FiveColour and runs 25× faster.**
The plan count is normal; the cost is *per position*.

For the record, the odometer composition (FiveColour, by driver card, `sum_odo` share):
Unite the Coalition 62% (avg 122, max 2688 — the S∈[0..5] modal split × 2 copies),
Lightning Greaves 17% (avg 169, max 1024), everything else ≤ 7%.
Worth revisiting later, but it is not what makes this deck slow.

## 3. It IS the mana backtracker

`callgrind` (build/Profile, 2 games, seed 2002, d3/b10) — 24.82e9 Ir total:

| component | Ir | share |
|---|---|---|
| **`TapForCostBacktrack` (all attributions incl. its memo hashtable + `CanPayFlat`)** | **8.30e9** | **33.4%** |
| `ComputeLordBonus` | 0.91e9 | 3.7% |
| `EffectiveProduces` / `DomainColors` | 0.36e9 | 1.5% |

One third of every instruction is spent assigning colours to mana sources. `MTG_TAP_STATS`
top-level entries over 6 games: fivecolour 116,202, hinata 36,510, creature_giving 27,151,
burn 7,880 — and the *entry count* is only 4× creature_giving's while the *cost* is 25×, so
each entry is also far more expensive.

That is exactly what this deck's mana base predicts: 5 colours, 11 fetchlands, 7 duals/triomes,
plus **two dynamic domain sources** (Faeburrow Elder, Bloom Tender) that produce one mana of
*each* colour among controlled permanents, and Deathrite Shaman which produces *any* colour.
The backtracker's per-node branching is the product of those choices, and `DomainColors`
rescans the whole battlefield on every `EffectiveProduces` call inside that recursion.
`ComputeLordBonus` at 3.7% is the same story (Faeburrow's `domain_self_pump` recomputed live).

## 4. The straggler tail is still severe

`-DMTG_PROFILE=ON` counters build, 60 games, seed 2002, d3/b10:

```
top  5% of games hold 59.7% of nodes
top 10% of games hold 74.5% of nodes
median game nodes: 28,914     max/median: 186x
heaviest games (index : nodes : ms):
  #42 : 5,379,758 : 234,932      <- 3.9 MINUTES in one game, 33% of all nodes
  #36 : 2,153,128 : 171,254
  #14 : 2,165,376 :  51,532
```

Reproducer for the worst: `--seed 2002 --game-index 42 --games 1 --depth 3 --budget-ms 10
--ignore-play-profile --lookahead-bottoming`.

This is a *different* shape from the 3.4 h Stage-4 atom that was fixed earlier (that one was a
single 7.4e7-position enumeration). Game #42 crosses no 1e6 odometer watermark at all — it is
978,211 positions spread over *many* moderately large enumerations, with 59,730 backtracker
entries in that one game (half of what six average games use). So the atom fix did not address
this; it is the ordinary cost of the deck, concentrated on unlucky seeds.

## 5. Sizing verdict — do NOT register yet

Smoke-style sizing probe at seed 1001 (the *favourable* seed), single-thread:

| case | s/game | wall for the sizing used by th/hinata/dragonstorm |
|---|---|---|
| d0, 1000 games | 0.0007 | 0.7 s — fine |
| d3 b10, 150 games | 1.81 | **272 s** |
| d5 b20, 75 games | 2.85 | **214 s** |

That is **~8.1 min added to a 15-min smoke budget**, from one deck, at its best seed. At the
regression seeds it is worse: d3 at seed 2002 is 5.58 s/game, so a 300-game case is ~28 min on
its own — the whole regression budget is 45 min for ten decks.

Registering FiveColour at any useful game count is not affordable until §3 is addressed. A d0-only
entry (0.0007 s/game) is affordable today and would still cover the new card implementations, but
d0 exercises no search, so it would not protect the subsystems this deck actually added.

## 6. Also found: FiveColour is on the WRONG decision provider

`SelectDecisionProvider` (`src/ai/DecisionProviders.cpp:5618`) sets `anti = true` for any deck
with `!p.fetch_land_types.empty()`. FiveColour's 11 fetchlands (Misty Rainforest, Scalding Tarn,
Verdant Catacombs, Windswept Heath, Wooded Foothills) trip it, and the deck carries **no other**
archetype signature — so it falls through to `AntiLifegainProvider`, whose fetch/tutor tiebreaks
are tuned for a specific 4-colour anti-lifegain shell. Confirmed in the callgrind profile:
`AntiLifegainProvider::FetchCandidates` is 242M Ir (~1%) of a FiveColour run.

This is the third deck to hit this trap — Goblins and Creature Giving each needed an explicit
escape hatch *above* the `anti` check (the code comments at 5630 and 5680 document both). Nobody
added one for FiveColour, because the whole deck was analyzed without anyone checking which
provider it rides.

**Consequence:** the shipped analysis (avg 5.3340) was measured under the wrong provider. Fixing
it is a behaviour change that moves FiveColour's numbers and requires re-verification, so it is
recorded here rather than done silently. The likely fix mirrors the other two: detect a
FiveColour signature (e.g. `domain_mana`) and return `g_generic` before the `anti` check — or,
better, narrow the `anti` signature itself so a bare fetchland stops implying anti-lifegain.

## 7. Tooling gap worth closing

There is **no per-game wall-time or straggler report in a Release build**. Every timing line in
`GoldFishRunner.cpp:305-313` is `#ifdef MTG_PROFILE`, so a normal run cannot say which game is
slow. The only straggler instrument in the tree is the keep generator's (`MTG_KEEP_SLOW_MS`,
default 30 s, `ExhaustiveKeep.cpp:1422`) — nothing equivalent for goldfish games. That is why the
3.4 h Stage-4 atom presented as "the analyzer is hung" rather than "game N is slow".

Proposal: an env-gated `MTG_SLOW_GAME_MS` in `GoldFishRunner` mirroring `MTG_KEEP_SLOW_MS` —
inert when unset, prints `[goldfish] SLOW-GAME <ms>ms gi=<i> seed=<s>` to stderr past the
threshold. Cheap (one `steady_clock` read per game), and it turns every future degenerate deck
from a mystery hang into a one-line reproducer.

## Suggested order of work

1. Close the tooling gap (§7) — small, inert, makes everything below observable.
2. Decide the provider question (§6) — it changes FiveColour's numbers, so do it before any
   re-baselining, not after.
3. Attack the backtracker (§3). Cheapest first: memoise `DomainColors` per (battlefield, controller)
   instead of rescanning inside the recursion, and confirm with a paired callgrind Ir A/B.
4. Re-measure §1/§5 and only then size the regression entries.


## Resolution of section 6 — FiveColourProvider ADOPTED (2026-08-07)

`FiveColourProvider` (archetype signature `domain_mana`, detected ABOVE the `anti` check exactly
like the Goblins and Creature Giving escapes) now owns `FetchCandidates`. The policy is
user-directed: get the colours that let us cast early ACCELERATION first, spread the five colours
over different sources, then build toward two sources of each. Encoded as a strict lexicographic
key (accel_new > spell_new > breadth > untapped > depth > colours > name) so the ordering is total
and deterministic, with no float weights to re-tune. It returns the full ordered list; the engine's
`FetchSearchCap` (2) decides how much of it the search branches on.

**Quality — improves on every seed set tried, including held-out:**

| seed set | old (AntiLifegain) | new (FiveColour) | delta |
|---|---|---|---|
| 4200000 (500 games) | 5.3400 | 5.1280 | **−0.212** |
| 90001 (500 games) | 5.3300 | 5.0880 | **−0.242** |
| 555000 (500 games, held out) | 5.3180 | 5.0980 | **−0.220** |

**Cost — it is also ~2x CHEAPER.** Deterministic, contention-immune counters (20 games, seed 2002,
one thread), which is the metric to trust on this box:

| counter | old | new | change |
|---|---|---|---|
| rollout calls | 990,802 | 524,203 | −47% |
| rollout turn steps | 1,136,565 | 580,015 | −49% |
| `TapForCostBacktrack` entries | 1,882,968 | 670,306 | **−64%** |

Wall clock agrees (100 games, seed 2002, one thread: 10.44 → 4.97 s/game) but the counters are the
evidence; absolute wall on this box drifts between runs.

The cost win and the quality win are the same effect: fetching for **coverage** means the greedy
scarcity-first payer finds a legal payment far more often, so the exponential backtracker is
entered a third as much. That is the mechanism section 3 predicted — the backtracker is expensive
*because* the mana base is under-fixed, and fixing the fetch policy attacks it at the source.

Smoke 30/30 + regression 50/50 byte-identical, 0 play-changed: no other deck is on this provider.

**Still open:** even at 4.97 s/game FiveColour is ~22x the next-heaviest deck, so §3/§4/§5 stand
and registration stays deferred. Next cheapest lever is still memoising `DomainColors` out of the
backtracker recursion.


## Resolution of section 7 — MTG_SLOW_GAME_MS (44285cd)

`GoldFishRunner` now times every game and streams any that exceeds `MTG_SLOW_GAME_MS` (ON by
default at 30 s, `=0` disables), mirroring the keep generator's `MTG_KEEP_SLOW_MS`:

```
[goldfish] SLOW-GAME 161159ms  gi=42 wt=5  repro: --seed 2002 --game-index 42 --games 1
```

Validated end-to-end: run blind over 100 games at seed 2002 it independently flagged **exactly**
gi=36 and gi=42 — the same two games the `-DMTG_PROFILE=ON` counters build had identified in §4,
without needing that separate build. One `steady_clock` read per game; nothing feeds a decision.

## Also fixed: the backtracker never exiled Deathrite's graveyard land (44285cd)

`TapForCostBacktrack` carries its own inline copy of `ExileGraveyardLandForMana`, and it read
`gy[g].IsLand()` on a graveyard placeholder — always false, so the loop exiled nothing. The fuel
gate said "live" and the tap took the mana while the land stayed in the graveyard, so **N Deathrites
could all tap off ONE graveyard land in a single payment** (this deck runs four). Same placeholder
class as 333ab17; a repo-wide sweep for raw `.IsLand()/.IsCreature()/.ManaValue()/.IsMulticolored()`
on hand/library/graveyard cards now finds none left in the engine. Play-neutral (+0.001 avg over
3x500 games), as expected for removing an advantage that was never legal.

## REFUTED lever — colour-aware B&B gate (do NOT retry)

Hypothesis: the total-mana gate has no colour awareness, so a `{W}{U}{B}{R}{G}` probe on a board
with five lands but three colours passes it (5 >= 5) and then walks the whole tap tree to prove
what one colour count already knew. Implemented as a lossless top-level per-colour capacity check
(each source's full max net credited to every colour it can make — a deliberate over-count;
colours read through `ProducesForPayment`).

**Measured: 0.28%** (paired callgrind Ir, 2 games seed 2002: 24,177,852,448 -> 24,109,381,014).
Inside the noise band this repo already burned itself on (see `shard-volley-hold.md`, where a
+0.14% Ir claim was retracted). REVERTED rather than shipped.

*Why it failed, so the next person does not re-derive it:* the greedy scarcity-first payer already
handles the colour-easy cases, so `TapForCostBacktrack` is entered precisely when the colours ARE
available and only the assignment is hard. A colour-feasibility gate therefore almost never fires
at the point it is checked. Any future attack has to prune INSIDE the tree, not at its root.

## Where it stands now

Single-thread, 100 games, d3/b10, current HEAD:

| seed | before (start of this work) | now | change |
|---|---|---|---|
| 2002 | 5.578 s/game | **3.318** | −40% |
| 3003 | — | **2.423** | |
| 1001 | 1.81 (at 150 games) | **1.039** | −43% |

Still ~15x the next-heaviest deck (creature_giving 0.222), and **the tail still dominates**: at
seed 2002 two games out of a hundred (gi 36 at 44 s, gi 42 at 161 s) account for 205 s of the
331.8 s total — 62% of the run in 2% of the games.

**Sizing verdict — still deferred, but closer.** A 300-game d3 regression case would be ~12-17 min
at the regression seeds, against a 45-min budget shared by ten decks. Options when this is revisited:
1. Attack the tail directly (it is 62% of the cost; the two reproducers above are cheap to profile).
2. Register d0-only now (0.0007 s/game) — affordable immediately, but exercises no search.
3. Register a SMALL d3/d5 case (~50 games) and accept ~2-3 min, after checking the chosen seed
   range has no straggler (`MTG_SLOW_GAME_MS` now makes that a one-line check).
