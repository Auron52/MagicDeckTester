# Exemption-free condemnation: fix the ORDER, not the rule

> ## STATUS 2026-08-30 (LATER): ARC EXECUTED — RANGES BUILT, SOUNDNESS PROVEN, AND THE ANSWER
> ON HINATA IS "SOUND CONDEMNATION IS INERT". AWAITS USER REVIEW of the range table (below).
>
> USER (2026-08-30, the range directive, verbatim): *"Let's continue looking into if condemnation
> is possible. Either way, I don't want arbitrary exemptions. They should be handled within the
> order instead. (so you would have a potential range of positions for a few specific spells, but
> most would have one spot in the order) That way, I can review any changes you make to the order."*
>
> **Corrections to the plan below (the flag inventory was stale):** the four TYPE exemptions
> (mana source, ritual/Treasure, tutor, cost reducer — bugs 4-6) were ALREADY DELETED on
> 2026-08-28 (`TurnSolver.cpp` BpSlotIsAfterSite: "NO TYPE EXEMPTIONS"), replaced by the
> card-agnostic `BpTurnManaSettled` (LAND_SETTLED, sources-count growth re-admits) and the
> order-aware comparison. What remains are DEFINITIONAL gates (searched-only decision space,
> plan-casts-it, drawn-card-is-new, copies-fungible dominance, payability) plus one SITE rule
> (`MTG_BP_CONDEMN_MANA_SITE_EXEMPT`, bug 7 — measured **inert on Hinata**: manasite=0 of 11.7M
> consultations). So the nodecond measurement was already the exemption-free filter.
>
> **What this session found (all on the node arm, ORDER_FULL + NO_GREEDY_CONT + BP_NODE):**
> 1. **The exemption-free filter still deletes reachable wins.** Of nodecond's 19 regressions
>    vs node (1200x2), 14 are churn but **5 survive 100x budget** (hold gi1124/1132/1178, train
>    gi53/1035): the condemned-and-needed cards are the ENGINE TIER at CANTRIP sites (Reality
>    Spasm/Sol Ring/Hinata/Gamble @ Ponder/Preordain, plus Ponder/Preordain @ Soulfire). The
>    winning lines interleave everything across breakpoints — hold gi1132's T4 win is
>    `Spasm, Soulfire, Spasm, Ponder, Sol Ring, Preordain, Irencrag, Crackle` — because every
>    draw re-prices the chain (Spasm float, staged Soulfire casts, her per-target discount).
> 2. **The RANGE mechanism (built, this commit):** `DecisionProvider::CastOrderRankLatest`
>    (default = nominal ⇒ byte-identical), consumed ONLY by `BpSlotIsAfterSite` — a card is
>    condemned only when even its LATEST position precedes the site. Hinata's table under
>    `MTG_HINATA_RANGE` (default OFF): tiers 4-14 (rocks, Hinata, Spasm, Gamble, Ponder/
>    Preordain, EI, Ornithopter) extend to 21; Soulfire/Opus/Irencrag/Crackle keep single spots.
>    All 5 deleted-line games RECOVER to node's result at play settings.
> 3. **The verdict: sound condemnation fires ZERO times.** 300-game probe (whynot histogram):
>    bp_condemn_seen=11.42M, **drops=0**; units 21.53M ≈ node's 21.70M. Every one of the unsound
>    rule's 623k drops (5.3%) was a claim the real win lines falsify.
> 4. **And even unsound, it was never a work lever here:** node children 6.90M vs nodecond
>    6.34M, but child DUPES fell 3.37M→2.86M — **91% of what condemnation removed, the node's
>    state dedup was already killing** at one cheap apply each; non-dupe children fell only
>    1.4%, units only ~2%. The whynot CEILING caps a perfect order at ~2.3x drops. Condemnation
>    cannot close the class's ~+0.03 equal-compute gap.
>
> **Recommendation (awaiting USER):** do not carry BP_CLASSIFY on Hinata — on this deck the
> node's dedup IS the sound version of the same idea (duplicate detection by state identity
> instead of by declared order, and Hinata's chains falsify any order narrow enough to bite).
> The range mechanism stays as the reviewable-order semantics; its live target is decks with
> genuinely fixed positions (Mirrorwing's bug-8 games — bodies/magnets at trick sites — are the
> same shape and could keep condemnation biting there WITH ranges instead of the tail exemption).
> Doubts flagged in the table: Izzet Signet and EI extrapolated from tier-mates (no direct
> sighting); Ornithopter's ruling said "before Soulfire" but a post-Soulfire one still targets
> Crackle, so its range errs wide pending review.

**Status of the section below: the original plan, kept for the reference numbers. Steps 1-4
executed 2026-08-30 (see STATUS above); step 5 answered negatively (units do not shrink).**

USER (2026-08-30, verbatim): *"That makes no sense that there is nothing to condemn. If done
correctly across breakpoints we should be able to condemn a lot of things. We may need to
separate some draw spells, though."* And: *"We don't want 'soundness exemptions' at all. We want
to fix the order so it supports the few things we do need implicitly."*

## The reframe

The condemnation bugs 4–6 (`breakpoint-condemnation-status.md`) were each fixed with a CLASS
exemption — rituals (bug 4), tutors (bug 5), cost reducers (bug 6, `MTG_BP_CONDEMN_REDUCER_EXEMPT`)
— and the sum of exemptions made condemnation nearly inert on Hinata (measured 2026-08-30 at
n=1200x2 on top of MTG_BP_NODE: 5.35% drop rate, −1.0% units, quality +0.0000 train / −0.0075
hold vs ng). The USER's diagnosis: the exemptions are patches over an order that MIS-RANKS the
exempted classes. Bug 6's deleted line ("Ponder finds the land that makes Hinata castable") was
caused by Hinata-the-creature ranking EARLIER than the cantrips — so casting Ponder read as
"passed on Hinata" and condemned her. The exemption route kept the bad ranking and neutered the
rule; the ORDER route re-ranks so the rule is sound with no carve-outs:

* **Information first.** Cantrips/draw spells rank before the decisions they inform. A prefix
  that casts only cantrips then passes nothing earlier — nothing is condemned, and the
  post-draw continuation is fully open. (The node partitions at the first plain cantrip, so
  this is exactly the pend-heavy case.)
* **Enablers implicitly supported by adjacency.** A ritual ranks immediately before its payoff;
  a cost reducer (Hinata) ranks before the spells her discount enables. Then "cast X while
  passing an earlier Y" genuinely means Y was declined for this turn — condemnation is sound
  because the order already encodes the dependency the exemption was protecting.
* **"We may need to separate some draw spells"** — the plain cantrips may need distinct ranks
  (not one shared class rank), both because the node partitions at the FIRST one in the order
  and because not every draw spell is pure information (an expensive draw spell can be a
  payoff-turn competitor).

## Why the old "order fix is worse" verdict does NOT bind

`breakpoint-condemnation-status.md` records the order-side fix (find-promotion-only order,
cantrips ahead of the engine creature) as measured WORSE (+0.0113/+0.0120) — but that was
PRE-NODE, when the order also fed the greedy/canonical continuation picker (`cands[0]` under
MTG_BP_NO_GREEDY_CONT) and the wave machinery. Under MTG_BP_NODE the continuation is SEARCHED
(full list + empty arm, drawn card visible), so the order's role shifts from "picks the
continuation" to "defines what condemnation may prune." The prize is also different now: with
exemption-free condemnation biting across breakpoints, every continuation at every link offers
only (cards later in the order than the last cast) + (the drawn card) — the USER's "phases +
fully ordered condemnation should be a relatively cheap way to search the turn," with the
recorded caveat that greedy continuations may still be cheaper (they skip search entirely; the
target is the same ballpark, then quality decides). Today's condemnation bites 5.35%; this
design should bite far harder and shrink the node's child fan-out structurally (fs_bp_node was
30–39% of units; child dupes 49–56%).

## Where everything lives (found 2026-08-30, unverified beyond grep)

* The full order: `HinataProvider` — `MTG_HINATA_ORDER_FULL` gate at
  `src/ai/DecisionProviders.cpp:4664` (USER-reviewed 2026-08-26), `CastOrderRank` ~4918,
  land-drop rank 0 (`LandDropCastOrderRank`, land-first doctrine).
* Exemption flags found so far (audit for completeness before designing):
  `MTG_BP_CONDEMN_REDUCER_EXEMPT` (bug 6, default ON), the ritual/tutor exemptions (bugs 4–5,
  see `breakpoint-condemnation-status.md`), `MTG_BP_CONDEMN_MANA_SITE_EXEMPT` (bug 7, default
  ON), `MTG_CONDEMN_PASS_EXEMPT` (default on), `MTG_CONDEMN_ENABLER_EXEMPT` (default on), and
  `MTG_BP_CONDEMN_TAIL_EXEMPT` (default OFF — its comment already says "USER: fix the ORDER").
  NOTE which of these are CLASS exemptions (the USER's target: delete) vs CORRECTNESS filters
  that stay (affordable-at-the-time, drawn-card-is-new, free-cast — those are part of the
  rule's definition, not patches).
* The condemnation core: `BpCardWasInHandBefore` / the pre-draw hand snapshot
  (`CantripOrderScope`), `BpClassifyActive` (`MTG_BP_CLASSIFY` force lever),
  `CondemnsConsideredAtBreakpoint` provider hook.

## The plan (next session)

1. Audit every exemption: classify CLASS-patch (to delete) vs DEFINITIONAL filter (to keep).
2. Design Hinata's exemption-free order (information-first; separated draw-spell ranks;
   ritual-before-payoff adjacency; reducer-before-enabled adjacency). Cast order is
   **USER-REVIEWED per deck** — propose with doubts flagged, get sign-off before adoption.
3. Wire the arm: node + MTG_BP_CLASSIFY + new order variant (heurarm slot) + class exemptions
   OFF. Byte-identical with the variant unset.
4. Measure (paired 1200x2 vs ng and node, the `logs/bp_node/ab_logs` pattern): quality, units,
   drop rate, child fan-out. The recorded reference points: node +0.0142/0.0000 at 20 ms
   (funded by +35% overshoot; WORSE at equal units: +0.024/+0.038), nodecond +0.0000 exactly.
5. If the order + condemnation shrinks units enough, re-run the equal-compute point — the
   class's standing failure is that it loses ~+0.03 at true equal compute; aggressive sound
   condemnation is the first lever that could plausibly close that gap structurally.

## Reference numbers to beat (all 2026-08-30, paired 1200x2, d5/20ms unless noted)

| arm | hold | train | units (300g hold) |
|---|---|---|---|
| ng (control) | 5.6658 | 5.7033 | 16.08M |
| node | 5.6517 | 5.7033 | 21.70M (id_depth 2.79) |
| nodecond (exemption-laden) | 5.6583 | 5.7033 | ~21.5M, drops 5.35% |
| node@12ms (equal-units) | 5.6900 | 5.7417 | 16.39M (id_depth 2.52) |
