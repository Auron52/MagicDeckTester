# Breakpoint condemnation: SIX bugs fixed; now globally adoptable -- the flip is a USER call

**Status: the engine work is DONE and committed. As of 2026-08-26 the correctness fixes are DEFAULT
ON and condemnation is quality-neutral on every deck it can fire on, so the remaining decision is
whether to flip `MTG_BP_CLASSIFY` itself (which moves GT on mirrorwing). That belongs to the USER.** Self-contained. Supersedes the "condemnation is harmful" reading that circulated on
2026-08-25 (measured on a filter with two reachability bugs still in it) AND the "it buys nothing,
net +0.72% dearer" reading from earlier the same day (which measured how a BUDGET was spent rather
than what the prune costs -- see the cost section).

## What condemnation is

At a breakpoint (a mid-main re-solve opened by a draw), the continuation re-enumerates the turn. The
condemnation filter refuses to re-offer a cast that the pre-breakpoint section already considered
and passed on. It is a PRUNE — a pure cost device — and it is per-deck
(`DecisionProvider::CondemnsConsideredAtBreakpoint`), plus a global `MTG_BP_CLASSIFY`.

Live defaults (2026-08-26): the FILTER is still off -- `MTG_BP_CLASSIFY` off, `MTG_AL_BP_CONDEMN`
off, `MTG_KE_CONDEMN` off -- but every CORRECTNESS fix is now default ON
(`MTG_BP_CONDEMN_ORDER_AWARE`, `_MANA_EXEMPT`, `_RITUAL_EXEMPT`, `_TUTOR_EXEMPT`,
`_REDUCER_EXEMPT`). That split matters: previously a deck that flipped its per-deck hook silently got
the PRE-FIX filter, because the fixes for bugs 1, 4 and 5 were themselves default OFF.

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
| 6 | **COST REDUCERS condemned** — a reducer is an accelerant that pays in discounts | Hinata, train gi=1938, 2026-08-26 |
| 7 | **TREASURE-makers and, under a copy MAGNET, solo-target tricks** — condemnation FORCES THE CAST EARLY | Mirrorwing, train gi=13, 2026-08-26 |

Bugs 4-6 are below. Every one was found by root-causing a NEGATIVE measurement rather than by
review -- five of the six were. Bugs 3, 4 and 6 are the SAME bug three times: an accelerant is
whatever changes what the turn can afford, and each fix recognised only one more form of it
(permanent -> spell -> discount).

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

That is a USER call. **SUPERSEDED IN SCOPE by bugs 4-6 below**: `MTG_BP_CONDEMN_ORDER_AWARE` is now
default ON, and the question is no longer Kitty-only -- with the reducer/ritual/tutor exemptions in,
condemnation is quality-neutral on ALL five decks it can fire on, so the live proposal is to flip the
GLOBAL `MTG_BP_CLASSIFY` rather than any per-deck hook. The section below has those numbers.

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

## Bug 6 -- the cost reducer, and the result that unblocks a global default

With bugs 4 and 5 fixed, condemnation STILL measured negative on Hinata under the deck's SHIPPED
generic tiering: +0.0027 (t=1.89) train, +0.0040 (t=3.21) hold, and 0.9% DEARER in work units.
Instrumenting its 10 remaining train regressions: 8,999 condemnations, ONE victim -- **Hinata,
Dawn-Crowned herself**, at Ponder / Preordain / Gamble sites. She is rank 10 (creature) against
cantrips at 20, so the order-aware rule reads her as strictly earlier.

The line it deletes, straight from the logs (train gi=1938, a T4 win becoming T5):

```
  OFF (wins T4)                        ON (wins T5)
  T3 cast Ponder                       T3 cast Hinata
  T3 DRAW Forbidden Orchard            (Ponder banned in the continuation)
  T3 PLAY that land   <-- found by the cantrip
  T3 cast Hinata
  T4 Reality Spasm + Crackle = lethal  T4 Spasm + Crackle, too small
```

The cantrip is what FINDS the land that makes the 4-drop castable this turn. This deck plays its
land AFTER a draw spell in 22% of the turns that do both, so it is not an edge case.

**A cost reducer is an accelerant that pays in discounts rather than in mana**, so bug 3's argument
-- "how much mana the turn needs is exactly what a breakpoint draw reveals" -- applies to it in full.
`MTG_BP_CONDEMN_REDUCER_EXEMPT`, DEFAULT ON.

### Result: condemnation is now adoptable GLOBALLY, with no per-deck gating

Probing all 15 suite decks, condemnation only ever FIRES on five (per 8 games): mirrorwing 21,841,
fivecolour 2,189, hinata 1,249, kitty 254, antilife 39. The other ten are structurally inert -- no
breakpoint site ever has an already-considered cast. Measured ON vs OFF at play settings,
n=3000/cell, two blocks, with every correctness fix on:

| deck | quality | search work | note |
|---|---|---|---|
| **mirrorwing** | +0.0017 (t=0.73) / +0.0007 (t=0.21) | **-3.60% / -2.49%** | fires most; neutral and genuinely cheaper |
| kitty | -0.0003 / +0.0000 | -0.19% / -0.16% | ~inert, slightly cheaper |
| antilife | +0.0000 / +0.0000 | +0.04% | ~inert |
| fivecolour | +0.0000 / +0.0000 | +0.03% | fires 2,189x and changes 0-2 games: a pure prune |
| **hinata** | **BYTE-IDENTICAL** | — | after bug 6: 1,249 condemnations -> 0 |
| other 10 decks | — | — | condemnation never fires |

The reducer exemption is **provably inert** on the other four: a static scan of their decklists finds
no card with `hinata_cost_reducer` / `reduces_spell_color` / `reduces_spell_subtype`.

So the per-deck opt-in (`CondemnsConsideredAtBreakpoint`, `MTG_AL_BP_CONDEMN`, `MTG_KE_CONDEMN`)
exists to protect against bugs that are now fixed. Flipping `MTG_BP_CLASSIFY` to default ON is
quality-neutral everywhere and cheaper where it bites. **It WILL move ground truth on mirrorwing
(335-344 of 3,000 games change), so it needs a GT rebaseline -- that is the USER's call.**

### The ORDER was the other candidate fix, and it is the worse one

Ranking the cantrips ahead of the engine creature also fixes bug 6's case (the `>=` rule then exempts
her), and that is what an information-first order does. Measured, it costs more than condemnation
saves: the find-promotion-only order is **+0.0113 (t=3.41) / +0.0120 (t=3.85)** against baseline on
its own, and 1.9-2.5% dearer. Under it condemnation does improve (hold t=3.21 -> 0.71), but
`base -> min_cond` is still +0.0120/+0.0147. The exemption buys the same soundness for free. See
`hinata-cast-order.md`.

## Bug 7 (Mirrorwing) -- the damage can run the OTHER way: a FORCED-EARLY cast

Every earlier bug deleted a cast. This one adds one. Mirrorwing was the deck condemnation fires on
most (21,841 per 8 games) and it measured neutral-but-leaning-worse: +0.0027 (t=1.23) train,
+0.0013 (t=0.43) hold. Root-causing its 24 train regressions -- all 24 have condemnations, none is a
mulligan divergence -- the line diff says it plainly (train gi=13, a T4 win becoming T5):

```
  OFF (wins T4)                          ON (wins T5)
  T2  holds Gold Rush                    T2 CAST Gold Rush      <- forced early, 1 creature out
  T3/T4 casts it twice, more bodies out
```

**CORRECTED 2026-08-27 -- the first reading of this was wrong.** The obvious explanation is that
banning the card in the continuation leaves only "now" or "never" within the turn. That cannot be
what happens, and the USER caught it: *"why would that happen before the body arrives?"* Instrumented
by turn on gi=13, **ZERO condemnations fire on turn 2** -- the turn whose decision changes:

| turn | condemnations |
|---|---|
| 2 | **0** |
| 3 | 405 |
| 4 | 4,521 |

The turn-2 decision moves because the SEARCH PRICES THE FUTURE under condemnation. Its lookahead
reaches turns 3-4, where the filter bans the lines that make holding Gold Rush worthwhile, so
"hold it and cast it later" is projected as worse than it is and the search commits the card early.
**Condemnation's damage is NOT local to the breakpoint: it propagates BACKWARDS through the search's
valuation into earlier turns**, which is why a line diff alone cannot identify the cause and why the
counters at the divergence turn are empty.

In a Zada / Mirrorwing deck a solo-target trick is copied once per other creature, so a trick cast
before the turn's bodies arrive is a drastically weaker spell -- that part stands.

### The UNRECOVERABLE census, and the rule it produced

Aggregate neutrality is not the bar; the no-lossy-truncation bar is. So all 28 of Mirrorwing's
condemnation regressions were escalated on BOTH arms at 10x, 100x, and 100x + 1 depth ply:

**10 of 28 survived 100x budget AND +1 depth** -- genuinely deleted lines, the class the bar rejects
outright. **Gold Rush is the breakpoint SITE in 8 of the 10.**

That is the clue. Gold Rush mints a Treasure (one per creature when a magnet copies it), so its
continuation is exactly where the turn's affordability changes -- and condemnation bans everything
ranked before it there. Two distinct ways it bites:

* a card that was UNPAYABLE before is not a decline at all. gi=1205 turn 4 has 6 mana available
  (Game Trail 1 + Sandstone Needle 2 + Mountain 1 + Forest 1 + Ignoble Hierarch 1) and the winning
  line spends 7 ({1}{G} Gold Rush, {3}{R} Zada, {G} Draught) -- the Treasure is what pays for Zada.
* an X-SPELL is always "payable" at X=0, so the payability guard never protects it, but its SIZE
  scales with the pool. Luxurious Libation is {X}{G} and wants those Treasures.

So the fix keys on the **SITE, not the candidate**: `MTG_BP_CONDEMN_MANA_SITE_EXEMPT` (default ON) --
**if the card that opened this breakpoint made mana, condemn nothing here.** Result:

| | before | after |
|---|---|---|
| regressions | 28 | 19 |
| **UNRECOVERABLE** | **10** | **3** |
| quality vs OFF (train / hold) | +0.0003 / -0.0040 | -0.0010 (t=-0.60) / -0.0040 (t=-1.60) |
| search work vs OFF | -3.62% / -2.46% | -2.62% / -1.51% |

It gives back about a third of the prune saving, which is what condemning less is supposed to cost.
*Honest limit: it is not free -- it fixed 8 of the original worse games and introduced 3 new ones,
2 of which are among the 3 remaining unrecoverable.* The three that remain are condemned at
Impolite Entrance / Fists of Flame sites (draw and pump, not mana), so they are a different cause
and are NOT explained by this rule.

### Two exemptions built for Mirrorwing and DELETED -- recorded so they are not re-proposed

* **A copy-MAGNET exemption.** By count the magnet looks like the dominant victim (gi=13 turn 4:
  Mirrorwing Dragon 1,798 + Zada 1,158 at Gold Rush sites). Isolated it measures **-0.0007
  (t=-1.00) / -0.0003 (t=-1.00)** -- 4 games fixed, 1 broken, over 6,000. The USER's question killed
  it: *"Why would you ever want to cast the magnet after Gold Rush? Unless it is a duplicate magnet
  or something?"* -- exactly right. Of the 4 it fixes only 2 are the duplicate-magnet case; the other
  2 cast the FIRST magnet after Gold Rush, and a trick cast before any magnet is out is not copied
  at all, so that is a line worth deleting.
* **A copy-magnet TRICK exemption** (don't condemn a solo-target trick while a magnet is in play).
  It measured well on its own, but once the mana-site rule is in it is **redundant**: -0.0003
  (t=-0.58) / +0.0003 (t=1.00). Deleted in favour of the single site rule.

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
