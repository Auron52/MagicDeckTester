# Dragonstorm — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored per `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 per-deck sweep.
Deliverable is this document; integration into `DragonstormProvider::CleanupDiscardCandidates` is
done centrally afterwards, behind a default-on `EnvOn("MTG_DSTORM_BUCKET_DISCARD", true)` with `=0`
restoring `GenericProvider::CleanupDiscardCandidates` as the A/B hatch, returning a shed order
routed through `CleanupDiscardRankingWithOrder(s, required_pieces, shed)`.

Every card claim below was read out of `src/cards/data/cards.json` (Rule 0). Nothing here is recall.

---

## 0. Two recorded constraints this proposal is bound by

**(A) NO SPARE-COPY BAND FOR THE RITUALS — EVER. This is measured, not a preference.** A
"shed any name with 2+ hand copies before unique cards" tier lived in the shared ranking on
2026-08-06/07 and was REMOVED as an engine rule; the removed-tier comment in
`src/core/SpellEffects.h` (just above tier B) records that it *actively hurt this deck*:
**+0.063 turns over an overnight run, 11 of 12 cells worse, 0 better**, because
**ritual copies are cumulative fuel** — the deck chains them, so a "spare" Rite of Flame is next
turn's mana *and* next turn's storm count. `DragonstormProvider`'s own header comment states the same
("Cleanup discard: NO spare-copy rule for this deck, ever"), with the label figure alongside
(band 85.8% optimal vs the base MV rule's 99% over 401 decisions).

**Accordingly: this proposal contains no duplicate-copy term for fuel, lands or accel.** Ritual
copies are ranked by exactly the same function as first copies. The one place a copy count appears
at all is the PAYOFF quota (§5.4/§5.5), and that is not the removed band — it is (i) a per-bucket
quota of 1 on a card whose second copy is redundant against a passive opponent, (ii) **already the
incumbent behaviour** (max-MV sheds a spare Apex/Dragonstorm first today, 110 of 238 measured sheds),
and (iii) separately measured good, which is constraint B.

**(B) A SPARE PAYOFF MUST GIVE WAY TO THE RITUALS.** `SpellEffects.h` (the comment above
`EffectiveDiscardProtectScope`) records why this deck runs `discard_protect: "hand"` rather than the
default `"all"`: protecting every copy left the rituals as the highest-MV survivors, so the deck
**shed its own engine** — overnight s7007 `gi79` / `gi193` / `gi379` each kept an identical opening
hand, shed 4-5 rituals to save payoffs, cast 0-5 spells instead of 6-8, and turned wins on T8/T6/T5
into **unwon**, at depth 8 / 20,000 ms. `gi193` held TWO Apex of Power; pitching one won on T6.
`DiscardPolicy.h` prices the scope change at **−0.0058** over the overnight suite (4 seeds,
~100k games/arm) — the largest per-deck effect in that table.

Both constraints point the same way: **fuel outranks payoff copies, and payoff copies outrank
nothing except genuinely dead cards.**

---

## 1. Evidence — what this rule actually decides on this deck

### Measured for this proposal (`MTG_SHED_STATS`, shipped play settings d5/b20, 200 games, s1001)

| | sheds in REAL play | sheds inside the SEARCH | ratio | taken with <4 lands | sheds/cleanup |
|---|---|---|---|---|---|
| **Dragonstorm** | **15** | **7,952** | **530x** | 6,682 (84.0%) | **1.00** |
| (Minotaur, for scale) | 99 | 250,265 | 2,528x | 100% | 1.35 |
| (Dragons, for scale) | 66 | 661,269 | 10,020x | 99.6% | — |

**Be honest about the size: this deck sheds far less than its two peers**, and 530x is the smallest
ratio of the three. It wins on turn 4.53 and empties its hand doing it. But `real == 0` is not the
test (the brief's closing line), and 7,952 rollout sheds decided by index 0 **with no search above
them** is not inert — every one of them shapes a line the search then scores.

`sheds/cleanup = 1.00` is worth reading too: **every cleanup on this deck sheds exactly one card**
(all 253 traced decisions are `hand=8`, i.e. exactly one over the limit). So **only index 0 ever
matters in real play.** The tail of the order exists for the rollout and for totality, not for the
game.

### What the incumbent rule sheds (`MTG_TRACE=discard`)

400 d0 games are the recorded historical census (28 events, `cleanup-discard-measured.md`); I ran a
bigger one. **4,000 games at d0, seed 3003 → 238 real sheds:**

| shed card | n | | shed card | n |
|---|---|---|---|---|
| Dragonstorm (spare) | 60 | | Dragonlord Kolaghan | 15 |
| Apex of Power (spare) | 50 | | Lathliss, Dragon Queen | 14 |
| Utvara Hellkite | 39 | | **Seething Song** | **5** |
| Scourge of Valkas | 24 | | **Irencrag Feat** | **5** |
| Karrthus, Tyrant of Jund | 24 | | Pyretic Ritual / Desperate Ritual | 1 / 1 |

and the state it happens in:

* `landsinhand=0` in **238 of 238** — and `dropopen=1` throughout. The deck plays every land it
  draws, so a hand that reaches 8 cards has **no land in it**. **This deck's cleanup discard is
  exclusively a mana-screw decision among spells.**
* `lip` (lands in play) 0-2 in **214 of 238**; 5 is the maximum seen once.
* **No land was ever shed**, and no Ruby Medallion or Lotus Bloom either.

### The three defects this buys, and the one place the incumbent is already right

Incumbent tier B is descending mana value, which on this decklist is:

    Apex 10 > Dragonstorm 9 > Utvara 8 > Karrthus 7 > Kolaghan 6 = Lathliss 6 > Scourge 5
      > Irencrag 4 > Seething 3 > Desperate 2 = Pyretic 2 = Ruby Medallion 2 > Rite 1
      > lands 0 = Lotus Bloom 0

**It is already right at the top**, and that is not an accident of this deck — it is measured:
`cleanup-discard-measured.md` finding 3 says *"the highest-mana-value rule IS load-bearing —
Dragonstorm loses +0.1239 when it is effectively removed"*. **So unlike Minotaur and Dragons, this
deck is NOT a max-MV inversion**, and a proposal that scrambles the top of the order would be
arguing against a recorded number. The value is in three specific, local defects:

1. **The fuel order is exactly INVERTED.** When the payoffs are protected and no Dragon is in hand,
   max-MV sheds `Irencrag Feat (4) → Seething Song (3) → Pyretic/Desperate (2) → Rite of Flame (1)`
   — i.e. **the strongest ritual first**. Irencrag nets **+3** mana and Seething **+2**; Rite of
   Flame nets **+1**. The census catches this red-handed: **Seething Song 5 and Irencrag Feat 5 of
   the 12 ritual sheds; Rite of Flame 0.** The deck pitched its two best accelerants and kept its
   weakest, 10 times out of 12.
2. **Ruby Medallion (MV 2) sheds ahead of Rite of Flame, ahead of every land, and ahead of Lotus
   Bloom.** Every castable nonland in this deck carries an `{R}` pip (the Medallion itself is {2} and
   Lotus Bloom has no mana cost at all — it can only arrive off suspend), so one Medallion
   takes **1 generic off every spell the deck casts** and they stack per copy — on a 5-spell storm turn
   that is ~4-5 mana, worth roughly two rituals (§3 worked arithmetic). Max-MV ranks it as the
   *third* most expendable nonland class. It only escapes being shed today because a Dragon or a
   spare payoff is nearly always present.
3. **Colour-dead Dragons sit BELOW castable ones.** Karrthus needs `{B}` and `{G}`; Kolaghan needs
   `{B}`. **No land in this deck produces black or green except Unclaimed Territory** — whose
   coloured mana is `colored_creature_only`, so it *can* pay a Dragon's pips but cannot pay a single
   pip of a ritual, Dragonstorm, Apex or a Medallion. With no Unclaimed Territory and no chosen-colour
   float available, Karrthus is the deadest card the deck can draw, and max-MV sheds Utvara (8),
   a spare Apex (10) and a spare Dragonstorm (9) **ahead of it**.

And one thing to preserve rather than fix: the spare-payoff shed (110 of 238) is constraint B working.

---

## 2. The deck, in one paragraph — and therefore its shape

Dragonstorm is a **mono-red storm COMBO deck** and it is a combo in the strict sense: no single card
does anything. It converts **land mana → ritual float → one very expensive payoff → a wave of
Dragons off the library**. `Dragonstorm` ({8}{R}) has `tutor_to_battlefield` + `tutor_types:["Dragon"]`
and puts `min(spells_cast_this_turn, Dragons left in library)` Dragons straight onto the battlefield —
the storm counter includes Dragonstorm's own cast, so **every spell cast earlier in the turn is one
more Dragon**. Each put routes through `FireEtbWatchers`, so Scourge of Valkas pings
(`dragon_ping_on_enter`, X = Dragons controlled), Lathliss spawns a 5/5 per later nontoken Dragon,
and the wave attacks the same turn only if a `grants_haste` Dragon is in it. `Apex of Power`
({7}{R}{R}{R}) is the deck's second engine and its only dig: `impulse_exile:7` +
`impulse_float_amount:10` means 10 mana in, 10 mana and seven fresh cards out, which is a refuel
rather than a win. The 17 rituals plus Lotus Bloom and Ruby Medallion are the fuel; the 20 lands are
the ignition. So the shape is **combo, and the buckets are one per combo part with similar effects
grouped** (the brief's table): **ignition (lands) · acceleration (the two non-land sources) · fuel
(all five ritual names, one bucket — this is the "similar effects grouped" case) · payoff
(Dragonstorm) · dig/refuel (Apex) · win condition (Dragons in hand)**. Six buckets, and the
justification for six rather than two is that the parts are not substitutes: no amount of fuel wins
without a payoff, and no payoff resolves without fuel.

---

## 3. The storm arithmetic — why the fuel bucket's quota is what it is

This is the "net of board" rule (structural rule 2) applied to a combo deck, and it is the whole
basis for §5.3. **Fuel needed is a function of the payoff's cost, and mana on board offsets it:**

    shortfall = eff_cost(payoff)  −  mana available from the board
    eff_cost(Dragonstorm) = 9 − (Ruby Medallions on battlefield)      // generic only, pips never
    eff_cost(Apex)        = 10 − (Ruby Medallions on battlefield)

Each ritual closes the shortfall by its **NET**, not its gross, and adds **+1 storm**:

| ritual | cost | gross float | **net** | needs | note (from `params`) |
|---|---|---|---|---|---|
| Rite of Flame | {R} | 2 (+1 per copy already in a graveyard) | **+1** | 1 red source | `ritual_float_gy_self_bonus` — a chain escalates 2,3,4,… |
| Pyretic Ritual | {1}{R} | 3 | **+1** | 2 sources | — |
| Desperate Ritual | {1}{R} | 3 | **+1** | 2 sources | `splice_onto_arcane` — a spliced copy STAYS IN HAND and is reusable |
| Seething Song | {2}{R} | 5 | **+2** | 3 sources | — |
| Irencrag Feat | {1}{R}{R}{R} | 7 | **+3** | 4 sources, 3 red pips | `max_casts_after:1` — **caps the chain** |

Worked, from a bare board (this is the deck's actual go-off, and it is why the quotas are large):

* **4 red sources, no Medallion.** Seething (4−3+5 = 6) → Pyretic (6−2+3 = 7) → Pyretic (→8) →
  Rite (8−1+2 = 9) → **Dragonstorm at 9, storm = 5 → a 5-Dragon wave.** *Four lands and four
  rituals.*
* **4 red sources, one Medallion resolved.** Seething costs 2 (net +3) → 7; Pyretic costs 1 (net +2)
  → 9; **Dragonstorm at 8, storm = 3.** *One Medallion ≈ two rituals* — the §1 defect-2 claim.

Two consequences, both load-bearing:

1. **The fuel bucket essentially cannot overflow, so the HARD constraint holds structurally rather
   than by assertion.** A ritual is surplus only when *both* `shortfall ≤ 0` **and** the storm count
   already reached the `Dragons left in library` ceiling (≤ 8, and one more spell is one more Dragon
   right up to it). A cleanup happens at `lip` 0-2 with no land in hand — the exact opposite state.
   So no ritual is ever shed while any Dragon, spare payoff, surplus land or surplus accel is in the
   hand, and a ritual copy is never shed *because it is a copy*.
2. **Irencrag Feat's rank is a function of the shortfall, not a constant** (§7 promotion 1). Its
   `max_casts_after:1` means only ONE spell may follow it, so it must be the penultimate cast and it
   **forfeits the storm count of every ritual it replaces**. At `shortfall ≥ 3` it is the best card
   in the bucket (it single-handedly closes the gap); at `shortfall ≤ 0` mana is not the binding
   constraint and it is the *worst* (it buys nothing and caps the chain). `CastOrderRank` already
   encodes the ordering half of this generically — rank 18, "must be the LAST ritual … so the only
   spell that follows it is the payoff".

---

## 4. Card-by-card role table

Every row's bucket is derived from `params` (the predicate is named in §8), never from the card name.
MV is from the printed cost in `cards.json`.

| card | n | cost | MV | `params` keyed on | bucket |
|---|---|---|---|---|---|
| Mountain | 9 | — | 0 | `produces:[R]` | 1 · LANDS |
| Sandstone Needle | 4 | — | 0 | `produces:[R]`, `produces_amount:2`, `enters_tapped`, `enters_tapped_with_depletion:2` | 1 · LANDS |
| Mercadian Bazaar | 4 | — | 0 | `storage_land`, `storage_charge_mode:tap`, `enters_tapped` | 1 · LANDS |
| Dwarven Hold | 1 | — | 0 | `storage_land`, `storage_charge_mode:upkeep_if_tapped`, `enters_tapped` | 1 · LANDS |
| Unclaimed Territory | 2 | — | 0 | `produces:[W,U,B,R,G,C]`, **`colored_creature_only`** | 1 · LANDS (last — see §6.1) |
| Lotus Bloom | 4 | — | 0 | `suspend_time_counters:3`, `sac_for_mana_amount:3` | 2 · ACCEL |
| Ruby Medallion | 3 | {2} | 2 | **`reduces_spell_color:"R"`** | 2 · ACCEL (reducer) |
| Rite of Flame | 4 | {R} | 1 | `ritual_floating_mana:2`, `ritual_float_color:R`, `ritual_float_gy_self_bonus` | 3 · FUEL |
| Pyretic Ritual | 4 | {1}{R} | 2 | `ritual_floating_mana:3`, `ritual_float_color:R` | 3 · FUEL |
| Desperate Ritual | 4 | {1}{R} | 2 | `ritual_floating_mana:3`, `splice_onto_arcane` | 3 · FUEL |
| Seething Song | 4 | {2}{R} | 3 | `ritual_floating_mana:5`, `ritual_float_color:R` | 3 · FUEL |
| Irencrag Feat | 1 | {1}{R}{R}{R} | 4 | `ritual_floating_mana:7`, **`max_casts_after:1`** | 3 · FUEL (restrictor) |
| Dragonstorm | 4 | {8}{R} | 9 | **`tutor_to_battlefield`**, `tutor_types:[Dragon]`, `tutor_shuffle_after` | 4 · PAYOFF |
| Apex of Power | 4 | {7}{R}{R}{R} | 10 | **`impulse_exile:7`**, `impulse_expiry_this_turn`, `impulse_float_amount:10` | 5 · DIG/REFUEL |
| Scourge of Valkas | 3 | {2}{R}{R}{R} | 5 | `dragon_ping_on_enter`, `firebreathing_cost:{R}` | 6 · DRAGONS (pinger) |
| Lathliss, Dragon Queen | 1 | {4}{R}{R} | 6 | `etb_other_subtype_creates_tokens`, `team_pump_cost:{1}{R}` | 6 · DRAGONS (token engine) |
| Utvara Hellkite | 2 | {6}{R}{R} | 8 | `attack_per_matching_creates_tokens:1` | 6 · DRAGONS (attack engine) |
| Dragonlord Kolaghan | 1 | {4}{B}{R} | 6 | `grants_haste`, `subtypes_affected:[Dragon]` | 6 · DRAGONS (haste lord, **off-colour**) |
| Karrthus, Tyrant of Jund | 1 | {4}{B}{R}{G} | 7 | `grants_haste`, `subtypes_affected:[Dragon]` | 6 · DRAGONS (haste lord, **off-colour ×2**) |

**Genuinely ambiguous roles — flagged:**

* **Apex of Power is three things at once** (10 mana in / 10 mana out, a 7-card dig, and a payoff the
  storm count does not scale). I give it its own bucket because it is the deck's dig and because a
  second copy has recorded evidence attached to it (constraint B, `gi193`). Reading it as *fuel*
  instead would be defensible and would change its quota from 1 to "unbounded like the rituals";
  I reject that because it is mana-**neutral**, not mana-positive, and its float is withheld when it
  is cast off another Apex's staged exile (`cast_from_hand` gate).
* **Ruby Medallion — mana or fuel?** It is a *permanent* that discounts every future spell, so it is
  classified with the mana (ACCEL), exactly as Minotaur's Ragemonger was and for the same recorded
  reason: a cost reducer is the answer to a mana problem, and a mana problem is the only state in
  which this deck ever sheds. Its quota is net of board; a resolved one satisfies it.
* **Lotus Bloom is a land-independent mana source, not a rock** — `suspend_time_counters:3` for {0},
  then `sac_for_mana_amount:3`. It is the only card the deck can deploy with **zero** lands, which is
  the state 100% of its sheds occur in.
* **The two haste lords are a combo PART, not a threat.** Their P/T and flying are inert (no blockers
  in the goldfish) and Karrthus's ETB is inert/deferred per its bracket note; their only live text is
  `grants_haste` to Dragons — i.e. they are what makes the wave attack the turn it lands. That role
  is normally supplied **from the library** by the put, which is why a hand copy is near-dead (§5.6).

**`[bracket note]`s that touch a role** (per the brief) — all read and none changes a bucket, but two
change a *value*: Unclaimed Territory's chosen-type restriction is "not modelled", so its coloured
mana pays for **any** creature (all of this deck's creatures are Dragons, so this is inert here) while
`colored_creature_only` still blocks it from every noncreature spell — that asymmetry is real and
load-bearing (§6.1). Karrthus's ETB "gain control of all Dragons" is inert and "untap all Dragons" is
**deferred**, so Karrthus is modelled as a 7/7 haste lord and nothing more, which lowers its hand
value further. Kolaghan's same-name life-loss trigger is inert. Dwarven Hold/Mercadian Bazaar model
charging as "+1 counter per idle turn", verified off-by-one — earliest useful burst is *turn played +
2*, which is the basis for ranking an in-hand storage land low (§6.1).

---

## 5. The buckets, with quotas — every one net of board

### 5.1 LANDS · quota = **4**, hard ceiling **5**, net of board

Filled by the battlefield first: a Mountain in play, a charged storage land, a Sandstone Needle with
depletion counters left, and **3 per Lotus Bloom already on the battlefield** (`sac_for_mana_amount`)
all count toward it. Four is the number §3's worked go-off needs; the profile's own mulligan rule
agrees independently (`max_lands: 5`), and a **resolved Ruby Medallion lowers the target to 3** because
it takes 1 off the payoff and 1 off each ritual.

**Say plainly that this bucket is near-dead code in real play**: 0 of 238 traced sheds had any land in
hand, and only 1 of the 15 at d5 did. Its consumer is the ROLLOUT, which declines land drops far more
often than real play does, and which is where 7,952 of the 7,967 sheds happen.

### 5.2 ACCEL · quota = **1 Lotus Bloom + 1 Ruby Medallion**, net of board

A suspended or resolved Bloom fills the Bloom slot; a Medallion on the battlefield fills the reducer
slot. Both are ranked ABOVE the second land, for the same reason: a Bloom costs {0} to deploy (it is
the deck's only play from a landless hand) and a Medallion is worth ~2 rituals on the go-off turn
(§3). A second Medallion is a real second discount, so it gets a **soft** slot late in the ladder
rather than being treated as spare; a second Bloom likewise (see the doubt in §9.2).

### 5.3 FUEL · quota = **min(rituals in hand, Dragons left in library)** — i.e. effectively unbounded

Derived in §3, and the structural guarantee behind constraint A. Nothing in this bucket carries a
copy count.

### 5.4 PAYOFF (Dragonstorm) · quota = **1**

Never a permanent, so nothing on board fills it. The engine already protects the last hand copy
(`required_pieces: ["Apex of Power","Dragonstorm"]` with `discard_protect: "hand"`), so this quota
mostly documents what the engine enforces. Copies 2+ are **surplus** and shed at S2, which is
constraint B and the incumbent behaviour.

### 5.5 DIG/REFUEL (Apex of Power) · quota = **1**

As above, engine-protected at the last hand copy. Copy 2+ shed at S2, ahead of the spare Dragonstorm
(it costs one more and it refuels rather than wins) — and `gi193` is literally a two-Apex hand whose
shed won on T6.

### 5.6 DRAGONS IN HAND · quota = **0**, promoted to **1** in two named states

**This is the sharpest call in the proposal, so here is the argument.** The deck's Dragons are a
**library** resource: `tutor_to_battlefield` puts them onto the battlefield for free, and 8 of them
are in the deck for a wave that is capped at `min(storm, Dragons left in library)`. A hand copy does
not serve that role — it is a 5-8 mana creature with no haste in a deck that plays 20 lands and wants
to spend its whole turn on rituals. The net-of-board rule in its strongest form: **the role's supply
is the library, and the hand owes nothing.** The measured state agrees — every shed happens with
0 lands in hand and ≤ 2 in play, which is precisely when a 6-8 drop is furthest from castable.

Promoted to quota 1 when either holds:

* **(a) A castable pinger.** Scourge of Valkas is {2}{R}{R}{R} — **all red**, the cheapest Dragon,
  and a *resolved* Scourge makes every later Dragon in a wave ping (`dragon_ping_on_enter` fires from
  `FireEtbWatchers` at every Dragon-enter site, and multiple Scourges each ping). With a Scourge
  already out, a 4-Dragon wave pings 2+3+4+5 = 14 to the face on top of the wave's own pings; without
  it, the wave pings only if a Scourge is among the Dragons put. `firebreathing_cost:{R}` also turns
  leftover ritual float into face damage. It is the one Dragon whose hard cast is part of the combo.
* **(b) A haste lord the library can no longer supply.** `grants_haste` is what lets a wave attack
  the turn it lands, and the put reserves one *from the library*. If **no `grants_haste` Dragon
  remains in the library** and the hand copy's off-colour pips are coverable, the hand copy is the
  only route to haste. The engine already models exactly this line —
  `RestrictSacColorsToHasteAndRed`: a Lotus Bloom floats BLACK instead of RED precisely when
  Karrthus/Kolaghan is castable this turn.

---

## 6. Within-bucket order, and the distance term

### 6.1 The distance-to-playable term (structural rule 4)

Two halves, and the **colour half is the one that earns its keep on this deck**:

* **COLOUR COVERAGE is a gate, not a penalty.** A card whose coloured pips cannot be paid from
  board+hand sources sinks to the bottom of the *whole hand*, below every other card, regardless of
  mana value. Counting rules, all derived from `params`:
  * a `colored_creature_only` land (Unclaimed Territory) covers a coloured pip **only for a creature
    spell**; for a ritual, Dragonstorm, Apex or a Medallion it contributes its `{C}` to the generic
    portion and nothing else;
  * a Lotus Bloom on the battlefield covers **one** colour (3 of a single chosen colour); an Apex
    castable this turn likewise (10 of one colour);
  * this is the same shape as the existing provider-local `FluctuatorCanPayNow` (lands + convertible
    sources vs a cost's pips) — reuse that pattern rather than reaching into `ManaPayment`'s Hall
    scan.
  On this decklist the gate fires on exactly two cards, and they are the two the incumbent gets
  wrong: **Karrthus** ({B} and {G}) and **Kolaghan** ({B}).
* **MANA DISTANCE, reducer-aware.** `eff_mv(i) = mv(i) − (Ruby Medallions on battlefield)`, generic
  only, floored at the pip count; `reach = board sources + live lands in hand + 3 per resolved Lotus
  Bloom`. A resolved Medallion **erases** distance from every card in hand at once (this is the
  Minotaur precedent, generalised: there, one resolved reducer lowered the land target; here it
  lowers every cost in the deck). Distance only ever reorders cards **within** a bucket.

Deliberately **bounded**: max-MV is measured *good* on this deck (+0.1239 when removed), so a blanket
"shed the most expensive" is not the thing to overturn. The distance term must not become one.

### 6.2 LANDS, best kept first

Ordered by **mana delivered on the go-off turn**, which is the only turn that matters:

1. **Sandstone Needle** — `produces_amount:2`, so it is two mana from one land slot on the combo turn.
2. **Mountain** — one untapped red, no delay.
3. **Mercadian Bazaar / Dwarven Hold** — storage batteries; they burst big on the go-off turn but pay
   **nothing for two turns** after arriving (earliest useful burst = turn played + 2, per their notes).
4. **Unclaimed Territory** — last. Its coloured mana is `colored_creature_only`, so it cannot pay a
   single pip of the 17 rituals, Dragonstorm, Apex or a Medallion; for the combo it is a colourless
   land. **Exception:** it is the deck's ONLY black/green source, so it promotes to *first* while an
   off-colour Dragon in hand is otherwise castable — keep exactly one, no more (one Territory covers
   one pip).

### 6.3 ACCEL, best kept first

**Lotus Bloom** (deployable at {0} from a landless hand) > **Ruby Medallion** > second Medallion >
second Bloom.

### 6.4 FUEL, best kept first — `value × P(play)`, shortfall-aware

`value` is the ritual's **net** float (§3) plus two param-derived credits, and `P(play)` decays in
`gross_cost − reach`:

    value(i) = net_float(i)
             + 1 if params.splice_onto_arcane        // a spliced copy STAYS IN HAND and is reused
             + (copies of this name already in a graveyard) if params.ritual_float_gy_self_bonus
             ± the Irencrag shortfall term (§7.1)
    P(i)     = decay(eff_gross(i) − reach)           // 1.0 when castable now

Shed the lowest product first. Two behaviours fall out rather than needing a rule, which is why this
shape is the one to propose:

* **At reach 1, Rite of Flame promotes to the top of the bucket** — it is the only ritual castable off
  a single red source, so every other ritual's `P` collapses. At reach 4+ it sinks to the bottom
  (net +1, and its `gy_self_bonus` credit is 0 until a copy is actually in a graveyard).
* **Multiple copies are ranked identically to first copies.** There is no dupe term, so a second Rite
  of Flame never jumps ahead of a unique card of lower value — constraint A, structurally.

This is the `EV = P(play) × value` shape the repo has already measured *and adopted* on Minotaur
(round 3, `MTG_MINOTAUR_EV_DISCARD`, −0.00037 t/game across three seed blocks), and the shape whose
lexicographic alternative ("playability first, then value") measured **worse** there. Do not ship the
lexicographic form.

### 6.5 DRAGONS, best kept first

**Scourge of Valkas** (5, mono-red, the pinger and a firebreathing sink) > **Lathliss** (6, {4}{R}{R},
token engine) > **Utvara Hellkite** (8, mono-red, but its payoff needs it to survive to attack) >
**Kolaghan** (6, needs {B}) > **Karrthus** (7, needs {B} and {G}) — **with the §6.1 colour gate
overriding all of it**: an off-colour Dragon with uncoverable pips drops below every card in the hand.

Note this is deliberately **not** `TutorToBattlefieldPutOrder`'s ranking (haste → Lathliss → Utvara →
Scourge). That hook ranks Dragons that arrive **free, from the library, mid-wave**, where haste and
the token engine dominate; this ranks Dragons that must be **hard-cast from hand**, where cost and
colour dominate. Two different questions about the same five cards — the integration must not
"unify" them.

### 6.6 PAYOFF / DIG

Nothing to order beyond the quota: copy 1 is kept (and engine-protected), copies 2+ shed, Apex's
spare before Dragonstorm's.

---

## 7. State promotions

### Proposed (all four are facts a cleanup ranking can establish)

1. **Irencrag Feat's rank tracks the SHORTFALL (§3).** `shortfall = eff_cost(payoff) − board mana`.
   At `shortfall ≥ 3` Irencrag is the **best** card in the fuel bucket (it closes the gap alone); at
   `shortfall ≤ 0` mana is not binding, storm count is, and `max_casts_after:1` means Irencrag
   *forfeits* the storm count of the rituals it replaces — so it becomes the **worst**. Computed from
   the board and the card data, no projection.
2. **The colour-coverage gate** on off-colour Dragons (§6.1) — a coverage fact, not a damage estimate.
3. **Scourge promotion when castable** (§5.6a) — a reach-and-pips test.
4. **A resolved Ruby Medallion erases distance across the whole hand** (§6.1) — mechanical, per copy.

### Rejected as SEARCH-owned (the brief's rule 5: a promotion whose real content is a damage
projection or a cast choice belongs to the search)

* **"Is the go-off lethal this turn?"** — that is `HasExtraLethalModel` / `ExtraLethalDamage`, which
  already project the wave's Scourge pings for the `wins` check. The ranking must not re-derive it;
  an over-projection there only steers a pick, but in a *ranking* it would silently decide a shed.
* **Which Dragons the put takes, and in what order** — `TutorToBattlefieldPutOrder` owns it (§6.5).
* **Splice or hard-cast the Desperate Rituals** — `splice_count` is a searched plan variant
  (`UseSpliceCollapse`). The ranking credits spliceability as a static +1 value and nothing more.
* **Which colour to float off a Lotus Bloom or an Apex** — `ImpulseFloatColorRedOnly` /
  `RestrictSacColorsToHasteAndRed` own it. §5.6b *reads* that hook's condition; it must not change it.
* **"Hold the land drop to set up a bigger turn"** — the trace's own `dropopen`/`tower` fields exist
  because this is a **land-drop** question, and the comment there already says it is reported at the
  discard site rather than fixed there. All 238 sheds had `dropopen=1`.
* **A hand-emptying inversion** (Minotaur's Neheb case) — **not applicable**: no card in this deck
  rewards a small hand.

---

## 8. The total order over a hand

### Keep ladder (interleaved, most valuable first)

Minotaur's shipped lesson: quotas must be **interleaved**, not filled bucket-by-bucket, because the
fill order is also the marginal-value order and read backwards it is the order quota-protected cards
give way in. Proposed ladder (`S_ACCEL` = Lotus Bloom, `S_RED` = Ruby Medallion):

    S_LAND, S_ACCEL, S_PAYOFF, S_FUEL, S_LAND, S_FUEL, S_RED, S_FUEL, S_DIG, S_FUEL,
    S_LAND, S_FUEL, S_DRAGON, S_LAND, S_RED, S_ACCEL, S_LAND

Reading the front: with zero lands nothing can be cast at all, so **land 1 leads** — but a **Lotus
Bloom is second**, ahead of the payoff and every ritual, because it is the only card the deck can
deploy from a landless hand and the census says a landless hand is the *only* hand that ever sheds.
The payoff comes third (without it nothing wins, and it is engine-protected anyway), then the chain
alternates fuel against the next land, with the Medallion taking the slot after land 2 — the Minotaur
"the reducer IS mana, and a mana problem is the state every one of these sheds is taken in" ruling,
which applies here *more* strongly because this Medallion discounts the entire deck rather than one
tribe. `S_DRAGON` fills only when a §5.6 promotion fires; otherwise it is skipped and the slot is lost.

### Shed order (most expendable first — this is what the provider returns)

* **S0 — COLOUR-DEAD cards.** Any card whose coloured pips are uncoverable from board+hand (§6.1).
  On this decklist: Karrthus, then Kolaghan. The deadest cards the deck can hold, and the incumbent
  sheds three other cards ahead of them.
* **S1 — Dragons past the quota**, reverse of §6.5: Karrthus → Kolaghan → Utvara → Lathliss →
  Scourge. (The first two are normally already gone at S0; they only reach S1 when their pips *are*
  coverable — an Unclaimed Territory in hand, a resolved Bloom — which is exactly when they are worth
  more than a 6-8 drop that grants nothing.)
* **S2 — surplus payoff copies**: Apex #2+, then Dragonstorm #2+. (Constraint B; unchanged from the
  incumbent, which does this 110 times in 238.)
* **S3 — surplus ACCEL past quota**: Medallion #3, Bloom #2+ — *after* S2, because a second Bloom is
  three real mana and a spare Apex is zero.
* **S4 — surplus LANDS past the quota**, §6.2 reversed: Unclaimed Territory → storage → Mountain →
  Needle. (Rollout-only in practice.)
* **S5 — surplus FUEL**, lowest §6.4 product first. **Structurally unreachable** in a hand of ≤ 8
  (§5.3); it exists so the list is total.
* **S6 — anything unrecognised**, last among the overflow. No opinion is a reason to protect a card,
  not to pitch it (Minotaur's S5, kept verbatim). Empty on this decklist; a screening arm's new card
  lands here.
* **KEEP TAIL — the ladder read backwards**, so **every card in the hand is named** and index 0 is
  determined for any hand. This is the Mirrorwing gi295 trap: anything omitted falls through to the
  shared tier B, and on this deck that would silently re-impose max-MV over exactly the cards this
  policy exists to reorder — the rituals.

**Worked, on the state the census says is typical** (`lip=1`, no land in hand, hand = Dragonstorm,
Dragonstorm, Apex, Karrthus, Seething Song, Rite of Flame, Pyretic Ritual, Ruby Medallion):
incumbent max-MV sheds the **spare Apex**… but Apex is the sole copy and protected, so it sheds the
**spare Dragonstorm** (9). Proposed: S0 takes **Karrthus** (uncoverable {B}{G}) — the only genuinely
dead card in the hand, and the spare Dragonstorm survives as a second go-off attempt.

---

## 9. Classification predicates (named, so integration is mechanical)

```
is_land(i)        := CleanupDiscardIsLand(hand[i])                     // via the DEFINITION
is_storage(i)     := d->params.storage_land
is_big_land(i)    := d->params.produces_amount >= 2                    // Sandstone Needle
is_creature_only(i):= d->params.colored_creature_only                  // Unclaimed Territory
is_accel(i)       := d->params.suspend_time_counters > 0
                  || d->params.sac_for_mana_amount   > 0               // Lotus Bloom
is_reducer(i)     := !d->params.reduces_spell_color.empty()            // Ruby Medallion
is_fuel(i)        := IsManaRitual(*d)                                  // SpellEffects.h; == ritual_floating_mana > 0
is_restrictor(i)  := d->params.max_casts_after >= 0                    // Irencrag Feat
is_splice(i)      := d->params.splice_onto_arcane                      // Desperate Ritual
is_gy_scaling(i)  := d->params.ritual_float_gy_self_bonus              // Rite of Flame
net_float(i)      := d->params.ritual_floating_mana - eff_mv(i)
is_payoff(i)      := d->params.tutor_to_battlefield && !tutor_types.empty()   // Dragonstorm
is_dig(i)         := d->params.impulse_exile > 0                       // Apex of Power
is_wincon(i)      := d->card.IsCreature() && CardHasSubtype(d->card, tutor_subtype)
is_pinger(i)      := d->params.dragon_ping_on_enter                    // Scourge
is_token_engine(i):= d->params.etb_other_subtype_creates_tokens        // Lathliss
is_attack_engine(i):= d->params.attack_per_matching_creates_tokens > 0  // Utvara
is_haste_lord(i)  := d->params.grants_haste                           // Karrthus / Kolaghan
```

`tutor_subtype` is read **off the payoff's own `tutor_types`** (and off `attack_token_requires_subtypes`
/ `subtypes_affected` as fallbacks), so nothing hardcodes the string "Dragon" and a screening arm that
swaps the tribe keeps the right buckets.

Board census, netted before any quota is computed:

```
board_sources     = lands (via PermanentManaYield, so produces_amount and storage_counters count)
                  + 3 per battlefield Lotus Bloom (sac_for_mana_amount)
board_reducers    = battlefield permanents with reduces_spell_color matching the spell's colour
suspended_blooms  = Lotus Blooms in exile with time counters      // fills the ACCEL quota
dragons_in_library= library cards carrying tutor_subtype          // fuel ceiling + promotion 5.6b
haste_in_library  = those with grants_haste                       // promotion 5.6b
```

A library count is **deck knowledge, not clairvoyance** — it is a multiset count, never an order —
and `CleanupDiscardProtected` already does one under the `deck` scope, so the precedent exists.

### Three integration traps, each of which has bitten this repo before

1. **`IsSubtypeCostReducer` DOES NOT MATCH RUBY MEDALLION.** That shared helper is
   `!reduces_spell_subtype.empty() || chooses_creature_type || !reduces_subtype_colored_subtype.empty()`
   — the *subtype* twin. Ruby Medallion is the **colour** reducer (`reduces_spell_color:"R"`). Copying
   Minotaur's `is_reducer` verbatim would drop the deck's best accelerant into the unrecognised bucket.
2. **`params.mana_rock` IS FALSE FOR LOTUS BLOOM.** Copying Minotaur's board census verbatim would
   count zero non-land sources on a board holding four Blooms. Use `sac_for_mana_amount` /
   `suspend_time_counters`.
3. **CHECK THE DEFAULTS BEFORE A `> 0` TEST.** `max_casts_after` defaults to **−1**, so the restrictor
   test must be `>= 0`; `produces_amount` defaults to **1**, so the big-land test must be `>= 2`. This
   is the Dragons classifier bug (`reduces_spell_subtype_amount` defaults to 1, which made every
   nonland card in hand read as a cost reducer and made a shipped, "measured-neutral" policy a
   measurement of nothing). Every characteristic read goes through
   `CardDatabase::Instance().LookupCached(card)` — a hand card is a name-only placeholder with empty
   type and subtype masks, which is the same bug's third head.

---

## 10. Doubts, flagged — the user reviews and amends these

1. **`discard_protect` should probably be `"deck"`, not `"hand"` — and this is the single
   highest-leverage item I found.** `DiscardPolicy.h`'s table only ever measured **`hand` vs `all`**
   for this deck (−0.0058). `"deck"` was never measured. The recorded `gi79` note argues for it
   directly: *"gi79 held one of each but the LIBRARY held more, and pitching it to keep the rituals
   won on T8"* — with 3 more Dragonstorms and 3 more Apexes in the library, a lone hand copy is not
   irreplaceable, and under `"hand"` scope it is unsheddable. **This is a one-line profile change and
   an `MTG_DISCARD_PROTECT=deck` A/B, independent of everything else here.** I would run it first,
   because if it lands it changes which cards this policy is even allowed to name.
2. **Quota 1 or 2 for Lotus Bloom and Ruby Medallion?** A second Bloom is three more mana one turn
   later, which for a deck that wins on turn 4.5 may be entirely dead; a third Medallion is a third
   discount but costs {2} to deploy. The deck's learned `card_scores` say the 2nd Bloom is bad
   (−0.736 against the 1st copy's +0.662, the deck's best card) — but see doubt 6 before weighting
   that at all. I propose 1 hard + 1 soft for each and flag the numbers as the ones I am least sure of.
3. **Irencrag Feat's shortfall threshold of 3 is a guess.** The *direction* is derived (§3) but the
   cut point is not; the honest form is a graded term rather than a threshold, and it is worth one
   behavioural diff to see how often the promotion actually fires (Irencrag is a 1-of, so possibly
   rarely enough not to matter).
4. **Is Sandstone Needle really the best land?** `produces_amount:2` says yes on the go-off turn;
   `enters_tapped` + `enters_tapped_with_depletion:2` say it is a blank the turn it arrives and gone
   after two taps. `card_scores` prefer it strongly (+0.500/+0.467 vs Mountain −0.141) — again,
   see doubt 6. I ranked it first and I would not defend that over a measurement.
5. **Should a spare Dragonstorm really shed after the Dragons?** Constraint B's recorded evidence is
   payoff-**vs-ritual**, never payoff-vs-Dragon. My S1-before-S2 ordering rests on doctrine (a
   colour-castable 9-drop in a deck engineered to reach 9 vs an 8-drop creature that must then
   survive), not on a number. If it is wrong the fix is trivial (swap S1 and S2), and the behavioural
   diff would be large — Dragons and spare payoffs are 226 of the 238 observed sheds.
6. **The deck's `card_scores` are STALE and I have deliberately used them only as corroboration.**
   They were computed on 2026-07-19 (`8d9da214`); the profile was last touched 2026-07-29 and
   **1,143 `src/` commits have landed since**. That is an order of magnitude worse than the Minotaur
   round-4 case (127 commits) that got a 7-hour measurement cancelled for being "precision on the
   wrong table". They are also an opening-hand marginal confounded with castability, not a
   "value of playing". Where they agree with the doctrine above (every Dragon negative and Karrthus
   the worst card at −0.587; Scourge the only positive Dragon at +0.118; Seething Song positive on
   both copies; Ruby Medallion positive) I have said so; **nothing in §5-§8 depends on them.** They
   do disagree in one place worth recording: they rank Lathliss (−0.558) *below* Kolaghan (−0.378),
   where §6.5 has Lathliss above on castability grounds.
7. **Is keeping a castable Scourge over a marginal ritual right?** This is the one place I invert a
   rule that is measured good on this deck (max-MV sheds Scourge at 5, ahead of every ritual). The
   ping arithmetic in §5.6a is strong, but it is arithmetic I did, not a measurement.
8. **The ladder's tail (slots past ~12) and the whole LANDS bucket are unvalidated by real play**
   (0 of 238 sheds saw a land in hand; every cleanup shed exactly one card, so only index 0 was ever
   consumed). Their only consumer is the rollout. I have not invented confidence there and the
   integration should not either.
9. **Unclaimed Territory and the off-colour Dragons are jointly live or jointly dead.** §6.2's
   promotion and §6.1's gate are two views of one fact, and I have specified them separately. If that
   coupling is judged too clever for a cleanup ranking, the simpler policy — sink both
   unconditionally — costs the deck very little and I would not argue against it.

---

## 11. Honest assessment — read before approving

**Expect no metric win, and say so up front.** The adoption bar is **non-inferiority** plus doctrine
quality, and three facts bound the upside here more tightly than on the peer decks:

* **The incumbent is not inverted on this deck.** Max-MV is *measured load-bearing* here (+0.1239 when
  removed) and already right about the biggest class of shed (a spare payoff, 110 of 238). Minotaur
  and Dragons each got a policy because the fallback was backwards; Dragonstorm's fallback is
  backwards only about the **rituals**, which are 12 of 238 real sheds (5%).
* **The rule fires less than on any peer**: 530x real-to-rollout, versus 2,528x and 10,020x, and
  exactly one card per cleanup.
* **The deck is fast** (avg 4.53 turns at d5/b20) and the shed only ever happens in mana-screwed
  hands, many of which are lost whatever is shed.

**What is nonetheless worth shipping**, in order of my confidence:

1. **The fuel order (§6.4).** This is the real finding and it is measured, not argued: the incumbent
   pitched Irencrag Feat and Seething Song 10 times out of 12 and Rite of Flame 0 — the deck's two
   best accelerants first and its weakest last. A storm deck's mana is its life.
2. **The colour gate (§6.1).** Karrthus and Kolaghan are dead cards that currently outrank three
   live ones.
3. **The Medallion and Bloom promotions (§5.2).** A card worth two rituals should not be the third
   most expendable nonland.
4. **The `discard_protect: "deck"` question (doubt 1)**, which is not part of this policy at all and
   may be worth more than all of it.

**What to measure**, in the order the repo's own history recommends (behavioural diff first — on
Minotaur round 4 the cheap half produced everything and the expensive half produced nothing):

1. `test/tools/discard_behaviour_diff.py` at d0 — does the policy fire, and is the first divergence
   the doctrine working? (Expect `Irencrag Feat → Rite of Flame` and `spare Dragonstorm → Karrthus`.)
2. `MTG_DSTORM_BUCKET_DISCARD=0/1` direct A/B, paired, at d0 **and** at the shipped d5/b20 — d0 is
   where this axis has power (the Minotaur numbers were 10-100x larger at d0 than at d3).
3. Smoke + regression through the accept flow, with the byte-identity check on the `=0` arm.
4. Separately and first, the `MTG_DISCARD_PROTECT=deck` arm from doubt 1.

**Un-run, recorded as un-run:** the skill's rule-vs-searched zero-regret label check. Per
`per-deck-discard-analysis-phase.md` the labeller probes only the CR 514.1 cleanup, which this deck
*does* reach (unlike Minotaur) — 15 times per 200 games at d5 — so it is genuinely runnable here and
was simply not run as part of authoring this document.
