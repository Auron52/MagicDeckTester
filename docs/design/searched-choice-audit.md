# Searched-choice audit: branch by default, heuristic as prior

USER directive (2026-08-25, verbatim intent): a decision like the Crop Rotation sacrifice
target "should be a branch by default and be overridden by a heuristic", and more broadly:
"we have a few architectural weaknesses like this. Cases where bad heuristics are introduced
with no testing. They should really be fixed."

This doc carries (A) the concrete design for the SAC-LAND AXIS (the exemplar fix, deferred at
a compaction boundary mid-implementation), and (B) the class-level inventory of other
heuristic-only decision points that should get the same treatment.

## The architecture to reuse: the scripted-index axis

The codebase already has the correct pattern (scry_choice / etbdig_choice / ponder_choice /
tutor_choice): the Plan carries an INDEX; a thread-local RAII pin (e.g. `ScriptedTutor`,
SpellEffects.h ~8459, `g_scripted_tutor_choice` ~8455) installs it for the apply; the SHARED
resolution function consumes the pin at the TRUE mid-plan state (land played, prefix casts
paid); base plans keep index = -1 = the provider's front at that same state, so base and
variants are one ranking at one state by construction. Fan-out lives in
EnumeratePlansWithLand next to the tutor axis (~TurnSolver 19992), emitting index variants
off BASE plans only (one axis at a time -> additive cost). The search picks the winner; the
COMMITTED plan's index rides to the executor, which installs the same pin
(AIEngine ~2432 `ScriptedTutor _stut_exec(plan.tutor_choice)`) -> lockstep by construction.
Breakpoint capture/resume carries pins (TurnSolver ~15544/15754). **The pin MUST be folded
into the canonical memo key** (TurnSolver ~25478/25484 folds tutor/reorder pins) — see the
canon-adoption-laws order-invariance rule.

The heuristic's role under this architecture: (1) the branch ORDER (provider front first ->
best cutoff behaviour), (2) the ONLY decision on paths with no plan (d0 greedy, rollouts,
mulligan trials), (3) an optional collapse of the branch where it is provably lossless
(the dork-atk contest's "every obvious case closed heuristically, search the contested
remainder" idiom).

## (A) SAC-LAND AXIS design — IMPLEMENTED 2026-08-25

Status: built exactly per the design below, plus the bounceland prior tier. Sites:
`Plan::sac_pins` (TurnSolver.h), `ScriptedSacLand` + `ConsumeScriptedSacPin` +
consumption in `PerformSacrificeLandCost` (SpellEffects.h; the one consumption rule is a
SHARED helper because two open-coded copies of one sacrifice rule is exactly the cg30
lockstep hole), thread-locals in GameLogger.cpp, fan-out + `SacAxisEnabled` +
branchstats + memo-key suffix fold (TurnSolver.cpp; the remaining UNCONSUMED pin suffix
folds -- a fully consumed list keys as no list), bp-snapshot carries the cursor only (the
list is re-installed from the resumed plan by the entry guard), executor pins in
AIEngine.cpp (`_ssac_exec` + the cast-path site consumes the same shared helper).
Verified: acid test PASSED -- cg30 with MTG_SAC_AXIS=1 and MTG_SAC_SPAWN_LAND_LAST=0 wins
T4 (baseline without either: 5; game log confirms 3 Orchards on board after the double
Crop Rotation, Temple Garden + the mid-plan Forest eaten -- the pinned ordinal resolving
to a land that exists only mid-plan, which is what the +1 width headroom is for);
axis+veto also 4; clean-env smoke 42/42 with 0 config changes (dormant = byte-identical).
The bounceland tier (below) is live in the prior under MTG_SAC_SPAWN_LAND_LAST
(fungible < bounceland < spawn, tapped-first within each band); Creature Giving runs
Azorius Chancery so the tier is exercised in the suite. Still open from the doctrine: the
Crop-Rotation cast-desirability gate for plan-less paths (d0/rollout MoveOrder) -- the
axis prices it at searched depths.

## (A.1) Original design (for reference)

Motivating case: cg30 (overhaul ledger). The sac target is chosen by
`DecisionProvider::SacrificeLandCandidates` (tapped-first; + MTG_SAC_SPAWN_LAND_LAST
spawn-lands-last, commit 743c60e7) via `PerformSacrificeLandCost` (SpellEffects.h ~8937,
now the single shared site for search AND executor). It is not a branch, so the search
could not even represent the line where the Orchard survives. The static spawn-last lever
fixed cg30 but is a VETO — superseded in shape by this axis; it remains as the branch
ORDER/prior once the axis exists.

Design decisions (worked out pre-compaction):
- **Per-sac-ordinal pins, not one pin per plan.** cg30's winning deviation is the SECOND
  CR's sac (first sac's default is already right). `Plan::sac_pins` = `std::vector<int>`
  (empty = all provider-front); a thread-local pin-list + cursor, RAII-installed; each
  `PerformSacrificeLandCost` call consumes the next entry; entry c >= 0 picks
  `ranked[min(c, ranked.size()-1)]` (clamp = the tutor axis's duplicate-not-whiff rule).
- **Fan-out** (lever `MTG_SAC_AXIS`, DEFAULT OFF, adoption-config candidate): for base
  plans (no other axis variant) containing S sacrifice_land casts, emit per-ordinal
  one-hot variants: for j in [0,S), c in [1,W): sac_pins = (-1 x S), sac_pins[j] = c.
  Additive cost S x (W-1) per base plan.
- **Width** W = min(4, distinct-sacable-land-NAMES at turn-start + 1). Turn-start sizing
  only; resolution clamps drift — the +1 headroom matters because the mid-plan land set
  changes (cg30: the Forest only exists after Misty's crack, and the winning c=1 at CR#2
  resolves to it).
- **Executor**: install the pin list around the committed plan's apply, mirroring
  AIEngine ~2432. claude-play's interactive sacrifice chooser is unchanged (human
  override). d0/rollout paths keep provider front.
- **Memo key**: fold the pin list (or its cursor-consumed hash) alongside the tutor pin
  fold at ~25478.
- **Acid test**: `MTG_SAC_AXIS=1` with `MTG_SAC_SPAWN_LAND_LAST=0` must recover cg30
  (DTL-only and full bundle) to 4 at d3 unbounded — the branch must find what the veto
  found. Then decide with the USER whether the adoption config carries the axis, the
  spawn-last prior, or both (prior-as-order costs nothing and improves cutoffs).

USER heuristic doctrine for the sac-land PRIOR (2026-08-25, verbatim intent):
- "Sacrifice Orchard last. Anything else is a better choice except maybe the bounceland."
  -> rank: fungible lands first (tapped-first within), bounceland (etb_bounce_land, e.g.
  Azorius Chancery — its tap is 2 mana/turn of ongoing production) second-to-last, spawn
  land (Orchard) strictly last. The current MTG_SAC_SPAWN_LAND_LAST handles only the
  Orchard tier; add the bounceland tier when touching this next.
- "In general it is a wasted play to use Crop Rotation when you would have to sacrifice an
  Orchard." -> a CAST-desirability rule, above the target choice: when no non-spawn land
  is available to sacrifice, the CR cast itself should be disfavoured on heuristic paths
  (d0/rollout MoveOrder/eval), not just given a least-bad target. (Sacking an Orchard to
  fetch an Orchard is net-zero engines plus a wasted card and a shuffle; sacking one for a
  fungible land is strictly worse than not casting.) The AXIS prices this naturally at
  searched depths — the heuristic gate is for the paths that never search.
- Perf: creature_giving is the only sacrifice_land deck in the suite (Crop Rotation;
  Shard Volley decks would also fan). Measure the axis's cost on the creature_giving train
  cell + smoke wall-clock before any default flip.

## (B) Inventory: heuristic-only decision points (candidates for the same treatment)

Ordered by suspicion (defects already traced to them, or same shape):

1. **Sac-land target** — (A) above. Defect: cg30 (fixed by veto; axis pending).
2. **Second-main planning family** — fc96/fc341 (ledger "fc96 dig"): the m2 is cast-only
   by default (`MTG_MAIN2_DROP` off, parked adoption item), and even with the drop the m2
   machinery misses the executor-realizable kill (fetch-crack at m2 / pool estimate — the
   winning path also routes around the [m2t]/[fsw] trace sites in BOTH arms; instrument the
   collapsed-main route). Not an axis per se, but the same class: a modelling hole no
   budget/depth can cross.
3. **Sac-CREATURE victim** (`SacCreatureCandidateIndices`, SpellEffects.h ~8975): static
   most-expendable-first + MTG_SAC_SPARE_ATTACKERS lever. Same shape as sac-land; Natural
   Order victims already ride Action::soulfire_own_targets in some paths — check whether
   the search actually branches victims everywhere or only at collection.
4. **Payment tap order** — rank-driven (ManaSourceRank), explicitly not a branch ("tap
   order is NOT a search branch", overhaul ledger band-D note). Known residuals: st993's
   Archdruid-over-ArborElf tap (Cluster B), the drip-land-vs-dork situational call (Grove
   comment: "left to future search"). A full tap-order axis is likely too wide; the
   turn-scope reserve rungs are the current mitigation. Revisit only with a measured case.
5. **Bounce-land choice** (`BounceLandCandidates`) — static score; no known defect; low.
6. **Fetch/dual colour picks in deck providers** (e.g. AntiLifegainProvider::
   FetchCandidates) — partially covered by the tutor axis where the fetch IS a tutor;
   verify coverage per provider.
7. **Cleanup discard** — heuristic; DiscardPolicy.h; no known defect; low.
8. **Attack declaration** — AttackWith heuristic + the searched dork contest covers the
   contested-dork slice only. The fc96 dig confirmed vigilance exemptions are present; no
   open defect beyond the m2 family above.

Process rule going forward (the USER's "introduced with no testing" complaint): a NEW
heuristic decision point must either (a) ride an existing searched axis, or (b) come with
a measured A/B (the heuristic-optimization skill's loop) before adoption. A bare untested
rank/order added to both engines is the failure mode cg30 exemplifies — the tapped-first
sac rule shipped inside a feature and was never separately measured.
