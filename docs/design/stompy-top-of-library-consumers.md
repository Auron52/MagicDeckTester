# Stompy: top-of-library consumers vs library-writing tutors (deferred design)

**Status (updated 2026-09-15):** Item 1 IS BUILT — `MTG_TOP_RESOLVE` (aa1aabef, 2026-08-21),
default OFF pending its adoption A/B, measured in stompy-order-and-top-resolve.md. **Item 2 IS BUILT
— `MTG_UPKEEP_CALL` (2026-09-15), default OFF, measured (−0.0019 on the shipped deck, 39 games
faster and 0 slower), NOT adopted: human play still needs a chooser.** Item 3 remains deferred.
Every number in Item 2 was **re-derived on 2026-09-15** at 2x the games on held-out seeds; the two
lever deltas reproduced exactly, and the screening-bias estimate was corrected from 0.0014 to
**0.0005** (the old figure differenced two different decklists — see "The re-measurement that
replaced the bias estimate").

**Status of the ITEMS BELOW that are still deferred: not being built yet.** Recorded per the deferred-work rule after the
StompySurprise cast-order review (USER, 2026-08-21; the order itself is implemented behind
`MTG_STOMPY_ORDER`, see `cast-order-rankings.md`). This doc holds the three modeling items the
cast-order rank *cannot* express, with the user's rulings verbatim.

## The problem

Worldly Tutor is not a draw — it is a **library write**: "search, shuffle, put on top". The USER:

> I just realized one problem with tutors like Worldly tutor. They search, but don't nicely draw
> the card. Instead they change the library. Arguably this ordering could be handled a different
> way. We could put the search cards above and trigger something like a breakpoint that re-enables
> cards that interact with the top of the library.

The deck's **top-consumers** are: Call of the Wild activations ({2}{G}{G}: reveal top; creature →
battlefield), Turntimber Symbiosis (look 7, put a creature), Vaultborn-style enters-draws (the
draw *is* the top card), and Mirri's Guile (upkeep arrange 3). A tutor mid-line changes what every
later consumer sees, so consumer decisions bound before the tutor resolves are stale — the exact
defect that hit the play viewer (seed 1 T4: Turntimber's put-candidates were collected pre-tutor,
so "put the tutored Craterhoof" was never offered; fixed for HUMAN play by moving the put to
resolution time off the real look).

## Item 1 — tutor as a breakpoint that re-enables top-consumers

The USER's [9] tier: "Worldly Tutor (resets Call of the Wild Activations and Turntimber when
cast)". Clarified rulings, verbatim: "We need to build the reset for my combo to be workable. To
be clear, it could also be done as a loop." / "Cast worldly tutor -> now activations and
Turntimber can be cast" / "I don't want the order to be static. It is a loop. If you play Tutor
you unlock Turntimber again as a possibility." / "You can think of it as a do-while loop. That
continues as long as we cast Tutors."

So the tier list is the order of ONE PASS, and a tutor cast opens the next pass with the
consumer tiers re-enabled — a do-while over passes whose condition is "this pass cast a tutor".
Natural Order sits OUTSIDE the loop (USER: "natural order does not need to be part of the loop.
It could go before the other effects except that it shuffles") — it never consumes the top and is
never re-enabled. Its POSITION was updated the next day (USER, 2026-08-21): late by default (just
before Craterhoof — "avoid us sacrificing creatures early and... drop a powerful Craterhoof with
it"), early only to keep its shuffle off a live tutor stack; and a new tier appeared between the
late NO and Craterhoof — "Worldly tutor loop one time (if searching for Craterhoof)" — a tutor
cast AFTER the late Natural Order whose stacked hoof the loop's consumers then drop. The static
rank cannot see a tutor's target, so that late-tutor-for-hoof placement rides on this Item's
re-solve, not on the order. Full verbatim + rank mapping in `cast-order-rankings.md`.
Intended shape: consume the unknown top first (Call activations, a first Turntimber),
then the tutor stacks a known creature, then the consumers go again (another activation round, a
second Turntimber copy). A static cast rank cannot express "A, then B, then A again" — two copies
of the same card share one rank. What can: the existing **breakpoint re-solve** machinery
(draw/staging/cascade cards already re-solve the rest of the turn from the post-resolution state;
`MTG_ACQ_RESOLVE` deliberately armed the same re-solve for tutor-to-HAND). Arming a re-solve after
`tutor_to_top` resolution would:

* let the search enumerate the post-tutor continuation (a fresh Call activation, the second
  Turntimber) against the *real* stacked top, and
* close the **autonomous composition gap**: Turntimber's clairvoyant named candidates are
  collected at action-collection time, pre-plan, so a plan casting Worldly Tutor(X) before
  Turntimber can never bind "put X" today (human play no longer cares — resolution-time modal —
  but the search still cannot compose the line).

Cost caution: breakpoints multiply rollout work; Turntimber was measured as this deck's #1
branching driver before the width cap. Any implementation must be measured on the suite like every
other lever.

## Item 2 — upkeep Call of the Wild activation (pre-draw window)

> **STATUS: BUILT 2026-09-15, behind `MTG_UPKEEP_CALL` (DEFAULT OFF). Measured; NOT adopted.**
> Raised again by the USER: *"we probably should double check we are using Call of the Wild
> correctly. It is important that we allow its activation in the upkeep."* The verification below
> confirmed the gap was real, and the patch closes it. See **Measurement** and **Adoption blocker**
> at the end of this item.

The USER, verbatim:

> Note that Call of the Wild Activation in Upkeep is a real play that we may need to model.
> If you have just the mana to cast Worldly Tutor at the end of turn this can be important.
> Since you often don't want to draw the card, but dump it into play for 4. (if you searched for
> it anyway)

The line: end-of-turn Worldly Tutor (cast-order rank 24) stacks a fatty; next turn's UPKEEP
activation puts it into play for 4 mana **before the draw step would pull it into hand** (where a
7–11 MV body must be hard-cast). Today Call activations exist only as main-phase plan actions —
by main phase the draw has already eaten the stacked card, so the engine cannot represent the
line's whole point.

**Gate — intentionally-stacked tops only.** The USER, verbatim:

> If you didn't stack the top I don't think it's as important to enable this, so we potentially
> could just allow it in those cases.
> I'm not particularly interested in having it take advantage of clairvoyance to do the same when
> it was not put there intentionally.
> Though I'm sure this will still happen from activations during the turn as well. (in that
> activations will look good when a bomb is coincidentally on top.

So the upkeep window should exist **only when the top was stacked by an intentional write** (a
`tutor_to_top` resolution since the last draw, or a Mirri's Guile arrangement) — never because the
clairvoyant search happens to know an unstacked top is a creature. (An unstacked upkeep
activation is the same blind gamble as the main-phase one and needs no new window; and yes,
in-turn main-phase activations already exploit clairvoyance — that is a known systemic property
of the search, acknowledged above, not something this window should add to.) Implementation
shape when picked up: an upkeep decision point in the vial-charge mold (heuristic default:
activate iff a stacked-known creature is on top and the mana doesn't strand the turn's plan;
human play surfaces it as a modal; state tracks "top stacked by <source> since last draw").

### What was verified first (400 logged games, before any code changed)

The gap was confirmed rather than assumed, and the *other* half was confirmed WORKING — which is
what bounded the scope:

| pattern | count |
|---|---|
| Worldly Tutor searches | 234 |
| **same-turn** compose (tutor, then a Call activation later that turn) | 36 — **36/36 revealed the tutored card** |
| tutored card left on top into the draw step | 187 |
| …with a Call of the Wild already on the battlefield | 8 |
| …and the stranded card MV >= 5, with mana to pay next upkeep | **6 (1.5% of games)** |

So the main-phase compose the cast-order rank was built for is sound; only the cross-turn line was
missing. 1.5% was flagged at the time as a **lower bound**, because the engine had no upkeep payoff
to plan toward — with the window built the real rate is **5%** (20 upkeep puts in the same 400
games), confirming the search does set the line up deliberately once it can.

**Re-measured on 1,000 logged games per deck, 2026-09-15** (`logs/callput/`), the 5% holds and the
copy-count dependence shows up here too:

| deck | games with an upkeep put | creature puts |
|---|---|---|
| shipped StompySurprise (4 Call of the Wild) | 44 / 1000 (**4.4%**) | 45 (4.5%/game) — 39 Craterhoof, 3 Worldspine, 2 Terastodon, 1 Elderscale |
| the recommended list (2 Call of the Wild) | 22 / 1000 (**2.2%**) | 22 — 19 Craterhoof, 2 Terastodon, 1 Worldspine |
| either deck, lever OFF | **0** | **0** |

Halving the Call of the Wild count halves the put rate (4.4% → 2.2%), which is the same ~2x the
lever-value ladder shows between 4 and 2 copies — two independent measurements of the same thing
agreeing. The body is almost always **Craterhoof Behemoth**: it is the tutor target whose value is
most timing-sensitive, so getting it a turn earlier is exactly what the window buys.

**A METHOD TRAP in this count.** The rate is measured by diffing the battlefield across the turn
boundary (end of turn T vs the DRAW-phase board of T+1), because reveals log under `MAIN_1` and there
is no `UPKEEP` phase to key on. That diff also catches **ETB tokens created by the put creature** — a
Terastodon put brings 3 Elephants, so the raw diff read 28 "puts" where only 22 were puts. Diff by
the permanent's `card` NUMBER and filter tokens; counting names alone inflates the rate by ~27%
whenever Terastodon is the body.

### How it is built

| piece | where | precedent it follows |
|---|---|---|
| the window's gate | `UpkeepRevealTopCandidate`, `core/SpellEffects.h` | — (one definition, shared by both worlds) |
| stacked-top identity | `GameState::top_stacked_card_number`, stamped in `PerformTutor` | pairs with the existing `top_stacked_turn` |
| the lever | `UpkeepRevealTopEnabled()`, `ai/EngineFlags.h` | `Main2DropEnabled()` (shared-reader rule) |
| pay/decline judgement | `DecisionProvider::ActivateRevealTopAtUpkeep` | `PayEchoToKeep` |
| executor | `AIEngine::ResolveUpkeepRevealTop` <- `GameEngine::UpkeepTail` | `ResolveEchoUpkeep` (pays mana at upkeep) |
| rollout | `TurnSolver::SimulateEndAndStartNextTurn`, before the draw | the echo block directly above it |
| the put | `ApplyRevealTopDeploy` | **already shared — no new code** |
| dominance | `top_stacked_turn` + `_card_number` folded as a gated pair | `scripted_vial_charge` (also live across the turn boundary) |

**The gate is the USER's ruling, implemented literally.** Two conditions, not one: the stack must
have been made on the PREVIOUS turn (`top_stacked_turn == turn_number - 1`), and the library front
must STILL be that exact card. The identity half is what separates this from reading a known top —
a clairvoyant engine always knows its top card, so "the top is a creature" is not a legitimate
trigger, and without the identity check a stack already eaten by another consumer would let a
coincidental creature re-qualify. A same-turn stack is deliberately rejected: that is the
main-phase compose, which already works.

**The heuristic is narrower than the doc proposed**, and deliberately so: the doc's "the mana
doesn't strand the turn's plan" cannot be evaluated here, because the upkeep runs BEFORE plan
enumeration. What replaced it is a static trade test — activate only when the stacked body costs
MORE than the activation does. Declining is not "lose the card": the draw step hands it over for
free and it then costs its mana cost, so the activation only gains on a discount. For this deck
every tutor target qualifies (Craterhoof 8, Worldspine 11, Terastodon 8, Hornet Queen 7, Apex
Altisaur 9, against a cost of 4) while a tutored dork (Priest of Titania 2) correctly declines and
is simply drawn. Making the "strand the plan" half searchable is the follow-up: a `Plan` axis in
the `vial_charge_choice` mold, which is precisely the mechanism for pinning a next-upkeep answer
from this turn's plan.

### Measurement (20,000 paired games per deck, same seeds, d6/b20)

| deck | delta | se | t | identical | faster / slower |
|---|---|---|---|---|---|
| shipped StompySurprise (4 Call of the Wild) | **−0.0019** | 0.0003 | −6.25 | 99.81% | 39 / **0** |
| the 2026-09-15 recommended list (2 Call of the Wild) | **−0.0005** | 0.0002 | −2.67 | 99.93% | 12 / 2 |

On the shipped deck it is **strictly one-sided** — 39 games faster, not one slower. The effect is
small because the line, while real, is rare (5% of games) and often redundant with a Natural Order
that would have deployed the same body anyway.

Both rows were **recomputed from the saved per-game dumps on 2026-09-15** (`logs/upkeep_ab/*.wins`)
and reproduce exactly: −0.00195 (t = −6.25, 39/0) and −0.00050 (t = −2.67, 12/2). The win SETS are
identical across lever states in both decks (`only_on = only_off = 0`), so dropping the games that
never win inside `max_turns` does not bias the pairing — worth checking, because a lever that pulled
a win inside the horizon would have been silently dropped from the intersection and undercounted.

### The re-measurement that replaced the bias estimate (2026-09-15)

The two rows above are two DIFFERENT decklists, run at different seeds under different profiles, so
differencing them attributes to *Call of the Wild copy count* something that also contains every
other difference between the two lists. Redone properly as **one axis on one shell** — Call of the
Wild ↔ World War Hulk across 5 combined slots on the recommended shell, 40,000 paired games per arm,
the whole ladder run twice on the same seeds (`logs/stompy_screen/callrecheck.json`):

| Call copies | avg, lever OFF | avg, lever ON | lever value (ON−OFF) | se | t | faster / slower |
|---|---|---|---|---|---|---|
| 1 (Hulk 4) | 4.3493 | 4.3491 | −0.00018 | 0.00007 | −2.65 | 7 / 0 |
| 2 (Hulk 3) — **the recommended list** | 4.3637 | 4.3633 | −0.00043 | 0.00011 | −3.71 | 19 / 2 |
| 3 (Hulk 2) | 4.3810 | 4.3804 | −0.00058 | 0.00013 | −4.43 | 25 / 2 |
| 4 (Hulk 1) | 4.3972 | 4.3963 | −0.00093 | 0.00016 | −5.78 | 39 / 2 |

The lever's value scales monotonically with the number of Call of the Wild in the deck, as it must.

**Two numbers in this doc were wrong and are corrected here.**

* **The bias on the 4 → 2 decision is 0.0005, not 0.0014** — the lever is worth 0.00093 at 4 copies
  and 0.00043 at 2, so the OFF measurement understated the 4-copy arm by **0.00050 ± 0.00020**. The
  old 0.0014 was the confounded cross-deck difference; it overstated the bias by 2.8x, which at
  least erred in the cautious direction.
* **The per-copy value of the trade is 0.0142–0.0172, not "~0.015–0.02."** The ladder's adjacent
  steps are +0.0144 / +0.0172 / +0.0162 with the lever OFF and +0.0142 / +0.0170 / +0.0159 with it
  ON (adjacent-pair se ±0.0007). The top of the old range was not supported. Note what this quantity
  actually is: not the standalone value of a Call of the Wild, but the cost of **trading one for a
  World War Hulk**, which is the trade the screen decided.

**The conclusion is now measured rather than inferred, and it holds.** With the lever ON the ladder
keeps its exact ordering and very nearly its slope; the 4 → 2 decision is worth 0.0334 turns OFF and
0.0329 ON. The bias is **1.5% of the decision it could have distorted** (~67x margin), so no screen
result from that evening moves.

### The heuristic is DELIBERATELY not searched, and it costs nothing

USER, 2026-09-15: *"How about if you always activate it in upkeep if we Worldly Tutored up a threat
last turn and otherwise do not? It might make sense to have a heuristic like that to avoid paying a
budgetary cost."* That is exactly the rule implemented above, and it is a decision NOT to build the
`Plan`-axis version floated as a follow-up: refining a 0.002-turn effect is not worth branching in
the hottest code in the repo. The gate is an integer compare that fails instantly on every turn and
every deck that did not stack.

"A threat" is implemented as **MV > the activation cost (4)**, and the deck makes that unambiguous —
its Worldly Tutor targets are Worldspine Wurm 11, Apex Altisaur 9, Terastodon 8, Craterhoof 8,
Vaultborn Tyrant / Hornet Queen / Elderscale Wurm 7, then the dorks at 1-3. **Nothing sits at MV 4-6**,
so the threat/dork split has a three-point margin either side and cannot misfire.

Cost, measured two ways:

| test | result |
|---|---|
| Goblins (a deck that can NEVER trigger it), lever ON vs OFF | **identical digest** `15d64391adf24192`; ms +0.6% (noise) |
| the recommended list, 3 INTERLEAVED 20,000-game pairs | +4.62%, −1.33%, −0.43% — **sign flips, no measurable cost** |

**A TRAP worth the line, because it nearly produced a wrong verdict.** Three SEQUENTIAL off-then-on
runs read +1.5%, +3.5% and +5.9% and looked like a consistent wall regression -- enough to call this
"not a clean win". Interleaving showed it was box drift: the OFF arm alone moved 700,034 -> 795,982 ms
across the three pairs. `ms=` is a SUM of per-game wall times, so it tracks machine load, exactly as
`.claude/skills/deck-screening.md` warns ("compare runs on a quiet box"). **Never read an A/B wall
delta off sequential runs; interleave, and look at whether the sign is stable.**

**Why this number mattered beyond the card.** The same evening's deck screening
(`analysis-StompySurprise.md`) cut Call of the Wild 4 -> 2 in favour of World War Hulk, which is a
comparison where the engine modelled one side less completely — exactly the judgement the
deck-screening skill says no guard can make. The bias is **0.0005 turns** (the one-axis ladder
above; the cross-deck difference of the two rows, 0.0014, was confounded), against a measured
per-copy trade value of 0.0142–0.0172. Around a thirtieth of one copy, and 1.5% of the decision it
could have distorted: the screening conclusions are unaffected, and running the ladder at both lever
states shows that directly instead of arguing it from a ratio. Recording it because the check, not
the outcome, is the point.

### Adoption blocker — human play has no chooser (the ONLY one left)

On quality and performance this now reads as a clean win: strictly better play on the shipped deck
(39 games faster, none slower), no measurable wall cost, and bit-identical where it cannot fire. The
chooser below is the one thing standing between here and flipping the default.


Echo surfaces its pay-vs-decline to a human (`m_external_echo_chooser`); this window does not, so
with the lever ON it would fire autonomously mid-viewer-session and record an activation the player
never chose. Harmless while the default is OFF. **Wire a chooser (and the viewer protocol entry)
before flipping it on**, along with the usual stompy-tier GT rebaseline, since play changes.
Stamping `PerformUpkeepReorder` (Mirri's Guile) into the same marker so a Guile arrangement can open
the window is the other loose end; it was left out because Guile is cut from the list this was built
for.

## Item 3 — Call-activation position within the turn (the [6] tier)

The USER's tier [6] places Call activations between the Call cast [5] and the cheat-out casts
[7]/[8]. The executor dispatches every activation AFTER every cast (trailing dispatch) — which is
exactly right for the tutor → activation compose (rank 14 tutor resolves before the trailing
activation reads the top), but cannot express "activate on the unknown top BEFORE the tutor
stacks it" or any cast-activation interleaving. If Item 1's breakpoint lands, the re-solve
subsumes this; a standalone fix (activations joining the ordered sequence like MTG_GARTH_ORDERED
did for Garth) is the fallback route.
