# FiveColour: the mulligan run is impractical, and what its slow games told us

2026-08-14. Two findings from the R=1 `--gen-mulligan recommend` scout, plus one general rule.

**Status 2026-08-14 (later the same day): both findings are now CODE.** The fetchland rule is a
by-construction merge in `DiscoverEquivalence` (K=31 -> 27 on this deck, size-7 5,655,953 ->
1,977,898 -- the predicted 2.86x, measured); the atom's first lever is a mana-cache fix (section 4).
The scout itself was stopped at 7h31m/25.2% at the user's request -- R=1 was never going to ship a
profile, and the slow games were the deliverable. Its 253 reproducers are preserved in
`test/slow_repro/` (the gen's own `.slow.log` is gitignored scratch).

## 1. ALWAYS merge fetchlands (user rule, 2026-08-14)

Equivalence discovery at the documented threshold (`MTG_EQUIV_THRESHOLD=0.01`) merged **nothing** on
this deck: K=31 raw buckets, every card its own dimension. The fetch cycle is why —

```
Windswept Heath  ~ Verdant Catacombs   0.0125
Wooded Foothills ~ Misty Rainforest    0.015
Scalding Tarn    ~ Wooded Foothills    0.035     <- all five, all ABOVE 0.01
Lightning Greaves ~ Nicol Bolas        0.06      <- must NOT merge
```

Five functionally interchangeable fetchlands each took a dimension, at a measured distance of at most
0.035 turns. The cost of that is not marginal:

| buckets | size-7 hands | vs K=31 |
|---|---|---|
| K=31 (as run) | 5,655,953 | -- |
| K=27, fetch cycle merged | 1,977,898 | **2.86x smaller** |
| K=20, + duals merged | 351,480 | 16x -- but see below |

**The rule: fetchlands always merge, as a CLASS, regardless of threshold.** They are a known
equivalence, and leaving it to a distance threshold means a deck-by-deck coin flip on a 2.9x cost
difference. A threshold of 0.04 happens to do it here (merging the cycle and stopping short of
Greaves~Bolas at 0.06) but that is luck, not a rule.

**Do NOT extend this to duals.** Their distances (Steam Vents ~ Mountain 0.0725, Island ~ Steam Vents
0.0775) are ~5x the fetchlands' and that is real signal: this deck plays Faeburrow Elder and Bloom
Tender, whose output scales with the COLOURS you control, so which dual you hold genuinely matters.
The 16x is a trap.

Caveat on the fetches themselves: they are not strictly interchangeable either (different colour
pairs), so merging is an approximation bounded by the measured 0.035. It is accepted because the deck
runs enough duals to cover most fetch targets.

## 2. The profile is impractical for this deck either way

At K=31 the scout reached 25.2% of the size-7 phase in 7h11m on 23 cores (1,426,276 of 5,655,953
cells at 55 cells/s), i.e. **~21 more hours for the R=1 floor pass alone**, before the 3,097,330 fused
sub-table batches. Merging fetches brings the whole pass to roughly 10 hours -- still too long
(user). And R=1 is not shippable: **R < 10 cannot produce a runtime profile** at all.

So FiveColour stays on mulligan defaults. This is the K=31-with-1-ofs case the skill's feasibility
guide calls out; the deck is a 60-card singleton-heavy pile and is simply not compressible enough.

## 3. What the slow games DID buy: the atom is variable multi-colour mana

253 slow rollouts (>=30 s), 506 minutes total, **worst single rollout 1,851 s -- 31 minutes**. Ranked
by share of slow-rollout time:

```
Bloom Tender        198 hands  25,602 s   78.3% of slow hands
Mountain            103        15,759     40.7%
Blood Crypt          95        15,273     37.5%
Overgrown Tomb       91        12,509     36.0%
Faeburrow Elder     115        11,376     45.5%
```

Top co-occurrence: **Bloom Tender + Faeburrow Elder (85 hands)**.

Those two are the SAME effect -- *"{T}: Add one mana of each color among permanents you control"* --
a mana source whose output is a function of board state rather than a fixed set. Everything else on
the list is a land that feeds them. So the ranking is not five separate problems; it is one:

> **Mana-payment enumeration over VARIABLE-OUTPUT sources is the degenerate atom.**

This corroborates the independent `perf` profile of the depth-matrix tail (flat profile, mana payment
the largest coherent cluster at 22.3%, one stuck thread at 39% in mana backtracking) from a completely
different measurement path. Two routes, one answer.

**Why it matters beyond this deck:** the same class appears wherever a deck plays Bloom Tender /
Faeburrow Elder / Chromatic Lantern-style effects, and the cost is combinatorial breadth, which no
micro-optimisation touches. A fold or memo keyed on the *realised colour set* rather than the source
permutation is the shape of the fix -- the sibling tap-backtrack collapse is the precedent.

Reproducers: `test/slow_repro/fivecolour_keepgen_slow.txt` (all 253, with the pinned bucketing and a
`MTG_KEEP_REPLAY` recipe -- each one replays as a single timed rollout, straight under `perf`).

## 4. First lever: a domain source was TURNING THE MANA CACHE OFF

The payable-mana cache (`MTG_MANA_CACHE`, default on) memoises the batch-prepay solve. Its key is
built by scanning the active player's mana sources -- and it **bailed on the whole board** if any of
them had state-dependent branching:

```cpp
if (McStateDependent(d)) { return false; }   // domain_mana || IsScaledManaLand
```

So one Bloom Tender on the battlefield disabled the payment memo for every solve for the rest of the
game. That is exactly backwards. A domain source is the card that makes payment enumeration expensive
-- its tap yields 2-5 mana at once, multiplying the orderings the DFS explores -- and it was the card
that switched off the memo meant to absorb that.

The fix is the one `reflecting` already used: don't bail, **hash the realised quantity**. Both scaling
kinds are invariant across a solve (payment only TAPS -- nothing enters, leaves or changes colour), so
the value read at key-build time is the value every branch reads: hash the domain COLOUR SET, hash the
scaled CREATURE COUNT, and two boards that differ in either get different keys. Everything else those
paths read (`ScaledManaFeederMana`) is already keyed.

Measured, `MTG_MANA_CACHE_SCALING=0` vs `=1`, FiveColour:

| | legacy (bail) | hashed | |
|---|---|---|---|
| d5 x75 + d3 x150, digests | `ab95fa49…` / `9d09ffbd…` | **identical** | byte-identical, per-game logs too |
| d3 x150 | 228,595 ms | 204,331 ms | -10.6% |
| d5 x75 | 108,672 ms | 106,333 ms | -2.2% |
| one degenerate rollout (156 s capture) | 55,557 ms | 50,063 ms | -9.9%, same `win_turn=9` |

`MTG_TAP_STATS` now reports the cache's own accounting (hit / miss / unstorable / skipped), added
with this fix because "-10%" does not say WHY. One rollout, both arms:

```
legacy   hit=122,011 (66.4% of consulted)  miss=61,823   skipped: shape=227,220 key=282,238
hashed   hit=335,105 (71.9% of consulted)  miss=130,967  skipped: shape=227,220 key=0
         nodes 68.8M -> 53.5M (-22%)   top-level entries 571k -> 358k (-37%)   wall -32%
```

The bail was refusing **282,238 lookups in a single rollout -- 60.6% of every call that had the right
shape** -- and they hit at 71.9% once consulted. So the mechanism works exactly as intended.

**Honest reading: real, free, and NOT the whole atom.** It is worth having unconditionally (-32% on a
healthy rollout, byte-identical, no cost), but the degenerate tail only moved -9.9%, and a 31-minute
rollout does not become tractable at -10%. Two reasons the tail resists it, both now measured:

1. The cache skips REPEAT solves; it does not narrow ONE solve. The tail is a small number of
   enormous solves (494 nodes per unpayable entry, 67-71% of all nodes spent PROVING FAILURE), which
   is DFS breadth -- a fold keyed on the realised COLOUR SET rather than the source permutation is
   the shape of that fix (the sibling tap-backtrack collapse is the precedent).
2. **Half of all misses could not be stored** (49.6% unstorable) -- a solved payment whose tap-set
   includes a source the hit path could not replay. On this deck that is Deathrite Shaman (4 copies):
   its tap EXILES a graveyard land. Every one of those solves was paid for and thrown away.
   **Now fixed -- see section 5.**

## 5. Every source is replayable (the store gate is gone)

The store gate rejected any solution tapping a storage, drip or Deathrite source. A tap-set alone
genuinely cannot reproduce those, but a tap-set plus the two ORDER-DEPENDENT choices can, provided the
replay **calls the same functions the DFS called instead of storing deltas**:

| effect | how the hit path reproduces it |
|---|---|
| storage counter burn | recorded per tap as a DELTA (depends on the floating pool at that point in the tap order -- not derivable) |
| drip lifegain | one aggregate amount (life arithmetic is order-independent; only drip moves opponent life during a payment) |
| drip SIGN | `OpponentGainsLife` re-reads `RemedyActive` -- a Tainted Remedy board flips gain to loss without the key ever seeing the enchantment |
| graveyard exile | re-runs `ExileGraveyardLandForMana` -- the key hashes the fuel COUNT, not the contents, so each board exiles ITS OWN first land, which is what the DFS would have done to it |

That last row is the whole idea: replaying by CALLING means anything the replayed function reads is
resolved against the board the hit is actually landing on, so entries stay shareable across boards the
key deliberately does not distinguish.

Measured on FiveColour d3 x60 (`MTG_MANA_CACHE_ALLSRC=0` vs `=1`):

```
unstorable   275,823 (56.3% of misses)  ->  0
hit rate     71.3%                      ->  83.9%
misses       489,732                    ->  274,665   (-44%)
backtracker nodes  89.8M                ->  73.1M     (-18.6%)
```

### 5a. Deferred: the cache covers ONE of the engine's two payment questions

`MTG_TAP_STATS` now splits the backtracker's nodes by the cache's REACH. A hit costs no nodes, so
those two buckets are the whole payment cost. FiveColour d3 x14, `--threads 1`:

```
hit 380,216 (82.0% of consulted)   miss 83,699        skipped-shape 101,214
nodes:  miss 6,098,803 (45.0%)     skipped 7,443,961 (55.0%)      [sums to 100.0%]
per entry:   72.9 (miss)      vs        73.5 (skipped)
```

The cache is not failing at what it covers. It covers one shape:

| question | call site | cached? |
|---|---|---|
| "can I afford this whole plan?" (`out_full_pool`) | `TurnSolver.cpp` batch prepay | YES -- 82% hit |
| "pay this one cost, tell me the leftover" (`out_leftover`, sometimes non-empty floating) | `TapForCostSharedOnce`'s backtracker fallback | **no** |

The uncovered shape is 17.9% of calls but **55% of the nodes**, and is never memoised, so identical
repeats re-solve every time. Per-entry cost is the same in both (73.5 vs 72.9), so it is not that
those calls are pathological -- there are just a lot of them.

**DONE -- and it was worth more than the estimate.** See section 5b.

### 5b. Both payment questions are cached -- payment nodes HALVED

Why memoise the per-cast solves instead of widening the batch prepay to map the whole turn? Because
**pre-floating is actively wrong for a scaling source** (user, 2026-08-14). The prepay pre-loads the
combined cost as floating, which means tapping Bloom Tender UP FRONT -- banking today's colour count
and throwing away the extra mana it would make after the next cast resolves. The same pre-float also
spends mana a later phase may want held. Memoising keeps payment lazy and in-order (each subset paid
when it is due) and makes only the REPEATS free, which is what was actually expensive.

One trap on the way in, and it would have been silent: **`collapse_colors` is gated on
`out_leftover == nullptr`.** The leftover pool gets floated and later drained colour-sensitively, so
that mode must not recolour; the batch mode may. The two modes therefore run different searches and
can find different first solutions for the same board and cost -- sharing one entry between them would
have handed a collapsed-colour pool to the one caller that reads colours. The output mode is now part
of the key, so they memoise separately. The incoming floating pool is keyed too (the filter-feed retry
passes a ritual's reserve); it is empty on the batch path, so every pre-existing entry keys unchanged.

Measured, FiveColour d3 x14, `--threads 1` (`MTG_MANA_CACHE_LEFTOVER=0` vs `=1`):

```
skipped-shape       101,214 (55.0% of nodes)  ->  0 (0.0%)
backtracker nodes   13,542,764                ->  6,584,731     (-51.4%)
top-level entries   184,913                   ->  92,888        (-49.8%)
hit rate            82.0%                     ->  83.6%
digest              85007c517c8d89d9          ->  85007c517c8d89d9
```

Wall, same 60 games at 12 threads: cache OFF 72,546 ms / batch-only 68,753 / both shapes **65,922**.
So the cache's value roughly DOUBLES (-5.2% -> -9.1% off the no-cache baseline), and the payment DFS
is cut in half.

That also revises section 5a's own estimate upward: I sized this at "~2-4% at best" from the ~7%
payment share, and it landed at ~4.1% of total runtime. The estimate was low because the uncovered
shape turned out to be half the nodes, not a fraction of the remainder.

### 5c. Canonical (type-multiset) keying -- big win, NOT correct yet, default OFF

Where mana actually sits after 5a/5b, `perf` over the live keepgen pool on HEAD (669K samples, 60 s,
all threads, categorised by subsystem):

```
SEARCH: solve        26.47%      MANA: payment solve   10.30%
OTHER (flat tail)    32.59%      MANA: pool/produces    4.61%
STATE copy/alloc     11.13%      MANA: cost/eligibility 3.49%
EVAL                 10.70%      ---- ALL MANA         18.40%
```

The cache key identifies each mana source by **battlefield INDEX**, so two untapped Mountains pose the
same payment problem under different keys and a wide board re-solves it once per permutation of which
copy is which. Keying by the sorted MULTISET of source descriptors instead:

```
hit rate            83.6%      ->  92.7%
misses              92,888     ->  42,247      (2.20x fewer solves)
backtracker nodes   6,584,731  ->  1,833,273   (-72.2%)
nodes per entry     70.9       ->  43.4
```

The equivalence is not invented: it is the one the DFS's own identical-sibling collapse (`s_dup_of_buf`)
uses -- same def, chain-eligibility, storage counters/hold, full counters vector. That collapse is also
what makes realisation cheap: only the lowest-index currently-untapped member of a group is ever
explorable, so a solution taps a PREFIX of each group, and an entry can store (descriptor, RANK)
instead of an index.

**Three things went wrong. All are now understood; the net result is a negative.**

1. *Fixed.* The first sort-free version accumulated `ms2 += dh * C`, and `sum(dh*C) == C*sum(dh)` -- a
   linear image of `ms1`, so the "128-bit" key was really 64 with a verify that merely re-derived it.
   Caught by a per-game log diff on a 200-game job that a 14-game digest check had passed.

2. *Fixed.* Dropping the sort was itself necessary: the sorted version measured Dragonstorm **2-3%
   SLOWER**, a sort per call outweighing the solves it saves on a low-redundancy board.

3. *ROOT CAUSE, fixed.* The multiset key was **unsound**, and not because the descriptor was missing a
   field. The DFS iterates candidates in BATTLEFIELD-INDEX order, so two boards with the same source
   multiset but a different INTERLEAVING explore differently and find different first solutions:

   ```
   Board A: [Mountain@1, Island@2]  -> tries Mountain first
   Board B: [Island@1,  Mountain@2] -> tries Island   first
   ```

   For a cost either can pay, a tap-set cached from A is simply the wrong answer for B. Rare by
   construction -- 6 diverged games in 1000 at `treasure_hunt` d0, while a 1,300-game A/B on three
   other decks passed clean.

   The sound weakening is to key on the ordered **SEQUENCE** of descriptors rather than the multiset:
   same sequence => isomorphic search => same solution, stored as ordinal positions in the source list.
   Absolute indices still drop out (the part that created spurious keys); the interleaving does not.
   Verified: `treasure_hunt` d0 divergences **6 -> 0**, smoke 36/36 with 0 play-changed, identical
   digests AND per-game logs across the 1,300-game six-config A/B.

**And then it does not pay.** The sequence key cuts solve nodes only **-22%** (6,584,731 ->
5,116,093) where the unsound multiset version cut 72% -- and -22% of a ~10% slice does not cover
hashing a descriptor per source on every call. Best-of-two at 20 threads, indexed vs canonical:

```
al_d5  -1.4%    fc_d3  +0.8%    ds_d3  +1.7%    ds_d5  +2.3%    fc_d5  +2.3%    al_d3  +4.5%
```

Consistently negative, even after deduplicating the `CanTapNow` call the first cut made twice per dork.
So it ships **default OFF** (`MTG_MANA_CACHE_CANON=1` to enable).

**The 72% is still on the table, but it is no longer a byte-identical edit.** It requires
canonicalising the DFS's OWN iteration order -- sort candidates by descriptor rather than by index --
which makes the multiset key sound by construction. That changes which solution is found, so it is a
play change needing a full A/B and a GT rebaseline. Whether a 72% cut to a ~10% slice justifies moving
every deck's ground truth is a judgement call, not a perf edit.

### 5d. Does mana grow on the tail? NO -- it shrinks. (2026-08-15)

The premise behind attacking mana was that its 18.4% share would GROW in the degenerate regime. It
does not. Profiling one genuinely slow keep-rollout (harvested fresh at HEAD, 16,808 samples,
single-threaded so the shares are clean) against the healthy gen pool:

```
category                  healthy gen   TAIL rollout    delta
  MANA: payment solve         10.22%          7.63%     -2.59
  MANA: pool/produces          4.61%          3.79%     -0.82
  MANA: cost/eligibility       3.49%          3.38%     -0.11
  SEARCH: solve               26.47%         21.38%     -5.09
  EVAL                        10.70%         11.34%     +0.64
  MACHINERY                   20.85%         21.94%     +1.09
  OTHER (flat tail)           22.95%         31.14%     +8.19
  >>> ALL MANA                18.32%         14.80%     -3.52  (0.81x)
```

Mana and search both SHRINK on the tail; the extra cost goes into an even flatter tail of everything
else. The tail rollout's biggest single symbol is `MidGameEvaluator::Score` at 6.32%, then
`SolveUncached` 4.88%, `CollectActions` 4.09%, `GameState` copy 3.24%. There is no dominator anywhere.

**Caveat, stated plainly: n=1, and it is the wrong shape.** Slow rollouts are rare enough (one over 8 s
in four minutes on 24 cores) that this is a single sample, and its hand is Deathrite Shaman x4 -- NOT
the Bloom Tender board that made up 78.3% of the original corpus. So this does not settle the Bloom
Tender case; it only shows that "the tail is mana-bound" is not true in general.

**What this closes.** Three rounds of mana work (scaling-source keying, the leftover shape, canonical
keying) delivered one real win (the leftover shape, -9.1% off the no-cache baseline) and two
measured-negative results. The remaining mana slice is ~4% solver plus ~10% helpers, in a regime where
mana is not even the largest block. Per the user (2026-08-15): "it's only worth optimizing where there
is real headroom; digging into 4% isn't worth our time." Mana optimisation is CLOSED unless a
Bloom-Tender-shaped tail profile reopens it.

**Where the headroom actually is,** if this is picked up again -- MACHINERY at ~21% (allocation, copies,
container churn: `operator new` 3.1%, `GameState` copy 2.2-3.2%, `memset` 2.2%, `malloc`/`free` ~2%) is
the largest coherent block that is not algorithmic, and reducing it is behaviour-neutral. `EVAL` at
~11% holds the single biggest symbol in the engine. (Note: an earlier "29.5% machinery" figure was
WRONG -- it matched on full mangled signatures, so any function whose parameter list mentions
`unordered_map` or `allocator` was counted. Match on the function NAME only; the corrected figure is
21.3%.)

**The correction that matters more:** the 22.3% "mana payment" figure in section 3's perf profile came
from the DEGENERATE rollouts, not from normal play. Payment dominates the TAIL. That is why the tail
wants the breadth fix (item 1) and not more memoisation -- a memo can only ever attack repeats, and
what makes those rollouts degenerate is the width of a single solve.

(Instrument trap, now documented at the counters: every per-entry node figure -- including the
pre-existing payable/unpayable split -- is a delta on a process-wide counter taken around one worker
call, so under N threads it counts the other N-1 threads' work too. The tell is the buckets summing
past 100% of `nodes`: 122.9% at 12 threads, exactly 100.0% at 1. Read them single-threaded.)

Byte-identical: identical digests and per-game logs on all three decks that own one of these sources
(Anti-Lifegain = drip + Tainted Remedy, Dragonstorm = two storage lands, FiveColour = Deathrite), and
identical to `MTG_MANA_CACHE=0` as well -- three cache configurations, one answer.

**Wall time barely moves, and the calibration says why.** Same 60 games: cache OFF 74,691 ms, legacy
gate 73,659 ms, all sources 71,589 ms. **The entire payment cache is worth ~4% of this deck's play
time**, so -18.6% of backtracker nodes is -18.6% of a small slice. What the extension really did is
roughly TRIPLE the cache's value (1.4% -> 4.2% off the no-cache baseline). Worth keeping -- it is
byte-identical, never slower, and removes a carve-out -- but it is not a tractability lever, and the
degenerate tail still needs the breadth fix in item 1.

(The capture said 156 s and the isolated replay takes 55 s: keepgen's per-rollout timing is WALL under
23-way load, not CPU. The ranking is sound; the absolute numbers are inflated.)

Related: `depth-matrix-degenerate-games.md` (the perf profile and the per-game abort),
`.claude/skills/mulligan-profile.md` (feasibility guide), `mana-source-reservation.md`.

## 6. Where the time actually goes -- and WHICH WORKLOAD you mean (2026-08-15)

Section 5d closed mana as a lever. Re-profiling after the Deathrite lifegain cut (b77b2ac)
produced the answer to "what dominates FiveColour" -- and the answer turned out to depend
entirely on which workload is meant, which is worth stating loudly because it silently
misdirected an afternoon:

**Mulligan GENERATION runs `mull_gen_depth: 3` / `budget_ms: 3`, NOT the shipped d6/b20.**

| bucket | keep-GEN (d3/b3) | play d5 (shipped) |
|---|---|---|
| PLAN-ENUM | **24.2%** | 15.6% |
| MACHINERY (alloc / state copy / containers) | 21.2% | 23.2% |
| MANA | 18.8% | 14.8% |
| value-leaf EVAL | 7.9% | **18.7%** |
| APPLY/SIM | 10.3% | 11.1% |

The value leaf leads shipped PLAY and is nearly irrelevant to GENERATION. Mana leads neither,
but note it is HIGHER in gen than in play, and the leading mana symbol flips: `TapForCostSharedOnce`
in play, the **backtracker** (`TapForCostBacktrackWorker` 3.02 + `TapForCostBacktrack` 1.44 +
1.06 inlined ~= 5.5%) in gen. That is the Bloom Tender atom of section 3 -- present, but not the
average-case leader.

### 6a. Call-graph attribution (the part worth keeping)

Flat profiles say WHERE, not WHY. A dwarf call-graph pass (single-thread, attached after setup)
attributed the machinery bucket:

* `operator new` is **diffuse** -- no single caller above 0.5% (GameState copy 0.48, TapForCostSharedOnce
  0.44, vector<int>::push_back 0.44, vector<Action>::reserve 0.24, CollectActions 0.21...). There is
  no one allocation to kill; this is a broad refactor, not a fix.
* `__memset_avx2` is **0.61 of its 0.69% from `TapForCostBacktrackWorker`** -> shipped, see below.
* `GameState::GameState(const&)` is **1.61% from `FSLineTail`** (then FSLineWin 0.51, SolveWithLookahead
  0.43, EnumeratePlansWithLandUncached 0.20) -- concentrated, NOT diffuse.

### 6b. Shipped from this pass (both byte-identical, no GT rebaseline, no regeneration)

* **Value-leaf flat layout** (f84969e): 120 trees x depth 4 = ~480 dependent loads over a 79.5 KB
  pointer-chased structure -> complete trees in level order, implicit 2i+1/2i+2 indexing, ~14 KB
  internals + ~15 KB leaves. **fc_d5 median -3.94% (6/6 paired runs negative), fc_d3 -1.64%.**
* **Tap failure-memo bucket trim** (f9d1b4c): clear() memsets a retained bucket array that never
  shrinks (max 20,753 buckets = 162 KB per clear; 26.7% of clears on a memo nothing was inserted
  into). **Only ~1% on gen throughput, neutral on play** -- the max is not the typical. Free, so kept.

### 6c. DEFERRED: GameState copies in FSLineTail (1.61%)

The one concentrated machinery target left. `FSLineTail` copies a whole `GameState` (2 `Player`s,
stack/battlefield/exile vectors) per horizon line. Not attempted here. The obvious shapes are an
undo/scratch-state reuse (one state per depth, mutate-and-restore) or shrinking what a line actually
needs to copy. Both are behaviour-preserving in principle but touch the search's hot path, so either
needs a full digest check -- and note that unlike the two above, an undo scheme is easy to get
subtly wrong in a way a 36-case smoke will not catch.

### 6d. Method traps this pass hit (all cost real time)

* **`perf` cannot write its data file under `/workspaces`** -- it is a 9p (`C:\`) mount and perf dies
  with `failed to write perf data, error: Bad address` after ~1 KB. Write to `/tmp`.
* **`MTG_KEEP_REPLAY` of one captured hand is useless for profiling** -- the rollout is <1 s of a
  ~35 s process, so ~99% of samples are startup + hand enumeration. Attach `perf -p` to a running gen.
* **A capture replayed at a different `--seed` is a DIFFERENT rollout of the same hand.** Six hands
  captured at 11-31 minutes replayed in 37-592 ms; that is NOT evidence they got faster.
* **Bucket the profile on the FUNCTION NAME only.** Matching mangled signatures counts every function
  whose *parameters* mention `allocator`/`unordered_map` and inflated MACHINERY from 21% to 29%.
* **`pkill -f <string>` matches its own shell** if the command line contains that string. It killed
  an A/B run before it started.

## 7. The recommend probe LANDED (2026-08-16): exhaustive gen is a WEEK, not a night

The full R=1 floor pass over every cell finished — `--gen-mulligan recommend` on mainline
(`3b1b1d2`), inheriting `value_play.mull_gen_depth: 3` / `mull_gen_budget_ms: 3`, K=27 with the five
fetchlands merged. It resumed the existing journal (48,220 cell-sides) and completed the remaining
5.2M, then wrote the poolable probe chunk and stopped, as `recommend_only` promises (no refine, no
profile).

```
floor pass: 39,289 s (10.9 h) @ 132 rollouts/s;  5,273,162 cells (both pd)
projected COMPLETE (full bottom, R40): ~440.6 h   (upper bound; adaptive keep trims it)
projected FAST     (adaptive,   R30): ~220.3 h
overnight target ~8 h  ->  BOTH exceed (~27.5x even for FAST)
```

**This is the answer to "can FiveColour have an exhaustive mulligan profile": not on one box.** Nine
to eighteen days. The recipe choice (complete vs fast) is not the lever here — fast is still 27.5x
over an overnight window — so the options are another machine, a weekend-plus, a multi-machine pool
(the parity-fingerprint handoff in `.claude/skills/mulligan-profile.md`), or accepting a
lower tier for this deck. The probe chunk is banked either way:
`FiveColour.keepmodel.exhaustive.raw.json.probe` is a byte-identical r=0 slice that any later
`complete`/`fast` run reuses, so the 10.9 h is never repaid.

### 7a. The slow games name ONE card

259 slow rollouts are on file (`<raw>.slow.log`, cumulative across this probe and the earlier run —
this probe itself streamed only 3, because resuming meant the pathological cells were already
banked). Their shape:

| | |
|---|---|
| total | 30,599 core-seconds (~8.5 core-hours) in 259 rollouts |
| worst | **1,851 s — one rollout, 30.9 minutes** |
| distribution | 8 over 600 s; 47 in 120–600 s; 56 in 60–120 s; 147 in 30–60 s |
| shape | **all 259 are size-7**; play/draw split even (131/128) |

Card presence across those 259 hands, and the share of slow SECONDS in hands containing each:

| card | in hands | share of slow time |
|---|---|---|
| **Bloom Tender** | **78.4%** | **84.4%** |
| Faeburrow Elder | 45.6% | 37.6% |
| Mountain | 40.9% | 51.9% |
| Blood Crypt | 37.5% | 50.3% |
| Overgrown Tomb | 35.5% | 41.0% |
| Stomping Ground | 29.7% | 31.9% |
| Breeding Pool | 24.7% | 33.9% |

Bloom Tender is in four of every five slow hands and five of every six slow seconds. The mechanism
is not incidental and the co-occurrence table states it: Bloom Tender and Faeburrow Elder are both
`domain_mana` — *"{T}: For each color among permanents you control, add one mana of that color"* —
so each extra distinct COLOUR on board multiplies what one tap yields, and every card riding with
them in the slow list is a colour-fixer (Mountain, Blood Crypt, Overgrown Tomb, Stomping Ground,
Breeding Pool). That is the same finding section 6 reached from the other end: the mana BACKTRACKER
(`TapForCostBacktrackWorker` + `TapForCostBacktrack`, ~5.5%) leads gen where `TapForCostSharedOnce`
leads play. Flat profile and slow-game list agree, which is why this one is worth trusting.

So the tractability lever for this deck is **domain-mana payment**, not bottoming, not R, not the
recipe. Until that is cheaper, the projection above is what an exhaustive profile costs.

## 5. The atom re-measured at HEAD (2026-08-16): it is the QUERY COUNT, not the query cost

Re-measured because sections 3-4 predate the scaling fix, the value leaf, and the 2026-08-16 gate
fixes. Same settings for every deck (12 games, seed 1001, `--threads 1`, `MTG_TAP_STATS`):

| deck | payment entries/game | nodes/game | nodes/entry |
|---|---|---|---|
| FiveColour | **5,567** | 224,625 | 40.3 |
| Goblins | **6** | 12 | 1.9 |

**FiveColour asks ~930x more mana-feasibility questions per game.** Cost per question is only 21x
worse. So the deck's ~23x rollout penalty (55 cells/s on 23 cores against the skill's ~110
rollouts/s/core guide) is driven by HOW OFTEN payment is asked, not by how expensive each answer is.

This retro-explains the parked flow-guided tap order (`flow-guided-tap-order.md`): it cut backtracker
nodes 13.6x and moved total runtime ~1%, on BOTH the play and the gen workload. It made each answer
cheaper. The engine was still asking a thousand times more often.

Current split at HEAD (FiveColour, same run):

```
payable   55.8% of entries but 98.3% of NODES   (71.1 nodes/entry for a ~4.5-source answer)
UNpayable 44.2% of entries,     1.7% of nodes   (1.6/entry -- at its floor)
mana cache hit=91.7% of consulted   skipped: shape=0  key=0   flow-prune bail=0.0%
```

**CORRECTION (same day).** An earlier draft of this section said the residue was the per-query KEY
BUILD and recommended hoisting it first. That was wrong, and the run's own numbers say so:

```
hit=739,814   miss=66,804   top-level entries=66,804      <- misses EQUAL backtracker entries
```

Misses equal entries exactly, so every repeat is already absorbed and only genuine misses reach the
solver. The cache is not leaking; it is working. What remains is **5,567 GENUINELY DISTINCT payment
questions per game** -- new (cost, tap-state, floating) combinations never asked before. A cache
cannot answer a question that has never been asked. Goblins asks 6.

The arithmetic confirms which half dominates: 5,567 distinct solves x 40.3 nodes = 224,625 nodes/game,
exactly the measured figure (Goblins: 6 x 1.9 = 12). The key build is real work -- ~67k source scans
per game, hit or miss -- but it is the LINEAR half, and it cannot move a 23x ratio on its own.

**So the levers, correctly ordered:**

1. **Ask fewer DISTINCT questions -- the real lever.** See section 6.
2. **Make asking cheaper** -- hoist the mana-cache key build to once per enumeration (the battlefield
   is invariant across a payment, the same argument the key already relies on). Bounded and
   byte-identical, but it only touches the linear half. Worth doing; not sufficient.

**Sizing what "possible" needs.** At K=27 (fetch cycle merged) the size-7 phase is 1,977,898 cells;
at R=10 (the floor for a shippable runtime profile) that is 39.6M rollouts. At the guide's
110 rollouts/s/core x 23 cores that is ~4.3 h. At FiveColour's measured 4.8 rollouts/s/core it is
~100 h. **The entire feasibility question is that one ratio** -- close the query-count gap and the run
lands in hours, not days. No change to K or R is needed if the rollout rate becomes typical.
