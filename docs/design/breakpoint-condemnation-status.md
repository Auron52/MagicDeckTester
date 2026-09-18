# Breakpoint condemnation: NINE bugs; bug 9 is the one that is not a bug in the filter at all

**Status (updated 2026-09-16): BUG 9 is the headline and it is a different KIND of defect from 1-8.**
Those were all the filter inferring a decline that never happened, and each was fixed by making its
picture of "considered and passed over" more accurate. Bug 9 is a case where the decline is
completely real and condemning is STILL wrong, because **the continuation slot is exclusive**: a
cast can be worth making purely to deny the slot to something else, and such a cast has no sibling
line by construction. See the 2026-09-16 section at the end for the mechanism, the exact repro
(gi=1357 -- Skred is goldfish-inert and occupies the slot so a freshly-drawn Rimefeather Owl cannot
tap a Druid out of an exact-lethal attack), the fix (`MTG_BP_CONDEMN_NEW_OPTION`, 0 regressions in
2,000 paired games), and two corrections to the record -- including that the widely-quoted
"+0.0000, 5 better / 5 worse" condemnation A/B is STALE. Note also that "unrecoverable" now has a
fixed meaning here at USER direction: **not recoverable at budget 0 AND depth 8**, which the census
cells do not establish.

**Status (updated 2026-09-03):** the four type-exemptions named below (`_MANA_EXEMPT`,
`_RITUAL_EXEMPT`, `_TUTOR_EXEMPT`, `_REDUCER_EXEMPT`) were DELETED outright at USER direction
(87871ac4, 2026-08-28 — "I don't want any general exemptions") and replaced by the card-agnostic
`BpTurnManaSettled` test (`MTG_BP_CONDEMN_MANA_SITE_EXEMPT`, default ON);
`MTG_BP_CONDEMN_SEARCHED_ONLY` is also default ON since 9626afe8. `MTG_BP_CLASSIFY` remains
default OFF. The flag inventory below predates the deletion.

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

## 2026-08-28: THE BREAKPOINT FILTER NEVER GOT THE DECISION-SPACE GATE

USER, on being told condemnation was worth only 0.04% of Hinata's eager-site-3 cost: *"No matter
what the result of condemnation on Hinata it should reduce the overall work significantly. If it's
not doing that, we already have a bug."* That was right, and the bug is a missing gate.

### The measurement that could not previously be taken

`g_condemn_drops_total` / `g_condemn_drops_greedy` belong to the **main-phase** filter
(`MainPhaseFilterActive`), so a breakpoint-condemnation run reported `condemn_drops=0` and the
question "is this pruning the search, or just deleting the playout's line?" could not be asked of the
breakpoint filter at all. Adding the counters (`MTG_ROLLOUT_STATS` -> `[rollout-stats] bp_condemn_*`)
answers it immediately -- Hinata, full order + site 3, 40 games:

    bp_condemn_seen=978735 drops=1202 drop_rate=0.0012 (greedy=1080 searched=122 greedy_frac=0.8985)

**90% of drops land in the GREEDY collector**, i.e. the rollout leaf.

### Why that is backwards, in the words of the filter that already knows

The main-phase filter has carried `MTG_CONDEMN_SEARCHED_ONLY` since 2026-08-21 and ships it DEFAULT
ON, with its own comment calling the `=0` hatch *"the BROKEN arm, not a neutral one"*. Its rule:

> condemn only in the DECISION SPACE. Two readers qualify -- a SEARCHED collect
> (`g_search_candidate_enum`): a drop deletes a branch the ranker would have expanded, which is real
> pruning; and the EXECUTOR (`g_condemn_root_turn < 0`): committed play. The rollout LEAF is
> neither: it is not deciding, it is ESTIMATING. Restricting an estimator cannot improve the
> decision -- it only makes the value pessimistic.

Its measured cost when that gate was off: 96.6% of drops in the greedy pass, buying turn_steps +4.0%
and rollout calls +4.4% for a 0.2% candidate reduction. The breakpoint filter has the same disease at
89.9%, which is why its saving is ~0 and why it can be a quality loss at the same time.

`MTG_BP_CONDEMN_SEARCHED_ONLY` (default OFF, heurarm-poolable) applies the identical two-reader rule.

### Corollary: this reframes "condemnation is nearly inert on Hinata"

The section above attributes the inertness entirely to rank structure (sites at rank 7, near the
front of the order). That remains true of the *surface*, but it is not the whole story: of the drops
the rule DID find, ~90% were spent where they could not prune anything. Rank structure caps how many
drops exist; the missing gate wasted most of the ones that did.

## 2026-08-28: `MTG_BP_SITE3_DEFER` IS INERT, AND NOT BECAUSE OF THE DIG BYPASS

Digest proof (ga2, 2,500 games x 2 disjoint blocks): `s3` and `s3_eager` are identical,
`43e12252f5347b5c` on train. The lever does nothing, so the recorded "open site 3 DEFERRED, not
eager" recipe is void -- site 3 costs +0.0228 hold / +0.0268 train at 1.51x work units with no
working mitigation.

**Cause: two different masks.** Site 3's cost is `class_on` in `bp_searched_plan`, which reads the
FULL `BpSiteMask()` and gates the expensive `EnumerateBreakpointPlans`. `MTG_BP_SITE3_DEFER` clears
bit 3 from `BpWave0SiteMask()`, which selects only WHICH BASE PLANS GET VARIANTS APPENDED in wave 0.
The lever never touched the mechanism that costs. A real deferral has to make the class eligible only
in the deferred wave phase -- a design change, not a flag.

**A hypothesis built and reverted:** that the `dig_bp` bypass in `AppendBreakpointVariants`
(`if (!dig_bp && (opens & sites) == 0) continue;`) overrode the wave-0 mask. It cannot:
`BpDigFanoutPending` returns false unless bit 4 is already in `sites`, so rewriting it as
`opens |= 1<<4` is a PROVABLE no-op -- confirmed byte-identical, then reverted rather than left in
the tree as dormant code. A comment at the site records this so it is not re-derived.

## 2026-09-16: BUG 9 -- THE CONTINUATION SLOT IS EXCLUSIVE, so the redundancy premise is false

Bugs 1-8 were all the same shape: the filter condemned a card it had no right to condemn, because
the *decline* it inferred never happened (no order awareness, a tie read as earlier, an accelerant,
a rank test reading a slot the plan never reached). Every one of them was repaired by making the
filter's picture of "was this considered and passed over?" more accurate.

Bug 9 is not that. Here the decline is real -- the card was in hand, its slot precedes the site, the
plan reached it and did not cast it -- and condemning it **still** deletes the win. The premise
itself is wrong.

### The premise, and what it actually compares

Condemnation's soundness argument is REDUNDANCY, in the USER's own words: *"there is a line that
plays it and we want to make that the line."* Dropping X from the continuation costs nothing because
a SIBLING plan casts X at its proper position.

That compares the wrong two things. **Casting X in the CONTINUATION also declines everything else
the continuation could cast. Casting X as a PLAN cast does not** -- the trailing pass still runs
afterwards and still offers the rest. The continuation slot is *exclusive*; a plan slot is not. So
the sibling line is not equivalent to the condemned line, and dropping X does not fall back to "cast
nothing". It hands the slot to whatever outranks X.

At a tap-draw site, what outranks X is very often **the card the site just drew** -- a card that was
not in hand when the plan declined X, so the decline says nothing whatever about the comparison that
is now being made.

### gi=1357 (930000 block), exact

Turn 8. Hand is four Skreds. Scrying Sheets' `{1}{S}`, `{T}` tap-draw puts Rimefeather Owl into hand.

* Condemnation drops Skred. Every gate passes legitimately: Skred was in hand before, it is not a
  plan cast, and the whole cast order precedes an ACTIVATED site (site 8 runs in the trailing pass,
  so `BpSlotIsAfterSite` is false for every card in hand).
* The continuation casts the Owl instead. The Owl's `{5}{U}{U}` taps a Boreal Druid **(1/1)** for
  mana.
* Base attack: 2 Boreal Druids + Rimescale Dragon (5/5) = 1+1+5 = **7, exact lethal against 7 life**.
  Condemned attack: 1 Druid + Dragon = **6**. The game is never won.

**Skred is goldfish-inert.** It does not win the game and casting it changes no board total. Its
entire function in the winning line is to OCCUPY THE SLOT so the Owl cannot -- which is precisely
the exclusion the redundancy premise cannot see. A cast whose value is that it *denies the slot to
something else* has no sibling line, by construction.

Not a search artefact: `MTG_BP_CLASSIFY=0` and `MTG_BP_NODE=0` lose the same game the same way
(greedy casts the Owl), and `MTG_BP_EMPTY_ARM=1` -- which makes "cast nothing" a scored candidate --
still loses it. The hypothesis that an empty continuation would recover it was tested and **refuted**.

### The fix: `MTG_BP_CONDEMN_NEW_OPTION` (`BpSiteAddedAPayableOption`)

Spare the drop exactly when the site put a new **payable** card in hand. That is the condition under
which the slot is contested by an option the plan could not have weighed. When the site draws
nothing, or draws something we cannot cast, the slot really does fall back to "cast nothing" and the
premise holds -- so we still drop, and the prune keeps most of its volume.

Card-agnostic and route-agnostic. No type exemption, no per-card clause (USER 2026-08-28: *"I don't
want any general exemptions"*).

**Do not "tighten" it with a cast-order rank gate.** The obvious refinement -- spare only when the
new card OUTRANKS the candidate -- looks like it would recover more of the prune's saving. It is
unsound: the continuation is SEARCHED, not rank-ordered, so a lower-ranked new card can still take
the slot whenever the alternatives are worse. The broad test is the conservative one and conservative
here means keeping the action.

### Measured (2,000 paired games, Snow, play settings d5/b20, one pooled 8-arm batch)

| arm | Δ vs base | better/worse | units | regressions |
|---|---|---|---|---|
| `cond` | +0.0000 | 3 / 3 | **0.9940** | 487, **1357**, 1553 |
| `condno` (`cond` + new-option) | −0.0010 | **2 / 0** | 1.0017 | **none** |
| `be` (`MTG_BP_BASE_EMPTY`) | −0.0135 | 29 / 2 | 1.0098 | 237, 1384 |
| `be_cond` | **−0.0155** | 33 / 2 | **1.0031** | **1357**, 1384 |
| `be_condno` | −0.0145 | 31 / 2 | 1.0113 | 237, 1384 |

**State the trade honestly: the rule gives the prune's units saving back** (0.9940 → 1.0017
standalone) and about a third of its quality gain. What it buys is that `condno` regresses **0 of
2,000**, and `be_condno`'s regressions are exactly `be`'s own. Note also that these units figures are
budgeted, and a budgeted number says how the budget was SPENT, not what the prune costs -- see the
cost section above. An unbudgeted read is still owed.

**`be` here is `MTG_BP_BASE_EMPTY`, the PRE-EXISTING lever, and not `MTG_BP_EMPTY_ARM`.** The two are
easy to conflate and were conflated once already in this arc: base-empty deletes the greedy
continuation for BASE plans only, while the empty ARM emits "done acting in this phase" as a scored
wave-0 candidate at every breakpoint index. **`MTG_BP_EMPTY_ARM` carries NO measurement** -- it was
added after this batch ran and appears in none of these rows. Do not read the `be`/`be_cond`/
`be_condno` numbers as evidence for it.

### Two corrections to the record

1. **The pre-2026-09-16 condemnation A/B was stale.** The often-quoted "+0.0000, 5 better / 5 worse"
   predates `MTG_BP_CONDEMN_PLAN_CAST` becoming default-on. That guard alone repairs gi=206 (8→7),
   gi=1847 (7→6) and gi=1935 (6→5), each confirmed by toggling it off and watching the loss return.
   It does NOT repair gi=1357: it cuts that game from 1,663 drops to 79, and the four survivors are
   `plan_n=1`. **"Volume is not harm" has now been the wrong predictor five times in this arc.**
2. **`plan_n=1` is not a discriminator.** It is 78% of the regressions' drops but 75% of the whole
   population. Gating on it would disable condemnation, not target the harm.

### Terminology, fixed at USER direction

USER 2026-09-16: *"To be clear unrecoverable means not recoverable at unlimited budget (0) and depth
8."* The census cells in `test/tools/snow_ab/gen_condemn_escalate_manifest.py` are 100x budget and
100x budget + 1 ply. Those **SCREEN** for the label cheaply across every disagreeing game; they do
not confer it. A survivor there is a candidate needing its own `--depth 8 --budget-ms 0` cell.

The converse trap is the more dangerous one, because it fails silently in the direction of a false
clearance: **a d8/b0 Snow game can run for many hours (44 h repros are on record) and a cell that
never completes settles nothing.** Report the strongest bound actually measured; never let a missing
cell read as a recovery.

## 2026-09-16: WHY THE COST WIN IS ~NOTHING ON SNOW -- three multiplicative limits, and a ceiling

Condemnation is a PRUNE, so "how much does it save?" is the question it lives or dies by, and the
answer on Snow is *almost nothing* -- **0.60% unsound, 0.03% sound**. That has now been mis-read
twice in this arc (once as "it buys nothing, +0.72% dearer", once as "dead lever"), so this section
records the decomposition rather than the headline. 200 games, d5/b20, same seeds, `MTG_ROLLOUT_STATS`.

### Limit 1 -- REACH: the filter can only touch 17.4% of the work

| site | share of all units |
|---|---|
| `la_cand` (root candidate loop) | 35.1% |
| `rollout_step` | 23.7% |
| `greedy_fallback` | 22.9% |
| **`la_bp_wave` + `fs_bp_wave`** | **17.4%** |
| `fs_pre` | 0.8% |

Condemnation only fires inside breakpoint continuations. **82.6% of Snow's search cost is somewhere
the filter does not run at all.** This is structural rather than an artefact of the shipped depth:
the breakpoint share measures 16.3% / 17.2% / 15.8% at d3 / d5 / d7. It does not grow with depth --
`la_cand` grows faster.

### Limit 2 -- YIELD: only 24% of consultations even reach the test

Of 3,335,374 consultations (`MTG_BP_CONDEMN_WHYNOT`):

| gate | count | share |
|---|---|---|
| `noplancast` -- the plan cast nothing, so it declined nothing | 1,267,205 | **38.0%** |
| `notdecision` -- a rollout leaf (a drop there prunes nothing, by design) | 644,998 | 19.3% |
| `managrew` | 539,804 | 16.2% |
| `plancasts` -- the plan casts it itself | 75,967 | 2.3% |
| **reached the dominance test** | 803,344 | 24.1% |

Of those that reach, **46% are unpayable anyway** (366,344) -- dropping them removes a candidate the
search was never going to cast -- and 156,905 more are not dominated. 280,095 real drops remain, an
8.4% drop rate, which cuts the breakpoint sites by 5.5%.

**The largest blocker is soundness, not slack.** `noplancast` is the plan_n=0 rule. The bulk of what
condemnation cannot prune, it cannot prune because pruning it would be wrong.

### Limit 3 -- REINVESTMENT: 38% of the saving is spent before it is banked

| | gross saving at bp sites | spent elsewhere | net |
|---|---|---|---|
| `cond` | −320,235 | **+121,661** (`la_cand` +103,476) | −198,574 = **−0.60%** |
| `condno` | −28,308 | +19,518 (69%) | −8,790 = **−0.03%** |

0.174 reach x 0.055 cut = 0.96% gross, x 0.62 retained = **0.60% net**. That is the whole number.

### ...and raising the budget does NOT recover it

The obvious next move -- "measure it at a higher budget, where there is slack" -- was tested and
fails. Ladder at d5, 300/300/250 games:

| budget | base units/game | growth vs prev | budget step | `cond` | `condno` |
|---|---|---|---|---|---|
| 20 ms | 166,610 | -- | -- | 0.9948 | 1.0003 |
| 50 ms | 363,593 | **2.18x** | 2.50x | 1.0012 | 1.0011 |
| 120 ms | 828,156 | **2.28x** | 2.40x | 1.0011 | 1.0023 |

**Units grow in proportion to the budget**, so the budget is fully binding at every rung: the search
never completes depth 5, it spends whatever it is given, and freed work is immediately reinvested.
The ratio sits at ~1.00 regardless. `cond` stays cheaper on most GAMES (202/61 at b50) while being
dearer in TOTAL -- the tail sets the sign, exactly as the cost section above warns.

**A prune only returns real work where the search would otherwise FINISH.** Under any shipped budget,
condemnation's only available benefit is QUALITY -- spending the same units better. Cost-neutrality
here is structural, not a defect to engineer away.

### ...and UNBOUNDED: the prune WINS BIG, but the SOUNDNESS GUARD turns it into a loss

The obvious corollary of the paragraph above is that the regime where a prune *can* pay is UNBOUNDED
search, which is exactly what value-leaf label generation runs (USER 2026-09-16: *"it could help with
cost for parts of the value-leaf generation"*). **That was measured and it is false, by a wide
margin.** An earlier revision of this section recommended it as worth trying; it is corrected here
rather than deleted, because the reasoning behind it is sound and someone will re-derive it.

**CORRECTED 2026-09-17.** An earlier revision of this section read the unbounded result as
"condemnation is dearer, the value-leaf idea is refuted". That measured the FIXED filter without
realising it (`MTG_BP_CONDEMN_NEW_OPTION` had just been flipped default ON, so an arm passing only
`MTG_SNOW_CONDEMN=1` silently carried the guard). The UNFIXED filter is **16.2% CHEAPER** unbounded.
The USER's prediction that unbounded search is where the prune pays was RIGHT.

Snow, 10 games, depth 2, `--budget-ms 0` (the only unbounded depth that terminates on this deck):

| arm | drops | units | vs base | cache lookups | hit rate |
|---|---|---|---|---|---|
| base (`MTG_SNOW_CONDEMN=0`) | 0 | 34,413,950 | -- | 27,744,829 | 98.06% |
| condemnation **UNFIXED** | 817,461 | 28,832,176 | **-16.2%** | 23,103,744 | 97.40% |
| condemnation **FIXED** (shipped) | 159,178 | 39,400,239 | **+14.5%** | 32,590,957 | 97.64% |

**IT IS NOT LOST MEMOISATION SHARING. THAT WAS TESTED DIRECTLY AND REFUTED (2026-09-17).** This
section previously asserted the cost was cache-key width: the filter's answer depends on WHICH CARDS
THE PLAN ALREADY CAST, so `BpEnumBuildKey` folds the plan's cast set, so sibling plans reaching one
breakpoint state cannot share an enumeration. The reasoning is real -- the fold is required for
soundness, and it does cost misses -- but it is nowhere near the magnitude of the penalty.

The test (USER's idea, 2026-09-16: *"Is there a way we can reduce what we cache on to be less than
the full plan?"*). There are exactly three reads of `g_bp_plan_casts` and all three are order- and
multiplicity-blind: `BpPlanCasts(h)` is only ever called with a name hash taken from a card in the
CURRENT hand, `BpPlanHasTail(ap)` walks `ap.hand` and asks that per card, and `BpPlanMadeACast()` is
`!empty()`. So the minimal sound key is `(cast set n hand name-hashes, is-the-set-empty)`, and the
verbatim plan-order fold was far finer than anything observable. Both narrowings were built and
measured (`MTG_BP_KEY_CASTS_WIDE` restores the old fold, so the A/B is one binary), Snow 10 games
d2/`--budget-ms 0`, one pooled batch per arm at 4 threads:

| arm | key folds | units | vs base | misses | lookups |
|---|---|---|---|---|---|
| base (`MTG_SNOW_CONDEMN=0`) | -- | 34,414,522 | -- | 539,191 | 27,745,176 |
| `wide` -- verbatim plan order (old) | list, ordered | 39,412,487 | **+14.52%** | 768,804 | 32,590,437 |
| `cond` -- canonical set | sorted unique + empty bit | 39,376,994 | **+14.42%** | 762,710 | 32,573,177 |
| `condN` -- + hand intersection | minimal sound key | 39,346,154 | **+14.33%** | 735,914 | 32,543,489 |
| `condNH` -- + `MTG_BP_KEY_NARROW` | " + snapshot narrowed | 39,346,154 | +14.33% | 735,914 | 32,543,489 |

**The provably-minimal key recovers 0.19 of the 14.52 points -- 1.3% of the penalty.** Misses do fall
(-4.3%), so the narrowing works as designed; it simply is not the mechanism. `condNH` is
byte-identical to `condN`, so the pre-draw-hand narrowing adds nothing once the cast set is narrowed.
**Do not re-open "narrow the enum cache key" as a cost item -- it is measured and it is worth ~1%.**

**WHAT THE DATA DOES SAY, and it is a different mechanism: the filter raises the number of
enumerations DEMANDED, with the play unchanged.** Lookups go 27,745,176 -> 32,590,437 (**+17.5%**)
while units go +14.5%, i.e. units track lookups (units/lookup 1.240 base vs 1.209 wide) and the hit
rate barely moves (98.06% -> 97.64%). `la_bp_wave` +17.9% and `la_cand` +14.8% are both SEARCH-work
counters, so the search really is doing more scoring and more wave work -- it is not cache overhead
and not the guard's own arithmetic. And it does all of it for nothing: on this cell **all five arms,
`base` included, emit one identical per-game digest**, so 158,860 drops changed not a single
decision.

That reframes the question from "why are hits lost?" to "why does pruning options make the search
REQUEST more enumerations?". The leading hypothesis, NOT yet measured, is the breakpoint wave's WIDTH
mechanism (the W variants / deferred waves): if the wave backfills toward a width TARGET, then
removing options from each continuation makes it issue more requests to hit the same width, which
would produce exactly this signature -- more lookups, same decisions. The test is to sweep the wave
width and see whether the +17.5% moves with it. See `docs/design/breakpoint-width-deferred-waves`
material and the `la_bp_wave` partition.

Method notes worth keeping, both learned the hard way here:
  * **A hit-RATE test does not reveal lost sharing** (98.06 -> 97.64 looks like nothing); miss COUNT
    and lookup COUNT do. Reading a flat hit rate as exoneration was a mistake made in this file.
  * **`units_total` is deterministic only for a FIXED thread/pool shape.** It counts re-derivation
    work, and cache residency is per-thread, so the same config measured at a different worker count
    gives slightly different units (base here is 34,414,522 at 4 threads vs 34,413,950 on the earlier
    1-worker direct-CLI run). Arms must share the thread count; cross-run absolute comparisons do not.

So there are two independent effects: **pruning subtracts work and scales with drop count**, while
**lost sharing adds work and is a FIXED toll for having the filter on at all**. Unfixed drops 817,461
and the prune wins (-16.2%); the new-option guard spares 81% of those, gutting the benefit while the
toll is unchanged (+14.5%). A 30-point swing caused entirely by the soundness guard.

**THE REACH INVERTS, and that part stands.** `la_bp_wave` is 17.4% of units at d5/b20 but **68% at
d2/b0** -- unbounded, the breakpoint enumeration IS the search, which is why anything that perturbs
enumeration dominates the unbounded cost. The cache-key fold is still REQUIRED for soundness
(`g_bp_plan_casts` is bound `if (CantripOrderEnabled() || classify)`, and without the fold a plan is
served a sibling's already-condemned list) -- it is just not where the cost lives.

The two sentences that used to end this paragraph -- *"Finer keys mean fewer hits, and when
enumeration is 68% of all work the lost hits cost far more than the pruning saves. A 32x cache
recovers only 30% of the penalty (consultations 8,308,740 -> 7,070,001), so the rest is intrinsic."*
-- are **withdrawn**. The first is refuted by the fold table above (the minimal key recovers 1.3% of
the penalty, not most of it). The second measured CONSULTATIONS, which is not a cost: consultations
fall whenever re-derivations fall, so it moves with cache residency while saying nothing about units.

**RETRACTED 2026-09-17 -- this paragraph compared a config with ITSELF.** It used to read: *"The
drops are not the cost, the APPARATUS is. `condno` drops 85% fewer candidates than `cond` and costs
the same (ratio 1.1415 vs 1.1406 on 40 games). Anything that scales with drop count is therefore
ruled out as the mechanism."* In that 40-game manifest (`logs/snow_perf/unb2.manifest.json`) the
`cond` job set only `MTG_SNOW_CONDEMN`, and `condno` set `MTG_SNOW_CONDEMN` +
`MTG_BP_CONDEMN_NEW_OPTION` -- but `MTG_BP_CONDEMN_NEW_OPTION` had already been flipped DEFAULT ON,
so both jobs ran the FIXED filter. The proof is in the run's own output: the two jobs report the
**same play digest** `f56c993276c7d5c8` (base is `080b11af41bf9ff9`). Two arms that cost the same
because they ARE the same arm rule nothing out, and the conclusion drawn from it -- that cost does
not scale with drop count -- is exactly BACKWARDS: the corrected table above shows the drop count is
what separates -16.2% from +14.5%.

Generalise the trap, because it has now cost three measurements in this file: **once a lever is
flipped default ON, an arm that names it is no longer distinguishable from an arm that omits it.**
Every arm must set every lever it depends on EXPLICITLY to `0` or `1`, and the cheap tell is the play
digest -- two arms meant to differ that report one digest are one arm.

Two consequences worth stating plainly:
  * A deck that opts into condemnation pays ~10-15% MORE for its value-leaf generation, because
    `NEVER_CONDEMN` in `scripts/valueleaf.sh` is CELL condemnation -- an unrelated mechanism -- and
    does not gate this filter.
  * **The fix is NOT to pin the filter off for generation.** The value leaf must be fitted to the
    play the deck actually ships; generating without a filter that play uses fits the model to a
    deck we do not ship, which is the same class of error as a profile-less generation. The choice
    is between paying the generation cost and not shipping the filter -- not between them.

Caveat on the magnitude: depth 2 was forced by tractability, and while the MECHANISM is
depth-independent (it is `budget->Unlimited()` plus cache-key width), the 14% figure is not verified
at generation depth.

Second caveat, on the SAMPLE rather than the mechanism: these are 10-game sums on a deck with
extreme per-game cost variance, and they are not evenly sourced. The unfixed arm's own log
(`logs/snow_perf/d2b0_cond_unfixed.log`) reports `SLOW-GAME 501891ms gi=8` and `129173ms gi=9`
against a ~10-minute total, so **two of the ten games are most of the measurement** and gi=8 alone
could carry the -16.2% by itself. `units_total` is a sum with no per-game breakdown, so this cannot
be decomposed from the existing logs -- it needs one process per game. Treat -16.2% as "the prune
wins on the games that dominate unbounded cost", which is the operative claim for generation anyway,
rather than as a per-game expectation.

### The ceiling: there is no headroom to chase

`PEER=0`, and the census's own ceiling line reads
`realistic max ~280,095 (1x today), hard upper bound 280,095 (1x)`.

**Condemnation is already extracting 100% of what is available to it on Snow.** A finer cast order
recovers nothing, because site 8 is an ACTIVATED site: every card in hand ranks before it, so nothing
is ever peer-blocked. Do not open "tune the Snow cast order to feed condemnation" as a work item --
it is provably worth zero.

### Why the SOUND version gives even that back

`condno` drops 40,618 where `cond` drops 280,095: the new-option guard spares **231,351, i.e. 85% of
all drops**. That is a fact about this deck, not about the rule. Snow's only breakpoint class is site
8, the `{T}` tap-draw of Scrying Sheets / Frost Augur -- the site's whole purpose is to draw a card,
and Snow's curve is cheap, so "the site put a new payable card in hand" is true nearly every time it
fires. **On a deck whose only breakpoint is a draw, condemnation and its soundness guard are close to
mutually exclusive.**

## 2026-09-17: THE ALL-PATHS RULE -- measured. The COST case is dead; the SOUNDNESS case is real, small, and needs a design decision

USER's proposal (2026-09-16, restated 2026-09-17): *"only condemn in cases where all of the lines
that reach that state condemn"*, with the cost argument *"It should be a cost lever vs no
condemnation assuming we can make it work, since it has the same or fewer distinct states."*

The rule is: condemn X at breakpoint state S only if EVERY line reaching S would condemn X --
i.e. intersect the condemn sets, equivalently keep the union of the kept sets. It makes the verdict
path-independent, so the bp-enum key would no longer need the plan-cast fold.

### KEY WIDTH is not the filter's cost -- but the cost itself is still UNEXPLAINED

Read this subsection as "the cache is not the mechanism", NOT as "the cost is real and intrinsic".
The USER's acceptance spec below says a correct implementation adds no misses at all, and the
measured work increase is therefore a defect that is still at large.
Snow, 10 games, seed 930000, d2/`--budget-ms 0`, one pooled batch per arm at 5 threads
(`logs/snow_perf/ceiling.sh`, run `c4`):

| arm | units | vs base | bp-enum misses | vs base |
|---|---|---|---|---|
| base (`MTG_SNOW_CONDEMN=0`) | 34,414,324 | — | 537,091 | — |
| `wide` (shipping fold) | 39,424,071 | +14.56% | 768,814 | +43.1% |
| `casts` (`MTG_BP_KEY_CASTS_NONE=1`) | 39,309,220 | +14.22% | 649,295 | +20.9% |
| `snapnone` (`MTG_BP_KEY_SNAPSHOT_NONE=1`, all five folds) | 39,403,885 | **+14.50%** | 645,923 | +20.3% |

**Key merging recovers 0.06 of 14.56 points (0.4%).** All four digests identical
(`1922378d4c0c`), so this cell has NO POWER as a play test -- units and misses only.

### THE SPEC, AND WHY THE PARAGRAPH THAT USED TO BE HERE WAS WRONG

**USER, 2026-09-17, and this is the acceptance criterion for the whole feature:** *"There should be no
additional misses if implemented correctly."* … *"We should miss exactly where baseline misses and hit
otherwise. Our only extra work is checking condemnation status and deciding what we need to
implement."*

So a correct condemnation has, against the condemnation-OFF arm:
* **misses identical** -- the same key, therefore the same distinct-state set, therefore the same
  miss set. Not "similar": identical.
* **hits identical** -- every lookup base hit, this hits.
* **the only extra work is the condemn check itself** (a per-candidate predicate), plus whatever the
  smaller candidate lists SAVE downstream.

Measured against that spec, with `MTG_BP_KEY_SNAPSHOT_NONE=1` (key byte-for-byte base's):

| quantity | base | filter on, base's key | vs base |
|---|---|---|---|
| bp-enum misses | 537,091 | 645,923 | **+20.3%** |
| bp-enum lookups | 27,745,046 | 32,580,099 | **+17.4%** |
| cache clears | 64 | 78 | **+21.9%** |

**ALL THREE ARE BUG SIGNATURES, NOT COSTS, AND THE PRIOR VERSION OF THIS SECTION RECORDED THEM AS
PHYSICS.** It said the extra derivations "are therefore the search visiting more distinct breakpoint
states … No key-merging scheme of any kind recovers them." That was an inference presented as a
measurement, and it contradicts the USER's standing doctrine on this exact feature (2026-08-28: *"No
matter what the result of condemnation on Hinata it should reduce the overall work significantly. If
it's not doing that, we already have a bug."* -- the doctrine that found bug 8). **A prune cannot
raise lookups.** Removing candidates from a continuation list can only shrink the tree, so +17.4%
lookups is something re-expanding work the prune removed.

**THE TWO CANDIDATE EXPLANATIONS, SEPARATED (`logs/snow_perf/missspec.sh`, run `ms1`).** The cache is
**clear-on-full at 8192 entries, not LRU** (`if (cache.size() >= cap || !Fits(psz)) cache.clear();`),
so each wipe forces up to 8192 re-derivations. The cap is documented result-neutral, which is what
makes eviction separable from distinct-state growth. Snow, 10 games d2/b0, `MTG_BP_ENUM_CACHE_CAP`
raised 8192 -> 400,000:

| arm | misses | clears | units | vs base |
|---|---|---|---|---|
| base | 505,795 | 3 | 34,410,386 | — |
| **base_rep** (deliberate replicate) | 509,193 | 4 | 34,411,512 | noise floor |
| `snapnone` (filter on, base's key) | 536,242 | 4 | 37,838,450 | **+9.96%** |
| `perpath` (filter on, full key) | 648,523 | 4 | 37,753,415 | +9.72% |

**ABOUT A THIRD OF THE "COST" WAS EVICTION CHURN.** At the default cap the filter measured +14.5%
units; at a cap where clears fall from 64 to 3-4 it measures **+9.96%**. Misses at equal key fall
from +20.3% to **+6.0%** (30,447 over base, against a measured noise floor of 3,398 from the
replicate). The +20.3% figure this document previously carried was mostly the 8192-entry wipe.

**WHAT SURVIVES, AND IT STILL VIOLATES THE SPEC.** At equal key and matched clears the filter still
costs +9.96% units, +6.0% misses and **+12.2% lookups** (27,742,138 -> 31,132,453). A prune cannot
raise lookups. The leading suspect is unchanged and still **UNMEASURED: the bp wave's width
backfill** -- `units.la_bp_wave` +17.9% and `units.la_cand` +14.8% move with it. Sweep the wave width
and see whether the lookup delta follows.

**AND NOTE THE UNITS/MISSES DISSOCIATION, which is itself a clue.** The five key folds cost 112,281
misses (`perpath` 648,523 vs `snapnone` 536,242, +20.9%) while moving units by −0.2%. So misses are
not the cost driver either; whatever is spending the units is not paid for per derivation. That is
consistent with the backfill hypothesis and inconsistent with any cache explanation.

**A CAVEAT ON THE ZERO: clears reach 3-4, not 0, and the count cap cannot drive them lower** -- the
byte budget (`plancache::Fits`) also triggers a wipe and `MTG_BP_ENUM_CACHE_CAP` does not control it.
The arms are matched at 3-4 so the comparison is clean, but a run claiming "misses ARE the
distinct-key count" needs the byte budget raised too.

**DO NOT RECORD +14.5% AS "THE FILTER'S COST".** It is an unexplained work increase in a prune, which
by this project's own doctrine means a defect is still hiding. What IS established is narrower: **key
width is not the explanation** (all five folds off recovers 0.4%), so whatever causes it is not cache
fragmentation.

**METHOD NOTE, because this took three attempts.** Condemnation does not add ONE fold to the
bp-enum key; binding `CantripOrderScope` adds FIVE: the pre-draw hand snapshot (every card NUMBER in
it), the deferred-Karoo reservation, the mana-source count, the plan cast set, and the site plus how
it was reached. Condemnation-OFF binds none of them. The first "ceiling" arm dropped the cast set
alone and was reported as "the filter on base's key" -- it was a partially-merged key and bounded
nothing. USER: *"The misses should be the same as no condemnation. I sense a bug."* The second
attempt guarded four of the five and missed the SITE fold, which on Snow is the split that matters
(Scrying Sheets 103 vs Frost Augur 111 at the same state). That one was caught by the arm's own
self-check. **An arm that claims to reproduce another arm's key must assert the miss count, not the
units.**

### RESOLVED (2026-09-17, run `kc1`): the filter reaches FEWER states. The spec violation is LOOKUPS, alone.

Everything above measures states through `misses`, and `misses` cannot answer the question. The USER's
spec is a **set relation with a direction** -- *"we should miss exactly where baseline misses"*, and
*"it is possible for us to do less work if there are lines we never run … but there should be none the
other way"* -- while a miss count conflates three things: residency (clear-on-full, not LRU), the byte
budget (`plancache::Fits`, which `MTG_BP_ENUM_CACHE_CAP` does not control, which is why clears stall at
3-4), and per-thread duplication (the cache is `thread_local`, so which games shared a worker moves the
number -- the same non-reproducibility `units_total` has).

**THE INSTRUMENT: `MTG_BP_KEY_CENSUS` (+ `MTG_BP_KEY_CENSUS_DUMP`).** A process-global, never-cleared
set of every key the bp-enum cache is *consulted* with, recorded before the `find`. It is immune to all
three confounds, and it is **reproducible**, which nothing else here is. Snow, 10 games, seed 930000,
d2/b0, **default cache settings** (deliberately -- the census needs no cap raising, so the 30 GB
byte-budget fight is not on the critical path at all):

| arm | distinct states | lookups | misses | clears |
|---|---|---|---|---|
| base (filter OFF) | 405,277 | 27,274,130 | 431,881 | 50 |
| **base_rep** (replicate) | **405,277** | **27,274,130** | 433,593 | 50 |
| `snapnone` (filter ON, base's key) | **403,455** (−0.45%) | **31,987,946 (+17.3%)** | 508,007 | 59 |
| `perpath` (filter ON, full key) | 407,429 (+0.53%) | 32,110,062 (+17.7%) | 514,255 | 61 |

**1. The census is reproducible, across runs as well as arms.** `base_rep`'s key set is byte-identical
to `base`'s -- same 405,277 keys, same dump file size -- while its `misses` differ by 1,712. And run
`kc2`, launched separately hours later with a different arm design, reproduced **every distinct count in
this table to the digit** (405,277 / 403,455 / 407,429) while its `misses` moved again (430,827 vs
433,593 on the same replicate config). That is the property that makes the rest of the table mean
anything, and the reason to read `distinct`, never `misses`.

**2. THE FILTER REACHES FEWER STATES, NOT MORE: −0.45%.** The claim this document carried for a day --
that the extra derivations *"are the search visiting more distinct breakpoint states"* and that *"no
key-merging scheme of any kind recovers them"* -- is now **refuted by measurement**, not merely
retracted as an unsupported inference. The USER's instinct (*"The misses should be the same as no
condemnation. I sense a bug"*) was right on the substance, and the direction is even slightly better
than the spec demands: the prune removes states, as a prune should.

**3. AND THE FIVE KEY FOLDS SPLIT ALMOST NOTHING: +0.53%** (2,152 keys). `misses` reported that same
widening as +20.9% on an earlier cell. So the key-width story, which cost two arms and a retraction,
is a ~0.5% effect end to end.

**4. WHAT IS LEFT IS THE WHOLE DEFECT: +17.3% LOOKUPS ON A SMALLER STATE SET.** Lookups are
cap-independent -- no residency argument can touch them -- so this is the spec violation in isolation,
with every other explanation now excluded:

* lookups per distinct state: **67.3 → 79.3**. The filter consults the cache 4.7M more times while
  visiting 1,822 fewer states.
* the miss inflation is *entirely* eviction, and now quantified: re-derivations (misses − distinct) are
  26,604 for base but **104,552** for `snapnone` -- 3.9x the churn on a 0.45% *smaller* working set,
  which is what 17% more lookups through a clear-on-full cache does.

**A prune cannot raise lookups.** Per the USER's standing doctrine (2026-08-28) that is a defect with a
location, and the location is no longer "somewhere in the cost". The candidate mechanism is the **wave
backfill**, and the wave probe's own comments describe the mechanism precisely: *"retired = the rank was
PAST THE END of the continuation list, so the apply's only product was learning `n`"*, and *"STILLBORN
= a wave-0 plan's slot opens at rank W (`BpSearchWidth`) and the breakpoint's real continuation list is
SHORTER than W"*. **Condemnation's entire effect is to make continuation lists shorter**, so it
manufactures exactly the condition those counters measure -- and each such probe apply is a full
`ApplyPlanDirect` that walks breakpoints and consults this cache. That raises lookups and units while
*reducing* states, which is the measured shape exactly. `logs/snow_perf/wavebackfill.sh` tests it
directly (`scored`/`retired`/`stillborn`/`nskip` vs base). Note `MTG_BP_WAVE_NSKIP` already defaults ON
and its scope (`UnbudgetedWorkScopeActive()`) is live at b0, so any delta is what survives the existing
mitigation -- and its `known_n` is keyed on `(base plan index << 8 | bp_at)`, a **positional** key,
while condemnation's drop count is **path-dependent**, which is a concrete way for that mitigation to
leak under condemnation specifically.

#### THE SET RELATION ITSELF (run `kc2`, the corrected arms) -- the violation is 715 states, interior-only

With the lever moved to the environment so the arm vector matches (see the batch-arm-fold trap below),
the sets compare. Snow 10 games, seed 930000, d2/b0, `SNAPSHOT_NONE=1` on both arms, default cache.
Firing assertion passed: `base` drops=0, `snapnone` drops=125,953.

| relation | keys |
|---|---|
| shared | 402,740 |
| **`snapnone` \ `base`** -- states ONLY the filtered search reaches | **715 (0.18%)** |
| `base` \ `snapnone` -- states the filter never enumerates (**the upside**) | **2,537** |

**And play is IDENTICAL on every arm** (`digest=7916f1f572f914e7`, `avg=5.9000`, all four). So the 715
are **pure search interior**: removing a candidate changes the node's B&B cutoffs, so the search reaches
a few states the unfiltered walk never needed. They are not new *play*, and they are not a soundness
problem -- but per the USER's spec (*"there should be none the other way"*) they are still the wrong
direction, and at 715 keys they are small enough to enumerate and root-cause individually rather than
argue about (`LC_ALL=C comm -13 base.keys snapnone.keys`).

**The proportions are the story.** 125,953 drops buy 2,537 fewer states (net −1,822) -- and cost
**+17.7% lookups** and **+14.8% units** (40,142,740 -> 46,099,869, both arms narrowed so the narrowing
cancels). The filter is doing a great deal of dropping for very little state reduction, and paying for
it many times over in consultations. That is the shape option (D) is designed to fix: it keeps the
2,537-state upside available while making the lookup column baseline's by construction.

#### CORRECTION (2026-09-17): `MTG_BP_KEY_SNAPSHOT_NONE` IS NOT AN "EQUAL KEY TO BASE" ARM

This document has said, repeatedly and as the premise of the whole key-width investigation, that
*"Condemnation-OFF binds none of them -- on Snow the scope is not even constructed."* **That is false,
and the arm built on it does not measure what it claims.**

Read `CantripOrderScope`'s constructor (`TurnSolver.cpp:10020-10040`). The scope is constructed
**unconditionally** at the breakpoint sites (`TurnSolver.cpp:23919`, `25232`, `AIEngine.cpp:3885`) --
`BpClassifyActive(state)` is merely passed *as an argument*, it does not gate construction -- and the
ctor binds two members with no condition at all:

| fold | binds when | in the condemnation-OFF baseline? |
|---|---|---|
| pre-draw hand snapshot (`g_bp_hand_before`) | `CantripOrderEnabled() \|\| classify` | no (`MTG_CANTRIP_ORDER` is absent from `src/ai/data/heuristic_defaults.env`, so off) |
| plan cast set (`g_bp_plan_casts`) | `CantripOrderEnabled() \|\| classify` | no |
| site + how reached (`g_bp_site_def`, `g_bp_site_activated`) | `classify` | no |
| **mana-source count (`g_bp_mana_sources_before`)** | **unconditional** | **YES** |
| **deferred-Karoo reservation (`g_land_drop_reserved`)** | **unconditional** | **YES** |

Both of those folds are guarded in `BpEnumBuildKey` by `!s_snapshot_none` *alone*
(`TurnSolver.cpp:43118`, `43121`). So `MTG_BP_KEY_SNAPSHOT_NONE=1` does not equalise the arm to base's
key -- **it makes the key strictly NARROWER than base's**, on both arms, by merging states that the
baseline engine keeps apart.

**And narrowing is expensive, measured.** Two runs of the *same* condemnation-OFF Snow cell
(10 games, seed 930000, d2/b0), identical play (`digest=7916f1f572f914e7`, `avg=5.9000` both):

| base arm | `units_total` | wall |
|---|---|---|
| `MTG_BP_KEY_SNAPSHOT_NONE` unset (run `ap2`) | 34,414,324 | 697 s |
| `MTG_BP_KEY_SNAPSHOT_NONE=1` (run `kc1`) | **40,142,740 (+16.6%)** | 1,951 s |

Merging on mana-source count makes the cache serve a list derived at a state with a different mana
count, so the walker learns a wrong continuation length (`g_bp_cands_last`) and does more work. Play
survives it; cost does not.

**WHAT THIS INVALIDATES.** Every "at equal key" figure above -- the `missspec` +9.96% units / +6.0%
misses / +12.2% lookups, and the `ceiling` arm's "key merging worth 0.4%" -- compared `snapnone`
(condemnation ON, key **narrower** than base's) against `base` (condemnation OFF, **normal** key). The
key narrowing's own +16.6% is folded into those numbers with the opposite sign to the one assumed. Those
rows are **not** "the filter's cost at equal key"; they are the filter's cost *plus* a narrowing penalty,
minus whatever the filter saves. Do not quote them.

**What remains valid, and why the `kc1`/`kc2` table above is unaffected:** in those runs the narrowing
is applied to **both** arms (`base`, `base_rep` and `snapnone` all carry `SNAPSHOT_NONE=1`), so it is
controlled rather than confounded. The −0.45% states and +17.3% lookups are a like-for-like comparison
*within* the narrowed key space. They just cannot be compared against the older tables, which live in a
different key space.

**AND THIS IS AN ARGUMENT FOR OPTION (D).** "Equal key" is not reachable by narrowing -- narrowing is
itself a large cost, and it is unsound besides. It is reachable by making condemnation **add no fold in
the first place**, which is precisely what filtering at consumption does: the continuation list stops
being a function of the arriving cast set, so base's own folds (mana source, Karoo) stay exactly where
they are in *both* arms and nothing needs suppressing. Under (D) the spec is met by construction; under
emission-time filtering it cannot even be *measured* without perturbing the thing being measured.

#### THE TRAP THAT VOIDED RUN `kc1`'s SET DIFFERENCE (counts are fine; sets were not)

`kc1` reported `shared=0`: base and `snapnone` had **zero** keys in common out of ~405,000 each, with
each arm's "unique" count equal to its entire set. Two runs of the same ten games cannot disagree about
every breakpoint state, so that is an arm-design bug, and it is this:

> `MTG_SNOW_CONDEMN` is heurarm slot `SNOW_CONDEMN`; a manifest's per-job `flags` sets that slot; and
> `BpEnumBuildKey` folds **the whole arm vector** into every key -- deliberately, as the **BATCH-ARM
> FOLD**, so that a mixed-arm pooled batch cannot share cache entries across arms. Two arms differing
> in any slot therefore inhabit **disjoint key spaces by construction**.

`MTG_BP_KEY_SNAPSHOT_NONE` does not suppress it and must not -- it is not one of the five condemnation
folds. Setting the lever in the **environment** leaves the slot unset (`heurarm::Flag` returns the env
default when `t_arm[slot] < 0`), so the fold is identical across arms and the sets compare.

**What this does NOT invalidate, so nobody "fixes" the neighbouring scripts by mistake:** an arm
constant is a **bijection** on the key space. It changes *which* keys are built, never *how many*. Every
count in the table above, and every count in `missspec.sh` / `ceiling.sh` / `allpaths.sh`, stands. Only
a set comparison needs the env form.

**Two further traps, both live in this directory's scripts:** `EnvOn` is `getenv(k) != "0"`, so
`MTG_SNOW_CONDEMN=false` reads as **ON** -- passing `false` to the env form silently makes the baseline
arm a second filtered arm. The drops assertion in `census.sh` exists to catch that rather than trust a
comment. And `comm` must run under `LC_ALL=C` against these dumps, or locale collation can report both
files as wholly unique -- the same `shared=0` shape from an unrelated cause.

### THE MODEL, IN THE USER'S OWN TERMS (2026-09-17) -- read this before the numbers

Four statements, and they define what this feature is for and how to judge it:

1. **The asymmetry is the whole design.** *"It is possible for us to do less work if there are lines
   we never run. That part is the upside of the condemnation design, but there should be none the
   other way."* So the target is **misses <= baseline, lookups <= baseline**, with the savings coming
   from lines never run. Fewer is the prize; more is a defect. Any table in this document showing the
   filter above baseline on a work metric is describing a bug, not a price.
2. **The cache is where path-dependence actually bites, and it is a WORK question, not only a
   soundness one.** *"We may determine a line does condemn A and then later reprocess the same cache
   entry from a case that doesn't condemn A. In that case where we run A is different."* The entry
   was built under one line's condemn set; a later line with a weaker set needs A available. So the
   entry is not wrong so much as INCOMPLETE for the second arrival, and the question is where the
   work to cover A gets done.
3. **The ideal case is the point of the rule.** *"In the ideal case, A is never accessed at all
   because all lines condemn it."* That is the full upside: unanimous condemnation means the line is
   never enumerated by anybody, which is strictly less work than baseline.
4. **The deliverable is a FREQUENCY, not an argument.** *"The only real question is how often this
   actually happens in practice."* Which is exactly what the cast-set collision probe measures, and
   the answer is in the next subsection: **Snow 290 of 331,245 key builds (0.09%), kitty 7,388 of
   184,212 (4.0%)**. Unanimity is the common case by a wide margin; disagreement is rare. That is the
   number that should drive the design decision, and it is already measured.

### The soundness case is real: two lines DO reach one state with different cast sets

`MTG_BP_CASTSET_PROBE` builds the key twice -- once normally, once with the plan-cast fold suppressed
-- and records which cast set each cast-LESS key arrived with. A repeat arrival with a different set
is path-dependence, counted exactly, with no name test and no re-derivation. A second, INDEPENDENT
and zone-complete state fingerprint (hand/graveyard/battlefield card numbers + tapped, exile, both
life totals, lands played, library size, floating mana, turn, phase) says whether the two arrivals
were at the SAME state, which is what separates path-dependence from key imprecision.

| deck | cell | keys probed | mismatch | same state | diff state | site kind | map clears |
|---|---|---|---|---|---|---|---|
| Snow | 6 games d2 b200 | 331,245 | 299 (0.09%) | **290** | 9 | activated, `casts=0` | 0 |
| kitty | 60 games d3 b10 | 184,212 | 7,388 (4.0%) | **7,388** | 0 | cast site, `casts=1` | 0 |

`map_clears=0` on both, so no arrival was forgotten (a wipe can only HIDE a mismatch).

**SNOW HAS PATH-DEPENDENCE AND THE OBVIOUS ARGUMENT SAYS IT SHOULD NOT.** All 8.3M consultations are
at an ACTIVATED site, and an activated site fires only in `ApplyPlanDirect`'s trailing pass, after
every cast (verified in code, not from a comment: the mid-loop activation dispatch needs
`inline_acts` = `plan.human_action_order && plan.searched_order && s_human_play`, and the site-8
block is itself gated `!s_human_play` -- mutually exclusive). With no tail, identical states must
produce identical cast sets. They do not, 290 times in 6 games.

**LEADING HYPOTHESIS, NOT YET CONFIRMED:** `plan_cast_names` is built from `plan.actions` BEFORE
application, while `apply_one` silently drops a cast it cannot pay for ("checks its precondition and
its payment FIRST and no-ops without mutating anything when either fails"). So `g_bp_plan_casts`
records the plan's INTENT, not the turn's history -- and two lines can reach an identical state with
different intents. Every printed Snow mismatch has `casts=0` on the arriving line, i.e. the
disagreement is about `BpPlanMadeACast()`, which gates the drop entirely. Confirm by dumping both
cast sets at a mismatch before building on this.

### The design fork -- three coherent options, and only one is what the USER asked for

All-paths = keep the UNION of the kept sets over arriving lines. Online, that union is only ever
partial, which is the whole difficulty.

* **(A) Accumulating union cache** (the USER's construction, 2026-09-17: *"safe to run any lines not
  condemned by the current line and only run lines that were condemned in the cache if the new
  approach to reach that state does not condemn them"*, *"we skip lines that might be fully condemned
  until it is proven otherwise"*). Store the weakest condemn set seen; a line that condemns less
  triggers a re-derivation and the entry grows. Exact in the limit and strictly safer than today.
  **BLOCKER: the served list depends on arrival order**, so play would vary with thread shape and the
  Linux/Windows determinism-parity job would go red. Needs a rule that makes the served value a
  function of (state, that line's condemn set) alone.
* **(B) State-determined over-approximation.** Condemn only what is condemned under the weakest
  assumption the state can support. Deterministic and strictly safer. **But on Snow it disarms the
  filter**: at an activated site a line that cast nothing is always possible (the source is already
  in play and needs no cast), so "some line reaching S condemns nothing" is always true and nothing
  is ever condemned. All-paths on Snow then EQUALS condemnation-off -- which, per the table above, is
  14.5% CHEAPER. That is decision-relevant on its own: it would settle the standing three-way ship
  decision for Snow as **ship nothing**.
* **(C) Derive-uncondemned-once, filter per arriver.** Deterministic, one derivation per state
  instead of one per cast set, play identical to today. But it is today's PER-PATH semantics, not the
  all-paths rule, so it carries no soundness gain -- and it gives up the prune's derivation saving,
  which the table above says was only ~0.4% anyway.

* **(D) FILTER AT CONSUMPTION, NOT AT EMISSION.** The preferred option as of 2026-09-17, and the only
  one that satisfies the USER's spec *structurally* rather than by measurement. See below.

**Do not sell any of these on cost.** The cost case died with the `snapnone` arm.

### (D) THE EMISSION/CONSUMPTION FORK -- one root under all three symptoms

Today's drop is a **`continue` in the candidate-emission loop** (`TurnSolver.cpp` ~11340): the plan for
`ap.hand[i]` is never emitted, so **the continuation list itself gets shorter**. Every hard problem in
this document is a consequence of that one choice:

1. **`bp_choice` is a positional index** (`out = cands[plan.bp_choice]`), so the list's length and order
   are load-bearing. A widening discovered mid-decision would renumber candidates the search has
   already scored -- which is exactly why all-paths stage 1 must **defer**, and why it measured
   `deferred_widenings=890,258` against 2,475 realised drops.
2. **The list is a function of the arriving line's cast set**, so the cast set must be folded into the
   bp-enum key -- five folds -- and the path-dependence of the verdict becomes a *soundness* problem
   rather than a bookkeeping one.
3. **The list is shorter than `W`**, which manufactures the stillborn/retired wave slots that are the
   leading candidate for the **+17.3% lookups**.

**The change:** emit *every* candidate -- list identical to baseline's, same length, same order -- mark
the condemned ones with a per-candidate flag, and skip them where they would be **scored / applied /
rolled out**. Consequences, in the order the USER's spec asks for them:

* **Misses and lookups become baseline's BY CONSTRUCTION.** The list is no longer a function of the
  cast set, so **no condemnation fold is needed in the key at all** -- not five, not one. *"We should
  miss exactly where baseline misses and hit otherwise"* stops being something to measure and becomes
  something the design cannot violate. The 0.53% state-splitting and the eviction churn both go to zero.
* **The fixpoint becomes free.** Indices are baseline's, so a widening is a **bit flip** on a candidate
  already in the list: no re-derivation, no renumbering, no deferral, no re-run. The all-paths rule
  applies *within* the decision, and all 890,258 deferred widenings are realisable. **The index-stability
  compromise that made stage 1 a floor was a consequence of emission-time filtering, not a property of
  the rule.**
* **The wave-slot length `n` becomes baseline's**, so symptom 3 cannot arise, and `MTG_BP_WAVE_NSKIP`'s
  positional `known_n` stops being a stale-length hazard under a path-dependent filter.
* **The saving relocates to where the cost actually is.** Emission-time filtering saves a *derivation*
  (measured: ~0.4%). Consumption-time filtering saves the **apply and the rollout** -- and on Snow
  77.6% of all units are wave applies, of which 85% never reach a rollout. This is a better lever on
  the USER's own framing that all-paths *"should be a cost lever vs no condemnation … since it has the
  same or fewer distinct states"*: now it has **exactly** the same states.

**What it gives up:** building the `Plan` for a candidate that will be skipped. That is the ~0.4%
derivation saving, and buying the whole spec with it is the trade this document has been looking for.

**WHY NOT A RE-RUN FIXPOINT -- and this is measured precedent, not preference.** The obvious
alternative is to re-solve the decision when a widening appears. This file already tried that shape
elsewhere and records the verdict in `ApplySecondMainInSearch`: of the M2 fixpoint's three forms, *"an
unconditional re-solve-and-play was net-red on the hinata battery"* and *"a lethal-only NESTED SOLVE
re-pass taxed the whole suite's budget (478 searched games slower)"*; what survived is **enumeration +
probe applies only**. A per-decision re-solve for all-paths is the same shape that lost twice. Option
(D) needs no re-entry at all.

#### AUDIT OF (D), FIRST PASS DONE 2026-09-17 -- one real leak found, and it is a one-line fix

The premise is that the continuation list is a *menu* and nothing reads it as semantics. Checked, not
assumed. **READ THE SECOND PASS BELOW BEFORE ACTING ON THIS ONE:** it locates the drop in
`CollectActions`, not in the function named here, and it retracts the wave-0 item at the end.
The list source is `TurnSolver::EnumerateBreakpointPlans` (`TurnSolver.cpp:21953`), and
**the executor indexes the same list** (`AIEngine.cpp:3918`, `resolve_draw_breakpoint`) -- so the
condemned flag must be stamped *inside* that function, where both sides get it by construction. That is
the existing lockstep rule, and it makes (D) cheaper rather than harder: full-length in both sides means
`bp_choice` finally denotes the same candidate on both.

Consumers of the list found so far:

| consumer | reads | under (D) |
|---|---|---|
| `out = cands[plan.bp_choice]` | one index | **safe** -- the search never scores a condemned index, so it never commits one |
| `g_bp_cands_last` (21956) | length | **improves** -- becomes baseline's length, which is the fix for symptom 3, and for NSKIP's positional `known_n` going stale under a path-dependent filter |
| `g_bp_cands_fp_distinct`, `bpcands::g_fp_*` | length + content | stats only (`MTG_ROLLOUT_STATS`) |
| **`g_bp_cands_has_empty` (21985-21987)** | **membership** | **THE LEAK -- must be fixed** |

**THE LEAK, exactly.** The node's child loop (`TurnSolver.cpp:35704`) walks `k = 0..node_n`, where
`k == node_n` is the explicit EMPTY continuation, and pre-skips that arm when the list already contains
an apply-empty entry:

> *"EMPTY pre-skip: the k loop reached the EMPTY arm (so **every cands index was visited**) and the list
> holds an apply-empty entry -- the EMPTY arm's state is already in the dedup set, so the resume apply it
> would pay is pure waste. **Exact by construction**."*

Under (D) the loop still reaches `k == node_n` but **no longer visits every index** -- condemned ones are
skipped. If the apply-empty entry is itself condemned, `g_bp_cands_has_empty` is still true (it is
present in the list) while its state was never reached, so the EMPTY arm is skipped on a premise that no
longer holds and **the empty line is silently lost**. That is a lossy prune, i.e. the dealbreaker class,
and it would not show up as a crash or a counter -- only as a missing line.

**Fix:** compute `g_bp_cands_has_empty` over **non-condemned entries only**. The existing comment
("exact by construction") is precisely the invariant to preserve, and it names its own repair.

This is the pattern to expect for the rest of the audit: membership read as semantics, in a spot whose
comment already states the invariant.

#### AUDIT OF (D), SECOND PASS DONE 2026-09-17 -- `bp_seen_states` is SAFE, and TWO CLAIMS ABOVE ARE WRONG

**`bp_seen_states` is safe, and the reason generalises.** It keys on the POST-APPLY STATE
(`BuildDedupKey(copy)`, e.g. `TurnSolver.cpp:37275`, `:37188`, `:37745`), never on a list index, so
restoring the list to baseline's length cannot disturb it. Every one of its sites performs a real
`insert(...).second` check rather than inferring presence. So `g_bp_cands_has_empty` is the ONLY
membership-as-semantics reader on this path, and the one-line fix is confirmed to be literally one
line: one write site (`:21985-21987`) feeding two read sites (`:35722`, `:37148`) that need no change.

**CORRECTION 1 -- the drop is NOT in `EnumerateBreakpointPlans`; it is in `CollectActions`.** The
`continue` is at `TurnSolver.cpp:11337`, inside `CollectActions` (`:11087`), which emits the ACTION
list. `EnumerateBreakpointPlans` (`:43000`) is a one-line wrapper over `BpEnumEntryFor`, whose miss
path calls `EnumeratePlansWithLand` -> `CollectActions`. So the condemned candidate is absent from the
CACHED plan list, which is exactly why the arriving line's cast set had to be folded into the key. The
first-pass audit's instruction to "stamp the condemned flag inside that function" was written against
the wrong function, and taking it literally would have stamped the cached entry.

**AND THIS IS THE ONE DISCIPLINE THAT DECIDES WHETHER (D) WORKS AT ALL.** If the condemned bit is
baked into the cached plans, the entry is again a function of the arriving line's condemn set, the
five folds come straight back, and every defect this document has chased returns intact. So:

> **The cache must store baseline's list, unstamped. The condemned bit is evaluated PER ARRIVAL, at
> consumption.** `EnumerateBreakpointPlans` returns `std::vector<Plan>` BY VALUE (`:43003` --
> `return BpEnumEntryFor(...)->plans;` copies), so stamping the copy is already safe and the entry
> stays baseline's. The executor's twin (`AIEngine.cpp:3918`) calls the same function and therefore
> gets the same stamping from the same predicate -- lockstep by construction.

The USER's spec sanctions exactly this and no more: *"Our only extra work is checking condemnation
status and deciding what we need to implement."* That is a per-arrival check over a full-length list.

**CORRECTION 2 -- wave 0 CANNOT decline to emit a variant for a condemned index.** Retract that line.
The wave-0 fan-out (`:29936-29944`) emits `bp_choice = 0..W-1` for each base plan **blind**, before any
apply: the comment at `:21953` says so ("the caller can only emit `bp_choice = 0..W-1` blind"), and
condemned-ness is a property of a breakpoint state that emission has not reached yet. There is nothing
to decline.

**What happens instead is better than the retracted plan, and it needs no new code.** An unresolved
`bp_choice` already falls through to the EMPTY continuation, and that fallback is deliberately
unconditional (`:22017-22023`: *"A continuation the plan did not carry is EMPTY ... so there is no scope
predicate for a future change to widen or misread"*). A condemned index is an unresolved index, so the
slot degrades to **EMPTY** -- a continuation the USER explicitly asked to be available at every segment
(*"Empty needs to be a valid option"*, *"for every segment"*) -- and a duplicate EMPTY arrival is caught
by the variant dedup at `:37275`. So the slot is not wasted; worst case it is a deduped repeat.

**THE REAL TRADE (D) MAKES, WHICH THE FIRST PASS DID NOT PRICE: it gives up RANK COMPACTION.** Today's
emission-time drop compacts the list, so surviving candidates are pulled DOWN into the low ranks the
W-wide wave-0 window can reach. Under (D) the ranks are baseline's, so a survivor at baseline rank >= W
stays out of wave 0's reach. Note the direction carefully:

* **Versus condemnation-OFF baseline this is not a loss** -- baseline cannot reach that rank either. So
  the no-lossy-truncation bar is not engaged.
* **Versus TODAY's condemnation it is a loss**, and an unmeasured one. It is also the mirror image of
  the stillborn/retired accounting: today's filter buys reach by shortening lists, and that shortening
  is precisely what manufactures the short-list wave symptom and forces the key folds. (D) declines the
  bargain in both directions at once.

**AND THAT SPLITS (D) INTO TWO OPTIONS. THEY ARE NOT INTERCHANGEABLE.**

* **(D1) Full-length indexing; a condemned slot resolves to EMPTY.** `bp_choice = k` denotes
  `cands[k]` of baseline's list regardless of anyone's condemn set. Keys need no folds, the executor
  agrees by construction, a widening is a bit flip at a stable index, and the spec is satisfied
  STRUCTURALLY. Cost: baseline's wave-0 reach, i.e. no compaction.
* **(D2) Full-length list, but `bp_choice = k` denotes the k-th NON-CONDEMNED entry.** Keeps
  compaction and today's reach. **But it forfeits (D)'s entire headline gain:** the meaning of `k`
  becomes a function of the condemn set again, so the executor must reproduce that set exactly to
  index the same candidate -- and "the search binding no scope where the executor did" is already one
  of the three apparatus bugs this document records. The cached LIST would stay baseline's, but the
  served MEANING would not.

**(D1) is the option that matches the USER's spec.** (D2) is today's problem wearing a longer list.

**WHAT TO MEASURE BEFORE BUILDING (D1), and the instrument already exists.** Compaction can only have
bought something on a list LONGER than `W`: at `len <= W` wave 0 indexes every survivor anyway, so
deleting an entry and marking it are indistinguishable for reach. `MTG_BP_CANDS_PROBE` already reports
exactly that -- `capped[site]` is "breakpoints with `cands.size() > W`", alongside `reach` /
`unreachable` and a length histogram (`TurnSolver.cpp:20216-20275`). A small `capped%` on the FILTERED
arm prices (D1)'s only real cost at ~nothing. **Added to `logs/snow_perf/wavebackfill.sh`, which is
where it belongs, because it is THE SAME QUESTION AS THE BACKFILL HYPOTHESIS FROM THE OTHER SIDE:**
`stillborn` requires the list to be SHORTER than `W`, so a confirmed backfill result simultaneously
proves compaction was buying nothing, and exonerates (D1). If instead `capped%` is large AND stillborn
is rare, (D1) has a real quality cost to weigh and the (D1)/(D2) choice is live.

#### HOW (D1) IS ACTUALLY BUILT -- and the one hard part is a SCOPE SPLIT, not the flag

Worked out 2026-09-17 before writing any code, because the naive version reintroduces the fold.

**The flag itself is trivial.** `Plan` already carries a dozen booleans and no digest keys on any of
them; `bool bp_condemned = false` is free. The skip is ~3 lines at `TurnSolver.cpp:22008-22012`: if
`cands[plan.bp_choice].bp_condemned`, leave `resolved == false` and let the existing unconditional
EMPTY fallback take the slot. Plus the `g_bp_cands_has_empty` one-liner. That is the whole change --
*if* the flag is computed in the right place.

**WHY THE FLAG CANNOT BE SET DURING THE DERIVATION, which is the tempting cheap route.** Marking the
`Action` in `CollectActions` and OR-ing it into the plan as the odometer builds would be one field and
one line. But plans are built *inside the derivation*, and the derivation is what the bp-enum cache
STORES -- so the entry becomes a function of the arriving line's condemn set again, the five folds come
straight back, and every defect in this document returns. **Cheap-and-wrong; do not build it.**

**So the predicate is re-run at CONSUMPTION, over the returned copy.** For each candidate, for each of
its cast actions, run the existing conjunction on the corresponding hand card; any condemned cast
condemns the plan. Cost is O(list x casts) per lookup with the same predicate the emission gate already
uses -- which is exactly, and only, what the USER's spec budgets for: *"our only extra work is checking
condemnation status."*

**AND HERE IS THE HARD PART.** The `CollectActions` drop site serves THREE contexts, distinguished in
its own `[condemn-who]` dump by `where=EXEC | srch | leaf`:

| context | reaches the cands list? | under (D1) |
|---|---|---|
| searched continuation enum (`srch`) | yes, via `EnumerateBreakpointPlans` | condemnation must be **scoped OFF** here so the cached list is baseline's; the consumption stamp replaces it |
| executor (`EXEC`, `g_condemn_root_turn < 0`) | yes -- `AIEngine::resolve_draw_breakpoint` calls the same function | covered by the same stamp, and this is where (D1) *gains*: `bp_choice` finally denotes the same candidate on both sides |
| rollout / leaf (`leaf`, `!g_search_candidate_enum`) | **no** -- greedy playout plan building, no `cands` indexing | the test must **STAY** in `CollectActions`, or condemnation silently vanishes from the playout layer |

So (D1) is not "move the filter"; it is **a scope split**: off in the bp-enum derivation, unchanged in
the playout layer, replaced by a stamp on the searched/executor list. **Treat that as the risk item.**
The three apparatus bugs this document already records include exactly this failure --
*"the search binding no scope where the executor did"* -- and a clean zero or a clean no-change on one
side of a split is this feature's established bug signature, not a pass. **Put a firing counter on each
of the three contexts and assert all three move in the expected direction before believing any
measurement.**

### MEASURED 2026-09-17 (runs `wb1`, `d1a`): the lookup delta IS wave apply work -- but NOT by the mechanism proposed

`logs/snow_perf/wavebackfill.sh`. Same Snow cell as the census (seed 930000, d2/`--budget-ms 0`,
`MTG_BP_KEY_SNAPSHOT_NONE=1` on both arms, `MTG_BP_CONDEMN_NEW_OPTION=1`, condemn set in the
ENVIRONMENT). `wb1` = 10 games; `d1a` = 8 games (gi=0..7, i.e. the same cell with the two monster games
excluded -- 16 seconds instead of ~30 minutes). Play **identical on both arms of both cells**
(`wb1` digest `7916f1f572f914e7` avg 5.9000; `d1a` digest `bd3f8a9af6ff6cee` avg 5.6250), so both are
pure work measurements with no power as play tests.

| metric | base (10g) | filtered (10g) | Δ | base (8g) | filtered (8g) | Δ |
|---|---|---|---|---|---|---|
| lookups (`hits+misses`) | 27,274,268 | 32,082,910 | **+17.63%** | 437,955 | 447,213 | +2.11% |
| wave `slots` | 2,517,447 | 2,976,767 | **+18.24%** | 99,540 | 100,857 | +1.32% |
| wave `scored` (applies) | 28,019,861 | 33,046,727 | **+17.94%** | 365,400 | 372,944 | +2.06% |
| `rolled` | 3,139,778 | 3,806,620 | +21.24% | 105,229 | 106,309 | +1.03% |
| `stillborn` | 1,680,277 | 2,012,074 | +19.75% | 70,620 | 71,407 | +1.11% |
| **`retired`** | 3,280 | 3,290 | **+0.30%** | 157 | 157 | **+0.00%** |
| `dupstate` | 59,486 | 59,159 | −0.55% | 1,792 | 1,792 | +0.00% |
| `nskip` | 3,423 | 3,433 | +0.29% | 1,385 | 1,385 | +0.00% |
| wave host `nodes` | 22,757 | 25,240 | +10.91% | 5,931 | 5,971 | +0.67% |
| `units_total` | 40,143,167 | 46,043,691 | **+14.70%** | 1,647,283 | 1,665,044 | +1.08% |
| drops | 0 | 125,164 | — | 0 | 762 | — |

**CONFIRMED: the lookup delta is wave apply work.** `+17.63%` lookups against `+17.94%` scored applies
is a match to a third of a point, and every wave apply runs a full `ApplyPlanDirect` that walks
breakpoints and consults the bp-enum cache. Combined with the census (states **−0.45%**), the shape is
settled: the filter consults the same states more often -- **lookups/state 67.30 -> 79.52** -- rather
than reaching new ones.

**REFUTED: the proposed MECHANISM. `retired` is FLAT (+0.30%), so it is NOT "shortened lists push more
ranks past the end".** That was the hypothesis the script was written to test and it is wrong. Nor is
it stillborn *rate*: `stillborn/slots` moves only 66.75% -> 67.59%. What actually happens is that the
wave opens **+18.24% more slots across +10.91% more host nodes**, with every downstream wave counter
scaling along at ~the same ratio. So the filter is not wasting a fixed amount of wave capacity more
often -- **it is causing the search to open more wave capacity.** The leading explanation, and it is
the same root the census already identified for the 715 interior-only states: removing a candidate
removes the cutoffs it was producing, so enclosing bounds fail to fire and more siblings expand.
**The fix therefore does NOT belong at the slot level, and the previous version of this section's
verdict text ("the fix belongs at the SLOT level ... condemnation itself is exonerated") is retracted.**

**THE CLEANEST STATEMENT OF THE DEFECT: each drop costs work.** 5,900,524 extra units over 125,164
drops = **~47 units per drop** (10g); 17,761 over 762 = ~23 (8g). A prune that charges per removal is
the whole finding in one number, and it is the number to watch any fix against.

**AND THE COST IS A TAIL PHENOMENON, WHICH IS NEW.** On gi=0..7 the filter costs **+1.08% units**; the
full cell costs +14.70%. 124,402 of the 125,164 drops (99.4%) and ~99.7% of the extra work are in
gi=8 and gi=9 alone -- the two degenerate games. Any future A/B on this feature that excludes those
two games will measure a filter that looks nearly free, and any cell that includes them is really
measuring two games. Say which.

#### THE DIAGNOSTIC PROBES WERE WRITING OUT OF BOUNDS -- every past `MTG_BP_PROBE` / `MTG_BP_CANDS_PROBE` number on Snow is VOID

Found while reading `wb1`'s `[bp-cands]` block, because it was internally impossible:
`capped=24,557,301` against `n=57,573` for a counter that increments at most once per call,
`max=1,408,102` on a list whose reported mean was 28, and an **empty length histogram on every site at
once**.

**Root cause:** `kBpSites` was **8**, while `bp_searched_plan` is called with site **8** (snow
look-at-top put-into-hand) and site **9** (post-entry activation) -- and `BpSiteMask` returns
`... | 0x100 | 0x200`, i.e. both are **unconditionally ON**. `BpCandsProbe` stores `hist` first, so
`hist[8][b]` landed exactly on `n[b]`, `n[8]` on `total[0]`, and so on across every array in both
probe structs. The printed rows for sites 0-7 were a mixture of real data and smear, and on Snow --
where **every** consultation is at site 8 -- the site that mattered was never reported at all.

**Blast radius is diagnostic-only:** both `BpHit` and `BpCands` early-return when their env flag is
off, so no measured play, no digest and no shipped run was ever affected, and nothing in the census /
`[bp-waves]` / `[bp-enum]` / `units_total` numbers above comes from these structs. But **no number
previously quoted from either probe on a deck that reaches site 8 or 9 can be trusted.**

**Fixed 2026-09-17:** `kBpSites = 10` with both names added, the `MTG_BP_SITES` comment block brought
back into step, and -- the durable half -- a `BpSiteInRange` guard in both writers that prints a loud
one-time out-of-range warning instead of smearing, so adding site 10 cannot repeat this silently. The
post-fix run reports Snow's real sites with coherent numbers and populated histograms, which is itself
the confirmation.

#### THE (D1) COMPACTION NUMBER, MEASURED ON THE FIXED PROBE (run `d1a`)

Snow's site 8, 8 games, filtered arm:

| quantity | value |
|---|---|
| lists enumerated (`n`) | 71,045 (site 8) + 1,784 (site 9) |
| mean list length | **6.69** |
| implied wave width | `reach/n = 1.86`, i.e. **W = 2** |
| lists LONGER than W (`capped`) | **73.9%** |
| continuations rank-gated OUT (`unreachable`) | 343,270 of 475,217 = **72.2%** |
| length histogram | 1=10,143 2=8,365 3=10,418 4=6,762 5-8=17,273 9-16=12,336 17-32=5,002 33-64=686 65+=60 |

**So rank compaction is structurally LIVE, and that is the honest reading of the number.** With W=2 and
a mean length of 6.69, deleting a front-rank candidate really does promote a rank-2 entry into wave 0's
window on three lists in four. (It is also an independent re-measurement of the known W=2 reachability
hole -- 72.2% here against the 45-90% recorded in `in-tree-greedy-reachability-hole`.)

**BUT IT HAS NEVER BEEN OBSERVED TO DELIVER ANYTHING ON SNOW, AND THAT IS DECISIVE FOR THE BUILD.**
125,164 drops across `wb1` and 762 across `d1a` produce **byte-identical play on every arm**, and the
standing 2,000-game paired measurement is **0 regressions and 0 improvements**. If compaction were
converting rank promotion into decisions, play would move. So:

* **Build (D1).** Its cost is a structural possibility that this deck has never cashed.
* **Carry `capped%` as the warning for the decks where condemnation DOES move play** (Hinata, and
  kitty's 7,388 cast-site cases). There, giving up compaction is a real risk and must be measured
  rather than argued -- same instrument, same one number.
* **And note the third option the number suggests:** if compaction turns out to be where the value is,
  the lever it points at is **W**, not condemnation. A filter that earns its keep by promoting rank-2
  entries into a width-2 window is a re-ranker wearing a prune's clothes, and the honest form of that
  is a wider window or a better ranking -- both of which are available without any path-dependence.

### 2026-09-17: "HOW DO WE MAKE IT NOT RE-EXPAND THE SAME GROUND?" (USER) -- DESIGN ONLY, nothing built

Four levers. They are not alternatives -- they attack different halves of the measured overshoot, and
**two of them are baseline defects that condemnation merely scales**, which is why the bar reads as a
condemnation problem when part of it is not.

**FIRST, THE ARITHMETIC OF THE TARGET.** `nodes` +10.91%, `slots` +18.24%, `scored` applies +17.94%,
`lookups` +17.63%, on `distinct` states **−0.45%**. So no new ground is being found; the same ground is
walked more times. `lookups/state` 67.30 -> 79.52. And of the 2,517,447 baseline wave slots,
**1,680,277 (66.7%) are STILLBORN** -- and that is the residual *after* `MTG_BP_WAVE_NSKIP`, which is
already on at b0 and already halves them (its own record: slots 4,618,573 -> 2,918,243, stillborn
3,426,123 -> 1,724,192, −4.2% units, `improved` identical on both arms).

**(1) MAKE THE CONTINUATION LENGTH A CROSS-NODE FACT. Biggest single number, and it is not about
condemnation at all.** NSKIP's insight is right -- *"the length is not unknowable, it is merely
unremembered"* -- but its memo is **per node and positional**: `bp_known_n` is a local in the node
(`TurnSolver.cpp:36979`) keyed `(bp_base << 8 | bp_at)`, and it only populates when wave 0's own k=0
variant reached that breakpoint in *this* node. Every other node that reaches the same breakpoint pays
a full `ApplyPlanDirect` to re-learn a length some node already knew.
* **Why it cannot simply key on the breakpoint state** (the obvious idea, and it does not work): the
  walker must decide whether to open a slot *before* applying, and the apply is what produces the
  breakpoint state. The state is not available at the decision point. That is exactly why NSKIP keys
  on the plan position.
* **What IS available before the apply: the node's state and the base plan.** Together they determine
  the breakpoint state deterministically. So the memo can be keyed on
  `(BuildDedupKey(node state), BpCandFingerprint(base plan), bp_at) -> n` and shared across nodes.
  Both helpers already exist, and using the plan FINGERPRINT rather than `bp_base` structurally avoids
  the stale-index hazard that made this lever lossy once already (`:30307-30318`).
* Cost: one hash per candidate slot against a saved `ApplyPlanDirect`. Lossless on the same argument
  NSKIP already carries: it removes no rank the walker would have SCORED, only the probe that
  discovers an exhausted list. **Same soundness check applies -- `improved` must be identical.**

**(2) MAKE THE POST-APPLY DEDUP CROSS-NODE.** `bp_seen_states` is a local set per node, and the probe's
own labels say so: `dupstate` is *"a post-apply state a SIBLING already reached"*, split into `dup_self`
/ `dup_cross` / `dup_w0` -- all within one node (`dup_cross`'s comment literally says it *"needs a
node-level mechanism"*). Measured, it catches **59,486 of 28,019,861 applies (0.2%)**. A per-game memo
keyed on the post-apply state would let a repeat arrival skip the rollout outright.
* **THE SOUNDNESS CONSTRAINT IS ALREADY RECORDED AND IS NOT OPTIONAL.** A value computed at one
  remaining depth / budget cannot be served to an arrival with a different one -- that is exactly the
  order-free WIN-reuse defect, whose fix was **SPLIT KEYS plus a budget-gated reuse wave**. Any
  cross-node memo here must carry the same split, and must be scoped with
  `UnbudgetedWorkScopeActive()` rather than `budget->Unlimited()` (the `a54fdaff` scoping bug).
* This is the lever with the largest headroom and the largest soundness risk. It should be measured
  as a pure counter first (how many applies WOULD hit a cross-node memo) before anything is served
  from one.

**(3) THE MISSES CLAUSE IS SEPARABLE, AND IT IS A CACHE-POLICY BUG.** `misses` +17.92% tracks `clears`
+18.37%, on 0.45% FEWER distinct states -- so the extra derivations are **eviction**, not discovery.
The bp-enum cache is **clear-on-full at 8192, not LRU** (`if (cache.size() >= cap || !Fits(psz))
cache.clear();`). The census already measured what policy is worth: raising the cap cut the miss
overshoot from +20.3% to **+6.0%**. So **an LRU (or larger) bp-enum cache can satisfy the USER's
"no additional misses" clause even while lookups remain above baseline** -- the two clauses have
different causes and should stop being reported as one number. Note `plancache::Fits` wipes on a BYTE
budget that `MTG_BP_ENUM_CACHE_CAP` does not control, so a real fix needs both.

**(4) THE ONLY LEVER THAT ADDRESSES THE EXTRA NODES AT SOURCE: STOP WEAKENING THE BOUND.** (1)-(3)
make re-expansion cheaper or rarer; they do not stop the search from *opening* +10.91% more nodes. That
comes from dropping a candidate that was carrying value: the node's best value falls, the bound
weakens, and siblings expand that baseline cut off. Corroborated by the drop split --
`drops=125,164 (searched=125,164 exec=0 rollout=0)`: **every Snow drop is in the searched space**, which
is precisely where a bound exists to weaken. Two shapes:
* **(4a) Condemn only where the drop is provably value-neutral.** This is what "redundant" was always
  supposed to mean, and bug 9 refuted it for the general case. Proving it requires the sibling's value,
  i.e. a search -- so it is expensive, and it is the honest form of the original premise.
* **(4b) DEMOTE INSTEAD OF DELETE -- condemnation as a RANKING signal, not a prune.** Keep the
  candidate in the list and rank it last. Then the candidate set is baseline's, no bound weakens, the
  tree is baseline's, and **the bar is met on every work metric by construction** -- while the
  practical effect is that a condemned line is scored only after everything else, and usually cut.
  **The compaction measurement is independent evidence that this is what condemnation has really been
  doing:** W = 2 against a mean list of 6.69, with 73.9% of lists longer than W, so deleting a
  front-rank entry promotes a rank-2 entry into the window. A filter whose value comes from promoting
  rank-2 entries into a width-2 window **is a re-ranker wearing a prune's clothes.** (4b) makes that
  explicit and free, and it also gives up nothing that (D1) gave up.

**WHAT THIS MEANS FOR THE THREE-WAY SHIP DECISION.** (4b) and (D1) are not both needed: (4b) subsumes
the reason (D1) existed (no list shortening, so no key folds, no positional renumbering, so the
all-paths fixpoint is free) **and** fixes the node growth that (D1) provably cannot. If (4b) measures
neutral-or-better on play, it is strictly the better build. It should be priced against (1) rather than
credited with (1)'s saving, since (1) helps both arms.

### Instruments added (all default OFF, counters only)

* `MTG_BP_PATHDEP_PROBE` -- drop-gate census: consultations, pending plan casts, peer-exempt split,
  tail, site kind, `plan_nocast`. An UPPER bound: `BpPlanCasts` is a NAME test, so a plan that cast
  one Boreal Druid and holds a second copy from before the draw reads as pending (Snow: inflated to
  143,677 of 8,296,920).
* `MTG_BP_CASTSET_PROBE` -- the exact measurement above, with the independent state fingerprint.
* `MTG_BP_KEY_CASTS_NONE` / `MTG_BP_KEY_SNAPSHOT_NONE` -- key-width arms. Both deliberately UNSOUND
  as play; units and misses only.
* `MTG_BP_KEY_CENSUS` (+ `MTG_BP_KEY_CENSUS_DUMP=<path>`) -- **the instrument that settled the spec
  question.** Process-global, never-cleared set of every key the bp-enum cache is consulted with,
  recorded before the `find`; prints `distinct=` and `builds=` at exit, and dumps the sorted key set so
  two arms' sets can be DIFFERENCED. Immune to residency, to the byte budget and to thread shape, and
  **reproducible** -- which `misses` and `units_total` are not. Needs no cap raising, so it replaces
  the whole "raise `MTG_BP_ENUM_CACHE_CAP` until clears reach 0" line of attack, which could never have
  reached 0 anyway (`plancache::Fits` also wipes). One mutex-guarded insert per key build.
  **Read `distinct`, not `misses`, for any question about how many states the search reaches.**

**A DEAD END, RECORDED SO IT IS NOT RETRIED:** `MTG_BP_ENUM_VERIFY` cannot answer "does the cast fold
carry information on Snow". Its baseline is 21.9% pre-existing, and the arms do not check identical
populations because the verifier itself perturbs cache residency: fold on 59,416/271,539 (21.883%),
fold off 59,496/271,698 (21.898%), all folds off 59,496/271,796 (21.890%). 80 counts against a
59,416 baseline settles nothing in either direction.

---

## 2026-09-17: THE ALL-PATHS RULE, MEASURED AT GAMES=10 -- 0.1% OF THE OVERSHOOT

The USER's rule (option A: *"only condemn in cases where all of the lines that reach that state
condemn"*) was run as a third arm against `off` and per-path condemnation on the full 10-game Snow
cell (d2/b0, `SNAPSHOT_NONE` all three arms, `logs/snow_perf/barcheck_bar10.log`). All three arms
play **byte-identical** (`7916f1f572f914e7`, avg 5.9000), which is itself the first result: on Snow,
condemnation in any form changes no play at all.

| metric | off | per-path | vs off | all-paths | vs off |
|---|---|---|---|---|---|
| hits | 26,841,846 | 31,573,408 | +17.63% | 31,569,033 | +17.61% |
| misses | 432,422 | 509,502 | +17.83% | 509,615 | +17.85% |
| clears | 50 | 59 | +18.00% | 59 | +18.00% |
| wave host nodes | 22,757 | 25,240 | +10.91% | 25,237 | +10.90% |
| slots | 2,517,447 | 2,976,767 | +18.25% | 2,976,432 | +18.23% |
| scored applies | 28,019,861 | 33,046,727 | +17.94% | 33,042,412 | +17.92% |
| units_total | 40,143,167 | 46,043,691 | +14.70% | 46,038,075 | +14.68% |
| **LOOKUPS (h+m)** | **27,274,268** | **32,082,910** | **+17.63%** | **32,078,648** | **+17.62%** |

**The all-paths rule returns 0.1% of per-path's overshoot: 4,262 of 4,808,642 lookups.** That is the
number the 0.09% path-disagreement rate predicted (290 of 331,245 key builds), and it confirms the
derivation rather than merely agreeing with it: the rule shrinks the condemn set by *path
disagreement*, and Snow's overshoot is not caused by condemning on the wrong paths. It is caused by
condemning lines that carry value on **every** path. Condemning LESS cannot fix that.

(`allpaths` drops 125,167 vs per-path's 125,159 -- slightly MORE, not fewer. Re-admitting changes the
tree, which changes how many consultations happen at all, so the drop count is a firing counter, not
a monotone measure of the condemn set. The script's `<` assertion is therefore too strong; the
verdict reads the lookup delta.)

## 2026-09-17: WHAT A CONDEMNATION *DOES* -- THE DECOMPOSITION (`MTG_BP_CONDEMN_DROP_MODE`)

The USER's bar names two separate things -- *"our only extra work is **checking** condemnation status
and **deciding what we need to implement**"* -- and every measurement so far had them fused. The flag
separates them. All three modes run the identical condemnation test; they differ only in the response.

| mode | response to a condemned candidate |
|---|---|
| 0 `DELETE` | not emitted (the shipped behaviour) |
| 1 `COUNT_ONLY` | counted, then **emitted anyway**. The control: checking without deciding. |
| 2 `DEMOTE` | emitted, and every continuation that casts it is ranked **last** (`std::stable_partition` after `MoveOrderPlans`, inside `BpEnumEntryFor` so the executor's replay indexes the same order) |

### Measured, Snow 8-game cell (gi=0..7, 16 s, `logs/snow_perf/armcheck_mode1.log`)

All four arms play byte-identical (`bd3f8a9af6ff6cee`, avg 5.6250); all three condemning arms have
identical firing counts (762 consultations dropped), so no row below is a no-power artifact.

| metric | off | count-only | demote | delete |
|---|---|---|---|---|
| hits | 410,167 | +0.00% | −0.01% | **+2.26%** |
| misses | 27,788 | +0.00% | +0.00% | −0.10% |
| wave host nodes | 5,931 | +0.00% | +0.00% | **+0.67%** |
| slots | 99,540 | +0.00% | +0.00% | **+1.32%** |
| scored applies | 365,400 | +0.00% | −0.02% | **+2.06%** |
| stillborn | 70,620 | +0.00% | −0.01% | +1.11% |
| units_total | 1,647,283 | +0.00% | −0.00% | **+1.08%** |
| **LOOKUPS** | **437,955** | **+0.00%** | **−0.01%** | **+2.11%** |

Firing counters: `count` = `emitted_anyway=762` (== drops, the assertion the mode exists to make);
`demote` = `emitted_anyway=762 demote_lists=424 demote_plans=189`.

**THREE FINDINGS.**

1. **CHECKING IS FREE.** `count-only` is `+0.00%` on every row, to the last digit, against 762
   consultations that all fired. So the half of the bar the USER explicitly allowed costs *nothing
   measurable* -- the entire overshoot is the *deciding*, i.e. the deletion.
2. **DEMOTE MEETS THE BAR.** Every row is `<=` baseline, several marginally below it. This is the
   first form of condemnation that satisfies the spec as written, and it satisfies it by
   construction rather than by luck: the continuation list keeps baseline's LENGTH, so no rank
   compaction occurs, no bound weakens from a shortened list, and the search explores baseline's
   tree.
3. **DEMOTE ALSO SAVES ESSENTIALLY NOTHING** (−0.01%). That is not a defect of the implementation, it
   is a fact about where the work is: **wave 0 emits `bp_choice = 0..W-1` blind**, so the slot count
   does not depend on which entries are in the window. Substituting one entry for another cannot
   change the number of applies. The only mechanism that shortens the deferred-wave walk is making
   the list shorter -- which is deletion, and deletion costs 17.6% elsewhere.

### What that means

On Snow, condemnation cannot pay for itself in *either* form: deletion overshoots by +17.6% and
demotion breaks even. The upside the design was premised on -- *"it is possible for us to do less
work if there are lines we never run"* -- does not materialise at `b0`, because at unlimited budget
the deferred waves walk the whole list regardless of its order.

Demotion is still the better shape, for a reason that is not about cost: a demoted line stays
**reachable** by a later deferred wave, so it is not a truncation at all, which retires the
exclusive-slot defect (bug 9) by construction rather than by argument.

**THE NEXT MODE TO TRY (not built).** `DEFER` -- demote, and let the deferred-wave loop treat the
condemned tail as *"only if budget remains"*. At `b0` that saves nothing by definition, which is
exactly why every measurement in this document is blind to it: the whole cell is unlimited-budget.
At a finite play budget it is a real saving, and it stays lossless under the USER's soundness bar
(*"unrecoverable means not recoverable at unlimited budget (0) and depth 8"*) because at unlimited
budget the tail is still walked. Pricing it needs a cell at Snow's actual play settings, not `b0`.

## 2026-09-17: THE MISS CLAUSE IS A CACHE-POLICY BUG, AND IT IS ALREADY FIXED BY THE CAP

The bar's first clause -- *"there should be no additional misses"* -- is separable from the rest, and
it is not about which states the search discovers. The bp-enum cache is **clear-on-full at 8192, not
LRU**, so misses track evictions. Four arms, 8-game cell (`logs/snow_perf/armcheck_cap1.log`):

| metric | off | off + cap 400k | on | on + cap 400k |
|---|---|---|---|---|
| misses | 27,788 | 26,930 (−3.09%) | 27,760 (−0.10%) | **26,902 (−3.19%)** |
| clears | 1 | **0** | 1 | **0** |
| LOOKUPS | 437,955 | +0.00% | +2.11% | +2.11% |

With the cache no longer thrashing, **the condemning arm has FEWER misses than baseline**
(26,902 vs 26,930). The miss clause is met; the lookup clause is untouched by the cap, exactly as it
should be (the cap moves the hit/miss split, not the number of consultations). The two clauses have
different causes and should stop being reported as one number.

---

## 2026-09-18: THE PRUNE DOES DO LESS WORK. THE SOUNDNESS GUARD IS THE ENTIRE COST.

**USER, 2026-09-18:** *"I don't want waves. I want to delete unnecessary paths according to
condemnation while eliminating any duplication."* and *"There should be a solution that does less
work. We need to find and implement it."*

There is, and the previous section was measuring the wrong arm. **Every `armcheck.sh` run in this
investigation pinned `MTG_BP_CONDEMN_NEW_OPTION=1`** -- the exclusive-slot soundness guard, which
spares **81% of all drops**. The harness put the fixed probe variables AFTER the per-arm ones, and
`env` takes the last assignment, so an arm could not override it. Every arm was the expensive one.

### Snow, 10 games, d2/b0, `SNAPSHOT_NONE` all arms, play BYTE-IDENTICAL in all four (`7916f1f572f914e7`, avg 5.9000)

| metric | off | guarded (shipped) | **unguarded** | unguarded + len-memo |
|---|---|---|---|---|
| hits | 26,842,249 | +17.73% | **−24.31%** | −24.31% |
| misses | 431,881 | +18.35% | +0.49% | +0.49% |
| wave host nodes | 22,757 | +11.08% | +3.69% | +3.67% |
| slots | 2,517,447 | +18.39% | +1.30% | +1.30% |
| scored applies | 28,019,734 | +18.05% | **−23.30%** | −23.30% |
| rolled | 3,139,744 | +21.70% | **−14.31%** | −14.31% |
| stillborn | 1,680,273 | +19.88% | +4.77% | +4.77% |
| **units_total** | **40,142,740** | **+14.84%** | **−19.43%** | **−19.43%** |
| **LOOKUPS (h+m)** | **27,274,130** | **+17.74%** | **−23.92%** | −23.92% |
| wall | 860 s | 962 s | **667 s (−22.4%)** | 667 s |
| drops | 0 | 125,953 | **754,719** | 754,719 |

**The filter, allowed to actually filter, is a −19.4% units / −23.9% lookups / −22.4% wall win with
identical play.** This reproduces the 2026-09-17 finding (UNFIXED −16.2% / FIXED +14.5%) in the
current harness and at a larger drop count.

**IT IS NOT A "TOLL PLUS A BENEFIT" MODEL, AND THAT MATTERS.** The guarded arm drops 125,953 and
costs +14.84%; the unguarded arm drops 754,719 and saves 19.43%. The cost is *non-monotone in drops*:
a small number of drops is strictly worse than none, and a large number is strictly better than
either. Deleting a few candidates perturbs the continuation lists (which is what grows nodes +11%,
slots +18%, stillborn +20%) without deleting enough lines to pay for the perturbation.
**A half-applied prune is the worst of both.** That is the single most useful thing this table says,
and it retires the "condemnation has a fixed toll" framing that three earlier sections are built on.

### THE BLOCKER IS EXACTLY ONE GAME, AND IT STILL BITES

`gi=1357` (930000 block, d5/b20), re-verified today against the current binary:

| arm | result |
|---|---|
| condemnation off | **8** |
| guarded (shipped) | **8** |
| unguarded | **9 (unwon)** |

So the guard is still load-bearing and the default cannot simply be flipped. The mechanism is the one
`BpSiteAddedAPayableOption`'s comment records: the continuation slot is EXCLUSIVE, so dropping Skred
hands the slot to Rimefeather Owl, whose `{5}{U}{U}` taps a Boreal Druid and turns exactly-lethal
1+1+5 = 7 into 6.

### NEW TODAY: MAKING THE SLOT NON-EXCLUSIVE DOES NOT FIX IT

If the defect were purely the exclusive slot, then scoring "cast nothing more" as a real alternative
would recover the game -- EMPTY preserves the Druids and the exact-lethal attack. It does not:

| arm (all unguarded) | gi=1357 |
|---|---|
| control | 9 |
| `MTG_BP_EMPTY_ARM=1` | 9 |
| `MTG_BP_BASE_EMPTY=1` | 9 |
| both | 9 |

**So the search SCORES THE OWL LINE ABOVE THE WINNING EMPTY LINE.** That is a VALUATION defect, not a
condemnation defect: condemnation only removes the third option (Skred) that happened to outrank the
Owl for unrelated reasons. `EnumeratePlans` already owns the right debit
(`CollectAttackingManaSources` / `AttackTapDiscount`: "a subset that taps them to pay loses their
attack"), gated on `is_pre_combat`. **The next step is to find why that debit does not make EMPTY beat
the Owl in the continuation** -- if it can be made to, the guard becomes unnecessary and the −19.4%
is unlocked without any exemption, which is what the USER's instruction asks for.

### A LEVER THAT DID NOT WORK, RECORDED SO IT IS NOT RETRIED

`MTG_BP_NSKIP_GLOBAL` -- the cross-node continuation-LENGTH memo. NSKIP's own comment is right that
the length "is not unknowable, it is merely unremembered", and its memo `bp_known_n` really is
node-local and keyed positionally, so the idea was to key it on
`(node state dedup key, base plan fingerprint, bp_at)` -- all available before the apply, and a LENGTH
needs no depth/budget split because it is a property of the state alone.

**Built, verified sound, and it is a DUD on Snow.** `MTG_BP_NSKIP_GLOBAL_VERIFY=1` (record and compare,
never skip) reports **0 mismatches** over repeat keys, so the key is fine enough. But the memo barely
populates: on the 10-game cell, `records=4607 hits=1027 skips=3`. Three slots skipped out of 1.76M
stillborn. The reason is the RECORD condition, inherited from NSKIP: only a wave-0 `bp_choice == 0`
variant inside the FSLineWin node loop ever writes, which on Snow is a tiny fraction of the applies
that discover a length. Kept default-OFF with its verifier; widening the record site is where any
future attempt must start, not the key.

### HARNESS CORRECTIONS

* `armcheck.sh` now places per-arm variables LAST so an arm can override a default. The old order is
  what hid this entire finding.
* `SNAPNONE=0` measures at the real shipping key. Checked today: `MTG_BP_KEY_SNAPSHOT_NONE` has **no
  effect at all** on a condemnation-OFF arm (identical hits/misses/units), and on the condemning arms
  it moves only `misses`, not `units` or `lookups`. So every comparison above is robust to it. The
  memory note claiming the narrowing alone costs +16.6% units does NOT reproduce on this binary.

---

## 2026-09-18: THE GUARD IS NAME-BLIND, AND FIXING THAT RECOVERS 3/4 OF ITS COST

**USER, 2026-09-18:** *"We shouldn't be guarding like that? We should be condemning particular card
names etc."* / *"if the card is new, but the new card's name has not yet been condemned then we still
keep it, but we would drop a card whose name had already been condemned"* / *"if the card is one we've
already passed on we don't reconsider it."*

**THE DEFECT.** Everything else in condemnation is built on NAMES -- the `dominated` scan keys on
`m_name_hash` + `BpCardWasInHandBefore`, `BpPlanCasts` is a name test. `BpSiteAddedAPayableOption`
alone is name-blind: it asks *"did the site put ANY new payable card in hand?"* and, if so, spares the
drop -- whatever the candidate is and whatever the new card is. So a site that draws a **second copy
of a name the plan already declined at its own slot** disarms the filter for that entire consultation,
on the strength of an "option" that is new in no sense the premise cares about. That is the mechanism
behind the guard sparing **81% of all drops**.

**THE RULE (`MTG_BP_CONDEMN_NEWOPT_BYNAME`, default OFF).** A new card counts as a new option only if
its NAME was not already in hand before the breakpoint. `BpNamePassedOnBefore`.

**THE EXPIRY EXCEPTION, and it is load-bearing** (USER: *"the only exception to the 'name' thing is
new cards with expiry, such as from Light up the Stage. We don't condemn cards that expire earlier
than the one that was condemned"*). A fresh copy that expires EARLIER than the copy we passed on is
not the same decision: the plan declined a patient copy, which says nothing about a copy that will be
exiled this turn if unused. So "passed on" requires `BpCardUrgency(old) <= BpCardUrgency(new)` --
extracted into one helper so the candidate side and the guard cannot drift, since this is the exact
test the `dominated` scan already applies to the candidate. **Inert on Snow** (no staged cards -> both
urgencies are `INT_MAX`), so every Snow number below is unaffected by it; it matters for the Light Up
the Stage / Expressive Iteration decks (kitty, Hinata), which is precisely where a name-only version
would have condemned the urgent half of every staged pair.

**THE CANDIDATE SIDE NEEDED NO CHANGE** -- it already implements the USER's rule. The `dominated` scan
condemns a card when a copy of the same name was in hand before and is at least as urgent, so a
freshly drawn copy of an already-passed name is condemnable today. The guard was the only place the
name test was missing.

### Snow, 10 games, d2/b0, play BYTE-IDENTICAL in all four arms (`7916f1f572f914e7`, avg 5.9000)

| metric | off | guarded (shipped) | **byname** | unguarded |
|---|---|---|---|---|
| hits | 26,842,249 | +17.73% | **+2.69%** | −24.31% |
| misses | 431,881 | +18.35% | +17.92% | +0.49% |
| wave host nodes | 22,757 | +11.08% | +11.33% | +3.69% |
| slots | 2,517,447 | +18.39% | +18.18% | +1.30% |
| scored applies | 28,019,734 | +18.05% | **+3.58%** | −23.30% |
| stillborn | 1,680,273 | +19.88% | +21.11% | +4.77% |
| **units_total** | **40,142,740** | **+14.84%** | **+3.62%** | **−19.43%** |
| **LOOKUPS (h+m)** | **27,274,130** | **+17.74%** | **+2.93%** | **−23.92%** |
| wall | 971 s | 1076 s | **973 s** | 736 s |
| drops | 0 | 125,953 | **526,566** | 754,719 |

`refused_samename=442,641` -- the rule fires hard. Drops go 125,953 -> **526,566**, 70% of the way to
unguarded, and it **recovers 11.2 of the guard's 14.84 units points** (and 14.8 of its 17.74 lookup
points), landing at **wall parity with baseline**.

**AND IT KEEPS BOTH KNOWN REGRESSIONS SAFE**, which is the whole point -- it is a narrower guard, not
a weaker one. Re-verified at d5/b20 on the current binary:

| game | off | guarded | **byname** | unguarded |
|---|---|---|---|---|
| gi=487 | 6 | 6 | 6 | 6 (repaired since; no longer a blocker) |
| gi=1357 | 8 | 8 | **8** | 9 (unwon) |
| gi=1553 | 6 | 6 | **6** | 7 |

gi=1357 is safe *by construction*: no Rimefeather Owl was in hand before the Scrying Sheets
activation, so the Owl is still a genuinely new name, the guard still fires, and Skred is still spared.

### WHAT IS LEFT, AND WHERE THE NEXT LEVER IS

byname still spares 228,153 drops (30%) and those cost the difference between +3.62% and −19.43%, so
the residual exemption is worth ~23 points. Note the shape: byname's `nodes` (+11.33%), `slots`
(+18.18%) and `stillborn` (+21.11%) are still at GUARDED levels while its `hits`/`scored`/`units` came
right down. **The tree only shrinks once enough is dropped** -- unguarded is the only arm where
`nodes` and `slots` come back toward baseline. That threshold, not a linear saving, is what the next
lever has to cross.

The obvious next narrowing is per-CANDIDATE rather than per-consultation: the guard still spares
*every* candidate whenever one genuinely-new payable name arrives, but the exclusive-slot harm only
reaches a candidate that actually contests the slot with that new option. Sparing only the contested
candidates is the same kind of granularity fix as this one, one level down.

**NOT ADOPTED YET.** The default stays OFF: three games is not the adoption bar. The gate is the
paired 2000-game d5/b20 cell the guard itself was adopted on (`logs/snow_perf/condfix_wins`), read
per-game via `test/paired_arms.py`, not by deck mean.

### gi=919 ROOT-CAUSED: T1 LAND CHURN, NOT A DELETED LINE

USER rule (2026-09-18): *"a general rule I recommend with condemnation cases is not to immediately
reject if you see something unrecoverable. It's best to understand the why and potentially fix the
condemnation order if that is the cause."* And, sharpening it: *"The real question is, if X was
intended to be cast, why we can't cast it in an order that is consistent with the order I made?"* --
*"This is a trickier question than 'can it run the original winning line?'. Obviously it cannot
because of condemnation and that condemnation may be just fine."*

**STEP 1 -- what byname condemns that guarded does not.** `MTG_CONDEMN_WHO=1` on gi=919, diffed
between arms, is exactly two cards:

| drop | rank | site | site_rank |
|---|---|---|---|
| Abominable Treefolk | 135 | Scrying Sheets / Frost Augur | 103 / 111 |
| Ice-Fang Coatl | 302 | Scrying Sheets / Frost Augur | 103 / 111 |

All `where=srch` (no executor drops), ~85% `plan_n=1 tail=0`. Both ranks are what
`SnowProvider::CastOrderRank` intends: commitments are `100 + mv*8 + role*2 + big` (Treefolk mv4 =
135) and "THE DRAWS, last" are `300 + mv` (Coatl mv2 = 302, because its ETB draws a card).

**STEP 2 -- and neither card was the one the winning line needed.** `byname` *casts* Abominable
Treefolk at T6; Ice-Fang Coatl is kept to hand in BOTH arms and cast in NEITHER.

**STEP 3 -- the lines first diverge at TURN 1, ON THE LAND DROP.** Via `--game-trace-dir` with a
manifest carrying `"game_index": 919` (the chunk offset, which reproduces the single `--seed
930919 --game-index 919` run exactly):

| | guarded (wins T7) | byname (T8) |
|---|---|---|
| T1 land | Snow-Covered **Forest** | Snow-Covered **Mountain** |
| T1 cast | **Boreal Druid** `{G}` | -- nothing |

**There is no breakpoint at T1** (no Scrying Sheets on board yet), so nothing was condemned there, and
Boreal Druid sits at rank 109 exactly where the order puts it -- nothing prevented casting it at its
slot. **So the answer to the USER's question is that in this game we CAN cast it in an order
consistent with the order; the search simply did not choose to.** The regression is mediated purely
through the search's VALUES: condemnation changes continuation sets deep in the tree, the deep
evaluations move, and at a finite budget the early land pick moves with them. That is BUDGET CHURN,
a different failure class from gi=1357 (where condemnation genuinely deleted the Skred occupying the
slot), and it is what the d5/b0 recoverability run tests.

**A WRONG TURN, RECORDED SO IT IS NOT RETAKEN.** Ice-Fang Coatl's FLASH was proposed as the root
cause -- it is ranked 302 as a draw, and a flash card arguably has no sorcery-speed slot at all, so
"offered at its slot and declined" would be a false premise. Two corrections: flash IS modelled (it is
a KEYWORD -- `keywords: ['Flying','Flash']` -> `Keyword::Flash`, read in TurnSolver's stack/phase
test; the first check looked in `parameters` and wrongly concluded it was unmodelled), and USER:
*"Flash doesn't matter here"* -- in a goldfish model nothing makes holding it better, so the
main-phase decline is a real decision.

### HELD-OUT SWEEP (in flight)

`logs/snow_perf/sweep3.manifest.json` -- ONE pooled batch, 6 jobs = {off, guarded, byname} x {seed
940000, 950000} x 2000 games at d5/b20, 16 threads (16/16 workers busy). Seeds are held out from the
q2 cell (930000..931999) and the bases are spaced 10,000 apart, well clear of the 2,000-game span.
Read per-game, never by deck mean:
`python3 test/paired_arms.py logs/snow_perf/sweep3_wins --base guarded --arm byname --list-moved`.

`MTG_BP_CONDEMN_NEWOPT_BYNAME` is now also a **heurarm slot**, which is what lets the arms share one
pooled queue. NOTE the heurarm vector is folded into every bp-enum key, so a pooled run puts the arms
in DISJOINT key spaces: fine for a QUALITY sweep, but never read arm-vs-arm CACHE numbers off one --
use `armcheck.sh`, which sets the arms in the environment.

---

## 2026-09-18: WHY THE COST IS NON-MONOTONE IN DROPS -- FULLY ROOT-CAUSED

USER: *"That makes no sense to me. I would like to dig into that until we have it fully addressed."*
The anomaly: `guarded` drops 125,953 and costs **+14.84%** units, while `unguarded` drops 6x more
(754,719) and **saves 19.43%**. More pruning, less cost -- but the intermediate arm is the expensive
one. It is not about bounds, depth or cutoffs. It is about **how many breakpoints the search reaches**.

### THE MEASUREMENT (Snow 10 games d2/b0, `armcheck_byname10_*`)

| arm | drops | site-8 breakpoints | mean list len | len-1 lists | nested-slots | units |
|---|---|---|---|---|---|---|
| off | 0 | 1,569,675 | 17.61 | 57,573 (3.7%) | 562,943 | — |
| guarded | 125,953 | **+15.9%** | 17.92 | 101,332 (5.6%) | **+16.5%** | +14.84% |
| byname | 526,566 | +13.2% | 15.97 | 185,073 (10.4%) | +13.5% | +3.62% |
| unguarded | 754,719 | **−2.7%** | 13.47 | 235,863 (15.4%) | **−4.6%** | −19.43% |

**TWO EFFECTS RUN IN OPPOSITE DIRECTIONS.**

1. **List shortening is MONOTONE in drops** -- length-1 lists go 3.7% -> 5.6% -> 10.4% -> 15.4%. This
   is the prune working, and it is pure saving.
2. **Breakpoints reached is NOT** -- it rises 16% then falls below baseline. Everything expensive
   (slots, scored applies, lookups) is charged PER BREAKPOINT REACHED, so effect 2 dominates at low
   drop rates and effect 1 only wins at high ones.

`nodes` tracks breakpoints reached almost exactly (22,757 / 25,279 / 25,336 / 23,596), which is why
the tree *looked* like it was growing: it was hosting more waves, not searching deeper. Depth is
pinned at 2 and the budget is unlimited, so nothing here can change the horizon.

### THE MECHANISM, WITH EVIDENCE AT EACH STEP

**Drop composition by card name** (`MTG_CONDEMN_WHO=1`, 8-game cell) is the key:

| dropped card | guarded | byname | unguarded | role |
|---|---|---|---|---|
| **Frost Augur** | **1** | 237 | **475** | **IS a breakpoint site** (its `{S},{T}` tap-draw) |
| **Boreal Druid** | 4 | 145 | **370** | **mana SOURCE** |
| Skred | 581 | 708 | 2,504 | mana sink (goldfish-inert) |
| Marit Lage's Slumber | 66 | 933 | 2,845 | mana sink |
| Ice-Fang Coatl | 0 | 2,063 | 4,480 | mana sink |
| Abominable Treefolk | 0 | 206 | 1,309 | mana sink |
| TOTAL | 762 | 6,640 | 15,876 | |

**CONDEMNING A MANA SINK FREES MANA IN THE CONTINUATION.** The trailing pass can then afford the
`{1}{S}` tap-draw activation it could not before, which **opens another breakpoint** -- and a
breakpoint costs a whole continuation list plus its wave slots, far more than the cast it replaced.
`guarded` drops almost only sinks (581 of its 762 drops are Skred; it condemns the Augur exactly
ONCE and the Druid 4 times), so it manufactures breakpoints: **+15.9%**.

**CONFIRMED BY THE NESTING COUNTERS, which is what the mechanism predicts:** the extra breakpoints are
opened INSIDE continuations, so `nested-slots` must move in lockstep with breakpoints reached. It
does -- +16.5% vs +15.9% for guarded, −4.6% vs −2.7% for unguarded. `nested-scored` is already
2,807,416 of 28,019,734 applies at baseline (10%), so each nested breakpoint is expensive.

**CONDEMNING A MANA SOURCE OR THE SITE CARD DOES THE OPPOSITE.** `unguarded` condemns Boreal Druid
370 times and Frost Augur 475 times, removing both the mana that funds an activation and the
permanent that provides one, so breakpoints fall back to baseline and the list-shortening saving is
finally allowed to show.

### WHAT THIS MEANS

On Snow, condemnation at a low drop rate does not remove work -- it **converts "spend mana on a card"
into "spend mana on a tap-draw activation"**, and the second is far costlier because it opens a
breakpoint. The +14.84% is that conversion, not overhead and not bound weakening (the earlier
"weakened cutoffs / bound weakening" reading in this document is superseded: depth and budget are
fixed, and the growth is entirely in breakpoints hosted).

Two consequences worth acting on:

* **The lever that would actually cut Snow's cost is bounding NESTING** (`BpSearchDepth`, nested
  discovery), which is independent of condemnation and helps the `off` arm too.
* **A prune that frees a resource is not automatically a saving** on a deck whose expensive decision
  is an ACTIVATION funded by that resource. This generalises beyond Snow: any deck with a
  mana-costed breakpoint site has it.

### HELD-OUT QUALITY (4,000 games, seeds 940000 + 950000, d5/b20, paired vs `guarded`)

| comparison | delta | better / worse | moved |
|---|---|---|---|
| byname | +0.0003 ± 0.0004 | 1 / 2 | 3/4000 (0.07%) |
| off | +0.0000 ± 0.0006 | 3 / 3 | 6/4000 (0.15%) |

byname is a wash against the shipped guard (and moved ZERO of the 2,000 block-B games). **And so is
`off`** -- the "condemnation earns its keep, off is 3 games worse" result from the 930000 block does
NOT replicate on held-out seeds. So condemnation's QUALITY benefit on Snow is not established; its
case rests entirely on the unbounded-search cost, where `byname` is worth 11.2 units points over the
shipped guard.
