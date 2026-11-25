# Worldfire APPLY-DROP: three reach gaps of the crit-mode line-keep rung (2026-11-25)

Mana-clean gate `claude-queue-20261125a` (src `773d4e440d5c`, Worldfire games 0-22999, seed 30000000 + gi): Worldfire
had 7,392 issues, 6,938 of them APPLY-DROP. Branch `wip/worldfire-ad`. Measurements: `logs/worldfire-ad/report.md`.

## The cluster

Of the 5,000 whole flag lines the gate kept, 4,473 (89%) have Mercadian Bazaar on the board, and every dropped cast
is a one-blue-pip spell: Ponder 2,353 of the 6,938, Brainstorm 885, Personal Tutor 707, Mystical Tutor 545,
Preordain 283. The shape is always the same. A red payment earlier in the line (Seething Song `{2}{R}`, Irencrag Feat
`{1}{R}{R}{R}`, Keldon Halberdier's or Rift Bolt's suspend `{R}`) is paid with the deck's blue-capable sources (Steam
Vents, Saprazzan Skerry), while the Bazaar's banked red is held, because a storage land taps last. The cantrip then
has no `{U}`. The oracle pays the red with the Bazaar and keeps the blue.

The engine has a rung for exactly this: the line-keep rung (`LineReducerKeepHold`, ManaPayment.cpp) in its crit mode
(`MTG_LINE_KEEP_CRIT`, adopted 2026-11-25). It trial-pays on a scratch board, judges the rest of the declared line
with the complete backtracker, and re-pays with some of the greedy's sources held out only when the greedy's payment
strands the line. Its design note (`docs/design/line-keep-crit-2026-11-24.md`) lists what it does not reach. Those
gaps are this cluster:

1. **Suspend lines were unbound.** `LineKeepBound` counted held CASTS, so `{suspend Keldon Halberdier + Ponder}`
   (one cast) never bound the rung, although `ApplySuspend` pays the `{R}` through the shared payer before every cast
   and the line hold already carries it. Repro: s30000447 gi447 T3, Bazaar + Steam Vents.
2. **The gate saw one last source only.** `LineKeepCritGate` passes a payment only when ONE untapped source is the
   last of a colour the rest owes. `{Seething Song + Brainstorm}` on Steam Vents + Bazaar + Saprazzan Skerry: Song's
   `{2}{R}` took Vents (1 `{U}`) and Skerry (2), which together are all the blue. Neither alone is. Repro: s30000065
   gi65 T4.
3. **A ritual's float was not in the rest.** `{Song, Song, Personal Tutor}`: the second Song is paid from the first
   Song's `{R}x5`, so the rest after the first payment (`{2}{R}{U}`) was judged on a board without that float. The
   joint test refused (7 mana on 4), and nothing was repaired. Repro: s30000125 gi125 T4.

Not an engine defect of play correctness in the usual sense: each is a payment the greedy makes that provably drops
a cast of the line the search chose, while another payment of the same sources casts it. It is not a modelling gap
either: the Bazaar's burst, Skerry's depletion and the rituals are modelled correctly. It is not a USER-owned policy
question: the USER ruled on banked storage counters for Dragonstorm (2026-11-18, *"it should spend the counters if
they are required for the line"*), and the rung spends them only in that case: the greedy's own payment strands the
declared line, and a hold-out keeps it.

## The change (three levers, all DEFAULT ON, each `=0` byte-identical)

- `MTG_LINE_KEEP_SUSPEND` (`LineSuspendPaid`, TurnSolver.cpp): a suspend with a mana cost counts as a held payment
  in `LineKeepBound`, and its cost counts in `LineNoncreatureMv` (a suspend cost is no creature spell's). A `{0}`
  suspend (Lotus Bloom) is not counted.
- `MTG_LINE_KEEP_CRIT_MULTI` (`LineKeepCritGate`): for each owed colour c, the bill's non-c pips may take whole
  c-capable sources, the largest first, until they cover those pips. The gate passes when what is left of c falls
  short of the rest's c plus the bill's own. The one-source test is this test with one source, and the largest source
  is always taken, so it only passes more payments to the exact judge. The judge is unchanged.
- `MTG_LINE_KEEP_RITUAL` (`LineKeepRockFor` / `PutLineKeepRock`): when the payment is for a fixed-burst ritual, the
  ritual's float (at no splice, the least it adds) is on every board the rest is judged on, as `MTG_LINE_KEEP_ROCK`
  puts a rock there. The amount pre-test counts it.

All three widen only the rung's REACH. The rung still changes a payment only when the greedy's own payment strands
the rest of the declared line and a hold-out of the greedy's sources keeps it. It is the shared payer, so the
executor, the rollout and the walks move together, at every budget. It is a payment choice (the standing mana-payment
exemption), not a search-window decision.

Unit tests: `test/unit/test_worldfire_line_keep.cpp` (the three repro boards, each lever on and off, plus the
no-strand controls).

## Measurements

See `logs/worldfire-ad/report.md`.
