# Brightcap Badger's grant skips DEFINED Fungi at two payment sites (found 2026-10-28, unmeasured)

Status: DEFERRED -- found while implementing Paradise Mantle, not fixed, not measured.

## What

`ManaDefOf` (SpellEffects.h) documents that Brightcap Badger's "each Fungus and Saproling you control
has '{T}: Add {G}'" reaches TWO populations: definition-less Saproling TOKENS, and Fungi that HAVE a
CardDefinition but no mana ability of their own (Thallid, Sporecrown Thallid, Mycoloth, Psychotrope /
Vitaspore Thallid, Utopia Mycon). Most sites honour both. Two only grant to the first:

* `TapForCostBacktrackWorker`'s top-level source list (`SpellEffects.cpp`, the `s_src_cands_buf`
  build): a granted body is admitted only on the `!d` (token) branch; a defined Fungus falls to the
  `BasicLand || ManaDork || mana_rock || IsPaySacSource` test and is skipped.
* `ManaCacheKey` (`SpellEffects.cpp`): same shape -- the `!d` branch hands a token the synthetic face;
  a defined Fungus is not hashed as a source. The site's own comment says it "must move in LOCKSTEP
  with the payer's predicate", and the greedy payer (`ManaPayment.cpp`) DOES credit defined Fungi.

## Why it might matter

The backtracker can then fail to find a payment that needs a Thallid's granted tap, and the cache key
can collapse two boards that differ only in whether such a Fungus is tapped. The bounds
(`LooseManaCeiling`, `UntappedManaUpperBound`) were already fixed for this population after fungusb
s2002 gi40 (see the comment in `UntappedManaUpperBound`'s grant branch).

## Why it was not fixed with Paradise Mantle

Mantle's hosts are exactly this population (real creature cards with no mana ability), so the Mantle
change added an equip-host branch at BOTH sites (`EquipGrantReaches`). Widening that branch to every
`GrantReaches` body would move Fungus play, which needs its own A/B on the Fungus suite rows; it was
kept out of an overnight screen's scope. The fix is the obvious one: use `GrantReaches` instead of
`EquipGrantReaches` in those two branches (keeping the token cap for definition-less bodies only), then
smoke + regression the Fungus decks and A/B whatever moves.
