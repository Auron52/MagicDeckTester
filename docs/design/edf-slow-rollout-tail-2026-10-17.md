# EDF mulligan generation: the slow-rollout tail (2026-10-17)

Context: EldraziDisplacerFlicker's mulligan generation rolls out games at depth 1, budget 1 ms, with
`MTG_DECISION_WORK_X=1000`. Most rollouts cost ~0.3 core-s; a tail costs 30-600 s each (650 keep-rollout
lines in the live `slow.log` when it was copied, median 45 s, max 598 s) and held generation at ~50
rollouts/s on 32 threads. The worst replayed alone in **598,838 ms while charging only 3,960 units**,
about what an ordinary rollout charges. This note is where that time goes, why the budget never sees it,
what was done about it (byte-identical, landed default ON), and what is left (play-changing, built
default OFF, the user's call).

Repro (on a COPY of the deck folder; never `decks/EldraziDisplacerFlicker`):

```
MTG_DECISION_WORK_X=1000 MTG_KEEP_BENCH=1 \
MTG_KEEP_REPLAY="Wild Growth x2; Emiel the Blessed x1; Eladamri's Call x1; Conservatory x2; Cloud of Faeries x1" \
MTG_KEEP_REPLAY_R=1 MTG_KEEP_REPLAY_PD=1 mtg-analyze EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
  --cards-json src/cards/data/cards.json --gen-mulligan fast --threads 1
# -> [replay] DONE win_turn=5 elapsed=... units=3960   (play digest bb4c7075c9ec9cdd)
```

`MTG_KEEP_BENCH=1` only makes the discovery cache ignore the play digest (a fast hit after an engine
change); the replay block runs before the bench. The pooled form, `MTG_KEEP_REPLAY_LIST=<slow.log>`
(+ `MTG_KEEP_REPLAY_SAMPLE=<n>` ordinary rollouts), now honours `MTG_KEEP_BENCH_DEPTH` / `_BUDGET` too,
for the large-budget check below.

New diagnostic: **`MTG_BP_DERIVE_SLOW_MS=<ms>`** prints every breakpoint-continuation derivation slower
than `<ms>` with its consumer (`reason` 1 = canon, 2 = eligible wave variant), `rollout_nest`, the hand and
board, and with `MTG_ENUM_STATS=1` the subsets entered and plans emitted. Pair it with
`MTG_DECISION_PROGRESS=1` and `MTG_BP_USE_CENSUS=1`.

## 1. Where the time goes

Section 1 numbers are the Profile build at 7ab396bd9 on a box whose other 30 cores ran the generation
(wall inflated ~1.5x against PGO; ratios are what matter).

**One decision owns the rollout.** `MTG_DECISION_PROGRESS` on the replay:

| turn | ms | units |
|---|---|---|
| t1 | 120 | 561 |
| t2 | 133 | 724 |
| t3 | 61,549 | 2,112 |
| t4 | 96 | 349 |
| **t5** | **819,407** | **214** |

t5 is 93% of the rollout and 5% of its units. t3 is the same mechanism one step removed: its depth-1
search reaches the same t5 boards.

**The board.** By turn 5 the hand holds **42-45 cards**: the turn-4 line drew most of the library (the
blink loop's mana banked into Mariposa Military Base / Clue draws; the slow derivations show up to 154
floating mana). Four Fertile Grounds, four Overgrowths, three Traces, three Drakes, three Emiels, two
Living Wishes, a dozen lands.

**The decision is breakpoint continuations.** `perf --call-graph dwarf` over 60 s of t5: **99.4%**
inclusive under `FSLineWin -> ApplyPlanDirect -> TurnSolver::BpUnbranchedCanon -> BpEnumEntryFor ->
BpDeriveContinuationList -> EnumeratePlansWithLand -> EnumeratePlans`. Every base plan the search applies
draws mid-turn (a breakpoint). At a breakpoint the plan does not branch on, `BpUnbranchedCanon`
(`MTG_BP_BASE_CANON=1`, adopted 2026-09-17) takes the continuation list's `front()`, which means
**deriving the whole list**: a full plan enumeration over the 45-card hand.

`MTG_BP_DERIVE_SLOW_MS=1000` over the replay: 43 derivations above 1 s, **874 s of the replay's 881 s**:

| consumer | n (>= 1 s) | total s | subsets entered | plans emitted | plans kept |
|---|---|---|---|---|---|
| canon (reads `front()` only) | 34 | 816.5 | 17.8M | 4.31M | 306k |
| eligible wave variant | 9 | 57.8 | 2.35M | 0.16M | 31k |

The largest canon derivation entered 1,477,847 subsets, emitted 741,069 plans and kept 32,533 after the
signature dedup, in 50-100 s. `MTG_BP_USE_CENSUS` (whole process): canon derived 819 lists (2,106 Gcyc,
518 plans per list) and **only 7 of the 819 were ever read by a branching consumer**; eligible derived
8,494 lists for 178 Gcyc. Some canon lists are derived twice on a visibly identical board: the cache key
folds the pre-draw hand snapshot (`g_bp_hand_before`), and those states differ there. Raising
`MTG_BP_ENUM_CACHE_CAP` to 1,000,000 changed nothing (819 canon derivations either way): not eviction.

A second slow rollout (`Overgrowth x3; Eladamri's Call x1; Conservatory x2; Cloud of Faeries x1`, play,
r=0) shows the other consumer: its **turn-1** decision runs 339-714 s because every candidate's horizon
PLAYOUT reaches a turn-6 board with a 45-46 card hand, and the playout's ELIGIBLE wave-variant
derivations (`reason=2 rollout_nest=1`, 1,574,632 subsets each, 8-12 s apiece) do the same enumeration.

**Inside each derivation: the EDF land-Aura range projection.** perf inclusive, t5:

| inclusive | what |
|---|---|
| 74.5% | `EdfRangeAurasBehindUntappers` (the cast-order range, `MTG_EDF_AURA_RANGE`) |
| 65.3% | via `SubsetPayableSequential` -- the per-SUBSET payability rescue (`exec_feas_rescues`) |
| 24.3% | via the dedup's `rh_moved` (`MTG_EDF_AURA_RANGE_HOSTS`: does the arrangement move this plan?) |
| 56.9% | `OrderPaysSequentiallyImpl` (the sequential walk) |
| 47.3% | `TapForCostShared` (the payer under the walk) |

Every subset holding an untapper (Drake, Cloud) reaches the sequential rescue, because the scalar ceiling
in front of it compares only coloured pips. The rescue arranges the subset's casts, and the range
projection tries up to 2^min(k,6)-1 = 63 candidate orders, each a full sequential payment walk, until one
pays; an unpayable subset walks all of them. `MTG_EDF_RANGE_STATS`: 14.1M calls, 11.5M reaching the
candidate loop, 337M candidate orders, 78M walks (80M more skipped by the prefix rule), 5.25M ending "no
candidate pays" after walking everything.

**And most of those subsets are the same subset.** 741,069 emitted -> 32,533 kept: 95.6% of what the
enumerator builds is a duplicate by signature, overwhelmingly the same casts naming another physical copy
(with four Fertile Grounds in hand, "two Fertile Grounds" is six subsets). The exact-id probe memo
(`MTG_PAY_PROBE_MEMO`) cannot see that: its key is the candidate indices, and copies have different ones.

## 2. The mechanism, precisely

1. **What is enumerated:** at each breakpoint of each applied base plan (canon, in-window) and of each
   wave variant (eligible, in-window AND inside horizon playouts), the full main-phase plan space of the
   post-draw board: every land choice x the odometer over a 40+ card hand (the search enumerator's group cap
   still admits ~1.5M positions), each subset priced by the sequential rescue (up to 64 walks), each emitted
   plan copied, signed and deduplicated. The canon then reads ONE plan.
2. **Why it is unbounded:** a derivation is an enumeration, not a search node. `BpDeriveContinuationList`
   never reaches `ConsumeAt` (the 2026-10-17 enumeration note measured identical `work_units` with the bp
   cache disabled), and nothing inside `EnumeratePlans` polls a budget. `MTG_DECISION_WORK_X` sums
   `SearchBudget::Consume` (plus the greedy `SolveUncached` walk) and nothing else, so the 1000x ceiling
   never trips; `FSLineWin`'s `Overrun()` test at entry sees 214 units. The only bound is the odometer's
   breadth cap.
3. **Why units stay ~4,000:** units count search nodes and rollout steps. The t5 decision visits a handful
   of nodes (214 units); all its work happens inside the applies of those nodes. A rollout whose hand never
   balloons and one whose hand holds the library charge the same.
4. **Why it is a tail:** the cost per derivation is exponential in the hand. It bites once a develop line
   draws the library -- a minority of hands -- and then every searched apply and every playout that reaches
   that board pays it.

**Memory.** RSS sampled every 2 s during the replayed rollout (process floor ~360 MB after the
play-digest phase): p50 711 MB, p90 891 MB, peaks **1.05-1.07 GB**, fluctuating rather than growing --
~0.7 GB of transient allocation inside one rollout, consistent with the enumerator materialising every
emitted plan before the dedup (`TurnSolver::Plan` is 456 B plus 448 B per `Action`; one derivation emitted
741k plans) plus the per-game bp cache holding the derived lists. The pooled replay of the 650 tail
rollouts on 30 threads peaked at **17.0 GB** RSS: ~0.55 GB per concurrent heavy rollout, which is the
generation's 10-18 GB footprint and its RSS-cap aborts. The byte-identical levers below remove time, not
this allocation (sampled replay RSS unchanged: p50 711-729 MB, max 1.04-1.07 GB with and without them).

## 3. What landed (byte-identical, default ON, `=0` restores the old path)

| lever | what |
|---|---|
| `MTG_PAY_PROBE_CANON` | the order-probe memo (`OrderPaysSequentially`, `OrderPrepaysWholeTurn`) and a new memo of the whole `SubsetPayableSequential` verdict, all keyed on COPY-EQUIVALENCE instead of candidate ids: two candidates equal in every `Action` member except `hand_index` / `equiv_ord` share a key. SPS is keyed after its rank sort. Excluded: Vial puts, reveal-or-pay casts, human play |
| `MTG_SPS_WALK_MEMO` | `SubsetPayableSequential`'s own walk of the arranged order goes through `OrderPaysSequentially` (it IS that walk when the subset has no Vial put / reveal surcharge), so the walk the range projection just ran is served from the memo |
| `MTG_RH_MOVED_MEMO` | the range-hosts dedup's "does the arrangement move this plan" verdict memoised on the rank-ordered copy-equivalence tokens of the plan's casts |

Soundness: the walks, the prepay, the arrangement and the payable-order retry never read an action's
identity (`hand_index`: the scratch walk does not remove the cast card from the hand; `equiv_ord`: the copy
ordinal), the board is the enumerator scope's const object, and the payment-context globals are checked
per probe (`paymemo::Ctx`). The whole `Action` is compared (defaulted `operator==`), not a list of fields,
so a field a walk starts reading later cannot merge two questions it distinguishes. Verify modes:
`MTG_PAY_PROBE_MEMO_VERIFY=1` re-runs the real probe on every hit (canonical ones included),
`MTG_RH_MOVED_MEMO_VERIFY=1` re-arranges every `rh_moved` hit.

**Verification** (rebased onto `phase-1-2-deck-analyzer` 991b6950a, Release):

* Identity batch, the round-3 manifest (EDF d0 x1000 / d1 b1 x300 / d3 b10 x100 / d5 b20 x60, Hinata2
  d3 x40 / d5 x16, Bruna d1 b3 x60 / d3 x40, Snow d1 x60 / d3 x30, KittyEquipment d3 x30 / d5 x20;
  seed 2002, 1,756 games, 16 threads): levers off / on / verify -- digests, units and every per-game
  `.wins` / `.units` identical. Verify: **0 mismatches** over SPS 5.20M hits, OPS 38.56M, OPWT 8.56M,
  `rh_moved` 7.73M. **Instructions 7.378e12 -> 5.531e12 (-25.0%)**, user 888 s -> 700 s.
* Smoke tier **128/128 PASS** against the committed GT (scenarios 217/217); unit tests 1,428/1,428.
* The replay: win turn 5, units 3,960, play digest bb4c7075c9ec9cdd -- identical on and off, and identical
  to the pre-rebase tree (the upstream rebase did not move this deck's d1 b1 play).

**Speed on the tail** (Release, new tip):

| | levers off | levers on |
|---|---|---|
| 599 s replay (single thread) | 362.7 s (t3 32.7, t5 329.8) | **113.5 s** (t3 13.1, t5 100.2) |
| `Overgrowth x3 ...` replay, turn-1 decision | 713.8 s | 339.2 s |
| pooled tail, 650 slow.log rollouts, 30 threads: core-s | 53,042 | **21,222 (0.400x)** |
| ... median / p90 / max per rollout | 54.3 / 127.0 / 1,104 s | 21.7 / 51.8 / 514 s |
| ... rollouts >= 30 s | 648 | 169 |
| ... wall of the pooled run | 32.3 min | 13.3 min |
| pooled ordinary sample, 2,000 rollouts: core-s | 1,232 | 1,104 (0.896x) |

Every one of the 2,649 rollouts compared (650 tail + 1,999 sample; one sample was still in flight when
the off arm was read) has the identical win turn in both arms: a second identity check, at the exact
generation shape. A tail remains after the levers: 169 of the 650 still take >= 30 s, the worst 514 s.
The per-subset cost is down 2.5x; the NUMBER of subsets is untouched.

## 4. What is left changes play -- built default OFF, for the user's decision

### 4a. `MTG_BP_DERIVE_METER` (built, measured, NOT adopted)

Bills every subset a breakpoint derivation's enumerator visits to the ARMED decision meter at the greedy
walk's exchange rate (`GreedyChargeWeight`, default 1:1). Mode 1 only bills; mode 2 also stops a
derivation once the meter is exceeded, returns EMPTY (the canon's own fallback) and never caches it.
Inert unless the meter is armed (`MTG_DECISION_WORK_X` on a budgeted decision), so at an unlimited budget
nothing changes, and the ceiling scales with the budget: it RE-PRICES work and cannot make a line
unfindable at a large budget. Known caveat of mode 2: an interrupted derivation only sits in a pass the
overrun aborts, so it reaches play through the anytime rescue alone, but there the executor re-derives the
breakpoint outside the meter and would apply the canon `front()` where the search scored EMPTY.

Measured on the pooled tail + sample (650 + 2,000 rollouts, generation shape, levers on in both arms):

| | meter off | mode 2 |
|---|---|---|
| tail core-s | 21,222 | **12,629 (0.595x)** |
| tail median / p90 / max | 21.7 / 51.8 / 514 s | 17.7 / 29.8 / 148 s |
| tail rollouts >= 30 s | 169 | 60 |
| tail mean win turn | 5.2677 | **5.3508 (+0.0831; 49 worse, 8 better)** |
| sample core-s | 1,168 | 1,170 (1.002x) |
| sample mean win turn | 4.7155 | 4.7160 (+0.0005; 1 worse, 0 better) |
| max RSS | 17.0 GB | 13.2 GB |

Mode 1 on the 599 s replay keeps the T5 win but bounds little (the root keeps applying plans, and each
apply runs its derivation to completion before the next overrun poll); mode 2 on it: 11.5 s, but T5 -> T6.

Verdict: mode 2 is what bounds the tail, and it costs **+0.083 turns on exactly the hands it bounds**
(7.5% of them lose a turn or more), at no cost or benefit on ordinary hands. A keep table is fitted to
those labels, so this is not a free speedup.

**Completeness check (the large-budget half of the adoption bar).** The 57 rollouts whose win turn moved
under mode 2 at b1, re-run at **b20** (`MTG_KEEP_BENCH_BUDGET=20`, still `MTG_DECISION_WORK_X=1000`, so a
20x larger ceiling): mode 2 and meter-off agree on **every one of the 56 compared** (0 better, 0 worse;
the 57th was still running in the meter-off arm after 20 min and was left out). So the turns mode 2 loses
at b1 are found again at a larger budget -- it re-prices work, as designed, and does not delete lines.
What it costs is quality at the generation's own budget, which is where the keep table's labels come from.

### 4b. Not built

1. **Fold copy permutations in the odometer** (`MTG_FOLD_SEARCH_ODO`, adopted per deck for Snow
   2026-10-01). It attacks the NUMBER of subsets, which is the half the byte-identical levers leave alone:
   95.6% of emitted plans here are copy duplicates, so the walk -- and the 0.55 GB of transient plans per
   heavy rollout -- would shrink by an order of magnitude. A USER-reserved decision; it can delete a line
   when the canonical twin is not enumerable (knights gi497), so it needs the held-out A/B and the user's
   call. Measured as it stands (`MTG_FOLD_SEARCH_ODO=1`, same pooled tail + sample, levers on in both
   arms): tail core-s 21,222 -> 17,746 (**0.836x**, max 514 -> 384 s, >= 30 s 169 -> 131), sample 1,168 ->
   1,032 (0.884x), and **zero win-turn changes in all 2,650 rollouts**. So it is quality-neutral here but
   reaches only part of the duplication: its canonical-prefix rule folds the equivalence classes
   `FinalizeFoldTags` proves interchangeable, not every copy-permutation the EDF hand produces (land-Aura
   casts carrying different hosts, for one, are distinct actions). Extending the fold to those classes is
   the lever with the most headroom.
2. **A canon that does not derive the list.** `front()` is the max of the WithLand comparator (wins, idle,
   value, land tie ...) over every payable plan, so a top-1 derivation still prices every subset, and with
   the dedup's `pick` / range-hosts tiers and `MTG_BP_NEW_ONLY` in between an early exit is not provably
   the same plan. Parked by the 2026-10-17 enumeration note (Deferred #2); it would also need a per-game
   identity check.
3. **Stream the dedup** (dedup at emission instead of materialising every emitted plan) for the memory.
   Byte-identical in principle, but `pick` (best AuraHostKey per signature) and the range-hosts tiers decide
   per class, so the streaming form must reproduce them exactly; not attempted.

None of these touches the NO-GREEDY rule: the canon is the adopted searched default at an un-branched
breakpoint; the meter only decides how much of a decision's budget a derivation may spend.

**The banked EDF journal.** The byte-identical levers leave the keepgen play digest at bb4c7075c9ec9cdd
(checked on the replay before and after the rebase). Adopting either play-changing lever moves that digest
by construction, so the journal would no longer resume under it.

## 5. Recommended next step

1. **Keep the three byte-identical levers** (landed default ON): 2.5x on the tail population, ~1.1x on
   ordinary rollouts, -25% instructions on the identity batch, zero play change, journal digest intact.
   With them the 32-thread generation's tail work drops from ~53k to ~21k core-s per 650 tail rollouts.
2. **Do not adopt `MTG_BP_DERIVE_METER` as it stands.** It is the only lever here that BOUNDS the tail
   (max 514 -> 148 s, 0.595x tail cost) and it passes the completeness check, but it loses 0.083 turns on
   the tail hands at b1. If the user wants the bound anyway, the shape to try is a later trip, not a
   different mechanism: a higher exchange weight for derivation visits (W > 1), so the meter trips only on
   the truly pathological derivations, measured on this same pooled population.
3. **The structural next step is the subset COUNT, not the per-subset cost**: extend the copy fold so the
   enumerator never visits copy-permutations of the bloated hand (95.6% of what it emits there is a
   duplicate), starting from `MTG_FOLD_SEARCH_ODO`, which already measured quality-neutral on all 2,650
   rollouts at 0.84x. That cuts time AND the ~0.55 GB-per-rollout transient allocation behind the
   generation's memory spikes. It needs the held-out A/B and the user's sign-off (the fold is a reserved
   decision).
4. **The canon's `front()`-only derivation** (819 lists, 7 ever branched on) is the other structural cost;
   a lazily-derived canon is not byte-identical by construction and stays parked behind (3).

The pooled population measurement, on a copy of the deck folder holding the slow.log copy:

```
MTG_DECISION_WORK_X=1000 MTG_KEEP_BENCH=1 MTG_KEEP_REPLAY_LIST=slow.log MTG_KEEP_REPLAY_SAMPLE=2000 \
  [MTG_BP_DERIVE_METER=2 | MTG_FOLD_SEARCH_ODO=1 | MTG_KEEP_BENCH_BUDGET=20 ...] \
  taskset -c 0-29 mtg-analyze EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
  --cards-json src/cards/data/cards.json --gen-mulligan fast > rl_<arm>.out
```

Each `[replay-list] ms=... win_turn=... <rollout>` line pairs across arms by its rollout text. Keep the
thread count fixed across arms (the TT is sized per worker).

## 6. Follow-on (2026-10-18): the peak-memory half -- `MTG_ENUM_DEDUP_STREAM` (byte-identical, default ON)

The EDF overnight tier was aborted by its RSS cap (32 workers on `edf_overnight_d0_*`, 16.8-17.65 GB with
the plan and FSL caches empty), and both EDF mulligan-generation aborts were the same thing: LIVE memory,
the walk in `EnumeratePlans` materialising every emitted plan before the signature dedup keeps ~4% of them.

`MTG_ENUM_DEDUP_STREAM` compacts the plan list WHILE the odometer walk runs (first at 4,096 plans,
`MTG_ENUM_DEDUP_STREAM_AT`, then each time the list doubles over what survived), dropping only members
the dedup can never keep: per signature class it keeps the member with the greatest (wins_this_turn,
value), earliest emission among equals -- exactly the dedup's survivor, at exactly its sorted position,
because the survivors keep their emission order and the sort is stable. With the range-hosts tiers armed
the key adds the rank-ordered copy-equivalence tokens of the casts (the `MTG_RH_MOVED_MEMO` key), so every
extra member those tiers keep survives too. It stands down for human play, the creature-Aura host ranking,
the legacy signature builder and the print-only instruments that read the pre-dedup count. The go-off cuts
before the walk and Plan B after it are untouched. `MTG_ENUM_DEDUP_STREAM_VERIFY=1` compacts nothing, tags
every plan with its emission index, marks what each compaction would drop and counts every plan the real
dedup keeps that carries a mark.

Measured (Release, at 064dc806b + this change; lever `=0` vs default):

| | off | on |
|---|---|---|
| single EDF d0 game (seed 6533, game-index 527, `--games 1`): peak RSS | 882-911 MB | **164-169 MB** |
| ... wall | 7.6 s | 6.8 s |
| 640-game EDF d0 slice (`build/scratch/poolmem/m.json`, 32 workers): peak RSS | 5.90-6.32 GB | **3.17-3.21 GB** |
| ... wall / per-game ms sum | 9.1 s / 86-88k | 8.8 s / 83k |
| identity batch (1,756 games, 16 threads): peak RSS | 7.72 GB | **4.33 GB** |
| ... instructions | 5.542e12 | 5.738e12 (+3.5%) |

Identity: the batch, the slice and the single game are byte-identical (digests, units, every per-game
`.wins` / `.units`); verify over the batch: 5,183,531 kept plans checked, 16,120,047 would-drop marks,
**0 kept plans marked**. Smoke 128/128, scenarios 217/217, unit 1,428/1,428. The +3.5% instructions is the
signature computed once more for each survivor, plus the range-hosts tokens; wall did not move.

What is left of the single game's peak (~165 MB against a ~20 MB floor) is elsewhere -- lowering the trigger
to 1,024 plans did not move the slice -- so the next memory target is not this list.
