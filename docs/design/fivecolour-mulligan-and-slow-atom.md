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
