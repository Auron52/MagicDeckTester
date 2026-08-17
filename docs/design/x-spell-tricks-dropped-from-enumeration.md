# An `{X}` spell that is not X-DAMAGE was dropped from enumeration entirely

**Status:** FIXED 2026-08-17 (in the Fortifying Draught / Impolite Entrance / Luxurious Libation
commit). Applies to the cast enumerator in `src/ai/TurnSolver.cpp`. Found while implementing
Luxurious Libation for the Mirrorwing trick-suite screen.

## The bug

The enumerator routes every card with `{X}` in its cost into one block:

```cpp
if (def.card.m_mana_cost.has_x)
{
    ...                                                   // untap ritual (Reality Spasm)
    if (def.tmpl != CardTemplate::DirectDamage) { continue; }   // <-- drops the card
    ...                                                   // X-damage variants
    continue;
}
```

Every path out of that block is a `continue`, and the `DirectDamage` test rejects anything else. So
an `{X}` card that is not X-burn is **never emitted as an action at all** — not mis-valued, not
deprioritised, simply absent from the search. It can never be cast, in any line, at any depth.

The block's own comment said so, and was read as a limitation rather than a bug:

> Only X-damage (DirectDamage) is modeled today; other X templates (DrawX) stay skipped until their
> effect is scaled in BOTH cast paths.

## Why it went unnoticed

Nothing fails. There is no error, no log line, no coverage gap: `analyze_deck.py --coverage-only`
reports the card `full` (its oracle text IS fully implemented — in a resolver that never runs), and
the card's params are read into `CardParams` normally. A deck holding the card simply plays as
though those slots were blank, and the win-turn it reports is a perfectly ordinary number.

**How it was caught, and the technique worth reusing.** Not by reading code — by neutralising the
card's payload params in a copy of `cards.json` and running both against the same seeds in one
pooled batch:

```
ARM A (real params)   v_s920000: avg=8.4750 digest=c58dde90395d3633
ARM B (params zeroed) v_s920000: avg=8.4750 digest=c58dde90395d3633   <-- BIT-IDENTICAL
```

A card whose payload can be deleted with **zero** effect on 40 games is not being cast. This is a
cheap, general post-implementation gate — much stronger than "the coverage check is clean", and it
answers the question coverage cannot: *does the implementation ever actually execute?* Note the
token param was zeroed too, so even a cast at X=0 would have shown up.

After the fix, on the same seeds:

```
ARM A (real params)   6.7250 / 7.0500
ARM B (params zeroed) 8.4250 / 8.4250
```

i.e. the card is worth ~1.7 turns on that test deck, and the neutralised arm reproduces the old
never-cast number — which is what confirms the diagnosis rather than merely being consistent with it.

## The fix

Two parts, both byte-identical for every shipped deck (smoke 36/36):

1. **Gate the X block** on `&& !def.params.solo_target_trick`, so an `{X}` trick falls through to
   the trick enumerator, which has the shape it actually needs (per-target variants, copy fan-out).
   No shipped card is both `{X}` and a trick, so nothing else changes.
2. **Emit one trick variant per candidate X**, cost `+= X * x_pips`, `chosen_x` stamped on the
   action. Variants share `hand_index`, so they are mutually exclusive in a plan and the SEARCH
   picks among them; the provider narrows the range via `XCandidates`, never the machinery. A
   non-`{X}` trick gets exactly `{0}` → one action per target → byte-identical.

### X = 0 is offered deliberately

`GenericProvider::XCandidates` returns EMPTY when `max_affordable <= 0`, documented as "empty → the
spell is not cast this turn". That is right for an X burn (X=0 deals nothing) and **wrong for a
trick**, whose non-X riders carry the card: Luxurious Libation at X=0 still costs `{G}` and still
makes a Citizen token per copy, which under a copy magnet is a board of bodies for one mana.

So the trick path always includes `0` in its candidate list. This is *widening*, which the core
invariant permits — only NARROWING is reserved to providers. A provider that wants to suppress the
X=0 line may still rank it away; the machinery must not decide it is worthless.

## Still open

`DrawX` (Braingeyser) and any other non-damage `{X}` template remain skipped by the same
`DirectDamage` test — this fix only rescued the trick shape. Any deck that ships one should expect
the card to be inert until its own path is added, and should confirm with the neutralise-and-diff
gate above rather than assuming.

## Related

The generic-machinery half of this is the [[core invariant]] the analyze-deck skill states: only a
provider heuristic may narrow the search. A blanket `continue` in the enumerator is the most
extreme possible narrowing — it removes every branch for the card — and it lived in shared
machinery rather than in a named, A/B-testable provider hook, which is exactly why no per-deck
review caught it.
