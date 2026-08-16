# Tap-backtrack blow-up: the mixed-class sighting the open defect predicted (HANDOFF)

**Status:** diagnosed with profiles + a deterministic repro; fix NOT implemented. Found 2026-08-16
on Mirrorwing during mulligan generation. This is the residual case
[[tap-backtrack-blowup]] left open.

That doc's Residual section said:

> mixed-class boards (many near-identical but not def-identical sources) still multiply as
> `(count+1)^classes`; **no sighting of that being pathological yet**. A hard node cap + greedy
> fallback remains the documented next lever if one appears.

**One has appeared.**

## Deterministic repro (single rollout, byte-identical to generation)

Both reproduce in isolation and exit after one rollout. Second is the more severe.

```bash
# 11.3 s
MTG_TAP_STATS=1 MTG_KEEP_EXHAUSTIVE=1 \
 MTG_KEEP_REPLAY="Mountain x1; Gruul Turf x1; Goblin Instigator x2; Forest x1; Elvish Mystic x2" \
 MTG_KEEP_REPLAY_R=0 MTG_KEEP_REPLAY_PD=0 \
 MTG_EQUIV_CACHE="decks/Mirrorwing Dragon/Mirrorwing Dragon.keepmodel.gencache.json" \
 ./build/Release/mtg-analyze "decks/Mirrorwing Dragon/Mirrorwing Dragon.cod" \
   --cards-json src/cards/data/cards.json --gen-mulligan recommend --seed 1000000

# 45.0 s  -- same command, this hand:
 MTG_KEEP_REPLAY="Mountain x1; Forest x1; Fists of Flame x1; Elvish Mystic x4"
```

Worst observed in the wild (same deck, from the slow log, not yet replayed): **2.6 HOURS** for one
rollout — `Sandstone Needle x2; Rootbound Crag x1; Ancestral Anger x1; Goblin Instigator x1;
Forest x1; Elvish Mystic x1`, and **39.8 min** — `Kazandu Refuge x1; Goblin Instigator x1;
Forest x1; Elvish Mystic x4`.

## Profile: the share GROWS with severity

`perf record --no-children`, Profile build:

| repro | wall | `TapForCostBacktrackWorker` self | Amdahl bound |
|---|---|---|---|
| 11.3 s | 11.9 s | 42.4% + 6.5% lambda = **48.9%** | 1.96x |
| 45.0 s | 44.3 s | 63.9% + 10.4% lambda = **74.2%** (+2.4% CanTapNow) | **3.9x** |

Profiling a MILD case understates this badly -- worth knowing, because a 1%-looking result on a fast
case does not falsify the defect. Extrapolating the trend, the multi-hour cells are likely 85-95%
payment solving.

## Mechanism, measured (`MTG_TAP_STATS=1`)

```
11.3s case: top-level entries=75,025,401   nodes=75,183,561   max board n=80
            memo-off(n>64) top-level=74,967,160          <- 99.9% of entries
            payable   38,008 entries ->    238,669 nodes (   6.3/entry)
            UNpayable 20,309 entries -> 74,993,745 nodes ( 3,692/entry, 99.7% of all nodes)
            DUP COLLAPSE skips=4,166 (0.00 per node)

45.0s case: nodes=246,811,909   max board n=70
            payable   36,648 entries ->    231,715 nodes (   6.3/entry)
            UNpayable 19,506 entries -> 246,628,130 nodes (12,644/entry, 99.9% of all nodes)
```

Three findings, each independently actionable:

1. **99.7-99.9% of all nodes go to proving costs UNPAYABLE.** Finding a payment costs 6.3 nodes and
   is FLAT across both cases; proving one impossible costs 3,692 then 12,644 and is where all the
   growth lives. Success stops at the first hit; failure must exhaust the space.

2. **The failure memo is disabled on exactly the boards that need it.** Its key is
   `std::pair<std::uint64_t,std::uint64_t>` = a 64-bit tapped-source mask + packed pool, so it is
   gated off at `n > 64` ("bitmask won't fit", SpellEffects.cpp ~1224/1247). Mirrorwing's fan-out
   (Treasures from copied Gold Rushes, Goblin tokens, dorks) reaches **n=70-80**, so 99.9% of
   top-level entries run with NO memo at all.

3. **The 2026-08-12 identical-sibling collapse is inert here** -- 4,166 skips against 75-247M nodes.
   Exactly as predicted: Mirrorwing's sources are mixed-class (Forest x9, Mountain x4, Gruul Turf x2,
   Rootbound Crag x2, Kazandu Refuge, Sandstone Needle, Elvish Mystic x4), so `(count+1)^classes`
   is ~10*5*3*3*2*3*5 = 40,500 per payment and no two candidates are def-identical.

**Why Elvish Mystic tracks the slowdown**: it is another flexible source, so each copy multiplies the
class product by up to 5x. It is not slow in itself.

## Fix directions

- **Widen the memo key past 64 sources** (e.g. `array<uint64,2>`). Directly targets finding (2), and
  is byte-identical by the doc's existing argument (only PROVEN failures are pruned). Most surgical.
- **Cheap unpayable precheck** -- a sound necessary condition (total producible mana < CMC, or
  per-colour availability < pips) rejects in O(n) instead of thousands of nodes. Targets finding (1),
  which is 99.7% of the work, and is byte-identical (it only rejects what the search would reject).
- **Node cap + greedy fallback** -- the doc's documented lever. Bounds the worst case but is NOT
  byte-identical, so it needs a GT rebaseline.

## Levers already in the tree

- `MTG_TAP_STATS=1` -- the stats block above.
- `MTG_NO_TAP_DUP_COLLAPSE=1` -- same-binary A/B for the sibling collapse (must stay byte-identical).
- `MTG_KEEP_REPLAY` / `_R` / `_PD` -- the single-rollout replay harness used above.

## Why this matters beyond one deck

Mulligan generation for Mirrorwing is **405,756 cells**; the probe alone projected 9+ hours with
throughput decaying 57 -> 10 cells/s as pathological cells accumulated. Generation is dominated by
this tail, and the tail is the most backtracker-dominated part of it.
