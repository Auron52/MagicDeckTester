# The replicate count is a searched decision (`MTG_REPLICATE_SIG`, `MTG_REPLICATE_PENDING`)

Status: IMPLEMENTED, **both default ON** (heurarm slots `REPLICATE_SIG`, `REPLICATE_PENDING`, appended);
`=0` on both is byte-identical to the parent commit 782347a6 (measured below). 2026-10-14. Found by the
slivers_vial cast-order draft (`docs/design/cast-order-drafts-2026-10-14/slivers_vial.md`, finding 6.2, on
`work/dig-defer-and-provider-holds`) and verified here, with a second defect the draft did not see.

## The defect: two ways the count was picked, not searched

The USER's no-greedy rule (2026-09-30) turned the Hatchery Sliver replicate count into a fan in
`CollectActions`: one cast variant per pinned count k = 0..kmax, priced into the cast's own bill, plus the
unpinned "spare mana" cast (`replicate_count == -1`, made at resolution by the greedy max). It still was not
searched, in two ways.

1. **The autonomous dedup collapsed the fan (the draft missed this one).** `EnumeratePlans`' powerset dedup
   keys a hand cast by NAME plus a list of param-gated sub-keys (`#D` devour, `#S` modal, `#X` ...). The
   replicate count was keyed only in the HUMAN-play branch. So in autonomous play every class of plans
   differing only in counts collapsed to its highest-VALUED member, and the value is the ordering hint
   `eval += k * EvalCard`. Even with Hatchery **already on the battlefield**, the hint, not the search, picked
   the count. This is the devour defect again (the devour comment in the same signature describes it).
2. **A Sliver cast after a same-plan Hatchery had no fan at all (the draft's finding).** `CanReplicate` reads
   the board at the node. A Sliver whose granter is cast (or Vial-put) EARLIER IN THE SAME PLAN was offered
   only the unpinned cast. Resolution then made the greedy max, and whichever post-Hatchery Sliver resolved
   first took every spare copy: resolution order, not the search, chose which Sliver got the copies.

Both are greedy picks inside the search window. Neither is a permitted site (the horizon playout and the
d0 runner never see the fan: `g_greedy_solve_nest > 0`).

## The probes (hand-built boards; `--scenario`, max_turns = the probe turn, so "not lethal" = unwon)

Inputs: `logs/slivers-replicate/probes/*.json` in this worktree (gitignored) and the draft's
`build/scratch/castorder/probe_slivers_vial/*.json`; runner `logs/slivers-replicate/run_probes.py <arm> <bin>`
(MTG_REPLICATE_TRACE=1). Outputs on the build volume, `build/scratch/slivers-replicate/probes/<arm>/`.
"cell" = the probe's own d3 b20; "d8b0" = `--depth 8 --budget-ms 0`.

| probe | board | parent, cell / d8b0 | fixed, cell / d8b0 |
|---|---|---|---|
| `onboard_split` | T6, 6 mana, **Hatchery on the battlefield**; hand Cloudshredder, Muscle; opp 12 | not lethal (opp 3) / not lethal (opp 10). Trace: `Cloudshredder pinned=-1 copies=1`, `Muscle pinned=0`: the spare copy went to the first Sliver | **T6** / **T6**: `Cloudshredder pinned=0`, `Muscle pinned=1 copies=1` -> 4/4 + 3/3 + 3/3 + 3/3 = 13 |
| `pending_split` | T6, 8 mana, empty board; hand Hatchery, Cloudshredder, Muscle; opp 13 | not lethal (opp 4) / not lethal (opp 13). `Hatchery pinned=0`, `Cloudshredder pinned=-1 copies=1`, `Muscle pinned=-1 copies=0` | **T6** / **T6**: `Cloudshredder pinned=0`, then Muscle takes the copy |
| `copy_gal_first` (draft) | T5, 5 mana, Vial on 2; hand Hatchery, Galerider, Muscle; opp 20 | T6, opp -2 / same | T6, opp -4 / same |
| `copy_mus_first` (draft) | same, Muscle before Galerider | T6, -5 / same | T6, -4 / same (same win turn; life is not the metric) |
| `cast_hatchery_first`, `vial_hatchery_hfirst` (draft) | Hatchery before Muscle in hand | T6 / T6; T4 / T4 | unchanged (now `Muscle pinned=1` instead of -1 for the Vial line) |
| `cast_muscle_first`, `vial_hatchery_mfirst` (draft) | Muscle before Hatchery in hand | not lethal / not lethal (both) | **unchanged, not lethal at d8 b0: the cast ORDER** (below) |
| `hivepool_after_cast`, `hivepool_after_put` (draft) | the affinity line | T3 / T3 | T3 / T3 |
| `haste_hfirst`, `haste_mfirst`, `onboard_haste` | the count is not the constraint | T6, T6, T5 | unchanged |

The draft's own example (`cast_hatchery_first`, `[rep-trace] path=aiengine turn=6 Muscle Sliver pinned=-1
copies=1`) shows the greedy count but not a loss: there the greedy max is right. `onboard_split` and
`pending_split` are the boards where it is wrong, and both lethal lines are unfindable at unlimited budget on
the parent.

## The fix (`src/ai/TurnSolver.cpp`, `src/core/SpellEffects.h`)

1. **`MTG_REPLICATE_SIG`:** the autonomous `plan_signature` keys a pinned count as `#R<k>`, gated on
   `replicate_count >= 0`. No deck but slivers pins a count, so every other signature is byte-identical. The
   human-play signature (which already keyed the count) is untouched.
2. **`MTG_REPLICATE_PENDING`:** the existing fan, extended. In the creature branch, a cast the board does not
   grant replicate to, but that a granter IN HAND would (`GrantsReplicateTo`, factored out of `CanReplicate`
   with identical semantics; a second copy is needed if the cast is itself the granter), gets the same pinned
   fan with the same kmax bound and the same `DecisionProvider::ReplicateCounts` prune hook. The unpinned cast
   is emitted FIRST and unchanged, so a subset without the granter enumerates exactly what it did before. No
   new route: the same block, one more condition.
   * **Legality filter** `SubsetHasUngrantedReplicatePin` (both subset walkers, Solve::consider and
     eval_and_push; per-candidate table `SubsetFilterPre::rep_pending`, empty for every other deck): a subset
     that pins a pending count with **no granter cast or Vial-put in it** is rejected. Such a pin can never be
     granted, so it realises exactly the unpinned cast, which stays in the same group: an identity fold.
   * **Order is not predicted.** Whether the granter resolves before the pinned cast is the apply's realised
     order (the USER-owned cast order, the range ladder, the payable-order fallback). A pin the granter does not
     precede realises no copy (both worlds: `CanReplicate` at resolution), lands on the unpinned cast's state,
     and the post-apply dedup skips it. Predicting the order in the filter would be the lockstep hazard.
   * **Projection.** A pending pin does NOT add its copies to `haste_attack_power`: the copies exist only if
     the granter resolves first, and `SolveWithLookahead` returns a projected-lethal plan without a rollout, so
     the projection must under-count (the line is still searched) rather than over-count (a phantom win).
   * Search window only: never the greedy picker (`g_greedy_solve_nest`), never human play (whose resolution
     dialog already asks for this count).

Stale comments corrected: the fan comment's "an unpinned (-1) cast now reaches the resolution loop only beyond
the search horizon" (false, as the draft said) and `Action::replicate_count`'s "Fanned only under
HumanPlayActive()".

Unit test: `test/unit/test_slivers_replicate_searched.cpp` (4 cases). The two main cases REQUIRE both splits
of the spare copy as distinct plans (on-board and pending) and apply them to check the copies land on the
right Sliver; each has a control arm with its lever off that checks the parent's enumeration (the two splits
never co-exist; no pin after a same-plan Hatchery), i.e. the main assertions fail on the parent's behaviour.
A no-granter case checks both levers are inert. Full suite: 1037 cases pass.

## Measurement

One pooled batch on the fixed binary, the arms as per-job levers: `off` = both levers 0, `sig` = only
`MTG_REPLICATE_SIG`, `on` = default. slivers_vial, held-out seeds 10600001..10606000 (6000 games per cell,
paired), loss scored 9 (max_turns 8). Cells: shipped (`value_play` d5, b20, profile attached), `--depth 3
--budget-ms 10`, d0. Manifests and `.wins` under `build/scratch/slivers-replicate/` (`ab.json`, `ab/wins/`);
comparison `logs/slivers-replicate/compare.py ab`.

| cell | off avg | on avg | faster | slower | net (turns) | play digests moved | work units off -> on |
|---|---|---|---|---|---|---|---|
| shipped d5 b20 | 4.2157 | 4.2147 | 6 | 0 | **-6** | 188 | 9.49M -> 11.31M (1.19x) |
| d3 b10 | 4.2203 | 4.2190 | 10 | 2 | **-8** | 189 | 41.17M -> 42.15M (1.02x) |
| d0 | 4.6655 | 4.6655 | 0 | 0 | 0 | 0 (byte-identical) | - |

Two-Headed Giant (the suite's `slivers2hg` case: starting life 30, two heads), 3000 games per cell, seeds
10620001..10623000 (`ab2hg.json`, `logs/slivers-replicate/compare2hg.py`):

| cell | off avg | on avg | faster | slower | net | digests moved | work units off -> on |
|---|---|---|---|---|---|---|---|
| shipped d5 b20 | 4.8627 | 4.8620 | 3 | 1 | **-2** | 65 | 22.52M -> 27.47M (1.22x) |
| d3 b10 | 4.8913 | 4.8907 | 4 | 2 | **-2** | 68 | 33.40M -> 34.76M (1.04x) |

Attribution (single-player sample): `off -> sig` alone is 6 / 0 (-6) and 10 / 2 (-8); `sig -> on` is 0 / 0
at both searched cells (23 and 25 digests moved, no win turn). The measured gain is the on-board fan finally being searched; the
pending fan is what makes the `pending_split` class findable, rare in this sample, and costs the last 6% of
units at the shipped cell. Wall clock was not compared (the box ran at load ~60 on 24 cores throughout); units
are the deterministic budget measure. The cost is real branches that used to be collapsed: at the shipped
cell decisions are mostly below their budget, so the extra plans show up as work; at d3 b10 the budget binds
and they mostly re-price it.

**Identity.**
* The `off` arm IS the parent: the parent binary on the first 1500 games of each cell, same seeds and game
  indices, matches the fixed binary's `off` arm game for game (win turn and play digest), 4500 / 4500
  (`logs/slivers-replicate/offcheck_cmp.py`).
* Every other suite deck is byte-identical (parent binary vs fixed binary, default levers; `ident_*.json`, seed
  10700001, 40 games at d0, 20 at d3 b10, 20 at the shipped setting; 99 jobs, every one identical per game in
  win turn and play digest; the fixed binary's slivers jobs with both levers off also match). No other deck
  holds a replicate granter or pins a count, so this is inert by construction as well. Work units match
  exactly on every non-slivers job except kittyv2 d3 b10, kittyv3 d3 b10 / shipped and snow d3 b10 / shipped,
  which differ by 0.0001-0.3% with identical play (the shared-cache capacity effect, not this change).
* Side finding, NOT this change (it reproduces on the parent): kittyv3 seed 10700013 / gi 12 takes ~30 min
  of wall at d3 b10 and at the shipped setting for only ~200k budget units (`[goldfish] SLOW-GAME 1836474ms
  ... units=198758`), i.e. a per-node cost blowup, not a budget one.
* Lockstep oracle (`MTG_FLAG_NONCONV=1 MTG_FD_ORACLE=1`, 400 games per arm at both searched cells): 0
  `[nonconv]`, 0 `[fd-diverge]` on either arm.
* slivers_vial references (`test/viewer_protocol_check.py --only slivers_vial --strict`): 10 / 10 repaired,
  0 drift, identical to the parent (human play is untouched).

**Every slower game recovers (stage 1, `--depth W --budget-ms 100`, W = max(5, the parent's win turn)).**

| cell | game (seed / gi) | parent | fixed | stage 1 (fixed) | d3 b40 | d3 b160 | d3 b0 | mechanism |
|---|---|---|---|---|---|---|---|---|
| d3 b10 | 10601561 / 1560 | T4 | T5 | **T4** | T4 | T4 | T4 | budget churn: the T1 pick moves (Aether Vial -> Galerider) once the T2-T4 subtrees carry the searched counts; same draws |
| d3 b10 | 10604596 / 4595 | T4 | T5 | **T4** | T4 | T4 | T4 | budget churn: the T1 pick moves (Plated Sliver -> Aether Vial); same draws |
| 2HG shipped | 10621403 / 1402 | T4 | T5 | **T4** | - | - | - | budget churn: d5 b80 (4x the shipped budget) is T4 on both arms |
| 2HG d3 b10 | 10620747 / 746 | T4 | T5 | **T4** | T4 | - | T4 | budget churn |
| 2HG d3 b10 | 10622904 / 2903 | T4 | T5 | **T4** | T4 | - | T4 | budget churn |

All five recover at stage 1, at 4x budget at the cell's own depth, and (d3) at unlimited budget, where both
arms play the identical line. Recovery manifests: `recovery_s1.json`, `chk_d3.json`, `rec2hg.json`. Stage 2
was not needed. Net is negative at every searched cell, so the churn is outweighed, but it is real cost at the
budget we ship (the extra count branches dilute a 10-20 ms budget). The legitimate lever to reduce it is
ORDERING the fan (`DecisionProvider::ReplicateCounts` returns the counts in enumeration order); dropping a count
would make a line unfindable at unlimited budget, so a prune is out unless it is a proven dominance.

## What this does NOT fix: the cast order (USER-owned)

Hatchery Sliver ties with every Sliver at rank 10, so the plan (hand) order decides which resolves first.
With a Sliver listed BEFORE Hatchery, that Sliver can never be replicated in a plan that also casts Hatchery,
and no count fan can change that: `cast_muscle_first` (T6) and `vial_hatchery_mfirst` (the USER's "2 lords on
4", T4) are still not lethal at d8 unlimited. The draft's question 1 (rank Hatchery first) is the fix and is
the USER's to decide; this change does not touch slivers' cast order. With it adopted, the draft's positions
2-6 (which Sliver soaks up the copies) become unnecessary: that choice is now searched.

## Open questions for the USER (defaults taken, nothing blocked)

1. **Rank Hatchery Sliver before every other Sliver cast and Vial put** (the draft's question 1)? Without it the
   `cast_muscle_first` / `vial_hatchery_mfirst` lines stay unfindable at any budget. Default: unchanged (the
   cast order is yours).
2. **Human play:** a Sliver the viewer queues after a same-line Hatchery still gets its count from the
   resolution dialog (the pending fan is search-only, so references and the viewer are byte-identical).
   Offer the pinned count at queue time instead, as for an on-board Hatchery? Default: unchanged.

## Notes

* A pinned count can still be starved by an EARLIER unpinned cast in the same plan (autonomous play has no
  line hold: `SinkCostWithLineHold`), e.g. `copy_gal_first`'s `Hatchery pinned=-1 copies=1` then `Galerider
  pinned=2 copies=0`. Both worlds realise it identically and the realised split is its own enumerated plan
  (Hatchery pinned 1, Galerider 0), so nothing becomes unfindable; the starved plan is a duplicate.
* The levers exist for the A/B above (one pooled batch, three arms). Once the suite run accepts the change
  they can be deleted ("delete, don't lever").
