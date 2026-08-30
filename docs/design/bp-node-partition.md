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

**nodem2 kills the old attribution and opens a new question (USER 2026-08-30: "It should be at
least even").** `hinata-all-second-main.md` predicted the arm was blocked on `FSLineTail` having
no breakpoint hosting; the node gives it full hosting and the arm got WORSE (−0.048/−0.068 vs
s3m2's −0.018/−0.031), sitting exactly on the breadth ladder (W=2 −0.018 → node −0.048 → W=4
−0.089 → W=8 −0.123). The USER's dominance argument is GAME-correct on this deck (goldfish, no
pumps/haste, the dork is summoning-sick either way), so the penalty must be an ENGINE asymmetry —
the arm is a DETECTOR for m2-host inferiority, not a refuted idea. Known host asymmetries:
`FSLineTail` has no group-wave phase, no fresh-axis variants, no value-ranked beam reorder.
Condemnation is ELIMINATED as a cause (Hinata never arms `CondemnsPassedMainPhase`). Working
hypothesis: in main 1 the node's breadth is FUNDED by deleting the (prefix x blind-tail)
product; under all-main-2 there is no product left to delete (s3m2 was already the cheapest arm
measured, block −13%), so node breadth in m2 is nearly pure added cost, paid in committed depth.
Pending: the nodem2 units/id_depth probe and per-game root-cause of its worse games.

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

## Cost follow-ups (untried)

1. **Enumeration-side filter**: stop emitting subsets whose canonical order continues past the
   first plain cantrip (31% of pends are duplicate prefixes; each pays a full prefix apply).
   MUST first check whether the enumerator emits the pure-prefix subsets those plans collapse to
   — if enumeration is maximal-subset-biased, dropping instead of truncating deletes the class.
2. **Child-dup prediction**: 49% of children collapse post-apply; predicting the collision before
   the resume apply would halve fs_bp_node.
3. **Pass-cost prediction** for node passes (the overshoot above).
