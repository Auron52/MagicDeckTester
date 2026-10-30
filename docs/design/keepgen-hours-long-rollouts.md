# Hinata2 keep generation: the hours-long rollouts (2026-10-30)

**Status: DRAFT DIAGNOSIS, NOT YET MEASURED.** Written read-only while the Hinata2 mulligan generation
held the box (USER: nothing runs beside a generation). Everything below comes from the generation's own
`slow.log` / `gen.log` and from reading the generation's source tree. **No replay, perf or census has been
run yet.** The runs that turn the hypotheses into a diagnosis are queued in section 6, and section 7's fix
depends on what they show. No play change is implemented or proposed for adoption here.

USER: *"Games that are in the hours range should be practically nonexistent when we run with a budget."*
USER: *"But it still should be something we can bound."*

Generation: `artifact-worker-MagicDeckTester2-76d2cc`, `src` tree `beb79a7eb977783b4188e3280f257ee9f38e1f52`
(= `ae41aaad0` + 11 journal commits = `7b22b6db3`, checked out detached at `.claude/worktrees/keepgen-prof`
for the profiling build). Recipe `fast` (R=30), K=20, d1 / b3 ms, `max_turns` 8,
`MTG_DECISION_WORK_X=1000`, PGO binary, 24 threads.

## 1. The data

`slow.log` holds every keep-rollout over 30 s since the run started (2,075 lines, the same set
`gen.log` carries; per-rollout wall under full-box load):

| band | rollouts | thread-hours |
|---|---|---|
| > 30 s | 2,075 | 198.3 |
| > 1 min | 717 | 183.2 |
| > 10 min | 81 | 159.8 |
| > 1 h | 38 | 141.2 |
| > 4 h | 13 | 99.7 |

Worst three: `size7 draw r=1` 15.7 h (Soulfire x2, Preordain, Cascade Bluffs, Orchard x3), `size6 draw r=10`
14.0 h (Soulfire x3, Orchard x3), `size6 draw r=15` 10.2 h (Soulfire x2, Reflecting Pool, Island, Orchard x2).

**What the hands share.** The table below is set against the cell population, not the deck draw, because
the generator enumerates compositions uniformly: 431,144 size-7 cells over the 20 buckets, the same count
as `frozen=0/862288` in `gen.log`.

| feature of the starting hand | all size-7 cells | slow 30-60 s | 1-10 min | 10-60 min | > 1 h |
|---|---|---|---|---|---|
| >= 1 Forbidden Orchard | 30% | 94% | 94% | 95% | 92% |
| >= 2 Orchards | 8.0% | 69% | 73% | 65% | 71% |
| >= 3 Orchards | 1.8% | 35% | 43% | 40% | 39% |
| >= 1 Soulfire Eruption | 30% | 37% | 34% | 47% | **53%** |
| >= 2 Orchards AND >= 1 Soulfire | ~2.5% | 23% | 23% | 30% | **39%** |
| >= 1 Hinata | 30% | 11% | 11% | 5% | **3%** |

So Orchards set up the whole tail, from 30 s up (20x over base at three or more). Soulfire becomes more
common the longer the rollout runs. **Hinata in the opening hand protects against it**: a hand with her
wins before the Spirits pile up. A hand without her plays long (up to `max_turns` 8), the opponent gains
one Spirit per Orchard per turn, and she arrives late if she arrives at all. When the combo finally comes
online, it does so on the widest board of the game.

**What the tail costs the generation.** Five run segments (four resumes) total 78.6 h of wall, about
1,886 thread-hours at 24 threads (the monitor reports `idle=0.0%` throughout).

* CPU: > 30 s rollouts are **10.5%** of all thread-hours, > 10 min **8.5%**, > 1 h **7.5%**, > 4 h **5.3%**.
  These are 81 of roughly 9 M rollouts.
* Wall: the 300 s monitor shows **6.6 h (8.4% of wall)** with throughput under 20% of the segment
  median. One segment-4 stretch had `rollsub 0/s` and the journal silent for 11,255 s. The box was busy
  (idle 0%) but almost every thread was inside a straggler.
* Not counted: each of the four resumes threw away whatever tail rollouts were in flight, and a resume
  reruns them from zero (the slow log only records rollouts that finish). The true cost is therefore
  higher than the table shows.
* Estimate: bounding every rollout to a few minutes would save about **6-10% of this generation's
  wall (5-8 h)** and remove the stall hours. That is an upper-bound estimate, not a measurement.

## 2. What bounds a keep rollout today, and what does not

| bound | where | what it sees |
|---|---|---|
| `SearchBudget` | `SearchBudget.h` (900 units per virtual ms -> 2,700 units at b3) | search nodes / turn-steps only |
| per-decision ceiling `MTG_DECISION_WORK_X=1000` | `DecisionWorkMeter.h`; armed at the two decision roots (`SolveWithLookahead`, `FullSearchLine`) | every `Consume`, plus the greedy `SolveUncached` walk (`TurnSolver.cpp:48190-48210`: with no charge budget installed, which is always the case for Hinata, every visit bills the armed meter and stops the walk once it is exceeded), plus the unbudgeted rollout step (`:80295`). About 2.7 M units per decision. |
| per-rollout wall | none (USER 2026-10-12, `ExhaustiveKeep.cpp` ~2585: *"There should be no abandon wall for mulligan profiles!"*) | -- |

**Unbilled, so neither the 3 ms budget nor the 1000x ceiling can see it:**

1. **`EnumeratePlans`' odometer walk** (the search's plan list). `MTG_ENUM_CHARGE` is built, DEFAULT
   OFF, awaiting the USER (`TurnSolver.cpp:63905`). AccomplishedAlchemist measured ~5x more wall per
   metered unit from this walk alone.
2. **Breakpoint-continuation derivations.** `BpUnbranchedCanon -> BpDeriveContinuationList ->
   EnumeratePlansWithLand -> EnumeratePlans` runs once at every breakpoint the plan does not branch on.
   `MTG_BP_DERIVE_METER` is built, DEFAULT OFF, the USER's decision (`TurnSolver.cpp:27141`,
   `:65015`). EDF measured exactly this class: a hand holding its library, 14-minute decisions billing
   **214 units**, and perf 99.4% under `BpDeriveContinuationList`
   (`docs/design/edf-slow-rollout-tail-2026-10-17.md`).
3. **Per-visit cost.** The meter counts visits and nodes, never what each one costs. A node that copies
   a 40-permanent `GameState` or rescans the board per candidate costs more without billing more.
4. **Anything outside an armed decision root** (executor-side re-solves are separate roots with fresh
   ceilings, per `TurnCensus.h`'s `site` column). Not yet confirmed on this deck.

## 3. Hypothesis H1 (USER): Spirits turn into a huge "draw" through Soulfire

The chain, by code location:

1. **Forbidden Orchard** gives the opponent one 1/1 Spirit per Orchard per turn
   (`SpawnForbiddenOrchardTokensTurnStart`, `SpellEffects.h:19505`; executor `GameEngine.cpp:422`,
   rollout `TurnSolver.cpp:62220`). The Spirits never block and leave only by damage. With three or four
   Orchards, the opponent has roughly 12-25 Spirits by T5-T7.
2. **Soulfire Eruption** targets the opponent's face, yourself if life > 9, **and every opponent
   creature** (`SoulfireTargetCount`, `SpellEffects.h:22941`). `SoulfireDig` (`:23085`) exiles that many
   cards and **stages each one into HAND** (`EnterHand(..., HandEntryReason::Stage)`, playable until end
   of next turn). One Soulfire on a 20-Spirit board therefore puts about 22 cards into hand.
3. **Hinata's discount makes it cheapest exactly when it digs most.** `discount_max_targets: 8` against
   `{6}{R}{R}{R}`, so with six or more targets Soulfire costs `{R}{R}{R}` (and Magma Opus `{U}{R}`). A
   staged Soulfire recast for RRR digs the same amount again, so two casts can stage most of the
   library. That is the EDF "hand holds the library" board, reached through a different door.
4. **Every staged card is a new option.** After the dig the rollout arms the deferred acquisition
   re-solve (`TurnSolver.cpp:57193`, `AcqResolveEnabled`), and each cantrip (Ponder, Preordain,
   Expressive Iteration) opens a breakpoint. Each breakpoint the plan does not branch on derives the full
   continuation list over the bloated hand. The engine's own comment at `TurnSolver.cpp:49270` names
   this explosion for the greedy walk (*"the hand is bloated with the cards a deep Soulfire/cantrip dig
   staged, so the powerset/odometer below explodes"*). The only cut there, the ritual + max-X finisher
   line, fires **only when that line wins**. A non-lethal turn on that hand falls through to the full
   odometer.
5. **Why the budget does not bind.** Steps 2-4 happen inside applies and derivations, which are
   enumeration rather than nodes: unbilled items 1 and 2 above. The greedy walk is billed but stops only
   at 2.7 M visits per decision, and the bill restarts at every new root.

Prediction, testable in section 6: the slow decision is late (T5-T8, after the first Soulfire on a wide
board). It shows few units, enormous `enum_enter`, long `MTG_BP_DERIVE_SLOW_MS` derivations, and a
`[soulfire]` trace with `opp_creatures` in the teens and `n` above 15. perf should show it inclusive
under `BpDeriveContinuationList` / `EnumeratePlans`.

## 4. Hypothesis H2 (USER): efficiency bugs on huge boards

The opponent's battlefield grows by about one permanent per Orchard per turn and never shrinks.
Candidates for per-node cost that grows with board width B:

* each `GameState` copy (per search node, per apply, per `SubsetPayableSequential` board copy) copies
  all B `Permanent`s;
* O(B) board scans per cast candidate or per apply: `HinataInPlay` (`SpellEffects.h:30306`),
  `HinataAvailableTargets` (`:30324`, counts every permanent for `discount_targets_permanents` spells:
  Reality Spasm, Magma Opus), `SoulfireTargetCount`, `SoulfireOppCreatureOrder`, CollectActions' cost
  finalisation, the SBA pass, the state hash / plan signature, and the leaf evaluator's features;
* combat bookkeeping over a board of non-blocking tokens (no blocks, but still walked).

On its own H2 is polynomial: B going from ~10 to ~40 permanents gives 4x per scan, 16x if two nested.
That fits the 30-300 s band but not a 15 h rollout, which is about 75,000x the generation's mean of
~0.75 s. The two hypotheses compound, because H1 multiplies the number of visits and H2 the cost of
each. The test that separates them is **wall per unit and per visit by turn**: H2 shows a steady climb
with B, while H1 shows a cliff at the dig.

## 5. Hypothesis H3: many roots per turn

The executor's `resolve_draw_breakpoint` re-solve (capped at 4,096 calls per main phase) and the other
re-solve sites each open a root with a brand-new `DecisionBudget()` and ceiling. A Soulfire / cantrip
turn opens many of them. This is visible as the `site` column (2 = bp re-solve) in the turn census.

## 6. Queued measurement (run ONLY after the generation ends; at most 4 threads in total)

Everything runs on the gen's own `src` tree, so the replay is byte-identical to the logged rollout.

```
P=/workspaces/MagicDeckTester2/.claude/worktrees/keepgen-prof          # detached at 7b22b6db3
S=/workspaces/MagicDeckTester2/build/scratch/keepgen-slow              # volume scratch (slow.log copy is here)
git -C $P rev-parse HEAD:src     # must print beb79a7eb977783b4188e3280f257ee9f38e1f52
(cd $P && MTG_BUILD_JOBS=4 ./build.sh profile)                         # -> $P/build/Profile/mtg-analyze
mkdir -p $S/deck/Hinata2
cp $P/decks/Hinata2/Hinata2.cod $P/decks/Hinata2/Hinata2.profile.json \
   $P/decks/Hinata2/Hinata2.value.json $P/decks/Hinata2/Hinata2.escgate.json $S/deck/Hinata2/
gunzip -c $P/decks/Hinata2/Hinata2.keepmodel.exhaustive.profile.json.gz > $S/buckets.json  # bucket list must
                                                       # equal the worker's PREV.json (same 20 buckets, checked)
ENV="MTG_DECISION_WORK_X=1000 MTG_KEEP_BUCKETS_FROM=$S/buckets.json MTG_DECISION_PROGRESS=1
     MTG_BP_DERIVE_SLOW_MS=1000 MTG_ENUM_STATS=1 MTG_BP_USE_CENSUS=1 MTG_SOULFIRE_TRACE=1"
ANALYZE="$P/build/Profile/mtg-analyze $S/deck/Hinata2/Hinata2.cod --cards-json $P/src/cards/data/cards.json --gen-mulligan fast"
```

**R1, the full profile of a medium tail rollout** (11.6 min in the gen; one core):
`size7 play r=1 seed=4354685712243287207  hand: Soulfire Eruption x1; Ponder x2; Forbidden Orchard x4`

```
env $ENV MTG_TURN_CENSUS=$S/r1.census.tsv \
  MTG_KEEP_REPLAY="Soulfire Eruption x1; Ponder x2; Forbidden Orchard x4" MTG_KEEP_REPLAY_R=1 MTG_KEEP_REPLAY_PD=1 \
  taskset -c 0 perf record -e cpu-clock -F 49 --call-graph dwarf,8192 --no-buildid -o /tmp/kg-r1.cg.data -- \
  $ANALYZE > $S/r1.out 2> $S/r1.err &
```

(The 64-game play-digest battery runs first, on the one pinned core. The replay's census rows are the
ones from the last seed block, per memory `turn-census-units-vs-buckets`. Then:
`perf report -i /tmp/kg-r1.cg.data --stdio --children --no-call-graph | head -60` and `--no-children`,
`python3 $P/scripts/turn_census.py $S/r1.census.tsv`.)

**R2 and R3, the two worst, in parallel, without perf** (each one core; let them run; they are my own
experiments and may be stopped by PID once the slow turn is characterised):

```
env $ENV MTG_TURN_CENSUS=$S/w1.census.tsv MTG_KEEP_REPLAY="Soulfire Eruption x2; Preordain x1; Cascade Bluffs x1; Forbidden Orchard x3" \
  MTG_KEEP_REPLAY_R=1 MTG_KEEP_REPLAY_PD=0 taskset -c 1 $ANALYZE > $S/w1.out 2> $S/w1.err & echo $! > $S/w1.pid
env $ENV MTG_TURN_CENSUS=$S/w2.census.tsv MTG_KEEP_REPLAY="Soulfire Eruption x3; Forbidden Orchard x3" \
  MTG_KEEP_REPLAY_R=10 MTG_KEEP_REPLAY_PD=0 taskset -c 2 $ANALYZE > $S/w2.out 2> $S/w2.err & echo $! > $S/w2.pid
```

When `MTG_DECISION_PROGRESS` first prints a decision over 60 s (or none has finished 30 min after the
last one), take a 15-minute slice: `perf record -p $(cat $S/w1.pid) -e cpu-clock -F 49 --call-graph
dwarf,8192 --no-buildid -o /tmp/kg-w1-slice.data &`, then `kill -INT <perf pid>`. Attaching should work
at `perf_event_paranoid=2` for an own-uid process, but this is unverified here. If attach is refused,
relaunch R2 under `perf record` with `--switch-output=900s` and read the file for the slow turn.

**What each reading means:**

| reading | means |
|---|---|
| slow turn late, `[soulfire] opp_creatures` >= 10, derivations > 1 s dominate, perf inclusive under `BpDeriveContinuationList` / `EnumeratePlans` | **H1** (derivation / enum walk, unbilled) |
| wall per unit and per `walk_enter` climbs turn over turn with B; perf self-time in `GameState` / `Permanent` copy, board scans | **H2** |
| many `site=2` rows per turn, each modest | **H3** |
| work outside every census row | an unarmed host: find it in perf |

**R4, the population check** (4 threads, after R1-R3 name the mechanism): `MTG_KEEP_REPLAY_LIST=<the
30-60 s band + the 10-60 min band>` with `MTG_KEEP_REPLAY_SAMPLE=1000` for the bulk, one arm per lever
in section 7 against `off`, same thread count across arms. Then the completeness half: every rollout
whose win turn moved, rerun at `MTG_KEEP_BENCH_BUDGET=20`, the EDF procedure.

## 7. Where a budget-respecting bound belongs (proposal; nothing adopted)

**The single right place is the per-decision work meter that already exists.** Every enumeration visit
(greedy walk, the `EnumeratePlans` odometer, breakpoint derivations) should bill the armed
`DecisionWorkMeter` in the same unit. A finite budget then bounds the decision's total work in
proportion to that budget. At `--budget-ms 0` the meter is disarmed (`decisionwork::Armed()` is false),
so nothing is findable at a small budget that a large one cannot reach: raising the budget raises the
ceiling and fills the lines back in. That is the shape CLAUDE.md asks for (a price, not a gate). The
greedy walk already bills it. The other two holes have built levers, default OFF and each the USER's
decision:

* **`MTG_BP_DERIVE_METER=2`** bills derivation visits and stops a derivation once the meter is spent.
  On EDF: tail 0.595x, max 514 s -> 148 s, **+0.083 turns on the bounded hands** at b1, and every moved
  rollout recovered at b20 (re-pricing confirmed). The shape the EDF note recommends next is a higher
  exchange weight for derivation visits (W > 1), so it trips only on pathological derivations.
* **`MTG_ENUM_CHARGE`** (W = 4) bills the search's odometer to the root budget after each enumeration.

Both are **play changes** (budgeted play moves wherever they bind). Per the rules they must apply in play
**and** in generation (generate on the engine you ship; NO UNBUDGETED-ONLY), and need the held-out A/B
plus the large-budget completeness check before the USER decides. They also move the keepgen play digest,
so the banked Hinata2 journal would not resume under them.

**WASTE fixes (play-identical) to try first, scoped by what R1-R3 show:**

* *H1, subset COUNT.* Staged copies are interchangeable (two staged Ponders are "two subsets" of one
  choice). `MTG_FOLD_SEARCH_ODO` (the canonical-prefix fold, adopted for Snow / KittyEquipment)
  measured **zero win-turn changes on all 2,650 EDF rollouts at 0.84x**. Opting Hinata in
  (`FoldSearchOdometerOptIn`) is a USER-reserved decision and needs the held-out A/B. Check also whether
  `MTG_BP_CANON_FRONT` (default ON) engages on these boards (`BpCanonFrontPays`): 7 of 819 canon lists
  were ever branched on in EDF.
* *H2, per-node cost.* Hoist board facts that are constant across a Solve or an enumeration
  (`HinataInPlay`, opponent-creature count, `HinataAvailableTargets` per definition) into the scope that
  owns the board. Look for a "board-level answer per creature" scan (the Fungus family, memory
  `board-level-scan-per-creature-family`). Each is byte-identical by construction and is verified with
  the `.wins` / `.units` identity batch.
* *H3, many roots.* A per-turn cap would be a play change. The waste version is a per-game cache for a
  re-solve on an identical board and hand.

**Recommendation once measured.** Land whatever waste fixes the profile names. Then put to the USER the
bound that closes the hole the census attributes the tail to, most likely `MTG_BP_DERIVE_METER` (at
W > 1) and/or `MTG_ENUM_CHARGE`, with their A/B and b20 completeness numbers on this deck's own tail. Do
not lower the cap or add a wall, which is a stall guard and not a cost fix (`ExhaustiveKeep.cpp`).

## 8. Open questions for the USER (surfaced, not blocking)

1. When the measurement confirms H1: should the derivation / enum charge be adopted fleet-wide (it
   moves budgeted play for every deck where it binds), or first only on the decks whose tails it bounds?
   The rules point fleet-wide: a bound scoped by what keeps GT still is the cop-out shape.
2. Orchard modelling: the opponent gets a Spirit **every turn per Orchard** on the assumption that every
   Orchard is tapped every turn (`cards.json`). The search never chooses not to tap an Orchard. A real
   pilot holding Orchards untapped would keep the board narrow. That is a modelling question and outside
   this note's fix, but it is the root of the board width.
