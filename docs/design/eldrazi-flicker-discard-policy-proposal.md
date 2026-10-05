# EldraziDisplacerFlicker — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored per `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 sweep. **Deliverable
is this document; no `.cpp`/`.h` was touched.** Integration is central: the shipped form is
`EldraziFlickerProvider::CleanupDiscardCandidates`, returning a shed order (most expendable first)
through `CleanupDiscardRankingWithOrder(s, required_pieces, shed)`, behind a default-on
`EnvOn("MTG_EDF_BUCKET_DISCARD", true)` with `=0` restoring `GenericProvider::CleanupDiscardCandidates`
as the A/B hatch. (`MTG_EDF_BUCKET_DISCARD` is unused today — checked against every `MTG_EDF_*` in
`src/`.) Every card fact below is read from `src/cards/data/cards.json`, never recalled.

---

## 1. The deck, and therefore its shape

EldraziDisplacerFlicker is an **infinite-mana COMBO deck with no combat plan at all**, and the engine
already says so in its own voice: `EldraziFlickerProvider::ComboCardValue` re-prices a roleless body
at "the generic floor, because it never attacks", and `ProvenWinlessThisTurn` exists precisely because
the deck can hold unbounded mana and still not win. Functioning means assembling **five** things, and
`ComboOffPossible`'s five-row rule table is the authoritative list of them: an **OUTLET**
(`blink_cost` — 3 Eldrazi Displacer, 4 Emiel the Blessed), an **untap PAYLOAD** (`etb_untap_lands` —
3 Cloud of Faeries, 4 Peregrine Drake) whose refund exceeds the outlet's cost, **enough LANDS** for
that refund to be positive at all (the payload untaps *lands*, so land COUNT is a combo input, not
just a curve input), a **SINK** to cash the mana on (the maindeck holds exactly ONE — Shivan Gorge,
red-gated; both real finishers are sideboard singletons reachable only off Living Wish), and the
scarce **{C} supply** plus the **activation-cost reducer** (Training Grounds) that decide whether the
loop's per-iteration arithmetic closes. So the shape is **combo**, bucket count **five**, with the
mana bucket carrying the user's LANDS-vs-ACCELERATION sub-split (23 lands = land drops, 16 land Auras
= acceleration) and a hard **{C}-source sub-quota** that no mana-value rule can see. There is **no
separate dig/cantrip bucket**: every repeatable draw source in this deck is a LAND (Mariposa's
`tap_draw_cost`, Conservatory's and Kitchen's `tap_investigate_cost`), so "dig" is a sub-role inside
mana, and the two tutors are fungible with the parts they fetch rather than being a bucket of their
own (§3.6).

**The bottleneck is measured, and it is the SINK.** `decks/EldraziDisplacerFlicker/screen_sinks.json`
records 60 logged games: 53% get outlet + Peregrine Drake onto the battlefield, 55% get a sink, and
only **33% get both**; 84% of boards holding engine+sink went off, and of the 13 that assembled and
did not, **nine had no sink at all**. That number, not intuition, is why sink access sits at the top
of the keep ladder.

---

## 2. Card-by-card role table

Read from `cards.json`. `mv` is printed mana value; the `params` column names the key I would
classify on. Sideboard cards are listed because Living Wish (`tutor_types: [Creature, Land]`,
`wish_from_sideboard`) puts them **in hand**, so the ranking must name them.

### Main deck (60)

| # | card | cost | mv | `params` keyed on | bucket |
|---|---|---|---|---|---|
| 3 | Eldrazi Displacer | `{2}{W}` | 3 | `blink_cost {2}{C}`, `blink_returns_tapped` | **OUTLET** |
| 4 | Emiel the Blessed | `{2}{W}{W}` | 4 | `blink_cost {3}`, `blink_own_only`, Legendary | **OUTLET** |
| 3 | Cloud of Faeries | `{1}{U}` | 2 | `etb_untap_lands 2`, `cycling_cost {2}` | **PAYLOAD** |
| 4 | Peregrine Drake | `{4}{U}` | 5 | `etb_untap_lands 5` | **PAYLOAD** |
| 4 | Living Wish | `{1}{G}` | 2 | `tutor_to_hand` + `wish_from_sideboard` | **SINK (access)** |
| 1 | Eladamri's Call | `{G}{W}` | 2 | `tutor_to_hand`, `tutor_types [Creature]` | **proxy** → outlet/payload (§3.6) |
| 2 | Training Grounds | `{U}` | 1 | `reduces_creature_activation 2` | **ENABLER** |
| 4 | Wild Growth | `{G}` | 1 | `is_land_aura`, `extra 1`, `produces [G]` | mana / **aura** |
| 4 | Fertile Ground | `{1}{G}` | 2 | `is_land_aura`, `extra 1`, `produces []` (wild) | mana / **aura** |
| 4 | Trace of Abundance | `{R/W}{G}` | 2 | `is_land_aura`, `extra 1`, `produces []`, `grants_shroud` | mana / **aura** |
| 4 | Overgrowth | `{2}{G}` | 3 | `is_land_aura`, `extra 2`, `produces [G]` | mana / **aura** |
| 4 | Brushland | — | 0 | `produces [G,W,C]`, `tap_self_damage 1` | mana / **land + {C}** |
| 4 | Yavimaya Coast | — | 0 | `produces [G,U,C]`, pain | mana / **land + {C}** |
| 2 | Adarkar Wastes | — | 0 | `produces [W,U,C]`, pain | mana / **land + {C}** |
| 3 | Aether Hub | — | 0 | `produces [C,W,U,B,R,G]`, `etb_energy 1`, `energy_per_colored_tap 1` | mana / **land + {C} + one-shot fixer** |
| 3 | Mariposa Military Base | — | 0 | `produces [C]`, `tap_draw_cost {5}`, `etb_optional_tapped_rad 2` | mana / **land + {C} + DRAW** |
| 4 | Conservatory | — | 0 | `produces [G,W]`, `enters_tapped`, `tap_investigate_cost {4}` | mana / **land (no {C}) + DRAW** |
| 2 | Kitchen | — | 0 | `produces [G,U]`, `enters_tapped`, `tap_investigate_cost {4}` | mana / **land (no {C}) + DRAW** |
| 1 | Shivan Gorge | — | 0 | `produces [C]`, `tap_damage_cost {2}{R}`, `tap_damage_each_opponent 1`, Legendary | mana / **land + {C}**, *and* **SINK** when red is live |

### Sideboard (8 — reachable in hand only via Living Wish; each a singleton consumed on fetch)

| card | cost | mv | `params` | bucket |
|---|---|---|---|---|
| Essence Depleter | `{2}{B}` | 3 | `drain_cost {1}{C}`, `drain_amount 1` | **SINK (finisher)** |
| Dimensional Infiltrator | `{1}{U}` | 2 | `exile_opponent_top_cost {1}{C}` | **SINK (finisher, deck-out)** |
| Eldrazi Displacer | `{2}{W}` | 3 | `blink_cost` | OUTLET |
| Cloud of Faeries | `{1}{U}` | 2 | `etb_untap_lands 2` | PAYLOAD |
| Adarkar Wastes | — | 0 | `produces [W,U,C]` | mana / land + {C} |
| Mariposa Military Base | — | 0 | `produces [C]`, `tap_draw_cost` | mana / land + {C} + DRAW |
| Azorius Chancery | — | 0 | `produces [W,U] x2`, `enters_tapped`, `etb_bounce_land` | mana / land (**karoo, no {C}**) |
| Vexing Shusher | `{R/G}{R/G}` | 2 | **`parameters: {}`** | **NO BUCKET** |

### Ambiguous roles, flagged

* **Shivan Gorge is in two buckets at once** — a {C} land *and* the only maindeck sink. It is resolved
  as a LAND with a sink promotion (§3.4), because as a sink it is conditional on a red source
  (`GorgeKillLive` = `gorge_dmg > 0 && HasRedSource`), and the deck runs **no red land**: red comes
  only from Fertile Ground / Trace of Abundance (wild) or an energy'd Aether Hub.
* **Mariposa Military Base is in three** — {C} source, repeatable draw (`D`, an ingredient in 3 of
  the 5 combo-off rules), and a wish target. Resolved as a land with the DRAW sub-role promotion.
* **Living Wish is the most fungible card in the deck** — it can fill the sink, outlet, payload *or*
  land bucket. It is assigned to SINK because that is the slot **nothing else in hand can fill**.
* **Vexing Shusher is in no bucket at all**, deliberately: its `parameters: {}` is documented in
  `cards.json` as keeping "this {T}-less repeatable mana sink out of the go-off". It is the deck's
  one genuinely dead card and heads the shed order.

### `[bracket note]`s that change where a card sits

1. **Emiel's counter trigger is NOT a payoff.** `cards.json`: "the arithmetic is the opposite of what
   it looks like: CR 400.7 makes the returned permanent a NEW OBJECT, so each blink WIPES the counter
   the previous iteration added … A 13-iteration loop leaves ONE counter, not 13." So Emiel is an
   outlet and nothing else — it must not be ranked as a threat. The same note records that the
   optional `{G/W}` "is paid whenever affordable", which means an **Emiel iteration really costs `{3}`
   plus `{G/W}`** while `FlickerEconomics` prices only the `blink_cost`. That is a real economic
   difference from Displacer's `{2}{C}` and it feeds the outlet order (§4.1).
2. **An aura's bonus is never `wild_c`.** Fertile Ground and Trace of Abundance make "one mana of any
   color", credited as wild, "deliberately not as wild_c: a colour cannot pay a {C} pip". So **all 16
   land Auras can never pay a {C} pip** — and neither can Conservatory or Kitchen, which makes **22 of
   the deck's 39 mana sources `{C}`-blind**. That is what makes the {C} sub-quota a separate thing
   from the mana quota.
3. **Trace of Abundance's shroud is symmetric and modelled** (`land_aura_grants_shroud`): "a land
   already carrying a Trace can take NO further auras". A hand aura whose only host is shrouded is a
   blank.
4. **Overgrowth's bonus is two GREEN, not two wild** — "an Overgrowth'd land cannot pay Eldrazi
   Displacer's {C} off the bonus", and it also cannot pay Trace's `{R/W}` pip. This is why Overgrowth
   is the worst aura despite making the most mana.
5. **Training Grounds has a one-mana floor.** "{2}{C}" becomes "{C}" and `{3}` becomes `{1}`; the
   floor is enforced. One copy therefore already maxes both reductions — **copy 2 is provably inert**.
6. **Aether Hub's energy is a PLAYER resource** with `etb_energy 1`, so copies are *not* redundant
   (each brings its own one-shot any-colour, i.e. its own one-shot Shivan Gorge enabler); and "in
   HAND the card is deliberately NOT stripped", so the hand-side heuristics correctly see a real fixer.
7. **Dimensional Infiltrator is only a sink while the opponent's library is modelled** —
   `IsTlessFinisher` gates on `s.opponent_library_dealt && !s.opponent_decked`. Classify through that
   helper, not through the raw param.
8. **Azorius Chancery is a KAROO** (`etb_bounce_land`) — the Minotaur/Dragons caveat applies verbatim.

---

## 3. The buckets, with quotas — every one NET OF BOARD

Census the battlefield **first**; the hand owes only the remainder. The board walk is one pass
collecting: `has_outlet`, `has_payload`, `has_sink`, `n_lands`, `n_c_sources`, `n_auras`,
`has_reducer`, `has_draw`, `reach`, `reach_c`, `emiel_in_play`, `gorge_in_play`, `has_red`.

### 3.1 SINK / KILL ACCESS — quota **1**

Filled from the battlefield by: a finisher permanent (`IsTlessFinisher`), **or** Shivan Gorge in play
with a red source live (`GorgeKillLive`). Filled from hand by, in order: a fetched finisher, then a
Living Wish. **This is slot 1 of the ladder** because the funnel measurement says the sink is the
binding gate, because both finishers exist *only* in a sideboard reachable *only* through Living
Wish, and because unbounded mana with nothing to spend it on is exactly the state
`ProvenWinlessThisTurn` was written to detect. The learned profile agrees from the other direction:
Living Wish is the **second-best** card by `card_scores` (+0.360) and the **only** card in the deck
besides Mariposa with a *positive* second-copy marginal (+0.059) — the sideboard being singletons,
wish #2 genuinely fetches a different card.

### 3.2 OUTLET — quota **1** (similar effects grouped)

`blink_cost.has_value()`. Board presence fully satisfies it: an outlet is a permanent with no `{T}`
in its cost, so one copy serves forever. Three Displacers and four Emiels share the bucket because
they do the same job; they are ordered inside it (§4.1), not split.

### 3.3 PAYLOAD — quota **1** (similar effects grouped)

`etb_untap_lands > 0`. Board presence fully satisfies it — the loop blinks the *same* body every
iteration. Drake and Cloud share the bucket; §4.2 orders them, and this is the one bucket where the
distance term genuinely flips the answer.

### 3.4 MANA — the sub-split, with a hard colourless sub-quota

Parent quota binds; sub-quotas express preference within it and are **fungible upward** (a hand with
no auras keeps a seventh land; a hand with lands already on board keeps a third aura).

* **3.4a LANDS (land drops) — reach 6, counting the battlefield.** Six is the deck's *own* number,
  not an invention: `EldraziFlickerProvider::TutorCandidates` prices a wished land at
  `30 + max(0, 6 - lands) * 5`, with the comment "Six is where this deck's loop stops needing more
  lands than it untaps". Lands are not interchangeable with auras for this count — the payload untaps
  *lands*, so land count sets the loop's refund ceiling while an aura only raises one land's yield.
* **3.4b {C}-CAPABLE SOURCES — sub-quota 2, counting the battlefield (the rule table's `C2`).**
  `EffectiveProduces` contains `Color::Colorless`. In this deck that is Brushland, Yavimaya Coast,
  Adarkar Wastes, Mariposa, Shivan Gorge, Aether Hub — and **not** Conservatory, **not** Kitchen,
  **not** Azorius Chancery, **not** any of the 16 auras. Three of the deck's key activations are pure
  `{C}` pips (`blink {2}{C}`, `drain {1}{C}`, `exile {1}{C}`), and `FlickerTopLandYields`' own header
  records the failure mode verbatim: "with ONE colourless source on board, a Displacer iteration
  produces one {C} and spends one {C}, so the sink can never be fed however long the loop runs —
  USER, EDF seed 7". Relaxes to **1** when the outlet in play is pip-free (Emiel — this is exactly
  the `E` substitution in `ComboOffPossible`) **and** the live sink carries no `{C}` pip.
* **3.4c COLOUR COVERAGE — one source each of {W}, {U}, {G}, counting the battlefield.** {W} for the
  outlets, {U} for both payloads *and* Training Grounds, {G} for the auras and both tutors. Nearly
  free (every land but Mariposa and Shivan Gorge covers two of the three), which is why it is a
  tie-break inside the land slots rather than a slot of its own. {R} is wanted by **one card**
  (Shivan Gorge's activation) and {B} by **one** (casting a fetched Essence Depleter), and the only
  sources of either are the wild auras and an energy'd Aether Hub — see §4.3 and §8.
* **3.4d AURAS (acceleration) — sub-quota 2, and only while a legal host exists.** `is_land_aura`.
  A host means a non-shrouded land on the battlefield, or a land in hand to play. With neither, every
  aura in hand is a blank.
* **3.4e DRAW-CAPABLE LAND — sub-quota 1 while no draw source is on board.**
  `tap_draw_cost || tap_investigate_cost`. This is the `D` ingredient, live in three of the five
  combo-off rows, and it is the *only* thing that makes a Living Wish still in the LIBRARY count
  (`WishReachesFinisher`'s `draws` argument). Mariposa fills 3.4b, 3.4e and the wish pool at once.

### 3.5 ENABLER (Training Grounds) — quota **1**, hard

`reduces_creature_activation > 0`, satisfied by a battlefield copy. This is a genuine fifth bucket,
not a nicety, and the arithmetic is the argument:

| | blink cost | net per iteration, 5 lands untapped |
|---|---|---|
| Displacer, no reducer | `{2}{C}` = 3 | +2 |
| Displacer + Training Grounds | `{C}` = 1 | **+4** |
| Emiel, no reducer | `{3}` = 3 | +2 |
| Emiel + Training Grounds | `{1}` = 1 | **+4** |
| **Cloud of Faeries** payload, no reducer | vs a 3-mana blink | **−1 — not a loop at all** |
| **Cloud of Faeries** + Training Grounds | vs a 1-mana blink | +1 |

It also halves the kill: both finishers' `{1}{C}` becomes `{C}`. `screen_sinks.json` independently
calls Cloud "a marginal engine (it needs an aura'd land to be net-positive at all) where the Drake
never is", and the provider's own tutor-ranking note records "the logged turn-6 kill was gated on
Training Grounds, not on the Infiltrator". **Copy 2 is provably inert** (one-mana floor, §2 note 5)
and is the deck's best shed after a true blank.

### 3.6 The tutors: NOT a dig bucket — each is fungible with the part it fetches

The brief asks whether a tutor belongs in a dig bucket. For this deck, **no**, and the reason is that
the two tutors have **different reach**, so one shared bucket would assert they are similar effects
when they are not:

* **Living Wish** searches the *sideboard* (`wish_from_sideboard`), which is the **only** place
  Essence Depleter and Dimensional Infiltrator exist. Shedding the last one **deletes the kill**.
  → it *is* the SINK bucket (§3.1). Secondarily it reaches a land, a spare Displacer or a spare Cloud,
  so a surplus copy is still a real card, not a blank.
* **Eladamri's Call** searches the *library* for a **creature** — i.e. an OUTLET or a PAYLOAD, and
  nothing else. It can never reach a finisher. → it claims `outlet1` or `payload1` when that slot is
  otherwise unfilled, at mv 2 (cheaper than either piece, so it is a discount as well as a finder);
  and when **both** parts are already covered across board+hand it can only fetch a redundant copy,
  which makes it nearly dead and puts it in the overflow tier.

This is "a tutor is worth roughly the piece it is missing" applied **per tutor**, which is the only
way to get it right here.

---

## 4. Within-bucket order, and the distance-to-playable term

**REACH.** Use the deck's own function: `reach = FlickerTopLandYields(s, me, kFlickerMaxUntaps)` —
the full-untap yield of our lands with aura bonuses included, i.e. the same quantity
`MTG_EDF_WISH_CAST_GATE` already calls `capacity` — plus `s.floating_mana.Total()`, plus 1 if a land
drop is still available and a land is in hand. `reach_c` is its `out_colorless` companion. Mana value
alone cannot answer this deck's questions (that function's header says so explicitly), and reusing it
means the discard ranking and the go-off recognizer cannot drift apart about what is castable.

### 4.1 OUTLET order — board-conditional on `{C}`

* `reach_c >= 1` → **Eldrazi Displacer** first. Cheaper to cast (mv 3 vs 4, one white pip vs
  `{W}{W}`), and under Training Grounds its `{C}` blink is the cheapest activation in the deck. Its
  per-iteration cost is also *honestly* 3, where Emiel's is 3 **plus the `{G/W}` it pays whenever
  affordable** (§2 note 1).
* `reach_c == 0` → **Emiel the Blessed** first. Its `{3}` carries no `{C}` pip at all, which is the
  whole content of `EmielInPlay` and the reason `E` substitutes for a second `{C}` source in three of
  the five combo-off rows. On a board with no colourless, a Displacer is an outlet that cannot be
  activated.
* Tie-break: fewer coloured pips, then lower mv.

### 4.2 PAYLOAD order — this is where distance flips the answer

* `reach >= 5` → **Peregrine Drake** first. Five untaps beat any outlet's cost unaided, and
  `cards.json` notes the cast is itself "mana-neutral via the tap-ahead", which is exactly why the
  threshold is 5 and not higher.
* `reach < 5` → **Cloud of Faeries** first. The Drake is not castable and the Cloud is; and the Cloud
  is the payload that is *never* fully dead, because `cycling_cost {2}` converts it into a card.
* The demotion is erased by the same promotions that erase it elsewhere: a payload already on the
  battlefield (the loop refunds the lands), or a resolved Training Grounds (which is what makes the
  Cloud net-positive in the first place).

### 4.3 AURA order — distance first, colour flexibility as the tie-break

Primary key is distance-to-playable, per the brief's rule 4; colour flexibility breaks ties.
**Shed order among surplus auras:** Overgrowth → Trace of Abundance → Fertile Ground → Wild Growth.

* **Overgrowth sheds first** on both keys at once: mv 3 (the only aura the early board cannot cast)
  *and* two GREEN that can pay neither a `{C}` pip nor Trace's `{R/W}`.
* Trace before Fertile among the mv-2 wild auras: Trace's `{R/W}` resolves to white in practice here
  (the only red on an aura-less board is an energy'd Aether Hub), so it needs {W}+{G} where Fertile
  needs {G}+anything; and Trace's shroud **locks its host out of further auras**.
* **Wild Growth is kept last** — mv 1, castable on any green board, and the cheapest funder of the
  rest of the chain (which is the user's own already-adopted reason for `MTG_EDF_AURA_CHEAP_FIRST`).
* When `reach >= 3` all four are castable and the key collapses to colour flexibility alone:
  Overgrowth → Wild Growth → Trace → Fertile.

### 4.4 LAND order — {C} first, then colour, then untapped, then draw

Shed order among surplus lands: non-{C} **and** enters-tapped (Conservatory, Kitchen) → other non-{C}
(Azorius Chancery) → a {C} land whose colours are already covered → the {C} lands that also carry a
sub-role, kept longest: **Aether Hub** (its own energy = a one-shot any-colour and the only non-aura
red, i.e. a one-shot Shivan Gorge enabler) and **Mariposa** ({C} + the `D` draw engine + a wish
target, and the only land with a *positive* learned second-copy marginal, +0.036). The FIRST
Conservatory or Kitchen is not surplus at all while nothing on board draws — it fills 3.4e.

### 4.5 SINK order

A fetched finisher in hand outranks a Living Wish — it *is* the kill and needs no extra spell — **but
only if it is castable-ish.** Essence Depleter is `{2}{B}` and **nothing in the deck taps for {B}
outright**: only a wild aura (Fertile Ground / Trace of Abundance) or an energy'd Aether Hub can cast
it, so on a board with neither it drops behind the Wish — which can go fetch the `{1}{U}` Infiltrator
instead, castable off Yavimaya Coast, Adarkar Wastes, Kitchen or a Hub. Among two finishers in hand,
shed **Essence Depleter before Dimensional Infiltrator** on the same cast-distance grounds.

---

## 5. The total order over a hand

Quotas are **interleaved, not filled bucket-by-bucket** — the Minotaur lesson: "five mana sources"
and "one payload" are both constraints on the same seven-card hand, so the ranking has to say which
land beats which piece. Read forwards this is the quota fill; read backwards it is the order the
quota-protected cards give way in.

### 5.1 The keep ladder (most-kept first)

```
sink1  >  land1  >  outlet1  >  payload1  >  land2({C})  >  enabler1  >  land3  >  aura1
       >  land4  >  aura2  >  land5  >  sink2  >  land6  >  outlet2  >  payload2  >  aura3
```

* `land2` is **required to be {C}-capable** if `land1` was not, and vice versa — that is the `C2`
  sub-quota expressed inside the ladder rather than as a separate pass.
* `enabler1` sits at slot 6, above `land3`, because Training Grounds is worth roughly two lands *per
  iteration* (§3.5) and the deck runs only two copies. See §8 for the one piece of evidence that
  disagrees.
* `aura1` claims its slot **only if a legal host exists**; otherwise it falls straight to the
  overflow tier as a blank.
* `payload1` and `outlet1` are resolved by §4.1/§4.2 *before* slotting, so the ladder never protects
  the uncastable member of a pair.
* Eladamri's Call claims `outlet1` or `payload1` when that slot is otherwise unfilled (§3.6).

### 5.2 The overflow tier — this is the head of the returned shed vector

Index 0 of the returned vector is the first entry below that still exists in hand:

| | shed first → | why |
|---|---|---|
| **O1** | **Vexing Shusher** | `parameters: {}` — in no bucket. A 2/2 that cannot ramp, sink, block or matter. |
| **O2** | **a legend-dead copy** — Emiel the Blessed or Shivan Gorge whose name is already on our battlefield | the legend rule makes it a replacement at best, and in *this* deck the enter event is worth nothing (§7 P2) |
| **O3** | **a second Training Grounds** | provably inert at the one-mana floor |
| **O4** | **a hostless land aura** — no non-shrouded land on board and no land in hand | a blank; the aura analogue of the karoo caveat |
| **O5** | **a self-bouncing Azorius Chancery** — no other land on board and no other non-karoo land in hand | the KAROO CAVEAT; strictly worse here than Minotaur's Carnarium because the Chancery is not even a {C} source |
| **O6** | **Eladamri's Call with both parts covered** across board+hand | it can only fetch a redundant copy |
| **O7** | **surplus land auras**, §4.3 order | the three worst learned second-copy marginals in the deck are aura second copies (Fertile −0.467, Overgrowth −0.423, Wild Growth −0.408) |
| **O8** | **surplus lands** past the 6-land / 2-{C} quota, §4.4 order | |
| **O9** | **surplus outlets / payloads**: Emiel → Drake → Displacer → **Cloud last** | no removal exists in a goldfish, so redundancy buys nothing; Cloud is last because `cycling_cost {2}` means it is never fully dead |
| **O10** | **surplus sink access** — a third+ Living Wish, or a second Living Wish once a finisher is already in hand or on board; a spare finisher (Depleter before Infiltrator) | |

Nothing is omitted: every hand card lands in a ladder slot or an overflow tier, so index 0 is always
determined. **This is the trap the brief names**, and it matters acutely here: anything unnamed falls
through to the shared tier B, **descending mana value**, whose index 0 on this deck is **Peregrine
Drake (mv 5) — the deck's engine**, ahead of a spare Overgrowth and a hostless Wild Growth. The
learned profile makes the same point from its own direction: the two highest `card_scores` in the
deck are Eldrazi Displacer (+0.627) and Living Wish (+0.360), and max-MV reaches past both.

---

## 6. The param-level classification predicates

All read through `CardDatabase::Instance().LookupCached(card)` — a hand card is a name-only
placeholder. Every one of these already exists in `src/ai/DecisionProviders.cpp`; reusing them is what
keeps the discard ranking and the go-off recognizer from disagreeing about what a "piece" is.

| predicate | source | bucket |
|---|---|---|
| `DefIsBlinkOutlet(d)` → `d->params.blink_cost.has_value()` | existing | OUTLET |
| `DefIsUntapPayload(d)` → `d->params.etb_untap_lands > 0` | existing | PAYLOAD |
| `comborules::IsTlessFinisher(s, d)` → `drain_cost && drain_amount>0`, **or** `exile_opponent_top_cost && s.opponent_library_dealt && !s.opponent_decked` | existing | SINK (finisher) |
| `d->params.tutor_to_hand && d->params.wish_from_sideboard` | existing (tutor rank) | SINK (access) — Living Wish |
| `d->params.tutor_to_hand && !d->params.wish_from_sideboard && tutor_types has Creature` | new, 2 lines | outlet/payload proxy — Eladamri's Call |
| `d->params.reduces_creature_activation > 0` | existing (`CastOrderRank`) | ENABLER |
| `d->params.is_land_aura` | existing | mana / aura |
| `d->params.land_aura_produces.empty() && land_aura_extra_mana > 0` = `LandAuraMakesAnyColor` | existing | aura colour flexibility (§4.3) |
| `d->params.land_aura_grants_shroud` + `LandHasShroud` | existing (`SpellEffects.h`) | host legality (O4) |
| `CleanupDiscardIsLand(card)` | existing helper | mana / land |
| `EffectiveProduces(s, me, *d, in_hand)` contains `Color::Colorless` | existing | {C} sub-quota (3.4b) |
| `d->params.tap_draw_cost \|\| d->params.tap_investigate_cost` = `HasRepeatableDrawSource`'s test | existing | DRAW sub-role (3.4e) |
| `d->params.tap_damage_cost && d->params.tap_damage_each_opponent > 0`, gated by `comborules::HasRedSource` | existing (`GorgeKillLive`) | Shivan Gorge's sink promotion |
| `d->params.etb_bounce_land` | existing | karoo caveat (O5) |
| `d->params.enters_tapped` | existing | land tie-break (§4.4) |
| `d->params.cycling_cost.has_value()` | existing | Cloud shed-last (O9) |
| `d->card.HasSupertype(Supertype::Legendary)` + a battlefield name match | existing | legend-dead copy (O2) |
| `FlickerTopLandYields(s, me, kFlickerMaxUntaps, &reach_c)` + `s.floating_mana` | existing | the REACH term (§4) |

**No card names anywhere except the two deck-specific VALUE ORDERS** the brief permits: the aura shed
order (§4.3) and the surplus outlet/payload order (O9). Both are stated as *rules over params* first
(mv, `land_aura_produces` emptiness, `cycling_cost`, Legendary) with the card names given only as
what those rules evaluate to on this decklist — so a screening arm that swaps an aura or a payload
keeps the right bucket and the right order.

---

## 7. State promotions

### Proposed (a cleanup ranking can actually act on these)

* **P1 — the {C}-starvation promotion.** While board+hand hold ≤1 {C}-capable source, a {C}-capable
  land outranks *both* a non-{C} land and `aura2`. Twenty-two of the deck's 39 mana sources can never
  pay a `{C}` pip, and the user has already reported the failure in play: "both {C} sources died, and
  neither the blink nor the exile could ever be activated again" (the incident that
  `MTG_EDF_C_CONSERVE` exists for).
* **P2 — the legend-dead-copy promotion.** A hand copy of a Legendary card already on our battlefield
  sheds ahead of every live card (Emiel the Blessed, Shivan Gorge). **The precedent caveat is
  respected:** `HandShedIsPayoff`'s neighbouring comment records that a duplicate legend cast is *not*
  universally a tie (Lathliss, Lyra). Here it is, and it is the **deck** that makes it one — the only
  enter event is Emiel's own `{G/W}` counter, which `cards.json` documents as value-not-wincon and
  which the next blink wipes, and `ComboCardValue` prices a roleless body at the floor "because it
  never attacks".
* **P3 — the inert second reducer.** Copy 2 of Training Grounds is dead at the one-mana floor. Read
  off the card text plus `EffectiveActivationCost`, not guessed.
* **P4 — the hostless aura.** `is_land_aura` with no non-shrouded board land and no land in hand is a
  blank. **Worth adopting as a MULLIGAN rule independently of this policy**, exactly as the karoo
  caveat was: this deck can keep four auras and no land, and all four are blanks.
* **P5 — the karoo caveat**, for a wished Azorius Chancery. Same rule as Minotaur's Rakdos Carnarium
  and Dragons' Gruul Turf.
* **P6 — the sink-access floor.** While no sink exists on board or in hand, the **last** Living Wish
  is never shed. This is the discard-side twin of the already-built, still-unadopted
  `MTG_EDF_WISH_SINK_FLOOR`, and it rests on the 33%/nine-of-thirteen funnel measurement rather than
  on taste.

### Deliberately rejected as SEARCH-owned

* **R1 — "cycle the surplus Cloud instead of shedding it."** `cycling_cost {2}` is a cast decision
  with a mana cost and a priority window; a cleanup ranking cannot spend mana. (Same shape as
  Minotaur's Burning-Fist-ammunition rejection.) It does justify Cloud being shed **last** among
  surplus payloads, which is the part a ranking *can* express.
* **R2 — "would this shed change whether the loop kills this turn."** That is an arithmetic
  projection over iteration counts, bankable mana and `{C}` pips — `ComboOffPossible`, `BankableMana`,
  `Fundable`, `ExtraLethalDamage`. Re-deriving it here would create a second model that can disagree
  with the one that actually pays, which is the failure mode those functions' own comments guard
  against.
* **R3 — Mariposa's rad mode.** The rad mill moves cards library→graveyard, never to hand, so it has
  no interaction with hand size. Explicitly no promotion.
* **R4 — shedding to dodge painland / rad life loss.** `cards.json` records our life total as inert
  against the passive opponent (Brushland, Adarkar Wastes, Essence Depleter's lifegain). No promotion.
* **R5 — there is NO hand-size payoff in this deck**, so Minotaur's Neheb inversion ("discarding is a
  benefit") does **not** transfer. Verified: no card here carries `hand_size_anthem_max`. Stated
  explicitly so the promotion is not copied across by analogy.

---

## 8. Interaction with the engine's `required_pieces` protection — READ THIS ONE

The brief asks which cards I expect to be engine-protected already, so the ranking does not fight it.
**The answer is: none.**
`decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json` carries
`mulligan.required_pieces: []`. `CleanupDiscardProtected` returns `false` immediately for a name not
in that list, so **no card in this deck is protected today** and nothing this ranking says will be
vetoed. The ranking carries the entire load — which is the strongest reason for it to name the whole
hand.

**And a warning, because the obvious "improvement" would break it.** `MulliganProfile::discard_protect`
defaults to `DiscardProtectScope::All`, and under `All` the helper returns `true` for a listed name
**unconditionally, before any redundancy counting**. So populating `required_pieces` with the four
piece names would protect **every copy** of Eldrazi Displacer, Emiel, Cloud of Faeries and Peregrine
Drake, drop them all from tier A, and hand the surplus decision back to tier B's descending mana
value — whose first pick is the Drake. That is precisely the regression this policy exists to prevent.

Recommendation: **leave `required_pieces` empty.** If it is ever populated for the *mulligan's* sake,
then also (a) set `discard_protect` to `hand` or `deck`, and (b) implement
`EldraziFlickerProvider::InterchangeableRequiredGroup` returning
`{Eldrazi Displacer, Emiel the Blessed}` for the outlets and `{Cloud of Faeries, Peregrine Drake}` for
the payloads. This deck is the textbook case for that hook: two distinct cards fill one role in each
of two buckets, and name-only counting would make **both** read as "last copy" and protect both —
exactly the AntiLifegain defect (`antilife s3003 gi226`) the group hook was built to fix.

---

## 9. Doubts, flagged — the user reviews and amends these

1. **Training Grounds at ladder slot 6 is my single least-certain call, and the learned profile
   contradicts it.** `card_scores` ranks Training Grounds **last in the whole deck** (−0.410) while my
   doctrine calls it the loop's multiplier. Both readings are defensible: `card_scores[c][k]` is an
   *opening-hand* marginal (see the units warning in `minotaur-discard-policy-proposal.md` round 4),
   and an opening-hand Training Grounds with no outlet and no payload really is a dead `{U}`
   enchantment — which is not the state a cleanup shed happens in. **I went with slot 6** (the
   documented-default rule); the alternative, slot 8-9 behind `land3`/`aura1`, is a one-line change
   and a cheap A/B arm. *Additional caveat: this deck's `card_scores` were generated at `f2cb1fb8`
   (2026-09-02) and **425 `src` commits** have landed since, so every learned number cited in this
   document is a fingerprint of an engine that no longer exists.*
2. **Is the sink really above the first land?** Slot 1 vs slot 2 is the ladder's most consequential
   pair. My case is replacement probability (39 of 60 cards are mana; 4 are Living Wish) plus the
   funnel measurement. The counter-case is that a hand with zero lands does literally nothing, and a
   Living Wish you cannot cast is not access either. A conditional — *sink1 outranks land1 only once
   `reach >= 2`* — is the obvious compromise and I did **not** take it, to keep the ladder readable.
   Worth one A/B arm.
3. **Six lands may be too many to protect from HAND.** Six is the deck's own number, but it was fitted
   for *wishing* a land onto the battlefield, not for *holding* one. Holding a sixth land in hand is
   worth strictly less than having a fifth in play. If the shed census shows the policy hoarding
   lands, cut the ladder's land slots at 5 and let `land6` fall into overflow.
4. **The outlet order's `{C}` condition may be the wrong shape.** I made it a hard board-conditional
   switch (`reach_c == 0` → Emiel). It could instead be a graded preference, or it could reasonably
   read hand {C} sources too (a Brushland in hand is one land drop away). I chose the board-only form
   to match `ColorlessSourceCount`, which is what the five-rule table uses.
5. **Emiel's `{G/W}` per-iteration cost is real but unmodelled in the recognizer.** `FlickerEconomics`
   prices only `blink_cost`, so an Emiel loop's true net is one lower than the recognizer believes
   whenever the `{G/W}` is affordable. I used this only as a *tie-break argument* in §4.1 and did
   **not** build the order on it, because the honest fix is in `FlickerEconomics`, not here. Flagging
   it as a separate finding for whoever owns that function.
6. **The aura shed order uses distance as the primary key and the learned scores disagree with the
   result.** Distance says keep Wild Growth (mv 1); `card_scores` likes Wild Growth too (+0.262) but
   *dislikes* Fertile Ground (−0.173), which my colour-flexibility tie-break keeps longest. The two
   rules agree about Overgrowth being worst and disagree about second place. Low stakes — these are
   overflow cards by construction — but it is a real disagreement and I am not hiding it.
7. **A repeatable draw engine arguably relaxes the sink-access quota.** `WishReachesFinisher` counts a
   Living Wish still in the LIBRARY once a draw source is live, so with Mariposa/Conservatory online
   the last wish in hand is less unique than P6 assumes. Folding that in means counting library copies,
   which is the `MTG_EDF_WISH_SINK_SCARCE` shape ("copy COUNT is deck knowledge a player has"). I left
   it out of the default form deliberately; it is a clean second arm.
8. **Which finisher is "better" is a wish-ranking question I have only half-answered.** §4.5 orders
   them by cast distance (Infiltrator easier), which is right for a discard. Whether the deck-out or
   the drain is the better *kill* depends on library depth vs life total and belongs to
   `TutorCandidates` / `ProjectsAlternateWin`, not here.
9. **Should Shivan Gorge be quota-protected as a SINK rather than as a land?** I made it a land with a
   promotion. The case for treating it as `sink1` is that it is the only maindeck sink; the case
   against is that firing it needs `{2}{R}` and the deck has no red land, so a Gorge with no wild
   aura and no energy'd Hub is just a {C} land. My rule follows `GorgeKillLive` (promote only when a
   red source is actually live), which I believe is right but which does mean a hand holding Gorge +
   Fertile Ground is not credited with a sink until the aura resolves.

---

## 10. Honest assessment — read before approving

**This deck probably sheds rarely in real play, and that is not the denominator.** The deck is a
combo deck that spends its hand; it has no discard outlet and no hand-size payoff. But per the
Minotaur evidence section, the rule that matters runs inside the **search**: the rollout's cleanup
(`SimulateEndAndStartNextTurn`) has no search above it and takes **index 0**, so this ranking biases
every line the search scores. `real == 0` does not mean the rule is inert.

**What the shed census would add, and why it is UN-RUN.** The proper accompaniment to this document is
`MTG_SHED_STATS=1` over ~200 games plus an `MTG_TRACE=discard` sample, to report the real-vs-rollout
ratio and the land-count distribution at the shed (Minotaur: 99 real vs 250,265 rollout, 100% with
<4 lands). **I did not run it:** the box was saturated at authoring time (load average ~28) and
CLAUDE.md permits one batch at a time. Recording it as **UN-RUN**, not deferred — it should be taken
by whoever integrates, alongside the A/B.

**Expected effect on the metric: small, and possibly null at shipped depth.** Note also that this
deck is **not in the regression suite** (`test/regression_cases.sh` has no EldraziDisplacerFlicker
case) and has **no value sidecar**, so the usual smoke/regression fingerprints will not see it at
all — the measurement has to be a direct paired A/B on this deck (`MTG_EDF_BUCKET_DISCARD=1` vs `=0`),
and adoption rests on **non-inferiority plus doctrine quality**, per the brief.

**Where the doctrine value actually is, in one line:** the generic fallback's index 0 on this deck's
typical land-light full hand is **Peregrine Drake** — the deck's engine, its best card by the untap
arithmetic, and the card `screen_sinks.json` says the Drake-vs-Cloud comparison exists to protect.
The bucket policy sheds a Vexing Shusher, a dead second Training Grounds, a hostless Wild Growth or a
spare Overgrowth instead. That is the behaviour change to look for in the shed census, and it is the
case for shipping.
