# Mirrorwing Dragon: cast-order draft (2026-10-14)

Provider `MirrorwingProvider` (src/ai/DecisionProviders.{h,cpp}). Current list = `v3-heroism-draught`
(adopted 2026-08-30): 4 Mirrorwing, 4 Zada, 4 Frontline Heroism, 4 Hierarch, 4 Mystic, 4 Fists,
4 Gold Rush, 4 Ancestral Anger, 3 Oracle's Restoration, 4 Fortifying Draught, 21 lands.

## 1. Status: **(A) USER-agreed order, shipped default ON**

* `MTG_MW_ORDERED` is `EnvOn(...,true)` (DecisionProviders.cpp:20025-20033). The USER held the review
  on 2026-08-18; the ruling is quoted verbatim in cast-order-rankings.md under "Mirrorwing Dragon".
* Frontline Heroism goes at rank 6, by the USER's ruling of 2026-08-27 (*"before all tricks, but it
  can go after magnets"*; mirrorwing-instigator-slot-screen.md, "The cast-order gap").
* The land drop goes first, by the USER's ruling of 2026-08-27 (DecisionProviders.h:1673-1677,
  rank 0).
* **Caveat:** the list changed after the review. **Oracle's Restoration** gets rank 14 from its
  `cast_draw` param. An agent checked that slot (instigator-slot-screen "Cast order was checked
  BEFORE launching"), but the **USER never reviewed it**.
* No USER order is OFF.

## 2. What plays today (verified)

**The report's ranks are what plays.** Every trick here is a `solo_target_trick`, so every trick
set is order-opaque (`OrderingOpaque`, ManaPayment.cpp:2722-2738). Mirrorwing opts into rank-sorting
those sets anyway (`OrderOpaqueCastsByRank` = `MTG_MW_ORDERED`, :20121). Checked in both apply
twins (AIEngine.cpp:6292-6400, TurnSolver.cpp:42767-42890) and in 30 traced games
(`MTG_USER_ORDER_TRACE`, d3/b10, seeds 1001-1030). Everything is cast in main 1, because
`uses_second_main=no` means the phase filter never runs (TurnSolver.cpp:20388).

| pos | card(s) | main | what put it there |
|---|---|---|---|
| 1 | land drop | m1 | rank 0 (USER). Pinned first at d0; at depth > 0 the search decides whether the drop is taken |
| 2 | Zada, Mirrorwing *(tie)* | m1 | 5, MAGNET. **Hoisted** (`CastEnablerFirst`): cast before every trick |
| 3 | Frontline Heroism | m1 | 6, hoisted. The report shows "(unclassified)" only because `CastOrderTierName` has no case 6 |
| 4 | Ignoble Hierarch, Elvish Mystic *(tie)* | m1 (the report labels Mystic **m2**, but the label is inert) | 10, creature, hoisted |
| (4') | Gold Rush, when the late slot cannot pay | m1 | `MTG_MINT_CREDIT_EXACT` hoist: placed after the longest hoist prefix the base pool pays together with it (`PlaceHoistedMinters`) |
| 5 | Ancestral Anger, Oracle's Restoration: **TIE, decided by hand order** | m1 | 14, DRAW. A re-solve breakpoint follows each one |
| 6 | Gold Rush | m1 | 15, funding ladder 15 -> 13 -> 6. Rungs 13 and 6 give the same position in this list, because the bodies are hoisted out of the trick list |
| 7 | Fists of Flame | m1 | 16, PAYOFF |
| 8 | Fortifying Draught | m1 | 18, last |

`MTG_PAYABLE_ORDER` swaps in the nearest payable permutation, but only when the ranked order cannot
pay.

## 3. Proposed order

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| 1 | **land drop** (before the draws) | m1 | - | USER 2026-08-27: a land drawn later can still be played later; clairvoyance gains nothing by deferring |
| 2 | Zada / Mirrorwing *(tie OK)* | m1 | - | The copy target must exist before any trick. Neither card has an ETB or cast trigger the other reads, so their order cannot change anything |
| 3 | Frontline Heroism | m1 | - | USER: after the magnets, before every trick. Each later targeted trick makes a Soldier and copies itself onto it |
| 4 | Elvish Mystic / Ignoble Hierarch *(tie OK)* | **m1** (Mystic m2 -> m1) | - | Same {G} cost, no triggers, Exalted is combat-only. Each body is one more payload for every fan, which is one more draw, Treasure or life even while the body is summoning-sick |
| 5 | Ancestral Anger | m1 | - | A draw with a flat +X per payload. It must precede Fists so its draws are counted |
| 6 | **Oracle's Restoration** (after Anger) | m1 | - | A draw that also gains 1 life per payload. Under magnet + Heroism the later trick carries one more payload, and Oracle's extra payload adds +1 to every later Draught payload |
| 7 | Fists of Flame | m1 | - | The payoff: after every draw it counts |
| 8 | **Gold Rush** (after Fists) | m1 | [after Fists .. before Fists .. before the draws] | Follows the USER's 2026-08-18 words (*"Draw spells Anger, ... and Fists should come before other spells, Gold Rush is an exception ... after is better because we might get more creatures on board after drawing"*). The funding ladder walks it earlier only while the line cannot pay. The mint hoist (4') is unchanged |
| 9 | Fortifying Draught | m1 | - | Last (USER). X counts all of the turn's earlier lifegain, and Draught is the steepest escalator (+2 per payload) |

## 4. What changes and why it matters

The general mechanism: **with a magnet AND Heroism live, every trick leaves a Soldier that every
later fan also copies onto, so the k-th trick carries one more payload than the (k-1)-th.** The
order of two tricks then decides which one gets the extra payload. The search cannot fix a wrong
choice here. Under **`MTG_BP_NEW_ONLY` (default ON)**, a "cast X alone, then old cards in the
continuation" line is dropped as a duplicate of its sibling {X, old cards}. That sibling plays in
**rank order**, so whenever the order matters, the rank is the only way the line can be expressed.
Every probe below is in `build/scratch/castorder/probe_Mirrorwing Dragon/`.

1. **Anger before Oracle (fixes a hand-order tie that matters).** Probe: T4, Zada + Goblin
   Instigator + Heroism, Mountain/Forest/Forest, hand Anger + Oracle + Draught, opponent at 88.
   * Anger -> Oracle -> Draught deals **88** (`tie_A`, kills).
   * The same board with Oracle listed first in hand deals **82** (`tie_B`). Even at
     `--budget-ms 0`, no line reaches 88.
   * With `MTG_BP_NEW_ONLY=0` the search finds the 88 line by casting {Anger} alone and re-solving.
   * The opposite case: with no Draught cast and an Anger already in the graveyard (X=2), Oracle
     first is better by 1 damage (`tie_C` 19 vs `tie_D` 20). That small case is all the
     unconditional rule gives up. An exact rule is in §7.
2. **Gold Rush after Fists.** Probe `gr_fists_heroism`: Zada + Instigator + Heroism, 4 lands, hand
   Fists + Gold Rush. The shipped GR -> Fists deals **46** (measured). Fists -> GR deals **50**,
   and no line reaches it at unlimited budget (with `MTG_BP_NEW_ONLY=0` it is found). The rule
   under magnet + Heroism: GR later wins iff `2*Treasures + (payloads of the earlier slot) + 1 >
   cards drawn this turn before Fists`. It usually holds early in a go-off and fails after two or
   more mass draws.
   * Without Heroism the two orders give the same power. Your "more creatures after drawing" line
     is already reachable through the breakpoint split when Fists finds a body: probe
     `gr_fists_6land` deals 25 by Fists -> drawn Mystic -> GR, found at b20 and at b0.
   * So outside Heroism the move is weakly positive: the funding ladder keeps it payable, and the
     line becomes the default sibling instead of a split the search has to find.
3. **Frequency, to size the stakes.** In 30 traced games, Heroism-live turns that cast both GR and
   Fists came up 4 times, Fists + Draught 5 times, and Anger + Oracle once. Not every one of those
   had a magnet. This is a minority of turns, but it lands on go-off turns, where the Heroism decks
   win.
4. **Cosmetic only:** add the missing tier label for rank 6, and change Mystic m2 -> m1. Neither
   changes play.

## 5. Questions for the USER

1. **Ancestral Anger before Oracle's Restoration** (Oracle last among the draws, because its life
   is counted by Draught)? *Default: yes.*
2. **Gold Rush's preferred slot AFTER Fists**, with the ladder walking it before Fists only while
   the line cannot pay? This is your 2026-08-18 wording; the adopted table put it before Fists.
   *Default: yes, behind a lever, A/B before it ships.*
3. Under magnet + Heroism, should the provider **compute** the order of the pairs whose sign flips
   (Anger/Oracle, Gold Rush/Fists, Fists/Draught) from exact payload arithmetic, falling back to the
   fixed order when the pair is tied? *Default: yes, as a follow-up under its own lever.*
4. Engine-wide (§6): should **`MTG_BP_NEW_ONLY`** keep an old-cards-only continuation when the
   sibling's rank order cannot express it? *Default: yes. This needs your call because the lever
   is adopted; it is not a Mirrorwing-only choice.*

## 6. Generic-order findings

* **`MTG_BP_NEW_ONLY` turns every order-sensitive tie or rank into a hard prune.** Its soundness
  argument (TurnSolver.cpp ~4630-4666; bp-new-only-continuations.md) assumes the sibling
  `P u {X}` "reaches the same end state". That holds only if the sibling's rank order matches the
  split's order. Both probes above were found with the lever at 0 and were unfindable at budget 0
  with it on, which breaks the "complete at large budget" invariant. Any deck with a tie whose
  order matters, or a wrong rank, has the same problem.
* **Generic tie-break = hand order.** Shown here at rank 14. A tie should be allowed only where
  the order provably cannot matter.
* The main-phase classifier labels a vanilla mana dork (Mystic) m2 and Hierarch m1. In a
  copy-magnet or Heroism deck every body is a payload recipient, so pre-combat is always right.
  This is inert while `uses_second_main=no`.
* The report is missing a tier name for rank 6, and the tiers for absent cards (11 Libation,
  12 Twinflame, ladder rung 13 "after Twinflame") are stale. All of this is harmless.

## 7. Implementation notes (param-keyed, house style)

* **Ranks** (renumbered so the new slots fit):
  * magnets `copies_solo_targeted_spells` 5; Heroism `!IsCreature && MwBodyCount>0` 6;
    creatures 10 (Generic).
  * **Anger** `cast_draw>0 && cast_lifegain==0` 14; **Oracle** `cast_draw>0 && cast_lifegain>0`
    15. Note: archived v1 Scale the Heights matches the Oracle rule too.
  * **Fists** `pump_per_cards_drawn_power` 17. Keep it ahead of the `cast_draw` check, as today.
  * **Gold Rush** `creates_treasures` 18, `CastOrderFallbackRanks` {18, 16, 13}. Rung 6 is the
    same position as rung 13 in the trick list; the true "GR before the bodies" line is the mint
    hoist.
  * **Draught** `pump_per_life_gained_power` 19.
  * Update `CastOrderTierName`, adding case 6 and the new numbers.
* **Gating:** a new lever (e.g. `MTG_MW_ORDER_V3`), default OFF and byte-identical when off, so
  the A/B arm has a control. The archived v1/v2 lists share the provider, so check their reference
  benches.
* **Limitation:** the magnet + Heroism conditionals cannot be scalar ranks. They need the set, so
  they belong in `MirrorwingProvider::ArrangeCastOrder`. That hook is already called by both apply
  twins, the continuations and the pricing walk (`ApplyDeckCastArrangement`). The comparator
  compares the marginal value of the extra payload:
  * Anger: `X`
  * Oracle: `1 + (Draught payloads to come)`
  * Gold Rush: `2*(T+m+1)`
  * Fists: `D+m+1`
  * Draught: `L+2*(m+1)`

  Here T = Treasures, D = cards drawn this turn, L = life gained this turn, and m = the payload
  count at the earlier slot. Keep the fixed constraints: draws before Fists, Oracle before Draught.
