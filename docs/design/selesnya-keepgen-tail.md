# SelesnyaLifegain keep-gen: the pathological rollout TAIL

Status: **ROOT-CAUSED AND FIXED (byte-identical)**, branch `selesnya-perf-tail` (based on
`selesnya-pooled-flicker`, d379968d). 2026-10-06.

Scope: the hours-long keep rollouts of `mullgen.sh run decks/SelesnyaLifegain fast` (K=19, d2/b3).
The ordinary per-rollout cost (the bulk) is a separate work item; where the two share a cause it is
said below.

## 1. Symptom

The cancelled 8.6 h `fast` run logged 366 rollouts >= 30 s
(`SelesnyaLifegain.keepmodel.exhaustive.raw.json.slow.log`, 18.5 core-hours between them). Worst:
15,861,417 ms (4.4 h) for `Wirewood Lodge x1; Verdant Sun's Avatar x1; Priest of Titania x3;
Nykthos Paragon x2` at a **3 ms** budget. Card frequency in the slow hands pointed at Nykthos
Paragon / Priest / Genesis Wave / Archdruid / Lodge -- i.e. at a big white spell on an Elf-mana board.

## 2. Where the time went

`perf` on the replayed 4.4 h rollout (single thread, attached after startup):

| symbol | self |
|---|---|
| `TapForCostBacktrackWorker` | **54.0%** |
| `_Hashtable<pair<u64,u64>,...TapBacktrackMemoHash>::_M_rehash` (the failure memo) | 9.9% |
| `CardHasSubtype` (scaled-dork / burst yield reads inside the DFS) | 8.2% |
| `~_Hashtable` (failure-memo swap at the bucket cap) | 6.2% |

Call path: `SolveWithLookahead -> SimulateToEnd -> ApplyPlanDirect -> TurnSolver::BatchPrepayMainCasts
-> TapForCostBacktrack -> TapForCostBacktrackTop -> TapForCostBacktrackWorker` (deep recursion).
The mana backtracker is not charged to the search budget, which is why a 3 ms budget can buy hours.

A new diagnostic, `MTG_TAP_STATS=1 MTG_TAP_BIG_DUMP=<nodes>` (prints the board, cost, reserve mask and
node count of any ONE top-level payment solve over `<nodes>`), named the shape exactly. Every big
solve was a whole-turn **prepay of one or two Nykthos Paragons** (`{4}{W}{W}` each: `gen8 W4`,
`gen12 W6`) that the DFS proved **UNPAYABLE** -- 0.2 M to **11.7 M nodes per failed solve**, and the
same solve re-asked once per rung of the prepay reserve ladder. E.g. (4.4 h rollout, T7):

```
nodes=11741437 ok=0 T7 cost=gen12 W6 crt=1 res=401 full=1 | Brushland; Forest; Priest{15} x4;
  Archdruid{15} x3; ... Branchloft Pathway x2 (G face); Brushland x2; Blossoming Sands;
  Selesnya Sanctuary{2}; Accomplished Alchemist x2; Wirewood Lodge{14}; ...
```

Untapped, unreserved {W} sources: 2 Brushland + Blossoming Sands + Sanctuary + 1 Alchemist = **5 W
against W6**. Plain colour-counting proves it unpayable; the backtracker has TWO lossless relaxations
whose whole job is to say so in O(sources) -- and both said "feasible".

## 3. Root cause: both relaxations over-credited {W}

### 3a. Flow oracle -- Karoo priced as two of EITHER colour

`TapFlowInfeasible` (`src/core/SpellEffects.cpp`, plain-source path, was line ~2298
`srcs.push_back({ bits, amt, amt, i })`) gave a bundle land `per_col = amt = 2` on each listed colour,
so Selesnya Sanctuary supplied `{W}{W}`. The DFS cannot do that: its `bundle_src` branch (same file,
~line 3752) adds exactly one unit per `produces` entry and has **no** single-colour branch (the
2026-08 Karoo fix). One phantom {W} is precisely the difference on every one of these boards
(e.g. the 921,025-node w15 solve: Brushland + Sanctuary + Alchemist = 3 real {W} vs W4; the flow saw 4).

### 3b. Colour gate -- Wirewood Lodge widened to ALL SIX colours at the burst yield

`SourceColorCapLive` (`src/core/SpellEffects.h`, ~line 28700) set `mask = kAll` for any untap-land and
`amt = UntapBurstBestYield(...)` -- a Priest of Titania's live Elf count (8-15 on these boards). So the
Lodge alone credited **15 {W}**, and the interior colour gate (`TapForCostBacktrackWorker`, "COLOUR
half of the same gate") could never fire while the Lodge was untapped. The DFS's Lodge branches add
only the land's own `{C}` or `by` units of the FEED colour `{G}` (burst branch ~line 3656: consume one
`{G}`, add `by` `{G}`; `UntapBurstBestYield` only admits dorks producing exactly the feed colour). The
same function also credited the Karoo with 2 of each colour (its comment said so deliberately, as a
safe over-count).

With both holes open, the DFS was left to prove a colour shortfall by enumeration: tap orderings of
~22-28 sources (scaled dorks with distinct counter states defeat part of the identical-sibling
collapse), times every reserve-ladder rung, times every candidate plan the search prices.

## 4. The fix (one lever, DEFAULT ON, `MTG_TAP_BUNDLE_EXACT=0` reverts)

`BundleExactEnabled()` in `SpellEffects.h` gates three exact pricings, all in the two relaxations:

1. **Flow oracle, bundle source**: `per_col` = that colour's multiplicity in `produces` (1 for a
   Karoo), total = `max(amt, produces.size())`. Same predicate as the DFS's `bundle_src`
   (`amt > 1 && produces.size() > 1 && !IsSingleColorBurstSource`, not `LegacyKarooPay()`); drip lands,
   energy/creature-only-stripped and reflecting shapes keep the old over-credit.
2. **Colour gate, untap-land**: mask = `{C} | produces | feed colour | land-aura colours`, amount
   unchanged (`max(SourceMaxNetLive, burst yield)`).
3. **Colour gate, bundle source**: amount = max colour multiplicity + `LandAuraBonus` (the aura can
   share a colour), same predicate as the DFS plus exclusions for every shape that takes a different
   DFS branch (filters, storage, scaled dorks, drip, energy, creature-only, etb-choose-colour).

**Soundness.** Each relaxation is a PRUNE of a DFS whose answer it never changes: it may only claim
"infeasible" when no DFS branch can pay. The new bounds equal what the DFS's own branches can add for
those sources, per colour, so a payable cost is still never cut; the first solution the DFS finds, and
the sources it leaves tapped, are unchanged. Nothing else reads these numbers
(`SourceColorCapLive` has exactly one caller). It is therefore a **sound collapse** in the CLAUDE.md
sense: default ON, no quality A/B owed.

## 5. Measurements (all same seed / same hand: `MTG_KEEP_REPLAY` identity, play digest `5cb8f5f626c943ad` on both arms)

Single-replay, before (d379968d) vs after:

| rollout (logged in gen) | before | after | win turn |
|---|---|---|---|
| Lodge, Avatar, Priest x3, Paragon x2 -- draw r=1 (15,861,417 ms) | **> 90 min** single-thread, unfinished when I stopped it (contended box, load 30-45) | **504 ms** (> 10,700x) | 6 after (before never finished) |
| Mystic x2, Priest, Genesis Wave, Forest x2, Branchloft -- draw r=0 (3,175,289 ms) | **1,148,105 ms** | **436 ms** (2,633x) | 5 = 5 |
| Avatar, Priest, Forest x4, Steppe -- draw r=0 (440,723 ms) | **41,694 ms** | **159 ms** (262x) | 6 = 6 |

Backtracker nodes for the 41.7 s rollout: **177,306,924 -> 310,743** (571x); identical-sibling skips
60.7 M -> 2.8 K; payable/unpayable split identical (37,364 payable top-level solves on both arms).

Pooled replay of the WHOLE slow log on one binary (`MTG_KEEP_REPLAY_LIST`, 10 threads, both arms on
the same build, `MTG_TAP_BUNDLE_EXACT=0` vs default):

* all **366** logged rollouts with the fix: sum **121 s** (they had cost 66,555 s = 18.5 core-h in the
  gen); worst **4.87 s** (was 1,517 s in the gen), median 231 ms, p99 1.24 s.
* matched A/B on the 286 logged rollouts < 120 s (the OFF arm of the larger ones would take hours):
  OFF sum **11,168 s** vs ON **89 s** = **125x**; per-rollout speedup min 13.8x, median 115x, max 1,279x;
  **win turn identical 286/286**.
* ordinary workload, 2,000 deterministic pseudo-random size-7 rollouts (`MTG_KEEP_REPLAY_SAMPLE`):
  mean **273.6 -> 201.4 ms (1.36x)**, median 183 -> 144, p99 1,620 -> 1,064 ms; **win turn identical
  2000/2000**. So the bulk SHARES the cause, at a smaller multiplier: the same phantom {W} makes
  ordinary Paragon prepays walk a few thousand nodes instead of a few.

**Suite (smoke, 10 threads, Release):** scenarios 147/147; batch 117/118 PASS against committed GT.
The one FAIL is `selesnya_smoke_d3_s1001` 5.4200 -> 5.4267 (gi74 5->6, gi89 digest-only) -- exactly
the move the BASE commit d379968d (mana-cache scaled-dork key) already records as owed ("re-accept of
selesnya_smoke_d3_s1001"); it is not this change. Proof: the same binary with `MTG_TAP_BUNDLE_EXACT=0`
gives the identical digests for all three Selesnya smoke cells (d0 `9ec6bc35db96e6e8`, d3
`4067858d4eb1db88`, d5 `706e6d60ef51cb7e`). Every other deck -- including Fungus's Simic Growth
Chamber Karoo -- matched GT byte-for-byte. **Regression tier (10 threads): 165/165 PASS byte-identical
(all five Selesnya cells included), scenarios 147/147, viewer references 506 refs: 0 play-drift /
0 board-diverged / 0 enum-gap.** CI (push, eeb3fffc): ubuntu + windows green, Linux/Windows
determinism parity green. Play wall at the shipped settings (d3/b10, d5/b20) is
unchanged within noise (two interleaved runs per arm: d3 269/313 s ON vs 278/286 s OFF core-ms); the
saving is a keep-gen (d2/b3 prepay-heavy rollout) effect. Unit suite 457/457, including a new
soundness test (`payment: Karoo and untap-land are priced exactly ...`) that passes on both arms.

The remaining worst rollout (4.87 s, Lodge/Wellwisher/Priest x3/Paragon/Steppe) is no longer the
backtracker (0.49 M nodes total over 115 K solves, 4.3/solve); its profile is flat
(`SolveUncached` 5%, `CardHasSubtype` 3.9%, `TapForCostBacktrackTop` 2.2% ...) -- ordinary node count,
i.e. the bulk work item, not a tail.

## 6. What this does NOT change

* The backtracker is still not charged to `SearchBudget`; a future board with a shortfall NEITHER
  relaxation can see would reopen the hole. The relaxations are now exact for every source in this
  deck, which is the lossless route; a work bound would be a quality decision (see
  `mulligan-rollout-performance-floor.md`, "the bound is UNAPPROVED").
* Scaled dorks with different +1/+1 counter states (Nykthos Paragon / Ageless Entity boards) are not
  merged by the identical-sibling collapse (`counters_equal`), although no payment reads a +1/+1
  counter on a Priest. Not needed after this fix (the relaxations prune before the DFS fans out), and
  not attempted: proving "no payment reads it" needs every power-scaled source audited.

## 7. Tools added (diagnostic, default off)

* `MTG_TAP_BIG_DUMP=<nodes>` (with `MTG_TAP_STATS=1`, single-threaded): one line per top-level
  payment solve over `<nodes>` DFS nodes -- board with tapped flags, counters, live per-source yield,
  cost, reserve mask.
* `MTG_KEEP_REPLAY_LIST=<slow.log>`: replays every `SLOW-ROLLOUT` line of a slow log in ONE process on
  one pooled queue (the logged seed is re-derived and checked), prints new ms / logged ms / win turn.
  `MTG_KEEP_REPLAY_SAMPLE=<N>` appends N deterministic pseudo-random size-7 rollouts (the ordinary
  workload), so one A/B prices tail and bulk together. Use with a gencache beside the deck.

Repro:

```bash
env -C <tree> MTG_KEEP_REPLAY_LIST=<slow.log> MTG_DECISION_WORK_X=1000 [MTG_TAP_BUNDLE_EXACT=0] \
  build/Release/mtg-analyze decks/SelesnyaLifegain/SelesnyaLifegain.cod \
  --cards-json src/cards/data/cards.json --gen-mulligan fast > out.txt
```

## 8. Follow-up (2026-10-07): the Lodge was still credited off RESERVED Priests

After §4 the tail was gone, but a single-threaded `MTG_TAP_BIG_DUMP=2000` pass over the same slow log
plus 300 ordinary rollouts still found **1,029 top-level solves over 2,000 DFS nodes** (worst 52,489).
Every one was UNPAYABLE and every one had the same shape: Wirewood Lodge untapped, and **every**
Priest of Titania / Elvish Archdruid in the prepay's reserve mask (the "keep the mana creatures home"
rungs of `BatchPrepayMainCasts`). Together they were **54% of all backtracker nodes** in the sample.

Cause: the three relaxations (`TapFlowInfeasible`, the colour gate's `SourceColorCapLive`, and the
total-mana B&B gate through `SourceMaxNetLive` / `UntappedManaUpperBound`) price the Lodge with the
PLANNER form of `UntapBurstBestYield`, which counts any untapped, tappable Elf as a burst target. The
DFS never taps a reserved source, and its burst branch needs the target TAPPED at that node, so a
reserved Elf that is still untapped can never be burst. E.g. the 52,489-node solve: `{12}{W}{W}{W}{W}{W}{W}`
(18 mana) against 16 mana of unreserved lands and 1-mana Elves plus the Lodge's own `{C}` = 17. The
bound read the Lodge as 10 (a reserved Priest's 11, minus the feed), so 26 >= 18 and the DFS had to
prove it by enumeration.

Fix (`MTG_TAP_BURST_RESERVE_EXACT`, DEFAULT ON, `=0` reverts on one binary): `UntapBurstBestYield`
takes the payment's `reserved_mask` and skips an UNTAPPED reserved target (a TAPPED reserved Elf still
counts: the burst reverses a tap that already happened). Only the backtracker passes a mask, on both
sides of its running total (the top-level `UntappedManaUpperBound` sum and the per-tap
`source_max_net` subtraction, so the two stay one bound); every other caller passes 0 and is
unchanged. Only one card has `untap_creature_cost` (Wirewood Lodge) and only SelesnyaLifegain plays
it, so no other deck can reach the changed lines. Sound by the §4 argument: the new bound is exactly
the set of targets the DFS's own burst branch can ever see in that payment.

Measured, same binary, both arms concurrently on 6 cores each (366 slow + 1,000 ordinary rollouts):

| | `=0` | default |
|---|---|---|
| play digest at gen settings | `5cb8f5f626c943ad` | `5cb8f5f626c943ad` |
| win turn, per rollout | | identical 1,366 / 1,366 |
| backtracker nodes | 14,867,461 | **4,200,011** (3.5x fewer) |
| nodes per UNPAYABLE solve | 17.3 | **1.2** |
| nodes per payable solve | 11.8 | 8.6 |
| wall, slow list / ordinary sample | 248 s / 430 s | 230 s / 427 s (1.08x / 1.01x) |

So determining that a cost is unpayable now takes about one node, and the backtracker is no longer
where the keep-gen's time goes: the wall barely moves because what remains is the search itself
(the bulk work item, `selesnya-keepgen-bulk-cost.md`). Kept regardless, as a sound collapse (CLAUDE.md:
wasted work is wasted work). Unit test: `payment: Wirewood Lodge cannot burst off a reserved,
untapped Priest` (held Priest -> 4 is the board, 5 is not; tapped + reserved Priest still bursts),
passing on both arms; unit suite 477/477.
