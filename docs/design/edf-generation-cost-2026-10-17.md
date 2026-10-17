# EldraziDisplacerFlicker: what generation would cost, and where the time goes (2026-10-17)

USER 2026-10-17: mulligan profile FIRST (the value leaf "has been pretty useless recently ... I think
something was broken", under investigation separately); *"we should also focus on optimizing what we
can of EDF from branch prunes to other optimizations"*.

## Projections (HEAD src 8c08b8e7, Release, idle 32-thread box)

| stage | measured | projection |
|---|---|---|
| value leaf | `scripts/wk_valueleaf_cost_probe.py` (3.5 min): H1..H5 0.85 / 4.7 / 13.1 / 22.2 / 18.6 core-s/game; phase A 4.3 s/game | floor ~54.5 core-h = 1.7 h wall; expect 2-4 h. (Bruna's probe said 8.7 h floor, actual 9.5 h.) H5 is a 12-game sample. Cheap-bottom arm: H cells 26.4 -> 15.4 core-h |
| mulligan, gen setting | `derive_mullgen_setting.py` (dry run, no sidecar -> writes the PROFILE): d1 b1, rho 0.9979 (floor 0.99), 0.27x play units | |
| mulligan, discovery | K = 19, NO merges (nearest: Aether Hub~Brushland 0.08, Kitchen~Conservatory 0.0875) | 386,945 size-7 hand types -- exactly PD's count |
| mulligan, rollout | d1 b1 + `MTG_DECISION_WORK_X=1000`: 2,304 rollouts, 1,480 cpu-s = **0.64 core-s/rollout**, 12.3 GB RSS | `fast` (PD's 15.5M rollouts): ~86 h Release / ~66 h PGO on 32 threads (PD: 40.9 h). `complete` > 1 week |

0.64 core-s/rollout is ~2x Bruna's 0.30 at d1 b3 with 4x FEWER work units: on this deck a unit is
~8x dearer, i.e. the time is not in the units the budget counts.

## Where the time goes at the generation shape (perf --call-graph dwarf, d1 b1)

| inclusive | what |
|---|---|
| 31.7% | `TapForCostShared` (the real payer) |
| 28.2% | `SubsetPayableWithFilters` -- enumeration asking the real payer per subset |
| ~24% | `EnumeratePlans` / `EnumeratePlansWithLand` |
| 20.1% | `comboroute::Run` via `EdfComboRouteTrial` (Loop 15.3%, Blink 8.8%) |
| 15.7% | `BpDeriveContinuationList` |
| 11.0% | `OrderPaysSequentially` + `SeqWalkFixedOrder` (the cast-order range walk, part 2 of 2026-10-14) |
| 8.8% | `SimulateToEnd` (the rollouts themselves) |
| ~10% | GameState copies (memmove, `array<Player>` copy) |

### Lever 1 -- the route trial's gate admits boards it cannot win (`MTG_EDF_ROUTE_STATS`)

24 openers x R=4 (192 rollouts): **133,877 trials** (~700 per rollout), memo hits 17,947 (13%), misses
115,930 at 84 us each (9.75 s of 88 cpu-s); verdicts on misses: **no 97,852 (84%)**, win 9,680,
develop 8,398. The dominant "no" is `no loop: neither deploy order reaches a finish`; 537 of a 3,616
sample had an empty log (nothing deployable). A necessary-condition pre-gate (best-case loop yield vs
cheapest blink cost) refusing only boards the trial would refuse is a sound, byte-identical prune.

### Lever 2 -- branch duplicates (`MTG_DUP_DIFF`, classifier extended with the remaining pin fields)

26.5% of post-apply states (79,841 / 301,230) duplicate a sibling's. NOTE the post-apply dedup already
keeps duplicates out of their ROLLOUTS, so what each still costs is the GameState copy + apply.

| class | n | reading |
|---|---|---|
| `PL[etbc] SAMEACTS` (+~5k in mixed classes) | 25,962 | Emiel's opposite-payment variant (`TurnSolver.cpp` "SEARCHED OPTIONAL ETB COUNTER") is emitted on EVERY base plan while Emiel is in play (`live = watcher`); when the base apply never reads `g_etb_counter_pin` (one read site, `SpellEffects.h` FireEtbWatchers' optional-counter loop) the variant IS the base apply. Fold: skip the variant pre-apply when its base's apply was clean (no pin read, no unit, no breakpoint, not pending) -- the `PutBounceFold` / `w0_nobp` shape |
| `A+{X k26 ...} B+{}` (Conservatory, Shivan Gorge, Clue, Mariposa, Kitchen) | ~15k | activation present vs absent, same result; `dedup_why_act strand_dup=7,370` are unpayable activations the apply silently drops |
| `A+{Displacer/Emiel k25 x2-3 ...} B+{}` | ~5k | blink loops that end in the base state |
| `PL[bp] SAMEACTS` / `PL[tutor] SAMEACTS` | 5.4k / 1.6k | |
| copy permutations (COPY[Conservatory], Conservatory<->Kitchen, ORDER) | ~4k | |

### Lever 3 -- per-node: payability

`SubsetPayableWithFilters` -> `TapForCostShared` is the largest single block. Candidates: a cheaper
necessary-condition prefilter ahead of the real payer, memoising the subset verdict across the
breakpoint continuations, and whether `OrderPaysSequentially` (11%) can reuse the walk the filter
already did.

Diagnostics added for this: `MTG_EDF_ROUTE_STATS` (DecisionProviders.cpp) and the extra `PL[...]`
fields in `MTG_DUP_DIFF`'s classifier (TurnSolver.cpp). Both are print-only and off by default.

## Outcome (same day): rounds 1-3 landed, all byte-identical

29 commits, `5589b604a`..`9f482696f`: route pre-gate, ETB-counter fold, supply gate, range-rescue kind gate
(round 1); five enumeration levers (`edf-bp-continuation-enum-cost-2026-10-17.md`); nine combo-route levers;
plan-signature arena, and the keep-generator per-game cache clear (`edf-round3-2026-10-17.md`). Every lever
is a default-ON `EnvOn` flag with `=0` restoring the old path and a verify mode reporting 0 mismatches.

Integration check on the combined tree: smoke 120/120 PASS + EDF's 4 new keys identical to the
pre-optimization run; regression 166/166 PASS + EDF's 5 identical; unit 1273/1273.

Re-time at 48 hands x R24, d1 b1, 32 threads, Release, idle box, alternating:

| | user s | wall s | max RSS |
|---|---|---|---|
| before (HEAD 6786722a8) | 1427, ~1420 | 97 | 12.3 GB |
| after (9f482696f) | 899, 898 | 54-56 | 6.7 GB |

Scoring output byte-identical (md5) across all four runs. `fast` projection: ~66 h -> **~42 h PGO**.
