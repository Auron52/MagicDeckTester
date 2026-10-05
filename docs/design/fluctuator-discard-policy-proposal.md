# Fluctuator — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored from `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 sweep. **Proposal
only** — no `.cpp`/`.h` was touched. The shipped form would be
`FluctuatorProvider::CleanupDiscardCandidates`, routed through
`CleanupDiscardRankingWithOrder(s, required_pieces, shed)`, behind a default-on
`EnvOn("MTG_FLUCT_BUCKET_DISCARD", true)` with `=0` restoring
`GenericProvider::CleanupDiscardCandidates`.

Every card claim below was read out of `src/cards/data/cards.json` (Rule 0), and every quota is
stated net of board. Where a predicate already exists in `FluctuatorProvider` I reuse it by name
rather than re-deriving it — that provider's own history says why (reading `produces` raw once made
a board of `{Polluted Mire, Capital City}` claim it could pay `{1}{R}`, and the phantom stopped the
deck cycling a turn early every time a Capital City was out).

---

## 1. The deck in one paragraph — and therefore its shape

Fluctuator is a **CYCLING COMBO** deck, and an unusually pure one: **55 of its 60 cards carry
`cycling_cost`**. Fluctuator (`reduces_cycling_activation: 2`, floor **zero**, no Training-Grounds
"can't reduce below one" clause) makes every `{2}` cycler in the list **free**; Drannith Stinger
(`cycle_trigger_damage_each_opponent: 1`) turns each of those free cycles into 1 damage; Hollow One
(`cost_less_per_cycle_or_discard: 2`) becomes a `{0}` 4/4 after three cycles; Unearth
(`reanimate_creature_max_mv: 3`) buys a binned Stinger back for `{B}`. The deck therefore needs four
distinct things — the **enabler** that makes cycling free, the **threat** that converts cycles into
damage, the **rebuy** that redeploys it, and the **clock** that the close-out arithmetic counts as
`swing` — plus **mana**, whose only real jobs are `{2}` for the enabler, `{1}{R}` for the threat and
`{B}` for the rebuy. That is the combo shape from the brief's table: **one bucket per combo part,
similar effects grouped**. The brief also allows "a dig/cantrip bucket where the deck has one", and
this deck's answer is the important structural point below: **the dig is not a bucket here, it is a
property every card has.**

### The dig is a PROPERTY, not a bucket — so "a dead card" does not exist in this deck

`cycling_cost` is on 55 of 60 cards. Under a resolved Fluctuator every one of them is a **free draw
plus one point of damage per Stinger on the battlefield**. The deck's own adopted arithmetic already
prices that exactly: `HoldFuelWhileComboing` and `FluctuatorWantsSecondThreat` both compute
`lib * sting * heads` against `opp_life - swing`, i.e. **each card in hand is worth
`sting * heads` damage.** So the floor value of a card in this hand is never zero, and the usual
"shed the dead card" framing has nothing to grip.

It also means the **two cards without `cycling_cost` — Fluctuator and Enlightened Tutor — are the
only cards in the 60 that can be truly dead**, because they are the only ones that cannot be
converted into a draw. That inverts the naive reading (an uncastable `goldfish_inert` Forsake the
Worldly *looks* like the dead card; it is not — it is a free cycle) and it is what sets index 0.

---

## 2. Card-by-card role table

Every card in the 60, with the `params` the classification keys on.

| n | card | cost | type | keyed on (`params`) | bucket |
|---|---|---|---|---|---|
| 4 | **Fluctuator** | `{2}` | Artifact | `reduces_cycling_activation: 2` — **and no `cycling_cost`** | **ENABLER** |
| 1 | **Enlightened Tutor** | `{W}` | Instant | `tutor_to_top`, `tutor_types: [Artifact, Enchantment]` — **no `cycling_cost`** | **ENABLER** (finder) |
| 4 | **Drannith Stinger** | `{1}{R}` | Creature 2/2 | `cycle_trigger_damage_each_opponent: 1`, `cycling_cost {1}` | **THREAT** |
| 4 | **Unearth** | `{B}` | Sorcery | `reanimate_creature_max_mv: 3`, `cycling_cost {2}` | **REBUY** |
| 4 | **Hollow One** | `{5}` | Artifact Creature 4/4 | `cost_less_per_cycle_or_discard: 2`, `cycling_cost {2}` | **CLOCK** |
| 1 | **Forsake the Worldly** | `{2}{W}` | Instant | `goldfish_inert: true`, `cycling_cost {2}` | **FODDER** |
| 4 | Capital City | — | Land (Town) | `any_color_filter`, `produces WUBRG`, **untapped**, `cycling_cost {2}` | MANA — universal fixer |
| 4 | Canyon Slough | — | Land | `produces [B,R]`, `enters_tapped`, `cycling_cost {2}` | MANA — **both key colours** |
| 4 | Blasted Landscape | — | Land | `produces [C]`, **untapped** (no `enters_tapped`), `cycling_cost {2}` | MANA — colourless, untapped |
| 4 | Fetid Pools | — | Land | `produces [U,B]`, `enters_tapped` | MANA — B (U casts nothing) |
| 4 | Polluted Mire | — | Land | `produces [B]`, `enters_tapped` | MANA — B |
| 2 | Festering Thicket | — | Land | `produces [B,G]`, `enters_tapped` | MANA — B (G casts nothing) |
| 4 | Sheltered Thicket | — | Land | `produces [R,G]`, `enters_tapped` | MANA — R (G casts nothing) |
| 4 | Glittering Massif | — | Land | `produces [R,W]`, `enters_tapped` | MANA — R (+W) |
| 2 | Smoldering Crater | — | Land | `produces [R]`, `enters_tapped` | MANA — R |
| 4 | Irrigated Farmland | — | Land | `produces [W,U]`, `enters_tapped` | MANA — W (U casts nothing) |
| 4 | Scattered Groves | — | Land | `produces [G,W]`, `enters_tapped` | MANA — W (G casts nothing) |
| 2 | Drifting Meadow | — | Land | `produces [W]`, `enters_tapped` | MANA — W |

**Which colours are RELEVANT is user-ruled, not inferred.** `decks/Fluctuator/Fluctuator.buckets.json`
carries the 2026-09-06 rulings: the W-cyclers "all give W for Enlightened Tutor / Forsake the
Worldly; the **U/G side colours cast nothing in this list**"; the B-cyclers' "B for Unearth is the
only relevant colour"; and Canyon Slough is kept apart from the R-only bucket because it "produces
BOTH of the deck's key colours (B for Unearth, R for Drannith Stinger)". So the relevant colour set
is **{R, B, W}** — and **W is relevant only while an Enlightened Tutor is in hand**, because Forsake
the Worldly is `goldfish_inert` and is never cast. U and G are never relevant.

Land census (computed, not recalled): **34 of 42 lands enter tapped; 8 enter untapped** — 4 Blasted
Landscape (`{C}`) and 4 Capital City. Capital City is the deck's **only untapped source of coloured
mana**, and its colour is *fed* (`{1}, {T}`), so reaching it costs a unit from another source.

**Ambiguous roles, flagged:**

* **Enlightened Tutor.** Its `oracle_text` bracket note describes the *Anti-Lifegain* usage
  ("Tainted Remedy first, else Aria"); `enabler_then_wincon` resolves `lifegain_to_loss` then
  `verse_damage`, **neither of which exists in this deck**, and `FluctuatorProvider` does not
  override `TutorCandidates`, so the generic search-primary default applies: the search chooses
  between **Fluctuator** and **Hollow One** (the only Artifacts in the list). So the Tutor is an
  enabler-finder *primarily* and a "put a free 4/4 on top for `{W}`" *residually* — which is why it
  is not quite as dead as a surplus Fluctuator once the enabler is online.
* **Hollow One** is both a payoff and, in the enabler-less state, an uncastable brick. It gets the
  one distance-to-playable rule this policy uses (§5, D1).
* **Capital City** is a land whose value is *fixing*, not count. It is the last land shed in almost
  every state.

---

## 3. Is a cleanup shed a COST or a BENEFIT here? — **a COST, and the two upside stories do not hold**

The brief's prompt asks this directly, because `DecisionProvider::DiscardLandsFirst` exists for the
deck where a discard IS the payoff (Land's Edge: `discard_land_damage`, lands are ammunition and the
shed *is* the damage outlet), and Minotaur's Neheb inversion is the same shape. **Neither applies
here, and the structural analogy actually runs the other way.**

**Story 1 — "shedding cheapens Hollow One". FALSE, and not by a modelling gap: by timing.**
The cleanup discard happens in the **cleanup step** (CR 514.1), after the last main phase. Hollow
One is a creature: it cannot be cast in the cleanup step, and by the caster's next main phase the
`this turn` counter has been zeroed at untap (`GameEngine.cpp:224`, `TurnSolver.cpp:29838`). The
engine models this exactly — `Player::cards_cycled_or_discarded_this_turn` is incremented **only at
the cycle site** (`SpellEffects.h:20355`), and Hollow One's own bracket note states the reason and
proves it exact for this deck: *"the only other discard, the cleanup shed, happens after the last
main phase and is zeroed at the following untap, so it can never cheapen a cast."* So a cleanup shed
**cannot ever** discount a Hollow One, in the engine or in real Magic. This is not a promotion to
defer to the search — it is arithmetically unreachable, and a policy that assumed otherwise would be
pricing a benefit that does not exist.

**Story 2 — "a shed creature becomes an Unearth target". TRUE, but narrow.** Unearth returns a
creature card of mana value ≤ 3; the deck's **only** creature inside that cap is Drannith Stinger
(MV 2 — Hollow One is MV 5 and is *not* a legal target, per Unearth's own note). The graveyard
persists and one target is enough, so this is worth something **exactly once**, and only while an
Unearth is held with a payable route. It makes shedding a Stinger *cheap* in one specific state — it
does not make shedding *good* in general. It is the whole content of promotion **R1** in §5.

**Why the Land's-Edge analogy inverts.** In this deck the ammunition is spent by **CYCLING**, which
is `1-for-1` (discard the card, draw a card, ping per Stinger). A cleanup shed is `1-for-0`: same
card gone, **no draw and no ping**. So a shed is strictly worse than the cycle that was available
for the same card, and it destroys ammunition instead of firing it. Concretely: **`DiscardLandsFirst`
must stay `false` for this deck** (it is inherited from `GenericProvider` and this proposal does not
touch it) — "lands first" would shed the deck's most plentiful ammunition ahead of cards that have
no other use at all.

**Therefore index 0 is not "the card whose shed profits most" — it is the card whose loss costs
least,** and the deck's own floor value (one cycle = one card + `sting * heads` damage) makes that a
genuine total ordering rather than a hunt for a dead card. There is no Neheb-style inversion to
exploit, and I say so explicitly rather than inventing one.

**What IS inverted is the generic fallback.** Tier B is descending mana value, which on this
decklist is:

    Hollow One (5)  ->  Forsake the Worldly (3)  ->  Fluctuator (2) / Drannith Stinger (2)
      ->  Enlightened Tutor (1) / Unearth (1)  ->  every land (0), LAST

So the shared rule sheds the **free 4/4 clock first** and keeps the seventh redundant land; it puts
the **threat and the engine ahead of every land**; and in a hand that is (typically) 70% lands it
will reach for a combo piece while five sheddable lands sit in hand. That is the Dragons-class
inversion — on this deck the cheap cards *are* the mana and the expensive card *is* the payoff — and
it is the reason to ship a policy here.

---

## 4. The buckets, with quotas (net of board)

Board census first, per the brief's rule 2. Count over `s.battlefield` for
`p.controller_index == active`: `bf_enabler` (`reduces_cycling_activation > 0`), `bf_stinger`
(`cycle_trigger_damage_each_opponent > 0`), `bf_clock` (`cost_less_per_cycle_or_discard > 0`),
`bf_sources` + their colours (conversion-aware, see below), `swing` (attack-capable power, via
`CanAttackFull`, exactly as `HoldFuelWhileComboing` computes it), plus `yard_target` (a creature card
in the graveyard with MV ≤ the held Unearth's `reanimate_creature_max_mv`).

**B1 — ENABLER. Quota 1, net of board.** `bf_enabler >= 1` ⇒ quota **0**: a second Fluctuator
reduces nothing (the floor is already zero and the deck's costliest cycle is `{2}`), which is the
same judgement `SelectDigSource` already encodes by ranking a redundant enabler copy near-first
fodder. Members: `reduces_cycling_activation > 0` (Fluctuator) and the **enabler-finder**
(Enlightened Tutor). Keep-best-first inside the bucket: **Fluctuator > Enlightened Tutor** (the
Tutor costs a card and a turn to convert, and needs `{W}`). The provider already declares these two
as one role via `InterchangeableRequiredGroup`, so this bucket is that group.

**B2 — THREAT. Quota 1, net of board** (`bf_stinger` fills it); **2** when the deck's own close-out
inequality fails, i.e. `lib * sting * heads < opp_life - swing` — the library cannot carry the kill
with the Stingers already out, which is precisely when `FluctuatorWantsSecondThreat` says a second
one is worth a card. Reuse that expression rather than a new projection, so the two can never
disagree (the discipline the provider's own comments insist on).

**B3 — REBUY. Quota 1, always.** User doctrine, quoted in the provider: an Unearth held over an
empty graveyard *"is not a dead card — it is half the wincon, and the Stinger it will rebuy has not
been binned yet."* Quota **2** when the same close-out inequality fails **and** a target exists or is
in hand (the `FluctuatorSecondThreatLive` shape). Note the ordering consequence: this bucket is
*not* demoted for having no legal target today — that inversion was already found and fixed once on
the cycling side.

**B4 — CLOCK. Quota 1, soft, net of board** (`bf_clock` fills it). Hollow One is what the close-out
arithmetic subtracts as `swing`; 4 power for `{0}` is real, and the deck's `HoldFuelWhileComboing`
rule exists partly because that body is usually already attacking by the kill turn. **This is my
least-certain quota** (§8).

**B5 — MANA (lands). Quota = COVERAGE + COUNT, net of board, coverage dominating.**
* **COVERAGE (first, always):** ≥1 **R** source (Drannith Stinger `{1}{R}`), ≥1 **B** source
  (Unearth `{B}`), and ≥1 **W** source **only while an Enlightened Tutor is in hand**. Computed with
  the provider's existing conversion-aware accounting (`FluctuatorCastRouteReachable` /
  `IsManaConversionSource`), never off raw `produces` — Capital City's five colours cost a **feed**
  from another source, and reading them as free is the exact bug that already cost this deck a turn
  of cycling per game.
* **COUNT:** **4** total sources while `bf_enabler == 0` (`{2}` for the enabler plus `{1}{R}` for
  the threat), **3** once an enabler is resolved (the only remaining casts are `{1}{R}` and `{B}`).
  The count is deliberately tiny because this deck's mana needs are tiny: its most expensive spell
  is **free**, and every source past coverage buys nothing but ammunition.
* **COUNT → COVERAGE-ONLY while `HoldFuelWhileComboing(s, me)` is true.** This is not a nicety: that
  rule **skips the land drop entirely** at both main-phase sites (`AIEngine.cpp` ~2871 and ~2910:
  *"A full SKIP, not a defer"*), so while the chain is live a land in hand has **zero** drop value
  and is purely ammunition. Gating on the same predicate keeps the shed and the land-drop rule from
  contradicting each other.
* **Sub-split — the deck's analogue of "lands vs dorks".** There are no dorks or rocks here, so the
  mana sub-split is by **fixing quality**, kept-best-first:
  1. **Capital City** — `any_color_filter`, untapped, covers every colour. Last land shed.
  2. A land supplying a **needed, uncovered** relevant colour — **Canyon Slough first** among duals
     (the only land covering both key colours; user ruling 2026-09-06).
  3. **Untapped** (`!enters_tapped`) — Blasted Landscape. Colourless, but 34 of 42 lands enter
     tapped, so "mana on the turn it lands" is this deck's scarcest land property and can move a
     cast a full turn earlier.
  4. Tapped land whose relevant colour is **already covered**.
  5. Tapped land with **no** relevant colour — the W-cyclers when no Tutor is held. Most expendable
     land in the deck.

**B6 — FODDER.** `goldfish_inert` (Forsake the Worldly), plus every card overflowing B1–B5.

---

## 5. Within-bucket order, the distance term, and the state promotions

**D1 — the one distance-to-playable rule: the CLOCK's distance, erased by the enabler.** Hollow One
costs `5 - 2 x (cycles this turn)`. With an enabler resolved it is reliably `{0}` (three free
cycles, and 55 of 60 cards cycle), so the distance is **zero** and the generic max-MV instinct is
simply wrong. With **no** enabler on the battlefield *and none castable in hand* and board+hand
sources `< 5`, it is genuinely uncastable for multiple turns and its only live value is the cycle —
which any land also provides *while additionally advancing the mana count*. So, reach-conditional
and bounded exactly like Minotaur's Ragemonger clause: **while `bf_enabler == 0`, no enabler in hand
is castable within reach, and `reach < 5`, the CLOCK quota drops to 0 and Hollow One sheds AHEAD of
overflow lands.** The moment an enabler is online the promotion reverses and Hollow One returns above
lands. This is the single place where this policy and the generic fallback agree, and the single
place it matters that the agreement ends.

**No distance rule for the THREAT, deliberately.** A Stinger with no R source reachable is "far
away", but the deck's own user-ruled doctrine is *"if you can't play it this turn you should wait
until you can"*, and demoting the wincon on distance is how the first version of `SelectDigSource`
got the deck's signature line backwards. The Stinger's absolute value dominates; rule 4 is not
applied to it.

**R1 — REBUY-COMPLETING THREAT SHED (the only clause where a shed carries upside).** Shed a Drannith
Stinger *ahead of overflow lands* when **all** of:
* an Unearth is in hand whose `{B}` is payable from the lands we control
  (`FluctuatorCastRouteReachable`), **and**
* the graveyard holds **no** creature within the Unearth cap (`!yard_target`), **and**
* the Stinger's own `{1}{R}` is **not** payable from those same lands.

In that state the shed converts a dead Unearth into a **1-mana deployment of the threat**, where
holding the Stinger means waiting for a second land *and* a red source. It is the deck's signature
line one zone over — the same argument `FluctuatorImmediateRebuyExecutable` already makes on the
cycling side, where reference `s10`'s T3 showed the hardcast unpayable while the `{B}` route was
live and the "wait" reading cost the kill a full turn. Proposed with its own sub-lever
`MTG_FLUCT_SHED_REBUY` (default on inside the policy, `=0` to isolate it) because it is the clause I
would measure first (§8). It never fires against the last Stinger with no Unearth — that protection
is the package rule the provider already documents.

**Promotions I deliberately REJECT as search-owned (or impossible):**
* **"Shed to cheapen Hollow One" — IMPOSSIBLE**, not deferred. §3, story 1.
* **A per-card damage projection** ("which card's ping matters most") — search-owned, exactly as
  Minotaur rejected Burning-Fist ammunition and the Fanatic devotion reach. I reuse only the deck's
  **closed-form** close-out inequality, and only as a quota modifier.
* **"Cycle it instead of shedding it"** — not a cleanup decision at all: the cleanup step is past
  every activation window, so by the time this ranking runs the cycle was already offered (and
  declined, or unaffordable) in the main phase.
* **"Shed the Unearth, it has no target"** — rejected on the user's own doctrine; the target has not
  been binned *yet*, and R1 is the correct direction (shed the Stinger to *create* the target).

---

## 6. The total order over a hand — index 0 is determined for any hand

Walk the tiers; within a tier use the stated sub-order; ties break on **lowest per-copy
`m_number`** so the executor and the rollout shed the identical physical card (the lockstep
discipline `SelectDigSource` uses).

**The tie-break is deliberately `m_number` and deliberately NOT a colour-breadth split.** Two lands
in the same sub-class — a Drifting Meadow and a Scattered Groves with no Tutor in hand, or a Polluted
Mire and a Fetid Pools — are *identical for this deck*, and that is a user ruling, not an assumption:
`Fluctuator.buckets.json` merges the three W-cyclers and the three B-cyclers precisely because "the
U/G side colours cast nothing in this list", and records that the generator's attempt to split them
on distances landed "in the 0.01–0.06 coin-flip band … the same failure mode as the recorded
FiveColour fetchland case". Inventing a "fewer `produces` colours sheds first" refinement here would
re-create exactly the split the user struck down.

| tier | what | why it is here |
|---|---|---|
| **S0** | **Dead, non-ammunition enabler copies** — a Fluctuator while the ENABLER quota is met, then an Enlightened Tutor while it is met. Fluctuator first. | The only cards in the 60 that can be neither cast usefully nor cycled (no `cycling_cost`). A surplus Fluctuator is worth **exactly zero**; the Tutor still has a residual `{W}` "Hollow One to the top". |
| **S1** | **Forsake the Worldly** (`goldfish_inert`). | Never castable against this opponent, so its only value is one cycle. Matches `SelectDigSource` rank 0. |
| **S2** | **R1 Stinger shed**, when its three conditions hold. | The one shed that turns a dead card live. |
| **S2b** | **Hollow One**, when D1 fires (`bf_enabler == 0`, no castable enabler in hand, `reach < 5`). | Uncastable for multiple turns; ammunition only, and unlike a land it advances nothing. |
| **S3** | **Overflow lands** beyond the MANA quota, in B5's sub-order (most expendable first: no-relevant-colour tapped → covered-colour tapped → untapped → uncovered-colour → Capital City). | Overflow mana is ammunition, and while `HoldFuelWhileComboing` is true it is *only* ammunition (the drop is skipped). |
| **S4** | **Surplus Hollow One** beyond the CLOCK quota. | Matches the cycle spine (lands rank 3 spend before Hollow One rank 4 — i.e. Hollow One is valued above a land). |
| **S5** | **Surplus Unearth** beyond the REBUY quota. | The cycle spine ranks Unearth last (rank 5) among fodder; "half the wincon". |
| **S6** | **Surplus Drannith Stinger** beyond the THREAT quota. | The wincon; a surplus copy is still the second finish. |
| **S7** | **Last-resort tail, quota-protected cards:** the CLOCK → the least-needed in-quota land (the W source with no Tutor held, then a redundant-colour land; the last R source, the last B source and Capital City go last of all) → the REBUY → the THREAT → the ENABLER. | Named so the order is **TOTAL**. Anything omitted falls through to tier B = descending MV, which on this deck sheds Hollow One first and a Fluctuator before any land — the Mirrorwing gi295 failure mode. |

No widening cost: the shared base ranking already returns the **whole hand** (tiers A+B+C), so
`AIEngine::ChooseDiscard`'s searched fan is the same size as today — only the *order* changes, and
this deck's cleanup hands are 8–9 cards, not Treasure Hunt's 15–25.

**Worked examples.**
* Hand (T2, no board): `Fluctuator, Fluctuator, Polluted Mire, Canyon Slough, Blasted Landscape,
  Drifting Meadow, Hollow One, Unearth` — 8 cards, one over. ENABLER quota 1 ⇒ the second Fluctuator
  is S0 ⇒ **index 0 = the surplus Fluctuator** (see the inertness caveat in §8, D-a; today the engine
  vetoes this and the answer falls to S3, the Drifting Meadow, which is still right and still not
  what generic does — generic sheds the **Hollow One**).
* Hand (going off: Fluctuator + Stinger on board, land drop suppressed): `Blasted Landscape,
  Scattered Groves, Fetid Pools, Hollow One, Hollow One, Unearth, Forsake the Worldly, Capital City`
  ⇒ S1 ⇒ **index 0 = Forsake the Worldly**. Generic sheds a Hollow One.
* Hand (T3, one tapped Polluted Mire on board, no enabler): `Drannith Stinger, Unearth, Hollow One,
  Smoldering Crater, Drifting Meadow, Scattered Groves, Fetid Pools, Blasted Landscape`. D1 does
  **not** fire (`reach` = 1 board + 5 hand lands = 6, which is ≥ 5), so Hollow One keeps its quota
  slot. MANA: B covered by the board, R by the Smoldering Crater, W irrelevant (no Tutor held), count
  4 ⇒ 3 hand lands kept, **2 overflow**, and the overflow class is "tapped, no relevant colour" =
  the Drifting Meadow and the Scattered Groves ⇒ **index 0 = whichever of those two has the lower
  `m_number`** (they are interchangeable by the user's own W-cycler merge). Generic sheds the
  Hollow One.

---

## 7. Param-level classification predicates (so integration is mechanical)

All reads go through `CardDatabase::Instance().LookupCached(card)` — a hand card is a name-only
placeholder. **Every param below defaults to `0`/`false` in `CardDatabase.cpp`** (lines 746, 778,
972, 1251–1255), so the `> 0` tests are safe; this was checked explicitly because the Dragons policy
shipped broken on `reduces_spell_subtype_amount`, which defaults to **1**.

| predicate | bucket / use |
|---|---|
| `d->params.reduces_cycling_activation > 0` | ENABLER (and the board census's `bf_enabler`) |
| `d->params.tutor_to_top && !d->params.tutor_types.empty()` **and** some card in library+hand matching those `tutor_types` carries `reduces_cycling_activation > 0` | ENABLER (finder). The refinement keeps a screening arm honest: a tutor that cannot find this deck's enabler is FODDER, not an enabler. Scan once per call, and only when a tutor is actually in hand. |
| `d->params.cycle_trigger_damage_each_opponent > 0` | THREAT (and `bf_stinger`) |
| `d->params.reanimate_creature_max_mv > 0` | REBUY; the value is also the graveyard-target MV cap |
| `d->params.cost_less_per_cycle_or_discard > 0` | CLOCK (and `bf_clock`) |
| `d->params.goldfish_inert` | FODDER (uncastable in this matchup) |
| `d->params.cycling_cost.has_value()` | the **ammunition** property — its ABSENCE is what makes S0 dead |
| `CleanupDiscardIsLand(card)` | MANA |
| `d->params.any_color_filter` | universal fixer — last land shed |
| `!d->params.enters_tapped` | untapped land — ranks above tapped peers |
| `d->params.produces` ∩ {R, B, W-if-Tutor-held} | relevant-colour contribution, **via `FluctuatorCastRouteReachable`'s conversion-aware accounting**, never raw |
| `HoldFuelWhileComboing(s, me)` | going-off state ⇒ MANA count collapses to coverage-only |
| `lib * sting * heads < opp_life - swing` | close-out failure ⇒ THREAT quota 2, REBUY quota 2 |
| `FluctuatorCastRouteReachable(s, cost)` | R1's two payability legs, and the enabler-castable leg of D1 |

The only **card-name** judgement anywhere in this policy is the *within-bucket keep order*
(Fluctuator before Enlightened Tutor; Canyon Slough first among duals) — and both of those are
themselves param-derivable (no `cycling_cost` + the enabler param; the size of the
relevant-colour intersection). Nothing keys on a name at classification time.

---

## 8. Doubts — the user reviews and amends these

**D-a (the big one). S0 is INERT TODAY, and fixing it is a profile change I am not making.**
`Fluctuator.profile.json` lists `required_pieces: ["Fluctuator", "Enlightened Tutor"]` and **omits
`discard_protect`**, so the scope is the default **`All`** (`DiscardPolicy.h`) — *every copy* of both
cards is protected, and `CleanupDiscardRankingWithOrder` explicitly drops protected entries from the
provider's tier A ("a provider order does NOT get to override required-piece protection"). So the
truly-dead surplus Fluctuator can never be index 0 as things stand; S0 is a correct clause that
never fires, which is exactly the Dragons-class defect this brief warns about. The fix is one
profile line — `"discard_protect": "deck"` (protect only the genuinely irreplaceable last copy;
with 4 Fluctuators in the list, early-game redundant copies become sheddable) or `"hand"`. Two notes
for that decision: (i) the value sidecar is `leaf: "none"`, so **no learned model** is at risk — only
the measured ladder choice; (ii) the 18 MB exhaustive keep table was generated under current play, so
a play change ages it. **Recommendation: ship the policy with S0 stated but inert, and A/B
`discard_protect` separately.** I did not touch the profile.

**D-b. The CLOCK quota (1 vs 0).** Keeping one Hollow One assumes the free 4/4 is worth more than
one cycle. Against it: the library holds 4 and every cycle can find one, and `SelectDigSource`'s
land-before-Hollow-One order was **measured inert** (0 of 100 games) on the cycling side — so the
shed side may be equally insensitive. This is the quota I would sweep second.

**D-c. Land sub-order: untapped vs colour breadth.** I ranked "untapped" above "more relevant
colours" among *already-covered* lands, on the ground that 34 of 42 lands enter tapped so
same-turn mana is the scarce property. The opposite is defensible and it is a cheap A/B.

**D-d. R1 is the only clause claiming a shed is upside.** It is tightly gated (rebuy payable,
hardcast not payable, no yard target), which is deliberately narrower than the cycling-side rule
because a shed gives **no draw and no ping**. Measure it with `MTG_FLUCT_SHED_REBUY` before trusting
it. If the user prefers, ship it **off** and turn it on only on evidence.

**D-e. The MANA count (4 / 3 / coverage-only).** Worth a 3/4/5 sweep. Too low risks shedding the
colour the deck cannot cast without; too high pushes the shed onto a combo piece, which is where the
generic rule already fails.

**D-f. How often does this actually fire? I did not measure it, and I am recording that as UN-RUN,
not as zero.** No `MTG_SHED_STATS` census was taken: the box was at **load 26 on 24 cores** with the
16-agent sweep live, and the box rule is one batch/test at a time, so a timing- or
utilisation-sensitive probe would have measured contention rather than the deck. What the *structure*
says in the meantime, and it cuts both ways:
* **Cycling is hand-size NEUTRAL** (discard one, draw one), so the deck cannot cycle its way under
  the limit; the hand shrinks only via the land drop and casts. With 42 lands the deck makes a drop
  nearly every turn, so the hand hovers at 7 and **real-play sheds should be rare** — consistent
  with the recorded note that this deck's hands are unusually well-optimised by its mulligan
  (~92% of play cost is clairvoyant bottoming rollouts).
* **But `HoldFuelWhileComboing` SKIPS the land drop while the chain is live.** In that state the hand
  grows by exactly +1 per turn, so **every cleanup of a going-off chain that fails to kill takes a
  shed** — and the generic answer there is to pitch a Hollow One or a Stinger out of a hand that is
  otherwise all ammunition. That is the sharpest real-play case for this policy.
* And per the brief, the denominator that matters is the **rollout**: index 0 decides every rollout
  shed with no search above it, and this deck's mulligan generation plays out keeps by the hundred
  thousand. `real == 0` would not make the rule inert.

**D-g. The rule-vs-searched zero-regret check is UN-RUN** (it needs a FAN lever this provider does
not have) — same status Dragons shipped with, recorded as un-run rather than passed.

**D-h. A cards.json nit, non-behavioural.** Capital City's bracket note says *"38 of this deck's 42
lands enter TAPPED"*. The count is **34** (the 4 Blasted Landscape enter untapped — its own note says
so explicitly, and `enters_tapped` is absent). The note's load-bearing claim — Capital City is the
only untapped source of *coloured* mana — is correct.

## Expected outcome, honestly

The adoption bar is **non-inferiority plus doctrine quality and rollout fidelity**, and that is what
I expect. The one place I predict a measurable difference is the going-off-chain cleanup, where
generic pitches the `{0}` 4/4 or a combo piece out of a hand of pure ammunition and this policy
pitches a redundant tapped land. Everything else is rollout fidelity: the search's idea of what a
Fluctuator position is worth currently includes "and then we throw away the Hollow One".
