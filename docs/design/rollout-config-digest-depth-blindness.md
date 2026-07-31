# `RolloutConfigDigest` is blind to depth / budget / max_turns

**Status: FOUND AND MEASURED 2026-07-31, NOT FIXED — the fix invalidates existing sidecars' reuse,
so it is the user's call.** Found while building runtime coverage for the keep generator's carry
paths (`test/lib/keepgen_check.sh`), not by inspection: the probe-carry path advertises
"byte-identical" and measurably is not.

## The claim, and the measurement that breaks it

`src/analyzer/ExhaustiveKeep.cpp: RolloutConfigDigest(deck, profile, depth, budget_ms, max_turns)`
is the `play_digest` stamped into every raw sidecar, probe chunk and runtime profile. It is the gate
on every cross-run reuse in the generator:

| consumer | what the digest lets it skip |
| --- | --- |
| probe carry (`use_probe_carry`, set by `--gen-mulligan complete` and `fast`) | the r=0 rollout of **every** cell |
| prior-raw carry `reuse_all_cells` (`MTG_KEEP_PRIOR_RAW`) | **the entire prior table — zero fresh rollouts** |
| cross-machine pooling (`RunKeepMerge`) | nothing — but it decides whether two chunks may be pooled at all |

The function takes `depth`, `budget_ms` and `max_turns` as parameters and **never hashes them**. It
folds one thing: the per-game logger digests of a fixed 64-game goldfish battery. So it detects a
depth change only if that change happens to alter one of those 64 goldfish games.

For `decks/burn`, it does not:

```
rollout-config play digest (d5/b20, 64-game battery): 6b778107926aa886     <- env path,     depth 5
rollout-config play digest (d6/b20, 64-game battery): 6b778107926aa886     <- recipe path,  depth 6
```

Same hash, and the header prints the depth it is supposedly fingerprinting right next to it.

Meanwhile the keep rollouts the digest is gating *do* differ between those two depths — measured on
`decks/burn`, seed 4242, 4 equivalence probes, K=5, max_mull=6, 791 cells:

```
recommend probe (d6) r=0  vs  fresh uniform R=1 (d5) r=0   ->  16 / 791 cells differ (~2%)
                                                               H=7: 8/330  H=6: 5/210  H=5: 1/126  H=3: 2/35
```

so a `complete`/`fast` gen that reuses a probe rolled at a different depth silently mixes the two.
Both runs are *individually* deterministic (repeat runs: 0 differing cells), so this is systematic,
not flakiness.

## What was ruled out first

Each of these was measured, not argued, because each is a more likely-sounding culprit:

| suspected cause | test | result |
| --- | --- | --- |
| wall-clock / scheduling (`budget_ms` is a **time** budget) | same config, 24 threads vs `taskset -c 0-5` | **0 / 791** — rollouts are scheduling-independent |
| adaptive bottoming (`cfg.adaptive_bottom`) | uniform R=1 with vs without it | **0 / 791** |
| the adaptive floor path's seed stream (`r_floor` > 0) | R=2/r_floor=1 vs uniform R=1, comparing only cell-sides still at count==1 | **0 / 1085** — the `work_idx` seed-stream invariant holds |
| bucketing / decklist | `bucket_fp`, `deck_fp`, `equiv_seed`, `K`, `max_mull` | all matched (the carry engaged) |
| the carry mechanism itself | plain R=10 vs probe-carry R=10 | the 32 differing (entry,field) pairs are **exactly** the 16 cells where the probe's r=0 differs — the freshly-rolled r=1..9 matched plain's r=1..9 exactly |

The last row is the important one: **preloading works correctly.** It preloads a value that was
rolled under a different config.

## Why this is worse than one rollout in forty

Probe carry perturbs 1/40 of the samples in ~2% of cells — small, and both values are legitimate
rollouts of the same cell, so it is a *fidelity* break rather than data corruption.

`reuse_all_cells` is the severe one. When the prior's `play_digest` equals this run's, the generator
reuses the **whole** prior table and rolls nothing:

```
[keepgen] EXECUTION-TRACE: <n> cells reused from the identical-play prior (whole-pool)
```

Regenerate a deck at a different depth after a play-logic change and that is exactly what a
`MTG_KEEP_PRIOR_RAW` carry will do — report a full regeneration while having rolled zero games at the
new depth. The chunked multi-machine workflow drives depth/budget through env overrides, so the
configurations differing between two chunks is a normal operating condition, not an exotic misuse.

## The fix (one line, not applied)

Fold the three parameters into the hash alongside the battery, which is what the name already
promises:

```cpp
uint64_t fold = 1469598103934665603ULL;
fold = Fnv("d" + std::to_string(depth) + "/b" + std::to_string(budget_ms)
                + "/t" + std::to_string(max_turns), fold);
for (int gi = 0; gi < kGames; ++gi) { /* ... unchanged ... */ }
```

**Consequence, and why it is not applied unilaterally:** every existing sidecar, probe and profile
carries a battery-only digest, so after the change none of them match a fresh run. Every carry then
*refuses* and re-rolls. That is the safe direction (refuse rather than wrongly accept) but it is not
free: it discards the reuse value of already-generated artifacts, including any long-running
generation in flight. Rebuilding them is the user's call, not an agent's.

A cheaper variant, if preserving existing artifacts matters more: keep `play_digest` as-is and add a
separate `rollout_cfg` field (`{depth, budget_ms, max_turns}`) to the raw meta, gate the carries on
both, and treat a *missing* `rollout_cfg` as "unknown -> refuse to reuse whole-pool, allow per-cell".
More code, no forced regeneration.

## Reproducing

```bash
D=/tmp/probe_exp; mkdir -p $D; cp -r decks/burn $D/
# depth 5 (env path)
MTG_KEEP_EXHAUSTIVE=1 MTG_KEEP_ROLLOUTS=1 MTG_EQUIV_PROBES=4 MTG_KEEP_OUT_RAW=$D/d5.raw \
  ./build/Release/mtg-analyze $D/burn/burn.txt --seed 4242
# depth 6 (recipe path) -- writes $D/d6.raw.probe
MTG_EQUIV_PROBES=4 MTG_KEEP_OUT_RAW=$D/d6.raw \
  ./build/Release/mtg-analyze $D/burn/burn.txt --seed 4242 --gen-mulligan recommend
grep -h "play digest" ...          # same hash, different advertised depth
```

Related: [`exhaustive-keep-policy.md`](exhaustive-keep-policy.md) (the pooling identity),
[`per-deck-folder-layout.md`](per-deck-folder-layout.md) (raw-artifact policy),
`.claude/skills/mulligan-profile.md` (the commit-bound generation rule this digest implements).
