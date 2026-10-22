# NivMizzetCombo: the Brainstorm put-back fan blows the RSS cap (2026-10-22)

The NivMizzetCombo mulligan generation (`mullgen.sh run decks/NivMizzetCombo fast`) died twice in its
settings screen on 2026-10-22 at the engine's RSS watchdog (`MTG_RSS_CAP_GB`, 35.28 GB on the 47 GB
box): the d1 b1 scorer arm at 35.9 GB, then phase F 2/2's discovery at play settings (d5 b20) at
39.65 GB, 31% through. The tracked pools were small (`plan=93M/1205M`, `fsl=0M`), so the memory was
untracked. This note records what holds it, what was fixed, what was not, and the deferred real fix.

## What holds the memory

**One rollout-turn candidate list.** A jemalloc heap profile of the d1 b1 arm at its peak
(`MALLOC_CONF=prof:true,prof_gdump:true`, `MTG_POOL_ALLOC=0`, Profile build) attributes 97-99% of the
live heap to

```
AppendSubdecisionAxes  TurnSolver.cpp  `TurnSolver::Plan v = p;`   <- the SEARCHED BRAINSTORM PUT-BACK axis
  <- EnumeratePlansWithLandUncachedBody <- EnumeratePlansWithLand
  <- TurnSolver::SolveWithLookahead (candidates = ..., nested, depth 1, not enforcing)
  <- RolloutPreCombatSolve <- SimulateToEndImpl (a future turn of the leaf rollout)
  <- SimulateToEnd(EffectiveFdLeafDepth() = 1) <- FSLineWin (the d1 root's leaf) <- FullSearchLine
  <- AIEngine::TakeTurn <- RolloutWinTurn <- RolloutKeepWinTurn <- RunScoreCompsMode
```

Plan copy (its vector of 496-byte `Action`s) 75-79%, the `extra` vector's growth 15-17%, the final
`all.insert` 1-7%. It is concentrated: 5 of 24 threads held 1.1-7.5 GB each, the rest under 0.2 GB.
It is NOT the breakpoint continuation lists (`BpEnumEntryFor` / `BpDeriveContinuationList`: 0.8%).

**Why the lists are that big.** `MTG_PUTBACK_TRACE=1` on the same arm:

```
[putback] T5 11104 base plan(s) fanned, widest 320, +3267460 variants (of 108910 plans)
[putback] T5 13880 base plan(s) fanned, widest 321, +3225210 variants (of 111980 plans)
[putback] T5  3402 base plan(s) fanned, widest 320,  +927686 variants (of 28120 plans)
```

The put-back fan is one clone per candidate rank up to `PutBackAxisWidth`, an UPPER bound. When the
plan also holds a Ponder / fetch / tutor (`PutBackPredictionUnsafe`) the bound is the class count over
hand + the WHOLE library, ~320-360 ordered pairs, against a resolution hand that offers far fewer. The
excess ranks clamp to the last candidate and `PutBackFoldMemo` declines them before any unit is
charged -- so they cost no units and change no result, but every one of them is MATERIALISED first,
as a full `Plan` copy: 3.3M clones of ~2 KB in one list, every time a leaf rollout's turn re-solves
such a board at depth 1. Several threads reach the same board together (discovery probes the same hand
with each candidate card), which is how discovery went from 10.8 GB to 39.5 GB in four seconds.

**And the pool kept it.** `PoolAllocator` (default ON) parked every freed block of <= 512 bytes on a
per-thread free-list forever. A 1-action plan's `Action` vector is one such block, so each giant list
ratcheted its thread's footprint up for good: with the pool the d1 b1 arm climbed monotonically to the
cap in under a minute; with `MTG_POOL_ALLOC=0` the same arm fell back to 1-6 GB between lists.

## Fixed (sound, byte-identical): the pool's retention cap

`MTG_POOL_ALLOC_CAP_MB` (default 64 MB per thread; `=0` = the old unbounded retention). A freed small
block past the cap goes straight back to malloc. Changes where memory lives, never a value: identical
scorer labels and work units (d1 b1, 48 openers x R=2, against the uncapped binary). This removes the
RATCHET, not the peak. `MTG_PUTBACK_TRACE` also prints a `[putback-big]` line while a fan crosses 1M /
2M / 4M ... clones, so a list that kills the process still names its site.

## NOT fixed: the peak itself -- and no result-neutral workaround survives discovery

Every run below is the Release binary at 1aa6cc32a (+ the cap where stated), a 33 GB test cap:

| run | threads | pool | result |
|---|---|---|---|
| d1 b1 scorer arm (48 openers, R=24) | 24 | on (old) | dies at 48 s, monotonic climb |
| d1 b1 scorer arm | 24 | off | completes, 120 s, peak 29.9 GB |
| d1 b1 scorer arm | 24 | capped | dies at 49 s (33.7 GB) |
| d1 b1 scorer arm | 12 | capped | completes, 143 s, peak 19.1 GB |
| discovery, d5 b20 (7,600 rollouts) | 24 | off | dies at 29% (10.8 -> 39.5 GB in 4 s) |
| discovery | 12 | capped | dies at 30% (5.0 -> 35.4 GB in 4 s) |
| discovery + `MTG_PUTBACK_TRACE` | 24 | capped | dies at ~29% (20.4 -> 33.8 GB in 2 s) |

Discovery dies at the same point whatever the threads or the pool: rollout ~2,250-2,400, probes 71 / 95
/ 112 (the same opener tried with each candidate card, so several threads reach the same boards at
once). The traced run shows the shape -- all of them a leaf rollout's nested solve
(`rollout_nest=1 solver_nest=1 bp_enum_depth=0`, `fsline_nest` 1-3), late turns, a hand of 17-21 cards:

```
[putback] T6 7346 base plan(s) fanned, widest 357, +2257233 variants (of 58766 plans)
[putback] T8 6792 base plan(s) fanned, widest 285, +1746374 variants (of 66744 plans)
[putback] T6 4514 base plan(s) fanned, widest 356, +1292210 variants (of 27908 plans)   (x3, same board)
```

A list is several GB, so no cache cap or thread count BOUNDS it; fewer threads only lowers the odds.
(Discovery throughput: ~5/s at 12 threads against ~6/s at 24 -- these rollouts are bound by the copying.)

## The real fix (DEFERRED -- needs the USER)

Three shapes; each removes the materialisation (or most of it):

1. **Lazy put-back block (identity fold, byte-identical).** The enumeration records each fan
   (base index, width, group) and one prototype clone instead of the clones; the candidate list is a
   view whose put-back block (and the soulbond twins of its members, which `AppendSoulbondDeclineVariants`
   also makes) is synthesised per index. `PutBackFoldMemo::Declines` reads only (group, choice), so a
   declined rank is never synthesised at all. The `niv-uncharged` branch's `MTG_BP_PUTBACK_LAZY` does
   this for breakpoint continuation lists only; this needs it for `SolveWithLookahead`'s candidates
   (and FSLineWin's), whose loop indexes by `&plan - candidates.data()` and whose pre-passes (combat-pin
   share, put/etb/etbc folds, the wave walker, the escalation pool) read the whole list. A day's work
   with a verify mode (synthesised == materialised at every index), not an evening's.
2. **A tighter width (sound collapse, moves budgeted play).** Bound the fan by the classes the
   resolution hand can actually hold (hand minus the plan's earlier casts plus the cards drawn before
   the put-back), not hand + whole library. Every distinct candidate stays reachable at every budget;
   only clamped duplicates go. They are declined free of units today, but the list size feeds the
   root overrun guard and the wave walker, and an incomplete sibling apply leaves a duplicate charged,
   so this is a play change: held-out A/B + GT acceptance. It also cuts the copying that makes these
   rollouts take 37-70 s.

3. **Copy-on-write `Plan::actions` (byte-identical, every deck).** A clone then costs its 472-byte
   shell, not its Action vectors (~76% of the profile). Broad: ~400 const uses convert implicitly, ~21
   non-const sites change, and every "mutable reference into `actions`, then copy the plan, then write
   through the reference" pattern would leak the write into the copy, so it needs that audit and a full
   suite pass.

Until one of them lands, Niv's mulligan generation cannot run: its settings screen and discovery reach
these boards deterministically.
