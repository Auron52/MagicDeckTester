# Breakpoint condemnation: EIGHT bugs; bug 8 was the big one -- the rank test infers a decline that never happened

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

### USER STEER (2026-08-27): this is really a RANGE statement, not a condemnation rule

> *"I don't totally disagree with that statement, but it's more of a Gold Rush range type statement
> in reality."*

Read the mana-site rule as a stopgap, not the destination. What it actually encodes is that **Gold
Rush has no single proper position** -- its slot depends on whether the line can pay -- and this repo
already has the mechanism for exactly that: the cast-order RANGE and its funding ladder
(`CastOrderFallbackRanks`, `docs/design/cast-order-ideal-with-ranges.md`). Mirrorwing already
declares `[6..15]` for it, from the USER's 2026-08-18 review.

So the next move is NOT another condemnation predicate. It is to express Gold Rush's
affordability-dependent position as a RANGE and let condemnation respect that range, instead of
special-casing "the site made mana". Two loose ends to pick up with it:

* `MTG_ORDER_RANGE` is **default OFF**, so the declared `[6..15]` ladder is inert today. The probe
  on gi=13 confirms it never walks (`[order-range] ideal order pays`).
* The ladder's floor of 6 puts Gold Rush ahead of every body (creatures at 10), which contradicts the
  USER's 2026-08-27 *"spells should go last in the order"*. If the range is ever adopted, that floor
  needs revisiting -- the base order is already spells-last; only the ladder violates it.

#### THE DECISION TO MAKE NEXT (USER, 2026-08-27)

> *"We can decide which approach is better in general, your rule or just expanding the range. We
> could potentially only allow this if a magnet is already present."*

Three arms, and they are directly comparable because the census above gives an objective scorer:
**how many regressions survive 100x budget AND +1 depth** (3 today, 10 before the site rule), with
quality and work units alongside.

| arm | what it is | note |
|---|---|---|
| **A. site rule** | `MTG_BP_CONDEMN_MANA_SITE_EXEMPT` (shipped, default ON) | condemns nothing at a mana-adding site; blunt — it drops the prune everywhere Gold Rush fires |
| **B. range** | turn on `MTG_ORDER_RANGE` and let Gold Rush's declared `[6..15]` ladder express the affordability-dependent slot; condemnation then respects the range | the USER's preferred framing; needs the floor revisited (6 is ahead of the bodies, vs *"spells go last"*) |
| **C. either, magnet-gated** | apply A or B only while a copy magnet is already on the battlefield | **NOT a refinement -- this is the CORRECT form of A**, see below |

**C is mandatory, because A's premise is FALSE without a magnet.** USER, 2026-08-27: *"Gold Rush
does not make mana when not."* Exactly right, and it invalidates the rule as shipped: Gold Rush costs
`{1}{G}` and mints ONE Treasure, so bare it is **mana-NEGATIVE (-1)**. It only becomes a mana engine
when a magnet copies it once per other creature. So "the site added mana" -- the whole justification
for condemning nothing there -- is simply untrue whenever no magnet is out, and the rule is dropping
the prune on games where there was never an affordability change to protect.

**So `MTG_BP_CONDEMN_MANA_SITE_EXEMPT` as committed (default ON) is OVER-BROAD** and its measured
numbers include those unjustified firings. The gate to add is net-mana-positive-IN-CONTEXT, which for
a treasure-maker means a copy magnet on the battlefield; `ritual_floating_mana` /
`untap_x_mana_sources` sites are unconditionally mana-positive and need no gate. Re-measure the
census after gating -- the ~third of the prune saving currently given up should come back without
losing the recovered lines.

**Do NOT score these by condemnation counts.** Three times this session the count pointed the wrong
way (see VOLUME IS NOT HARM below). Score on the unrecoverable census + the paired metric.

Note B is not merely a re-spelling of A: A says "this SITE invalidates every decline", B says "this
CARD has no single proper slot". B is narrower and more honest about what is actually true of Gold
Rush, and it leaves condemnation working at mana-adding sites for every other card.

#### THE DECISION, RESOLVED (2026-08-27) -- the rule is right, and it belongs at the ORDER, not the SITE

USER: *"use the 'Gold Rush positive' rule to decide whether we need to consider casting it earlier.
If it doesn't add mana or fix colours then we hold it. This is true for any point prior to 15."*

Built as **two levers** (a lever spanning two call sites is two levers), predicate
`TreasureSpellNetMana` in `SpellEffects.h`, both default OFF, byte-identical to HEAD at defaults
over 5 decks. Measured on Mirrorwing, **n=8000 per cell, two blocks**.

**HOW OVER-BROAD THE SHIPPED RULE IS: 15,863 of 16,294 treasure-site exemption tests (97.4%) are at
net = -1** (`MTG_TREASURE_SITE_PROBE`, 20 games). The USER's arithmetic is exactly right.

##### The ORDER half is PROVABLY INERT -- and that is the result, not a null

`MTG_MW_GR_LADDER_POSITIVE` (Gold Rush's funding ladder offers rungs 13/6 only at net >= 0):

* **byte-identical to condemnation-off over 16,000 games** (digests `07822de8…` / `45ba25a4…`), and
* **byte-identical to the shipped condemnation over another 16,000** (`a50ccda5…` / `3a670f11…`).

It is NOT a dead lever -- 13 games of 8,000 differ in *work units* -- it fires, denies an early rung,
and the search picks the same line anyway. Confirmed independently by `MTG_ORDER_RANGE_PROBE`: over
40 games the ladder is entered 11,133 times, the **ideal order pays 11,074 of them, and "stepped-down
order pays" NEVER prints**. The engine already holds Gold Rush at 15 unless it is positive; the lever
only makes that true by construction. It is also already encoded in the PAYMENT layer --
`MintedTreasureSpendable` / `FreshHoldActive` (default ON) refuse to credit a minted Treasure as
same-turn mana without a live magnet. **The USER's rule was already the engine's behaviour.**

##### The SITE half is arithmetically right and measures WORSE -- rejected

| arm | quality vs OFF (hold / train) | search work vs OFF | UNRECOVERABLE vs OFF |
|---|---|---|---|
| **shipped** (any minter exempts) | +0.0011 (t=1.29) / +0.0003 (t=0.24) | -1.42% / -1.89% | **29** |
| **positive** (net >= 0 only) | +0.0016 (t=1.57) / -0.0009 (t=-0.63) | **-2.38% / -3.10%** | **40** |
| **nosite** (no exemption at all) | +0.0101 (**t=6.84**) / +0.0071 (**t=4.20**) | -1.30% / -2.16% | -- |

Head to head: `positive` deletes **12** lines `shipped` keeps; `shipped` deletes **8** that
`positive` keeps. So the corrected rule is ~1pp cheaper and aggregate-neutral (+0.0005 t=0.76 /
-0.0011 t=-1.01) but **deletes more reachable lines**, which the no-lossy-truncation bar rejects.
*Honest limit: 12-vs-8 is a thin margin on its own; the 40-vs-29 figure against condemnation-off is
the stronger of the two and points the same way.*

##### Why -- and it is the USER's own "or fix colours" clause

**VOLUME IS NOT HARM, a FOURTH time, and the sharpest instance yet.** The exemption suppresses only
**938 of 18,609 condemnations (5.0%)**, every one at a Gold Rush site (`MTG_CONDEMN_WHO`, 12 games:
Fists of Flame 10,648 / Scale the Heights 4,810 / Ancestral Anger 1,190 / Expedite 1,023 / Gold Rush
938). Removing that 5% costs **+0.0101 and +0.0071 at t=6.84 and t=4.20** -- 5% of the firings carry
essentially all of the damage.

What makes a Gold Rush site special is **not that it adds mana** -- 97.4% of the time it does not --
but that it changes **what the pool can PAY FOR**. Even at net -1 it converts `{1}{G}` into a WILD
Treasure, and Mirrorwing is a two-colour deck where that is a real fix (the engine already carries a
scarce-colour rank tier motivated by exactly this: *"mw326: Gold Rush {1}{G} eating the lone {R}
source"*). That is the **"or fix colours"** half of the USER's sentence -- and the `net >= 0` gate
drew the colour-fix line at *zero cost*, when a colour fix that costs one mana is still a colour fix.

**So the two halves take DIFFERENT bars, and that is the resolution:**

| call site | question it asks | bar | verdict |
|---|---|---|---|
| **ORDER** (funding ladder) | is casting it early a net MANA gain? | `net >= 0` | **the USER's rule, and already the behaviour** |
| **SITE** (condemnation) | did the pool's PAYING POWER change? | mints a Treasure at all | **the shipped rule -- right, for a reason its own comment states wrongly** |

Actions: `MTG_BP_CONDEMN_TREASURE_SITE_POSITIVE` stays **default OFF** -- built, measured, rejected,
recorded here so it is not re-proposed. Bug 7's comment is corrected to say COLOUR FIX rather than
"added mana". `MTG_MW_GR_LADDER_POSITIVE` may be flipped on as a free correctness guard (provably
inert over 32,000 games) or left off; it makes no measurable difference either way.

This also retires arm **B** (expand the range): the range machinery is already live on Mirrorwing via
`OrderOpaqueCastsByRank`, and the probe shows its early rungs are all-but-dead in play. There is no
"expansion" left to make -- the ladder is already declining to walk Gold Rush early.

*Correction to the numbers recorded above:* at n=8000 the shipped site rule measures **+0.0011 /
+0.0003 vs condemnation-off**, not the -0.0010 / -0.0040 recorded from n=3000. The earlier
"improvement" was noise; the rule is quality-NEUTRAL, not positive.

## BUG 8 (2026-08-27) -- the rank test infers a decline that never happened

> USER: *"My take here is that condemnation should not produce worse results almost ever. Since it
> reduces the overall work. So, the data may be hiding an actual issue."*

**That is the correct test, and it was hiding one.** A prune that removes only genuinely
considered-and-declined casts **cannot** lose at 100x budget -- there is no dilution left to blame.
So every survivor of the escalation census is a FALSE PREMISE, not the price of pruning. 29 survivors
meant 29 false premises.

### The specimen: hold gi=5259, identical hands and draws, ONE difference

| | base (condemnation off) | condemnation |
|---|---|---|
| T1 | Forest, Ignoble Hierarch | same |
| T2 | Sandstone Needle, **hold** | Sandstone Needle, **CAST Gold Rush** |
| T3 | Kazandu Refuge, Zada | same |
| T4 | Forest, **Expedite, Elvish Mystic, Twinflame, Gold Rush -> WIN T4** | Forest, nothing |
| T5 | -- | Twinflame, Expedite, Elvish Mystic, Fists -> win T5 |

`MTG_CONDEMN_WHO` on that game: **`T4 drop=Twinflame rank=12 site=Expedite site_rank=14` x3,309.**

Twinflame is exactly the card base's winning T4 line needs. The order-aware rule condemns it because
its RANK (12) precedes the site's RANK (14) -- *"its slot already passed"*. It had not. The plan was
**the cantrip ALONE**, cast first to draw before committing, with the rest of the turn deliberately
deferred to the continuation. Nothing preceded it, so nothing was declined.

**Why the rank test is unsound here.** `OrderingOpaque()` (ManaPayment.cpp) returns true for any set
containing a draw / stage / cascade / retrace / `solo_target_trick` card -- *"that set keeps its
canonical plan/breakpoint order (**the search owns the ambiguous ordering**)"*. That is precisely the
class that opens breakpoints, and on Mirrorwing it is every trick in the deck. So condemnation is
enforcing a cast order the engine deliberately refuses to apply.

### The damage propagates BACKWARDS, and it is NOT budget

With the T4 line deleted, "hold Gold Rush" prices worse than it truly is, so the arm dumps it on T2
with no magnet out -- where a solo-target trick is worth ONE copy instead of N. **There are ZERO
condemnations on T2**, the turn whose decision actually changed. And no budget recovers it: a pruned
continuation is not under-searched, it is *smaller*. That is why 100x + 1 ply changes nothing.

Across all 29 the shape is identical -- a BODY or a MAGNET condemned at a TRICK site:

| condemned (rank) | at site (rank) | count over the 29 |
|---|---|---|
| Twinflame (12) | Fists 16 / Anger 14 / Expedite 14 / Scale 14 | 103,698 |
| Expedite / Anger / Scale (14) | Fists of Flame (16) | 28,114 |
| Goblin Instigator (10) | trick sites | 25,071 |
| **Zada / Mirrorwing Dragon (5) -- the MAGNETS** | trick sites | 16,166 |

The magnets are the tell: this deck's whole engine is *"bodies first: more copies for the fan-outs"*,
and condemnation was banning the magnet in the continuation of a trick.

### The fix was already in the tree, built and never measured

`MTG_BP_CONDEMN_TAIL_EXEMPT` -- root-caused on KittyEquipment 2026-08-25, default OFF *"pending
measurement"*: skip condemnation when the plan has no cast LEFT to make, because *"the base plan is
ONE plan, not an exhaustive verdict on every card in hand"*. That is exactly the cantrip-alone plan.
**It recovers 24 of the 29.**

| arm (vs condemnation OFF) | quality hold / train | regressions | **UNRECOVERABLE** | work hold / train |
|---|---|---|---|---|
| bug 8 present (shipped) | +0.0011 (t=1.29) / +0.0003 (t=0.24) | 26 + 26 | **29** | -1.42% / -1.89% |
| **+ tail exemption** | +0.0003 (t=1.00) / -0.0001 (t=-0.22) | 3 + 4 | **5** | -0.09% / -0.19% |
| + tail + treasure-positive | -0.0003 (t=-0.50) / -0.0001 (t=-0.14) | 4 + 4 | **5** | -0.27% / -0.21% |

Condemnation also RECOVERS one line base loses (train gi=2647), unchanged by the fix.

**Now DEFAULT ON.** Byte-identical at shipped defaults over 5 decks (condemnation is off everywhere
by default), so it moves no ground truth.

### The honest cost, and it is the real finding

**Condemnation's work saving was coming almost entirely from the unsound prune.** -1.42%/-1.89%
becomes -0.09%/-0.19% once the false premise is removed. Done correctly, condemnation is ~free on
both axes rather than a win on work. That is worth stating plainly before anyone flips
`MTG_BP_CLASSIFY` globally expecting a perf return.

It also **rehabilitates the Gold Rush net>=0 site gate**: `tail_pos` is the best arm measured (both
blocks directionally faster than condemnation-off, 5 unrecoverable, -0.27%/-0.21% work), and
`tail_pos` vs `tail` is 0 unrecoverable in BOTH directions. The gate was only harmful because it
compounded bug 8's damage.

### What remains: 5 games, and it is the same bug in its pure form

train 2039 / 6433, hold 1485 / 2804 / 5407. All five are one shape -- **the arm dumps tricks EARLY,
before the magnet lands.** The tail exemption does not fire because those plans DO have a tail, yet
the rank comparison is still invalid for the same reason (search-owned order). The residual
condemnations are the same table as above (Twinflame at Expedite, the magnets at trick sites).

The principled next fix is to stop inferring the decline from a static rank at all: condemn only when
the plan actually cast something BEFORE the site. Untried.

## THE ORDER PROGRAMME (USER, 2026-08-27) -- the direction this work takes next

Four steers, in the USER's words, which together replace "tune the prune" with "fix the order":

> *"I don't want to exempt things from the prune. Instead, I want to figure out an order that works
> reliably with occasional cases where a specific card is given a range."*
> *"Essentially condemnation is all about search declining to play a card in our order without a
> legitimate excuse for not doing so."*
> *"And by adjusting the order and rules, we want to make there no legitimate excuses for choosing a
> different order."*
> *"So, essentially, we condemn if we can play a card in the order and choose not to do so. This
> would not condemn the extra lands, but not emit them until we actually have the option to play
> them."*

So condemnation is not a prune to be tuned but a **detector for order defects**: every line it
deletes is a case where the declared order disagrees with the line that wins. Exempting where it
bites suppresses the detector. `MTG_BP_CONDEMN_TAIL_EXEMPT` is therefore back to **default OFF** and
kept only as a diagnostic (it isolates that 93.4% of firings sit at no-tail plans, carrying ~93% of
the work saving) and as the fallback if no reliable order exists.

### The land drop is part of the order, and for Mirrorwing it goes FIRST

**USER, 2026-08-27: *"Land drop can go first in this deck."*** — and the reason, which is what makes
the pin safe rather than merely convenient: ***"Because we can always play a drawn land later (there
is no cost to this)."*** Deferring the drop buys option value only if holding it can ever pay; here it
cannot, because a land the turn later reveals is playable on a subsequent turn at no loss. With
nothing to gain from holding, playing the held land at slot 0 dominates, and declining it is a
genuine decline rather than a deferral.

And the argument that generalises: ***"And no real advantage to deferring as a clairvoyant."***
Deferring a land drop is an INFORMATION play -- hold it until you see what you draw -- and this
search already knows. It is the same principle the USER used to justify condemnation in the first
place (*"'more information' is irrelevant to the clairvoyant player"*), applied to the land drop.

And the one case where deferring genuinely does pay is **already covered by an existing rule**:

> ***"There is an advantage to deferring in the case we draw a better land, but that case is not
> condemned. So, simple rules work here."***

A land the breakpoint DRAWS is a new card, and the drawn-card exemption already spares it -- the same
rule that spares a drawn cantrip. So the pin does not have to carve out the one exception that
matters; it inherits it. That is the whole argument for keeping the rule simple rather than
conditional, and it is why "pin the drop, condemn the held lands" is safe here.

The remaining reasons to defer are MECHANICAL, not informational: a Karoo bounce land, a storage
land, Land's Edge ammo, landfall timing. The engine already carries `HoldDeferredDropForLethal` and a
`karoo_deferred` reservation for exactly those, which is what the pin must continue to respect. The
USER scoped the statement to this deck (*"in this deck"*), and those mechanical exceptions are why
the scoping is right even though the clairvoyance argument is general.

That settles the open question and makes land condemnation well-defined:

* *"no land drop is a true play the game can make... and this play condemns all lands in hand"* --
  declining the drop at its slot is a real move, so having declined it, a land already in hand may not
  be played later in the continuation. A land **drawn** at the breakpoint is a new card and is exempt
  under the existing rule.
* The EMISSION half is already correct and needs nothing built: `LandDropsAvailable()` gates land
  actions, so a land that cannot be played is never offered and never reads as a decline. Confirmed
  empirically -- **no land is condemned in 175,481 firings**, because condemnation lives in the CAST
  enumeration only.

**What this predicts, and it is the next measurement.** Of 28 post-trick land plays in winning
(condemnation-off) turns, **28 were lands already in hand and 0 were drawn by the trick** -- i.e. the
search routinely defers a held drop past its casts. With the drop pinned first, those deferrals become
order violations. The USER's position is that they are the search exploiting a missing rule and that
playing the land at its slot wins just as fast. **Test:** pin the drop, replay those turns, see
whether the win turn moves. If it does not, land condemnation is free and correct, and it TIGHTENS
the prune rather than loosening it -- the opposite direction from every exemption in this document.

### BUILT (2026-08-27): `MTG_BP_CONDEMN_LAND`, and it is the dominant half of condemnation

Three pieces, all default OFF / byte-identical (verified against a worktree build of the committed
tree over 12 decks x 40 games at each deck's shipped play settings):

* `DecisionProvider::LandDropCastOrderRank()` -- the drop's slot, `-1` = "no declared slot" for
  every deck. `MirrorwingProvider` returns **0**.
* `BpLandDropSlotPassed()` (TurnSolver.cpp) -- composed with the site rank rather than duplicating
  `BpSlotIsAfterSite`, so the drop obeys the same order law as every cast: strictly-earlier
  condemns, a TIE does not, and a mana-adding site condemns nothing.
* The filter itself, in `EnumeratePlansWithLandUncached` where `land_names` is built.

**Keyed on the SIGNATURE GROUP, not the card number and not the name.** That is the equivalence this
enumeration already uses -- two lands sharing a `land_sig` are interchangeable by construction, only
one is ever enumerated -- so drawing a second copy of a signature we already declined is not new
information. It is the cast side's fungibility clause (*"a duplicate copy of X being drawn doesn't
change anything"*) stated in the lands' own currency, and it is both stricter and more principled
than a name-keyed rule: the drawn Mountain and the held Forest are the same decision here. Urgency
dominates it exactly as on the cast side, so a STAGED land (Light Up the Stage) is never condemned by
a permanent hand copy.

**TWO SOUNDNESS CONDITIONS, both found by asking "when is the defer branch NOT a decline?"**

1. **A forced defer is not a decline.** Treasure Hunt is the live counter-example: the strict flood
   gate suppresses every "play land THEN cast the draw engine" plan, so `add_for_land("", "")` is the
   ONLY route to casting it. Condemning off the back of that would delete lines the search never
   chose to skip. Written into the hook's contract -- a deck with a flood engine, or any other gate
   that removes the land-first plans, must leave the rank at -1.
2. **A drop already TAKEN has not been declined.** USER, on extra land drops: *"that is a good point
   that extra land drops would unlock land plays. We should not condemn lands not played when we are
   not allowed to play them."* Guarded on `lands_played_this_turn == 0`, which also closes the
   narrower hole underneath it -- a Karoo the root skipped because no other land was out was never
   OFFERED at the drop's slot, and a first drop is what would make it enumerable. **Provably inert
   today:** `LandDropsAvailable()` is `1 + bonus`, the only card in the whole pool that grants a
   bonus is Scale the Heights, and it is not in the shipped list -- so `drop_available` already
   implies `lands_played_this_turn == 0`. The guard is a tautology now and a correctness condition
   the moment a second drop returns.

**A THIRD HOLE, AND IT IS THE ONE THE STATE CANNOT SHOW YOU: a RESERVED drop is not a declined
one.** `ApplyPlanDirect` reserves the drop for an `etb_bounce_land` (`karoo_deferred`) and plays it
only after the main casts, so the Karoo must bounce a land this turn's casts have already tapped.
Through the whole cast section the land is therefore *still in hand* and `lands_played_this_turn` is
*still 0* -- a breakpoint sees a state **indistinguishable** from "the plan passed on its drop", when
the plan in fact chose Gruul Turf. Condemning off that reading is a false premise for every other
land in hand, i.e. bug 8's exact shape. Carried on `CantripOrderScope` (the object BOTH worlds
already construct at a breakpoint), so `AIEngine::resolve_draw_breakpoint` binds the same fact at the
same place -- the lockstep-pair discipline that class exists for.

**...and because it is invisible in the state, it must be FOLDED INTO THE BP-ENUM CACHE KEY.** Two
plans reaching the same breakpoint -- one having reserved Gruul Turf, one having passed on its drop
-- are byte-identical mid-turn, so without the fold the second is served the first's plan list. Same
class as the memo's own "enum-memo verify find #4" (live scripted pins steer enumeration and must be
folded). Checked against the memo's own contract, which is that results are identical with and
without the cache: `MTG_NO_BP_ENUM_CACHE=1` agrees to 4 decimals in all three arms (defaults 4.6667,
condemnation 4.6667, +land 4.6733), which also rules out the neighbouring question of whether
`g_bp_site_def` needs folding.

**IT IS NOT A MARGINAL RULE.** 10 games, `MTG_BP_CLASSIFY=1 MTG_BP_CONDEMN_LAND=1`, MTG_CONDEMN_WHO:
**6,832 land condemnations against 773 cast condemnations** -- so with the drop pinned, condemnation
on this deck is ~90% a LAND rule. By site: Oracle's Restoration 4,438, Impolite Entrance 2,178, Fists
of Flame 216. By land: Game Trail 2,566, Mountain 1,616, Sandstone Needle 1,094, Forest 747, Gruul
Turf 574, Rootbound Crag 235. Volume is not harm (four times over in this arc), which is why the
paired per-game measurement is the output and this count is only proof the lever fires.

### MEASURED (2026-08-27) -- REJECTED as specified, and the reason is NOT land timing

Shipped Mirrorwing, 12,000 games x 2 disjoint blocks, play settings (d5/b20), pooled with the
rollout-ranker sweep in one queue. Paired per-game, baseline = what the deck ships:

| arm | train delta (t) | hold delta (t) | faster/slower |
|---|---|---|---|
| `cond` (cast condemnation) | -0.0010 (-1.17) | -0.0013 (-1.53) | 46/35, 43/31 |
| **`ng` (MTG_BP_NO_GREEDY_CONT alone)** | **-0.0008 (-2.67)** | **-0.0006 (-2.11)** | **12/2, 9/2** |
| `cond_ng` | -0.0014 (-1.60) | -0.0019 (-2.09) | 52/36, 52/33 |
| `cond_land` | +0.0008 (0.66) | +0.0014 (1.16) | 60/73, 62/72 |
| `cond_land_ng` | +0.0005 (0.39) | +0.0013 (1.05) | 77/84, 76/87 |
| `cond_tail` | -0.0003 (-0.77) | +0.0001 (0.30) | 9/6, 5/6 |
| `cond_land_tail` | +0.0021 (2.12) | +0.0022 (2.01) | 37/62, 41/57 |

**Isolated (baseline = `cond`, so this is what the LAND rule adds): +0.0018 (t=2.15) train,
+0.0027 (t=3.19) hold, 23 faster / 46 slower on BOTH blocks.** A 2:1 loss ratio reproduced exactly
across disjoint seeds is not noise. The rule is REJECTED; `MTG_BP_CONDEMN_LAND` stays default OFF.

Two collateral results worth more than the rejection:

* **`cond_tail` is now ~INERT on the shipped list** (92/99 games changed, t under 1), where on the
  archived variant it was the lever that recovered 24 of 29 unrecoverable lines. The SCOPE WARNING
  below was right to insist every card-level number be re-derived.
* **`ng` is a clean adoption CANDIDATE** and it is the USER's "delete all greedy" direction measuring
  *better*, not merely neutral: 12 faster : 2 slower and 9 faster : 2 slower, t = -2.67 / -2.11.

#### The escalation census, and what it actually says

138 disagreeing games, 552 single-game jobs at 100x budget and at 100x budget + 1 ply, BOTH arms
escalated. Of the 92 games `cond_land` loses at shipped settings, only **6 survive both cells**; 86
are budget/depth churn. So the rule is NOT deleting reachable lines wholesale -- which is what makes
the residual interesting rather than damning.

#### ROOT CAUSE (hold gi=1293, cond wins T5, cond_land T6, survives 100x AND +1 ply)

Repro: `--seed 1051294 --game-index 1293`. T3 diverges, and the two lines are:

* `cond`: Impolite Entrance -> **Fists of Flame** -> *Mountain* -> Goblin Instigator. Win T5.
* `cond_land`: Impolite Entrance -> *Mountain* -> Fists of Flame -> Fists of Flame. Win T6.

**The rule fired exactly as specified and the land moved earlier. That is the problem.** Both lines
play the same Mountain on the same turn; the only difference is that the pin forces it BEFORE Fists,
which makes one more mana available earlier -- and the search spends it on a second Fists of Flame
instead of the Goblin Instigator. In a Zada deck a body is worth more than a second pump, because
every later solo-target trick is copied once per creature.

So the failure is not land timing at all. **The pin exposes a weakness in the CAST ORDER**: with the
extra mana the search casts a rank-16 payoff twice in preference to a rank-10 creature it holds. The
USER's premise ("playing the land at its slot wins just as fast") is false here for a reason that
lives one layer away from the land rule, and the next order-side question is that, not the drop.

#### A HYPOTHESIS THAT WAS WRONG, recorded so it is not re-proposed

Before instrumenting I inferred a "bug 9": that a land DRAWN at one breakpoint reads as
already-in-hand at a NESTED breakpoint, since the drawn-card exemption is per-breakpoint while the
drop's slot passes once a turn. It was built (a turn-level snapshot inherited by nested scopes) and
it is a **provable no-op** -- identical to four decimals over 2,000 games -- because the nested inline
path does not rebind `CantripOrderScope` at all, so the bound snapshot is *already* the outermost
one. Reverted rather than shipped as a dormant guard: the comment would have asserted a defect the
engine does not have. `MTG_CONDEMN_WHO` on gi=1293 is what settled it (the Mountain was in hand from
turn start, so the rule was firing correctly) -- INSTRUMENT, don't read call graphs, a fourth time.

### The legitimate excuse looks like ONE rule, not several

Of 23 turns where a trick is cast ahead of a body, 12 play a land immediately afterwards -- the
trick's draw supplied the land drop the rest of the turn needed. Same shape as Gold Rush's Treasure:

> **A card may be cast ahead of its slot only when doing so supplies a RESOURCE the rest of the line
> needs and cannot otherwise get** -- mana or colour (Gold Rush's Treasure), or a card / land drop (a
> cantrip's draw; Scale the Heights' `grants_extra_land_drop` states it outright).

One rule, several instances, which is what *"no legitimate excuses"* wants. Note it is the same
mechanism `CastOrderFallbackRanks` + `FirstUnpayablePos` already implement for Gold Rush -- walk a
card earlier only while the line cannot otherwise be paid.

### SCOPE WARNING -- the card-level numbers above are on the ARCHIVED decklist

Everything measured in the bug-8 section used `decks/Mirrorwing Dragon/v1-twinflame-anger`, which is
the archived variant. The shipped list (and the one `test/regression_cases.sh` uses) is the top-level
deck, which **drops Twinflame, Expedite, Ancestral Anger and Scale the Heights** -- every card in the
condemnation tables, Twinflame alone being 103,698 of the firings. The STRUCTURAL findings should
carry (the rank test enforcing an order `OrderingOpaque` refuses to apply; 48.2% of condemnations at
plans whose only cast is the site; "no land drop" being an unmodelled play). Every card-level number
must be re-derived on the shipped deck before it is used.

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
