# Sac-for-mana outlets: model them as a LAST-RANKED MANA SOURCE, not a searched action

**Status (2026-10-01): BUILT 2026-09-18, DEFAULT OFF, and its A/B IS RUNNING FOR THE FIRST TIME.**
See §"WHY THIS WENT 13 DAYS UNMEASURED" at the bottom — the short version is that the A/B manifests
were written and never executed, and the lever was then carried as *"believed measured"*.

USER, 2026-10-01, restating the original request and in effect re-asking for this:

> *"To be clear, Utopia Mycon mana should be able to produce black if needed."*
> *"But I'm not worried about presumptively holding black. We should be able to just do it on
> demand. (i.e. sacrifice a saproling or tap a land)"*

That is precisely what this lever does, and it is the reason it SUPERSEDES the colour-fan narrowing
in `sac-pool-colour-gate.md`: with `MTG_SAC_OUTLET_PAY` on there is no float colour to commit, hence
no colour fan, no singleton gate, and no speculative-black question at all — the payer makes black
when a cost needs black.

**The in-flight run (2026-10-01):** `logs/victim_ab/pay_ab.json` → `logs/victim_ab/pay_ab.out`,
72 jobs / 15,600 games, ONE pooled batch, three arms pinned PER JOB so the process default is
irrelevant:

| arm | flags |
|---|---|
| `base` | `MTG_SAC_POOL_TURN_COLOR=false` — the pre-2026-10-01 baseline |
| `tc` | `MTG_SAC_POOL_TURN_COLOR=true` — the colour narrowing alone |
| `pay` | `MTG_SAC_OUTLET_PAY=true` + the above — this lever |

Decks: Fungus `candidate-b-2026-09` **and Goblins** (Skirk Prospector is the same card shape, and
Goblins is the one of the two in the regression suite, so it is the real gate). Cells d0 / d3 b10 /
d5 b20, 4 paired seed blocks each, seeds spaced by `games`.

**Read it against the bar below: avg win turn must not regress (any regression is disqualifying),
and `units` is the cost metric** because units are deterministic and so immune to the shared box.
**Built-in control:** Skirk pins `{R}`, so its fan is already a singleton — Goblins `base` and `tc`
must come out IDENTICAL. If they differ, the colour narrowing is mis-scoped, not the pay lever.

**If Fungus comes back quality-negative, suspect §"STILL MISSING: the plan-added fodder credit"
FIRST, not the ranking.**

A `heurarm` slot rather than a bare env flag so both arms ride ONE pooled batch (an
`EnvOn` static can only ever BE one arm, which forces the per-arm wave CLAUDE.md forbids). The OFF
arm is byte-identical: smoke `configs changed: 0`, `play-changed=0`, scenarios 100/100.

Read §"AS BUILT" below before changing any of it — the implementation makes three choices the
proposal did not anticipate, and it uncovered a class of latent defect that is worth more than the
feature.

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

---

# AS BUILT (2026-09-18) — "§2b"

Named §2b because it is the second instalment of §2a (`TreasurePaySourceEnabled`, "one-shot lump
sac sources as PAYMENT sources"), and shares its machinery and its contract.

## The structural difference from §2a, and what falls out of it

A Treasure **is** the source: it taps, it dies, one permanent. A repeatable outlet is a permanent
that eats OTHER permanents. So:

> **The payment source is each piece of legal FODDER, priced at the outlet's yield. The outlet is
> only the permission.**

N Saprolings under a Utopia Mycon are N one-mana rainbow sources; N Goblins under a Skirk
Prospector are N `{R}` sources. That single reframing is what let this reuse the whole §2a path
instead of inventing a repeatable-activation concept inside the mana solver.

## Where it plugs in

| piece | what |
|---|---|
| `SacOutletPayEnabled()` | the lever (`SpellEffects.h`) |
| `LiveSacPayOutlet(state, controller)` | the board's outlet, resolved **once per payment** |
| `IsSacPayFodder(p, def, outlet)` | subtype filter + "Sacrifice ANOTHER" |
| `SacPayOutletColors(params)` | pinned `{R}` vs "any color" — same shape as `EffectiveProduces` |
| `SacPayFodderRank(...)` | **base 500** — behind every real source (defaults top out at 62) |
| `Permanent::pay_sac_eaten` | the in-flight mark; `CommitPaySacSacrifices` does the real sacrifice |
| `GameState::deck_has_sac_mana_outlet` | per-game presence gate, stamped in `StampDeckTraits` |
| kind 5 in `TapForCostSharedOnce` | the payer branch that eats |
| `CollectActions` | **suppresses** the mana outlet's searched actions — this is the cost win |

The four supply scans that must agree with the payer or they prune payable lines — `AvailableManaPool`,
`AvailableManaPoolNoAttackers`, `BuildNonCreaturePool`, `BuildColorFeasibility`, `ComputeAvailableColors`
— all credit fodder, and so does `UntappedManaUpperBound`. That last one is **load-bearing, not
bookkeeping**: `PaymentManaCovers` turns a short bound into a *proof* of unpayability and refuses
the payment before the greedy runs, so a bound blind to fodder would reject exactly the casts this
lever exists to enable.

## Three choices the proposal did not anticipate

**1. TAPPED bodies are fodder.** Sacrificing is not tapping, and *attack, then eat the attackers
post-combat for mana* is the Skirk line the Goblin deck is built on. The payer's source loop
short-circuits on `p.tapped` — that had to become `p.tapped && !sac_outlet.valid()`, which keeps
the historical short-circuit (including the `LookupCached`) when the lever is off. Within fodder,
`SacPayFodderCostsAttack` then defers a body that still *has* an attack to give, which is the
engine-side expression of the user's "prioritizes saprolings with summoning sickness".

**2. GREEDY PATH ONLY.** The backtracker memoises payments in `g_mana_cache` as a set of tapped
battlefield ordinals, and an eaten body is not a tap — a cached entry could not replay it. Fodder
ranks last, so the greedy reaches it whenever it is the answer; a cost only the backtracker could
assemble simply does not see fodder. Pessimistic, i.e. the safe direction: a payable cast fails
rather than an unpayable one resolving. (The legacy `MTG_TAP_LEGACY` 4-step path is likewise not
covered; it is an A/B baseline, not a ship path.)

**3. The commit is a REAL sacrifice.** Eaten fodder goes through `SacrificePermanentAt` —
graveyard, sacrifice watchers, death triggers — not the bare erase a Treasure gets, because
Pashalik Mons pings on a Goblin death and Rundvelt Hordemaster impulse-exiles, and the searched
`SacForMana` action this replaces fired both. That is correct, and it is also what made the whole
thing explode; see below.

## THE REAL FIND: a payment that sacrifices a creature MUTATES ZONES

The engine's payment layer rests on an unwritten invariant — **a mana payment does not change the
zones** — and essentially every caller relies on it. Making a payment sacrifice a creature breaks
that invariant comprehensively: the body moves to a graveyard, watchers fire, tokens are created,
cards are pushed into hand.

Turning the lever on segfaulted the batch. `perf`/gdb only showed the *symptom* (a corrupt free
inside `PoolAllocator`), so this was settled with an **ASan build** (a deliberate separate cmake
route, `build/Asan`, run with `MTG_POOL_ALLOC=0` so the custom allocator does not hide the poison).
ASan named the site in one run. Three defects, **two of them pre-existing and latent under §2a**:

| site | defect |
|---|---|
| `TurnSolver.cpp` `apply_one` | held a **hand iterator** across the cast's own payment. Eating a Goblin fired Rundvelt Hordemaster's dies-impulse, which `push_back`s into that same hand and reallocates it — so `it->m_is_staged` and `zone.erase(it)` ran on freed memory. Fixed by re-finding the copy by its stable per-copy `m_number`. |
| `AnimateLandsShared` | range-`for` over `state.battlefield` writing `p.is_animated` **after** a payment that can erase. Pre-existing: a cracked §2a Treasure does it too. Fixed to index + re-find by number. |
| `ActivateTapTokensShared` | cached `bf_size` across a payment that can shrink the battlefield — an out-of-bounds write on the `tapped = false` rollback. Pre-existing, same cause. Fixed to a live bound. |

And one in the new code: `CommitPaySacSacrifices` used a `static thread_local` scratch vector, which
a death trigger reaching another payment re-enters and clears underneath the loop walking it. Now a
plain local.

**The standing lesson, which outlives this lever:** a mana payment is not side-effect-free, and the
set of callers holding a zone iterator, a battlefield reference or a cached index across one is not
knowable by reading. ASan is the tool; guessing is not. The §2a note *"No index survives a
successful payment"* states the contract correctly — it was simply not being honoured.

## STILL MISSING: the plan-added fodder credit (stage 2)

The user's load-bearing requirement — *"We do need to count saprolings (or goblins) that are added
to the battlefield during the plan"* — **is not implemented.**

At APPLY time it happens to work: the plan applies sequentially, so a spore activation earlier in
the plan has already put its Saproling on the board when the payer runs. The hole is at
**ENUMERATION** time: every supply scan above reads the plan-start battlefield, so a subset whose
mana comes from a body the same subset creates scores as unpayable and is never offered. That is
exactly the Doubling-Season-off-an-activated-Saproling line §3c of
`fungus-second-main-and-devour.md` is built on.

The failure is invisible in aggregate — the line does not error, it simply is never chosen, and the
deck merely looks a little slower. If the A/B comes back quality-negative on Fungus, **this is the
first thing to suspect**, not the ranking. `SubsetOversubscribesSacFodder` already bails out
entirely when a co-selected action can add a matching creature, and Melira Pod (where persist
*returns* a sacrificed body) is the regression case that catches a naive static count.

---

# WHY THIS WENT 13 DAYS UNMEASURED (recorded 2026-10-01, at the user's question)

Worth writing down because the failure is procedural, not technical, and it is repeatable.

**There was never a technical blocker.** The lever has a `heurarm` slot, so it pools cleanly — which
is why its first real A/B could be launched as 72 jobs in a single batch. (Contrast
`MTG_FORCE_USES_M2`, which genuinely *could not* be measured in a pooled batch because
`GoldFishRunner.cpp` read it as a process-wide `static const bool`. That one had an excuse.)

**The A/B was planned and then skipped, inside three minutes.** Timestamps:

| when | what |
|---|---|
| 09-18 00:30 | `logs/sacpay_ab/manifest.json` written |
| 09-18 01:48 | the lever commits (`d81195f7`) — scenarios 100/100, smoke byte-identical OFF, ASan clean ON over 250 Goblins + 100 Fungus games |
| 09-18 01:51 | `logs/sacpay_ab/holdout.json` written |
| 09-18 02:05 | next commit is condemnation perf — the session had moved on |

Both of those files are **manifests only**: a `jobs` array of 16, and no `avg`, no `digest`, no
`played=` anywhere in them. Verified again 2026-10-01, they are still results-free. So the careful
work went into *correctness* (and found three real zone-mutation bugs, two of them live under §2a)
while the cost/quality question the lever exists to answer was never asked.

**Then it was carried as "believed measured."** The existence of `holdout.json` was mistaken for its
results. Caught 2026-09-18 11:08 while sequencing the Fungus value-leaf freeze, and recorded as the
memory *"open the artifact before believing a measurement exists."*

**And then the parking was legitimate, which is why nobody came back.** The freeze rule is that every
open play lever must be *settled* first, and *"this ships OFF for now"* is a complete way to settle
one. So it was settled OFF, the freeze proceeded correctly, and the measurement simply stayed
undone. It surfaced once more on 2026-09-24 — but only to verify it does **not** fix the same-line
fodder hole (`sac-fodder-created-in-the-same-line.md` §"root cause"), a narrow correctness check
rather than the A/B.

## The gap this exposes, which is the transferable part

**A lever parked as "ships OFF pending measurement" is indistinguishable in the tree from one that
was measured and REJECTED.** Both read as a default-OFF flag with a long justifying comment at the
read site. Nothing in the comment, the flag registry or the doc header separated the two, so every
later reader — including three of my own sessions — saw a settled decision where there was an unpaid
debt.

**So: a read-site comment for a default-OFF lever must say WHICH of the two it is.** "Default OFF,
UNMEASURED — A/B pending" and "default OFF, measured and rejected (evidence: ...)" are different
facts and must not share a phrasing. `MTG_FUNGUS_SAC_DRAW_CLOCK` was on the same 2026-09-18 list
("built and measured, adoption not yet decided") and is worth auditing for the same ambiguity.
