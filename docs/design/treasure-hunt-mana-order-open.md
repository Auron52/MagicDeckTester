# treasure_hunt mana ordering — two fixes landed, what remains

Written 2026-08-27 from a viewer session on `decks/treasure_hunt`; REWRITTEN 2026-08-27 after the
seed-8 investigation below overturned the first version's central claim. Recorded here rather than
in any agent's memory (CLAUDE.md) so it is available to whoever picks it up.

## Landed 1: floating mana may feed a ramp filter (adopted 2026-08-27)

Ferrous Lake is `{1}, {T}: Add {U}{R}` — a filter that needs a mana *input*. The payment path has
always spent floating mana on that `{1}`; `AddSourceToPool` only looked for another untapped
**permanent**, so on a board whose sole feeder was the floating reserve the pool credited the Lake
**zero** and every cast needing it was pruned before enumeration.

Found from a saved viewer artifact (`treasure_hunt` s3/gi2 T3, verdict `legal_not_enumerated`):
floating `{R}` + untapped Ferrous Lake casts Treasure Hunt `{1}{U}`, and none of the 24 enumerated
plans contained it. Rationale and the measurement live in the code
(`FloatFeedsRampFilterEnabled`, `src/core/SpellEffects.h`); pinned by
`test/unit/test_mana_payment.cpp` ("ramp filter: FLOATING mana feeds it…"), which fails on exactly
one assertion under `MTG_NO_FLOAT_FEEDS_FILTER=1` — the *pool* assertion, not the payment ones,
which is the asymmetry in one line.

## Landed 2: depletion tap order (2026-08-27) — and the CORRECTED seed-8 diagnosis

The first version of this doc mis-transcribed Throes of Chaos's cost as `{2}{R}` (it is **`{3}{R}`**)
and, from that, derived a phantom defect — "three sources tapped for a 3-mana cost with `{U}`
floating", flagged as an unexplained over-tap. With the real cost there was **no over-tap**: the
retrace needs 4 mana, and every observed board paid it with normal depletion over-produce. A
payment-path trace (temporary `MTG_TAP_TRACE`) established what actually happens on seed 8:

* The engine's T5 retrace payment was **exact**: Sandstone Needle (`{R}{R}`) + Saprazzan Skerry
  (`{U}{U}`) = 4 for `{3}{R}`, zero leftover. Not the defect.
* The real defect fired on **Fiery Islet's sac-to-draw `{1}`**: with a fresh Island available, the
  greedy tapped the last-counter Skerry instead — 2 produced for 1 needed, the land sacrificed, the
  spare `{U}` wasted. Cause: `ManaSourceRankBase` ranked a basic Island and a mono depletion land
  identically (10), and the strict `<` broke the tie by battlefield position (Skerry at index 0).
* That same tie is what made the user's line — *"play the Island and sacrifice in that order"* —
  unreachable: paying the `{1}` with the Skerry killed it, stranding the retrace that needed its
  mana, so the whole line dropped from enumeration.

What shipped (`DepletionTapOrderEnabled`, `src/core/SpellEffects.h`; off-switch
`MTG_NO_DEPLETION_TAP_ORDER` reverts both halves):

1. **Tier** (`ManaSourceRankBase`): a depletion land taps one slot past its plain-tier peers
   (mono 10→11, dual 20→21), same shape as the drip-land nudge. Ordering, not exclusion — a cost
   only it can pay still taps it.
2. **Tiebreak** (`TapForCostSharedOnce`, direct + feeder loops): among equal-rank depletion lands,
   **more counters tap first** (USER doctrine 2026-08-27). This preserves per-turn burst, not total
   mana: tapping the 2-counter copy leaves two lands = two taps next turn; tapping the 1-counter
   copy kills it for the same total.
3. **Viewer** (`AppendHumanPlayDigPlans`, human-play only, autonomous byte-identical): dig plans
   now fan over the land-drop options the base plans already carry, so
   `land=Island; sacrifice Fiery Islet to draw` is one pickable plan and the drop's mana funds the
   dig's cost. Verified on the seed-8 repro: Island pays the `{1}`, the draw fires, and the retrace
   is then paid exactly by Needle + Skerry.

Pinned by `test/unit/test_mana_payment.cpp` ("depletion tap order: …"), whose three tapped-pattern
assertions revert under the hatch. Structural blast radius: only decks holding a depletion land can
move — treasure_hunt, Dragonstorm, Mirrorwing Dragon (Sandstone Needle). Measurement is recorded in
the adopting commit.

### OPEN 1 — the OVERNIGHT tier's ground truth is STALE for treasure_hunt (now 3 decks)

**(Updated 2026-09-03: CLOSED — the overnight tier has been re-run and accepted many times
since, latest 3ffb1f09; no TH/dragonstorm/mirrorwing overnight debt remains.)**

Smoke and regression were rebaselined for the ramp-filter fix, and again for the depletion tap
order. **Overnight was deliberately not run** (user, 2026-08-27: *"We don't need to do overnight
yet"*). Until it is, an overnight run will report failures on treasure_hunt / dragonstorm /
mirrorwing that are these adopted changes, not regressions:

```
bash test/regression.sh --overnight            # inspect: expect diffs on the 3 depletion decks only
bash test/regression.sh --overnight --accept   # only after inspecting
```

Anything moving outside those three decks is a real finding and should not be accepted.

## OPEN 2 — a whole class of turn gets NO mana-source reservation

The whole-turn "leave it out if you can" reservations — depletion (`DepletionReserveEnabled`),
attacker, dork — live **only** inside `TurnSolver::BatchPrepayMainCasts`. That function fast-declines
on a turn with fewer than two eligible casts, and `possible` counts only `CastFromHand` and
`GarthActivate`:

```cpp
else if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land && !a.alt_cost)
{ ++possible; }
...
if (possible < 2) { return Pp(PP_FEW_CASTS); }
```

A **retrace** cast is from the graveyard and a **Land's Edge** discard is an activation, so a turn
made of those scores **zero**, the prepay declines, and payment falls through to the per-cast greedy.

**Status after the seed-8 correction: real, but demoted.** The board that motivated this item was
proven to be entirely the tap-order tie above — `MTG_NO_DEPLETION_RESERVE=1` produced a
byte-identical board (the reserve was inert, not wrong), and a prototype that lifted the gate
(`MTG_RESERVE_SINGLE_CAST`: count `CastFromGraveyard`, threshold 1) moved the probe histogram
(`declined: <2 casts` 100% → `PREPAID` 25%) while changing **nothing** on the board, because the
depletion hold is all-or-nothing per class: the turn genuinely cannot be paid holding BOTH depletion
lands, so the held attempt fails and falls through to the same greedy. The remaining exposure is the
narrow inter-cast shape: ≥2 casts where at least one is a retrace/activation, plus a cross-cast
colour conflict the (now depletion-aware) per-cast greedy misorders. A complete fix must **fold the
retrace cast's cost into `combined`** (the eligibility loop currently `continue`s past
`CastFromGraveyard`, so widening the gate alone would prepay only part of the turn) and/or give the
depletion class a partial-hold rung (the ladder has one for creatures — `MTG_DORK_HOLD_PARTIAL` —
but not for depletion). Measure on the full suite; it touches every deck's payment path.

## Sequencing note

The tap-order fix (Landed 2) was measured on its own before any OPEN 2 work, per the original
version's rule: two play changes bundled into one rebaseline are unattributable. OPEN 2, if picked
up, gets its own measurement — and should be re-scoped first, since the case that motivated it is
now closed.

Standing caution: this repo has measured three *"make the mana projection more accurate"* fixes that
ALL lost, with zero games better in any arm (`goblins-enabler-worse-games.md`). The two landed fixes
here are the other shape — restoring lines the search could not reach at all (a pruned cast; an
unenumerable dig order) — plus a user-doctrine ordering with a measured-neutral-or-better suite
result. OPEN 2 remains a re-ranking change, i.e. the side that lost last time. Measure before
believing.
