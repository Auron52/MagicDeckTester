# Creature Giving — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored per `docs/design/discard-bucket-authoring-brief.md`. Proposed as
`CreatureGivingProvider::CleanupDiscardCandidates` behind a default-on `MTG_CG_BUCKET_DISCARD`.
**This deck is an UPGRADE, not a new policy** — it already has a one-line override, and the first
job of this document is to say what that override does and does not buy.

---

## 0. What ships today, and why it is not a bucket policy

`CreatureGivingProvider::CleanupDiscardCandidates` (`src/ai/DecisionProviders.cpp:8462`) is:

```
base = GenericProvider::CleanupDiscardCandidates(...)   // = descending mana value
move every "Defense of the Heart" copy to the BACK of base
```

`GenericProvider::CleanupDiscardCandidates` returns the shared tier-B ranking — **descending mana
value, ties to the earlier hand index**. This deck has no discard outlet (`discard_land_damage`
nowhere in the 60), so tier A never fires and tier B *is* the whole rule. Written out over this
decklist, the shipped shed order is:

| # | card(s) | MV | why this is wrong |
|---|---|---|---|
| 1 | **Massacre Wurm** | 6 | the deck's mass-drain payoff, and Defense of the Heart's best free put |
| 2 | **Hunted Phantasm** | 3 | the deck's best gift-giver; alone it satisfies the engine's 3-creature condition |
| 3 | Suture Priest / Sylvan Scrying / Varchild's War-Riders | 2 | **Suture Priest is the deck's damage converter** |
| 4 | Soul Warden / Essence Warden / **Birds of Paradise** / Crop Rotation / Enlightened Tutor | 1 | arbitrary hand order decides between the deck's **best** card and its two weakest |
| 5 | every land, incl. a dead Azorius Chancery | 0 | arbitrary hand order again |
| 6 | Defense of the Heart | 4 | the one-line patch |

So the existing override protects exactly one card and hands everything else to max-MV. It sheds
the **entire drain chain** (Wurm → Phantasm → Suture Priest) before it will shed a Soul Warden, and
it decides "Birds of Paradise or Soul Warden?" and "which of nine lands?" by **draw order**. That is
the "too arbitrary" rule this sweep exists to replace, and a single-card patch over it is not a
policy.

Two further facts about the status quo, both verified here:

* **The header comment is STALE.** It says "the spare-copy band and MV rule order the rest". The
  spare-copy band was **removed as an engine rule** on 2026-08-06/07 (`src/core/SpellEffects.h:452`:
  "REMOVED as an engine rule... If a future deck's labels ever demand it, implement it in THAT deck's
  provider"). Nothing bands spare copies today, so the comment describes behaviour the engine no
  longer has.
* **Defense of the Heart has NO engine-level protection on this deck.** `Creature Giving.profile.json`
  carries `"required_pieces": []`, so `CleanupDiscardProtected` returns false for every card. The
  one-line override is the *only* thing between the rollout and shedding the engine.

## 1. Evidence — the rule is a ROLLOUT rule, and index 0 *is* the decision

Measured with `MTG_SHED_STATS=1` at **shipped play settings** (`value_play` d5 / b20, profile
attached), 200 games seed 1001:

| | |
|---|---|
| sheds in REAL play | **0** |
| sheds inside the SEARCH's rollouts | **3,909** |
| of those, taken with **fewer than four lands** | **3,909 (100%)** |
| sheds per cleanup | **1.00** |
| avg turn-to-win on the run | 4.6850 |

Real play essentially never reaches CR 514.1 here: **2 cleanup sheds in 6,000 d0 games** (~1 in
3,000). The deck curves out, wins ~T4.7 and has no draw engine, so the hand can only exceed seven by
one card and only when we are not casting. `CleanupDiscardSearchWidth()` is the base **1**, so the
rollout takes **index 0 and nothing else** — 3,909 times per 200 games, **with no search above it**,
and every single one of them in the land-light state where max-MV is least defensible.

`real == 0` therefore does not make the rule inert; it makes it invisible. (Same shape as Minotaur
and Dragons, and more extreme on the real-play side.)

### The two real-play sheds, traced — the generic ranking picks the ENGINE

`MTG_TRACE=discard`, 6,000 d0 games, seed 1001:

```
[discard] g1461 T7 hand=8 cands=8 lip=1 landsinhand=1 dropopen=0 -> Defense of the Heart
[discard] g3324 T5 hand=8 cands=8 lip=1 landsinhand=0 dropopen=1 -> Massacre Wurm
```

The trace lives inside the shared builder (`CleanupDiscardRankingWithOrder`), so it prints the
ranking **before** `CreatureGivingProvider` reorders it — i.e. it is the generic tier-B pick. Both
lines are the failure this deck's patch exists for, caught in real play rather than in a rollout:
left alone, the shared ranking sheds Defense of the Heart at one land in play. The patch catches the
first line. Nothing catches the second.

## 2. The deck, and therefore its shape

Creature Giving wins by **giving the opponent creatures and billing them for it**. Three parts have
to meet. **FUEL** puts bodies on the opponent's side — Hunted Phantasm (`etb_opp_creates_tokens 5`),
Varchild's War-Riders (`cumulative_upkeep_opp_token`, a growing 1-per-age-counter gift), and
Forbidden Orchard (`taps_spawn_opp_token`, one Spirit per Orchard per turn). **CONVERTERS** turn
those bodies into life loss — Suture Priest (`opp_creature_enters_life_loss 1`, on every entry) and
Massacre Wurm (`etb_opp_creatures_debuff 2` sweeps every 1/1 gift, `opp_dies_life_loss 2` bills each
death). **The ENGINE** is Defense of the Heart: at our upkeep, if the opponent controls three or more
creatures, it sacrifices itself and puts **two creature cards from the library onto the battlefield
for free** — which in this deck means Hunted Phantasm plus Massacre Wurm, i.e. five gifts and then a
sweep, at zero mana. Underneath sits a five-colour mana base (19 lands + 4 Birds of Paradise) whose
two land tutors are both Orchard-first by provider override, so the mana base *is* partly the fuel
engine. Nothing else in the 60 touches the opponent's life total.

That is a **COMBO** deck, so per the brief it gets **one bucket per combo part with similar effects
grouped, plus the mana split** — four buckets (MANA with a lands/dorks sub-split, FUEL, DRAIN,
ENGINE), plus an explicitly-named **blank class** that is not a bucket.

Two facts that shape everything below:

* **10 of the 23 mana sources produce ANY colour** (4 Forbidden Orchard, 2 City of Brass, 4 Birds of
  Paradise). Single pips are nearly free. The binding constraints are the **double and triple pips**:
  Hunted Phantasm `{1}{U}{U}` and Massacre Wurm `{3}{B}{B}{B}`. Direct (non-fetch, non-reflecting)
  colour sources out of 17: **G 16, W 12, U 12, B 11, R 11**. So a distance term here must be
  **colour-exact**, not a raw source count.
* **The engine reads the LIBRARY, not the hand.** `DecisionProvider::SacTutorPutList` enumerates
  *library* creature names only and scores the immediate drain burst, reading `enter_drain` /
  `death_drain` off the **battlefield**. Two consequences that drive this whole policy: a resolved
  Suture Priest multiplies the entire Defense of the Heart burst, and a creature in **hand** is a
  copy the engine can no longer supply for free.

## 3. Card-by-card role table

Every row derived from `src/cards/data/cards.json`; the `params` keyed on are named. `card_scores`
is the deck profile's learned first-copy opening-hand marginal (higher = better), quoted as
**corroboration only** — see Doubt D8.

| card | n | cost | bucket | keyed on | score |
|---|---|---|---|---|---|
| Forbidden Orchard | 4 | land | **MANA / lands** (tier 1) — *and fuel* | `taps_spawn_opp_token`, `produces` = WUBRG | +0.278 |
| City of Brass | 2 | land | MANA / lands (tier 2) | `produces` = WUBRG, `tap_self_damage` (free here) | +0.056 |
| Windswept Heath | 4 | land | MANA / lands (tier 3) | `fetch_land_types` [Forest, Plains] | +0.078 |
| Misty Rainforest | 1 | land | MANA / lands (tier 3) | `fetch_land_types` [Forest, Island] | −0.039 |
| Temple Garden | 1 | land | MANA / lands (tier 4) | `etb_pay_life_to_untap 2`, 2 colours | −0.008 |
| Breeding Pool | 1 | land | MANA / lands (tier 4) | same | −0.044 |
| Stomping Ground | 1 | land | MANA / lands (tier 4) | same | −0.007 |
| Overgrown Tomb | 1 | land | MANA / lands (tier 4) | same | −0.122 |
| Forest | 1 | land | MANA / lands (tier 5) | `produces` = [G] only | +0.019 |
| Tree of Tales | 1 | land | MANA / lands (tier 5) | `produces` = [G] only | −0.060 |
| Reflecting Pool | 1 | land | MANA / lands (tier 6) | `reflecting` — **produces nothing as our only land** | −0.130 |
| Azorius Chancery | 1 | land | MANA / lands (tier 7) | `etb_bounce_land` + `enters_tapped` — **Karoo caveat** | −0.187 |
| Birds of Paradise | 4 | `{G}` | **MANA / dorks** | `tmpl == ManaDork`, `produces` = WUBRG | **+0.564** |
| Crop Rotation | 4 | `{G}` | MANA / tutors | `tutor_land_to_battlefield` + `sacrifice_land`, `tutor_types` [Land] | −0.023 |
| Sylvan Scrying | 4 | `{1}{G}` | MANA / tutors | `tutor_to_hand`, `tutor_types` [Land] | −0.073 |
| Suture Priest | 4 | `{1}{W}` | **DRAIN** (entry converter) | `opp_creature_enters_life_loss 1` | −0.105 |
| Massacre Wurm | 3 | `{3}{B}{B}{B}` | **DRAIN** (death converter + sweeper) | `opp_dies_life_loss 2`, `etb_opp_creatures_debuff 2` | −0.114 |
| Hunted Phantasm | 4 | `{1}{U}{U}` | **FUEL** (burst giver) | `etb_opp_creates_tokens 5` | +0.109 |
| Varchild's War-Riders | 4 | `{1}{R}` | **FUEL** (ramping giver) | `cumulative_upkeep_opp_token` | +0.025 |
| Defense of the Heart | 4 | `{3}{G}` | **ENGINE** | `upkeep_sac_tutor_creatures 2`, `upkeep_sac_tutor_opp_min 3` | +0.155 |
| Enlightened Tutor | 3 | `{W}` | ENGINE / finder | `tutor_to_top`, `tutor_types` [Artifact, Enchantment] | +0.029 |
| Soul Warden | 3 | `{W}` | **BLANK CLASS** | `any_creature_enters_lifegain 1`, **no** drain param | −0.174 |
| Essence Warden | 4 | `{G}` | **BLANK CLASS** | same | −0.169 |

**The learned order independently reproduces the policy's extremes.** Birds of Paradise is the
deck's best card by a wide margin (+0.564, next is +0.278); Azorius Chancery is its worst (−0.187);
and Soul Warden / Essence Warden sit 21st and 22nd of 23 — **below every land except the Karoo**.

### The one genuinely ambiguous role, resolved

**Soul Warden and Essence Warden (7 of 60 cards).** They are `any_creature_enters_lifegain` — *we*
gain the life, and **nothing in this deck converts our lifegain into anything**. The card notes
state the condition under which the lifegain would matter ("in a deck with a 'whenever you gain
life' watcher... each trigger is its OWN life-gain event... so it converts straight into board
presence"); this decklist runs **no such watcher** — no `lifegain_self_counters`,
`lifegain_each_own_creature_counters`, `lifegain_target_own_counter` or `own_creature_dies_lifegain`
anywhere in the 60. The opponent is the passive goldfish and never attacks, so our life total is
unloseable and the lifegain can only ever pay back the deck's own life costs (City of Brass, four
shocks, five fetchlands) — which are themselves free for the same reason.

So the wardens' **ability is provably inert** and what remains is a 1-mana 1/1 body worth 1 combat
damage per turn. That is not zero, which is why they shed *behind* a dead Karoo and not ahead of it —
but it is the weakest thing in the deck that is not literally worthless.

**Beware the cast order here.** `CreatureGivingProvider::CastOrderRank` puts the wardens at rank 8
*together with* Suture Priest ("watchers: Suture Priest / the Wardens, before every giver"). That is
a **timing** statement — both are enter-watchers whose triggers want to precede the givers — not a
value one, and the params separate them cleanly anyway (Suture Priest carries
`opp_creature_enters_life_loss` + `own_creature_enters_lifegain`; the wardens carry only
`any_creature_enters_lifegain`). A bucket policy that read rank 8 as a value tier would protect
seven blanks.

### Consistency with the adopted cast order (2026-08-19)

The cast order is a **dependency DAG over one turn**, and it is the same DAG as these buckets:

| cast rank | card | what it says | bucket agreement |
|---|---|---|---|
| 5 | Crop Rotation | mana first, mana-neutral, cheapest | MANA is quota 1 |
| 6 | Sylvan Scrying | mana, one turn slower | MANA, ranked below Crop Rotation |
| 8 | Suture Priest (+ wardens) | **the converter before every giver** | DRAIN is the prerequisite of FUEL |
| 10 (generic) | Varchild's War-Riders | giver, after the watchers | FUEL, below Phantasm |
| 12 | Hunted Phantasm | giver, after the watchers | FUEL, the burst giver |
| 22 | Enlightened Tutor | after every same-turn shuffle (to-top dies to one) | ENGINE / finder, **below** Defense of the Heart |
| 28 | Massacre Wurm | **LAST** — gifts enter first, then die for 2 each | DRAIN, but the *capstone*: it needs fuel on board already |

The order that matters most for this proposal is **rank 8 before rank 12/28**: the converter is
worth more than the fuel, because the fuel's value is multiplied by the converter and not the other
way round. That is exactly why DRAIN sits above FUEL in the keep ladder below.

## 4. The buckets, with quotas (net of board)

Every quota counts the **battlefield first**; the hand owes only the remainder. Fill order, quotas
and what satisfies them from the board:

### Bucket 1 — MANA. Quota: **5 mana sources**, net of board.

Five because the deck's *plan* tops out at four: Defense of the Heart `{3}{G}` is the card the whole
deck builds toward, and it puts the 6-drop onto the battlefield **for free** — so the sixth source
buys only a hardcast Massacre Wurm, the line we take when the engine never arrives. Five lets us
cast the engine and a 1-drop, or Suture Priest plus Hunted Phantasm, in one turn.

*Filled from the board by*: battlefield lands + battlefield mana dorks/rocks. **Fetchlands never
reach the battlefield** (they sacrifice on activation), so they only ever count from hand.

**Sub-split (the brief's "acceleration and land drops"):**
* **LANDS** — the land-drop backbone; at most one per turn, so they are not fungible with dorks in
  tempo terms.
* **DORKS** — sub-quota **1 Birds of Paradise while none is on board**. Birds is one mana, makes any
  colour, and adds a source *without spending the land drop* — the only card in the deck that turns
  a `{1}{U}{U}` or `{B}{B}{B}` requirement from two turns away into one. It is also the deck's
  highest learned score by a factor of two.
* **TUTORS** — one slot for a land tutor, ranked **Crop Rotation > Sylvan Scrying** (cast order 5
  before 6): Crop Rotation is mana-neutral and `{G}`, Scrying costs `{1}{G}` and only reaches the
  *hand*. Both are Orchard-first by the existing `TutorCandidates` override, so a tutor slot is
  specifically "a Forbidden Orchard", i.e. a rainbow source **and** a per-turn gift engine.
* Sub-quotas are **fungible upward** (brief rule 3): the parent total of 5 binds; a hand with no
  Birds keeps more lands.

**Colour coverage comes first** (brief): among the hand's mana, a source that covers a colour with
**zero** sources outranks its tier. The priority is derived from the hand's own costs — cover the
colours of the cheapest cards in hand first — not from a hardcoded colour list, so a screening swap
keeps working. On this list that resolves to G, then W, then U, then R, then B.

**Two liveness caveats, both of which make a card worth zero rather than little:**

* **KAROO CAVEAT (Azorius Chancery).** `etb_bounce_land` must "return a land you control"; with no
  other land it returns **itself** and is a blank. It counts as a land only if the board has another
  land or the hand holds another non-Karoo land. Same finding as Minotaur's Rakdos Carnarium and
  Dragons' Gruul Turf, and `card_scores` independently ranks it the **worst card in the deck**
  (−0.187). A dead Chancery is the first thing shed. Even when live it enters tapped, so it ranks
  last among live lands.
* **REFLECTING POOL.** The card note is explicit: it "produces NOTHING when it is the only land".
  So it counts as a source only while another non-reflecting land is on the board or in hand.
* **CROP ROTATION liveness** (the deck's own third case). `sacrifice_land` is an additional cost, so
  with **zero** lands on the battlefield Crop Rotation is uncastable and does not fill its slot.
  At one land it is live and *correct* — tap the land for `{G}` first, then sacrifice it, and the
  fetched Orchard enters untapped — so the threshold is `board_lands >= 1`, not 2.

### Bucket 2 — DRAIN (the converter). Quota: **1**, net of board.

The two cards that convert opponent bodies into opponent life loss, **grouped because they do the
same job** (the brief's "similar effects grouped") and because either alone is a complete converter:
Suture Priest bills entries, Massacre Wurm makes deaths happen and bills them.

*Filled from the board by*: a Suture Priest or a Massacre Wurm on the battlefield. This is the
highest-leverage board census in the policy — `SacTutorPutList` reads `enter_drain`/`death_drain`
off the battlefield, so a **resolved** converter multiplies the entire Defense of the Heart burst,
and a converter in hand multiplies nothing.

**Within-bucket order, best kept first: Suture Priest > Massacre Wurm.** Structural, not a name
preference: an **entry** converter is live given *any* fuel — the Orchards produce an entry every
turn and War-Riders one per age counter per upkeep — whereas a **death** converter is live only
given a sweeper, and the only sweeper in the deck is the Wurm itself. Add that Suture Priest is
`{1}{W}` against `{3}{B}{B}{B}` and it is the one of the two that is castable in the state 100% of
these sheds are taken in. This inverts max-MV exactly.

A second slot (slot 11 of the ladder) is **soft**: two Suture Priests double every entry, so a
surplus converter is genuinely additive, not a dead duplicate.

### Bucket 3 — FUEL (gift the opponent bodies). Quota: **1**, net of board and net of the Orchards.

*Filled from the board by*: **the opponent's projected creature count at our next upkeep already
reaching the engine's threshold.** Projected = current opponent creatures + Forbidden Orchards we
control (one Spirit each at turn start) + Varchild's War-Riders age counters (gifts at upkeep).
Every term is visible and monotonically increasing — the passive opponent never loses a creature
except to our own sweep — so this is a sound lower bound, and it subsumes the three cases that
matter (a giver already on board; the opponent already at three; two or more Orchards, which reach
three within two turns) in one formula.

This is the quota that the mana base pays for: the Orchards are in bucket 1, so on a board with
Orchards out the FUEL quota is *already filled* and a gift-giver in hand is overflow.

**Within-bucket order: Hunted Phantasm > Varchild's War-Riders.** Param-derivable and sharp:
`etb_opp_creates_tokens` (5) is `>=` the engine's `upkeep_sac_tutor_opp_min` (3), so **Hunted
Phantasm single-handedly turns Defense of the Heart on, in one card, at instant speed relative to
our upkeep.** War-Riders ramps (1, then 2, then 3...) and needs three upkeeps to match one Phantasm.
Phantasm is also a 4/6 to War-Riders' 3/4. Against that, Phantasm needs `{U}{U}` — the deck's
tightest requirement — so this is precisely a pair the **distance term** is allowed to flip.

### Bucket 4 — ENGINE. Quota: **1**, net of board.

Defense of the Heart, plus Enlightened Tutor as its finder — in this deck Enlightened Tutor's only
legal targets are Defense of the Heart (Enchantment) and Tree of Tales (Artifact land), so it is
*"Defense of the Heart, one turn and one card later"*. Grouped for that reason; ordered **Defense of
the Heart > Enlightened Tutor**, which is also what cast rank 22 says (the to-top placement is
destroyed by our own Sylvan Scrying / Crop Rotation shuffle).

*Filled from the board by*: a Defense of the Heart on the battlefield. **And this quota is almost
always unfilled, structurally** — see §5.

### The BLANK CLASS — not a bucket: Soul Warden, Essence Warden.

No quota, so they are always overflow, and they shed ahead of surplus mana. Reasoning in §3; the
placement is the judgement call flagged hardest (D1).

## 5. How the Defense of the Heart result is preserved — structurally

The measured result to preserve: the probe-retirement classification (2026-08-06, games gi564 and
gi798) found that **shedding Defense of the Heart rolled out a full turn worse than shedding any
other card**. The shipped rule reproduces it by moving the card to the back **by name**. This policy
reproduces it from the deck's structure, in three independent ways — and each one also fixes
something the name patch does not.

**1. The first Defense of the Heart is quota-protected, so it is never in the shed list at all.**
It is the sole occupant of the ENGINE bucket (bar a strictly-worse finder), the quota is 1, and
**omission from the returned order means keep**. In the land-light state that 100% of these sheds
occur in there is no Defense of the Heart on the battlefield — at two or three lands we have not cast
a four-drop — so the quota is unfilled and the card is protected. The name patch only pushed it
*behind the cards the generic ranking named*; it still shed it once everything else was gone, which
is exactly what the g1461 trace shows the underlying ranking wanting to do.

**2. The quota is structurally durable, because the engine CONSUMES ITSELF.** Defense of the Heart
*sacrifices* itself when it fires (`upkeep_sac_tutor_creatures` — "sacrifice this enchantment,
search..."). So "a Defense of the Heart on the battlefield" is a narrow window between resolving and
the next upkeep at three-plus opponent creatures; the instant it fires the quota re-opens and the
next copy in hand is protected again. A quota on a self-consuming permanent cannot drift into
permanent satisfaction the way a lord or a mana rock can.

**3. It protects the cards the old rule shed AHEAD of the engine, which is the larger half of the
bug.** Under max-MV, Massacre Wurm, Hunted Phantasm, Suture Priest and Birds of Paradise were all
pitched *before* Defense of the Heart. Each of those is now a combo part with its own quota, so the
policy does not merely stop shedding the engine — it stops dismantling everything the engine is
for. Keeping Defense of the Heart while pitching the two creatures it is going to fetch and the
converter that bills them is not really a protection at all.

**Where this is deliberately WEAKER than the name patch, stated plainly.** The old rule put *all
four* copies at the very back. Here the **second and subsequent** copies are ENGINE overflow. That
is intentional: a spare Defense of the Heart is not a dead duplicate — every copy on the battlefield
triggers at the same upkeep and each sacs for two more free creatures, so copies are additive. The
surplus therefore sheds **last among all overflow** (behind surplus fuel and surplus converters), not
into the blank class. I could not locate the per-game record for gi564/gi798 to check whether those
hands held one copy or several (see D7), so this is the one place the new rule could measure worse
than the old one, and it is the first thing the A/B should be read for.

## 6. Within-bucket order and the distance-to-playable term

**Order within MANA / lands**, best kept first — entirely param-derived, **no card names**:

| tier | predicate | this decklist |
|---|---|---|
| 1 | `taps_spawn_opp_token` | Forbidden Orchard — rainbow **and** the fuel engine **and** what both tutors fetch |
| 2 | `produces.size() >= 5` | City of Brass — rainbow; `tap_self_damage` is free vs a passive opponent |
| 3 | `!fetch_land_types.empty()` | Windswept Heath, Misty Rainforest — always reach G plus a choice of W/U/B/R off the shocks |
| 4 | `etb_pay_life_to_untap > 0`, 2 colours | Temple Garden, Breeding Pool, Stomping Ground, Overgrown Tomb |
| 5 | `produces.size() == 1` | Forest, Tree of Tales — G only, the deck's deepest colour, so the least needed |
| 6 | `reflecting` | Reflecting Pool — worth its union only while another non-reflecting land exists |
| 7 | `etb_bounce_land` | Azorius Chancery — enters tapped, and **worthless** when it must bounce itself |

With the colour-coverage override on top (a source covering a zero-source colour outranks its tier)
and `LandWouldEnterTapped(s, def)` as the final tiebreak between equals.

**DISTANCE-TO-PLAYABLE, and it must be COLOUR-EXACT here.** A raw source count is the wrong
instrument on a deck where 10 of 23 sources make any colour but two cards want `{U}{U}` and
`{B}{B}{B}`. Define, over board **and** hand:

* `reach` = battlefield lands + battlefield dorks/rocks + **live** hand lands + hand dorks, where
  "live" excludes a self-bouncing Chancery and a lone Reflecting Pool;
* `src_cnt[colour]` = distinct sources that can make that colour, counting battlefield
  lands/dorks/rocks via `EffectiveProduces` (which resolves Reflecting Pool's runtime union), plus
  non-fetch hand lands, plus hand dorks — the same census
  `AntiLifegainProvider`'s fetch ranking already builds;
* `deficit(card) = max( MV − reach, max over colours( pips_needed − src_cnt[colour] ) )`.

**Rule: inside an over-full bucket, a card with `deficit >= 2` ranks below a card with
`deficit <= 1`.** Slack 1 — castable now or after one more land drop — not 2: Minotaur measured
slack 2 firing ~9x more often and coming out ~6x worse, so the loose threshold is a known direction,
not a calibration question.

On this list the term bites exactly two cards, which is the point: **Massacre Wurm** (six mana and
three of eleven black-capable sources) and **Hunted Phantasm** (two of twelve blue-capable). It is
bounded and conditional on purpose — it is not the blanket max-MV rule the evidence rejects.

**Should fetchlands in hand count as colour sources?** Yes, and this is a real decision, because the
AntiLifegain census *excludes* hand fetchlands. In this deck a Windswept Heath or Misty Rainforest
fetches from {Forest, Temple Garden, Breeding Pool, Stomping Ground, Overgrown Tomb} — always G,
plus a *chosen* one of W/U/B/R — so a hand fetchland is a genuine flexible two-colour source, and
the 1 life plus the shock's 2 are free here. It counts as **one** source of the colour it is being
asked for, never as three black sources for the Wurm. Flagged as D4.

**The promotion that erases distance.** A resolved Defense of the Heart with its trigger live erases
the cost of every legal put entirely — but it erases it for the **library** copy, which is precisely
why the **hand** copy becomes redundant rather than cheap. See §8 promotion 1; the two rules point
the same way and do not conflict.

## 7. The total order over a hand

Implemented in the shape the brief and `MinotaurProvider` establish: an **interleaved** slot fill
producing `keep[]` plus an acquisition order, then a shed list of overflow-weakest-first followed by
the keep tail reversed. Interleaved rather than bucket-at-a-time because "five sources" and "one
converter" are constraints on the same eight-card hand, so the ranking has to say *which land beats
which converter* — and read backwards the same list is the order the protected cards give way in,
which is most of them.

**KEEP PRIORITY (fill order, most wanted first):**

```
 1. LAND       -- a missed land drop is unrecoverable, and the 1st land is what makes Crop Rotation live
 2. DRAIN      -- Suture Priest at {1}{W}: castable while land-light, and it multiplies every gift
                  AND the whole Defense of the Heart burst (SacTutorPutList reads the battlefield)
 3. DORK       -- Birds of Paradise: a source that costs no land drop and makes any colour
 4. ENGINE     -- Defense of the Heart: the card the deck is built to cast
 5. LAND       -- 2nd source
 6. MANATUTOR  -- Crop Rotation > Sylvan Scrying (live only at board_lands >= 1)
 7. LAND       -- 3rd source
 8. FUEL       -- the first gift-giver
 9. LAND       -- 4th source
10. LAND       -- 5th source
11. DRAIN      -- 2nd converter (soft: two Priests double every entry)
12. FUEL       -- 2nd gift-giver (soft)
```

DRAIN above the second land because the converter is the scarcer card (4 of 60 against 23 sources of
60) and because cast rank 8 says the converter precedes the fuel. The engine above the second land
for the measured reason in §5.

**SHED ORDER (index 0 first):**

```
S0  DEAD           a self-bouncing Azorius Chancery; a Reflecting Pool that would be our only land.
                   Worth literally zero -- the only cards in the deck of which that is true.
S1  BLANKS         Soul Warden / Essence Warden (ability provably inert; see D1).
S2  REDUNDANT      a gift-giver or sweeper in hand that a LIVE Defense of the Heart is about to
    FETCH TARGET   supply from the library for free (promotion 1 -- fires only in that window),
                   ordered by DESCENDING deficit so the least castable goes first: on this
                   decklist Massacre Wurm before Hunted Phantasm. These are close to tied with
                   S1 and arguably worse; the blank goes first only because its inertness is
                   UNCONDITIONAL, while S2 is conditional on the trigger firing.
S3  SURPLUS MANA   lands past the quota, weakest tier first (Chancery > Reflecting Pool >
                   mono-colour > shock > fetch > City > Orchard); then surplus Birds; then
                   surplus land tutors.
S4  SURPLUS        Enlightened Tutor while an engine is already kept (on board or in hand):
    ENGINE-FINDER  a second route to a card we already hold.
S5  SURPLUS FUEL   reverse of the fuel keep order (War-Riders before Phantasm, unless the
                   distance term flipped them).
S6  SURPLUS DRAIN  reverse of the drain keep order (Massacre Wurm before Suture Priest).
S7  SURPLUS ENGINE a 2nd+ Defense of the Heart -- last among overflow, because copies are additive.
S8  UNRECOGNISED   nothing on this decklist; "no opinion is a reason to protect a card, not to
                   pitch it" (Minotaur's rule). Present so a screening swap cannot fall through
                   to tier B.
S9  KEEP TAIL      the keep priority read backwards: the 2nd fuel, the 2nd converter, the 5th and
                   4th land, the 1st fuel, the 3rd land, the land tutor, the 2nd land, Defense of
                   the Heart, Birds, Suture Priest, and last of all the first land.
```

**Index 0 is determined for any hand**, and the list names every card — the Mirrorwing gi295 lesson.
Worked examples:

* *8 cards, 2 lands in play, hand = 3 lands + Soul Warden + Birds + Suture Priest + Defense of the
  Heart + Massacre Wurm.* Sources = 2 board + 3 hand + 1 Birds = 6 > quota 5, so one land is surplus.
  S1 fires first: **index 0 = Soul Warden.** (Generic sheds Massacre Wurm; the current patch also
  sheds Massacre Wurm.)
* *8 cards, 1 land in play, hand = 1 land + Birds + Suture Priest + Defense of the Heart + Crop
  Rotation + Hunted Phantasm + Massacre Wurm + Enlightened Tutor.* Nothing dead, no blanks, no live
  engine on board. Every other card fills a slot, so Enlightened Tutor is the only overflow and S4
  fires: **index 0 = Enlightened Tutor** — the redundant finder for a card already in hand.
  Everything after it is the keep tail (S9), which begins with Massacre Wurm (the soft second DRAIN
  slot, taken last). Defense of the Heart is 4th from the end.
* *Same hand but a Defense of the Heart has RESOLVED and the opponent is at 4 creatures.* S2 fires:
  **index 0 = Massacre Wurm**, because the engine is about to put a library copy onto the
  battlefield for free at our upkeep.
* *8 cards, 0 lands in play, hand = Azorius Chancery + 6 spells + Essence Warden.* The Chancery has
  nothing to bounce. **index 0 = Azorius Chancery.** (Generic sheds Massacre Wurm and keeps the dead
  land; the current patch does the same.)

## 8. Param-level classification predicates

Named concretely so integration is mechanical. Every characteristic read goes through
`CardDatabase::Instance().LookupCached(card)` — a hand card is a name-only placeholder
(`DeckLoader::MakePlaceholder`), so its own type/cost masks are empty.

```
is_land(c)        CleanupDiscardIsLand(c)
mv(c)             CleanupDiscardManaValue(c)
is_dork(c)        d->tmpl == CardTemplate::ManaDork || d->params.mana_rock
is_karoo(c)       p.etb_bounce_land
is_reflect(c)     p.reflecting
is_fetchland(c)   !p.fetch_land_types.empty()
is_orchard(c)     p.taps_spawn_opp_token                       -> MANA/lands tier 1 (+ fuel)
is_rainbow(c)     p.produces.size() >= 5                       -> MANA/lands tier 2
is_shock(c)       p.etb_pay_life_to_untap > 0

is_manatutor(c)   (p.tutor_land_to_battlefield || p.tutor_to_hand)
                  && any(t == "Land" for t in p.tutor_types)    -> MANA/tutors
                  ... Crop Rotation additionally p.sacrifice_land -> live iff board_lands >= 1

is_drain(c)       p.opp_creature_enters_life_loss > 0           -> DRAIN, entry converter (rank 0)
               || p.opp_dies_life_loss > 0                      -> DRAIN, death converter (rank 1)
is_sweeper(c)     p.etb_opp_creatures_debuff > 0                (what makes a death converter live)

is_fuel(c)        p.etb_opp_creates_tokens > 0                  -> FUEL, burst giver (rank 0)
               || p.cumulative_upkeep_opp_token                 -> FUEL, ramping giver (rank 1)

is_engine(c)      p.upkeep_sac_tutor_creatures > 0              -> ENGINE (rank 0)
is_finder(c)      p.tutor_to_top && p.tutor_types intersects the TYPES of any card in the
                  library/hand carrying upkeep_sac_tutor_creatures   -> ENGINE/finder (rank 1)
                  (derived, not name-bound: swap the engine card and the finder follows)

has_lg_payoff(s)  any card on the battlefield or in hand with
                  lifegain_self_counters > 0 || lifegain_each_own_creature_counters > 0
                  || lifegain_target_own_counter || own_creature_dies_lifegain > 0
is_blank(c)       p.any_creature_enters_lifegain > 0
                  && !is_drain(c) && !is_fuel(c) && !has_lg_payoff(s)   -> BLANK CLASS
```

`is_blank` is written as the *absence* of a payoff on purpose: add an Ajani's Pridemate, a Heliod or
an Archangel of Thune to this deck and the wardens leave the blank class automatically, which is the
deck-screening property the brief asks for. Note the params already separate the wardens from Suture
Priest (`any_creature_enters_lifegain` vs `own_creature_enters_lifegain`), so the `!is_drain` guard
is belt-and-braces rather than load-bearing.

**Board census, computed before any quota:**

```
board_lands       battlefield lands we control (fetchlands never appear -- they sac on activation)
board_sources     board_lands + battlefield dorks/rocks
board_drain       any battlefield permanent with is_drain
board_fuel        any battlefield permanent with is_fuel
board_orchards    battlefield lands with taps_spawn_opp_token
board_engine      any battlefield permanent with is_engine
war_rider_age     sum of Permanent::age_counters over our cumulative_upkeep_opp_token permanents
opp_creatures     opponent creatures on the battlefield
opp_projected     opp_creatures + board_orchards + war_rider_age
src_cnt[colour]   per-colour distinct sources over board + hand (see the distance term)
```

`board_lands` is tracked separately from `board_sources` for the same reason Minotaur does it: the
mana quota is about mana, so a dork counts; the Karoo and Crop Rotation tests are about having a
**land**, and a Birds of Paradise cannot be bounced or sacrificed to Crop Rotation.

**Only one name is used anywhere in this policy**, and it is inherited: `TutorCandidates` already
hardcodes `"Forbidden Orchard"`. This document keys the Orchard on `taps_spawn_opp_token` instead,
so the discard policy itself is fully name-free.

## 9. State promotions

### Proposed — a cleanup ranking can act on all three

**1. THE LIBRARY IS A BETTER PLACE FOR A FETCH TARGET THAN YOUR HAND.** This is the deck's genuinely
distinctive rule, and it comes out the opposite way from the naive reading.

Defense of the Heart searches the **library** — `SacTutorPutList` enumerates library creature names
only. So a creature in hand is *not* a fetch target; it is a copy that has **left** the fetch pool.
When a Defense of the Heart is on the battlefield **and** `opp_projected >= upkeep_sac_tutor_opp_min`
(3) **and** at least one copy of that card's name remains in the library, the engine *will* put the
best two library creatures onto the battlefield at our next upkeep for free — and on this decklist
that scorer's answer is Hunted Phantasm plus Massacre Wurm. Under exactly that state those two cards
in **hand** demote to the blank class (shed slot S2): their whole value is being cheated onto the
battlefield, and the engine is about to do that from a different zone.

Every term is visible or deck information: battlefield contents, opponent creature count, and which
names remain in the library. **Library CONTENTS are not clairvoyance** — the multiset of remaining
cards is derivable from the decklist minus everything seen; library *order* would be. The shared
`CleanupDiscardProtected` already scans `ap.library` under the `LastInDeck` scope, so this is
established practice, and the existing provider comment's "visible information only" claim survives.

Gating on a library copy remaining is what makes it sound: with the last copy in hand, the engine
cannot supply it, and the card returns to its normal bucket.

**2. THE DEAD-CARD TESTS** (Karoo with nothing to bounce; Reflecting Pool as our only land; Crop
Rotation with no land to sacrifice). Pure board reads, and the first two identify the only cards in
the deck worth *literally* zero. The Karoo case is worth a mulligan rule independently of this
policy, exactly as Minotaur and Dragons concluded for theirs.

**3. THE DISTANCE TERM AS A PROMOTION, NOT ONLY A DEMOTION.** A Massacre Wurm at six mana and three
black-capable sources is demoted; the same Wurm with a live engine on board is *free*. Both
directions fall out of one rule, because the free put lands the **library** copy.

### Deliberately REJECTED as search-owned

* **"Shed the Wurm because the board already kills this upkeep."** Real content is a lethal damage
  projection over the coming Suture Priest drain. That is the search's job, precisely as Minotaur
  rejected Fanatic of Mogis's "opponent within reach" term.
* **"Keep the second Suture Priest because a Hunted Phantasm is castable next turn."** An EV
  projection over next turn's mana and cast order. Search.
* **"Keep the land that would let us cast BOTH the converter and the giver this turn."** A cast-set
  affordability question — the enumerator already solves it, and duplicating it in a cleanup ranking
  would be a second, worse copy.
* **A raw spare-copy band.** It was removed as an engine rule on 2026-08-06/07 after losing to
  authored per-deck orders on every deck where duplicates mattered, and on this deck it would be
  actively wrong: spare Defense of the Heart copies and spare Suture Priests are both additive.
  Duplicate pressure is expressed through the quotas instead.

## 10. Implementation contract

* `CreatureGivingProvider::CleanupDiscardCandidates` returns the **full shed order over the hand**,
  most expendable first, routed through
  `CleanupDiscardRankingWithOrder(s, required_pieces, shed)` so the staged-card and required-piece
  protections stay engine-enforced. **Omission = keep** (unused here: the order names every card).
* Behind `static const bool s_bucket = EnvOn("MTG_CG_BUCKET_DISCARD", true);` with `=0` restoring
  `GenericProvider::CleanupDiscardCandidates`.
* **ASK FOR A SECOND ARM.** `=0` dropping to *generic* also drops the existing name patch, so it
  measures "buckets vs no rule at all". The adoption bar is non-inferiority against **what ships**,
  which is the DotH-last patch. Please also wire `MTG_CG_DISCARD_V0` (default off) restoring the
  current one-line override, so the A/B is three-armed: generic / V0 / buckets. Without it a
  regression against the shipped rule is invisible.
* `CleanupDiscardSearchWidth()` stays at the base **1** — the rollout takes index 0 only, which is
  where 100% of this deck's sheds are decided. The executor's searched pass trials exactly the
  returned indices, and it is reached ~1 game in 3,000, so widening the return costs nothing there.
* `CleanupDiscardShedStable()` stays at the default **true**. The liveness flags can only improve
  along a prefix (the Chancery and the Reflecting Pool are the *weakest* lands, so they always shed
  before the land that keeps them live), and the measured `sheds/cleanup` is **1.00** over 3,909
  sheds, so the batched multi-shed path is never exercised on this deck. Verify with
  `MTG_DISCARD_SHED_VERIFY=1` rather than assuming it.
* Fix the stale header comment while touching the function: drop the "spare-copy band" reference.

## 11. Honest assessment

The metric upside is probably small and the case is doctrine plus rollout fidelity, as it was for
Minotaur and Dragons. But this deck's status quo is **worse** than theirs, in a way the numbers
above pin down: the whole decision is a rollout decision (3,909 to 0), it is taken land-light 100% of
the time, and the rule deciding it is descending mana value with one card excused. That rule sheds
the deck's damage converter and its best gift-giver before a Soul Warden, and picks between Birds of
Paradise and a blank by draw order. There is more headroom here than "non-inferiority" suggests, and
the three shed-census lines to read the A/B on are: **Massacre Wurm and Hunted Phantasm shed less,
Soul Warden / Essence Warden and dead Azorius Chancery shed much more, Birds of Paradise shed
~never.**

Validate per the skill: a behavioural diff first (`test/tools/discard_behaviour_diff.py` — cheap, and
on Minotaur the cheap half produced everything the expensive half did), then rule-vs-searched labels,
then smoke + regression through the accept flow. Note the **d0 cell is where this will be visible**:
d5 runs the value leaf, and at d3/d5 this axis has historically been near-null on other decks.

## 12. Doubts — the user reviews and amends these

**D1. Do BLANKS really shed ahead of SURPLUS LANDS?** This is my least certain call and the one that
fires most often. Minotaur sheds surplus lands first (its S1); I put blanks first (S1) and surplus
mana third (S3). My reasoning: the wardens' *ability* is provably inert here (no lifegain payoff in
the 60, and a passive opponent means our life total cannot matter), whereas a surplus land keeps two
live uses — Crop Rotation fodder and the sixth source for a hardcast Massacre Wurm. Against that, a
warden is still a 1-mana 1/1 worth 1 damage a turn, and this deck wins on combat damage more than I
have given it credit for. `card_scores` agrees with me (both wardens below every land but the Karoo),
but those scores are stale (D8). **If you want it the other way, swap S1 and S3.**

**D2. Is the mana quota 5 or 6?** Five assumes the deck reaches Massacre Wurm through Defense of the
Heart rather than by hardcasting it. That is the plan, but the deck holds three Wurms and will
sometimes have to pay `{3}{B}{B}{B}`. I propose a **conditional +1**: raise the target to 6 while the
hand holds a Wurm and there is no engine on the battlefield or in hand. That is derivable and
visible, but it is one more rule and it should be separately leverable so it can be measured on its
own rather than bundled.

**D3. Is the DRAIN quota 1 or 2 (one of each converter)?** Two Suture Priests double every entry and
a Suture-plus-Wurm hand is the deck's best draw, so there is a case for protecting one of each. I
proposed 1 hard plus a soft second slot at ladder position 11, because a hard quota of 2 would
protect a six-mana `{B}{B}{B}` card in the two-land state where 100% of these sheds happen — the
exact Minotaur mistake. I am not confident the soft slot is far enough down.

**D4. Should hand fetchlands count as colour sources?** I say yes (§6) — in this deck a Heath or a
Misty is always G plus a chosen second colour. The `AntiLifegainProvider` census says no. One of the
two is wrong for one of the two decks; I did not measure which, and the difference changes the
`deficit` of Hunted Phantasm and Massacre Wurm in a hand holding fetchlands, which is common.

**D5. Is the projected-opponent-creature formula the right FUEL quota test?**
`opp_creatures + board_orchards + war_rider_age` is a sound lower bound on the count at our next
upkeep, but it is a *projection*, and the brief warns that a promotion whose real content is a
projection belongs to the search. My defence is that every term is a current, visible integer and the
count is monotone against a passive opponent, so no simulation is involved. If you disagree, the
conservative fallback is `opp_creatures >= 3` alone, which under-fills the quota and therefore only
keeps more fuel.

**D6. Enlightened Tutor's `tutor_heuristic` is "enabler_then_wincon", whose named targets are not in
this deck.** The parameter reads *"fetch Tainted Remedy first... else Aria of Flame"* — both
Anti-Lifegain cards. Traced through: with no `lifegain_to_loss` and no `verse_damage` card in the
library it falls to "the first match", and because `CreatureGivingProvider::TutorCandidates` narrows
only **Land** tutors, Enlightened Tutor gets the full Generic list — `{Defense of the Heart, Tree of
Tales}` — and **the search picks**. So the behaviour is fine, but the parameter is a leftover from
another deck and its `oracle_text` note is misleading on this decklist. Worth cleaning up separately;
it does not affect this policy.

**D7. I could not verify the gi564/gi798 hands.** The provider comment is the only surviving record;
there is no per-game data in `docs/` or memory. So I cannot tell whether those hands held one Defense
of the Heart or several — which is exactly the case where this policy is weaker than the name patch
(§5). Read the A/B's shed census for "Defense of the Heart shed" first.

**D8. `card_scores` are STALE and I have used them only as corroboration.** `Creature Giving.profile.json`
dates from 2026-08-06; a great many `src/` commits have landed since, including this deck's own cast
order (2026-08-19), its value leaf and its exhaustive keep model. Per the Minotaur round-4 lesson
they are an engine-state fingerprint, and worse, they were measured while *the generic max-MV rule
was making these very discards*. I have not keyed a single bucket or order off them; every ordering
above is derived from the cards' params and the deck's structure, and the scores are quoted only
where they happen to agree. Do not read the agreement as evidence.

**D9. The MANA / tutors slot counts a tutor as a slot, not as a source.** Crop Rotation and Sylvan
Scrying do not raise `reach` this turn (they spend mana and a card to produce a land), so I gave them
a dedicated ladder slot instead of folding them into the five. The alternative — count Crop Rotation
as a source, since it is mana-neutral and puts the land onto the battlefield untapped — is arguable
and would change which hands have surplus mana at all.

**D10. Nothing in this policy is measured yet.** Every number in §1 is a *census* of how often and
where the rule fires, not a result. The ordering claims are structural arguments; the brief's
adoption bar is non-inferiority plus doctrine quality, and until the A/B in §10 runs (three arms,
please — D of §10) that is all this document establishes.
