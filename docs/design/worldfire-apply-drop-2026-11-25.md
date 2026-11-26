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

- `MTG_LINE_KEEP_RITUAL_TIGHT` (DEFAULT ON; `=0` is the loose ritual rung, kept as an A/B arm): the cheaper form of
  RITUAL, added after a rung-counter probe (6 decks, 112 games, `MTG_LINE_REDUCER_KEEP_STATS=1`) showed RITUAL alone
  taking the rung's trials 76k -> 300k and its strand-nofire repairs (a strand no hold-out keeps) 377 -> 13,238. Two
  cuts. (a) The joint test judged board + float, which lets the float pay the ritual's OWN bill. Every repair pays the
  bill off the board and the rest off what is left plus the float, so the board alone must pay the bill plus the rest
  net of the float's Y units (its colour's pips first, then generic; only a single-colour float and a rest with no
  hybrid, phyrexian, twobrid or X pip). That is a necessary condition, so it is exact. (b) The crit gate counts the
  float as the rest's supply: a strand of colour c needs the rest's c short after the bill, and the float meets the
  rest. That is right in the gate's own model, but it is not byte-identical to the loose form, because the loose gate
  passed red payments only because it ignored the float, and the judge sometimes found a strand of another colour
  there. On the probe the 112 games are digest-identical to the loose form, trials 355k -> 126k, strand-nofire
  20,365 -> 403, crit repairs 21,622 -> 18,421 (off: 11,766). The A/B carries a `loose` arm on the ritual decks to
  measure (b).

All three widen only the rung's REACH. The rung still changes a payment only when the greedy's own payment strands
the rest of the declared line and a hold-out of the greedy's sources keeps it. It is the shared payer, so the
executor, the rollout and the walks move together, at every budget. It is a payment choice (the standing mana-payment
exemption), not a search-window decision.

Unit tests: `test/unit/test_worldfire_line_keep.cpp` (the three repro boards, each lever on and off, plus the
no-strand controls).

## Measurements (2026-11-26, binary = a8735ea32 Release, MTG_PGO=0)

- Unit 4197 / 4197. The Niv hole-1 lever-OFF block now pins MULTI off: MULTI catches that strand by itself (Niv's
  four non-{U} pips can take Skerry + Bluffs, leaving 2 {U} for the 3 owed; a legal payment exists).
- Repros (MTG_MANA_ORACLE=3), APPLY-DROP base -> new: gi447 T3 12 -> 2 (the 2 are an UNDECLARED Personal Tutor, also
  on base), gi65 T4 2 -> 0, gi125 T4 8 -> 0.
- CPU (perf instructions:u, 6 decks x 16 games, play settings): off 562.89 G; on (tight) 565.89 G = +0.53%; on
  (loose, TIGHT=0) 575.91 G = +2.31%. Rung trials 76k / 126k / 355k, strand-nofire 377 / 403 / 20,365. Tight and
  loose are digest-identical on all 28 probe jobs.
- Gate (Worldfire gi 0-22999, MTG_MANA_ORACLE=2): APPLY-DROP off 6,924 -> on 2,019 (-70.8%); APPLY-DROP-ORDER
  453 -> 437; ORACLE-GAP 1 -> 1. Leave-one-out on gi < 6000 (off 1,767, on 548): minus MULTI 1,376, minus SUSPEND 822,
  minus RITUAL 939, loose 548. Of the 2,019 left, 1,085 are "a lost action was not declared in the plan" (outside any
  line-keep rung). Table: `logs/worldfire-ad/gate_report.txt`.
- Harness trap: a binary copied under `build/scratch/<x>/` resolves `src/cards/data/cards.json` up from the
  executable, so it reads the PRIMARY checkout's data unless `src` / `decks` symlinks sit beside it (`build.sh` plants
  them for worktree builds). Fixed for this run's scratch dirs.
- Play-setting A/B (off / on, plus loose on the ritual decks; seed base 4870000): see the commit that adds it.
