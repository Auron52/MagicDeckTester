# Mana-source reservation ("leaving sources up")

Handoff for the follow-on to the shipped **scarcity-first tap ordering**. Self-contained:
everything needed is below or in the referenced code — no external notes required.

## STATUS UPDATE (2026-07-02) — superseded by whole-turn batch payment

The **per-payment** reservation scheme sketched later in this doc was built and **regressed**:
holding a special source whenever *a single payment* didn't need it stranded a *later same-turn*
cast (treasure_hunt seed 3044: reservation ON → T4 vs OFF → T3). Root cause: "payable without S"
judged against one payment, not the whole turn. It is **disabled by default** (`ReserveEnabled` →
`MTG_RESERVE` opt-in, inert = byte-identical off); the per-payment two-tier wrappers remain inert
infra.

That investigation generalized into a real correctness finding: the **per-cast greedy strands a
jointly-castable line even with reservation off** (it won't feed a ramp-filter, and can spend a
scarce colored source on a generic pip a later cast needs). The fix that **shipped** is
**whole-turn batch payment** — `TurnSolver::BatchPrepayMainCasts`
([`src/ai/TurnSolver.cpp`](../../src/ai/TurnSolver.cpp)): pay the turn's combined main-cast cost in
ONE `TapForCostBacktrack` solve, pre-load `state.floating_mana` (colored pips pinned, generic as
`wild`; generic drains `wild`-first — see `SpendFloatingTowardCost`), and let the casts drain it.
Filters get fed, colors allocated jointly, and **unneeded sources stay untapped for free**. Default
on (`MTG_NO_BATCH_PAY` off-switch); GT rebaselined 2026-07-02 (net-positive: hinata d0 +16 wins;
th/hinata/antilife/slivers searched faster; burn/knights byte-identical; the tiny d3 slowdowns are
budget churn — equal at unlimited budget). `out_full_pool` added to `TapForCostBacktrack` captures
the produced pool.

### The reservation follow-up, redefined (the correct shape)

Batch-pay leaves *some* surplus source up, but does **not** specifically preserve the *valuable*
ones (a depletion counter, a dork/manland kept up to attack) — the solver may tap those first. To
capture that value, do **"leave out if you can"** *inside* batch-pay, driven by the **whole-turn**
solve (sound — no stranding, unlike per-payment):

- In `BatchPrepayMainCasts`, first solve the combined cost with `reserved_mask` covering the
  reservable specials (`ReservableSpecialMask`: inflexible dorks, {C}-manlands, depletion lands).
  Reuse the existing (inert) `reserved_mask` plumbing.
- Sound + safe: "leave out" just means *don't pre-tap*; the source stays on the battlefield, so a
  post-draw re-solve that genuinely needs it can still tap it (never stranded).
- **Scoping:** depletion-land reservation is self-contained (counter preservation, no attack) and
  can land alone. **Dork/{C}-manland reservation only pays off with the exalted-aware attack
  declaration** (`AntiLifegainProvider::ShouldAttackWith`, built but opt-in `MTG_EXALTED_ATTACK`,
  default off, uncommitted) — a reserved 0-power dork that then swings into breaking the lone-attacker
  exalted bonus is worthless/worse. So dork/manland reservation should ride together with that fix.

#### SHIPPED (2026-07-02) — depletion-land reservation

Landed the depletion slice in `BatchPrepayMainCasts` ([`src/ai/TurnSolver.cpp`](../../src/ai/TurnSolver.cpp)),
gated `DepletionReserveEnabled()` ([`src/core/SpellEffects.h`](../../src/core/SpellEffects.h), default
ON, off-switch `MTG_NO_DEPLETION_RESERVE`):

- Cheap prescan builds `reserved` = every *untapped* depletion land (`enters_tapped_with_depletion>0`)
  the active player controls. Non-depletion decks find none → `reserved=0` → the held attempt is
  skipped entirely (byte-identical, no extra solve).
- **All-or-nothing, not per-source-maximal.** The combined cost is solved once with *all* depletion
  lands held; if it pays **wild-free**, their counters are preserved for free. If holding them makes
  the turn unaffordable or forces a wild/ambiguous tap, restore and solve unrestricted (they are
  needed this turn). Chose all-or-nothing over the maximal-subset the bullet above sketched: the
  per-source greedy did *k* extra full backtrack solves per batch call (~80 % of which reject, since
  the land was needed) → ~15 % slower on treasure_hunt; all-or-nothing is 1 solve in the common slack
  case, ≤2 otherwise → **perf-neutral**. Still lossless (holds nothing when it can't hold all = the
  original); only misses the rare partial save (hold 1 of 2), which is empirically nil here.
- **Result: byte-identical across the whole smoke + regression + overnight-seed A/B on treasure_hunt
  (the only depletion deck).** treasure_hunt wins ~turn 4, too fast for a preserved counter to change
  any outcome. So this is correct, zero-regression, perf-neutral *latent* infrastructure — no numbers
  move today, but it's the substrate the dork/{C}-manland reservation (next, with the exalted fix)
  builds on, and it's a genuine improvement for any future grindy depletion deck.

Everything below predates this update (the original per-payment design); keep for context.

## Background: what already shipped

The greedy mana solver pays each pip from the **least-flexible qualifying source**, so
flexible sources stay untapped and the exponential `TapForCostBacktrack` fallback rarely
fires.

- Ranking: `GenericProvider::ManaSourceRank` — [`src/ai/DecisionProviders.cpp`](../../src/ai/DecisionProviders.cpp)
  (Hook 24, declared in [`src/ai/DecisionProvider.h`](../../src/ai/DecisionProvider.h); per-deck
  overridable). Ranks, spend-lowest-first: fixed/bounce 10, dual 20, filter/ramp-filter 25,
  tri 30, rainbow/any 50, **{C}-only manland (Mutavault) 60 = saved to attack**.
- **The two greedies MUST stay lockstep:** the scarcity selection is duplicated in
  `AIEngine::TapForCost` ([`src/ai/AIEngine.cpp`](../../src/ai/AIEngine.cpp)) and
  `TurnSolver::TapForCostDirect` ([`src/ai/TurnSolver.cpp`](../../src/ai/TurnSolver.cpp)).
  Change one, change both, or the rollout and the executor desync (a divergence bug).
- Toggles: `MTG_TAP_LEGACY` reverts to battlefield-array order (A/B lever); `MTG_TAP_STATS`
  counts top-level `TapForCostBacktrack` entries (backtracker pressure); `MTG_UNPRUNED`
  (`DecisionUnpruned()`) is the standing unpruned-vs-pruned audit switch.
- Completeness stays **by construction**: `TapForCostBacktrack`
  ([`src/core/SpellEffects.h`](../../src/core/SpellEffects.h)) remains the complete fallback,
  so the heuristic only ever picks *which* legal payment, never *whether* one exists.

Commits: `8cfebf1` lossless backtracker micro-opts · `2c98c88` scarcity default-on + GT ·
`efd5758` overnight GT.

## The problem this doc is about

Scarcity ordering decides *which legal payment to make*. But the mana decision with real
search value is a different one: **which sources to leave UNTAPPED.** And the search
cannot currently see it, because **tapping is not a search branch** — the planner
enumerates cast *plans* and delegates tapping to the greedy. Consequences:

- The search **does** recover cast-*sequence* value at depth (a different order of the same
  casts is a plan it can explore).
- The search **cannot** recover pure **reservation** value: leaving a dork untapped to
  attack, holding an Island for next phase's Ponder, or not spending a depletion counter.
  No amount of depth finds these, because the choice never becomes a branch.

## Scope — bounded, NOT the exponential tap search

Reservation only matters for a **few special sources**. Do not turn general tapping into a
search space; enumerate hold-vs-tap only for:

- a mana **dork** (hold to attack),
- a `can_animate` **manland** (hold to attack — already why Mutavault ranks 60),
- a **depletion** land (hold to avoid wasting a counter).

Two drivers, treated differently:

1. **Hold-for-future-cast** — clairvoyance-driven and deterministic (e.g. keep {U} up for a
   Ponder next phase). This is also the real fix for the Treasure-Hunt d0
   sequential-execution strand.
2. **Hold-to-attack** — a combat-value judgment.

## Direct user requirements (from the play-viewer sessions)

Requirements the user stated explicitly; treat as authoritative alongside the scope above.

- **Branch only when the special sources COMPETE.** If the current line can be paid while leaving
  *all* special sources untapped, just leave them all up — emit no hold-vs-tap branch. A branch is
  needed only when the plan genuinely needs some of them for mana (tap-for-mana vs reserve). "Ignore
  cases where there is no conflict — just keep the source untapped." This keeps the branch count near
  zero on most turns.
- **Cheap branching via value ordering.** When forced to tap some-but-not-all, reserve the
  *highest-value* creatures and tap the least-valuable first, so you don't enumerate all subsets.
  For a dork that can deal combat damage, prefer keeping the **largest-power** one back; **exalted**
  matters — a lone attacker gets the exalted bonus, so keeping exactly one creature back can be the
  damage-maximizing choice.
- **One-shot / mode-sacrifice sources generalize the "don't waste it" rule.** Besides depletion
  lands, this includes **sac lands** — come-into-play-tapped lands with two tap modes: **tap for {1}**
  (keeps the land) or **tap for {2} and sacrifice it**. These are NOT fetchlands. The reservation
  choice here is per-*mode*, not hold-vs-tap: don't use the tap-for-{2}-and-sacrifice mode unless the
  extra mana is actually needed, since it permanently loses the land. Default to the {1} mode; only
  branch into the sacrifice mode when the plan needs that extra mana this turn.
- **Grove of the Burnwillows — deferred, and it's a different problem from reservation.** In an
  anti-lifegain shell, tapping Grove makes the *opponent lose* life (its "each opponent gains 1"
  flipped by a punisher), so it looks like a damage source. But it is deferred for a deeper reason:
  making it work requires the engine to support **tapping a source for no reason — with nothing to
  cast** (tap Grove purely for its mana-ability side effect, floating/wasting the mana). Today every
  tap is driven by paying a spell's cost; there is no "activate a mana source for its side effect
  with no spend" path. That missing capability — not a reserve-vs-tap branch — is the prerequisite,
  which is why Grove is a distinct, later piece.
- **Motivating repro:** Anti-Lifegain **Seed 12, Game 11** — the user believes a turn-5 win exists
  but the greedy taps mana dorks that should have attacked, so the win is unreachable. Validate the
  reservation branch recovers it.

## Recommended plan (oracle-first)

Add hold-vs-tap **branches under `MTG_UNPRUNED`** — 2^k over the k≈0–3 special sources on
board — as an audit, *before* writing any heuristic. That reveals, per deck, whether
reserving actually wins. Then build a per-deck provider hold-hook **only where the audit
shows it pays**. The rollout machinery already values an untapped dork's attack / reserved
mana; the only new machinery is enumerating the reserve subset.

This is also the concrete answer to "use `MTG_UNPRUNED` to audit tapping decisions."

## Hard constraints (do not violate)

- **`MTG_UNPRUNED` must NOT fall back to the default heuristic.** Unpruned means genuinely
  explore the reservation branches — falling back to the heuristic defeats its whole purpose.
- **Keep `TapForCostBacktrack` as the complete fallback.** Completeness stays by construction.
- **Search-primary:** heuristics only prune; keep the unpruned-vs-pruned A/B byte-identical
  when the heuristic is off.

## Gotchas / learnings

- **Tapping is irreducibly a decision (Mode A).** The perf/behaviour effect *is* the
  different resource state; you cannot "canonicalize a tapping choice to be lossless."
- **Manland ranking is subtle.** Rank **{C}-only** manlands last (reserve to attack);
  **colored/dual** manlands and **depletion** lands rank *by color* and are deliberately NOT
  reserved by default — they are mana you normally spend. An early version reserved *all*
  manlands and regressed Slivers via forfeited Mutavault attacks.
- The current regression suite barely exercises reservation (the only mana-creature in it is
  Hinata's Ornithopter — 0 power, rainbow, always held), so it "doesn't bite" today.
  **Validate on decks where a held dork/manland attack or an unspent depletion counter
  actually changes the outcome.**

## Validation

- `MTG_UNPRUNED` off must stay **byte-identical** (the guarantee that the heuristic only
  prunes).
- Use `MTG_TAP_STATS` to watch backtracker pressure.
- Run `test/regression.sh` (smoke → regression → overnight) and use the `--accept` flow with
  a **per-game audit** — never hand-edit ground truth.

## One nearby change, so the code doesn't surprise you

`9229b25` added a branch-and-bound **total-mana gate inside `TapForCostBacktrack`**
(`MTG_NO_MAXMANA_GATE` disables it; lossless, ~15–19× faster on Hinata). That is about
*pruning the backtracker's search*, not tap ordering — but it is why the backtracker looks
different from the `8cfebf1`-era code.
