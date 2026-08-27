# treasure_hunt mana ordering — one fix landed, three things still open

Written 2026-08-27 from a viewer session on `decks/treasure_hunt`. One defect is fixed and adopted;
the rest is **deferred work**, recorded here rather than in any agent's memory (CLAUDE.md) so it is
available to whoever picks it up.

## Landed: floating mana may feed a ramp filter (adopted 2026-08-27)

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

### OPEN 1 — the OVERNIGHT tier's ground truth is STALE for treasure_hunt

Smoke and regression were rebaselined with the fix (`--accept`, treasure_hunt keys only, both
tiers). **Overnight was deliberately not run** (user, 2026-08-27: *"We don't need to do overnight
yet"*). Until it is, an overnight run will report treasure_hunt failures that are this adopted
change, not a regression:

```
bash test/regression.sh --overnight            # inspect: expect th_* digest-only diffs
bash test/regression.sh --overnight --accept   # only after inspecting
```

Expect the same shape the other two tiers showed — **identical averages, digests only,
`slower=0 faster=0`**. Anything else is a real finding and should not be accepted.

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
made of those scores **zero**, the prepay declines, and payment falls through to the unreserved
greedy.

**Reproduction** (user-reported, seed 8, reproduced exactly):

```
build/Release/mtg decks/treasure_hunt/treasure_hunt.txt \
  --profile decks/treasure_hunt/treasure_hunt.profile.json \
  --cards-json src/cards/data/cards.json --claude-play --seed 8 --game-index 7 \
  --max-turns 8 --depth 0 --choices 0,0,1,2,0,0,4,0,4,0,5,41,0,0,6
```

T5 board is Fiery Islet, Land's Edge, Sandstone Needle, Saprazzan Skerry with an Island in hand.
Plan 6 plays the Island — the land drop *is* applied before payment — and then pays Throes of
Chaos's retrace `{2}{R}` by tapping **Sandstone Needle + Saprazzan Skerry**, leaving the fresh
Island *and* Fiery Islet untapped. That is 4 mana produced for a 3-mana cost, and it spends the
Skerry's last depletion counter, sacrificing it. `Sandstone Needle {R}{R} + Island {U}` pays it
exactly, for free.

**Proof the reservation is inert rather than wrong:** `MTG_NO_DEPLETION_RESERVE=1` produces a
byte-identical board.

On this deck the shape is routine, not exotic — Throes of Chaos retrace plus Land's Edge is a normal
treasure_hunt turn. Fixing it means widening the prepay's eligibility test (or giving the
reservations a home outside it), which touches **every deck's** payment path, so it needs the full
suite rather than a treasure_hunt-only sample.

## OPEN 3 — the tap order cannot tell two depletion lands apart

User, 2026-08-27: *"we misorder depletion lands. We should prefer those with more counters before
those with less."*

`ManaSourceRank` takes a **`CardDefinition`**, not a `Permanent`:

```cpp
int rank = ResolveProvider(state).ManaSourceRank(state, *def);
...
if (rank < best_rank) { best_rank = rank; best_i = i; }
```

Two Saprazzan Skerries with different counter counts are indistinguishable to it, and the strict `<`
means the winner is whichever sits earlier on the battlefield. There is no counter-aware tiebreak
anywhere in the tap loop; the counters live on `Permanent.counters` as `Counter::Type::Depletion`.

**Why "more counters first" is right, and it is not about total mana.** Tapping the 2-counter copy
leaves A(1) + B(1): two lands, two taps available *in the same turn*. Tapping the 1-counter copy
sacrifices it and leaves A(2): the same total mana, but only one tap per turn. The rule preserves
per-turn burst, not resources.

Any fix is a per-permanent tiebreak in the greedy tap loop (`TapForCostSharedOnce`, the path both
executor and rollout call, so one change keeps them in lockstep) — not a change to
`ManaSourceRank`'s signature.

## Sequencing note

OPEN 2 and OPEN 3 are both play changes and both want their own measurement; do **not** bundle them
into one ground-truth rebaseline or neither is attributable. OPEN 2 is the bigger prize — a missing
class of turn rather than a tiebreak — and is likely to subsume many of the cases where OPEN 3 would
otherwise bite, so measure it first and re-check whether OPEN 3 still moves anything.

Standing caution for both: this repo has measured three *"make the mana projection more accurate"*
fixes that ALL lost, with zero games better in any arm — the pessimistic projection was acting as a
tempo prior (`goblins-enabler-worse-games.md`). The fix landed above went the other way (6 better /
0 worse over 16,000 held-out games), and the distinction that seems to matter is *restoring a line
the search could not see at all* versus *re-ranking two lines it could*. OPEN 2 and OPEN 3 are both
re-ranking changes, i.e. the side that lost last time. Measure before believing.
