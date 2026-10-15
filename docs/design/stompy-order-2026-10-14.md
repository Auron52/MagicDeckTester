# StompySurprise: the USER-approved cast order, built and measured (2026-10-14)

**Status (2026-10-15): BUILT and MEASURED; everything DEFAULT OFF, adoption is the USER's call (§5, §6).**
The payment fix + three engine levers clear the whole bar with the order OFF; the order with them is net
<= 0 in every cell, references unchanged, but 2 of 598 slower games do not recover (§3.3, §3.4, §4f).
Self-contained. The USER's verbatim 2026-08-21 order
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
* **The TOTAL order's name key** is the canonical one (`DecisionProvider::CastOrderTotalByName` +
  `CastOrderNameTieLess`, ManaPayment.h, integration `2ac8db0b`, cherry-picked here as `12940063`; the
  first build's own `CastOrderTotal` hook is gone, play-identical -- `e6e3db0d`): the shared comparator breaks
  a rank tie between two DIFFERENT cards by name, never plan / hand order. Copies of ONE card carrying
  different choices are refined by the arrangement's `StompyFixedKeyLess`: tutor target (by name, or with
  `MTG_STOMPY_NO_TARGET_RANK` for Natural Order copies by the target's own slot -- default OFF, it moved
  nothing measured), then the sacrificed card, then X.
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

## 3. The measurement

### 3.1 v4 (2026-10-15): the order with the payment fix -- the adoption measurement

ONE pooled batch on the fix binary (`85621796`, order and fix both default OFF, every arm a per-job flag)
beside the base binary (`3e3c44a1`) in the same box-lock slot; seeds 10900001+gi, loss = 9, paired per game
against the new binary's OFF arm. **Identity:** the OFF arm equals the base binary game for game on every
suite deck x 3 cells x 150 games plus a StompySurprise prefix per cell -- **18,900 games, 0 differences**.

Arms: `fix` = `MTG_HOLD_RELEASE_YIELD=1` alone; `ofix` = the order (with its searched slots and the TOP_RESOLVE
opt-in, as built) + the fix; `onotop` = the order + the fix WITHOUT the TOP_RESOLVE opt-in.

| deck | cell | games | arm | faster | slower | net | units |
|---|---|---|---|---|---|---|---|
| stompy | shipped | 4,000 | fix | 5 | 0 | **-5** | 0.988x |
| | | | ofix | 19 | 2 | **-17** | 1.904x |
| | | | onotop | 14 | 0 | **-14** | 1.017x |
| stompy | d3 b10 | 6,000 | fix | 11 | 0 | **-11** | 0.998x |
| | | | ofix | 53 | 8 | **-45** | 1.219x |
| | | | onotop | 42 | 0 | **-42** | 1.008x |
| stompy | d0 | 20,000 | fix | 309 | 2 | **-309** | - |
| | | | ofix | 1,043 | 304 | **-774** | - |
| | | | onotop | 917 | 289 | **-658** | - |
| stompy2hg | shipped | 2,000 | fix | 6 | 0 | **-6** | 0.993x |
| | | | ofix | 32 | 6 | **-26** | 1.564x |
| | | | onotop | 19 | 2 | **-17** | 1.009x |
| stompy2hg | d3 b10 | 3,000 | fix | 12 | 0 | **-12** | 0.996x |
| | | | ofix | 53 | 8 | **-45** | 1.206x |
| | | | onotop | 39 | 4 | **-35** | 1.012x |
| stompy2hg | d0 | 10,000 | fix | 134 | 3 | **-134** | - |
| | | | ofix | 496 | 188 | **-330** | - |
| | | | onotop | 456 | 158 | **-319** | - |

**What TOP_RESOLVE buys** (ofix vs onotop): shipped -17 vs -14 (StompySurprise) and -26 vs -17 (2HG); d3 -45
vs -42 and -45 vs -35; d0 -774 vs -658 and -330 vs -319 -- for **1.90x / 1.56x the work units at the shipped
settings** (1.22x / 1.21x at d3), the BP_NEW_ONLY interaction of §2.

**The fix on every other suite deck** (150 games per cell, fix vs off): byte-identical everywhere except
SelesnyaLifegain (shipped 1 faster, d3 1 faster, d0 1 SLOWER -- gi36 6->7), FiveColour (d0 1 faster, two
shipped and two d3 games digest-only) and Bruna (one digest-only game per cell). Those three are re-measured
at a larger sample in v5 (§3.2), with every slower game's recovery.

### 3.2 v5 (2026-10-15): every v4 slower game's recovery, and the fix's movers at a larger sample

ONE pooled batch on the fix binary (2,026 jobs, 19,650 games; finished before the 9p mount loss). Stage 1 =
`--depth max(shipped, old win turn) --budget-ms 100`, stage 2 = `--depth 8 --budget-ms 0`, each under its
arm's flags (`recov_v5.py`).

| arm | slower games (v4) | stage 1 | stage 2 | NO |
|---|---|---|---|---|
| fix | 6 (stompy d0 2, stompy2hg d0 3, selesnya d0 1) | 6 | - | 0 |
| ofix | 516 | 512 | 0 | **4**: stompy2hg ship gi160, ship gi797, d3 gi797, d3 gi2290 (each 4 -> 5, s1 = s2 = 5) |
| onotop | 453 | 448 | 0 | **5**: the same four + stompy2hg d3 gi2791 |

Every 20-life StompySurprise slower game recovers under both order arms. The four 2HG ones are §4b-§4d.

**The payment fix's movers** (fix vs off, paired; v4's games 0..149 pooled with v5's 150..; `analyze_v5.py`):

| deck | cell | games | faster | slower | net | units |
|---|---|---|---|---|---|---|
| SelesnyaLifegain | shipped | 750 | 3 | 0 | **-3** | 0.996x |
| | d3 | 1,150 | 5 | 1 | **-4** | 0.996x |
| | d0 | 3,150 | 10 | 1 | **-9** | - |
| FiveColour | shipped | 450 | 0 | 0 | 0 | 0.998x |
| | d3 | 650 | 0 | 0 | 0 | 0.999x |
| | d0 | 2,150 | 5 | 1 | **-4** | - |
| Bruna | shipped / d3 / d0 | 300 / 450 / 1,150 | 0 | 0 | 0 | 1.000x |

The three slower games -- selesnya d3 gi964 6->7, selesnya d0 gi36 6->7, fivecolour d0 gi1040 6->7 -- all
recover at stage 1 (single-game repros). **The payment fix alone meets the bar on every deck it moves.**

### 3.3 v6 (2026-10-15): the candidate lever set

ONE pooled batch (`run_v6.sh`, `gen_v6.py`, `analyze_v6.py`; 2,616 jobs, 644,400 games, 15:22-16:02 UTC,
20/20 workers busy; binary = `cfd18d67`'s src): StompySurprise at twice the v4 sizes under `off`, `ofix`,
`onotop`, `ofixall` (= ofix + `MTG_SAC_VICTIM_SAME_NAME` + `MTG_EXEC_PUT_DEFERRED` + `MTG_HOLD_PRODUCER_FIRST`,
the candidate), `onotopall`, `ofixsvnpd` (without producer-first) and `fixall` (the order OFF, the payment fix
+ the three levers); every other suite deck at 150 games per cell under `fixall`. Paired per game vs `off`,
loss = 9. Full table: `test/tools/stompy_order_ab/results/analysis_v6.txt`.

**Identity:** `off`, `ofix`, `onotop` equal v4's same arms game for game on every StompySurprise cell (v4's
games: 4,000 / 6,000 / 20,000 and 2HG half -- 0 differences in 45,000 x 3 arm-games).

| deck | cell | games | `ofixall` vs off (faster / slower / net / units) | `onotopall` vs off | `fixall` vs off | `ofixall` vs `ofix` |
|---|---|---|---|---|---|---|
| stompy | shipped | 8,000 | 58 / 5 / **-53** / 1.910x | 44 / 0 / -44 / 1.015x | 14 / 0 / -14 / 0.989x | 3 / 0 / -3 |
| stompy | d3 b10 | 12,000 | 107 / 13 / **-94** / 1.210x | 89 / 2 / -87 / 1.006x | 33 / 1 / -32 / 0.996x | 13 / 1 / -12 |
| stompy | d0 | 40,000 | 2,767 / 300 / **-2,537** | 2,545 / 271 / -2,333 | 1,327 / 24 / -1,306 | 1,013 / 63 / -950 |
| stompy2hg | shipped | 4,000 | 81 / 7 / **-74** / 1.549x | 64 / 1 / -63 / 0.990x | 31 / 0 / -31 / 0.974x | 20 / 0 / -20 |
| stompy2hg | d3 b10 | 6,000 | 133 / 8 / **-126** / 1.202x | 112 / 3 / -110 / 1.000x | 52 / 0 / -53 / 0.989x | 38 / 0 / -38 |
| stompy2hg | d0 | 20,000 | 1,375 / 265 / **-1,177** | 1,281 / 219 / -1,129 | 637 / 9 / -634 | 503 / 9 / -501 |

* **Every cell net <= 0 for every arm**, and the three engine levers improve on the order + payment fix in
  every cell (`ofixall` vs `ofix`). `MTG_HOLD_PRODUCER_FIRST` carries almost all of that (`ofixall` vs
  `ofixsvnpd`: -3 / -12 / -945 / -17 / -35 / -504); the other two are rare-board fixes.
* **What TOP_RESOLVE buys now** (`ofixall` vs `onotopall`): shipped -9 (14 / 5) at 1.88x units and 2HG -11
  (19 / 8) at 1.56x; d3 -7 / -16 at 1.20x; d0 -204 / -48.
* **The other suite decks under `fixall`** (vs v4's off, 150 games per cell): byte-identical outcomes
  everywhere except SelesnyaLifegain (shipped -2, d3 -2, d0 1 faster / 1 slower -- the known gi36 -- and
  -1 / -1 / -1 against the payment fix alone) and FiveColour d0 (-1); digest-only moves on Bruna and
  FiveColour; KittyEquipment v2 / v3 change work units only (0.998x-1.005x), not play.

### 3.4 v6 recovery and the reference bench (one lock slot, 17:03-17:06 UTC)

**Recovery** (`gen_rec_v6.py` / `recov_v6.py`; `results/recovery_v6.txt`), stage 1 then stage 2:

| arm | slower games | stage 1 | stage 2 | NO |
|---|---|---|---|---|
| `ofixall` (the candidate) | 598 | 596 | 0 | **2**: stompy d3 gi8441 (4 -> 5), stompy2hg d3 gi4296 (5 -> 6); s1 = s2 = slower (§4f) |
| `fixall` (order OFF) | 35 (stompy 25, stompy2hg 9, selesnya 1) | 35 | 0 | 0 |
| `onotopall` | 496 | 492 | 0 | 4: the same two + stompy2hg gi2791 shipped and d3 (§4e) |

**`fixall` -- the payment fix and the three engine levers with the order OFF -- clears the whole bar**: every
deck and cell net <= 0 and every slower game recovers at stage 1. The candidate with the order has two
non-recoverers, both one root cause (§4f).

**The reference bench** (`scripts/ref_bench.py`, every reference deck, 580 hand-played games, the shipped play
policy, forced hands; `run_refs.sh` -> `results/refs_v6_cmp.txt`): A = defaults, B = `MTG_STOMPY_ORDER=1
MTG_HOLD_RELEASE_YIELD=1 MTG_SAC_VICTIM_SAME_NAME=1 MTG_EXEC_PUT_DEFERRED=1 MTG_HOLD_PRODUCER_FIRST=1`.
**No win turn moved on any of the 580** (net 0; the four pre-existing shortfalls -- bruna, dragonstorm,
fungus candidate b, melira pod -- are unchanged). The levers did reach the engine: 8 games' traces differ
(7 of the 11 archived-list StompySurprise references, 1 Anti-Lifegain), same win turns
(`results/refs_v6_tracediff.txt`).

### 3.0 The first A/B, without the fix (2026-10-14, superseded)

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

### 4b. 2HG gi160: a stale Natural Order victim pin falls back by battlefield position

* The proposed fix was "order Natural Order copies by TARGET rank, not target name" (`MTG_STOMPY_NO_TARGET_RANK`,
  built, default OFF). **It is not the cause and recovers none of the four**: the plan's two Natural Orders
  carry no pinned target there (each resolves the provider's choice), so the name key never ordered them.
* **The cause** (scenarios `scn_2hg160_t3.json`, `_t4.json`, `_t4b.json`): the T4 kill is Natural Order ->
  Ghalta (its ETB puts the drawn Craterhoof from hand) and Natural Order -> Craterhoof, paying with both
  Priests (tapped while three Elves live) and the lands, Fyndhorn Elves held to attack: 31 against 30.
  TurnSolver emits a Natural Order's sacrifice victim **once per distinct NAME**, the pre-plan board's
  most-expendable copy pinned by card number -- so BOTH Natural Orders pin the SAME Priest. The second pin
  is stale when it is paid, and `PerformSacrificeCreatureCost` fell back to the most-expendable creature by
  power, then **battlefield position**. With Fyndhorn entered between the Priests (the USER order casts the
  1-drop first) it ate the held Fyndhorn. The same T4 board in the two layouts: T4 vs T5 **on either arm,
  order on or off** -- the order only exposes it.
* **Fix: `MTG_SAC_VICTIM_SAME_NAME`** (`EngineFlags.h`; `ResolveSacVictimId`, SpellEffects.h): a stale pin
  means what the axis means -- another copy of that name (tapped first, then lowest card number); the
  payment's sac-victim pin reads the same id, in both worlds (one helper). Rules: any green creature is a
  legal victim (CR 601.2h pays the additional cost while casting); this only names which. Only
  StompySurprise plays Natural Order. **gi160 recovers at stage 1.**

### 4c. 2HG gi797 (shipped and d3): the executor replayed a deferred breakpoint inline

* T4 line Elvish Mystic, Vaultborn Tyrant, Natural Order (sacrificing Vaultborn, whose token copy and the
  fetched Craterhoof each draw) + the continuation [Fyndhorn, Fyndhorn, Craterhoof]. The order's searched
  candidate (`Plan::searched_order`) took the executor's searched-order loop, where `put_in_hand_armed()`
  (MTG_BP_PUT_IN_HAND, site 10: "did the hand gain a card?") is an INLINE breakpoint: the committed segment
  replayed right after Vaultborn's draw, before Natural Order -- one Fyndhorn castable, the other Fyndhorn
  and the Craterhoof not yet drawn, and the end-of-main catch-all then saw the segment as replayed.
* **The rollout arms site 10 as a DEFERRED re-solve** (end of `apply_one`: `deferred_cantrip_resolve`, cast
  the rest of the plan, then run the continuation), and the executor's clean loop matches it -- which is why
  the order-off arm (clean route) won T4. The searched and opaque loops did not.
* **Fix: `MTG_EXEC_PUT_DEFERRED`** (executor only; the rollout already defers): those two loops treat a
  site-10 cast like `acq_deferred` -- a committed segment replays at the catch-all, the no-commit fallback
  re-solves once after the main casts. Searched decisions only. **gi797 recovers at stage 1 in both cells.**

### 4d. 2HG gi2290 (d3): the prepay held the power-2 Archdruid on every rung

* T4 kill: Natural Order x2 -> Craterhoof x2, sacrificing the 1-drops Llanowar and Mystic; 8 mana EXACTLY,
  with Elvish Archdruid tapped while four Elves live and Fyndhorn held to attack (32 against 29). Same board
  in two layouts (`scn_2hg2290_t4.json` / `_t4b.json`, Archdruid before / after Fyndhorn): T4 vs T5 on either
  arm.
* `MTG_PREPAY_HOLD_TRACE`: every rung of the whole-turn prepay held Archdruid -- (a) orders only EQUAL-power
  bodies by yield, so a power-2 body outlives every 1/1 -- and (c) excluded only the FIRST Natural Order's
  victim, so the prepay declined and the sequential payer's battlefield order decided which body paid.
* **Fix: `MTG_HOLD_PRODUCER_FIRST`** (under `MTG_HOLD_RELEASE_YIELD`): (e) the ladder releases the biggest
  producer first (yield, then power, then name / number) -- under a team pump every body is one attacker, and
  one Archdruid tap pays what four 1/1s would; (f) no body any of the turn's casts sacrifices is held (every
  Natural Order victim; a repeated pin takes another copy of its name). **gi2290 recovers at stage 1.**
  Payment is the permitted greedy area; unit cover in `test_stompy_order.cpp`.

### 4e. 2HG gi2791 (d3): recovers with TOP_RESOLVE, not without

Slower only in the `onotop` arm. The faster line casts Vaultborn, then Worldly Tutor (its card on top), then
Natural Order, whose entries make Vaultborn draw the tutored card the same turn. With the TOP_RESOLVE opt-in
(the shipped design) it recovers at stage 1 (`ofixall`); without it, it stays at 5 at both stages. It is a
statement about the TOP_RESOLVE question (§5.1), not a defect of the order.

**All together** (`ofixall`: the order + `MTG_HOLD_RELEASE_YIELD` + `MTG_SAC_VICTIM_SAME_NAME` +
`MTG_EXEC_PUT_DEFERRED` + `MTG_HOLD_PRODUCER_FIRST`): the four 2HG non-recoverers AND the ten 20-life
shipped / d3 slower games of v4 recover at stage 1 (`rec_ntr/r_all`, single-game repros).

### 4f. v6's two non-recoverers (stompy d3 gi8441, stompy2hg d3 gi4296): floating colour spent on a generic pip

NOT BUILT -- root-caused, amendment proposed. Both are the same payer weakness, which the USER order exposes
because it casts the 1-drop Elves before the Archdruids.
* gi8441 T3 (`scn_8441_t3.json`): Forest, Llanowar Elves, Sol Ring, Priest of Titania; the base line casts
  Elvish Archdruid x2 + Llanowar (7 mana: 5 green pips, 2 generic -- exactly the green the Forest, Llanowar and a
  3-Elf Priest make, with Sol Ring's {C}{C} for the generics). Under the order it realises Llanowar, Archdruid,
  Archdruid: Priest's tap (now 3 Elves) floats {G}{G}{G}, the first Archdruid pays its {1} out of that FLOATING
  GREEN instead of Sol Ring, and the second Archdruid finds Sol Ring's {C}{C} plus one green: unpayable
  (`[uo-cast] ... paid=0 pool=3`). The rollout realises the same (lockstep), so the search scores the line as
  a T5 -- at every depth and budget (s1 = s2 = 5). The base order casts the Archdruids first, while Sol Ring
  is still the only generic source in reach.
* gi4296 (2HG): the base line's T3 board (`scn_2hg4296_t3.json`: Forest, Elvish Mystic, Sol Ring, Priest;
  Archdruid x2 + the drawn Mystic) fails the same way under the order -- Mystic, Archdruid paid, the second
  Archdruid `paid=0 pool=3` with Sol Ring untapped -- so from T1 the search no longer sees that line and
  plays Sol Ring first instead (T6 vs T5).
* The whole-turn prepay cannot save either: it taps every source BEFORE the 1-drop resolves, so it prices
  Priest at one Elf fewer and declines the turn.
* **Proposed amendment (payment, the permitted greedy area; one payer, so lockstep by construction):** pay a
  generic pip from colourless before floating coloured mana the rest of the plan still needs -- the real
  payer's twin of the affordability walk's `PayFromPoolLineAware` (ManaPayment.cpp), so a scaler's burst
  keeps its green for the plan's later pips and Sol Ring pays the {1}. Behind its own lever, then a held-out
  A/B on every deck that taps a rock beside a scaled dork; not built in this session.

## 5. Findings and questions for the USER

1. **TOP_RESOLVE costs ~1.9x work units at the shipped settings** for a small gain: with everything else on
   (`ofixall` vs `onotopall`, §3.3) shipped -9 at 1.88x (StompySurprise) and -11 at 1.56x (2HG), d3 -7 / -16
   at 1.20x, d0 -204 / -48; it also carries 2HG gi2791 (§4e). Shipped ON inside the order per the approved
   design; the USER's call whether that price is right, or to wait for the breakpoint audit's narrowing of
   the reset's continuation (§2).
2. **The tie-breaks of §1.2** are agent choices for the USER to confirm: the 1-drops by name; Terastodon
   last, the other fatties cheapest first, Hornet Queen before Elderscale; copies of one card by target name.
3. **Natural Order copies by target rank** (`MTG_STOMPY_NO_TARGET_RANK`): built as asked, default OFF. It is
   not what lost gi160 and moved none of the measured games; the rule (Ghalta 1700 before Craterhoof 2100,
   the USER's own order applied to what Natural Order puts) is the USER's to adopt or drop.
4. **The payment fix + three engine levers, order OFF (`fixall`), clear the whole bar** (§3.3 / §3.4: every
   deck and cell net <= 0, 35 / 35 slower games recover at stage 1, references unchanged). Recommendation:
   adopt `MTG_HOLD_RELEASE_YIELD`, `MTG_SAC_VICTIM_SAME_NAME`, `MTG_EXEC_PUT_DEFERRED`, `MTG_HOLD_PRODUCER_FIRST`
   default ON. Each is a battlefield-position or executor / rollout lockstep defect (§4, §4b-§4d).
5. **The order itself** (`ofixall`): every cell net <= 0 (-53 / -94 / -2,537; 2HG -74 / -126 / -1,177), 596 of
   598 slower games recover, the references are unchanged -- but **two games do not recover** (§4f), both the
   floating-green-on-a-generic payer weakness the order exposes. Recommendation: build §4f's amendment and
   re-measure before turning the order ON; or the USER may accept the two as they stand.
6. **Terastodon's K projection** (pre-existing, not the order): `ProjectEtbDestroyK`'s this-turn Craterhoof
   window prices the route against the pool BEFORE the destroyed permanents, while the destroy loop eats
   UNTAPPED Forests once the tapped ones run out -- so K can eat mana a later Craterhoof cast needed.
   Terastodon-last-in-the-group minimises the exposure; the projection itself is unchanged.
7. **World War Hulk early with a plan that hard-casts the dearest body:** chapter I frees that same body
   and the hard-cast finds no card (its mana is left unspent). The searched late slot covers it at d > 0;
   d0 plays the canonical (early) slot.

## 6. Defaults as shipped

All DEFAULT OFF (`=1` enables, per job via heurarm): `MTG_STOMPY_ORDER` (since `0731e8a5`; its
`MTG_STOMPY_ORDER_SEARCH` and `MTG_STOMPY_TOP_RESOLVE` stay ON inside it), `MTG_HOLD_RELEASE_YIELD`,
`MTG_SAC_VICTIM_SAME_NAME`, `MTG_EXEC_PUT_DEFERRED`, `MTG_HOLD_PRODUCER_FIRST`, `MTG_STOMPY_NO_TARGET_RANK`.
Off, every one is byte-identical to the tree before it (v6 identity: 0 differences). Adopting any of them is
the USER's decision (§5.4, §5.5).

## RESUME (2026-10-15, 17:20 UTC)

* **Branch** `worktree-agent-a9d0a1cbf3361f164` (local only, nothing pushed). Code: `d95813f6` (victim +
  target-rank levers), `cfd18d67` (put-deferred + producer-first), `093daf8c` (unit case, reference tools).
  Unit: `test_stompy_order.cpp` 11 / 11, full suite 1061 / 1061.
* **Binaries** (build volume): `build/scratch/stompy-order/bin/mtg.final` = `mtg.pf` = `mtg.svn` (re-pointed
  before v6 started) = `cfd18d67`'s src; `mtg.svnonly` = `d95813f6`.
* **Done:** v5 (recovery + the payment fix's movers), v6 (the candidate set, 644,400 games), v6 recovery, the
  reference bench. Results under `test/tools/stompy_order_ab/results/`.
* **Left:** §4f's amendment (generic pips paid from colourless before floating colour the plan still needs)
  behind its own lever, its two repros (`scn_8441_t3.json`, `scn_2hg4296_t3.json`), then one pooled A/B of
  the order with it; the USER's decisions in §5.
