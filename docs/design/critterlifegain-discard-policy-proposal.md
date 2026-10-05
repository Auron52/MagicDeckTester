# CritterLifegain — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored from `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 per-deck sweep.
**Proposal only — no `.cpp`/`.h` was touched.** The shipped form would be
`CritterLifegainProvider::CleanupDiscardCandidates`, returning a shed order (most expendable first)
through `CleanupDiscardRankingWithOrder(s, required_pieces, shed)`, behind a default-on
`EnvOn("MTG_CRITTER_BUCKET_DISCARD", true)` with `=0` restoring
`GenericProvider::CleanupDiscardCandidates` as the A/B hatch. The provider already exists (it holds
`LegendKeepIndex`, `SearchesWalkerCastActivation` and `CastOrderRank`), so no routing promotion is
needed — this is one new override.

Every card below was read from `src/cards/data/cards.json` (Rule 0), not recalled. Decklist read is
**v2**, `decks/CritterLifegain/CritterLifegain.cod` (60 cards / 23 lands, adopted 2026-09-15 per
`critter-lifegain-v2-adoption.md`).

> **PREMISE CORRECTION, up front.** The task brief said the deck runs **3 Ajani, Strength of the
> Pride**. That is the **v1** list (`decks/CritterLifegain/v1-thune4-basilica/`). **v2 runs ONE.**
> Its 2nd copy measured worse than a Plains in both formats (−0.0121 / −0.0086), which is why it was
> cut. So the "are copies 2 and 3 sheddable while one is resolved?" question is **unreachable from
> the shipped decklist** — I answer it anyway (§7, rule R2), because the rule is free, it was live on
> v1, and a screening arm that restores the count must not get it wrong. Note
> `CritterLifegainProvider::LegendKeepIndex`'s own comment ("Three Ajani, Strength of the Pride") is
> stale in exactly the same way; with v2 that hook only ever fires for **Heliod x2**, where both
> loyalties are 0 and it falls through to keep-oldest.

---

## 1. The deck in one paragraph, and therefore its shape

Mono-white. It wins by converting **life-gain EVENTS** into **damage**, and it needs three different
things on the battlefield at once to do it: a **source of gain events** (the enter-watchers — each
creature entering on *either* side, including the passive opponent's spawn tokens, is a separate
event per watcher, CR 119.10), a **payoff** that turns events into power (`+1/+1` counters), and
**creatures entering** for the watchers to see. Our own life total is goldfish-inert — the opponent's
life is the wincon — so a watcher with no payoff produces nothing but a number, and a payoff with no
event source is a vanilla 2/2. That product structure is what makes this an **engine/combo-shaped
deck, not a simple aggro deck**, and it is exactly what the deck's own adopted cast order encodes
(watchers rank 8, payoffs rank 9). The third part is genuinely distinct: **Ocelot Pride** manufactures
entering creatures every end step *without spending a card*, and with the city's blessing the copy
clause makes N Prides super-additive (2, 6, 14, 30 entered-this-turn tokens for N=1..4). So the
buckets are **MANA / ENABLERS (event sources) / PAYOFFS (converters) / FUEL (the token engine)**,
which is the brief's combo row — one bucket per engine part, similar effects grouped — plus one card
in **no bucket at all**. Four buckets, one sub-split inside MANA and one inside PAYOFFS.

## 2. Card-by-card role table (every card, from `cards.json`)

| card | n | cost (MV) | `params` keyed on | bucket |
|---|---:|---|---|---|
| Soul Warden | 4 | `{W}` (1) | `any_creature_enters_lifegain 1` | **ENABLER** (either side) |
| Soul's Attendant | 4 | `{W}` (1) | `any_creature_enters_lifegain 1` | **ENABLER** (either side) |
| Auriok Champion | 3 | `{W}{W}` (2) | `any_creature_enters_lifegain 1` | **ENABLER** (either side) |
| Daxos, Blessed by the Sun | 1 | `{W}{W}` (2) | `own_creature_enters_lifegain 1`, `own_creature_dies_lifegain 1`, `toughness_equals_devotion_color "W"`, Legendary | **ENABLER** (own side only) |
| Ajani, Strength of the Pride | 1 | `{2}{W}{W}` (4) | `loyalty_start 5`, `loyalty_abilities[].effect == "lifegain_creatures_plus_walkers"` (+1) / `"pridemate_token"` (−2), Legendary | **ENABLER** (repeatable, 1 event/turn) — also fuel via −2 |
| Ranger-Captain of Eos | 1 | `{1}{W}{W}` (3) | `tutor_to_hand`, `tutor_types ["Creature"]`, `tutor_max_mv 1`, `sac_creature_outlet`, `sac_outlet_self_only` | **ENABLER** (fetches one) |
| Ajani's Pridemate | 4 | `{1}{W}` (2) | `lifegain_self_counters 1` | **PAYOFF** (event→counters) |
| Voice of the Blessed | 4 | `{W}{W}` (2) | `lifegain_self_counters 1`, `counter_threshold_flying_vigilance 4`, `counter_threshold_indestructible 10` | **PAYOFF** (event→counters) |
| Archangel of Thune | 2 | `{3}{W}{W}` (5) | `lifegain_each_own_creature_counters 1` | **PAYOFF** (event→team counters) |
| Heliod, Sun-Crowned | 2 | `{2}{W}` (3) | `lifegain_target_own_counter true`, `creature_requires_devotion 5`, `lifelink_grant_cost {1}{W}`, Legendary | **PAYOFF** (event→one target counter) |
| Serra Ascendant | 4 | `{W}` (1) | `life_threshold_pump_life 30` / `_power 5` / `_tough 5`, Lifelink | **PAYOFF**, *threshold* sub-role |
| Ocelot Pride | 4 | `{W}` (1) | `endstep_lifegain_tokens 1`, `endstep_token_ascend_copy true`, `ascend true`, Lifelink, First strike | **FUEL** (token engine) |
| Plains | 19 | — | `produces ["W"]` | **MANA**, *untapped drop* |
| Remote Farm | 4 | — | `produces ["W"]`, `produces_amount 2`, `enters_tapped true`, `enters_tapped_with_depletion 2` | **MANA**, *tapped burst* |
| Unexpectedly Absent | 3 | `{X}{W}{W}` (2) | `targeting "nonland_permanent"`, `tuck_to_library`, template `removal` | **NO BUCKET** |

**Genuinely ambiguous roles, stated rather than hidden:**

* **Ocelot Pride** is a payoff *and* fuel *and* a lifelink clock. It is bucketed as FUEL because its
  unique, unsubstitutable contribution is manufacturing **enters** with no card spend — the one thing
  no other card in the deck does. (Its own lifelink is also an *independent* event source, which
  matters in §4.)
* **Serra Ascendant** reads the life **TOTAL**, not the event count. Grouped into PAYOFFS ("similar
  effects grouped": both convert lifegain into a clock) as a **sub-role**, because the state that
  switches it on is different from the one that switches a Pridemate on.
* **Ranger-Captain of Eos** is an enabler only by proxy (it fetches an MV≤1 creature) and is also the
  deck's only sac outlet — which is live only while Daxos is out. Ranked last in ENABLERS.
* **Ajani** is an enabler (+1 is one gain event per turn, reliably) *and* fuel (−2 makes a
  Pridemate token). Bucketed ENABLER; the MV-4 distance term does most of the work on it anyway.

**Bracket-note findings (`[...]` clauses that are stale or narrowing):**

1. **Ranger-Captain's note names a v1 fetch pool.** It says *"Legal pool in this deck is exactly Soul
   Warden / Soul's Attendant / Serra Ascendant (every other creature is MV>=2)"*. On v2 **Ocelot
   Pride (`{W}`, MV 1) is a fourth legal target** — and by the deck's own profile the best card in it
   (`[0.306, 0.211]`). No correctness impact (tutor axis width 6 ≥ 4 candidates, so all four are
   scored), but the note is wrong and should be refreshed by whoever next touches that entry.
2. **Auriok Champion's note says "4 Champions" and reasons about Orzhov Basilica.** Also v1 text
   (v2 runs 3 Champions and no Basilica). Model unaffected.
3. Disclosed inert clauses that make some cards *look* better than they play here: Voice's counter
   thresholds, Archangel's and Ocelot's flying/first strike, Heliod's indestructible, Auriok
   Champion's protection (inert on all four DEBT axes), Serra's granted flying. **None of them is a
   reason to keep a card in this apparatus.** The v2 adoption doc already flags the mirror risk — the
   sim reports Pridemate and Voice as *identical by construction* (mono-white erased the cost
   difference that separated them on v1), which is why §4 breaks their tie on hand order and not on
   an invented value claim.

## 3. The buckets and quotas — every one stated NET OF BOARD

Census the battlefield first (controller-side permanents only), then the hand owes the remainder.
Two separate mana censuses, for two different questions — the split is Minotaur's precedent
(`board_lands` vs `board_sources`) and it is load-bearing here because Remote Farm is worth 2 mana
but only one land drop:

* `board_drops` = count of own lands — answers *"will I make my land drops?"*
* `reach` = Σ `produces_amount` over own lands **plus** every land in hand — answers *"can this card
  be cast soon?"* (a Remote Farm contributes **2**).

| bucket | quota (net of board) | what fills it from the battlefield |
|---|---|---|
| **MANA** | **3 land drops**, raised to **4** while a *kept* card of MV ≥ 4 is held (cap 5) | any own land; a Remote Farm with `depletion > 0` counts 1 drop / 2 reach |
| **ENABLERS** | **2** (hard floor 1) | any own permanent with `any_creature_enters_lifegain > 0` or `own_creature_enters_lifegain > 0`; an own Ajani counts 1 |
| **PAYOFFS** | **2** (hard floor 1) | any own permanent with `lifegain_self_counters`, `lifegain_each_own_creature_counters`, `lifegain_target_own_counter`, or `life_threshold_pump_life` — **including an Ajani `-2` Pridemate token**, which resolves the real definition by name |
| **FUEL** | **2** | any own Ocelot Pride (`endstep_lifegain_tokens > 0`) |
| **NO BUCKET** | — | Unexpectedly Absent is never protected |

**Sub-splits (fungible upward — the parent total binds, per the brief's rule 3):**

* **MANA: untapped drops vs tapped burst.** With no untapped land available, keep at least one
  non-`enters_tapped` land: a Remote Farm cannot cast the turn-1 Soul Warden that the whole engine
  wants. Surplus sheds tapped-first. **The Karoo caveat does NOT apply** — Remote Farm needs no other
  land (its sacrifice is a depletion clause, not a bounce), so unlike a lone Rakdos Carnarium it is
  never a blank.
* **PAYOFFS: event-counter payoffs preferred over the threshold payoff**, *inverted* while
  `life >= life_threshold_pump_life` (30) — at which point Serra is a 6/6 lifelink for `{W}` and is
  the best card in the bucket. This is not hypothetical bookkeeping: the **`critter2hg` regression
  case starts at 30 life**, so in that cell the inversion is on from turn one, and because the
  comparison is against the card's own param (absolute 30, matching `SpellEffects.h:3106`) it needs
  no format branch.

**Why the quotas are deliberately loose (and what that implies).** They sum to 9 slots against a
7–8 card hand, so on a fresh hand there is often **no surplus at all** and the shed is decided by the
*tail of the keep ladder* rather than by a quota breach. That is intentional: on this deck the
**net-of-board** term does nearly all the work, because the board fills roles fast (the deck wins
T4–5). Tightening the quotas to force surplus would just be a different way of writing the ladder.

## 4. Within-bucket order, and the distance term

Best-kept **first**; shed order is this reversed.

**MANA.** non-`enters_tapped` land > `enters_tapped` land; then higher `produces_amount`; then hand
order. (So a surplus Remote Farm sheds before a surplus Plains.)

**ENABLERS.** `any_creature_enters_lifegain` (fires on **either** side — the opponent's spawn tokens
enter on 8 of 10 game indices, so these are strictly better watchers here) > `own_creature_enters_lifegain`
(Daxos) > `tutor_to_hand` proxy (Ranger-Captain) > walker (Ajani). Within the first group, **ascending
MV** — which puts Soul Warden / Soul's Attendant ({W}) ahead of Auriok Champion ({W}{W}) without
naming any card, and agrees with the profile (`Soul Warden 0.209`, `Soul's Attendant 0.236`, `Auriok
Champion 0.012`) and with v2's cut of the 4th Champion.

**PAYOFFS.** `lifegain_self_counters` (Pridemate / Voice — the reliable 2-mana clock; tie on hand
order, they are identical by construction on this mana base) > `lifegain_each_own_creature_counters`
(Archangel — the highest ceiling, a *team* counter per event) > `lifegain_target_own_counter` (Heliod
— one counter per event, and its body is devotion-gated) > `life_threshold_pump_life` (Serra), the
last two swapping with Serra while life ≥ 30. Archangel's MV 5 is handled by distance, not by burying
it in the static order.

**FUEL.** Ocelot Pride only; tie on hand order.

**DISTANCE-TO-PLAYABLE.** A card of **MV ≥ 4 while `reach <= 3`** does not fill a quota slot and
ranks below every castable bucket card; among such cards the higher MV sheds first (Archangel before
Ajani). Bounded and reach-conditional on purpose — this is *not* the max-MV rule this policy exists
to overturn. **This deck has no cost reducer**, so unlike Minotaur's Ragemonger clause there is
nothing that can erase the distance; the term is therefore unconditional and simpler. Colour is never
part of distance here: every land makes `W` and every spell is mono-white. Because `reach` counts hand
lands, the rule and the MANA quota do not fight: a hand holding five lands has `reach 5`, so its
Archangel is *not* demoted and the 4th land is protected to cast it.

## 5. The total order over a hand (index 0 is always determined)

Shed order, **most expendable first**. Every card in hand is named — nothing falls through to the
shared fallback's tier B (descending mana value), which on a deck of 1-drop payoffs is exactly
backwards (the Mirrorwing gi295 lesson).

| tier | contents |
|---|---|
| **S1** | **Unexpectedly Absent** — no bucket. Inert vs a passive goldfish and the deck's worst-scoring card (`−0.216`); the same card, with the same verdict, is why `EquipmentProvider` puts removal in no bucket (USER 2026-08-19: *"Swords and Unexpectedly are essentially unused in goldfish"*). |
| **S2** | **Dead legend duplicate** — `Legendary && loyalty_start == 0` with a same-named permanent already on **our** battlefield (a 2nd Heliod; a 2nd Daxos under a screening arm). It dies on resolution and its entry does nothing. |
| **S3** | **Surplus MANA** beyond the drop quota — `enters_tapped` first, then lower `produces_amount`, then hand order. |
| **S4** | **Unreachable expensive cards** — MV ≥ 4 while `reach <= 3`, higher MV first. |
| **S5** | **Bucket overflow** beyond quota, worst-first: bucket furthest over quota first, and inside a bucket the §4 order reversed. |
| **S6** | **Quota-protected cards**, in **reverse ladder order** (last slot filled yields first). |

**The interleaved keep ladder** (Minotaur's shipped lesson #1 — "5 sources" and "3 threats" are
constraints on the *same* hand, so the ladder must say which land beats which spell). Read forwards
it is the quota fill; read backwards it is S6:

```
drop1 > E1 > P1 > F1 > drop2 > P2 > E2 > drop3 > F2 > drop4*
```

`drop4` exists only while a kept MV ≥ 4 card is held. The ladder holds **quota slots only** — a third
enabler or payoff is overflow (S5), not a protected slot. **One scarcity swap on top of it:** the
`(E1, P1, F1)` triple is ordered by **board census ascending** — an engine part already on the
battlefield yields its slot to a part that is not — with the static tie-break `E > P > F`. That
static tie-break **is** the adopted cast order's ruling (watcher before payoff), and it is the answer
to "a watcher is the enabler the payoffs depend on": with zero watchers out, a hand payoff is a
vanilla body, so the watcher is kept first. The scarcity swap is what the cast order cannot express,
because `CastOrderRank` sequences a turn's casts and says nothing about what to keep.

**Worked examples** (index 0 in bold):

* **Flooded, engine online.** Board 3 Plains + Soul Warden + Pridemate; hand 8 = Plains, Plains,
  Remote Farm, Soul Warden, Voice, Archangel, **Unexpectedly Absent**, Ocelot. `reach` = 3 + 1 + 1 + 2
  = 7, so Archangel is castable-soon and keeps its slot; three surplus lands exist — but S1 outranks
  S3, so index 0 is **Unexpectedly Absent**.
* **Land-heavy, no UA.** Board 2 Plains; hand 8 = Plains×3, **Remote Farm**, Archangel×2, Heliod,
  Soul Warden. Quota 3 drops + 1 (a kept MV-4+ card is held) = 4; board 2 → 2 hand lands protected →
  two surplus lands, tapped first ⇒ **Remote Farm**.
* **Board already holds the enablers.** Board 4 Plains + 2 Soul Warden + Soul's Attendant + Pridemate;
  hand 8 = **Plains**, Soul Warden, Soul's Attendant, Auriok Champion, Voice, Serra, Serra, Ocelot.
  Board drops 4 ≥ 3 and board enablers 3 ≥ 2 → the hand's Plains and all three hand enablers are
  surplus; S3 precedes S5, so index 0 is the **Plains**, and the next shed is **Auriok Champion**
  (MV 2, shed before the {W} watchers).
* **No surplus at all — the tail decides.** Empty board; hand 8 = Plains×3, Soul Warden, Pridemate,
  Voice, Ocelot, **Heliod**. Protected: 3 drops, E1, P1, P2, F1 = 7; the 8th card is the third payoff,
  beyond quota ⇒ **Heliod** (weakest per-event payoff, MV 3 at `reach 3`).

## 6. Param-level classification predicates (so integration is mechanical)

Names are used **nowhere**. Every read goes through `CardDatabase::Instance().LookupCached(card)` —
a hand card is a name-only placeholder.

```
is_land(i)      := CleanupDiscardIsLand(hand[i])
mv(i)           := CleanupDiscardManaValue(hand[i])            // {X}{W}{W} -> 2
tapped_land(i)  := p.enters_tapped                             // Remote Farm
reach_of(i)     := is_land(i) ? max(1, p.produces_amount) : 0  // Remote Farm -> 2

ENABLER(i)      := p.any_creature_enters_lifegain > 0 || p.own_creature_enters_lifegain > 0
                   || (p.loyalty_start > 0 && any a in p.loyalty_abilities:
                          a.effect == "lifegain_creatures_plus_walkers")
                   || (p.tutor_to_hand && "Creature" in p.tutor_types && p.tutor_max_mv <= 1)
PAYOFF(i)       := p.lifegain_self_counters > 0 || p.lifegain_each_own_creature_counters > 0
                   || p.lifegain_target_own_counter || p.life_threshold_pump_life > 0
THRESHOLD(i)    := p.life_threshold_pump_life > 0               // PAYOFF sub-role
FUEL(i)         := p.endstep_lifegain_tokens > 0
NO_BUCKET(i)    := d.tmpl == CardTemplate::Removal || p.tuck_to_library
DEAD_LEGEND(i)  := d.card.HasSupertype(Supertype::Legendary) && p.loyalty_start == 0
                   && exists own Permanent with the same m_name
SERRA_ONLINE    := players[me].life >= p.life_threshold_pump_life
```

Board census uses the same predicates over `s.battlefield` (controller-side), plus
`p.endstep_lifegain_tokens` for FUEL and the payoff predicate for the **Ajani `-2` token** (created
named, so `LookupCached` resolves the real Pridemate definition — the census must not miss it).

**Deliberate widening, disclosed.** `CastOrderRank` ranks payoffs on `lifegain_self_counters` **only**
(rank 9); my PAYOFF bucket also admits `lifegain_each_own_creature_counters` (Archangel),
`lifegain_target_own_counter` (Heliod) and `life_threshold_pump_life` (Serra). That is *not* a
contradiction of the measured adoption: the 5g mining measured Pridemate/Voice orderings and ranked
what it measured — Archangel and Heliod fall through to Generic there, an absence of evidence, not a
measured exclusion. Grouping all four as "converters of lifegain into damage" is the brief's
"similar effects grouped". The ENABLER predicate is **character-for-character** the one `CastOrderRank`
uses for rank 8, so consistency with the adopted order is by construction on the half that was
measured.

## 7. State promotions

**Proposed (a cleanup ranking can act on all of these — each is a plain state read, no projection):**

* **R0 — NET OF BOARD.** Every quota above; the scarcity swap on `(E1,P1,F1)`. This is the policy's
  centre of gravity for this deck, and the direct answer to the asymmetry in the brief: a resolved
  **watcher** demotes hand watchers; a resolved **payoff** demotes hand payoffs and promotes hand
  watchers into the scarce slot.
* **R1 — Serra promotion at `life >= 30`.** Serra becomes a 6/6 lifelink and takes the top of the
  PAYOFF order. Reads `players[me].life` against the card's own param; correct in 2HG (30 starting
  life) with no branch.
* **R2 — Dead legend duplicate (S2), with planeswalkers EXCLUDED.** A 2nd Heliod under a resolved
  Heliod is provably dead (it dies to the legend rule on resolution and its entry gains nothing).
  A duplicate **planeswalker** is *not* dead: it enters at loyalty 5 and this provider's own
  `LegendKeepIndex` keeps the higher-loyalty copy, so a fresh Ajani **recharges** a spent one (four
  loyalty and an unused activation). Hence the `loyalty_start == 0` guard — that is the answer to
  "are Ajani copies 2 and 3 sheddable?": **low value, but not dead**, so they sit at the bottom of
  S5 (and are usually demoted by S4 anyway at MV 4), never in S2. Unreachable on v2's single Ajani;
  live on v1 and under any screening arm that restores the count.
* **R3 — Distance-to-playable** (§4). Unconditional here: no cost reducer exists to erase it.

**Deliberately rejected as SEARCH-owned (each is a cast/damage decision, not a fact about a hand):**

* *"We are lethal this turn, shed anything"* — a damage projection.
* *Hold Unexpectedly Absent to tuck our own permanent* (a Pridemate reset / re-enter for another
  watcher trigger) — a cast decision with a value projection, and the autonomous path is pruned to
  opponent targets anyway.
* *Hold a 2nd Heliod because Daxos turns its legend-rule death into a gain event* — real (one event,
  three mana) but a cast/activation choice; and it would not move the card out of the bottom tier even
  if credited. The related self-sac emission is already gated by `SelfSacHasDeathPayoff` in the
  enumerator, which is where it belongs.
* *Which MV≤1 creature Ranger-Captain fetches* — already a searched plan axis (tutor width 6).
* *Ocelot's ascend timing* (holding a land to hit ten permanents) — a sequencing choice for the search.
* **Flagged but NOT proposed: a FUEL promotion while `has_city_blessing`.** `Player::has_city_blessing`
  is readable and the copy clause makes a 2nd Pride worth far more once the blessing is held, which
  argues for lifting `F2` above `P2`/`drop2`. It is a legitimate state read, but the *value* claim is
  unmeasured, so it belongs in a measured arm (`MTG_CRITTER_DISCARD_ASCEND`), not in V1.

## 8. Doubts — the user reviews and amends these

1. **`P2` vs `E2` is a genuine coin flip.** Counters ≈ events × payoffs, so the two marginals are
   symmetric by construction. I put `P2` first because event supply is **not** watcher-exclusive —
   lifelink combat damage (Serra, Ocelot, Archangel) and Ajani's `+1` generate events with zero
   watchers out — whereas a 2nd watcher's marginal is zero if no payoff exists anywhere. It is a
   one-line swap in the ladder if you read it the other way.
2. **MANA quota 3 (+1).** Chosen because 16 of the deck's 37 nonland cards cost `{W}` and only 3 cost more than 3. The
   equivalent Minotaur number was *raised by the user* on review, so this is the most likely line to
   be amended. A defensible alternative: 3 flat, with the 4th land never protected.
3. **FUEL and net-of-board pull in opposite directions.** Prides are **super-additive** (the ascend
   copy clause), and the profile agrees (2nd copy `+0.211`, the only strongly positive second copy in
   the deck). A strict board discount therefore *under*-values a hand Pride. I kept quota 2 net of
   board for doctrinal consistency; the honest alternative is to not discount FUEL by board count at
   all. Worth one measured arm.
4. **Remote Farm is counted 1 drop / 2 reach.** Both are slightly wrong in opposite directions: it
   enters tapped (so its mana arrives a turn late) and it depletes after two activations (so its 2
   is not permanent). Counting it as 2 reach is what lets a 3-land hand see Archangel at all.
5. **Serra's promotion threshold is a hard `>= 30`.** At 29 with the engine online it flips next turn,
   but "will reach 30" is a projection and belongs to the search. If you want the softer read, name
   the number (e.g. `>= 24` with a watcher and a payoff on board).
6. **`card_scores` were used only as a cross-check, never as the order.** They are fresh for this
   list (written by v2's own analysis, `src` tree `7129a4d1`, 2026-09-15 — unlike Minotaur's, which
   predated its provider by 127 commits), but the units are *opening-hand* group-mean differences
   confounded with castability. They disagree with the authored order on **Archangel (−0.127)** and
   **Daxos (−0.101)**; I read that as the castability confound (a 5-drop and a `{W}{W}` own-side-only
   watcher), not as evidence those cards are bad, and the authored order does not follow them.
7. **One of Pridemate's / Voice's second-copy marginals is noise.** `Pridemate [.., −0.083]` vs
   `Voice [.., +0.168]` while the v2 keep table **merged them into one bucket as identical by
   construction**. They cannot both be right; neither drives the within-bucket order (tie on hand
   order).
8. **Adopting this makes the shipped keep table very slightly stale.** `CritterLifegain.keepmodel.
   exhaustive.profile.json.gz` (K=13) was generated with the **generic** cleanup shed inside its
   rollouts. Changing the rollout policy changes the apparatus that table was fitted to. Not a
   blocker — the table is presence-gated and GT re-acceptance measures play — but the next
   regeneration should happen *after* this lands, not before. Note also that this hook does **not**
   decide London **bottoming** (that is the keep model plus `AIEngine::CardScore`), so nothing here
   transfers there; the two should merely not contradict.

## Honest assessment — read before approving

**This deck barely sheds, and the reason is structural, not statistical.** The analyzer's Stage-5i
verdict is `DISCARD_INERT` on **both** lists — *"no cleanup shed reached by either caller"* (v1,
2026-09-08) and again at v2's step 2 — and the deck carries **no discard cost** anywhere
(`NO_COST_INTERACTIONS`), so `ChooseNonCleanupDiscardIndex` is unreachable too. The mechanism: a hand
crosses seven cards only on a turn with **no land to play and nothing castable**, and this deck makes
a land drop or a `{W}` cast essentially every turn, then wins on T4–5 (GT ≈ 4.33 at d3/b10).

Per the brief, that does **not** make the rule inert — it makes it invisible. Its denominators are
(a) the **rollout** cleanup, which sheds heuristically with no search above it, and (b) above all
**keep/bottom generation**, where a forced-keep land-light hand crosses seven every turn
(`EquipmentProvider` measured 1 real shed per 120 games against 425–591 rollout sheds *per game*).
But I will not repeat Minotaur's original mistake in the opposite direction either: I have **not
measured** this deck's rollout-shed census, so I cannot tell you whether the number is Minotaur's
250,265 per 200 games or something near zero. **`MTG_SHED_STATS` is UN-RUN** — deliberately: this box
is at load average 26 under the concurrent 16-agent sweep, and BOX-LIMITS is one batch at a time.
That census is the first thing integration should run, and it is cheap.

**Recommended measurement order at integration** (mirroring what actually paid off on Minotaur, where
the expensive half produced nothing the cheap half had not already shown):

1. `MTG_SHED_STATS=1`, ~200 games at the deck's shipped cell — **size the denominator first.** If the
   rollout count is also ~0, ship for doctrine/fidelity only and say so, and skip step 3.
2. `test/tools/discard_behaviour_diff.py` — confirm the rule *fires* and that its signature is the
   doctrine working (expected: stops shedding Ocelot Pride and cheap payoffs; starts shedding
   Unexpectedly Absent, dead 2nd Heliods and surplus Remote Farms).
3. Paired A/B at d0 and d3 on the critter cells (`test/tools/paired_arms.py`), then smoke + regression
   through the accept flow. **The bar is non-inferiority**, plus doctrine quality — including the
   `critter2hg` canary, which is the only cell where Serra's promotion is on from turn one.

The claim I would stake this on is not a turn number: it is that **index 0 becomes
Unexpectedly Absent instead of Archangel of Thune**. The generic fallback's tier B is descending mana
value, so today the shed on a land-light hand is the deck's 5-mana team-counter payoff while the card
the user has twice called unused in goldfish stays in hand. That inversion is the defect this policy
removes.
