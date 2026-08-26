# Breakpoint condemnation: fixed, play-neutral, a real but small prune -- flip is a USER call

**Status: the engine work is DONE and committed; the adoption decision is OPEN and belongs to the
USER.** Self-contained. Supersedes the "condemnation is harmful" reading that circulated on
2026-08-25 (measured on a filter with two reachability bugs still in it) AND the "it buys nothing,
net +0.72% dearer" reading from earlier the same day (which measured how a BUDGET was spent rather
than what the prune costs -- see the cost section).

## What condemnation is

At a breakpoint (a mid-main re-solve opened by a draw), the continuation re-enumerates the turn. The
condemnation filter refuses to re-offer a cast that the pre-breakpoint section already considered
and passed on. It is a PRUNE — a pure cost device — and it is per-deck
(`DecisionProvider::CondemnsConsideredAtBreakpoint`), plus a global `MTG_BP_CLASSIFY`.

Live defaults today: OFF everywhere. `MTG_BP_CLASSIFY` off, AntiLifegain's `MTG_AL_BP_CONDEMN` off,
KittyEquipment's hook returns `EnvOn("MTG_KE_CONDEMN")` (off). So everything below is inert until
someone flips a default.

## The USER's specification, and why the first version was wrong

> *"If the card was in hand and earlier in the order then it should be rejected even if it was
> drawn. The reason is because there is a line that plays it and we want to make that the line.
> 'more information' is irrelevant to the clairvoyant player."* — USER, 2026-08-25

That is the soundness argument, and it is conditional: condemning X loses nothing **only if a
SIBLING line casts X at its proper position**. The original filter had no notion of order at all, so
it condemned cards that had never been reached — measured 0 better : 6 worse on KittyEquipment.

Three fixes were needed before the filter matched the specification. Each was found by measurement,
not review, and each deleted wins that no depth or budget could recover.

| # | bug | pinned by |
|---|---|---|
| 1 | no order awareness at all — condemned cards whose slot came AFTER the breakpoint site | 6 losses, all Kor Duelist |
| 2 | rank TIES treated as "earlier" (strict `>` instead of `>=`) | hold gi=1325, seed 901326, mode 2 |
| 3 | mana sources condemned — an accelerant re-sequences legitimately | train gi=26, seed 300027, **mode 3 only** |
| 4 | **RITUALS condemned** — bug 3's fix only recognises accelerants that are PERMANENTS | Hinata, 75 regressions, 2026-08-26 |
| 5 | **TUTORS condemned** — what a tutor fetches is chosen at resolution, so the sibling line gets a different card | Hinata, same run |

Bugs 4 and 5 are in §"Bugs 4 and 5" below. Both were found by root-causing a NEGATIVE measurement
rather than by review, which is now four for five on this filter.

**Bug 2, why a tie is not "earlier".** Two cards at the same rank are peers with no enumerated order
between them, so condemning a peer is symmetric: whichever the plan casts first, the other is
condemned in the continuation, and NO sibling line survives. Concretely, with Puresteel Paladin out
and Bonesplitter + Lightning Greaves both Equipment at rank 8, the baseline casts Greaves, draws a
Plains off the trigger, plays it as the land drop, and the extra mana affords Bonesplitter for a
second draw — T4. Strict `>` gives the turn one draw instead of two: T5.

**Bug 3, why a mana source is special.** The sibling-line argument assumes a card's placement does
not change what else is castable. For an accelerant it always does, because how much mana the turn
needs is exactly what the breakpoint draw reveals. `MTG_CONDEMN_WHO` (a diagnostic at the drop site)
reported **70,805 condemnations in one game, every one of them Sol Ring** at an Equipment site —
nothing else was ever condemned. The baseline casts Shadowspear, draws, casts O-Naginata, draws
Lightning Greaves, and only THEN casts Sol Ring to afford the Greaves: T5, which becomes T6 once the
accelerant is nailed to rank 5. Sol Ring is `{1}` for `{C}{C}`; re-sequencing it is a pure mana
decision the cast order cannot express. `MTG_BP_CONDEMN_MANA_EXEMPT` (default ON) exempts
`mana_rock` and `ManaDork`.

## The measurement, after all three fixes

10,000 PAIRED games, two modes, train + hold blocks of 2,500 each. Baseline = HEAD (condemnation
off). Arm = `MTG_KE_CONDEMN` + `MTG_BP_CONDEMN_ORDER_AWARE`.

| cell | settings | better | worse | avg vs base | work units |
|---|---|---|---|---|---|
| m2 train | d5 / b40 | 0 | 0 | identical | −0.11% |
| m2 hold | d5 / b40 | 0 | 0 | identical | +0.21% |
| m3 train | d7 / b10000 | 0 | 0 | identical | +3.13% |
| m3 hold | d7 / b10000 | 0 | 0 | identical | −1.14% |

Net work **+0.72%** at the shipped budgets — which reads like "no saving", and is the wrong
reading. **That number measures how the budget was SPENT, not what the prune costs.**

`SearchBudget` is denominated in the very units being counted, and the iterative-deepening start
gate reinvests anything a prune frees into BEGINNING another pass. So a budgeted search never
returns a saving: total units stay pinned near the allowance and tail noise sets the sign. The repo
already documents this dynamic for a different lever — the m2 search memo, where "a hit skips the
nested search's budget consumption, so the outer deepening fits more passes ... that changed budget
dynamic is part of what the adoption A/B judges."

Re-measured at depth 5 with `budget_ms=0` (UNLIMITED), where a prune has nowhere to reinvest —
1,500 paired games, hold block:

| | cheaper | dearer | identical | mean ratio | total |
|---|---|---|---|---|---|
| condemnation vs base | **250** | **3** | 1,247 | 0.9969 ± 0.0005 | −0.11% |

**It is a genuine prune and it does strictly less work**, ~83:1 in its favour on the games it
touches. The total is only −0.11% because the filter fires in ~17% of games at all. Quality at
unlimited budget is again 0 better : 0 worse, with 2 games playing differently.

So the honest summary: a correct condemnation is play-safe and genuinely cheaper per search, but on
this deck the budget converts that saving into MORE SEARCH at equal cost — and the extra search
measures 0/0. The original 44–61% claim remains a d3 gate-cell artefact.

## The open decision

The adoption bar is *improve quality, or be quality-neutral **with other upside** (perf counts)*.
Condemnation is now quality-neutral across 11,500 paired games and three budget regimes, and the
upside is real but SMALL: strictly less search work (250 : 3 at unlimited budget) that the shipped
budget spends on more search rather than returning as time. On this deck that extra search buys
nothing measurable — so the flip neither clearly passes nor clearly fails the bar on perf grounds.

The stronger argument for ON is **doctrinal**: the USER's *"within a turn all breakpoints and phases
should be treated as one decision"* framing, which AntiLifegain already encodes as
`MTG_AL_CONDEMN`. Under that doctrine the search should not be free to change its mind about a card
mid-turn, and the fact that doing so is currently free is beside the point.

A third consideration is FORWARD-LOOKING: the filter fires in only ~17% of KittyEquipment games, and
this deck has exactly one breakpoint class. A deck with frequent breakpoints would see a
correspondingly larger saving, so "inert here" is not "inert in general".

That is a USER call. The engine is ready either way: flipping
`MTG_BP_CONDEMN_ORDER_AWARE` to default ON and Kitty's hook to `true` is a two-line change, and it
would affect ONLY KittyEquipment (every other condemnation default is off).

## Bugs 4 and 5 (Hinata, 2026-08-26) -- the exemption test was narrower than its own argument

Condemnation measured NEGATIVE on Hinata in every configuration: +0.033 on the generic order,
+0.052 on its full reviewed order, and +0.007..0.008 even with order-awareness (n=3000/cell, two
blocks, play settings). That is a much larger effect than anything in the Kitty work above, so it
was root-caused rather than attributed.

**Method.** All 75 regressions (`peer -> peeroa`) were reproduced with `--seed base+gi
--game-index gi`: 75 of 75 reproduce exactly and **none is a mulligan divergence**, so every one is
a real play difference. `MTG_CONDEMN_WHO` over five of the losing games gives 4,541 condemnations
and exactly TWO victims:

| dropped | count | at site | rank test |
|---|---|---|---|
| Gamble | 3,869 (85%) | Ponder / Preordain (7) | 6 < 7 |
| Reality Spasm | 665 (15%) | Soulfire Eruption (20) | 15 < 20 |

**Bug 4, RITUALS.** Bug 3's fix exempts `mana_rock || CardTemplate::ManaDork`. But its ARGUMENT --
"an accelerant is cast when the rest of the turn needs the mana, and how much mana the turn needs is
exactly what a breakpoint draw reveals" -- is a statement about MANA, and the test only recognises
accelerants that are PERMANENTS. A ritual is an accelerant that is a SPELL and every word applies to
it unchanged. Reality Spasm (`untap_x_mana_sources`; with Hinata's discount cancelling its {X} it is
{U}{U} to untap X sources) and Irencrag Feat (`ritual_floating_mana`) fall straight through. Across
the 75 regressions Reality Spasm is the most common cast the baseline makes and the condemnation arm
does not (33), Irencrag 6 more. Fixed by `MTG_BP_CONDEMN_RITUAL_EXEMPT`.

**Bug 5, TUTORS.** The sibling-line argument needs the earlier line to cast the card at its proper
position AND GET THE SAME THING. For a tutor that is false: the fetch is chosen at resolution from
the state (`HinataProvider::TutorCandidates` is combo-aware -- fetch Hinata while she is missing,
else the missing piece), so "Gamble then Ponder" and "Ponder then Gamble" fetch different cards. A
tutor declined before a breakpoint was declined under strictly less information, which is the same
reason a card the breakpoint DREW is never condemned. Fixed by `MTG_BP_CONDEMN_TUTOR_EXEMPT`.

### VOLUME IS NOT HARM -- the most transferable result here

| fix | vs order-only, train | hold | condemnations (5 games) |
|---|---|---|---|
| none | +0.0070 (t=2.09) | +0.0077 (t=2.62) | 4,541 |
| **ritual exempt** | **+0.0033 (t=1.23)** | **+0.0020 (t=0.97)** | 3,874 |
| tutor exempt | +0.0057 (t=2.48) | +0.0073 (t=3.32) | 207 |
| both | +0.0033 (t=2.36) | +0.0007 (t=0.71) | 6 |

The tutor exemption removes **85% of condemnations by COUNT and almost none of the damage**; the
ritual exemption removes 15% by count and is the whole fix. Never rank a prune's bugs by how often
they fire. (Bug 5 is kept anyway, on the soundness argument -- it is simply not what was hurting.)

### An ORDER-side fix for the same finding was built and REJECTED

Gamble is only condemnable because the reviewed order ranks it (6) ahead of the cantrips (7), so
`MTG_HINATA_GAMBLE_LATE` moves it to 8. Its own counter kills it: **Gamble is itself a breakpoint
site** (`tutor_to_hand` is in `is_draw_engine`), so at rank 8 it condemns the rank-7 cantrips
instead -- 4,541 -> 4,271, now Preordain 2,400 + Ponder 1,140. It relocates the bug. Recorded so
nobody re-proposes it.

### Cross-deck: SAFE everywhere, but demonstrated useful only on Hinata

The exemptions are engine-wide, so they were priced on every deck that can run condemnation, with
condemnation ENABLED IN BOTH ARMS (n=2500/cell, two blocks):

| deck | condemnations fire? | what it drops | delta | games differ |
|---|---|---|---|---|
| Anti-Lifegain | yes, 46 / 15 games | Plague Drone, Aria of Flame | +0.0000 | 0 |
| KittyEquipment | yes, 287 / 15 games | Puresteel Paladin, Stoneforge Mystic | +0.0000 | 3-4 |
| **Dragonstorm** | **NO -- 0 / 15 games** | — | +0.0000 | 0 | 

So: no regression anywhere, and on AL the exemptions correctly decline to fire (neither victim is a
ritual or a tutor).

> **The Dragonstorm cell is VACUOUS and must not be read as evidence.** It was included precisely
> because it is the ritual deck, but condemnation never fires there at all (no `is_draw_engine`
> card, so no breakpoint arms the hand snapshot). A zero delta from an arm where the lever cannot
> fire says nothing. THE RITUAL EXEMPTION IS DEMONSTRATED ON HINATA ONLY. An earlier version of this
> control was vacuous for a different reason -- it ran AL and Kitty at their shipped defaults, where
> condemnation is OFF -- which is the second time in one session that a "0 games differ" was a dead
> lever rather than a safe one. Always confirm the lever FIRES before reading its null result.

### What this does NOT fix

With both exemptions condemnation on Hinata is ~inert (30-52 of 3,000 games differ) and
neutral-to-marginally-negative -- it stops losing, it never wins. The reason is structural: this
deck's breakpoint sites are its CANTRIPS, at rank 7 near the FRONT of the order, so "already
considered and declined" is nearly an empty set by construction. A cast order cannot fix that. The
forward-looking note above ("a deck with frequent breakpoints would see a correspondingly larger
saving") should be read with this qualifier: what matters is not breakpoint FREQUENCY but how much
of the order sits BEFORE the sites that fire.

## Two things worth carrying elsewhere

* **Mode 3 earned its keep on its first real use.** Bug 3 is invisible at play settings — mode 2 read
  a clean 0/0 with the bug still in — and shows up only at d7/b10000. That is exactly the
  reachability class `three-measurement-modes.md` predicts mode 3 exposes, and it is the argument
  for running mode 3 before adopting any PRUNE.
* **Condemnation makes the cast order load-bearing for REACHABILITY.** Every one of these bugs is
  the same shape: a heuristic ranking, which the search is otherwise free to violate, becomes a hard
  law at breakpoints. That is the collision recorded in `cast-order-ideal-with-ranges.md` /
  `cast-order-rankings.md`. If condemnation is ever turned on for a deck whose order is less
  carefully reviewed than Kitty's, expect the same class of loss — and note that the GENERIC order
  ranks all non-creatures at 20, so ties are the common case, not the exotic one.
