# slivers_vial: cast-order draft (agent proposal, 2026-10-14; for USER review)

## 1. Status: **(D) generic tiers only**

- The provider is `VialProvider` (DecisionProviders.h:796). Its `CastOrderRank` (DecisionProviders.cpp:7679)
  adds only the Knights clauses: Worthy Knight 8, and Contender 9 or 12, behind `MTG_KNIGHTS_ORDER`
  (default ON). Those clauses key on `cast_trigger_creates_tokens` / `etb_dig_count`, which no Sliver
  carries, so every Sliver card falls through to `GenericProvider::CastOrderRank` (DecisionProviders.cpp:3013).
- No `MTG_SLIVERS_*ORDER` flag exists. The `slivers_vial` section of `cast-order-rankings.md` is only the
  generated table. **No USER order exists, so none is OFF.**
- The USER's 2026-10-09 discard review (slivers-vial-discard-policy-proposal.md, USER section) does state
  the plan the order has to serve: *"casting 2 lords on 4 (potentially replicate from hatchery)"* and
  *"Hatchery can also fill in the space ... due to replicate for both 1-drops (3 on turn 3) or lords
  (2 on turn 4)"*. When Hatchery enters the same main as the Slivers it is meant to copy, those lines
  depend on the order (section 4).

## 2. What plays today

| pos | card(s) | main (report) | rule |
|---|---|---|---|
| land | land drop | m1 | No cantrip, so it goes first at depth 0. At depth > 0 it is searched as part of the plan. |
| 10 | Hatchery, Muscle, Predatory, Sinew, Leeching, Cloudshredder, Crystalline, Hibernation, Galerider, Striking, Plated **(11-way TIE)** | m1 for the anthems and Cloudshredder, m2 for the rest | Generic creature tier. Ties fall to plan-vector (= hand) order (`CastOrderLessRanked`, ManaPayment.cpp:2645). A Vial put sits in the same tie, at its creature's rank. |
| 20 | Aether Vial, Thrumming Hivepool **(TIE)** | m1 | other noncreature |

**The main column is inert here.** The deck is single-main (`uses_second_main=no`), so
`MainPhaseFilterActive` returns false (TurnSolver.cpp:20388) and every cast is pre-combat. None of the
cards is order-opaque (no draw, stage, cascade or retrace), so the rank sort governs every cast set.

## 3. Proposed order

| pos | card(s) | main | range | why (card text, then the deck's plan) |
|---|---|---|---|---|
| land | land drop | m1 | - | The deck has no draw, so nothing wants to come before the land. |
| 1 | Hatchery Sliver (x4; copies are identical, so the tie is moot) | m1 | - | "Each Sliver spell you cast has replicate." This is an enabler, so it goes before what it enables (principle 3): every Sliver spell cast after it may be copied. Its Vial put sequences at this rank too, so a put Hatchery also comes before the casts. |
| 2 | Muscle, Predatory, Sinew **(tie: moot)** | m1 | - | +1/+1 to every Sliver, the same effect on all three. Ranked first after Hatchery so that they get the leftover replicate copies (see 4.2); each copy pumps the whole team. |
| 3 | Leeching Sliver | m1 | - | "Whenever a Sliver you control attacks, defending player loses 1 life." A copy adds one more drain per attacker, close to an anthem's worth on the attack. |
| 4 | Galerider, Striking, Plated **(tie: moot)** | m1 | - | In a goldfish all three are identical 1/1s for one pip; their grants (flying, first strike, +0/+1) are unmodelled or inert. As leftover copies they give the most bodies per mana. |
| 5 | Crystalline, Hibernation **(tie: moot)** | m1 | - | Identical vanilla 2/2s for two pips; shroud and the bounce ability are unmodelled. |
| 6 | Cloudshredder Sliver | m1 | - | "Sliver creatures you control have ... haste." Haste is a static grant, so where it sits does not change what attacks. A second copy is only a 1/1 for 2 mana, so it is last in line for copies. |
| 7 | Aether Vial | m1 | - | Enters with no counters, so it does nothing the turn it is cast. It is not a Sliver, so it does not move Hivepool's cost. Its tie with Hivepool is provably order-free (both costs are generic only). |
| 8 | Thrumming Hivepool | m1 | - | "Affinity for Slivers": it costs 1 less for each Sliver already on the battlefield, counting casts, Vial puts and replicate copies. So it goes **LAST**. Its haste and double strike are static, so the Slivers cast before it still get them. This is unchanged from today's rank 20 vs 10, and it is made explicit. |

- **Main:** m1 for everything, matching the USER's "everything main 1" for Knights. The deck gains nothing
  from post-combat information: it has no draws and no combat-made resources.
- **Ranges:** none. Every rank here is a pure creature or generic-cost reorder. The payable-order fallback
  (default ON) already handles a colour clash, and the replicate copies are line-held, so they never
  take the mana a co-planned cast needs.
- **Positions 2-6 matter for one reason only.** When Hatchery enters this main, the copy count of every
  later Sliver is decided by resolution greedy, not by the search (finding 6.2). Whichever post-Hatchery
  Sliver resolves first takes all the spare mana. If the engine fanned that count, positions 2-6 would be
  order-free and could collapse into one tie.

## 4. What changes and why it matters

1. **Hatchery first: two USER-plan lines are unreachable today, at any budget (probed).** Board: two
   attacking Galeriders, opponent at 6. The lines need **Hatchery, then Muscle + 1 replicate copy**: both
   Galeriders become 3/3, which is exact lethal.
   - **T6, six lands** (`cast_{hatchery,muscle}_first`). Hatchery listed first: lethal on T6. Muscle
     listed first: Muscle is cast with no replicate and the opponent is left on 2. The kill is not found
     at **d8 unlimited** either; that game wins on T7 (`cast_muscle_first_d8b0_mt`).
   - **T4, four lands + Aether Vial on 2: the USER's "2 lords on 4" line** (`vial_hatchery_{h,m}first`).
     Vial-put Hatchery, then cast Muscle and replicate it. Hatchery listed first: T4 lethal. Muscle listed
     first: the search puts Muscle and casts Hatchery x2, leaving the opponent on 2. At d8 unlimited it
     is still not lethal.
   - Same cards, different hand order. The executor trace confirms the mechanism (finding 6.2).
2. **Lords next: the leftover copies go to the anthem (probed, smaller stakes).** T5, five mana, Vial on 2,
   hand {Hatchery, Galerider, Muscle}, opponent at 20.
   - Muscle listed first: put Hatchery, Muscle + copy, **and** Galerider. The game ends at -5 on T6.
   - Galerider listed first: the copies would go to Galerider, so the search drops Galerider from the
     turn to keep Muscle's copy. The game ends at -2 on T6.
   - Cost: one body a turn late. The line "Muscle x2 + Galerider" cannot be expressed in that hand order.
   - Probes: `copy_{gal,mus}_first`.
3. **Hivepool stays last, and that is already right (probed).** The same-turn affinity credit
   (`SameTurnAffinityDiscountFor`, TurnSolver.cpp:6273) counts only creatures that rank before Hivepool.
   Hivepool after a cast (`hivepool_after_cast`) and after a Vial put (`hivepool_after_put`) are both T3
   lethal. The new creature ranks 8-13 stay below Hivepool's.
4. **No main-phase change.** The deck stays single-main; the report's m2 labels never act.
5. **Shared base: this order must NOT go on `VialProvider`.**
   - `KnightsProvider` and `WhiteKnightsProvider` inherit `VialProvider::CastOrderRank` directly.
     `SoldiersProvider` falls back to it whenever `MTG_SOLDIERS_ORDER` is off, and uses 100 + it for its
     unlisted cards when it is on (DecisionProviders.cpp:8991/9008).
   - A lord tier keyed on `LordEffect` would therefore reorder Knights (6 lords, under its USER-reviewed
     order), WhiteKnights (2) and Soldiers (5).
   - The Hatchery clause alone would be inert there: only Hatchery carries `grants_replicate_to_subtypes`.
   - Hence the proposed `SliversProvider` (section 7).

## 5. Questions for the USER

1. **Hatchery Sliver first, before every other Sliver cast or Vial put: approve?** Default: **yes**. It is
   dominant, because replicate is optional and its copies are line-held.
2. **After Hatchery: anthems > Leeching > 1-drops > 2/2s > Cloudshredder, as the order that decides which
   Sliver soaks up the leftover replicate copies: approve?** The alternative leaves them tied (hand order).
   Default: **approve**.
3. **Keep slivers single-main (everything main 1): approve?** Default: **yes**.
4. **Ship as a new `SliversProvider` (derived from `VialProvider`) with `MTG_SLIVERS_ORDER` default ON
   (=0 gives today's order), confirmed on the slivers suite cases: yes?** Default: **yes**.
5. **The copy count for a Sliver cast after a same-main Hatchery is never searched; resolution takes the
   greedy max. Fix it in the engine (fan the count) so that ranks 9-13 stop mattering?** Default: **yes**,
   as a separate engine task under the NO-GREEDY rule.

## 6. Generic-order findings

1. **A replicate GRANTER ties with the creatures it grants to.** `grants_replicate_to_subtypes` should rank
   8, beside the subtype-reducer tier. Same principle as the reducer: the enabler goes before the
   beneficiary. Only Hatchery carries the param, so a generic rule would be inert for every other deck.
2. **The post-Hatchery replicate count is a greedy inside the search window (engine).**
   - `CanReplicate(def, state.battlefield, ...)` (SpellEffects.h:16977) gates the count fan
     (TurnSolver.cpp:24874) on the board at enumeration time. A Sliver that is cast after a Hatchery
     entering the SAME plan is therefore left at `replicate_count = -1`.
   - Resolution then makes the line-held greedy maximum, in both worlds (AIEngine.cpp:7789,
     TurnSolver.cpp:41612).
   - Trace on `cast_hatchery_first`: `[rep-trace] path=aiengine turn=6 Muscle Sliver pinned=-1 copies=1`
     (Hatchery itself: `pinned=0`, fanned).
   - The comment at TurnSolver.cpp:24804 ("an unpinned (-1) cast now reaches the resolution loop only
     beyond the search horizon") is therefore not true for this case.
   - Side effect: the line hold covers casts only (`LineCastCostTotal`), and the activation pass runs
     after every cast. So a greedy copy can also take the {1} a planned Mutavault animation needed.
     This is unverified by probe.
3. **Phase classifier misses (inert here; they would bite if the deck ever went two-main):**
   - Hatchery is labelled m2 (vanilla template) while the anthems it would replicate are m1.
   - Leeching (`attack_trigger_life_loss`) is labelled m2, although its trigger pays off on THIS turn's
     attacks.
4. **Report labels:** the report's `tier_name` (main.cpp:7885) has no names for ranks 8, 9 or 11-13. The
   new provider needs a `CastOrderTierName`.

## 7. Implementation notes

`SliversProvider : VialProvider` would be an empty derivation plus `CastOrderRank` / `CastOrderTierName`,
gated on `EnvOn("MTG_SLIVERS_ORDER", true)` with a heurarm slot. Off falls through to
`VialProvider::CastOrderRank`.

- **Routing:** route it above `if (vial) { return g_vial; }` (DecisionProviders.cpp:18734), on a mainboard
  signature only Slivers carry (`grants_replicate_to_subtypes` or `affinity_for_subtype`).
- **Optional:** move the existing `MTG_SLIVERS_BUCKET_DISCARD` rule there too, which removes the
  shared-base hazard noted at DecisionProviders.cpp:18730.
- **Rank by params, never by names:**

| rank | param test | card |
|---|---|---|
| 8 | `grants_replicate_to_subtypes` | Hatchery |
| 9 | creature && `tmpl == LordEffect` && `power_bonus > 0` | Muscle, Predatory, Sinew |
| 10 | `attack_trigger_life_loss > 0` | Leeching |
| 11 | other creature, mv 1 (Plated: LordEffect with `power_bonus == 0`, falls here) | Galerider, Striking, Plated |
| 12 | other creature, mv 2, no `grants_haste` | Crystalline, Hibernation |
| 13 | creature with `grants_haste` | Cloudshredder |
| 20 | other noncreature (generic) | Aether Vial |
| 25 | `affinity_for_subtype` | Thrumming Hivepool |

- **Consumers compare ranks relatively,** with no absolute-number tests found. Vial puts inherit their
  creature's rank, so a put Hatchery leads automatically.
- **No engine limitation blocks this order.** Every position is unconditional, so a scalar rank expresses it.
- **Known gaps:**
  - Ranks 9-13 only mitigate finding 6.2. The real fix is a pinned count fan for Slivers whose plan holds
    an earlier-ranked Hatchery cast or put, priced into the bill as the on-board fan already is.
  - Not expressible, and not proposed: an animated Mutavault counts as a Sliver for Hivepool
    (`AnimatedAllTypes`), but animation runs in the post-cast activation pass. "Animate, then Hivepool
    for 1 less" therefore cannot happen (1 mana).
- **Measurement:** the order ages the value leaf and keep table only through play, as any order change
  does, so measure it with the sidecars in place.
- **Probes** (read-only, Release binary): `build/scratch/castorder/probe_slivers_vial/*.json` and their
  `.log.json` outputs; `run.sh` re-runs them.
