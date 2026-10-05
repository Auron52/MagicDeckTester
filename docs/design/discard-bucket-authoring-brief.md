# Authoring brief — the per-deck cleanup-discard BUCKET policy

Shared brief for the 2026-09-23 sweep that closes the discard-policy gap across `decks/`. One
agent per deck authors a PROPOSAL from this brief; integration into C++ is done centrally
afterwards (16 agents editing `src/ai/DecisionProviders.cpp` concurrently would collide).

**Your deliverable is a proposal document, NOT code.** Write
`docs/design/<slug>-discard-policy-proposal.md`. Do not edit any `.cpp`/`.h` file.

---

## Rule 0 — read the cards, never recall them

Read **every** card of the deck from `src/cards/data/cards.json` before reasoning about it: mana
cost, P/T, types, `oracle_text`, and above all `params`. Card recall is unreliable and an
unverified claim about a card is the single most common defect in this repo's card work. The
decklist is `decks/<Name>/<Name>.cod` (or `.txt`).

Note any `[bracket note]` in a card's `oracle_text` — it flags a clause the engine models
incompletely, which can change where the card belongs.

## What "bucketed" means (USER, 2026-09-23)

> "Bucketed here just means that we look at what the deck needs and separate the pieces into
> buckets. Simple decks have 2 buckets like mana and threats. Ramp decks tend to have a category
> for ramp as well, though it splits up the ramp. Combo decks want each part of the combo split
> but similar effects grouped."

So: **start from what THIS deck needs to function**, and let the deck's own shape pick the bucket
count. Do not import another deck's bucket list.

| deck shape | buckets |
|---|---|
| simple aggro/midrange | **2** — mana, threats |
| ramp | mana, **ramp** (split by role — see below), threats |
| combo | **one bucket per combo part**, similar effects grouped, plus a dig/cantrip bucket where the deck has one |

"It splits up the ramp" and the mana sub-split are the same idea: LANDS (land drops) vs
DORKS/ROCKS (acceleration) are different roles and you keep some of each — "so you have
acceleration and land drops". Colour coverage comes first: the minimal set that, with the board,
covers the deck's colours.

"Similar effects grouped" is the counterweight to per-piece splitting: two cards that do the same
job in the combo share a bucket; two cards that fill different slots do not.

## The structural rules (all USER-ruled)

1. **Quota-first.** Fill every bucket to its quota BEFORE anything is sheddable. Only overflow
   beyond quota is sheddable, and the shed is the overall-lowest-priority card.
2. **NET OF BOARD (USER, 2026-09-23: "what is on board is considered as part of what we have
   available").** Every permanent-based quota — mana sources, permanent combo pieces, lords,
   enablers — counts the BATTLEFIELD first; the hand owes only the remainder. A role already
   filled on board needs no hand copy. Census the battlefield before you compute a single quota.
3. **Sub-quotas are fungible upward.** The PARENT bucket's total binds; sub-roles only express
   preference within it. A hand with no dorks keeps more lands.
4. **Distance-to-playable orders the shed INSIDE an over-full bucket** — colour/mana coverage
   across board+hand vs the card's cost, not raw size. A payoff that is nowhere near castable
   sheds before a castable one; a promotion that erases the cost (a cost-reducer resolved, a
   free-cast engine online) erases the distance too.
5. **State promotions** where the deck warrants them — but only ones a cleanup ranking can
   actually act on. A promotion whose real content is a damage projection or a cast choice
   belongs to the SEARCH, not here; say so and leave it out.

## Two traps that have each cost a session

* **NAME EVERY CARD IN HAND.** Anything your order omits falls through to the shared fallback's
  tier B, which is **descending mana value** — and on a payoff deck that is backwards (it ranked
  Creature Giving's Defense of the Heart first to pitch, and shed FiveColour's Progenitus for a
  measured 1-turn cost). An under-covering list hands the rest of the decision back to max-MV
  (the Mirrorwing gi295 lesson). Your ranking must totally order the hand so index 0 is always
  the right answer.
* **CLASSIFY BY `params`, NOT CARD NAMES,** wherever a param expresses the role. A screening arm
  that swaps a card must keep the right bucket. Card names are acceptable only for a deck-specific
  VALUE ORDER among cards of the same role (FiveColour's threat order, Equipment's enabler order);
  say which you used and why.

## The implementation contract (describe it; do not write it)

The shipped form is `<Provider>::CleanupDiscardCandidates` returning a **shed order, most
expendable first**, routed through `CleanupDiscardRankingWithOrder(s, required_pieces, shed)` so
the staged-card and required-piece protections stay engine-enforced. **Omission = keep.** Behind a
default-on `EnvOn("MTG_<X>_BUCKET_DISCARD", true)` with `=0` restoring
`GenericProvider::CleanupDiscardCandidates` as the A/B hatch.

Read `MinotaurProvider::CleanupDiscardCandidates` (`src/ai/DecisionProviders.cpp`, search for
`---- MinotaurProvider::CleanupDiscardCandidates`) as the worked reference: its header comment is
the shape your proposal should argue in, and its body shows the board census, the param-level
classification and the reach-conditional distance term. `docs/design/minotaur-discard-policy-proposal.md`
is the proposal shape.

Available helpers: `CleanupDiscardManaValue(card)`, `CleanupDiscardIsLand(card)`,
`IsSubtypeCostReducer(def)`, and `CardDatabase::Instance().LookupCached(card)` — a hand card is a
name-only placeholder, so **every** characteristic read must go through the definition.

## Your proposal document must contain

1. **The deck, in one paragraph** — what it needs to function, and therefore its shape
   (simple / ramp / combo). This is the justification for your bucket COUNT.
2. **A card-by-card role table**: every card in the deck, its mana cost, and the bucket it lands
   in — derived from `cards.json` (cite the `params` you keyed on). Flag any card whose role is
   genuinely ambiguous.
3. **The buckets, with quotas**, each stated net of board, and the sub-splits where the deck has
   them. Say what fills a quota from the battlefield.
4. **The within-bucket order**, and the distance-to-playable term if the deck needs one.
5. **The total order over a hand** — enough that index 0 is determined for any hand.
6. **The param-level classification predicates** you'd implement, named concretely
   (e.g. "`p.mana_rock` → ramp/rocks"), so integration is mechanical.
7. **State promotions** you propose, and the ones you DELIBERATELY rejected as search-owned.
8. **Doubts, flagged.** Judgement calls the deck's cards do not settle. Be explicit — the user
   reviews and amends these.

Keep it honest: if the deck barely sheds, say so. The adoption bar is **non-inferiority** plus
doctrine quality, because the ROLLOUT always sheds heuristically even when real play rarely does —
`real == 0` does NOT mean the rule is inert.
