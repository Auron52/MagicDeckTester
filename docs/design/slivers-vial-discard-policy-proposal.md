# slivers_vial — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored from `docs/design/discard-bucket-authoring-brief.md`. **Proposal only — no `.cpp`/`.h` was
touched.** Target: `VialProvider::CleanupDiscardCandidates`, behind a default-on
`MTG_VIAL_BUCKET_DISCARD`.

---

## SCOPE WARNING — READ FIRST: `KnightsProvider` DERIVES FROM `VialProvider`

`class KnightsProvider : public VialProvider` (`src/ai/DecisionProviders.h:445`). A policy placed on
`VialProvider` is **inherited by Knights and by every future Aether-Vial deck**. A separate agent is
authoring the Knights policy, which will override on `KnightsProvider`; **this document is authored
for the slivers list specifically.**

That inheritance is why every predicate below is read from the **hand and battlefield via
`params`**, never from a Sliver name and never from a decklist fact the provider cannot see. What
each predicate does on the Knights list, checked against `decks/Knights/Knights.cod` and
`cards.json`:

| predicate | on slivers | on Knights |
|---|---|---|
| `creature_mana_only` (dominated land) | Ancient Ziggurat ×2 | **none** (Tournament Grounds is `produces:[W,R,B]`, unrestricted) → inert |
| `can_animate` (manland-is-also-a-body) | Mutavault ×2 | **none** → inert |
| `grants_haste` / `grants_double_strike` / `grants_replicate_to_subtypes` | Cloudshredder, Hivepool, Hatchery | **none in main** → the unique-grant bucket is EMPTY and every Knight falls to bodies. **That is an under-fit, not a safe default** — which is exactly why Knights needs its own override. |
| `power_bonus>0 && !subtypes_affected.empty()` | Muscle / Predatory / Sinew | Benalish Marshal, Inspiring Veteran, Knight Exemplar → correctly top-of-bodies |
| `upkeep_adds_charge` (Vial quota 1) | Aether Vial ×4 | Aether Vial ×4 → fires; plausible there too, but it is Knights' call |

**The one trap I deliberately designed around.** The obvious land quota for this deck is a
constant — "4, because the creature curve tops at mana value 2". That constant is **false for
Knights** (Haytham Kenway is MV 4; `Knights.profile.json` `vial_target_mv` 3 vs slivers' 2). A
hardcoded 4 would silently become Knights' quota. So the land target below is **derived from the
hand**, not authored: `2 + max(effective MV over the hand's own creature cards)`, clamped `[3,5]`.
On slivers that evaluates to 4 for any hand holding a 2-drop; on a Knights hand with a Haytham it
evaluates to 5 without anyone writing "5" anywhere.

---

## 1. The deck, and therefore its shape

Sliver tribal aggro on Aether Vial: 20 lands, 4 Aether Vial, 35 Sliver creatures, 1 Thrumming
Hivepool. **Every creature in the deck costs 1 or 2 mana** — the only card above MV 2 is Thrumming
Hivepool at `{6}` with affinity for Slivers. Damage is **purely combat**: the one non-combat-damage
effect in the list, Leeching Sliver's `attack_trigger_life_loss`, still requires a Sliver to attack.
To function the deck needs (a) one coloured land, without which nothing but the two artifacts is
castable; (b) a *second* coloured source for its three two-pip gold Slivers; (c) bodies, since the
clock is literally the number of attacking Slivers; (d) the *scaling* grants, each worth +1 damage
per attacking Sliver per turn; (e) the *unique* grants — haste, double strike, replicate — which the
board either has or does not; and (f) the Vial, which converts hand cards into board presence at
zero mana.

**Shape: tribal aggro with a deployment engine — 4 buckets.** It is not the brief's "simple
aggro → 2 buckets" case, because the Vial is neither mana nor a threat, and because the
grant-vs-body split is a genuine slot boundary rather than an ordering (below). It is not a combo
deck: there is no assembly, no dig, and no piece whose absence blanks another.

**Where the "lords vs bodies" axis really is.** Nearly every Sliver grants something to all Slivers,
so the usual tribal "lord vs body" split does collapse — but it collapses into a *sharper* question,
and this is the interesting part of the deck: **is the grant BINARY or ADDITIVE?** A second Muscle
Sliver is a second real +1/+1; a second Cloudshredder's haste grant is worth exactly nothing. That
is structural rule 2 (net of board) applied to **abilities** rather than to counts, and it is the
policy's headline rule.

---

## 2. Card-by-card role table

All 19 names read from `src/cards/data/cards.json` (Rule 0). `params` cited are the ones the
classification keys on. **Bracket notes matter here more than on any other deck in the repo: four
Slivers' entire text is unmodelled, and a fifth's is modelled but structurally inert.**

### Mana — lands (20)

| card | n | `produces` | key params | bucket |
|---|---|---|---|---|
| Cavern of Souls | 4 | W U B R G **C** | `colored_creature_only` | MANA / coloured |
| Secluded Courtyard | 4 | W U B R G **C** | `colored_creature_only`, `colored_creature_ability_ok` | MANA / coloured |
| Unclaimed Territory | 4 | W U B R G **C** | `colored_creature_only` | MANA / coloured |
| Sliver Hive | 4 | W U B R G **C** | `colored_creature_only`, `tap_token_cost {5}` | MANA / coloured |
| **Ancient Ziggurat** | 2 | W U B R G (**no C**) | **`creature_mana_only`** | MANA / coloured — **DOMINATED, sheds first** |
| **Mutavault** | 2 | **C only** | `can_animate`, `animate_cost {1}`, 2/2 | MANA / colourless — **and a body** |

### Deployment engine (4)

| card | n | cost | params | bucket |
|---|---|---|---|---|
| Aether Vial | 4 | `{1}` | `upkeep_adds_charge` | VIAL, quota 1 |

### Slivers and the Hivepool (36)

| card | n | cost | base P/T | modelled contribution | class | bucket |
|---|---|---|---|---|---|---|
| **Thrumming Hivepool** | 1 | `{6}` | — | `grants_double_strike` + `grants_haste` + `upkeep_creates_tokens:2`, `affinity_for_subtype` | BINARY ×2 (+additive tokens) | UNIQUE GRANT |
| **Cloudshredder Sliver** | 3 | `{R}{W}` | 1/1 | `grants_haste`, `subtypes_affected:[Sliver]` | **BINARY** | UNIQUE GRANT |
| **Hatchery Sliver** | 4 | `{1}{G}` | 2/2 | `grants_replicate_to_subtypes`, `has_replicate` | **BINARY** | UNIQUE GRANT |
| Muscle Sliver | 4 | `{1}{G}` | 1/1 | `power_bonus:1, tough_bonus:1, subtypes_affected:[Sliver]` | **ADDITIVE** | BODIES (top) |
| Predatory Sliver | 4 | `{1}{G}` | 1/1 | `power_bonus:1, tough_bonus:1, subtypes_affected:[Sliver]` | **ADDITIVE** | BODIES (top) |
| Sinew Sliver | 4 | `{1}{W}` | 1/1 | `power_bonus:1, tough_bonus:1, subtypes_affected:[Sliver]` | **ADDITIVE** | BODIES (top) |
| Leeching Sliver | 2 | `{1}{B}` | 1/1 | `attack_trigger_life_loss:1, subtypes_affected:[Sliver]` | **ADDITIVE** | BODIES (top) |
| Crystalline Sliver | 3 | `{W}{U}` | **2/2** | `params: {}` — *"shroud not modelled"* | none | BODIES (2-power) |
| Hibernation Sliver | 1 | `{U}{B}` | **2/2** | `params: {}` — *"bounce lord not modelled"* | none | BODIES (2-power) |
| Plated Sliver | 4 | `{W}` | 1/1 | `power_bonus:0, tough_bonus:1` — **toughness-only** | **INERT** | BODIES (last) |
| Galerider Sliver | 4 | `{U}` | 1/1 | `params: {}` — *"flying lord not modelled"* | none | BODIES (last) |
| Striking Sliver | 2 | `{R}` | 1/1 | `params: {}` — *"first strike lord not modelled"* | none | BODIES (last) |

**Every name is classified. `rest`/unrecognised is EMPTY on this decklist** — which matters, because
an omitted card falls through to the shared tier B (descending mana value), the exact ranking this
policy exists to overturn (the Mirrorwing gi295 lesson).

### The four verifications behind that table

1. **Anthems are ADDITIVE.** `ComputeLordBonus` accumulates `pb += ldef->params.power_bonus` inside a
   per-lord loop (`src/core/SpellEffects.h:3213`). No Sliver sets `lord_excludes_self`, so each anthem
   is **self-inclusive**: a lone Muscle Sliver is a 2/2, and it additionally makes every other Sliver
   +1/+1. An anthem therefore **strictly dominates** a plain 2/2 body — no judgment call needed to
   rank it above Crystalline/Hibernation.
2. **`grants_replicate_to_subtypes` is BINARY, and this is a code fact, not an oracle reading.**
   `CanReplicate()` returns **`bool`** (`SpellEffects.h:11563`), and the replicate fan's `kmax` is
   bounded by `AvailableManaPool(state).Total()` — the **mana pool**, not the number of granters
   (`TurnSolver.cpp:15560-15578`). A second Hatchery Sliver adds **exactly zero** replicate capacity.
3. **`grants_haste`/`grants_double_strike` are BINARY** — keywords, tested via `HasKeyword`. Note
   `IsLordPermanent`'s own comment confirms the split: *"vanilla creatures with
   subtypes_affected-only params (haste granters like Cloudshredder Sliver) do not become P/T
   lords."*
4. **Toughness is STRUCTURALLY INERT, so Plated Sliver's `+0/+1` is worth zero.** The goldfish
   opponent **never attacks and never blocks** (`Dominance.h:37`, `DecisionProvider.h:1250`,
   `SpellEffects.h:6713`). Nothing ever damages our creatures, so `tough_bonus` with
   `power_bonus == 0` can never change a game. Plated Sliver is a 1/1 for `{W}`. This is an
   engine-wide fact, not a Slivers fact, so the predicate is safe on any inheriting deck.

### The manabase finding: two lands are each half a land, in *opposite* halves

| | casts a coloured Sliver | pays `{1}` for Aether Vial / `{6}` for Hivepool / `{1}` animate / `{5}` Hive token |
|---|---|---|
| Cavern / Courtyard / Territory / Hive (16) | **yes** | **yes** (their `{C}` face is unrestricted) |
| **Ancient Ziggurat** (2) | yes | **NO** |
| **Mutavault** (2) | **NO** | yes |

* **Ancient Ziggurat is strictly dominated.** `produces:[W,U,B,R,G]` + `creature_mana_only` gives the
  same coloured capability for creature spells as the four duals, **minus** an unrestricted `{C}`
  face. `ManaPayment.cpp:522` — `if (def.params.creature_mana_only && !for_creature) return false;`
  — and `TurnSolver.cpp:24377` names precisely this case: *"a `creature_mana_only` land (Ancient
  Ziggurat) contributes nothing — an Aether Vial plus slivers batch off that manabase reads
  combined-UNPAYABLE."* **A hand of Aether Vial + Ancient Ziggurat cannot cast the Vial. Ever.**
  This is a domination, not a preference, so it is the first land shed.
* **Ambiguity flagged:** Secluded Courtyard additionally carries `colored_creature_ability_ok`, so it
  is *strictly better* than the other three duals. On this list the difference is near-inert (the only
  creature-source activations are Mutavault's animate — chicken-and-egg, the land is not yet a
  creature — and Sliver Hive's token, whose source is a land). **Not encoded**; noted so nobody
  later mistakes the omission for an oversight.

### Colour requirements — the coloured sub-quota is **2**, not 1

Every dual produces all five colours, so *which* colours is a non-question; *how many sources* is
the question. Three Slivers carry **two coloured pips** and need two coloured sources:
**Cloudshredder `{R}{W}`** (the deck's only repeatable haste), **Crystalline `{W}{U}`**,
**Hibernation `{U}{B}`**. Everything else needs one. Hence a coloured sub-quota of 2 — and note that
the card it unlocks is the deck's most important binary grant.

---

## 3. The buckets, with quotas (all net of board)

### Bucket 1 — MANA (lands). Parent quota **hand-derived**, sub-split COLOURED vs COLOURLESS

There are **no mana rocks and no dorks** in this deck — verified, nothing in the list carries
`mana_rock` and no Sliver produces mana. So the brief's lands-vs-acceleration sub-split does not
apply; **this deck's real sub-split is which FACE of the manabase a card can use.**

* **Parent quota** = `clamp(2 + max_eff_mv_over_hand_creatures, 3, 5)` **minus board mana sources**.
  On any slivers hand holding a 2-drop that is **4**; on an all-1-drop hand, 3. The `+2` is the one
  authored constant and it encodes the deck's realistic deployment rate — two spells a turn (a 2-drop
  plus a 2-mana replicate copy, or two 2-drops). Hand-derived rather than a literal 4 so the quota is
  not a Sliver-curve fact baked onto a shared base class (see the scope warning).
* **Thrumming Hivepool deliberately does NOT raise the land quota**, even at effective cost 6. Its
  affinity is paid in **bodies, not lands** (`ManaPayment.cpp:1355-1373` reduces generic by the number
  of Slivers controlled, counting an animated Mutavault). A Sliver kept instead of a land reduces the
  Hivepool's cost by exactly the 1 mana the land would have added **and** is a body. Hoarding a land
  to cast the Hivepool is self-defeating; this is the free-cast promotion run in reverse.
* **Sub-quota COLOURED = 2**, net of board. A coloured land is one whose `produces` contains a
  non-`Colorless` colour — the 16 duals **and** Ancient Ziggurat. **Mutavault never satisfies it**
  (`produces:["C"]`).
* **Sub-quota COLOURLESS = the remainder.** Fungible upward per structural rule 3: the parent quota
  binds, so a hand with no Mutavault simply keeps four coloured lands.
* **What fills the quota from the battlefield:** every land the active player controls (including a
  Mutavault, which pays generic pips), counted by `CleanupDiscardIsLand` on the definition. Slivers
  on board contribute nothing to this quota — nothing in the deck taps for mana.

### Bucket 2 — AETHER VIAL. Quota **1**, net of board

Its own bucket because it is neither mana (it produces none) nor a body: its role is converting hand
cards into board presence at zero mana. A resolved Vial climbing to `vial_target_mv` = **2** deploys
**25 of the deck's 35 creatures** (every MV-2 Sliver) for free, permanently.

* **Quota 1 because a second copy is near-dead**: the first Vial's counters have already done the
  climbing; the second starts at 0 and repeats a finished job. Corroborated by the deck's own learned
  second-copy marginal, **−0.3423 — the largest negative number in the whole `card_scores` table.**
* **Filled from the battlefield**: any controlled permanent with `params.upkeep_adds_charge`.
* **CONDITIONAL, the Ziggurat caveat:** a Vial whose `{1}` cannot be paid from **generic-capable**
  sources in board + hand (i.e. everything except Ancient Ziggurat) **drops out of its quota slot and
  sheds at S0.** Protecting an uncastable Vial is the Karoo mistake in a different costume. On this
  list 18 of 20 lands are generic-capable, so it fires rarely — but it fires exactly where it matters.

### Bucket 3 — UNIQUE (BINARY) GRANTS THE BOARD LACKS. Quota **1 per distinct grant**

The bucket boundary is the deck's headline rule. Contents by param: `grants_haste`,
`grants_double_strike`, `grants_replicate_to_subtypes`.

* **A card whose only grant is BINARY and whose grant is already on the battlefield is not a grant at
  all — it drops to bucket 4 as a plain body.** So a Cloudshredder Sliver in hand is a unique grant
  on an empty board and an inert 1/1 once *any* haste source is out — including a **Thrumming
  Hivepool**, which carries `grants_haste` too. That is net-of-board applied to abilities.
* **A second copy of the same binary grant is likewise a plain body**, whatever the board says.
* On this list the bucket is at most 3 slots (haste, double strike, replicate) and realistically 2,
  since the Hivepool fills two of them in one card.
* **Additive-grant Slivers are NOT in this bucket** and need no net-of-board treatment at all — their
  grant is never redundant. They sit at the top of bucket 4.

### Bucket 4 — BODIES (the catch-all). Quota: the rest of the hand

Every Sliver, including the additive anthems, Leeching's drain, and every demoted duplicate/redundant
binary granter. "Extra spells are always threats."

---

## 4. Within-bucket order, and the distance-to-playable term

### Bodies, best kept first

The engine-true value of a Sliver in hand is *(its own power once the board's anthems apply)* +
*(what it adds to every other attacking Sliver)*. The second term is the whole tiering:

1. **ADDITIVE SCALING GRANTS — grouped, because in this engine they are the same effect.**
   `power_bonus>0 && !subtypes_affected.empty()` (Muscle / Predatory / Sinew) and
   `attack_trigger_life_loss>0` (Leeching). An anthem's +1 power and Leeching's 1 life loss both
   scale **exactly** with the number of attacking Slivers, and neither is redundant with the other or
   with itself. This is the brief's "similar effects grouped."
   *Inside the tier, anthems rank above Leeching*, on two discriminators that are card facts rather
   than guesses: an anthem's power is **doubled by the Hivepool's double strike** while an attack
   trigger fires once, and an anthem is self-inclusive so it arrives as a 2/2. Both are small; the
   tier is close to a genuine tie and I say so.
2. **PLAIN 2-POWER BODIES** — Crystalline Sliver, Hibernation Sliver, and a demoted Hatchery Sliver
   (still a 2/2 for `{1}{G}`). Ordered by base power, then the distance term, then MV descending.
3. **INERT 1-POWER BODIES, shed first among bodies** — Plated Sliver (`tough_bonus` only, inert),
   Galerider Sliver, Striking Sliver, and a demoted Cloudshredder Sliver (a 1/1 with nothing).

### The distance-to-playable term — three deck-specific inputs, each with a concrete engine reading

**(a) A resolved Aether Vial erases the cost of its whole MV tier.** `eff_cost(card) = 0` when the
card is a creature and some controlled permanent with `upkeep_adds_charge` has
`charge_counters == mv(card)`. That is the engine's own deploy rule, not an approximation
(`SpellEffects.h:11493-11506`). This is the brief's *"a free-cast engine online erases the
distance"*, and it is the sharpest promotion on the deck: with a Vial on 2 counters, **no 2-drop
Sliver is ever "far"**, which in turn makes a surplus land worth even less.
*Graded softening (judgment call, flagged):* `charge_counters + 1 == mv` → treat as cost 1, because
`WantVialCharge` ticks toward it whenever nothing at the current count is being held — the common
case on a deck with 25 cards at one MV.

**(b) Colour reachability, not just a mana count.** `coloured_deficit(card) =
max(0, distinct_coloured_pips(card) − coloured_sources(board+hand))`, with Mutavault excluded from
`coloured_sources`. On a one-rainbow-land board this cleanly separates the three gold two-pip
Slivers from everything else — including demoting Cloudshredder, which is otherwise the top of
bucket 3. This is the real form of the brief's *"colour/mana coverage across board+hand vs the
card's cost"*; a raw MV comparison cannot see it, because every Sliver is MV 1 or 2.

**(c) Affinity shrinks with the board — and here P(play) and value(playing) move TOGETHER.**
`eff_cost(Hivepool) = max(0, 6 − slivers_controlled)`, counting an animated Mutavault. Its generic
pips exclude Ancient Ziggurat, so `generic_reach` must be counted separately from `coloured_sources`.
The board that makes the Hivepool cheap is the board that makes double strike enormous, so a plain
`eff_cost − reach` term gets it right **with no EV model** — the opposite of Minotaur's Kragma, where
the two terms fight and an `EV = P × value` product was needed.

### Unique grants, best kept first (fixed order)

1. **`grants_double_strike`** — doubles all combat damage, and nothing else in the deck can ever
   supply it.
2. **`grants_haste`** — converts a just-deployed Sliver into damage this turn; on a deck that deploys
   every turn (Vial + replicate + land drops) that is close to a standing +1 turn.
3. **`grants_replicate_to_subtypes`** — converts leftover mana into extra bodies, so it is worth
   exactly what the deck's spare mana is: **less** than haste on a land-light board (which is 100% of
   this rule's decisions — see §7) and more on a flooded one. **I deliberately do NOT encode that
   crossover**: it is a projection of future spare mana, i.e. a cast/plan question, which belongs to
   the search.

The Hivepool holds slots 1 and 2 in one card, so it heads this bucket whenever (c) permits.

---

## 5. The total order over a hand

Quotas are **INTERLEAVED, not filled bucket-by-bucket** — Minotaur's shipped lesson: "four lands"
and "two grants" are competing constraints on the same seven-card hand, so the ranking must say which
land beats which Sliver, and read backwards the ladder is the keep tail.

```
land1(coloured) > body1 > VIAL > land2(coloured) > grant1 > body2
    > land3 > body3 > land4 > grant2 > body4 > body5 > ...
```

| adjacency | why |
|---|---|
| **land1 coloured, first** | with zero coloured sources the hand casts nothing but the two artifacts; a missed first land drop is the one thing this deck cannot recover |
| **body1 before the Vial** | a Vial with no Sliver in hand deploys nothing, and a T1 one-drop is real damage |
| **VIAL before land2** | a Vial cast off land1 is on 2 counters by T3 and then deploys 71% of the deck's creatures for free, for the rest of the game. Nothing else in the hand compounds. *(This is where I part from Minotaur V2 — see doubt D1.)* |
| **land2 coloured before grant1** | the second coloured source is what casts Cloudshredder `{R}{W}` at all, and grant1 is frequently Cloudshredder |
| **grant1 before body2** | a unique grant the board lacks beats a redundant body |
| **land4 before grant2** | the second binary grant is usually replicate, which is already live at 4 sources |

**Shed order, most expendable first — and it names EVERY non-staged card in the hand:**

* **S0** — cards that are *structurally dead*, not merely surplus: an **Ancient Ziggurat** while
  another coloured land is available in board+hand (strictly dominated), and an **Aether Vial with no
  generic-capable source** in board+hand (uncastable).
* **S1** — lands past the parent quota, worst first: dominated (`creature_mana_only`) → surplus duals
  → **Mutavault LAST among lands** (§6).
* **S2** — a **second Aether Vial** (learned 2nd-copy −0.3423).
* **S3** — surplus bodies, **reverse of the body keep order**: inert 1-drops (including a demoted
  Cloudshredder) → plain 2-power bodies (including a demoted Hatchery) → additive-grant Slivers.
* **S4** — anything unrecognised. **Empty on this decklist**; kept so that no opinion means "shed
  last among overflow", never "pitch first".
* **S5** — the **keep tail**: the ladder above read backwards, down to land1, the last card this
  policy will ever give up.

**Index 0 is therefore determined for any hand.** Worked examples:

* *7 cards, 2 lands in play, hand = Ziggurat, Cavern, Muscle, Muscle, Galerider, Aether Vial,
  Hivepool.* Quota 4 − 2 board = 2 coloured lands needed; the Ziggurat is not dominated here (it and
  the Cavern are both needed), so S0 is empty. Nothing is past quota. **Index 0 = Galerider Sliver**
  (inert 1-power body, the weakest thing in the hand) — *not* Thrumming Hivepool, which the status quo
  sheds first on MV 6.
* *Same hand but 4 lands in play including a Cavern.* Land quota is met by the board, so both hand
  lands are surplus: **index 0 = Ancient Ziggurat** at S0 (dominated), Cavern next at S1.
* *Vial resolved on 2 counters, board has a Cloudshredder.* Every 2-drop is free (a), and the
  second Cloudshredder in hand is an inert 1/1 (bucket 3 → 4). **Index 0 = Cloudshredder Sliver.**

---

## 6. Mutavault — weighed against the measured heuristic, and NOT contradicted

The recorded measurement (`DecisionProviders.cpp:2041-2071`): a colourless-only manland ranks **60**
in the mana-tap ladder — past even rainbow — so it is tapped only when nothing else can pay, because
keeping it untapped to attack is worth more. Swept on **slivers, 1,800 games, seeds 2002/3003/4004**
via the since-deleted `MTG_MANLAND_RANK`: **rank 5 and rank 30 are each +0.05 turns WORSE** than the
reserve at 60, and identical to each other — so the reserve is the whole effect.

**What I concluded: that measurement says Mutavault's value is as an ATTACKER, not as mana — and
three keep/shed consequences follow from it rather than fight it.**

1. **Mutavault never satisfies the coloured sub-quota.** `produces:["C"]`. A hand whose only lands
   are Mutavaults casts nothing but Aether Vial and Thrumming Hivepool. (Amusingly that *is* a
   functional draw — T1 Mutavault, cast Vial, tick to 2 — but 2 Mutavaults plus five Slivers is dead,
   and the coloured sub-quota of 2 is what prevents the policy from calling it a mana-complete hand.)
2. **Mutavault DOES count toward the parent land quota.** It is still a land drop and still pays
   generic pips — including, uniquely against the colour-restricted half of this manabase, Aether
   Vial's `{1}`, which Ancient Ziggurat cannot pay at all.
3. **Among lands past the quota, Mutavault sheds LAST.** A surplus dual is pure overflow; a surplus
   Mutavault is a 2/2 that **every anthem pumps** — an animated land has all creature types, so it
   matches `subtypes_affected:["Sliver"]` (`SpellEffects.h:2734`, `:3184-3196`) *and* counts for the
   Hivepool's affinity (`ManaPayment.cpp:1362`) — and that the engine deliberately keeps untapped to
   attack. **This is the keep/shed reading of the measured reserve, not a departure from it:** if the
   engine has measured that Mutavault's mana is worth less than its attack, then for keep/shed it
   must be priced as a land drop plus a body, which ranks it above a redundant rainbow land.

Nothing here re-prices the tap order, and nothing here asks the mana solver to spend a Mutavault
earlier.

---

## 7. Evidence

### 7a. The real-play denominator is ZERO — and at d0 the rule is inert BY CONSTRUCTION

Measured with `MTG_SHED_STATS=1` on the committed `build/Release/mtg`, single-threaded, seed 1001:

| cell | games | **real** sheds | **rollout** sheds | low-land (<4 lands) | sheds/cleanup |
|---|---|---|---|---|---|
| **d0** (the suite's *largest* cell) | 1,000 | **0** | **0** | — | — |
| **d3 / b10** (smoke cell size) | 250 | **0** | **4,159** | **100%** | 1.00 |
| **d5 / b20** (shipped `value_play`) | 200 | **0** | **1,806** | **100%** | 1.00 |

The 1,000-game d0 run was also taken with `MTG_TRACE=discard`: **zero real cleanup discards**,
confirming `cleanup-discard-measured.md`'s 400-game census (slivers: 0 events, joint-lowest in the
suite).

Two consequences, and the second is a free gift no other deck's proposal has:

* **The entire effect lives in the ROLLOUT at depth > 0**, exactly as `MTG_SHED_STATS`' own header
  comment anticipates: *"a deck whose keep table mulligans away its land-light hands sheds ~never in
  play, yet the search still sheds constantly inside its rollouts... `real == 0` does NOT mean the
  rule is inert."* Index 0 of this ranking decides all 1,806 of them with no search above it, and
  **100% are taken with fewer than four lands out** — the state where max-MV is least defensible.
* **At d0 the policy cannot change anything.** Zero cleanups of either kind in 1,000 games. So the
  suite's biggest cells carry **no signal**, and a d0 movement would be a **bug, not a result** — a
  correctness check that costs nothing to run.

**Honest sizing: this is a much smaller surface than Minotaur's.** For scale, Minotaur reports
250,265 rollout sheds per 200 games at d5/b40 — **139× this deck.** Minotaur's own adopted model was
worth ≈ −0.00037 t/game. **Predict ≈ 0 here and set the bar at non-inferiority.** Also note the value
leaf roughly halves the rule's reach at shipped depth (9.0 sheds/game at d5/b20 vs 16.6 at d3/b10),
because the O(1) evaluator replaces the horizon rollout that would have done the shedding — expected,
and one more reason not to promise a number.

### 7b. But the status quo is *provably* wrong on this deck — no measurement required

Tier B is descending mana value, and this deck's MV ladder is: **Hivepool 6 > every 2-drop Sliver 2 >
Aether Vial and the 1-drops 1 > every land 0.** So on every hand the generic rule:

1. **sheds Thrumming Hivepool first** — the deck's only double-strike source, and a card affinity
   routinely brings to `{2}`–`{3}`;
2. then sheds a 2-drop Sliver by **hand order** — an arbitrary pick among the anthems, Cloudshredder's
   haste and Hatchery's replicate, i.e. the deck's entire engine;
3. and **keeps every surplus land**, because lands are MV 0 and sort last — in a deck whose creature
   curve tops at MV 2, in a state (100% of these decisions) with fewer than four lands out.

All three are backwards. This follows from the ranking and the card data; it is the case for shipping.

### 7c. The learned second-copy marginals independently reproduce the binary/additive split

`decks/slivers_vial/slivers_vial.profile.json` `card_scores`, second-copy entries:

| card | cost | my classification | learned 2nd-copy marginal |
|---|---|---|---|
| Muscle Sliver | `{1}{G}` | **ADDITIVE** | **+0.0596** |
| Predatory Sliver | `{1}{G}` | **ADDITIVE** | **+0.0100** |
| Sinew Sliver | `{1}{W}` | **ADDITIVE** | −0.0107 |
| Galerider Sliver | `{U}` | INERT | −0.1622 |
| Crystalline Sliver | `{W}{U}` | INERT | −0.1910 |
| **Hatchery Sliver** | `{1}{G}` | **BINARY** (replicate) | **−0.2004** |
| Plated Sliver | `{W}` | INERT (toughness-only) | −0.2329 |
| **Cloudshredder Sliver** | `{R}{W}` | **BINARY** (haste) | **−0.2827** |
| **Aether Vial** | `{1}` | **BINARY** (quota 1) | **−0.3423** — largest negative in the table |

**Every additive card is ≥ −0.011; every binary/inert card is ≤ −0.162. No overlap.** And the
castability confound is held **exactly** constant by a natural control: Muscle, Predatory and Hatchery
are all `{1}{G}` Slivers and *Hatchery has the bigger body* (2/2 vs 1/1) — yet the two additive
anthems read +0.060 / +0.010 and the binary replicate-granter reads **−0.200**, a 0.26-turn gap that
neither cost nor colour can explain.

**Caveats, stated rather than buried.** `card_scores[c][k]` is
`avg_win_turn(k copies in the OPENING HAND) − avg_win_turn(k+1)` — an unadjusted group-mean
difference confounded with castability (Minotaur round 4's units note). And **this profile is stale**:
last written at `a2ba8712`, with **1,281 commits touching `src/` since** — the same defect that
mis-ordered Minotaur round 4. So I use it **only** as corroboration of a mechanism derived
independently from the card text and the engine code, **never as the value order**, and I explicitly
decline a `CSVAL`-style "take the value order from `card_scores`" arm — Minotaur measured that and did
not adopt it.

One thing worth flagging beyond this policy: unlike Minotaur (`hand_score_threshold` = −1e18, scores
nearly inert), **this deck's `hand_score_threshold` is 0.1955 — a live number** — so its stale
`card_scores` *are* live at the mulligan hand-score gate.

---

## 8. State promotions

### Proposed — a cleanup ranking can act on all four

1. **The Vial promotion — a free-cast engine online erases the distance.** A resolved Vial at
   `charge_counters == mv(card)` makes that card cost **0**. The strongest promotion on the deck, and
   it is read directly off `Permanent::charge_counters`.
2. **A binary grant already on board demotes its card to a plain body** — the headline rule. Keyed on
   `grants_haste` / `grants_double_strike` / `grants_replicate_to_subtypes` present on the
   battlefield, so a Hivepool correctly kills a hand Cloudshredder's haste slot.
3. **Affinity shrinks with the board** — Hivepool promotes as Slivers (including an animated
   Mutavault) accumulate.
4. **The colour-deficit class** — a two-pip gold Sliver ranks below a one-pip Sliver at a single
   coloured source, regardless of body size.

### Deliberately rejected as SEARCH-owned

1. **How many replicate copies to pay for, and holding a card as replicate fuel.** A mana-allocation
   decision at cast time; the engine already fans it under human play / `MTG_UNPRUNE=replicate` and
   greedily maxes it at resolution otherwise. Nothing a cleanup ranking can establish.
2. **"Is this turn lethal?"** — every grant promotion could be sharpened by a damage projection.
   Same ruling as Minotaur's Burning-Fist and devotion-reach promotions: search-owned.
3. **The replicate-vs-haste crossover by projected spare mana** — a projection of *future* mana, i.e.
   a plan question.
4. **Whether to animate Mutavault** — already a searched activation (`TurnSolver.cpp:17935`,
   `:36845`); the mana half is the measured tap-order reserve.

---

## 9. The implementation contract (described, not written)

```cpp
// src/ai/DecisionProviders.h  — on VialProvider (KnightsProvider overrides it)
std::vector<int> CleanupDiscardCandidates(
    const GameState&, const std::vector<std::string>* required_pieces) const override;
bool CleanupDiscardShedStable() const override { return false; }   // see below
```

* Returns the **full shed order**, most expendable first, covering **every non-staged hand index**
  (the Minotaur shape, which the brief names as the worked reference — not FiveColour's
  `resize(1)`).
* Routed through `CleanupDiscardRankingWithOrder(s, required_pieces, shed)` so the staged-card and
  required-piece protections stay engine-enforced. **Omission = keep**; nothing is omitted here.
* Gated: `static const bool s_bucket = EnvOn("MTG_VIAL_BUCKET_DISCARD", true);` — `=0` returns
  `GenericProvider::CleanupDiscardCandidates(s, required_pieces)` as the A/B hatch.
* Every characteristic read goes through `CardDatabase::Instance().LookupCached(card)` — a hand card
  is a name-only placeholder. Helpers: `CleanupDiscardManaValue`, `CleanupDiscardIsLand`.
* `required_pieces` is **empty for this deck** (`slivers_vial.profile.json`
  `mulligan.required_pieces: []`), so the protection layer is inert and this ranking alone decides.
* **`CleanupDiscardShedStable() → false`**, declared rather than assumed. The duplicate/redundant
  binary-granter demotion and the Vial quota are **hand-copy-keyed**, and the hand-derived land
  target changes when the shed removes the hand's most expensive creature — so the order is not
  provably prefix-stable. It costs nothing here: **measured `sheds/cleanup = 1.00`** at every depth,
  so the per-shed loop and the batched path do identical work. Verify either way with
  `MTG_DISCARD_SHED_VERIFY=1`.

### Classification predicates, named for mechanical integration

| predicate | bucket / effect |
|---|---|
| `CleanupDiscardIsLand(card)` | MANA |
| `!def.params.produces.empty()` and it contains a non-`Colorless` colour | MANA / **coloured** sub-quota |
| `def.params.creature_mana_only` | MANA — **dominated**, sheds at S0 while another coloured land exists |
| `def.params.can_animate && produces has no colour` | MANA / colourless — **sheds LAST among lands** (also a body) |
| `def.params.upkeep_adds_charge` | **VIAL**, quota 1; board copies net it out; drops to S0 with no generic-capable source |
| `def.params.grants_haste \|\| grants_double_strike \|\| grants_replicate_to_subtypes` | **UNIQUE GRANT**, 1 per distinct grant — demoted to BODIES if that grant is on board or a copy is ahead of it in hand |
| `def.params.power_bonus > 0 && !def.params.subtypes_affected.empty()` | BODIES, **top tier** (additive) |
| `def.params.attack_trigger_life_loss > 0` | BODIES, **top tier** (additive, grouped with the anthems) |
| `def.params.tough_bonus > 0 && def.params.power_bonus == 0` | **no grant credit** (toughness inert vs a non-blocking opponent); still a body |
| `def.params.affinity_for_subtype` | effective cost `= max(0, mv − matching permanents)`, counting `is_animated` |
| `def.card.IsCreature()` and nothing above | BODIES, by base power then distance then MV desc |

### Validation the central integration should run

1. **`MTG_VIAL_BUCKET_DISCARD=0` must be byte-identical to today**, and **every d0 cell must be
   byte-identical with it ON** — d0 has zero cleanups (§7a), so any d0 movement is a defect.
2. **`MTG_TRACE=discard` + `MTG_SHED_STATS=1`** to confirm the rollout counts above reproduce.
3. **A behavioural diff** (`test/tools/discard_behaviour_diff.py`) — cheap, and on Minotaur the cheap
   half produced everything the expensive half did. Expected signature: **stops shedding Thrumming
   Hivepool and 2-drop Slivers; starts shedding Ancient Ziggurat, surplus duals, second Vials and
   inert 1-drops.** If it does not show that, the rule is not firing.
4. Then smoke + regression through the accept flow, judged on **non-inferiority**, with the signal (if
   any) in the d3/d5 cells only.

---

## 10. Doubts, flagged — the user reviews and amends these

**D1 — Where does the Vial sit in the ladder? I kept it above land2; Minotaur's V2 moved it down to
shed with the mana.** Minotaur's argument was *"a Vial is a turn-1 play; drawn into the turns this
deck discards it needs several turns of ticking before it deploys anything."* **That argument is
materially weaker here, and the reason is a number:** this deck's `vial_target_mv` is **2**, Minotaur's
and Knights' is 3. A Vial cast on turn 3 is live on turn 5 — two ticks — and **25 of 35 creatures
(71%) sit at exactly that MV**. On Knights the Minotaur reasoning is much stronger. I kept it
protected; this is the single most reviewable placement in the ladder, and Minotaur's own correction
found the Vial change *"barely measurable in either direction"* (t fell from 2.31 to 1.94 at 4× the
sample), so the honest expectation is that it barely matters here either.

**D2 — Is the coloured sub-quota 2 or 1?** Two is justified by the three gold two-pip Slivers
(Cloudshredder ×3, Crystalline ×3, Hibernation ×1 = **7 of 60 cards**), one of which is
Cloudshredder. But the other **28** creatures need only one coloured source and the remaining 25
cards need none at all, so a rigid 2 could protect a second dual ahead of a castable Sliver — the exact defect Minotaur's `MTG_TRACE`
probe caught in a bucket-at-a-time fill. I mitigated it by *interleaving* (land2 sits behind the Vial
and ahead of grant1, not inside a land block), but the number itself is a judgment call.

**D3 — The anthem-vs-Leeching order inside the additive tier is nearly a tie.** I ranked anthems
first on two small card facts (doubled by double strike; self-inclusive 2/2). Both are conditional.
This tier could equally be declared a flat tie broken by hand order, and the difference would
probably not be measurable.

**D4 — The learned marginal disagrees with me about a *second* Hatchery Sliver.** My rule demotes it
to "a plain 2/2 for `{1}{G}`", i.e. as good as Crystalline Sliver. The learned 2nd-copy marginal is
**−0.2004**, worse than that implies. It may be the castability confound, or it may be real (e.g. the
first Hatchery's replicate already consumes the spare mana a second body would need). I did not
resolve it. **Do NOT resolve it by reaching for `card_scores`** — the profile is 1,281 `src` commits
stale, and Minotaur round 4's ruling was *regenerate first, then measure, and only if a cheap read
suggests there is anything there.*

**D5 — Is the land target's `+2` right?** It is the one authored constant in the policy and it encodes
"two spells a turn". Nothing in the card data pins it. It is also the constant that will be inherited
by any future Vial deck, so it is worth a deliberate ruling rather than a default.

**D6 — Should the Vial-tick softening (`counters + 1 == mv` → cost 1) exist at all?** It is a
one-turn lookahead into `WantVialCharge`'s decision, which is itself a heuristic. Dropping it is
strictly simpler and loses a small amount of accuracy on the most common board in the deck.

**D7 — I did not encode Secluded Courtyard's `colored_creature_ability_ok` advantage** over the other
three duals, judging it near-inert on this list (the only creature-source activations are Mutavault's
animate, which is chicken-and-egg, and Sliver Hive's token, whose source is a land). If it is not
inert, the Courtyard should rank above the other duals in the S1 surplus order.

**D8 — A doc inaccuracy found in passing, not a bug.** `SpellEffects.h:3184` names *"Predatory
Sliver"* as the `scales_per_matching` example, but Predatory Sliver's `cards.json` params are flat
`power_bonus:1, tough_bonus:1` with **no** `scales_per_matching` — which matches its real oracle text
("Sliver creatures you control get +1/+1"). The behaviour is right; the comment's example is stale.
My classification follows the data, not the comment.

**D9 — Out of scope but adjacent, and worth a look while someone is here.** The mulligan profile has
`min_color_sources: {}` — **empty** — yet §2 shows a hand whose only lands are Mutavaults can cast
nothing but the two artifacts, and a hand with one coloured source cannot cast any of the three gold
Slivers. That is the same *shape* as Minotaur's KAROO CAVEAT (which was worth a whole game outright),
and it is a **mulligan** question, entirely independent of this policy. I have not measured it and I
am not proposing it here; I am flagging it so it is not lost.
