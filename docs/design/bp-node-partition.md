# The plain-cantrip breakpoint as a real search node (MTG_BP_NODE)

**Status: BUILT + MEASURED (2026-08-29). The first form of the plain-cantrip class that does not
lose at the shipped 20 ms budget — hold +0.0142 (t +1.21), train 0.0000, where every prior form
lost. Costs 1.35x units / ~2x wall at fixed budget settings (1.62x wall at play settings).
Default OFF; adoption is a USER decision.**

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

**The headroom reading.** The node commits 0.44 plies SHALLOWER — the exact depth loss that made
the s3 form lose — and plays neutral-to-better anyway: per-node decision quality (drawn-card-aware
continuations) fully compensates the depth loss. Any cost reduction that recovers depth should
turn the class outright positive (equal-depth value of the class: +0.05, the crossover doc).
Note the passes also OVERSHOOT the 20 ms allowance (21.7M vs the control's 16.1M — pass-boundary
gating + Overrun only), so the deepening ladder both overspends and commits shallow; a
pass-cost-aware predictor for node passes is untried.

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
