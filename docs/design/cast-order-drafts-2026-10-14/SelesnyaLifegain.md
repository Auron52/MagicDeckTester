# SelesnyaLifegain -- cast-order draft (agent proposal, 2026-10-14)

Deck: Elf ramp (Priest of Titania / Elvish Archdruid scale with Elves) into lifegain payoffs (Verdant
Sun's Avatar makes every entering body a gain; Ageless Entity / Nykthos Paragon turn gains into
counters; Accomplished Alchemist turns them into mana; Blossoming Bogbeast into a team pump), closed by
Genesis Wave / Craterhoof. No draw, cantrip or opaque card in the 60, so every set takes the rank sort
(`route=clean` in every probe trace).

## 1. Status: **(D) generic tiers only**
* `SelesnyaLifegainProvider` (src/ai/DecisionProviders.h:2681) overrides only the cleanup-discard hooks.
  It has no `CastOrderRank`, no `MainPhaseOverride`, and no order flag (no `MTG_SELESNYA_*ORDER` / `MTG_SL_*ORDER`).
* docs/design/analysis-SelesnyaLifegain.md:279 (deferral 7) parks "a `CastOrderRank` putting the
  lifegain ENGINE before the PAYOFFS" as unmeasured. It was never proposed to you.
* Binding USER input: the dork-growth rule (2026-08-20, analysis-StompySurprise.md:154: *"play every elf
  we can ... without tapping scaling dorks or Wirewood Lodge. Then ... every elf remaining with scaled
  mana"*). It ships generically as `MTG_DORK_GROWTH`, default ON (DecisionProviders.cpp:3167, tier 9).
  Your card views are in selesnyalifegain-discard-policy.md. **No USER order exists for this deck, and none is OFF.**

## 2. What plays today
| pos | card(s) | main | rule |
|---|---|---|---|
| L | land drop: first. Selesnya Sanctuary is played after the casts (`karoo_deferred`). Blossoming Sands is played FIRST | m1 | no cantrip in the deck, so the land goes first |
| 9 | Llanowar, Mystic, Priest, Wellwisher, Archdruid, Alchemist: **TIE, hand order** | m1 | DORK_GROWTH tier. Applies only while an untapped, non-summoning-sick Priest/Archdruid is on board **at sort time**, which is after the whole-turn prepay (AIEngine.cpp:6207 runs before the sort). So the tier switches on or off depending on which dorks the prepay happened to tap |
| 10 | Avatar, Ageless, Bogbeast, Paragon, Craterhoof (+ every Elf whenever tier 9 is off): **TIE, hand order** | m1 | generic creature tier |
| 20 | Feed the Clan, Genesis Wave: **TIE, hand order** | m1 | generic noncreature tier |
| after | Wellwisher {T}, Blighted Steppe sac, Wirewood Lodge untap | m1 | trailing activations, battlefield order |

The report labels Llanowar, Mystic, Priest and Alchemist **m2**. Those labels are inert: `uses_second_main=no`
because `MTG_SL_SECOND_MAIN` defaults OFF (GoldFishRunner.cpp:234), so the deck plays one main.

## 3. Proposed order (one main, all m1)
| pos | card(s) | main | range | why |
|---|---|---|---|---|
| L | **untapped** land drop | m1 | - | No draws, so nothing wants to precede it. Mana for the turn |
| 1 | Verdant Sun's Avatar | m1 | [1..7] | ENABLER: "whenever this creature or another creature you control enters, you gain life equal to that creature's toughness". Every later body becomes a gain event, which feeds Ageless counters, Bogbeast's X and the Alchemist. Walks to 7 only when the turn cannot be paid without the Elves' growth |
| 2 | Ageless Entity | m1 | [2..7] | WATCHER: "whenever you gain life, put that many +1/+1 counters". It must be down before the gains it converts. It goes after the Avatar: Avatar-first turns Ageless's own 4-toughness entry into 4 life, while Ageless-first only buys 1 more counter on a summoning-sick body |
| 3 | Elvish Archdruid | m1 | [3..5] | LORD: "other Elves get +1/+1". The first Elf down, so every later Elf enters 1 toughness bigger (+1 life per Avatar). Falls to its cheapest-first slot when the mana needs it |
| 4 | Llanowar Elves = Elvish Mystic | m1 | - | Growth Elves cheapest first. That is your rule taken literally: cast the most Elves before a scaled dork taps. Identical cards |
| 5 | Priest of Titania = Wellwisher | m1 | - | Both {1}{G} 1/1 Elves, and both summoning sick, so their {T} abilities are off this turn. Order provably irrelevant. (Archdruid's fallback slot is here) |
| 6 | Accomplished Alchemist | m1 | - | The dearest Elf. With an Avatar out it is the biggest Elf gain (toughness 5, 6 with the lord), and it lands on a waiting Ageless |
| 7 | *(Avatar, then Ageless: fallback slot)* | | | Your Elves-first order, used when the mana needs it |
| 8 | Blossoming Bogbeast | m1 | - | Plain body: its trigger is on attack, and it cannot attack this turn |
| 9 | Genesis Wave | m1 | - | After every hand body: its X is sized on the Elves grown before it (your s4_gi3 T6 report), and Avatar/Ageless see every entrant. With max-X it only shares a main with growth-funded casts |
| 10 | Nykthos Paragon | m1 | - | After the bodies: its own entry (6 life with an Avatar out) lands on the full board, and an unused copy is still waiting for Feed and the trailing Wellwisher/Steppe gains |
| 11 | Craterhoof Behemoth | m1 | - | Last creature: "+X/+X where X is the number of creatures you control" is counted on resolution |
| 12 | Feed the Clan | m1 | (0, blocked: Q3) | Last: ferocious is read on resolution (power 4+ means 10 instead of 5), and it lands after the Paragon. Ideal with an Alchemist that can tap: FIRST, but the engine cannot use that today |
| end | **tapped** land drop: Selesnya Sanctuary (as today) **and Blossoming Sands** | m1 | - | They enter tapped, so playing them early adds no mana. Sands' 1 life is a gain EVENT, and played first it spends every unused Paragon's once-a-turn trigger on +1. Played after the casts and activations it costs nothing |

Ties exist only between cards that are identical for the turn (rows 4-5, and copies of one card).

## 4. What changes and why it matters
Repros: `--seed <s> --game-index <i> --games 1 --depth 3 --budget-ms 10`. These are regression seed 2002, game i, with
per-game seed s = 2002+i; traced with `MTG_USER_ORDER_TRACE=1`.
1. **Avatar ahead of the Elves.** Today the Avatar never goes ahead on purpose. A surplus turn has the growth tier
   put the Elves first even though the prepay has already paid. s=2006 i=4 T5: 21 mana for a 9-mana bill,
   Priest of Titania before Avatar, so the Priest's entry gained nothing, with Bogbeast attacking (-1/-1 per attacker).
   An exact turn falls to hand order. s=2095 i=93 T5: pool 10 = bill 10, Mystic before Avatar, which loses 2 life
   and 2 counters on an attacking Ageless. **Why the range is needed:** s=2073 i=71 T4 has 7 un-grown mana for a
   10-mana Wellwisher+Mystic+Avatar. Only Elves-first pays there, and the ladder falls back to exactly that.
2. **Blossoming Sands after the casts.** s=2080 i=78 T6: two unused Paragons out. Sands played first spent both on
   its 1 life (+1/+1 twice). The Paragon cast next took the Avatar's 6 alone, for +8/+8 per creature in total.
   Deferred, the single 6-life event feeds all three Paragons: **+18/+18 on every creature.**
3. **Paragon after the bodies and the Avatar.** s=2090 i=88 T5, a hand-order tie: Paragon, then Avatar. The two Paragons
   took the Avatar's own 5 (+10/+10 total). Avatar first instead gives +5, then +6 from the new Paragon's entry:
   +11/+11 and 6 more life. In s=2049 i=47 T5 the prepay had tapped the Priest and Archdruid, so the growth tier was
   off and hand order cast the Paragon before two Alchemists. They missed its +6/+6.
4. **Craterhoof last.** s=2032 i=30 T4, a hand-order tie: Craterhoof, then Ageless Entity. X counted 6 creatures, not 7:
   -1/-1 on every attacker.
5. **Feed the Clan with an Alchemist that can tap (blocked).** s=2095 i=93 T5: Feed's 10 resolved after the prepay had
   already tapped the Alchemist for its floor of 1. Cast first, the Alchemist taps for 10. No order can reach
   that line today (section 7e).

## 5. Questions for the USER
1. **Avatar (and Ageless) ahead of the Elves** whenever the turn pays without the growth, with your 2026-08-20 Elves-first
   order as the fallback when the mana needs it? *Default: yes.*
2. **Blossoming Sands after the casts and activations** (no mana lost; avoids s=2080's +8 vs +18)? *Default: yes.*
3. **Your Alchemist package** ("a good keep with lifegain burst + something expensive like Genesis Wave",
   2026-10-10) cannot be played inside one main. Enumeration, Genesis Wave's X sizing and the order walk all credit
   the Alchemist at 1, so "Feed the Clan, then the Alchemist's X pays the Wave" is unfindable at any budget. Should we
   build that same-turn credit, then rank Feed the Clan FIRST while an Alchemist can tap? *Default: yes, as an engine
   item; until then Feed stays last.*
4. **Nykthos Paragon's "you may" is greedy**: every copy spends on the turn's first gain. That is what makes Sands,
   and with an unused Paragon out the Avatar's 5, pre-empt Feed's 10 and the Steppe's 2N. Should we fix the spend
   (hold for the plan's biggest gain) as its own heuristic sweep, rather than bend the order around it?
   *Default: yes.*
5. **One main.** An agent switched `MTG_SL_SECOND_MAIN` OFF on 2026-09-30: 120/120 identical win turns at 2.15x
   CPU (analysis-SelesnyaLifegain.md "Second main"). Keep it off, so every cast is m1? *Default: yes.* If it is ever
   re-armed, the Elves and the Avatar must stay m1: they are this turn's growth and Bogbeast's X, so the report's
   m2 labels would be wrong.

## 6. Generic-order findings
* **The growth tier reads the board after the whole-turn prepay** (DecisionProviders.cpp:3167 vs AIEngine.cpp:6207).
  On a surplus turn it still forces Elves first, where it buys nothing and costs triggers (s=2006). On an exact turn
  the prepay taps the dorks and the tier disappears (s=2049). It belongs at the cost-efficient end of a range:
  "needed only when the un-grown board cannot pay". It should not be a fixed rank, and it should not key on `tapped`.
* **Within the growth tier the tie is plan order.** Your rule maximises the Elves cast before a scaled dork taps,
  which means cheapest first.
* **Creature ties go to hand order** for three classes that can be derived from params and want a fixed position:
  * an own-creature ENTER watcher goes first: `own_creature_enters_lifegain_toughness`. `MTG_WATCHER_ORDER` covers
    only `dragon_ping_on_enter` / `haste_on_flying_enter`, and it is OFF;
  * a lifegain-counter watcher goes before the gains: `lifegain_self_counters_that_many`;
  * `etb_team_pump_per_creature` goes LAST among creatures. Craterhoof is also in StompySurprise.
* **Enters-tapped lands are played first** unless they are Karoos. A tapped land with no same-turn mana should come
  after the casts unless its ETB is information (scry/surveil). A gain-land's ETB is an event that wants to land last.
* **No same-turn credit for life-scaled mana** (`mana_per_life_gained`), in any order walk or enumeration.

## 7. Implementation notes
* **Hook.** `SelesnyaLifegainProvider::CastOrderRank`, plus `CastOrderTierName` for the report, plus
  `OrderOpaqueCastsByRank() { return true; }`. No card here is OrderingOpaque, so this only arms
  `ApplyCastOrderRangeLadder` at the clean sites (gate at ManaPayment.cpp:3943; Mirrorwing precedent).
  Ranged cards return their cost-efficient rank under `IdealOrderSuppressScope`, so `CastOrderRangeOf` and
  `--cast-order-report` show `[ideal..ce]`.
* **Slots by params, with suggested numbers:**

  | slot | params | rank |
  |---|---|---|
  | Avatar | `own_creature_enters_lifegain_toughness` | 40, ce 61 |
  | Ageless | `lifegain_self_counters_that_many` | 41, ce 62 |
  | Archdruid | a scaled dork (`mana_per_creature_subtype`) that is also a lord over that subtype (`subtypes_affected`, `power_bonus`) | 45, ce 53 |
  | Elves | a creature with the deck's scaled-dork subtype (`mana_per_creature_subtype` = Elf), whatever is tapped | 50 + mv (Llanowar/Mystic 51, Priest/Wellwisher 52, Alchemist 54) |
  | other creatures (Bogbeast) | none | 64 |
  | Wave | `reveal_x_put_permanents` | 70 |
  | Paragon | `lifegain_each_own_creature_counters_that_many` | 74 |
  | Hoof | `etb_team_pump_per_creature` | 78 |
  | Feed | `cast_lifegain > 0` | 80 |

  Rank conditions must not read `tapped`, because the sort runs after the prepay.
* **(a) The ladder's walk does not credit dork growth.** `FirstUnpayablePos` uses the un-grown `AvailableManaPool`, so a
  growth-dependent turn drops straight to the terminal rung (every ranged card at its fallback). It never stops at an
  intermediate rung (e.g. Avatar after one Elf). That is conservative: it never drops a cast and may forgo a trigger.
  When the prepay pays, order cannot affect payment, so the ideal order is free.
* **(b) Lockstep with the growth credit.** EnumeratePlans' growth credit models the selection-order prefix.
  Cheapest-first realises a prefix at least as long, so the executor gets at least what the credit assumed (safe
  direction). The rollout follows through the shared `CastOrderLess` / ladder.
* **(c) Deferring Sands is land TIMING, not a rank.** It means generalising `karoo_deferred` (AIEngine.cpp:3814/6766,
  the ApplyPlanDirect twin, and the `MTG_KAROO_BP_LOCKSTEP*` guards) to `enters_tapped && etb_lifegain` (or every
  enters-tapped land without a scry ETB). Order: casts, then trailing activations, then the deferred drop.
* **(d) Wave co-castability.** Genesis Wave's X is the whole pool's max (TurnSolver.cpp ~23113), so the Wave's slot
  against Paragon/Hoof/Feed only matters with a growth surplus or `MTG_UNPRUNED=xspell`.
* **(e) The blocker for Feed-first.** Feed-first with an Alchemist needs a per-subset lifegain-growth credit (the
  Alchemist card's own PARTIAL). It has to go in EnumeratePlans, in the Wave's X sizing (`gpool`, ~23110), and in
  `CreditCastOutput` (ManaPayment.cpp ~2885). Until that exists, ranking Feed first only throws away ferocious.
* **(f) What a scalar rank expresses badly.**
  * Paragon vs Craterhoof: with an Avatar out, Paragon-after gives Craterhoof +6, while Paragon-before gives +1 per
    attacker. Both in one main is rare (14 mana), so Craterhoof goes last.
  * Avatar with an unused Paragon out: the better position depends on the spend heuristic (Q4).
* **Not covered here:** trailing-activation order (Wellwisher / Steppe / Lodge, `ActivationOrderRank` 0).
