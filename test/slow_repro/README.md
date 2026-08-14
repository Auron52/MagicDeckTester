# Saved degenerate-rollout reproducers

Slow-rollout captures kept for PROFILING. Everything a `[keepgen] SLOW-ROLLOUT` line carries -- hand
(as bucket labels), side, rollout index and the seed -- is enough to replay that one rollout in
isolation, byte-identically to the run that produced it. The gen's own `<raw>.slow.log` is gitignored
and dies with the run's scratch, so anything worth profiling later is copied here.

## FiveColour keep-generation, 2026-08-14

`fivecolour_keepgen_slow.txt` -- 253 rollouts over 30 s from the R=1 `--gen-mulligan recommend` scout
(7h31m on 23 cores, stopped at the user's request; the profile was never going to be shippable at R=1,
the slow games were the deliverable). 506 minutes of rollout time; worst single rollout **1,851 s**.

What they say, ranked by share of slow-rollout time: **Bloom Tender 78.3%** of slow hands, top
co-occurrence **Faeburrow Elder** (85 hands). Those are the same effect -- *"{T}: For each color among
permanents you control, add one mana of that color"* -- and everything else on the list is a land that
feeds them. One atom, not five problems: **mana payment over variable-output sources.** See
`docs/design/fivecolour-mulligan-and-slow-atom.md`.

`fivecolour_keepgen_scout.log.snapshot` is the scout's full console (config, bucket count, progress).

### Replaying one

`MTG_KEEP_REPLAY` reconstructs and runs exactly one keep-rollout, times it, and exits -- so it drops
straight under `perf`:

```sh
MTG_EQUIV_CACHE=<unpacked gencache> \
MTG_KEEP_REPLAY="Jetmir's Garden x1; Jared Carthalion x2; Garth One-Eye x1; Faeburrow Elder x3" \
MTG_KEEP_REPLAY_R=0 MTG_KEEP_REPLAY_PD=0 \
  ./build/Profile/mtg-analyze decks/FiveColour/FiveColour.cod \
    --cards-json src/cards/data/cards.json --gen-mulligan recommend --seed 1000000
```

`_R` / `_PD` (1=play, 0=draw) plus `--seed` pin the exact `rs = f(seed, r, w, pd)` printed on the
capture line. Build with `./build.sh profile` for a faithful stack.

### The bucketing matters -- these captures predate the fetchland merge

`FiveColour.keepmodel.gencache.json.gz` is the equivalence cache **as the capture ran** (K=31, nothing
merged), kept because a capture line is meaningless without it: the hand is a list of BUCKET LABELS
and the cell index `w` (which, with the seed, picks the rollout) is a position in that bucketing.

Discovery has since gained the by-construction fetchland merge -- FiveColour K=31 -> 27 -- so these
captures are **not** exactly replayable on current `HEAD`, and pointing `MTG_EQUIV_CACHE` at the
pinned file will not restore them: the cache is now gated on `kDiscoveryVersion` too, so a pre-merge
cache MISSES by design and discovery re-derives the merged classes. Two honest routes:

* **Exact**: check out the capture's commit (`f5b1600`'s parent tree or earlier) and replay there --
  the same rule as every other artifact in this repo, which are engine-state fingerprints.
* **Structural** (usually what you want): run the hand as-is on current `HEAD`. Non-fetch labels all
  still resolve; you get a *different* rollout of the same hand, and the property being profiled --
  a board with Bloom Tender / Faeburrow Elder plus fixing -- is what makes it degenerate, not the
  particular seed.
