# Sac-for-mana outlets: model them as a LAST-RANKED MANA SOURCE, not a searched action

**Status:** proposed, not built. Deferred here per the CLAUDE.md rule that deferred work lives in
`docs/design/`.

**Origin — user, 2026-09-17**, on Utopia Mycon and then generalising it:

> *"it's not clear to me whether we really need to search the sac ability for mana generation.
> Theoretically we could just treat it as a mana source with the highest level of deferral. i.e. It
> is only used if absolutely necessary and prioritizes saprolings with summoning sickness."*
>
> *"Honestly, it might make sense to do it this way for Skirk Prospector in Goblins as well. That
> is a very similar case."*
>
> *"So the idea would be to only spend the creatures when absolutely needed for our line."*

## The two cards are structurally identical

| card | deck | params |
|---|---|---|
| **Utopia Mycon** `{G}` | Fungus | `sac_creature_outlet`, `sac_creature_requires_subtype: Saproling`, `sac_outlet_add_mana_any_color`, `amount 1` |
| **Skirk Prospector** `{R}` | Goblins | `sac_creature_outlet`, `sac_creature_requires_subtype: Goblin`, `sac_outlet_add_mana_color: R`, `amount 1` |

Same shape; only the colour (any vs `{R}`) and the required subtype differ. Whatever is built here
serves both, and any future "sacrifice a creature: add mana" card for free.

## What the engine does today

They are emitted as **searched actions** in `CollectActions` (`src/ai/TurnSolver.cpp` ~15570-15730):
one action per activation count K, carrying `a.ritual_float = sac_outlet_add_mana_amount` and a
float colour, which `Solve` then credits. The *victim* is already heuristic, not searched — the
shared expendability ranking picks one canonical body, precisely because one-action-per-victim
"makes the O(2^candidates) subset search explode on a wide Goblin board / Krenko tokens and hangs".

So the search branches over **how many creatures to eat**, on a deck whose whole plan is making
dozens of fungible bodies. That is a branching axis paid on every enumeration.

There is already a note in the same function that these belong on the payment side —
*"Skirk's MANA outlet ... floats mana and needs the mana-solver float path (separate follow-up)"* —
so this direction is not new, just unfinished.

## The proposal

Move them to the **mana-payment** side, ranked last:

* `DecisionProvider::ManaSourceRank` already exists for exactly this class of choice — *"mana-source
  TAP ORDER: flexibility rank of a mana source (LOWER = tap earlier) ... a QUALITY heuristic for a
  sub-decision the search does NOT branch over (searching tap orderings is the very blowup we
  avoid)"*. Defaults run basic/bounce 10, dual 20, filter 25, tri 30, rainbow 50, `{C}`-manland 60.
* A sac outlet ranks **far above all of them** (say 500). The greedy payer takes the lowest-ranked
  qualifying source first, so the outlet is reached **only when nothing else can pay** — which is
  the user's "only spend the creatures when absolutely needed for our line", expressed structurally
  rather than as a heuristic anyone has to tune.
* **Victim preference: summoning-sick bodies first.** A creature that entered this turn cannot
  attack, so eating it costs nothing this turn. Then tokens, then the existing expendability order.

### What this buys

1. **A whole branching axis disappears** — directly relevant to the Fungus search-cost work
   (`fungus-token-search-cost.md`), and to the Goblin boards the current code already had to
   hand-bound.
2. **The semantics get simpler and more correct.** "Only if necessary" is what a last-ranked
   payment source *is*; as a searched action it is something the evaluation has to rediscover.

   **The sharp form of that (user):** *"what that means is that we don't take lines that sacrifice
   more creatures than we want to — since the search punishes us with a slower win turn."* A
   payment source eats **exactly as many as the cost requires, and only when no other source can
   pay**, so over-sacrifice becomes unrepresentable rather than merely discouraged.

   **BUT NOTE THE PARENTHETICAL, because it sets the bar for this whole change.** The search
   *already* rejects over-sacrificing lines: eating bodies slows the clock, and the objective IS
   avg win turn, so those lines score worse and lose. **So this is primarily a COST change, not a
   correctness fix.** The searched model arrives at the right answer; it just pays a branching
   axis on every enumeration of a deck that makes dozens of fungible bodies to get there.

   That flips the risk direction, and the adoption bar with it. If the search is already right,
   a heuristic replacing it can only match or lose on quality — so the bar is **quality-neutral,
   cost-better**, and any win-turn regression is disqualifying rather than something to trade
   against the speedup. Do not sell this as a correctness improvement.

   (The related guards `SubsetWastesCreatureSacMana` / `SubsetOversubscribesSacFodder`, and the
   sac-outlet enumeration divergence four sweep agents found independently, are about the
   enumeration being *wrong* — offering fodder that does not exist. That is a different failure
   from over-sacrifice, though this change would retire the axis both live on.)
3. **It may retire a heuristic.** `GoblinsProvider::DeferSacOutletPreCombat` haste-gates Skirk's
   mana outlet and defers value outlets to main 2 — a prune that exists partly because the
   action-side model offers the outlet in states where it is pointless. A payment-side outlet is
   never offered pointlessly.

## The hard part — and it is a real one

A payment source can only sacrifice a body that **already exists**. It cannot run the two-step line:

> activate a spore ability to CREATE a Saproling, then sacrifice that Saproling for the mana that
> casts the spell

The user flagged exactly this as the ordering complication:

> *"Ordering-wise, though, this does make things trickier as we need to consider dropping saprolings
> using abilities prior to doubling season if Utopia Mycon is in play."*

This collides with the deck's other ordering rule (`fungus-second-main-and-devour.md` §3c step 2):
**Doubling Season should be cast before any activation**, so the tokens it doubles are doubled. But
if the Season is castable *only* by eating a Saproling you must first make, the order has to invert
— and the Saproling made pre-Season is **not** doubled. That undoubled token is the price of
casting the Season a turn earlier, and whether it is worth paying is a genuine trade.

**The resolution that keeps both:** the *creation* step stays a searched action (spore activations
already are), and the *sacrifice* step becomes a payment source. A plan that activates and then
casts the Season simply has one more Saproling on the battlefield when the payer runs, and the
payer picks it up. Nothing needs to look ahead. The ordering rule in §3c then needs relaxing from
"Season strictly first" to "Season first **unless** an activation is required to pay for it".

### THE REQUIREMENT THAT MAKES OR BREAKS IT

**User:** *"We do need to count saprolings (or goblins) that are added to the battlefield during
the plan, though."*

This is the load-bearing constraint, not a detail. The payer's fodder count must be taken over the
board **as the plan leaves it**, including bodies created *earlier in the same plan* — spore
activations, a Tukatongue death refund, a Mycoloth upkeep, a Krenko/Siege-Gang token on the Goblin
side. A payer that reads only the plan-start battlefield will under-count available mana, and the
whole activate-then-sac line — the one the Doubling Season case is built on — silently disappears.
That failure is invisible in aggregate: the line does not error, it just never gets chosen, and the
deck merely looks a little slower.

The engine has already been bitten from the opposite direction here, which is a good sign the
awareness exists but the accounting is fiddly: `SubsetOversubscribesSacFodder` (added 2026-09-17)
has to **bail out entirely** when a co-selected action can add a matching creature, precisely
because plan-added fodder makes a static count wrong. A first cut of that guard broke Melira Pod,
because **persist returns a sacrificed body** — one creature legitimately feeding many activations.
Whatever counts fodder for the payer has to handle the same replenishment cases, and Melira Pod is
the regression test that catches it.

Implementation note: this argues for the fodder count being derived from the same post-apply state
the plan's other effects are, rather than a separate pre-pass — a pre-pass is what both of the
above defects were.

## Risks and how to measure

* **It is a behaviour change, not a refactor.** Both Goblins and Fungus play digests will move; GT
  must be re-accepted after inspection, not regenerated. Goblins is in the regression suite, so it
  is the real gate.
* **Risk: under-use.** A last-ranked source is never tapped for *value* — e.g. eating a soon-to-be-
  devoured Saproling for mana you did not strictly need. Against a passive opponent that is
  probably right, but it is the thing to watch in the A/B.
* **Risk: the float is coarser.** Today's action floats mana that a plan can spread across several
  casts; a payment source is pulled per cost. Likely equivalent, but worth a digest diff rather
  than an argument.
* Measure per `heuristic-optimization.md`: train seeds, held-out validation, report before adopting.
  The headline numbers are avg win turn (must not regress) **and** enumeration/branching cost, which
  is the reason to do it at all.
