# Angels — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored from `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 sweep. **This
document is a proposal, not code.** Nothing in `src/` was touched. The shipped form would be
`AngelsProvider::CleanupDiscardCandidates` behind a default-on `EnvOn("MTG_ANGELS_BUCKET_DISCARD",
true)`, `=0` restoring `GenericProvider::CleanupDiscardCandidates` as the A/B hatch, with the return
routed through `CleanupDiscardRankingWithOrder(s, required_pieces, shed)` so the staged-card and
required-piece protections stay engine-enforced. Omission from the returned order means KEEP.

`AngelsProvider` (`src/ai/DecisionProviders.h:277`) is today an **empty `DeckProvider` derivation** —
it holds a `Certificate()` and a `Name()` and nothing else, so every heuristic is byte-for-byte
`GenericProvider`'s. This would be its first judgement hook. `DeckProvider : public GenericProvider`
(`DecisionProviders.h:229`), so the `=0` delegation compiles as written.

**Every card below was read from `src/cards/data/cards.json` in this session.** Where a claim is a
measurement it cites the run.

---

## 0 — A CORRECTION TO THE TASK BRIEF, read this first

The assignment described the deck as *"4 Archangel of Thune, 3 Lyra Dawnbringer, 2 Giada, 2 Serra"*.
**That is the ARCHIVED v1 list** (`decks/Angels/v1-thune4-chancery/`). The shipped list
(`decks/Angels/Angels.cod`, adopted 2026-09-19) is:

| | v1 (as briefed) | v2 (SHIPPED, what this policy is for) |
|---|---|---|
| Archangel of Thune | 4 | **2** |
| Lyra Dawnbringer | 3 | **1** |
| Giada, Font of Hope | 2 | **4** |
| Lyra, Archangel of Dawn | — | **4** |
| Lightstall Inquisitor | — | **4** |
| Remote Farm | — | **4** |
| Legion Angel | 1 (+3 SB) | **0** |
| Youthful Valkyrie | 4 | **1** |

This matters for the legend question specifically, and it inverts it. **Lyra Dawnbringer is a
1-of, so it can never collide with itself** — there is no legend problem on that card at all. The
live legend counts are **4 Lyra, Archangel of Dawn / 4 Giada, Font of Hope / 2 Serra the
Benevolent**. Two of the three routing comments in `DecisionProviders.cpp` (lines 9838-9839) and
several `cards.json` disclosures still say "3 Lyra + 2 Giada"; they are stale in the same way.
Flagged, not fixed — a `cards.json` edit is out of this document's scope.

---

## 1 — The deck, and therefore its bucket COUNT

Mono-white Angels tribal, 23 lands, 37 spells of which **32 are ever cast in this simulation**
(measured: Unexpectedly Absent 0 casts and Swords to Plowshares 0 casts across 9,000 games,
`angels-new-cards-screen.md`). The live curve is **5/10/12/2/3** by mana value — 27 of the 32 cost
three or less.

**It does not win by casting a big creature; it wins by a three-stage engine.** Every Angel
*entering* is a trigger (Bishop of Wings gains 4, Righteous Valkyrie gains that creature's live
toughness, Seraph Sanctuary gains 1, Giada's CR 614 replacement adds counters, Youthful Valkyrie
takes a counter). Each of those gains is its **own life-gain EVENT** (CR 119.10), and an event is
what Archangel of Thune and Lyra, Archangel of Dawn read to put a +1/+1 counter on the whole team /
every Angel, what Resplendent Angel's cumulative-5 end step reads for a 4/4 Angel token, and what
pushes life toward Righteous Valkyrie's `StartingLife() + 7` anthem for +2/+2. So the chain is

    an Angel ENTERS  →  LIFE is gained  →  life becomes counters, anthems and more Angels

and **two of the three stages can be missing from a hand.** A hand of four Angel bodies with no
life source gains nothing and therefore triggers none of the payoffs. A hand of Archangels of Thune
with nothing entering does literally nothing. That is combo/engine shape, not "aggro with lords".

**Therefore NOT 2 buckets, and not 3 either. FOUR** — mana plus one per stage of the chain:

1. **MANA**
2. **ANGEL ENTERS** (bodies / trigger fodder)
3. **CONVERTERS** (an Angel entering → life gained)
4. **PAYOFFS** (life or an Angel board → damage: team counters, tokens, anthems, lords)

The brief framed the choice as 2-vs-3, so here is why 3 is wrong and the split into converter/payoff
earns its place. A 3-bucket read lumps Bishop of Wings in with Archangel of Thune as "the engine".
Given one free slot it protects both and sheds a body, which is measurably the wrong trade because
the two are **not interchangeable and not symmetric**:

* **Bishop of Wings alone is useful.** Gain 4 per Angel entering feeds the Valkyrie anthem and
  Resplendent Angel's threshold with no other card's help.
* **Archangel of Thune alone is worth zero.** Its trigger is "whenever you gain life", and with no
  converter on board this deck gains no life at all. (Nothing in the 60 gains life outside the
  enter-watchers, Seraph Sanctuary's own land-ETB, and combat lifelink.)

So one-of-each strictly beats two-of-either, and a bucket list that cannot express that preference
is leaving the deck's central interaction on the floor.

The split also pays off through **structural rule 2**, and in the one way a 3-bucket list could
never see: **the converter role is fillable by a LAND.** A Seraph Sanctuary on the battlefield gains
1 life per Angel entering — and since Thune and Lyra ArchDawn count *events*, not amounts, 1 life is
worth exactly as much to them as Bishop's 4. So a Sanctuary in play means an Archangel of Thune in
hand is LIVE with no Bishop and no Valkyrie anywhere, and the ranking has to know that before it
computes a single quota.

---

## 2 — Card-by-card role table (all 17 cards, from `cards.json`)

`mv` is as the ENGINE reads it (`CleanupDiscardManaValue` → `ManaCost::ManaValue()`; `{X}` counts 0).

| card | n | cost | mv | bucket(s) | keyed on |
|---|---|---|---|---|---|
| Plains | 15 | — | 0 | **MANA** / white land | `IsLand` + `produces` ∋ `White` |
| Remote Farm | 4 | — | 0 | **MANA** / white land, **tapped + depleting** | `enters_tapped`, `enters_tapped_with_depletion 2`, `produces [W]`, `produces_amount 2` |
| Seraph Sanctuary | 4 | — | 0 | **MANA** / colourless land **+ CONVERTER** | `produces [C]`, `own_creature_enters_lifegain 1` + `enters_watch_subtypes ["Angel"]`, `etb_lifegain 1` |
| Sol Ring | 1 | `{1}` | 1 | **MANA** / rock | `mana_rock`, `produces_amount 2` |
| Lightstall Inquisitor | 4 | `{W}` | 1 | **ANGEL ENTERS** (pure body) | subtype `Angel`, **no** engine params |
| Youthful Valkyrie | 1 | `{1}{W}` | 2 | **ANGEL ENTERS** (self-growing body) | `own_creature_enters_self_counters 1` + `enters_watch_subtypes ["Angel"]` |
| Giada, Font of Hope | 4 | `{1}{W}` | 2 | **PAYOFF** (as-enters counters) + restricted source; **LEGENDARY** | `other_subtype_enters_counters_subtype "Angel"` + `_per_each 1`; `tmpl==ManaDork` + `creature_mana_only` + `mana_only_subtype "Angel"` |
| Bishop of Wings | 4 | `{W}{W}` | 2 | **CONVERTER** (gain 4/Angel) | `own_creature_enters_lifegain 4` + `enters_watch_subtypes ["Angel"]`; `dies_watch_subtype "Angel"` + `dies_trigger_creates_tokens 1` |
| Righteous Valkyrie | 4 | `{2}{W}` | 3 | **CONVERTER** + **PAYOFF** + body | `own_creature_enters_lifegain_toughness` + `enters_watch_subtypes ["Angel","Cleric"]`; `life_above_start_anthem_life 7` + `affects_all_creatures` |
| Lyra, Archangel of Dawn | 4 | `{2}{W}` | 3 | **PAYOFF** (event → Angel counters); **LEGENDARY** | `lifegain_each_own_creature_counters 1` + `lifegain_counters_subtypes ["Angel"]` |
| Resplendent Angel | 4 | `{1}{W}{W}` | 3 | **PAYOFF** (≥5 life → 4/4 Angel) + body + 6-mana sink | `endstep_lifegain_tokens 1` + `endstep_lifegain_threshold 5`; `firebreathing_cost {3}{W}{W}{W}` |
| Serra the Benevolent | 2 | `{2}{W}{W}` | 4 | **PAYOFF** (flying anthem / 4-4 Angel); **LEGENDARY** walker | `loyalty_start 4` + `loyalty_abilities` (`flying_team_pump`, `angel_token_44`) |
| Archangel of Thune | 2 | `{3}{W}{W}` | 5 | **PAYOFF** (event → team counters) | `lifegain_each_own_creature_counters 1`, `lifegain_counters_subtypes` **empty** |
| Lyra Dawnbringer | 1 | `{3}{W}{W}` | 5 | **PAYOFF** (Angel lord + lifelink grant); legendary but a **1-of** | `tmpl==LordEffect` + `subtypes_affected ["Angel"]` + `lord_excludes_self` + `grants_lifelink` |
| Lightning Greaves | 1 | `{2}` | 2 | **NO BUCKET** (utility: haste) | `is_equipment` + `equip_grants_haste` + `equip_cost_generic 0` |
| Unexpectedly Absent | 3 | `{X}{W}{W}` | 2 | **NO BUCKET** (0 casts / 9,000 games) | `tmpl==Removal` + `tuck_to_library` |
| Swords to Plowshares | 2 | `{W}` | 1 | **NO BUCKET** (0 casts / 9,000 games) | `tmpl==Removal` + `controller_lifegain_equals_power` |

### Genuinely ambiguous roles, flagged

1. **Righteous Valkyrie is three cards in one** — a converter (gain = the entrant's LIVE toughness),
   a payoff (the `StartingLife()+7` anthem, self-inclusive and non-legendary so four copies stack to
   +8/+8) and an Angel body. It fills three quotas by itself, which is why it is the most protected
   card in the ranking and also why a hand holding one frees *other* cards to be shed. Independently
   measured the **most valuable card in the deck** (screen 16 single-donor ladder, 4th copy 0.0934)
   and it carries the **highest second-copy learned marginal in the profile, +0.257**.
2. **Giada is a payoff, a legend, and a RESTRICTED mana source.** `creature_mana_only` +
   `mana_only_subtype "Angel"` means her `{W}` **cannot cast Bishop of Wings** (Human Cleric), Sol
   Ring, Lightning Greaves or either removal spell. She is classified PAYOFF and counted only in a
   separate `angel_sources` tally (see §4).
3. **Seraph Sanctuary is a land AND a converter.** Classified MANA (playing it costs no card slot),
   but it *satisfies* the converter role from the battlefield, and it is **not a white source** —
   which is the deck's real colour trap, disclosed in `cards.json`: "4 of 24 lands produce colourless
   only, against `{3}{W}{W}` Archangel of Thune and the other WW Angels, so Sanctuary-heavy openers
   genuinely colour-screw."
4. **Bishop of Wings is a CONVERTER and never a body**, and this is card data rather than taste. It
   is a Human Cleric, so: it does not trigger its own enter half, Seraph Sanctuary skips it, Lyra
   ArchDawn's `lifegain_counters_subtypes ["Angel"]` recipient filter skips it, Giada's as-enters
   replacement skips it, Lyra Dawnbringer's Angels-only lord skips it, and Serra's `+2` (flying only)
   skips it. It is the one creature in the deck that receives **none** of the deck's own buffs, and
   it enters as a plain 1/4. Its whole worth is the trigger.
5. **Lyra Dawnbringer is a payoff AND a combat converter** (`grants_lifelink` makes every other
   Angel's combat damage a life-gain event, hence a Thune wave per connecting Angel). The converter
   half is attack-dependent, so it is **not** used to fill the converter quota — see the rejected
   promotions in §7.
6. **Lightning Greaves is honestly unbucketed.** Haste is real (it unlocks Giada's `{T}` the turn she
   lands, per CR 302.6, and gives a fresh 5-drop Angel a turn of damage), but nothing in the deck
   *needs* it. Learned first-copy marginal **−0.115**.

### `[bracket note]` disclosures that change a bucket

| card | disclosure | effect on its role |
|---|---|---|
| **Lightstall Inquisitor** | "THE ETB IS NOT MODELLED AND IS STRUCTURALLY INERT HERE" — no opponent cast path; vigilance also inert | It is a bare **`{W}` 2/1 Angel**. That is the entire card here, and it is exactly why it belongs in the fodder bucket: the **cheapest possible Angel is the cheapest possible trigger**. |
| **Unexpectedly Absent** | pruned search casts it at **X=0 only**, and declines it (tucking a token that ceases on leaving the battlefield does nothing to a passive opponent) | mv is 2, and it is never cast. |
| **Swords to Plowshares** | lifegain rider inert without Tainted Remedy; `allow_self_target` opens the self-exile-for-life line in **HUMAN PLAY ONLY** | never cast autonomously; see Doubt D4. |
| **Serra the Benevolent** | the `−6` emblem is granted as a **no-op** (nothing can damage us) and is not enumerated for the autonomous search | her real content is `+2` and `−3`. |
| **Resplendent Angel** | "each end step" collapsed to one trigger/turn (provably inert: the opponent takes no turns); the `{3}{W}{W}{W}` pump is routed to a **searched** main-phase activation | the 6-mana sink is the search's call, not this ranking's. |
| Flying / first strike / vigilance across the deck | parsed, structurally inert for combat — **but flying IS read by Serra's `+2`** | no bucket effect; noted so nobody "simplifies" a keyword away. |

---

## 3 — What the status quo does today, and why it is indefensible on THIS deck

`GenericProvider::DiscardLandsFirst` is `false` (`DecisionProviders.cpp:369`), so Angels currently
falls straight to the shared **tier B: descending mana value, ties to the earlier hand index**
(`SpellEffects.h:460-474`). On an Angels hand that produces this shed order:

    Lyra Dawnbringer / Archangel of Thune (5)  →  Serra (4)
      →  Righteous Valkyrie / Lyra ArchDawn / Resplendent Angel (3)
      →  Bishop / Giada / Youthful Valkyrie / Greaves / Unexpectedly Absent (2)
      →  Lightstall / Sol Ring / Swords (1)      →  every land (0)

Three things are wrong with that, and all three are load-bearing:

1. **It sheds the payoffs first.** Index 0 is Archangel of Thune or Lyra Dawnbringer on essentially
   every over-full hand that holds one. This is the Mirrorwing gi295 / FiveColour-Progenitus failure
   shape the brief names: max-MV is backwards on a payoff deck.
2. **It keeps the two cards that are never cast, ahead of the deck's best card.** Swords to
   Plowshares (mv 1) and Unexpectedly Absent (mv 2) sit *behind* Righteous Valkyrie (mv 3) in the
   shed order — so the fallback pitches the measured best card in the deck to protect two cards
   measured at **0 casts in 9,000 games**. Unexpectedly Absent's third copy measured **exactly at
   screen 16's FLOOR arm (0.0000)**, i.e. indistinguishable from an empty slot.
3. **Ties are broken by hand ORDER.** Righteous Valkyrie, Lyra ArchDawn and Resplendent Angel all
   have mv 3, so which of the deck's three best nonland cards gets pitched is decided by draw order.
   And every land is kept ahead of everything, including a 6th land — on a deck where screen 16
   measured 24 lands **worse** than 23 (+0.0071 t, t = +3.5) and the learned Plains marginals go
   negative from the second copy on (+0.0013, −0.108, −0.163, **−0.284**).

### Evidence, stated honestly

* **The Angels-specific rollout census is UN-RUN.** I did not run `MTG_SHED_STATS` (16 agents share
  this box today and the box rule is one batch at a time). The command a reviewer should run is
  `MTG_SHED_STATS=1 ./build/Release/mtg --deck decks/Angels/Angels.cod --profile
  decks/Angels/Angels.profile.json --depth 3 --games 200`.
* **The class evidence is strong and directly analogous.** Measured over 200 games at shipped
  depth: Minotaur 99 real sheds vs **250,265** inside the search (2,528x), 100% at fewer than four
  lands; Dragons 66 vs 661,269 (10,020x), 99.6% land-light. Index 0 of this ranking decides every
  one of those with no search above it.
* **Angels' own real-play census says the same thing the other two decks' did** — and it is the
  wrong denominator for the same reason. `analysis-Angels.md`: discard analysis verdict
  `STATUS_QUO_OK`, order A/B `mean_delta` 0.0005 at d0 and **0.0 at d3** over 400 games. Read as a
  bound on the *direct play* metric that is correct and should be quoted in any adoption decision.
  Read as "the rule is inert" it is the exact mistake the Minotaur document had to withdraw.
* **A second large consumer is the KEEP GENERATOR.** Angels ships an exhaustive keep table
  (`Angels.keepmodel.exhaustive.profile.json.gz`, `expected_buckets 15`, `mull_gen_depth 1`,
  `mull_gen_budget_ms 3`), and a land-light kept hand crosses seven cards every turn — so this
  ranking runs on every bucket of that generation too.

**Expected metric effect: small.** The honest case for shipping is doctrine quality and rollout /
keep-generation fidelity, plus non-inferiority. Say so up front, as the Minotaur document eventually
had to.

---

## 4 — The board census (every quota is netted against this)

Computed once, before any quota, iterating `s.battlefield` for `p.controller_index ==
s.active_player_index`, every characteristic via `CardDatabase::Instance().LookupCached(p.card)`
(a hand card is a name-only placeholder, so its own masks are empty):

| counter | what fills it | why separately |
|---|---|---|
| `board_white` | a land/rock whose `produces` ∋ `White`, weighted by `produces_amount` | **A single untapped Remote Farm is TWO white pips** (`produces_amount 2`) and therefore casts Bishop of Wings by itself. Counting it as one source makes half the deck look uncastable. |
| `board_sources` | every land + `mana_rock` (Sol Ring counts 2) | total mana, colour-blind |
| `board_untapped_white` | `board_white` that is not tapped | the turn-1 / turn-N-now question; a Remote Farm entering **tapped** contributes to next turn, not this one |
| `angel_sources` | `tmpl==ManaDork` + `creature_mana_only` + `mana_only_subtype "Angel"` (Giada) | **restricted**: usable for Angel spells only. Counted into reach for a hand card with subtype `Angel` and for nothing else. |
| `board_converters` | `own_creature_enters_lifegain > 0` **or** `own_creature_enters_lifegain_toughness`, with `enters_watch_subtypes` matching `Angel` | the life stage. **Includes a LAND** — the watcher loop in the engine scans all battlefield permanents, not only creatures, which is how Seraph Sanctuary fills this. |
| `board_payoffs` | `lifegain_each_own_creature_counters > 0`, `endstep_lifegain_tokens > 0`, `life_above_start_anthem_life > 0`, `grants_lifelink`, or a `loyalty_abilities` entry making an Angel token / flying pump | the damage stage |
| `board_legend_names` | names of legendary permanents we control | the redundancy test in §6 |
| `sol_ring_out` | `mana_rock` resolved | Sol Ring's quota of 1 |

**Deliberate asymmetry, and it is the sharpest thing in this document.** Rule 2 says a
permanent-based quota counts the battlefield first. **The ANGEL-ENTERS quota is not
permanent-based** — the resource is *an Angel entering*, which a resolved Angel has already spent.
So a board of six Angels does not reduce the need for more Angels to enter; if anything a wide
board **raises** the value of the next body, because Giada's replacement scales with "each Angel you
already control" (N ≥ 1 whenever it applies, per the 2024-11-08 Scryfall ruling) so the next
entrant is bigger, and Righteous Valkyrie's gain scales with that larger toughness. Converters and
payoffs are a STOCK and net against the board; bodies are a FLOW and do not.

---

## 5 — The buckets, with quotas

### Bucket 1 — MANA. Quota **4 sources net of board, of which ≥2 make `{W}`**; a 5th is SOFT.

* **Why 4 and not Minotaur's 5.** Four mana casts 29 of the 32 live spells and *double-spells* the
  1- and 2-drops that are the engine's fodder; only the three 5-drops (1 Lyra Dawnbringer, 2 Thune)
  need a fifth. And the deck is measurably flood-sensitive: 24 lands is worse than 23 (+0.0071,
  t +3.5), the land ladder is **not monotone** (21 worse than 22, t +2.8…+7.9), and the learned Plains
  marginals are +0.0013 / −0.108 / −0.163 / **−0.284**. A fifth source is kept only when nothing else
  in the hand fills a role.
* **The `{W}` floor of 2 is hard, and it is the deck's real constraint.** `{W}{W}` appears on Bishop
  of Wings, Resplendent Angel, Serra, both 5-drops and Unexpectedly Absent, while **4 of the 23
  lands and the Sol Ring make no white at all.** A Seraph Sanctuary can never pay a `{W}` pip.
* **Sub-splits (preferences within the bucket; fungible upward per rule 3):**
  * **Sol Ring is never surplus while none is resolved.** `{1}` for two mana is the deck's only real
    acceleration and the **best first-copy learned marginal in the profile, +0.351**.
  * **≥1 UNTAPPED white source** when `board_untapped_white == 0`. This is the Angels analogue of
    Minotaur's Karoo caveat, keyed on `enters_tapped`: a hand whose only white lands are Remote
    Farms plays a tapped land and casts **nothing** that turn. Keyed on the param, so a screening arm
    that swaps in another tapped land inherits the rule.
  * **Beyond the white floor, a Seraph Sanctuary OUTRANKS a surplus Plains** — and this is
    counter-intuitive enough to be worth the sentence. Screen 16 measured the **4th Seraph Sanctuary
    at 0.0817, the second-best marginal copy in the whole deck**; the learned marginals agree
    (+0.148 / −0.052 vs Plains' +0.001 / −0.108). It fills the converter role for free, its own
    land-ETB (`etb_lifegain 1`) is itself a life-gain EVENT that triggers Thune and feeds Resplendent
    Angel's cumulative 5, and it gains 1 per Angel entering forever. The demotion applies **only to
    the `{W}` floor**, never to the card's value.
  * Remote Farm likewise sits above a surplus Plains once untapped white exists: it taps for
    `{W}{W}` (the whole cost of Bishop of Wings or half of a 5-drop in one land), and its ladder was
    measured monotone to the legal cap of 4. Its depletion (2 counters, then it sacrifices itself —
    fully modelled, `Counter::Type::Depletion`) makes it a bad *late* keep, which the distance term
    in §6 already expresses.

### Bucket 2 — ANGEL ENTERS (bodies / fodder). Quota **3 in hand, NOT netted against the board.**

Cheapest first, because the trigger is the same size whatever the body: a `{W}` Lightstall
Inquisitor and a `{3}{W}{W}` Archangel of Thune each gain Bishop 4, each gain Seraph Sanctuary 1,
each take Giada's counters. Quota 3 because the deck's real pattern is two Angels a turn from turn 3
and the binding constraint is mana, not cards. Only **5 of the 60 cards are pure bodies** (4
Lightstall + 1 Youthful Valkyrie) — every other Angel also carries a converter or payoff role and is
protected by that role first, which is what stops this bucket from swallowing the hand.

### Bucket 3 — CONVERTERS (Angel enters → LIFE). Quota **1 hard + 1 soft, net of board.**

Filled by Bishop of Wings (4), Righteous Valkyrie (the entrant's live toughness) or **Seraph
Sanctuary (1) — from the battlefield, for free.**

* **The second one is genuinely good, unlike Minotaur's second Aether Vial**, and the mechanism is
  explicit in `cards.json`: N converters on one Angel entering is **N separate life-gain events**,
  never one gain of the sum — so it is N team-wide +1/+1 waves from each Archangel of Thune / Lyra
  ArchDawn, and N steps toward Resplendent Angel's cumulative 5. Converters multiply with payoffs.
  Hence 1 hard + 1 soft rather than a flat 1.
* **A counter-maker is NOT a converter.** Giada (`other_subtype_enters_counters_per_each`) and
  Youthful Valkyrie (`own_creature_enters_self_counters`) put counters on bodies; they gain no life,
  so they do not arm Thune, Lyra ArchDawn, the Valkyrie anthem or Resplendent Angel. The engine's
  own `DuplicateEntryOrDeathHasUpside` deliberately treats counters and life together — correct for
  *its* question ("does anything fire?"), wrong for this quota.

### Bucket 4 — PAYOFFS (life / a board → damage). Quota **1 hard + 1 soft, net of board.**

Archangel of Thune, Lyra ArchDawn, Resplendent Angel, Righteous Valkyrie's anthem, Lyra
Dawnbringer's lord, Serra. **A payoff in hand is demoted hard while `board_converters == 0` and the
hand holds no converter** — with no life source, a Thune is a 3/4 for five mana and nothing else.
The converse is the promotion in §7: a single Seraph Sanctuary in play un-demotes it.

### NO BUCKET

Swords to Plowshares, Unexpectedly Absent (0 casts / 9,000 games; UA's 3rd copy = the floor arm) and
Lightning Greaves. This follows the standing user ruling on the same two cards in the Kitty policy
(*"Swords and Unexpectedly are essentially unused in goldfish"*, 2026-08-19) — see Doubt D4 for the
one place that ruling is uncomfortable here.

---

## 6 — Within-bucket order, the legend rule, and distance-to-playable

### 6a — THE LEGEND RULE, net of board (structural rule 2 applied where it bites hardest)

**A hand copy of a legendary card whose NAME is already on our battlefield fills no quota** — it
drops out of its bucket into the shed pool. Live on 4 Lyra ArchDawn, 4 Giada and 2 Serra. (Not on
Lyra Dawnbringer: a 1-of cannot collide.)

**But it is NOT top-of-shed, and getting this wrong is the trap.** `cards.json` is explicit, on both
cards: `EnforceLegendRule` runs **AFTER** the enter cascade, so a redundant Giada or Lyra ArchDawn
still fires *every* Angel-enter watcher on its way in — Bishop's 4, Righteous Valkyrie's toughness
gain, Seraph Sanctuary's 1, a Youthful Valkyrie counter, Giada's own as-enters counters — each its
own life-gain EVENT, hence a team wave from each Thune / Lyra ArchDawn; and **the legend-rule death
is itself Bishop of Wings' "an Angel you control dies"**, i.e. a 1/1 flying Spirit. A redundant
legend is a *one-shot trigger package*, not a blank, and `cards.json` warns in terms: "do not score
copies 2 and 3 as dead."

**So the demotion is conditional on a watcher existing — which is exactly the engine's own
predicate.** Use `DuplicateEntryOrDeathHasUpside(s, controller, def)`
(`DecisionProviders.cpp:450`, a file-static in the same TU the provider lives in) rather than writing
a second opinion about the same card:

* redundant legend **with** upside → shed class 4 below (kept ahead of surplus mana), ordered among
  themselves by how many watchers would fire;
* redundant legend **without** upside → shed class 1, near the top.

I verified the predicate behaves correctly on this deck by reading it: its loop is over
`s.battlefield` with **no `IsCreature()` gate**, so a lone **Seraph Sanctuary makes a redundant
legendary Angel live**; and it returns **false** for a duplicate **Serra** (subtypes `["Serra"]`
matches no `enters_watch_subtypes ["Angel"]`, no dies-watcher, and a planeswalker entering is not a
creature entering), which is right — a duplicate Serra is the one genuinely dead redundant legend in
the deck.

**Empirical support, and one piece of counter-evidence that must not be over-read.** Giada's learned
second-copy marginal is **−0.276, the worst in the profile**, against non-legendary Righteous
Valkyrie at **+0.257** and Lightstall at **+0.217** — the data separates cleanly along the legend
line. *But* screen 16 measured the **4th Lyra ArchDawn at +0.0774** (t +4.5 at 40 life) and screen 18
verified the **4th Giada** against a 3rd Thune and a 2nd Dawnbringer. Those are deckbuilding
verdicts, and they are not in conflict: four copies of a legend is right **because the copies are not
permanent, not because they are free.** A name-based "shed the second legend" rule would contradict a
measured adoption; the net-of-board form does not, because it fires only while a copy is actually
resolved.

### 6b — Distance-to-playable (rule 4). This deck needs a COLOUR-AWARE one.

    reach_total = board_sources + live hand lands (+ angel_sources, for a hand card with subtype Angel)
    reach_white = board_white  + white hand lands (weighted by produces_amount)
    distance(i)  = max( mv(i) - reach_total ,  white_pips(i) - reach_white )

Three deck-specific facts it has to carry, none of which a colour-blind version can see:

1. **A single untapped Remote Farm casts Bishop of Wings.** `produces_amount 2` — count it as two
   white, or the ranking demotes half the deck as uncastable.
2. **Giada does not cast Bishop of Wings.** `mana_only_subtype "Angel"`, and Bishop is a Human
   Cleric. `cards.json` calls out exactly this: without the subtype restriction Giada "would become a
   21st white source for a `{W}{W}` two-drop in a deck with only ~20 white sources", and "being
   white-short with Giada untapped is a reachable line, not a corner case". So Bishop is the hardest
   card in the deck to cast relative to its cost, and the ranking must not treat a board of Angel-only
   mana as reach for it.
3. **A Remote Farm in hand is next turn's mana, not this turn's** (`enters_tapped`).

Bounded and reach-conditional, exactly as Minotaur's is: demote only at `distance >= 2` (two turns
away), and never past a strictly lower-value card at distance 0. Angels has **no cost reducer**, so
there is no reducer promotion — the equivalents that erase distance are a resolved Sol Ring (+2) and
an untapped Remote Farm (+2 white).

### 6c — The authored VALUE order (deck knowledge, and the brief's permitted use of names)

Used only to order cards of the *same* role. Derived from screen 16's measured single-donor ladder
(marginal worth of the 4th copy, `angels-new-cards-screen.md`) cross-checked against the profile's
learned first-copy marginals:

| rank | card | screen 16 | learned 1st | note |
|---|---|---|---|---|
| 1 | Righteous Valkyrie | **0.0934** | +0.230 | three roles in one card |
| 2 | Sol Ring | — | **+0.351** | best learned marginal in the deck |
| 3 | Seraph Sanctuary | **0.0817** | +0.148 | a converter that taps for mana |
| 4 | Lyra, Archangel of Dawn | 0.0774 | +0.177 | legend |
| 5 | Lightstall Inquisitor | 0.0678 | +0.208 | cheapest Angel trigger; 2nd copy still +0.217 |
| 6 | Giada, Font of Hope | 0.0545 | +0.240 | legend; 2nd copy **−0.276** |
| 7 | Remote Farm | monotone to 4 | +0.149 | `{W}{W}` burst |
| 8 | Youthful Valkyrie | 0.0416 | −0.108 | |
| 9 | Resplendent Angel | 0.0380 | −0.005 | |
| 10 | Serra the Benevolent | 0.0305 | −0.092 | |
| 11 | Bishop of Wings | 0.0299 | −0.045 | **see below — placed by ROLE, not by this number** |
| 12 | Plains | — | +0.001 / −0.108 / −0.163 / −0.284 | flood is measured |
| 13 | Archangel of Thune | — | −0.168 | Legion→Thune −0.0110; a 3rd Thune lost to a 4th Giada |
| 14 | Lyra Dawnbringer | — | −0.252 | worst nonland; user floor "at least one" |
| 15 | Lightning Greaves | — | −0.115 | |
| 16 | Unexpectedly Absent | **0.0000 [FLOOR]** | −0.218 | 0 casts / 9,000 games |
| 17 | Swords to Plowshares | — | −0.212 | 0 casts / 9,000 games |

Three caveats on this table, all of which a reviewer should hold against it:

* **These are MARGINAL-COPY worths, not first-copy values** — screen 16's arms each cut one card
  from a 4-of and sent the slot to a 24th Plains, and every arm therefore carries the same 24-land
  penalty ("the ladder ranks but cannot prescribe"). Using them as a value order over a hand is a
  proxy, the same proxy the Minotaur document flags for `card_scores`.
* **The learned `card_scores` describe the right decklist but a slightly older engine.** They were
  regenerated on the adopted v2 list at `4e81a3e7` (2026-09-19); **48 commits have touched `src/`
  since, 9 of them `DecisionProviders.cpp` / `SpellEffects.h`.** That is a mild staleness, nothing
  like Minotaur's 127 — and note the units: `card_scores[c][k]` is an unadjusted opening-hand group
  mean, `avg_win_turn(k) − avg_win_turn(k+1)`, so positive = the extra copy helps and the whole
  series is confounded with castability.
* **Bishop of Wings is placed by its ROLE, deliberately overriding its measured rank.** Screen 16
  ranks the 4th Bishop the weakest real card in the deck (0.0299) and a dedicated probe found that
  cutting it delays the anthem by 0.016 turns and costs 1.8pp of games reaching it — near nil. The
  user nevertheless ruled it to 4 *against the screens*, and correctly: two of its three value
  streams are structurally zero here (life as a defensive resource — `OpponentDeck.h:119`, the
  opponent never takes a turn, so our life only ever rises — and the Spirit half, because nothing
  kills our Angels). Rather than hard-code a name exception, this policy honours that ruling
  **structurally**: Bishop is the CONVERTER, the converter quota is 1 hard + 1 soft, and a
  quota-protected card is not sheddable at all. Its weak *marginal* number only ever decides between
  two Bishops.

---

## 7 — State promotions

### Proposed (a cleanup ranking can actually establish these)

* **P1 — the converter role filled from the BOARD by a land.** `board_converters > 0` because a
  Seraph Sanctuary is in play ⇒ payoffs in hand (Thune, Lyra ArchDawn, Resplendent Angel) stop being
  demoted. Pure board census; no projection. This is the single highest-value promotion here because
  it is invisible to any policy that does not separate converters from payoffs.
* **P2 — the redundant-legend demotion, gated on `DuplicateEntryOrDeathHasUpside`** (§6a). Reuses
  the engine's own predicate so the discard ranking and the duplicate-legend cast prune cannot drift
  apart.
* **P3 — Righteous Valkyrie is never a duplicate.** `affects_all_creatures` +
  `life_above_start_anthem_life` and **non-legendary**: four copies stack to +8/+8 and each copy's
  enter triggers the others. So the copy-count penalty that applies to legends and (weakly) to
  lands must not apply to her. The threshold is read as
  `gamesetup::StartingLife() + p.life_above_start_anthem_life`, **never a literal 27** — Angels is
  the only deck in the matrix with a live 2HG regression cell (`angels2hg`, starting life 30, where
  the correct threshold is 37), and `cards.json` records an assertion that fires on the literal.

### Deliberately REJECTED as search-owned (rule 5)

* **Resplendent Angel's 5-life end step is in reach this turn.** "The Angels I can still deploy would
  gain ≥5" is a cast plan plus arithmetic over a sequence — a projection. The only cheap half
  (`life_gained_this_turn >= 5` already) is a no-op for the shed: the token happens tonight whichever
  card goes. **Left out entirely.**
* **Lyra Dawnbringer's lifelink grant as a CONVERTER.** It is real (N granted Angels connecting is N
  life-gain events, hence N Thune waves) but it is attack-dependent, and per CR 510.2 the engine
  defers those gains until after all damage is assigned. Pricing it needs a combat projection.
  **Not used to fill the converter quota.**
* **Lightning Greaves promoted when a 5-drop is castable this turn.** Haste on a fresh Archangel is a
  turn of damage, but equip is a sorcery-speed action the search already enumerates and the value is a
  damage projection. **Left out.**
* **Serra's `−3` / a resolved Resplendent Angel reducing the BODY quota** on the grounds that the
  board is producing Angel enters by itself. Both are *choices* (a loyalty activation; a conditional
  end-step trigger), not facts a census establishes. **Left out — but see Doubt D5.**
* **No hand-shed-as-payoff inversion exists here.** Nothing in the 60 reads hand size (`no_max_hand_size`
  and `hand_size_anthem_max` are both absent), so there is no Neheb analogue. Stated so nobody looks
  for one.

---

## 8 — The total order over a hand (index 0 is determined for every hand)

Skip `m_is_staged` cards entirely (Angels has none — no foretell/adventure — but the guard is free).
Compute the §4 census, then fill the **interleaved keep ladder**. Interleaved, not bucket-at-a-time,
for the reason the Minotaur implementation found the hard way: "4 sources" and "3 bodies" are
constraints on the same eight-card hand, so the ladder has to say which land beats which Angel, and
a bucket-at-a-time fill protected a fifth land ahead of a castable body.

    S1   Sol Ring, if none resolved
    S2   white source #1      (prefer an UNTAPPED one: Plains > Remote Farm)
    S3   converter #1         (net of board -- a Seraph Sanctuary in play fills it)
    S4   white source #2      (the {W}{W} floor)
    S5   body #1              (cheapest Angel)
    S6   payoff #1            (net of board)
    S7   source #3            (any; white preferred)
    S8   body #2
    S9   converter #2         (soft -- converters multiply)
    S10  source #4
    S11  body #3
    S12  payoff #2            (soft)
    S13  source #5            (soft)
    ---- everything beyond here is SURPLUS ----

Read forwards it is the quota fill; read backwards it is the order the protected cards give way in.
Everything the ladder did not claim is SHED, in these classes, **most expendable first**:

    class 0  removal            Unexpectedly Absent, then Swords to Plowshares
    class 1  a redundant legend with NO entry/death upside   (always a duplicate Serra)
    class 2  surplus mana       Plains > Remote Farm > Seraph Sanctuary   (never Sol Ring)
                                EXCEPT while board_untapped_white == 0, where a Plains is the
                                LAST land shed
    class 3  Lightning Greaves
    class 4  a redundant legend WITH upside, fewest watchers fired first
    class 5  role-exhausted spells: distance(i) descending, then authored value ascending
    tie      hand index ascending  -> a strict total order, so index 0 is always determined

Two placements inside that are worth defending:

* **Surplus mana sheds BEFORE Lightning Greaves**, on count-matched learned marginals: the 4th Plains
  in hand is **−0.284**, the Greaves is **−0.115**.
* **Surplus mana sheds BEFORE a live redundant legend** (class 2 before class 4): a redundant Giada
  with a Bishop out is "gain 4, a team counter from each Thune / Lyra ArchDawn, then a 1/1 flier" for
  `{1}{W}` — a real play. A sixth land is flood.

And **the list names every card in the hand**, which is the non-negotiable part: anything omitted
falls through to tier B's descending mana value, which is the ranking this policy exists to overturn
(§3). Naming everything is also what makes the tail correct without special cases.

---

## 9 — The classification predicates, named for mechanical integration

Every read via `CardDatabase::Instance().LookupCached(c)`; `p` is `def->params`.

```
is_land(i)        CleanupDiscardIsLand(hand[i])
mv(i)             CleanupDiscardManaValue(hand[i])
white_source(i)   is_land(i) or p.mana_rock, and p.produces contains Color::White
                    weight = max(1, p.produces_amount)
colourless_src(i) is_land(i) and p.produces has no White        -> Seraph Sanctuary
tapped_land(i)    p.enters_tapped                                -> Remote Farm
depleting(i)      p.enters_tapped_with_depletion > 0             -> Remote Farm
rock(i)           p.mana_rock                                    -> Sol Ring
restricted_src(i) def->tmpl == CardTemplate::ManaDork && p.creature_mana_only
                    && p.mana_only_subtype == "Angel"            -> Giada
converter(i)      (p.own_creature_enters_lifegain > 0 || p.own_creature_enters_lifegain_toughness)
                    && enters_watch_subtypes matches "Angel"
                    -> Bishop of Wings, Righteous Valkyrie, Seraph Sanctuary
counter_maker(i)  p.other_subtype_enters_counters_per_each > 0
                    || p.own_creature_enters_self_counters > 0
                    -> Giada, Youthful Valkyrie   (NOT converters -- they gain no life)
payoff(i)         p.lifegain_each_own_creature_counters > 0      -> Thune, Lyra ArchDawn
                    || p.endstep_lifegain_tokens > 0             -> Resplendent Angel
                    || p.life_above_start_anthem_life > 0        -> Righteous Valkyrie
                    || p.grants_lifelink                        -> Lyra Dawnbringer
                    || p.loyalty_start > 0                       -> Serra
angel_body(i)     def->card has subtype "Angel"
equipment(i)      p.is_equipment                                 -> Lightning Greaves
removal(i)        def->tmpl == CardTemplate::Removal             -> Swords, Unexpectedly Absent
legend(i)         def->card.HasSupertype(Supertype::Legendary)
redundant(i)      legend(i) && board_legend_names contains def->card.m_name
dup_live(i)       redundant(i) && DuplicateEntryOrDeathHasUpside(s, controller, *def)
anthem_on()       ap.life >= gamesetup::StartingLife() + <that card's life_above_start_anthem_life>
```

Nothing keys on a card name except the §6c value order, which the brief explicitly permits for a
deck-specific value order among cards of the same role. Note two traps the brief warns about and
this list respects: `reduces_spell_subtype_amount`-style **defaulted** fields are never read with a
bare `> 0` (the fields above all default 0/false/empty and are new+gated), and **template is never
used as a proxy for behaviour** — Righteous Valkyrie is `vanilla_creature` and carries the deck's
biggest param block, exactly the case `IsSubtypeCostReducer`'s comment warns about.

---

## 10 — DOUBTS, flagged for the user

**D1 — Is the mana quota 4 or 5?** I chose **4 hard + 1 soft** on two measured grounds (4 mana casts
29 of 32 live spells; 24 lands measured worse than 23 and the Plains marginals go negative at the
second copy). But the deck genuinely wants to double-spell every turn, and more mana converts
directly into more Angel enters while the hand is full — which is *exactly* the state a cleanup shed
happens in. If the sweep shows the policy shedding lands it then wants, 5-hard is the first thing to
try. **My recommendation: 4 + soft 5.** Cheap to A/B either way.

**D2 — Body quota 3 in hand, and NOT netted against the board.** I am confident in the *reasoning*
(an Angel entering is a flow, and Giada's replacement makes a wide board raise the next body's value)
but the number 3 is authored, not measured. It is the single most likely thing to want tuning.

**D3 — Is the converter/payoff split worth its complexity, or should it collapse to 3 buckets?** My
case is in §1 and I believe it, but it should be *measurable*: a 3-bucket arm that merges converters
and payoffs into one "engine" bucket with quota 2 is a clean A/B against the 4-bucket form, and it is
the arm I would run first if the 4-bucket version measures neutral.

**D4 — Removal sheds first, and there are two things uncomfortable about it.** The measurement is
unambiguous (0 casts in 9,000 games; UA's 3rd copy at the apparatus FLOOR) and it matches the
standing user ruling quoted in the Kitty policy. But (a) the user's own curve argument is that
Unexpectedly Absent **is** a real 2-drop in a real game and the search declines it on *value*, not
legality — so the sim's zero is a property of the apparatus; and (b) Swords carries
`controller_lifegain_equals_power` + `allow_self_target`, which the user added on 2026-09-18
specifically so that **human play** can exile our own Angel for life — and in this deck that gain is
a life-gain EVENT, i.e. a genuine one-shot converter. I considered gating the removal class on
`HumanPlayActive()` and **deliberately did not propose it**: it would make the game's ranking differ
from the rollouts' that project it, which is the lockstep property this repo protects everywhere else.
Within the two, I shed **Unexpectedly Absent first** on the floor measurement and the learned
marginals (−0.218 vs −0.212) — that pair is a near-tie the data cannot settle. **This is the one
place where "classify by params" degrades to "classify by card class" (`tmpl == Removal`), and it is
justified by a measurement on these two specific cards, not by the template.** A screening arm that
introduces removal the sim *would* cast must revisit it.

**D5 — Should a resolved Serra or Resplendent Angel reduce the body quota?** Both put Angel *tokens*
onto the battlefield through the shared `CreateToken` and the universal enter cascade, so each token
is a full Angel entering — Bishop's 4, the Valkyrie's toughness, Sanctuary's 1, Giada's counters. A
board that produces its own fodder arguably needs less in hand. I left it out because both are
conditional on a choice (a loyalty activation; a cumulative-5 threshold), which puts them on the
search's side of rule 5 — but I am least sure of this call of anything here.

**D6 — Giada's summoning sickness in the reach term.** I count a resolved Giada in `angel_sources`
unconditionally. She cannot tap the turn she lands (unless Greaves grants haste, CR 302.6), so for a
"can I cast this *now*" question that over-counts by one. I chose to count her because the discard
question is about the next two or three turns, not this one — but over-counting a source is the unsafe
direction, and a `CanTapNow`-style gate is the conservative alternative.

**D7 — Stale disclosures in `cards.json` and the routing comment.** Several bracket notes and
`DecisionProviders.cpp:9838-9839` still describe the v1 list ("3 Lyra + 2 Giada", "4 Archangel of
Thune"). Harmless to behaviour, actively misleading to the next agent. Should be corrected by whoever
owns the Angels card entries; I did not touch them.

---

## 11 — What adoption would cost, and the bar

* **Non-inferiority plus doctrine quality**, per the brief. `real == 0` does not mean inert: the
  rollout and the keep generator are the denominators (§3).
* **This is a play change, so it is a GT rebaseline.** Angels has **10 cells** in
  `test/regression_cases.sh` — smoke `angels 0/3/5` + `angels2hg 3`, regression `angels 0/3/3/5/5` +
  `angels2hg 3`. The `angels2hg` cell is the one to watch: it is the only row in the repo that fails
  if anyone writes Righteous Valkyrie's threshold as a literal 27, verified by injection on
  2026-09-20 — hardcoding the compare moved `angels2hg_smoke_d3_s1001` by ~0.09 turns while leaving
  `angels_smoke_d3_s1001` byte-identical (`regression_cases.sh:248-262`).
* **Validate the way the skill asks:** an `MTG_TRACE=discard` behavioural diff first (it costs
  seconds and, on Minotaur, it was the half of the work that produced everything of value), then a
  paired outcome A/B at d0 and d3 with `MTG_ANGELS_BUCKET_DISCARD=0` as the control arm, then smoke +
  regression through the accept flow. Do **not** provision it like Minotaur's round 4: the measured
  ceiling on this axis for this deck is `mean_delta` 0.0005 at d0 and **0.0 at d3**, so a seven-hour
  batch cannot be justified by it.
