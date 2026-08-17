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

#### SHIPPED (2026-07-02) — exalted-aware attack declaration

`AntiLifegainProvider::ShouldAttackWith` ([`src/ai/DecisionProviders.cpp`](../../src/ai/DecisionProviders.cpp))
is now **default ON** (off-switch `MTG_NO_EXALTED_ATTACK`). It holds back a 0-power, no-attack-trigger
creature (a mana dork) rather than swinging it alongside a real attacker — which deals nothing and
breaks Ignoble Hierarch's *lone-attacker* Exalted bonus — and lets it attack only as the sole eligible
creature (to switch Exalted on / carry an Invigorate pump). Honoured in lockstep by every combat site
(`PendingAttackDamage` projection, the rollout's ApplyCombat, and the real DeclareAttackers).

Net win on Anti-Lifegain (the only exalted deck): **+2–3 % d0 wins** and faster searched averages
across smoke/regression/overnight seeds, **0 win↔loss**. A handful of searched-depth games win a turn
**later**, but that was proven to be **fetch-shuffle draw variance, not a bug**: the more accurate
exalted valuation flips an early *land* tie-break, a fetchland reshuffles, and the game draws
differently from there. Among **462 games with identical draw sequences, ON never wins later** (0
regressions); every turn-later game has a divergent post-fetch draw. GT rebaselined smoke + regression
(`--accept`); **overnight antilife GT is still stale (exalted-OFF) — rebaseline on the next overnight
run**. NOTE: for this deck the deliverable is the attack fix *itself* — Ignoble is tri-color
(flexible), which the reservation mask excludes, so dork *reservation* is a no-op here. Dork/{C}-manland
reservation still pays off only on a deck with an **inflexible** 0-power dork + Exalted.

#### SHIPPED (2026-07-02) — "hold your beater" attacker reservation

Extends the `BatchPrepayMainCasts` leave-out-if-you-can hold (alongside depletion) to the controller's
**greatest-power attacker WHEN it is a mana source** (dork/manland): don't tap it for mana if the turn
pays without it, so it stays untapped to swing — and, since an own-creature pump lands on
`FindBestOwnAttacker`'s pick, reserving that creature makes the pump target the one left up (the
practical answer to "reserve the pumped creature" without needing the target chosen before payment).
Restricted to mana-source creatures because a non-mana beater is never in the tap set (reserving it is
inert). Gated `AttackerReserveEnabled()` (default ON, off-switch `MTG_NO_ATTACKER_RESERVE`).

Net improvement, **0 turn-later regressions**: Anti-Lifegain d0 +1 win / faster d3, Hinata d3 +1 win
(a no-win → win), and slivers/burn/treasure_hunt/knights **byte-identical** (no mana-source creatures,
or none is the best attacker in a tapped spot). Note: every mana dork in the current card DB is
0-power, so the benefit is the *downstream* effect of which 0-power dork stays up (pump target /
exalted / deeper lines), not raw attack damage — no clean distinguishing scenario is constructible, so
it's validated by the seed A/B; `test/scenarios/dork_pump_target.json` guards the pump coordination it
supports. GT rebaselined smoke + regression; **overnight antilife/hinata GT rebaselined in the same
overnight pass as the exalted change.**

#### SHIPPED (2026-08-17) — EVERY mana creature, and a hold LADDER

Generalises the beater hold to **every untapped mana creature**, which is the user's own rule from the
play sessions (quoted verbatim under "Direct user requirements" below): *if the line can be paid while
leaving all special sources untapped, just leave them all up — branch only when they COMPETE.* A land
has no use but its mana; a creature attacks, carries Exalted, is a legal target for a pump/copy trick,
and can be sacrificed — so spending a dork on a pip a land could have covered throws that away for
nothing. Gated `DorkReserveEnabled()` (default ON, off-switch `MTG_NO_DORK_RESERVE`). Mana **rocks**
are deliberately not reserved: a rock has no other use, so holding one only risks a failed solve.

The second half is the real fix. All-or-nothing (one held attempt, then unrestricted) throws away the
**partial** save whenever the two reservable classes compete, so the hold silently released the dork:

> Mirrorwing s24 gi23 T4 — board Forest, Forest, Mountain, Sandstone Needle (depletion), Ignoble
> Hierarch; line = Gold Rush ×2 ({1}{G} twice). Holding **both** the Hierarch and the Needle leaves 3
> mana for a 4-mana turn → infeasible → old code released both and tapped the Hierarch. Holding just
> the Hierarch pays fine off Forest+Forest+Mountain+Needle. The tapped Hierarch could not attack, and
> the game that should win on **T4 won on T5** (reported as viewer issue #9; also #1 and, per the
> user, #12).

So the single held attempt became a short **ladder**, most valuable hold first: (1) everything
reservable, (2) creatures only, (3) depletion lands only, (4) unrestricted. Rungs 2–3 exist only when
BOTH classes are non-empty, so a deck with no dork — or no depletion land — makes exactly the same
single attempt as before (byte-identical, no extra solve). Creatures outrank a depletion counter: the
counter is mana either way, the creature is a body.

Measured (smoke, seed 1001, loss-penalized avg win turn, lower better; the trick-target changes of the
same session isolated out with `MTG_NO_DORK_RESERVE=1`): d0 **hinata −0.009, mirrorwing −0.017,
antilife +0.007**; searched **fivecolour −0.007 (d3) / −0.013 (d5), mirrorwing +0.020 (d3) / +0.013
(d5)**; slivers / burn / treasure_hunt / knights / dragonstorm / auras / goblins byte-identical (no
mana dork, or none ever reservable). Net across the suite is a small improvement, carried by d0 — the
depth with no budget, i.e. the honest read on the heuristic itself. `test/classify_turn_later.sh`
classifies 9 of the 13 searched slower games as budget **churn** (they recover at 4×/16×).

#### MEASURED (2026-08-17), NOT YET ADOPTED — the follow-up was in the BACKTRACKER, not the rank

The follow-up above guessed wrong about where the residual lived. `ManaSourceRank` was never the
gap: **`TapForCostBacktrackWorker` never consults it.** The backtracker walks its candidate sources
in raw **battlefield order** and is first-solution-wins (it taps `cands[0]` and recurses, so
`cands[0]` is spent whenever *any* payment containing it exists) — so on every payment the greedy
strands on, and on every ladder rung where the reservation releases its hold, which sources get
burned is decided by the order permanents happen to sit in. That is the actual mechanism behind the
user's "the dork was tapped instead of a Mountain, for no reason whatsoever" (viewer issues #1/#9/#12).

Two variants, behind a temporary `MTG_TAP_CAND_ORDER` selector (0 = baseline, byte-identical):

| | smoke s1001 | regression s2002/3003 | **held-out** s4004/5005 | total |
|---|---|---|---|---|
| **1 — mana creatures last** | **−0.0303** | **−0.0410** | **−0.0517** | **−0.1230** |
| 2 — full `ManaSourceRank` sort | +0.0050 | *(not run — lost on train)* | — | — |

Net avg win turn, negative = better, summed over every case in the tier. Variant 1 improves on all
three disjoint seed sets; no case regressed on smoke, one did by +0.0010 on regression (fivecolour
d0) and antilife came back +0.0043 on held-out (a wash: −0.0040/−0.0040/+0.0043 across the three).
References stayed at **0 play-drift, 0 enum-gap** (208 refs). It is also 11.4% *fewer* backtracker
nodes (115,790 → 102,618 on 12 mirrorwing games, `MTG_TAP_STATS`, deterministic) — the backtracker is
~1% of runtime, so that is a rounding error, not a reason.

**Variant 2 losing is the same result the flow-order work already got** — see
`flow-guided-tap-order.md`, where a colour-scarcity reorder of the same list measured +0.0140 and was
refused. Scarcity-first is the right rule for the *greedy* (spend the least flexible first) and the
wrong one here; "spare the body" is a different axis and it is the one that pays.

**Why it concentrates on mirrorwing** (−0.0213 / −0.0300 / −0.0480, an order of magnitude more than
any other deck): Zada / Mirrorwing Dragon copy a solo-target trick *for each other creature you
control*, so an untapped creature is worth two things at once — a copy target and an attacker. A
1/1 Elvish Mystic tapped for {G} forfeits its Gold Rush copy (+2/+2 per Treasure) **and** the swing
that copy was for. Decks whose dorks are only mana move far less (fivecolour −0.008, antilife ~0),
which is the shape you would predict and is why this is a real rule rather than a seed artifact.

Lossless per the heuristic skill's Rule 0b infinite-budget test: it is a permutation of the DFS
candidate list, not a cap — the backtracker still descends into a creature when the payment needs
one, so no tap set becomes unreachable.

ADOPTED (`efa341ab`, GT `4e4c2a02`): the creature-last stable partition ships default-on behind
`MTG_NO_TAP_SPARE_CREATURES`, skipped while `g_flow_order_live` so it cannot undo the parked flow
permutation. The off-switch is byte-identical to the pre-adoption GT.

#### FOUR REFINEMENTS MEASURED AND REJECTED (2026-08-17) — do not re-derive these

All four came from the user's own counterexamples to the shipped rule, and all four measured worse.
Deltas are vs the ADOPTED baseline above, net avg win turn summed over the tier (negative = better).

| variant | smoke | regression | held-out | verdict |
|---|---|---|---|---|
| `ManaSourceRank` as a SECONDARY key (scarcity within the land group) | +0.0303 | — | — | rejected |
| spare only where the deck's `ShouldAttackWith` says it would attack | −0.0444 | **+0.0180** | −0.0200 | **rejected — play-drift** |
| ...the same, but a board-visible COPY MAGNET spares everything | −0.0434 | — | — | rejected (never beat the simpler arm) |
| user's tiering: inflexible lands < creatures < flexible/depletion/storage | +0.0470 | — | — | rejected |

**The `ShouldAttackWith` arm is the interesting failure and the one worth understanding.** The idea
is right on its face — a creature that adds no damage to this turn's attack can be tapped freely, and
it composes beautifully (`GenericProvider` returns true always, so a deck with no attack rule keeps
sparing everything, and Hinata / FiveColour / Anti-Lifegain each already carry a dork-holding rule it
would inherit). It won big on smoke. It then **reversed on the regression seeds, including on
mirrorwing itself** (−0.0444 smoke / +0.0100 regression / −0.0150 held-out — sign-inconsistent, the
overfit signature), and it broke a reference:

```
Anti-Lifegain/claude_s5_gi4.json: replay win_turn=5 vs ref win_turn=4
```

That is **the same reference, broken the same way, as the flow-order scarcity bias** (see
`flow-guided-tap-order.md`, which fixed it by ranking a live drip source maximally scarce). Freeing
the 0-power Ignoble Hierarch to tap means the payment stops reaching for Grove of the Burnwillows —
and Grove's drip, under a Tainted Remedy, IS the damage that wins on turn 4. So on Anti-Lifegain
**"spare the body" and "fire the drip" are the same decision**, and any refinement that separates
them — however correct it is about combat — throws the win condition away. Two references have now
been lost to exactly this mechanism; a third attempt should expect it.

Two structural facts that bound the remaining space, both checked rather than assumed:

* **No deck in the repo holds both a storage land and a mana creature** (Dragonstorm has Dwarven Hold
  + Mercadian Bazaar and zero dorks; slivers has the only {C}-manland and zero dorks). The
  storage-vs-creature ordering is unreachable today and cannot be measured here.
* **Depletion vs creature IS live, on exactly one deck** — Mirrorwing runs Sandstone Needle alongside
  Elvish Mystic and Ignoble Hierarch. `ManaSourceRank` ranks a depletion land by COLOUR (Needle =
  mono, 10) on the reasoning that depletion is ramp you normally want to spend, so the shipped
  partition will burn a counter ahead of a dork. Overriding that to 55 is the fourth row above, and
  it lost. Note the tension in the source material: viewer issue #9 asked for precisely the shipped
  behaviour ("the Hierarch tapped, when we should use the Sandstone Needle instead"), while the
  general principle argues the other way. The measurement sides with issue #9.

The pump-waste is otherwise a VIEWER-only gap (the AI's `FindBestOwnAttacker` is tap-aware: resolved
after payment, `CanAttackFull` skips tapped, so it never wastes its own pump — see
`docs/design/scenario-harness.md`). The precise per-target reservation (reserve exactly the human's
chosen creature, tapping a different one) would need the pump-target decision moved BEFORE the turn's
payment — a `--choices` reorder that breaks pump references — so it's deferred in favour of the
greatest-power heuristic above, which covers the common case (you pump your best attacker).

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
