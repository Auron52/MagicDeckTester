# Flicker combo — the mechanic set

Reference for the engine additions that make an **infinite-mana blink loop** work
(`EldraziDisplacerFlicker`). Per-deck findings and the run's open items live in
[analysis-EldraziDisplacerFlicker.md](analysis-EldraziDisplacerFlicker.md); this file is the
mechanic-level design, for anyone adding another card that touches one of these axes.

## The loop

```
    repeat:
      1. fire the DAMAGE SINK      (Shivan Gorge: {2}{R},{T}: 1 damage to each opponent)
      2. TAP AHEAD                 (tap up to N lands into the turn-scoped float)
      3. pay the OUTLET            (Eldrazi Displacer {2}{C} / Emiel {3}; no {T}, repeatable)
      4. BLINK the PAYLOAD         (exile + return -> both ETB cascades re-fire)
           -> Peregrine Drake's "untap up to five lands" refunds step 2
           -> Emiel's watcher puts a +1/+1 counter on the returning creature
```

Net mana per iteration = (yield of the top-N lands) − (activation cost). Positive ⇒ unbounded.
The whole loop is one shared driver, `ApplyBlinkLoop` (`src/core/SpellEffects.h`), called
identically by the rollout (`TurnSolver::ApplyPlanDirect`) and the executor (`AIEngine::TakeTurn`),
so the realised line is the scored line.

## Four things that are easy to get wrong

**1. The cost on the Action is ONE activation, not K pre-scaled.**
Every other K-count activation (`ActivateRevealTop`, `ActivatePump`) multiplies its cost by K at
enumeration. That is right for an ability that only spends. It is wrong here, because the loop is
**self-funding**: iteration k+1 is paid for by iteration k. Pre-scaling would price a 20-iteration
go-off at 20× its entry cost and prune the only line the deck wins with. The apply loop pays per
iteration and **breaks** when one cannot be paid, so an over-large K costs a wasted branch, never
phantom mana.

**2. Without the TAP-AHEAD the untap refunds nothing.**
"Untap up to five lands" on a board that is already untapped is a no-op. The manoeuvre is the one
Reality Spasm's literal-untap model introduced (`RitualTapAheadIntoFloat`): tap first, into the
turn-scoped float, *then* let the resolution untap them. Applied at all four sites a cast's cost is
paid — enumeration's sequential-payability probe, the rollout apply, the executor, and the human
trial-pay — or the executor realises a different board than the plan priced.

**3. The untap order is by YIELD, and that makes a low-yield SINK unreachable.**
`EtbUntapLands` untaps the highest per-tap yield first, which is correct for mana and exactly wrong
for Shivan Gorge: it makes `{C}`, so on a board of Overgrowth'd lands it never makes the top five
and the loop untaps it **never**. Measured: the go-off was recognised on 708 nodes of a 3-game
sample and never executed as a kill. `g_etb_untap_priority` (a scoped thread_local, set only by
`ApplyBlinkLoop`) promotes the sink above the yield order. The sink must also fire **before** the
tap-ahead, or the tap-ahead taps it for its one `{C}` first.

**4. A `{C}` PIP is not a colour.**
`ManaPool::wild` ("one tap of a multi-colour land") used to cover a colourless deficit, so a Fertile
Ground's "add one mana of any colour" projected as able to pay `{2}{C}`. The payment path always
refused it; only the projection was permissive — the classic over-acceptance that surfaces later as
`[fd-diverge]`. `ManaPool::wild_c` is a **subset count** of `wild`: how many of those units come
from a source that can also produce `{C}` (a Yavimaya Coast, not a Fertile Ground).

## Cost reduction on ACTIVATIONS is a new axis

`EffectiveSpellCost` handles seven reducers and every one is keyed on the card being **cast**.
Training Grounds reduces an **activation** cost, which nothing did before, and it differs in two
ways worth remembering:

* the floor is **one mana**, not zero (every spell reducer floors the generic half at 0);
* it reduces the **generic** half only, so `{2}{C}` → `{C}` — the colourless pip survives, and the
  Displacer still needs a real `{C}` source even under two Training Grounds.

`EffectiveActivationCost` must be applied at **every** site an activation cost is read: enumeration,
the rollout pay, the executor pay, and the human trial-pay. Miss one and the planner prices a line
the payer cannot buy, or the reverse.

## Land Auras: the first "another permanent changes my yield"

Every other yield modifier in this engine (`produces_amount`, `storage_land`, `domain_mana`, the
scaled dorks) is intrinsic to the source's own `CardDefinition`. A land Aura's bonus belongs to the
**aura**, so it can only be found by looking at what is attached to the permanent — which is why
`AddSourceToPool` gained an optional `const Permanent*` (it previously took only the def and
therefore could not see it). `PermanentManaYield` folds the bonus in, because that is the honest
number for every yield RANKING; `AddSourceToPool` then subtracts it back out and credits it
separately, in the aura's own colour, so an Overgrowth's `{G}{G}` is not laundered into the host's
wild.

Attachment reuses `Permanent::aura_attached_to` unchanged (a stable `m_number`, not a pointer). The
creature-only restriction lives in exactly two predicates, both now branched on `is_land_aura`:
`LegalEnchantTargets` and `ResolveEnchantTarget`.

## The go-off heuristic

`EldraziFlickerProvider` recognises an assembled loop from the **board only** (never a card in
hand), prices activations through `EffectiveActivationCost`, and proposes ONE extra count candidate
sized to whichever sink exists — Gorge damage, Emiel counters, or an `{X}` draw. With **no** sink it
proposes nothing, because unbounded mana a goldfish cannot spend is pure cost; that single clause
took the heuristic's overhead from +87% to +29%.

`ExtraLethalDamage` is only ever an **addend to a projection**. Execution stays the arbiter, and
both go-off short-circuits re-simulate with `ApplyPlanDirect` before believing a lethal — so an
over-claim can mis-rank a plan but cannot report a win the game did not produce.
