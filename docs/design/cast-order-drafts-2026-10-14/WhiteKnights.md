# WhiteKnights: cast-order draft (2026-10-14)

Deck: `decks/WhiteKnights/WhiteKnights.cod`. Mono-W Knights: 19 Plains + 4 Remote Farm, 1 Aether Vial, 1 Sol Ring, and
1 each of Swords and Unexpectedly Absent. **Every creature card is a Knight** (Kinsbaile is a Kithkin Knight).
Provider: `WhiteKnightsProvider : KnightsProvider : VialProvider`. Single main (`uses_second_main=no`).
- No order-opaque card. Basri's cycling is an activated ability, not a `draw` param.
- Vial puts are sequenced at their creature's rank (USER, 2026-10-06).

## 1. Status: (A) by inheritance only. Effectively (D) for every card Knights lacks.

- The deck runs the USER-agreed Knights order: `MTG_KNIGHTS_ORDER`, default ON (`DecisionProviders.cpp:7675`).
  - Neither Knight provider overrides `CastOrderRank`, so it comes in through `VialProvider::CastOrderRank`.
  - The USER reviewed that order for the *Knights* list (2026-08-19). It was never reviewed for this list.
  - The planned "does it transfer" measurement (`analysis-WhiteKnights.md:245-247`, "5e") was never recorded.
- Live here: Worthy Knight at 8. The Contender clause is moot, because the current list has no Contender.
- Everything Knights lacks sits on generic tiers: Silverblade, Sol Ring, Kinsbaile, Hero, Accorder, Basri, and the
  removal.
- **No USER order is OFF.**
- One open main-phase measurement was never ruled on by the USER (§4.4, Q4).

## 2. What plays today

| pos | card(s) | main | rule |
|---|---|---|---|
| land | the land drop | m1 | First. The deck has no draw. |
| 5 | Sol Ring | m1 | Generic mana-rock tier. |
| 8 | Worthy Knight | m1 | Inherited Knights clause (cast-trigger watcher). |
| 10 (TIE x10) | Basri, Dauntless, Venerable, Accorder, Adeline, Benalish, Exemplar, **Silverblade**, Hero, Kinsbaile | m1 | Generic creature tier. The tie falls to hand order, and **Silverblade's place in it is NOT free** (§4.1). |
| 20 | Aether Vial, Swords to Plowshares, Unexpectedly Absent | m1 | Generic noncreature tier. Swords and UA are never cast autonomously: there is no opponent creature, and self-targeting is opened only in human play (their cards.json notes). |

The report's m2 labels (Basri, Dauntless, Venerable, Worthy Knight, Accorder, Silverblade, Hero) are inert:
- `MainPhaseFilterActive` returns false on a single-main game (`TurnSolver.cpp:20388`).
- The provider does not opt into `ClassifiesMainPhases`.

## 3. Proposed order

The land drop goes FIRST, because the deck has no draw. Remote Farm enters tapped either way.

| pos | card | main | range | why |
|---|---|---|---|---|
| 1 | Sol Ring | m1 | - | Its `{C}{C}` pays the generic part of the casts after it. Not a Knight, so Worthy Knight misses nothing. |
| 2 | Worthy Knight | m1 | - | All ten creature names are Knights, so every later cast is one more 1/1 Human (it also triggers on Silverblade's cast). |
| 3 | **Silverblade Paladin, when an attack-ready, unpaired creature with power >= 2 is on the battlefield** | m1 | - | Its trigger A pairs NOW with a creature that attacks this turn, so double strike lands this combat. Probe A: the T5 kill instead of T6. In one of the two hand orders, today's engine cannot reach that line even at d8/b0. |
| 4-12 | Basri, Dauntless, Venerable, Accorder, Adeline, Benalish, Exemplar, Hero, Kinsbaile | m1 | - | FREE block (see below). |
| 13 | **Silverblade Paladin, otherwise** | m1 | - | After the tribe, so trigger A picks the STRONGEST of this turn's entrants. Placed early, trigger B pairs it with whatever enters next. |
| 14 | Aether Vial | m1 | - | Order-inert. It charges at upkeep, and any source pays its `{1}`. |
| - | Swords to Plowshares, Unexpectedly Absent | m1 | - | No slot is needed: they are never cast autonomously, because there is no legal target. KittyEquipment's USER order does the same: "Removal is never cast ... and needs no slot" (`DecisionProviders.cpp:26268`). |

**Why the 4-12 block is free.**
- The lords' bonuses (Benalish, Exemplar) and Kinsbaile's double strike are static (CR 611.3). Adeline's power is a
  characteristic-defining count.
- Battle cry (Accorder, Hero), Hero's tokens and Adeline's token all trigger on attack. Basri's tap ability needs an
  unsick Basri.
- Dauntless's and Venerable's riders are inert.
- Everything cast this turn is summoning sick.
- Colour is not a rank question: the only coloured pips are W, and every land makes W.

## 4. What changes and why it matters

1. **Silverblade before the turn's other creatures when an attack-ready partner is up. A hand-order tie decides this
   today, and it is a hard prune.**
   - Probe `probe_WhiteKnights/run.sh`, board A: 5 Plains and a ready Venerable Knight. The hand is Accorder Paladin
     + Silverblade, so 5 mana casts exactly both.
   - Hand order [Accorder, Silverblade], at d0, d5/b20 and **d8/b0**: Silverblade pairs with the sick 3/1 Accorder
     (the hook picks the highest power). The T4 swing is **2**.
   - Hand order [Silverblade, Accorder]: it pairs with the ready Venerable. The T4 swing is **4**.
   - T5 is 15 either way. So pairing with the ready creature is +2 this turn and costs nothing afterwards.
   - At opponent life 19 that is a **T5 kill against T6** with the same cards and mana. The search cannot find it,
     because the order is fixed before the search runs.
   - With 3 Silverblades in the list, "Silverblade + another creature, with a creature already out" is a T4-T5
     staple.
2. **Silverblade AFTER the tribe when no strong ready partner is up.**
   - Probe W: the only ready partner is a 1/1. In hand order [Silverblade, Accorder] it pairs with the 1/1 (+1 now,
     then -1 every turn after). In hand order [Accorder, Silverblade] it pairs with Accorder. By T6 that is 28 vs 29
     damage.
   - The existing searched decline twin (`AppendSoulbondDeclineVariants`, `TurnSolver.cpp:54141`) skips any plan
     that "also casts a 2+-power creature (the pairing would land on THAT)". That is only true when Silverblade is
     ordered after that creature, which today is a hand-order tie. The late slot makes that comment true.
3. **What no scalar rank can say.** The early slot gives up a real alternative: pairing with a stronger creature
   cast this turn (Adeline, Hero) for double strike from next turn on.
   - Pair-now with a ready partner of power `a` beats pair-big with power `b` when `a(k+1) > bk`, where `k` is the
     number of further attacks.
   - Example: a ready 2-power creature beats a sick 5-power Adeline only when this turn's attack is the last one
     (`k = 0`). With one more attack it is 4 against 5.
   - So this is a judgement, and the complete fix is a searched two-way twin (Q2), on the 10-06 decline-twin
     precedent.
   - The conditional rank is the canonical first try, and the only answer at d0.
4. **The second main is an open, un-ruled main-phase call, and its measurement is stale.**
   - `analysis-WhiteKnights.md:818-858` forced a second main (`MTG_FORCE_USES_M2`): -0.0072 turns at d0, with 42
     games earlier and 0 later out of 6000, at the cost of a doubled per-turn solve.
   - It was recorded as "the decision is the user's". I can find no USER answer.
   - It was measured on 09-26 on the v1 list (3 Contender, 3 Vial, Lightning Greaves, Valiant Knight, 1
     Silverblade), not this one, and before the value leaf.
   - Nothing in this list makes a post-combat resource. So I suspect a d0 main-1 gap that the greedy second main
     papers over, rather than a real m2 line. No game was replayed to name it.
5. **Removal gets no slot.** No play change.

## 5. Questions for the USER

1. Silverblade at a conditional slot: right after Worthy Knight when an attack-ready, unpaired creature with power
   >= 2 is out, otherwise after the tribe? **Default: yes.**
2. Also add a searched twin, so both "pair now with the attack-ready creature" and "pair this turn's strongest new
   creature" stay findable at any budget (the decline-twin shape)? **Default: yes**, measured with an A/B before
   adoption.
3. In `SoulbondPartner`, at equal power, prefer the creature that can attack this turn? **Default: yes.** It is
   sound: the same value from next turn on, plus this combat. Today the tie breaks by card number, so a sick Worthy
   Knight (2/2) can take the pairing from a ready 2/1.
4. Keep WhiteKnights single-main (as the USER ruled for Knights) until a second main is re-measured on THIS list and
   the line it buys is named? **Default: yes.**
5. Does the Knights order (Worthy Knight first) carry over to this list unchanged, apart from Silverblade? The 08-19
   ruling was for the Knights list. **Default: yes.**

## 6. Generic-order findings

- **G1. A soulbond creature ties at 10, and the tie decides its partner.** Hence it decides this turn's double strike
  (§4.1). The rule above is card-param general (`soulbond`). Silverblade is the only soulbond card in cards.json.
- **G2. The decline twin's gate assumes an order the rank does not set** (§4.2). Re-derive the gate once Silverblade
  has a deliberate slot.
- **G3. `SoulbondPartner` / `SoulbondCandidatePower` ignore readiness at equal power (Q3)**, and also ignore double
  strike the partner already has.
  - With Kinsbaile out, every Knight already has double strike, so pairing one adds nothing, while a 1/1 token
    partner gains its full power.
  - This matters only for the single Kinsbaile, and it is a hook question rather than an order question.
- **G4. `ClassifyMainPhase` labels Silverblade and Worthy Knight m2.** Both change THIS turn's combat when cast
  pre-combat: the pairing, and the tokens from the turn's later Knight casts. Inert here.
- **G5. The report lists never-castable removal as a live slot (20/m1).** This is cosmetic. Also,
  `EquipmentProvider::CastOrderTierName` still names a rank-30 "GOLDFISH-INERT REMOVAL" tier (`:26963`) that its
  10-04 order no longer uses.

## 7. Implementation notes

- **Where:** add a `WhiteKnightsProvider::CastOrderRank` / `CastOrderTierName` override that falls through to the
  inherited Knights clauses. Do not put it on `VialProvider`; the shared-base note is in the Knights draft, Q3.
  - If the Knights clauses move to `KnightsProvider`, the override calls `KnightsProvider::CastOrderRank`.
  - If a slivers_vial order lands on `VialProvider`, both Knight decks inherit it unless `KnightsProvider`
    overrides.
- **Params:**

  | slot | rank | key |
  |---|---|---|
  | Sol Ring | 5 | `mana_rock` (unchanged generic) |
  | Worthy Knight | 8 | inherited |
  | Silverblade, early | 9 | `params.soulbond`, plus a battlefield scan for an own creature with `SoulbondPartnerIndex(perm, s) < 0`, `!SoulbondNumberIsClaimed`, `perm.CanAttack()` and `SoulbondCandidatePower(s, ctrl, perm) >= 2`. That is the same threshold as `SoulbondOnlyWeakPartners`' `min_power`. |
  | Silverblade, late | 11 | otherwise |
  | Vial | 20 | unchanged |
  | Swords, UA | 20 | unchanged; never cast |

  The board-scan shape is the Contender's 9/12 split.
- **Q3:** in `DecisionProvider::SoulbondPartner` (`DecisionProviders.cpp:1613`), break equal power toward
  `!entered_this_turn` before the card-number tie-break. Silverblade is the only soulbond card, so this touches only
  this deck.
- **Q2:**
  - Extend `AppendSoulbondDeclineVariants` with a twin that flips Silverblade between its two slots, or that tells
    the hook to take the best attack-ready candidate, via a plan scope like `PlanSoulbondDeclineScope`.
  - It applies only when the plan casts a soulbond creature AND a creature stronger than the best ready partner.
  - Fold the flag into the plan hash, as `soulbond_decline` is (`TurnSolver.cpp:17516`).
- **Engine limitation:** a scalar rank cannot price "pair now vs pair big". Only the twin makes both findable. The
  conditional rank itself needs no new machinery.
- **Probes** (read-only, under `build/scratch/castorder/probe_WhiteKnights/`): `run.sh` (boards A/B) and `weak.sh`
  (board W). B (Worthy Knight + Silverblade, ready Venerable) pairs with Venerable only because it holds the lower
  card number.
