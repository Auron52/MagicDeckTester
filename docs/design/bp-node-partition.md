# The plain-cantrip breakpoint as a real search node (MTG_BP_NODE)

**Status: BUILT + MEASURED (2026-08-29). The first form of the plain-cantrip class that does not
lose at the shipped 20 ms budget — hold +0.0142 (t +1.21), train 0.0000, where every prior form
lost. Default OFF; adoption is a USER decision.**

**COST RE-MEASURED ON A QUIET BOX (2026-09-01) — the wall figures below were CONTENDED and are
superseded. `1.52x wall`, not 2.10x, and the per-unit overhead is ~ZERO.** Every wall number in
this doc predating 2026-09-01 was taken while a second container shared the host; they scattered
across `~2x` / `1.62x` / `1.2–1.6x` / `2.10x` for the same arm, which is itself the tell. See
"Uncontended cost" below for the replacement numbers and for why the correction *narrows* rather
than reopens the case.

USER direction (2026-08-29): *"Wave phases, though they may be a backup plan for some problems,
are not the primary method in my opinion, I would like to return to partitioning the turn. From
there we can consider whether we need more of the turn plan at any one point. Enumerating ahead
without the drawn or staged cards doesn't make sense either way."*

## The defect this closes

A plan is a whole-turn cast subset. For a plan containing a plain cantrip (Ponder/Preordain), the
engine derived the *entire tail* against a hand that had not seen the draw, then patched the gap
with the `(base x bp_choice)` wave-0 product and a deferred wave phase walking ranks W.. — every
variant re-applying the whole prefix AND a blind tail. Two prior attempts failed at the wrong
layer: apply-time truncation (`MTG_BP_PARTITION_CANTRIP`, +15% work — the enumerator still derived
every tail) and per-candidate condemnation (~1%). See `cantrip-class-affordability.md` for why
seven prune attempts each returned ~1%: the class was starved, not expensive.

## The design

One flag, `MTG_BP_NODE` (heurarm slot; implies the class is open via `BpSiteMask`, so a
`MTG_BP_NODE=1 / MTG_BP_SITE3=0` split cannot exist). With it on:

1. **A plan ends at its first plain cantrip.** The existing defer+truncate shape (`bp_truncate` —
   already measured as the right timing: after the main casts and the Karoo drop) stops the apply.
   Blind tails are never applied.
2. **The apply RETURNS at the partition point** instead of resolving the continuation. The
   `BpPrefixSnap` capture (the wave machinery's own prefix-resume snapshot: state + sink + pins +
   armed flags) gains a `pending` bit. No greedy `Solve`, no `cands[bp_choice]` in the base apply.
3. **The plan loop hosts the continuation as a search node.** On `pending`, the host
   (`FSLineWin`'s pre loop, `FSLineTail`'s m2 loop) resumes each continuation from the snapshot
   via the existing `bp_resume` path — a state copy, no prefix re-derivation — and scores each
   through the normal combat/EOT + tail recursion under the node's B&B cutoff. The child set is
   the FULL `EnumerateBreakpointPlans` list **plus an explicit EMPTY continuation**
   (`kBpEmptyChoice`: cast nothing, play no land), so nothing is a lossy truncation. The list
   LENGTH is measured by child k=0's own apply (`g_bp_cands_last`, the walker's lesson) — a
   host-side enumeration runs outside `CantripOrderScope` and can disagree, stranding tail ranks
   (built that way first; the smoke digest changed when fixed).
4. **The rank machinery disengages for this class.** No wave-0 fan-out for site 3
   (`BpWave0SiteMask`), no wave-walker slots (`BpWaveSiteMask`), no width W. Sites 0/5/6 keep
   their machinery untouched.
5. **The committed line is unchanged.** Children record into the sink exactly as searched
   continuations always did; the winner's actions land in `breakpoint_actions` in the verbatim
   format `replay_recorded` replays. Verified live on hold gi=62 (T5 -> T4): T2 replays `recs=0`
   — the EMPTY continuation committing and replaying lockstep — and T4's continuation casts
   Reality Spasm + Crackle with Power, decided WITH the drawn card visible.

**Dedup, two levels.** Prefixes sharing one partition state collapse (`node_prefix_seen` — this is
where the old apply-time partition's duplicate-tails waste dies); children dedup post-apply
against everything the node scored, ordinary plans recording but never skipping (the
`bp_seen_states` contract).

**Lockstep pair.** A committed child re-applied WITHOUT resume (rollout re-application) truncates
by shape (2): `bp_choice >= 0` under the lever. The executor twin (`equip_bp_truncates`,
AIEngine) mirrors exactly that condition — NOT `fd_plan_committed`, because a committed
tranche/group-wave plan was scored full-tail-greedy and must execute its tail.

## Measured (Hinata2, d5 / 20 ms virtual, paired, 1200 games/cell)

| arm | hold delta | t | train delta | t | vs the s3 rank form |
|---|---|---|---|---|---|
| **node** | **+0.0142 (better)** | +1.21 | **0.0000** | 0.00 | s3 was −0.0233 / −0.0292 (t −2.15 / −2.68) |
| nodem2 (+ all-main-2) | −0.0483 | −3.42 | −0.0675 | −5.25 | s3m2 was −0.0175 / −0.0308 |

Hold: 80 games faster, 63 slower. Play-settings smoke (150 games, value_play policy): node 5.7667
vs control 5.7733 — consistent.

**nodem2 ROOT-CAUSED (2026-08-30). The funding hypothesis is REFUTED and the penalty decomposes
into three mechanisms.** The probe: nodem2 runs 22.06M units and commits id_depth 3.100 — DEEPER
than node's 2.792 — yet plays worse (−0.048 vs +0.014), so the penalty is NOT paid in committed
depth. Per-game root-cause of its worse-vs-ng games (255 across both blocks, dominated by
one-turn slips, 5→6 the largest bucket):

1. **Local depth starvation (the common class — recovers with budget).** hold gi=17/35/78 all
   recover at 400 ms. gi=17 anatomy: at T2 both arms hold the same hand; ng casts the mana dork,
   nodem2 casts two Preordains — its game spent 103.5k units vs ng's 40.5k (2.55x) yet committed
   shallower ({1,1,2,3} vs {1,3,3}), blind to the dork→Hinata→win payoff at horizon. Under
   all-main-2 EVERY plan is in the tail and nearly every plan pends, so the fan-out compounds
   through the fs_main2 recursion exactly at the early decisions that matter.
2. **The deleted mid-turn re-plan point (the structural class — does NOT recover with budget).**
   hold gi=22: node@20ms wins T4 (M1: land + Hinata; M2: Reality Spasm x3 + Ponder + Crackle —
   Hinata's per-target discount priced against the real board because she RESOLVED between the
   two enumerations); nodem2@2000ms still T5 (it plays Irencrag Feat T3 and finds the same chain
   a turn late, once Hinata is already down). The single-phase subset {Hinata, Spasm.., Crackle}
   would need a same-subset discount credit (the rock/haste-dork credit family) to be affordable
   at enumeration; the two-main structure gets the re-pricing FREE at the phase boundary. So the
   second main is not a phase label — it is a mid-turn re-plan point, and all-main-2 deletes it.
   (Same mechanism as bug 6's gi=1938 in `breakpoint-condemnation-status.md`.)
3. **Runaway-pass waste (m2-specific pathology).** id_pass waste is 9.2% of nodem2's units
   (2.04M in just 4 aborted passes) vs 0.25% for node. `kOverrunFloor = 1,000,000` units
   dominates `kOverrunBeta x Limit` (36k) at 20 ms, so one pathological node-heavy pass may burn
   55x the whole decision budget before the abort.

Consequence: the arm is a diagnosis, not a candidate — mechanism 2 says all-main-2 is structurally
WORSE for this deck (the USER's own partition intuition, inverted: MORE re-plan points, not
fewer). The m2-host feature asymmetries (group waves, fresh axis, value reorder) were secondary
suspects and are no longer needed to explain the result.

## The cost anatomy (300 games hold, MTG_ROLLOUT_STATS)

| | ng | node |
|---|---|---|
| units_total | 16.08M | 21.70M (+35%) |
| id_depth committed | 3.233 | **2.792 (−0.44 plies)** |
| fs_pre | 4.82M (30.0%) | 3.82M (17.6%) |
| fs_main2 | 8.96M (55.7%) | 8.65M (39.8%) |
| fs_bp_node | — | 6.90M (31.8%) |

`[bp-node]` anatomy: pends 2.65M, prefix_dupes 826k (31% — the enumeration-filter target),
children 6.90M, child_dupes 3.37M (49% — dedup discovering duplicates at one apply each),
adopted 669k (19% of surviving children beat the incumbent), **empty_adopted 8,520** (the
"cast nothing" arm gets chosen — its existence is not theoretical).

**The headroom reading — REFUTED by the equal-compute measurement (2026-08-30).** The original
inference ("per-node quality fully compensates the depth loss ⇒ cost work turns the class
positive") did not survive a direct test. The passes OVERSHOOT the 20 ms allowance (21.7M units
vs the control's 16.1M, +35% — pass-boundary gating + Overrun only), so the +0.0142/0.0000 at
matched `budget_ms` was partly FUNDED by compute the setting never intended to grant. Measured
at the equal-units point (node at 12 ms ≈ 16.4M units vs control at 20 ms = 16.1M; paired,
1200 games/cell):

| | hold | t | train | t |
|---|---|---|---|---|
| node@12 vs ng@20 (equal units) | **+0.0242 (worse)** | +1.94 | **+0.0383 (worse)** | +3.17 |
| node@12 vs node@20 (compute withdrawn) | +0.0383 | +5.92 | +0.0383 | +5.41 |

Budget ladder on the 300-game hold subset (monotone): node@20 5.7300, @15 5.7367, @13 5.7467,
@12 5.7567; ng@20 5.7233; id_depth falls 2.79 → 2.52 as the overshoot is withdrawn. So **at
genuinely equal compute the class is still a net quality loss (~+0.03)** — the node is the best
FORM of the class measured (the s3 rank form lost −0.023/−0.029 while ALSO paying 1.5x), but
per-node quality compensates only part of the depth cost, not all of it. Consequence for
adoption: at fixed `budget_ms` the node is neutral-to-better because it takes ~1.35x units /
~1.2–1.6x wall; spending that same extra wall on raising the CONTROL's budget would almost
certainly buy more than +0.014. The case for the node is therefore STRUCTURAL (the USER's
doctrine: no enumeration ahead of the drawn card; the wave/rank machinery deleted for the
class), not compute-efficiency.

**Condemnation on the node — SETTLED at full power (nodecond, node + MTG_BP_CLASSIFY, paired
1200/cell): train +0.0000 exactly (t 0.00) vs both ng and node; hold −0.0075 vs ng (t −0.65) /
+0.0067 vs node (t +2.00).** Neutral-to-marginally-worse than plain node, −1.0% units.

**2026-08-30 (later) — the verdict SHARPENED by the range arc (`exemption-free-condemnation-
order.md` has the full story).** The nodecond filter was ALREADY exemption-free (the type
exemptions were deleted 2026-08-28; manasite fired 0 times). Three findings: (1) nodecond's
regressions include **5 deleted lines** that survive 100x budget — engine-tier cards condemned
at cantrip sites while the real win lines interleave them across breakpoints; (2) with the
evidence-driven RANGES (`MTG_HINATA_RANGE`, CastOrderRankLatest) all 5 recover and sound
condemnation fires **exactly 0 times** in 11.4M consultations — inert; (3) the leverage anatomy:
of the children condemnation removed (6.90M→6.34M), **91% were already child_dupes** the node's
dedup was killing (3.37M→2.86M) — non-dupe children fell only 1.4%. So condemnation was never a
work lever on this deck: the node's state dedup is the sound version of the same permutation
argument, and no order narrow enough to bite survives Hinata's re-pricing chains. Not worth
carrying on Hinata in any form; the class's equal-compute gap will not be closed here.

**2026-08-30 (latest) — the pruning ceiling is CLOSED OUT and the adoption bar is set.**
Maximal condemnation (all order/mana gates off — the absolute bound, zero soundness): drop
rate 17.9%, **units −8.1%** (19.95M). P/P separation: MTG_HINATA_PP_STRICT is outcome-identical
to node (0 changed games / 2400 — the node searches both orders); MTG_CANTRIP_ORDER (heurarm
slot added) buys −2.6% units but deletes train gi811's T4 win at 100x budget (earlier-turn
P/Pre sequencing reaches different library states — not duplicates) → fails the bar, stays a
measurement lever. USER 2026-08-30: *"Hinata is a slow enough deck that I don't want to pay
50% extra."* With the ceiling measured (perfect stack ≈ 1.2x best case), **MTG_BP_NODE stays
DEFAULT OFF** — the premium is the added search dimension itself, and the structure-vs-compute
question is answered by the USER's bar unless a structurally cheaper form is invented.

## Uncontended cost (2026-09-01) — the wall number was wrong, and the diagnosis with it

Method: `scripts/wall_probe.sh` — single-threaded, pinned to one CPU, 3 reps rep-major, alone on
an idle box, 200 games Hinata2 d5/20 ms; startup (2.2 s, arm-independent) subtracted. Rep spread
was under 1.5%, versus the 1.2x–2.1x spread the contended figures showed for one arm.

| arm | wall (net) | vs base | units | id_depth | wall per unit |
|---|---|---|---|---|---|
| base (shipped, greedy continuation) | 54.0 s | — | 7.87M | 3.432 | 1.00x |
| recipe (SITE3+DEFER+NGC — LOSSY, see below) | 62.1 s | 1.15x | 8.03M | 2.975 | 1.13x |
| **node (SITE3+DEFER+NGC+BP_NODE — sound)** | **81.9 s** | **1.52x** | **11.69M** | **2.951** | **1.02x** |

**The 2.10x implied ~1.55x of UNCHARGED overhead — enumerator/apply work the virtual budget cannot
see. That overhead does not exist.** The node's wall/units ratio is 1.02: its units are
ordinary-priced, and it is expensive purely because it spends 48% more of them (11.69M against a
20 ms allowance the control meets with 7.87M — pass-boundary gating lets a node pass complete past
its estimate). Two consequences, and they point in opposite directions:

* **Good:** the virtual budget is an HONEST proxy for wall under the node, so the exact dedup
  levers in the cost menu convert ~1:1 into wall. The menu is not chasing the wrong metric.
* **Bad:** it kills the "the node does more real work than it is charged for" defence. There is no
  hidden work to reclaim — the node is simply buying less per unit. It spends 48% MORE units and
  still commits **0.48 plies SHALLOWER** (2.951 vs 3.432).

Also re-measured on the same quiet box, both previously reported from contended runs:
`MTG_EXEC_FEAS` costs **+2.8% wall / +0.003% units / depth unchanged at 3.432** (was reported
"+7% wall" — it is very nearly free), and `MTG_HINATA_SUBSET_CREDIT` costs **+18.7% wall / +15%
units, depth 3.432 → 3.333** (was "+21%" — that one was about right).

### The lossy-recipe control, now measured rather than argued

`MTG_BP_NO_GREEDY_CONT` alone is NOT a greedy-free form: `EnumeratePlans` drops the empty
combination, so `cands[0]` can never be "cast nothing", while greedy `SolveUncached` has a
do-nothing default (`best_mask = 0`) and the node re-adds `kBpEmptyChoice` explicitly. Paired
1200 games/cell against shipped confirms the hole is real and costly: recipe **+0.0200 hold
(t 2.03) / +0.0208 train (t 1.94)** — consistently worse — while node is **+0.0025 hold (t 0.23) /
−0.0067 train (t −0.62)**, i.e. neutral with signs disagreeing. The two arms differ only in
whether "stop here" is in the option set, so that one restored option is worth ~0.027.
**Greedy has never beaten the sound greedy-free form on QUALITY on this deck; the whole case
against the class is compute.**

### The right comparison: cost at EQUAL QUALITY, not at equal budget

Equal-`budget_ms` flatters the node (it takes 48% more units for the same setting) and
equal-units flatters the control. The USER's bar is stated in WALL, so the honest question is:
**how much wall does the control need to reach the node's quality?** Control budget ladder,
paired, 5000 games/cell on the same seed blocks, walls from the uncontended probe:

| arm | wall | hold | train | vs node20 (paired t) |
|---|---|---|---|---|
| base@20 (shipped) | 54.0 s | 5.6394 | 5.6668 | +0.0070 (1.34) / +0.0104 (2.04) — worse |
| **base@26** | **64.1 s** | 5.6286 | 5.6580 | **−0.0038 (−0.72) / +0.0016 (0.31) — TIED** |
| base@34 | 68.4 s | 5.6210 | 5.6490 | −0.0114 (−2.13) / −0.0074 (−1.45) — better |
| base@45 | 77.4 s | 5.6126 | 5.6420 | −0.0198 (−3.69) / −0.0144 (−2.84) — better |
| node@20 | 81.9 s | 5.6324 | 5.6564 | — |

**`base@26` is statistically indistinguishable from `node@20` on both blocks, at 64.1 s against
81.9 s. Removing greedy therefore costs +28% wall at equal quality** — materially less than the
+52% the raw `budget_ms` comparison suggests, because part of the node's extra cost does buy
quality (node beats shipped by −0.0070/−0.0104 at its own budget; the class does NOT lose on
quality). But the control dominates it across the whole range: base@34 is BETTER than node while
still costing 16% less wall.

Two consequences:

1. **The node's remaining problem is exactly the duplicate child applies.** Fresh anatomy at HEAD
   (200 games, `MTG_ROLLOUT_STATS`): children 3,660,751 (= `fs_bp_node` units — one unit per
   child), of which **child_dupes 1,541,982 = 13.2% of the node's 11.69M total units**, split
   dupes_cross 1,192,267 / dupes_innode 349,715 / fp_predictable 252,992. Removing all of them
   lands the node at ~10.15M units ≈ 71 s, i.e. **+11% over base@26 instead of +28%** — the
   difference between "not worth it" and "cheap enough to buy the doctrine with". 77% of the
   prize is the CROSS-node bucket, which the doc previously wrote off as "not predictable without
   applying"; that claim is now the thing to test rather than assume.
2. **Hinata is BUDGET-STARVED, and that is the best available use of extra wall today.** base@45
   buys −0.0268/−0.0248 for 1.43x wall — a better return than the node offers for the same money.
   This corroborates `cantrip-class-affordability.md` (crossover at 80 ms = 4x the shipped
   budget). Raising the shipped budget is a separate USER decision (it rebaselines every tier),
   but any "should we spend 1.4x wall on Hinata" conversation should compare against it.

### The cross-node dupes ROOT-CAUSED (2026-09-01, `MTG_BP_DUPE_TRACE`)

The cost menu wrote the cross bucket off as *"different prefixes converging post-continuation,
inherent to hosting at every base plan, not predictable without applying"*. That was an
assumption, and it is wrong. `MTG_BP_DUPE_TRACE` (print-only, default off, byte-identical when
off — verified: same `units_total`, same every `[bp-node]` counter) keys an origin map beside each
host's dedup set and reports who first claimed each colliding key, deduped into a case list.
40 games, node arm, BOTH hosts instrumented, 100% of dupes attributed:

```
cross_vs_plan=0  cross_vs_child=138912  cross_vs_variant=0  untraced=0  distinct_cases=5744
n=4203  CHILD [Ponder;Ornithopter of Paradise; +noland]  <=  child k=0 of [Ponder; +noland]
n=3467  CHILD [Preordain;Ornithopter of Paradise; +noland] <= child k=0 of [Preordain; +noland]
n=1393  CHILD [Preordain;Ponder; +noland]                <=  child k=0 of [Preordain; +noland]
n=1367  CHILD [Ponder; +Forbidden Orchard]               <=  child k=2 of [Ponder; +noland]
```

Every cross dupe is child-vs-child (never against an ordinary plan or a variant), and the case
list shows ONE defect wearing two hats:

* **Extended base plan.** The origin's base plan is the duplicate's base plan PLUS one card cast
  AFTER the breakpoint. `[Ponder;Ornithopter]` truncates its blind tail at Ponder and its
  continuation re-adds Ornithopter; `[Ponder]`'s continuation k=0 casts Ornithopter. Same land,
  same casts, same final state — one line reached twice. They survive the PREFIX dedup because
  reserving mana for the tail taps different sources, so the two pend states differ transiently
  and only converge once the continuation is applied — which is why this looks like an
  unpredictable transposition post-apply and is completely obvious pre-apply.
* **Land collision.** `[Ponder; +Forbidden Orchard]` vs `[Ponder; +noland]` whose continuation
  plays the land (`bp_play_searched_land`). The same redundancy on the land axis.

**So the cross bucket is not a transposition problem at all — it is the base-plan enumerator
enumerating PAST the breakpoint and the node then covering the same ground again.** That is
exactly what the USER's direction at the top of this doc forbids (*"Enumerating ahead without the
drawn or staged cards doesn't make sense either way"*): the doctrine was implemented for the node
but never enforced on the enumerator feeding it. The fix is the cost menu's own
**truncate-at-emission** entry (truncate AFTER `eval_and_push`'s filters — a naive pre-filter drop
is lossy, already confirmed), now with a measured target rather than a guess: it addresses
cross 1,192,267 of the 1,541,982 dupe children, ~10% of the node's units, taking the equal-quality
premium from +28% toward ~+15% before the in-node fingerprint lever (2.2%) is even counted. It
also removes duplicate PENDS, whose applies are charged to `fs_main2`/`fs_pre`, so the realised
saving should exceed the child-side arithmetic.

### MTG_BP_PREFIX_PREPAY -- BUILT, MEASURED, REJECTED (2026-09-01)

The float half of the cross bucket has a cause: `BatchPrepayMainCasts` taps for the WHOLE plan and
leaves the surplus floating, and it FAST-DECLINES on single-cast turns (`eligible < 2`). So
`[Ponder]` pends with `pool=0` while `[Ponder;Ornithopter]` pends with `pool=2` from identical
pre-breakpoint casts -- different pend key, prefix dedup misses, children reconverge once the float
is spent. Diagnostic `MTG_NO_BATCH_PAY=1` (kills the float globally; changes play, so not a
candidate) more than doubled the dedup catch rate, 16.8% -> 37.1% of pends, and cut cross dupes 42%.

`MTG_BP_PREFIX_PREPAY` (heurarm slot, default OFF) scopes the prepay to the pre-breakpoint casts
whenever `bp_capture` is armed -- search-internal only, the executor passes no capture. It works
mechanically: prefix_dupes 17.7% -> 28.5% of pends, children **-12.4%**, cross dupes **-31%**,
units **-2.6%**, `id_depth` unchanged at 2.98074.

**And it is REJECTED on quality: +0.0200 hold (t 6.20) / +0.0214 train (t 6.41), paired 5000/cell**
-- as large as the entire benefit of deleting greedy, and consistent across both blocks (36/104 and
34/111 better/worse). **The lesson is worth more than the lever: the "wasted" float is NOT waste.**
The mana a base plan taps for its tail is chosen jointly across the whole turn, and that joint
allocation is better for the node's CONTINUATIONS than a prefix-only payment is. So those
duplicate-looking pends differ for a load-bearing reason, and the float share of the cross bucket
is not recoverable by scoping the prepay. Do not re-propose this without a payment model that keeps
the joint allocation while dropping only the truly-dead tail.

Corrected sizing after this: a duplicate child costs ONE unit, so even the -12.4% children this
bought was only -2.6% units. The earlier note that the cross bucket is "not a transposition
problem" was too strong -- it holds for the float and land sub-cases, but the dominant survivor is
a genuine order transposition (`[X;Ornithopter]` casting the dork BEFORE the cantrip vs `[X]` whose
continuation casts it AFTER; Ornithopter is a mana dork so it ranks ahead of the cantrip). That
half needs a commutativity argument, exactly as the cost menu originally said.

## Suite-wide screen (2026-08-30, smoke tier, MTG_BP_NODE=1 over the whole matrix)

The generic-lever collateral check the v1 caveats called for: **14 of 15 decks + all 25
scenario fixtures are byte-identical PASS** — at play settings, site 3 binds only on Hinata in
this suite (no other deck's list contains a plain DrawSpell cantrip that reaches the class).
The mixed-site worry has no suite instance today. Hinata itself:

| cell | control | node | delta |
|---|---|---|---|
| d3 s1001 (150 games, 10 ms) | 5.6733 | 5.6600 | **−0.0133 (better)** |
| d5 s1001 (75 games, 20 ms) | 5.8533 | 5.8400 | **−0.0133 (better)** |

Movers: 7 slower / 10 faster / 75 play-changed. `classify_turn_later.sh`: **all 7 slower games
are churn** — every one recovers at 4x budget (incl. the loud gi11 8→loss, which recovers to 8;
its draws also diverge at T7, a different physical game). No draws-identical persistent
slowdown, i.e. no real regression in the tier.

Adoption would therefore move ONLY Hinata's GT (both searched smoke cells, and presumably the
regression/overnight tiers' Hinata keys — those tiers not yet run under the lever). NOTE for
whoever adopts: the node arm was measured here WITHOUT `MTG_HINATA_ORDER_FULL` /
`MTG_BP_NO_GREEDY_CONT` (suite runs shipped defaults) — the paired 1200-game blocks above had
both ON; the lever helps in both configurations.

## v1 scope (deliberate, in the caveat order they matter)

* **Nested chain links stay inline.** A cantrip cast INSIDE a continuation resolves at the inline
  site-0 path (canonical under `MTG_BP_NO_GREEDY_CONT`), not a nested node — the L*W-not-W^L
  trade, one searched link per line, unchanged. Chains ARE searched at every link's FIRST
  decision (each link's node sees the draw); only the second-order cross product is not.
* **Tranche / group-wave / SolveWithLookahead hosts keep the old shape** (no capture passed →
  full-tail greedy, exactly as before). A committed plan from those paths carries bp_choice < 0,
  so the executor twin correctly does not truncate it.
* **Mixed-site decks**: a plan opening site 6 AND a cantrip — shape (2) truncates any
  bp_choice-carrying plan at its cantrip, so a site-6-targeting variant's cantrip tail truncates
  too. On such decks measure before enabling; Hinata has only site 3.
* When the continuation list is empty (n=0), child k=0 falls through to the greedy Solve
  fallback (nothing to enumerate) and the explicit empty arm is not separately offered.

## The cost menu, SIZED (2026-08-30 probes; the untried list above, measured)

The dupe structure ([bp-node] counters, 300 hold games): node's 3.37M child dupes = 1.52M
in-node + 1.85M cross-node, of which 469k are the explicit EMPTY arm colliding and only 503k
(2.3% of units) are exactly predictable from the pend's own cands content (fp_predictable).
Pass-abort waste is 0.25% for node (the 1.35x overshoot is passes COMPLETING past their
estimate, not aborts). Cross-node dupes are different prefixes converging post-continuation
(same total cast set partitioned two ways) — inherent to hosting at every base plan, not
predictable without applying. The wall gap (2.10x wall at 1.35x units) is NOT the per-child
snapshot copy: hoisted-buffer reuse (committed) bought only ~1.4%.

| lever | saves (node arm) | status |
|---|---|---|
| dominance-filter answer to the old q1: `eval_and_push` runs filters (`SubsetWastesAccelerant`, stranded-equip, unbacked-X) that can reject a PURE PREFIX standing alone while its extension survives | — | so naive drop-at-emission is LOSSY, confirmed; only truncate-at-emission (after the filters) is sound |
| emission truncate-and-dedup (kills the 826k duplicate-prefix host applies) | ~3.8% units | untried; needs an emission-side partition-point predictor + apply-side verification counter |
| in-node exact content dedup (skip fp-duplicate cands in the k loop) | ~2.3% units | untried, and now known HARDER than it looks: BuildSimKey folds zone ORDER by name, so a copy-i-vs-copy-j swap collides only when every hand entry between the two copies shares one name — the equality test needs a position guard over the resume state's hand to stay exact |
| **EMPTY-arm pre-skip** (skip the explicit EMPTY when the k=0 apply reports an apply-empty cands entry — a guaranteed post-apply dupe) | **−0.8% units MEASURED** (190k skips / 300 games; play digest unchanged) | **BUILT + ON under the node** (`MTG_BP_NODE_EMPTYSKIP=0` restores); exact by construction |
| breakpoint condemnation (`MTG_BP_CLASSIFY`, fires under ORDER_FULL: 627k drops / 5.35% of consultations) | **−1.0% units measured**, children −5.3%, depth flat, ~neutral quality on 300 games | measured 2026-08-30; the soundness exemptions (rituals/tutors/reducers — each guards a recorded deleted-win class, and much of Hinata's hand IS those classes) cap the yield. USER note: greedy continuations may still be cheaper than any condemnation — they skip the search entirely; target is the same ballpark, then quality decides |
| node-aware overrun floor (m2's 9.2% waste; `kOverrunFloor`=1M = 55x a 20ms budget) | m2 only (~0.25% for node) | untried; only matters if all-main-2 is ever revived |
| value-ordering the k loop for B&B | small — children/pend ≈ 3.8, too few to reorder | judged not promising |

Honest total for the safe exact levers: ~6–8% units (overlapping), i.e. 1.35x → ~1.25x. There is
no single big lever left inside the node itself; the remaining big buckets are fs_main2 40% +
fs_pre 18% — the host plan loops, whose product the node already collapsed once.
