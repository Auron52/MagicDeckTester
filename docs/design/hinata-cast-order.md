# Hinata's full cast order (MTG_HINATA_ORDER_FULL)

**Status: BUILT, default OFF. Round 1 measured the order WITHOUT condemnation -- a methodology
error, see §6 -- and it regressed. Round 2 (order x condemnation) is the live measurement.**
Self-contained. Everything needed to resume is here or in git.

## 1. Why this deck needed an order at all

Hinata had **no user-authored cast order**. `HinataProvider` did not override `CastOrderRank`; it
inherited the generic card-parameter tiering, and that tiering puts **seven live cards on rank 20**:

| rank | cards |
|---|---|
| 5 | Sol Ring |
| 10 | Ornithopter of Paradise, Hinata |
| 15 | Reality Spasm |
| 18 | Irencrag Feat |
| **20** | **Gamble, Ponder, Preordain, Expressive Iteration, Crackle with Power, Magma Opus, Soulfire Eruption** (+ 4 never-cast) |

Only 7 of 13 providers override `CastOrderRank` (AntiLifegain, Vial, CreatureGiving, FiveColour,
Mirrorwing, Equipment, Stompy). Hinata, Dragonstorm, Goblins, TreasureHunt and Burn run on generic
tiers.

Two consequences, and the second is what prompted this work:

1. **Condemnation cannot work.** A tie is not a decline, so the `>=` tie-exemption fires and exempts
   the whole class. Hinata does not even opt into `CondemnsConsideredAtBreakpoint` -- correctly, per
   `TurnSolver.cpp`: soundness "is CONDITIONAL on the deck's cast order encoding draws-before-
   deploys", which the generic order does not establish here.
2. **The canonical continuation is undefined.** `MTG_BP_NO_GREEDY_CONT` replaces the greedy
   `TurnSolver::Solve` at a breakpoint with `cands[0]` -- the CANONICAL continuation. Ties fall
   through `std::stable_sort(CastOrderLess)` to plan order, so on Hinata "canonical" meant
   *enumeration order*, not judgement. Hinata is the ONE deck that leans negative under that lever
   (0 better : 4 worse, n=400, t=1.42 -- open item 1 of `bp-greedy-continuation-deletion.md`).

## 2. The order

Reviewed with the USER 2026-08-26. Param-derived, no card names (house style). Implemented in
`DecisionProviders.cpp` (`HinataFullOrderRank`); ranks are scaled by 64 with a mana-value + name-hash
totality guard on the never-cast tier.

| pos | card | why |
|---|---|---|
| — | **land drop** | NOT ranked -- searched at depth > 0 |
| 5 | Sol Ring | untapped {C}{C} the same turn -- real acceleration, unlike the sick dork |
| 6 | Gamble | most information; its random discard is safest on the FULLEST hand (`1/(n+1)` to hit the card just fetched), which also argues early |
| **7** | **Ponder** *and* **Preordain** | **peers -- see §3** |
| 9 | Expressive Iteration | its exiled card is playable THIS TURN only, so a cost-efficient-end placement throws away a third of the card |
| 10 | Ornithopter of Paradise | summoning-sick (no mana this turn) but one more creature for her per-target discount |
| 11 | Hinata, Dawn-Crowned | the engine, before every payoff |
| 15 | Reality Spasm | ritual float before the payoff |
| 20 | Soulfire Eruption | USER: *"Soulfire is typically better than Opus ... I would still put Soulfire higher."* |
| 21 | Magma Opus | payoff that draws 2 |
| 22 | Irencrag Feat | USER: *"must be second last as it can only be cast before Crackle"* |
| 23 | Crackle with Power | last; X consumes all remaining mana |
| 30+ | Icy Blast, Memory Lapse, Remand, Distorting Wake | never cast; positions exist only so the order is TOTAL |

### Two things this order deliberately gives up

* **Irencrag -> Soulfire is no longer expressible.** `max_casts_after: 1` means exactly one spell
  may follow Irencrag, and the USER reserves that slot for Crackle. With Soulfire at 20 and Irencrag
  at 22, a plan holding both casts Soulfire first. This is a narrowing accepted on the USER's
  ruling, not a no-op.
* **Reality Spasm's rank 15 does not bind today.** It is a DEAD CARD in the current model -- the
  static-pool planner does not offer X-untap nor chain the freed mana into a same-turn Crackle. That
  is 4 copies. Rank 15 is a position waiting for the deferred Layer-2 item.

### The land drop is searched, not ordered

USER: *"We also need to list the land drop, which can be before everything or after drawing. The
start is actually fine with clairvoyant as it can play any land currently in hand or draw and then
play a new type of drawn land."*

It is left unranked because the drop is **searched at depth > 0** (folded into the plan), and
measurement says it uses that freedom. Over 120 games at play settings, of **233 turns with both a
land drop and a draw spell, 51 (22%) play the land AFTER a draw spell**, including
`DRAWSPELL, LAND, DRAWSPELL`. Pinning it to "first" would delete those.

> **This is also a concrete reason to expect the greedy deletion to HELP this deck.**
> `TurnSolver.cpp` (`bp_play_searched_land`): *"Play a continuation's SEARCHED land drop ... **Inert
> for a greedy continuation (Solve never sets land_decided)**."* A greedy continuation structurally
> cannot play a land it just drew. On a deck whose early game is cantrip-into-land, every greedy
> continuation is one that cannot use its own draw.

## 3. Ponder / Preordain: measured, then A/B'd both ways

USER: *"Ponder and Preordain have a bit of a messy relationship (interacting with the top of the
deck) that might cause us some issues. It's possible you might sometimes want to do Ponder ->
Preordain -> Ponder"*, narrowed to: *"Only the 'I know what is on the top of the library' effects
like Ponder and Preordain are troublesome here."*

Expressive Iteration looks at three but CONSUMES all three (hand / exile / bottom), so it leaves no
known top and is not entangled. The pair is exactly {Ponder, Preordain}.

**Measured before choosing** (120 games, play settings): 12 turns cast more than one
top-manipulator, and one is exactly **`Ponder, Preordain, Ponder, Preordain`**. That line is
reachable *only because* the rank-20 tie lets `stable_sort` keep plan order. A strict
Ponder-before-Preordain forces `Ponder, Ponder, Preordain, Preordain` and deletes it.

This is the Bonesplitter / Lightning-Greaves case from `TurnSolver.cpp`'s condemnation rule -- peers
at one rank have no enumerated order between them, and the `>=` exemption stops them condemning each
other. So the shipped default is **peers**, and `MTG_HINATA_PP_STRICT` exists to measure the split
the USER asked for ("let's try it both ways"). Frequency is not the axis the no-lossy-truncation bar
uses, so a neutral result still favours `peer`.

**Round 1 could not answer this** -- with condemnation off the split is byte-identical, and the pair
only arbitrate anything once condemnation is on. See §6.

## 4. What was deliberately NOT measured into an order

USER: *"sometimes the order of other draw spells can be better done after or before, but a lot of
this is, I believe, just clairvoyance."*

Agreed, and it is why no position here was fitted to a sweep. An order tuned against a clairvoyant
search would be tuned to information a real player does not have, and would not transfer to the
non-clairvoyant path. Every position above is **structural** (cost, use-or-lose staging, the
Irencrag restriction) or an explicit USER call.

## 5. DEFERRED -- cast order vs the non-clairvoyant path

USER, 2026-08-26, on Reality Spasm's placement: *"(at some point we should look at how this
interacts with the non-clairvoyant path)"*.

Open question, not scheduled. The reviewed orders in this repo are all authored against a
**clairvoyant** search: the engine knows the library, so "cast A before B" is frequently a
formality -- the search would find the right sequence either way, and the order is doing canonical-
isation rather than judgement. On the non-clairvoyant path (the NC play policy) the same order is
load-bearing in a different way: it IS the judgement, because there is no lookahead to correct it.

Worth checking, when it comes up:

* Which positions here are inert under clairvoyance but decisive without it? Reality Spasm's is the
  USER's own example -- a human casts it after tapping out, the clairvoyant search does not care.
* Does an order authored for the clairvoyant engine *mislead* the NC policy anywhere?
* Should a deck be allowed to carry two orders, or is one order with the NC path as the tie-break
  the right shape?

## 6. Round 1, and the methodology error in it

Round 1 (`gen_hinata_order_manifest.py`, n=3000/cell, two blocks, 36,000 games) crossed the order
arms with `MTG_BP_NO_GREEDY_CONT`. Three results:

| finding | numbers |
|---|---|
| **`MTG_BP_NO_GREEDY_CONT` alone is NEUTRAL here** | hold +0.0007 (t=0.41, 10 better : 11 worse); train +0.0013 (t=1.41, 2 : 6) |
| **the full order alone REGRESSED** | hold +0.0210 (t=4.86, 21 : 67); train +0.0270 (t=6.06, 15 : 75) |
| **`MTG_HINATA_PP_STRICT` was BYTE-IDENTICAL to peer** | all 12,000 games, both blocks, with and without the greedy lever |

The first closes open item 1 of `bp-greedy-continuation-deletion.md`: the 0-better:4-worse lean that
made Hinata the one deck blocking that lever was an n=400 artifact and does not reproduce at n=3000.

**The error: condemnation was OFF in every round-1 arm.** `BpClassifyActive` is
`BpClassifyEnabled() || provider.CondemnsConsideredAtBreakpoint()`; `MTG_BP_CLASSIFY` defaults OFF
and Hinata does not opt in (only AntiLifegain and Equipment do). So round 1 priced the order purely
as a re-sequencing of casts -- never as the enabler it was authored to be. USER, catching it: *"Oh,
are we not using condemnation yet?"* Round 2 (`gen_hinata_cond_manifest.py`) crosses the order arms
with `MTG_BP_CLASSIFY` and `MTG_BP_CONDEMN_ORDER_AWARE`.

**A regression measured in the wrong configuration is not evidence about the right one** -- the same
shape as the mirrorwing finding that evaporated across the mana-overhaul rebase.

### Why the Ponder/Preordain split was inert, and when it stops being

An `MTG_CONDEMN_WHO` preflight (6 games, order on, `MTG_BP_CLASSIFY=1`) shows the pair **condemning
each other**: `Preordain @ Ponder` 340, `Ponder @ Preordain` 304. With condemnation OFF the two ranks
never arbitrate anything, hence byte-identical. With it ON, the `>=` tie-exemption is precisely what
decides whether the measured `Ponder, Preordain, Ponder, Preordain` line survives:

* **peer** (both rank 7) -- each is `>=` the other, both exempt, interleave reachable.
* **strict** (7 / 8) -- at a Preordain site, Ponder is strictly earlier and gets condemned, so
  `Preordain -> Ponder` is deleted.

So the USER's "try it both ways" is only answerable in the order-aware condemnation arms
(`peeroa` vs `strictoa`), and round 1's byte-identity is not an answer to it.

The same preflight shows condemnation dropping **Sol Ring 705 times** without order-awareness -- the
accelerant-nailed-to-its-rank failure the mana exemption exists for. `BpSlotIsAfterSite` only
consults that exemption when `MTG_BP_CONDEMN_ORDER_AWARE` is on, which is a second reason the
non-order-aware arm is expected to be the bad one.

## 7. Files

* `src/ai/DecisionProviders.cpp` -- `HinataFullOrderRank`, `HinataProvider::CastOrderRank`,
  `CastOrderTierName`, and the flag readers.
* `src/ai/HeuristicArm.h` -- slots `HINATA_ORDER_FULL`, `HINATA_PP_STRICT` (per-job, so every arm
  pools into ONE `mtg --batch`).
* `test/tools/kitty_ab/gen_hinata_order_manifest.py` -- the 2x3 cross (order arm x greedy lever),
  n=3000/cell, two blocks.
* Inspect with `MTG_HINATA_ORDER_FULL=1 mtg decks/Hinata2/Hinata2.cod --cast-order-report`.
