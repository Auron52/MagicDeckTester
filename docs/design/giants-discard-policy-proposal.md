# Giants — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored per `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 sweep. **This is a
proposal, not shipped code.** Nothing in `src/` was touched. If approved it ships as
`GiantsProvider::CleanupDiscardCandidates`, behind a default-on `EnvOn("MTG_GIANTS_BUCKET_DISCARD",
true)` with `=0` restoring `GenericProvider::CleanupDiscardCandidates` as the A/B hatch, returning a
shed order (most expendable first) routed through `CleanupDiscardRankingWithOrder(s,
required_pieces, shed)`.

Every card characteristic below was read from `src/cards/data/cards.json` in this session — mana
cost, P/T, type line, `oracle_text` and `params` — never recalled. Two Scryfall corrections already
recorded in `analysis-Giants.md` are load-bearing here and are repeated so nobody re-derives them
wrong: **Stinkdrinker Daredevil has no Prowl** (that is Stinkdrinker *Bandit*) and it is a **Goblin
Rogue, not a Giant**; **Surtland Flinger is not an MDFC** (one print, khm 377, no land back face).

**READ THE INTEGRATION CONSTRAINT FIRST (bottom section).** The Giants exhaustive mulligan
generation is in flight on the secondary machine at freeze `aaad47b3`, and it plays keeps out through
rollouts that consult this very ranking. Landing a `src/ai/DecisionProviders.cpp` change now moves
`HEAD:src` and costs that ~6.9 h run.

---

## 1. Evidence — measured this session

### The census (`MTG_SHED_STATS=1`, single-threaded, profile auto-attached, value sidecar live)

This deck is the sharpest example in the fleet of the brief's warning that a real-play census is the
wrong denominator. At the suite's own d5 cell, **real play sheds ZERO times in 200 games and the
search sheds 81,594 times**:

| cell (seed 1001, the suite's own cells) | sheds in REAL play | sheds inside the SEARCH | with <4 lands | avg turn |
|---|---|---|---|---|
| **d5 b20**, 200 games | **0** | **81,594** | **77,947 (95.5%)** | 6.1250 |
| **d3 b10**, 200 games | **0** | **30,646** | **29,035 (94.7%)** | 6.1350 |
| **d0**, 1000 games | 11 | 0 (no rollouts at d0) | — | 6.5650 |

(Commands, reproducible verbatim:
`MTG_SHED_STATS=1 ./build/Release/mtg decks/Giants/Giants.cod --games 200 --seed 1001 --depth 5
--budget-ms 20 --threads 1`, and the d0/d3 twins. `MTG_TRACE=discard` was on for the d5 run and
emitted **nothing**, which is the same fact from the other side: the trace is gated on
`g_real_resolution`, and there were no real sheds to trace.)

The ratio is not 2,528x (Minotaur) or 10,020x (Dragons) — it is **undefined, because the numerator
is zero at both searched depths**. Anyone sizing this work off games actually played would conclude
the rule is inert and be wrong by 81,594 decisions per 200 games, every one of them decided by index 0
of this ranking **with no search above it**. And ~95% of them are taken with fewer than four lands on
the battlefield: the screwed-and-flooding shape, where the ranking is choosing between cards the
player cannot yet cast and "shed the most expensive card" is least defensible. Note also
`sheds/cleanup = 1.00` in both searched cells — every cleanup that sheds sheds exactly one card, so
**index 0 is the whole decision**, with no second-place card to soften a wrong pick.

The d0 row is worth keeping for the one narrow thing it measures: real play *does* discard here, just
rarely (11 in 1000 games at d0), so this is not a decision the deck structurally never reaches.

### What the deck does TODAY, and why it is backwards

`GiantsProvider` is an empty `DeckProvider` derivation, `DeckProvider : public GenericProvider`, and
`GenericProvider::DiscardLandsFirst()` returns `false` — so Giants rides the base
`CleanupDiscardCandidates`, whose tier B is **descending mana value**. On this decklist that is, shed
first to shed last:

    Borderland Behemoth (7) / Hamletback Goliath (7)
      -> Inferno Titan (6) / Sunrise Sovereign (6)
      -> Giant Harbinger (5) / Surtland Flinger (5)
      -> Tectonic Giant (4)
      -> Stinkdrinker Daredevil (3)
      -> Lightning Greaves (2) / Fire Diamond (2) / Pyroclasm (2)
      -> Sol Ring (1) / Lightning Bolt (1)
      -> Mountain (0)

Read that list once more, because it states the whole case:

* **It sheds all 20 Giants and all 4 cost reducers before it will shed one Pyroclasm, one surplus
  Mountain, or a fourth Lightning Bolt.** The expensive Giants are not a liability in this deck; they
  are the deck. Max-MV pitches them first and keeps the two cards that are worth least.
* **Pyroclasm is kept over every threat**, and against this engine's opponent model Pyroclasm is a
  *blank* — see §5, this is the deck's single strongest finding.
* **Lands are shed LAST** (MV 0), so a flooded hand pitches its threats and protects its seventh
  Mountain. That is the exact inversion the "quota-first, net of board" rule exists to fix.
* **The cost reducer goes before Pyroclasm and before a surplus land.** Stinkdrinker Daredevil takes
  {2} off every Giant spell in the deck and stacks per copy; it is the card that makes the top of the
  curve reachable, and max-MV ranks it 8th of 14.
* Within the payoffs max-MV is right only by accident. It sheds Hamletback Goliath (7) before Inferno
  Titan (6), which happens to be correct; it also sheds **Borderland Behemoth (7) before Inferno
  Titan (6)** at an empty board, where Behemoth really is the worst card in the deck, and keeps that
  same ordering when three Giants are already out and Behemoth is a 12/12. Cost is only accidentally
  correlated with value here.

### A supporting signal, deliberately not load-bearing

The deck's learned `card_scores` (first-copy marginal, `Giants.profile.json`) put the ramp on top:

    Sol Ring 1.338 > Stinkdrinker Daredevil 0.740 > Fire Diamond 0.395
      > Tectonic Giant 0.214 > Surtland Flinger 0.138 > Inferno Titan 0.108
      > Giant Harbinger -0.137 > Lightning Greaves -0.150 > Sunrise Sovereign -0.176
      > Lightning Bolt -0.225 > Borderland Behemoth -0.243 > Hamletback Goliath -0.348
      > Pyroclasm -0.535

The three ramp cards are the three best-scoring cards in the deck and **Pyroclasm is dead last**,
which independently agrees with §5 and with the ramp-first doctrine below. But per the Minotaur
round-4 lesson these numbers are *not* used to order anything: `card_scores[c][k]` is an
opening-hand group-mean difference, confounded with castability, so using it as `value` inside an
EV = P × value model double-counts mana. They are also an engine-state fingerprint — this profile was
written once, at `bcf23579`, and **8 commits have touched `src/` since** (mild, versus Minotaur's
127, but real). Treat them as corroboration, never as the ordering.

---

## 2. The deck in one paragraph, and therefore its shape

Giants is mono-red Giant tribal that **ramps and re-prices its way into 4-to-7-mana Giants**. Twenty
of its sixty cards are Giants costing MV 4-7; it reaches them three ways — 24 Mountains, three mana
rocks (Sol Ring, 2 Fire Diamond), and four **Stinkdrinker Daredevil**, a cost reducer that takes {2}
off every Giant spell and stacks per copy. Everything else is support: three Lightning Greaves (the
only haste, which turns the turn a Giant lands into a full attack) and four Lightning Bolt (the only
non-creature reach). It wins around turn 6 (measured above: 6.125 at d5, 6.565 at d0), and its best
games are ramp games — the claude-play sweep's one turn-4 win (GI=15) is logged as *"ramp, not
Giants"*.

So its shape is **RAMP**, and per the brief that is mana + ramp-split-by-role + threats. The ramp
splits three ways here rather than two, which is the deck's one structural peculiarity: **land drops**
(Mountains), **acceleration** (rocks, which beat the one-drop-per-turn cap), and **cost reduction**
(the Daredevil, which does not add supply at all — it lowers demand). A cost reducer is a ramp-class
card for this deck, exactly as `MinotaurProvider` treats Ragemonger and `DragonsProvider` treats
Dragonspeaker Shaman, and `IsSubtypeCostReducer(def)` is the shared predicate for it. Two support
roles get small buckets of their own (haste, reach) and one card gets a bucket of its own for being
*negative* (Pyroclasm).

---

## 3. Card-by-card role table

Every one of the 14 distinct main-deck cards is named. Nothing falls through to `rest`, which is the
property the Mirrorwing gi295 lesson demands: an un-covered card is handed back to the max-MV
fallback this policy exists to overturn.

| n | card | cost | MV | type / body | bucket | keyed on (`params`) |
|---|---|---|---|---|---|---|
| 24 | Mountain | — | 0 | Land | MANA / lands | `CleanupDiscardIsLand`, `produces: [R]` |
| 1 | Sol Ring | {1} | 1 | Artifact | MANA / rocks (burst) | `mana_rock`, `produces_amount: 2`, `produces: [C]` |
| 2 | Fire Diamond | {2} | 2 | Artifact | MANA / rocks (slow) | `mana_rock`, `enters_tapped: true`, `produces: [R]` |
| 4 | Stinkdrinker Daredevil | {2}{R} | 3 | Creature — Goblin Rogue 1/3 | RAMP / cost reducer | `reduces_spell_subtype: "Giant"`, `_amount: 2` |
| 4 | Inferno Titan | {4}{R}{R} | 6 | Creature — Giant 6/6 | THREAT rank 0 | `attack_trigger_damage_any: 3`, `etb_damage_any: 3`, `firebreathing_*` |
| 4 | Sunrise Sovereign | {5}{R} | 6 | Creature — Giant Warrior 5/5 | THREAT rank 1 | `power_bonus: 2` + `subtypes_affected: [Giant]` + `lord_excludes_self` |
| 3 | Borderland Behemoth | {5}{R}{R} | 7 | Creature — Giant Warrior 4/4 | THREAT rank 2 *(board-conditional)* | `static_self_pump_per_other_subtype: "Giant"`, `_power/_tough: 4` |
| 2 | Tectonic Giant | {2}{R}{R} | 4 | Creature — Elemental Giant 3/4 | THREAT rank 3 | `attack_trigger_modal`, `attack_trigger_damage_each_opponent: 3`, `attack_trigger_impulse_*` |
| 2 | Surtland Flinger | {3}{R}{R} | 5 | Creature — Giant Berserker 4/6 | THREAT rank 4 | `attack_sac_fling`, `attack_sac_fling_double_subtype: "Giant"` |
| 2 | Hamletback Goliath | {6}{R} | 7 | Creature — Giant Warrior 6/6 | THREAT rank 5 | `any_creature_enters_self_counters_power` |
| 3 | Giant Harbinger | {4}{R} | 5 | Creature — Giant Shaman 3/4 | THREAT rank 6 | `tutor_to_top`, `tutor_types: [Giant]`, `tutor_optional` |
| 3 | Lightning Greaves | {2} | 2 | Artifact — Equipment | HASTE (quota 1) | `equip_grants_haste`, `equip_cost_generic: 0` |
| 4 | Lightning Bolt | {R} | 1 | Instant | REACH (quota 1 + 1 soft) | `damage: 3`, `targeting: Any` |
| 2 | Pyroclasm | {1}{R} | 2 | Sorcery | **BLANK — first shed** | `damage_all_creatures: 2` |

60 cards. The sideboard (Dragon Breath, Giant Harbinger, Mountain) is unreachable — no wish effect —
and Dragon Breath is not in `cards.json` at all; it is correctly out of scope.

**Genuinely ambiguous roles, flagged:**

* **Stinkdrinker Daredevil** is a creature, so a naive "creature ⇒ threat" partition puts it in the
  threat bucket. It must be classified as **ramp**, and the reducer test must run *before* the
  creature test (the same ordering `DragonsProvider` uses: `if (is_reducer(i)) return false;` inside
  `is_payoff`). Note it is a **Goblin Rogue**: Sunrise Sovereign's "other Giant" pump never reaches
  it, Borderland Behemoth does not count it, Surtland Flinger's fling is **not** doubled on it, and
  Giant Harbinger **cannot fetch it**. So a surplus Daredevil is not a body worth keeping — this is a
  deliberate divergence from Minotaur, where a surplus Ragemonger folds back into the threat pool as a
  3-mana 2/2 Minotaur. Here it stays a 1/3 that nothing in the deck cares about.
* **Lightning Greaves** is a colourless staple that any deck may add, which is exactly why
  `DetectDecisionProvider` deliberately excludes `equip_grants_haste` from the Giants *routing*
  signature. Using it here is fine — this predicate runs only inside the deck's own provider — but it
  is the one bucket key that is not archetype-specific, and a screening arm that cuts Greaves loses
  the bucket harmlessly (quota simply never fills).
* **Giant Harbinger** — tutor, threat, or its own bucket? Argued in §4; my answer is **threat, ranked
  last**, not a bucket. It is a 3/4 Giant body first and a selection effect second, and its selection
  value is at its *minimum* at the exact moment this rule runs (see §4).
* **Pyroclasm** is the one card whose bucket depends on the opponent model rather than on the card.
  See §5 and doubt D1.

---

## 4. The buckets, with quotas — all NET OF BOARD

> **Census the battlefield before computing a single quota** (brief rule 2). Every quota below counts
> permanents first and the hand owes only the remainder.

The board census this needs, all read through `CardDatabase::Instance().LookupCached(p.card)`:

```
board_lands        p.card.IsLand()
board_sources      board_lands + permanents with params.mana_rock
board_red_sources  sources whose params.produces contains Color::Red
board_reducers     permanents with IsSubtypeCostReducer(*d)
board_red_amount   SUM over those of max(1, d->params.reduces_spell_subtype_amount)   <-- SUM, not max
board_giants       permanents whose DEFINITION carries the tribe (CardHasSubtype(d->card, tribe))
board_haste        permanents with params.equip_grants_haste
```

**Where `tribe` comes from matters, and it must not depend on a reducer being visible.** Take it from
whichever card needs it and carries it: `static_self_pump_per_other_subtype` ("Giant") on Borderland
Behemoth supplies it for P4, `subtypes_affected[0]` on Sunrise Sovereign supplies it for the lord
half, and `reduces_spell_subtype` on a Daredevil supplies it for the discount. Nothing hardcodes
"Giant"; a reducer-less, Behemoth-less hand simply skips the rules that needed the name.

**`board_red_amount` must SUM, not take the maximum.** `ManaPayment.cpp:1420` is
`subtype_reduction += std::max(1, pd->params.reduces_spell_subtype_amount)` — the discount stacks per
copy, and Stinkdrinker's own oracle note says so explicitly ("one copy takes Inferno Titan {4}{R}{R}
to {2}{R}{R}, two take it to {R}{R}"). `MinotaurProvider` uses `std::max` because Ragemonger's
coloured-pip twin is what it reads; copying that here would silently under-credit a second resolved
Daredevil by {2}.

### Bucket 1 — MANA. Target **6 sources**, net of board, −2 with a reducer resolved, floor 4.

```
mana_need = max(0, 6 - 2 * min(board_reducers, 1) - board_sources)
```

*Why 6.* Six mana casts every card in the deck except the two MV-7s, and **six mana plus one resolved
Daredevil casts the entire deck** (Borderland Behemoth {3}{R}{R} = 5, Hamletback Goliath {4}{R} = 5).
That is the deck's plan stated as a number. The reducer credit is capped at one copy's worth so the
target floors at 4, which is where two resolved Daredevils already cast two Giants in a turn (Inferno
Titan {R}{R} + Tectonic Giant {R}{R}). Dragons' user-ruled target was 4-5 for a deck topping at MV 8,
but Dragons runs six reducers and four rocks against this deck's four and three. **6 is the number I
am least sure of** — see doubt D2 and the `MTG_GIANTS_DISCARD_MANA5` lever.

*The sub-split (brief: "so you have acceleration and land drops"; sub-quotas are fungible upward, the
parent total binds).* One ordered pick list, not two competing quotas:

1. **A land, first, whenever `board_lands == 0`.** Colour coverage comes first and this is also this
   deck's **KAROO-equivalent blank rule**: a mana rock in hand with no land anywhere is *not* a mana
   source, it is a blank. Sol Ring needs {1} and Fire Diamond needs {2}, and neither can be paid with
   no land on board and no land in hand. Sol Ring additionally `produces: [C]` only, so a
   Sol-Ring-only board cannot pay the {R} pip of any spell in the deck except the other rocks and
   Greaves. This is the Minotaur two-Karoo hand (seed 1001 gi=27, zero lands played in eight turns)
   wearing a different costume, and it is why a dead rock is the first *mana* card shed.
2. **Sol Ring** (`mana_rock && produces_amount >= 2`). It is the largest single mana jump in the deck:
   {1} for two mana, net +1 on the turn it is cast and +2 every turn after, and it is untapped on
   arrival. It outranks the second land drop, and it is the deck's highest learned card score (1.338)
   by a factor of 1.8.
3. **Remaining lands.**
4. **Fire Diamond last** (`mana_rock && enters_tapped`). It is real acceleration — it beats the
   one-land-per-turn cap — but it costs {2}, produces nothing the turn it lands (the engine honours
   `enters_tapped` on the cast path and gates the enumerator's same-turn rock credit off), and so needs
   two further turns to break even in a deck that wins on turn 6. It is the weakest mana card to hold.
5. **A blanked rock** (no land available) ranks below everything — it is the first card shed, tier S0.

*Colour:* a soft preference for two red sources across board+hand, because four of the seven Giants
carry {R}{R}. Nearly free on a 26-red-source manabase (24 Mountains + 2 Fire Diamonds of 27 sources),
so it is a tie-break inside the pick list, not a quota.

### Bucket 2 — RAMP / COST REDUCER. Quota **2 while none is resolved, 1 once one is, 0 at two.**

This mirrors the Dragons ruling ("2 while no reducer is on the battlefield, 1 once one is") and the
Minotaur ruling that the reducer **is mana, not a late enabler** — *"Ragemonger is also quite helpful
when dealing with mana problems. It's usually a good idea to keep 1 of them"* (user, 2026-08-30). The
argument transfers with more force here: {2} off every Giant is worth about two land drops, it stacks,
and the state it fixes — land-light with threats stranded in hand — is the state **95.5% of this
deck's sheds are taken in**. A card that answers the problem cannot be the last thing kept while
facing it. Surplus copies do **not** fold into the threat pool (§3).

### Bucket 3 — THREATS. Quota **2 hard + a 3rd soft.**

Deliberately FEWER than a threats-bucket instinct suggests, and this is the Dragons ruling verbatim
(user, 2026-08-30): *"you might keep fewer dragons, since they can be expensive... 2 at minimum"*. A
hand of five Giants and two Mountains does nothing. **Do not import Minotaur's 3-hard-plus-a-4th**:
that number was set for a deck of MV 1-5 bodies, and this deck's *cheapest* Giant (Tectonic Giant,
MV 4) costs more than Minotaur's most expensive threat.

Value order, best kept first — every test is a gated param, nothing is name-bound:

| rank | card | why | key |
|---|---|---|---|
| 0 | **Inferno Titan** | the only card that damages the face without attacking, and it does it twice: 3 on ETB *and* 3 on every attack, on top of a 6/6 body and a firebreathing mana sink. ~12 damage the turn after it lands (3 + 3 + 6), 14 under a Sovereign. | `attack_trigger_damage_any > 0` |
| 1 | **Sunrise Sovereign** | +2/+2 to every *other* Giant, **live on the turn it enters** (a static ability needs no untap step), non-legendary so copies stack, and it multiplies every Giant still to come — including making Behemoth bigger twice over (as a body it counts, and as a lord it pumps). | `power_bonus > 0 && !subtypes_affected.empty()` |
| 2 | **Borderland Behemoth** | the highest ceiling in the deck (4/4 + 4/4 per other Giant: a 12/12 at two other Giants, verified 14/14 with a Sovereign in GI=8) and the lowest floor (a 4/4 for seven mana at an empty board). **Board-conditional — see §5.** | `!static_self_pump_per_other_subtype.empty()` |
| 3 | **Tectonic Giant** | 3 damage to the face on *every* attack plus a 3/4 body = 6 a turn, unconditional, with an impulse mode as the alternative. | `attack_trigger_modal` |
| 4 | **Surtland Flinger** | a 4/6 that converts a Giant on board into double its power at the face — a finisher, but conditional on fodder it has to be willing to lose. | `attack_sac_fling` |
| 5 | **Hamletback Goliath** | a 6/6 for seven that grows only from creatures entering *after* it, so it is worth most early in the Giant sequence and is a plain 6/6 as the last card cast. | `any_creature_enters_self_counters_power` |
| 6 | **Giant Harbinger** | the worst body (3/4 for five) and the one threat whose value is *selection*. **It is at its minimum precisely when this rule runs**: a cleanup discard happens at a FULL hand, and a full hand is where another card's worth of selection is worth least. Its fetch also spends the next draw step, which in a 24-Mountain deck is its real cost. | `tutor_to_top` |

Two notes on this order. Tectonic Giant is ranked above Surtland Flinger on recurring output (6 a turn
unconditional versus 4 a turn plus a one-shot), and the learned card scores agree independently (0.214
vs 0.138). Hamletback Goliath is ranked above Giant Harbinger on value-when-resolved (a 6/6 that grows
beats a 3/4), where the learned scores **disagree** (−0.348 vs −0.137) — but that disagreement is
exactly the cost confound the EV model handles separately with `P(play)`, so importing it would
double-count mana. Flagged as doubt D4 with a lever.

### Bucket 4 — HASTE. Quota **1**, zero once `board_haste > 0`.

Lightning Greaves is the deck's only haste. It buys a full turn of a Giant's damage — with it, an
Inferno Titan's attack trigger and its 6 combat damage land on the turn it arrives (3 + 3 + 6 instead
of 3). At {2} it deploys on a turn that would otherwise be blank, and **Equip {0} re-points it for
free every turn**, so one copy serves every Giant the deck will ever cast and a second is close to
dead. Placed low in the ladder (§6), which makes it Dragons' *"a 2-mana enabler, IF THERE IS SPACE"*
in practice: on a 7-card keep it is the 7th slot.

### Bucket 5 — REACH. Quota **1 + 1 soft.**

Lightning Bolt: the deck's only non-creature damage, 3 at the face for {R} — 15% of the opponent's
life for one mana, and the only card in the deck that is castable on turn 1 off a single Mountain. It
is never stranded, so the distance term never demotes it; what bounds it is redundancy, and a third
and fourth copy are surplus.

### Bucket 6 (not a bucket — a **blank**) — Pyroclasm. **Always the first shed.**

See §5. It is listed here so the bucket list is complete: Pyroclasm has no quota and holds no slot.

---

## 5. State promotions — proposed, and the ones deliberately REJECTED as search-owned

### PROPOSED

**P1 — Pyroclasm is a blank, and shedding it weakly DOMINATES.** This is the deck's strongest
single finding and it is read straight off the card's own bracket note, not inferred. Against this
engine's opponent model Pyroclasm can do nothing good and can do real harm:

* The clock cannot move. The note is explicit — *"OUTCOME-INERT FOR THE CLOCK, disclosed not
  simplified: it kills opponent spawn creatures in 6 of the 10 spawn patterns, but spawns never attack
  or block and no card in this deck watches opponent creature deaths."*
* One copy kills nothing of ours (the deck's minimum toughness is 3 and Pyroclasm deals 2), so a
  single copy is a pure blank: two mana, no effect.
* Two copies **in one turn** deal 4 and kill **our own** Stinkdrinker Daredevil (1/3, a Goblin Rogue
  that Sunrise Sovereign's "other Giant" pump never reaches) and an unpumped Giant Harbinger or
  Tectonic Giant (3/4). Damage wears off at cleanup, so it does not accumulate across turns — but it
  does within one.
* The deck has no death payoff for it to enable: Hamletback Goliath counters on creature *entry*, and
  Surtland Flinger's sacrifice is an attack trigger, not a death trigger.

So its value is ≤ 0, and shedding it also removes a card the search could otherwise elect to cast.
Independently, it is the lowest-scoring card in the deck's learned `card_scores` (−0.535) by a clear
margin. **Key it on `params.damage_all_creatures > 0`, and document that the verdict rests on the
OPPONENT MODEL, not on the card** — the moment a blocker path or a spell-casting opponent exists this
must be revisited, exactly the standing of approved deferrals D1/D2/D3. Flagged as doubt D1.

**P2 — A resolved cost reducer erases the distance, and the discount floors at the coloured pips.**
The core of the distance term:

```
eff_mv(i) = mv(i) - min(2 * board_red_amount, d->card.m_mana_cost.generic)   // when the subtype matches
```

The subtype comes from the reducer's own `reduces_spell_subtype`, matched with
`CardHasSubtype(d->card, tribe)` **on the definition** — a hand card is a name-only placeholder
(`DeckLoader::MakePlaceholder`) whose `m_subtypes` is empty, which is the third defect the Dragons
provider shipped with. **The pip floor is not cosmetic on this deck.** `ManaPayment.cpp` reduces
`cost.generic` only, floored at 0 (CR 601.2f), so at two resolved Daredevils a naive subtraction
prices Tectonic Giant at **0** (true: {R}{R} = 2) and Surtland Flinger at **1** (true: {R}{R} = 2).
Minotaur's implementation does not floor; here it must.

What one reducer does to the curve, for reference: Titan 6→4, Sovereign 6→4, Behemoth 7→5, Goliath
7→5, Harbinger 5→3, Flinger 5→3, Tectonic 4→2. At two: 2, 2, 3, 3, 1, 2, 2.

**P3 — Distance-to-playable as EV = P(play) × value, not a lexicographic playability sort.** Adopt the
shape Minotaur's three-round arc already paid for, rather than re-deriving its refuted V1:

```
reach   = board_sources + (hand lands) + (hand rocks that are live)
need    = eff_mv(i) * (1 + copies_ahead(i))        // the k-th copy pays for all k
deficit = need - reach
P       = {1.0, 0.70, 0.35, 0.10, 0.03}[clamp(deficit)]   // HARD decay; 0.02 beyond 4
value   = 7.0 * 0.85^position-in-the-authored-order
EV      = P * value
```

Three reasons to start here instead of at a binary far-flag. (a) Minotaur measured
playability-ordered-above-value and it was the **wrong shape** — it sheds the deck's best card the
moment it is two sources short — and the gradient was monotone in how hard playability was weighted
(+0.00015 at slack 1, +0.00089 at slack 2; positive = worse). (b) Minotaur measured the 6-bucket
`value` term and it lost because ties inside the big bucket collapsed EV to pure playability; using
the **position in the full authored order** fixed it and was adopted at −0.00037 t/game across three
seed blocks. (c) Giants' cost spread is MV 1-7 against Minotaur's 1-5, and a resolved reducer moves
`eff_mv` by 2 or 4, so the distance term has strictly more work to do here. Cumulative-mana duplicates
apply **inside the threat bucket only** — the mana and reducer buckets have their own quotas and
charging both would double-penalise. As a property worth stating: with `P` equal the sort reproduces
the authored order exactly, so it degrades gracefully.

**P4 — Borderland Behemoth is board-conditional, and that is a P/T read, not a projection.** Its
power is `4 + 4 × (other Giants you control)`, recomputed live. So:

* `board_giants >= 2` → promote it to rank 0 (it is a 12/12, the biggest body in the deck);
* `board_giants == 0` → demote it below every other threat (a 4/4 for seven mana is the worst card in
  the deck, blank rock aside).

This is admissible where the rejected promotions below are not: it needs only a count of Giants on the
battlefield, no damage projection and no cast choice. **Sunrise Sovereign gets the milder half of the
same rule** — its marginal value is `2 × other Giants` plus a 5/5 body, so it promotes at
`board_giants >= 2` but never demotes to the bottom, because it still pumps every Giant still to come.

**P5 — The haste quota is zero once a Greaves is on the battlefield.** Equip {0} re-points for free
each turn, so the copy on board already serves every future Giant.

### REJECTED as search-owned (brief rule 5)

* **"Keep Surtland Flinger, we have a 6-power Titan to fling for 12."** A damage projection *and* a
  cast/attack choice — and the engine already searches it: `Plan::fling_victim_choice` /
  `GameState::scripted_fling_victim` (−2 = decline), measured and kept (`MTG_FLING_AXIS`). A cleanup
  ranking must not hold a second, differently-arbitrary opinion about it.
* **"Keep a Bolt, the opponent is exactly 3 from dead."** Pure damage projection. Same class as
  Minotaur's rejected Fanatic-devotion reach term.
* **"Keep Hamletback Goliath, more creatures are coming."** Needs a projection of future casts, and
  its counters depend on cast ORDER, which is the search's.
* **"Keep Tectonic Giant for the impulse mode."** Already a searched axis twice over
  (`MTG_TECTONIC_AXIS` for the mode, plus the mode-B keep axis built 2026-09-22).
* **"Promote Giant Harbinger because it can fetch the Titan we lack."** *Which* Giant it fetches is a
  searched plan variant (the tutor axis), and whether declining is better is deferral D4. A ranking
  cannot price a fetch whose target the search chooses.
* **A turn-number promotion for Fire Diamond** ("it is acceleration on turn 2 and a blank on turn 5").
  Real, but it is a cast-timing judgement and the quota already expresses most of it. Left out as a
  flagged doubt (D3) rather than smuggled in as a constant.

---

## 6. The total order over a hand — index 0 is always determined

### The keep ladder (INTERLEAVED, one card per slot)

Quotas are **not** filled bucket-at-a-time. Minotaur found that out for real: a bucket-at-a-time fill
protected a fifth land ahead of a castable Boros Reckoner at four lands in play, caught in an
`MTG_TRACE=discard` probe. Read forwards this is the quota fill; read backwards it is the order the
quota-protected cards give way in, which is what decides most real hands.

```
 1 MANA      (a land first while board_lands == 0; a blanked rock never fills a slot)
 2 THREAT
 3 REDUCER
 4 MANA
 5 THREAT
 6 MANA
 7 HASTE
 8 REDUCER   (the 2nd, only while none/one is resolved)
 9 THREAT    (the soft 3rd)
10 MANA
11 REACH
12 MANA
13 MANA
14 REACH     (the soft 2nd Bolt)
```

The first seven slots are what a 7-card keep actually consumes, and they encode the doctrine: the
first land beats the first threat (a missed land drop is the one thing this deck cannot recover, and
both the rocks and the reducer need mana to deploy); the first threat beats the first reducer (a
discount on an empty hand discounts nothing); the reducer beats the second land (it is worth about two
land drops); haste takes the 7th slot, i.e. "if there is space".

### The shed order

```
S0  BLANKS                a Pyroclasm; a mana rock with no land available anywhere
S1  surplus MANA          reverse of the pick list: Fire Diamond, then Mountains, then Sol Ring
S2  surplus HASTE         a 2nd/3rd Greaves, or the 1st once one is on board
S3  surplus REACH         a 3rd/4th Bolt
S4  surplus REDUCERS      beyond the quota; NOT folded into the threat pool (§3)
S5  surplus THREATS       reverse of the keep order, i.e. lowest EV first
S6  unrecognised          structurally empty on this decklist — all 14 cards are named
KEEP TAIL                 the ladder read backwards, so the list covers the WHOLE hand
```

The keep tail is mandatory, not tidiness: anything omitted falls through to the shared tier B, which
is descending mana value — the ranking this provider exists to overturn — and on a ramp deck whose
curve tops at seven, a hand where every card is quota-covered is the common case.

### Worked example A — the inversion, on a hand this deck really has

Turn 3, board 2 Mountains, no reducer resolved. Hand (8): 3 Mountain, Stinkdrinker Daredevil, Inferno
Titan, Sunrise Sovereign, Lightning Bolt, Pyroclasm.

`board_sources = 2` ⇒ `mana_need = 4`. Fill: MANA1 Mountain, THREAT1 Titan (rank 0), REDUCER1
Daredevil, MANA2 Mountain, THREAT2 Sovereign, MANA3 Mountain, HASTE1 —, REDUCER2 —, THREAT3 —, MANA4
— (hand exhausted), REACH1 Bolt. Seven kept; **index 0 = Pyroclasm** (S0).

**Today the same hand sheds Inferno Titan** — MV 6 tied with the Sovereign, ties keeping the earlier
hand index — and keeps the Pyroclasm.

### Worked example B — the flooded hand

Turn 3, board 3 Mountains. Hand (8): 5 Mountain, Inferno Titan, Hamletback Goliath, Sol Ring.

`board_sources = 3` ⇒ `mana_need = 3`. `board_lands > 0`, so the rock is live and leads the pick list:
MANA1 Sol Ring, THREAT1 Titan, REDUCER1 —, MANA2 Mountain, THREAT2 Goliath, MANA3 Mountain. Five
protected; the three surplus Mountains are overflow, so **index 0 = a surplus Mountain** (S1).

**Today the same hand sheds Hamletback Goliath**, then the Titan, and keeps all five Mountains.

### Worked example C — the keep tail doing the work

Turn 2, board 1 Mountain. Hand (8): 5 Mountain, 2 Giants, Lightning Bolt.

`mana_need = 5`, so the ladder protects all five Mountains (slots 1, 4, 6, 10, 12), both Giants (2, 5)
and the Bolt (11) — eight cards for seven slots, nothing is overflow. The keep tail decides: the
ladder reversed sheds the **last-taken mana card, the 5th Mountain**. That is the right answer for a
deck that wins on turn 6 (a sixth source arrives too late to cast anything new) and it is a decision
a bucket-at-a-time fill cannot express at all.

---

## 7. The predicates, named concretely (so integration is mechanical)

```cpp
auto def_of = [](const Card& c) { return CardDatabase::Instance().LookupCached(c); };  // ALWAYS
is_land(i)     -> CleanupDiscardIsLand(ap.hand[i])
is_rock(i)     -> d->params.mana_rock
is_burst_rock  -> d->params.mana_rock && d->params.produces_amount >= 2      // Sol Ring
is_slow_rock   -> d->params.mana_rock && d->params.enters_tapped             // Fire Diamond
is_reducer(i)  -> IsSubtypeCostReducer(*d)                                   // Stinkdrinker Daredevil
is_blank(i)    -> d->params.damage_all_creatures > 0                         // Pyroclasm
is_haste(i)    -> d->params.equip_grants_haste                               // Lightning Greaves
is_reach(i)    -> d->params.damage > 0 && (d->params.targeting == Targeting::Any
                                        || d->params.targeting == Targeting::Player)
is_threat(i)   -> d->card.IsCreature() && !is_reducer(i)                      // every Giant
```

**Partition test order matters:** `is_blank` → `is_reducer` → `is_land` → `is_rock` → `is_haste` →
`is_reach` → `is_threat` → `rest`. The reducer must precede the creature test (Daredevil is a
creature); `is_threat` deliberately needs no tribe at all, which is more robust than Dragons'
`mv_of(i) >= 4` fallback for a visible-tribe-less hand — on this deck that fallback would misclassify
Tectonic Giant (MV 4) as a payoff and Lightning Greaves (MV 2) as not one, for the wrong reasons.

`threat_rank(i)`, in this order, first match wins:

```
0  p.attack_trigger_damage_any > 0                      Inferno Titan
1  p.power_bonus > 0 && !p.subtypes_affected.empty()     Sunrise Sovereign
2  !p.static_self_pump_per_other_subtype.empty()         Borderland Behemoth   (P4 adjusts)
3  p.attack_trigger_modal                                Tectonic Giant
4  p.attack_sac_fling                                    Surtland Flinger
5  p.any_creature_enters_self_counters_power              Hamletback Goliath
6  p.tutor_to_top                                        Giant Harbinger
7  (default -- unreached on this decklist)
```

Ties inside a rank: higher power, then higher MV (so the cheapest body sheds first among equals).

Levers to expose alongside the default-on `MTG_GIANTS_BUCKET_DISCARD`, one per open judgement call, so
the doubts below are measurable rather than argued:

| lever | what it changes |
|---|---|
| `MTG_GIANTS_DISCARD_MANA5` | mana target 5 instead of 6 (doubt D2) |
| `MTG_GIANTS_DISCARD_DIAMOND_EARLY` | promote a Fire Diamond above surplus Mountains while the mana quota is unmet (D3) |
| `MTG_GIANTS_DISCARD_HARBINGER_HIGH` | swap Giant Harbinger above Hamletback Goliath (D4) |
| `MTG_GIANTS_DISCARD_HAND_REDUCER` | let a HELD, castable Daredevil discount the curve, not only a resolved one (D5) |
| `MTG_GIANTS_DISCARD_THREAT3` | threat floor 3 hard instead of 2 + 1 soft (D6) |
| `MTG_GIANTS_DISCARD_BINARY` | replace the EV product with Minotaur's V1 binary far-flag, as the A/B control for P3 |

All must be read with `EnvOn(...)` per the coding-conventions skill — never a presence-only `getenv`
test, and never a `> 0` test on a param whose default is 1 (the Dragons classifier bug:
`reduces_spell_subtype_amount` defaults to 1 on **every** card in the database, which once classified
seven of an eight-card hand as cost reducers).

---

## 8. Doubts — flagged for the user to review and amend

**D1 — Pyroclasm as a blank is a statement about the OPPONENT MODEL, not about the card.** Everything
in P1 is verifiable in this engine, and in a real game Pyroclasm is a real card. Shedding it first is
correct for the metric this repo optimises and would be wrong the moment blocking exists. I propose
shipping it, keyed on the param and documented at the code, with the same standing as deferrals
D1/D2/D3 ("inert against the passive goldfish; a future 1v1 mode must revisit"). The alternative —
treating it as a sweeper the deck wants — would be wrong *today* for a hypothetical benefit. **Your
call on whether that trade is acceptable.**

**D2 — the mana target: 6, or 5?** Six is the number that makes the whole deck castable with one
Daredevil, and the reducer credit floors it at 4. Dragons was user-ruled at 4-5 for a *higher* curve,
but with six reducers and four rocks. Too high costs a threat in a flooded hand; too low strands the
hand. `MTG_GIANTS_DISCARD_MANA5` exists to settle it.

**D3 — Fire Diamond's rank is turn-dependent and I have ranked it statically (last among mana).** It
is genuinely acceleration on turn 2 (it beats the one-drop-per-turn cap) and genuinely a blank on turn
5 (it costs {2}, enters tapped, and the game ends on turn 6). I did not invent a turn constant for it;
the quota absorbs most of the effect. The lever is named.

**D4 — Hamletback Goliath above Giant Harbinger is the pair I am least sure of.** Value-when-resolved
says the 6/6 that grows beats the 3/4 (my ranking). The learned `card_scores` say the opposite
(−0.348 vs −0.137), and the Harbinger argument is real: 24 of 60 cards are Mountains, so converting
the next draw into the best Giant in the library is worth a great deal. My counter is that the
Harbinger's selection is at its *minimum* at a full hand, which is exactly when this rule runs.
Measurable via `MTG_GIANTS_DISCARD_HARBINGER_HIGH`.

**D5 — should a HELD Daredevil discount the curve, or only a resolved one?** I propose board-only for
V1, which is the conservative reading. Minotaur's equivalent arm measured +0.000081 at t=1.94 over
160,000 games — barely distinguishable in either direction — so there is no strong precedent either
way, and Giants' reducer is a 4-of (more likely to be the held card).

**D6 — the threat floor of 2 + 1 soft is inherited from a user ruling about a different deck.** The
Dragons reasoning transfers ("a hand of five Giants and two lands does nothing") and this deck's curve
is, if anything, *more* expensive per threat. But Giants differs from Dragons in one way that argues
the other direction: **its threats are synergistic rather than redundant.** Borderland Behemoth gets
+4/+4 for each other Giant, Sunrise Sovereign pumps every other Giant and stacks, and Hamletback
Goliath grows off each one entering. So a second and third Giant are worth more here than a second and
third Dragon. I still propose 2 + 1, because 95.5% of these sheds happen land-light where the extra
Giant is uncastable — but this is the quota most likely to want raising, and the lever is named.

**D7 — the deep ladder slots (10-14) are reasoned, not measured.** Slots 1-7 are load-bearing on every
7-card keep; the tail only orders the keep-tail giveaway. I have placed REACH at 11 (so a Bolt outlives
a fifth Mountain but not a fourth) on the argument that a sixth mana source arrives on turn 6. This is
the part of the design a behavioural diff should settle rather than argument.

**D8 — one asymmetry the engine has and the ranking cannot see.** Sunrise Sovereign's and Borderland
Behemoth's **trample is structurally inert** (approved deferrals D1/D2: `Keyword::Trample` has no read
site and `Combat.cpp` has no blocker path), so both cards are modelled slightly *worse* than they
really are, while Inferno Titan, Tectonic Giant and Surtland Flinger are modelled in full. My value
order therefore sits on a board where two of the seven Giants are under-credited. It does not change
any ordering I propose (both still rank 1 and 2), but it is the `[bracket note]` asymmetry the brief
asks to be surfaced rather than hidden.

---

## 9. Integration constraints — READ BEFORE LANDING ANY CODE

**C1 — THE GIANTS MULLIGAN GENERATION IS IN FLIGHT AND THIS CHANGE WOULD COST IT.** The exhaustive
keep-table generation was handed to the secondary machine on 2026-09-23 at freeze **`aaad47b3`**, ~6.9 h
(`analysis-Giants.md`, "HANDOFF"). Two independent problems:

1. **Resumability.** Both resume paths gate on play identity, and a rebuilt binary is *refused loudly*.
   A `src/ai/DecisionProviders.cpp` edit moves `HEAD:src`, so the freeze no longer matches.
2. **Worse, it is a policy mismatch.** The keep generator plays every candidate keep out through
   rollouts, and those rollouts consult **this ranking**. A keep table fitted under the max-MV fallback
   and then shipped alongside a bucket policy has been fitted to a play policy the deck no longer runs.

So: **do not land this until the Giants keep table is generated, gated and committed** — or accept
explicitly that the table will be regenerated afterwards. Authoring this document costs the run
nothing; integrating it does.

**C2 — the value leaf was fitted under the old ranking.** `Giants.value.json` was adopted 2026-09-23
(−0.00312 t, t = −4.08, 0.14x cost) on rollouts that used max-MV sheds. Changing the ranking shifts the
rollout leaves the model approximates. Non-inferiority must therefore be measured **with the sidecar in
place** (the shipped configuration, which is also what the census above ran), and a value-leaf
regeneration is a legitimate deferred follow-up if the policy is adopted — not a blocker.

**C3 — the `GiantsProvider` class comment becomes false.** It currently reads *"an EMPTY DeckProvider
derivation: per the always-own-a-provider rule the deck gets a name and a certificate answer, and per
the 'no unmeasured narrowing' rule it gets no heuristics at all until one is proposed and measured"*
(`DecisionProviders.h:263`, and the matching note in `SelectDecisionProvider`). Shipping this is that
proposal-and-measurement; both comments need updating in the same commit, exactly as Minotaur's and
Dragons' did. `Certificate()` is unaffected and stays `NotAssessed`.

**C4 — `required_pieces` is empty** in `Giants.profile.json`, so no card here is protected and
`InterchangeableRequiredGroup` needs nothing. The staged-card exemption still matters: **Tectonic
Giant's mode B stages a card into hand** (`m_is_staged`, expiry turn+1), and staged cards are in exile
and cannot be shed at all. The partition must `continue` on `m_is_staged`, as both reference providers
do, and `CleanupDiscardRankingWithOrder` enforces it independently.

## 10. Validation plan and the honest expectation

The bar is **non-inferiority plus doctrine quality**, per the brief — the payoff is rollout fidelity,
not a headline number.

1. **Behavioural diff first** (`test/tools/discard_behaviour_diff.py`, paired within a game, stopped at
   each game's first divergence). It costs seconds and it is where Minotaur's round 4 earned everything
   it earned. Expect the signature to be: stops shedding Inferno Titan and Sunrise Sovereign, starts
   shedding Pyroclasm, surplus Mountains and spare Greaves.
2. **`MTG_TRACE=discard` will show NOTHING at either searched depth** (real = 0 at d3 and d5,
   measured above; the trace is gated on `g_real_resolution`). Do not read that as the
   rule being inert, and do not spend time hunting for the trace lines. Diff at d0, where real sheds
   exist (11 per 1000 games), and rely on the behavioural diff for the rollout channel.
3. **Paired outcome A/B** on the `=0` hatch, d0 and d3, fresh seeds disjoint from 1001/2002/3003 and
   from 770000-770100 (the sweep's). Pool it into ONE `mtg --batch` queue; no per-arm waves.
4. **Smoke + regression** through the accept flow, with per-difference verdicts, then
   `python3 test/check_gt_logs.py`.
5. **The skill's rule-vs-searched zero-regret check will not run here** for the same reason it does not
   run on Minotaur: the labeller probes only the CR 514.1 cleanup, and this deck reaches it **zero**
   times at searched depth. Record it as **UN-RUN**, never as passed. The behavioural diff plus the d0
   channel is the honest substitute.

**What I expect:** avg win turn barely moves. The deck sheds ~0 times per real game, so the metric can
only see this through the search's rollouts, and there it changes which lines the search *believes* are
good rather than which cards a player pitches. The two things that make it worth shipping anyway are
that the current fallback is **actively inverted** on this decklist (not merely arbitrary), and that
81,594 rollout decisions per 200 games are currently being made by "shed the most expensive card" in a
deck whose entire plan is to cast the most expensive card.
