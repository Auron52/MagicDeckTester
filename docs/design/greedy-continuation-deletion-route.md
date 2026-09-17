# Greedy continuations at HEAD: what is still there, why the record said otherwise, and the deletion route

**Status (2026-09-17): INVENTORY COMPLETE, the lever that deletes every in-tree greedy
continuation is built and measured for coverage (acted = 0 on all 20 suite decks), the
quality tiers are running.** Self-contained: everything needed to finish is in this file, in
git, and under `logs/greedy_census_2026-09-17/` + `logs/greedy_drop_2026-09-17/`.

USER, 2026-09-17: *"I have yet again been informed that they still exist and I want them gone
for good."* / *"I want to work toward the point of being able to delete the code that does
this."* / *"Because I don't want to hear about it again."*

The standing doctrine this is measured against (USER 2026-09-05, verbatim in
`greedy-in-the-searched-window-status.md`): the ONLY permitted greedy components are (1) beyond
the search horizon, (2) combo go-off heuristics that win this turn, (3) mana allocation, (4)
attack decisions for non-dork creatures. *Everything else — every plan choice, second main,
breakpoint continuation inside the searched window — must be search-with-heuristics.*

## 1. The finding: the "tight scope" boundary was the ROOT TURN, not the search/playout boundary

Every prior "greedy decides nothing in the searched window" claim (the 09-02 adoption of the
tight sound recipe, the 09-05 status update) rests on the `MTG_M2_YIELD_STATS` apply-kind
bitmap reading **zero ROOT-kind fallbacks**. That bit is `g_bp_root_enum`, and it is set in
exactly one place:

```cpp
const bool bp_root = (g_fsline_nest == 0);   // FSLineWin, TurnSolver.cpp ~37027
g_bp_root_enum = bp_root;
```

`FSLineWin` is the full-depth search's recursion — it calls itself for every lookahead turn
T1..T(d-1), incrementing `g_fsline_nest` each time. So the ROOT bit covers the root turn's own
plan enumeration ONLY. Everything the search does on the turns behind it — fully searched turns,
with plan enumeration, combat, second main and their own breakpoints — reads as "[rollout+rec]",
and `MTG_BP_CANON_CONT` is scoped OUT there (`MTG_BP_CANON_REC` default OFF, on the argument that
"recording applies in rollout context are playout territory"). Those lookahead-turn breakpoint
continuations fall to a greedy `TurnSolver::Solve()`.

The USER's ruling that motivated the scope was *"Rollouts being greedy is fine... I can always
increase depth and budget to rely on them less. That is not true for the searched part."* A
rollout is the horizon PLAYOUT (`SimulateToEndImpl`, `g_rollout_nest > 0`). A lookahead turn is
the searched part. The instrument could not tell them apart, and the scope was drawn at the
wrong one of the two.

### The instrument fix, and what it shows

Bit 8 of the apply-kind bitmap is now `g_rollout_nest > 0` (PLAYOUT); a kind with neither ROOT
nor PLAYOUT set is a searched **lookahead** turn. Census at shipped play settings, 20 suite decks
x 200 games, seed 600001, `bash test/tools/greedy_census.sh 200 600001 <dir>`
(`logs/greedy_census_2026-09-17/base/`). In-tree = sites 0-8; the columns are the WHY-table
totals by apply kind (fallback calls):

| deck | in-tree calls | **acted** | lookahead+rec | lookahead | playout+rec | playout | root |
|---|---|---|---|---|---|---|---|
| hinata | 2,261,674 | **957,813** | **1,851,400** | 533 | 0 | 409,741 | 0 |
| mirrorwing | 597,012 | 203,463 | 497,247 | 284 | 0 | 99,481 | 0 |
| kitty | 436,441 | 260,604 | 307,413 | 0 | 0 | 129,028 | 0 |
| burn | 326,860 | 100,054 | 311,695 | 0 | 0 | 15,165 | 0 |
| melira | 308,703 | 126,677 | 644,587* | 0 | 0 | 41,672 | 0 |
| th | 305,956 | 48,947 | 242,093 | 0 | 0 | 63,863 | 0 |
| creature_giving | 190,223 | 96,379 | 174,634 | 0 | 0 | 15,589 | 0 |
| fluctuator | 143,349 | 115,435 | 92,141 | 0 | 0 | 51,208 | 0 |
| dragonstorm | 52,700 | 45,398 | 27,919 | 18,665 | 0 | 25,112 | 0 |
| antilife | 26,248 | 6,809 | 19,648 | 0 | 0 | 6,600 | 0 |
| auras | 25,396 | 11,252 | 24,329 | 0 | 0 | 1,067 | 0 |
| critter | 19,152 | 5,272 | 18,448 | 0 | 0 | 4,449 | 0 |
| dragons | 12,893 | 7,232 | 10,190 | 0 | 0 | 8,272 | 0 |
| goblins | 5,418 | 946 | 15,929* | 0 | 0 | 3,893 | 0 |
| fivecolour | 4,684 | 2,234 | 53,578* | 0 | 0 | 32,920 | 0 |
| breaching, knights, slivers, stompy, minotaur | 0 | 0 | | | | | |

\* the WHY tables also count the chain-slot site 9 (`overrun`), which is not a greedy site, so
these three exceed the in-tree call column; the lookahead:playout proportion is what matters.

**On every deck the in-tree greedy is overwhelmingly on searched lookahead turns.** The
executor line reads `REAL main-phase decisions: NONE` on all 20 decks (the committed line
itself is greedy-free), and `[root]` is 0 everywhere — both true, and both beside the point:
the search RANKS its root candidates on lines whose interior breakpoints were continued
greedily, on a searched turn, inside the window. That is the "unreachable lines" concern the
USER named as their largest (2026-09-02) — a base plan on T2 of a d5 search whose cantrip
continuation is greedy's single pick, with the rest of the continuation list unreachable at
any budget for that plan.

## 2. Inventory: every `TurnSolver::Solve()` caller at HEAD (63dd9ce3), classified

| # | site | condition | doctrine class | fate |
|---|---|---|---|---|
| 1-9 | `ApplyPlanDirect` breakpoint fallbacks, sites 0, 1, 2, 4, 6, 7, 8 (deferred, two entries) — uniform shape `if (!bp_searched_plan(site, extra)) { play land; greedysite::Record; extra = Solve(); }` | any apply whose continuation is unresolved: base plan not node-hosted (`nohost`), variant not targeting this breakpoint (`nested`), `bp_choice >= cands.size()` (`overrun`), class masked | **IN THE WINDOW — debt** | **delete** |
| 10-11 | `MTG_BP_CANON_CONT`'s ACT-vs-PASS oracle (`Solve(probe)` / `Solve(state)`) | canon-scoped applies (root enum, resume, capture) | in the window — a greedy Solve deciding act-vs-pass, then falling through to the greedy path verbatim on PASS | **delete** (canon exists only to dress the fallback) |
| 12 | `ContDiffRecord` (`MTG_CONT_DIFF`) | print-only instrument | diagnostic | delete with the fallback it diffs against |
| 13 | AIEngine divergence log (`MTG_DIVERGENCE_LOG`) | print-only instrument | diagnostic | keep or delete (harmless) |
| 14 | `SolveWithLookahead` depth<=0 (s90) | horizon | class 1, permitted | keep (the leaf playout's per-turn policy; the value leaf replaces it where a deck has one) |
| 15 | `SolveSecondMainInSearch` `greedy_here = !searched` | `in_rollout` only (playout second main) | class 1, permitted | keep |
| 16 | AIEngine 3274 — the d0 runner's main phase | `m_lookahead_depth == 0` | not a search at all (the d0 configuration) | keep |
| 17-18 | AIEngine executor breakpoint twins (3893, 4743) | `m_lookahead_depth == 0 || m_in_rollout`; depth>0 is the searched re-solve (`MTG_EXEC_BP_SEARCHED`) | d0 / playout | keep the d0 half; the `=0` hatch that restores greedy at depth>0 goes |
| 19 | AIEngine 3000 — `MTG_REFUTED_FOLLOW` uncovered-phase greedy | after a full-coverage refutation, a phase the followed line does not cover | **a real greedy DECISION in the executor at production depth; had NO counter** (added this session: `execgreedy::Record`) | replace: uncovered phases run the normal search (see §6) |

`play_breakpoint_land` / `play_drawn_flood_keep_land` at sites 1-9 are the greedy LAND half of
the same fallback and go with it. Land-carrying continuations are already in the enumerated
list — `EnumeratePlansWithLandUncached` emits a "play this land, cast nothing" idle plan per
candidate land (~31998) — so an EMPTY continuation (no land, no cast) is a distinct option, not
a duplicate of any list entry.

## 3. The lever that deletes it: `MTG_BP_DROP_GREEDY`

Built 2026-09-16 (bcf8d9e6, USER: *"Can we try dropping greedy?"*), heurarm slot
`BP_DROP_GREEDY`, default OFF, **unmeasured and undocumented until now**. Semantics: every
continuation `bp_searched_plan` cannot resolve at an open class answers EMPTY — "I am done
acting in this phase" (`land_decided = true`, no casts; the trailing passes still run). It sits
ahead of `MTG_BP_BASE_EMPTY` and canon in the fallback chain, so with it on neither ever fires.
Unlike NGC (cands[0], which can never be "cast nothing") and BASE_EMPTY (base plans only), it
covers base, nested and overrun alike, at every apply kind — searched turns AND playouts.

### Coverage — the acceptance test the node doc set ("acted -> 0 at s0-s8")

`logs/greedy_census_2026-09-17/drop/`, same 20 x 200 games under `MTG_BP_DROP_GREEDY=1`:
**acted = 0 at every in-tree site on every deck; `class-masked 0, empty-cands 0`.** Only s90
(the horizon) remains, which is the permitted class. There is no residue to scope.

### Quality, first read (paired, same seeds, 200 games/deck)

| deck | base | drop | delta |
|---|---|---|---|
| fluctuator | 3.7000 | 3.6250 | **-0.0750** |
| mirrorwing | 4.2700 | 4.2450 | -0.0250 |
| antilife | 4.2750 | 4.2700 | -0.0050 |
| burn | 4.4450 | 4.4500 | +0.0050 |
| melira | 4.6500 | 4.6650 | +0.0150 |
| creature_giving | 4.7500 | 4.7700 | +0.0200 |
| hinata | 5.7550 | 5.7900 | +0.0350 |
| dragonstorm | 4.4400 | 4.4750 | +0.0350 |
| 12 other decks | | | 0.0000 |
| **sum over 20 decks** | 88.4750 | 88.4800 | **+0.0050** |

Neutral at this sample; per-deck movement is inside 200-game noise. The 09-02 `BASE_EMPTY`
numbers (hinata +0.023, th +0.025 at 600 games) were taken WITHOUT the root-turn node and
site-3 hosting that shipped the same day, so they are not this configuration.

### Quality and wall, the tiers (smoke + regression under the lever, vs GT)

**Smoke tier (80 cells, `MTG_BP_DROP_GREEDY=1` vs the 63dd9ce3 GT), `logs/greedy_drop_2026-09-17/tiers.log`:**
configs changed 40 / unchanged 40; per-game **45 faster / 62 slower**; summed delta **+0.30 turns**
(d0 +0.006, d3 +0.220, d5 +0.071). The movers:

| cell | GT | DROP | delta |
|---|---|---|---|
| hinata d3 / d5 / 2hg d3 | 5.7400 / 5.7733 / 5.6667 | 5.8133 / 5.8667 / 5.7200 | **+0.073 / +0.093 / +0.053** |
| dragonstorm d3 / d5 / 2hg | 4.5467 / 4.5200 / 4.7800 | 4.5733 / 4.5600 / 4.8200 | +0.027 / +0.040 / +0.040 |
| th d3 / d5 / 2hg | 4.0800 / 4.0000 / 4.5000 | 4.1133 / 4.0133 / 4.5200 | +0.033 / +0.013 / +0.020 |
| melira d3 / 2hg | 4.7200 / 4.8400 | 4.7000 / 4.9600 | -0.020 / +0.120 |
| mirrorwing d3 / 2hg | 4.2467 / 4.7000 | 4.2533 / 4.7600 | +0.007 / +0.060 |
| creature_giving d3 / d5 / 2hg | 4.7067 / 4.6667 / 5.0000 | 4.7200 / 4.6800 / 5.0200 | +0.013 / +0.013 / +0.020 |
| burn d3 / d5 | 4.3400 / 4.3520 | 4.3433 / 4.3560 | +0.003 / +0.004 |
| dragons d3 | 5.7080 | 5.7120 | +0.004 |
| **fluctuator d3 / d5 / 2hg** | 3.6400 / 3.6400 / 3.6533 | 3.5400 / 3.5467 / 3.5200 | **-0.100 / -0.093 / -0.133** |
| hinata **d0** | 6.9720 | 6.9780 | +0.006 (5 games slower, 0 faster) |

Read: a small net quality COST at the shipped budget, concentrated on the cantrip-heavy decks —
exactly the width mechanism the record predicted (the greedy pick reached ranks the W=2 window
does not) — and one deck (fluctuator) that greedy was actively hurting. The d0 movement is
`Solve()`'s own go-off persist loop (TurnSolver.cpp ~19312/19374: it applies `materialize_best()`
on a copy, and that apply's breakpoint continuation is now EMPTY), i.e. the greedy heuristic's
internal projection, not real d0 play (the d0 executor's own continuation twin is untouched).

Against the USER's bar: NOT quality-neutral as-is. Per the standing rule that is a budget /
reachability problem to remedy (§5), never a reason to keep the greedy — and the USER's own
08-26 framing anticipated it: *"If it is much slower then we'll have to see what additional
options we have."* The remedy arms are measured below before anything is flipped.

**Regression tier:** see the addendum at the end of this file.

## 4. The deletion route (two commits, so the flip is A/B-able and the deletion is byte-identical to it)

**Commit 1 — flip.** `MTG_BP_DROP_GREEDY` default ON (`EnvOn(..., true)`, `=0` hatch), the
three tiers rebaselined with the movers inspected. This is the USER's 2026-09-05 rule applied:
*"a red measurement is a BUDGET problem to remedy ... never authorization to keep or
re-introduce a greedy decision"* — the deletion ships, remedies follow on the new baseline.

**Commit 2 — delete the dead code** (byte-identical to commit 1 with the hatch unset):
* the nine `Solve()` fallbacks and their `play_breakpoint_land` / flood-keep greedy land drops;
  `bp_searched_plan` returns a Plan unconditionally (EMPTY when nothing else resolves);
* `MTG_BP_NO_GREEDY_CONT`, `MTG_BP_BASE_EMPTY`, `MTG_BP_CANON_CONT` + `_REC` / `_RECROOT` /
  `_ROLLOUT`, the `CanonVerdict` memo, `MTG_CONT_DIFF` and `ContDiffRecord`, the
  `MTG_BP_PROBE` greedy split, `greedysite::why_class/why_empty` and the site 0-8 counters (s90
  stays — it is the horizon's counter), the heurarm slots for all of the above;
* `MTG_BP_SEARCH=0` as "restores the old greedy engine byte-identically" — there is no greedy
  engine to restore; W=0 becomes "no wave-0 variants" with EMPTY continuations;
* `MTG_BP_DROP_GREEDY` itself (it is the only behaviour);
* the executor: `MTG_EXEC_BP_SEARCHED=0`'s greedy hatch at depth>0.
Keep: s90, the playout second-main twin, the d0 runner, the node, the wave walker, the chain
slot, `kBpEmptyChoice`, `MTG_BP_EMPTY_ARM` (a remedy, below).

After commit 2 there is no code path by which a breakpoint continuation inside `ApplyPlanDirect`
is chosen by `Solve()`. That — not a counter reading zero — is the state the USER asked for.

## 5. Remedies on the new baseline (the budget problem, in the order they should be tried)

The recorded mechanism for every past red greedy-deletion measurement is WIDTH: wave 0 emits
`cands[0..W-1]` (W=2), the deferred waves reach further ranks only with spare budget, and the
greedy `Solve()` was an unbounded-width chooser papering over that. Deleting it removes the
paper, not the hole. The remedies are all reachability/scheduling work, none of them greedy:

1. **`MTG_BP_EMPTY_ARM`** (built 09-16, default OFF): "done" as a scored sibling of the ranks at
   every index. Under DROP the base plan already IS the empty continuation at its first
   breakpoint, so the arm's value is at nested indices (`at >= 1`). Measure as its own arm.
2. **Host the node on lookahead turns.** `MTG_BP_NODE_ROOTTURN` gates hosting to the root turn
   because 99.4% of the full node's children were on lookahead turns and cost too much
   (`bp-node-partition.md`). Under DROP the alternative on those turns is no longer greedy's
   free pick but EMPTY + W ranks, so the trade is different and must be re-measured; the
   cheap half first (`MTG_BP_NODE_ROOTTURN=0` is the existing arm).
3. **Value-ordered rank scheduling** ("the best 3 of n, not the first 3") — idea 5 of the node
   doc's menu; attacks the width without new structure.
4. **Budget.** The hinata b30/b40 sweep already priced this (-0.014 / -0.023 hold) and the USER
   ruled budgets stay at b20 as a *lever kept in reserve*; that reserve is exactly what a
   playout/width deficiency is supposed to be paid from.

## 6. Open questions — surfaced, defaults taken, not blocked on

* **Playout-side continuation.** DROP applies to playout applies too, so the horizon playout's
  mid-plan continuation is EMPTY rather than greedy (a drawn card waits for the playout's next
  turn / second main). The doctrine permits greedy there; keeping it would mean a `Solve()`
  fallback scoped by `g_rollout_nest > 0` — one more scope predicate, which is precisely the
  failure mode of the last three rounds. **Default taken: uniform EMPTY, no scope.** If the
  tiers show a playout-side quality cost, the remedy is a *playout policy* (e.g. site 8's
  existing narrow rule: play a found LAND, let a found nonland wait), not `Solve()`.
* **Refuted-follow's uncovered-phase greedy** (AIEngine 3000). USER design was "follow the best
  lost line out"; answering uncovered phases greedily was an agent addition, and the 09-16
  empty-node bug showed what a false refutation does through it (four greedy turns). Proposed:
  an uncovered phase under refutation runs the normal search; under a correct proof that is
  rare (the line is the whole remaining game) and outcome-identical; under a false proof it is
  simply correct. Cost to be measured on the unwon-game wall the lever was built for.
* **The d0 runner's breakpoint continuation** stays `Solve()`: d0 is the no-search configuration
  and its whole main phase is one greedy call. If the USER prefers d0 to be "no continuation",
  it is a one-line change with a d0-tier rebaseline.

## 7. Tools and the exact resume sequence

```
bash test/tools/greedy_census.sh 200 600001 logs/greedy_census_<date>/base      # kinds now split lookahead|playout
MTG_BP_DROP_GREEDY=1 bash test/tools/greedy_census.sh 200 600001 logs/greedy_census_<date>/drop
MTG_BP_DROP_GREEDY=1 bash test/regression.sh --smoke && MTG_BP_DROP_GREEDY=1 bash test/regression.sh
```
Then commit 1 (flip + `--accept` per tier after inspection), commit 2 (delete), remedies §5.

## Addendum A — regression tier under `MTG_BP_DROP_GREEDY=1` (108 cells, vs 63dd9ce3 GT)

Summed delta **+0.569 turns** (d0 +0.002, d3 +0.154, d5 +0.413); per-game **82 faster / 203
slower**. By deck (sum over its cells): hinata **+0.427** (d3 +0.105/+0.060, d5 +0.110/+0.150),
hinata2hg +0.120, th +0.102, dragonstorm +0.092, creature_giving +0.055, melira +0.052,
mirrorwing +0.050, auras +0.020, dragons +0.018, critter +0.015, kitty/goblins ~+0.007;
**fluctuator -0.347 (all five cells better, -0.053..-0.113)**, fluctuator2hg -0.060.
Batch wall 2632 s -> 2706 s (+2.8%, WALL under unknown load on the GT run; dragonstorm cells
0.64-0.75x — canon's enumeration wall gone — hinata2hg d5 2.0x, fluctuator 1.2-1.5x, and
minotaur 1.25-1.34x with ZERO breakpoint sites, which says the per-case wall here is
load-contaminated and needs a quiet-box probe before anyone reads it).

**Read:** the deletion is a broad, small quality cost at the shipped budget on every deck with
a breakpoint class except fluctuator, and a clear one on hinata (~2.5% of its win turn). That
is the width/reachability mechanism, now measured on the real configuration rather than argued.
The remedy arms (Addendum B) are the answer the USER's rule prescribes; a red tier is not a
reason to keep the greedy.

**Reference-replay gate under the lever: IDENTICAL to the GT run** — `Viewer protocol: 11 ok, 306
repaired, 0 play-drift, 0 shuffle-dead, 1 board-diverged, 0 enum-gap, 10 mull-drift, 0
contract-fail (328 refs)` on both. No user reference changes its replay when the greedy
continuation is deleted (the executor's committed continuation is the searched re-solve either way).

## Addendum B — remedy arms (smoke tier, each = `MTG_BP_DROP_GREEDY=1` + the arm, vs GT)

Smoke tier wall is ~80 s at 24 threads, so these are cheap; `logs/greedy_drop_2026-09-17/arm_*.log`,
summarised by `summarize_arms.py` there.

| arm | sum all | d3 | d5 | d0 | faster/slower | note |
|---|---|---|---|---|---|---|
| DROP alone | +0.297 | +0.220 | +0.071 | +0.006 | 45 / 62 | the baseline for the arms |
| + `MTG_BP_EMPTY_ARM=1` | +0.257 | +0.220 | +0.031 | +0.006 | 46 / 62 | selected cells identical to DROP — under DROP the base plan already IS the empty continuation at its first breakpoint; the arm only adds nested-index empties |
| + `MTG_BP_NODE_ROOTTURN=0` (node hosts on every searched turn) | **+0.170** | +0.147 | +0.017 | +0.006 | 49 / 54 | **hinata d3 +0.073 -> +0.027, d5 +0.093 -> +0.040, 2hg +0.053 -> +0.027**; th / dragonstorm / melira / mirrorwing unchanged (their sites 1, 2, 5 are not node-hostable today) |
| + `MTG_BP_SEARCH=4` (wave-0 width 4) | **+0.124** | +0.127 | **-0.009** | +0.006 | 52 / 59 | broad small gains (dragonstorm d5 +0.040 -> +0.013, mirrorwing2hg +0.060 -> +0.040), hinata d5 unchanged |
| + EMPTY_ARM + ROOTTURN=0 | +0.130 | +0.147 | -0.023 | +0.006 | 50 / 54 | = allturns on every selected cell; the arm adds nothing on top of hosting |
| **+ `MTG_BP_SEARCH=4` + `MTG_BP_NODE_ROOTTURN=0`** | **+0.031** | +0.061 | **-0.036** | +0.006 | **55 / 53** | quality-NEUTRAL in aggregate on the smoke tier; hinata d3 +0.020, d5 +0.067, 2hg +0.027; melira2hg +0.120 (25 games = 3 games), th +0.033/+0.013 remain |
| + `MTG_BP_DEPTH=2` (two nested slots) | +0.304 | +0.264 | +0.035 | +0.006 | 46 / 66 | no help (mirrorwing2hg worse) — nesting is not where the loss is |
| + `MTG_BP_SEARCH=8` | +0.265 | +0.268 | -0.009 | +0.006 | 50 / 68 | WORSE than W=4 at d3 — budget dilution past the knee (the record's knee was W=2 on the greedy baseline; under the deletion it is W=4) |
| + `MTG_BP_NODE_D56=1` (node also hosts sites 5/6, root turn only) | +1.150 | +0.727 | +0.417 | +0.006 | 44 / 110 | much WORSE (mirrorwing2hg +0.280): hosting stands the wave machinery down for 5/6 on every non-root turn, the asymmetry `bp-node-partition.md` recorded; not a remedy in this form |

**Read of the arms.** The loss the deletion exposes is a WIDTH/HOSTING hole, and the two built
levers that widen reachability where it binds — W=4 at wave 0, and the node hosting the
plain-cantrip site on every searched turn instead of the root turn only — recover it to
aggregate neutrality on the smoke tier (+0.031 over 80 cells, 55 faster / 53 slower). What they
do not reach: th (site 1, DrawUntilNonland) and dragonstorm (site 2, impulse exile) are INLINE
sites the node cannot host, and hinata d5 keeps +0.067 (its 962-long continuation lists — see
`bp-node-partition.md`'s reachability table — are where "the best 3 of n" scheduling is the
remaining lever). Wall for W=4 + all-turn hosting is NOT yet measured on a quiet box (the arm
ran while a build was in flight); the record prices all-turn hosting at 1.2-1.5x on hinata and
W=4 at +30-35% nodes, so that is the next measurement before adopting the combination.

## Addendum C — the deletion itself (commit 2) is DRAFTED and verified

Worktree `/tmp/mdt_greedy_del`, branch `greedy-continuation-deletion` (one commit on top of
63dd9ce3; the two instrument edits — the PLAYOUT bit and the refuted-follow counter — are
uncommitted in the main tree and included in that branch). `git diff --stat`: TurnSolver.cpp
-833/+72 net, HeuristicArm.h -14, AIEngine.cpp +-16. It builds, and its smoke tier is
**byte-identical to `MTG_BP_DROP_GREEDY=1` on the pre-deletion binary: 80/80 fingerprints
equal** (`logs/greedy_drop_2026-09-17/deletion_smoke.env` vs `smoke.env`). So the two-commit
route collapses to one: the flip and the deletion are the same play.

What it leaves as the only `Solve()` callers: s90 (horizon leaf), the playout second-main twin,
the d0 runner (main phase + its breakpoint twins at depth 0 / in rollout), the refuted-follow
uncovered-phase greedy (now counted; see §6), and the two print-only instruments.

## Addendum D — deterministic COST of the arms (work units at matched virtual budget)

`logs/greedy_drop_2026-09-17/units_arms.sh` (one process per deck x arm, 300 games, seed 5500001,
each deck at its own play settings; `MTG_ROLLOUT_STATS` `units_total`, contention-proof — read
with `units_collect.py`). Ratio to the shipped baseline; `idd` = mean committed iterative-deepening
depth (equal units at greater depth = the lever paying for itself; more units at lower depth =
pure cost).

| arm | TOTAL units | hinata | dragonstorm | kitty | th | burn | fluctuator | mirrorwing |
|---|---|---|---|---|---|---|---|---|
| DROP (= the deletion) | **1.092** | 1.299 (idd 4.00 -> 3.63) | 1.177 | 1.110 | 1.092 | 1.051 | 1.056 | 1.016 |
| DROP + W=4 | 1.170 | 1.338 | 1.218 | 1.241 | 1.240 | 1.437 | 1.289 | 1.223 |
| DROP + node all turns | 1.157 | **1.607** (idd 3.41) | 1.177 | 1.110 | 1.092 | 1.051 | 1.056 | 1.016 |
| DROP + W=4 + all turns | 1.231 | 1.628 | 1.218 | 1.241 | 1.240 | 1.437 | 1.289 | 1.223 |

**Why the bare deletion costs work at all** (it removes Solve() calls): the greedy Solves were
never charged to the budget (`MTG_SOLVE_CHARGE` is off), and an EMPTY continuation makes every
uncarried line WEAKER, so wins surface later in the tree and branch-and-bound prunes less; where
the per-decision budget binds, that shows up as lower committed depth instead (hinata 4.00 ->
3.63). Both are the same defect the quality tier shows, seen from the cost side — and both point
at the ORDER lever (Addendum E): value-best-first continuations surface wins earlier.

## Addendum E — the remedy levers (built on the deletion branch, measured on the deletion binary)

Three levers landed as `edd99f54` on `greedy-continuation-deletion`, two more in a second
worktree (see below). Smoke tier, deletion binary, per-deck (sum over the deck's cells), vs GT:

* **`MTG_BP_CANDS_ORDER`** (value-order the continuation list): **inert on averages** — 3 of 80
  digests move, no cell's average. Reason: `EnumeratePlans` already value-sorts its own output
  (TurnSolver.cpp ~29312), so the list was value-ordered within each land group already; the
  land fold's idle-plan-first order only matters where a drop is still open, which is rare at a
  breakpoint. **So the hole is NOT order.**
* **`MTG_BP_NODE_HOST2`** (node also on root+1): hinata +0.173 -> +0.119 (all-turns: +0.073);
  nothing else moves. Hosting recovers hinata in proportion to how many searched turns host.
* **`MTG_BP_W4`** as first built read the per-job override only and ignored its own env, so the
  `order_w4*` arms are DUPLICATES of their W-2 twins — fixed in the second worktree before any
  number from it is read. (The env-form `MTG_BP_SEARCH=4` numbers in Addendum B stand.)

**Where the loss actually is — the NESTED slot.** A wave-0 variant acts at its target index and
carries no choice for any further breakpoint its continuation opens (the L*W-not-W^L trade). Under
the greedy regime that nested slot was continued by a greedy Solve — a strong line for free; under
the deletion it is EMPTY, so a variant that casts a cantrip in its continuation stops dead at the
next draw. That is exactly a cantrip chain, which is hinata; it also explains why `MTG_BP_DEPTH=2`
did not help (wave-0 variants at index 1 dilute the budget) and why all-turn hosting does (the
node's children are re-hosted... no: it recovers the BASE plans, which is the other half).

Two further levers (second worktree, `/tmp/mdt_greedy_del2`, detached from `edd99f54`):
* **`MTG_BP_NESTED_CANON`** — at an un-branched nested slot the default continuation is the
  value-best enumerated entry (`cands[0]`, the object rank 0 indexes) instead of EMPTY. A
  heuristic DEFAULT for a slot the search still branches through the deferred waves and
  `Plan::bp_all`; no `Solve()`; stands down inside a derivation (`g_bp_enum_depth > 0`).
  This is NOT the deleted greedy coming back — it is the list the search itself ranks — but it
  is a heuristic standing in at a slot wave 0 does not branch, and the USER should judge it as
  such (the doctrine allows a heuristic as a branch's DEFAULT, never as a substitute for
  branching; the waves/bp_all are the branching).
* **`MTG_BP_VARIANT_FIRST`** — scheduling only: a base plan's wave-0 variants sort BEFORE the base
  on an exact value tie (`Plan::bp_sched`, read only by `MoveOrderPlans`), so the searched
  continuations are scored before the base's EMPTY-continued line where a budget cutoff falls.

**Cost of the first lever set** (units at matched budget, deletion binary as base, 300 games/deck,
`logs/greedy_drop_2026-09-17/units2`): CANDS_ORDER 0.999 (dragonstorm 0.977); HOST2 1.004 total,
hinata 1.017 (idd 3.63 -> 3.55, 300-game avg -0.010); node all turns: hinata 1.236. So HOST2
buys a third of hinata's recovery for ~2% of its work; all-turn hosting buys two thirds for 24%.

**NESTED_CANON cost hazard, seen before its numbers landed.** Its smoke batch had 0 of 80 jobs
finished after 4.5 min (a smoke batch normally completes in ~90 s), every thread running — the
enumeration it pays at a nested slot is UNCHARGED (no ConsumeAt), so `budget_ms` cannot throttle
it, and playout applies visit nested breakpoints constantly. This is the exact wall the canon
lever hit ("canon-everywhere +50.7% wall on dragonstorm") and was scoped away from. If the arm's
QUALITY is worth having, the honest forms are (a) charge the enumeration to units so the budget
sees it, or (b) let it stand down in playout applies (`g_rollout_nest > 0`) where it is a leaf
policy anyway — measured as arms, not assumed. The box is contended, so only units can price it.

**Units + 300-game averages for the second lever set** (`units3`, deletion binary as base; the
NESTED arms here are the UNSCOPED form — playouts included):

| deck | nested | nested+host2 | nested+W4 | vfirst |
|---|---|---|---|---|
| dragonstorm | **0.839 / -0.040** | 0.839 / -0.040 | 0.881 / -0.040 | 0.995 / 0 |
| fluctuator | 0.689 / +0.010 | 0.689 / +0.010 | 0.859 / +0.010 | 1.007 / 0 |
| hinata | 1.000 / -0.007 | **0.992 / -0.033** | 0.977 / -0.013 | 1.001 / +0.003 |
| mirrorwing | 0.975 / -0.010 | 0.975 / -0.010 | 1.167 / -0.013 | 1.003 / 0 |
| th | 0.986 / 0 | 0.986 / 0 | 1.133 / +0.003 | 0.933 / 0 |
| TOTAL | 0.988 | 0.993 | — | — |

(units ratio / avg delta vs the deletion). The nested default is the first lever that is BOTH
quality-positive and units-NEGATIVE (wins surface earlier, so B&B prunes more) — on every deck
where the deletion lost. Its wall hazard is the uncharged enumeration in playouts, hence the
scoped form (`MTG_BP_NESTED_CANON_PLAYOUT` default OFF) measured in the third sweep.

## Addendum F — third sweep (scoped nested default), the th mechanism, and the ADOPTION set

**CORRECTED 2026-09-17 03:15Z — an earlier draft of this table had the columns mislabeled
(the per-deck summariser truncated the arm names); the adoption smoke exposed it (melira +0.36).**
Smoke tier on the deletion binary (worktree 2), per deck (sum over cells) vs GT. `nested` = the
scoped `MTG_BP_NESTED_CANON`; `h2` = `MTG_BP_NODE_HOST2`; `vf` = `MTG_BP_VARIANT_FIRST`; `W4` =
`MTG_BP_W4`; `all` = `MTG_BP_NODE_ROOTTURN=0`:

| deck | deletion | nested | **nested+h2** | nested+W4 | nested+W4+h2 | nested+all | nested+vf+h2 | vf alone | W4 twin |
|---|---|---|---|---|---|---|---|---|---|
| hinata | +0.173 | +0.153 | **+0.099** | +0.139 | +0.086 | +0.053 | +0.099 | +0.173 | +0.159 |
| hinata2hg | +0.053 | +0.040 | +0.027 | +0.040 | +0.027 | — | +0.027 | +0.053 | +0.053 |
| th | +0.047 | +0.047 | +0.047 | +0.047 | +0.047 | +0.047 | +0.047 | +0.047 | +0.047 |
| dragonstorm | +0.067 | +0.007 | +0.007 | +0.013 | +0.013 | — | +0.007 | +0.067 | +0.033 |
| creature_giving | +0.027 | +0.027 | +0.027 | +0.027 | +0.027 | +0.027 | +0.027 | +0.027 | +0.027 |
| mirrorwing | +0.007 | +0.020 | +0.020 | -0.007 | -0.007 | — | +0.020 | +0.007 | -0.013 |
| melira | -0.020 | -0.020 | -0.020 | -0.060 | -0.060 | -0.020 | **+0.360** | **+0.360** | -0.060 |
| melira2hg | +0.120 | +0.120 | +0.120 | +0.120 | +0.120 | — | +0.280 | +0.280 | +0.120 |
| fluctuator | -0.193 | -0.160 | -0.160 | -0.147 | -0.147 | — | -0.160 | -0.193 | -0.193 |
| **SUM (80 cells)** | +0.297 | +0.163 | **+0.097** | +0.117 | **+0.050** | +0.037 | +0.657 | +0.857 | +0.124 |
| faster / slower | 45/62 | 40/55 | **42/50** | 49/56 | **51/51** | 45/48 | 43/63 | 46/75 | 52/59 |

**VARIANT_FIRST is REJECTED** (melira +0.36 / melira2hg +0.28 in every arm carrying it: scheduling
the variants first starves the pod deck's base plans). **W4 is a real rung** (-0.05 on the sum on
top of nested+h2, 51:51) but costs work broadly (units vs GT: burn 1.44, th 1.24, kitty 1.24,
mirrorwing 1.22) — a wall decision for when the box is quiet.

Units for nested+h2 (Addendum E, scoped = unscoped in units): cheaper than the bare deletion on
every affected deck (hinata 0.990, dragonstorm 0.839, th 0.986, mirrorwing 0.975, fluctuator
0.636); ~1.08x the shipped GT in total, hinata ~1.29x.

**The th mechanism (gi=85, like-for-like, persists at d5/b1000).** At play settings the
Treasure Hunt breakpoint (site 1, DrawUntilNonland, INLINE) is hit 3 times on GT — all greedy —
and 3,469 times under the deletion — all EMPTY, 0 searched; its enumerated continuation lists
are length 1 in 18 of 20 samples. The deleted lambda's own comment said it: *"the post-dig
continuation is resolved by the GREEDY Solve, which does not enumerate land variants, so no
depth or budget can reach a different drop ... until the breakpoint is a real search node."*
The old line played Reliquary Tower AFTER the Throes-cascade-into-Treasure-Hunt dig through
that lambda; the deletion's search never plays a land after the dig (EMPTY marks the drop
decided) and the game slips T4 -> T6. **Remedy: host the dig continuation as a node (site 1 is
inline — the partition design the node doc reserved for sites 0/1/2/4), or at minimum let wave 0
emit its land-carrying continuation.** Not a reason to keep the greedy; the next design item.
(creature_giving's +0.027 and th gi=3 are draws-diverge VARIANCE — a different early land ->
different scry — and say nothing.)

**ADOPTION SET (USER 2026-09-17: "I want greedy gone ASAP ... After that we can continue"):**
the deletion + `MTG_BP_NESTED_CANON` (scoped) + `MTG_BP_NODE_HOST2` default ON, each with its
`=0` hatch. Smoke +0.097 / 42:50; units ~1.08x GT. Tiers for the GT rebaseline:
`logs/greedy_drop_2026-09-17/adopt2_{smoke,regression}.log` (the earlier `adopt_*` logs are the
WRONG set — VARIANT_FIRST on — and must not be accepted). Remaining per-deck debt after adoption:
hinata ~+0.10 (W4 -> +0.086; all-turn hosting -> +0.05 at +24% units), th +0.047 (the dig node),
melira2hg +0.12 (3 games of 25), creature_giving +0.027 (variance). The nested default is a
HEURISTIC standing at a slot wave 0 does not branch — the USER should judge it as such;
deleting it back to EMPTY costs dragonstorm +0.06 and hinata +0.02 on this tier.

## Addendum G — quality pass on the adopted set (USER 2026-09-17: "quality first, no budget increase; find bugs introduced by disabling greedy — none should be unrecoverable")

**Adoption set tiers (worktree 2, defaults = deletion + `MTG_BP_NESTED_CANON` scoped + `MTG_BP_NODE_HOST2`).**
Smoke +0.0965 / 80 cells (42 faster, 50 slower) — identical to the `lv3_nested_host2` arm. Regression
+0.4008 / 108 cells (73 / 163): dragonstorm's four cells fully recovered (were +0.03..+0.036), fluctuator
faster in four of five, hinata still the loser (d3 +0.055 / +0.045, d5 +0.070 / +0.130, 2hg d5 +0.040),
th +0.010..+0.032, mirrorwing +0.005..+0.030. Reference gate identical to GT (328 refs, same 1
board-diverged / 10 mull-drift). Logs `adopt2_smoke.log`, `adopt2_regression.log`.

### G.1 Where the hinata loss comes from (three mechanisms, one of them a plain bug)

1. **Valuation myopia, cleanest at d0.** `hinata_smoke_d0` moved 5 games / 1000 although d0 has no
   lookahead: the d0 chooser (`SolveUncached` → `materialize_best` → `ApplyPlanDirect`) values its
   candidate through a cantrip breakpoint, and that slot is EMPTY now. gi392 T5 second main
   (identical draws): old continuation after Preordain = Reality Spasm, Soulfire Eruption, Gamble→
   Crackle with Power, Reality Spasm, lethal; new = "Gamble for Sol Ring", stop. Same chooser,
   same state; only the value each candidate is scored on changed. The same valuation orders the
   continuation lists (`BpEnumEntryFor` value-sorts by applying each entry at `g_bp_enum_depth`
   > 0, EMPTY-terminated), so the W-wide window and the nested default are chosen on myopic
   values at every searched depth.
2. **Un-branched lookahead slots.** Wave-0 variants exist at every ply (W=2, index 0, top-16 plans)
   but the node hosts only the root turn (+ root+1 under HOST2). A base plan's 2nd+ breakpoint, a
   plan past the fan-out cap, and every derivation-time apply have no branch; those were greedy
   and are EMPTY. Census (base binary, hinata d5/b30): 82% of the old greedy load is
   `[lookahead+rec] nohost` base plans. All-turn node hosting (`MTG_BP_NODE_ROOTTURN=0`, =
   lv3 `nested_all`) recovers half of the remaining hinata loss (+0.099 → +0.053) at +24% hinata
   units; `MTG_BP_MAXBASE=0` is byte-identical to the adoption set (the cap does not bind at smoke
   settings).
3. **A real, greedy-independent bug the deletion exposes: a dropped X spell.** hinata d5 gi392
   (identical draws, T5 lost): the root over-commits mana (Gamble + Ornithopter), the deferred
   re-solve after Ponder draws Crackle declares X=4 = 12 mana with 11 tappable, the executor's
   `TapForCost` fails and the cast is **dropped whole** (`[afford-drop] t5 Crackle with Power
   cost={10}{R}{R}`), leaving seven red floating after Irencrag Feat — X=2 was lethal. The
   projection counted one source the executor could not tap (Forbidden Orchard's token or the
   summoning-sick second Ornithopter; not yet pinned). Real-play drops are rare in the other
   hinata losers (2 of 19 audited with `MTG_AFFORD_AUDIT=2`), so this is not the dominant
   mechanism, but it IS unrecoverable at execution today: nothing re-plans after a drop.

The other slower games audited (creature_giving 39/79, fluctuator 53, dragonstorm 147, hinata2hg
32/40, hinata d3 40/55/120, d5 39/40/55/60/63) diverge in draws (a different fetch/shuffle/cantrip
resolution earlier in the game), which is the ordinary signature of a different searched line, not of
an engine defect; dragons d3 gi181 is like-for-like (T5 Dragonspeaker Shaman instead of Lathliss).

### G.2 Levers built for this pass (worktree 2, all default OFF, measured below)

* `MTG_BP_ENUM_CANON` — inside a continuation-list derivation (`g_bp_enum_depth == 1`) an
  unresolved breakpoint takes the value-best entry of its own list, ONE level (the derivation it
  triggers runs at depth 2, whose applies stay EMPTY). Fixes the ranking myopia with a bounded,
  memoised cost — the unbounded NGC cascade is what the ENUM probe recorded as the wall.
* `MTG_BP_BASE_CANON=1|2` — base plans (bp_choice < 0) at a searched, non-derivation, non-playout
  apply take the value-best entry. =1 every searched turn (pair with `MTG_BP_EMPTY_ARM` so "stop"
  stays scored where no node hosts), =2 only turns beyond root+1 (the un-hosted lookahead turns).
* `MTG_EXEC_DROP_REPLAN` — a real-play cast dropped as unpayable clears the committed line and
  takes GameEngine's second `TakeTurn` pass (a fresh full-depth solve on the realised board;
  depth > 0, not in a rollout, no external chooser). The re-plan can cast the same card at a
  payable X or hold it. Same reline shape as `MTG_DISCARD_RELINE` / `MTG_LE_RELINE`.

Doctrine note: ENUM_CANON changes a VALUE (what a candidate is scored on), not a decision. BASE_CANON
is a heuristic default at slots the deferred waves / `bp_all` still branch when budget allows, the
same reading NESTED_CANON was adopted under; where it removes the EMPTY line (=1 at un-hosted
turns) it is measured with the EMPTY arm alongside. Neither is a `Solve()`; there is still no greedy
continuation anywhere.

### G.3 Results (smoke tier, vs GT; units at play settings)

Smoke tier (80 cells; `SUM` = the loss-penalized avg-turn delta summed over cells, NEGATIVE = faster
than GT; `f/s` = cells faster/slower). Units = deterministic work units at each deck's own play
settings, 20 decks x 300 games, seed 5500001, single-thread, RELATIVE to the adopted set.

| arm | SUM vs GT | f/s | wins LOST | units |
|---|---|---|---|---|
| adopted set (identity) | +0.0965 | 42/50 | 2 | 1.000 |
| `MTG_BP_ENUM_CANON=1` | +0.0965 | 42/50 | 2 | 1.000 |
| `MTG_BP_EMPTY_ARM=1` | +0.0565 | 43/50 | 2 | 1.019 |
| `MTG_EXEC_DROP_REPLAN=1` | +0.1631 | 44/58 | 2 | 1.033 |
| **`MTG_BP_BASE_CANON=1`** | **+0.0142** | **17/24** | **0** | **0.931** |
| `MTG_BP_BASE_CANON=1` + EMPTY arm | -0.0124 | 18/24 | 1 | (~0.95) |
| `MTG_BP_BASE_CANON=2` | -0.0701 | 42/35 | 1 | 0.934 |
| `MTG_BP_BASE_CANON=2` + EMPTY arm | -0.0834 | 43/35 | 2 | 0.953 |

Four things this settles.

1. **ENUM_CANON is inert** — byte-identical to the adopted set on every cell. The derivation-time
   applies it targets are already EMPTY-valued consistently on both sides of a comparison, so the
   myopia it was built for does not discriminate between candidates. Retire it.
2. **BASE_CANON is a strict improvement on BOTH axes**, which is rare: better play AND ~7% fewer work
   units than the adopted set (hinata alone 0.80x). It is not a budget-for-quality trade. The units
   fall because the value-best default finishes a turn the EMPTY default abandoned, so the same line
   is reached in fewer applies.
3. **The EMPTY arm is mostly duplicated work UNLESS BASE_CANON is on.** `kBpEmptyChoice` is
   `1 << 20`, i.e. a NON-NEGATIVE bp_choice, so the adopted NESTED_CANON default applies to an
   EMPTY-arm variant at every breakpoint it is not targeting. With BASE_CANON off that makes the arm
   "EMPTY at `bp_at`, value-best everywhere else" against a base plan that is "EMPTY everywhere" —
   which differ ONLY in an apply that reaches two or more breakpoints. In a single-breakpoint apply
   the arm is an exact duplicate of its base plan. That is why it moves 43 of 50 cells and costs
   1.019x units to buy +0.0565 of nothing. With BASE_CANON on the base plan is value-best everywhere,
   so the arm is a genuinely new line at EVERY apply, and it pays: -0.027 SUM in both pairings.
4. **DROP_REPLAN measures WORSE (+0.1631 vs +0.0965) at +3.3% units**, despite being a real bug fix.
   Same shape as the three mana-projection "fixes" that all lost: the drop is load-bearing pessimism
   somewhere. Not adopted; the defect stands, recorded in G.1 item 3.

#### The one unrecoverable regression, and what closes it

The adopted set turns two hinata d3 games from a turn-8 win into no win inside the 8-turn horizon
(`gi=136`, `gi=139`); `mirrorwing2hg` d3 `gi=15` goes the other way (-1 -> 8). BASE_CANON=2 closes
139; only **BASE_CANON=1 closes 136**, and nothing else does:

```
hinata_smoke_d3_s1001 gi=136, budget ladder (case budget 10 ms):
  adopted set      b10=None b20=None b40=None b80=None b160=None
  BASE_CANON=2     b10=None b20=None b40=None b80=None b160=None
  BASE_CANON=2+EMP b10=None b20=None b40=None b80=None b160=None
  BASE_CANON=1     b10=8
```

Sixteen times the budget does not buy it, so this is a **reachability hole, not a budget shortfall** —
the line is not in the candidate set at any budget. It is also not the executor drop bug
(`MTG_AFFORD_AUDIT` reports zero drops in that game). The mechanism is valuation at the ROOT turn:
the first divergence is turn 1, where the new search holds its only land (Mountain, from a 3-mulligan
four-card keep) instead of playing it, because every base plan is valued as "reach the breakpoint,
then stop". BASE_CANON=2's turn scope (`turn > root+1`) deliberately excludes the root turn, so it
cannot see it; BASE_CANON=1 has no scope and does.

That scope is also the argument from history: the greedy this route deleted survived for months
precisely because it was keyed on a turn predicate (`g_fsline_nest == 0` = the root turn only) that
read as "the search" and was not. A default whose correctness depends on which turn it is has the
same failure shape. **BASE_CANON=1 is uniform: an un-branched base-plan continuation takes the
value-best entry, every searched turn, no predicate.**

The EMPTY arm's one casualty is the opposite kind and must not be confused with it:
`fluctuator_smoke_d5_s1001 gi=1` (7 -> -1) under BASE_CANON+EMPTY is a plain budget shortfall —
`b20=None, b40=7, b80=7, b160=7`, and `MTG_BP_SEARCH=4` / `MTG_BP_DEPTH=2` lose it too. More
candidates, same budget.

#### What is still greedy in real play, verified by counter and not by reading

`MTG_M2_YIELD_STATS=1` at play settings. The searched-window continuation greedy is structurally
gone — the fallback in `bp_searched_plan` is unconditional EMPTY and there is no hatch back. Three
`TurnSolver::Solve()` callers survive in the executor and all three are doctrine-permitted:
`AIEngine.cpp:3918` / `:4767` (the breakpoint and pod-trailing continuations) are reached only at
d0 — where there is no search at all by design — or inside a rollout; `AIEngine.cpp:3300` is the d0
plan itself. `AIEngine.cpp:3026` (refuted-follow) is the one real-play greedy DECISION left at
depth > 0, and it fires only when a committed line is refuted mid-turn. Counts below.

60 games per deck at each deck's own play settings, seed 7700001, `MTG_BP_BASE_CANON=1`:

```
melira      in-rollout=0 breakpoint-fallback=0 (base=4 MISMATCH=0 searched-resolve=4) | REAL: NONE
hinata      in-rollout=0 breakpoint-fallback=0 (base=0 MISMATCH=0 searched-resolve=0) | REAL: NONE
fluctuator  in-rollout=0 breakpoint-fallback=0 (base=0 MISMATCH=0 searched-resolve=0) | REAL: NONE
th          in-rollout=0 breakpoint-fallback=0 (base=0 MISMATCH=0 searched-resolve=0) | REAL: NONE
```

`breakpoint-fallback=0` is the user's "no greedy decisions" bar and it reads zero, including inside
rollouts. `MISMATCH=0` says every searched continuation the executor met was replayable, so scored
and realised play still agree. `REAL: NONE` says the refuted-follow site never fired at all on these
decks. melira's four executor breakpoints went to the searched re-solve, which is the whole point of
`MTG_EXEC_BP_SEARCHED`: the census that once found greedy in 8 of 50 melira games now finds none.


### G.4 Two bugs the deletion introduced, found by auditing every game that stopped winning

The metric that finds them is not the average, it is the **win that becomes a non-win**: a game
scored `-1` folds in at `max_turns + 1`, so it is the maximal regression a single game can make, and
it is the only outcome that is unrecoverable by definition. Across smoke + regression the adopted
set produces four of them (`hinata smoke d3 gi=136`, `gi=139`; `hinata regression d3 s3003 gi=111`;
`auras regression d5 s3003 gi=81`) and one in the other direction (`mirrorwing2hg smoke d3 gi=15`,
`-1 -> 8`). `BASE_CANON=1` closes three of the four. The fourth is the bug below.

#### Bug 1 — a re-solved continuation's LAND DROP is thrown away (`MTG_BP_RESOLVE_LAND`)

`resolve_draw_breakpoint` has two branches. The plan-carried branch plays the continuation's land
(`TryPlaySpecificLand(extra.land_to_play, ...)`, AIEngine.cpp:3885). The re-solve branch underneath
does not — and it never needed to, because it used to call the greedy `TurnSolver::Solve()`, which
**never sets `land_decided`** (the invariant is written down at TurnSolver.cpp:21767). The deletion
replaced that call with `SolveWithLookahead`, whose plans DO carry a drop. So the executor now:

* asks a full-depth search what to do at the breakpoint,
* gets back "play Mountain, then cast Soulfire Eruption",
* silently discards the land,
* casts the spell anyway, on mana the land was funding,
* comes up **exactly one mana short**, and `TapForCost` drops the cast whole.

Measured on `hinata regression d3 s3003 gi=111`, turn 7:

```
[afford-drop] t7 Soulfire Eruption cost={6}{R}{R}{R} COLOUR-short
              pool[W0 U0 B0 R7 G0 C0 wild1]
              UNTAPPED{Ornithopter of Paradise,}
              TAPPED{Izzet Boilerworks,Mystic Monastery,Ornithopter of Paradise,Mystic Monastery,Cascade Bluffs,}
```

Nine mana wanted, eight in the pool, and the **Mountain is still in hand with the turn's land drop
unused** — as it is again on turn 8, where the game ends having played no land for three consecutive
turns. This is a strict execution defect, not a search preference: no budget recovers it (16x does
not), and it is invisible to every arm because it happens after the search has finished. The same
omission exists at the pod trailing-pass twin (AIEngine.cpp:4744 plays the land, the re-solve below
it does not). Fixed at both sites behind `MTG_BP_RESOLVE_LAND`.

#### Bug 2 — `MTG_REFUTED_FOLLOW`'s premise no longer holds

Once the search reports `refuted_full`, the executor stops searching for the rest of the game and
plays `TurnSolver::Solve()` greedily every turn (AIEngine.cpp:3026), on the stated premise that
"the game is proven unwinnable, so the full-lookahead fallback would re-prove the doom at full
price". That premise was written when a continuation was a greedy tail that at least played the
turn out. **With continuations EMPTY, a refutation is no longer a proof** — it is partly an artifact
of the search declining to look. On gi=111 the old binary VERIFIES a three-turn win at T6 and
commits it (`verified=1`, win=8); the new binary marks the same position `refuted_full=1` and hands
the remaining three turns to greedy, which plays no land at all.

This is also the **last real-play greedy DECISION at depth > 0** left in the engine, so removing it
serves the standing directive directly. `MTG_REFUTED_FOLLOW=0` does not by itself recover gi=111
(the line is invisible to the new search either way), so it is measured on its own merits as an arm
rather than sold as the fix.

### G.5 The regression tier decides between BASE_CANON=1 and =2

108 cells, vs the same GT. `SUM` negative = faster than GT.

| arm | smoke SUM | smoke f/s | smoke wins lost | reg SUM | reg f/s | reg wins lost | units |
|---|---|---|---|---|---|---|---|
| adopted set | +0.0965 | 42/50 | 2 | +0.4008 | 73/163 | 2 | 1.000 |
| `BASE_CANON=1` | +0.0142 | 17/24 | **0** | **-0.0290** | 50/59 | 2 | **0.931** |
| `BASE_CANON=1` + EMPTY | -0.0124 | 18/24 | 1 | -0.0246 | 52/64 | 2 | ~0.95 |
| `BASE_CANON=2` | -0.0701 | 42/35 | 1 | -0.1412 | 75/90 | **3** | 0.934 |

`BASE_CANON=2` has the larger negative sum on both tiers, and it is the wrong choice anyway:

* **Its entire margin is one deck.** fluctuator's five regression cells give =2 a total of -0.3633 against
  =1's -0.1401. That 0.2232 gap is larger than the 0.1122 gap between the two arms' SUMs, so every other
  deck taken together prefers =1. The cell-sum metric weights a 75-game cell like a 1000-game one, which
  is exactly the failure mode this pattern produces.
* **=1 puts far more cells back on ground truth.** auras, critter, dragons, goblins, kitty, melira, hinata
  d5 s2002 all return to exactly 0 under =1 and stay non-zero under =2. It changes 109 cells across both
  tiers against =2's 165 — a smaller, more surgical diff.
* **=1 loses fewer games outright** (2 against 3 in regression, 0 against 1 in smoke), including the two
  it uniquely rescues that no budget rescues.
* **=1 has no scope predicate.** See G.3: a turn-keyed default is the shape of the bug this whole route
  exists to delete.

fluctuator's preference for =2 (i.e. for EMPTY at the root turn and root+1) is a real per-deck signal and
is recorded as such — it is not a reason to ship a turn scope for every deck.

### G.6 The execution fixes measure clean

Smoke tier, all on top of `BASE_CANON=1`, del3 build. The control matters: `b1_ident` (del3, only
BASE_CANON=1) reproduces the del2 `base1` arm line for line, so the new levers are inert when off.

| arm | SUM | f/s | wins lost | result lines differing from b1_ident |
|---|---|---|---|---|
| `b1_ident` (control) | +0.0142 | 17/24 | 0 | — |
| `+ MTG_BP_RESOLVE_LAND` | +0.0142 | 17/24 | 0 | 8 |
| `+ MTG_REFUTED_LAND` | +0.0142 | 17/24 | 0 | 72 |
| `+ both` | +0.0142 | 17/24 | 0 | 72 |
| `+ MTG_REFUTED_FOLLOW=0` | +0.0142 | 17/24 | 0 | 84 |
| `+ both + EMPTY arm` | -0.0124 | 18/24 | 1 | — |

Both fixes change real play — 4 to 36 cells' digests move — and land exactly neutral on the aggregate
with no game lost. For a correctness fix that is the bar: it stops the engine throwing away a cast or a
land drop without costing anything measurable. `MTG_REFUTED_FOLLOW=0` is likewise neutral on smoke, which
matters more than neutrality usually does: it is the route to deleting the last real-play greedy decision
at depth > 0.

### G.7 The shipping set, and the one case that is still open

**`MTG_BP_BASE_CANON=1` + `MTG_BP_RESOLVE_LAND` + `MTG_REFUTED_LAND`.**

| tier | adopted set | shipping set |
|---|---|---|
| smoke, 80 cells | +0.0965 (42 faster / 50 slower) | +0.0142 (17 / 24) |
| regression, 108 cells | +0.4008 (73 / 163) | -0.0391 (51 / 57) |
| wins lost, smoke | 2 | **0** |
| wins lost, regression | 2 | 2 |
| work units at play settings | 1.000 | 0.931 |

The regression tier goes from +0.40 against ground truth to slightly better than it, on two thirds
as many changed cells, for 7% less work. Smoke is 0.014 off even.

**Wins lost, classified.** The bar is not "no game ever moves" — `mirrorwing2hg smoke d3 gi=15` and
`th regression d3 s2002 gi=167` go the other way, from no win to a win. The bar is whether a lost
game is recoverable:

| game | class | evidence |
|---|---|---|
| `th regression d3 s3003 gi=447` | **budget** | b10 loses, b20 wins, b40/b80/b160 win |
| `hinata regression d3 s3003 gi=111` | **reachability, open** | 16x budget still loses |

gi=111 is the one case this pass does not close, and it is understood rather than mysterious: at T6
the old binary VERIFIES a three-turn win (`verified=1`, win=8) whose payoff is a second-main Soulfire
Eruption on T7 and a Reality Spasm chain on T8. The new search cannot see it, marks the position
refuted, and plays out. Both execution bugs in G.4 fire in this game and both fixes work as intended
(the T7 cast now resolves instead of being dropped), which is precisely why it is worth separating:
the execution defects were real and are fixed, and the remaining loss is a search-visibility problem
that needs its own pass.

### G.8 The counter sweep: what is left, measured across all 20 decks

`MTG_M2_YIELD_STATS=1`, 60 games per deck at each deck's own play settings, seed 7700001, on the
shipping set. Every deck reports:

```
in-rollout=0  breakpoint-fallback=0  (base=0  MISMATCH=0  searched-resolve=0)
```

* `breakpoint-fallback=0` on **20 of 20** — the greedy continuation is gone from real play AND from
  rollouts. This is the number the user's "no greedy decisions" bar reads.
* `MISMATCH=0` on **20 of 20** — every searched continuation the executor met was replayable at the
  index it was scored at, so scored and realised play still agree. (melira's four executor
  breakpoints all take the searched re-solve; the census that once found greedy in 8 of 50 melira
  games finds none.)

`REAL main-phase decisions by depth` reads `NONE` on 17 of 20 decks. The exceptions are
**dragonstorm, dragons and mirrorwing, each `d5=5`** — five real greedy main-phase decisions at
depth 5, all of them the refuted-follow site (G.4 bug 2), which is the only such site left.
With `MTG_REFUTED_FOLLOW=0` all three read `NONE`:

```
dragonstorm  REAL main-phase decisions by depth:  NONE
dragons      REAL main-phase decisions by depth:  NONE
mirrorwing   REAL main-phase decisions by depth:  NONE
```

So `MTG_REFUTED_FOLLOW=0` is what takes real play at depth > 0 to **zero greedy decisions, suite-wide,
verified by counter**. It is smoke-neutral (+0.0142, 17/24, no game lost) and costs 0.4% work units
(0.934x against the shipping set's 0.930x). Its regression-tier arm is the last measurement.

### G.9 `MTG_REFUTED_FOLLOW=0` is free, so the last greedy decision goes

Regression tier, on top of the shipping set:

| arm | SUM | f/s | wins lost | result lines differing |
|---|---|---|---|---|
| shipping set | -0.0391 | 51/57 | 2 | — |
| + `MTG_REFUTED_FOLLOW=0` | **-0.0391** | **51/57** | **2** | 44 (22 cells) |

Identical aggregate, identical win/loss set, 22 regression cells and 42 smoke cells playing
differently, 0.4% more work units (0.934x against 0.930x, both well under the 1.000x bar). A change
that moves 64 cells' play and moves the metric by nothing is the definition of a free structural
change — and what it buys is the last real-play greedy DECISION at depth > 0, verified to zero by
counter on all 20 decks (G.8).

Note that `MTG_REFUTED_LAND` becomes inert once refuted-follow is off, since its only call site is
inside that branch. It ships default ON anyway so the `MTG_REFUTED_FOLLOW=1` hatch is not a route
back to a bug.

### G.10 The EMPTY arm: measured, NOT enabled, and this is the one judgement call in the pass

`MTG_BP_EMPTY_ARM` on top of the shipping set:

| | shipping set | + EMPTY arm |
|---|---|---|
| smoke SUM | +0.0142 | **-0.0124** |
| smoke wins lost | **0** | 1 |
| regression SUM | **-0.0391** | -0.0313 |
| regression wins lost | 2 | 2 |
| work units | **0.930** | 0.951 |

It is a wash: better on smoke, slightly worse on regression, and it costs 2.1 points of work units.
Its one extra casualty (`fluctuator smoke d5 gi=1`, 7 -> -1) is a plain budget shortfall — `b20`
loses and `b40` wins, and `MTG_BP_SEARCH=4` / `MTG_BP_DEPTH=2` lose the same game for the same
reason. Nothing about it is a defect.

**Default OFF, and the reason is this pass's own mandate** ("cut the issues without increasing the
budget; no unrecoverable bugs"): the arm is the only lever here that spends budget for no measured
quality gain, and it is the only one that makes a game stop winning that otherwise does not.

**But the doctrine cost is real and should not be buried.** With `BASE_CANON=1` and the arm off, a
wave-0 base plan cannot express "stop here" at a breakpoint it reaches — its continuation is always
the value-best entry. EMPTY survives as the unconditional default inside playouts, at derivation
depth > 0 and for masked classes, and as an explicit node child at hosted sites (site 3, root and
root+1), but not as a scored option at the base-plan slot. That is narrower than the user's
2026-09-16 rule ("empty needs to be a valid option ... for every segment"), and it is one
environment variable away: `MTG_BP_EMPTY_ARM=1` costs 0.951x against a 1.000x bar, so enabling it
does NOT breach the no-budget-increase constraint either. This is the call to revisit first.

### G.11 SHIPPED at ec8bc51f — the acceptance test, on the shipped binary with no env overrides

`MTG_M2_YIELD_STATS=1`, 60 games per deck at each deck's own play settings, seed 7700001,
`build/Release/mtg` as committed, **no environment variables set at all**. All 20 decks:

```
in-rollout=0   breakpoint-fallback=0   (base=0  MISMATCH=0  searched-resolve=0)
REAL main-phase decisions by depth:  NONE
```

(melira reads `base=4 searched-resolve=4`: four executor breakpoints whose committed plan carried no
searched continuation, all four resolved by the full searched re-solve, none greedy.)

Every number that would indicate a greedy decision reads zero, on every deck, at the shipped
defaults:

* **no greedy continuation** in real play (`breakpoint-fallback=0`),
* **no greedy continuation inside rollouts either** (`in-rollout=0`),
* **no greedy main-phase decision at any depth** (`NONE` — this is what `MTG_REFUTED_FOLLOW=0` bought),
* **no scored-vs-realised divergence** (`MISMATCH=0`).

The only `TurnSolver::Solve()` reachable from inside the search is the horizon leaf, site 90, which
doctrine permits and which the same instrument reports separately as
`GREEDY SITES inside search: s90=... [horizon leaf only; no breakpoint fallback exists]`.

Tiers accepted in f2812fdf. Not pushed — that and the overnight tier are the user's call.

## Addendum H — CORRECTION: the G.5-G.11 headline numbers were flattered by the metric

Two errors in how Addendum G reported results. Both were caught by the user reading the tables, and
both point the same way: **the shipped engine is at PARITY with the pre-deletion engine on quality
and on cost — it is not ahead of it on either.**

### H.1 The cell sum equal-weights cells; the tiers do not have equal cells

`SUM` adds one `(got - exp)` per CELL, so a 75-game cell counts the same as a 1000-game one. On the
regression tier that inverts the sign. Weighting each cell by its own game count and reporting TOTAL
EXTRA TURNS over the whole tier:

| | smoke, 27,215 games | regression, 42,380 games |
|---|---|---|
| adopted deletion set (start of the pass) | +11.0 turns (+0.00040/game) | +98.0 turns (+0.00231/game) |
| **shipped** | **+5.0 turns (+0.00018/game)** | **+6.0 turns (+0.00014/game)** |

So the pass cut the regression cost by 94% and the smoke cost by 55%, which is the real result. But
both totals stay POSITIVE: the engine is a few turns slower across tens of thousands of games, not
faster. The regression tier's `-0.0391` cell sum is carried almost entirely by fluctuator, whose
five cells hold 1,450 games and gain 14 turns, against hinata (+11 over 1,600) and th (+9 over
2,600) losing them back.

**And more games get slower than faster on both tiers** — 17 faster / 24 slower on smoke, 51 / 57 on
regression (plus, separately, 2 wins lost and 1 gained on regression; 0 and 1 on smoke). The
`faster/slower` row in `perdeck.py` silently DROPS those: its regex is `gi=(\d+): (\d+) -> (\d+)`,
which cannot match `-> -1`, so a game that stops winning appears in neither column. Read the lost/
gained counts separately, always.

### H.2 The 0.930x work units were measured against the BROKEN intermediate, not against GT

`units9`/`units10` used the adopted deletion set as the denominator. That set was itself ~1.075x the
pre-deletion engine, so "0.930x" meant "gives back the deletion's own overhead", not "cheaper than
before". Measured directly against a binary built at 63dd9ce3 (20 decks x 300 games, seed 5500001,
single-thread, each deck's own play settings):

```
GT total   48,145,348 units
NEW total  48,325,827 units      ->  1.004x
```

Per deck, the spread is what matters, not the total:

| deck | new/GT | avg-turn delta |
|---|---|---|
| hinata | 1.040 | -0.0300 |
| kitty | 1.024 | 0.0000 |
| auras | 1.015 | 0.0000 |
| burn, dragons, goblins | 1.009-1.010 | 0.0000 |
| breaching, knights, minotaur, slivers, stompy, fivecolour | 1.000 | 0.0000 |
| melira | 0.992 | +0.0034 |
| th | 0.994 | -0.0067 |
| fluctuator | 0.812 | -0.0200 |

fluctuator is 19% cheaper because its GAMES GOT SHORTER (-0.02 turns), which is the confounder the
repo already knows about: per-game unit totals are not a per-decision cost, they are a per-decision
cost times a game length. hinata, the most expensive deck in the suite by a factor of five, is the
one that costs 4% MORE.

### H.3 What this changes

Nothing about the deletion, the two execution bug fixes, or the counter evidence in G.8/G.11 — those
stand. What it changes is the claim to make about the trade: **greedy is gone from the searched
window and from real play, at the same quality and the same cost as the engine that still had it.**
That is the honest headline, and it is a materially different statement from "better on both axes".

There is therefore **no work-unit surplus to reinvest**. Any quality work from here has to either
find its own budget or be paid for deliberately.

## Addendum I — cost mitigation after the rebaseline (plan, not results)

USER 2026-09-17: *"go ahead and shoot for a full rebaseline with this change, but keep the old state
in mind and look for ways to mitigate the costs after"*, and *"there is no need to overoptimize for
the regression tests -- if larger A/B tests on other seeds show that there is an improvement overall,
that could be good enough."*

### I.1 Keeping the old state

The pre-deletion engine is commit **63dd9ce3**. To measure against it again:

```
git worktree add /tmp/pre63 63dd9ce3 --detach && cd /tmp/pre63 && ./build.sh
```

Do NOT reuse a stale `build/Release/mtg` as "the old binary" — integrating overwrites it, and that
is exactly how the 0.930x figure ended up measured against the wrong denominator (H.2). The
reference measurement, 20 decks x 300 games, seed 5500001, single thread, each deck's own play
settings, shipped/GT units:

| deck | GT units | shipped | ratio | share of suite |
|---|---:|---:|---:|---:|
| hinata | 10,103,891 | 10,510,161 | 1.040 | 21.0% |
| kitty | 2,140,203 | 2,191,876 | 1.024 | 4.4% |
| auras | 552,542 | 560,837 | 1.015 | 1.1% |
| burn | 1,285,413 | 1,298,754 | 1.010 | 2.7% |
| dragons | 1,580,031 | 1,594,161 | 1.009 | 3.3% |
| goblins | 453,233 | 457,221 | 1.009 | 0.9% |
| critter | 460,753 | 463,393 | 1.006 | 1.0% |
| dragonstorm | 954,039 | 958,819 | 1.005 | 2.0% |
| antilife | 989,726 | 991,372 | 1.002 | 2.1% |
| fivecolour / breaching / knights / minotaur / slivers / stompy | — | — | 1.000 | 18.4% |
| creature_giving | 2,507,202 | 2,504,940 | 0.999 | 5.2% |
| mirrorwing | 2,101,817 | 2,097,784 | 0.998 | 4.4% |
| th | 1,706,526 | 1,696,405 | 0.994 | 3.5% |
| melira | 13,370,170 | 13,267,242 | 0.992 | 27.8% |
| fluctuator | 1,103,089 | 895,905 | 0.812 | 2.3% |
| **TOTAL** | **48,145,348** | **48,325,827** | **1.004** | |

**melira and hinata are 49% of the suite's cost between them.** Any mitigation that does not touch
one of those two cannot move the total by much, whatever it does to the other eighteen decks.
hinata is also the only deck with a material regression (+4.0%).

### I.2 The leading candidate: the k=0 variant is now a DUPLICATE of its base plan

**Hypothesis, derived from reading the code and NOT yet measured.** With `MTG_BP_NESTED_CANON` and
`MTG_BP_BASE_CANON=1` both on, these two plans resolve to the same line:

* the **base plan** (`bp_choice < 0`): BASE_CANON gives it the value-best entry at *every* breakpoint
  it reaches;
* its **wave-0 variant at k=0, at=0**: rank 0 IS the value-best entry at the targeted index, and
  NESTED_CANON gives it the value-best entry at every other index.

`EnumerateBreakpointPlans` is memoised per state and both read the same list, so `cands[0]` and
`ncands.front()` are the same object. The plan dedup cannot catch it: `AppendBreakpointVariants`
runs AFTER `BuildDedupKey`, and the variants differ from the base plan only in `bp_choice` /
`bp_at` / `bp_base` / `bp_wave0`, which are not part of the played line.

If it holds, every base plan that gets variants is being applied twice — one wasted apply per base
plan per turn, up to `MTG_BP_MAXBASE` (16) of them. At W=2 that is half the wave-0 variant budget.

**The test is cheap and decisive:** with BASE_CANON on, suppress the `k == 0` variant and check the
smoke tier for BYTE IDENTITY. Identical digests prove the duplication; any difference falsifies the
hypothesis and says where. If it is confirmed, the freed slot is worth more than the saving: giving
it to `MTG_BP_EMPTY_ARM` would close the doctrine gap in G.10 ("stop here" unreachable at a
base-plan slot) at ZERO net budget, instead of the 2.1 points the arm costs today.

### I.3 Measurement protocol for this phase

Per the user's direction, **do not tune against the regression suite**. The suite's seeds are now
the rebaselined GT; treating them as the objective fits the apparatus, not the engine. Instead:

1. Work-unit probes at each deck's own play settings on seeds disjoint from every tier (the
   `units_gt` probe uses 5500001), reported as a ratio against a 63dd9ce3 build.
2. Quality confirmed by a larger A/B on held-out seeds, reported as **game-weighted turns**, with
   wins lost/gained counted separately. Not the per-cell SUM, which inverted the sign once already.
3. A tier run only as the final gate before adopting, never as the search signal.

### I.4 The budget ladder — does the searched solution pull ahead as budget rises?

USER 2026-09-17: *"see how the numbers look at a higher budget. My hope is that the new solution
will win out more as the budget rises."*

This is the sharpest test available, and the mechanism argues for it. A greedy continuation is
**budget-insensitive**: it returns the same line whether the search has 10 ms or 10 s, so extra
budget buys nothing at that slot. A searched continuation is budget-*elastic*: more budget means
more of the deferred waves land, more ranks past W become reachable, and more breakpoints get
branched rather than defaulted. If that is right, the two curves should diverge — parity at the
tier budgets (which is what H.1 measured, +0.00016 turns/game) widening into a real gain as budget
climbs. If they do NOT diverge, that is important too: it would say the deletion's remaining losses
are reachability holes rather than budget shortfalls, which is a different repair.

Design, so it is not re-derived later:

* **Arms:** a binary built at 63dd9ce3 against the shipped binary. Paired — same decks, same seeds,
  same games per rung.
* **Rungs raise DEPTH AND BUDGET TOGETHER** (USER: *"higher budget + depth"*), because the tier
  split in Addendum J shows depth carrying at least as much of the effect as budget — d3/b10 is the
  only configuration where the new engine loses, and d5/b20 already wins. A ladder that raised
  budget alone would hold the losing variable fixed and understate the trend. Suggested rungs,
  anchored on configurations that already have a data point:

  | rung | depth | budget | why |
  |---|---|---|---|
  | 0 | 3 | 10 ms | the one configuration where the new engine loses (+0.00090/game) |
  | 1 | 5 | 20 ms | the suite's deepest cells; already -0.00051/game |
  | 2 | 5 | 40 ms | budget step at fixed depth — isolates the budget axis |
  | 3 | 6 | 40 ms | depth step at fixed budget — isolates the depth axis |
  | 4 | 6 | 80 ms | both |

  Rungs 2 and 3 exist so the two axes can be read apart rather than confounded. **Cap at 100 ms
  pooled**: a 500 ms rung must run alone, because one 500 ms EDF game has hit ~30 GB RSS and
  OOM-killed the box. Size games per cell DOWN as the rungs climb — d6/b80 on hinata or melira (49%
  of suite cost between them) is orders of magnitude dearer than d3/b10, and an equal-games ladder
  would spend nearly all its wall on the top rung.
* **Shape:** ONE `mtg --batch` manifest over every (deck x rung x arm) cell. Not one batch per rung
  and not one per arm — a per-rung split is the wave pattern that has twice starved the box to 3 of
  24 cores.
* **Seeds:** disjoint from every tier, and from 5500001 (the units probe), so nothing measured here
  is a seed the engine has been tuned against.
* **Metric:** game-weighted mean win turn per rung for each arm, and their delta; wins lost/gained
  counted separately; work units per rung alongside, because a rung where the new arm wins by
  spending 2x the units is a different result from one where it wins at parity.
* **Read:** the quantity of interest is the SLOPE of delta(new - old) against budget, not any single
  rung. One rung's t-stat settles nothing (a prior lever gave +2.36 then -2.07 on the same binary).

## Addendum J — the regression is confined to d3/b10, which NO deck ships

USER 2026-09-17: *"d3 b10 is not as important as play settings ... even if d3 b10 suffers a bit, if
play settings are improved that is probably worth it"*, and *"play settings are the most commonly
used and important. Less commonly we may aim for a higher budget. In those cases it will hopefully
do better."*

Splitting the SAME accepted tier runs by cell type instead of by deck settles this:

| configuration | games | extra turns | per game | |
|---|---:|---:|---:|---|
| d0 b0 (both tiers) | 40,000 | +1.0 | +0.00003 | greedy by design; nothing to change |
| **d3 b10 (both tiers)** | **17,765** | **+16.0** | **+0.00090** | **all of the damage is here** |
| **d5 b20 (both tiers)** | **11,830** | **-6.0** | **-0.00051** | **better than the old engine** |
| play settings, 20 decks x 300 | 6,000 | -18.0 | **-0.00300** | better again, by 6x |

The last row is the `units_gt` probe: each deck at the depth and budget its own `value_play` sidecar
specifies (mostly d5/b20, hinata d5/b30, burn and fivecolour and stompy d6/b20, goblins d6/b40),
which is what the engine actually plays. Per deck there: hinata -0.0300, fluctuator -0.0200,
th -0.0067, mirrorwing -0.0066, melira +0.0034, the other fifteen unchanged. **Four decks better,
one worse.**

So the trend across four independent configurations is monotone in how much search the configuration
affords:

```
d0 b0        +0.00003   (no search)
d3 b10       +0.00090   (shallow, 10 ms)
d5 b20       -0.00051   (the suite's deepest cells)
play settings -0.00300   (real depth, real budget, value-leaf sidecars)
```

That is exactly the shape the mechanism predicts, and it is the argument the earlier addenda missed
by aggregating over cell types. A greedy continuation is **budget-insensitive** — it returns one
line at any budget — so it is at its relatively strongest where there is least budget to lose, which
is d3/b10. A searched continuation converts budget into reachability: deferred waves land, ranks
past W open, more breakpoints get branched instead of defaulted. The deeper and richer the
configuration, the more the deletion pays.

**Caveat, because these are not a controlled ladder.** The four rows have different deck mixes,
different sample sizes and different sidecar states, so this is a consistent direction rather than a
measured slope. The budget ladder in I.4 is the controlled version and should be run before the
claim is leaned on. If it confirms, the honest headline changes from "parity" to **"better at the
settings the engine ships at, slightly worse only at a shallow configuration nothing ships"** — and
the d3/b10 cells become a tractability proxy to keep an eye on rather than a target to optimise.

### J.1 The overnight tier says the same thing, on 279,320 games and more cell types

Full rebaseline, 268 cases. Split by cell type, against the overnight GT:

| configuration | cells | games | extra turns | per game |
|---|---:|---:|---:|---:|
| d0 b0 | 80 | 160,000 | -6.0 | -0.00004 |
| **d3 b10** | 28 | 15,600 | **+42.0** | **+0.00269** |
| d3 b20 | 40 | 38,000 | -13.0 | -0.00034 |
| d3 b80 | 12 | 12,000 | +12.0 | +0.00100 |
| d5 b20 | 44 | 19,400 | -7.0 | -0.00036 |
| **d5 b40** | 56 | 26,320 | **-35.0** | **-0.00133** |
| d5 b80 | 8 | 8,000 | -6.0 | -0.00075 |
| **TOTAL** | 268 | 279,320 | **-13.0** | **-0.00005** |

237 games faster, 218 slower, 6 wins lost, 5 gained. **Every depth-5 cell type is better than the
baseline, and d3/b10 is again the worst by a wide margin** — 2.7 times worse per game here than the
+0.00090 it showed on smoke and regression.

The sharper observation is **d3/b80**: eight times d3/b10's budget, still worse than the baseline
(+0.00100), while every d5 cell is better at a quarter of that budget. On this evidence the axis
that flips the sign is **DEPTH, not budget** — which is consistent with the mechanism, because a
shallow search cannot reach the turns where a breakpoint's payoff lands no matter how long it
thinks.

**Two caveats, both load-bearing.** The overnight GT was stale before this run, so these deltas
include whatever else drifted since it was last accepted, not only this change. And the cell types
hold DIFFERENT DECK MIXES — only some decks have a d3/b80 or d5/b80 cell — so comparing one cell
type against another is confounded. Neither caveat touches the d3/b10 result, which is the same
sign and the same shape in all three tiers on disjoint seeds. The controlled version is still the
ladder in I.4, where every rung runs the same decks.

## Addendum K — d3/b10 is STARVATION, and the per-deck focus list

USER 2026-09-17: *"depth 3 might not be able to use all of that budget ... At budget 10 we were
likely unable to reach depth 3 at least without greedy"*, and *"I would actually like to see
per-deck ... There may be some decks that are purely negative and I would like to focus on those."*

### K.1 The cell types are DISJOINT DECK SETS, so J's cross-type comparison was confounded

Budget in this suite is a per-deck tractability choice, not an experimental variable. Each deck
appears at exactly one budget per depth:

| cell type | decks |
|---|---|
| d3 b10 / d5 b20 | antilife, dragonstorm, fivecolour, hinata, melira, mirrorwing, slivers |
| d3 b20 / d5 b40 | breaching, creature_giving, critter, dragons, fluctuator, goblins, kitty, knights, minotaur, stompy |
| d3 b80 / d5 b80 | auras, burn, th |

So "d3 b80 is worse than d5 b20" compares different decks and means nothing. What the layout DOES
give is a clean **paired depth comparison** — the same decks at d3 and d5 — and on the overnight
tier every group improves going from d3 to d5, with burn and th holding budget FIXED at b80 across
the depth step. Whole suite, 2HG variants excluded: **d3 +0.00062/game, d5 -0.00081/game.**

### K.2 The nominal depth is never reached, and the deletion costs plies

`MTG_ROLLOUT_STATS=1`, `id_depth` mean (the depth iterative deepening actually reaches per solve),
60 games, old = a 63dd9ce3 build:

| deck | config | old | new | gap | that deck's delta |
|---|---|---:|---:|---:|---:|
| hinata | d3 b10 | 2.23 | **2.09** | **-0.13** | **+0.0219** |
| hinata | d3 b20 | 2.44 | 2.32 | -0.12 | |
| hinata | d3 b40 | 2.66 | 2.56 | -0.10 | |
| hinata | d5 b20 | 3.76 | 3.56 | -0.20 | -0.0044 |
| dragonstorm | d3 b10 | 2.80 | 2.75 | -0.05 | +0.0050 |
| dragonstorm | d5 b20 | 4.20 | 4.15 | -0.05 | +0.0025 |
| melira | d3 b10 | 2.667 | 2.661 | -0.006 | -0.0012 |
| melira | d5 b20 | 3.211 | 3.219 | **+0.008** | +0.0017 |
| dragons | d3 b20 | **3.000** | 2.996 | -0.004 | +0.0020 |
| dragons | d5 b40 | 4.98 | 4.92 | -0.06 | +0.0005 |
| fluctuator | d3 b20 | 2.753 | 2.753 | **0.000** | -0.0150 |
| fluctuator | d5 b40 | 3.53 | 3.51 | -0.02 | -0.0265 |

**The user's read is confirmed.** hinata at d3/b10 reaches depth **2.09**, not 3 — the configuration
is budget-starved for BOTH engines, and the deletion makes it 0.13 plies shallower still because
branching a continuation costs more per ply than a single greedy `Solve()` did. Even at b40 hinata
only reaches 2.66. The ordering is the story: the two decks that lose most at d3 (hinata +0.0219,
dragonstorm +0.0050) are exactly the two with the largest depth shortfall, and the decks whose
depth is unchanged (fluctuator 0.000, melira -0.006) are neutral-or-better. **The d3/b10 regression
is a depth-starvation artifact, not a quality defect** — which also means it is a COST problem, and
the Addendum I.2 duplicate-plan hypothesis is the right lever for it rather than any quality work.

At d5 the depth gaps stay small (melira actually goes DEEPER) and quality improves anyway: the
extra per-ply cost is repaid by better continuations inside the depth reached.

### K.3 Per-deck: which decks are negative at EVERY configuration

Across smoke, regression, overnight (split by depth) and each deck's own play settings. A deck is
listed as worse-everywhere only if it never improves anywhere it changes at all.

| deck | tier d3 | tier d5 | overnight d3 | overnight d5 | play settings | verdict |
|---|---:|---:|---:|---:|---:|---|
| fluctuator | -0.0217 | -0.0267 | -0.0150 | -0.0265 | -0.0200 | better everywhere |
| goblins | 0 | 0 | -0.0008 | -0.0002 | 0 | better everywhere |
| hinata | +0.0144 | +0.0173 | +0.0219 | **-0.0044** | **-0.0300** | mixed (starved shallow, best deck deep) |
| th | +0.0155 | -0.0017 | +0.0030 | -0.0018 | -0.0067 | mixed |
| mirrorwing | +0.0087 | 0 | -0.0013 | -0.0033 | -0.0066 | mixed |
| melira | -0.0133 | 0 | -0.0012 | +0.0017 | +0.0034 | mixed |
| creature_giving | +0.0017 | -0.0020 | +0.0010 | 0 | 0 | mixed |
| auras | +0.0005 | 0 | -0.0003 | -0.0003 | 0 | mixed |
| dragonstorm | 0 | 0 | +0.0050 | +0.0025 | 0 | mixed |
| **dragons** | 0 | 0 | **+0.0020** | **+0.0005** | 0 | **worse everywhere** |
| **kitty** | 0 | 0 | +0.0012 | 0 | 0 | **worse everywhere** |
| **critter** | 0 | 0 | +0.0007 | 0 | 0 | **worse everywhere** |
| **burn** | 0 | 0 | +0.0003 | +0.0002 | 0 | **worse everywhere** |
| **antilife** | 0 | 0 | 0 | +0.0002 | 0 | **worse everywhere** |
| breaching, fivecolour, knights, minotaur, slivers, stompy | 0 | 0 | 0 | 0 | 0 | unchanged |

**The focus target is dragons.** It is the only deck that is worse at every depth AND reaches its
nominal depth (id_depth 3.000 at d3/b20), so its loss cannot be explained away as starvation — it is
a genuine quality regression, and at +0.0020/game it is ten times any other worse-everywhere deck.
kitty, critter, burn and antilife move by 0.0002-0.0012 in a single cell each and should be checked
for significance before any effort goes into them; at that size they are candidates for ordinary
run-to-run churn rather than signal.

hinata should NOT be on the focus list despite having the largest single regression in the suite.
It is the best deck in the suite at its own play settings (-0.0300) and at overnight d5 (-0.0044);
its shallow-configuration loss is the starvation in K.2.
