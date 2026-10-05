# Knights — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored to `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 per-deck sweep.
Deliverable is this document; no `.cpp`/`.h` was touched. Every card characteristic below was read
from `src/cards/data/cards.json` at authoring time (Rule 0) — nothing here is recalled.

Target: `KnightsProvider::CleanupDiscardCandidates`, behind a default-on
`EnvOn("MTG_KNIGHTS_BUCKET_DISCARD", true)`.

---

## 0. THIS MUST BE AN OVERRIDE ON `KnightsProvider`, NOT ON `VialProvider`

`KnightsProvider : public VialProvider` (`src/ai/DecisionProviders.h:445`). A separate agent is
authoring a policy for `slivers_vial`, which routes to `g_vial` — i.e. to `VialProvider` itself
(`SelectDecisionProvider`, `if (vial) { return g_vial; }`). **If the slivers policy lands as
`VialProvider::CleanupDiscardCandidates` and Knights ships no override, Knights silently inherits
a sliver bucket list.** That is precisely the misroute class this repo keeps paying for — the same
class `provider_audit.py --check` exists to catch ("inheriting another deck's answer", the comment
at `DecisionProviders.cpp:10200`) and the class the routing comment at `:9879` already guards for
the *routing* half but cannot guard for the *inheritance* half.

So: declare and define `CleanupDiscardCandidates` **on `KnightsProvider`**. The flag name is
`MTG_KNIGHTS_BUCKET_DISCARD`, distinct from whatever the Vial/slivers policy uses, and `=0` must
fall through to `GenericProvider::CleanupDiscardCandidates` (the shared tier-B fallback) — **not**
to `VialProvider`'s, so the A/B hatch measures this policy against the generic rule rather than
against another deck's.

The signature is deck-exclusive today, checked rather than assumed: exactly **6** cards in
`cards.json` carry the routing signature (`subtypes_affected` containing `"Knight"`,
`cast_trigger_subtype == "Knight"`, or `etb_dig_subtypes` containing `"Knight"`) — Worthy Knight,
Acclaimed Contender, Knight Exemplar, Inspiring Veteran, Marshal of Zhalfir, Haytham Kenway — and of
every decklist under `decks/`, only `Knights.cod` contains any of them. So this override cannot leak
onto a neighbouring deck.

---

## 1. The deck, in one paragraph — and therefore its shape

Knights is a 60-card mono-white-plus-splash Knight tribal aggro deck that wins by going wide and
then multiplying: 17 body-copies, **19 anthem-copies** (five different lords), 20 lands and 4 Aether
Vial. Its damage is close to literally multiplicative — `total ≈ Σ(base power) + bodies × anthem
points` — because every one of its lords is *itself* a Knight, so lords pump each other and a body
and an anthem each raise the other's marginal value. It needs three things to function: **white mana
(3–4 sources; the curve tops at mana value 4 and there is no mana sink in the deck — Marshal of
Zhalfir's tap ability is documented inert and there are no X spells or equip costs)**, **bodies for
the anthems to multiply**, and **anthems to multiply them**. On top of that sits one genuinely
separate engine piece, **Aether Vial**, which is neither mana nor a threat: it produces no mana and
cannot pay a single pip, yet it deploys Knights for free by matching charge counters to mana value —
and it is the only route in the deck that ignores colour. Finally, 11 of the 36 spells carry an
off-white pip ({U} on Marshal of Zhalfir and Haytham Kenway, {R} on Inspiring Veteran) that **8 of
the 20 lands cannot produce**, which makes colour, not quantity, the binding mana constraint.

**Shape: aggro, but not the brief's "simple 2-bucket" aggro.** Four buckets, and each one is forced
by something the deck needs and the others cannot supply:

| # | bucket | why it is not merged into another |
|---|---|---|
| 1 | **MANA (lands)**, sub-split by **colour role** | the deck's real mana sub-split. There are **no dorks and no rocks**, so the brief's lands-vs-acceleration split is vacuous here; what replaces it is mono-{W} vs multi-colour fixer, because 11 spells are uncastable off Plains alone |
| 2 | **THE VIAL ENGINE** | produces no mana (cannot fill the land quota) and has 0 power (is not a threat). Its currency is charge counters, and it is the deck's only colour-free deployment route |
| 3 | **ANTHEMS** | value scales with the number of bodies |
| 4 | **BODIES** | value scales with the anthem total |

Buckets 3 and 4 are split for the same reason the brief splits a combo's parts: they are a
**multiplicative pair**, and a product is maximised by keeping some of each. A hand of five anthems
is not broken (they pump each other) but it is slow; a hand of five 2/1s caps out around 16 power.
Neither is "the threats bucket" and collapsing them loses exactly the decision this policy exists to
make. Sub-splits *inside* those two buckets are value order, not quotas (see §4).

---

## 2. Card-by-card role table

Every main-deck card. `params` cited are the fields the classification keys on. Bracket notes (the
engine's own admissions of incomplete modelling) are carried where they change a rank.

| card | cost | MV | ×  | bucket / role | keyed on (`params` unless noted) |
|---|---|---|---|---|---|
| Plains | — | 0 | 8 | MANA — **mono-{W}** | `CleanupDiscardIsLand`; `produces = [W]` (size 1) |
| Unclaimed Territory | — | 0 | 4 | MANA — **fixer** | `produces = [W,U,B,R,G,C]` (size > 1 ⇒ a `wild` tap); `colored_creature_only` |
| Secluded Courtyard | — | 0 | 4 | MANA — **fixer** | same; + `colored_creature_ability_ok` |
| Tournament Grounds | — | 0 | 4 | MANA — **fixer** | `produces = [W,R,B]` (size > 1 ⇒ `wild`). **See D7** |
| Aether Vial | {1} | 1 | 4 | **VIAL ENGINE**, quota 1 | `upkeep_adds_charge` |
| Haytham Kenway | {2}{W}{U} | 4 | 3 | ANTHEM — **+2/+2 Knights**; LEGENDARY | `power_bonus = 2`, `subtypes_affected = [Knight]`, `lord_excludes_self`; `Supertype::Legendary` |
| Benalish Marshal | {W}{W}{W} | 3 | 4 | ANTHEM — **+1/+1 ALL creatures** | `affects_all_creatures`, `power_bonus = 1` |
| Knight Exemplar | {1}{W}{W} | 3 | 4 | ANTHEM — +1/+1 Knights | `subtypes_affected = [Knight]`, `power_bonus = 1` |
| Inspiring Veteran | {R}{W} | 2 | 4 | ANTHEM — +1/+1 Knights, **{R} pip** | same |
| Marshal of Zhalfir | {W}{U} | 2 | 4 | ANTHEM — +1/+1 Knights, **{U} pip** | same |
| Adeline, Resplendent Cathar | {1}{W}{W} | 3 | 1 | BODY — **count-scaler**; LEGENDARY | `power_equals_creature_count`, `attack_creates_tokens = 1` |
| Worthy Knight | {1}{W} | 2 | 4 | BODY — **body multiplier** | `cast_trigger_creates_tokens = 1`, `cast_trigger_subtype = "Knight"` |
| Acclaimed Contender | {2}{W} | 3 | 4 | BODY — **gated digger** (3/3, the biggest base body) | `etb_dig_count = 5`, `etb_dig_subtypes = [Knight]`, `etb_dig_requires_subtypes = [Knight]` |
| Venerable Knight | {W} | 1 | 4 | BODY — plain 2/1 | `IsCreature`, no role params |
| Dauntless Bodyguard | {W} | 1 | 4 | BODY — plain 2/1 | `IsCreature`, no role params |

Counts check: 20 lands + 4 Vial + 19 anthem + 17 body = 60.

### Bracket notes that matter here

* **Tournament Grounds** — *"the engine treats any multi-color source as wild, so it can also pay
  U/G — a known dual-land approximation that slightly over-fixes the deck's blue."* Verified against
  `ManaPool::wild` ("one tap of a multi-color land (satisfies any single color or generic)"). So in
  simulation the deck has **12 blue sources, not 8**. The colour term below is written against the
  engine's semantics because that is what decides castability in the measurement — but see **D7**.
* **Venerable Knight** — its dies trigger is *not modelled at all* (`template: vanilla_creature`,
  `params: {}`). Two consequences: it is **engine-identical to Dauntless Bodyguard**, and the deck
  contains **no dies-watcher and no ETB-watcher in params at all**, which is what makes the
  redundant-legend shed in §5 genuinely dead (see §5 S1).
* **Worthy Knight** — the "three tokens → exchange for a 2/2 Knight" clause is not modelled, a mild
  under-count of its late value. Also note its tokens are **Human**, not Knight: the Knight-only
  lords do not pump them and they do not satisfy Acclaimed Contender's dig gate. Only Benalish
  Marshal pumps them.
* **Dauntless Bodyguard / Knight Exemplar / Haytham / Marshal of Zhalfir** — indestructible, first
  strike, protection, the ETB exile and the tap ability are all documented inert in goldfishing. The
  lord halves and the legend rule are modelled.
* **Unclaimed Territory / Secluded Courtyard** — the ETB type choice and the colour restriction are
  not modelled; they read as all-five-colour producers with `colored_creature_only`. Every coloured
  pip in this deck is on a creature spell, and Aether Vial's {1} is generic (payable by their {C}),
  so `colored_creature_only` is **non-binding for this decklist**.

### Genuinely ambiguous roles, flagged

* **Aether Vial** — mana-adjacent but not mana. Resolved in §1: its own bucket.
* **Acclaimed Contender** — body vs card-flow. Placed in BODIES (it is the deck's biggest base body)
  with a board-gated promotion, because its dig is *conditional* and the condition is exactly what
  the board census can answer.
* **Haytham Kenway** — simultaneously the deck's biggest anthem, a 3/3 body, and a legend-rule
  liability. All three are expressed: anthem rank 1, and the redundant-legend band at the very front
  of the shed order.
* **Worthy Knight** — body vs engine. Placed in BODIES; its engine half sets its value rank.

---

## 3. The buckets, with quotas — every one net of board

**Census the battlefield before computing a single quota** (brief rule 2). For this deck the census
needs: our lands (split mono / multi-colour), our resolved Vials **and their charge counts and
tapped state**, our resolved anthems (and their `power_bonus`), our creature count, whether any of
our creatures is a **Knight** (Contender's gate), and the **names** of our legendary permanents.

### Bucket 1 — MANA (lands). Quota **4**, net of board.

Counts lands only. **A Vial does not count** — it produces no mana and can pay no pip. There are no
rocks or dorks in the pool, so `sources == lands` and the two are not tracked separately (contrast
`MinotaurProvider`, which must, for its Karoo test).

Why 4: the curve tops at MV 4 (Haytham), Benalish Marshal wants {W}{W}{W} at three, there is no mana
sink, and the deck's **own generated mulligan profile sets `max_lands: 4`** — an opening hand with
five lands is a mulligan by its own artifact. GT says it wins on turn **4.28–4.56** (d3/d5, every
tier), so it makes at most ~4 land drops in a won game. A fifth land is overflow. There are **no
Karoos**, so the Minotaur caveat does not apply and no land in this deck is ever a blank.

**Colour sub-split (the deck's real mana sub-split):**

* **fixer** = `def->params.produces.size() >= 2` — Unclaimed Territory, Secluded Courtyard,
  Tournament Grounds. A multi-colour tap is a `wild` unit that satisfies any single pip, so these
  are the *only* source of {U} and {R}.
* **mono** = `produces.size() == 1` — Plains.
* **Sub-quota: at least 1 fixer among the kept lands**, live only while (a) the keep-set holds a
  card with an off-{W} pip, (b) the board has no fixer, and (c) no resolved Vial can still reach
  that card (§4's `d_vial`).
* **Fungible upward** (brief rule 3): the sub-quota is a *preference inside* the parent quota of 4.
  A hand with no fixer fills all four slots with Plains; the sub-quota never manufactures a keep.
* **Among surplus lands, shed mono first.** A fixer strictly dominates a Plains in the engine (its
  `wild` pays any pip; Unclaimed Territory and Secluded Courtyard additionally make {C}), and
  nothing in the deck prefers a Plains. The deck's own learned `card_scores` agree independently and
  per copy: Secluded Courtyard +0.171/+0.031, Unclaimed Territory +0.131/+0.119, Tournament Grounds
  +0.036/+0.082 — all positive — against **Plains −0.002 / −0.083 / −0.122**, monotonically worse.

### Bucket 2 — THE VIAL ENGINE. Quota **1**, net of board (so **0** if any Vial is resolved).

Filled from the battlefield by any permanent with `upkeep_adds_charge`. Four independent reasons the
second copy is overflow, not insurance:

1. **Charge counters do not pool.** A second Vial enters at 0 and needs `mv` upkeeps to matter, in a
   deck that wins on turn ~4.4. It is not a spare; it is a restart.
2. **Both Vials climb to the same level.** `WantVialCharge` (`src/core/SpellEffects.h:11506`) reads
   the *shared hand* — `count_at` / `count_next` / `count_above` are hand-wide — so two Vials make
   the same decision every upkeep and their deploys duplicate each other.
3. **The learned marginal, on two independent decks.** Knights' own `card_scores` put the second
   Aether Vial at **−0.3635**; `slivers_vial` independently learned **−0.3423**
   (`docs/design/better-mulligan-model.md`, which calls a 4-Vial hand "a hand a human instantly
   mulligans").
4. **Precedent.** `MinotaurProvider`'s shipped policy caps Aether Vial at 1 for exactly this reason
   ("a second copy is close to dead once the first is online"). *(The task brief attributed that cap
   to the Equipment deck's policy; `EquipmentProvider` in fact never mentions the Vial, and no
   Equipment decklist runs one — Minotaur is the real precedent.)*

### Bucket 3 — ANTHEMS. Quota **3**, net of board.

Predicate: **`IsLordPermanent(def)`** — the engine's own helper (`src/core/SpellEffects.h:3049`),
which is `tmpl == LordEffect`, or a creature with `(power_bonus != 0 || tough_bonus != 0)` **and**
`(!subtypes_affected.empty() || affects_all_creatures)`. Reuse it rather than re-deriving: it already
handles the dual-role case and is the same test the P/T layer uses. `power_bonus` defaults to **0**
(`src/cards/CardDatabase.h:36`), so this predicate has **no DragonsProvider-style default-value
trap** — the field must be set for a card to classify. *(That trap is worth naming: reading
`reduces_spell_subtype_amount > 0` classified seven of eight cards in a measured hand as cost
reducers because that field defaults to 1. `power_bonus` was checked against its default before this
predicate was written.)*

Board fills the quota: a resolved anthem is anthem points we already have. `anthem_points` = sum of
`power_bonus` over resolved lords that pump our creatures (the two spellings — `affects_all_creatures`
and `subtypes_affected` containing a subtype we control — both count).

### Bucket 4 — BODIES. Quota **4**, net of board.

Residual: `def->card.IsCreature() && !IsLordPermanent(def)`. Board fills it with **our creature
count, tokens included** — a Human token is a body Benalish Marshal pumps and a body Adeline counts.
(That tokens are *not* pumped by the Knight-only lords is a value-order nuance, not a quota one.)

### The quota sum is deliberately larger than a hand

4 + 1 + 3 + 4 = 12 slots for a 7-card hand, the same shape `MinotaurProvider` ships (11 rungs).
So in practice **the ladder in §5 *is* the ranking**, and "overflow" only ever names a redundant
legend, a second Vial, a fifth land, or a threat past its bucket's last slot. This is stated plainly
because it is the honest reading of "quota-first" for a deck that barely sheds: what the quotas
really buy is *the order the protected cards give way in*, which the Minotaur integration found is
the part that decides real hands.

---

## 4. Within-bucket order, and the distance-to-playable term

### 4a. The Vial's charge count IS the distance function — and it is asymmetric

This is the deck's one genuinely unusual mechanic and it has real teeth. Established from code, not
recalled:

* The deploy requires **exact** equality: `if (vp.charge_counters != mv) { continue; }`
  (`AIEngine.cpp:3532`, and the rollout's twin `TurnSolver.cpp:25386`).
* **The counter only ever increases.** `WantVialCharge` returns whether to *add* one; nothing
  removes one. Its own comment: *"a creature whose MV is BELOW c can no longer be deployed by this
  Vial and is irrelevant."*
* A Vial deploy **taps** the Vial: one creature per Vial per turn.
* **A Vial put is not a cast.** `AIEngine.cpp`: *"Vial is not a cast so no on-cast trigger fires."*
  ETB watchers, Acclaimed Contender's `etb_dig` and `EnforceLegendRule` all *do* fire on a put.

Therefore, with `c` = the largest charge count among our **untapped** resolved Vials (−1 if none):

```
d_vial(i) = (c >= 0 && mv(i) >= c) ? (mv(i) - c) : INF        // NOTE: mv >= c, not max(0, mv - c)
```

Three consequences, each a rule:

1. **`mv(i) == c` ⇒ the card is FREE this main phase, colour irrelevant.** Distance 0 — the
   strongest playability signal available in this deck, and it is the *only* thing that makes
   Marshal of Zhalfir ({W}{U}) and Inspiring Veteran ({R}{W}) deployable with zero blue or red on
   board. A Vial at 4 does the same for Haytham. **Never shed a Vial-exact card while a costed card
   of the same role is available.**
2. **`mv(i) < c` ⇒ the Vial route is permanently CLOSED for that card.** It can only be hard-cast.
   This inverts the naive intuition that cheap is always closer: once the Vial climbs to 3, a
   1-drop has *lost* a route a 3-drop still has. Writing `max(0, mv - c)` here would be wrong.
3. **The Vial substitutes for colour, and the substitution expires.** The fixer sub-quota in §3
   relaxes while a resolved Vial can still reach the off-colour card (`c <= mv`), and **snaps back**
   once the Vial has climbed past it — a Vial at 3 no longer helps Marshal of Zhalfir at all, so a
   fixer becomes necessary again. That is the net-of-board term with the sharpest teeth in this
   deck, and it is entirely board-readable.

### 4b. Hard-cast distance

```
reach      = our lands on battlefield + lands in hand          // no rocks/dorks; the Vial is not mana
wild_reach = multi-colour sources (board + hand)               // produces.size() >= 2
off_pip(i) = the cost carries a non-{W} coloured pip           // at most one on any card in this deck
d_hard(i)  = max(0, mv(i) - reach) + (off_pip(i) && wild_reach == 0 ? 3 : 0)
dist(i)    = min(d_hard(i), d_vial(i))
```

**The off-colour penalty is +3, not INF, on purpose.** A fixer is 12 of 20 lands, so a blocked card
is roughly one draw step from live, and the Vial can unblock it outright. INF would shed the
off-colour lord out of every mono-Plains hand — which is the max-mana-value mistake in a new
costume, and the Minotaur arc measured that direction losing (its round-1 "playability first" arms
were all worse, monotonically in how hard playability was weighted).

**Use it as a bounded binary far-flag in V1, not as a continuous sort key:** a threat with
`dist >= 2` ranks below every threat with `dist <= 1` *within its own bucket*. Two turns is most of
this deck's game (it wins turn ~4.4). Do **not** ship the continuous form first — see §7.

### 4c. Anthem value order, best kept first (⇒ shed last)

By anthem points, then breadth, then ascending mana value. Colour is deliberately **not** baked in;
§4b handles it conditionally, which is the whole point.

1. **Haytham Kenway** — 2 points, double every other anthem in the deck. (Demoted by the far-flag at
   MV 4 / no wild; sent to the front of the *shed* order when a Haytham is already resolved.)
2. **Benalish Marshal** — 1 point but the **broadest**: `affects_all_creatures`, so it is the only
   anthem that pumps Worthy Knight's and Adeline's Human tokens, of which this deck makes many. All
   pips white, so castable off any three lands.
3. **Inspiring Veteran** — 1 point, Knights only, **MV 2**.
4. **Marshal of Zhalfir** — 1 point, Knights only, MV 2. Engine-identical to (3); see **D4**.
5. **Knight Exemplar** — 1 point, Knights only, **MV 3**: one mana more for the same effect as
   (3)/(4), and the far-flag is what should decide between them rather than a baked-in order.

**No duplicate penalty on anthems.** Every one is also a Knight body, they stack, and they pump each
other; a second Knight Exemplar is a real 2/2 and a real +1/+1. This is the opposite of Minotaur's
cumulative-mana `DUPES` rule and the difference is structural, not a tuning choice: Minotaur's
duplicates were 5-mana, these are 2–3.

### 4d. Body value order, best kept first

1. **Adeline, Resplendent Cathar** — `power_equals_creature_count` plus a tapped-and-attacking body
   every attack (which then raises her own power). In a 36-creature go-wide deck she is the single
   largest damage source. A 1-of, so a duplicate can never arise from this list.
2. **Worthy Knight** — turns every subsequent Knight **cast** into a body; earliest is best. No
   duplicate penalty: two copies mean two triggers per cast.
3. **Acclaimed Contender** — 3/3 (biggest base body) plus, when the gate holds, a Knight into hand.
   **Gate promotion:** with another Knight already on the battlefield, promote **above Worthy
   Knight** — a 3/3 plus a card for three mana. With no Knight on board, leave it here: Worthy
   Knight cast first satisfies the gate, and the engine's own `VialProvider::CastOrderRank` already
   sequences it that way (rank 8 watcher, rank 9 gate-met digger, rank 12 gate-unmet digger). Read
   the gate with the *same* battlefield scan that code uses, so the two cannot disagree.
4. **Venerable Knight / Dauntless Bodyguard** — 2/1 for {W}, engine-identical (**D5**). The cheapest
   way to add a body for the anthems and for Adeline, so **no duplicate penalty**; hand index breaks
   the tie.

---

## 5. The total order over a hand — index 0 is determined for any hand

### The keep ladder (each slot skipped when the board already fills it)

```
L1 > B1 > L2 > V1 > A1 > B2 > L3 > B3 > A2 > L4 > B4 > A3 > (unprotected remainder)
```

`L` = land (fixer preferred while the colour sub-quota is live), `B` = body, `A` = anthem,
`V` = Vial. Read forwards it is the quota fill; **read backwards it is the order the protected cards
give way in**, which is what actually decides an 8-card hand (the Minotaur integration found a
bucket-at-a-time fill got exactly this tail wrong, protecting a fifth land ahead of a castable
body).

Why this interleave: `L1` first because with no mana nothing happens; `B1` before `L2` because a
turn-1 body is the deck's best opening and an anthem with nothing to pump is only a 2/2; `V1` after
two lands because the Vial pays off from turn 3 onward; `A1` before `B2` because the *first* anthem
multiplies every future body. Bodies then take 4 of the 7 threat slots and anthems 3 — roughly
deck-proportional (17 bodies vs 19 anthems) with a mild body tilt, because a body under one anthem
gains +2–3 power while an anthem over one body gains +1.

**Two mirror-image promotions decide the contested `A1`/`B2` pair, both from the board census only:**

* **Anthem saturation → swap `A1` and `B2`.** With `anthem_points >= 2` on board and `<= 1` of our
  creatures being pumped, a third anthem is +1 power while a body is +3.
* **Body multiplier online → swap `A1` and `B2`.** With a resolved `cast_trigger_creates_tokens`
  (Worthy Knight) or `power_equals_creature_count` (Adeline) permanent, a kept body is worth more
  than its own stats: a Knight cast mints a Human, and every body raises Adeline's power.

### The shed order (most expendable first — what the hook returns)

**S1. Redundant legend.** `def->card.HasSupertype(Supertype::Legendary)` **and** a permanent we
control shares its name. This card is **dead, not merely worse**, and the engine says so itself:
`OfferDuplicateLegendCast` prunes the cast for a `LordEffect`/`vanilla_creature` card unless
`DuplicateEntryOrDeathHasUpside` vetoes — and that veto enumerates only ETB-watchers and
dies-watchers, **of which this deck has none in params** (Venerable Knight's dies trigger is
unmodelled). A Vial put is no escape either: `deploy_via_vial` runs `EnforceLegendRule` immediately.
Only Haytham Kenway can reach this state from this list; measured at **4 legend-duplicate events per
600 d0 Knights games** (`DecisionProvider.h:1884`), so the state is real and rare.

**S2. Surplus Aether Vial** — every copy beyond one, counting the battlefield first (§3, bucket 2).
Ahead of surplus land because a resolved Vial makes a held Vial *dead*, a strictly stronger claim
than "the fifth land is surplus".

**S3. Surplus land** beyond 4 net of board: **mono-{W} first**, then a fixer whose colour role the
board already covers.

**S4. Surplus threats** — bodies and anthems past their bucket's last ladder slot, worst first by
(far-flag, bucket value rank, hand index).

**S5. The ladder read backwards** — the quota-protected cards, least-protected first.

**Every card in hand is named.** Each lands in exactly one bucket; each bucket's order is a strict
total order (value rank → far-flag → hand index); S1–S5 partition the hand. Nothing falls through to
`CleanupDiscardRankingWithOrder`'s tier B — descending mana value — which on a payoff deck is
backwards and is the ranking this override exists to overturn (the Mirrorwing gi295 lesson).

### Two worked hands, both diverging from max-mana-value

**A — the Vial case.** Board: Plains, Plains, Unclaimed Territory, **Aether Vial at 2 counters,
untapped**. Hand (8): Plains, Plains, Venerable Knight, Venerable Knight, Marshal of Zhalfir,
Inspiring Veteran, Acclaimed Contender, Aether Vial.

* Tier B sheds **Acclaimed Contender** (MV 3, the maximum).
* This policy: a Vial is resolved ⇒ the held Vial is surplus ⇒ **index 0 = Aether Vial**. Index 1 is
  the surplus Plains (board 3 lands, quota 4, hand owes 1 of its 2). And the Vial at 2 makes *both*
  MV-2 anthems `dist = 0` — which matters because with no {U} or {R} source on board they are
  otherwise uncastable, so max-mana-value was about to shed the 3/3 digger while keeping a dead Vial.

**B — the colour case.** Board: 2 Plains. Hand (8): Plains, Venerable Knight, Dauntless Bodyguard,
Knight Exemplar, Benalish Marshal, Marshal of Zhalfir, Haytham Kenway, Aether Vial.

* `reach` = 3, `wild_reach` = 0 ⇒ both {U} cards are far-flagged (Marshal of Zhalfir `dist` 3,
  Haytham 4).
* Anthems, best kept first, after the far-flag demotion: Benalish Marshal, Knight Exemplar, then the
  flagged pair in value order — Haytham, **Marshal of Zhalfir**.
* **Index 0 = Marshal of Zhalfir.** Tier B would shed **Haytham** (MV 4). Keeping Haytham is right:
  both are uncastable right now, Haytham's payoff is double, and a single fixer draw at four lands
  turns it on.

---

## 6. Param-level classification predicates (integration should be mechanical)

All via `CardDatabase::Instance().LookupCached(card)` — a hand card is a name-only placeholder
(`DeckLoader::MakePlaceholder`), so **every** characteristic read must go through the definition.

| predicate | bucket / effect |
|---|---|
| `CleanupDiscardIsLand(card)` | → MANA |
| `d->params.produces.size() >= 2` | → MANA/**fixer** (a `wild` tap pays any single pip) |
| `d->params.produces.size() == 1` | → MANA/**mono**; shed first among surplus lands |
| `d->params.upkeep_adds_charge` | → VIAL (same predicate `MinotaurProvider` uses) |
| `IsLordPermanent(*d)` (`SpellEffects.h`) | → ANTHEM; points = `d->params.power_bonus` |
| `d->card.IsCreature() && !IsLordPermanent(*d)` | → BODY |
| `d->params.power_equals_creature_count` | body value rank 1 (Adeline); also fires the body-multiplier promotion |
| `d->params.cast_trigger_creates_tokens > 0 && !d->params.cast_trigger_subtype.empty()` | body value rank 2 (Worthy Knight); also fires the body-multiplier promotion. Same test as `CastOrderRank` rank 8 |
| `d->params.etb_dig_count > 0 && !d->params.etb_dig_requires_subtypes.empty()` | gated digger (Contender); gate = the battlefield scan `CastOrderRank` uses for ranks 9/12 |
| `d->params.affects_all_creatures` | breadth tie-break among equal-point anthems (Benalish Marshal) |
| `d->card.HasSupertype(Supertype::Legendary)` + same-name battlefield scan | **S1 redundant legend** |
| `p.charge_counters` / `p.tapped` on an `upkeep_adds_charge` permanent | the `d_vial` distance and the colour relaxation |

**Card names are used for nothing.** The value orders in §4c/§4d are fully derivable from the params
above plus mana value, breadth and power — deliberately, so a screening arm that swaps a card keeps
the right bucket. The only places names appear are the two engine-identical pairs (**D4**, **D5**),
where hand index, not a name, breaks the tie.

---

## 7. State promotions — proposed, and the ones deliberately rejected

### Proposed (all board-readable; none needs a projection)

1. **Redundant legend → shed first** (S1). Board-readable; the engine already refuses the cast.
2. **Vial-exact protection.** `mv == c` on an untapped resolved Vial ⇒ the card costs nothing this
   main phase; it is the cheapest card in the hand in real terms and must not be shed while a costed
   card of the same role exists.
3. **Vial-closed demotion.** `mv < c` ⇒ the Vial route is gone; the card keeps only its hard-cast
   distance and earns no Vial protection. Stated explicitly so the rule is not accidentally generous
   (the arithmetic trap in §4a.2).
4. **Colour relaxation, and its expiry.** The fixer sub-quota is satisfied by a board fixer, and
   relaxed while a resolved Vial can still reach the off-colour card (`c <= mv`) — and restored once
   the Vial climbs past it.
5. **Contender gate promotion** (§4d.3) — a Knight on board ⇒ promote above Worthy Knight.
6. **The two mirror saturation promotions** on the `A1`/`B2` pair (§5).

### Rejected as search-owned — and each looks like it belongs here, which is why it is listed

* **"Hard-cast this Knight instead of Vialling it, to collect Worthy Knight's token."** A real and
  non-obvious anti-synergy — a Vial put fires no cast trigger, so every Vialled Knight forfeits a
  1/1 Human — but it is a **cast-route** decision between two lines. The search owns it. It is
  recorded here because it slightly lowers the Vial's value in this deck specifically, which is part
  of the D1 argument.
* **"Shed the card that would strand the Vial at the wrong charge count."** Steering the charge
  trajectory is `WantVialCharge` / `AIEngine::DecideVialCharge`'s job, and it is already a searched
  axis with its own flag (`MTG_SEARCHED_VIAL`, opt-in since 3efbe969). The discard must not try to
  drive it. Note the coupling is real and one-directional — the shed changes `count_at`/`count_next`
  and therefore the next upkeep's charge decision — so promotion 2 is deliberately the *only* place
  this policy touches the Vial's trajectory: protect what is free **now**, never speculate about
  what the counter should become.
* **"Keep the card that makes this turn lethal."** Damage projection ⇒ search.
* **"Adeline is out, so count the exact damage a body adds."** The *quota* form (a body is worth more
  when a count-scaler is resolved) is board-readable and shipped as promotion 6. The sharpened form
  — is it lethal — is a projection and is not.
* **The continuous EV form, `P(play) × value`.** Not rejected on merit; **deferred on ordering.** It
  is the measured-better *shape* on Minotaur (−0.00037 t/game at d3, negative across three
  independent seed blocks) but only after two failures that are directly instructive here: the
  lexicographic "playability first" arms were monotonically worse, and plain EV degenerated because
  its `value` term had ties. Ship §4b's bounded far-flag as V1 so the first A/B is attributable,
  then measure the EV form as the named follow-up arm with `value` taken from the **full** order in
  §4c/§4d (a total order, no ties — the fix that turned Minotaur's model positive).

---

## 8. Doubts, flagged — the user reviews and amends these

**D1 — the Vial's ladder position (`V1`, rung 4). The learned scores disagree with me.** Knights'
own `card_scores` put the **first** Aether Vial at **−0.1132**: a hand holding a Vial finished 0.11
turns *slower* than one without. Taken at face value that argues for the Vial near the *back* of the
ladder, not rung 4. Against it: the units are an **opening-hand group-mean difference** confounded
with castability (a hand holding a Vial holds one fewer Knight or land), the profile was generated
**2026-06-18** — before `MTG_KNIGHTS_ORDER` shipped (2026-08-19) and before this deck's value leaf —
so it fingerprints an engine that no longer exists, and the Vial is the deck's only colour-free
deployment route. **I went with rung 4** and recommend `MTG_KNIGHTS_DISCARD_VIALLOW` (Vial sheds with
the surplus mana, i.e. drop the `V1` slot — Minotaur's round-1 arm, which at 4× sample there was
barely measurable in either direction) as the **first** follow-up arm.

**D2 — land quota 4 vs 3.** Anchored on `max_lands: 4` in the deck's own mulligan profile and a curve
topping at MV 4. A 4.3-turn average win would also justify 3. Un-measured; 4 is the conservative
choice (it protects a land the deck may not need, which is the cheaper error in a hand that is one
card over).

**D3 — should a resolved Vial lower the land quota?** I say **no**: it produces no mana and pays no
pip. But it does take over one deployment per turn, so a case exists for 4 → 3 while a Vial is
online *and* matched to a held card. Left out of V1 as an unforced complication.

**D4 — Inspiring Veteran and Marshal of Zhalfir are ENGINE-IDENTICAL.** Both are 2/2 Knights for two
with `+1/+1` to other Knights and exactly one off-{W} pip; Zhalfir's tap ability is documented inert.
No order between them is defensible from the cards. I put Veteran first on weak grounds — blue is the
more contended colour in this deck (7 {U} pips across Zhalfir and Haytham vs 4 {R}), so a {U} card
competes with Haytham for the same fixer — and the learned scores weakly agree (0.168 vs 0.091).
**This is the order most likely to want amending.**

**D5 — Venerable Knight and Dauntless Bodyguard are engine-identical** (2/1 for {W}; the dies trigger
is unmodelled and the ETB-choice/sacrifice is documented inert). Hand index only. If Dauntless
Bodyguard's sacrifice is ever modelled, this changes and so does the "no dies-watcher" premise under
**S1**.

**D6 — the 4-body / 3-anthem slot split is authored, not measured.** It is deck-proportional with a
mild body tilt. The load-bearing part is the pair of saturation promotions, not the fixed pattern; if
the pattern is wrong the promotions should absorb most of the error.

**D7 — Tournament Grounds is a blue source only by approximation.** Its own bracket note says the
engine treats any multi-colour land as `wild`, so it pays {U} in simulation though the paper card
cannot. The colour term above is therefore written against 12 blue sources; the true number is 8. If
that approximation is ever tightened, the fixer sub-quota gets **considerably** sharper teeth and D2
may need revisiting too (a deck that is short on blue wants more lands). **Flagged, not fixed** —
it is a modelling question, not a heuristic one.

**D8 — real-play sheds are ~1 per 400 games for this deck** (measured:
`docs/design/cleanup-discard-measured.md`, 400 d0 games per deck — Knights **1**, against Treasure
Hunt's 336). The deck curves out, empties its hand, and wins on turn 4.3–4.6. **That is the wrong
denominator** and the brief says so: the rollout sheds constantly, and index 0 of this ranking
decides each with no search above it (Minotaur: 99 real vs **250,265** in-search, 2,528×, 100% of
them land-light). **UN-RUN, and recorded as un-run rather than deferred:** the equivalent census for
Knights, `MTG_SHED_STATS=1` over ~200 games at this deck's shipped `d5 / b20` (from
`Knights.value.json`'s `value_play`). I did not launch it — 16 agents are authoring proposals
concurrently and the box rule is one batch at a time, so a sim from this agent would corrupt whatever
else is measuring. Run it at integration; it is the number that sizes this work.

**D9 — a separate bug, found while establishing the Vial distance function; NOT a Knights issue, and
NOT fixed here.** `GoblinsProvider::TutorCandidates` prices a Vial deploy as `mv <= charge`:
`DecisionProviders.cpp:7844` (`t = min(t, max(0, mv - vial_charge))`) and `:7941`
(`vial_charge >= mv`). The real deploy requires **exact** equality (`AIEngine.cpp:3532`,
`TurnSolver.cpp:25386`), and Aether Vial's oracle text is "mana value **equal to** the number of
charge counters". So that scorer believes a Vial at 3 can free-drop a 1-drop, which it cannot. Scoped
to **Goblins' tutor ranking** (Goblins runs 2 Vials; Knights has no tutor — Acclaimed Contender's dig
is the separate `etb_dig` path), so it does not touch this policy. It is flagged because it is
exactly the arithmetic a Knights discard policy would be tempted to copy, and §4a deliberately does
not.

**D10 — the 2HG cell.** `knights2hg` shares this provider and runs ~0.7 turns longer (5.01–5.09 vs
4.28–4.56), so it is where the rule will see the most real-play use. Worth including in the
validation rather than measuring the 1-v-1 cells alone.

---

## Implementation contract (describe only — do not write it from this document)

```
class KnightsProvider : public VialProvider {
    std::vector<int> CleanupDiscardCandidates(
        const GameState&, const std::vector<std::string>*) const override;   // on KNIGHTS, not Vial
};
```

* `static const bool s_bucket = EnvOn("MTG_KNIGHTS_BUCKET_DISCARD", true);` — `=0` returns
  `GenericProvider::CleanupDiscardCandidates(s, required_pieces)` as the A/B hatch. Not
  `VialProvider`'s (§0).
* Empty hand ⇒ delegate to the generic path, as every other bucket rule does.
* Return the shed order via `CleanupDiscardRankingWithOrder(s, required_pieces, shed)` so the staged
  (`m_is_staged`) exemption and required-piece protections stay engine-enforced. Knights' profile has
  `required_pieces: []`, so nothing is name-protected today — the routing still matters, because
  omission-equals-keep and the staged-card invariant come free from it.
* Skip `m_is_staged` cards when partitioning (they are in exile and cannot be shed).
* Helpers to reuse rather than re-derive: `CleanupDiscardManaValue`, `CleanupDiscardIsLand`,
  `IsLordPermanent`, `CardDatabase::Instance().LookupCached`.
* **`CleanupDiscardShedStable()`** — the default is `true`, and every bucket rule in the suite keeps
  it. Keep it here too, **but run `MTG_DISCARD_SHED_VERIFY=1` during integration**: `reach` counts
  lands in hand, so a multi-card cleanup that sheds a land lowers `reach` and can far-flag a threat
  that was not flagged, which would reorder the tail. Knights reaches hand 8 (one card over) in real
  play, so multi-card sheds are a rollout-only concern — but the claim should be *checked*, not
  assumed, and if it trips, declare `false` as `TreasureHuntProvider` does for its own reason.
* Instrument with `MTG_TRACE=discard` (already in the shared builder, `g_real_resolution`-gated and
  one write per line since the tearing fix) before believing any A/B number.

**Adoption bar** per the brief: **non-inferiority plus doctrine quality**, because the rollout sheds
heuristically even when real play (§D8: ~1 in 400 games) does not. Validate with a behavioural diff
first — it costs seconds and, on Minotaur, produced every finding the expensive half did — then
smoke + regression through the accept flow, including the `knights2hg` cell (D10).
