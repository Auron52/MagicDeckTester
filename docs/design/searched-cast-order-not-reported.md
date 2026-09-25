# A searched cast ORDER the viewer could neither see nor choose

**Status: FIXED 2026-09-25.** Reported by the user from the play viewer the same day.

> **This file was called `same-line-rock-mana-not-spendable.md` and its first diagnosis was wrong.**
> It blamed the committed payment for not crediting a mana rock the same line casts. That is not what
> happens here, and the refutation is recorded below rather than deleted — it is the second cheap
> hypothesis this bug survived, and both were plausible enough to build.

## The report

> "The mana usage is just bad in this line. It fails to keep the Peat Bog despite the fact that it
> isn't difficult to do so."

candidate-B Fungus, **seed 8, game-index 0, turn 3**. Reproduce with:

```
build/Release/mtg decks/Fungus/candidate-b-2026-09/Fungus.cod \
  --profile decks/Fungus/candidate-b-2026-09/Fungus.profile.json \
  --cards-json src/cards/data/cards.json --claude-play \
  --seed 8 --game-index 0 --max-turns 8 --depth 0 \
  --choices "1,4,-1,-1,12,-1,-1,<plan>"
```

(`logs/wild2/replay8.py` walks to that frame; `logs/wild2/s8ab.py` commits a plan matched by CONTENT
rather than index — see the pitfall at the bottom.)

## The board and the line

Turn 3, pre-combat main, before the land drop:

| permanent | produces |
|---|---|
| Forest | `{G}` |
| Peat Bog | `{B}{B}` — **one depletion counter left**, so the next tap sacrifices it |
| Undercellar Myconid | one mana of any colour (cast T2, not summoning-sick) |
| 1/1 Saproling Token | — |

Hand holds `Secluded Courtyard` (the land drop), `Sol Ring` `{1}`, `Shroofus Sproutsire` `{2}{G}`,
`Wild Growth` `{G}`, and two more. The committed line plays Secluded Courtyard and casts
**Sol Ring + Shroofus Sproutsire** (4 mana).

## What happens

`MTG_TAPDBG=1` on the committed apply is the whole story in three lines:

```
[line-order] turn=3 plan searched=1 human=0 vector: Shroofus Sproutsire Sol Ring
[paydbg] cost={2}{G} ok=1 taps: Peat Bog Forest
[paydbg] cost={1}   ok=1 taps: Secluded Courtyard
```

**The creature is cast first.** Its `{2}{G}` takes Forest plus Peat Bog's last depletion counter and
the land is sacrificed; Sol Ring is then cast off the Courtyard and never taps at all.

Cast Sol Ring first and its own `{C}{C}` pays the `{2}`: Forest covers the `{G}`, the Courtyard
covers the `{1}`, Peat Bog is never touched. That line exists, and the engine enumerated it.

## Root cause — a plan's REALISED cast order was neither reported nor reachable

Three things compose.

**1. Human play turns the cast-ORDERING search on.** `OrderingSearchEnabled` is
`env || DecisionUnpruned(UnprunedGate::SearchOrder) || provider`, and the unpruned gate is open under
the viewer. So every multi-cast set arrives as k! sibling plans that differ *only* in execution
order, each with `searched_order = true`. (Autonomous play never sees them: the gate is shut, no
`MTG_SEARCH_ORDER`, and Fungus's provider does not opt in — which is why this never showed up in any
aggregate and why the fix moves no ground truth.)

**2. `cast_order_canonical` reported the CANONICAL SORT for plans that execute in VECTOR order.**
`apply_plan_actions` has two routes — `searched_order` casts in vector order, everything else sorts
by `CastOrderLess`. `BpPrepayPrefix` mirrors that split correctly and its comment names re-deriving
it separately as a lockstep hazard. `CanonicalNonSacCastOrder` — the *only* order the decision JSON
carries — never got the split. So at this frame:

| plan | executes | advertised `cast_order_canonical` | Peat Bog |
|---|---|---|---|
| 444 | Shroofus, Sol Ring | `[Sol Ring, Shroofus Sproutsire]` | **sacrificed** |
| 445 | Sol Ring, Shroofus | `[Sol Ring, Shroofus Sproutsire]` | alive |

Two plans whose only difference is a destroyed land, byte-identical on the wire.

**3. The 200-plan display cap then removed the good one.** 571 plans at this frame. The cap's
diversity pass keys on `(land, face, sorted cast-name multiset)`, which is order-blind, so the two
orderings share a slot and "first by index" wins it. `next_permutation` runs over a NAME-SORTED list,
"Shroofus" < "Sol", so **444 took the slot and 445 fell outside the emitted menu entirely.** The
payload-coverage pass ahead of it is order-blind too.

Net: the only line the human could click was the one that throws a land away, and the field that
should have warned them said the opposite.

## The fix

* **`TurnSolver::RealisedNonSacCastOrder`** — vector order for a `searched_order` plan, the canonical
  sort otherwise. Both decision-JSON emitters (`WriteDecisionJson`, `WriteValidation`'s
  `matched_plans`) now use it. The wire key keeps the name `cast_order_canonical` — 265 saved
  references carry it and the viewer reads it by that name — but its value is now the truth.
* **`TurnSolver::CastOrderIsCanonical`** — `std::is_sorted` under the same shared comparator, so no
  allocation and at most k-1 comparisons.
* **The display cap keeps the CANONICAL ordering as a cast set's representative**, replacing an
  earlier non-canonical winner in place. The canonical order is the engine's own sequencing
  judgement (`CastOrderRank`: a mana rock before the spells it funds — exactly this case) and it is
  what autonomous play uses, so it is the honest default.

Nothing is narrowed: the alternates stay enumerated, stay indexable by `--choices`, and stay
reachable from the viewer, which pins the human's queued order through `--cast-order` for any
multi-cast line (`applyAccepted`). Only which sibling is shown *by default* changes.

**Verified** at the reported frame — the menu's entry for that cast set is now 445, and both it and
the three-cast variant (Sol Ring + Shroofus + Wild Growth, plan 342) leave Peat Bog on the
battlefield with Sol Ring tapped.

## Two refuted hypotheses, kept

**Tap order.** The first guess was that the source ranking preferred the depletion land. It does
not — and a reserve lever (`MTG_DEPLETION_LAST_RESERVE`) that pushed Peat Bog from rank 11 to 63 was
built, measured on this exact game, and **changed nothing**: without Sol Ring on the battlefield the
only other source is the dork-reserved Myconid, which ranks later still. Reverted rather than
shipped as a default-OFF no-op.

**The batch prepay.** The second guess was `BatchPrepayMainCasts` — "solve the ideal total cost once,
then work back" — paying the subset as a lump before any of its casts resolve, so a rock the line
casts is never a source. It reads convincingly, and the engine really does model the rock's
contribution twice (`want_rock` / `Action::rock_mana` / `joins_for_mana` credit a same-line producer
on the *payability* side). **But the prepay never runs here:** it declines on any producer cast —

```cpp
if (a.ritual_float > 0 || a.rock_mana.Total() > 0)
{ if (!g_prepay_producer_on) { return Pp(PP_PRODUCER); } }   // producer breaks fungibility
```

— and `MTG_PREPAY_PRODUCER` is default off. Sol Ring carries `rock_mana`, so the turn falls through
to the per-cast path, which pays each cast as it resolves and *would* have seen Sol Ring's mana. The
`{2}{G}` / `{1}` pair in the `paydbg` trace above is the proof: two separate payments, not one lump.
**The ordering was the only defect.** `MTG_TAPDBG=1` would have said so in one line on day one; two
hypotheses were built before anyone ran it.

## A pitfall worth recording

`--choices` selects a plan by its **`index` field**, not by its position in the `plans` array. On
this frame 571 plans are enumerated and 200 emitted, carrying indices up to 570; position 169 has
index 340. Any A/B over plans must match on CONTENT and read the index off the matched plan — the
first comparison run for this report was invalid for exactly this reason.
