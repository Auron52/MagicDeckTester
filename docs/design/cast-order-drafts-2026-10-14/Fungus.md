# Fungus -- cast-order draft (2026-10-14)

Deck: `decks/Fungus/Fungus.cod` (shipped 60, mono-G; 19 Forest + 3 Simic Growth Chamber). Provider: `FungusProvider`
(src/ai/DecisionProviders.h:2425). Card data read from `cards.json` for all 14 names.

## 1. Status: **D**. Generic tiers play. A USER partial order is on record but was never built.

* **D, what plays.** `FungusProvider` has no `CastOrderRank` / `CastOrderRankLatest` / `ArrangeCastOrder` override, and
  there is no `MTG_FUNGUS_*ORDER*` flag. Everything falls through to `GenericProvider::CastOrderRank`
  (DecisionProviders.cpp:3013).
* **The USER order on record, never built.** USER 2026-09-17 (`docs/design/fungus-second-main-and-devour.md` §3b):
  *"Note that, order wise, Doubling Season should be dropped before any activations and in main 1 so that Beastmaster
  counters are doubled."* §3c then sets out the sequence: land + Wild Growth, Doubling Season, Beastmaster, Sporecrown,
  remaining creatures, spore activations. §6e says it was never built: *"Unbuilt: `CastOrderRank` for
  Doubling-Season-before-everything"*. It was never measured and never switched off, so **no USER order is OFF.** This
  draft starts from that order.
* **Main phase, USER-ruled and live.** USER 2026-09-23: *"Only Mycoloth should go in the second main ... Everything else can
  be skipped."* The code is `FungusProvider::MainPhaseOverride` (DecisionProviders.cpp:34561): devour goes to Main2,
  everything else is pinned to Main1, behind `MTG_FUNGUS_M2_DEVOUR` (default ON) and `MTG_FUNGUS_M2_GATE` (default ON).

## 2. What plays today

| pos | card(s) | main | rule |
|---|---|---|---|
| land | Forest | m1 | Land drop first (no cantrip among the casts) |
| land | Simic Growth Chamber | m1 | **Deferred until after the main's casts** (`karoo_deferred`, TurnSolver.cpp:39645, `MTG_NO_KAROO_DEFER`), so its bounce returns a land that is already spent. The report's "LAND FIRST" line does not show this |
| 5 | Wild Growth | m1 | Land-Aura ramp tier (DecisionProviders.cpp:3153) |
| 10 **TIE** | Essence Warden, Thallid, Tukatongue Thallid, Utopia Mycon, Sporecrown Thallid, Thallid Shell-Dweller, Psychotrope Thallid, Sporesower Thallid | m1 | Creature tier. A tie falls back to plan order, i.e. hand order (`CastOrderLessRanked`, ManaPayment.cpp:2645) |
| 10 **TIE** | Mycoloth | m2 (m1 at d0) | Creature tier. Alone in m2 in searched play. At d0 the phase filter is off (`SearchedPlayActive`, TurnSolver.cpp:20395), so it ties with the bodies in m1 |
| 20 **TIE** | Beastmaster Ascension, Doubling Season | m1 | Other noncreature spells |
| (unranked) | spore pops, Psychotrope draw | -- | Activations, which run in the TRAILING pass after every cast of the main |
| (unranked) | Utopia Mycon sac-for-mana | -- | Runs in a PRE-pass before every cast. It is collected from the battlefield only |

## 3. Proposed order

All casts are main 1 except Mycoloth. No ranges. Every cost is `{G}`+generic. The Chamber's `{U}` pays generic only,
and all six multi-mana spells (Sporecrown, Shell-Dweller, Psychotrope, Beastmaster, Sporesower, and Mycoloth / Doubling
Season) carry generic. The only same-turn producer the order places, Wild Growth, is already at both ends of any range.

| pos | card(s) | main | why (card text / plan) |
|---|---|---|---|
| land | Forest | m1 | First, before every cast. Nothing drawn could change the land drop: Psychotrope's draw is an activation that runs after every cast. Draw-then-land is still possible through the searched deferred drop and the main-2 drop |
| land | Simic Growth Chamber | m1 | After the casts, unchanged. **Wild Growth goes ON the Chamber, after it lands, whenever the bounce would otherwise return Wild Growth's host** (USER §3c step 1: *"float the {G} off the Forest first, play the bounceland returning the now-spent Forest, then cast Wild Growth on the bounceland"*). See §4.1 |
| 1 | Wild Growth | m1 | Mana first. The host must be untapped when it pays, so it precedes every spell it funds. Season first would tap the host: T5 with 5 Forests and WG + Season casts both only in this order |
| 2 | Utopia Mycon | m1 | *"Sacrifice a Saproling: Add one mana"* (no {T}) can pay for every later spell. Its cast creates nothing the Season would double. **Inert today**: see §7. Departs from §3c's literal "Season first among casts". Q2 |
| 3 | Doubling Season | m1 | **USER 09-17**: before any token/counter event of the turn: Mycoloth's devour counters, spore pops, Tukatongue's refund |
| 4 | Beastmaster Ascension | m1 | USER §3c step 3. Counters come from DECLARED attackers, so it must be in play before combat |
| 5 | Sporecrown Thallid | m1 | USER §3c step 4. The lord's anthem only helps the combat it precedes |
| 6 | Essence Warden | m1 | *"Whenever another creature enters"*: a watcher goes before the bodies it watches. Lifegain is inert in a goldfish |
| 7 | Sporesower Thallid | m1 | 4/4 body. Upkeep trigger only. Position is inert |
| 8 **TIE** | Thallid, Tukatongue Thallid, Thallid Shell-Dweller | m1 | Order provably cannot matter. None has a cast or ETB effect, and all enter summoning-sick (the list has no haste). Their spore abilities run in the trailing pass. Tukatongue's token needs a devour, which happens in main 2 |
| 9 | Psychotrope Thallid | m1 | Its draw is an activation that runs after every cast, so the cast position is inert |
| 10 | Mycoloth | **m2** (USER) | **LAST of whatever shares its main, and after the Season.** *"Devour 2 (As this creature enters, you may sacrifice ...)"* eats only bodies already on the battlefield. The counters it enters with are doubled only if the Season is already out |

## 4. What changes and why it matters

**Bottom line.** On the shipped 60 at searched depth, the rank changes in rows 2-10 are **expected to keep every score
the same** (digests may still move). The one real line loss found is item 1, and fixing it is a host-variant tie-break,
not a rank change.

1. **Wild Growth is thrown away to the Chamber's bounce.** This was found by probe in the smoke cell itself
   (`fungus 3 1001 150 10`), seed 1001 gi0 T2, d3/b10. Board: one Forest. The played line casts Wild Growth on that
   Forest. The deferred Chamber then returns the Forest, and **Wild Growth goes to the graveyard** (`boardAfter`:
   graveyard [59]).
   * On the same seed at **d0**, the greedy line plays your §3c line and keeps Wild Growth on the Chamber.
   * The engine *has* the right variant: SAME-TURN KAROO HOST, TurnSolver.cpp:23923-23933, which casts Wild Growth after
     the Chamber lands. That variant is appended **last** to the host list.
   * The rollout trace at T2 shows the Wild Growth variants tied at win=6. Turn-to-win cannot price a lost card plus
     {G} per turn, so the tie goes to the Forest host. Probable mechanism: the tie-break.
   * WG rank 5 and the Chamber deferral are both correct. The fix is the order in which the host variants are offered
     (Q1).
2. **Doubling Season before every creature.** Today the Season (20) follows every creature (10).
   * **In searched play this changes nothing on the shipped list.** The only same-main token/counter event among the
     shipped casts is Mycoloth's devour, and Mycoloth is alone in m2. Every spore pop already trails every cast. Your
     "before any activations" therefore already holds by structure. The rank makes it hold *by rule*.
   * **It binds at d0.** There the phase filter is off, so Mycoloth (10) resolves before the Season (20) in one main 1.
     Example: devour 3 enters with 6 counters instead of 12, which means 12 instead of 24 Saprolings per upkeep under
     the Season. This needs 10 mana in one turn: 0 of 60 probed d0 games did it.
   * **It also binds on candidate B**, which uses the same provider. Undercellar Myconid's ETB Saproling and Saproling
     Burst's 7 fade counters are undoubled when they are cast before the Season.
3. **Mycoloth last.** This applies at d0 only on the shipped list, where it ties with the bodies in m1. A body cast after
   it cannot be eaten. The devour count is capped at the creatures in play at the start of the main
   (TurnSolver.cpp:24822), so being last changes WHICH bodies are eaten (a same-main Tukatongue refunds a Saproling),
   not HOW MANY.
4. **Three ties replaced by a stated order.**
   * Today: creatures 10 = hand order, Season vs Beastmaster 20 = hand order.
   * After: inert by the proofs above. The order may still move digests, because battlefield-index order breaks ties
     in trailing activations and in the canonical sac victim. It should not move outcomes.

## 5. Questions for the USER (one word settles each)

1. **Wild Growth vs the Chamber.** Should the Chamber be the FIRST-offered Wild Growth host on the deferred-Chamber
   branch, so a tie goes to your line (`order`)? Or should the search prune the "Wild Growth on the land the Chamber
   returns" pairing, which is dominated because it is the same state minus Wild Growth (`prune`)? **Default: `order`.**
2. **Utopia Mycon ahead of Doubling Season?** It is a mana source, and its cast is not an activation the Season would
   double. It is inert in the engine today. **Default: yes.**
3. **Mycoloth LAST in whatever main it shares** (after the Season and every body)? **Default: yes.**
4. **Same order for candidate B** (param-keyed; this is where Season-first actually changes games)? **Default: yes.**
5. **Keep your 09-23 main split unchanged** (Mycoloth m2, everything else m1)? **Default: yes.**

## 6. Generic-order findings

* **Token/counter doublers rank 20, after every creature.** This is wrong by principle 3 wherever a same-main cast makes
  tokens or enters with counters (ETB tokens, devour, fading, +1/+1 ETB). Candidate rule: `doubles_tokens ||
  doubles_counters` joins the before-the-creatures tiers (8/9).
* **Devour creatures tie at 10 with the bodies they eat.** They should rank after the other creatures.
* **Summoning-sick-usable sac-for-mana creatures are same-turn mana sources** (Utopia Mycon; Skirk Prospector, which
  pins {R}), in the same way rocks (tier 5) are. The rank is moot because the engine collects activations from the
  battlefield only (TurnSolver.cpp:20760).
* **The report's "LAND DROP: FIRST" line is wrong for a Karoo.** A Karoo is deferred after the casts. The fix is display
  only.
* **The enter-watcher tier (`MTG_WATCHER_ORDER`, default OFF) misses lifegain watchers** (`any_creature_enters_lifegain`).
  This is inert here; CritterLifegain carries its own order.

## 7. Implementation notes

* **New `FungusProvider::CastOrderRank`.** It is param-keyed, behind a new `MTG_FUNGUS_CAST_ORDER` / heurarm. Read the
  `EnvOn` rules in `.claude/skills/coding-conventions.md`; default ON is the USER's call. Unlisted cards return
  `GenericProvider::CastOrderRank`. The ranks:
  * 5: `is_land_aura && land_aura_extra_mana>0`, and `mana_rock` (both generic, kept)
  * 6: `sac_creature_outlet && IsSacManaOutlet` (Mycon)
  * 7: `doubles_tokens || doubles_counters`
  * 8: `quest_counter_per_attacker`
  * 9: `CardTemplate::LordEffect` with `subtypes_affected` set (Sporecrown)
  * 10: `any_creature_enters_lifegain` (Warden)
  * 11: `spore_upkeep_each_fungus` (Sporesower)
  * 12: other creatures
  * 13: `sac_outlet_draw` (Psychotrope)
  * 25: `devour > 0`, after any noncreature 20 and before the self-damage 30
  * Add `CastOrderTierName` for the report labels.
  * On candidate B: Saproling Burst (`fading_counters`) and Undercellar Myconid (`etb_self_creates_tokens`) land after
    the Season automatically.
* **The Wild Growth fix (Q1) is not a rank.** It is the order of the host variants on the deferred-Chamber branch.
  Ordering options is provider-owned (NO-GREEDY rule), so it belongs in a provider hook, not in TurnSolver.
  `PickLandAuraHosts` already stands down on that branch (`g_enum_karoo_drop`, TurnSolver.cpp:22087).
* **Engine limitations the order cannot fix:**
  * **(a) Same-main Mycon mana is inexpressible.** A Mycon cast this main cannot sac in this main, because activations
    are collected from the battlefield and §2b `MTG_SAC_OUTLET_PAY` is OFF. Rank 6 becomes real only if that changes.
  * **(b) Devour k is capped** at the creatures in play when the main starts.
  * **(c) `MTG_SAC_FODDER_SAME_LINE` (OFF) pops a Saproling in the PRE-pass**, which runs before every cast, so the
    Season-first rank cannot double it.
  * **(d) A Season found by Psychotrope's trailing-pass draw is cast in the draw's continuation.** Any spore pop that
    ran earlier in battlefield-index order was not doubled. `FungusProvider` has no `ActivationOrderRank`. Not verified
    which runs first today.
* **Measure before any adoption:** smoke + regression for `fungus` and `fungusb`. Expected for `fungus`: score-neutral
  at d3/d5, possible digest-only moves, and d0 may move. `fungusb` should move.
* **The rank sort applies to every cast set.** No shipped card is ORDER-OPAQUE (no `draw`, staging, cascade, retrace,
  impulse or solo-target params). Psychotrope carries `sac_outlet_draw`, which is not in `OrderingOpaque`. So for this
  deck the report's rank is what plays.
* **The probes are valid.** Every probe passed an absolute `--cards-json`, and all 66 games won (T4-T8), so none hit the
  "no cards loaded" pitfall.
* **Probe artifacts** (read-only) are in `build/scratch/castorder/probe_Fungus/`; run `probe_Fungus.sh <d> <b> <base> <gi..>`.
  The Wild Growth loss is in `v2_d3_b10_s1001_g0`; the d0 contrast is in `v2_d0_b0_s1001_g0`.
