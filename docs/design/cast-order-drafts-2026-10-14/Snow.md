# Snow -- cast-order draft (2026-10-14)

## 1. Status: **(A) USER order shipped, default ON**

* `MTG_SNOW_CAST_ORDER` is `EnvOn(..., true)` (`src/ai/DecisionProviders.cpp:4241-4245`). Turned ON 2026-10-04 in `9be68a01`
  after you objected (*"we are still using the generic order for snow? We shouldn't be."*). Held-out sweep in that commit, vs
  generic (seeds 9101-9104): d0 -0.008, d3 -0.013, d5 -0.020, 4 of 4 seeds better in every cell.
* The report (`reports/Snow.txt`) prints ranks 1 / 2 / 109...159 / 302. Those are the ON-path values. The OFF path would print 3 / 4 plus the generic tiers.
* Sub-levers, all yours: FIXER on (09-15), SPLIT on (09-15, *"split them up in any order"*), WATCHER on (10-04),
  ACT_ORDER + ACT_AUGUR_FIRST on (10-04, *"augur costs quite a lot less to activate"*), CONDEMN on.
  DRAW_EARLY is **OFF**. You asked to try it on 09-18 and it was measured on 09-25, but the result was never put to you (Q2).
  TAPDRAW_EARLY is off. It measured byte-identical to DRAW_EARLY on this list.
* **Stale text that could repeat the 09-25 mistake.** `snow-intractable-games.md` §7 still says *"`MTG_SNOW_CAST_ORDER` stays OFF"*
  and has no superseded note. The code comment heading the flag (`DecisionProviders.cpp:4202-4234`) still reads "DEFAULT OFF ... MEASURED
  NEUTRAL", and `:4329` still reads "THE SHIPPED ORDER is the generic one". The details are in section 7.

## 2. What plays today

| pos | card | main | rule that put it there |
|---|---|---|---|
| land | land drop | m1 | Played before every cast. A "defer" plan (`TurnSolver.cpp:55017`) lets a cast-draw go first, and its continuation then plays the land. Land condemnation is OFF (`:4833`). |
| 1 | Arcum's Astrolabe | m1 | fixer (`any_color_filter`) |
| 2 | Marit Lage's Slumber | m1 | enter-watcher (`snow_enter_scry`) |
| 109 | Boreal Druid | m1 | cheapest first: 100 + 8*mv + 2*role + big; role 0 = mana source |
| 111 | Frost Augur | m1 | mv 1, permanent |
| 117 | Coldsteel Heart | m1 | mv 2, rock |
| 127 | Jorn, God of Winter | m1 | mv 3 |
| 135 | Abominable Treefolk | m1 | mv 4 |
| 158 / 159 | Rimescale Dragon / Rimefeather Owl | m1 | mv 7, split by power (>= 5 first) |
| 302 | Ice-Fang Coatl | m1 | draw band last (`etb_self_draw`) |
| -- | Skred | -- | `NeverCast` (USER 10-03) |
| trailing | Owl ice counter (0), Frost Augur look (10), Scrying Sheets look (20) | | `ActivationOrderRank`. Augur goes before Sheets. |

* **No ties.** Every distinct card has its own rank.
* **The m1/m2 column in the report is inert.** It prints m2 for Druid, Jorn, Dragon and Coatl, but `uses_second_main=no` makes
  `MainPhaseFilterActive` return false (`TurnSolver.cpp:20388`). On a turn where Jorn attacks, a searched main 2 opens and offers the whole hand again
  (`UntapSecondMainLive`, `SpellEffects.h:15227`). So in practice every card is cast in m1.
* **No order-opaque bypass.** No card in the list carries an `OrderingOpaque` param (`ManaPayment.cpp:2722`). The draws ride
  `etb_self_draw`, not `draw`. I probed 12 single games at d3/b10 and 60 at d0 with `MTG_USER_ORDER_TRACE`. Every set ran `route=clean`
  (or `cont-bp` / `replay` for continuations), and every multi-cast set executed in rank order.

## 3. Proposed order: your order, with ONE swap (Q1)

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| land | land drop, before the casts | m1 | -- | Free mana. Draw-before-land is still reachable through the defer plan. gs=1005 T2 did exactly that: Astrolabe drew an Island, then Augur was cast, then the Island and a 2nd Astrolabe were played in the continuation. |
| 1 | **Marit Lage's Slumber** (today 2) | m1 | -- | *"Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1."* Every later snow permanent, **Astrolabe included**, then scries. |
| 2 | **Arcum's Astrolabe** (today 1) | m1 | (the fixing case is reached by search, see section 4) | It still precedes every coloured spell it can fix except Slumber. Its ETB draw now resolves after Slumber's scry. |
| 109 | Boreal Druid | m1 | -- | {G}, summoning-sick: funds nothing this turn. It is a snow permanent, so it gives one scry. |
| 111 | Frost Augur | m1 | -- | {U}. Its {T} look is not usable until next turn. Druid and Augur cannot interact: both are sick, neither has an ETB, each gives one scry, and the joint prepay decides the payment. |
| 117 | Coldsteel Heart | m1 | -- | Enters tapped. It picks its colour from the hand's remaining demand, so it is better placed after the 1-drops. |
| 127 | Jorn, God of Winter | m1 | -- | 3/3. Its untap only fires on a later attack. |
| 135 | Abominable Treefolk | m1 | -- | Its CDA counts snow permanents live. Its ETB tap is inert. |
| 158 / 159 | Rimescale Dragon / Rimefeather Owl | m1 | -- | Neither has a cast or ETB trigger that reads the other, so their order cannot matter. |
| 302 | Ice-Fang Coatl | m1 | -- | Draw last (your 09-15 rule). With Slumber out, every commitment's scry happens before this draw. Re-planning after the draw is still reachable: plan {Coatl} plus its site-10 continuation (Q2). |
| -- | Skred | -- | -- | Never cast. |
| trailing | Owl ice, Augur look, Sheets look | -- | -- | Unchanged. The line "dig, then ice with the leftover mana" is reachable through the dig's site-8 continuation, and activation condemnation is off (`MTG_BP_CONDEMN_ACTIVATION=0`). |

## 4. What changes and why it matters

1. **Slumber before Astrolabe. This is the only rank change.**
   * Today, a {Slumber, Astrolabe} plan always sorts Astrolabe first. So Slumber never scries on the Astrolabe entering, and the Astrolabe draws blind.
   * Nothing can express the other order on a non-Jorn turn: there is no second main.
   * Probe (d0, `MTG_SNOW_ORDER_FIXER=0`, the only existing lever that puts Slumber first):
     - **gs=1013 T6.** Shipped: Astrolabe drew Rimescale Dragon, then the scries bottomed Skred and kept Druid. Slumber first: its scry bottomed
       Rimescale Dragon, the Heart's bottomed Skred, the Astrolabe's kept Boreal Druid, and the Astrolabe **drew the Druid instead of the Dragon**.
     - **gs=1042 T3.** 3 scries instead of 2.
     - Both games won on the same turn at d0, so the effect is small and has not been measured at depth.
   * The fixing case still works. When Slumber's {U} needs the filter, plan {Astrolabe} opens its site-10 draw breakpoint
     (`MTG_BP_PUT_IN_HAND` is on), and the continuation casts Slumber. Slumber cannot be condemned there: it was unpayable at the root, and the
     Astrolabe is a new mana source, so `BpTurnManaSettled` re-admits it.
   * Your 10-04 reason is kept: *"only Arcum's Astrolabe is relevant there ... stick it first"*. It still goes ahead of every coloured
     spell it can fix except Slumber, and the line where it fixes Slumber stays reachable through its own draw breakpoint.
2. **Not a rank change: a missed scry on the land drop (engine limit, Q3).**
   * In gs=1002 T4 and gs=1004 T4 (d3/b10), Highland Weald entered tapped and paid for nothing that turn. It was played *before* the same-turn
     Slumber, so its scry was lost.
   * "Slumber, then the land" cannot be expressed. The drop is played before every cast, or only inside a draw's continuation.
     `LandDropCastOrderRank` only feeds land condemnation; it is not an execution position.
3. **Not a rank change: Coatl draw-early (Q2).**
   * Your 09-18 trial measured slightly better than draw-last on 09-25: -0.0020 at d0, -0.0066 at d3, 0 at d5. That was 4 seeds, on the engine before Augur-first and before put-in-hand.
   * On this deck, draw-last has a reason the generic principle 1 lacks: with Slumber out, every commitment's scry comes before Coatl's draw.

## 5. Questions for the USER

1. **Slumber at 1, Astrolabe at 2?** I recommend yes. Until you answer, your current order (Astrolabe first) stands.
2. **Coatl: keep draw-last, or switch to draw-early?** Default: **keep draw-last**. If you want draw-early, both arms get re-measured on the current engine in one pooled batch.
3. **Build a "land after a same-turn Slumber" slot, starting with the enters-tapped lands (Rimewood Falls, Highland Weald)?** It is mana-neutral for them this
   turn. Default: **no, record it as a known limit**. Say "build" and the Karoo deferral is the model to copy.

## 6. Generic-order findings

* **Watchers are ranked after what they watch.** The generic tiers put a noncreature enter-watcher at 20, after the rocks (5) and creatures (10) it watches. That was s4004 gi28,
  which went 7 -> 6 at d8b0 once fixed. Fix: a watcher-first tier keyed on enter-watcher params, generalising Creature Giving's [8] WATCHER.
* **Not every rock should be in the rock tier.** Tier 5 "rock first" holds only for an untapped rock that adds net mana. Coldsteel Heart (`enters_tapped`) and Astrolabe (a pure converter:
  `any_color_filter` + `filter_no_free_colorless`) fund nothing extra this turn. Fix: use the predicate the rock-ramp stamp already uses
  (`mana_rock && !enters_tapped`, `TurnSolver.cpp:24668`) and add `!filter_no_free_colorless`.
* **ETB-draw permanents are not treated as draws.** The generic tiers do not count `etb_self_draw` permanents as draws, so principle 1 never reaches them. The report even prints "no
  cantrip in the deck" on a list with 8 cast-draws.
* **The report mislabels provider ranks.** `--cast-order-report` labels them through the generic tier table: Slumber at 2 prints as
  "DRAW ... (MTG_IDEAL_ORDER)" and the rest as "(unclassified)". `SnowProvider` should define `CastOrderTierName`.
* **The land-drop slot is not where the land is played.** `LandDropCastOrderRank` is not an execution slot, so "watcher, then land" cannot be expressed on any deck with an ETB or landfall
  watcher.

## 7. Implementation notes

* **Slot params (no names):**
  - fixer = `any_color_filter`
  - watcher = `snow_enter_scry > 0`
  - draw band = `etb_self_draw || cast_draw` (Coatl; the fixer clause catches Astrolabe first)
  - commitment key = `100 + 8*mv + 2*role + (power >= 5 ? 0 : 1)`, where role 0 = `mana_rock` or ManaDork, role 2 = instant/sorcery
  - Skred = `damage_equals_snow_permanents`, via `NeverCast`
  - activations by `tap_draw_cost`: creature 10, land 20
* **Q1 code change** (`SnowProvider::CastOrderRank`, `DecisionProviders.cpp:4353-4363`):
  - Return 1 for `snow_enter_scry` and 2 for `any_color_filter`, behind a heurarm flag so the A/B can run in one pooled batch.
  - `SnowDrawBandBase()`'s early value of 2 then needs to be at least 3. Coatl lands at 4 either way.
  - The OFF-path amendment (3 / 4, `:4339-4343`) should swap too.
* **Do not lean on `MTG_PAYABLE_ORDER` for the fixing case.**
  - The trace's walk printed `pays=0` (unpayable at position 0) on land-first Astrolabe sets that paid in full: gs=1001 T5, gs=1013 T6, gs=1042 T3.
  - I did not chase the root cause. Play was unaffected, because the executed order was your order every time.
* **Q3 has no rank that can express it.** The land drop would need the Karoo deferral's apply plus executor twin (`karoo_deferred`), keyed on "enters tapped, and a
  watcher is cast this phase".
* **Side observation, not a cast-order issue.** At gs=1006 T4 main 2 (a Jorn turn), the plan {Astrolabe, Coldsteel Heart, Ice-Fang Coatl} was admitted on 4 lands with `pays=2`, and Coatl
  failed to pay. That is 5 mana of cost, so no order pays it. It looks like a subset admitted through the Astrolabe's credited mana, and it is not root-caused.
* **Stale text to fix when this code is next touched:**
  - `DecisionProviders.cpp:4202-4234`
  - `:4258-4260` "(measured-off) Snow order"
  - `:4329`
  - `:4414-4421` and `DecisionProviders.h:2287` "Sheets before Augur" (Augur has gone first since 10-04)
  - `snow-intractable-games.md` §7
  - `snow-main2-non-jorn-gain.md:125`
  - Also: Snow has no section in `cast-order-rankings.md`.
* **Artifacts.** A rank change changes play. The keep table (adopted 10-09 under this order) is fitted to play and goes to the staleness queue. No value leaf
  ships for Snow.
* **Probe scripts and logs:** `build/scratch/castorder/probe_Snow/` (`run_probe.sh`, `run_d0.sh`, `run_two.sh`, `run_nofixer.sh`).
