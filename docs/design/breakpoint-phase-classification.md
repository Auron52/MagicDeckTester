# Breakpoint-phase classification: single-consideration across a mid-turn draw

**USER directive (2026-08-17, verbatim):**
* "I don't want greedy. I want the solution I designed where we order and decide when things can
  and cannot be cast so that they are almost always considered once."
* "Just as we do for main 1 and main 2 we should do for breakpoints."
* "Now, to be fair, I should be clear that new spells added to the hand have no such limitation."
* "If they are new, then they cannot be treated as second-class citizens."
* "For example if the order was Ponder -> Preordain and you had no Ponder in hand you wouldn't be
  able to ignore the Ponder that Preordain drew."

This is step 3 of the single-consideration arc (`single-consideration.md`). Step 1 was main-phase
classification: the m1/m2 partition gives each spell ONE phase, so "cast now vs after combat" stops
being asked twice a turn. Step 2 was canonical cantrip ordering. This is the same partition applied
across a **mid-turn breakpoint** -- the point where a cantrip resolves and the engine re-decides.

## The multiplicity

`EnumerateBreakpointPlans` (TurnSolver.cpp:19533) runs the **full** `EnumeratePlansWithLand` on the
post-draw state. Every spell already considered alongside the cantrip in the base plan is therefore
considered AGAIN in every continuation. There is no partition: the breakpoint is the one decision
boundary in the engine that re-asks the whole question.

Measured cost of that, on `hinata_overnight_d3_s4004 gi=5` (seed 4009), which is the exemplar of the
5->8 family in `classify-stack-adoptable-subset.md`:

* Enabling the plain-cantrip breakpoint class (`MTG_BP_SITES=63`, bit 3) takes the unbounded search
  wall from **7.1 s to 22.4 s** -- and the cost is flat in the continuation width W (W=1 22.4 s,
  W=2 19.8 s, W=3 21.0 s), so it is not the width, it is re-asking the question at all.
* `MTG_BP_CANDS_PROBE=1`, one game: the cantrip site sees **3,307 searched breakpoints**, mean 3.84
  continuations, max 40, **45.6% capped** at W=2 and **57.8% of all continuations rank-gated out**.
* At the suite's budget (hinata d3 runs at `budget_ms 10`, the tightest case in the suite) that 3x
  cost is a lost turn: base wins T5, the lever wins T8. It is TRUNCATION, not mis-ranking -- the
  lever alone returns to T5 at budget >= 100 and stays there to unbounded. (This refutes, for this
  configuration, the 2026-07-31 note at TurnSolver.cpp:2705 that the class is "mis-RANKED, not
  merely mis-afforded"; that was measured under mask 0x1F with nesting off.)

## The rule

> In a breakpoint continuation, drop a hand cast **iff** (a) that card was already in hand before
> the breakpoint, and (b) it is payable from the pool as it stands at the breakpoint, before any
> new land.

Everything else is kept: a card **drawn** at the breakpoint, and anything that only becomes castable
once the new land is down.

**Why it is exact and not a heuristic.** If X was in hand and payable without the new land, then for
every land L the base plan `{play L, cast cantrip, cast X}` was payable from the same pool and was
therefore already enumerated -- the continuation's copy is a true permutation duplicate, and the
apply's `CastOrderRank` decides the order. This is the same argument as the cantrip-ordering lossless
guard. The multi-card case is exact for the same reason: the base needed `cantrip + X + Y` payable
from `pool + L`, and the continuation needs `X + Y` payable from `pool + L - cantrip` -- the identical
inequality, so neither world can form a subset the other cannot.

Note what the rule does NOT need: a snapshot of the pre-breakpoint mana. At collection time inside a
continuation the state is already post-draw and pre-land, so "payable from the pool as it stands" is
just the current untapped pool. Only the pre-draw **hand** has to be captured.

## New cards are first-class -- two consequences

**(1) They are exempt from the drop.** A drawn card was never considered pre-breakpoint, so it is not
a duplicate of anything. This is the whole reason the partition is safe to make aggressive.

**(2) They are exempt from the canonical-ordering ban, and today they are NOT -- a soundness bug.**
`CantripOrderBans` (applied at TurnSolver.cpp:3643) suppresses a canonically-earlier cantrip inside a
continuation on the argument that the earlier-first chain was already enumerated. That argument holds
only if the banned cantrip was in hand when the chain started. The USER's case: cast Preordain with
**no Ponder in hand**; Preordain draws a Ponder. The Ponder->Preordain twin chain was never
enumerable, so banning that Ponder deletes a real line rather than a duplicate. The ban must
therefore fire only for cards present in the pre-breakpoint hand -- the same snapshot the partition
needs, which is why one hook serves both. (`single-consideration.md` already lists "a drawn-card
exemption" as a known extension of Collapse #3; this is that extension, and it is a correctness fix,
not a tuning knob.)

**(3) The partition is what BUYS them their standing.** "New cards cannot be treated as second-class
citizens" is not satisfied by exempting them from filters alone: with 57.8% of continuations
rank-gated out at W=2, a continuation featuring the drawn card is routinely unreachable no matter how
it is classified. Shrinking the list to "the new card plus what the land enabled" is what makes W
stop binding. The success criterion for this work is therefore not only wall clock -- it is
`MTG_BP_CANDS_PROBE`'s `unreachable` falling toward zero on the cantrip site.

## Implementation shape

Mirrors the main-phase filter (`CollectActions`, TurnSolver.cpp:5909: a Main2-classified cast is
`remove_if`'d from the pre-combat enumeration and re-offered once, post-combat).

* **Snapshot**: the card numbers (`Card::m_number`) in the active player's hand, captured where the
  breakpoint site is armed -- i.e. BEFORE the draw resolves. Rollout arming sites:
  TurnSolver.cpp:9803, 10193, 10464, 10577, plus the resume path at 11022 and the capture at 11168.
  Executor arming sites: AIEngine.cpp:2633, 2774, 2863.
* **Binding**: extend `TurnSolver::CantripOrderScope` (TurnSolver.h:460) to carry the snapshot
  alongside the site, so the existing lockstep pair (ApplyPlanDirect 11174 / resolve_draw_breakpoint
  2524) binds both in one place and cannot drift.
* **Application**: in `CollectActions`, beside the existing cantrip ban -- drop a `CastFromHand`
  whose card number is in the snapshot and whose cost is payable from the current pool.
* **Cache**: `EnumerateBreakpointPlans` already folds the bound site into its key
  (TurnSolver.cpp:19559); the snapshot changes the emitted list too, so it must be folded as well.
* **Lever**: `MTG_BP_CLASSIFY`, default OFF -> byte-identical, and an `UnprunedGate` so an unbounded
  A/B can open it. The drawn-card exemption to `CantripOrderBans` rides the existing
  `MTG_CANTRIP_ORDER` lever (it only ever makes that lever less aggressive).

## Success criteria

1. Off: byte-identical (smoke 36/36, 0 configs changed).
2. On, with `MTG_BP_SITES=63`: `unreachable` on the cantrip site falls toward zero, and the
   unbounded-budget wall for seed 4009 gi=5 falls back toward the 7.1 s baseline.
3. The 5->8 family (seeds 4009, 4062, 5039, 5099, 7172, 7227, 7262) recovers WITHOUT the
   `MTG_BP_W0_SITES` cost prune, which is the greedy dodge this design replaces.
4. Suite train, then held-out overnight, before any adoption claim.

---

## MEASURED (2026-08-17, first build) -- the partition works; it does NOT pay for itself yet

**Criterion 1 -- MET.** Lever off: smoke 36/36, **0 configs changed**. The snapshot capture is
gated by `TurnSolver::BreakpointHandSnapshotWanted()`, so a ship config does not even build the
vector on the cast hot path.

**Criterion 2 -- HALF MET.** `MTG_BP_CANDS_PROBE`, one game (seed 4009 gi=5, four levers), cantrip
site:

| | classify off | classify on |
|---|---|---|
| searched breakpoints | 3,307 | 2,361 |
| mean continuations | 3.84 | **2.47** |
| max continuations | 40 | **13** |
| **rank-gated OUT** | **7,351 (57.8%)** | **2,291 (39.3%)** |

A 69% cut in unreachable continuations, and the longest list falls from 40 to 13 -- so a drawn card
is materially less of a second-class citizen. But `unreachable` is not near zero, so the width still
binds on the remaining lists.

**Criteria 2 (wall) and 3 -- NOT MET, and the reason is structural.** Unbounded wall for that game
is UNCHANGED: 18.6 s off, 18.5 s on (baseline with the class disabled entirely: 7.1 s). Nesting is
not the residual either -- `MTG_BP_NEST_DISCOVER=0` gives 18.1 s / 18.1 s. The family recovers
2 of 7 (gi=5 8->7, gi=94 8->6); the other five are unchanged.

**Why.** The filter lives in `CollectActions`, which runs INSIDE
`EnumerateBreakpointPlans -> EnumeratePlansWithLand`. So the continuation enumeration is paid in
full and the partition only shrinks its OUTPUT. The duplicates are no longer *offered*, but they are
still *computed*. This is exactly the distinction `single-consideration.md` draws in its ranked next
collapses ("the right route ... **deletes the calls instead of caching them**").

**Next, and it is the real step 3:** hoist the partition above the enumeration. A breakpoint whose
post-draw hand contains no new castable card and no land-enabled cast has NO continuation that is
not a permutation duplicate -- that breakpoint should not enumerate at all, rather than enumerate
and then discard. The snapshot needed to decide that is already bound (`g_bp_hand_before`), so the
test can be made before `EnumeratePlansWithLand` is called. Expected shape: `n` (searched
breakpoints) falls far below 2,361 and the wall moves toward 7.1 s.

Nothing here is adopted; `MTG_BP_CLASSIFY` is default off and byte-identical off.
