# Tier-2 cache overflow (2026-11-03)

Branch `tier2-overflow` (cut from `budget-rebalance` a18ae02bf). Builds on
`docs/design/two-tier-search-cache.md` (the tier-1 / tier-2 contract).

## The request
USER 2026-11-03: *"be ready to use the tier 2 caching if we need it ... for any caches that will hit us
heavily (particularly)"*. When a fixed per-thread tier-1 cache fills, overflow into tier 2 instead of
losing entries, and never let tier-2 residency move play.

## What was built
`src/core/Tier2Overflow.h/.cpp`:
- `tier2::Overflow<Map, Bytes>`: a PER-THREAD overflow map over the consumer's own map type, so a
  spilled tier-1 entry moves as a node handle (no copy, no allocation). `Put` / `SpillAll` / `Find` /
  `Take` (promote back) / `Clear`.
- `tier2::Pool`: ONE shared byte budget (CAS reserve, never exceeded) with per-category gauges and
  counters (stores, refused, lookups, hits, ctx_misses, dropped_at_game_end, peak). Exit line
  `[tier2-overflow] ...` (printed only when something was offered to tier 2); heartbeat token `t2=`.
- Budget: `membudget::DefaultTier2OverflowKb()` = min(workers x 32 MB, cache_mb / 8) -- its OWN slice of
  the RAM-derived cache budget, not the plan-cache pool (one plan-heavy game's bp cache reaches 788 MB,
  which would starve the overflow). This box (23.5 GB, 32 workers): ~502 MB.

Why per-thread storage with a shared budget, not a shared map: every cache that could overflow is
scoped to one game (mana) or one decision (m1memo, fsl) on one worker, so another thread can never use
an entry; a shared map would add a lock to every tier-1 miss and turn any residual key hole into a
cross-thread channel. What is shared is the memory: one heavy game can use most of the slice.

Levers (all read via `EnvOn` / explicit value parse):
| lever | default | meaning |
|---|---|---|
| `MTG_TIER2_OVERFLOW` | ON | `=0` restores tier-1-only behaviour byte for byte |
| `MTG_TIER2_OVERFLOW_KB` | RAM-derived | shared budget in KB; `0` = unbounded; `1` = the purity test's "refuse ~everything" arm |
| `MTG_TIER2_MANA_CTX` | ON | DIAGNOSTIC only: `=0` serves a tier-2 mana entry whatever its context (the tier-1 rule) |

## The charge rule
A tier-2 hit must charge exactly what a recompute at the hit site would: budget units AND priced work
(`workctr::t_priced`, which the X=100 priced decision ceiling reads), and have the recompute's side
effects. If a recompute's charge depends on anything but the key, the cache does not qualify.

## Per cache

### mana (payable-mana cache, SpellEffects.cpp) -- WIRED
- Tier-1 full behaviour before: WHOLESALE CLEAR at the 24 MB allocation (and at 500,000 entries).
- Now: the clear SPILLS every entry into tier 2 (`McTier1Clear`); a tier-1 miss looks in tier 2 and a
  hit is PROMOTED back into tier 1 through the same store (`McTier1Insert`) a recompute takes, so tier-1
  contents, byte count and clear points are exactly what they would have been had tier 2 refused it.
  Tier-2 refusal (budget full) drops the entry -- the old clear's behaviour, bounded.
- Charge: payment charges no units, and `workctr::kPay` is counted in `TapForCostShared` before the
  cache, so a hit and a recompute both cost nothing -> pure wall.
- Content hazard and the context match: `MTG_MANA_CACHE_VERIFY` finds colour-only differences between a
  hit and a fresh solve when the payer context / plan traits moved since the store (Niv seed 2085 gi83: 40
  of 143,959 hits). Deterministic inside tier 1, but from a RAM-sized tier 2 it would be RAM-dependent
  play. So each entry records `PayContextDigest()` and `CurrentPlanTraitsHash()` read BEFORE its solve
  (the moment a lookup reads them) and a TIER-2 entry is served only when both match. The tier-1 key is
  untouched (the X_TRAITS / X_PAYCTX levers stay default off), so the fold is confined to the overflow
  path (USER 2026-11-03: okay "as long as it is truly isolated and doesn't cause any other changes").
- A SECOND hole, found by the purity run (below): the tier-1 key's source descriptor does not carry
  `Permanent::chosen_color`, which decides what an `etb_choose_color` source can make (Coldsteel Heart in
  Snow, Utopia Sprawl in AccomplishedAlchemist), and the search explores different colour choices for the
  same board. The tier-2 context word (`McT2PayWord`) therefore also folds (index, chosen colour) of the
  active player's permanents. Inside tier 1 the hole is deterministic but WRONG: Snow regression d5 s3003
  gi27 at default sizes has 292 tier-1 hits whose verdict or pool differ from a fresh solve.
  `MTG_MANA_CACHE_CHOSEN_KEY=1` (default OFF; a play change for Snow and Alchemist -- the user's call) folds
  it into the tier-1 key and takes that game to 0.

### m1memo, fsl, tt, gamelc -- NOT wired (unsound under a finite budget)
Full behaviour at the tier-1 bound today, and measured peak vs allocation (regression tier + tail corpus):
| cache | scope | at the bound | peak / alloc |
|---|---|---|---|
| m1memo (rollout pre-combat solve memo) | one decision epoch | WHOLESALE CLEAR (8 MB, or 262,144 entries) | 7.9 / 8 MB |
| fsl (FSLineCache) | one decision | REFUSE the store (`tier1::Fits`), plus a 2M-entry cap | 14.7 / 16 MB |
| tt (TranspositionTable) | one decision / a bottoming loop | REFUSE the store | 6.8 / 8 MB |
| gamelc (game-persistent FS line cache) | one game | REFUSE ("never evicts") | 14.5 / 24 MB |

None of them bound at default sizes in the regression tier (0 tier-1 refusals; m1memo is the
closest). What each would need before tier 2 could hold its entries:

1. **The hit is free, so the tier-2 hit must charge what a recompute AT THE HIT SITE would charge.**
   For all four, that is not a function of the key: the miss runs a nested search (rollouts, a deeper
   FSL frame) whose own cost depends on the warm state of the OTHER free-hit caches it reads -- the
   caller's TT, the solve memo, the m1/m2 memos, the FSL and game line caches. Storing the units the
   FIRST computation charged and replaying them is exactly `MTG_CACHE_HIT_CHARGE` mode 2, which was
   measured to OVERCHARGE: KittyEquipment seed 1015 gi14 d3 b10 read 20,443 units against 4,424 when
   every lookup recomputed, and lost a turn (TurnSolver.cpp, cachecharge). The same holds for priced
   work (`workctr::t_priced`), which the X=100 priced ceiling now reads: a recompute's apply / enum /
   simkey counts depend on the same warm caches.
2. **The hit must leave the nested caches as a recompute would.** A recompute also STORES into the
   nested caches (TT entries, solve-memo entries, FSL lines); a served tier-2 entry does not, so every
   later free hit in the decision differs -> units differ -> play differs.
3. Mode 1 (store only computations that charged nothing -- the plan caches' rule) gives an EMPTY set:
   under a finite budget every m1 rollout solve with depth > 0 charges rollout steps, and every FSL /
   TT / gamelc entry is a searched node that charged units. At an unlimited budget they would qualify,
   but that is an unbudgeted-only change (USER 2026-10-14) with nothing to gain where the tiers run.
4. FSL / TT / gamelc entries are also not plain values: they are cutoff-dependent BOUNDS reused across
   iterative-deepening passes (an FSL node searched under one cutoff proves only "no win <= cutoff"),
   so even content purity would need the bound semantics carried into tier 2.

The sound route, if one of them ever binds: make its MISS a pure computation -- run it with its nested
memos scoped fresh and its side channels isolated (the `MTG_BP_DERIVE_ISOLATE` pattern), so its units,
priced work and result are functions of the key -- then cachecharge mode 2 is exact. That changes
tier-1 play for every miss (not only overflowed ones) and costs the warm-cache savings, so it is a play
change with its own held-out A/B, not a wiring job. Until then, the cheaper lever is the tier-1
allocation itself (deterministic, machine-independent): m1memo is the first candidate to raise if it
starts clearing.

### solvememo -- NOT changed
The greedy Solve memo (+ the searched second-main memo) is bounded by a portable ENTRY-COUNT cap
(16,384, wholesale clear; MTG_BIG_SOLVE_MEMO / heurarm selects 262,144), not by bytes, and peaks at
73 MB in one game (p99.9 24 MB). It is already machine-independent, so it does not violate the tier-1
contract; it is just not in the byte ledger's admission. Its hits skip greedy re-enumeration, which
costs NO units on every deck except under `MTG_SOLVE_CHARGE` (Snow, W=16) -- but it does cost PRICED
work (greedy visits, enumeration calls, plans), and the X=100 priced ceiling reads that. So:
- **a tier-1 byte allocation** would move its clear points (from "every 16,384 entries" to "every N
  bytes"), which moves priced work, Snow's units, and through the ceiling possibly play. It needs a
  measured A/B; not done here. If wanted: allocation ~24-32 MB covers p99.9 (24 MB), with the entry
  cap kept as a second bound so no game clears MORE often than today.
- **tier-2 overflow** needs a hit to replay the greedy charge (Snow) and the priced delta, and the
  recompute's priced delta is itself warm-cache dependent (the enumeration inside reads the plan caches),
  so the same objection as the free-hit caches above applies. Not recommended.

## Purity evidence
All runs: the regression tier's 264 jobs / 75,244 games as ONE pooled `mtg --batch` per arm, compared by
job (digest, avg, played, unit total -- covers every game) and per game (`SLOW-GAME` lines under
`MTG_SLOW_GAME_MS=1`, i.e. every game of >= 1 ms; `scripts/cmp_runs.py`). Scratch:
`build/scratch/tier2/` (`jobcmp.py` does the job-level unit comparison).

1. **Purity, first attempt (2098054aa, payer-context + traits match only):** mana tier-1 1 MB, tier-2
   KB=1 vs unbounded: 0 digest / 0 win-turn differences, but **7 games' units differed** (alchemist d5 x4,
   snow d5 x3). They reproduced single-game with the plan cache unbounded AND off, so the overflow itself was
   impure. `MTG_MANA_CACHE_VERIFY` on Snow d5 s3003 gi27: 78 of 6,070 tier-2-served hits disagreed with a
   fresh solve with context and traits EQUAL -> the chosen-colour key hole (above).
2. **Purity, final (71a656d57):** mana tier-1 1 MB (8.86M tier-2 offers per arm), tier-2 KB=1 (8,858,222 of
   8,859,979 refused) vs unbounded (2,992,692 tier-2 hits, peak 122 MB):
   **264/264 jobs identical in digest and unit total (75,244 games); 41,763 per-game rows, 0 win-turn and
   0 unit differences.** The 7 games above are identical too.
3. **Are the payer-context / traits folds needed?** Same, unbounded, `MTG_TIER2_MANA_CTX=0` (board-hole fold
   kept): **2 games' units differ** (niv d5 s2002 gi135 174,038 -> 174,039; niv d5 s3003 gi140 479,699 ->
   479,751), win turns equal. So yes: without them tier-2 residency moves NivMizzetCombo's units, exactly the
   game family `MTG_MANA_CACHE_VERIFY` flagged. They stay, scoped to tier-2 entries.
4. **Isolation at default sizes** (the USER's condition for the folds): a18ae02bf (its parent's binary with
   `MTG_TIER1_MANA_MB=24`, the only src change in a18ae02bf) vs this branch with `MTG_TIER2_OVERFLOW=0` vs
   this branch at defaults (overflow on): **264/264 jobs identical in digest and unit total, 75,244 games;
   per game (38-39k rows each) 0 win-turn, 0 unit differences.** Overflow on vs off: also 0 / 0 (41,388
   rows). At default sizes exactly ONE regression game overflows (Snow d5 s2002 gi80: 365,962 entries
   spilled, 135,087 tier-2 hits, 16,985 context misses, tier-2 peak 65.4 MB), and its play is unchanged.
   (Caution for the reader: an earlier "base" arm used a binary built just BEFORE a18ae02bf, i.e. mana 16 MB,
   and it differed on Snow d5 s2002 gi76 by 2 units -- the mana ALLOCATION moves Snow's units through the
   chosen-colour hole. So a18ae02bf's commit note "its size moves wall only" is not true for Snow.)
5. Unit tests: 2,436 / 2,436 (incl. 5 new `tier2 overflow:` cases: budget admit/refuse, replacement, take /
   clear accounting, destructor release, 8-thread budget invariant). NOTE: `mtg-test` resolves its data by
   walking up from the EXECUTABLE, so a test binary under `<primary>/build/...` reads the PRIMARY checkout's
   `cards.json` and `decks/`; run from a stale primary that gives 7 spurious failures.

## Wall and memory
- At default sizes the overflow fires in ONE regression game (Snow gi80). Snow gi80 alone, 3 reps each,
  concurrent on a shared box (wall s): overflow off 22.3 / 22.1 / 18.7, on 20.5 / 20.8 / 20.6, tier-1
  uncapped 20.0 / 24.1 / 18.9. Same units in all nine. The gain is within noise: the mana cache's refill is
  cheap (the earlier single-shot reading was uncapped 14.7 s vs 24 MB 15.0 s, 2%). Regression tier
  overflow on vs off: 13,692 vs 13,528 s summed game wall (+1.2%, concurrent arms, noise level).
- With tier 1 deliberately 1 MB: tiny vs unbounded tier 2, 12,233 vs 12,380 s (no gain either): payment
  re-solves are not where the time goes.
- Memory: the shared slice is ~502 MB on this box (min(32 x 32 MB, 3.7 GB / 8)). Peak use: 65.4 MB at default
  sizes (Snow gi80, RSS 212 MB vs 156 MB with overflow off; uncapped tier 1 209 MB); 122-133 MB with tier 1 at
  1 MB across 5-8 threads. The per-miss context read (`PayContextDigest` + traits + a board scan) costs one
  digest per tier-1 MISS only, ~0 measurable.
- Verdict: wired, pure and isolated, but the mana cache was not a wall problem. Its value is readiness (a
  heavier game or a smaller tier-1 allocation on the 12-thread / 11 GB box keeps its entries) and the key
  hole it exposed.

## A finding outside this change: plan caches vs the priced ceiling
**The plan caches (tier 2) are NO LONGER play-invisible since the X=100 priced ceiling went default (6a59eafc1,
06:31 UTC).** The 50,760-game proof (p2a/p2b, ~05:00 UTC) predates it. Plan-cache hits skip PRICED work
(`workctr::kEnumCall`, `kPlans`, `kOdoVisit`) that a recompute counts, and `decisionwork::PricedExceeded()`
reads `workctr::t_priced`, so plan-cache residency (RAM- and interleaving-dependent) moves the ceiling's trip
point. Regression tier, default vs `MTG_PLAN_CACHE_KB=1`: **6 job digests, 3 win turns (dragonstorm d5 s2002
gi200 7->6, elementals d5 s2002 gi121 4->5, hinata2hg d5 s2002 gi49 5->6), 14 games' units** differ.
Single-game, 5 of those games: `MTG_DECISION_PRICED_X=0` -> KB=0 and KB=1 identical in all 5; X=100 -> all 5
differ (3 win turns). Fix direction: a plan-cache hit must replay the priced work its computation counted
(store the `workctr::t_n` deltas in `cachecharge::Cost`, add them on a hit), and verify at scale that a
recompute's priced count equals the first run's (inner caches may make it warm-dependent, the mode-2 problem
for priced work). Not done here.
