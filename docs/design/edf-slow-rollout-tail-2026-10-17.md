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

### 6a. What the stream costs, and the cost removed (2026-10-18)

The first cut (c936f3cba) cost +3.6% instructions on the identity batch and +10.6% on the EDF d0 slice:
each surviving plan's fast signature was built twice (compaction key, then the dedup), every range-hosts
cast paid a `CastOrderLess` rank computation and a copy-compare scan over the token representatives, and
the compaction re-hashed every survivor's key on every pass. Now:

* **The dedup reuses the stream's signatures.** A key's first `siglen` chars are the plan's
  `PlanSignatureFast`; the keys are finished for the plans the stream has not seen (pushed after the last
  compaction, Plan B), carried through the sort -- an index permutation stable-sorted with the same
  comparator and applied in place to plans and keys together, i.e. the identical order StableSortPlans
  produces -- and read by position. Under `MTG_PLAN_SIG_FAST_VERIFY` the cached string is checked against
  the legacy builder and the legacy string returned, as `plan_signature` does. Off under the stream's own
  verify and under `MTG_PLAN_SORT_BY_INDEX_VERIFY`. Human play and the legacy builder never reach it.
* **Interned range-hosts tokens.** Every distinct cast action (exact `Action ==`) is interned once per
  enumeration with its copy-equivalence token and its `CastOrderKeyOf` (new in ManaPayment:
  `CastOrderLess(a, b) == CastOrderLessRanked(a, key(a), b, key(b))`, CastOrderLess now calls it); a cast
  is a hash probe plus one exact compare, the rank sort uses the interned keys, tokens go in as 3 raw bytes.
* **Compaction carries its class map** (key hash -> best plan) across passes and remaps the survivors'
  positions instead of re-hashing them; a hash collision between unequal keys only keeps both plans.

Idle box, Release, 32 threads, two alternating rounds each; off = c936f3cba with the stream `=0`, on =
c936f3cba, fix = this change:

| | off | on (c936f3cba) | on + fix |
|---|---|---|---|
| identity batch: instructions | 5.551e12 | 5.753e12 (+3.6%) | **5.564e12 (+0.2%)** |
| ... cycles | 4.092e12 | 4.150e12 (+1.4%) | 4.064e12 (-0.7%) |
| ... user CPU / wall | 1,045 s / 50.2 s | 1,078 s / 50.3 s | 1,062 s / 50.1 s |
| ... peak RSS | 11.0-11.2 GB | 6.35-6.37 GB | 6.39-6.56 GB |
| EDF d0 slice: instructions | 456.5e9 | 504.8e9 (+10.6%) | **468.4e9 (+2.6%)** |
| ... cycles | 319.0e9 | 348.1e9 (+9.1%) | 338.6e9 (+6.1%) |
| ... user CPU / wall | 72.5 s / 9.24 s | 78.8 s / 8.98 s | 76.4 s / **8.32 s** |
| ... peak RSS | 6.07-6.16 GB | 3.17-3.18 GB | 3.18-3.27 GB |
| slice, 1 thread: instructions / cycles | 455.7e9 / 179.7e9 | 504.1e9 / 192.6e9 | 467.8e9 / 187.0e9 |

What is left on the slice (+2.6% instructions) is the exact `Action ==` of the token interning (~1% of
instructions), key strings/allocation, and the compaction itself; the identity batch is at parity. All arms
byte-identical (identity batch, slice); `MTG_ENUM_DEDUP_STREAM_VERIFY` 0 of 5,183,531 kept plans marked;
`MTG_PLAN_SIG_FAST_VERIFY` 14,435,736 signatures, 0 mismatches; smoke 132/132, scenarios 217/217, unit
1,428/1,428.

## 7. Round 2 (2026-10-21, after the real generation): a fast repro, two byte-identical levers, what is left

The generation finished (61.5 h) with the levers of sections 3 and 6 on, and its slow log still held 3,959
keep-rollouts over 30 s (p50 43 s, p90 120 s, max 14,213 s). A turn census of the 12 slowest replays put
**97% of their 28,700 s in 11 single decisions**, each a pre-combat main of a TRIAL game (`site` 1,
`probe` 1, host `FullSearchLineHybrid`) that emitted 1.3-1.8 billion plans and ~1.8e10 payment calls while
charging 1,000-3,000 units. The mechanism is section 1's, reached from a smaller hand.

### 7a. The repro, in seconds instead of a 50-minute rollout

`docs/design/edf-slow-rollout-tail/` holds three `mtg --scenario` files:

| file | what | Release wall |
|---|---|---|
| `keepgen-worst-t2.json` | the worst rollout's turn-2 board (Kitchen; Wild Growth, Training Grounds, Living Wish, Displacer, 2 Cloud of Faeries; Brushland on top of the dumped 52-card library), d1 b1, **`max_turns` 4** | 277 s |
| `keepgen-worst-t4-frame.json` | the frame that decision's playouts reach: turn 4, Dimensional Infiltrator has drawn the library (51 cards in hand), 52 floating {G}, mid-turn (`resume_at`) | 7 s |
| `play-d3-g319-t4-frame.json` | the slowest EDF game at d3 b10 with the keep table on (seed 4323, game-index 319, 17.9 s for 4,418 units): one canon derivation on a 27-card hand | 7 s |

`max_turns` is not cosmetic: at 2 or 3 the playouts stop before turn 4 and the decision costs 0.1 s. The
turn-2 decision derives 116 continuation lists over 99 distinct full keys and 81 distinct boards
(`MTG_BP_USE_CENSUS`). Each derivation on the turn-4 frame walks the capped odometer once per land choice:
10 land names x ~126k positions, 12 option groups of which 7 are land-Aura copies over the two land hosts
(3 Traces + 3 Overgrowths + a Fertile Ground: 3^7 of a 139,968-position walk), ~362k plans emitted, 9,583
kept by the dedup.

### 7b. Landed: `MTG_LAND_AURA_COPY_FOLD` (default ON, identity fold, search enumerator only)

The branch-shape census priced the copy fold at 7.3x on that walk. That price assumes a copy permutation is a
copy, and for a land Aura it is not, twice over:

1. **It is a cast order.** Copies of one card tie on cast rank and `CastOrderLessRanked` keeps plan (= hand)
   order on a tie, so {copy0 -> Kitchen, copy1 -> Brushland} enchants Kitchen first and its swap Brushland
   first; a land Aura adds mana, so the first can pay for the second. The multiset form (the creature-Aura
   fold's) cut the frame's survivors 9,583 -> 9,383.
2. **Selection order is read by payers.** The PREFIX form ("the selected copies are the first ones", each
   keeping its host, in order) kept every survivor and measured 1.42x on the turn-2 decision, but moved d0
   play (smoke 1 game, regression 3, one faster): Solve's `SubsetPayableAsExecuted` and EnumeratePlans'
   filter fallback `SubsetPayableWithFilters` (armed by any pending land Aura) pay in SELECTION order, so
   moving a copy across another card's cast changes the verdict (EDF d0 seed 1911 game 910: {Trace, -, Trace}
   payable with Overgrowth / Displacer / Fertile Ground between, its twin {Trace, Trace, -} not).

What shipped is the prefix form with the condition that makes it exact: a gapped copy folds into an earlier
unused one only when **no other group is selected between them**, so the twin selects the same casts in the
same selection order and only same-name copies trade places. It is applied in the search enumerator only
(its dedup keeps the earliest-emitted member of a tie, which is the twin; Solve breaks ties on the smallest
candidate mask). Class identity is the creature fold's plus the breakpoint snapshot side
(`g_bp_hand_before`), because `MTG_BP_NEW_ONLY` reads hand cards by number.

| measurement (Release) | off | on |
|---|---|---|
| turn-4 frame | 6.87 s | 6.29 s (plans emitted 362k -> 291k, kept 9,583 both, game log identical) |
| turn-2 decision | 277 s | 250 s (game log and per-decision units identical) |
| 1,000 EDF games, d3 b10 x400 + d5 b20 x300 x2 | 936 s user | 912 s user (-2.6%); digests, unit totals, per-game win turns identical |
| smoke / regression tiers | | unchanged (every config) |

### 7c. Landed: the continuation list is trimmed before it is cached

A cached list leaves the derivation with the capacity of its pre-dedup peak: 45,385 plans in a buffer of
83,050, 18 MB of the entry's 186 MB. `BpEnumEntryFor` now `shrink_to_fit`s the list (and its demoted twin)
before pricing it for the pool. Contents unchanged, so byte-identical.

### 7d. Found, not changed

1. **The plan-cache pool refuses these lists, and a refusal is a re-derivation.** A plan costs ~4 KB here
   (`sizeof(Plan)` 472 B + 6.3 `Action`s x 488 B), so one turn-4 list is 186 MB. The scenario's pool (1 GB)
   refused 17 of the turn-2 decision's 116 stores; with `MTG_PLAN_CACHE_KB=6000000` it derived 99 lists and
   ran **250 -> 187 s**, game log and units identical. A 32-worker generation shares one pool sized
   min(workers x 100 MB, cache/4), so the heavy rollouts there refuse at least as often. Raising the pool is a
   memory-policy change (RSS cap); the byte-identical fix is a compact entry: the 286,866 `Action`s of that
   list are copies of a few dozen candidates, so a per-list action table plus per-plan index vectors would cut
   it by roughly two orders of magnitude. Not attempted: `EnumerateBreakpointPlansRef` hands out a
   `const std::vector<Plan>&` and its consumers copy one plan out, so the entry needs an accessor rewrite.
2. **The snapshot split** (99 full keys over 81 boards) is real content, not churn: the lists differ by the
   new-only filter (45,385 vs 45,381 plans), and the raw enumeration reads the snapshot too (condemnation,
   `EnumeratePlansWithLandUncached`), so it cannot be shared below the filter without care.
3. **The land-Aura HOST axis is where the walk's work goes, and the dedup throws it away.** On the play frame
   9 of 11 groups are land Auras over two hosts: 157,464 positions, 48,510 plans emitted, **402 kept**
   (99.2% folded by the dedup's land-Aura host fold, which keeps a class's first member, plus the range-hosts
   tier's extra members). Every discarded host assignment was priced by the payment walks first. Skipping
   them soundly needs (wins, value) to be host-invariant within a class, or the host chosen after the cast
   set; both are designs inside the USER's EDF cast-order package (Q30, part 2c), so this is a proposal, not
   a lever.
4. **Solve's payer check pays in selection order, the executor in cast order.** `SubsetPayableAsExecuted`
   hands `BatchPrepayMainCasts` / `SubsetPayableWithFilters` the casts as selected; the apply worlds arrange
   them (`ArrangeCastOrder`) first. Section 7b's seed 1911 game 910 is a case where the two disagree (the
   same casts, payable or not depending on where in the selection a Trace sits). Possibly an
   executor-agreement gap; not investigated further.
5. **With the keep table on, EDF's play tail is small.** 1,000 games at d3 b10 / d5 b20: 4 over 10 s, worst
   39 s, ~9% of CPU, the same uncharged-derivation shape (game 319: one 15.8 s canon derivation, 4,418 units).

### 7e. What is next, ranked

1. **Compact continuation-list entries** (7d.1): byte-identical; removes the refusals (~25% of the heavy
   decision) and most of the generation's transient RSS.
2. **A host-late land-Aura enumeration** (7d.3): the largest remaining waste; needs a design and the USER.
3. **`MTG_BP_DERIVE_METER` with an exchange weight above 1** (section 4a): the only lever that BOUNDS the
   tail; play-changing, the USER's call.

## 8. Round 3 (2026-10-21): the value leaf's own exposure (phase A), and the pooled-pool multiplier

The keep table removed the mulligan trial games, so the remaining question for the value leaf was its own
phase A: every position of 2,500 played games labelled by K=3 earliest-win searches at the engine's label
ceiling (1e6 virtual ms, effectively unbounded). Probe: phase A's own games (seed 900000+k, game index k,
`MTG_DUMP_VALUE_ROWS`, `MTG_EVAL_ROWS_K=3`), first 160 pooled on 32 threads, then 960 more on 27.

### 8a. What a slow label is

Every slow game broken down has the same shape: ONE of a position's three sampled futures has no early
win, and the label ladder (`EnumerateEarliestWins`, one FSLineTail pass per cutoff) must prove it, e.g.

    seed 900411 gi161, t1:  result=7 | dd3=56ms dd4=3.8s dd5=195s (7.6M applies) dd6=0.6s

-- 195 of the game's 200 s, every other label in the game milliseconds. Game 157 (seed 900157) is the same
thing one turn deeper (the `MTG_LABEL_HORIZON` comment's own example) at 44.8M units / 75 min. The branch
census says it is volume, not width: 157 = 37M `EnumeratePlans` calls (30M at turn 7, average odometer 31);
900411 = 2.1M (turns 5-6, average 10-17); game 43 = 5.5M (average 12). The widest single decisions are
land-Aura copies over two hosts (4 Overgrowth + 2 Trace = 46,656 positions) but they are rare: the copy
fold prices 1.16-1.25x over the whole search, the host fold 1.50x on the ~2% of calls it reaches.

### 8b. Landed: the FSL line-cache pool bills real bytes (the pooled multiplier)

The same game cost very different amounts alone and pooled: seed 900411 gi161 was 7.7M units / 197 s alone
and 12.5M / 1,007 s inside the 27-worker probe; the 160-game probe reported 377,478 refused FSL stores.
Shrinking the pool alone reproduced it monotonically (300 MB: 14.3M / 373 s; 50 MB: 30.1M / 773 s). The
cause was the accounting, not the budget: `ApproxFslKb` rounded every entry UP to a whole KB, and a label
search's entries are ~100-150 B (no phases -- stripping them was tried and changed nothing), so the pool
held ~8x fewer entries than its memory bound allowed. `ApproxFslBytes` bills real bytes against the same
bound: 900411 at 300 MB is back to 7.7M / 201 s, labels byte-identical across every variant; the pooled
re-run of the probe peaks at 112 MB of the 1.6 GB pool. Inert while the pool does not bind: smoke and
regression byte-identical on every non-EDF key. Game 157 alone was also mildly starved (4 GB pool under
the old accounting: 44.8M -> 40.1M units).

Over the population, pooled on 30 threads with the pushed binary: the wide probe's 53 slowest games went
**23,561 s -> 6,313 s (3.7x) and 485M -> 197M units (2.5x)**, 51 of 52 comparable win turns identical (one
5 -> 4, the EDF rule change); the worst monster went 3,823 s -> 1,125 s, seed 901016 gi16 3,680 s / 118.9M
units -> 348 s / 10.6M. The first 160 games re-run pooled: no pool refusals at all; game 157 4,160 s /
40.1M units -- the one genuine outlier left (section 8d).

Not the cause, measured: the bp continuation cache's 8,192-entry clear-on-full (cap 1M: identical units on
157) and the plan-cache pool (20 MB: identical units on 900411).

### 8c. Landed: the USER's EDF rules

USER 2026-10-21: *"not to use Emiel's come into play with a +1/+1 counter. At minimum anytime we flicker"*,
*"I have never used the +1/+1 counter with this deck ever"*, and *"flickers should only target untappers.
There is no benefit to targeting anything else."* `DecisionProvider::PaysEtbCounterOnFlicker` (ApplyBlink
installs a declining payer that outranks the searched pin and the blink loop's own payer) and
`SearchesOptionalEtbCounter` (the etb-counter axis) are both false for EDF; `BlinkTargetCandidates` no
longer offers the attacker branch. Restore: `MTG_EDF_FLICKER_COUNTER=1`, `MTG_EDF_SEARCH_ETB_COUNTER=1`,
`MTG_EDF_BLINK_UNTAPPER_ONLY=0`. Work on the slow phase-A games: within 2% (they are not where it goes).
Play: smoke d0 5.3170 -> 5.3160, regression d0 5.3420 -> 5.3410 (gi738 7->6, gi785 7->6, gi91 7->8), d3/d5
same scores; regression d3 s2002 gi37 still wins T7 by combat, at opponent life 0 instead of -1.

### 8c'. A fresh sample on the pushed engine (games 1,120-2,119, pooled on 30 threads)

999 of the 1,000 games finished in ~25 min of wall; worst completed 1,229 s (seed 901565, 34.1M units, a
deep refutation), then 821 s (seed 902003) and 763 s. One straggler (seed 901675) was still running past
30 min -- the game-157 class. Seed 902003 is the same ladder shape with a different per-node cost: its t2
sample's turn-5 refutation pass took 755 s for 2.0M applies, and 345 breakpoint continuation derivations
of >= 0.5 s inside it account for 541 s (lists of 1,000-9,600 plans on land-Aura-heavy combo boards,
453 of 581 large derivations on never-seen keys -- distinct work, not cache churn). That is section 7d.3's
host axis showing up in phase A; 762 s alone vs 821 s pooled, so the pool is not involved.

### 8d. What is left

1. **The bricked-future refutation itself.** Exact routes: a certificate that refutes one turn EARLIER
   (bound end-of-next-turn damage from a node, so a whole turn of plans need not be enumerated) -- a
   design job in the EDF provider's `ProvenWinlessThisTurn` family; or per-node cost.
2. **Derivation-heavy refutations** (seed 902003): compact continuation lists (7e.1, exact), or the
   host-late land-Aura enumeration (7e.2, USER design), or `MTG_BP_DERIVE_METER` (7e.3, USER's call --
   note it would not act here: the label search is effectively unbudgeted).
3. **`MTG_LABEL_HORIZON`** (built, default OFF): stop the ladder at pass H and label deeper samples by a
   searched playout. It changes which labels exist -- the label ceiling is a USER decision.
