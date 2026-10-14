# Auras: cast-order draft (agent proposal for USER review, 2026-10-14)

Deck: `decks/Auras/Auras.cod` (GW Bogles: 4 creatures, 14 Auras, 21 lands). Provider: `AurasProvider`. It has
no `CastOrderRank` of its own and inherits `GenericProvider`'s.
Deck flags: `feeds_combat=yes`, `uses_second_main=no`.

## 1. Status: **C**, layered on top of D

* **D: generic tiers.** Creatures are 10 and every other spell is 20. This is all the report shows.
* **C: an agent-authored Aura sub-order that plays but has never been reviewed.** It lives in the
  `GenericProvider::CastOrderRank` Light-Paws block (`src/ai/DecisionProviders.cpp:3102-3140`), behind
  `MTG_AURA_CAST_ORDER`, which defaults ON.
  * It came in as commit `21f8433f0` (2026-08-02), measured at -0.0117 with 24 jobs better and 0 worse.
  * The ranks are: plain Aura 20, scaling Aura 22, restricted Aura 23.
  * It is live only while a Light-Paws is already on the battlefield.
  * No USER ruling on the Auras cast order exists in `docs/design`. `cast-order-rankings.md` § Auras holds only a
    flag fix, and the report cannot show this sub-order, because the report state has no Light-Paws.
  * `auras-discard-policy-proposal.md` §9 calls the order "already reviewed". I found no USER quote to back that.
* **No USER order exists, and none is OFF.**

## 2. What plays today

| pos | card(s) | main | rule that put it there |
|---|---|---|---|
| L | land drop | m1 | Report: "no cantrip in the deck", so it goes FIRST. At depth > 0 the land is folded into the searched plan. |
| 10 | Light-Paws, Kor Spiritdancer, Slippery Bogle, Gladecover Scout (**tie**) | m1 in effect | Generic creature tier. The report labels them m2, but that label is inert: with `uses_second_main=no` the phase filter never runs (`TurnSolver.cpp:20388`). |
| 20 | **No Light-Paws in play at the start of the turn:** all 14 Auras (**tie**, so plan/hand order decides) | m1 | Generic noncreature tier. The SeqAura plan sort moves a restricted Aura behind its same-turn enabler. |
| 20 / 22 / 23 | **Light-Paws in play:** plain {Rancor, Audacity, Hyena Umbra, Spider Umbra, Gryff's Boon, Spirit Link, Alpha Authority, Spirit Mantle, Armadillo Cloak} (**tie**), then scalers {Ethereal Armor, All That Glitters, Ancestral Mask} (**tie**), then restricted {Daybreak Coronet, Lion Umbra} (**tie**) | m1 | `MTG_AURA_CAST_ORDER`. The idea: cast the Aura worth duplicating LAST, so an earlier Light-Paws fetch can take a copy while ours is still in hand. Restricted Auras go last because their host must already be enchanted, which is a legality rule. |

Checked: no card in the list is order-opaque (`OrderingOpaque`: no draw / staging / cascade / retrace / impulse /
trick params), so the clean rank sort is what plays.

## 3. Proposed order: one unconditional order, not board-dependent

| pos | card(s) | main | range | why (card text / plan) |
|---|---|---|---|---|
| L | land drop | m1 | - | **First.** The search folds it into the plan at depth > 0. See §4 D5. |
| 1 | Light-Paws, Kor Spiritdancer, Slippery Bogle, Gladecover Scout (**tie**) | m1 | - | Both engines must be on the battlefield before any Aura is cast or enters: Light-Paws says "Whenever an Aura you control enters, if you cast it", and Kor says "Whenever you cast an Aura spell". Hosts must exist too. The tie is safe: none of the four has a cast or ETB trigger that another creature feeds, and colour is settled by the whole-turn prepay (`BatchPrepayMainCasts`), not by the order. |
| 2 | Armadillo Cloak (MV3, +2) | m1 | - | **Plain Auras go highest MV first.** The big fetch brackets go first, so the MV-1 fetch comes LAST. That is where the 2026-08-25 USER fetch rule ("last chance at the scaler") takes Ethereal Armor. A Cloak fetch made while a Mask is in hand can also take a second Mask. |
| 3 | Alpha Authority (MV2, +0), then Spirit Mantle (MV2, +1) | m1 | - | Same rule. Within one MV the weakest goes first, so the stronger flat name is still fetchable when the weak one's trigger resolves. |
| 4 | Spirit Link (0), then Gryff's Boon / Hyena Umbra / Spider Umbra (+1; **tie**), then Rancor / Audacity (+2; **tie**) | m1 | - | MV1 plains, weakest first. The ties are safe because tied cards have equal power and the fetch pool holds the other copy's equal (Rancor ×4 / Audacity; three +1 names). |
| 5 | Ancestral Mask (MV3), then All That Glitters (MV2), then **Ethereal Armor (MV1) last** | m1 | - | **Scalers go after every plain Aura, highest MV first.** A Mask's or Glitters' fetch can take a second Ethereal Armor while ours is still in hand. The reverse order blocks the Armor name. Each scaler reads the enchantment count, so a duplicate compounds. |
| 6 | Daybreak Coronet, Lion Umbra (**tie**) | m1 | - | **Last, for legality.** Coronet says "Enchant creature with another Aura attached to it" and Lion Umbra says "Enchant modified creature", so any other Aura in the plan may be their enabler. The tie is safe: both are MV2 with +3, and each blocks only its own name. |

Every nonland card is covered: 4 creatures and 14 Auras. No ranges are needed: nothing here changes
affordability except through the prepay. Main phase is m1 throughout.

## 4. What changes and why it matters

* **D1. Light-Paws cast THIS turn: the sub-order is off today.**
  * Why: the rank is read once, on the section's start board (`TurnSolver.cpp:42926`, mirrored in AIEngine).
    Light-Paws is not on that board yet, so every Aura ties at 20 and hand order decides.
  * Example (constructed): T4, 4 lands, Light-Paws + Ethereal Armor + Rancor.
    * Armor first: the fetch cannot take Armor. Rancor and Audacity tie at 2, so library order decides.
      Then Rancor's fetch takes the next MV1. The new Auras total +9 or +10.
    * Rancor first: the fetch takes Armor (the copy in hand makes it scale-first). Then our Armor fetches
      Audacity. The total is 2 + 4 + 4 + 2 = +12.
    * So the +12 line is unfindable whenever hand order puts the Armor first.
  * Frequency: 0 of 160 probe games, because Light-Paws is usually a T2 play. Rare, but it is a real prune.
* **D2. Scalers, highest MV first (today a hand-order tie at 22).**
  * Example (constructed): T4, Light-Paws out, Ancestral Mask + Ethereal Armor in hand.
    * Mask first: the fetch takes a second Armor, then our Armor fetches Rancor.
    * Armor first: the fetch takes Rancor, then the Mask fetch takes All That Glitters, a singleton. If
      Glitters is gone, it takes Daybreak Coronet at +3.
  * The two lines tie when Glitters is available. Otherwise Mask-first gains N-3 power, where N is the final
    enchantment count (>= 4).
* **D3. Plain Auras, highest MV first (today a hand-order tie at 20).**
  * Probe, regression seed (`--seed 2008 --game-index 6`), T3: hand order cast Spirit Mantle and then Spider
    Umbra. Mantle fetched All That Glitters, and Spider's fetch, now the last one, took Ethereal Armor.
  * Cheapest-first would have taken Spider's fetch while the Mantle was still pending. The fetch code then
    calls it "not last" and ranks Armor (realized 2 at one enchantment) level with Rancor and Audacity (2).
    Library order decides, and Armor's +4 can drop to +2.
  * Same shape in smoke seed `--seed 1021 --game-index 20`, T4: Cloak fetched Mask and Spirit Link fetched
    Armor.
* **D4. Kor in play: the order changes nothing that can be reached.**
  * Every Aura cast opens a breakpoint (site 10, `TurnSolver.cpp:50591-50614, 50867`) and the turn is
    re-planned after the draw.
  * `MTG_BP_NEW_ONLY` keeps the plan's own pending casts, and every singleton plan is enumerated too. So
    "Mask, draw, Rancor" and "Rancor, draw, Mask" reach the same end state. Without Light-Paws there is no
    shuffle, so the same two cards are drawn either way.
  * With Light-Paws, each fetch shuffles, and which card Kor draws depends on the order. That is
    clairvoyance-only, which you have ruled out as a goal, so this order needs no Kor exception.
* **D5. The land stays first.** Kor makes every Aura a cantrip, so "draw before the land" is a live
  principle. Here it is worth almost nothing:
  * 12 of the 21 lands tap for both colours (Canopy, Razorverge, Brushland).
  * Every land enters untapped, except a Razorverge played past its third land, which enters tapped
    whenever it is played.
  * So moving the land neither creates nor costs mana. Only a Branchloft face or a Forest-vs-Plains pick
    could use the drawn card.
* **D6. The report becomes the truth.** An unconditional order prints exactly what plays. Today's
  board-dependent 22/23 tiers have never appeared in any `--cast-order-report`.

## 5. Questions for the USER

1. **One unconditional Aura order on every turn**, instead of only while Light-Paws is already in play?
   Default **yes**. It fixes D1. The only other effect is Kor-turn prefixes, which D4 shows are neutral.
2. **Within plain Auras and within scalers: highest MV first** (Cloak before Mantle before the 1-drops; Mask,
   then Glitters, then Armor)? Default **yes**.
   * The alternative is cheapest-first: Kor-friendly in principle, and it gets Light-Paws enchanted before an
     MV-2 fetch so Coronet or Lion Umbra become fetchable.
   * Under today's fetch ranking, though, cheapest-first fetches a flat 1-drop instead of Armor more often
     (D3).
3. **Within one MV, weakest first** (Spirit Link / Alpha Authority before Rancor / Audacity)? Default **yes**.
4. **Search the Aura order on Light-Paws turns** (`WantsCastOrderingSearch`), only for plans with 2+ Auras and
   Light-Paws in play or in the plan (at most 3! = 6 orders, deduplicated by end state)? Default **no for
   now**: adopt the rank first, then A/B the search.
   * The fetch is not searched, so today the order is a prune over which Auras Light-Paws can fetch.
5. **Fix the Light-Paws fetch ranking separately?** Default **yes**, as its own measured change.
   * The ranking is `LightPawsAuraCandidates` mode 4 (`DecisionProviders.cpp:1727-1790`).
   * (a) Your 2026-08-25 rule says "the last 1-drop Aura fetch in the plan". The code treats a fetch as last
     only when the hand holds no Aura of ANY MV castable this or next turn.
   * (b) An Armor copy in hand always outranks a Mask. So a Cloak cast with Armor in hand fetches the
     duplicate Armor (+N) instead of a Mask (+2(N-1)), giving up N-2.

## 6. Generic-order findings

* **Watcher cantrips are invisible to the land-drop rule and the report.** `--cast-order-report` says "no
  cantrip in the deck" (`main.cpp:7914`, `IsIdealOrderCantrip` over the deck's own cards). Kor's
  `draw_on_aura_cast`, and Sram's `draw_on_cast_subtypes` the same way, turn every watched cast into a
  cantrip. The report should say so.
* **The m2 baseline for creatures is wrong in principle for an Aura deck.** It is inert here. Light-Paws and Kor
  ENABLE same-turn main-1 Auras; deferring them past combat would forfeit every fetch and draw. If this deck ever
  played a second main, a creature watcher of a main-1 class (`aura_cast_tutor_attach`, `draw_on_aura_cast`)
  should pull to Main1, the same way the dependency-map enabler pull does.
* **Board-conditional ranks cannot see this plan's own earlier casts.** The rank is read on the section's start
  board, which is the D1 defect. Any generic tier keyed on "X in play" has the same hole when X is cast in the
  same plan.
* **A stale card note.** Kor Spiritdancer's `oracle_text` bracket in `cards.json` still says the draw goes "to
  hand for later turns (no same-turn re-solve)". Site 10 (`MTG_BP_PUT_IN_HAND`, default ON) now re-plans after
  that draw. It is a data-note fix only.
* **The Light-Paws trigger does not depend on the host** (card text; `EffectHandler.cpp:326-341`). Any Aura you
  cast triggers it, and the fetched Aura always goes onto Light-Paws. Which creature a cast Aura enchants is the
  searched `enchant_target` variant, not an order question.

## 7. Implementation notes

* **Where.** Add an `AurasProvider::CastOrderRank` / `CastOrderTierName` override (`DecisionProviders.h:1964`).
  Leave the generic Light-Paws block alone: only this deck carries `aura_cast_tutor_attach`, so the generic
  block simply stops being reached here. Other decks hold Auras too (land Auras, Sram lists), so the
  unconditional order must NOT go into `GenericProvider`.
* **Params, by house style (no names).**
  * Creature: `IsCreature()`. Keep it at 10.
  * Aura: `is_aura`. Restricted: `!aura_enchant_requires.empty()`.
  * Scaler: `!aura_scale_kind.empty() && aura_scale_power > 0`.
  * Position keys: `ManaValue()` and `aura_power_bonus`.
* **Suggested ranks.**
  * Plain Auras: `21 + 3*(3 - MV) + min(power, 2)`. That gives Cloak 23; Alpha 24, Mantle 25; Spirit Link 27;
    +1 MV1s 28; Rancor/Audacity 29.
  * Scalers: `31 + (3 - MV)`. That gives Mask 31, Glitters 32, Armor 33.
  * Restricted: 40.
  * The ladder stays inert, since no card has a range.
* **Gate.** `MTG_AURAS_ORDER`, read with `EnvOn` plus a heurarm slot. Default OFF until you sign off; `=0`
  plays exactly today's `MTG_AURA_CAST_ORDER` behaviour.
* **Byte-identity.** The order is unconditional, so digests move on every multi-Aura turn, Kor-only turns
  included (neutral by D4). If byte-identity on Light-Paws-free turns matters for reading the A/B, gate it on
  "a Light-Paws in play OR in hand" instead. The hand is visible to the rank; the plan is not.
* **Engine limitations.**
  * (i) A scalar rank cannot express "restricted last only when its host is not already enchanted". Always-last
    is safe, and `CastOrderActionRank` sees `enchant_target` if the case ever matters.
  * (ii) The fetch target is a heuristic pick (`LightPawsAuraCandidates().front()`), not a searched decision.
    So the cast order plus the fetch ranking jointly decide what Light-Paws fetches (questions 4 and 5).
  * (iii) The cast-ordering search path already guards restricted-before-enabler
    (`OrderingPlacesRestrictedAuraBeforeEnabler`).
* **Dig timing.** Horizon Canopy's sac-draw (dig) runs at the end of the plan's apply, after the casts. It is
  outside `CastOrderRank`, so this order does not affect it.
* **Measuring.** Run the Auras suite cells and the deck's references. Every other deck stays byte-identical,
  because only `AurasProvider` changes. Probes: 160 single games at the play profile (d5/b20), logs in
  `build/scratch/castorder/probe_Auras/`.
