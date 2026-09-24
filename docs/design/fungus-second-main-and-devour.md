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

## 7. THE 2.45x, AND WHY IT WAS NEVER THE BRANCHING (2026-09-23, second pass)

§6d priced adoption at **2.45x** in the d1/b3 generation regime and treated that as the price of the
phase. The USER rejected the premise outright:

> *"Wait, why is the cost 2.45x? That is a red flag. It shouldn't be that big a multiple just from
> moving it to second main when we prevent everything else from running there. The cost should be ~
> that of the original if this is done correctly. Since we are disallowing main 1 mycoloth and
> enabling main 2 mycoloth and changing nothing else."* — and later — *"Since we are not doing any
> additional branching and all cards are only allowed to be played in one phase."*

The premise was right and the implementation did not honour it. Three separate defects were found.
The lever now costs **1.47x**, and what remains is structural rather than waste.

### 7a. FIRST, THE MEASUREMENT WAS WRONG

**Per-job `ms` from a pooled `--batch` is not a cost metric.** Two runs of the byte-identical arm
(digest `aff67f86425a7556`) reported 191,958 ms and 237,406 ms — a 24% spread — purely because an
8-job pool and a 9-job pool leave the box under different load as arms retire. Every "1.2x / 1.7x /
2.45x" in §6 is drawn from that column and none of it is reliable; the d3/d5 ratios there are
noise-dominated. This is the standing wall-A/B lesson (aggregate right, per-item signs wrong) and it
was ignored because the pooled run was cheap.

**What replaced it:** `/usr/bin/time %U` over a single-threaded 150-game run at d1/b3, arms
interleaved, 2-3 reps. Reproducible to **±0.8%**, which is what makes the decomposition below
readable at all.

### 7b. DEFECT 1 — the phase opened for a card that could not be cast

The deferred-cast gate asked *"is a Main2-classified card in HAND"*. Mycoloth is in the opening hand
about a third of the time and costs five; the gate therefore opened the phase from turn 1.
`MTG_M2_YIELD_STATS` over 60 games: **181,528 interior m2 solves, 80,738 of them (44.5%) returning
an EMPTY plan**.

Fixed by requiring the deferred cast to be **payable** — `PaymentManaCovers`, whose `false` is a
proof of unpayability over every tap ordering, so the skip can only ever drop a solve that had
nothing to find. Result: **181,528 → 22,097 solves, 44.5% → 0.0% empty.**

**And it bought ~5% of wall.** The solves were never where the time was. Recording this because the
count was so persuasive: an 8.2x reduction in the thing being counted moved the thing being paid for
by almost nothing, and two further rounds were spent before that was believed.

### 7c. DEFECT 2 — main 2 re-offered the whole hand

The split shipped as **half a split**. `ClassifiesMainPhases` filters the PRE-combat enumeration, and
by deliberate design (`MainPhaseOverride`'s contract: a Main2 class must never be able to DELETE a
line) the post-combat enumeration was never filtered at all. So main 1 dropped one card and main 2
went on offering every other one — every card in hand branched **twice per turn**. That is precisely
the duplication the USER said was not supposed to exist.

Fixed with a post-combat half of the same filter, scoped to `SecondMainNeedsDeferredCast` (a
provider asserting its second main exists for the deferred cast alone). **Only casts are dropped** —
the predicate tests `Action::Kind::CastFromHand`, so activations and the land drop survive, which is
what keeps §3c step 10 (the leftover Psychotrope draw, Utopia Mycon mana on a Saproling that has
already attacked). Measured after: **1.23 m2 plans per m2 decision** — the fan-out is now minimal.

### 7d. DEFECT 3 — the cost was at the HORIZON EDGE, not in the solves

With both of the above in, the lever still cost 1.85x. The decomposition that found it:

| arm | what is on | d1/b3 CPU (150 games) | ratio |
|---|---|---|---|
| base | single main | 20.0 s | 1.00x |
| `struct` | second main exists; NOTHING deferred; every interior m2 solve skipped | 33.9 s | **1.70x** |
| `gate` | full lever, before the edge gate | 37.1 s | 1.85x |

**1.70x with the phase doing nothing at all.** The only site left is `FSLineTail`'s `second_main`
branch — the forward-search tail, where 99.7% of this deck's work sits — which fans EVERY m1 plan
out over the m2 enumeration at every horizon-edge node, each plan costing a `GameState` copy plus an
apply. On a 20-40 wide Saproling board those are the most expensive operations the engine has, and
`units` undercounts them badly: `units.fs_main2` reads **3-5%** while carrying most of the delta.
(That mis-read is why the search went to the interior solves first.)

Fixed by applying the same deferred-cast gate at that site. Same scope rule as 7c, so
`SkipsUnproductiveSecondMain` decks — KittyEquipment — are untouched: that hook's adoption
measurement never included this site.

**Two things that were measured and were NOT the cause**, recorded so they are not re-tried: the
per-turn empty-plan `ApplyPlanDirect` in the rollout (skipping it changed the wall by 0.0%), and the
main-phase classifier's board walks. The latter were still worth fixing on their own — see 7f.

### 7e. WHERE IT LANDED

Cost, single-threaded d1/b3, interleaved, 2 reps:

| | base | **adopted** | root-turn-only |
|---|---|---|---|
| CPU | 20.05 s | **29.39 s (1.47x)** | 21.4 s (1.07x) |

Quality, 6,720 games, train and HELD-OUT seed blocks (lower is better):

| cell | base tr | **adopted** tr | base **ho** | **adopted ho** | root ho |
|---|---|---|---|---|---|
| d0        | 6.1000 | **6.0525** | 6.1725 | **6.1000** | 6.1000 |
| d1/b3     | 5.6100 | **5.5950** | 5.6825 | **5.6600** | 5.6800 |
| d3/b10    | 5.6650 | **5.6600** | 5.5850 | 5.5900 | 5.5850 |
| d5/b20    | 5.6250 | **5.6000** | 5.6000 | **5.5917** | 5.6000 |

Better on **7 of 8 cells**; the one exception (d3/b10 held-out, +0.005) is a single game. ADOPTED
default ON: `MTG_FUNGUS_M2_DEVOUR`, `MTG_FUNGUS_M2_GATE`.

`MTG_FUNGUS_M2_ROOT` — defer only at real decision turns, so a projected future turn keeps Mycoloth
in main 1 — is BUILT and **default OFF**: it is nearly free (1.07x) but gives up essentially the
whole gain at every searched depth (d1/b3 held-out 5.6800 vs base 5.6825), so it buys nothing. It
stays as the instrument that isolates 7d, and it is the arm to reach for if the 1.47x ever has to
come down. Note AL measured the same hook as a quality REJECTION for its own reasons.

**The residual 1.47x is the phase itself.** The branching is now provably minimal (1.23 plans per m2
decision; main 1 and main 2 offer disjoint cast sets), so what is left is the engine executing one
more main phase per simulated turn on its most board-wide deck. It is not waste, and there is no
further factor-of-two hiding in it.

### 7f. A GENERAL FIX THAT FELL OUT

`CountProwessAttackers` tested `CanAttackFull` (which re-reads the whole battlefield for lord/static
effects) and the provider's `AttackWith` BEFORE the prowess keyword — making it O(n^2) in board
width, on every enumeration, for a deck with no prowess card at all. The `&&` is now keyword-first;
same conjunction, same answers. Likewise the main-phase filter's two classifier inputs
(`HasteAccessThisTurn`, `BoardHasScalingAttacker`) are computed LAZILY: `ClassifyMainPhase` consults
`MainPhaseOverride` first, so a provider that classifies its whole deck per card never reads either,
while the old eager form paid both battlefield walks at every node. Both are byte-identical
everywhere and help any token-wide deck that turns the filter on.

---

## 8. THE COST ATTRIBUTION IN §7d WAS WRONG (2026-09-23, USER-PROMPTED)

The USER read §7d and asked the obvious question: *"Do we really need to do the GameState copy like
this in general? It seems insane that having that adds so much cost. Do we do the same copy at every
breakpoint and for main 1 as well?"* The answer to the second half is **yes** — `LoadPlanState` is
the universal unit at the main-1 plan loop, both breakpoint-node child loops, the m2 loop and its
condemned tranche, the rollout leaves, and the payability probes. The answer to the first half is
that **the copy was never the cost**, and §7d's claim that it was does not survive measurement.

### 8a. The direct refutation

A `MTG_M2_EMPTY_FAST` path was built so a PROVEN-EMPTY second main costs the recursion and nothing
else — no `GameState` copy, no apply, no dedup key, no plan loop. It is byte-identical by digest at
d0/d1/d3/d5, and it fires on **98.3%** of second-main scans (6,126 of 6,232 in 30 games).

It bought **0.3% of wall.**

Two further exact figures kill the rest of the §7d story. Over 150 games the two arms differ by
**+1.9% of search units** (229,051 -> 233,473) against **+48% of wall**; and all 24,236 second-main
solves together take **12.06 s of a 31 s run** at 498 us each, while `GameState`'s copy constructor
does not reach 1% of a perf profile. The phase was not paying for copies, applies or nodes.

### 8b. WHAT THE COST ACTUALLY IS: 2.2x AS MANY SAPROLINGS

Deterministic counters (`MTG_TOKEN_STATS`), 150 games, d1/b3:

| | base (Mycoloth in main 1) | second main | ratio |
|---|---|---|---|
| CPU | 20.44 s | 31.06 s | 1.52x |
| rollout wall | 19.80 s | 30.36 s | 1.53x |
| **tokens created** | 5,911,746 | **13,138,123** | **2.22x** |
| ETB enters | 6,783,841 | 14,083,034 | 2.08x |
| **permanents walked by the enter cascade** | **1.19 billion** | **2.89 billion** | **2.42x** |
| mean board width at an enter | 202 | 220 | 1.09x |

Devouring AFTER combat eats the attackers, so Mycoloth is bigger, so it makes **more than twice as
many Saprolings**. That is the feature working, and it is most of the 1.4-1.5x. The USER's second
invariant ("when it is available it should add almost no cost") therefore cannot mean "a bigger
Mycoloth is free"; what it can mean is that **per-token work should be O(1), not O(board)**.

### 8c. MEASURE THE TAIL, NOT THE MEAN -- the methodology trap that cost this session hours

The same two arms on a **30-game** subset of the same seed run **1.91 s vs 2.00 s (1.05x)**. On
**150** games they run **1.48x**. Every counter comparison made on the 30-game set pointed the wrong
way: it showed the second-main arm creating FEWER tokens, doing FEWER board visits and FEWER solves,
which is true of that subset and false of the workload. The cost of this feature lives entirely in a
handful of wide-board games -- the same Doubling Season tail as
`fungus-doubling-season-rollout-tail.md`. **Size a Fungus cost experiment by whether it contains the
tail; a "representative" small sample of this deck is not representative of its cost.**

### 8d. REJECTED BY THE USER: deferring only in the decision space

The obvious way to delete the rollout's per-turn second main is to stand the phase filter down
inside a playout (`g_rollout_nest > 0`), so a rollout casts the deferred card in main 1 as the
single-main engine does. It was built and measured at **1.07x**. The USER rejected it outright:

> *"I don't want to cast it in main 1 in either situation. That makes no sense whatsoever. The
> purpose of casting it second main is to have the attack phase in-between."*

That is right, and it generalises: a rollout that devours before the Saprolings swing does not score
the line the search is choosing, it scores its opposite. The rollout's second main must stay a
second main. Only its PRICE is negotiable. The code carries this note so it is not re-derived; the
same objection applies to `MTG_FUNGUS_M2_ROOT` (§7e) wherever it would reach a rollout.

### 8e. WHAT WAS ADOPTED, AND WHAT IT IS WORTH

All three are **byte-identical by digest** (base `d30c843184162462`, second main `9782c501ef6edee6`
at d1/b3, and unchanged at d0/d3/d5):

1. **`MTG_M2_EMPTY_FAST`** (default ON) -- a proven-empty second main costs the recursion only. The
   USER's first invariant, in the strict per-node sense. Worth 0.3%; adopted because the invariant
   is right even where the wall is not, and because it makes the phase's cost honest.
2. **`MTG_M2_SKIP_EMPTY_APPLY`** (default ON) -- the rollout no longer runs an `ApplyPlanDirect`
   with no action, no land and no breakpoint on every simulated turn. Worth **-3.1%** on its own.
   An earlier note recorded this site as "0.0%, reverted"; that reading came from per-job `ms` in a
   pooled `--batch`, which swings 24% on a byte-identical arm and never had the resolution to see
   it. **Do not re-cite the old number.**
3. **`Permanent::def_absent`** -- the deck-wide one. A permanent whose name is not in the card DB
   (every token: they are named "1/1 Saproling Token" and no such card exists) carries a bool
   saying so, and the hot board walks test it instead of calling `LookupCached`. Applied to
   `FireCreatureEnterWatchers`, `DoublerShift`, `LiveSacPayOutlet`, `SacPayFodderCount` and
   `UntappedManaUpperBound` -- between them billions of visits per run. Worth **-4.7%** on the
   second-main arm and **-2.6%** on base, and it helps EVERY token deck, not just this one.
   It defaults FALSE ("not known, do the lookup") so a creation site that forgets to set it costs a
   lookup rather than dropping a trigger.

4. **Namespace-scope flag reads.** The diagnostic counters added here are consulted from
   `CreateTokenOnce` and `FireEtbWatchers` -- tens of millions of calls -- and the Meyers
   function-local-static form emits a guard-variable acquire load on EVERY call. Moved to an inline
   namespace-scope variable, the same fix `CardDatabase::Instance()` already documents at ~6%.
   Worth a further **-2.7%** here, and a reminder that a "free" `static const bool` on a hot path
   is not free.

Net: base **20.26 s -> 19.74 s** (-2.6%), second main **29.76 s -> 27.87 s** (-6.4%), so the phase
costs **1.41x** instead of 1.48x. Verified by `test/scenarios.sh` 103/103, `regression.sh --smoke`
93/93 with **0 configs changed**, and `--regression` 129/129 with **0 configs changed and 0 play
changes**; reference reproducibility unchanged (0 play-drift, the same 1 board-diverged / 10
mull-drift). No ground-truth re-accept was needed, which is the point of byte-identity.

### 8f. THE REAL PRIZE IS STILL ON THE TABLE

`def_absent` removes the CALL from the hot walks; it does not remove the WALK. `FireCreatureEnterWatchers`
is still O(board) per enter, and Fungus contains exactly **one** enter-watcher (Essence Warden) on
boards of 200+ permanents -- 2.9 billion permanent inspections to find one card, 14 million times.
Maintaining the watcher set incrementally (or an index of "permanents that can ever be watchers")
turns that into ~14 million. A diagnostic bound is in place: `MTG_NO_ENTER_WATCHERS=1` fires nothing
at all, and PLAY IS UNCHANGED under it (avg 5.6067 either way, all four arms) -- so the whole walk is
addressable without touching a decision. It is not free to build: invalidation has to cover every
battlefield mutation, and the safe direction (err towards scanning) must be preserved.

---

## 9. THE 1.41x WAS MEASURED ON A DECK THAT NO LONGER EXISTS (2026-09-24)

USER, on being shown the 1.41x: *"Wait, so the second main is still 1.41x the price? If so we need
to fix that."*

It is not. **It is 1.00x on the deck we ship.** The 1.41x in section 8 is a correct measurement of a
deck that stopped existing about four hours after it was taken.

### 9a. The measurement

One binary (HEAD `a0877f59`, i.e. the SAME build that produced the 1.41x), one manifest shape
(`logs/m2fast/t_d1b3.json`, 150 games, d1/b3, `--threads 1`, `/usr/bin/time %U`), arms interleaved,
3 reps. The only variable is which sidecars sit next to the decklist. `logs/m2fast/nokeep/` is a
sibling-free copy of `decks/Fungus/` -- the same `.cod`, the same `.profile.json`, the same
`.value.json`, and **no `.keepmodel.exhaustive.*`** -- so it reproduces the deck exactly as it stood
when section 8 was written.

| deck artifacts | base (single main) | second main | ratio | avg win turn |
|---|---|---|---|---|
| **without** the keep table (= the section-8 deck) | 19.29 s | 27.72 s | **1.44x** | 5.6067 |
| **with** the adopted keep table (= what we ship) |  2.79 s |  2.76 s | **0.99x** | 5.4533 / 5.4333 |

The no-keep row reproduces section 8's numbers to within a percent, including its avg of 5.6067 --
which is what makes this a controlled comparison rather than two unrelated runs.

### 9b. Why adopting a MULLIGAN table deleted a SEARCH cost

Because Fungus's cost was never spread across its games. `fungus-suite-entry-and-cost.md` recorded
it years-of-sessions ago: **half the deck's cost is 6 games.** Those are the hands that cannot
deploy, so the game runs long, so Mycoloth devours a wide board, so the board reaches 200+
permanents and every subsequent enter pays O(board). The second main's marginal cost rode on exactly
those games and only those -- it is 2.2x the Saprolings, but 2.2x of a number that is only large in
the disaster tail.

The adopted keep table refuses those hands. The user had already seen it from the other side:
*"our Psychotrope kept showing up in the slowest hands."* Removing the hands removed the boards,
and removing the boards removed both the base cost (7x) and the second main's marginal cost
(to nothing). The second main is now, if anything, marginally FASTER than single-main (2.76 vs
2.79 s) while also being better play (5.4333 vs 5.4533).

**This is the general lesson and it is worth more than the number: a perf ratio is scoped to the
ARTIFACTS the deck was carrying when it was taken, not just to the commit.** The freeze discipline
in this repo covers `HEAD:src`; it does not cover the sidecars, and a keep table or a value leaf can
move a cost ratio by more than any engine change in this document.

### 9c. What was still worth fixing, and was

The enter cascade is real regardless -- it is what makes the tail quadratic, and the tail is still
there on any deck without a keep table, in deep rollouts, and on every other token deck. Re-reading
it turned up three defects, all of the same family as the 2026-09-19 pair and all fixed
**byte-identically** (digests `d30c8431...` / `9782c501...` unchanged on both arms of the heavy
workload):

1. **`CardDefinition::enter_watcher`** -- a derived per-definition bool, computed in
   `RebuildInternedIndex`. The watcher loop probed **six** separate `CardParams` fields, hundreds of
   bytes apart in a very large struct, for every non-token permanent on every enter, to reach the
   answer "no". That is ~5 cache lines per permanent per enter; it is now one byte adjacent to
   `params`. Kept in lockstep with the loop by the shared `DefHasCreatureEnterWatcher()`.
2. **`GameState::deck_has_self_bounce_etb`** -- the 2026-09-19 audit's inventory of "watcher scans
   already gated on the entrant" was INCOMPLETE. It missed the self-bounce scan (Breaching
   Dragonstorm clause 2), which has no entrant gate of any kind, does not even take the
   `def_absent` short-circuit, and therefore ran a full battlefield walk with a `LookupCached` per
   permanent for EVERY permanent entering on EVERY deck in the repo. A Saproling board paid ~200
   lookups per token created.
3. **`DoublerShift`'s guard was DEAD.** It tested `CardDatabase::HasTokenDoubler()` and its comment
   claimed "the MaxHandSizeAnthemMax idiom". It is not that idiom -- `MaxHandSizeAnthemMax` compares
   a DB constant to a LIVE GAME VALUE and can be false, while `HasTokenDoubler` is a bare predicate
   over all 387 cards in `cards.json` and is unconditionally true. This is precisely the trap
   `GameState.h`'s own presence-gate block documents, reintroduced one function away from it. So
   DoublerShift walked the battlefield on every token-creation event on every deck. Now stamped per
   game (`deck_has_token_doubler` / `deck_has_counter_doubler`). **Fungus is unaffected** -- it
   really does play Doubling Season, and it is the ONLY deck in the repo that plays any doubler, so
   every other deck was walking the board to discover it owns none.

   **But do not claim a win for this one.** Measured on Goblins (80 games, d3/b10, the repo's other
   heavy token deck), three binaries interleaved, 2 reps: 2.665 s -> 2.640 s, i.e. **-0.9%**, which
   at 2 reps is inside the noise. The reason is the same one section 9b is about -- Goblins' games
   end on turn 3.8, so its boards never get wide enough for an O(board) walk to cost anything. The
   gate is kept because it is free, byte-identical (digest `d159fa66...` on all three binaries) and
   repairs a guard that `GameState.h` already documents as a trap, NOT because it was measured to
   pay. The honest summary is that fixes 1-3 are worth ~13% where boards are wide and ~1% where
   they are not, and no shipped deck currently has wide boards.

Measured on the heavy (`nokeep`) workload, where this cost is visible at all -- 4 arms interleaved,
2 reps, same `%U` protocol:

| arm | before | after | delta |
|---|---|---|---|
| base (single main) | 19.29 s | 17.86 s | **-7.4%** |
| second main | 27.72 s | 24.15 s | **-12.9%** |
| ratio | 1.437x | **1.352x** | |

Larger on the second-main arm exactly as predicted: more tokens, more enters, more walk. On the
SHIPPED deck the same change is worth about -1.8%, because the shipped deck no longer builds the
boards that make it matter.

### 9d. Section 8f is hereby CLOSED, and deliberately NOT built

8f proposed maintaining the watcher set incrementally to turn 2.9 billion permanent inspections into
~14 million. **Do not build it.** Its payoff was computed against the 200-permanent boards of the
pre-keep-table deck; on the deck we ship those boards do not occur, and the three fixes above
already took the per-visit cost down to a single byte. What remains is an array stride, which is
near the floor for a scan.

The cost it WOULD carry is not near the floor: a sound index has to cover 45 distinct
`battlefield.push_back/insert/emplace` sites across 10 files, and the failure direction is a
**dropped trigger** -- the opposite of the safe direction every presence gate in this file was
careful to preserve (`default true == do the scan == old behaviour`). That is a bad trade for a
few percent of a workload we no longer run.

If it is ever reopened -- a new token deck without a keep table, say -- the design that survived
review is a `uint64_t` OR-reduction of per-definition property bits held on `GameState`, sound as an
UPPER BOUND (stale-high is harmless because the walk is authoritative; only a missed INSERT is
unsafe), defaulting to all-ones so an unstamped state scans, with a `MTG_VALIDATE_BF_MASK` debug
mode that recomputes from scratch at every consumer and aborts on a missing bit.
