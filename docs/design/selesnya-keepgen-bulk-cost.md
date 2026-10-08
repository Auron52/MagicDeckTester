# SelesnyaLifegain keep-gen: where the BULK per-rollout cost goes (2026-10-06/07)

Companion to `selesnya-keepgen-tail.md` (the hours-long rollouts, fixed in `eeb3fffc`). This doc is
the ORDINARY rollout: the 2026-10-06 `fast` generation (K=19, 351,711 size-7 cells, d2/b3 with the
value leaf, horizon 8) took ~6 h for its 1.41M-rollout size-7 floor on 24 threads -- roughly 4
rollouts/s/thread against the ~110 rollouts/s/core rule of thumb in `mulligan-profile.md`.

## 1. The "25x" is mostly not a Selesnya anomaly

**Instrument.** `MTG_KEEP_BENCH=N` (`d7009509`, `ExhaustiveKeep.cpp`): N size-7 floor rollouts through
`run_one` -- byte-identical to the gen's own floor -- cells sampled by a fixed stride, r=0, play/draw
alternating. Prints wall and **thread-CPU** per rollout, search units, the slowest hands and a digest of
every (cell, side, win-turn). `_DEPTH`/`_BUDGET` bench other rollout settings on the same sample;
`_DUMP` writes the per-rollout table. Under it the equivalence cache ignores the play digest, so an
engine change that moves play is still benched on the SAME buckets. The tail agent's
`MTG_KEEP_REPLAY_LIST=/dev/null MTG_KEEP_REPLAY_SAMPLE=2000` (r uniform in [0,30)) is the second
workload used below. Exact cost comparisons are **callgrind instruction counts** on 40 bench rollouts
(single thread, instrumentation switched on at the `[bench] start` marker -- `logs/bench/cg.sh`): the
box ran at load average 60-90 for most of this work, so wall and even CPU time drifted +-10%.

| | rollouts/s/core (10 threads, 4000 stride rollouts) | units/rollout | mean win turn |
|---|---|---|---|
| SelesnyaLifegain d2/b3 | **11.1** (wall); 7.1 (CPU, loaded box) | 6,552-6,677 | 6.08 |
| Mirrorwing Dragon d2/b3 (its own gen settings; its gen COMPLETED) | **10.5** (wall) | 5,491 | 5.07 |

**Selesnya's ordinary rollout costs the same as Mirrorwing's at the same settings.** The ~110/s/core
guide is a d1-era number; at d2 with a heuristic-leaf escalation every deck pays ~10/s/core. Where the
rest of the gap to the observed ~2.7 rollouts/s/thread came from:

* **24 threads on this host are not 24 cores.** 8 P-cores with HT + 8 E-cores, plus host load the
  container cannot see (load average 60-90 with our container idle). The floor's 518k thread-seconds
  over 1.41M rollouts is 368 thread-ms/rollout against 90-140 ms in the bench.
* **The tail.** `slow.log` holds 275 size-7 rollouts >= 30 s summing 57,315 s -- 11% of the floor's
  thread-time, max 15,861 s for ONE rollout. Fixed by the tail agent (`eeb3fffc`).

## 2. Where an ordinary rollout's time goes (callgrind, 40 rollouts, 488M instructions each)

| inclusive | |
|---|---|
| 90.4% | the hybrid's HEURISTIC ESCALATION (`FullSearchLine` from `FullSearchLineHybrid`) |
| 9.2% | the value-leaf PROBE |
| 89.8% | `FSLineTail` -> `SimulateToEnd` at leaf depth 1 (the 1-ply-lookahead horizon rollout) |
| 65.9% | ...the nested greedy rollouts that 1-ply lookahead runs per candidate (`SimulateToEnd'2`) |
| 37.7% | ...greedy `Solve` inside them (the permitted beyond-horizon policy) |
| 35.1% | `ApplyPlanDirect` (12.3% of it mana payment `TapForCostShared`, 4.4% `BatchPrepayMainCasts`) |
| 11.7% | `CollectActions` |

**The value leaf buys this deck almost nothing in generation.** `MTG_HYBRID_STATS` over 1000 bench
rollouts: 6,487 decisions, **5,090 escalated (78%)**, the probe leaving 96% of the budget unused. The
sidecar's `value_fallback_crossover.take_heuristic_at_hdepth[2] = 1`, i.e. a d2 value-leaf line is
always replaced by the heuristic line once the escalation commits anything; only the 22% of decisions
where the probe VERIFIES a win skip the escalation. So the gen is effectively running the heuristic
ladder plus a 9% probe tax. (That table is the deck's measured artifact -- V2 5.442 vs H1 5.351 -- so
this is reported, not touched.)

**It is not branching.** `MTG_BF_CENSUS`: mean width 18.3 candidates per lookahead node, max 448, only
8% exact-duplicate post-states (`MTG_DEDUP_CENSUS`: 11%), no X-axis or token fan-out
(`Wellwisher`/`Blighted Steppe` "chosen_x" is the k=1 activation, not a menu). Collapsing the
duplicates (`MTG_CAND_DEDUP=1`) removes 2.5% of units but costs more in key hashing: **+0.1%
instructions** (19.531G vs 19.509G). Per CLAUDE.md, with nothing unreasonable left in the branching,
the residual is per-node -- and the per-node profile is FLAT: the hottest self symbol is
`SolveUncached` at 5.4%, then a long tail of 0.5-3% items.

**It is not the budget either.** The mandatory first ladder pass dominates, so the budget barely
binds: units/rollout b3 6,415, b2 6,004, b1 5,750; and **mull_gen_depth 1 vs 2: 6,438 vs 6,552 units**
(CPU 290 vs 318 ms on a loaded box). Lowering the gen depth would buy ~5%, not a night.

## 3. What was changed (all byte-identical)

Byte-identity evidence for every row: the 40-rollout callgrind digest `59f1c80971f8fbd3` and units
(5,896.27/rollout) unchanged; the 4000-rollout bench digest `515488e3a5fe2933` unchanged; the
2000-rollout replay-sample win-turn digest unchanged; smoke 117/118 (the one move,
`selesnya_smoke_d3_s1001` gi74 5->6, is base commit `d379968d`'s documented mana-cache fix and is
produced identically by the base binary); **regression tier at `d80c66bd`: 165/165 PASS byte-identical,
"no searched-depth slowdowns or play changes"; viewer references 506: 0 play-drift / 0 board-diverged /
0 enum-gap / 0 mull-drift** (20 threads).

| commit | change | instructions (40 rollouts) |
|---|---|---|
| base `7e59a977` | tail fix on top of `d379968d` | 19.350G |
| `d06b8c44` | `LookupCached` fast path force-inlined (`CardDatabase.h`, `MTG_DB_ALWAYS_INLINE`); `MTG_RESCUE_TOTAL_GATE` read once (`TurnSolver.cpp` `RescueTotalGateEnv`) | 18.557G (-4.1%) |
| `d06b8c44` | `InternedName::EmptyStr` inline over a `constinit` string (`NameRegistry.h`); `GameState::deck_has_land_aura` gating the four land-Aura battlefield walks (`SpellEffects.h`, stamped in `GoldFishRunner::StampDeckTraits`) | 18.135G (-6.3%) |
| `d80c66bd` | `ApplyPlanDirect`'s site-9 snapshot (`SnapshotActivatableAbilities`) taken only when one of its four readers can fire (`site9_keys_read`, `TurnSolver.cpp`) -- a base plan inside a rollout never reads it | 17.696G (**-8.5%**) |
| `f7eb9932` `./build.sh pgo` | PGO + LTO build (`scripts/build_pgo.sh` -> `build/PGO/`), trained on 48 games at the deck's gen settings | see below |

**PGO+LTO is the largest single lever and changes nothing the program computes.** Same rollout digest,
same play digest (`5cb8f5f626c943ad`), same win turns on all 2000 replay-sample rollouts. No
`-ffast-math`, no `-march`, so IEEE semantics are untouched; it changes layout, inlining and branch
prediction. Linux/GCC only (MSVC's PGO is a different toolchain; the Windows build is unchanged).
**USER 2026-10-08: the default for everything except quick development cycles, screens included.**
`test/lib/harness.sh` (`scripts/engine_bin.py` for Python) now picks `build/PGO` whenever its
`SRC_TREE` equals `HEAD:src` with a clean `src/` -- so a stale PGO binary can never run another engine --
and the long runners (`mullgen.sh`, `valueleaf.sh`, `deck_compare.py`, the overnight tier) build it
first when stale. `./build.sh pgo` with no deck trains on the whole suite. (Originally opt-in via
`MTG_GEN_PGO=1`, generation only, with validation on `build/Release`.)

## 4. Before / after, at the gen settings

Tail agent's replay sample (2000 rollouts, r uniform in [0,30), 16 threads, process CPU = user+sys,
two interleaved runs each; win-turn digest identical in every run):

| binary | CPU s / 2000 rollouts | rollouts/s/core | vs pre-tail |
|---|---|---|---|
| pre-tail `3240890b` (the engine the 10-06 gen ran) | 341.7 / 335.0 | 5.85 / 5.97 | 1.00x |
| tail fix `7e59a977` | 340.7 | 5.87 | 1.00x |
| + `d06b8c44` (Release) | 310.9 | 6.43 | 1.10x |
| + `d06b8c44`, PGO+LTO | 237.9 | 8.41 | 1.44x |
| + `d80c66bd` (Release) | 297.1 | 6.73 | **1.13x** |
| + `d80c66bd`, PGO+LTO (`./build.sh pgo decks/SelesnyaLifegain`) | 232.4 | 8.61 | **1.44x** |

(Two sessions, so two pre-tail rows; each ratio is against the pre-tail run of its own session.)

(The tail fix does not move this sample -- none of its 2000 rollouts is a pathological Nykthos hand --
which is why the tail agent's separate 1.36x and these numbers measure different things.)

## 5. Projected wall for `mullgen.sh run decks/SelesnyaLifegain fast`

The 10-06 run was cancelled at 8.6 h "about half done", i.e. ~17 h total on the same box. Of its
thread-time, ~9% was rollouts >= 30 s (the tail, now fixed) and ~91% the bulk. Bulk at 1.44x and the
tail removed: **~17 h x 0.91 / 1.44 = ~10.9 h** with the PGO binary (~13.9 h on Release at 1.13x),
under the same host conditions. That is the honest
number: **still over an ~8 h night** unless the box runs the gen alone (the 10-06 run shared a host at
load 60-90), or one of the levers below is taken.

## 6. Levers that are NOT byte-identical -- user decisions, measured, not adopted

| lever | speed | quality | status |
|---|---|---|---|
| **horizon leaf depth 0** for this deck (`MTG_FD_LEAF_DEPTH=0`; per deck `MulliganProfile::search_leaf_depth`) | **2.10x** CPU per rollout (136.8 -> 65.2 ms, 4000 paired rollouts) | rollout win turn **+0.040 +- 0.004** worse (190 worse / 32 better / 3778 same of 4000) | OFF. It is a PLAY setting (the user has ruled out generation-only rules), so adopting it means this deck PLAYS with a greedy horizon leaf, which needs the full play A/B. |
| `MTG_TT_NOWIN_CACHE=1` (bound-qualified no-win leaf memo -- a SOUND memo, but freed units move budgeted decisions) | **-3.7%** instructions, -9% units | 40/40 rollouts identical; 1000-rollout digest moves (mean win turn 6.09 both) | existing flag, default OFF engine-wide; adopting it is a GT rebaseline across every deck |
| `MTG_CAND_DEDUP=1` | +0.1% instructions | -- | no speed value on this workload |
| mull_gen_depth 1 instead of 2 | ~5% | not measured | not worth it (section 2) |
| skip the value-leaf probe at d2 when the crossover always takes the heuristic | <= 9% | changes the 22% verified-win decisions | not built |

Leaf depth 0 plus everything in section 3 would put the projection near **17 h x 0.91 / (1.44 x 2.10) = ~5.2 h**.

## 7. What is left, ranked (per-node, all small)

`PrePlanAvailabilityKeys` (3.2%, eager per apply but read by <1% of applies -- needs the pre-apply
state, so it cannot simply be made lazy), `BuildSimKey`/`BuildBreakpointKey` (~4%, the Solve-memo key;
the memo still pays for itself 5x), `CollectAttackingManaSources` + `PendingAttackDamage` per greedy
Solve (3.3%), the plan-signature strings in `EnumeratePlans`' dedup (~1.5%), `SolveUncached`'s odometer
re-summing `mcost` over all groups per position (~1.1%). None is worth more than a few percent; the
structural cost is the 1-ply-lookahead horizon rollout itself (section 2), which only the leaf-depth
decision changes.

## 8. Method notes

* `perf report --children` on a dwarf call-graph sits for 15+ min in `addr2line`; pass `--no-inline`.
* `/workspaces` is case-INSENSITIVE: `build/pgo` and `build/PGO` are the same directory (the first
  PGO script copied its binaries onto themselves). The work tree is `build/pgo-work`.
* An engine change that moves the play digest makes `--gen-mulligan` RE-DISCOVER buckets (10 min, and
  it rewrites the gencache). `MTG_KEEP_BENCH` bypasses the digest in the cache match for that reason;
  `MTG_KEEP_REPLAY_*` does not, so keep a copy of the gencache and restore it after such a run.
