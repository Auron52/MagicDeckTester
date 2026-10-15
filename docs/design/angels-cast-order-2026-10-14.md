# Angels: the USER's cast order + the narrow Giada second main (2026-10-14/15)

## What the USER approved (2026-10-14, verbatim)

> "The angel's order + caveats look good. I agree, though, that the second main is potentially wanted
> here. It would be only exactly cases where Giada can attack and we would use her mana where it would
> be considered. I wonder if we can use that to reduce any associated costs. The idea would be for it
> always only to be one spell maximum. We don't need to consider multiple since she can only pay for
> or help pay for one spell."

The draft (order, reasoning, probes): `docs/design/cast-order-drafts-2026-10-14/Angels.md`. Cross-deck
ruling the same day: *"Let's fully order the cards in each list inasmuch as possible. I don't like the
randomness of choosing the order."*

## Part 1 -- the order (`MTG_ANGELS_ORDER`, heurarm `ANGELS_ORDER`)

`AngelsProvider::CastOrderRank` / `CastOrderTierName` / `ArrangeCastOrder` (src/ai/DecisionProviders.cpp,
after `AngelsProvider::CleanupDiscardCandidates`). All main 1.

| slot | card | key (params) |
|---|---|---|
| land | the drop | structural: ApplyPlanDirect / TakeTurn play it before any cast |
| 1 | Sol Ring | `mana_rock`, noncreature |
| 2 | Lightning Greaves | `is_equipment && equip_grants_haste` |
| 3 | Bishop of Wings | creature, `own_creature_enters_lifegain > 0` |
| 4 | Righteous Valkyrie | `own_creature_enters_lifegain_toughness` |
| 5 | Archangel of Thune | `lifegain_each_own_creature_counters > 0`, no recipient filter |
| 6 | Lyra, Archangel of Dawn | same, with `lifegain_counters_subtypes` |
| 7 | Giada, Font of Hope | `other_subtype_enters_counters_subtype` |
| 8 | Youthful Valkyrie | `own_creature_enters_self_counters > 0` |
| 9 | Lyra Dawnbringer | `LordEffect` template |
| 10 | Lightstall Inquisitor | any other creature (sub-key mana value) |
| 11 | Resplendent Angel | `endstep_lifegain_tokens > 0` |
| 12 | duplicate legend whose name is on our battlefield | state; sub-key = its own slot |
| 13 | Serra the Benevolent | `loyalty_abilities` |
| 14 | Swords to Plowshares, Unexpectedly Absent | `Removal` template, sub-key mana value |
| 15 | anything unrecognised (noncreature) | sub-key mana value |

TOTAL: `rank = slot << 16 | sub << 12 | name ordinal`, the ordinal being the name's alphabetical
position over every loaded definition -- different names never compare equal, plan/hand order decides
nothing (unit test: 40 hand permutations, one realised order). `ArrangeCastOrder` adds, in this order:

* a SECOND copy of a legend in the same cast set takes slot 12 (it is a duplicate once the first resolves);
* USER caveat (b): no converter (Bishop / Righteous / Seraph Sanctuary) on our battlefield or in the cast
  set -> Thune and Lyra AoD behind Youthful Valkyrie (Thune before Lyra AoD), ahead of Dawnbringer;
* USER caveat (a): the plan carries a haste Equip onto a Giada cast in it and an Angel sits between the
  hoist point and her -> Giada moves to right after Greaves (or after the slot <= 2 casts when Greaves is
  already out) IF a walk that credits her unlocked `{W}` (Angel spells only; it fires the moment she and
  Greaves are both out, as `ApplyManaUnlockEquips` does) pays strictly more casts. The payable-order
  fallback cannot do this: `CreditCastOutput` does not credit a hasted dork's mana.

`=0` -> `GenericProvider` tiers and a no-op arrangement: byte-identical to the integration tip (measured,
below).

## Part 2 -- the narrow Giada second main (`MTG_ANGELS_GIADA_M2`, heurarm `ANGELS_GIADA_M2`)

Generic hooks `DecisionProvider::RestrictedSecondMainLive` / `RestrictedSecondMainAdmits`, read via
`ProviderSecondMainLive` (DecisionProviders.h; false whenever `uses_second_main`), wired beside Jorn's
`UntapSecondMainLive` at every post-combat site: AIEngine::TakeTurn's two `play_this_phase` gates; in
TurnSolver the rollout's per-turn m2, FSLineTail, the edge-tail elision, the honest per-k evaluation and
the four SolveWithLookahead candidate-loop `ApplySecondMainInSearch` sites -- the decisive "cast now vs
hold for after combat" sites, which the Omnitool predicate is NOT wired into; Giada's must be, or main 1
could never choose to hold an Angel for her. The menu filter sits at the tail of `CollectActions`
beside the Omnitool's (post-combat only, never in human play).

The deck rule lives in `AngelsProvider` (USER 2026-10-14: deck rules belong in providers):

* OPENS when our untapped, vigilant, subtype-restricted mana dork (Giada) passes `CanAttackFull` and
  `CanTapNow` post-combat (not summoning sick, or hasted by Greaves), and some Angel spell in hand is
  payable with `AvailableManaPool(s)` but NOT with `AvailableManaPool(s, giada)`.
* MENU = exactly those Angel casts. No other cast, no land (a land held since main 1 is barred from main
  2 already), no equip, no activation, no loyalty ability. At most one cast is ever payable: each needs her
  one `{W}`, and a second would have to be paid without it.
* SOUND IDENTITY FOLD, not a lossy gate: an Angel payable without her was payable in main 1 together with
  main 1's casts without tapping her (the payer holds an attack-capable dork back while the cost allows),
  and is weakly better there in a goldfish -- its entry triggers land before combat. Goldfish-specific;
  re-derive for a real opponent.
* VIGILANCE was already modelled (Combat.cpp's attack-tap honours `CreatureHasVigilance`); Giada's card
  note ("vigilance lets her attack and still tap for Angel mana the same turn") was false only because
  Angels plays no second main. With the lever on it is true (unit test: attack, then pay off her `{W}`).

USER QUESTION, the narrow reading was kept as the default (coordinator, 2026-10-15): "would pay for, or
help pay for" + "it must use Giada's mana" is read as "an Angel that cannot be paid WITHOUT her". An
Angel the lands alone pay was castable in main 1, where it is at least as good.

## Measurement (2026-10-15)

One pooled batch per binary through the box lock, cwd on the build volume. Paired seeds (held out):
angels ship 11400001 (4000 games, budget 20, depth from value_play = 5, profile + keep table + value
leaf attached), d3 b10 11410001 (6000), d0 11420001 (20000); angels2hg ship 11450001 (2000), d3
11460001 (3000), d0 11470001 (10000). Loss = 9. OLD = the integration tip ff3e205b binary.

### Batch 1: the two parts as built

| cell | arm | games | avg OLD | avg NEW | faster | slower | net | units vs OLD |
|---|---|---|---|---|---|---|---|---|
| angels ship | P1 (order) | 4000 | 4.6608 | 4.6392 | 89 | 3 | -86 | 0.930 |
| angels ship | P12 (order + Giada m2) | 4000 | 4.6608 | 4.6215 | 159 | 2 | -157 | 0.990 |
| angels d3 b10 | P1 | 6000 | 4.6832 | 4.6608 | 140 | 6 | -134 | 0.981 |
| angels d3 b10 | P12 | 6000 | 4.6832 | 4.6390 | 272 | 7 | -265 | 0.983 |
| angels d0 | P1 | 20000 | 5.2509 | 5.2378 | 271 | 11 | -261 | - |
| angels d0 | P12 | 20000 | 5.2509 | 5.2373 | 283 | 11 | -273 | - |
| angels2hg ship | P1 | 2000 | 5.1600 | 5.1405 | 40 | 1 | -39 | 0.955 |
| angels2hg ship | P12 | 2000 | 5.1600 | 5.1245 | 72 | 1 | -71 | 1.029 |
| angels2hg d3 b10 | P1 | 3000 | 5.1757 | 5.1510 | 77 | 3 | -74 | 0.990 |
| angels2hg d3 b10 | P12 | 3000 | 5.1757 | 5.1360 | 123 | 4 | -119 | 1.009 |
| angels2hg d0 | P1 | 10000 | 5.7756 | 5.7532 | 220 | 2 | -224 | - |
| angels2hg d0 | P12 | 10000 | 5.7756 | 5.7526 | 227 | 4 | -230 | - |

Part 2 alone (P12 vs P1, same binary): ship 71 faster / 0 slower, d3 132 / 1, d0 12 / 0; 2hg ship 32 / 0,
d3 47 / 2, d0 7 / 2. Its unit cost over part 1: +6.5% (ship), +0.2% (d3, budget-bound), +7.8% (2hg ship),
+1.9% (2hg d3) -- and against the old tree it is 0.990 / 0.983 / 1.029 / 1.009.

Identity: the both-levers-OFF arm on a seed prefix of every cell (600/1000/3000, 2hg 300/500/1500) is
byte-identical (win turn + digest) to the base binary; all 171 identity jobs of every other suite deck
(d0 / d3 b10 / shipped, 24 games each, incl. the 2HG variants) are byte-identical.

References (Angels, 21 incl. the archived v1 list): `viewer_protocol_check.py --strict` 20 ok, 1 repaired
(informational), 0 play-drift / enum-gap / board-diverged, identical old and new; `ref_bench.py` 4.900
search vs 4.900 human on both binaries, 0/10 short.

### Root causes of the slower games

Stage 1 / stage 2 recovery (single-game, `--threads 1`): at stage 2 (`--depth 8 --budget-ms 0`) every d0
slower game but one recovers, and almost no searched one does. The searched slower games are 15 distinct
games (27 game-arm entries): 2 recover at stage 2 as built (angels d3 gi3508 and angels2hg d3 gi1962,
P12 only: budget churn), and the other 13 are two mechanisms:

1. **A payer defect the new order exposes (10 of the 13; recover at stage 2 with the fix, the mechanism
   traced on gi2060).** `LineSpareSourceMask`'s SHORT
   test is strict: "the rest of the line owes MORE than the float + non-spare board can make". At ZERO
   surplus it reads "not short", the generic pip spends the colour, and a later pip is stranded. The
   USER's order puts Righteous Valkyrie `{2}{W}` ahead of Lightstall Inquisitor `{W}`; off Remote Farm
   (`{W}{W}` in one tap) + Sol Ring, the Farm's second `{W}` paid Righteous's generic, Sol Ring's `{C}`
   floated and Lightstall was dropped -- in the rollout too, so the line was unfindable at every budget
   (angels ship gi2060: T3 kill lost; at `--depth 3 --budget-ms 0` the new search cast NOTHING on T2).
   The search answered by avoiding Remote Farm on T1, which is the "T1 Remote Farm vs Sanctuary + Sol
   Ring" first difference of most slower games. Fix: `MTG_LINE_SPARE_TIGHT` (the boundary is short too);
   it reaches every deck's payer, so it is a separate lever, default OFF, measured below.
2. **Giada's counters lost to an Angel ranked ahead of her (3 games).** angels d3 gi873 and angels2hg
   d3 gi2425: Righteous Valkyrie before Giada with no engine buys 2 life where Giada first gives Righteous
   N +1/+1 counters -- both lose exactly that counter's damage (opponent at 1). angels2hg d3 gi1165: Lyra,
   Archangel of Dawn before Giada at one converter -- a counter tie by the draft's `(k-1)(1+m)` arithmetic,
   but Giada first enters Lyra bigger. Proposed amendment `MTG_ANGELS_GIADA_FIRST` (default OFF, the
   USER's call): (c) no engine on the battlefield or in the set -> Righteous behind Giada; (d) at most one
   converter -> Lyra AoD behind Giada. All three recover at stage 2 with it.

The d0 slower games are greedy-policy churn (the same Righteous-before-Giada trade, and different greedy
subsets); every one recovers at stage 2 except angels2hg d0 gi2274 under P1 (recovers under P12 and with
the payer fix).

### Batch 3: the payer fix and the amendment

(filled in below when the batch lands)

## Defaults as shipped

(filled in below)
