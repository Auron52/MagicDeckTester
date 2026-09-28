# Prevent Damage — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-28)

Authored from `docs/design/discard-bucket-authoring-brief.md` (analyze-deck §5i). **Proposal only; no
`.cpp`/`.h` was touched, and nothing was built or run.** The shipped form would be
`PreventDamageProvider::CleanupDiscardCandidates`. It returns a shed order (most expendable first) through
`CleanupDiscardRankingWithOrder(s, required_pieces, shed)`, so the staged-card and required-piece
protections stay engine-enforced. It sits behind a default-on `EnvOn("MTG_PD_BUCKET_DISCARD", true)`, and
`=0` restores `GenericProvider::CleanupDiscardCandidates` as the A/B hatch. The provider already exists
(`PreventDamageProvider`, `src/ai/DecisionProviders.{h,cpp}`). It has no discard override today, so it
runs the shared fallback. This is one new override.

Every card claim below was read from `src/cards/data/cards.json` in the `/tmp/pd-wt` worktree (Rule 0).
That includes all 13 sideboard names, because Living Wish reaches the sideboard and a wished card can
be in hand at cleanup.

---

## 0. Evidence: what the status quo does, and what is known about it

**The status quo is pure max-MV.** `GenericProvider::DiscardLandsFirst` is false, so the fallback goes
straight to tier B, which sorts by descending mana value. On this deck that ranking is backwards in
three separate ways:

* **Beseech the Queen is MV 6** (`{2/B}{2/B}{2/B}`, CR 202.3f; the twobrid parser fix of 2026-09-27
  made every reader see 6). Beseech therefore sheds first from any hand that holds it, even though it
  casts for `{B}{B}{B}` and is the deck's only unrestricted tutor.
* **Lands are MV 0, so they are the last thing shed.** A seventh land is kept over a Tamanoa.
* **The MV-4 cards are three unrelated roles:** Manabarbs (fuel), Pyrohemia (burn), and Rhox
  Faithmender (amplifier). Hand order decides between them.

**The analyzer's Stage 4 discard evidence** (`logs/prevent_damage/stage4.json`, `discard_analysis`) is as
follows. The run was 400 games at d3 and produced only **14 real cleanup decisions**, all at hand size 8,
with `multi_optimal_rate 1.0`. The status quo's label regret is **0.143** (85.7% optimal). The derived
order `Spellshock; Manabarbs; Pyrohemia; Beseech` reached 0.071 regret but was A/B-neutral
(d3 −0.00125, t −0.23), and the verdict was `NO_RULE_CONSIDER_SEARCH`. Per-name labels, where regret is
the cost of shedding that card when it was offered:

| name | offered | shed-optimal | mean regret |
|---|---:|---:|---:|
| Spellshock | 9 | 9 | 0.000 |
| Manabarbs | 10 | 10 | 0.000 |
| Pyrohemia | 7 | 7 | 0.000 |
| Beseech the Queen | 8 | 7 | 0.125 |
| Rolling Earthquake | 6 | 5 | 0.167 |
| Living Wish | 10 | 8 | 0.200 |
| Vito | 7 | 5 | 0.286 |
| Tamanoa | 9 | 6 | 0.333 |
| Green Sun's Zenith | 6 | 4 | 0.333 |

The opening-hand `card_scores` in `Prevent Damage.profile.json` (first copy) rank the cards the same way:

* Engine pieces and tutors are positive: Tamanoa +0.37, Vito +0.30, Living Wish +0.24, GSZ +0.17,
  Dina +0.07.
* The fuel cards are negative: Rolling Earthquake −0.13, Manabarbs −0.21, Spellshock −0.23,
  Pyrohemia −0.45.
* Rhox Faithmender is −0.26 and Beseech −0.05.
* Among lands, Ancient Tomb is highest at +0.28, then City +0.24, Citadel +0.21, Coliseum +0.20,
  Reflecting Pool +0.01, Brushland +0.00, Forge −0.08, Karplusan −0.11.

These scores come from opening-hand group means, so they are confounded with castability. I use them
as a cross-check only; the order below is authored.

**Two things are UN-RUN, deliberately** (instruction: another agent holds the box and the source files):

1. `MTG_SHED_STATS` rollout census. Its denominator is unknown. The d0 greedy plays this deck badly
   (31.7% won in Stage 5b), which suggests the rollout reaches cleanup with 8+ cards far more often than
   real play's 14 per 400. That is only suggestive, not measured.
2. `test/tools/discard_behaviour_diff.py`.

Both are the first things integration should run (§9).

---

## 1. The deck in one paragraph, and therefore its shape

**This is a combo deck with an unusual shape: the damage is fuel, and the lifegain is converted into
the kill.** The deck damages itself on purpose:

* painlands, City of Brass, Tarnished Citadel, Grand Coliseum and Ancient Tomb on the tap;
* Manabarbs on every land tap;
* Spellshock on every cast;
* Pyrohemia and Rolling Earthquake, which hit every creature and every player.

A **gain engine** (Tamanoa: "whenever a noncreature source you control deals damage, you gain that
much"; or Purity from the sideboard) turns each of those damage events into a lifegain event. A
**drain payoff** turns each lifegain event into opponent life loss: Vito loses them *that much*; Dina
loses them *1 per event*. An **amplifier** scales the gain: Rhox Faithmender ×2, or Bilbo +1 per event
from the sideboard.

Three tutors assemble the pieces:

* Green Sun's Zenith puts a green creature onto the battlefield, which here means only Tamanoa or Dina.
* Living Wish fetches a creature or land from the sideboard.
* Beseech the Queen fetches any card with MV ≤ lands.

The lands are both the mana and part of the fuel.

So the brief's combo row applies: one bucket per engine part (**GAIN / DRAIN / AMP / FUEL**), similar
effects grouped, plus a **DIG** bucket for the tutors and **MANA** (lands only: the deck runs no dorks or
rocks). There are six buckets, plus an unprotected **OTHER** class for the wishable sideboard bodies. The
count is high because the deck really has four distinct engine slots. A Tamanoa with no drainer does
nothing to the opponent. A Vito with no gain engine does nothing without Faithmender's lifelink combat
or Vito's team-lifelink pump. Fuel with no gain engine only damages us.

---

## 2. Card-by-card role table (every main + sideboard card, from `cards.json`)

| card | zone (n) | cost (MV) | `params` keyed on | bucket |
|---|---|---|---|---|
| City of Brass | main 4 | — | `produces [WUBRG]`, `tap_self_damage 1` | MANA (5-colour, untapped) |
| Tarnished Citadel | main 4 | — | `produces [WUBRGC]`, `tap_self_damage 3` | MANA (5-colour, untapped) |
| Grand Coliseum | main 4 | — | `produces [WUBRGC]`, `tap_self_damage 1`, `enters_tapped` | MANA (5-colour, tapped) |
| Ancient Tomb | main 4 | — | `produces [C]`, `produces_amount 2`, `tap_self_damage 2`, `tap_self_damage_any_mode` | MANA (2 colourless) |
| Battlefield Forge | main 3 + side 1 | — | `produces [RWC]`, `tap_self_damage 1` | MANA (2-colour) |
| Brushland | main 2 + side 1 | — | `produces [GWC]`, `tap_self_damage 1` | MANA (2-colour) |
| Karplusan Forest | main 1 | — | `produces [RGC]`, `tap_self_damage 1` | MANA (2-colour) |
| Reflecting Pool | main 4 | — | `reflecting true` (static `produces` ignored) | MANA (conditional: **nothing** alone) |
| Tamanoa | main 3 + side 1 | `{R}{G}{W}` (3) | `noncreature_damage_lifegain` | **GAIN** |
| Purity | side 1 | `{3}{W}{W}{W}` (6) | `prevent_noncombat_to_self_gain`, `graveyard_replace_shuffle_library` | **GAIN** |
| Vito, Thorn of the Dusk Rose | main 3 + side 1 | `{2}{B}` (3), Legendary | `lifegain_target_opp_loses_that_much`, `team_pump_grants_lifelink` | **DRAIN** (by amount) |
| Dina, Soul Steeper | main 2 + side 2 | `{B}{G}` (2), Legendary | `lifegain_each_opp_loses 1`, `sac_creature_outlet` | **DRAIN** (per event) |
| Rhox Faithmender | main 2 + side 2 | `{3}{W}` (4) | `lifegain_multiplier 2`, Lifelink | **AMP** |
| Bilbo, Birthday Celebrant | side 1 | `{W}{B}{G}` (3), Legendary | `lifegain_plus 1`, `life_gated_put_creatures_cost` | **AMP** |
| Rolling Earthquake | main 4 | `{X}{R}` (1) | `x_damage_each_creature_and_player` | **FUEL**, burst (also direct burn) |
| Manabarbs | main 4 | `{3}{R}` (4) | `land_tap_damage_each_player 1` | **FUEL**, passive (per land tap) |
| Spellshock | main 3 | `{2}{R}` (3) | `on_cast_trigger_damage 2`, `on_cast_trigger_any_mv` | **FUEL**, passive (per cast) |
| Pyrohemia | main 1 | `{2}{R}{R}` (4) | `ping_all_cost {R}`, `ping_all_amount 1`, `endstep_sac_if_no_creatures` | **FUEL**, ping (also direct burn) |
| Living Wish | main 4 | `{1}{G}` (2) | `tutor_to_hand`, `tutor_types [Creature, Land]`, `wish_from_sideboard`, `exiles_self_on_resolve` | **DIG** (rank 0) |
| Green Sun's Zenith | main 4 | `{X}{G}` (1) | `tutor_to_battlefield_single`, `tutor_mv_max_is_x`, `tutor_types [Creature]`, `tutor_color G` | **DIG** (rank 1) |
| Beseech the Queen | main 4 | `{2/B}×3` (6) | `tutor_to_hand`, `tutor_types []`, `tutor_max_mv_is_lands` | **DIG** (rank 2) |
| Timeless Witness | side 1 | `{2}{G}{G}` (4) | `etb_return_gy_to_hand`, `eternalize_cost` | OTHER (best) |
| Dimir House Guard | side 1 | `{3}{B}` (4) | `transmute_cost {1}{B}{B}` | OTHER |
| Vexing Shusher | side 1 | `{R/G}{R/G}` (2) | none (vanilla 2/2 here) | OTHER |
| Shriekmaw | side 1 | `{4}{B}` (5) | `etb_destroy_nonartifact_nonblack`, `evoke_cost {1}{B}` | OTHER; **NEG** when the opponent has no creature |
| Acidic Slime | side 1 | `{3}{G}{G}` (5) | `etb_destroy_artifact_enchantment_land` | **NEG** (always hits our own permanent) |

**Roles that are genuinely ambiguous, stated rather than hidden:**

* **Rolling Earthquake and Pyrohemia** are fuel *and* the deck's only direct burn to the opponent.
  Rule 5 of the brief says to group similar effects, and both are "damage each creature and each
  player", so they sit in FUEL with their own sub-rank. They are not a separate FINISHER bucket.
  Earthquake is a sorcery, so the board can never fill its slot (see §3).
* **Rhox Faithmender** is an amplifier only for Vito's "that much". Dina drains per event, so a
  doubled amount is invisible to her. Without Vito, Faithmender is a 1/5 lifelink whose combat damage
  is a gain event, and a survival tool (our life is the fuel). That is why AMP is **conditional** (§3).
* **Vito's team-lifelink pump** (`{3}{B}{B}`) is a mana sink as well as a drain. It is not counted as a
  sink for the land target because it is idempotent (K capped at 1) and five lands already pay for it.
* **Beseech** is MV 6 on paper but costs 3 with three black sources. Its distance term uses the
  effective cost (§4).
* **Green Sun's Zenith** reaches only GAIN (Tamanoa) and DRAIN (Dina) in this list. Faithmender is
  white and Vito black. GSZ searches the library, never the sideboard.

**Stale bracket notes found while reading (no model impact; flag for whoever next edits the entries):**

1. **Living Wish's `oracle_text`** says *"BOTH win conditions live in the sideboard it fetches (Essence
   Depleter, Dimensional Infiltrator)"*. That was written for another deck. Neither card is in Prevent
   Damage's sideboard.
2. **Vexing Shusher's note** says it is *"badly dominated by the sideboard's two {C} mana sinks"*. That
   is the same other-deck sideboard, and there is no `{C}` sink here.
3. **`PreventDamageProvider`'s class comment** says `TutorSearchWidth 16`, but the override returns
   **20** (its own member comment explains the raise).

---

## 3. The buckets and quotas, each stated net of board

Census the controller's battlefield first; the hand owes only the remainder.

| bucket | quota (net of board) | what fills it from the battlefield |
|---|---|---|
| **MANA** (lands) | **5 lands in total**, or **6** while a scalable sink is available (a `ping_all_cost` permanent on board, or an `x_damage_each_creature_and_player` / `ping_all_cost` card in hand). Hand owes `target − board_lands`. | every own land |
| **GAIN** | **1 hard + 1 soft**. The 2nd copy is real value: each Tamanoa triggers separately, so two Tamanoa double the event count that Dina drains per event. | own `noncreature_damage_lifegain` / `prevent_noncombat_to_self_gain` permanents |
| **DRAIN** | **1 hard + 1 soft for the OTHER name**. Vito and Dina stack; a second copy of the same legend does not. | own `lifegain_target_opp_loses_that_much` / `lifegain_each_opp_loses` permanents, **counted by distinct name** |
| **AMP** | **1**, and only while a Vito-type drainer (`lifegain_target_opp_loses_that_much`) is on board or kept in a DRAIN slot; otherwise **0** | own `lifegain_multiplier > 1` / `lifegain_plus > 0` permanents |
| **FUEL** | **1 always + 1 more while no fuel permanent is on board**. Earthquake is a sorcery, so the first slot can never be filled from the board. | own `land_tap_damage_each_player` / `on_cast_trigger_damage` / `ping_all_cost` permanents (second slot only) |
| **DIG** | **1**, plus tutors drafted as **wildcards** into an empty GAIN1 / DRAIN1 slot (below) | n/a (tutors are spells) |
| OTHER / NEG | **0**: never protected | n/a |

**Sub-splits (fungible upward; the parent total binds, per rule 3):**

* **MANA, colour coverage first.** The first lands kept are the minimal set that, together with the
  board, covers every colour the hand's nonland cards need. Only after that is the remaining land
  order by mana amount. Ancient Tomb (2 colourless) outranks a 5-colour land *only* once colours are
  covered. That is the ledger's own unwon class: *"Tomb ×2 + Forge, no G/B source"* (Stage 5b g11), and
  *"Pool + Ancient Tomb (no coloured source)"* (g283).
* **FUEL: burst > passive > ping** (§4). Passive fuel (Manabarbs, Spellshock) counts as fuel only while
  a **gain engine is available**: on board, held, or reachable by a held tutor. Without one, those two
  cards do nothing but damage us, and they sort behind Pyrohemia.
* **GAIN / DRAIN wildcards.** When GAIN1 or DRAIN1 has no real hand candidate, the best **tutor that can
  reach that role** fills it. This is how "similar effects grouped" meets "tutors are the missing
  piece". A tutor fills at most one slot.

**Why the quotas are loose.** GAIN 2 + DRAIN 2 + AMP 1 + FUEL 2 + DIG 1 + lands is more than an 8-card
hand. So on a fresh hand, the shed is usually decided by the **tail of the keep ladder** (§5), not by a
quota breach, which is the CritterLifegain situation. The net-of-board term does most of the work,
because the deck assembles its engine by T3–4 and wins T5–6 (Stage 5b d5 kill turn: T5 116 / T6 120).

---

## 4. Within-bucket order, and the distance term

Best kept **first**; the shed order within a bucket is this list reversed.

* **MANA.**
  1. **Greedy colour cover.** Repeatedly take the land that adds the most still-missing needed colours,
     where needed = the coloured pips of every non-staged nonland hand card (twobrid pips excluded; a
     hybrid pip is satisfied by either colour). Ties fall to steps 2–6.
  2. A **live** reflecting land always ranks below every non-reflecting land.
  3. `produces_amount` descending (Ancient Tomb 2).
  4. Untapped before `enters_tapped` (Grand Coliseum last among five-colour lands).
  5. Colour count descending (WUBRG only; `C` does not count).
  6. Hand index.

  A **dead** Reflecting Pool is not in this order at all; it is S0 (§5). A Pool is dead when no
  non-reflecting land exists on board **or** in hand: `reflecting` produces nothing when it is the
  only land type (the card's own bracket note).
* **GAIN.** `noncreature_damage_lifegain` (Tamanoa, 3 mana) before `prevent_noncombat_to_self_gain`
  (Purity, 6 mana with WWW), then lower effective cost.
* **DRAIN.** `lifegain_target_opp_loses_that_much` (Vito) before `lifegain_each_opp_loses` (Dina). Vito
  drains at least as much as Dina on every event of size ≥ 1, and he is what AMP multiplies. The labels
  agree: Vito regret 0.286; the card scores agree too (+0.30 vs +0.07).
* **AMP.** `lifegain_multiplier` (×2) before `lifegain_plus` (+1).
* **FUEL.** `x_damage_each_creature_and_player` (Earthquake: burst, X-scalable, hits the opponent) >
  `land_tap_damage_each_player` (Manabarbs) > `on_cast_trigger_damage` (Spellshock) > `ping_all_cost`
  (Pyrohemia). With no gain engine available, the two passive cards drop below Pyrohemia.
  * Earthquake is on top because it is the one fuel card whose labels show any keep-value (0.167).
  * Manabarbs beats Spellshock because the deck taps 4–6 lands a turn but casts 1–3 spells, and Dina
    drains per event. The card scores agree narrowly (−0.21 vs −0.23).
  * Pyrohemia is last: `{2}{R}{R}`, it kills our own Vito/Dina after three pings (the card's own note),
    it self-sacrifices with no creatures, and it has the deck's worst card score (−0.45).
* **DIG.** `wish_from_sideboard` (reaches every engine part including Vito, Purity and Faithmender, and
  lands) > `tutor_to_battlefield_single` (GSZ: Tamanoa/Dina straight onto the battlefield) > other
  `tutor_to_hand` (Beseech). This is a param-level **value order** among cards of one role, and it
  follows the card scores (+0.24 / +0.17 / −0.05) and the labels (Beseech is the most sheddable tutor).
* **OTHER** (kept best first): `etb_return_gy_to_hand` > `transmute_cost` > anything else (the vanilla
  Shusher, or a Shriekmaw that has an opposing creature to kill).

**DISTANCE-TO-PLAYABLE** uses the Pirates shape, which is bounded:

```
reach     = Σ board lands' mana (produces_amount, min 1; a reflecting land counts 0 when dead)
          + Σ hand lands' mana (same rule)
avail     = colour bits of board lands ∪ hand non-reflecting lands (a live Pool adds nothing new)
distance  = max(0, eff_cost − reach) + (needed colour bits ⊄ avail ? 1 : 0)
far       = distance ≥ 2
```

`eff_cost` is the MV from `CleanupDiscardManaValue` with three exceptions:

* **Twobrid** (`twobrid_count > 0`, Beseech): `2·twobrid_count − min(twobrid_count, b_sources)` plus any
  other generic, where `b_sources` = board + hand lands that produce `B` (a live Pool counts if `avail`
  has B). Beseech costs 3 with three black sources and 6 with none.
* **GSZ** (`tutor_to_battlefield_single && tutor_mv_max_is_x`): `1 + min MV` over the GAIN/DRAIN cards it
  can reach, so Dina gives 3 and Tamanoa gives 4. With nothing in reach it is dead (S0).
* **Earthquake**: its printed MV (1). It is always castable, and the X decision belongs to the search.

**Distance orders only *inside* a bucket** (brief rule 4). A slot takes a non-far candidate before a far
one, and an overflow list sheds far cards before near ones. It never moves a card across buckets. The
deck has **no cost reducer**, so no promotion ever erases distance.

---

## 5. The total order over a hand (index 0 is always determined)

**The interleaved keep ladder.** Read forwards, it is the quota fill. Read backwards, it is the tail of
the shed order.

```
LAND > GAIN1 > LAND > DRAIN1 > LAND > DRAIN2 > LAND > DIG1 > FUEL1 > GAIN2 > AMP1 > LAND > FUEL2 > LAND
```

Which slots are active:

* A `LAND` slot takes a land only while `land_need = land_target − board_lands` is positive, so the
  owed lands take the earliest `LAND` positions.
* `GAIN1` is active iff `board_gain == 0`. `GAIN2` is active iff `board_gain ≤ 1`, and only a real GAIN
  card fills it.
* `DRAIN1` is active iff no drainer is on board. `DRAIN2` is active iff fewer than 2 distinct drainer
  names are on board. Each slot takes a name that is not on board and not already kept.
* `DIG1` and `FUEL1` are always active. `FUEL2` is active iff no fuel permanent is on board.
* `AMP1` is active iff `board_amp == 0` and (`board_vito` or a Vito-type card was just kept in a DRAIN
  slot).

Slot fill: take the best remaining candidate of that bucket, **non-far first, then the §4 value order**.
For `GAIN1` / `DRAIN1` the candidate order is:

1. a non-far real card;
2. a non-far tutor that reaches the role (tutor rank order);
3. a far real card;
4. a far tutor.

Why the ladder looks like this:

* **The first land beats everything.** A missed drop is unrecoverable on a curve of 3–4-drops that
  need three colours.
* **GAIN1 comes before DRAIN1**, because the drainer is inert without a gain event, while Tamanoa turns
  the lands' own pain into gains immediately.
* **The second drainer (the other name) comes before DIG, FUEL and AMP.** Vito's labels (0.286) and
  card score say shedding a drainer is the most frequent real mistake after Tamanoa.
* **FUEL1 sits below DIG1.** Three evidence sources (every fuel label at 0 regret, negative card scores,
  and the analyzer's own derived order) say fuel is the most sheddable real card. The painlands already
  supply fuel.
* **The last land slot is only active with a sink**, where a sixth land is +1 Earthquake X or +1
  Pyrohemia ping per turn.

**Shed order, most expendable first.** It names every non-staged hand card, so nothing falls through
to max-MV.

| tier | contents | order inside |
|---|---|---|
| **S0 DEAD** | (a) a legendary whose name is already on **our** battlefield (a 2nd Vito, Dina or Bilbo dies to the legend rule on resolution); (b) a tutor with **no legal target** in its zone; (c) a **dead** Reflecting Pool | a, b, c; then hand order |
| **S1 NEG** | `etb_destroy_artifact_enchantment_land` (Acidic Slime: the goldfish has no such permanent, so it always destroys ours); `etb_destroy_nonartifact_nonblack` while the opponent controls **no creature** (Shriekmaw must then kill one of our nonblack creatures, and Tamanoa and Faithmender are nonblack) | Slime first |
| **S2 SURPLUS LANDS** | lands not taken by a LAND slot | §4 land order, reversed |
| **S3 OTHER** | unprotected sideboard bodies | vanilla/other → transmute → gy-return |
| **S4 OVERFLOW** | bucket cards not taken by any slot | bucket precedence **FUEL → DRAIN (same-name backup) → AMP → DIG → GAIN**; inside a bucket, far before near (larger distance first), then the §4 order reversed |
| **S5 KEPT** | every slot-kept card | reverse acquisition order (the last slot filled yields first) |

The S4 bucket precedence mirrors the ladder: the extra copies of what the deck needs least go first. A
same-name drainer backup is live only after the first copy dies. In this deck the only things that kill
our own Vito or Dina are our own Earthquake (X ≥ 3), Pyrohemia (3 pings) and a Shriekmaw that has no
other target. So the backup ranks below an extra amplifier.

---

## 6. Param-level classification predicates (integration is mechanical)

**No card names are used anywhere.** Every read goes through `CardDatabase::Instance().LookupCached(card)`,
because a hand card is a name-only placeholder. `p` = `def->params`, `c` = `def->card`.

```
is_land(i)     := CleanupDiscardIsLand(hand[i])
reflecting(d)  := p.reflecting
land_mana(d)   := max(1, p.produces_amount)                   // Ancient Tomb -> 2
land_bits(d)   := OR of colour_bit(x) for x in p.produces     // C ignored; reflecting -> 0

GAIN(d)        := p.noncreature_damage_lifegain || p.prevent_noncombat_to_self_gain
DRAIN(d)       := p.lifegain_target_opp_loses_that_much || p.lifegain_each_opp_loses > 0
VITO(d)        := p.lifegain_target_opp_loses_that_much
AMP(d)         := p.lifegain_multiplier > 1 || p.lifegain_plus > 0
FUEL(d)        := p.x_damage_each_creature_and_player || p.land_tap_damage_each_player > 0
                  || p.on_cast_trigger_damage > 0 || p.ping_all_cost.has_value()
FUEL_RANK(d)   := x_damage_each_creature_and_player ? 0 : land_tap_damage_each_player > 0 ? 1
                  : on_cast_trigger_damage > 0 ? 2 : 3        // +3 for ranks 1-2 when !gain_available
FUEL_PERM(d)   := p.land_tap_damage_each_player > 0 || p.on_cast_trigger_damage > 0
                  || p.ping_all_cost.has_value()               // board census for FUEL2
SINK(d)        := p.x_damage_each_creature_and_player || p.ping_all_cost.has_value()
DIG(d)         := p.tutor_to_hand || p.tutor_to_battlefield_single
DIG_RANK(d)    := p.wish_from_sideboard ? 0 : p.tutor_to_battlefield_single ? 1 : 2
NEG(d)         := p.etb_destroy_artifact_enchantment_land
                  || (p.etb_destroy_nonartifact_nonblack && !opp_has_creature)
OTHER_RANK(d)  := p.etb_return_gy_to_hand ? 0 : p.transmute_cost.has_value() ? 1 : 2
DEAD_LEGEND(i) := c.HasSupertype(Supertype::Legendary) && an own Permanent has the same m_name
```

Classification precedence is **LAND > GAIN > DRAIN > AMP > FUEL > DIG > NEG/OTHER**, because Vito
also carries `team_pump_*` and Bilbo also carries `life_gated_put_creatures_cost`.

**Tutor reach (one zone pass per held tutor NAME, at most three).** Walk
`TutorZoneView(ap, p.wish_from_sideboard ? &ap.sideboard : nullptr)` with the shared legality predicates
(`CardMatchesTypeName` over `tutor_types`, `CardHasColorNamed(card, tutor_color)`,
`TutorNumericFilterOk`). Record `reach_gain`, `reach_drain`, `reach_amp`, `reach_any` and
`min_mv_engine`.

Beseech's cap is replaced by **`MV ≤ board_lands + (hand holds a land ? 1 : 0)`**. At cleanup the land
cap that matters is next turn's; `TutorLandCapSlack` reads the *current* turn's drop and returns 0 once
the drop is spent. `tutor_mv_max_is_x` is ignored for reach (X is chosen at cast). I do **not** propose
calling `GenericProvider::TutorCandidates` for this: it builds a name vector and a hash set per call.
This hook runs inside rollouts, and the deck already fails the 5j cost gate. The census only needs
booleans.

**Board census** (own permanents; opponent only for `opp_has_creature`):

* `board_lands`, `board_reach`, `board_bits`, `board_has_nonrefl_land`;
* `board_gain` (count), `board_drain_names` (distinct), `board_vito`, `board_amp`, `board_fuel_perm`,
  `board_sink` (a `ping_all_cost` permanent);
* the legendary names.

Then `gain_available := board_gain > 0 || a hand GAIN card || a hand tutor with reach_gain`.

---

## 7. State promotions

**Proposed.** Each is a plain state read that a cleanup ranking can act on.

* **R0: net of board, everywhere** (§3). A resolved Tamanoa demotes the hand Tamanoa to the soft GAIN2
  slot. A resolved Dina turns DRAIN2 into "keep a Vito". A resolved fuel permanent closes FUEL2.
* **R1: AMP is live only beside a Vito** (board or kept). Otherwise Faithmender/Bilbo are overflow.
* **R2: passive fuel is live only with a gain engine available.** Otherwise Manabarbs/Spellshock sort
  behind Pyrohemia. They only hurt us, and `MTG_PD_PAIN_PAY`'s whole reason to exist is that
  un-refunded pain loses games.
* **R3: +1 land while a scalable sink is available** (Earthquake held, or Pyrohemia held or on board).
* **R4: dead-card detection (S0)** for a duplicate legend, a target-less tutor, and a dead Reflecting
  Pool. The Pool case is the ledger's largest unwon class at the keep (g3, g266: two Pools). The same
  fact at cleanup is free to act on.
* **R5: Shriekmaw is NEG exactly while the opponent has no creature.** Its mandatory ETB then destroys
  one of ours.
* **R6: GAIN1/DRAIN1 tutor wildcards** (§3).

**Rejected as search-owned.** Each of these is a cast, payment or damage projection, not a fact about a
hand.

* *"Earthquake for X is lethal next turn, keep it over a land"*. That is a damage projection; the X
  axis is searched.
* *"Hold Pyrohemia because K pings with Vito + Faithmender out is a kill"*. Same projection, plus which
  of our creatures die. K is a searched axis (`ManaSinkActivationCounts`).
* *"Our life is low and there is no gain engine, so Manabarbs/Spellshock are suicide"*. The cast is
  already guarded (`MTG_PD_SELF_LETHAL_GUARD`, `PainAwarePay`). R2 covers the part a hand ranking can
  see honestly.
* *"Keep the painful land (Citadel's 3) over a painless one while Tamanoa is out, because its pain is a
  3-point gain event"*. This is real, but it is a mana-payment value claim and unmeasured (see Doubts).
  `ManaSourceRank` / `SelfDamageUseful` already own which land is tapped.
* *Which graveyard card Timeless Witness returns*, and *which MV-4 card House Guard transmutes for*.
  Both are searched axes.
* *"A 2nd Vito is castable for value under Spellshock + Tamanoa"* (`OfferDuplicateLegendCast`'s
  exception). That is the cast decision. As a hand card it is still a 3-mana 2-point event, below
  everything, so it stays S0.

---

## 8. Doubts: the user reviews and amends these

1. **FUEL placement.** Every fuel label is 0-regret and every fuel card score is negative, yet FUEL1 is
   still a protected slot above GAIN2/AMP1. The case for it: Earthquake is the one burst card (label
   0.167), and without any fuel the deck relies on land pain alone. A defensible alternative is to drop
   FUEL1 below AMP1, or to protect only a *burst* card (Earthquake/Pyrohemia) and never a passive one.
2. **Pyrohemia last among fuel.** Every piece of evidence says so, but its ceiling (one `{R}` =
   (creatures+2) gain → Vito × Faithmender) is the highest in the deck. If you read it as a finisher,
   swap it above Spellshock.
3. **Land target 5, or 6 with a sink.** The curve tops at 4 (Manabarbs/Pyrohemia/Faithmender/GSZ-for-
   Tamanoa) and Vito's pump needs 5. Under Manabarbs every extra land is also +1 event per turn, which
   argues for 6 flat. This is the most likely number to be amended, as Minotaur's was.
4. **Colour cover before Ancient Tomb** (test T8 sheds a Brushland and keeps the Tomb *after* Karplusan
   covers G). Tomb's +0.28 is the best land score, but that score is an opening-hand mean. The
   no-coloured-source keeps in the ledger are why colour comes first.
5. **Reflecting Pool ranks as the worst live land.** Its tap is not a pain event (only a Manabarbs
   event), and it is conditional. The alternative is to rank it by its runtime colour union.
6. **Overflow precedence: DRAIN same-name backup below AMP overflow** (test T6 sheds the 2nd Vito
   before Bilbo). Bilbo is +1 *before* the ×2 (the card's own note: (a+1)·2), so with Vito +
   Faithmender a 1-point Manabarbs event drains 4 instead of 2. That is strong, but Bilbo is only in
   hand if the search wished for it.
7. **Tutor order Wish > GSZ > Beseech** follows the card scores. GSZ is the cheaper route to a
   *resolved* Tamanoa (4 mana, one card, to battlefield) and has the higher label regret (0.333 vs
   0.200). A slot-specific order (GSZ for GAIN1, Wish for DRAIN1, since Wish reaches Vito) is the
   obvious refinement, but it is unmeasured.
8. **A painful-land preference with a gain engine** (rejected in §7) is the one plausible promotion I
   left out. If you want it, it is a single sort key (`tap_self_damage` desc when `gain_available`).
9. **The evidence base is tiny:** 14 real decisions in 400 games, and `multi_optimal_rate 1.0`
   means several options tie on most of them. Real play barely sheds. Per the brief that does not make
   the rule inert: the rollout and keep/bottom generation are the denominators. That is why the rollout
   census (§9 step 1) matters.
10. **Timing.** No value leaf or keep table exists for this deck yet (cost gate first). Adopting this
    **before** those generations is the right order, because it changes the rollout policy they would be
    fitted to. Adopting it after would leave them stale.

---

## 9. Implementation sketch (against the existing helpers)

**Header (`PreventDamageProvider`, `DecisionProviders.h`):**

```cpp
// Authored bucket cleanup-shed policy (analyze-deck §5i; docs/design/prevent-damage-discard-policy-
// proposal.md). MTG_PD_BUCKET_DISCARD (default ON); =0 -> GenericProvider::CleanupDiscardCandidates.
std::vector<int> CleanupDiscardCandidates(
    const GameState&, const std::vector<std::string>*) const override;
```

**Optionally**, add a heurarm slot `PD_BUCKET_DISCARD` to `src/ai/HeuristicArm.h` next to
`PD_PAIN_PAY` (and its name in the env list), so the A/B pools both arms in one batch the way the other
`MTG_PD_*` levers do.

**Body (`DecisionProviders.cpp`, beside the other `PreventDamageProvider::` methods):**

```cpp
std::vector<int> PreventDamageProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    static const bool s_env = EnvOn("MTG_PD_BUCKET_DISCARD", true);
    if (!heurarm::Flag(heurarm::PD_BUCKET_DISCARD, s_env))       // or just `if (!s_env)`
    { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }
    const int me = s.active_player_index;
    const Player& ap = s.players[me];
    const int n = static_cast<int>(ap.hand.size());
    if (n <= 0) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }
    auto def_at = [&](int i) { return CardDatabase::Instance().LookupCached(ap.hand[i]); };

    // 1. board census (own permanents; opp only for opp_has_creature) -- §6 fields.
    // 2. partition non-staged hand indices into lands / gain / drain / amp / fuel / dig / other,
    //    in precedence order; mark S0 dead legends (name on OUR battlefield) as they are met.
    // 3. per distinct held tutor NAME: one TutorZoneView pass -> reach_{gain,drain,amp,any},
    //    min_mv_engine (Beseech cap = board_lands + (hand holds a land)). reach_any == false -> S0.
    // 4. hand-land facts: live reflecting? (board_has_nonrefl_land || another nonrefl hand land);
    //    dead Pool -> S0. reach / avail / b_sources; eff_cost + distance per §4.
    // 5. land_target = 5 + sink; land_need = max(0, land_target - board_lands);
    //    land keep order = greedy colour cover, then §4 keys.
    // 6. walk the ladder (static array of Slot enums, exactly Minotaur's kFill shape) with the
    //    activity rules of §5; take(i) records keep[] and taken_order.
    // 7. emit: S0 dead, S1 NEG, S2 unkept lands (reverse land order), S3 OTHER (worst first),
    //    S4 overflow (FUEL, DRAIN, AMP, DIG, GAIN; far first, then reverse value), S5 reverse(taken_order).
    //    put() skips staged + already-listed, so the list is a permutation of the non-staged hand.
    return CleanupDiscardRankingWithOrder(s, required_pieces, shed);
}
```

The helpers used are `CleanupDiscardManaValue`, `CleanupDiscardIsLand`, `TutorZoneView`,
`CardMatchesTypeName`, `CardHasColorNamed`, `TutorNumericFilterOk`, `Card::HasSupertype`, and
`ManaCost::{white,black,red,green,hybrid_count,hybrid_pair,twobrid_count,twobrid_color,has_x}`.
`IsSubtypeCostReducer` is not used: the deck has no reducer. There are no card names anywhere. The
profile's `required_pieces` is empty today, so `CleanupDiscardRankingWithOrder`'s protection is inert
but still routed.

**Measurement order at integration** (brief: non-inferiority plus doctrine; one pooled batch each; box
permitting):

1. **`MTG_SHED_STATS=1`**, ~200 games at the deck's play cell. This sizes the rollout denominator first.
2. **`test/tools/discard_behaviour_diff.py`**. The expected signature: Beseech stops being index 0, and
   Pyrohemia/Spellshock, surplus lands, dead Pools and 2nd legends start being shed.
3. **Paired A/B `MTG_PD_BUCKET_DISCARD=0` vs `=1`** at d0 and at the play cell, on seeds disjoint from
   the suite and Stage 5 (for example 41001+).
4. Add the `verify_deck.py` `discard_policy` gate; the override plus hatch satisfies "not PATCH-ONLY".

---

## 10. Unit-test boards (hand + board → expected ranking)

These use the `test_pirates_provider.cpp` harness shape:

* hand cards are name-only placeholders; board cards are full cards, `controller_index 0`;
* `turn_number 5`; the opponent has no permanents unless stated;
* the library is filled with `players[0].library.push_back`, as in `test_prevent_damage_creatures.cpp`,
  and `players[0].sideboard` is the 13-card list;
* the **default library** is Tamanoa, Dina, Vito, Rhox Faithmender, Rolling Earthquake plus 15 lands.

"Generic" is the status-quo index 0, for contrast. Assert the full vector where one is given; otherwise
assert index 0 and that the result is a permutation of all 8 indices (**no fall-through**).

| # | board (own) | hand `[0..7]` | expected | generic idx0 |
|---|---|---|---|---|
| **T1** fuel overflow | City of Brass, Battlefield Forge, Ancient Tomb, Tamanoa | Vito, Manabarbs, Spellshock, Pyrohemia, Beseech, Tarnished Citadel, Reflecting Pool, Rhox Faithmender | **`[3,2,7,1,4,0,6,5]`**. Sink (Pyrohemia) → target 6, need 3, both hand lands kept; DRAIN1 Vito, DIG1 Beseech, FUEL1 Manabarbs, AMP1 Faithmender (Vito kept), FUEL2 Spellshock; Pyrohemia is the lone overflow | 4 (Beseech) |
| **T2** dead legend | City of Brass, Grand Coliseum, Karplusan Forest, Brushland, **Vito**, Tamanoa | Rolling Earthquake, **Vito**, Dina, Green Sun's Zenith, Living Wish, Tarnished Citadel, Manabarbs, Rhox Faithmender | idx0 = **1** (S0 duplicate legend) | 6 (Manabarbs) |
| **T3** flood | City of Brass, Ancient Tomb, Tarnished Citadel, Brushland, Battlefield Forge, Tamanoa, Dina | Grand Coliseum, Reflecting Pool, Karplusan Forest, City of Brass, Vito, Spellshock, Beseech, Living Wish | **`[1,0,2,3,6,5,7,4]`**. No sink → target 5, need 0, so all 4 lands are surplus: Pool (reflecting) → Coliseum (tapped) → Karplusan (2 colours) → City. Then Beseech (DIG overflow behind Wish), then kept Spellshock/Wish/Vito | 6 (Beseech) |
| **T4** no engine in hand: wildcards | Ancient Tomb, City of Brass | Beseech, Green Sun's Zenith, Manabarbs, Spellshock, Pyrohemia, Rolling Earthquake, Grand Coliseum, Battlefield Forge | **`[4,3,2,5,0,6,1,7]`**. GSZ fills GAIN1 (reaches Tamanoa, eff 3 ≤ reach 5); Beseech fills DRAIN1 (cap 2+1 = 3 reaches Vito/Dina; eff 6−2 = 4); FUEL1 Earthquake; FUEL2 Manabarbs (gain reachable via GSZ); overflow Pyrohemia then Spellshock | 0 (Beseech) |
| **T5** dead Pool | Reflecting Pool | Reflecting Pool, Tamanoa, Vito, Dina, Living Wish, Spellshock, Rolling Earthquake, Beseech | idx0 = **0** (S0: no non-reflecting land on board or in hand) | 7 (Beseech) |
| **T6** same-name backup | City of Brass, Tarnished Citadel, Ancient Tomb, Battlefield Forge, Tamanoa, Dina | Vito, Vito, Rhox Faithmender, Bilbo, Rolling Earthquake, Living Wish, Karplusan Forest, Spellshock | **`[1,3,7,2,4,5,0,6]`**. DRAIN2 Vito[0]; AMP1 Faithmender (×2 beats +1); overflow Vito[1] (DRAIN backup) before Bilbo (AMP) | 2 (Faithmender) |
| **T7** NEG sideboard body | City of Brass, Brushland, Ancient Tomb, Tamanoa; **opponent: no creature** | Acidic Slime, Vito, Manabarbs, Grand Coliseum, Living Wish, Beseech, Rolling Earthquake, Rhox Faithmender | idx0 = **0** (S1) | 5 (Beseech) |
| **T8** colour cover | Ancient Tomb, Battlefield Forge, Reflecting Pool | Tamanoa, Dina, Vito, Karplusan Forest, Brushland, Ancient Tomb, Spellshock, Manabarbs | **`[4,6,7,1,2,5,0,3]`**. No sink → target 5, need 2; B and G are missing; Karplusan wins the G tie on hand index; nothing adds B; then Tomb (2 mana) beats Brushland, so Brushland is surplus | 7 (Manabarbs) |
| **T9** dead tutor | City of Brass, Tarnished Citadel, Grand Coliseum, Ancient Tomb, Tamanoa, Tamanoa, Dina; **library = 20 lands only** | Green Sun's Zenith, Vito, Tamanoa, Manabarbs, Spellshock, Living Wish, Karplusan Forest, Rolling Earthquake | idx0 = **0** (S0: GSZ has no green creature to find) | 3 (Manabarbs) |
| **T10** hatch | T1 with `MTG_PD_BUCKET_DISCARD=0` (or the heurarm override) | as T1 | equals `GenericProvider().CleanupDiscardCandidates(s, nullptr)` | n/a |
| **T11** staged | T1 with hand[4] (Beseech) `m_is_staged = true` | as T1 | 4 absent from the provider's shed; still last in `CleanupDiscardRankingWithOrder`'s output (tier C) | n/a |

Derivation notes for the full-vector cases are in the "expected" cells. For each, the S5 tail is the
ladder acquisition order reversed. For example, in T1 the acquisition order is Citadel, Pool, Vito,
Beseech, Manabarbs, Faithmender, Spellshock, so the tail is `2,7,1,4,0,6,5`.
