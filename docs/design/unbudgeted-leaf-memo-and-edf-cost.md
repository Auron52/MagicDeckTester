# Making UNBUDGETED play cheaper on EldraziDisplacerFlicker

Scope: the value-leaf pipeline's **phase-C depth-matrix regime** — `--ignore-play-profile --depth D`,
i.e. `budget_ms = 0` → `SearchBudget::Unlimited()`. Budgeted play must not move at all (it meters
deterministic work units and feeds regression GT).

Probe set used throughout: `--seed 8008 --game-index 0 --games 3 --depth 3 --max-turns 15`
(seeds 8008/8009/8010, game indices 0/1/2), deck `decks/EldraziDisplacerFlicker`.

---

## 1. What the cost actually is (measured, not assumed)

`MTG_ROLLOUT_STATS` attributes the WORK UNITS, and on this deck ~78% of them are the horizon
rollout + its greedy leaf. That framing is misleading: **units are not time here.**

Paired concurrent lever sweep, one probe game (seed 8008 gi=0), all eight arms launched together so
every arm sees identical box contention:

| arm | wall | units | solve-memo misses |
|---|---|---|---|
| control | 128.2 s | 2,936,752 | 983,651 |
| `MTG_COST_REFRAME=1` | 127.1 s | 2,438,168 | 983,615 |
| `MTG_SOLVE_MEMO_CAP=1048576` | 117.2 s | 2,936,752 | 852,855 |
| `MTG_DOM_PRUNE=1` | 122.9 s | 2,639,697 | 895,095 |
| no-win leaf memo | 112.5 s | 1,759,330 | 869,798 |
| no-win + `MTG_BIG_SOLVE_MEMO` | 110.3 s | 1,759,330 | 843,503 |
| no-win + `MTG_SOLVE_MEMO_CAP=1048576` | 110.6 s | 1,759,330 | 836,665 |
| no-win + `MTG_DOM_PRUNE` | 108.5 s | 1,621,161 | 800,969 |

Every arm produced `avg = 5.0000`. Wall time tracks **solve-memo misses** at a near-constant
129–137 µs per miss, and tracks units not at all (the no-win arm removes 40% of units for 12% of the
wall — because the rollouts a state memo removes are the ones whose greedy leaves were already
memo HITS, i.e. the cheap ones).

**So: the unbudgeted cost is ~870k–984k `TurnSolver::SolveUncached` calls (i.e. greedy-Solve memo
MISSES) at ~130 µs each, and nothing else.** A `perf` profile of the same game agrees and is flat — no single hot function,
~20% in the mana-payment backtrack (`TapForCostBacktrack` / `TapForCostSharedOnce`), ~10% in
payability/enumeration, ~10% in GameState copying, ~9% in `SolveUncached` itself.

### Where the calls come from

`MTG_CONSIDER_STATS` + `MTG_ENUM_STATS` + the new `MTG_CAND_CENSUS`, same game:

* the real decision is `AIEngine` → `TurnSolver::FullSearchLineHybrid` (NOT `SolveWithLookahead`);
* 14,988 FullSearchLine interior nodes;
* each interior node's tail estimate is `SimulateToEnd(..., s_fd_leaf_depth, ...)` and
  **`s_fd_leaf_depth` defaults to 1**, so every simulated turn of every tail rollout runs a 1-ply
  lookahead: 13,743 such nodes × **33.7 candidates each** = 463,550 candidates scored;
* 430,604 rollouts × 2.87 turn-steps = 1,236,077 greedy `Solve` calls;
* 893,484 enumeration calls over **121,867,740 odometer positions** (136 positions per call).

## 2. What lossless memoisation can and cannot buy

Duplicate-state rates, measured directly:

| level | measure | duplication |
|---|---|---|
| root/interior candidates (post-apply `BuildDedupKey`) | 463,550 cands → 351,896 distinct | **1.32x** |
| rollout leaf (`SimulateToEnd` no-win probes) | 1,082,557 probes → 422,175 hits | **39% hit** |
| greedy `Solve` (`BuildBreakpointKey`) | 968,440 key builds → 677,892 distinct | 1.43x |

The candidate collapse is only 1.32x, so a candidate-level dedup (what `MTG_COST_REFRAME`'s
`reframe_seen` does) cannot pay — and measured, it does not (127.1 s vs 128.2 s).

**The rollout memo hypothesis is right but already implemented.** `SimulateToEnd` has been a
memoising wrapper since the transposition table was added; what was missing is that it stores only
WINS. `MTG_TT_NOWIN_CACHE` (the bound-qualified no-win half) has sat DEFAULT OFF "pending
measurement" since the treasure_hunt investigation. This document is that measurement.

### The one thing that would be worth 2.26x, and why it is not free

`MTG_SOLVEKEY_CENSUS` (new) decomposes the greedy-Solve memo's key:

```
[solvekey-census] key_builds=968440 distinct_full=677892 distinct_base=677892
                  distinct_canon=299928 midturn_frag=1.00x order_frag=2.26x
[solvekey-census] live folds: float=88640 storm=269626 casts_rem=0 mv_cast=0
                  pins=0 free_casts=0 m1_hand=0
```

* `midturn_frag = 1.00x`: every mid-turn scalar `BuildBreakpointKey` adds over `BuildSimKey`
  (floating mana, storm count, casts remaining, pins, free casts, the m1 stamp) contributes **zero**
  extra distinct states on this deck, even though float and storm are live on 9% / 28% of calls.
  A deck-gated "drop the storm fold" idea is therefore refuted before it is built.
* `order_frag = 2.26x`: 677,892 order-exact states collapse to **299,928** order-insensitive ones.
  The memo's state space is more than half zone PERMUTATION — the same defect `MTG_CANON_SIMKEY`
  fixed for the state key.

The Solve memo cannot use the canonical key as-is: a `Plan` is index-encoded (`hand_index`,
battlefield positions) and greedy tie-breaks read vector order, which is exactly the
misindexed-replay class `FSLineEntry::order_sig` exists to prevent (mirrorwing gi=363,
leaf-verify `cached=8 fresh=7`). A canonical Solve memo would need a name+ordinal remap on the hit
AND would still be lossy through the tie-breaks. Ceiling 2.26x on the dominant cost; not attempted
here.

Two smaller lossless options, both measured and both declined:
* `MTG_SOLVE_MEMO_CAP` (default 16,384 → 53–59 wholesale clears per game). Raising it to 1,048,576
  removes the thrash entirely (0 clears) for only **1.04x**, and the d5-tail measurement in
  `solvememo::Cap`'s own note prices that cap at 683 MB per thread. Not worth it for phase C.
* Game-persistent (epoch-free) Solve memo. Sound in principle (search shuffle is on by default, so
  `BuildSimKey` folds the full ordered library), ceiling 869,798 → 677,892 = 1.28x, but the entries
  are `Plan`s, so a whole game's working set is far past what the cap exists to bound.

## 3. SHIPPED: the unbudgeted leaf no-win memo

`MTG_UNBUDGETED_LEAF_MEMO` (DEFAULT ON, `=0` kill switch), armed by the **unbudgeted-play latch**
raised in `AIEngine::TakeTurn` (`EngineFlags.h`).

Gating is structural, not measured: the latch can only be raised by the one frame that holds the
real play budget (`m_budget_ms <= 0`), so with any real budget the whole mechanism is dead code on
that thread. It deliberately does NOT test `budget->Unlimited()` at the leaf — several sub-budgets
inside a BUDGETED search are default-constructed and therefore Unlimited (`probe_cap_budget`,
`esc_alloc_budget`, `meas_budget`), so that test would arm inside budgeted play. It also does not
use `WinlessCertificateActive`'s `g_unbounded_label_search` arm, so `EnumerateEarliestWins` is
untouched.

The entry is a full REPLAY, not just a bound (`TranspositionTable::NoWinEntry`):

* **the graded leaf quantity** (`leafeval::t_tb` / `t_life`) — replaying the bound alone measurably
  reorders the argmax (leaf-eval publishes 416 → 343, flips 15 → 11 on seed 8008). It is
  cutoff-dependent, and the reuse rule is exact: `SimulateToEndImpl`'s loop is
  `while (turn <= max_turns)` aborting at `turn > cutoff_turn`, so a run with `cutoff >= max_turns`
  never aborts and reaches the only site that publishes a quantity, while a narrower run always
  leaves through a site that publishes `kInvalid`. Since `bound == min(cutoff, max_turns+1)`,
  `bound >= max_turns` is exactly "the stored run was a full run".
* **the condemnation-drop delta** (`g_condemn_drops`), read by the escalation window as the
  candidate's filter-touched flag — replayed as a delta exactly as `enummemo::Entry` /
  `solvememo::Entry` replay theirs.
* `leafeval::t_inf` is NOT replayed: its only consumer is guarded on `win_turn <= max_turns`, so a
  NO-WIN's stamp is unreachable.
* A store is DECLINED when the run was a full run whose natural-horizon exit did not publish (the
  one hole in the frame rule), and when `MTG_ROLLOUT_HORIZON` is set (its truncated tail publishes
  from a non-cutoff-dependent branch).

### Measured

Paired, both arms launched concurrently at `--threads 3` on the same box:

| game | OFF | ON | ratio | win turn |
|---|---|---|---|---|
| seed 8008 gi=0 | 106.5 s | 90.9 s | 1.17x | 5 → 5 |
| seed 8009 gi=1 | 425.2 s | 364.3 s | 1.17x | 7 → 7 |
| seed 8010 gi=2 | 121.2 s | 105.7 s | 1.15x | 9 → 9 |
| **arm wall** | **425.3 s** | **364.4 s** | **1.17x** | |
| units | 10,028,466 | 5,721,291 | **1.75x** | |

No-win memo: 1,082,557 probes, 422,175 hits (**39.0%**), 424,850 stores.
Batch play digest **`6c2876e2e11d0315` in both arms** and equal to the pre-change binary's.

Per-site units, seed 8008 gi=0 (1 thread), OFF → ON:

| site | OFF | ON |
|---|---|---|
| `rollout_step` | 1,236,077 (0.421) | 698,284 (0.397) |
| `greedy_fallback` | 1,222,137 (0.416) | 686,906 (0.390) |
| `la_cand` | 463,550 (0.158) | 359,152 (0.204) |
| `fs_pre` | 14,988 (0.005) | **14,988 (0.009)** |
| total | 2,936,752 | 1,759,330 |
| rollout calls | 430,604 | 237,701 |

`fs_pre` is IDENTICAL, which is the shape you want: the FullSearchLine tree is not altered at all,
only its leaf rollouts are deduplicated.

Verification: smoke 73/73 PASS, 0 configs changed, `slower=0 faster=0 play-changed=0`; the
six-reference digest replay byte-identical on all six jobs; the probe batch digest-identical at
`--threads 3` and `--threads 1`.

## 4. The lever that IS worth an order of magnitude — `MTG_FD_LEAF_DEPTH`

`s_fd_leaf_depth` (env `MTG_FD_LEAF_DEPTH`, **default 1**) is the fidelity of FullSearchLine's tail
estimate: at 1, every simulated turn of every tail rollout runs a 1-ply lookahead over ~34
candidates, each with its own sub-rollout. That single knob is ~98% of the unbudgeted cost.

Same 3 probe games, `MTG_FD_LEAF_DEPTH=0` (plain greedy tail):

| | leaf depth 1 (default) | leaf depth 0 |
|---|---|---|
| total CPU | 560,906 ms | **7,965 ms (70x cheaper)** |
| units | 5,721,291 | 143,794 (40x fewer) |
| avg win turn | 7.0000 | **5.6667** |
| per game | 5 / 7 / 9 | 5 / 7 / **5** |
| digest | 6c2876e2e11d0315 | 54abfb69aaf38107 |

It is LOSSY (a fidelity change, different play), and on this small sample it is not merely cheaper
but BETTER — seed 8010 gi=2 goes 9 → 5.

Widened to 20 games (seed 8008, gi 0..19, same flags), the depth-0 arm returns **avg 5.1500**,
digest `c319f91f83117b66`, for 573,210 ms of summed per-game wall across all twenty (the same run
at 4 threads under load reported 1,071,579 ms — the figure is wall, so it inflates with contention;
the digest is identical either way, i.e. thread-invariant). The depth-1 control on the same
manifest is far more expensive — single
games of 851 s, 870 s and **1,935 s** — and 15 of its 20 games had landed when this was written.
PAIRED on exactly those 15 (gi 0,1,2,3,5,6,7,8,9,11,13,14,15,16,18):

| | depth 1 | depth 0 |
|---|---|---|
| mean win turn (15 games) | 5.200 | **5.067** |
| games identical | — | 12 of 15 |
| games better | — | 1 (gi=2: 9 → 5) |
| games worse | — | 2 (gi=5, gi=7: +1 turn each) |

So on the evidence available the 1-ply tail lookahead costs ~70x and buys **no measurable quality**
on this deck — 12 of 15 games play to the same win turn, and the net delta (-0.133 turns) is
carried entirely by one game the cheap leaf wins four turns earlier. Small n, one deck, and the
five unfinished control games are the expensive tail, so this is a strong lead, NOT a verdict.

This is a USER decision, not an agent's: it changes what the depth matrix measures. Recorded here
rather than adopted. The obvious next step is the same comparison at 40+ games with the control
arm run to completion, and on a second deck, before anyone touches the default.

## 5. Diagnostics added (all default OFF, one branch when off)

| flag | what it answers |
|---|---|
| `MTG_CAND_CENSUS` | per-`sub_depth` candidates vs DISTINCT post-apply states — the ceiling on any candidate dedup |
| `MTG_SOLVEKEY_CENSUS` | decomposes the greedy-Solve memo key: mid-turn-scalar fragmentation vs zone-ORDER fragmentation, plus each extra fold's liveness |
| `MTG_TT_STATS` (extended) | `nowin_lookups` / `nowin_hits` / `nowin_stores` — the win map's counters could not see the no-win half at all |
