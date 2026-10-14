# Creature Giving: cast-order draft (2026-10-14)

## 1. Status: **(A) USER-agreed order, shipped default ON**, with one slot that does not play as agreed

* `MTG_CG_ORDER` is `EnvOn("MTG_CG_ORDER", true)` (src/ai/DecisionProviders.cpp:14891-14899). It was adopted 2026-08-19
  together with `MTG_ACQ_RESOLVE`, and the USER ruling is quoted verbatim in docs/design/cast-order-rankings.md
  ("Creature Giving"). The ranks are at DecisionProviders.cpp:14911-14917. The Scrying-before-land hook is
  `LandDropAfterHandLandTutor` (:14923).
* **Delivery gap: Crop Rotation.** The report and the USER order put it at rank 5, the first cast after the land.
  In play it is cast **LAST in the main phase, after Massacre Wurm and Enlightened Tutor**. Every `sacrifice_land`
  cast is left out of the rank sort and replayed in a trailing "sac-land pass", in the executor
  (AIEngine.cpp:6486 filter, 6564-6572 loop) and in the rollout (TurnSolver.cpp:42927 filter, 42955-42963 loop;
  `PlanCastOrder` 8700-8717). Seen in every multi-cast Crop turn of a 24-game probe at the shipped d5/b20 (seed 1001).
  It is also confirmed on hand-built boards at d0 and at d5 (section 4).
* The order-opaque check is clean: no card in the list carries an `OrderingOpaque` param. The ranks govern every
  other cast.

## 2. What plays today

| pos | card(s) | main | rule that put it there |
|---|---|---|---|
| land | land drop | m1 | At d0 the land comes first unless Scrying is payable from the board and no Orchard is in hand; then it is deferred until after the casts (`LandDropAfterHandLandTutor`). At d>0 the land is a searched fold: before all casts, or after Scrying via its acquisition re-solve (seen: gi 10 T4, 14 T5, 16 T3). A Karoo (Azorius Chancery) always goes after the casts. |
| 6 | Sylvan Scrying | m1 | CG rank 6 (`tutor_to_hand`) |
| 8 | Suture Priest, Essence Warden, Soul Warden | m1 | CG watcher rank. **TIE**, decided by plan order. Harmless: each pair yields exactly +1 life whichever goes first. |
| 10 | Birds of Paradise, Varchild's War-Riders | m1 | Generic creature rank. **TIE**. Harmless: Birds is summoning-sick, and War-Riders' gift comes at upkeep. |
| 12 | Hunted Phantasm | m1 | CG giver rank (`etb_opp_creates_tokens`) |
| 20 | Defense of the Heart | m1 | Generic noncreature rank |
| 22 | Enlightened Tutor | m1 | CG rank (`tutor_to_top`) |
| 28 | Massacre Wurm | m1 | CG LAST rank (`etb_opp_creatures_debuff`) |
| **after all** | **Crop Rotation** | m1 | The sac-land pass. The report's rank 5 is never read. |

The **main** column is m1 throughout. `uses_second_main=no`, so the report's m2 labels are inert: all 136 casts in
the 24 probe games happened in MAIN_1. No card here gains from main 2, because the passive opponent never blocks and
no cast feeds combat.

## 3. Proposed order: the USER order, with Crop Rotation actually delivered

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| 1 | Sylvan Scrying (before the land when payable from the board and no Orchard is in hand) | m1 | - | USER: "should go before the land drop if we can, since it can fetch another Forbidden Orchard" |
| - | **land drop** | m1 | - | After Scrying in that case, otherwise first. Before Crop Rotation (USER: "always play land before crop rotation ... more options for sacrifice"). The deck has no draws. |
| 2 | Crop Rotation | m1 | Q2: prefer after the watchers (rank 9), walk back to 6 when its mana is needed | USER: "first as it is mana neutral". It is neutral only when cast BEFORE the spells its fetched Orchard pays for. Its shuffle must also come before ET, and its Orchard's Spirit must enter before Wurm. |
| 3 | Suture Priest, Essence Warden, Soul Warden | m1 | - | USER: watchers before the givers ("Suture Priest should go before Hunted Phantasm"). Tie provably harmless (symmetric +1 life, nothing else interacts in the turn). |
| 4 | Birds of Paradise, Varchild's War-Riders | m1 | - | After the watchers (+1 life each). No in-turn effect, so the tie is harmless. |
| 5 | Hunted Phantasm | m1 | - | After the watchers (5 Goblins = 5 drain per Priest); before Wurm |
| 6 | Defense of the Heart | m1 | - | Upkeep trigger only, so its place in the turn does not matter |
| 7 | Enlightened Tutor | m1 | - | USER: "after all other tutors to avoid the library shuffle". That means after Scrying, Crop Rotation and the fetchland crack. |
| 8 | Massacre Wurm | m1 | - | USER: LAST, "to ensure that enemy creatures are created first". Every Goblin and Spirit, including Crop's Orchard Spirit, enters before the sweep. |

In rank terms: Scrying 5, Crop 6 (the two numbers swap), watchers 8, creatures 10, Phantasm 12, Defense 20, ET 22,
Wurm 28. The swap is only needed once Crop is delivered. Today the shipped turn (Scrying, land, then Crop) is right
by accident, because Crop always goes last.

## 4. What changes and why it matters

1. **The USER's own T1 line, land then Crop Rotation then Birds, cannot be played.** I built a board: Forest in hand
   with Crop + Birds, Orchards in the library. At both d0 and d5 the engine plays Forest then Birds and leaves Crop
   for T2. CheckLine says `illegal: can't pay {G} for 'Birds of Paradise'`. There are two blockers:
   * Crop's fetched untapped land gets no affordability credit (TurnSolver.cpp:71775 refuses one on purpose).
   * There is no re-solve after Crop.

   Four of the 24 sampled openings had this line available (non-Orchard land + Crop + a 1-drop: gi 2, 3, 15, 22).
   It was never played. More generally, Crop cast last costs a real {G}, so on every turn where the casts plus Crop
   come to the available mana plus one, a cast is lost. Example: two lands with Crop + Suture Priest, where the
   Orchard pays the {W}.
2. **Enlightened Tutor's card gets shuffled away.** Seed 1001 gi=20 T4 went Essence Warden, then ET (Defense #16 to
   the top), then Crop Rotation (shuffle). T5 drew Defense #14: the tutored copy was buried, and another copy happened
   to come up. On a hand-built board (ET + Crop, a single Defense in the library), d0 and d5 both play ET then Crop,
   and the next draw is filler. This breaks the USER's ET rule every time Crop shares the turn.
3. **Massacre Wurm sweeps before Crop's Spirit exists.** Hand-built board: 7 lands, 6 opponent tokens, opponent on
   14, hand Wurm + Crop. d0 and d5 both play Wurm then Crop. The fetched Orchard's Spirit survives, the opponent ends
   on 2, and the kill comes on **T6**. The control board, with that 7th creature present before the sweep, kills on
   **T5**. That is a whole turn lost against the USER's "Massacre Wurm should be last".
4. **What today's accident gets right, and should keep (Q2).** Because Crop goes last, Suture Priest and the Wardens
   are already in play when its Orchard Spirit enters. That gives +1 drain per Priest and +1 life per Warden. In 5 of
   the 6 multi-cast Crop turns in the sample a Warden was already down. That was only +1 life each; no Priest shared
   a turn with Crop in the sample. Moving Crop to "first" gives this up whenever the line is payable either way.

## 5. Questions for the USER

1. **Deliver Crop Rotation at its agreed slot (right after the land, before the watchers) instead of last?**
   Default: **yes**. This fixes items 1-3.
2. **When the line is payable either way, should Crop wait until after Suture Priest / the Wardens (they bill its
   Spirit), and go first only when its mana or colour is needed?** Default: **yes, as a second step**. This is the
   same producer-funding ladder as Mirrorwing's Gold Rush.
3. **Should Sylvan Scrying go before Crop Rotation when both are cast?** Default: **yes**. It is the only order that
   keeps both of your rules (Scrying before the land, the land before Crop) on a turn where the drop is held for
   Scrying.
4. **Should a Forbidden Orchard land drop wait until after the watchers when they are payable without it, while
   staying before Wurm?** That is +1 drain per Priest. The sample missed it 3 times (gi 8 T2, 11 T2, 12 T5). I did
   not check whether any of those cost a turn. Default: **record it as an open item, don't build it now**.
   The land drop is not a cast, so a scalar rank cannot put it in the middle of the sequence.

## 6. Generic-order findings

* **The sac-land pass overrides every provider rank for `sacrifice_land` casts, in both worlds.** That is right for
  Shard Volley (burn: tap the land, then sacrifice it). It is wrong for a cost that puts a land back
  (`tutor_land_to_battlefield`). These are the only two `sacrifice_land` cards in the pool.
* **`--cast-order-report` prints a rank for sac-land casts that play never reads.** So the report's "cannot drift
  from play" guarantee fails for this class. The report's land line also says "nothing wants to precede it -> FIRST"
  and ignores `LandDropAfterHandLandTutor`.
* **Same-turn mana from a land put onto the battlefield untapped is neither credited by the enumerator nor
  re-solved.** The acquisition re-solve (`MTG_ACQ_RESOLVE`) covers only `tutor_to_hand`.
* **The land drop has only two positions:** first, or after a breakpoint (a Karoo goes after everything). A
  "giver" land (Orchard) wants "after the watchers, before the sweeper". This is the land-two-position item.

## 7. Implementation notes

* **What identifies each slot** (house style): Scrying = `tutor_to_hand` + `tutor_types:[Land]`. Crop =
  `tutor_land_to_battlefield` (+`sacrifice_land`). Watchers = `opp_creature_enters_life_loss` /
  `any_creature_enters_lifegain`. Phantasm = `etb_opp_creates_tokens`. ET = `tutor_to_top`. Wurm =
  `etb_opp_creatures_debuff`. Birds, War-Riders and Defense keep their generic ranks.
* **Rank change:** swap 5 and 6 at DecisionProviders.cpp:14911-14912 (and the tier names at :14958-14959).
* **Engine changes for Q1:** take `tutor_land_to_battlefield` out of the sac-land pass at every lockstep site:
  * AIEngine.cpp: the clean filter at 6486, the opaque filter at 6386, and the loop at 6564-6572.
  * TurnSolver.cpp: `PlanCastOrder` 8705-8716, `ApplyPlanDirect` 42874 / 42927 / 42955-42963.

  The sacrifice stays at cast time and goes to a tapped land first (`SacrificeLandCandidates`). So a Crop cast first
  sacrifices the land that paid for it, which is what makes it mana-neutral.
* **Re-solve for item 1:** add an acquisition re-solve in the `tutor_land_to_battlefield` branch
  (TurnSolver.cpp:42075) plus its executor twin, gated with `AcqResolveEnabled`. The continuation then spends the
  Orchard, and nobody has to invent an enumerator credit.
* **Q2 needs more machinery:** `CastOrderFallbackRanks` returning {9, 6} for Crop. The ladder only runs under
  `OrderOpaqueCastsByRank` (or `MTG_ORDER_RANGE`), and its payability projection must count the fetched land's mana.
  Without that it never walks.
* **Minor, d0 and rollouts only:** the held drop (AIEngine.cpp:6580-6597) is played after ALL first-pass casts. So a
  Wurm, ET or Crop in that pass comes before the fetched Orchard, or before a fetchland's shuffle. The USER's rule
  puts only Scrying before the land; playing the drop right after the hand-land tutor resolves would match it.
* **Stale text, no play effect:**
  * DecisionProviders.h:1480 and DecisionProviders.cpp:14887 say "default off pending measurement"; the flag is ON.
  * The rank-6 tier text calls Scrying-before-land an open item, but it is delivered (d0 hook + d5 re-solve).
  * cast-order-rankings.md says "the ranks are the whole delivery". That is not true for Crop.
* **These change play.** Adopting them is the USER's call, with the standard bar (held-out, every deck <= 0).
