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

## 3c. The monster cohort, end to end

Shipped defaults (`MTG_LABEL_EDGE_TAIL` on, `MTG_LABEL_GOFF_DOM` on at width 128, `MTG_LABEL_WAVES`
off). Both monsters are from the 2026-09-09 list of games that ran **6+ hours without finishing**;
both of these runs shared the box with 4–5 other jobs, so the clean numbers are lower.

| game | before | after | plans cut by the width | group-wave phases cut | edge tails elided | rows |
|---|---|---|---|---|---|---|
| 900036 gi=36 | 6 h+, never finished | **5 m 55 s** | 139,857 | 42 | 178,828 | 4, none dropped |
| 900021 gi=21 | 6 h+, never finished | **52 m 28 s** | **78,768,660** | 12,325 | 2,737,367 | 3 + 1 dropped |

900021's residual class is the same shape as everywhere else — 27,916 residual edge nodes, **7** of
them wins (0.03%) — but an order of magnitude more of them, and its edge nodes reach 9,418
candidates. The width cut alone removed 78.8 million candidate scorings from that one game.

Attribution on 900036 (paired, same box): restoring either half — `MTG_LABEL_WAVES=1`, or
`MTG_LABEL_GOFF_DOM=0` — puts the game back over **40 minutes and still running** against the
5 m 55 s shipped arm. Neither cut carries this cohort on its own.

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

## 6. The long tail is the DEEP passes — and the bounded label (`MTG_LABEL_HORIZON`, default OFF)

**Where this came from.** The EDF value-leaf phase A of 2026-09-16 (1,465 games, 24 workers,
`MTG_MEM_BUDGET_MB=16384`, commit 6c74fc87) had every game's first row after an hour and was then
cancelled by the user at 7 h 24 min with 20 games still in flight, 15 of them over two hours, the
longest (seed 900097) at 6.98 h. 509 of the 1,465 games ran over 30 s; the slowest FINISHED ones
were 900463 at 338 min, 900552 at 258 min, 900725 at 249 min, 900324 at 233 min. The cuts of
sections 2–3b are all in force; this is the residue they leave.

**It is not play, not contention, not the transposition table.** Seed 900157 plays in 4.6 s; its
value rows take 10 m 17 s solo. Eight copies of that solo run side by side cost 1.05x. Removing
the per-worker TT cap (`MTG_TT_CAP`) changes nothing (9 m 33 s). The cost is the LABELLER, and the
per-pass instrument added here (`[ladder]` line under `MTG_WINLESS_STATS=1`: wall ms and
`ApplyPlanDirect` calls per horizon pass, per K-sample) says exactly which part. 900157, position
t1, its three reshuffled futures:

```
[ladder] t1 cands=5 result=5 | dd0=0ms/4 dd1=0ms/4 dd2=10ms/391 dd3=181ms/11945 dd4=393ms/24145
[ladder] t1 cands=5 result=8 | dd0=0ms/4 dd1=0ms/4 dd2=1ms/72 dd3=30ms/1193 dd4=489ms/19328 dd5=7317ms/410304 dd6=515425ms/17252625 dd7=2ms/54
[ladder] t1 cands=5 result=3 | dd0=0ms/4 dd1=0ms/18 dd2=2ms/69
```

One sample of one position spent 515 s and 17 million applies in pass dd6 — refuting "a win by
turn 7" — and then found its turn-8 win in 2 ms at dd7. Every other sample of the game costs
between 2 ms and 3 s. Seed 900043 is the same shape: 17 of its 19 m 53 s are position t1. **The pass
cost grows 15–70x per horizon turn**, so the exact tail of a slow future is bought at an exponential
price, for a label (8 versus 9) the model can barely use. `perf` on 900043 is flat — plan enumeration
50%, the applies, `BuildSimKey`, the mana backtrack — so there is no hot spot to shave; the node count
is the only lever, and the node count is the ladder's depth.

**The bounded label.** `MTG_LABEL_HORIZON=H` (default 0 = off = the full ladder; active only on the
`earliest_only` value-row path). The ladder climbs EXACTLY to pass H, so a sample whose earliest win
lies within H turns keeps its exact label. A sample still unsettled after pass H is labelled by a
SEARCHED PLAYOUT instead of passes H+1..depth-1: the first `MTG_LABEL_HORIZON_WIDTH` (8)
`MoveOrderPlans`-ordered candidates are each applied and played out by `SimulateToEnd` at
`MTG_LABEL_HORIZON_PLAYOUT_DEPTH` (2) under a `MTG_LABEL_HORIZON_PLAYOUT_MS` (20,000) virtual budget,
and the best playout is the label. An observed searched line is an upper bound on the exact earliest,
so beyond H a label can only move LATER — never earlier and never past `max_turns+1`, which is what a
loss reads anyway. The K-sample mean keeps its shape (a swingy position stays labelled swingy), which
a plain censor at turn+H+1 would not. A playout that exhausts its budget bumps `g_fs_trunc_events`
like any budget abort, so the position is DROPPED rather than labelled with a fabricated no-win.
Stats line: `=== LABEL HORIZON: H= samples-cut= playouts= ... playout-wins= ===`.

**Measured, solo (idle box, `MTG_MEM_BUDGET_MB=8192`).** Identical labels at H=4 on both slow games:

| game | exact ladder | H=4 | H=3 |
|---|---|---|---|
| 900157 gi=157 | 10 m 17 s | **58 s**, labels identical | 22 s, labels identical |
| 900043 gi=43 | 19 m 53 s | **12.6 s**, labels identical | 12.1 s, one row +0.33 (t4 6.67 → 7.00) |

**Measured, the batch (job 0 of the cancelled run: 250 games, seeds 900000–900249, 24 workers,
`MTG_MEM_BUDGET_MB=16384`, `logs/edf_longtail/hz_batch/`).** Wall **16 m 06 s** for all 250 games at
H=4, 1,072 rows, none dropped; 135 samples cut, 812 playouts, 116 of them wins. Joined on
(seed, turn) against the exact run's rows (`logs/vlq_eldrazidisplacerflicker/rows/all.rows`):

| rows joined | same | LATER (+0.33 each) | EARLIER (−0.33 each) | mean shift |
|---|---|---|---|---|
| 1,067 | **1,058** | 7 | 2 | +0.0016 |

plus the 5 rows of seed 900097, the game the exact run never finished (6.98 h in flight at the
cancel; 234 s here). Ninety games still run over 30 s in the batch, the longest 613 s (900236) —
that is the deck, not a tail.

**The two EARLIER rows are the ladder missing wins, not the horizon.** Both reproduce solo and
deterministically. 900045 t1, sample 3: the exact ladder refutes "win by turn 5" at dd4 (12,633
applies) and settles at 6 from dd5; the H=4 playout from the same candidates finds a turn-5 line.
900193 t2, sample 1: dd4 refutes turn 6 (66.6 s, 1.2 M applies), the ladder settles at 7; the playout
finds turn 6. It is NOT either lossy cut of this document: `MTG_LABEL_GOFF_DOM=0` and
`MTG_LABEL_WAVES=1` each reproduce the exact arm's answer with byte-identical apply counts per pass.
So the "exact" ladder's enumeration has a hole the depth-2 per-turn lookahead does not, of the same
class as the pessimism `label-horizon-ladder.md` fixed — and the playout beyond H is therefore not
only cheaper but a second, independent oracle. `[hz-playout]` (with `MTG_WINLESS_WINDUMP=n` under
the stats flag) prints the playout's lines so such a case can be read against the ladder's.
The trace on 900045 t1 sample 3 (`logs/edf_longtail/trace_h4_900045.log`): all eight playout
candidates win at turn 5, every one through the deck's mechanical route -- `Combo Off(x1)`, the
route's DEVELOP variant, at turn 4 (Peregrine Drake and Emiel the Blessed both deployed off eight
mana, the Drake's untap paying for Emiel) and `Combo Off(x999)` at turn 5 after the Conservatory
drop. The ladder's exact pass at cut 5 refutes that line from the same candidates, so its enumeration
does not reach "deploy the two pieces on the turn they become affordable together, go off next
turn". Not the lossy cuts (measured above), and not the stuck-turn closure (lossless by state
identity); the suspects are the enumerator's view of the route's develop action and the
affordability of that deploy at the interior node. Open: bisect with `MTG_LABEL_EDGE_TAIL=0`,
`MTG_LABEL_LADDER_DEDUP=0`, `MTG_LABEL_GOFF=0`, `MTG_WINLESS_DEVELOP=0`, `MTG_WINLESS_CERT=0` on
900045 (a 20 s game), then read the interior node at turn 4.

### 6b. The EARLIER rows are certificate holes: an enters-tapped dig land (fixed; it moves 17 MORE rows earlier than the exact reference), and a {C} bound that omitted the land drop (900193; fixed)

The bisect on 900045 (`logs/edf_longtail/bisect_summary.txt`): `MTG_LABEL_EDGE_TAIL=0`,
`MTG_LABEL_LADDER_DEDUP=0`, `MTG_LABEL_GOFF=0` and `MTG_WINLESS_DEVELOP=0` all reproduce the exact
arm to the apply; **`MTG_WINLESS_CERT=0` labels t1 6.000** -- the ladder finds sample 3's turn-5
win at dd4 in 2 ms and 8 applies -- at 2 m 34 s against 20 s. So the "provably winless this turn"
certificate (section 0's admissible bound) certified a turn the route wins.

The line, in `ProvenWinlessThisTurn`'s dig-land scan:

```
// A {T} ability needs the source untapped, so a land that ENTERS TAPPED is not a draw
// source on the turn it is played.
if (repeatable && !cc.on_board && cc.d->params.enters_tapped) { act = INT_MAX; }
```

On the turn-5 board -- Emiel + Peregrine Drake + Training Grounds on three Aura'd lands, a
Conservatory in hand -- the loop is unbounded (`mana_inf`), and the route's dig is the
Conservatory: it enters tapped, the Drake's ETB untaps it, and its `{4},{T}` Investigate then
draws the deck. The line excluded it regardless, `dig_lands` stayed 0, the `DigInf` decline never
fired, and the certificate went on to prove the turn winless. That is an UNDER-credit in a bound
whose every other quantity is deliberately an over-credit -- exactly the one direction the file's
own header forbids. The fix keeps the exclusion only where nothing can untap the land this turn:

```
if (repeatable && !cc.on_board && cc.d->params.enters_tapped
    && !mana_inf && untap_events == 0) { act = INT_MAX; }
```

(`untap_events` is this round's count of affordable ETB untaps, computed just above the scan; it is
0 under `mana_inf`, where the loop itself untaps.) Over-crediting an enters-tapped dig land is safe:
the certificate may only refuse to certify.

**Measured.** 900045 exact ladder: t1 6.333 -> **6.000** at the same cost (20.9 s vs 20.5 s).
900193 exact ladder: **unchanged**, t2 6.667 -- the fix does not touch it, while `MTG_WINLESS_CERT=0` labels 6.333 (`logs/edf_longtail/cert0_900193.*`), so 900193 is a SECOND certificate branch (below). The two slow games at H=4, labels and cost: 900157 t1..t5 = 5.333 / 5 / 5.667 / 6.667 / 5 and the certificate counters (fired=20115, dig-inf=105790) identical to the unfixed run, 2 m 23 s against 58 s solo-clean; 900043 6.667 / 5.333 / 7 / 6.667 / 5, counters (fired=54030, dig-inf=4233) identical, 32 s against 12.6 s -- both timings perturbed by the 250-game batch running alongside, the labels and counters not.
Job 0 at H=4 with the fix (`logs/edf_longtail/hz_batch/job0_h4fix.*`): 250 games in 17 m 38 s (the unfixed H=4 run: 16 m 06 s).
Joined against the exact reference (`join_report.py`): 1,067 rows matched, **1,041 same / 7 later /
19 earlier**, mean shift -0.0044 (histogram -0.67: 2, -0.33: 17, +0.33: 7). Against the unfixed H=4
rows: **17 rows changed, all 17 earlier, none later**, and the 7 later rows are the same 7. So the
fix moved 17 further samples (13 games) EARLIER THAN THE EXACT REFERENCE, on top of 900045 and
900193 -- 19 of 1,067 samples in 15 of 250 games. Read plainly: the exact reference itself is
over-labelled on at least 1.8% of its samples by this one certificate branch, and every "earlier"
row in section 6's table was a defect of the reference, not of the horizon. Each of the 17 is a win
the search finds once the certificate stops refusing a turn (the ladder's exact passes or, past pass
4, the playout -- this join does not separate the two, and the direction is what matters: a bound
that fires less can only let the search find more). The value-leaf's kept rows (`logs/vlq_eldrazidisplacerflicker/rows/
all.rows`) carry the same hole: a fresh queue on the fixed commit is the reference, not those rows.

**The second branch (900193 t2): the {C} bound omitted the land drop.** With fix #1 it still read
6.667; `MTG_WINLESS_CERT=0` reads 6.333. First the legality question, because the trace's plan text
("Essence Depleter(x8)", "(x11)") looked like more {C} than the board had: the plan text overstates what
is paid. In the traced turns checked, a five-{C}-land board (three Yavimaya Coast + two Aether Hub) with
Drake and Depleter takes the opponent from 10 to 1 and from 15 to 6 -- nine each time, five drains plus
four combat -- so the payer pays exactly one drain per {C} land and the search's line is legal. It was
never an engine rules question. The certificate's drain route bounds the drains by `c_ub / c_pips` with
`c_ub = c_now`, the {C} of the lands untapped NOW; the land drop's {C} (`drop_c`) entered only `c_total`,
the untap arithmetic, never the bound itself. A Yavimaya Coast played this turn is a {C} source at once,
and the sample's turn-6 line drains for exactly `c_now + 1`. Fix #2: `c_ub = c_now + drop_c`,
unconditionally (an enters-tapped drop over-credits, which the bound may do). Measured
(`logs/edf_longtail/fix2_probes_summary.txt`): 900193 exact ladder t2 6.667 -> **6.333**, the
certificate-off label, every other turn identical, in 16 s against 1 m 49 s with fix #1 alone and 7 m 33 s
with the certificate off -- the ladder now finds the turn-6 win instead of exhausting the deeper passes.
900045 unchanged at 6.000 with identical certificate counters. Gates: scenarios 99/99, combo-off 34/34, smoke byte-identical to the 12.14 baseline (80 job lines IDENTICAL, scenario lines 179/179, 75/5 as before).
Committed locally; nothing pushed. Both earlier rows are
closed; the exact reference's residual over-labelling from this branch across job 0 is measured by the
H=4 re-run noted in 12.17.

The play search does not use the certificate and never had this hole: the same turn-4 board as a
fixture (`logs/edf_longtail/fixtures/edf_t4_deploy_drake_emiel.json`, Conservatory in hand) wins
at turn 4 under the shipped play settings.

**What the batch/solo ratio says, and what it does not.** Under H=4 the batch still pays 4–5x per
game against solo (900157: 245 s vs 58 s; 900043: 63 s vs 12.6 s), but the shared line-cache pool's
high-water mark is 303 MB of its 4,153 MB — under H=4 the pool is not the pressure. Twenty-four
workers on twelve physical cores, sharing the host with another container, is. That is a throughput
question, not a tail. In the EXACT run the pool sat at its cap in every heartbeat
(`fsl=4153M/4153M`) and 900157 took 109 min in the batch against 10 min solo, so the per-worker
share of the pool (173 MB at 24 workers) IS a second lever for the exact ladder — a solo re-run at
`MTG_FSL_POOL=177152` (that share, in KB) was at 37 min and still on t1 when it was stopped, 3.6x the
default-pool solo and climbing, though contaminated by a 16-min overlap with the batch above. Not
quantified further: at H=4 it does not arise.

**Cross-deck.** `H=4 bash test/label_horizon_ab.sh <tag> [games] [seed]` runs both arms per deck over
the same games (one pooled batch per arm) and reports rows, wall, and the earlier/later split.
Run at 8 games/deck, K=3, seed 555000, 8 threads (`logs/labelhorizon/hz4/`):

| deck | rows off/on | sec off | sec on | movement (joined on seed, turn) |
|---|---|---|---|---|
| burn | 37 / 37 | 4.2 | 0.5 | 1 LATER (555007 t1 5.33 → 5.67), 36 same |
| Goblins | 24 / 24 | 3.0 | 2.4 | identical |
| Anti-Lifegain | 22 / 22 | 3.2 | 2.4 | identical |
| Dragonstorm | 40 / 40 | 0.9 | 0.7 | identical |
| slivers_vial | 33 / 33 | 0.2 | 0.2 | identical |
| treasure_hunt | 35 / 35 | 0.7 | 0.9 | identical |
| Knights | 34 / 34 | 0.4 | 0.3 | identical |
| Hinata2 | 50 / **51** | 17.2 | 8.8 | 4 LATER (+0.33, +0.33, +0.33, +0.67), 1 EARLIER (555006 t1 6.33 → 6.00), 45 same; one position the exact ladder DROPPED at the budget ceiling (555005 t2) is labelled 8.67 |

Six of the eight decks label identically -- their earliest wins lie within four turns of every
position -- and the two that move are the two whose ladders climb: burn's one losing-side sample
and Hinata's slow futures. Hinata's EARLIER row is the same class as EDF's (a searched line the
ladder's pass did not reach), and its recovered row is the drop-on-doubt doctrine meeting a playout
that answers where the ladder ran out of budget. Eight games per deck is a direction check, not a
cost measurement (see `label-horizon-ladder.md` on the same trap).

**Adoption is the user's call**, and it is a quality-versus-performance trade with both sides now
counted: at H=4 on EDF, 9 of 1,067 rows move by a third of a turn (7 later, 2 earlier, the latter
the ladder's miss) for a phase A that finishes in under half an hour instead of not finishing. The
flag is OFF by default; `valueleaf.sh` adds no knobs by design, so adoption would be an engine
default (global, or per deck through the provider) rather than a driver setting.

## 7. Where this goes next (parked 2026-09-17)

The user's direction, recorded in `label-work-bounding-by-reachable-states.md`: the horizon cut is
not wanted; bound the expensive cases by reasoning over which STATES are reachable rather than
enumerating plans; detect go-off positively; bound failed combos. The certificate work of 6b is the
seed of it.
