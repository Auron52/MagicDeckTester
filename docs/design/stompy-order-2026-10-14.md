# StompySurprise: the USER-approved cast order, built and measured (2026-10-14)

**Status (2026-10-14 night): BUILT (commit `e1f1bba1`), measurement PARTIAL -- paused for the USER's value-leaf run; see RESUME at the end.** Defaults in the commit: `MTG_STOMPY_ORDER`, `MTG_STOMPY_ORDER_SEARCH`, `MTG_STOMPY_TOP_RESOLVE` all ON (=0 hatches), PENDING the bar below. Self-contained. The USER's verbatim 2026-08-21 order
and rulings are in `docs/design/cast-order-rankings.md` §StompySurprise; the 2026-10-14 draft it was
amended from is `docs/design/cast-order-drafts-2026-10-14/StompySurprise.md`.

The USER, 2026-10-14: *"Let's try out your stompy suggestion. It seems pretty close to what I was looking for
and we can fix any issues that show up."* And, the same day: *"Let's fully order the cards in each list
inasmuch as possible. I don't like the randomness of choosing the order."*

## 1. What was built

`StompyProvider` (`src/ai/DecisionProviders.{h,cpp}`), all main 1 (the deck is single-main). Ranks are x100
and come from card params, never names; `[n]` is the USER's 2026-08-21 tier number.

| # | rank | card | slot rule |
|---|---|---|---|
| 0 | land | the land drop | first (no cantrip in the deck) |
| 1 | 300 | Elvish Mystic, Fyndhorn Elves, Llanowar Elves | USER [1]; identical text, **by name** |
| 2 | 500 | Sol Ring | USER [2] |
| 3 | 600 | Priest of Titania | USER [3] (scaler, mana value 2) |
| 4 | 700 | Elvish Archdruid | USER [4] |
| 5 | 800 | Call of the Wild (the cast) | USER [5]; activations trail every cast |
| 6 | 1200 / 1500 | Turntimber Symbiosis | USER [7]: on the current top (1200) by default; after the tutor (1500) when the PLAN casts a mid tutor. SEARCHED |
| 7 | 1300 | Natural Order, early | USER [8]: only when the PLAN casts a mid tutor. SEARCHED |
| 8 | 1400 | Worldly Tutor, mid | USER [9]: only when a Turntimber in the PLAN takes the stacked card. SEARCHED |
| 9 | 1600 | Vaultborn Tyrant | USER [10] |
| 10 | 1700 | Ghalta, Stampede Tyrant | puts every creature card in hand onto the battlefield |
| 11 | 1750 | World War Hulk, early | no Craterhoof / Ghalta in HAND: chapter I frees the biggest body first. SEARCHED |
| 12 | 1869 / 1870 / 1890 / 1910 / 1990 | Hornet Queen, Elderscale Wurm, Apex Altisaur, Worldspine Wurm, Terastodon | USER [11], made total (§1.2) |
| 13 | 2000 | Natural Order, late (DEFAULT) | USER [12]. SEARCHED |
| 14 | 2050 | World War Hulk, late | a Craterhoof / Ghalta in HAND: frees it after every body. SEARCHED |
| 15 | 2100 | Craterhoof Behemoth | USER [14] last for maximum pump |
| 16 | 2400 | Worldly Tutor, late (DEFAULT) | USER [14']: next turn's draw / upkeep Call. SEARCHED |

### 1.1 The pieces

* **`CastOrderRank`** gives every card its DEFAULT slot (Turntimber 1200, Natural Order 2000, Worldly Tutor
  2400, Hulk by the hand). **`ArrangeCastOrder`** (run at every site that realises a cast order, both
  worlds) moves the four dual cards to the slot the **plan** gives them (`CanonicalOrderSlots`):
  * Worldly Tutor **mid** only when a Turntimber is in the plan (and the top is not already stacked), the
    Turntimber then **after** it, a Natural Order in that plan **early**. A Call of the Wild activation is
    served by the late tutor (activations trail every cast). **Vaultborn is not a consumer**: the agent-added
    clause (measured 2 better : 0 worse to remove on 08-25) is retired, so the old `MTG_STOMPY_WT_LITERAL`
    behaviour is the default and that lever is deleted, as is `MTG_STOMPY_TT_LITERAL` (the searched
    Turntimber slot subsumes it).
  * The top already **stacked this turn** (an earlier pass's tutor, its card still on top --
    `top_stacked_card_number`): Turntimber consumes it where it stands; the tutor goes late.
  * World War Hulk reads the **hand** (chapter I frees a hand card).
  This removes the documented over-trigger of the 08-21 rank ("an UNCAST hand tutor still forces NO early").
* **`CastOrderTotal`** (new `DecisionProvider` hook): the shared comparator (`CastOrderLessRanked`) breaks a
  rank tie between two different casts by a FIXED key -- name, then tutor target, own target (Natural
  Order's sacrifice), X -- never plan / hand order. Every other provider keeps plan order (default false).
* **The searched two-way slots** (`WantsCastOrderingSearch` + `CastOrderingCandidates`,
  `MTG_STOMPY_ORDER_SEARCH`): the canonical order first, then one variant per dual card present at its OTHER
  slot, deduplicated (by sequence here, by end state in `EnumeratePlans`). A set with no dual card, or whose
  variants all coincide with the canonical order, returns no candidate, so the base plan stays un-searched
  and nothing is applied twice. Search window only: the rollouts beyond the horizon and the d0 runner use
  `Solve`, which never expands orderings, so they keep the canonical order.
* **The TOP_RESOLVE opt-in** (`TopResolveOptIn`, `MTG_STOMPY_TOP_RESOLVE`): every `MTG_TOP_RESOLVE` reader
  (the rollout's arming, the executor's draw-engine / deferred classification, the d0 lethality model, the
  spell//land hold) now reads `TopResolveActive(state)` = the global lever OR the provider opt-in. The global
  default stays OFF; the other `tutor_to_top` decks (Enlightened Tutor: Anti-Lifegain, Creature Giving,
  Fluctuator; Giant Harbinger: Giants; Forerunner of the Coalition: Pirates) are byte-identical (§3).

### 1.2 Every tie-break, and why (USER: "fully order the cards")

| tie | rule | why |
|---|---|---|
| Elvish Mystic / Fyndhorn Elves / Llanowar Elves | **by name** (Mystic, Fyndhorn, Llanowar) | identical text ({G}, 1/1 Elf Druid, "{T}: Add {G}"): no order can differ, so a fixed key costs nothing and removes the hand-order dependence |
| the fatty group | **Terastodon last**, the rest **cheapest first**, a **token maker first at equal cost**: Hornet Queen (7), Elderscale Wurm (7), Apex Altisaur (9), Worldspine Wurm (11), Terastodon (8) | Terastodon's ETB destroys up to three of OUR noncreature permanents for 3/3 Elephants, TAPPED ones first (`SpellEffects.h`: Forests, then other lands, Sol Ring, other noncreatures, Call of the Wild last); cast after the rest of the group has tapped what it pays with, more of what it eats is already spent. Cheapest first: if the line cannot pay a cast after all, the dropped one is the dearest, not a cheap body behind it. Hornet Queen before Elderscale: more bodies. Verified against the card data: none of the five reads another's entry (Insects and Elephants are below power 4, so Vaultborn draws nothing off them); every card that reads them (Natural Order late, Hulk late, Craterhoof) comes after the group |
| two copies of one card with different choices (two Worldly Tutors fetching different creatures, two Natural Orders) | the tutor target by name, then the sacrificed card, then X | the fixed key; never plan order |
| any other pair | none exists | every other card in the list has its own rank |

Ghalta (1700) and Hulk early (1750) are the table's order; they never meet in the canonical order (a Ghalta in
hand makes Hulk late).

## 2. Verify first: MTG_BP_NEW_ONLY versus the reset (draft §7 note 8)

**It does NOT drop the reset's Turntimber continuation in the rollout -- it stands down entirely, and that is
the cost.** Probe `test/tools/stompy_order_ab/probe_tutor_tt.json` (T4, 8 Forests + 4 elves, hand {Worldly
Tutor, Turntimber}, Craterhoof 11 deep): won T4 at d0 and d3 with `MTG_BP_NEW_ONLY` =1 and =0 alike;
`MTG_BP_NEW_ONLY_TRACE=4` shows `KEEP spells[Turntimber Symbiosis] <new>` with `arrived=[Turntimber
Symbiosis]`. Why: the tutor-to-top arming (TurnSolver.cpp, the `deferred_top_rearm` site) binds an EMPTY
hand snapshot on purpose, so EVERY hand card reads as "arrived" and the new-only filter keeps every
continuation.

Two consequences for the breakpoint-options audit (not fixed here, per instructions):
* **Cost.** On 60 shipped games (seed 1001) the opt-in raises the continuation lists the filter sees from
  554 to 23,385 (entries 1,118 -> 73,409; drop rate 58% -> 16%), and work units **2.3x** (108,039 ->
  248,424). The ordering search itself is ~free (248,424 vs 247,016 with `MTG_STOMPY_ORDER_SEARCH=0`).
  A sound narrowing would treat only the top-of-library consumers (a Turntimber cast, a Call activation,
  an enters-draw) as new at the reset -- every other old card is a sibling base plan's line -- but the
  order-sensitive old cards (Natural Order's shuffle burying the stack, a power-4 entry under Vaultborn
  drawing it) need care. That is the audit's call.
* **A latent executor/rollout mismatch.** The executor's no-commit fallback (AIEngine `arm_acq_deferred`
  -> `pin_rdb_hand`, then `bound_hand = rdb_hand_pass`) binds the PRE-TUTOR hand, not the empty snapshot
  the rollout bound, so under NEW_ONLY a held Turntimber reads OLD there; and if the plan carries a
  `bp_choice` at that site, `EnumerateBreakpointPlans` derives a different list than the rollout indexed.
  Reached only when a depth > 0 executor has no committed line (a committed line replays its recorded
  script), so it did not show in the probe.

## 3. The measurement (PARTIAL)

Pre-flight (seed 1001, the smoke seeds): the new binary with `MTG_STOMPY_ORDER=0` reproduces the committed
smoke GT exactly (d3 4.0333/c2ed3b5261a829ef, d0 4.9000/f4267685b45f6f99), as does the base binary. Order ON:
d0 1000 games 4.9000 -> 4.8890; d3 150 games 4.0333 -> 4.0333 (play changed); ship 60 games 4.0167 ->
4.0167. Unit tests 6/6 new + the full suite pass; scenarios 160/160; the 12 StompySurprise references
replay with the same win turns (12 repaired, 0 drift).

**PARTIAL pooled A/B** (`ab_v2`, base `3e3c44a1` vs new `e1f1bba1` at defaults, seeds 10900001+gi, loss = 9):

| deck | cell | games | old | new | faster | slower | net | units |
|---|---|---|---|---|---|---|---|---|
| stompy | ship | 2,750 of 4,000 | 4.1109 | 4.1084 | 10 | 3 | **-7** | 1.906x |

The three slower games (stompy ship, gi 287 4->5, gi 1526 3->4, gi 2653 3->4) are not yet recovery-checked.
Identity: 48 games (goblins + fivecolour shipped) byte-identical; the rest not run. d3, d0, stompy2hg and the
off-switch prefix: not run.

## 4. Every slower game: the root cause is a battlefield-position tap choice (2026-10-15)

**The three partial-A/B slower games did not recover** at stage 1 (d6 b100) or stage 2 (d8 b0): identical
digests at both, the budget never binding. **Attribution** (each game under every arm of the new binary):
the ORDER alone reproduces them -- `MTG_STOMPY_TOP_RESOLVE=0`, `MTG_STOMPY_ORDER_SEARCH=0` and both off give
the same slow line; `MTG_STOMPY_ORDER=0` restores the old win turn. So the default went OFF (`0731e8a5`)
until this was fixed.

**gi1526, pinned with scenarios** (`test/tools/stompy_order_ab/scn_1526_t*.json`, `bforder*.sh`):
* From the old line's T3 board (Forest x2, Sol Ring, Fyndhorn Elves, Elvish Mystic, Priest of Titania; hand
  Elvish Archdruid + Natural Order) the kill is Archdruid, then Natural Order sacrificing the tapped Priest
  and fetching Craterhoof: 21 damage. It needs BOTH 1/1 elves untapped to attack; the lands + Sol Ring pay
  4 of the 7 and Priest's one tap (3 Elves) the rest.
* **All 24 battlefield permutations, order OFF:** the kill is found exactly when Priest is NOT on the
  battlefield after both 1-drop elves. The USER order casts the 1-mana elves first ([1] before [3]), so
  Priest always lands last and the T3 kill (and every line built on it) disappears.
* **The mechanism (`MTG_PREPAY_HOLD_TRACE=1`, new, diagnosis only):** the whole-turn prepay
  (`TurnSolver::BatchPrepayMainCasts`, the one payer both the executor and the rollout run) first tries to
  hold every untapped mana creature (infeasible: 7 needed, 4 available), then its SINGLE-ATTACKER rung --
  hold ONE body, chosen by `FindBestOwnAttacker`, which breaks the equal 1/1 tie by LOWEST battlefield index
  -- and pays with the rest. With Priest last, that rung kept one 1-drop and the joint solve tapped the
  other plus Priest; with Priest earlier, the same ladder happened to keep both.
* **gi287** is the same defect one level in: its T3 line (Elvish Archdruid x2, 6 mana: three lands +
  Priest) needs both Fyndhorn Elves to swing; the single-attacker rung paid holding ONE Fyndhorn and the
  solve tapped the other ahead of Priest whenever Priest sat later on the battlefield (T4 vs T5 by
  Priest's position alone, 3 of 3 positions checked). **gi2653** recovers with the same fix.

**The fix: `MTG_HOLD_RELEASE_YIELD`** (`2f6f2634` + `85621796`; `EngineFlags.h` shared reader, heurarm slot;
the prepay is the one payer both worlds run, so lockstep holds by construction). Under the flag the hold
ladder never reads the battlefield index:
* (a) the partial hold releases equal-power bodies BIGGEST MANA YIELD first (`PermanentManaYield`: one tap of
  Priest pays what three 1/1s would, and each costs one attacker);
* (b) the single-attacker rung holds, among the strongest attackers that are mana sources, the lowest-yield one;
* (c) the line's own sacrifice victim (Natural Order's, the sac-fodder pin) is never held -- it cannot attack
  after its own cast (CR 601.2h: the additional cost is paid while casting);
* (d) the single-attacker rung is tried AFTER the partial rungs, not before them;
* remaining ties: card name, then card number.
Rules review: a summoning-sick dork can neither {T} for mana nor attack (CR 302.6), so it was and stays
outside the hold set (`CanTapNow`); haste is read by the same predicate. Payment is the permitted greedy
area -- this only re-keys its tie-breaks and the order of its rungs.

**Result on the probes:** gi1526's T3 board finds the kill in 24/24 orders (8/24 failed before), gi287's in
3/3, and with order + fix all three shipped games replay at their OLD win turns (287 T4, 1526 T3, 2653 T3).
Unit cover: `test/unit/test_stompy_order.cpp`, both boards in several battlefield orders.

The full re-measure with the fix is §3 (v4).

## 5. Findings and questions for the USER (so far)

1. **The TOP_RESOLVE opt-in costs ~2x work** at the shipped settings (1.9x on the partial A/B, 2.3x on the
   pre-flight), because the reset's continuation is not narrowed by BP_NEW_ONLY (§2). Shipped ON per the
   approved design; the USER may prefer to wait for the audit's narrowing.
2. **The tie-breaks of §1.2** are agent choices for the USER to confirm (1-drops by name; Terastodon last,
   the other fatties cheapest first, Hornet Queen before Elderscale).
3. **Terastodon's K projection** (pre-existing, not the order): `ProjectEtbDestroyK`'s this-turn Craterhoof
   window prices the route against the pool BEFORE the destroyed permanents, while the destroy loop eats
   UNTAPPED Forests once the tapped ones run out -- so K can eat mana a later Craterhoof cast needed.
   Terastodon-last-in-the-group minimises the exposure; the projection itself is unchanged.
4. **World War Hulk early with a plan that hard-casts the dearest body:** chapter I frees that same body
   and the hard-cast finds no card (its mana is left unspent). The searched late slot covers it at d > 0;
   d0 plays the canonical (early) slot. Watch the d0 cell for it.

## 6. Defaults as shipped

In `e1f1bba1`: all three ON. If the full measurement fails the bar they go OFF (=0 restores today exactly).

## RESUME (2026-10-14 night)

* **Base commit** `3e3c44a1` (work/integ-2026-10-14 tip). **Branch** `worktree-agent-a9d0a1cbf3361f164`.
  Implementation `e1f1bba1`; this WIP commit adds the tooling + partial results + this note.
* **Binaries** (build volume, regenerable): `build/scratch/stompy-order/bin/mtg.base` (3e3c44a1),
  `bin/mtg.new` (e1f1bba1). Rebuild with `./build.sh` at those commits if the volume is gone.
* **Resume the A/B:** `bash test/tools/stompy_order_ab/run_ab.sh v3` from the worktree root (both binaries
  concurrently, cores 0-5, one pooled batch each; manifests via `gen_manifest.py`, saved copies in
  `test/tools/stompy_order_ab/results/`). Cells: shipped (value_play, d6 b20), d3 b10, d0; stompy 4,000 /
  6,000 / 20,000 games, stompy2hg half; seeds 10900001+gi (chunk: seed = 10900001+off, game_index = off);
  identity = every other suite deck x 3 cells x 24 games; the off-switch prefix (new binary,
  `MTG_STOMPY_ORDER=0`) in the new manifest. To skip the 2,750 shipped games already done, reuse
  `ab_v2/wins_*` (the analysis pairs on (deck, cell, gi)) and drop chunks c0..c2500 of `stompy.ship.*`.
* **Then:** `analyze.py <wins_base> <wins_new> --slower-out slower.txt`; recovery stage 1 / 2 via
  `gen_recovery.py slower.txt 1|2 out.json` + `runm.sh mtg.new out.json <dir>`; per-turn diffs via
  `gen_trace.py` + `runt.sh` on both binaries + `diffgames.py`.
* **Left:** the rest of the A/B, every slower game's recovery / root cause, the rankings-doc update, the
  defaults decision.
