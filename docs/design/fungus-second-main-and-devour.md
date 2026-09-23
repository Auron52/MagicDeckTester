# Fungus: the second main, card placement, and implementing devour properly

**Status:** MEASURED 2026-09-23 (§6). The second main is a REAL quality win on every cell and is
BUILT behind `MTG_FUNGUS_M2_DEVOUR` / `MTG_FUNGUS_M2_GATE`, both default OFF pending adoption.
Adoption cascades into the value leaf -- see §6d before deciding.

**Origin.** This is the Fungus ledger's **Q1** (`analysis-Fungus.md`), which shipped
first-main-only as the documented default. The user weighed in on 2026-09-17:

> *"Devouring is one of the use-cases for second main I would say. On the flip side Beastmaster
> Ascension and Sporecrown Thallid wants to be in the first main. We could probably split the cards
> into first and second if needed, though. [...] We may want to properly implement devour for the
> future."*

---

## 1. Why the deck wants both mains

| card | wants | why |
|---|---|---|
| **Mycoloth** (`devour 2`) | **Main 2** | Attack with the Saprolings first, *then* cast Mycoloth devouring the bodies that already dealt their damage. In a goldfish the opponent never blocks, so the attackers always survive combat — the same creature banks its damage **and** its devour counters. With only a first main the search must trade one for the other. This is precisely the pattern the repo already whitelisted **Birthing Pod** for (`pod_mv_delta`). |
| **Beastmaster Ascension** (`quest_counter_per_attacker`) | **Main 1** | Quest counters come from **declared attackers** (CR 508.2). An Ascension cast post-combat banks nothing that turn — it must be on the battlefield before attackers are declared or the whole turn is wasted. |
| **Sporecrown Thallid** (`lord_effect`) | **Main 1** | An anthem only helps the combat it precedes. |

## 2. What the engine already does — more than expected

The "split the cards into first and second" mechanism **already exists and is mature**;
this is about using it, not building it.

* `ClassifyMainPhase` (`src/ai/TurnSolver.cpp`) returns `Main1` / `Main2` / `Both` per card.
* **Sporecrown Thallid is ALREADY Main1** — `CardTemplate::LordEffect` is in the attack-helping
  list, and its `power_bonus` would catch it independently. Nothing to do.
* `DecisionProvider::MainPhaseOverride(state, def)` is the per-deck hook, consulted **first**.
* `MainPhaseFilterActive` is **double-gated**, and both gates matter here:
  1. `state.uses_second_main` — *"deferring a cast to main 2 on a game with no main 2 deletes
     it"*. This is the same structural trap that made `GoblinsProvider`'s
     `DeferSacOutletPreCombat` delete Utopia Mycon, and it is why the Fungus provider dispatch
     sits above the goblin check.
  2. `ResolveProvider(state).ClassifiesMainPhases()` — **the provider must opt in.**

**Consequence: placement is a DECK-PROVIDER job.** Fungus currently routes to `GenericProvider`
and overrides nothing, so it has no opt-in and no override hook. That is the correct home for this
work anyway — the repo's core invariant is that only a deck/archetype provider may narrow the
search.

## 3. What is missing

* `GoldFishRunner::DeckUsesSecondMain` has no `devour` trigger, so Fungus is first-main-only.
* `ClassifyMainPhase` has **no rule for `devour`** (should be Main2) and **no rule for the quest
  params** (`quest_counter_per_attacker` / `quest_anthem_threshold`, should be Main1). The latter
  is a genuine gap: the attack-helping list enumerates `power_bonus`, `team_pump_cost`,
  `affects_all_creatures` and friends, and a quest anthem is none of those — so Beastmaster
  Ascension currently falls through to the default.
* There is no `FungusProvider`.

Both params are Fungus-only today (`devour` = Mycoloth, `quest_*` = Beastmaster Ascension), so
adding either rule leaves every other deck byte-identical.

## 3b. SEQUENCING WITHIN THE TURN — a second set of provider heuristics

**USER, 2026-09-17:** *"We definitely should make sure that saprolings have a chance to be produced
before we do this activation. (I guess these could be done in main 1?) Note that, order wise,
Doubling Season should be dropped before any activations and in main 1 so that Beastmaster counters
are doubled."*

Two distinct ordering constraints, both real, both currently unenforced:

**(a) Doubling Season FIRST, in main 1.** Both doublings are evaluated *at the moment of the
event*, so anything that resolves before the Season is simply not doubled:

* **tokens** — `CreateToken` consults `DoublerShift` when the token is made, so a spore activation
  taken before the Season yields 1 Saproling instead of 2;
* **quest counters** — and this is the one worth spelling out, because it is a whole turn of
  tempo. `ApplyAttackQuestCounters` fires one trigger **per declared attacker**, and each trigger
  is its own event for replacement purposes (CR 614), so `PutQuestCounters` doubles each
  separately. **VERIFIED in code 2026-09-17:** `PutQuestCounters` routes through
  `DoublerShift(..., for_tokens=false)`. With a Doubling Season out, Beastmaster Ascension needs
  **4 attackers, not 7** (4 x 2 = 8 >= the threshold of 7) — and the counters land in the
  declare-attackers step, so the +5/+5 applies to *that* combat.

  The mechanic is therefore already correct; what is missing is that nothing makes the search
  *prefer* to resolve the Season first. That is a **cast-order** heuristic (`CastOrderRank`), i.e.
  provider work, not an engine fix.

**(b) Produce Saprolings BEFORE the devour.** Mycoloth's value is `devour 2` x bodies, so the
spore -> Saproling activations want to happen first — every activation taken beforehand is two
more +1/+1 counters. The user's parenthetical is the natural resolution and fits the phase split
in §1: **activations in main 1** (a Saproling made in main 1 is summoning-sick and cannot attack
anyway, so nothing is lost by making it early), **Mycoloth in main 2**, eating both the
freshly-made tokens and the attackers that already connected.

So the full intended turn shape is:

```
main 1 :  Doubling Season  ->  Beastmaster Ascension / Sporecrown Thallid  ->  spore activations
combat :  attack (quest counters accrue, doubled)
main 2 :  Mycoloth, devouring the spent attackers + the new Saprolings
```

Every arrow there is a heuristic a `FungusProvider` would own: `CastOrderRank` for (a),
`MainPhaseOverride` for the split, and the activation placement for (b). None of it is an engine
change, which is the point — the invariant says narrowing and ordering belong in the provider.

## 3c. THE FULL TURN ORDERING (draft for review)

Assembled from the user's guidance plus what the card interactions force. Tags: **[U]** stated by
the user, **[D]** derived from the rules/card text, **[?]** open — needs measurement or a ruling.

### Upkeep — no decisions
Spore counters land on the five spore bodies; **Sporesower Thallid puts one on EACH Fungus you
control** (all eight Thallid-family cards are Fungus, including Mycoloth and Sporecrown — whose
counters are inert, they have no spore ability). Mycoloth creates one Saproling **per +1/+1
counter**. All are "at the beginning of your upkeep", and no ordering among them changes any
total, so there is nothing to search here. **[D]**

### Main 1 — in this order
1. **Land drop, and the Wild Growth line.** **[U]** Wild Growth is `Enchant land` — **ANY land,
   not just a Forest** (an earlier draft of this doc said Forest; that was wrong).

   **THE HOST CHOICE IS ABOUT BOUNCE EXPOSURE, NOT ABOUT MANA** — a second correction, because
   this doc previously said a bounceland was simply the *best* host (one tap for `{G}{U}` + `{G}`
   = three mana). The extra mana is real, but it is not the reason. **USER:** *"It's not that Wild
   Growth is best on a bounceland. It is that putting it on a bounceland can avoid it being
   bounced. If we have other things to bounce you can feel free to stick it on a Forest."*

   So the rule is conditional, not absolute. Every karoo we later play must return a land, and the
   one land we will never want to return is a karoo (returning it re-triggers the bounce and costs
   the tempo again), so an aura parked on a karoo is out of the line of fire. Park it on a Forest
   and the next karoo may have nothing else to return — and the aura goes with it. **Where the
   board already holds a spare land to return, the Forest is a perfectly good host.** What the
   engine needs is the *exposure* term, not a preferred host type.

   The user's line, which the engine must be able to express: *Wild Growth in hand, a Forest in
   play, a bounceland in hand* -> **float the {G} off the Forest first**, play the bounceland
   returning the now-spent Forest, then cast Wild Growth **on the bounceland**. The Forest's mana
   is banked before it leaves and the aura lands where the next karoo cannot reach it.

   **And the warning that came with it was a real defect.** *"You need to be careful of bounce
   lands when there are land enchantments out."* `BounceKarooLand` erased the land without the
   attachment-falls-off loop (CR 704.5m / 301.5c) that every other leaves-the-battlefield site
   runs; since per-copy `m_number` is stable, replaying that same physical card **re-attached the
   orphaned aura for free**. Traced on a constructed board, fixed, and pinned by
   `test/scenarios/karoo_bounce_drops_land_aura.json`. Both Fungus and EDF were exposed.

   Ordering consequence: **never bounce an enchanted land** when another is available — the
   provider's `BounceLandCandidates` currently prefers *tapped* lands, which is exactly the
   enchanted Forest you just tapped. That ranking needs an aura-aware term.
2. **Doubling Season — FIRST among casts, before ANY activation.** **[U]** Both doublings are
   evaluated at the moment of the event, so anything resolving earlier is simply not doubled:
   tokens (`CreateToken` -> `DoublerShift`) and counters (`PutQuestCounters` -> `DoublerShift`,
   verified in code).
3. **Beastmaster Ascension.** **[U]** Quest counters come from *declared attackers*, so it must
   be out before combat or the turn banks nothing. With a Season out it needs **4 attackers, not
   7** (4 x 2 = 8 >= 7), and the counters land in the declare-attackers step so the +5/+5 applies
   to THAT combat.
4. **Sporecrown Thallid.** **[U]** A lord only helps the combat it precedes. It pumps Fungus AND
   Saprolings — i.e. everything in the deck except Essence Warden (Elf Shaman).
5. **Remaining creature casts.** Summoning-sick this turn, but casting them now makes them devour
   fodder in main 2, and deferring buys nothing. **[D]**
6. **Spore -> Saproling activations.** **[U]** After the Season (else 1 token instead of 2), and
   before the devour so Mycoloth has bodies to eat. A Saproling made here cannot attack this turn,
   so nothing is lost by making it early — and it is still devour fodder.

### Combat
7. **Attack with every non-Defender**, including 0-power bodies. **[D]** Thallid Shell-Dweller
   (Defender) never attacks; **Utopia Mycon (0/2) SHOULD attack** whenever Beastmaster Ascension is
   on the battlefield — it adds 0 damage but is a declared attacker, so it is a quest counter (two
   with a Season), and a passive opponent makes the attack free. This is a genuine, easily-missed
   edge.

### Main 2
8. **Utopia Mycon sacrifices — only as far as needed to CAST Mycoloth.** `Sacrifice a Saproling:
   Add one mana of any color` is how the {3}{G}{G} gets paid on a board that is short a land.
   **[?]** Competes directly with step 9 for the same bodies.
9. **Mycoloth, devouring.** **[U]** Eats the spent attackers (they already dealt their damage —
   the Birthing Pod double-dip) plus any freshly-made Saprolings.
10. **Remaining sac outlets on leftovers** — Psychotrope Thallid (`{1}`, sac a Saproling: draw)
    and any further Utopia Mycon mana, spent on Saprolings that already attacked and were not
    devoured. **[D]**

### Devour victim ranking (within step 9)
Best first: **Tukatongue Thallid** (its death refunds a Saproling), then **Saproling tokens**,
then **Thallid Shell-Dweller** (0/5 Defender — can never attack, so it costs zero damage), then
other 1-cost bodies (Thallid, Utopia Mycon, Essence Warden). Among otherwise-equal spore bodies,
prefer the one holding **fewer spore counters** — eating a Thallid at 2 counters forfeits a
Saproling next upkeep. **[U]** for the candidate set, **[D]** for the ordering within it.

**Never devour:** Sporecrown Thallid (lord — shrinks the whole team), Sporesower Thallid (the
spore multiplier AND a 4/4 attacker), Psychotrope Thallid (the only card-draw outlet).

### Open
* **[?]** Step 8 vs step 9 is a real search decision (mana now vs permanent counters), not a
  ranking — it should stay searched rather than be frozen into the ordering.
* **[?]** Whether to hold a Saproling back from devour to keep Psychotrope draw live.
* **[?]** Whether attacking with Utopia Mycon is right when Beastmaster is NOT out (it does
  nothing then, and it taps a body that could be sacrificed — probably neutral, but it is free to
  measure).

## 4. The order to do it in — measurement first

**Do NOT add the `DeckUsesSecondMain` trigger first.** A searched second main roughly DOUBLES the
per-turn search cost, and this deck already has a cost tail. The repo ships the lever for exactly
this question:

1. **A/B `MTG_FORCE_USES_M2=0` vs `=1`** on Fungus, equal seeds, one pooled batch. This is the
   documented route for *"whether a non-whitelisted deck's skipped m2 ever has value"*. Note the
   phase FILTER stays off in this arm (Generic does not opt in), so it measures "does a second
   main help at all" without any placement change — the clean first question. Report avg win turn
   **and** wall.
2. **If the m2 arm wins:** add `p.devour > 0` to `DeckUsesSecondMain`, and add the two
   `ClassifyMainPhase` rules (devour -> Main2, quest -> Main1).
3. **If placement then needs to bind:** add a `FungusProvider` with `ClassifiesMainPhases()` true
   and a `MainPhaseOverride`. Measure again — the filter is a narrowing, so it needs its own arm.
4. Adoption follows `heuristic-optimization.md`: train seeds, held-out validation, report, adopt
   in the provider.

**If the m2 arm is neutral**, the whitelist is confirmed for this deck and the absence is a Stage
6a disclosure rather than a gap — which is a real result, not a non-result.

## 5. "Properly implement devour"

What is modelled correctly today (`ApplyDevourAsEnters` / `FireDeferredDevourDeaths`):

* the devour **count k IS a searched axis** (`Action::devour_count`, folded into the sim key and
  the dedup key);
* CR 702.81b **simultaneity** — victims chosen and removed in one step, so Tukatongue Thallid's
  replacement Saproling cannot be eaten by the same devour;
* **deferred death triggers**, fired after the devourer has entered, so the refunded Saproling
  arrives too late to be devoured;
* it cannot devour itself or a creature entering with it (it runs in the pre-push window);
* Doubling Season doubles the counters (CR 121.6 / 614.1c) but not the cost.

What "properly" would add:

* **Searched victim SELECTION.** Today `k` is searched but *which* k creatures is resolved by the
  shared `SacExpendabilityRank` heuristic. That ranking happens to be right for this deck
  (Tukatongue ranks expendable because its death refunds a Saproling), but it is a heuristic
  standing in for a decision, and on a mixed board the choice is real: eating a Thallid holding 2
  spore counters forfeits a future Saproling, eating a Sporecrown shrinks the team. **This is
  ledger Q2, still PROVISIONAL.**

  **USER, 2026-09-17, on the shape of it:** *"we should have some form of searched victim
  selection, but realistically we'll have a heuristic anyway. Anything that is 1-cost is a
  potential target along with thallid shell dweller."* So: a **searched axis over a
  heuristic-ranked candidate list**, not a full subset enumeration — the same shape the repo
  already uses for tutor targets (rank, then search the top few). The candidate set, checked
  against the actual decklist:

  | candidate | cost | P/T | why it is fodder |
  |---|---|---|---|
  | Saproling token | — | 1/1 | the bulk fodder; fungible |
  | Thallid | `{G}` | 1/1 | 1-cost |
  | Tukatongue Thallid | `{G}` | 1/1 | 1-cost, and its death **refunds** a Saproling |
  | Utopia Mycon | `{G}` | 0/2 | 1-cost; 0 power, never an attacker |
  | Essence Warden | `{G}` | 1/1 | 1-cost |
  | **Thallid Shell-Dweller** | `{1}{G}` | 0/5 | **Defender** — 2-cost but can NEVER attack, so feeding it costs no damage |

  NOT candidates: Sporecrown Thallid (a lord — eating it shrinks the whole team), Sporesower
  Thallid (4/4 beater and the spore engine), Psychotrope Thallid, and Mycoloth itself.

  Note the rule is not "cheap" but "**contributes no combat damage**": Shell-Dweller is the tell,
  and it is why `SacExpendabilityRank` alone is not the right ranking here — a 0/5 body reads as
  valuable on toughness while being worth exactly nothing to a goldfish.
* **`devour` as a real keyword.** The loader rejects `"Devour"` in a card's `keywords` array
  because the `Keyword` enum only carries keywords some code path reads; devour is fully modelled
  via the `devour` param. Recorded in `scryfall_divergences.json`. Cosmetic, but it is the kind of
  divergence that confuses the next reader.
* **The post-combat sequencing** in §1 — unrepresentable until the second main exists, and listed
  in the Stage 6a disclosure as **NOT inert**: a real under-rating of Mycoloth, bounded to one
  decision per copy per game.


---

## 6. MEASURED, 2026-09-23 -- the second main is worth a real turn fraction

§4 prescribed the order and it was followed exactly: force-lever A/B first, then the whitelist +
placement, then the cost gate. All arms pooled into ONE batch per round (per-job `flags`), fresh
held-out seeds (81001/82001/83001/84001), 3,360 games per round.

### 6a. Does a second main help at all? (§4 step 1)

Yes, on every cell. `MTG_FORCE_USES_M2=1`, no placement change:

| cell | base | forced m2 | delta | cost |
|---|---|---|---|---|
| d0 (400 g) | 6.1000 | 6.0525 | -0.048 | -- |
| d1/b3 (400 g) | 5.6100 | 5.5500 | **-0.060** | **4.25x** |
| d3/b10 (200 g) | 5.6650 | 5.6250 | -0.040 | 2.76x |
| d5/b20 (120 g) | 5.6250 | 5.5417 | **-0.083** | 1.90x |

§4's warning was right and then some -- it predicted "roughly DOUBLES"; d1/b3 measured 4.25x.

### 6b. Placement (§4 steps 2-3): `devour` on the whitelist + a FungusProvider classifier

Built as §1-§3 specified: `p.devour > 0` joins `pod_mv_delta`/`convoke` on `DeckUsesSecondMain`,
and `FungusProvider::MainPhaseOverride` sends devour to Main2 and pins **everything else** to Main1.

> **USER 2026-09-23:** *"Only Mycoloth should go in the second main in my understanding. Everything
> else can be skipped."* and *"doing mycoloth in the second main is important, because you want to
> attack with existing creatures and then sacrifice them."*

Pinning the rest Main1 is not just the user's rule, it is the cheaper AND better arm -- the base
template classifier would otherwise send this deck's summoning-sick 1/1 bodies to Main2 as well,
paying for a phase they have no use for. It also closes §3's noted gap for Beastmaster Ascension
(the quest params are in no attack-helping list, so the template would drop it to Main2 and waste
the turn): the Main1 pin covers it without needing a `ClassifyMainPhase` quest rule.

| cell | base | forced m2 | **Mycoloth-only** |
|---|---|---|---|
| d0 | 6.1000 | 6.0525 | **6.0525** |
| d1/b3 | 5.6100 | 5.5500 | **5.5475** |
| d3/b10 | 5.6650 | 5.6250 | **5.6100** |
| d5/b20 | 5.6250 | 5.5417 | **5.5417** |

Better than the blunt lever at d1/b3 and d3, equal elsewhere, and cheaper: d3 1.52x vs 2.66x.

### 6c. The cost gate -- solve m2 only when a Main2 cast is in hand

Fungus runs 2 Mycoloth in 60, so most turns defer nothing and the post-combat solve re-prices a
main 1 the search already did. `DecisionProvider::SecondMainNeedsDeferredCast`
(`MTG_FUNGUS_M2_GATE`) skips the phase on those turns.

It is deliberately NOT `SkipsUnproductiveSecondMain`: that gate asks *"did combat create
anything"*, which on a deck with no attack triggers is false EVERY turn -- so on Fungus it would
skip the second main always and DELETE the cast the phase was opened for. Same structural trap as
the `DeferSacOutletPreCombat` misroute in §2.

| cell | base | myco | **myco + gate** | gate cost |
|---|---|---|---|---|
| d0 | 6.1000 | 6.0525 | **6.0525** | -- |
| d1/b3 | 5.6100 | 5.5475 | **5.5500** | **2.45x** (from 3.95x) |
| d3/b10 | 5.6650 | 5.6100 | **5.6100** | **1.20x** (from 1.51x) |
| d5/b20 | 5.6250 | 5.5417 | **5.5500** | 1.74x |

Keeps essentially all the quality (-0.060 vs -0.063 at d1/b3, identical at d0/d3) for a third less
cost. What it gives up is a post-combat ACTIVATION on Mycoloth-less turns -- step 10 of the §3c
ordering, the leftover Psychotrope draw / Utopia Mycon mana. That is a real loss, which is why it
carries its own arm.

### 6d. WHAT ADOPTION COSTS -- read this before deciding

Everything above is default OFF and byte-identical (scenarios 103/103, smoke 93/93, 0 configs
changed). Turning it on is a PLAY-LOGIC change, and the artifacts cascade:

1. **The value leaf is invalidated first.** `Fungus.value.json` was fitted to single-main play, and
   it is UPSTREAM -- the mulligan generator reads `mull_gen_depth`/`mull_gen_budget_ms` from it.
   Value-leaf regeneration is hours and must run alone.
2. **Then the mulligan table**, which at 2.45x in the d1/b3 gen regime turns the measured 4.7 h
   `fast` projection into roughly 11-12 h. That is past the overnight window the `fast` recipe was
   chosen to fit.

So the honest trade is: **-0.06 avg win turn against a multi-stage regeneration**, versus shipping
a mulligan table now that is fitted to play we already know is ~0.06 turns worse. Still to do
before adoption either way, per §4 step 4: held-out validation on a second seed block.

### 6e. Still open from §3c (unbuilt)

The phase split is only the first line of the turn ordering. Unbuilt: `CastOrderRank` for
Doubling-Season-before-everything (§3c step 2), the spore-activation placement (step 6), attacking
with Utopia Mycon when Beastmaster is out (step 7), and the §3c step 8-vs-9 search question. Also
note the devour victim ruling in §3c/§5 (*"Never devour: Sporecrown, Sporesower, Psychotrope"*) was
SUPERSEDED on 2026-09-23 -- see `fungus-token-search-cost.md` Round 10: Sporesower/Sporecrown are
now both SEARCHED (measured lossless; exempting them costs a turn on 8 of 480 games) and Psychotrope
is the rung with the strongest claim rather than an exemption.
