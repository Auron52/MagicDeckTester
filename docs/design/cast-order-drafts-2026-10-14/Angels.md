# Angels -- cast-order draft (2026-10-14)

## 1. Status: (D) generic tiers only

* `AngelsProvider` (src/ai/DecisionProviders.h:530-550) overrides only the cleanup-discard hooks. It has
  no `CastOrderRank`, `CastOrderRankLatest`, `MainPhaseOverride` or `ArrangeCastOrder`, and no
  `MTG_ANGELS_*ORDER` flag exists in src/. Cast order is `GenericProvider::CastOrderRank`
  (DecisionProviders.cpp:3013).
* No USER cast-order ruling exists for this deck. analysis-Angels.md, angels-new-cards-screen.md and
  angels-discard-policy-proposal.md say nothing about order, and cast-order-rankings.md has no Angels
  section. The closest USER input is the settled discard taxonomy (2026-10-09): MANA / ENABLERS
  ("Bishop of Wings on its own") / ANGELS, with Righteous Valkyrie counted as a payoff.
* Precedent in the sibling deck: `CritterLifegainProvider::CastOrderRank` already ranks
  `own_creature_enters_lifegain` watchers at 8, ahead of the creature tier
  (`MTG_CRITTER_WATCHER_ORDER`, default ON).

## 2. What plays today

| pos | card(s) | main (report) | rule |
|---|---|---|---|
| land | the land drop | m1 | first (no draws in the deck) |
| 5 | Sol Ring | m1 | generic mana rock |
| 10 **TIE** | Lightstall Inquisitor, Bishop of Wings, Giada, Youthful Valkyrie, Lyra Archangel of Dawn, Resplendent Angel, Righteous Valkyrie, Archangel of Thune, Lyra Dawnbringer | m1 (Lightstall, Giada: m2) | generic "creature". `CastCheapestFirstWithinTier` only separates mana accelerants, so this 9-way tie falls back to PLAN ORDER, which is HAND order. Confirmed: swapping the hand order swaps the cast order in every probe below. |
| 20 **TIE** | Swords to Plowshares, Lightning Greaves, Unexpectedly Absent, Serra the Benevolent | m1 | generic "other noncreature" |

The m2 labels do nothing. `uses_second_main=no`, and `MainPhaseFilterActive` (TurnSolver.cpp:20388)
returns early in that case, so every cast is made in main 1.

## 3. Proposed order

Seen from the order, the deck is a chain. CONVERTERS turn an Angel entering into life
(Bishop 4, Righteous Valkyrie = the entrant's live toughness, Seraph Sanctuary 1). ENGINES turn each
life-gain event into counters (Thune: every creature; Lyra AoD: every Angel). Each Angel-enter
WATCHER sees only Angels that enter after it. So the order runs mana, then converters, then engines,
then the other watchers, then plain bodies, then Serra.

**Land drop: FIRST, before every cast.** Seraph Sanctuary must be in play before the Angels it
watches. Land-first is never worse: if an engine is cast this turn, a Sanctuary played after it gets
one wave from its own ETB gain, and a Sanctuary played before it gets the same wave from the engine's
entry, plus one more for every later Angel. There are no draws, so nothing wants to precede it.

| pos | card(s) | main | range | why (card text / plan) |
|---|---|---|---|---|
| 1 | Sol Ring | m1 | - | `{T}: Add {C}{C}`. Pays for the rest of the line (it can pay Greaves). |
| 2 | Lightning Greaves | m1 | - | Equip is a separate action that runs after the casts, so casting Greaves early costs no haste. It must be on the battlefield before any Angel that a same-turn Giada pays for. The engine fires the haste unlock the moment Greaves and Giada are both out (see §4-d). |
| 3 | Bishop of Wings | m1 | - | "Whenever an Angel you control enters, you gain 4." Every Angel after it is a 4-life event. It is a Human Cleric, so going first costs nothing: no Angel watcher would have seen it anyway. It pays `{W}{W}` from real white before any Angel can use Giada's Angel-only mana. Bishop vs Righteous is a tie on life (Bishop sees Righteous for 4, or Righteous sees Bishop's 4 toughness). |
| 4 | Righteous Valkyrie | m1 | - | "Whenever another Angel or Cleric enters, gain life equal to its toughness", reading LIVE toughness. Every later entrant is an event, and reaching +7 life earlier turns the anthem on earlier. |
| 5 | Archangel of Thune | m1 | - | "Whenever you gain life, +1/+1 counter on each creature." After the converters, so its own entry is a counter wave. Before every other Angel, so each later entry's gains are waves. Ahead of Lyra AoD because its waves also hit Bishop. |
| 6 | Lyra, Archangel of Dawn | m1 | - | Same engine, Angels only. *Conditional (§7): if no converter is on the board or in this cast set, positions 5-6 drop behind Giada and Youthful. With no life gained this turn the engines are inert, and Giada or Youthful first is worth 1+ counter.* |
| 7 | Giada, Font of Hope | m1 | 7 -> just after Greaves (2) | "Each other Angel enters with an additional +1/+1 counter for each Angel you already control." Every later Angel gets N counters. The range moves her to right after Greaves only when the line needs her Greaves-unlocked `{W}` to pay for an Angel ranked ahead of her (§4-d). |
| 8 | Youthful Valkyrie | m1 | - | "Whenever another Angel enters, +1/+1 counter on this." Before the remaining Angels. After Giada: it enters with N counters, at least the 1 it would get from seeing her. |
| 9 | Lyra Dawnbringer | m1 | - | "Other Angels get +1/+1": each later entrant's toughness is +1, so Righteous Valkyrie gains 1 more. After the watchers, which see her 5/5 entering. |
| 10 TIE | Resplendent Angel, Lightstall Inquisitor; also a duplicate legend whose name is already on our battlefield | m1 | - | Plain bodies. Every watcher above sees them. The tie is provably harmless: neither watches anything, the toughness sum and Giada's counter sum are the same in either order, and with no blockers the attack totals are identical. A redundant Giada or Lyra AoD is cast only for its entry triggers plus Bishop's Spirit when it dies to the legend rule, so it goes last among the creatures. Resplendent's end-step trigger reads the turn's total, so it does not care about order. |
| 11 | Serra the Benevolent | m1 | - | Her +2 pumps only the fliers present when it resolves (CR 611.2c), and her -3 makes a 4/4 Angel that wants every watcher and engine already in play. Both say "after every creature". |
| 12 TIE | Swords to Plowshares, Unexpectedly Absent | m1 | - | Never cast autonomously (0 casts in 9,000 games). Last, because a self-target Swords (human play) gains life equal to power, an event the engines should already be out for. Tie: neither interacts with the other. |

## 4. What changes and why it matters

Every case below is a single-game probe on the shipped binary
(`build/scratch/castorder/probe_Angels/`, `*.scenario.json` plus `logs/`, d5 b150).

Probe validity: every scenario passes `cards_json` as an absolute path, and the logs show real casts
and real wins, so the cwd pitfall did not apply. The report re-run from the repo root is
byte-identical. No card in the list is order-opaque (none has a draw / staging / cascade / retrace /
impulse / solo-target param), and the executor trace reads `route=clean`, so the rank sort is what
plays.

a. **Converter before the Angel it watches.** T3, 2 Plains plus a land drop. Hand: Lightstall,
   Bishop.
   * Today, with Lightstall first in hand: Lightstall, then Bishop. Life 20.
   * Bishop first: life 24. Today's order loses a 4-life event, nearly all of Resplendent's 5 and
     4/7 of the anthem.
   * Same pattern for Righteous + Lightstall on T4: life 20 vs 21.
b. **Converter before the engine.** T7, hand: Thune, Bishop.
   * Today, with Thune first in hand: Thune, then Bishop. Life 20, **no counters**.
   * Bishop first: life 24, +1/+1 on Thune and Bishop.
c. **Engine and Giada before the bodies.** Bishop in play, T4, hand: Lyra AoD, Lightstall.
   * Lightstall first: 2 counters (Lyra +1, Lightstall +1). Opponent at 3 after T6.
   * Lyra first: 3 counters (Lyra +2). Opponent at 1 after T6.
   * T3, Giada + Lightstall: Lightstall gets +0 when cast first, +1 when cast after Giada.
d. **Greaves at 20 makes an enumerated line unpayable.** T4, 4 Plains. Hand: Giada, Greaves,
   Lightstall.
   * `validate_line` accepts `Giada; Greaves; equip; Lightstall`, so the enumerator offers it
     (`HasteUnlockedManaOf`).
   * The executor sorts Lightstall and Giada (10) ahead of Greaves (20), runs out of mana, and
     **Greaves stays in hand** in both hand orders (probes H1/H2).
   * Greaves first: Greaves, then Giada, then the unlock fires, then Giada's `{W}` pays Lightstall.
   * The payable-order fallback cannot rescue it, because `CreditCastOutput` does not credit a hasted
     creature's mana.
e. **(Main column) a single main makes vigilant Giada's "attack, then pay" unfindable at any
   budget.** T3: Giada in play, 2 Plains plus a land drop. Hand: Righteous, Lightstall.
   * Main 1 only: both cast before combat, Giada taps and cannot attack. Opponent at 2 after T5,
     **no T5 win**.
   * With `MTG_FORCE_USES_M2=1`: Righteous in m1, Giada attacks (vigilance), Lightstall in m2 off
     Giada's mana. **Wins T5.**
   * The card note's "vigilance lets her attack and still tap for Angel mana the same turn" is not
     true of the shipped engine.

## 5. Questions for the USER

1. **Adopt the order** (land, Sol Ring, Greaves, Bishop, Righteous, Thune, Lyra AoD, Giada, Youthful,
   Dawnbringer, bodies, Serra, removal)? *Default: yes.*
2. **Righteous Valkyrie before Giada?** Righteous first means Giada's entry is a life event (+2 or
   more, and a counter wave from any engine in play). Giada first means Righteous enters with N
   counters. With an engine in play Righteous first is strictly better; without one it is 2 life
   against N counters. *Default: Righteous first.*
3. **Open a second main for Angels** so vigilant Giada can attack and then pay for an Angel after
   combat (§4-e)? It means a whitelist clause in `DeckUsesSecondMain` behind a heurarm lever, and a
   held-out A/B on the adoption bar, because a searched m2 costs CPU. *Default: build the lever and
   measure it; ship only on your call.*
4. **Lightning Greaves before every creature?** Haste is unaffected because the equip runs after the
   casts. *Default: yes.*
5. **Engines behind Giada and Youthful when no life source** (Bishop, Righteous, Seraph Sanctuary) is
   on the board or in the turn's casts? *Default: yes.*

## 6. Generic-order findings

* **The creature tier is one bucket with a hand-order tiebreak.** No ETB watcher precedes the
  creatures it watches. `MTG_WATCHER_ORDER` (tier 9, default OFF) covers only
  `dragon_ping_on_enter` / `haste_on_flying_enter`; CritterLifegain fixed it per deck. A generic
  watcher tier would cover this deck and Critter / Selesnya. It is principle 3 applied to enter
  triggers:
  * `own_creature_enters_lifegain`, `own_creature_enters_lifegain_toughness`,
    `own_creature_enters_self_counters`, `other_subtype_enters_counters_subtype`;
  * `lifegain_each_own_creature_counters` placed after the converters and before the remaining
    creatures.
* **A haste Equipment at 20 strands the hasted-dork mana unlock** that the enumerator credits (§4-d).
  Ranking `is_equipment && equip_grants_haste` before creatures is free, since the equip is a
  trailing action. The other route is to teach `CreditCastOutput` about `HasteUnlockedManaOf`, so the
  payable-order fallback can rescue it for any deck.
* **`DeckUsesSecondMain` has no vigilant-mana-creature clause.** A vigilant dork's "attack, then tap"
  is unreachable in every single-main deck (§4-e).

## 7. Implementation notes

* **Param keys** (house style, no names), in `AngelsProvider::CastOrderRank`, falling back to Generic.
  Suggested flag: `MTG_ANGELS_CAST_ORDER`, OFF until you approve, then A/B'd.

  | rank | card(s) | key |
  |---|---|---|
  | 5 | Sol Ring | generic `mana_rock` |
  | 6 | Lightning Greaves | `is_equipment && equip_grants_haste` |
  | 7 | Bishop of Wings | creature with `own_creature_enters_lifegain > 0` (Seraph Sanctuary shares the param but is a land, so never a cast) |
  | 8 | Righteous Valkyrie | `own_creature_enters_lifegain_toughness` |
  | 9 | Archangel of Thune | `lifegain_each_own_creature_counters > 0 && lifegain_counters_subtypes.empty()` |
  | 10 | Lyra, Archangel of Dawn | same, with the subtype list non-empty |
  | 11 | Giada | `!other_subtype_enters_counters_subtype.empty()` |
  | 12 | Youthful Valkyrie | `own_creature_enters_self_counters > 0` |
  | 13 | Lyra Dawnbringer | `tmpl == LordEffect` |
  | 14 | other creatures, and a legend whose name is already on our battlefield | - |
  | 20 | Serra | `!loyalty_abilities.empty()` |
  | 21 | Swords, Unexpectedly Absent | `tmpl == Removal` |

  Add `CastOrderTierName` labels for the report.
* **Two conditionals a scalar rank cannot express.** They belong in `ArrangeCastOrder`, which sees the
  cast set; `CastOrderRank` sees only the state.
  * (i) **Greaves hoist.** If the plan carries a haste Equip onto a Giada cast in this plan
    (`mana_dork` + `mana_only_subtype`) and `FirstUnpayablePos(order) >= 0`, move Giada to just after
    Greaves. The walk does not credit the unlock, so "unpayable" here means "the line needs her mana".
    This is why her range cannot be a `CastOrderFallbackRanks` funding ladder: that ladder's
    projection cannot see her as a producer.
  * (ii) **No life source.** If no converter is on our battlefield or in the cast set (k = 0), move
    Giada and Youthful ahead of the engines.
* **Arithmetic behind the conditionals** (k = converters, m = creatures already out):
  * Thune or Lyra AoD first beats a body by k(1+m) counters.
  * Thune or Lyra AoD first beats Youthful by k(1+m) - 1.
  * Lyra AoD first beats Giada by (k-1)(1+m_Angels): a tie at k = 1, worse at k = 0. Thune gains
    extra from non-Angels such as Bishop.
* **Check when implementing:** with Greaves cast early, its `{2}` must not take a `{W}` that a later
  `{W}{W}` Angel needs (Sanctuary or Sol Ring `{C}` should pay it). The whole-turn batch prepay should
  cover this; pin it with a fixture.
