# Goblins — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored per `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 sweep. **Nothing is
implemented**; this document is the argument the integration step should read before writing
`GoblinsProvider::CleanupDiscardCandidates` behind a default-on `EnvOn("MTG_GOBLINS_BUCKET_DISCARD",
true)` (`=0` restores `GenericProvider::CleanupDiscardCandidates` as the A/B hatch).

Every card claim below was read out of `src/cards/data/cards.json` (Rule 0). Every mechanism claim
was checked against the engine source, and the check is cited where it matters, because three of this
proposal's load-bearing claims are about what *stacks* and the intuitive answers are wrong.

---

## 0. What the deck does today, and why it is backwards

`GoblinsProvider` derives from `DeckProvider : GenericProvider` and overrides **neither**
`CleanupDiscardCandidates` **nor** `DiscardLandsFirst` (which `GenericProvider` answers `false`,
`DecisionProviders.cpp:369`). So today every Goblins cleanup shed falls through the shared builder's
**tier B — descending mana value** (`src/core/SpellEffects.h:460`).

On this decklist that order is:

    Muxus (6) > Siege-Gang (5) > Krenko (4) = Twinshot Sniper (4) > the {1}{R}{R} lords (3),
    Matron (3), Pashalik (3), Chainwhirler (3) > the {1}{R} cards (2) > Lackey, Skirk, Bolt, Vial (1)

i.e. it sheds **the three biggest crowd-creators first** — Muxus, Siege-Gang, Krenko — and keeps
**Stingscourger** (a {1}{R} 2/2 whose only ability `cards.json` flags GOLDFISH-INERT), a **spare
Aether Vial**, and **Lightning Bolt**. This is precisely the Mirrorwing gi295 / FiveColour-Progenitus
inversion the brief warns about, on a payoff deck whose payoffs are its most expensive cards.

**The real-play census says this fires zero times, and that is the wrong denominator.**
`docs/design/cleanup-discard-measured.md` measured **0 cleanup discard events in 400 d0 games** for
goblins (the joint lowest of the whole suite, tied with slivers), and
`decks/Goblins/Goblins.value.json` records that the deck "wins on turn ~3.79 inside a 6-ply horizon".
A deck that empties its hand by turn 3 does not reach CR 514.1. But per the brief and per
`ShedStats`' own header comment (`src/core/SpellEffects.h:196`), *"`real == 0` does NOT mean the rule
is inert"* — the consumer is the **rollout**: every searched line that declines a land drop, and
every keep the **mulligan generator** plays out rather than mulligans. Index 0 of this ranking
decides each of those with no search above it. Minotaur's equivalent count was 99 real against
250,265 rollout sheds (2,528x), 100% of them at fewer than four lands.

**The rollout count for Goblins is UNMEASURED** — see §9, Doubt 7, for the one command that closes it.
I deliberately did not run it: sixteen agents are authoring in parallel on a
one-batch-at-a-time box.

Two engine facts that shape everything below:
* `decks/Goblins/Goblins.profile.json` has **`required_pieces: []`**, so the engine-enforced
  required-piece protection never fires here. The **staged-card exemption is the only protection**,
  which makes the "name every card in hand" rule absolutely binding: anything this ranking omits
  goes straight back to descending-MV.
* `hand_score_threshold` is `-1e+18`, so the hand-score gate never binds either.

---

## 1. The deck in one paragraph, and therefore its shape

Goblins is mono-red tribal aggro: 23 lands (21 Mountain + 2 utility), **33 Goblin creatures**, and
four cards that are not Goblin creatures (2 Aether Vial, 2 Lightning Bolt). To function it needs
three things, and they are not two. (a) **Red mana and land drops** — the curve is pip-heavy
({1}{R}{R} five times, {R}{R}{R} once, {3}{R}{R}, {4}{R}{R}), which matters because one of the two
utility lands makes no coloured mana at all. (b) **A CROWD of Goblin bodies** — every payoff in the
deck is a function of the count: a lord's +1/+1 is per body, Piledriver is +2 per *other attacking*
Goblin, Krenko makes one token per Goblin, Muxus puts every Goblin MV≤5 from six revealed cards onto
the battlefield, Siege-Gang arrives as four bodies, Skirk eats bodies for mana. (c) **Free
deployment** — 4 Goblin Lackey, 2 Aether Vial, 2 Skirk Prospector and 1 Goblin Warchief exist to put
Goblins onto the battlefield without paying for them, and the deck's own learned `card_scores` rank
**Goblin Lackey +1.3249, four times the next card**, i.e. the engine's own measurement says the
free-deployment role is the single most valuable thing in the deck.

That third need is what stops this from being the brief's simple 2-bucket aggro deck. It is the
brief's **ramp sub-split wearing a different hat**: LANDS (land drops) vs ACCELERATION are different
roles and you keep some of each — except that here "acceleration" is Lackey/Vial/Skirk/Warchief
rather than dorks and rocks, and the two are *not* interchangeable (Vial costs {1}, Skirk and Lackey
cost {R}; every one of them needs a land first, and Skirk additionally needs bodies to eat).

**Shape: aggro with a ramp-shaped acceleration sub-structure and a multiplicative payoff structure.
Three buckets.** The count is argued in §3, including why not 2 and why not 4.

---

## 2. Card-by-card role table

Derived from `src/cards/data/cards.json`. The `params` column names the field the classification
keys on — not the card name (brief trap 2). "MV" is printed; effective MV under a resolved Warchief
is discussed in §5.

### Mana — LANDS (23)

| n | card | MV | `params` keyed on | bucket |
|---|---|---|---|---|
| 21 | Mountain | 0 | `produces: [R]` | MANA/lands |
| 1 | Cavern of Souls | 0 | `produces: [W,U,B,R,G,C]` + **`colored_creature_only: true`** | MANA/lands, with a caveat |
| 1 | Three Tree City | 0 | `produces: [C]` + `mana_per_creature_subtype: Goblin` + `mana_per_creature_feeder_generic: 2` | MANA/lands, **with the deck's Karoo-class caveat** |

### Mana — ENGINES (acceleration / free deployment) (9)

| n | card | MV | `params` keyed on | bucket |
|---|---|---|---|---|
| 4 | Goblin Lackey | 1 | `combat_damage_puts_subtype_from_hand: [Goblin]` | MANA/engines (also a body) |
| 2 | Skirk Prospector | 1 | `sac_creature_outlet` + `sac_outlet_add_mana_amount: 1` / `_color: R` | MANA/engines (also a body) |
| 2 | Aether Vial | 1 | `upkeep_adds_charge` | MANA/engines |
| 1 | Goblin Warchief | 3 | `reduces_spell_subtype: Goblin` (+ `grants_haste`) | MANA/engines (also a multiplier-ish; see §3) |

### Multipliers (11)

| n | card | MV | `params` keyed on | bucket |
|---|---|---|---|---|
| 3 | Rundvelt Hordemaster | 2 | `power_bonus: 1` + `tough_bonus: 1` + `subtypes_affected: [Goblin]` + `lord_excludes_self` (+ `dies_trigger_impulse_exile`) | MULTIPLIER |
| 2 | Goblin Chieftain | 3 | same lord fields + **`grants_haste`** | MULTIPLIER |
| 2 | Goblin King | 3 | same lord fields, nothing else | MULTIPLIER — **provably dominated, see §4** |
| 4 | Goblin Piledriver | 2 | `attack_pump_power_per_other_matching: 2` + `subtypes_affected: [Goblin]` | MULTIPLIER (tighter crowd test, see §4) |

### Crowd / payoffs / reach (17)

| n | card | MV | `params` keyed on | bucket |
|---|---|---|---|---|
| 3 | Muxus, Goblin Grandee | 6 | `etb_reveal_count: 6` + `etb_reveal_put_subtypes: [Goblin]` + `_creatures_only` + `_max_mv: 5` (+ `attack_self_pump_per_other_subtype`) | CROWD (top) |
| 4 | Siege-Gang Commander | 5 | `etb_self_creates_tokens: 3` + `sac_creature_outlet` + `sac_outlet_damage: 2` | CROWD |
| 1 | Krenko, Mob Boss | 4 | `tap_creates_tokens_per_controlled_subtype: Goblin` | CROWD, crowd-gated |
| 1 | Pashalik Mons | 3 | `dies_trigger_damage: 1` + `dies_watch_subtype: Goblin` + `sac_outlet_creates_tokens: 2` | CROWD |
| 1 | Mogg War Marshal | 2 | `etb_self_creates_tokens: 1` + `dies_trigger_creates_tokens: 1` + `echo_cost: {1}{R}` | CROWD |
| 2 | Goblin Matron | 3 | `tutor_to_hand` + `tutor_types: [Goblin]` | CROWD, **role-flexible (see §7)** |
| 1 | Twinshot Sniper | 4 | `etb_damage_any: 2` + `channel_cost: {1}{R}` + `channel_damage: 2` | CROWD/reach, **effective distance 2** |
| 1 | Goblin Chainwhirler | 3 | `etb_damage_each_opponent: 1` | CROWD/reach |
| 2 | Lightning Bolt | 1 | `damage: 3`, `targeting: any` — **not a creature, not a Goblin** | CROWD/reach |
| 1 | Stingscourger | 2 | `echo_cost: {3}{R}` and **nothing else** | CROWD (bottom) |

**Genuinely ambiguous roles, flagged:**

* **Goblin Lackey, Skirk Prospector, Goblin Warchief** are each simultaneously an ENGINE and a Goblin
  body. Resolved by the Minotaur-Ragemonger precedent: classify to the scarce role (ENGINE), and push
  **surplus copies into the CROWD pool** before the ladder runs — they are still Goblin creatures.
* **Goblin Warchief** is arguably a multiplier (it grants haste to the team). I place it in ENGINES
  because its `power_bonus` is **0** — it multiplies nothing statically, it *discounts*, which is what
  the mana bucket is for. Same call Minotaur made for Ragemonger, and for the same reason.
* **Goblin Matron** belongs to whichever bucket the hand is short of; see §7's role-flexibility
  promotion, and Doubt 4.
* **Goblin Piledriver** is a multiplier whose multiplicand is *other attacking Goblins*, not the
  lord's wider crowd. See §4.

### `[bracket note]` incompletenesses that change a card's placement

These are the clauses `cards.json` says the engine does not model, and four of them matter here:

1. **Stingscourger's ETB bounce is GOLDFISH-INERT** ("passive opponent creatures never block/attack").
   Stripped of it, Stingscourger is a {1}{R} 2/2 that **sacrifices itself at the next upkeep unless
   {3}{R} is paid** — 4 mana of investment for a 2/2. It is the weakest card in the deck in this
   engine, and the learned scores agree (-0.3519, second worst). **It is the floor of the crowd order.**
2. **Muxus's "or attacks" reveal is NOT modelled as a reveal** — it is proxied as
   `attack_self_pump_per_other_subtype` (+1/+1 per other Goblin). So the engine's Muxus is a one-shot
   cascade plus a scaling body, not a recurring engine. Muxus is therefore valued *less* than a real
   game would value it, and the ranking should not be tuned as if the attack trigger recurs.
3. **Goblin King's mountainwalk is INERT** and **Goblin Chainwhirler's creature-sweep half is inert**
   ("only matters vs opponent spawn tokens"). Both are load-bearing below: the King inertness is what
   makes the dominance proof in §4 sound, and the Chainwhirler inertness is why its face contribution
   is 1 (×heads), not a board sweep.
4. **Twinshot Sniper's Reach and Goblin Piledriver's protection-from-blue are inert** (no blockers, no
   opponent blue). Neither changes a bucket.

**Sideboard note (out of scope, recorded because it is a coverage gap).** `Goblins.cod`'s side zone
holds 1 Experimental Frenzy + 1 Lightning Bolt, and **Experimental Frenzy is not in `cards.json` at
all**. The engine goldfishes the main zone, so nothing here is affected — but anyone who later moves
Frenzy into the main deck needs the `analyze-deck` + `mtg-rules` route first, and Frenzy would be a
genuinely new bucket question (a top-of-library engine that forbids playing cards from hand, which
interacts with a hand-size shed in a way none of the above covers).

---

## 3. The buckets, with quotas — stated net of board

### Bucket 1 — MANA. Target **4 sources**, netting the battlefield first; never more than 4 hand slots.

**What fills it from the battlefield:** every own land, plus Skirk's ramp (below). The hand owes only
the remainder: `source_need = max(0, target − board_sources)`.

**Why 4 and not 5.** With 4 sources the deck casts every lord (3), Krenko (4), a 3-drop plus a 1-drop
in one turn, and — with one Warchief resolved — Siege-Gang. The only cards 4 does not reach unaided
are Siege-Gang (5) and Muxus (6), and both have non-mana routes (Lackey, Vial, Matron→Lackey). The
deck wins on turn ~3.79, which is three or four land drops. So the 5th source is overflow. **Raise
the target to 5** only while the best kept CROWD card needs 5+ *and* no eraser (§5) covers it; **lower
it by 1 per resolved Warchief**, floored at 3, exactly as Minotaur lowers its target for a resolved
Ragemonger. *This constant is Doubt 1 — it is the single number the cards do not settle.*

**Sub-role LANDS — at least 1, always, and colour coverage comes first.** Two caveats, and the first
is this deck's Karoo:

* **THREE TREE CITY CAVEAT (the Karoo analogue, and the one real mana finding).** Three Tree City's
  base mode is `produces: [C]` — **colourless only**. Its scaled mode is `{2}, {T}: add N red` where
  N = your Goblins (`mana_per_creature_subtype` + `mana_per_creature_feeder_generic: 2`), so its net
  yield is **N − 2**: mana-*negative* at one Goblin, break-even at two, profitable only at three or
  more. The deck has five {1}{R}{R} spells, a {R}{R}{R}, a {3}{R}{R} and a {4}{R}{R}. So **Three Tree
  City counts toward the source quota only if board+hand already holds another {R} source** (or the
  board has ≥3 Goblins, when the scaled mode is genuinely profitable). With no other red source it
  casts nothing in the deck except Aether Vial. Like the Rakdos Carnarium, it is not a blank often —
  but when it is, it is completely one, and it should be **the first card shed**, ahead of a surplus
  Mountain. Implement as a param test (`produces` contains no coloured mana), not a name test.
* **CAVERN OF SOULS CAVEAT (minor, real).** `colored_creature_only: true` — its coloured mana pays
  **creature spells only**. All 33 Goblins are creatures, so Cavern is a full red source for the deck's
  gas; it is a colourless-only source for **Lightning Bolt ({R})** and for **Twinshot Sniper's channel
  ({1}{R})**, neither of which is a creature spell. Consequence for the ranking, and it is the whole
  consequence: with Cavern as the *only* red source, a Lightning Bolt in hand is **not castable**, so
  its distance is +1 and it sheds ahead of an equal-value Goblin.

**Sub-role ENGINES — quota 1** (the brief's "similar effects grouped": all four do the same job, and
the first is what matters). Keep priority, board-conditional:

1. **Goblin Lackey** — against a goldfish *nothing blocks*, so a Lackey cast on turn 1 connects on
   turn 2 with near-certainty and then puts a Goblin **permanent** from hand onto the battlefield every
   combat. It is a repeatable total cost eraser, and the deck's learned first-copy marginal (+1.3249)
   is four times the next card's.
2. **Goblin Warchief** — {1} off every Goblin spell *and* blanket haste. See §5 for why it is also the
   deck's distance eraser and §4 for what a second copy is actually worth.
3. **Skirk Prospector** — converts bodies into {R}. Board-conditional in the strong sense: with no
   expendable Goblin it is a 1/1 and nothing else. `TutorCandidates` already computes exactly the right
   fodder count (`skirk_ramp`: board Goblins **excluding lords**, plus the entering body, **plus Skirk
   itself** — the last activation may eat the Prospector, so N bodies make N mana, not N−1). Reuse it.
4. **Aether Vial** — last. The profile pins `vial_target_mv: 3`, so a Vial charges to 3 and then
   deploys the deck's lords free; but it needs three upkeeps from the turn it lands, and this deck
   wins on turn ~3.79. The user's Minotaur V2 doctrine is the precedent — *"Aether vial is a good
   choice [to shed] given that we will be later than turn 1 when we do so"* — and the learned scores
   agree emphatically: **Aether Vial is the deck's worst card at -0.4916**. **A second Vial is always
   shed** (only one needs to tick). Whether the *first* Vial deserves a quota slot at all is Doubt 5.

**Rule 3 (fungible upward) applies:** the parent MANA total binds; a hand with no engine keeps another
land, and a hand with no land keeps no engine in its place (they cannot substitute — every engine needs
a land first).

### Bucket 2 — MULTIPLIERS. Quota **crowd-conditional: 0 / 1 / 2**. This bucket exists to CAP, not to protect.

Define **`crowd`** exactly as `GoblinsProvider::TutorCandidates` already defines `buff_targets`:

    crowd = (my Goblin creatures on the battlefield, TOKENS INCLUDED) + min(3, Goblin bodies in hand)

Reusing that quantity is deliberate — the two hooks should not disagree about the deck's multiplicand,
and the cap on hand Goblins is already there so "a flooded hand can't unboundedly balloon the term".
Tokens counting is the deck-specific half: a resolved Siege-Gang means `crowd ≥ 4` on its own, and a
resolved Krenko means more every turn.

| `crowd` | multiplier quota | why |
|---|---|---|
| 0 | **0** | a lord with nothing to multiply is a 2/2 for 3 that does nothing. Keep a body instead. |
| 1–2 | **1** | one lord is worth +1 or +2 total power; a second is worse than a body. |
| ≥3 | **2** | at three-plus recipients a second +1/+1 is +3 power and climbing. |

Cap at 2 in every case.

**This cap is the entire reason for a third bucket, and it is why the 2-bucket "threats, lords-first"
shape is not merely simpler but wrong.** A single threat bucket ordered lords-first sheds **bodies to
keep lords** on a multiplier-heavy hand — the exact inversion, because the bodies *are* the
multiplicand. The case is common enough to matter: with 11 multiplier cards in 60, an 8-card hand holds
**≥2 with probability 0.454 and ≥3 with probability 0.154** (hypergeometric, unconditional). And 0.154
is a *floor*, because a hand that has reached a cleanup shed has already deployed its cheap bodies —
the survivors are enriched in exactly these cards.

### Bucket 3 — CROWD. The catch-all; no cap. Value order in §4.

### Why not 2 buckets, and why not 4

* **Not 2** — see the cap argument above. If the user prefers the brief's simple aggro shape, the
  collapse is mechanical and I would not resist it: fold MULTIPLIERS into CROWD and keep the cap as a
  within-bucket rule. **The cap is the load-bearing part, not the bucket count.**
* **Not 4** (ENGINES as their own bucket) — an engine bucket with its own quota would let a hand keep a
  Vial while the land quota went unfilled, and every engine in this deck needs a land first. ENGINES is
  a *sub-role of mana*, which is the brief's own treatment of ramp.
* **No REACH/burn bucket**, though the deck has 2 Bolt + Twinshot + Chainwhirler + Siege-Gang's outlet
  + Pashalik's pings. The deck does not *need* reach to function (it wins on turn ~3.79 by attacking),
  and the provider's own **trained** constant says burn belongs *inside* the crowd order rather than
  beside it: `MTG_GOBLIN_FACE_VALUE_PER` is 90 against `BODY` = 100 (90 is the shipped value, since
  `MTG_TUTOR_AXIS_RESOLVE` defaults on; 160 in the legacy read), i.e. one point of face damage was
  measured at **parity with one point of creature power** (trained on s4004+s5005, validated on
  s6006+s7007). Parity is exactly the claim "rank it in the same list".

---

## 4. Within-bucket order

### CROWD — crowd-creators first, then reach, then plain bodies

Grounded in `GoblinsProvider::TutorCandidates`' `value_of`, which is this deck's **already-measured**
value model, so the two hooks agree:

1. **Muxus** — `etb_reveal_count × 75` = 450 plus a 4/4. The biggest single crowd creation in the deck.
2. **Siege-Gang Commander** — `3 × 90` tokens + 40 reach + a body: four bodies and a repeatable outlet.
3. **Krenko** — `G × 80`, i.e. **explicitly crowd-gated**: at `G = 0` Krenko is a 3/3 for 4 that taps
   for nothing. It should fall behind a 2-drop on an empty board, and the EV term in §5 does that.
4. **Pashalik Mons** — 2 tokens per sac + 1 damage per Goblin death; a crowd-and-reach engine.
5. **Mogg War Marshal** — two bodies for {1}{R} (ETB token + death token). The echo tax is already
   owned by `GoblinsProvider::PayEchoToKeep`; do not re-decide it here.
6. **Goblin Matron** — a 1/1 plus the best Goblin in the library. Role-flexible; see §7.
7. **Twinshot Sniper** — 2 face at BODY parity, **and** a from-hand channel, which is what makes it
   distance-2 rather than distance-4 (§5).
8. **Goblin Chainwhirler** — a 3/3 plus `etb_damage_each_opponent × OpponentHeads()`. Note `{R}{R}{R}`:
   three *red* sources, which Three Tree City cannot help supply.
9. **Lightning Bolt** — 3 face for {R}, one-shot. It is **not a Goblin and not a creature**: no
   Warchief discount, no Matron fetch, no Lackey put, no Vial put, and no Cavern coloured mana.
10. **Stingscourger** — the floor, for the reason in §2's bracket notes.

Ties inside a rank: higher power, then higher MV (so the cheapest equivalent body sheds first) — the
same shape Minotaur uses.

### MULTIPLIERS — and the three duplicate claims, two of which are counter-intuitive

**Duplicate +1/+1 lords are NOT dead.** `ComputeLordBonus` accumulates `pb += ldef->params.power_bonus`
over **every** lord permanent (`src/core/SpellEffects.h:~3210`), and `lord_excludes_self` means two
Goblin Kings each buff the other. So a second King is still +1/+1 across the whole crowd *plus* a 2/2.
The brief's prompt suggested "a SECOND lord of the same name while one is resolved is worth much less";
for the **stat** half of a lord that is not what this engine does, and I am not going to assert it. What
*is* true is that a second multiplier competes with a body for the multiplicand, which is what the
crowd-conditional quota already expresses. **This is the honest form of the prompt's intuition.**

**What genuinely does NOT stack is `grants_haste`.** It is a boolean grant, so a second haste source
adds nothing. Propose: when a Goblin haste granter is already resolved, **a hand Chieftain re-ranks as a
plain +1/+1 lord** (losing the haste term) and **a hand Warchief keeps only its discount term**. The
board scan that answers this already exists in `TutorCandidates` as `haste_source`, and
`HasHasteFromLords` is the shared reader.

**The k-th Warchief's discount is computable exactly, and it is usually zero.** `ManaPayment.cpp:1400`
reduces the **generic** portion only, stacking per reducer, **floored at 0**. So the k-th resolved
reducer is worth *the number of Goblin cards in hand with printed generic ≥ k*. On this decklist:

| printed generic | cards | 1st Warchief | 2nd Warchief |
|---|---|---|---|
| 4 | Muxus | yes | yes |
| 3 | Siege-Gang | yes | yes |
| 2 | Matron, Krenko | yes | yes |
| 1 | Hordemaster, Piledriver, Mogg War Marshal, Stingscourger, King, Chieftain, Warchief | yes | **no** |
| 0 | Lackey, Skirk, Chainwhirler | no | no |

So a second Warchief is worth **exactly nothing** against every {1}{R}, {1}{R}{R}, {R} and {R}{R}{R}
card in the deck. That is a precise, param-derived statement and it needs no authored constant.

**GOBLIN KING IS PROVABLY DOMINATED BY GOBLIN CHIEFTAIN in this engine.** Identical {1}{R}{R},
identical 2/2, identical +1/+1-to-Goblins; Chieftain *also* grants haste and has haste itself. King's
only differentiator is mountainwalk, which `cards.json` itself flags INERT in goldfishing.
`GoblinsProvider::TutorCandidates` **already drops King by exactly this rule** — the default-on
`MTG_GOBLIN_DOMINANCE` with its 14-element `caps_of` capability vector — and the comment there names
King as "the case in this deck". **Propose: reuse `caps_of` dominance here rather than authoring a lord
name order.** Among otherwise-equal multipliers the dominated one sheds first, by proof rather than by
opinion, and the two hooks stay consistent under any decklist swap. (Note `caps_of` already multiplies
`etb_damage_each_opponent` by `gamesetup::OpponentHeads()`, which the 2HG cell needs — see Doubt 9.)

**Goblin Piledriver takes a multiplier slot but on a TIGHTER crowd.** Its pump is +2 per other
**attacking** Goblin and it cannot attack the turn it lands, so its multiplicand is what is actually
deployed — the provider uses `pile_crowd = 2`, i.e. `G + entering + 1`, and deliberately clamps its
credit so Piledriver never exceeds lord parity (`MTG_GOBLIN_PILEDRIVER_DELAY` 50 under a `min()` with
the derived `(T−1)/T`). The user's ruling behind that clamp is explicit: *"it's important that the
Piledriver is not strictly better... If close to lethal and no haste the lord is better."* Honour it:
Piledriver ranks **level with, never above, a +1/+1 lord**, and its crowd test uses the deployed count,
not `buff_targets`. Whether it should occupy a capped slot at all is Doubt 3.

### MANA — shed order inside the bucket

Dead Three Tree City (no other red source and <3 Goblins) → surplus lands, colourless-only first →
second and later Vials → surplus Skirk → surplus Lackey/Warchief (which fall into CROWD as bodies).

---

## 5. Distance-to-playable — the deck's real contribution to this doctrine

**Goblins is the deck where "distance to playable" is not a mana count at all.** Four separate non-mana
routes shorten it, and three of them have a **capacity of one card per turn** — which is the part a
blanket eraser would get catastrophically wrong (it would protect a whole hand of bombs).

| eraser | param | effect | capacity |
|---|---|---|---|
| resolved Warchief(s) | `reduces_spell_subtype` / `_amount` (defaults to **1**) | `eff_mv = mv − k`, **generic-floored** — never below the coloured pips | whole hand |
| live Goblin Lackey | `combat_damage_puts_subtype_from_hand` on the battlefield **and** able to attack | distance **0** for the **single highest-value Goblin PERMANENT** in hand | **one per Lackey per combat** |
| untapped Aether Vial at charge *c* | `upkeep_adds_charge` + `Permanent::charge_counters` | distance **0** for hand **creatures with `mv == c` exactly** | **one put per Vial per turn** |
| Skirk Prospector on board | `sac_outlet_add_mana_amount` | `reach += skirk_ramp` (fodder excludes lords, includes self-sac) | bounded by fodder |
| Twinshot Sniper itself | `channel_cost` + `channel_damage` | `eff_distance = min(mv, channel_cost.ManaValue())` → **2, not 4** | n/a |

The Vial one deserves emphasis: the put requires **`mv == charge` exactly**, not `mv ≤ charge`. A Vial
at 3 charges does not deploy a Muxus, ever. The profile's `vial_target_mv: 3` says the engine charges
to 3, so in practice the Vial deploys **lords**, and the distance relief it grants is to MV-3 cards
only.

**Use EV = P(play) × value with the HARD decay, not playability-first.** This is not a free choice —
Minotaur measured the lexicographic "playability, then effectiveness" sort and it lost in a **monotone**
gradient (slack 1 → +0.00015, slack 2 → +0.00089, i.e. the harder it weighted playability the worse it
got), and the fix was the user's model: *"expected value = probability of playing × value of playing"*,
with `value` taken from the **full** order (EV2) rather than a bucket, because a bucketed `value` is
constant inside its bucket and EV degenerates to playability alone. On Goblins the equivalent failure
would be shedding Muxus and Siege-Gang on turn 3 — exactly what tier B already does.

**Duplicates pay cumulatively** (`MTG_MINOTAUR_DISCARD_DUPES`' reasoning, which transfers cleanly): to
cast the second Siege-Gang you must pay for both, so the k-th copy's `P` collapses without any
per-card constant, and cheap bodies are untouched (two Lackeys at two sources are both castable; two
Muxus need twelve). **Count board copies too** — and on this deck that interacts with the legend rule,
see §7's Muxus note.

The deck's learned second-copy marginals corroborate the *shape* of that rule (with the staleness
caveat of Doubt 6): **Siege-Gang -0.4119** and **Lackey -0.2671** against **Piledriver -0.0182** — the
worst duplicate in the deck is the 5-drop and the near-free one is the 2-drop, which is what "the k-th
copy pays cumulative mana" predicts without being told. Lackey's negative second copy is the one that
does *not* follow from cost, and I am not going to claim it follows from redundancy either: against a
goldfish both Lackeys connect, so two Lackeys really do make two free puts per combat. The plausible
reading is that the **puts compete for the same hand** — a put needs a Goblin *in hand* to put, and a
hand holding two Lackeys holds one fewer thing to cheat in — which is the castability confound Doubt 6
warns about, seen from the other side. Treat it as unexplained rather than as evidence.

---

## 6. The total order over a hand — index 0 is determined for any hand

Quotas **interleave**; they are not filled bucket-at-a-time. That is the single most important lesson
in `minotaur-discard-policy-proposal.md`'s "What shipped" section — a bucket-at-a-time fill protected a
fifth land ahead of a castable body, caught live in an `MTG_TRACE=discard` probe. The ladder below is
read forwards as the quota fill and **backwards as the order the protected cards give way in**.

    land1 > crowd1 > land2 > engine1 > mult1 > crowd2 > land3 > crowd3 > mult2 > land4 > crowd4 > ...

Justification for each of the contested adjacencies:

* **land1 above everything.** A missed land drop is the one thing this deck cannot recover, and *every*
  engine in the deck (Vial {1}, Skirk {R}, Lackey {R}) needs a land before it does anything.
* **crowd1 above land2.** One body on turn 1–2 is the clock; the second land can be drawn.
* **engine1 above mult1.** The measured enabler credit in `TutorCandidates` is 0.5–0.8× the stuck
  bomb's whole value, and `card_scores` puts Lackey at +1.3249 against the best lord's +0.2608. The
  overnight evidence quoted in that hook is four games where fetching an *enabler* rather than a bomb
  won a turn (gi602/gi206 Warchief, gi842 Skirk, gi924 Lackey). A multiplier can wait; the deployment
  engine is what makes the hand's bombs arrive at all.
* **mult1 above crowd2, but mult2 behind crowd3.** One multiplier is worth more than the second body;
  the second multiplier is worth less than the third body. That asymmetry *is* the cap.

Then the shed, most expendable first:

1. **Dead Three Tree City** (no other red source and <3 Goblins) — the Karoo-class blank.
2. Surplus MANA past the quota: colourless-only lands, then lands, then **second and later Vials**.
3. Surplus MULTIPLIERS past the crowd-conditional cap, weakest first — **`caps_of`-dominated copies
   first** (so a Goblin King sheds before a Goblin Chieftain), then by EV.
4. Surplus CROWD, lowest EV first — Stingscourger, then Bolt/Chainwhirler when reach is covered, up
   through the crowd order.
5. Everything else the quotas protected, in reverse ladder order.

**The list must name EVERY card in the hand.** Anything omitted falls to descending MV, which is the
ranking this provider exists to overturn, and with `required_pieces: []` there is no protection
standing between an omission and that fallback.

---

## 7. State promotions

### Proposed — each one a fact a cleanup ranking can establish

1. **Resolved-Warchief distance erasure**, generic-floored, k-th-copy aware (§4, §5). The Minotaur
   reducer model, and the brief explicitly asked for it.
2. **Lackey capacity eraser** — a live, attack-capable Lackey zeroes the distance of exactly **one**
   hand Goblin permanent, the highest-value one. The capacity is the point.
3. **Vial charge eraser** — an untapped Vial at charge *c* zeroes the distance of hand creatures with
   `mv == c` exactly, one per Vial. `TutorCandidates` already reads `vial_charge` off untapped Vials.
4. **Skirk reach** — `reach += skirk_ramp`, reusing the already-corrected fodder count (lords excluded,
   self-sac included, entering body counted).
5. **Crowd-conditional multiplier quota** (0/1/2 on `buff_targets`, tokens included) — §3.
6. **Haste redundancy** — a resolved Goblin haste granter zeroes the haste term of a hand
   Chieftain/Warchief (§4).
7. **`caps_of` dominance among same-role cards** — King behind Chieftain, by proof (§4).
8. **Twinshot channel distance** — `eff_distance = min(mv, channel_cost)` (§5).
9. **Colour liveness** — Three Tree City and Cavern of Souls (§3).
10. **Matron role-flexibility** — `tutor_to_hand` + `tutor_types: [Goblin]` means Matron converts into
    whatever the hand is short of, so it should be scored as **a copy of the best Goblin still in the
    library, at +3 mana and one turn** rather than as a 1/1 body. Note the hard limit that makes this
    safe: **Matron cannot fetch a land** (`tutor_types` matches subtypes; neither utility land carries
    one), so it never fills the MANA bucket except by fetching a Skirk Prospector. It is a CROWD /
    MULTIPLIER filler only. (The library-composition read is deck knowledge a player has, and it is
    what `TutorCandidates` already does — no clairvoyance about draw order.)

### Deliberately REJECTED as search-owned

* **"Is this Bolt / Twinshot / Siege-Gang activation the last 3 damage?"** — a lethal projection. The
  engine already has machinery for this (`UseLethalShortCircuit`, `face_burst`); a cleanup ranking
  cannot establish it. Same call Minotaur made for Fanatic's reach term.
* **"Channel Twinshot Sniper instead of discarding it."** A CAST decision — the cleanup runs after both
  main phases. Worth stating the diagnostic, though: if a trace shows Twinshot being shed while {1}{R}
  sat unspent, the defect is in the cast enumeration, not here.
* **Echo pay-or-sacrifice for Mogg War Marshal / Stingscourger.** `GoblinsProvider::PayEchoToKeep`
  already owns it, on a measured heuristic, and it is an upkeep decision.
* **Pashalik + Skirk chain damage** ("sac N bodies for N pings"). A damage projection, and the sac
  enumeration is searched — `DeferSacOutletPreCombat` already governs its timing.
* **Rundvelt Hordemaster's impulse dig as a reason to keep a sac outlet.** Explicitly rejected, because
  **this deck already measured it**: `value_of`'s `dies_trigger_impulse_exile` term exists but
  `MTG_GOBLIN_IMPULSE_PER` defaults to **0**. The pairing was priced and the price came out zero; do
  not reintroduce it here at a guessed weight.
* **A "shed to empty the hand" inversion.** Goblins has no `hand_size_anthem_max` card, so unlike
  Minotaur's Neheb there is no state in which shedding is a benefit. (The shared
  `HandShedIsPayoff` scan finds nothing here and is inert.)

### One promotion that looks like it should exist and must not: duplicate Muxus

Muxus is Legendary and the deck runs **three**. It is tempting to treat a hand Muxus as dead while one
is resolved. **It is not**, and the engine agrees twice over: casting the second Muxus fires its
`etb_reveal_count: 6` cascade *and then* the legend rule eats one body — so the duplicate buys a
reveal-6 for 6 mana, which is most of the card. And `OfferDuplicateLegendCast`'s prune does **not**
apply: its whitelist is enter-**inert** templates only (`VanillaCreature` / `LordEffect`), and Muxus is
`custom` — the comment there names Muxus explicitly as the card it must keep offering. So no
duplicate-Muxus demotion beyond the cumulative-mana `P` collapse in §5. Krenko (1 copy) makes the same
question moot for itself.

---

## 8. Param-level classification predicates (so integration is mechanical)

    is_land(i)            CleanupDiscardIsLand(card)                            -> MANA/lands
    colourless_only(i)    land whose params.produces has no coloured entry      -> MANA/lands, sheds first
    scaling_land(i)       !p.mana_per_creature_subtype.empty()                  -> Three Tree City caveat
    creature_only_mana(i) p.colored_creature_only                               -> Cavern caveat
    is_vial(i)            p.upkeep_adds_charge                                  -> MANA/engines (last)
    is_mana_outlet(i)     IsSacManaOutlet(p)  [shared reader]                   -> MANA/engines (Skirk)
    is_cheat(i)           !p.combat_damage_puts_subtype_from_hand.empty()       -> MANA/engines (Lackey, first)
    is_reducer(i)         IsSubtypeCostReducer(*def)  [shared helper]           -> MANA/engines (Warchief)
    is_lord(i)            p.power_bonus > 0 && !p.subtypes_affected.empty()     -> MULTIPLIER
    is_atk_pump(i)        p.attack_pump_power_per_other_matching > 0            -> MULTIPLIER (tight crowd)
    grants_team_haste(i)  p.grants_haste && !p.subtypes_affected.empty()        -> haste term (idempotent)
    is_cascade(i)         p.etb_reveal_count > 0                                -> CROWD rank 1
    makes_tokens(i)       p.etb_self_creates_tokens > 0                         -> CROWD (Siege-Gang, Mogg)
    taps_for_tokens(i)    !p.tap_creates_tokens_per_controlled_subtype.empty()  -> CROWD, crowd-gated (Krenko)
    sac_tokens(i)         p.sac_outlet_creates_tokens > 0                       -> CROWD (Pashalik)
    is_tutor(i)           p.tutor_to_hand || p.tutor_to_top                     -> CROWD, role-flexible
    face_of(i)            max(etb_damage_any,
                              etb_damage_each_opponent * OpponentHeads(),
                              channel_damage)                                   -> reach term (2HG-correct)
    channel_mv(i)         p.channel_cost ? p.channel_cost->ManaValue() : mv      -> distance floor
    echo_tax(i)           p.echo_cost.has_value()                               -> Stingscourger demotion

Board census (net of board, computed **before any quota**):

    board_sources     own lands (+ any mana_rock; none in this deck)
    board_red         own permanents whose produces contains R, honouring colored_creature_only
    board_goblins     own creatures with subtype Goblin, TOKENS INCLUDED   -> the crowd
    board_fodder      board_goblins excluding lords/scaling payoffs        -> Skirk's food
    board_reducers    IsSubtypeCostReducer, with reduces_spell_subtype_amount (default 1)
    board_haste       any grants_haste && !subtypes_affected.empty()
    board_lackey      combat_damage_puts_subtype_from_hand, and CanAttackFull -> live this turn
    vial_charge       max charge_counters over UNTAPPED upkeep_adds_charge permanents
    skirk_ramp        IsSacManaOutlet present ? board_fodder (+ self) : 0

Reuse, do not re-derive: `CleanupDiscardManaValue`, `CleanupDiscardIsLand`, `IsSubtypeCostReducer`,
`IsSacManaOutlet`, `HasHasteFromLords`, `CanAttackFull`, `CardHasSubtype`,
`gamesetup::OpponentHeads()`, and `caps_of`'s dominance test. Return through
`CleanupDiscardRankingWithOrder(s, required_pieces, shed)` so the staged-card and required-piece
protections stay engine-enforced; **omission = keep**.

**One concrete integration snag:** `IsSubtypeCostReducer` is defined at
`src/ai/DecisionProviders.cpp:11455`, *below* `GoblinsProvider`'s method bodies (≈5296–8000). Either
hoist the helper or place the new override further down the file — it will not compile where the
deck's other hooks live.

---

## 9. Doubts, flagged — the user reviews and amends these

1. **`source_target = 4` vs 5.** The cards do not settle it. 4 casts everything but Siege-Gang and
   Muxus unaided, and the deck wins ~T3.79; Minotaur's was user-set to 5 for a top-of-curve of 5, and
   Goblins' top of curve is 6 but is meant to be *cheated*. It is one constant and the cheapest thing
   in this document to A/B. **I take 4.**
2. **The multiplier quota thresholds (0 / 1 / 2 at crowd 0 / 1–2 / ≥3).** The *shape* is argued (a
   multiplier with no multiplicand is a bad body, and 15% of 8-card hands hold three); the thresholds
   are judgement.
3. **Should Piledriver occupy a capped multiplier slot at all?** It is 4 of the 11 multiplier cards, so
   this materially changes how often the cap binds. Its pump is attack-only and delayed, and the
   provider's measured model deliberately caps it at lord parity. I put it in the bucket on the tighter
   crowd count, but treating it as a body with a crowd bonus is defensible.
4. **Matron's bucket.** I score it as a copy of the best Goblin in the library, which makes a hand of
   two Matrons and no land look better than it is — and Matron **cannot fetch a land**. A flat "Matron
   is a 3-mana 1/1 plus a card" is the conservative alternative.
5. **Does the FIRST Aether Vial deserve a quota slot?** The learned scores call it the deck's worst
   card (-0.4916), the user's Minotaur doctrine sheds Vials with the mana, and `vial_target_mv: 3` says
   it takes three upkeeps to deploy a lord in a deck that wins on turn ~3.79. I keep one, last among
   engines. Dropping the engine quota to 0 when the only engine is a Vial is a one-line change.
6. **The learned `card_scores` are stale, and badly.** They were written by commit `94670f89` (the
   Stage-4 baseline) and **1,100 `src` commits have landed since** — including the entire tutor
   ranking, the enabler credit, lord amplification, the Piledriver model, the trained face-damage
   weight, the value sidecar and the exhaustive keep table. They also carry the units caveat from
   Minotaur round 4: an opening-hand group-mean difference, confounded with castability, whose negative
   half `AIEngine`'s own comment calls selection bias. **I used them only as corroboration, never as
   the order** (they corroborate Lackey top and Vial/Stingscourger bottom, both of which I derived
   independently). Per the Minotaur round-4 mis-ordering lesson: if anyone wants to measure a
   learned-value arm here, **regenerate first, then measure**.
7. **The rollout shed count for this deck is UNMEASURED,** which is the one piece of evidence this
   proposal is missing. The real-play count is 0 in 400 games, and per the brief that is the wrong
   denominator, not a verdict. The command, for the central integration step to run **alone on the
   box**:

       MTG_SHED_STATS=1 ./build/Release/mtg decks/Goblins/Goblins.cod \
           --profile decks/Goblins/Goblins.profile.json \
           --games 200 --seed 1001 --depth 5 --budget-ms 40

   (the decklist is POSITIONAL, not `--deck`; `--profile` is mandatory here — a profile-less
   measurement is the trap `deck-screening.md` and `valueleaf.sh` both exist to prevent.)

   It prints `real` / `rollout` / `rollout_lowland` / `cleanups`. If `rollout` is small for this deck
   *too* — plausible, since it wins on turn 4 and its keep table tolerates 1 land — then the honest
   verdict is that the policy is worth shipping for **doctrine quality only**, and that should be said
   rather than dressed up.
8. **`IsSubtypeCostReducer` placement** (see §8) — a compile-order snag, not a design question.
9. **`goblins2hg` runs the same deck and profile at 30 life with two heads** (`test/regression_cases.sh:239`).
   Every face-damage term must go through `gamesetup::OpponentHeads()`, exactly as `caps_of` already
   does, or the 2HG cell's GT moves for a reason that has nothing to do with this policy.
10. **Adoption bar.** Non-inferiority plus doctrine quality, per the brief. Expect avg-win-turn to
    barely move: the deck wins on turn ~3.79 with a hand it has already emptied. The case is that the
    status quo sheds **Muxus first**, and index 0 of this ranking decides every rollout cleanup with no
    search above it.
