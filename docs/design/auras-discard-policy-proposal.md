# Auras — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored per `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 per-deck sweep.
Proposes `AurasProvider::CleanupDiscardCandidates` behind a default-on
`EnvOn("MTG_AURAS_BUCKET_DISCARD", true)`, `=0` restoring `GenericProvider::CleanupDiscardCandidates`.

**Nothing here is shipped.** No `.cpp`/`.h` file was touched. Unlike Minotaur and Dragons this deck
already HAS a provider (`AurasProvider`, `src/ai/DecisionProviders.h:1271`, created for the Horizon
Canopy dig hooks), so adopting this adds a method rather than promoting the deck off `GenericProvider`.

Every card claim below was read out of `src/cards/data/cards.json`; every engine claim was read out of
the named source file. Nothing is recalled.

---

## 1. The deck, and therefore its bucket count

Selesnya Bogles voltron. 60 cards: **21 lands, 13 creatures, 26 Auras**, and *nothing else* — no
rocks, no dorks, no land auras, no spells, no discard outlets. It wins by putting one creature on the
battlefield and stacking Auras on it (`docs/design/analysis-Auras.md`: "pile Auras on ONE creature").

Its shape is **neither of the brief's simple cases.** It is not a 2-bucket aggro deck, because
"threats" is not one thing here: an Aura with no creature on the battlefield is **UNCASTABLE** — not
weak, *illegal*, and it sits in hand (`LegalEnchantTargets`, `src/core/SpellEffects.h:2750`: "Empty =>
the aura is uncastable (no legal target) and stays in hand"). A creature with no Auras is a 1/1. The
two halves do not substitute for each other in either direction, and the engine enforces that. It is
also not a combo deck: there is no assembled-pieces win, just a ratio.

So it is a **two-component aggro deck**, and the bucket count follows: **three buckets — MANA, HOSTS,
AURAS** — with a sub-split inside each. Answering the question the task posed directly: *yes, hosts
and auras are genuinely two buckets with a floor on each, and the deciding argument is not a value
judgement about the ratio but the engine's castability rule.*

**The `is_land_aura` premise in the task brief does not apply to this list.** All four cards carrying
`is_land_aura` (Wild Growth, Overgrowth, Fertile Ground, Trace of Abundance) belong to other decks;
this decklist holds **zero** land auras, so there is no "ramp deck splits up the ramp" sub-split to
make. The predicate is still specified below (§6) so that a screening arm which adds a Wild Growth
lands in MANA rather than in AURAS — but on the shipping list it is inert, and saying otherwise would
be inventing a bucket the deck does not have.

## 2. Evidence

### Real play essentially never sheds — and that is the wrong denominator

`docs/design/cleanup-discard-measured.md` (400 d0 games per deck) records **auras: 3 discard
events**. Under the Minotaur/Dragons finding that is not a verdict, it is a measurement of the wrong
channel: the SEARCH sheds inside its rollouts constantly (Minotaur 99 real vs **250,265** rollout
sheds per 200 games; Dragons 66 vs 661,269), and index 0 of this ranking decides every one of those
with no search above it. `MTG_SHED_WORST`'s own comment makes the point for a deck shaped like this
one (`src/ai/TurnSolver.cpp:3725`): "KittyEquipment sheds 0 times in real play … while its rollouts
shed ~2900 times per game."

**Auras has one property that should make the rollout count HIGHER than the deck's real-play count
suggests, and it is unique in the suite:** Kor Spiritdancer (`draw_on_aura_cast`) draws a card on
*every* Aura spell cast, so this is one of very few decks in `decks/` that can genuinely outdraw its
own mana and grow past seven cards without flooding. The real game dumps its hand and wins ~T4-5
(measured d3/d5 = 4.465, `analysis-Auras.md`), which is why the real-play count is 3; a rollout that
does not get there keeps drawing.

**The rollout shed census is UN-RUN.** Not deferred to the user — un-run, with a reason: this is a
16-agent fan-out and at authoring time the box was at load average **25.6 on 24 cores**, so starting
a measurement would have been the pooling violation CLAUDE.md forbids and would have produced a
contended number anyway. The exact command for whoever integrates:

```
MTG_SHED_STATS=1 build/Release/mtg --deck decks/Auras/Auras.cod \
    --profile decks/Auras/Auras.profile.json --games 200 --seed 1001   # at the shipped d5/b20
```

### What the shared fallback actually does on THIS deck — it is inverted twice over

`GenericProvider::DiscardLandsFirst` returns **false** (`src/ai/DecisionProviders.cpp:369`), so tier A
is empty and **tier B — descending mana value — decides every Auras shed**
(`CleanupDiscardRankingWithOrder`, `src/core/SpellEffects.h:460`). On this decklist that ranking is:

    MV 3: Ancestral Mask, Armadillo Cloak      <- shed FIRST
    MV 2: Light-Paws, Kor Spiritdancer, Daybreak Coronet, Lion Umbra,
          Spirit Mantle, All That Glitters, Alpha Authority
    MV 1: Ethereal Armor, Rancor, Audacity, Hyena/Spider Umbra, Gryff's Boon,
          Spirit Link, Slippery Bogle, Gladecover Scout
    MV 0: every land                            <- shed LAST, i.e. never

Two separate inversions:

1. **It sheds the deck's biggest payoff first.** Ancestral Mask is +2/+2 *per other enchantment*; on a
   dressed board it is the largest single power swing in the deck. Max-MV pitches it ahead of a
   redundant basic land. This is the Mirrorwing gi295 / Creature-Giving "Defense of the Heart" failure
   in Auras clothing.
2. **It never sheds a land**, in a deck whose curve tops out at **mana value 3** and whose own
   provider already declares four lands surplus (`AurasProvider::ShouldConsiderDig`: "with >= 4 lands
   controlled the post-sac board (>= 3) still casts everything in the deck (the curve tops at
   Ancestral Mask, MV 3)"). At five lands in play, every land in hand is the correct shed and max-MV
   ranks them last.

It also has no way to see the deck's one **strictly dead** card (§5.1).

### The deck's own generated artifacts already bucket it this way

The adopted exhaustive keep model's 19 hand classes (read out of
`decks/Auras/Auras.keepmodel.exhaustive.profile.json.gz`) are an in-repo, *generated*, *validated*
bucketing of exactly this decklist, and they group by role, not by name:

    ["Brushland","Horizon Canopy","Razorverge Thicket"]   the G/W duals, interchangeable
    ["Gladecover Scout","Slippery Bogle"]                 the hexproof 1-drops, interchangeable
    ["Audacity","Rancor"]                                 the {G} +2/+0 pair, interchangeable
    ["Gryff's Boon","Hyena Umbra"]                        the {W} +1 power pair
    ... then every scaling Aura, every restricted Aura, each engine host and each single-colour
        land as its OWN class

That is the brief's "similar effects grouped, different slots not" arrived at independently by a
generator, and it is the reason §6's predicates group by param rather than by name. It also records
`max_lands: 5` for keeps (`Auras.profile.json`), an independent statement that a sixth source is flood.

### The learned `card_scores` — corroboration only, and read with the units caveat

`Auras.profile.json` first-copy marginals (higher = more valuable), sorted:

    Light-Paws .730 > Slippery Bogle .395 > Kor Spiritdancer .290 > Gladecover Scout .246
      > Ethereal Armor .207 > Rancor -.001 > [lands] > All That Glitters -.040
      > Ancestral Mask -.062 > Audacity -.081 > Hyena -.136 > Spirit Link -.149 > Lion Umbra -.152
      > Gryff's Boon -.233 > Daybreak Coronet -.242 > Alpha Authority -.247
      > Armadillo Cloak -.271 > Spider Umbra -.286 > Spirit Mantle -.347

It corroborates four structural claims below: **all four hosts sit at the top**, above every Aura
(§6 host quota); **Ethereal Armor is the best Aura and the only one with a positive SECOND-copy
marginal (+0.099)** — scalers compound, which is also what the cast-order comment at
`DecisionProviders.cpp:1793` says; **both restricted Auras sit below plain Auras of equal or lower
power** (Daybreak Coronet -.242 despite +3/+3 — the layer restriction, §5.4); and **Alpha Authority /
Spirit Mantle / Armadillo Cloak are the bottom** (§5.2, §7).

It is **not** used as the authored value order, for two reasons that are both recorded repo lessons.
The units: `card_scores[c][k]` is an unadjusted opening-hand group-mean difference, confounded with
castability. And the staleness is worse here than in the Minotaur round-4 case that stopped a run:
`Auras.profile.json` has been touched by exactly one commit, `d00e0453` (2026-07-22), and **1,230
commits have touched `src/` since** — including the value leaf, the exhaustive mulligan profile, MDFC
Pathway modelling and the autonomous sequential-aura enumeration. Where it disagrees with the
structural reading in the middle of the order (it puts Spider Umbra below Armadillo Cloak), the
structural reading wins.

## 3. Card-by-card role table

Every one of the 60 cards. "params keyed on" is the field the classifier reads — never the name.

### MANA — 21 lands (all `CleanupDiscardIsLand`; `produces`)

| n | card | MV | sub-role | params keyed on |
|---|---|---|---|---|
| 4 | Horizon Canopy | 0 | dual + **DIG** — kept last among lands | `produces:[G,W]`, `sacrifice_draw_cost:"{1}"`, `tap_self_damage:1` |
| 4 | Razorverge Thicket | 0 | dual, **tapped past the window** | `produces:[G,W]`, `fastland_max_other_lands:2` |
| 4 | Brushland | 0 | dual, pain | `produces:[G,W,C]`, `tap_self_damage:1` |
| 4 | Branchloft Pathway | 0 | MDFC — either colour, then ONE | `produces:[G]`, `mdfc_back_produces:[W]` |
| 3 | Plains | 0 | single colour W | `produces:[W]` |
| 2 | Forest | 0 | single colour G | `produces:[G]` |

### HOSTS — 13 creatures (`def->card.IsCreature()`)

| n | card | MV | P/T | sub-role | params keyed on |
|---|---|---|---|---|---|
| 4 | Light-Paws, Emperor's Voice | 2 | 2/2 | **TUTOR engine** — every Aura you cast fetches another (MV ≤ it, different name) onto Light-Paws | `aura_cast_tutor_attach:true`; **Legendary** supertype |
| 2 | Kor Spiritdancer | 2 | 0/2 | **DRAW engine** + self-buff +2/+2 per Aura on it | `draw_on_aura_cast:true`, `aura_self_buff_power:2` |
| 4 | Slippery Bogle | 1 | 1/1 | plain host | none (hexproof disclosed inert) |
| 3 | Gladecover Scout | 1 | 1/1 | plain host | none (hexproof disclosed inert) |

Note `Slippery Bogle`'s `{G/U}` is modelled as `{G}` (disclosed in its `oracle_text`: the deck runs no
blue source), so it and Gladecover Scout are the same card to this ranking — exactly as the keep
model's bucket 1 has them.

### AURAS — 26 (`params.is_aura && !params.is_land_aura`)

`power` = realised power bonus with **no other enchantment on the battlefield** (the floor);
`power/MV` is that at the floor. **Toughness and lifelink are scored ZERO throughout** — see §8.8.

| n | card | MV | power (floor) | scaling | restriction | params keyed on |
|---|---|---|---|---|---|---|
| 4 | Ethereal Armor | 1 | **+1** | `+1/+1` per enchantment you control, **incl. itself** | — | `aura_scale_kind:"enchantments"`, `aura_scale_power:1` |
| 4 | Rancor | 1 | **+2** | — | — | `aura_power_bonus:2` |
| 1 | Audacity | 1 | **+2** | — | — | `aura_power_bonus:2` |
| 4 | Daybreak Coronet | 2 | **+3** | — | **`another_aura`** | `aura_power_bonus:3`, `aura_enchant_requires:"another_aura"`, `aura_grants_lifelink` |
| 1 | Lion Umbra | 2 | **+3** | — | **`modified`** | `aura_power_bonus:3`, `aura_enchant_requires:"modified"` |
| 4 | Ancestral Mask | 3 | **+0** | `+2/+2` per **OTHER** enchantment on the battlefield | — | `aura_scale_kind:"other_enchantments"`, `aura_scale_power:2` |
| 1 | Hyena Umbra | 1 | +1 | — | — | `aura_power_bonus:1` |
| 1 | Spider Umbra | 1 | +1 | — | — | `aura_power_bonus:1` |
| 1 | Gryff's Boon | 1 | +1 | — | — | `aura_power_bonus:1` |
| 1 | Spirit Mantle | 2 | +1 | — | — | `aura_power_bonus:1` |
| 1 | All That Glitters | 2 | +1 | `+1/+1` per artifact/enchantment you control, incl. itself | — | `aura_scale_kind:"artifacts_enchantments"` |
| 1 | Armadillo Cloak | 3 | +2 | — | — | `aura_power_bonus:2`, `aura_grants_lifelink` |
| 1 | Alpha Authority | 2 | **+0** | — | — | `aura_power_bonus:0` (hexproof/block clauses disclosed inert) |
| 1 | Spirit Link | 1 | **+0** | — | — | `aura_power_bonus:0`, `aura_grants_lifelink` |

**Three predicates partition all 60 cards with no residual.** That matters for the brief's trap #1:
the order below is TOTAL on every hand this deck can hold, so nothing falls through to max-MV.

**Ambiguous roles, flagged:**
* **Kor Spiritdancer** is a host, a draw engine *and* an Aura multiplier (`aura_self_buff_power:2`
  makes every Aura on it worth +2 more). It is bucketed as a HOST with an engine sub-slot; its base
  power of **0** is why the engine slot has to exist — a power-based comparator would rank the deck's
  third-best card last (§8.12).
* **Alpha Authority and Spirit Link grant no power at all.** In this engine their only two functions
  are (a) +1 to the enchantment count every scaler reads and (b) satisfying
  `aura_enchant_requires`. They are bucketed as AURAS, but they are the one class that can be a
  literal blank (§5.2) and the one class that can be worth +4 power (§7.1).
* **Horizon Canopy** is a land and the deck's only card selection. Bucketed as MANA, ordered last
  within it.

## 4. Engine facts this policy rests on (all verified, not recalled)

1. **An Aura with no legal host is uncastable and stays in hand.** `LegalEnchantTargets`,
   `src/core/SpellEffects.h:2750`.
2. **A restricted Aura needs a host that was ALREADY on the battlefield.** `AppendSequencedAuraCandidates`
   (`src/ai/TurnSolver.cpp:30569`) injects "cast the enabler then the restricted Aura this turn" plans
   and is **default-on autonomously** (`SeqAuraOrderingEnabled`, increment 2(a)) — but it only iterates
   `state.battlefield`. `AppendCreatureTargetAuraCandidates` (the "Aura onto a creature cast this same
   turn" injector) explicitly excludes restricted Auras: "plain auras only". So Daybreak Coronet and
   Lion Umbra **cannot** ride a host that enters this turn, even with an enabler in the same plan.
3. **A second Light-Paws, while one is on our battlefield, is STRICTLY DEAD.**
   `OfferDuplicateLegendCast` (`src/ai/DecisionProviders.cpp:498`, `MTG_PRUNE_DUP_LEGEND` default-on)
   refuses to offer the cast: Light-Paws is `vanilla_creature`, so it clears the template whitelist;
   `DuplicateEntryOrDeathHasUpside` scans only creature-**enter**/creature-**dies** watchers and
   Light-Paws' trigger watches *Auras* entering, so it finds nothing; `HandShedIsPayoff` is false (no
   `hand_size_anthem_max` in this deck). And were it cast, `EnforceLegendRule`
   (`src/core/SpellEffects.h:10888`) keeps the **oldest** copy, so the newcomer goes straight to the
   graveyard with the resident's Auras untouched. Zero value, in every path.
4. **Scaling floors differ, and one of them is zero.** `CountAuraScaleUnits`
   (`src/core/SpellEffects.h`): `"enchantments"` and `"artifacts_enchantments"` count the Aura itself
   once it is on the battlefield (floor +1); `"other_enchantments"` subtracts itself (**floor 0**). So
   Ancestral Mask on an otherwise-enchantment-free board is a 3-mana **+0/+0**.
5. **Light-Paws' fetch is MV-BOUNDED and NAME-EXCLUSIVE:** "an Aura with MV <= the cast Aura's, a name
   not already among the Auras you control, and whose own enchant restriction Light-Paws itself
   satisfies right now" (`PerformLightPawsAttach`, `src/core/SpellEffects.cpp:397`). With Light-Paws
   out, **a higher-MV Aura in hand buys a strictly larger fetch bracket** — the shared fallback's
   descending-MV shed is exactly backwards.
6. **Toughness and lifelink are clock-inert.** `CardDatabase.h:1580`: `aura_tough_bonus` is "stored but
   currently INERT vs the passive goldfish opponent"; lifelink is modelled but gains life against an
   opponent that deals none. Every other Aura keyword in this deck (trample, flying, first strike,
   vigilance, reach, hexproof, protection, umbra armor) is disclosed inert in `cards.json`.
7. **Nothing is protected by the engine here.** `Auras.profile.json` has `required_pieces: []`, so
   `CleanupDiscardProtected` returns false for every card and the provider order is fully in charge.
8. **A fastland's tapped-ness is state-dependent and there is a shared predicate for it.**
   `LandWouldEnterTapped` (`src/core/SpellEffects.h`) reads `fastland_max_other_lands` against the
   current battlefield — Razorverge Thicket enters tapped from the fourth land onward.

## 5. The buckets, with quotas (all stated NET OF BOARD)

**Board census first, before any quota** (brief rule 2). Computed once:

```
board_sources  = lands we control                       (all our mana is lands: no rocks, no dorks)
board_colors   = union over our lands of produces + mdfc_back_produces
board_hosts    = creatures we control
board_dressed  = creatures we control with an Aura attached (aura_attached_to == m_number)
                 or a +1/+1 counter        [= CreatureIsModified; this deck has no counter source]
board_tutor    = we control a creature with aura_cast_tutor_attach
board_drawer   = we control a creature with draw_on_aura_cast
board_legends  = names of Legendary permanents we control
scale_credit   = SUM over enchantments we control of params.aura_scale_power
                 ("how much power does ONE more enchantment on the battlefield add")
board_ench     = enchantments we control;  board_ench_all = enchantments, any controller
```

### 5.1 MANA — quota 4 sources, net of board. Sub-split: COVERAGE, then SELECTION.

`land_need = max(0, 4 - board_sources)`. Filled from the battlefield by any land we control (there is
nothing else in the deck that makes mana — no `mana_rock`, no dork, no land aura — so the sub-split
the brief describes for ramp decks has no second leg here).

**Why 4, from three independent in-repo sources:** the curve tops at MV 3 (Ancestral Mask, Armadillo
Cloak); `AurasProvider::ShouldConsiderDig` already declares four lands surplus because the post-sac
board of three "still casts everything in the deck"; and the adopted keep model caps a keep at
`max_lands: 5`. The fourth source is the one that buys a Horizon Canopy activation *and* a spell in
the same turn, so it is a real slot; the fifth is overflow.

**COVERAGE COMES FIRST (brief).** A land is never surplus while it is the only thing covering a colour
the deck needs. The deck needs **W** and **G**, and 16 of 21 lands cover both, so this is cheap — but
it is not free: Daybreak Coronet is `{W}{W}` and Lion Umbra is `{G}{G}`, so double-pip coverage is a
real condition. **Count a Branchloft Pathway as covering BOTH colours** (`produces` ∪
`mdfc_back_produces`) — the played face is a searched plan variant. That deliberately differs from the
mulligan/colour evaluator, which counts a Pathway in hand as its front colour `{G}` only (a disclosed
simplification in its `oracle_text`); for a discard the honest question is what shedding it costs, and
it costs either colour.

**SELECTION is the sub-split.** Once the count and the colours are satisfied, the lands are not
interchangeable: Horizon Canopy is the deck's only card selection and the reason `AurasProvider`
exists at all. It is kept last among lands.

### 5.2 HOSTS — quota 1 host + 1 tutor + 1 drawer, net of board (cap 3, usually 0-1).

```
host_need   = (board_hosts == 0) ? 1 : 0
tutor_need  = board_tutor  ? 0 : 1        // filled only by a card that is NOT a dead legend (§4.3)
drawer_need = board_drawer ? 0 : 1
```

One card may fill several: Light-Paws satisfies `host_need` and `tutor_need` together. Walk the hosts
in value order and clear whichever needs each one is the first to serve.

**Why the engine slots are PER KIND rather than one "engine" slot.** The two engines do orthogonal
things — `aura_cast_tutor_attach` converts an Aura cast into a *board* Aura from the library,
`draw_on_aura_cast` converts it into a *card* — and they stack multiplicatively (each Aura cast then
draws a card AND fetches one). That is the brief's "two cards that fill different slots do not share
a bucket", so a Kor in hand is not made redundant by a Light-Paws on the battlefield.

**Why the plain-host quota is only 1, which is the task's "net of board matters enormously" point,
and it holds for a reason specific to this simulation:** there is no removal and the opponent is
passive, so a resolved host **cannot die**. Every resilience clause in the deck — hexproof on both
1-drops, umbra armor on the three Umbras, protection on Spirit Mantle — is disclosed inert in
`cards.json`. So a second plain host is not insurance; it is one extra point of power per turn, which
every real Aura in the deck beats (§7). Corroborated by the learned second-copy marginal for Slippery
Bogle: **-0.210**.

**And the asymmetry the task asked about is real and is expressed by the quotas being on DIFFERENT
things:** the HOST quota is satisfied by the battlefield (one is enough, forever), while the AURA
floor is **not** reduced by resolved Auras at all — each further Aura adds power, and the three
scalers make the next one worth *more* than the last. That asymmetry is not a weighting; it is why
these are two buckets.

**The legend veto.** A hand Light-Paws does not fill `tutor_need` if `board_legends` already contains
its name — it is strictly dead (§4.3), and treating a dead card as a filled quota would protect it.

### 5.3 AURAS — floor 2, of which at least 1 must be PLAIN. Sub-split: PLAIN vs LAYER.

`aura_floor = 2`, and a LAYER Aura (`!params.aura_enchant_requires.empty()`) counts toward the floor
**only if it is not stranded** (§5.4). This is the Minotaur KAROO CAVEAT's exact shape: a Rakdos
Carnarium with nothing to bounce "is not a land at all", and a Daybreak Coronet with no host that can
carry it is not an Aura we can deploy.

The PLAIN/LAYER split *is* this deck's version of "it splits up the ramp": you need the first layer
before the second layer pays, in the same way you need land drops before acceleration pays. The floor
insists on the first layer.

The floor binds rarely — usually the AURAS bucket is the over-full one and its surplus does the work
(the deck is 26/60 Auras, so a random 7 holds ~3). It matters in the hands where the mana and host
quotas would otherwise eat everything.

### 5.4 The distance-to-playable term, and what it is measured in

Brief rule 4 wants distance to order the shed inside an over-full bucket. **On this deck distance is
NOT measured in mana.** The curve tops at 3 and the deck is at 3-4 sources by the time any of this
matters, so an MV-vs-reach term (Minotaur's `eff_mv >= 5 && reach <= 3`) would essentially never fire.
What is genuinely far away here is measured in **hosts and Aura layers**:

```
layer_castable_now(d)   = board_dressed > 0
                        || (board_hosts > 0 && a plain Aura in hand is affordable within reach)
layer_stranded(d)       = needs_dressed_host(d) && !layer_castable_now(d)
                          && board_hosts == 0 && no creature in hand
```

`layer_stranded` is the two-turns-away case and it is exact, from §4.2: with no creature on the
battlefield, a restricted Aura cannot be cast this turn even alongside a host *and* an enabler,
because the sequenced-Aura injector only offers battlefield creatures. A bare host plus a plain Aura
in hand is **not** stranded — that line is enumerated autonomously.

Mana distance is kept only as the trivial affordability test inside `layer_castable_now`; there is no
`eff_mv` demotion at all, deliberately, because Minotaur's round-1 result is that a
playability-weighted order gets monotonically worse the harder you weight it.

## 6. The classification predicates (name them, so integration is mechanical)

All reads go through `CardDatabase::Instance().LookupCached(card)` — a hand card is a name-only
placeholder (`DeckLoader::MakePlaceholder`), so its own masks are empty.

```
is_land(i)      = CleanupDiscardIsLand(hand[i])                       // def-based
is_ramp_aura(i) = d->params.is_aura && d->params.is_land_aura         // -> MANA. INERT on this list.
is_aura(i)      = d->params.is_aura && !d->params.is_land_aura        // -> AURAS
is_host(i)      = d->card.IsCreature()                               // -> HOSTS
is_tutor(i)     = d->params.aura_cast_tutor_attach
is_drawer(i)    = d->params.draw_on_aura_cast
is_engine(i)    = is_tutor(i) || is_drawer(i)
dead_legend(i)  = d->card.HasSupertype(Supertype::Legendary)
                  && board_legends contains d->card.m_name
needs_host(i)   = !d->params.aura_enchant_requires.empty()            // "another_aura" | "modified"
is_scaler(i)    = !d->params.aura_scale_kind.empty() && d->params.aura_scale_power > 0
zero_floor(i)   = d->params.aura_scale_kind == "other_enchantments"   // the only kind with floor 0
digs(i)         = d->params.sacrifice_draw_cost.has_value()           // Horizon Canopy
tapped_now(i)   = LandWouldEnterTapped(s, *d)                         // handles fastland_max_other_lands
colors(i)       = d->params.produces  UNION  d->params.mdfc_back_produces
```

`is_ramp_aura` is listed above `is_aura` on purpose: a screening arm that introduces Wild Growth must
land in MANA, and `is_aura` is true for land auras too. The same ordering bug (a land aura reaching a
creature-target injector) has already been found for real once — see the Stage-5d note at
`TurnSolver.cpp` `AppendCreatureTargetAuraCandidates`.

### The value scale: realised MARGINAL power, per MANA

```
scale_units_if_cast_now(d) =
    "enchantments"           -> board_ench + 1            // counts itself once it lands
    "other_enchantments"     -> board_ench_all            // subtracts itself, so the pre-cast count IS it
    "artifacts_enchantments" -> board_ench + 1            // no artifacts in this deck
    else                     -> 0

own_power(d)      = d->params.aura_power_bonus
                  + d->params.aura_scale_power * scale_units_if_cast_now(d)
marginal(aura)    = own_power(d) + scale_credit           // +its own bonus, +what it adds to live scalers
marginal(host)    = d->card.m_power.value_or(0)
value(i)          = marginal(i) / max(1, MV(i))           // POWER PER MANA
```

**`scale_credit` is the term that makes this deck's zero-power Auras correct.** Casting Alpha
Authority adds +1 enchantment, which every live scaler re-reads: with an Ethereal Armor, an All That
Glitters and an Ancestral Mask on the battlefield it is worth `1 + 1 + 2 = +4` power for `{1}{G}`, a
better rate than Rancor. With nothing on the battlefield it is worth 0. Same card, correctly valued in
both states, with no card name anywhere.

**Why POWER, and why PER MANA.** Power because toughness and lifelink are clock-inert (§4.6) — this is
a disclosed modelling fact about the simulation, not an opinion about Magic. Per mana because **at a
cleanup discard, cards are by definition the resource we have too much of and mana is the one that
binds**: the deck double- and triple-spells every turn from three lands, so the question "which of
these do I keep" is really "which buys the most power out of the mana I will have".

## 7. The within-bucket orders

### 7.1 AURAS — ascending `value(i)`, i.e. worst rate shed first

At the floor (empty board) that order is, best kept first:

    Rancor / Audacity 2.0  >  Daybreak Coronet 1.5* / Lion Umbra 1.5*  >  Ethereal Armor 1.0
      >  Hyena Umbra / Spider Umbra / Gryff's Boon 1.0  >  Armadillo Cloak 0.67
      >  Spirit Mantle 0.5 / All That Glitters 0.5  >  Ancestral Mask 0.0  >  Alpha Authority /
         Spirit Link 0.0                                        (* = LAYER, subject to §5.4)

and it **re-orders itself as the board fills**, which is the point of computing it rather than
authoring it. With three enchantments out (`scale_credit = 1+1+2 = 4`, `board_ench = 3`):

    Ethereal Armor  (1*4 + 4) / 1 = 8.0          <- was 1.0
    Ancestral Mask  (2*3 + 4) / 3 = 3.3          <- was 0.0, from worst real Aura to second-best
    Rancor          (2   + 4) / 1 = 6.0
    Alpha Authority (0   + 4) / 2 = 2.0          <- no longer a blank
    Spirit Link     (0   + 4) / 1 = 4.0

**Tie-breaks, stated as what gets SHED first so there is no ambiguity.** Among Auras of equal
`value(i)`:

1. **shed a non-scaler before a scaler** — `is_scaler(i)` is the only property that makes a card's own
   rate *rise* with every later cast, so at equal current rate the scaler has strictly more future.
   This is the same fact the cast-order comment states ("scaling Auras compound … they are exactly the
   ones worth duplicating", `DecisionProviders.cpp:1793`) and the only place the learned scores show a
   positive second-copy marginal (Ethereal Armor +0.099).
2. then **shed the HIGHER-MV card first** — at equal rate the cheaper one deploys sooner and can share
   a turn with another cast — **except when `board_tutor`, where this INVERTS and the LOWER-MV card
   sheds first**, because the higher MV buys a strictly larger fetch bracket (§4.5).
3. then hand index.

Tie-break 2 is the one place this policy and the `MTG_AURAS_DISCARD_FLATVALUE` counter-arm disagree by
construction — see doubt 5.

### 7.2 HOSTS

Best kept first: **tutor (Light-Paws) > drawer (Kor Spiritdancer) > plain host** (higher power, then
lower MV, then hand index). Tutor over drawer because the fetch is a free *board* Aura — power now —
where the draw is a card that still has to be cast; corroborated by the learned marginals (.730 vs
.290). A `dead_legend` host is never ranked here at all; it is band 0.

### 7.3 MANA — most expendable first, among lands not needed for coverage

1. `tapped_now(i)` — a Razorverge Thicket from the fourth land onward. A tapped land is a full turn
   behind in a deck that wins on turn 4-5.
2. single colour — `colors(i).size() == 1` (Plains, Forest).
3. MDFC — `mdfc_back_name` non-empty: flexible before the drop, one colour after it (Branchloft
   Pathway). This is also why the generated keep model gives Pathway its own class instead of merging
   it with the duals.
4. pain dual — `tap_self_damage > 0 && !digs(i)` (Brushland).
5. free dual — Razorverge Thicket inside its window.
6. `digs(i)` — **Horizon Canopy, kept last.**

Ties by hand index. Test `digs(i)` before `tap_self_damage` — Canopy carries both.

## 8. The total order over a hand (index 0 is determined for every hand)

Returned as a shed order, most expendable first, through
`CleanupDiscardRankingWithOrder(s, required_pieces, shed)`. **Omission = keep.** Bands, not a
comparator chain, so the rungs compose and a stable sort keeps hand order inside a band.

**Bands 0-7 hold SURPLUS cards** (everything past its bucket's quota). **Band 8 is the quota-protected
tail**, which only becomes reachable when the whole hand is inside quota.

| band | contents | within-band order |
|---|---|---|
| **0** | **DEAD** — `dead_legend(i)` (a Light-Paws with one already on our battlefield). Provably worth nothing in every path (§4.3). | hand index |
| **1** | **BLANK AURA** — `is_aura`, `marginal(i) == 0`, `!board_tutor && !board_drawer`, and **not the only plain Aura in hand while a LAYER Aura is held** (i.e. not the sole enabler). Alpha Authority / Spirit Link on an unscaled board. | higher MV first, then hand index |
| **2** | **DEAD SCALER** — `zero_floor(i)` realising 0 (`board_ench_all == 0`), no other Aura in hand or on board to raise the count this turn, and no engine in play. Ancestral Mask on an empty board — the deck's dead Karoo. | hand index |
| **3** | **SURPLUS LAND** beyond `land_need` and not required for colour coverage. | §7.3 |
| **4** | **SURPLUS PLAIN HOST** — `is_host && !is_engine`, with `host_need` already met. The task's "further hosts are close to dead weight". | lower power, then higher MV, then hand index |
| **5** | **STRANDED LAYER AURA** — `layer_stranded(i)` (§5.4). | higher MV first, then hand index |
| **6** | **SURPLUS AURA** beyond `aura_floor`. | §7.1 |
| **7** | **SURPLUS ENGINE HOST** — a distinct-kind engine whose need is already met on board. | drawer before tutor, then hand index |
| **8** | the quota-protected tail, in **reverse ladder order** (below) | — |

### The ladder (which quota slot yields first)

Quotas are constraints on ONE hand, so the ranking must say which land beats which Aura. Minotaur's
shipped lesson #1 is that a bucket-at-a-time fill gets this tail wrong. Fill order, highest keep
priority first:

    host1 > mana1 > aura1(plain) > mana2 > engine > mana3 > aura2 > mana4

Read backwards, that is band 8's internal order — `mana4` yields first (the softest slot: the fourth
source only buys a Canopy activation), then `aura2`, `mana3`, `engine`, `mana2`, `aura1`, `mana1`, and
**`host1` is the last card in the hand this rule will ever shed**, because without a legal host every
Aura in the deck is illegal to cast.

`host1` above `mana1`, and `mana2`/`mana3` above `aura2`: the engines and the best Auras cost 2, and
the deck double-spells from three, so being able to deploy outranks a second Aura. Both are flagged as
doubts 3 and 7 in §10.

### Worked examples

**A — the fallback's inversion, fixed.** Board: 4 lands (Brushland ×2, Forest, Plains), Slippery Bogle
wearing Ethereal Armor. Hand (8): Plains, Forest, Ancestral Mask, Daybreak Coronet, Rancor,
Light-Paws, Slippery Bogle, Spirit Link.
`scale_credit = 1`, `board_ench_all = 1`, `board_hosts = 1` (dressed), no engine on board.
Quotas: `land_need = 0` (board 4) → both hand lands surplus. `host_need = 0`; `tutor_need = 1` →
**Light-Paws protected**. Aura floor 2 → values: Rancor `(2+1)/1 = 3.0`, Daybreak Coronet `(3+1)/2 =
2.0` (castable: the Bogle is dressed), Spirit Link `(0+1)/1 = 1.0`, Ancestral Mask `(2·1+1)/3 = 1.0`.
Floor protects Rancor and Coronet. Surplus: Plains, Forest (band 3), Slippery Bogle (band 4), Spirit
Link and Ancestral Mask (band 6). Band 3 decides: both colours already covered, neither enters tapped,
both single-colour → **index 0 = the first of {Plains, Forest} in hand order.**
The generic fallback's index 0 here is **Ancestral Mask**.
(Inside band 6 the two surplus Auras tie at 1.0 and tie-break 1 sheds Spirit Link, the non-scaler,
ahead of the Mask — so even if both lands had been needed for coverage, the Mask is not the shed.)

**B — the dead card.** Board: Light-Paws wearing Ethereal Armor + Daybreak Coronet. Hand holds a
second Light-Paws. Band 0 → **index 0 = the second Light-Paws**, always. Under the fallback it is one
of seven MV-2 cards tied on mana value, and hand order decides whether the deck sheds a card worth
literally nothing or one of its best.

**C — the blank.** Board: a bare Gladecover Scout, 2 lands, no enchantments. Hand: Alpha Authority,
Spirit Link, Rancor, Ethereal Armor, Daybreak Coronet, Forest, Plains, Slippery Bogle.
`scale_credit = 0`, no engine. Alpha Authority and Spirit Link both have `marginal = 0`; the
sole-enabler veto does not save either (Rancor and Ethereal Armor both enable the Coronet), so both
are band 1. Higher MV first → **index 0 = Alpha Authority.** Note the deck still keeps Ancestral
Mask-style scalers here only if held; with no scaler and no engine, a 0-power Aura genuinely does
nothing.

**D — the tutor inversion.** Board: Light-Paws wearing Rancor, 3 lands. Hand over the limit with
Spirit Link `{W}`, Hyena Umbra `{W}`, Ancestral Mask `{2}{G}`, Alpha Authority `{1}{G}`.
`board_tutor` is true, so bands 1 and 2 are **disabled** — every Aura cast buys a fetch, so no Aura is
a blank. Values: Hyena `1/1 = 1.0`, Ancestral Mask `(2·1)/3 = 0.67`, Spirit Link `0`, Alpha Authority
`0`. Both zeroes are non-scalers, so tie-break 1 does not separate them and tie-break 2 decides —
**inverted, because the tutor is out:** Alpha Authority's `MV 2` bracket can fetch a Daybreak Coronet
(+3/+3), Spirit Link's `MV 1` bracket cannot. **Index 0 = Spirit Link.** Without the tutor the same tie
goes the other way and Alpha Authority is the shed.

## 9. State promotions — proposed, and rejected

**Proposed (all four are facts a cleanup ranking can establish from the battlefield alone):**

1. **The dead-legend promotion** (band 0). Structural, provable, and it is the single best shed the
   deck can make.
2. **The `scale_credit` promotion** — a live scaler on the battlefield raises *every* Aura's value by
   its `aura_scale_power`, which is what moves Alpha Authority from blank to better-than-Rancor. Reads
   only the battlefield.
3. **The zero-floor demotion** (band 2) — Ancestral Mask realising +0/+0 is currently a 3-mana blank
   and is self-correcting, so the demotion has to be state-conditional rather than a static rank.
4. **The tutor MV inversion** (§7.1 tie-break 2) — with `aura_cast_tutor_attach` in play, MV is an
   asset. Proposed **default-ON inside V1** with `MTG_AURAS_DISCARD_FETCHMV=0` as the hatch, on a
   DOMINANCE argument rather than a measurement: the fetch bracket is `MV <= the cast Aura's MV`, so a
   higher-MV Aura's eligible set is a strict SUPERSET of a lower-MV one's, and a superset cannot fetch
   worse. (That is the same "structural rather than fitted, and it costs nothing" basis on which
   `MTG_TH_TOWER_SPARE` ships below measurement.) It is deliberately only a **tie-break**, never a
   credit: the honest credit would be "the best Aura in the library at MV ≤ this MV", and reading the
   library's contents to value a card in hand is clairvoyance. The tie-break states a fact about the
   card, never about what is actually in there.

**Deliberately rejected as SEARCH-owned or non-discriminating:**

* **Which host to enchant.** `enchant_target` is already a searched plan variant
  (`analysis-Auras.md`, viewer item 1). Not ours.
* **Which Aura Light-Paws fetches.** Provider-owned via `LightPawsAuraCandidates` and a human-play
  chooser. A different decision at a different site.
* **Whether to sacrifice a Horizon Canopy.** `AurasProvider::DigDecisionSearched()` returns **true** —
  the enumerator fans dig/no-dig and the rollout decides (USER 2026-08-28). A discard ranking must not
  pre-empt it; §7.3 only says a Canopy is the *last* land to shed, which is a statement about the card
  and not about whether to activate it.
* **Kor Spiritdancer's draw credit.** A resolved Kor makes every Aura cast draw a card — so it scales
  every Aura identically and **cannot discriminate between two Auras in hand**. It is a reason the
  drawer slot is worth protecting (§5.2), not a ranking term. Same for `aura_self_buff_power`: a
  resolved Kor raises every Aura's realised power by +2 uniformly.
* **Any lethal or damage projection.** "Is this Aura enough to kill this turn", "is the host already
  big enough" — these are cast decisions with a damage model behind them, which is exactly the class
  the Minotaur proposal's Burning-Fist and full devotion promotions were rejected as.
* **Same-turn layering as a *plan*.** The ranking uses `layer_castable_now` only as a distance test.
  Which order to cast an enabler and a Coronet in is `CastOrderRank` 20/22/23 plus the sequenced-Aura
  enumeration, and it is already reviewed.
* **Emptying the hand.** No `hand_size_anthem_max` card exists in this deck, so there is no Neheb
  inversion here — a shed is a pure loss, never a benefit.

## 10. Doubts — flagged for the user's amendment

1. **The task brief's land-aura premise does not hold for this list.** There are no `is_land_aura`
   cards in `Auras.cod`, so there is no ramp sub-split and "land auras are RAMP not threats" changes
   nothing here. The predicate is specified anyway (§6) for screening arms. **If the intent was that
   Auras SHOULD be running Wild Growth-class ramp, that is a deckbuilding question for
   `deck-screening.md`, not a discard one.** Flagging rather than silently ignoring it.
2. **Land quota 4 vs 3.** Defended from the curve, `ShouldConsiderDig`'s own threshold, and the keep
   model's `max_lands: 5` — but never measured *for this decision*. Counter-arm: `MTG_AURAS_DISCARD_LAND3`.
3. **`host1` at the top of the ladder, above `mana1`.** With no land anywhere the hand is dead either
   way, and `mana1` is filled by the battlefield in nearly every state where this fires — so I believe
   it is low-stakes, but I have not measured it. Counter-arm: `MTG_AURAS_DISCARD_MANAFIRST`.
4. **Band 3 above band 4 — a surplus land sheds before a surplus plain host.** The argument is that
   the deck's own dig gate calls the fifth source zero-marginal while an extra 1/1 is at least a point
   of clock per turn. The learned second-copy marginals do not settle it (Bogle -0.210 is *worse* than
   Brushland +0.051, but both are opening-hand quantities measured in a state with no board at all).
   Counter-arm: `MTG_AURAS_DISCARD_BODYFIRST`.
5. **`value = marginal / MV` versus `marginal` alone.** Per-mana assumes mana binds, which I argue it
   does at a cleanup by construction. But it demotes Daybreak Coronet (1.5) below Rancor (2.0) even
   though Coronet is +3 in one card, and on a board that is already flooded the flat version may be
   right. Discriminating counter-arm: `MTG_AURAS_DISCARD_FLATVALUE`.
6. **The tutor MV inversion's magnitude** — shipped default-ON as a tie-break on a dominance argument
   (§9.4), which may be too timid: with Light-Paws out, a `{2}{G}` Ancestral Mask's fetch bracket covers
   *every* Aura in the deck, which is more than a tie-break's worth. A fuller credit would need the
   library, i.e. clairvoyance. Hatch: `MTG_AURAS_DISCARD_FETCHMV=0`.
7. **The aura floor of 2 (and "at least 1 plain").** Chosen to be the smallest floor that guarantees a
   deployable first layer. 3 is arguable given the deck is 26/60 Auras.
8. **Everything here scores toughness and lifelink as ZERO.** That is disclosed engine behaviour
   (§4.6), not my judgement — but it means Armadillo Cloak, Spirit Link and Daybreak Coronet's
   lifelink and every `aura_tough_bonus` in the deck are valued at nothing, and **this comparator
   inverts for exactly those cards the day the simulator gets an opponent that deals damage.** Worth a
   comment at the implementation site so it is not silently wrong later.
9. **The learned `card_scores` are 1,230 `src` commits stale** (2026-07-22) and are opening-hand group
   means. Used for corroboration only, never as the order. If this axis is ever worth a precise
   measurement, the Minotaur round-4 sequencing lesson applies: **regenerate the scores first, then
   measure** — precision on a stale table is not precision.
10. **The rollout shed census is UN-RUN** (§2) — not deferred, un-run, with the box at load 25.6/24.
    The command is given. It should be the first thing the integrator runs, because it is the number
    that sizes the whole exercise.
11. **This deck can flood its hand in a way the 3-per-400 real-play figure hides**, because Kor
    Spiritdancer draws on every Aura cast. Un-measured; it argues the rollout count is higher than the
    Minotaur ratio would predict, not lower.
12. **Kor Spiritdancer's base power is 0**, so a purely power-based comparator ranks the deck's
    third-most-valuable card (learned .290) at the very bottom of band 4/7. The per-kind engine slot
    (§5.2) is what prevents that, and band 7 is what keeps a *surplus* Kor above surplus lands and
    bodies. If the engine slot is ever simplified to one generic "engine", re-check this.

## 11. Implementation contract and validation plan

**Shape.** `std::vector<int> AurasProvider::CleanupDiscardCandidates(const GameState&, const
std::vector<std::string>*) const`, returning the band order above and ending in
`CleanupDiscardRankingWithOrder(s, required_pieces, shed)` so the staged-card and required-piece
protections stay engine-enforced. First line: `static const bool s_bucket =
EnvOn("MTG_AURAS_BUCKET_DISCARD", true); if (!s_bucket) { return
GenericProvider::CleanupDiscardCandidates(s, required_pieces); }`. Helpers available:
`CleanupDiscardManaValue`, `CleanupDiscardIsLand`, `LandWouldEnterTapped`,
`CardDatabase::Instance().LookupCached`. Classification by params only; the one place a card *name* is
used is the legend-redundancy test, which compares a hand card's name against our own battlefield —
that is a rules test (CR 704.5j keys on the name), not a value judgement.

**The levers, in one place.** Per `coding-conventions.md` every one is `EnvOn`-read (`=0` off, `=1` on,
never a presence-only `getenv`), and each wants a `heurarm::Slot` so one pooled `mtg --batch` can sweep
every arm — `AURAS_BUCKET_DISCARD` alongside the existing `KE_BUCKET_DISCARD`, plus one slot per arm.

| lever | default | what it does |
|---|---|---|
| `MTG_AURAS_BUCKET_DISCARD` | **ON** | the whole policy; `=0` restores `GenericProvider` exactly |
| `MTG_AURAS_DISCARD_FETCHMV` | **ON** | the tutor MV inversion in §7.1 tie-break 2 (dominance argument, §9.4) |
| `MTG_AURAS_DISCARD_FLATVALUE` | OFF | `value = marginal` with no per-mana divisor (doubt 5) |
| `MTG_AURAS_DISCARD_LAND3` | OFF | land quota 3 instead of 4 (doubt 2) |
| `MTG_AURAS_DISCARD_MANAFIRST` | OFF | swap `host1`/`mana1` at the head of the ladder (doubt 3) |
| `MTG_AURAS_DISCARD_BODYFIRST` | OFF | swap bands 3 and 4 — surplus body sheds before surplus land (doubt 4) |

`FLATVALUE` and the default are a **discriminating pair** in the Minotaur round-4 sense: either alone
is a number that cannot tell "mana binds at a cleanup" from "power binds" apart.

**Validation, cheapest-first, because the cheap half is where Minotaur's value came from:**

1. **Bound the axis before building anything.** `MTG_SHED_WORST=1` vs default on the Auras suite cells
   — it inverts the ROLLOUT cleanup specifically, which is the channel that matters here. If best and
   worst play the same, no ranking can be worth much and this should ship on doctrine alone.
2. **`MTG_SHED_STATS=1`** for the real-vs-rollout ratio and the lands-in-play distribution at the
   moment of the shed (Minotaur: 100% under four lands — the Auras equivalent decides whether §5.1's
   quota of 4 is even reachable in the states that fire).
3. **Behavioural diff** (`test/tools/discard_behaviour_diff.py`, seconds) — confirm the rule FIRES and
   that its signature is the intended one: fewer Ancestral Mask / Light-Paws sheds, more redundant
   basics and dead duplicate legends. A silent no-op reads exactly like "no effect"
   (`silent-noop-passed-every-gate`), and `MTG_TRACE=discard` now leads with `g<game_seed>` so
   decisions can be paired by game.
4. **Outcome A/B** on the suite's own Auras cells — `auras 0/3/5` in smoke, five cells in regression —
   through the accept flow. Auras is "the cheapest deep-search deck in the suite"
   (`test/regression_cases.sh:120`), so this is inexpensive; pool all arms into ONE `mtg --batch`.
5. The adoption bar is **non-inferiority plus doctrine quality** (the brief), because the rollout sheds
   heuristically even when real play does not — `real == 0` does not make the rule inert.

**Honest expectation:** real play sheds 3 times in 400 games, so avg-win-turn is unlikely to move much
at the top level. The case for shipping is that band 0 is a provable defect fix (the deck currently
sheds a card worth literally nothing only by hand-order luck), band 3 corrects a straight inversion
(max-MV never sheds a land in a deck whose curve tops at 3 and which declares 4 lands surplus), and
§7.1's value scale replaces "most expensive first" with "least power per mana first" on a deck where
those are close to opposites.
