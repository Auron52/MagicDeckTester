# The plain-cantrip breakpoint as a real search node (MTG_BP_NODE)

**Status: BUILT + MEASURED (2026-08-29). The first form of the plain-cantrip class that does not
lose at the shipped 20 ms budget — hold +0.0142 (t +1.21), train 0.0000, where every prior form
lost. Default OFF; adoption is a USER decision.**

**SUPERSEDED AGAIN 2026-09-02 -- READ "Why every greedy-deletion form has been lossy" (LAST SECTION)
FIRST.** The 136k-game gate found the recipe's harm is `MTG_BP_NO_GREEDY_CONT` ALONE (th +0.0017,
dragonstorm 11.8x WALL at identical answers and LOWER units), and root-caused the whole class:
`BpSearchWidth()` is **2**, so the base plan's greedy Solve is an UNBOUNDED-WIDTH escape hatch and
every deletion form is a REACHABILITY CUT. **Greedy deletion is NOT free and is NOT adopted.**
Widen the continuation search first, delete second. The ROOTTURN section below is still correct
about WHERE to host, but its "greedy deletion is effectively free" framing is wrong -- it priced
the node while the harm was in the flag beside it.

**SUPERSEDED 2026-09-01 -- READ "MTG_BP_NODE_ROOTTURN" BELOW FIRST. The class's cost was never the
node; it was hosting the node on turns that are never played.** 99.4% of its children sit on
lookahead turns (+1..+4) that get re-searched on arrival. Gating on the ROOT TURN gives
**+3.1% units and BETTER quality than the full node** (-0.0162 / -0.0174 vs shipped, t 6.0 / 6.8,
10000/cell) -- so deleting greedy is effectively free, not the "+27% wall at equal quality" the
sections below conclude. Those sections all measure node-at-every-turn; keep them for the method,
not the verdict.

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

### Where a child's time actually goes (MTG_BP_CHILD_TIMING, 2026-09-01)

The virtual budget charges ONE unit per child whatever a child really costs, so units cannot say
whether the node's wall is the state COPY or the APPLY -- and that decides which lever is worth
building (a cheaper copy would cut WALL without touching play; dedup cuts units). Measured, m2
host, 200 games:

```
[bp-child] children=1552028  copy=0.412s (0.27us/child, 5.5%)  apply=7.122s (4.59us/child, 94.5%)
```

**The copy is 5.5%.** The hoisted-buffer reuse already took that cost out, and the earlier
suspicion that per-child snapshot copying was the node's hidden expense is closed: 94.5% is the
apply itself, i.e. real work resolving the continuation. There is no cheap-copy win, so the ONLY
way to cut the node's wall is to run fewer children -- back to dedup, whose ceiling is the 42% of
children that are duplicates.

**What that ceiling is worth, honestly.** Removing ALL duplicate children saves ~42% of child
work, landing the node near 1.38x wall = **~+16% over the equal-quality control** -- right at the
USER's bar, but it requires the whole dupe bucket. It cannot all be had: the float share is
load-bearing (the rejection above) and the dominant remainder is an order transposition needing a
commutativity argument. The exactly-buildable piece is the in-node fingerprint dedup
(fp_predictable = 204k of 1.56M dupes), worth ~2% units, i.e. **~+24%**. So the realistic landing
zone for this class is +24%, not +15%, unless someone invents sound transposition detection.

### The in-node fingerprint dedup is BLOCKED on the prefix-resume cache (2026-09-01)

The cost menu's "in-node exact content dedup (~2.3% units)" was attempted and is parked, not
because the idea is wrong but because the CHANNEL cannot reach the consumer. Recorded so the next
attempt starts from the blocker rather than rediscovering it. Work is in `git stash` (message:
"WIP: MTG_BP_NODE_FPSKIP ...").

The design is sound and cheap: on the k=0 apply, fingerprint every entry of the continuation list
(`BpCandFingerprint`, which folds the plan's whole content) and mark any entry that repeats an
earlier one. Applying content-identical plans to the SAME resume state is deterministic, so a
marked child is a guaranteed post-apply dupe and its apply -- 94.5% of it real work -- is waste.
`MTG_BP_NODE_FPVERIFY` was built alongside to prove exactness empirically (audit, do not skip;
count marked children that land on a FRESH key -- must be zero) rather than trusting the argument.

**It never got to run, because the marks never survive to the host.** `g_bp_cands_fp_dupe` is a
thread_local filled inside the apply, and:

* the k=0 child apply RECURSES, so a nested breakpoint re-assigns the channel for ITS (usually
  tiny) list. Measured: the host snapshotted lists of average length **0.73** against a real list
  of ~2.6, with **zero** marks, while the fill side was demonstrably working (`fill_marks` equals
  `fp_predictable` exactly, 26,030 on 25 games);
* a one-shot ARM (host arms before the apply, first fill consumes it) did not change the snapshot
  by a single element -- byte-identical `snap_len` across two different fill regimes;
* the reason is the **deferred-breakpoint PREFIX-RESUME cache** above: variants re-enter
  `ApplyPlanDirect` in RESUME mode with the prefix skipped, so the enumeration whose marks the host
  needs is not the enumeration that runs.

Next attempt should carry the marks through the resume path itself (alongside the cached snapshot,
which is the thing that actually persists per (base, at)), not through a thread_local written
during an apply that recurses. Sizing is unchanged and modest: fp_predictable is 204k of 1.56M
dupe children, ~2% of units -- it moves the equal-quality premium from +27% to about +24%, so it
is worth doing only as part of a package, never on its own.

### MTG_BP_NODE_D0ONLY -- the class's cost and its payoff are at DIFFERENT DEPTHS (2026-09-01)

The node hosts at every search depth. It should not, and the counters say so plainly
(`bpnode::g_children_at` / `g_adopted_at`, 200 games, node arm):

| depth | pends | children | adopted | share of children | share of adopted | adopt-rate |
|---|---|---|---|---|---|---|
| **d0** | 713,864 | 1,480,102 | 350,622 | 39.7% | **79.1%** | **23.7%** |
| **d1** | 651,488 | 2,108,271 | 81,856 | **56.5%** | 18.5% | **3.9%** |
| d2 | 55,123 | 127,641 | 9,082 | 3.4% | 2.0% | 7.1% |
| d3+ | ~7,800 | ~13,300 | ~1,700 | 0.4% | 0.4% | -- |

**d1 burns 56.5% of the node's children to produce 18.5% of its adoptions -- an adopt-rate six
times worse than the root.** More than half the class's cost buys the evaluation of turns the
search re-decides when it reaches them; only the ROOT decision is ever committed. This engine
already holds exactly that doctrine for condemnation (`g_condemn_root_turn`: *"only ONE decision
per search has that authority... every deeper m1 is budget-starved, and its passes are exactly the
unreliable kind the m2 re-offer exists to rescue"*). It was never applied to the node.

`MTG_BP_NODE_D0ONLY` (heurarm slot, default OFF) hosts only at depth 0. Measured, 200 games:

| | node | node + D0ONLY | shipped |
|---|---|---|---|
| units_total | 11,928,902 | **8,218,402 (-31%)** | 7,916,678 |
| children | 3,729,307 | **973,721 (-74%)** | -- |
| id_depth | 2.981 | **3.367** | 3.448 |

So the whole cost of deleting greedy soundly AT THE COMMITTED DECISION is **+3.8% units**, not
+50.7%, and it hands back 0.39 of the 0.47 plies the full node was losing.

Quality, paired 5000/cell vs shipped: -0.0038 hold (t 1.18) / -0.0054 train (t 1.75) -- better on
both blocks but under t=2, so it was re-run at 3x. **CONFIRMED at 15000/cell: -0.0065 hold
(t -3.52) / -0.0059 train (t -3.31)** -- a real improvement over shipped on both blocks, not noise,
keeping roughly half the full node's gain for 7.5% of its cost. Against the control's
budget ladder that is ~2x more compute-efficient: matching noded0's quality by raising `budget_ms`
instead needs ~+7.8% units (interpolating b26's -0.0102 for +17%), versus noded0's +3.8%. **This is
the first form of the class that is cheaper than simply buying the same quality with budget.**

**What it gives up, stated precisely.** All these arms carry `MTG_BP_NO_GREEDY_CONT`, so when the
node declines to host, the deeper turn resolves its continuation from the CAST ORDER, not from
greedy `Solve` -- **greedy is still deleted everywhere**. What d>=1 loses is the explicit EMPTY
arm, i.e. it runs the "recipe" form that measured +0.020 worse when used at ALL depths. Using it
only for lookahead costs +0.0048/+0.0068 against the full node (t 1.07/1.47). So the trade is:
sound at the decision that is committed, ordered-but-empty-less in the lookahead that is not.
Whether that satisfies "search the entire window losslessly" is a USER doctrine call, not a
measurement -- flagged, not assumed.

A 2-child (order + EMPTY) mini-node at d>=1 was costed and rejected on arithmetic: ~651k pends x 2
children = +1.3M children, landing ~+20% units over shipped -- worse than the +15% bar it would be
trying to reach, for at most half of a 0.005 gap.

### MTG_BP_NODE_ROOTTURN -- gate on the TURN, not the depth (2026-09-01). The answer.

**Correction first, because the D0ONLY section above reasons from a wrong premise.** `FSLineTail`
recurses with `depth - 1`, so `depth` is REMAINING lookahead: `depth == 0` is the HORIZON leaf (plus
the pass-0 root), NOT the committed decision, which sits at `depth == pass_depth`. So
`MTG_BP_NODE_D0ONLY` keeps the node at the far end of the lookahead and DROPS it where the line is
actually chosen -- the opposite of the `g_condemn_root_turn` doctrine that section invoked. Its
measurements stand; its explanation did not.

**What the node's work is really spent on** (`bpnode::g_children_ahead`, bucketed by
`state.turn_number - g_condemn_root_turn`, 200 games):

| turns ahead of the root | children | share |
|---|---|---|
| **+0 (the turn being CHOSEN)** | **22,910** | **0.6%** |
| +1 | 281,501 | 7.5% |
| +2 | 1,304,818 | 35.0% |
| +3 | 1,423,006 | 38.2% |
| +4 | 697,072 | 18.7% |

**99.4% of the node's work is on turns whose plans are never played.** The engine does not "search
the current turn and roll out the rest": it full-searches a multi-turn tree (turn+1 / depth-1) and
hosts a node at EVERY searched turn, rolling out only at the leaf. Turns +1..+4 exist solely to
RANK the root's options and are re-searched from scratch when the game reaches them.

And it is not rollout work: `[bp-where] children: rollout=0 (0.0%) search=1,868,676 (100.0%)`
(`g_rollout_nest`, an RAII guard on `SimulateToEndImpl`). `SimulateToEnd` does call
`SolveWithLookahead` per simulated turn, but those solves never reach a breakpoint node -- so the
cost is the searched tree, not the playout.

`MTG_BP_NODE_ROOTTURN` (heurarm slot, default OFF) hosts only when
`state.turn_number == g_condemn_root_turn` -- the same authority argument condemnation already
makes, now applied where it actually holds.

| | full node | D0ONLY (horizon) | **ROOTTURN** | shipped |
|---|---|---|---|---|
| children | 3,729,307 | 973,721 | **15,797** | -- |
| units | 11.93M (+50.7%) | 8.22M (+3.8%) | **8.16M (+3.1%)** | 7.92M |
| id_depth | 2.981 | 3.367 | **3.366** | 3.448 |

Quality, paired **10000/cell** vs shipped: **-0.0162 hold (t -6.01) / -0.0174 train (t -6.80)**.
Head to head it beats D0ONLY (-0.0097 / -0.0119, t 4.35 / 5.50) and it beats the FULL node
(-0.0086 / -0.0122 at 5000/cell) **while costing +3.1% instead of +50.7%**.

**Why less node is MORE quality:** the class's cost was never buying much per node, and it stole
plies -- full node commits `id_depth` 2.981 against shipped's 3.448. Restricting it to the one turn
that is actually committed keeps the sound, greedy-free option set exactly where the decision is
made, and hands the depth back (3.366). Note also that children fall 62x from D0ONLY to ROOTTURN
while units move only 8.22M -> 8.16M: `budget_ms` is a VIRTUAL allowance, so freed work is simply
reallocated to ordinary plan exploration. The node's problem was never its per-node cost -- it was
that hosting everywhere OVERSHOT the allowance (11.93M against a 20 ms budget the control meets
with 7.92M).

**So deleting greedy is essentially free and is a quality WIN**: +3.1% units for -0.016/-0.017.
This supersedes the "+27% wall at equal quality" verdict, which measured the node hosting at every
turn. Wall still wants a quiet-box confirmation (expect ~+3-4% at the measured 1.02 wall/units).

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

## The adoption gate (2026-09-02)

Rebased onto `dde84b31` (another agent's EldraziDisplacerFlicker deck, `ManaPool::wild_c` for
{C} pips, `MTG_FEED_FILTER_FIRST` adopted, and a 3-tier GT rebaseline). Only `HeuristicArm.h`
conflicted, both sides having appended a slot; `TurnSolver.cpp` auto-merged. Verified the merge
did not perturb the default path: **smoke 48/48 byte-identical to the rebased GT, units 52/52,
`check_gt_logs.py` 320 consistent / 0 stale**. The Hinata figures above predate those engine
changes, so the gate re-measures Hinata alongside everything else rather than trusting them.

**The gate is not about the node.** `MTG_BP_NODE_ROOTTURN` only restricts *where* the node hosts,
and the node itself is Hinata's site 3. What the recipe ships to **every deck** is
`MTG_BP_NO_GREEDY_CONT`: the greedy `Solve` is replaced by `cands[0]` at every breakpoint site,
and that is **lossy on its own** — `EnumeratePlans` drops the empty combination, so `cands[0]` can
never be "cast nothing" (it measured +0.0200/+0.0214 worse on Hinata alone, t 6.2/6.4).
`MTG_BP_NODE` restores `kBpEmptyChoice`, but only where it hosts. So on the nine decks with no
site-3 cantrip class, and on every non-root turn everywhere, sites 0/5/6 run the lossy form with
nothing restoring the empty arm. **That, not the node, is what the cross-deck measurement is
for**, and it is the one way this recipe could regress a deck that the Hinata work would never
have seen.

`MTG_BP_NODE_ROOTTURN`'s gate reads `g_condemn_root_turn`, which `CondemnRootTurnGuard` sets
unconditionally at all three search entries (`SolveWithLookahead`, `FullSearchLine`, and the
hybrid's direct `FSLineWin`) — it is not condemnation-flag-gated, so the restriction is live on
every deck rather than silently inert where condemnation is off.

Gate manifest: `logs/seq_order/adopt_gate.json`, one pooled batch, 44 jobs / 136k games.
Depth and budget are **omitted per job on purpose** so `ResolvePlaySettings` reads each deck's
own `value_play` lock — the `[play]` line records what each resolved to, and both arms of every
pair resolved identically (the paired-ness precondition). Six movers (hinata, burn, th,
dragonstorm, mirrorwing, kitty) at 5000 games x two disjoint seed blocks; the other ten at an
800-game screen. Read with `scripts/paired_wins.py` off `MTG_DUMP_WINS` output — job averages
alone cannot produce the paired t, which is the gate.

## The gate's verdict: NGC is the whole problem, and it is NOT the node (2026-09-02)

The 136k-game gate says the recipe is a **quality win** — hinata -0.0162 hold / -0.0164 train
(t -4.16 / -4.50, the two blocks agreeing to 0.0002), mirrorwing -0.0014 both blocks (7 better /
0 worse on train), eleven of sixteen decks byte-identical. But two decks were not clean, and
BOTH root-cause to `MTG_BP_NO_GREEDY_CONT` ALONE.

**th -- a REACHABILITY loss, not a tuning loss.** The six-arm ladder (base / s3 / ngc / s3ngc /
node / roott, 5000 games x 2 blocks, same seeds as the gate) is unambiguous:

| arm | train | hold | |
|---|---|---|---|
| base | 4.0752 | 4.0978 | |
| s3 (SITE3+DEFER) | 4.0752 | 4.0978 | **byte-identical to base** |
| ngc | 4.0768 | 4.0996 | +0.0016 / +0.0018 |
| s3ngc, node, roott | 4.0768 | 4.0996 | **byte-identical to ngc** |

Opening the class does nothing on th; the node and ROOTTURN add nothing (th has no site-3
cantrip class, so the node never hosts). The regression is entirely NGC replacing the greedy
`Solve` with `cands[0]` where nothing restores `kBpEmptyChoice` -- i.e. **"cast nothing" stops
being reachable**. The 55 worse games are ±1-turn drifts (26x 4->5, 19x 5->6) with a single
win->unwon, which is the signature of a systematically narrowed option set rather than one
deleted line. This lands on the standing NO-LOSSY-TRUNCATION bar (`no-lossy-truncation-user-bar`:
rare-but-possible is reason enough), so it is a defect to fix, NOT a delta to net against hinata.

**dragonstorm -- an UNCHARGED cost blowup the budget cannot see.** Quality is inert
(-0.0004 / +0.0004) but pooled wall was **11.80x** (16.96x on hold), driven by a one-sided tail:
gi=3019 ran **1.52 h** and returned the SAME win turn as base (wt=7). Per-game, both arms, at the
deck's play settings:

| game | base | ngc | units base -> ngc | win turn |
|---|---|---|---|---|
| gi=2686 | 11.0 s | **159 s** | 59456 -> 53350 (0.90x) | 5 -> 5 |
| gi=1726 | 14.1 s | 143 s | 80360 -> 69088 (0.86x) | 7 -> 7 |
| gi=4020 | 15.7 s | 134 s | 70606 -> 83843 (1.19x) | 6 -> 6 |

`s3` is byte-identical to base; `s3ngc`/`node`/`roott` are all the same as `ngc`. **Units go DOWN
while wall goes up 10-14x, at an identical answer.** That is the `budget_ms`-is-virtual trap in
its worst form: the enumerator work NGC provokes charges no units, so the virtual budget cannot
throttle it and the run has no cost signal at all. A 300-game units probe MISSED this entirely
(dragonstorm 0.992x units) because the pathology lives in rare games in the hold block -- an
average-units measurement is the wrong instrument for a tail.

**Method note for whoever reads the numbers above.** A batch game's repro is
`--seed (job.seed + gi)` AND `--game-index gi` -- the shuffle comes from `job.seed + wi.game`
(BatchRunner: `SetupGame(job.deck, job.seed + wi.game)`), while `--game-index` only drives
`PopulateOpponentSpawns`. Passing the base seed with `--game-index gi` silently plays game 0 and
reports ~17 units of a trivial game, which is how the first attempt at this table came back
uniform and meaningless.

**Consequence.** NGC is the flag that actually implements the USER's "delete all greedy"
directive; the node only restores soundness at ONE site on ONE turn. So the recipe's value
(hinata) and the recipe's harm (th, dragonstorm) come from DIFFERENT flags, and they are
separable. `logs/seq_order/nongc.json` measures the recipe MINUS NGC. First result in:
`dragonstorm_nongc.train` digest `4f28b16f844780c8` == `dragonstorm.base.train` -- byte-identical,
so dropping NGC removes the blowup completely.

## Why every greedy-deletion form has been lossy: the base plan's greedy is a WIDTH-2 ESCAPE HATCH

`MTG_BP_BASE_EMPTY` (built 2026-09-02, heurarm slot, default OFF) gave the BASE plan the EMPTY
continuation instead of a greedy Solve -- the shape suggested by the USER's "we should make empty
continuation reachable". The reasoning was that a base plan already means "cast this subset", its
variants enumerate the extensions, and so base + variants would cover the whole space including
empty, with no greedy and no enumeration. **It measured WORSE nearly everywhere and is REJECTED**
(600 games/cell, seed 5500001, play settings):

| deck | base | BASE_EMPTY | +node+roott | wall |
|---|---|---|---|---|
| hinata | 5.7233 | 5.7467 (+0.023) | **5.8567 (+0.133)** | 1.04x / 1.18x |
| th | 4.0567 | 4.0817 (+0.025) | 4.0817 | 1.02x |
| kitty | 4.3167 | 4.3200 (+0.003) | 4.3200 | 0.99x / 0.93x |
| dragonstorm | 4.3650 | 4.3667 (+0.002) | 4.3667 | 1.09x / 1.13x |
| mirrorwing | 4.4267 | 4.4100 (-0.017) | 4.4100 | 0.96x / 0.93x |

The COST half of the argument held (no blowup: 0.93-1.18x, versus NGC's 10-14x -- it runs neither
the Solve nor the enumeration). The QUALITY half did not, and the reason generalises to the whole
arc.

**`BpSearchWidth()` is 2.** Wave 0's variants cover `cands[0]` and `cands[1]` only (deeper ranks
need the deferred wave phase and spare budget). The greedy `Solve` at the base plan is therefore
NOT merely "a greedy step": it is an **unbounded-width oracle** -- free to return any continuation
in the enumeration, including rank 3+ that no variant can reach. Deleting it does not remove
heuristic influence, it removes REACHABILITY:

* **NGC** answers `cands[0]`, which is exactly variant k=0. So the base plan becomes a DUPLICATE
  and the effective option set shrinks {greedy's free pick, cands[0], cands[1]} -> {cands[0],
  cands[1]}. Losing "cast nothing" is the visible half; losing the free pick is the bigger half.
* **BASE_EMPTY** gives {empty, cands[0], cands[1]}. Empty IS now reachable -- the stated goal --
  and it is still worse, because greedy's free pick was worth more than the empty arm.

This is why the class has resisted every substitution: each one trades an unbounded chooser for a
width-2 ranked list. It is the same shape as `cantrip-class-affordability.md`'s finding that the
class is STARVED rather than expensive, now with a mechanism.

**Consequence for the USER directive ("no greedy components in the search").** Greedy cannot be
deleted at these sites until the continuation search can REACH what greedy reaches -- i.e. raise
`MTG_BP_SEARCH` / make the deferred wave exhaustive, verify the width is no longer binding, and
only then remove the Solve. Deleting first is a lossy truncation dressed as a purification, and it
violates the no-lossy-truncation bar in the name of satisfying the no-greedy one. The affordability
of the wider search is the open question, and it is the right next measurement.

## The greedy census: what the search actually relies on (2026-09-02)

`greedysite::` (`MTG_M2_YIELD_STATS`) counts every greedy `TurnSolver::Solve()` reached from
inside the search, by site, with an `acted` counter for whether that call returned a play. Run
across all 16 suite decks at shipped settings, 150 games each:

| deck | in-tree sites (calls / **acted**) | horizon s90 (calls / **acted**) |
|---|---|---|
| hinata | s0 769036/**256298**, s8 1613965/**780184** | 243216/**144550** |
| mirrorwing | s0 1197556/**538482**, s8 2984986/**1493451** | 584580/**528122** |
| creature_giving | s8 298592/**131150** | 1410410/**1312947** |
| burn | s0 352926/**132801** | 7355/**6867** |
| th | s1 563040/**136452**, s4 534/**154** | 70124/**54814** |
| kitty | s8 167522/**97186** | 113757/**89750** |
| fivecolour | s8 (300g: 135137/**61316**) | (300g: 463541) |
| dragonstorm | s2 60634/**49619** | 73113/**38717** |
| antilife | s8 25362/**7787** | 19666/**9953** |
| auras | s4 26995/**12969** | 7341/**6026** |
| dragons | -- | 2386594/**2143015** |
| minotaur | -- | 44602/**44192** |
| stompy | -- | 43002/**36295** |
| slivers | -- | 7977/**7976** |
| knights | -- | 800/**799** |
| goblins | -- | 3709/**2021** |

**EVERY deck relies on greedy. No deck is greedy-free.** The six decks with no in-tree site are
greedy-free only INSIDE the searched region -- all of their greedy is at the horizon, and at
84-99.9% acted.

**TWO POPULATIONS, and they need different answers.**

* **In-tree (s0/s1/s2/s4/s8) -- a real defect.** The search IS exploring here and greedy
  short-circuits it. It concentrates: roughly ONE dominant site per deck. **s8 is the big one** --
  it is the DEFERRED continuation fallback (`bp_searched_plan(deferred_site_index(), ...)`
  returned false), where the deferred site is 3 (plain cantrip), 5 (trick/Gold Rush) or 6
  (equipment-ETB draw). Hinata's 780k acted s8 is site 3 being MASKED OFF by default
  (`BpSiteMask` 0x77 excludes 0x08), so every deferred plain-cantrip continuation falls to greedy.
  That is exactly what `MTG_BP_SITE3` opens and what the node hosts -- the arc's target was right,
  it was only ever bundled with the wrong second flag.
* **Horizon (s90) -- NOT a defect of the same kind.** This is `SolveWithLookahead`'s `depth <= 0`
  base case: depth ran out and something must choose a play. Every finite-depth search has a
  heuristic leaf; removing it means infinite depth. The real levers are pushing the horizon deeper
  (budget) or replacing the greedy playout with a learned evaluator -- the value leaf's territory.

### The instrument was lying about the most important site

Site 90 had `greedysite::Record(90)` but **no `RecordOutcome`**, so it reported `acted 0` on all 16
decks. Read literally that says the horizon leaf decides nothing, and the first pass of this census
concluded six decks were already greedy-free. Wiring the counter reversed it: those decks are
84-99.9% acted at the leaf. This is precisely the confusion `greedysite::act` was introduced to
prevent ("a count alone cannot distinguish 'greedy is still choosing plays' from 'greedy is called
on a state with no legal option'") -- and the one site where the distinction decides whether the
leaf is a RELIANCE was the site missing the call. Fixed 2026-09-02; smoke 48/48 byte-identical
(the flag gates only the counter).

### Plan

1. Generalize node hosting from hard-coded site 3 to the DEFERRED site (3/5/6) and to BASE plans,
   keeping ROOTTURN (node-at-every-turn is already measured worse -- budget starvation). The node
   is the only construct that is complete: "No rank width, no waves -- completeness is the
   enumeration itself", full cands PLUS the explicit empty arm.
2. Re-run this census as the ACCEPTANCE TEST. Success is `acted` reaching zero at s0-s8 -- a
   quality delta is not the criterion, reachability is.
3. Treat s90 explicitly as horizon policy, not a bug, and decide it separately.

## SIZED: the reachability hole greedy is papering over (2026-09-02)

USER, 2026-09-02: *"Greedy within the search window can make lines genuinely unreachable, which is
my largest concern."* That is correct, and it is the right frame -- sharper than "greedy
short-circuits the search", and it is now MEASURED rather than argued.

**The arithmetic.** At a breakpoint the search offers the BASE plan (greedy's pick) plus variants
`k = 0..W-1`, and `BpSearchWidth()` is **2**. So of `n` enumerated continuations exactly THREE are
ever explored -- `cands[0]`, `cands[1]`, and whichever one greedy evaluates to. `cands[2..n-1]`
minus greedy's pick is unreachable at ANY budget, which is the infinite-budget test failing.

`MTG_BP_CANDS_PROBE`, 120 games/deck at shipped settings:

| deck | site | mean n | max n | capped | **unreachable** |
|---|---|---|---|---|---|
| dragonstorm | impulse_exile (Apex of Power) | 19.35 | 436 | 87.7% | **89.9%** |
| th | DrawUntilNonland (Treasure Hunt) | 5.93 | 39 | 46.5% | **75.2%** |
| hinata | stages_cards/EI | 5.53 | **962** | 59.7% | **68.4%** |
| kitty | equipment_etb_draw (Puresteel) | 3.90 | 120 | 50.0% | **57.7%** |
| auras | dig_through_lands | 3.06 | 39 | 33.3% | **56.4%** |
| mirrorwing | trick_payload (Gold Rush) | 3.91 | 64 | 55.5% | **55.1%** |
| burn | stages_cards/EI | 3.08 | 23 | 45.2% | **45.7%** |
| mirrorwing | stages_cards/EI | 1.82 | 27 | 21.1% | 21.1% |
| hinata (site 3 OPENED) | deferred_cantrip | 3.99 | 243 | 52.4% | **57.5%** |

**45-90% of all continuations are unreachable on every affected deck.** Hinata enumerates up to
962 continuations at one breakpoint and explores three of them. This is a far stronger case for
the node than any quality delta: the node's contract is "No rank width, no waves -- completeness is
the enumeration itself", i.e. it drives `unreachable` to ZERO by construction.

### What a fix has to cover (greedysite:: WHY breakdown, same runs)

| deck | site | masked | base | nested | overrun |
|---|---|---|---|---|---|
| hinata | s3 | **100%** | -- | -- | -- |
| antilife | s3 | **100%** | -- | -- | -- |
| creature_giving | s3 | **100%** | -- | -- | -- |
| kitty | s6 | -- | 73.2% | -- | 26.8% |
| auras | s4 | -- | 94.9% | -- | 5.1% |
| dragonstorm | s2 | -- | 86.6% | 7.0% | 6.3% |
| mirrorwing | s5 | -- | 52.4% | 8.7% | 38.9% |
| burn | s0 | -- | 54.6% | 11.5% | 34.0% |
| hinata | s0 | -- | 76.0% | 13.7% | 10.3% |
| th | s1 | -- | 41.0% | 21.4% | 37.6% |
| **mirrorwing** | **s0** | -- | 22.8% | **70.3%** | 6.9% |
| hinata (s3 open) | s3 | -- | 83.1% | 9.9% | 7.0% |

* **masked** -> open the mask. Site 3 is off by default (`BpSiteMask` 0x77 excludes 0x08), and on
  antilife/creature_giving it is 100% of the fallback.
* **base + overrun** -> the node fixes BOTH by construction (it hosts the base plan and enumerates
  the full list). That is **78-100% of the fallback on every deck except mirrorwing s0**.
* **nested** -> a variant sitting at a breakpoint it is not targeting; it must still reach its own
  `bp_at`, so hosting does not help. Typically 7-21%. **mirrorwing s0 is the outlier at 70.3%** and
  is the one place where zero in-tree greedy needs the harder nested-hosting work (the
  L*W-not-W^L trade).

> **THE `base` ROW ABOVE IS WRONG, AND IT IS THE MOST CONSEQUENTIAL ERROR IN THIS DOCUMENT.**
> `kBase` was one bucket for two different things: a base plan in an apply a caller WAS hosting
> (which the node does fix) and a base plan in an apply **nobody was hosting** (which it cannot
> touch). Splitting them -- `greedysite::kNoHost`, 2026-09-02 -- shows the fix-able half is
> essentially **zero** and the whole of that 78-100% is the un-hostable half. See
> "The real constraint is the HOST SET, not the SITE SET" below; read that table, not this one.

### Acceptance test for the fix -- THREE gates, all of which must pass

USER 2026-09-02, correcting an earlier over-statement of this section: *"we do need to consider
quality and performance under budget as well, but it is also crucial that unreachable -> 0.
unreachable -> 0 is the reason I am pushing so hard on this problem, but it doesn't change our
needs on the other end."*

1. **REACHABILITY** -- `unreachable -> 0` at hosted sites, `acted -> 0` at s0-s8. The objective,
   and the reason the work is being done. Directly measurable with the two probes above.
2. **QUALITY** -- still the standing adoption bar (`change-adoption-bar`): improve the overall
   average at PLAY settings on a large sample, or be quality-NEUTRAL with other upside. A
   completeness fix does NOT get a quality exemption.
3. **PERFORMANCE** -- affordable under the SHIPPED budget, gated on **WALL ON TAIL GAMES**, not
   average units (units read dragonstorm at 0.992x while its real cost was 11.8x -- enumeration is
   uncharged, so `budget_ms` cannot throttle it and an average cannot see it).

**The tension between 1 and 2 is real and must not be wished away.** Under a FIXED budget,
enumerating all `n` continuations at the root spends budget that would otherwise buy depth, so a
strictly-more-correct search can PLAY WORSE -- which is exactly what node-at-every-turn measured
(+35% units, worse quality, id_depth 2.981 vs 3.448). When that happens the answer is to make the
enumeration CHEAPER (prefix/child dedup, enum memo, hosting only where it pays) or to allocate the
budget better -- **never to truncate the option set back**, which just reopens the hole this work
exists to close.

The earlier version of this section said "NOT a quality delta". That was meant as "do not judge a
correctness fix SOLELY by win rate", but as written it licensed shipping a complete search that
plays worse or costs 10x. It does not.

## Scoping the node generalization (2026-09-02, next session starts here)

The node is restricted to site 3 in **TWO** places, and both must move:

1. **The host gates** -- `(PlanOpensBreakpoint(state, q) & (1 << 3))` in BOTH hosts (`FSLineTail`'s
   m2 loop and `FSLineWin`'s pre loop). Widen to the set of sites the node should host.
2. **`partition_here`** -- gated on `plain_cantrip`, so the pend/truncate that MAKES hosting
   possible (`bp_truncate` -> `BpPrefixSnap.pending`) only ever arms for site 3. Without this the
   host gate alone does nothing: no plan ever pends, so no node is ever created.

**The remaining sites split along a line the code already measured**, so this is two jobs:

* **STAGE 1 -- the DEFERRED sites (3/5/6).** These already share the defer+truncate machinery
  (`deferred_cantrip_resolve` / `deferred_trick_armed` distinguishes site 5 from 3; site 6 has its
  own arming that already sets `bp_truncate`). Work is: arm `partition_here` for 5/6 as well as 3,
  and open the host gates to them. Coverage from the WHY table: kitty s6 (73.2% base + 26.8%
  overrun = **100%**), mirrorwing s5 (91.3%), hinata s3 (100% masked today; 90.1% base+overrun once
  opened), antilife/creature_giving s3 (100% base once opened). This is the high-yield, low-risk
  half.
* **STAGE 2 -- the INLINE sites (0/1/2/4).** These re-solve IN PLACE rather than deferring, and the
  partition comment records the measurement that makes this risky: on Hinata, 60 games, *deferred
  5.7000, inline without truncation 6.0333, inline WITH truncation 7.0333*. So truncating an inline
  site is worth **+1.33 turns of damage** on its own -- the continuation sees less mana because the
  deferred re-solve runs after the trailing passes (Hinata's reserved Karoo drop among them). A
  node at an inline site therefore needs its own design (make the site deferred first, or host
  WITHOUT truncating), NOT a copy of stage 1. Covers hinata s0, burn s0, th s1, dragonstorm s2,
  auras s4.
* **STAGE 3 -- nested** (`seen_before != plan.bp_at`): mirrorwing s0 at 70.3%, elsewhere 7-21%.
  A variant must still reach its own `bp_at`, so hosting does not help; this is the
  L*W-not-W^L trade and needs the nested-hosting design.

Do stage 1 first and gate it on all three acceptance gates above. It is the only part where the
machinery already exists and the measurement already says the shape is right.

---

## STAGE 1 BUILT AND MEASURED (2026-09-02): `MTG_BP_NODE_D56`

Built exactly as scoped above: `BpNodeSites()` returns the site bitmask the node hosts (site 3
always, plus 5 and 6 under the new default-OFF heurarm slot `BP_NODE_D56`), and every place that
was pinned to `(1 << 3)` now reads it -- `BpWave0SiteMask`, `BpWaveSiteMask`, the two host gates,
the pend gate, and the EMPTY pre-skip channel. One shared predicate `node_owns_site(site)` drives
both halves of the partition contract (the base plan's `bp_truncate` and the child's replay), so
they cannot drift. The executor got its twin clause in `equip_bp_truncates`, without which a
committed site-5/6 line would execute a tail its scored continuation already owned.

Two things came out of building it that the scoping note did not anticipate:

* **The PUT-armed site-6 case is excluded, on both sides.** `AIEngine`'s `PutFromHandAbility`
  branch does not arm a deferred re-solve at all, so there is no `equip_bp_truncates` hook to
  mirror a truncation with. Truncating in the rollout alone would make the node's child replay drop
  a tail the executor still casts -- the fd-diverge class this partition exists to prevent. Not
  truncating is self-consistent (the pend state simply includes the tail, and child and executor
  agree), so that is what it does. Giving the put an executor twin is its own change.
* **Site 6's deferred arm never truncated.** The scoping note said "site 6 has its own arming that
  already sets `bp_truncate`" -- that is the INLINE arm. The deferred one, which is the one the
  node pends at, did not.

### Flag-off inertness

`test/regression.sh --smoke`: 48/48 byte-identical, 0 configs changed, on both the D56 build and
the later `kNoHost` instrument build.

### The real constraint is the HOST SET, not the SITE SET

Reachability census, `scripts/stage1_census.sh` (60 games/cell, one process per cell, each deck at
its own `value_play` settings), five arms: `base` (shipped), `node`, `d56`, `node0`
(`+D0ONLY`, the standing adoption candidate), `d560`. `acted` = greedy `Solve` calls at in-tree
sites 0-8 that DECIDED something; site 90 (the horizon leaf) is out of scope by USER direction.

| deck | base | node | d56 | node0 | d560 |
|---|---|---|---|---|---|
| mirrorwing | 1,309,115 | 1,309,115 | 1,167,428 | 1,309,115 | 1,298,402 |
| kitty | 65,575 | 65,575 | **68,066** | 65,575 | 43,807 |
| hinata | 342,049 | 306,739 | 306,739 | 318,264 | 318,264 |
| th / auras / burn / dragonstorm / antilife / creature_giving | *identical across all five arms* ||||||

The lever is **correctly scoped**: only mirrorwing (site 5) and kitty (site 6) move, hinata's
`d56` is byte-equal to `node` and `d560` to `node0`, and the other six decks are identical in every
arm. It is also **not the fix**: mirrorwing -10.8% (d56 vs node) but only -0.8% (d560 vs node0),
and kitty is **+3.8% WORSE** under d56, -33% under d560. Nothing approaches `unreachable -> 0`.

**Why -- and this is the finding.** `kBase` was measuring two different things at once. Splitting
it (`greedysite::kNoHost`: a base plan in an apply where `bp_capture == nullptr`, i.e. one no
caller is hosting) re-reads the entire inventory:

| deck | arm | site | nohost | nested | overrun | base (hosted) |
|---|---|---|---|---|---|---|
| mirrorwing | node | s5 | **52.3%** | 7.8% | 39.8% | 0% |
| mirrorwing | d56 | s5 | **85.3%** | -- | 14.7% | 0% |
| mirrorwing | d560 | s5 | **100.0%** | -- | -- | 0% |
| mirrorwing | d560 | s0 | **100.0%** | -- | -- | 0% |
| kitty | node | s6 | **79.7%** | -- | 20.3% | 0% |
| kitty | d560 | s6 | **91.2%** | -- | 8.8% | 0% |
| hinata | node | s3 | **53.5%** | 3.6% | 43.0% | 0% |
| hinata | node0 | s3 | **87.5%** | 2.2% | 10.3% | 0% |
| hinata | node | s0 | 9.5% | 81.4% | 5.4% | 3.7% |

`base` -- a base plan the node could have pended but did not -- is **~0% everywhere**. That is not
a surprise once stated: with the node on and the site hosted, the pend `return`s *before* the
greedy fallback, so a base plan reaching the fallback proves no capture was offered. The
instrument now measures that rather than leaving it to be inferred.

So the 78-100% of the fallback this document credited to "base + overrun, which the node fixes by
construction" is really **`nohost`, which the node cannot touch at any site set**. The node
receives a `bp_capture` from exactly two callers -- `FSLineWin`'s `pre` loop and `FSLineTail`'s m2
loop -- and `nullptr` from everything else: every rollout apply, every enumeration probe, and
(under `D0ONLY` / `ROOTTURN`) every depth or turn those gates exclude. Note the direction of that
last one: **the gates that made the node affordable are themselves a large part of what keeps
greedy reachable**, which is why hinata's s3 `nohost` share RISES from 53.5% (node) to 87.5%
(node0). Affordability and completeness are pulling against each other through the same knob.

**Consequence for the plan.** Widening the site set was the wrong axis, and stages 2 and 3 as
written are the wrong next steps too -- they also only add sites. `unreachable -> 0` needs the set
of *callers that can host* to grow, which is a different and larger piece of work: it means a
rollout apply (or an enumeration probe) being able to pend and resume, not just the two search
plan loops. Whether that is even affordable is the open question, and it is the same 1-vs-2 tension
already recorded above -- a complete search under a fixed budget can play worse, and the answer is
a cheaper enumeration, never truncating the option set back.

`MTG_BP_NODE_D56` itself stays default OFF pending its quality/cost numbers; it is a real but
partial reduction (it removes `overrun` where it hosts: mirrorwing s5 39.8% -> 0, kitty s6
20.3% -> 8.8%), not a fix.

### An asymmetry Stage 1 inherits, and a lever it suggests

`BpWave0SiteMask` / `BpWaveSiteMask` drop the node's sites unconditionally, but the node only
*hosts* where `node_host_here` is true -- which under `MTG_BP_NODE_D0ONLY` is depth 0 only, and
under `MTG_BP_NODE_ROOTTURN` the root turn only. So at every other depth a node site has **no
wave-0 variants, no deferred waves, and no node**: it is purely greedy. That is a reachability
*loss* at depth > 0 bought for a node at depth 0, and it is exactly why hinata's site-3 `nohost`
share jumps 53.5% -> 87.5% when D0ONLY is added.

Site 3 has had this shape since the node was built, so Stage 1 replicates it faithfully for sites
5 and 6 rather than silently diverging -- but for 5/6 it is a *new* loss, because those classes did
have wave-0 and wave coverage before. It is a plausible part of why `d560` moves mirrorwing's
`acted` only -0.8% while `d56` (no depth gate) moves it -10.8%.

**The lever this suggests, NOT yet built or measured:** have the wave masks stand down only for
sites the node hosts *unconditionally* -- i.e. keep the rank machinery whenever D0ONLY/ROOTTURN is
narrowing hosting. Cost is duplicate coverage at the hosted depth (waste, not error); benefit is
that the un-hosted depths keep the reachability they have today. Note this would also change the
**site-3** behaviour of `node0`, which is the standing adoption candidate, so it must be measured
as its own arm against the previously recorded D0ONLY numbers rather than folded into Stage 1.

### THE QUALITY GATE: STAGE 1 IS REJECTED (2026-09-02, 102,400 games)

One pooled batch, four arms x two disjoint seed blocks (5500001 / 6600001, 1.1M apart) x 5,000
games on each mover, at each deck's own resolved play settings (`[play]` reports both movers at
depth=5 budget=20ms source=default). Paired per game via `MTG_DUMP_WINS` + `scripts/paired_wins.py`;
the engine is deterministic, so an unchanged game contributes an exact zero rather than noise.

| deck | arm | block | mean | d vs base | t | better | worse |
|---|---|---|---|---|---|---|---|
| mirrorwing | node0 | hold / train | 4.4096 / 4.4040 | **+0.0000 / +0.0000** | 0.00 | 0 | 0 |
| mirrorwing | d56 | hold / train | 4.4744 / 4.4712 | **+0.0648 / +0.0672** | 12.47 / 13.16 | 91 / 96 | 418 / 437 |
| mirrorwing | d560 | hold / train | 4.4642 / 4.4694 | **+0.0546 / +0.0654** | 10.86 / 13.72 | 68 / 59 | 328 / 362 |
| kitty | node0 | hold / train | 4.3906 / 4.3408 | **+0.0000 / +0.0000** | 0.00 | 0 | 0 |
| kitty | d56 | hold / train | 4.4152 / 4.3662 | **+0.0246 / +0.0254** | 10.87 / 10.74 | 3 / 8 | 125 / 135 |
| kitty | d560 | hold / train | 4.3904 / 4.3414 | -0.0002 / +0.0006 | -0.33 / 1.13 | 5 / 2 | 4 / 5 |

**Every arm that actually bites is worse, on both decks, on both blocks, at t 10.7-13.7.** The one
arm that is not worse (kitty `d560`) is inert -- nine games changed out of 10,000. `MTG_BP_NODE_D56`
does not clear gate 2 (quality) and is not adopted; it stays default OFF.

Two facts worth carrying forward from the same run:

* **`node0` is EXACTLY inert on both movers** -- zero games changed, both blocks. So the standing
  D0ONLY adoption candidate's recorded benefit comes from elsewhere in the suite (hinata and
  friends), and these two decks contribute nothing to it either way.
* **All 14 non-mover decks are digest-identical** between `node0` and `d560` (800 games each). The
  lever's scope is exactly what it claims, proven at the play level rather than by counters.

### Was it the stand-down or the partition? (`MTG_BP_NODE_KEEPWAVE`)

The rejection has two candidate causes and they need opposite follow-ups:

1. **Lost rank coverage.** D56 drops sites 5/6 from wave 0 AND the deferred waves, while the node
   hosts at only 2 of 42 apply sites (and, under D0ONLY, only at depth 0). Those sites may simply
   have less coverage than before -- a reachability LOSS, which would also explain why the census
   shows greedy RELOCATING (mirrorwing s0 787,537 -> 1,321,810 under d56) rather than disappearing.
2. **The partition itself.** Truncating a base plan at a site-5/6 cast is a play change on its own,
   exactly as the recorded inline measurement warns (deferred 5.7000 / inline+truncate 7.0333).

`MTG_BP_NODE_KEEPWAVE` separates them: it keeps 5/6 in the wave masks so the node ADDS to their
coverage instead of replacing it, leaving site 3 untouched so `node0`'s own recorded numbers still
stand. If it recovers the loss, cause 1; if not, cause 2. Same 4-arm x 2-block x 5,000-game shape.
