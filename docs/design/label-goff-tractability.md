# Making the value-leaf phase-A label tractable on EldraziDisplacerFlicker

Scope: `TurnSolver::EnumerateEarliestWins` under `g_unbounded_label_search` — the offline label
ladder that phase A of the value-leaf pipeline runs (`MTG_DUMP_VALUE_ROWS`). **Budgeted play and
ground truth are out of scope by construction**: everything here is gated on
`g_unbounded_label_search > 0`, which no budgeted search ever sets, so the regression suite is
byte-identical without measuring it (`test/scenarios.sh` 73/73, `regression.sh smoke` 73/73 both
re-run anyway).

Companion docs: `label-horizon-ladder.md` (the ladder itself), `unbudgeted-leaf-memo-and-edf-cost.md`
(the same deck's cost under the depth-matrix regime), `analysis-EldraziDisplacerFlicker.md` (the deck).

---

## 0. The problem

The 2026-09-09 phase-A run left a cohort of games that ran **6+ hours without finishing** — at
least seeds 900021/900034/900036/900097/900112/900136/900139/900151/900157/900180 (and 900251+1,
900271, 900294, 900320). The four lossless go-off cuts already in place (`MTG_LABEL_GOFF`,
`MTG_LABEL_LADDER_DEDUP`) had taken the *yardstick* monsters from ~5 h to 11–49 min, but this
heavier cohort was untouched by them.

USER's goal: *"aim to get the phase A time in the few hour range… Technically we could even
dominance prune those cases if we need to"*.

## 1. Where the cost is — measured, and NOT where it was assumed to be

The prior working hypothesis was that a RESIDUAL node (horizon edge, certificate void, seeds
missed) pays an unbudgeted plan **enumeration** and branch-and-bound that "gets deeper and wider on
high-mana boards". Two new instruments say otherwise. Yardstick game: seed 900255 `--game-index 5`.

| component | how measured | share of the game |
|---|---|---|
| plan **enumeration** (`EnumeratePlansWithLand` + `MoveOrderPlans`) | `MTG_ENUM_TIME` | 2.59 s of ~150 s — **1.7%** |
| the EDF **auto go-off** (recognise + size + `ApplyBlinkLoop`) | new whole-call timer, `MTG_WINLESS_STATS` | 0.39 s over 107,832 calls — **0.26%** |
| everything else = **`ApplyPlanDirect`** | new `g_lp_applies` counter | 153,453 calls ≈ **1 ms each** |

So the atom is the plan APPLY, not the enumeration and not the combo loop. Apply-site attribution
(`g_lp_site`, same run) puts 144,531 of the 153,453 applies (94%) under **`FSLineWin`'s plan loop**;
the ladder root's pass-0 loop contributes 228 and the ladder's per-pass loop 346.

Note the ratio: 144,531 applies against 95,702 enumerated label-path plans — **1.5 applies per
candidate**, and on the monster cohort it is far worse. Live sampling of seed 900021 (below) caught
a window of ~1,990 new candidates that cost **24,783 applies — 12.5 per candidate**. The excess is
the per-node wave machinery (`AppendBreakpointVariants`' deferred wave phase, the group-wave
tranche), which walks plan variants that are not in `pre` at all. It is bounded by `scanned`, which
is what makes a candidate-width cap effective against it as well.

### The residual class, quantified

`MTG_WINLESS_STATS` on 900255, exact arm:

```
WINLESS CERT scope: fsw nodes label=2234 edge=1956 | plans label=95702 edge=89650
WINLESS RESIDUAL:   1956 of 3426 edge nodes (57.1%) resolved by neither
WINLESS RESIDUAL by reason: dig-inf=1956          <- 100% of it
WINLESS RESIDUAL OUTCOME: searched=1956 wins=12 (0.6%) no-wins=1944
```

* the residual is **entirely** the `dig-inf` decline — the certificate gives up because its bound
  has concluded this turn's mana is unbounded, so the whole library reads as reachable;
* **87.5% of all label-path nodes that reach the enumeration are residual edge nodes**, and they
  hold 94% of the plans;
* only **0.6% of them actually hold a this-turn kill**. The other 99.4% pay the full plan loop to
  re-derive "no win".

### What wins, when a residual node does win

New `MTG_WINLESS_WINDUMP=<n>` dumps the winning plan and its RANK in the `MoveOrderPlans` order:

```
[res-win] t5 rank=1/1599  land=Brushland  casts=[Cloud of Faeries, Wild Growth x3, Fertile Ground, Living Wish, Trace of Abundance]
[res-win] t4 rank=1/78    land=Brushland  casts=[Emiel the Blessed, Cloud of Faeries, Eladamri's Call, Fertile Ground]
[res-win] t5 rank=7/29    land=-          casts=[Peregrine Drake, Eldrazi Displacer, Fertile Ground]
[res-win] t5 rank=49/83   land=Mariposa Military Base  casts=[Living Wish]
[res-win] t5 rank=70/592  land=Aether Hub casts=[Cloud of Faeries, Training Grounds, Living Wish]
...
```

All 12 ranks: **1, 1, 1, 7, 1, 1, 1, 49, 1, 5, 70, 1**. The kill is always "cast the ramp and the
pieces, then let the apply's tail run the loop", and the static move order puts it FIRST at 8 of 12.
That is the fact the width cut below is built on — and the fact that makes deleting the node wrong.

## 2. Cut 1 (EXACT) — `MTG_LABEL_EDGE_TAIL`, default ON

On a SINGLE-MAIN deck (EDF is one) `FSLineTail` is nothing but "simulate end of turn, then
`FSLineWin` at turn+1", and at `turn >= cutoff` that call's first line refuses `turn > cutoff`
outright. So every plan of every horizon-edge node was paying a full end-of-turn simulation
(untap, cleanup, the next turn's draw) to be told the no-win it was always going to be told. The
B&B-tightened cutoff the call actually passes is `min(cutoff, best.win_turn) <= cutoff`, so the
argument survives it.

Answer-identical AND budget-identical (the elided path consumes no work units before returning).
Measured: **82,131 continuations elided** on 900255, value rows **byte-identical**. It is worth a
couple of percent, not more — the EOT sim is cheap next to the apply.

## 3. Cut 2 (LOSSY) — `MTG_LABEL_GOFF_DOM` / `MTG_LABEL_GOFF_WIDTH`, default ON at width 128

This is the user-blessed dominance prune, in the only shape the measurements support.

**Where it applies.** A residual node — horizon edge, certificate declined, both go-off seed
families applied and neither killed — **and** only when the decline was itself a GO-OFF decline.
That last condition is new plumbing: `EldraziFlickerProvider::ProvenWinlessThisTurn` now records
whether it gave up at one of the three sites that are unreachable with a finite mana bound
(`dig-inf`, `gorge-inf`, `drain-inf`), and `EdfCertLastDeclineWasGoOff()` exposes it. Every other
decline — an unmodelled card, an unmodelled zone, an opponent permanent, a failure to settle, a
bounded board whose damage routes already reach lethal — reads false and keeps today's full search,
so "the analysis does not cover this" can never be mistaken for "this is a combo turn".

**What it does.** The node scores its first `W` `MoveOrderPlans`-ordered candidates and then
concedes the no-win, instead of scanning all of them.

**Why a width and not a deletion.** Width 0 (delete the node outright, which is the literal reading
of "dominance prune those cases") was implemented first and MEASURED BAD on both axes:

| arm | value rows vs exact | wall (same box, same minute) |
|---|---|---|
| exact | — | 163.9 s |
| width 0 (delete) | **all 4 rows a FULL TURN later** (4.67→5.67, 5→6, 5→5.67, 5→6) | 138.9 s (1.18x) |
| width 8 | one row +0.33 (5→5.33) | 131.7 s |
| width 32 | identical | 132.5 s |
| width 128 | identical | 135.5 s |

Deleting the node throws away the 0.6% of residual nodes that hold a real kill, and because the
ladder then cannot settle at that horizon it runs deeper, wider passes: node count went *up*
(2,252 → 2,310 label nodes; certificate checks 3,426 → 27,068). So it bought almost nothing and
cost a whole turn of label accuracy.

**Why 128.** It is chosen off the winner-rank distribution above (max observed 70), not off the
speed curve — which is flat, because the candidates a small width removes are the CHEAP ones
(`MoveOrderPlans` puts the big multi-cast plans, i.e. both the expensive applies and the winners,
first). Where the width really pays is the monster cohort, whose edge nodes reach **3,422
candidates** (measured live on seed 900021); 128 still deletes 96% of such a node, and it deletes
the wave fan-out those candidates would have spawned along with them.

**How it is lossy, precisely.** A residual node whose only kill ranks beyond `W` becomes a no-win at
that horizon, so its label moves LATER. Pessimistic, never optimistic; never a fabricated win (every
win on this path is still an OBSERVED kill, and a this-turn kill is exact by definition). The
conceded no-win is memoised as a bound exactly like the certificate's, and — deliberately — does
**not** bump `g_fs_trunc_events`: that counter is what makes `EarliestWinReport::truncated` fire,
and a truncated report tells the labeller to DROP the position entirely. This cut concedes a
slightly pessimistic answer; it does not refuse to answer.

## 3b. Cut 3 (LOSSY) — `MTG_LABEL_WAVES`, default OFF (waves suppressed), same class

The biggest lever in this session, and it only appears on the games that need it.

The wave phases exist to make a node's answer equal the UNCAPPED enumeration's once the budget
allows: `BpWaveWalker`'s deferred breakpoint-rank walk, and — the one that matters — the
**group-wave tranche re-enumeration**, which re-enumerates one `EnumGroupCap`-dropped candidate
group at a time. Under the label ladder's ~unlimited budget that reads "exhaust the combo turn's
plan space", which is exactly the class the user blessed pruning. So a residual go-off edge node
now skips them, under the same `MTG_LABEL_GOFF_DOM` master switch as the width.

| game | applies (waves on → off) | wall | rows |
|---|---|---|---|
| 900264 gi=14 | 352,623 → **167,126** | 1,369.7 s → **453.5 s (3.0x)** | 5 identical + 1 recovered |
| 900255 gi=5 | 150,223 → 148,907 | no measurable change | identical |

It is entirely the GROUP half — `MTG_GROUP_WAVES=0` alone reproduces the 900264 arm to the digit
(165,735 applies, 340.8 s). And the concentration is extreme: on 900264 only **7 nodes** had a
group-wave phase cut, and those 7 were worth ~185,000 applies. 900255 gains nothing because its
enumerations never hit the group cap, so there are no tranches to defer — there is nothing to tune
here, a node either dropped groups or it did not.

**Two ways it is lossy, both pessimistic.** The node answers from the CAPPED enumeration, so a kill
living only in a dropped group is missed. And it deliberately does **not** bump
`g_fs_trunc_events`, so such a position is EMITTED with that answer rather than dropped — on 900264
that turns the one position the labeller drops today into a row (turn 3, label 5.667), with the
other five byte-identical. Bumping instead would preserve the drop-on-doubt doctrine, but it would
mark EVERY position holding such a node (far more than the one that truncates today) and trade the
whole saving for most of the game's rows. **This is the one judgement call in this work that is
worth a second opinion; `MTG_LABEL_WAVES=1` reverses it.**

## 4. Instruments added (all `MTG_WINLESS_STATS`-gated, zero cost when off)

* `LABEL WORK` — `ApplyPlanDirect` calls, split by call site (root pass-0 / ladder pass /
  `FSLineWin` plan loop / seeds).
* `EDF GO-OFF WORK` — go-off calls, the subset that actually looped, blink iterations, and total
  seconds inside `EdfAutoGoOffAfterCasts`.
* `WINLESS RESIDUAL OUTCOME` — residual nodes searched, and how many were wins.
* `MTG_WINLESS_WINDUMP=<n>` — the first n residual winning plans with their rank in the candidate
  order. This is the shopping list for any future seed extension.
* `MTG_WINLESS_STATS_EVERY=<seconds>` — **dump the whole counter block on a wall-clock interval**,
  plus where the search currently is (turn, cutoff, candidate i of N, largest candidate list seen).
  The counters are otherwise printed at exit, which is no help at all on a game that runs six hours
  and never gets there; this box also refuses `gdb`/`perf` attach, so without this there is no way
  to look inside a monster while it runs.

**Wall clock on this box is not a measurement.** The same binary and the same game measured 163.9 s,
242.5 s and 246.0 s within one hour, purely on neighbouring load. Every A/B above is either
same-minute paired or read off the deterministic counters; a lone wall number should not be trusted
to better than a factor of 1.5.

## 5. What was evaluated and rejected

* **Extending the certificate to survive `dig-inf`** (the task's direction 2). The residual is 100%
  `dig-inf`, and 84% of `dig-inf` declines have NO loop the engine's own recognizer can see
  (`dig-inf/NO-LOOP=3309` of 3955) — so the bound is over-claiming. But the over-claim is in
  `mana_inf`, and tightening `mana_inf` is the UNSOUND direction: a false certificate silently
  deletes a real win, which is the one error this analysis may not make. Not attempted under time
  pressure; it remains the only route to a fully lossless fix and is the right next project.
* **Making the seeds catch the residual wins** (which would make the dominance cut nearly free).
  The winning plans are 4–7 casts deep ("cast the ramp, the pieces and Living Wish"); the
  constructed cast-seed set builds at most outlet+payload. A "maximal development" seed built from
  the greedy `TurnSolver::Solve` plus a land drop would plausibly cover 8 of the 12 measured wins
  at one apply per node. NOT built — it needs the land-drop-into-a-copy dance the enumerator does,
  and it is worth doing properly rather than quickly.
* **Lowering the per-position label budget** (`MTG_VALUE_LABEL_BUDGET_MS`). Rejected as a lever
  because it does not describe what is happening: the default 1e6 virtual ms is 9e8 work units,
  which nothing here reaches. 900264 already drops 1 of its 6 positions, and re-running it at
  100,000 produced **byte-identical rows and identical node counters** — so that drop is a
  `g_fs_trunc_events` bump (a group-wave breadth cap), not a budget overrun. The message the
  labeller prints on a drop blames `MTG_VALUE_LABEL_BUDGET_MS` and is therefore misleading; worth
  fixing separately.
* **Closing the stuck-turn state closure over the CANONICAL key** instead of the order-exact one.
  `WINLESS DEVELOP` collapses only 13.7% of boundaries on 900255 and 7.2% on 900264, and the
  obvious suspect was `BuildDedupKey` folding `FsOrderSig` on top of `BuildSimKey` — so two lines
  reaching the same board in a different zone order never collapse. The engine's own rule one level
  down is that a NO-WIN answer is order-free, and a duplicate here returns exactly a no-win, so the
  canonical key looked both cheaper and consistent. **BUILT AND REFUTED**: paired same-minute arms
  on 900255 moved the collapse from 575 to 579 boundaries (13.7% → 13.8%), with identical apply
  counts (150,223 both) and identical wall (205.0 s vs 205.1 s). The boundary states genuinely
  differ; zone-order variance is not what is keeping the closure small. The lever was deleted rather
  than shipped dark.
* **Cheapening `ApplyPlanDirect` itself** (the mana-payment backtrack is ~35% of a `perf` profile).
  Real, and probably the largest single opportunity on this deck — but it is engine-wide, not
  label-scoped, so it would have to carry a byte-identity proof against play and GT. Out of scope
  here; see `unbudgeted-leaf-memo-and-edf-cost.md`, which reaches the same conclusion from the
  depth-matrix side.
