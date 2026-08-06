# Cleanup discard as a search node (retire the probe oracle)

**Status: IN PROGRESS 2026-08-06 (Fable). Stage 1 not yet committed.**
User-directed redesign; the architectural ruling and the agreed shape are recorded below verbatim
so this survives context loss. Predecessors: `docs/design/th-d5-five-hour-game.md` (the probe
blowup + the heuristic-prune fix), `docs/design/post-breakpoint-search.md` (the `bp_choice`
pattern this follows), `docs/design/searched-cleanup-discard.md` (the probe being retired).

## The ruling (user, 2026-08-06)

The searched cleanup discard (`MTG_SEARCHED_DISCARD`, default ON) is an **out-of-band estimator**:
a side process that plays nested engine games per candidate and hands the executor a pick. That is
neither of the two sanctioned roles — it is not the search deciding (the committed search never
sees the discard as a choice; its lookahead models every future cleanup with the width-1
heuristic), and it is not a heuristic merely pruning. It is a third thing: a cheap oracle
*replacing* search judgment. Its nested-game structure is exactly what exploded on treasure_hunt
(d4=90s/game before the heuristic-prune fix). The sanctioned shape is the `bp_choice` template:

- **In-search**: the shed choice is a real branch inside the recursion — searched at declared
  depth, under the same memo, cutoffs, and first-win ladder as everything else. Nested games are
  impossible by construction.
- **Heuristics prune, as designed**: the provider's ranked `CleanupDiscardCandidates` narrows the
  branch (fat TH hands collapse hard under name dedup); `MTG_UNPRUNED`-style unbounded A/B stays
  available.
- **Executor lockstep**: the committed line carries the searched discard exactly as it carries
  `bp_choice`; the heuristic is the fallback when no line is committed.
- The probe then gets **deleted on the searched path**, not tuned.

## What already exists (all committed, found 2026-08-06)

The in-search half is BUILT (a prior session; found wired but dormant):

- `TurnSolver::Plan::discard_choice` (TurnSolver.h ~331, default −1 = provider top pick,
  byte-identical). Copied onto `GameState::scripted_discard_choice` by `ApplyPlanDirect`
  (TurnSolver.cpp ~5684) because the shed fires in `SimulateEndAndStartNextTurn`, after apply
  returns — it rides the STATE, like `lackey_choice`.
- Rollout consume (TurnSolver.cpp ~7637–7649): the FIRST shed of a cleanup takes
  `cd[min(scripted_discard_choice, cd.size()-1)]` (clamped — the plan was enumerated before this
  turn's draws changed the hand), clears the pin, and later sheds of the same cleanup fall back to
  the ranked default. One-choice-per-plan, same convention as the ETB-dig/Lackey axes.
- Variant emission (TurnSolver.cpp ~10726–10755): post-dedup, base plans only (no stacking with
  other axes), emits `discard_choice = 1..W-1` where
  `W = provider->CleanupDiscardSearchWidth()` — **default 1 everywhere** (= axis dead;
  five of nine suite decks never reach a cleanup discard, three more are rare).
  `TreasureHuntProvider::CleanupDiscardSearchWidth` reads `MTG_TH_DISCARD_WIDTH` (default 1,
  "see the sweep before raising"). Width is taken on faith because the over-limit hand only
  exists AFTER the turn's draws (TH's DrawUntilNonland is the flood); clamped at resolution.

The executor half does NOT exist:

- `AIEngine` replays committed phases (`m_committed_line`, a deque of
  `TurnSolver::PhasePlan{is_pre_combat, Plan}`) through its own action loop, NOT
  `ApplyPlanDirect` — so `scripted_discard_choice` is never pinned on the real state, and
  `AIEngine::ChooseDiscard` (AIEngine.cpp ~3390–3482) never reads the plan's choice.
- Instead the real decision runs the probe: `s_searched_discard && LookaheadBottoming() &&
  !m_in_rollout && cand.size() > 1` → one `RolloutWinTurnFrom(trial, ResumeAt::Cleanup)` FULL
  GAME per candidate; heuristic tie-break; **on deviation it must clear `m_committed_line`**
  (`s_discard_reline`, MTG_DISCARD_RELINE default ON) because the line assumed the heuristic
  shed — i.e. the probe *falsifies its own committed line* by construction. TH is already
  probe-inert (its provider returns ONE candidate — the 2026-08-06 heuristic-prune fix); generic
  decks still fan (base `CleanupDiscardCandidates` returns the full hand as the trial set).

## The remaining work, staged

**Stage 1 — executor lockstep (inert alone; byte-identical).** When the executor starts executing
a committed phase, pin `phase.plan.discard_choice` (≥0 only) onto the real
`state.scripted_discard_choice`. `ChooseDiscard` consumes it FIRST — before the probe: if the pin
is ≥0, shed `cand[min(pin, cand.size()-1)]`, clear the pin, skip the probe (the search already
decided under the same assumptions the line encodes; prediction == realisation, no reline).
Hygiene: clear the pin at every cleanup entry (consume-or-discard) so a pin from a turn that
never discarded cannot leak into a later turn — and mirror the same clear in the rollout
(TurnSolver ~7637: today it clears only when a shed actually runs; a no-shed turn leaks the pin
forward there too. Fix both sides identically or the lockstep replay diverges from the scored
line). While every provider width is 1, nothing pins ≥1, so this stage is byte-identical — gate
+ smoke digest check, then commit.

**Stage 2 — probe retirement behind a flag.** `MTG_DISCARD_NODE` (EnvOn, default OFF initially):
when ON, `ChooseDiscard` never runs the probe — committed pin if present, else heuristic top
pick (the transcript-agreed fallback). `MTG_SEARCHED_DISCARD` stays as-is when the flag is off
(exact hatch). A/B `MTG_DISCARD_NODE=1` vs baseline: suite three tiers + per-game wins. Risk to
watch: with all widths 1, retiring the probe = pure-heuristic shed for the generic decks that
still probed — a possible QUALITY loss on the rare generic-deck cleanups. Classify any changed
game mistake-vs-clairvoyance (user's rule) before widening anything.

**Stage 3 — activate the axis where the decision exists.** Under `MTG_DISCARD_NODE=1`, sweep the
axis width for the decks that actually discard (TH first via `MTG_TH_DISCARD_WIDTH`; the base
provider's width for the rare-discard decks — candidates for a distinct-name top-k return rather
than the full hand). Watch node cost (the variants pay a plan rollout each) and use unbounded
budget on changed games. The user's no-knobs principle applies to the END STATE: the width hook
should converge to "the size of the provider's returned candidate set" once measured — a
provider that returns k candidates gets k branches, no separate knob. (The blind-emission
constraint — the flooded hand doesn't exist at enumeration time — is why the hook exists at all;
document any residual gap between the hook and the return.)

**Stage 4 — adoption.** If stage 2+3 measure neutral-or-better: default `MTG_DISCARD_NODE=1`,
demote `MTG_SEARCHED_DISCARD` to a legacy hatch (or delete after a deprecation window), delete
`MTG_DISCARD_RELINE` on the lockstep path (the reline exists only because the probe deviates
from the line), GT rebaseline all three tiers, update this doc + memory.

## Why this is worth doing (beyond the ruling)

1. **fd-lockstep**: the probe's deviation currently falsifies the committed line (reline = clear
   + re-search). The search-node version keeps prediction == realisation by construction — the
   same class of win as `bp_choice`'s consistency argument.
2. **The blowup class dies**: nested full games at the real decision still exist for generic
   decks (bounded today only because their hands are rarely fat). After stage 2 the class is
   structurally gone.
3. **The rollout's future cleanups become searchable**: today every scored line assumes the
   width-1 heuristic shed on every future turn; the axis lets the search disagree where the
   provider offers alternatives.

## Verification bars

- Stage 1: byte-identical (smoke 30/30 digests) with all widths 1.
- Stage 2: `MTG_DISCARD_NODE=1` A/B — suite fingerprints + per-game wins; fd-diverge counts
  must not increase (expect decrease: reline events disappear).
- Stage 3: per-width sweeps, train seeds; winner validated on held-out (overnight) seeds;
  unbounded-budget recovery check on every slower game.
- The unpruned benchmark: base provider full-hand candidates at unbounded budget = free-rein arm.

## Measurements (2026-08-06) — stages 1–3

- **Stage 1 COMMITTED `c408ddd`** (lockstep, engine-member pin, byte-identical: smoke 30/30).
- **Stage 2 COMMITTED `6d44a04`** (`MTG_DISCARD_NODE`, default OFF; off = byte-identical).
  **A/B (flag on, axis dormant): the probe's measured value is hinata +0.0050..+0.0100 (all 4
  d3/d5 train cases) and antilife +0.0066/+0.0080 (2 cases); all 39 other cases byte-identical.**
  So the probe cannot be deleted without recovering those two decks somehow.
- **Stage 3 width sweep: THE BLIND AXIS IS THE WRONG MECHANISM — measured strictly harmful.**
  `MTG_DISCARD_WIDTH` (temporary base-hook lever, uncommitted) at W=2/4/8 under
  `MTG_DISCARD_NODE=1`, regression tier: monotonically worse with W on nearly EVERY deck
  (hinata +0.085→+0.25, antilife +0.017→+0.068, dragonstorm/slivers/goblins/creature_giving all
  degrade; makespan 132s→380s). Textbook budget dilution (the bp "site 3" lesson): the axis
  emits W−1 variants BLIND on every base plan of every deck, while the discard fires on a tiny
  fraction of turns — unlike `AppendBreakpointVariants`, which fans only plans that actually
  OPEN a breakpoint. `logs/vlq_all_ab/regression_dnode*.log`.

## The fork (presented to the user 2026-08-06 — DO NOT proceed without their pick)

A. **Accept the small loss and retire the probe now**: default `MTG_DISCARD_NODE=1` at width 1
   everywhere. Cost: hinata +0.005..0.010, antilife +0.007..0.008 on train seeds (held-out
   unmeasured). Buys: the oracle class is gone, reline gone, architecture clean.
B. **Rebuild the axis bp-style before retiring**: emit discard variants only when a plan's
   simulation actually reaches an over-limit cleanup with >1 candidates (apply-time discovery,
   like breakpoints; wave-compatible; memoizable). Structurally right per the ruling; a real
   build — the cleanup fires inside SimulateEndAndStartNextTurn's turn loop, not inside
   ApplyPlanDirect, so it needs its own discovery→fan plumbing (this is exactly why the original
   builder chose blind emission).
C. **Keep the probe** (status quo): measured-good, architecturally unsanctioned.
D. **Gate the blind emission on a provider predicate** ("will this plan plausibly flood?" —
   hand size + planned draws): cheaper than B, keeps some blindness, still per-deck tuning.

The temporary `MTG_DISCARD_WIDTH` base-hook lever is LOCAL/UNCOMMITTED (DecisionProvider.h) —
revert or keep per the pick.

## Session state (for resume)

- 2026-08-06: stages 1–2 committed; stage-3 sweep refuted the blind axis; fork presented.
  Knights live-model matrix closed separately (trust=5 honest at 2000g; t4's −2.1% Ir not
  available on the live model).
