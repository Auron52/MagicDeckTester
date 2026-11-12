# Pooled-vs-solo play drift on Extra Turns: root-caused and fixed (2026-11-12)

USER, 2026-11-11, before the artifact queue: *"Okay, in that case let's finish that work before launching
the artifact queue."*

## The symptom

Three suite keys sometimes came out of a pooled tier run (`regression.sh`, one `mtg --batch` over every
case) with the GT score but a different digest, while the same job run alone always matched GT:

| key | pooled (seen) | solo = GT |
|---|---|---|
| `extraturns_smoke_d3_s1001` | `4639ddd2…` / 440,412 units, `b2ef7c74…` / 453,614 | `76322f02352e42d8` / 417,966 |
| `extraturns2hg_smoke_d5_s1001` (gi 19) | `4e100ae2…` / 220,591 | `cc399f1a95984da0` / 162,410 |
| `extraturns_regression_d5_s2002` (gi 66) | `e97be7a575b479c4` / 1,951,165 | `254115e71dd5ea00` / 1,887,812 |

It was intermittent: several pooled smoke runs in a row matched GT.

## The mechanism

`ExtraTurnChainTrial` (TurnSolver.cpp, Extra Turns' verified depth-0 chain walk) keeps its state in
`thread_local`s (`etchain::active / seed / first_seq / failed_round / steps`). It reset that state
**only when the game SEED changed**. Every suite deck's d3, d5 and 2HG jobs play the **same seeds**
(`_smoke_d3_s1001` game k and `_smoke_d5_s1001` game k are both seed 1001+k). So in a pooled batch, a
worker that had just played another job's same-seed Extra Turns game carried that game's
`failed_round` into this one (the trial at that turn was then skipped outright: `step_f`, the free
rollout steps the trial plays, went from 8-24 to 0) and its `active` chain. In most games that only
moved the work counters; in some the skipped trial changed the line: gi 28 of the d3 job at
44,989 units instead of 22,543, gi 66 of the regression d5 job at 97,843 instead of 34,490.

Solo never showed it because a solo job's consecutive games on a thread never share a seed. It was
intermittent because whether a same-seed game of another Extra Turns job ran *immediately before* on
the *same worker* depends on scheduling.

## What was ruled out (each measured on base fe0152699)

- **The plan-cache global pool.** A pooled smoke tier at `MTG_PLAN_CACHE_KB=65536` (115,512 refused
  stores) changed 0 job digests vs GT. 37 games moved work counts only (alchemist/kitty d0, plus Gods,
  which has its own known PinScripts issue), and Extra Turns did not move. ET regression d5 gi 66 played
  single-threaded at KB 1 / 4096 / default is identical (603 muted recomputes at KB 1).
- **A different deck's history on the thread.** 145 single-threaded runs (every tier job's first two
  games, then ET d3 gi 28) were all 22,543 units, digest `cac24351`.
- **A same-seed game at a different depth of the SAME job set.** Games 0-2 of the d0/d3/d5 jobs
  interleaved on one thread (435 games): 0 diffs. A carrier needs another Extra Turns job whose trial ran
  at that seed.
- **Tier-2 mana pool:** never refuses. **Wall-clock reads in the engine:** diagnostics only.
- **Any other deck:** the twin battery (below) differs only on Extra Turns.

## How it was found

1. **A reproduction that needs no luck:** 16 replicas of `extraturns_smoke_d3_s1001` (same seeds,
   different job names) in one 32-thread batch, per-game units + work counts (`MTG_TIER1_STATS`) against
   the solo job. Base: 1, 9 and 6 of 800 games off in three runs; with the tier's ET jobs replicated 8x
   among the smoke companions, 7 of 1,800, including the exact gi 66 the regression tier had drifted on.
2. Every off game had `step_f` lower. A temporary backtrace at the free-step site (removed) put all of
   them in `ExtraTurnChainTrial` under `FullSearchLineHybrid`, whose state is keyed on the seed alone.

## Reproducing it

No suite run is needed; the drift shows up deterministically often enough in a replica batch:

1. Build a manifest of 16 copies of the smoke d3 Extra Turns job under different names
   (`extraturns_smoke_d3_s1001__r0` .. `__r15`; each `{"deck": "decks/Extra Turns/Extra Turns.cod",
   "profile": "decks/Extra Turns/Extra Turns.profile.json", "games": 50, "seed": 1001, "depth": 3,
   "budget_ms": 10, "ignore_play_profile": true}`), plus a solo manifest holding the job once.
2. Run each with per-game work stats:
   `MTG_RSS_CAP_GB=20 MTG_TIER1_STATS=<out>.t1 build/Release/mtg --batch <manifest> --threads 32`.
3. Compare each replica's per-game `units` and work counters (`step_c step_f greedy enum plans odo apply
   pay simkey plan_emit`) with the solo job's, keyed on game index. Before the fix, 1 to 9 of the 800
   replica games differ per run, always with `step_f` lower. After it, none do.

A tighter single-threaded variant, the "twin battery": run every searched smoke job's games 0-3 at
`--threads 1`, each game preceded by an identical twin job's same game, and compare each with the solo
game. Only Extra Turns ever differs, so no other deck has a same-seed carrier.

## The fix

`TurnSolver::ClearPerGameCaches` now calls `EtChainClearPerGame()`, which sets the chain state to what a
fresh thread holds. That is exactly the state every solo game already started from (its predecessor on
the thread always had another seed, so the trial reset itself on its first call), so **solo play is
unchanged by construction**; only the same-seed carry is gone.

## Verification

Per-game units AND every work counter (`MTG_TIER1_STATS`) against the solo job (`et3solo`: the three ET
keys alone, 32 threads; = GT digests):

| run | base (fe0152699) | fixed |
|---|---|---|
| 16 x `extraturns_smoke_d3_s1001` replicas, 32 threads, x3 | 1 / 9 / 6 of 800 games off | 0 / 0 / 0 of 800 |
| twin battery: every searched smoke job, games 0-3, each preceded by its own twin, `--threads 1` (580 games) | 1 off (ET d3 gi 1) | ET rows 0 of 12 off, and equal to base solo exactly |
| the tier's ET jobs x8 among their smoke companions (`etrep`), 32 threads | 7 of 1,800 off (incl. regression d5 gi 66) | 0 of 1,800 |
| `et3solo` itself (the three ET keys) at 32 / 8 / 1 threads | = GT (solo) | 0 / 0 / 0 of 225 games off; all three job digests = GT at every thread count |
| smoke tier, pooled (pre-rebase build, 0ec21a795 + fix) | ET d3 / 2HG keys drifted intermittently | ALL PASS, 195 configs unchanged, 0 play changes; ET keys = GT (`76322f02352e42d8` / 417,966, `cc399f1a95984da0` / 162,410) |
| units / smoke / regression on the build rebased onto origin afd791934 (pooled, `MTG_PGO=0`) | regression d5 drifted on gi 66 | units 2,855/2,855; smoke ALL PASS, 195 unchanged; regression ALL PASS, 309 unchanged; 0 play changes in either; `extraturns_regression_d5_s2002` = GT `254115e71dd5ea00` / 1,887,812 |
| units / smoke on the build rebased onto origin 7a2aac806, plus `extraturns_regression_d5_s3003` run SOLO | -- | units 2,905/2,905; smoke ALL PASS, 199 unchanged, 0 play changes; solo d5_s3003 = GT `32c058b7655cac81` (the digest Morph INT-11 re-accepted for gi 36, which that work bisected to its own 5d55fe9f6, not to this drift) |

No GT moved, so nothing was accepted: the committed GT was already the solo (carry-free) play on every key.
