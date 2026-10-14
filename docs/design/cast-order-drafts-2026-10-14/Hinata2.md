# Hinata2 -- cast-order draft (2026-10-14)

## 1. Status: **(B) a USER order exists but is OFF**

* `MTG_HINATA_ORDER_FULL`, default OFF (`src/ai/DecisionProviders.cpp:9635`, ranks in `HinataFullOrderRank` :9700).
  You reviewed it on 2026-08-26 and revised it on 08-28: rocks split, Hinata 11 -> 6, Reality Spasm 15 -> 7,
  Ornithopter -> 14, land drop at slot 0. On 08-30 you removed the dork's range. History is in
  `docs/design/hinata-cast-order.md` §7 and `searched-design-deck-rollout.md` §6.
* **Measured worse.** The 08-26 version: +0.0210 hold (t 4.86) / +0.0270 train (t 6.06), n=3000 per cell. The
  08-28 version (08-30): +0.029 (t 3.8) in the recipe context. It was never decomposed.
* **Recorded root cause:**
  * About half the 08-26 loss comes from the Irencrag "second last" pin. Moving that one card back ahead of the
    payoffs recovered 0.0100 hold / 0.0157 train.
  * The rest was never attributed formally. The Ornithopter/Hinata split measured play-inert (6,000 games). The
    other mechanism measured at that size is putting the find tier (Gamble, cantrips, EI) ahead of Hinata:
    +0.0113 / +0.0120 on its own. Your 08-28 revision already undoes that move.
* **The measurements are stale, and they measured an order that never fully played:**
  * Every number above predates the Irencrag waste gate (09-01), the default-on breakpoint node (09-02) and the
    10-10 restrictor and Crackle fixes.
  * Under that order, any plan holding Ponder, Preordain or EI ignores the rank (§2).
* **What plays now:** the generic tiers (report `provider: Hinata`, no override while the flag is off).

## 2. What plays today

| pos | card(s) | main* | rule |
|---|---|---|---|
| land | land drop | m1 | searched at depth > 0 (22% of land+draw turns play it after a draw); first at d0 |
| 5 | Sol Ring | m2 | mana rock |
| 10 | Ornithopter of Paradise, Hinata **TIE** | m2 | creature |
| 15 | Reality Spasm | m2 | ritual |
| 18 | Irencrag Feat | m2 | cast restrictor |
| 20 | Gamble, Ponder, Preordain, Expressive Iteration, Soulfire, Magma Opus, Crackle **TIE** (+ Icy Blast, Memory Lapse, Remand, Distorting Wake, never cast) | m2 (Crackle "both") | other noncreature |

The rank above only sequences **clean plans**, meaning plans with no Ponder, Preordain or EI. Those cards are
`OrderingOpaque` (`ManaPayment.cpp:2722`), and Hinata does not opt into `OrderOpaqueCastsByRank`.

* **Searched play.** A plan holding Ponder or Preordain plays the cantrip-first partition
  (`TurnSolver.cpp:19879`): [Sol Ring / Hinata / Spasm / Irencrag] -> [Ponder, Preordain] ->
  [Gamble, EI, Ornithopter, payoffs], each group in hand (candidate) order.
* **Plans holding EI but no plain cantrip** play in hand order.
* **Seen in single-game probes** (seeds 9100001-9100030, play settings), identically under the USER order:
  `Sol Ring|Ponder|Gamble`, `Preordain|Gamble`, `Ponder|Ornithopter`, `Expressive Iteration|Ornithopter|Gamble`.
* **Where the rank still decides:** Irencrag's one-follower count, in both subset walks (`TurnSolver.cpp` ~35616 /
  ~48206), and the waste gate. Both count by rank in every plan.

*Main column: the classifier's value. It is advisory: `MTG_HINATA_ALL_MAIN2` is OFF (measured red because the
main-2 loop is weaker). In practice 159 of the 192 traced cast lines in those probes were in main 1.

## 3. Proposed order: your 08-28 order with ONE position amended

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| 0 | land drop | -- | -- | Yours: "Land should be placed at the start"; still searched, so a land a cantrip finds is played after it |
| 4 | Sol Ring | m2 | -- | {1} -> untapped {C}{C} this turn; everything after it can spend it |
| 6 | Hinata, Dawn-Crowned | m2 | -- | "{1} less for each target" reaches Spasm, Soulfire, Opus and Crackle, so she goes before every targeted spell. Yours: "since a drawn Hinata can still be played later" |
| 7 | Reality Spasm | m2 | -- | With her out, {X}{U}{U} = {U}{U}. The engine taps the board into float before it resolves, so where it sits in the phase costs no mana once the land, rocks and Hinata are down. Yours: "fair game" from then on |
| 8 | Gamble | m2 | -- | Yours: before the cantrips. The random discard hits the fullest hand, before you draw the card you want, and Hinata/Spasm already cast are safe from it |
| 10 | Ponder, Preordain **(peers)** | m2 | -- | Information before the rest of the turn. The tie cannot matter inside one plan: each is a breakpoint that truncates the plan (strict split: 0 of 2,400 games differ). The interleave stays DEFERRED (yours) |
| 12 | Expressive Iteration | m2 | -- | Its exiled card is playable THIS turn only, so cast it with mana left. Yours: "Early is okay" |
| 14 | Ornithopter of Paradise | m2 | -- | Summoning-sick, and nothing in the 60 gives haste, so its only use this turn is as one more Soulfire/Crackle target. Yours: "after Hinata, but before Soulfire" |
| **18** | **Irencrag Feat (was 22)** | m2 | -- | **"Only one more spell" means ANY spell.** Second-to-last therefore has to mean "after every non-payoff, right before the payoff it funds", not "after Soulfire and Opus" |
| 20 | Soulfire Eruption | m2 | -- | Yours: "Soulfire is typically better than Opus ... put Soulfire higher" |
| 21 | Magma Opus | m2 | -- | payoff that draws two |
| 23 | Crackle with Power | m2 | -- | last: X takes all remaining mana |
| 30+ | Icy Blast, Memory Lapse, Remand, Distorting Wake | -- | -- | goldfish-inert, never cast; ranked only so the order is total |

* **No ranges.** The `[2..N]` spans in the report belong to the cantrip promotion, which is OFF for Hinata (it
  measured a loss here, +15/+13 game-turns). `MTG_HINATA_RANGE` only feeds condemnation, which is OFF on Hinata.
* **One companion change, needed for any of this to play:** make the rank binding inside cantrip/EI plans (§7,
  item 2).

## 4. What changes and why it matters

1. **Irencrag 22 -> 18. This answers the recorded root cause.**
   * At 22, Irencrag ranks after Soulfire and Opus, so a plan {Irencrag, Soulfire} counts zero followers, and the
     waste gate drops it unless it is lethal.
   * That kills the line you yourself adopted on 09-01 ("we'll go with your rule, but just those 3 cards"):
     Irencrag for a payoff that cannot be cast without the float.
   * **Re-probed on today's binary**, seed 6600665 gi 664 (the recorded hold gi=664):
     * T3 board: Forbidden Orchard, Mystic Monastery, Sol Ring, Ornithopter, plus Reflecting Pool as the drop.
       That is 6 mana, no Hinata, with Soulfire, Irencrag and Crackle in hand.
     * The generic order and the amended 18 both cast Irencrag -> Soulfire. Soulfire stages Cascade Bluffs and
       Preordain; T4 goes Preordain -> Ponder -> Hinata, and T5 Crackle plus her attack is lethal: **win T5**.
     * Your order (22) **never wins** (unwon after 8 turns). The literal "finisher only" gate also never wins.
   * The same pin blocks Irencrag -> Opus. The generic order played exactly that at seed 9100004 T4.
   * This also covers your 08-28 idea "Irencrag before Magma Opus at a low enough opponent life". The gate admits
     Irencrag -> Opus exactly when Opus needs the float, rather than by life total.
2. **Against the generic order: Irencrag now follows the cantrips, Gamble and EI.**
   * At generic 18, everything at 20 counts as its follower: cantrips, tutor, dig.
   * Probe board: 3 Mountain + 4 Island, hand Ponder + Irencrag + Crackle, opponent at 10, d3. Ponder is free
     here, since X=2 either way.
     * The generic order casts Irencrag -> Crackle and leaves Ponder in hand.
     * Your order, and the amended one, cast Ponder -> Irencrag -> Crackle.
   * On a lethal turn this costs nothing. On a set-up turn it is a free look at three cards thrown away.
3. **The order becomes what plays inside cantrip plans.**
   * Today, `Sol Ring|Ponder|Gamble` (seed 9100027 T2) and `Preordain|Gamble` (T3) cast Gamble after the cantrips
     under either order.
   * With the opaque rank sort (probed via the global `MTG_ORDER_OPAQUE=1`), it becomes `Gamble|Preordain`, which
     is your position.
   * Recorded cost of the same Ponder-first, Gamble-last shape (`searched-design-deck-rollout.md` §6d, train
     gi=0; an order-repair lever produced it there): Ponder drew a Reality Spasm, and Gamble then discarded it at
     random. **T5 -> T8.** That game no longer diverges on today's binary.
   * Also realised: EI ahead of Ornithopter.
4. **The payoff block gets an order** (today it is a 20-way tie decided by hand order). At seed 9100017 T5 the
   generic order cast `Spasm|Spasm|Crackle|Opus`, with Crackle's X spent before Opus; yours casts
   `...|Opus|Crackle`. Same win turn there.
5. **Kept: Hinata and Spasm ahead of the find tier** (your 08-28 revision). This answers the other recorded
   suspect: putting the find tier first cost +0.0113/+0.0120 on its own.

In 30 single-game probes, generic vs your order: 5 games play differently and 0 change the win turn. That is an
illustration, not a measurement.

## 5. Questions for the USER

1. **Irencrag reading.** Read "second last" as "right before the payoff it funds" (18, ahead of Soulfire, Opus and
   Crackle), rather than after Soulfire and Opus? *Default: yes.*
2. **Binding order.** Make the order binding inside Ponder/Preordain/EI plans (Hinata opts into
   `OrderOpaqueCastsByRank`)? Without it, your Gamble/EI/Ornithopter positions never play there. *Default: yes.*
3. **Gamble vs the cantrips.** Gamble BEFORE Ponder/Preordain (your position), or AFTER (today's play; it fetches
   with more information)? *Default: before.*
4. **Soulfire -> Irencrag -> Crackle.** Is the scalar 18 enough? That line then runs through Soulfire's deferred
   dig continuation. The alternative is a set-level "Irencrag immediately before the plan's LAST payoff", which
   allows it in one plan. *Default: scalar now; set-level only if games show the line losing.*
5. **Ponder/Preordain.** Stay peers until the deferred interleave? *Default: yes.*

## 6. Generic-order findings

* **The cast-restrictor tier (18) precedes ALL of tier 20.** So a cantrip, tutor or dig in the same plan counts as
  the restrictor's one follower (§4 item 2).
  * Fix: split tier 20 into set-up casts (draw / tutor / dig / cheap) BEFORE the restrictor, and payoffs AFTER it.
  * In other words, the restrictor goes last before the payoff tier. The waste gate's "a payoff that needs the
    float" already defines that tier.
* **Tier 20 ties the {X} sink with fixed-cost payoffs.** An X-damage sink (`x_damage_multiplier > 1`) should be
  last among payoffs.
* **For any deck without `OrderOpaqueCastsByRank`, the rank does not sequence plans holding a draw:**
  * searched play uses the cantrip-first partition;
  * depth 0 keeps hand order.
  * `--cast-order-report` prints ranks for those cards anyway. It should mark them "rank not applied".
* **The report's main column says Crackle `both`,** although `deck_feeds_combat=no` sends everything else to m2.

## 7. Implementation notes

* **Params per slot** (`HinataFullOrderRank`, no names):

  | slot | param | rank |
  |---|---|---|
  | Sol Ring | `mana_rock && !ramp_filter` | 4 (Signet: 5, sideboard only) |
  | Hinata | `hinata_cost_reducer` | 6 |
  | Reality Spasm | `untap_x_mana_sources` | 7 |
  | Gamble | `tutor_to_hand` | 8 |
  | Ponder | `cast_reorder > 0` | 10 |
  | Preordain | `cast_scry > 0` | 10 |
  | Expressive Iteration | `expressive_iteration` | 12 |
  | Ornithopter | `tmpl == ManaDork` | 14 |
  | Irencrag | `max_casts_after >= 0` | 22 -> **18** |
  | Soulfire | `damage_equals_top_mv` | 20 |
  | Magma Opus | `cast_draw > 0` | 21 |
  | Crackle | `x_damage_multiplier > 1` | 23 |
  | never-cast four | `goldfish_inert` | 30+ |

  The land drop's slot 0 is `LandDropCastOrderRank` (:9809), inert unless `MTG_BP_CONDEMN_LAND` is on.
* **Code changes:**
  1. Irencrag's rank becomes 18 unconditionally. This is the existing `MTG_HINATA_IREN_EARLY` arm, already built;
     fold it in.
  2. Add `HinataProvider::OrderOpaqueCastsByRank() { return HinataOrderFullEnabled(); }`. The executor
     (`AIEngine.cpp:6392`) and the rollout (`TurnSolver.cpp:42879`) then rank-sort opaque plans, which overrides
     the partition. Expect d0 movement too.
  3. Flip `MTG_HINATA_ORDER_FULL` only after your sign-off and a held-out A/B.
* **Ready-made arm:** `MTG_HINATA_ORDER_FULL=1 MTG_HINATA_IREN_EARLY=1`, plus `MTG_ORDER_OPAQUE=1` as a stand-in
  for change 2. That flag is global, so use it only in Hinata-only jobs.
* **Engine limitation (Q4).** A scalar rank cannot say "before the LAST payoff of this plan".
  * Dragonstorm does this set-level, with `ArrangeCastOrder` (Irencrag second-to-last before its closer).
  * But both walks' follower counts and `RestrictorPrePayable` count by `CastOrderRank`. An arrangement without
    them would split legality from realisation.
* **Housekeeping:**
  * `HinataProvider::CastOrderTierName` (:9904) was not renumbered after 08-28. With the flag on, the report labels
    Hinata "TUTOR", Spasm "TOP-OF-LIBRARY CANTRIPS", Ponder/Preordain "MANA DORK", and leaves Sol Ring
    unclassified.
  * The rank comment claims "a cantrip cast before her ... paid full price". That is false: Ponder, Preordain,
    Gamble and EI have no targets, so her discount never applies to them.
* **Not observed:** the cantrip-first partition hoisting Irencrag (it has `ritual_float`) ahead of a cantrip, which
  would hand that cantrip Irencrag's one follower. Change 2 removes the hazard either way; pin it with a unit test.
