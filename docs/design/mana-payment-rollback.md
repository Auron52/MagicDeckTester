# Mana-payment rollback: a rare, needs-based repair of past generic-pip payments

**Status (2026-10-05): heuristic layer BUILT (A/B round 2 running); the INSTRUMENT (step 2) and
the ROLLBACK itself (step 3) BUILT, both default OFF, unmeasured.** Origin: USER, 2026-10-05,
prompted by the Snow reference `references/Snow/claude_s4_gi3.json` (see "Worked example").
Self-contained.

## As built (2026-10-05) — instrument and mechanism

* **Instrument** `MTG_PAY_ROLLBACK_AUDIT=1|2` (`src/ai/PayLedger.h`, count-only, real play): records
  every committed tap with the alternatives it had, and at (1) an outright payment failure and
  (2) a dig's find declined on colour (`SnowLookFoundPlayable`), asks whether one same-turn re-pay
  per missing pip would have rescued it. First readings, d0, lever off:
  Snow ×1000: dig-find declined 230, **51 rescuable (~5% of games)**; payment failures on colour 18,
  6 rescuable. EDF ×300: 1,908 colour failures, only 8 rescuable — EDF's `{C}` shortfalls in the
  d0 go-off have no alternative to swap, so they are not a rollback case. Every Snow rescue named
  the same shape: *re-pay the generic from Boreal Druid / Coldsteel Heart / Scrying Sheets instead of
  a basic* — i.e. the creature band (bodies after lands) or a batch-prepay assignment spent the
  colour. The needs lever trimmed 51 → 46 only: these are exactly the cases where the colour needed
  depends on the card the dig reveals, which no forward heuristic can know.
* **Mechanism** `MTG_PAY_ROLLBACK` (`src/ai/PayRollback.h`; ledger `PayRollbackLedger` on the
  GameState, ~110 bytes, lockstep by construction). At the same two sites, per missing coloured pip
  X: find an earlier tap of A (produces X) whose pip B — untapped, never a payment source this turn,
  cannot make X — could have paid; untap A, tap B; re-try. Guards: one rescue per turn, no nesting,
  dominant swaps only (so no clairvoyance), both sources side-effect-free one-mana direct sources,
  nothing floating, and **disabled for the turn by any untap effect** (`untap_effect`, set by
  `EtbUntapLands` / `ApplyUntapCreature` / `UntapManaSources` / `RitualUntapSources` /
  `UntapSnowPermanents`) because "untapped now" no longer implies "untapped then". The event is
  written to the viewer stream as *"re-tapped: B for the generic pip instead of A (needs <card>)"*.
  Census `tried` / `fired` prints at exit when the lever is on.
* **First measurement of the mechanism** (Snow d0 ×1000, same seeds as the instrument readings):
  real-play rescues **47** against the instrument's 51 rescuable (the other four hit a guard:
  Jorn's attack untap, or no side-effect-free swap), i.e. ~4.7% of games and ~0.7% of turns —
  comfortably "uncommon"; residual rescuable dig-declines 0; average 6.4880 → 6.4840. Every event
  had the dominant shape (*"re-tapped: Boreal Druid / Coldsteel Heart / Scrying Sheets for the
  generic pip instead of Snow-Covered Island/Forest (needs Frost Augur / Ice-Fang Coatl / ...)"*).
  Rollout-world rescues are counted separately (`tried`/`fired` vs `REAL play`): the site-8 re-solve
  runs rollouts even at d0, and those counts scale with search effort, not with play.
* **Two lessons from wiring it in.** (1) The dig gate (`SnowLookFoundPlayable`) says "worth" for a
  colour-short find because `MTG_SNOW_LOOK_COLOR` is default OFF, and the re-solve then silently
  never casts the find — so the rescue must be triggered by the colour test itself
  (`SnowLookFoundColorShort`), BEFORE the gate, not by the gate's answer. (2) At the payment-failure
  site the retry can still fail (snow restriction, reserved mask); the swap is then undone
  (`payroll::UndoLast`), because a failed payment must be side-effect-free.
* Not built yet: the human-triggerable repair in the viewer, and the rollback's own A/B (it must
  clear the same bar as the levers, plus the per-deck firing-rate tripwire). The scenario suite
  passes 129/129 with the lever on.

## The framing (USER, 2026-10-05, second pass)

> *"Essentially we want good heuristics for choosing mana sources, but it seems like this alone is
> not consistent enough for the needs of fully accurate search. The rollback is a backup which
> attempts to bridge this consistency gap."*

So the division of labour is: the HEURISTIC carries the load and must be good enough that the
rollback is rare; the ROLLBACK exists because a per-payment heuristic cannot be made consistent with
what an accurate search needs on every line, and it closes that gap on demand. It is a backup, not a
second solver — see "It must stay RARE".

> *"We should try to improve our heuristics to reflect need (first hand and then deck for cases
> where we are drawing cards, since you want to retain the most potentially useful colour for the
> deck we are playing) and optimal order and only then dig into the rollback."*

> *"Regarding using colourless for generic costs that entirely depends on what our deck needs. EDF
> is a good example of a case that doesn't work nicely with this rule, so we need to be cautious.
> The handling should be generalized, but choose different types of mana based on the deck and
> hand."*

### What the rollback records — the two classes of discarded option

> *"The idea for the rollback would be to record some options for mana/land retention we did not
> consider, particularly surrounding colours and retry there only when we need that option in a
> subsequent step. Even better, if we retry there with what we need to retain in mind it should be
> easier to manage (keeping track of any mana we don't have access to yet). So one example would be
> if we threw away a colour for generic mana and another would be if we tapped a source for one of
> the colours it could produce and gave up the others. Because of our order prioritizing usage of
> simple lands first, I think most of our problems fall into the first camp? (that is spending types
> of mana we need for generic). Nonetheless, the second will occur some of the time and also needs a
> more general solution."*

1. **A colour spent on a generic pip.** The ledger entry: the source tapped, the colour(s) it
   produced, the pip it paid (generic), and the alternative sources that could have paid that pip
   instead at the time (untapped, able, not otherwise committed). Expected to be the common class,
   because the ladder spends mono sources first and the generic pips are where the colour choice is
   loosest.
2. **A multi-colour source tapped for ONE of its colours, the others given up.** The ledger entry:
   the source, the colour chosen, the colours foregone, and the alternative sources that could have
   paid the chosen colour instead. Rarer under the scarcity ladder (flexible sources tap last) but
   it needs the same general mechanism — the retry is "pay this pip from a different source so the
   flexible one is still up".

In both classes the retry is **goal-directed**: the later line that is short of colour X asks the
ledger for an entry whose alternative keeps an X-producer up, and re-pays THAT pip from the
alternative with X marked as "must retain". Tracking the mana the current line cannot reach yet
(the colours it needs that no untapped source provides) is what tells the retry what to retain, and
it is what keeps the search bounded: one targeted re-pay per missing colour, never a re-solve.

## What the first train-seed A/B taught (2026-10-05, regression cells x 4 pooled arms)

Round 1 (56,060 paired games per arm): `needs` +0.0000 overall but NOT uniform per deck -- fluctuator
d0 -9 and melira d0 -9 (clear wins) against treasure_hunt d0 +11 (two games 5 -> unwon), hinata +2,
antilife +2; `attack` moved 14 games, fivecolour 0/7. Every loss class was traced to one game and
each turned out to be something the LADDER had priced that the harm model could not see. Recorded
here because each is a general property of "needs" models, not a tuning constant:

| loss | mechanism | repair |
|---|---|---|
| Fluctuator s10 T3 (reference) | Capital City (any-colour filter) credited as supply of five colours and its `{C}` mode charged five shortfalls, so it was HELD while the duals paid | a conversion source is not supply of its outputs; only its free `{C}` counts |
| th d0 gi442, 5 -> unwon | Cascade Bluffs' fed mode charged 0, so needs promoted it over a harm-carrying Steam Vents; the `{1}{R}{R}` payload lost its conversion | filters are DEFERRED: compared once against the best direct source at that source's harm, i.e. by ladder rank -- needs reorders direct sources only |
| hinata d0 gi229, T7 -> T8 | Izzet Boilerworks (yield 2) charged per colour, so an Island paid instead and the bounce land's second mana floated | harm is per mana produced |
| antilife d0 gi124, T5 -> T6 | Grove's drip (opponent +1 life) is not a colour, so needs overrode the ladder's +1 nudge | drip = one colour's worth of harm; depletion counter = half |
| fivecolour d0 gi94, T5 -> T6 (`attack`) | Faeburrow Elder is printed 0/0 with `domain_self_pump`; `EffectivePower()` reads 0, so the biggest body ranked smallest | hold value uses combat's power: `EffectivePower() + ComputeLordBonus()` |

The pattern: the scarcity ladder is an ordinal encoding of several DIFFERENT costs (colour access,
conversion, yield, life, counters). A needs model that prices only colour access must either price
the rest in the same unit or leave those sources where the ladder put them. Round 2 is the re-run
with the repairs above.

## What the second train-seed A/B taught (2026-10-05, same cells, repairs above applied)

Round 2 (56,060 paired games per arm; base arm 155/155 identical to GT): `attack` 6 better / 0
worse (fivecolour d0 4/0, d3 1/0, mirrorwing d0 1/0; every deck net <= 0). `needs` 38 better / 23
worse, -0.0003 overall, but per deck still NOT uniform: treasure_hunt 4/10 (+6 turns), auras +1,
fivecolour +1, selesnya +1 against fluctuator d0 9/1, melira 7/1, dragons 5/0, fungusb 5/1. A
per-candidate instrument (`MTG_TAPDBG=1` now prints `[nddbg]` -- the pip, D, S and every
candidate's harm/rank/hold key) replaced guessing; every loss class again traced to one game:

| loss | mechanism | repair |
|---|---|---|
| th d0 gi50 (T4 -> T5), gi218, gi687 | the round-1 DEPLETION charge (half a shortfall) outranked the ladder: Temple of Epiphany paid `{1}{U}` ahead of Saprazzan Skerry's `{U}{U}`, so the Hunt's draw (Land's Edge `{1}{R}{R}`) found Skerry + Ferrous Lake able to make one `{R}` | depletion is NOT priced in harm; the ladder's +1 nudge and its dep tiebreak keep it, inside equal harm |
| (instrument) | Ferrous Lake (`ramp_filter`, "`{1}`, `{T}`: add `{U}{R}`") was counted as free `{C}` supply | a ramp filter has no free mode: not supply of anything |
| fivecolour d0 gi414, selesnya gi623, fluctuator gi618, melira gi725 | demand was the WHOLE hand (`ComputeRefloatDemand`): Blood Crypt held for an eight-mana `{4}{U}{B}{B}{R}` on five sources, Selesnya Sanctuary's only W held for a `{4}{W}{W}` two short -- and every land untaps, so the hold bought nothing and cost the ladder's order | demand is what could still be cast THIS TURN: hand and board costs with MV <= (untapped - this cost), the same cap the library half already used |
| dragons d0 gi340 (a GAIN the cap removed: T6 10 damage -> 9) | the uncapped hand had protected R by accident; the real need was Scourge of Valkas' firebreathing in combat, which `ApplyFirebreathing` pays from the whole leftover pool and which no demand set counted | repeatable combat pumps (`firebreathing_cost`, `team_pump_cost`) on a turn whose plan attacks demand pips x (remainder / MV) |

Re-running every round-2 mover at d0 on the rebuilt binary: 15 better / 0 worse / 22 same (round
2's binary: 38/23 over all cells). The gains that did not survive the cap (dragons gi551, fungusb
gi28, melira gi822) were read with the instrument and the game logs: they are d0 butterflies -- a
Peat Bog counter spent a turn earlier changed turn-5 mana and the greedy runner cast the bigger
spell first and lost a turn; a tie between Mountain and Gruul Turf for `{2}` broke by battlefield
order and floated a mana exactly as base does. One real shape remains unmodelled and is recorded,
not fixed: on a `{3}` with Llanowar Wastes, a Pathway and Orzhov Basilica (melira d0 gi725) the
per-pip greedy took the painland at harm 0 over the bounce land at harm 32 and then needed the
bounce land anyway -- four mana for a three-cost. Pricing it needs a two-pip lookahead ("this
multi-mana source pays the next pip too"), which the per-pip comparator cannot express.

Verification of the rebuilt binary at the levers' defaults is by construction (every change sits
behind `nd_live`); with all three levers on: scenarios 129/129, the four motivating references
replay (1 ok, 3 repaired, 0 drift), Snow d0 x1000 real-play rescues 47/591 and avg 6.4840 unchanged.

## Round 3 (depletion + ramp-filter repairs only, SIX pooled arms) and the usable-mana repair

Round 3 ran the stage-4 binary (the first two repairs above, demand still the whole hand) with
every arm in one pool -- base / needs / attack / both / rollback / all3, 930 jobs, 56,060 paired
games per arm, base 155/155 identical to GT:

| arm | better / worse | ALL GAMES delta | per-deck |
|---|---|---|---|
| `rollback` | 14 / 0 | -0.0003 (sign p 0.000) | dragons d0 11/0, snow d0 2/0, hinata d0 1/0; every deck <= 0 |
| `attack` | 6 / 0 | -0.0001 | every deck <= 0 (unchanged from round 2) |
| `needs` | 37 / 30 | -0.0002 | th d0 repaired; NEW dragonstorm d0 2/10, critter 0/3, singletons |
| `both` | 43 / 30 | -0.0003 | |
| `all3` | 55 / 30 | -0.0006 (sign p 0.009) | |

The rollback alone is clean on the train seeds. The needs arm's new class was the one the
instrument then read on the capped binary: **per-mana harm on a multi-mana source whose extra mana
the payment cannot use.** Sandstone Needle's `{R}{R}` at half harm won a lone `{R}` pip over a
Mountain; its second mana floated away and a depletion counter went with it (dragonstorm d0
gi168/438/560, mirrorwing d0 gi579 -- the same shape in all four). Repair: harm is divided by the
mana THIS payment can still use, `1 + min(yield - 1, generic pips still owed + coloured pips still
owed in the source's own colours)`, tracked per payment (`nd_left[7]`, decremented as pips land).
The hinata gi229 bounce-land case (four pips, both units used) keeps its half harm; a lone pip
pays full harm and the ladder's rank (Mountain 10 < Needle 11) decides, as base does.

With the castability cap, the pump sink and this repair (binary `logs/refgate/mtg_stage7`), every
d0 mover from rounds 2 and 3 re-run: **15 better / 0 worse / 35 same**. Round 4 (stage 6, no
usable-mana repair) was stopped at ten minutes as superseded; round 5 is the stage-7 run.

## Round 5 (every repair above, six arms) and the "first hand, THEN deck" key

Round 5 ran the stage-7 binary, base 155/155 identical to GT:

| arm | better / worse | ALL GAMES delta | per-deck |
|---|---|---|---|
| `needs` | 26 / 4 | -0.0005 (sign p 0.000) | every deck <= 0 except th (0/2: one shuffle, gi69, at d3 and d5) |
| `attack` | 6 / 0 | -0.0001 | every deck <= 0 |
| `both` | 32 / 4 | -0.0006 | |
| `rollback` | 14 / 0 | -0.0003 | every deck <= 0 |
| `all3` | 45 / 4 | -0.0009 (sign p 0.000) | |

The th shuffle was the last model flaw of the depletion-term family: on a lone `{1}` with an
Island and a one-counter Sandstone Needle, BOTH harms were reveal fractions (4/64 vs 2/64 -- the
library's expected pips, no hand demand at all), and the smaller fraction overrode the ladder,
spending Needle's last counter and floating its second mana. The user's priority is "first hand
and then deck": certain demand (hand, board, a pump sink) is whole pips and should outrank the
ladder; the deck's expectation should only break a rank tie. The plain-tier key is now
`(0, harm / 64, rank, harm % 64)`. th gi69 repairs at both depths; every d0 mover of rounds 2
and 3 stays 14 better / 0 worse. The two other round-5 losses (fluctuator d3 gi104, hinata d5
gi70) are line divergences -- the lever changes rollout values, so the search chose a different
plan in each arm (the payments differ because the lines differ, not the other way round) -- and
both decks are net better under the lever. Round 6 is the stage-8 run.

## Round 6 (the committed state, six arms) -- the train-seed bar is MET on every arm

Stage-8 binary (every repair, quantized key), base 155/155 identical to GT, 56,060 paired games
per arm:

| arm | better / worse | ALL GAMES delta | sign p | per-deck |
|---|---|---|---|---|
| `needs` | 23 / 2 | -0.0005 | 0.000 | every deck net <= 0 (the two losses, hinata d5 gi70 and fluctuator d3 gi104, are line divergences inside decks that are net better: hinata -7 turns, fluctuator -9) |
| `attack` | 6 / 0 | -0.0001 | 0.031 | every deck net <= 0 |
| `both` | 29 / 2 | -0.0006 | 0.000 | every deck net <= 0 |
| `rollback` | 14 / 0 | -0.0003 | 0.000 | every deck net <= 0 (dragons d0 11/0, snow 2/0, hinata 1/0) |
| `all3` | 42 / 2 | -0.0008 | 0.000 | every deck net <= 0 |

What is NOT yet done, and why the levers ship OFF: the bar's second half is a large HELD-OUT
sample (`CASES=overnight`), which the user asked to defer until the item work was finished and
another agent's rebaseline had landed; and the user's sign-off. The reference gate with all three
levers on reports no new play-drift (full sweep on the round-2 binary: 74 ok / 367 repaired / 0
play-drift / 1 shuffle-dead; the four motivating references re-verified on every later binary).

## The heuristic layer as built (2026-10-05)

Three pieces, each verified on the reference that motivated it; the two that change autonomous
play are behind measurement levers (`test/mana_tap_order_ab.sh`, four pooled arms) until the A/B
clears the adoption bar:

* **Scrying Sheets hold at TURN scope** (`SnowProvider::ManaSourceRank`, human play only, no
  lever). The hold now subtracts what the payment being ordered still owes
  (`g_pay_remaining_mv`, published by `PayNeedScope`, decremented per pip). `Snow/claude_s4_gi3`
  replays `ok` (win turn 5 = 5) — the dig's `{1}{S}` is paid by the other Sheets' `{C}` + one
  Island, and the second Island casts the found Frost Augur. The engine's hold had kept a Sheets
  that could never have dug again.
* **`MTG_NEEDS_TAP_ORDER`** (`ComputeNeedsDemandSupply`, `src/ai/ManaPayment.cpp`): the
  needs-based source choice described in "Heuristics first". Demand = the hand's cast costs and
  our battlefield's activation pips that are still CASTABLE this turn after this payment ("first
  hand"; the uncapped hand was measured and refuted, see round 2), plus the repeatable combat pumps
  as a sink on an attacking turn, plus — only when a dig/draw is live this turn — one expected
  reveal from the library's castable pips ("then deck"; the library is read as a multiset, never
  its order). Supply = untapped sources per colour (a conversion source counts only its free `{C}`),
  decremented as the payment taps. A candidate's harm is the unmet demand its tap would create; the
  plain-land tiers order by (harm, ladder rank). Colourless pays generic first only while no `{C}`
  pip is in demand — the EDF caveat, generalised rather than special-cased.
* **`MTG_ATTACK_BODY_TAP_ORDER`** (`DecisionProvider::ManaSourceHoldValue`): on a pre-combat main
  with an eligible attacker, equal-rank mana creatures tap lowest attack power first; colour comes
  after the body (USER: "colour flexibility only as a needs-based tiebreak").
  `Mirrorwing_Dragon/v2-instigator-entrance/claude_s51_gi50` replays at its recorded T4, with
  Mystic 6 + Mystic 6 + Dragon 9 = 21 (the human line was 20).

## The idea, in the user's words

> *"It should be needs based, so I can't get behind a general rule like that. However, on most decks
> colourless should indeed be spent before a colour and similarly common colours should be spent
> before uncommon when they may be needed. ... I think we need a way to look back 1-2 breakpoints to
> make mana sufficiently accurate. My idea is to have a reasonably accurate set of heuristics and
> then record which mana we threw away on generic costs and potential colours it could have
> generated instead. Then on a future line we can consider rolling back our mana spend only when it
> is required."*

> *"The tricky part with the backtrack idea is we need to be super careful to avoid it becoming
> degenerate. Hence the idea that it is an uncommonly required fix. We want our rules to be good
> enough to only occasionally really need it. Ideally there would also be a way in the viewer to
> backtrack payments like this so the user can see what the engine does."*

So there are two layers, and the order of priority matters:

1. **Heuristics first, and they carry the load.** Generic pips are paid by a needs-based rule: on
   most decks colourless before a colour, and a common colour before an uncommon one *when the
   uncommon one may be needed*. NOT a blanket rule — see "Why not a blanket rule" below.
2. **Rollback second, and only rarely.** Every payment records which sources it spent on GENERIC
   pips and what each of those sources could have produced instead (plus which untapped
   alternatives existed). When a later line in the same turn is short of a colour, and a recorded
   generic payment could have been made from a different source that is still untouched, the
   engine may retroactively swap the two — but only when that line actually needs it.

## How this differs from what already ships

Every payment mechanism shipped so far is **forward-looking reservation**: it must know the demand
at payment time.

| mechanism | what it does | doc |
|---|---|---|
| whole-turn batch payment (`BatchPrepayMainCasts`) | one joint tap solve per main phase | `mana-source-reservation.md` |
| scarcity-first ordering + the 15-lever overhaul | rank sources, hold classes | `mana-order-and-reserve-overhaul.md` |
| `MTG_M2_PAYLOAD_RESERVE` | reserve for the best post-combat payload | `main2-aware-mana-choice.md` |
| `MTG_LINE_SURPLUS_GENERIC` | generic pips spend (have − demanded) surplus first | USER doctrine 2026-09-10 |

Rollback is the complement: **backward repair, on demand.** It covers exactly the cases a forward
reservation cannot, the canonical one being demand that did not exist at payment time because the
payment itself funded the reveal that created it.

## Worked example (why it exists)

`Snow/claude_s4_gi3`, turn 5. Board: two Scrying Sheets (#38 just played, #39), Snow-Covered
Forest #45, Snow-Covered Islands #50 and #56. Scrying Sheets: `{1}{S}, {T}: look at the top card; if
it is snow, put it into your hand`, and `{T}: Add {C}`.

* **Human:** activated #38; paid `{1}{S}` with #39's `{C}` + Island #50; Forest #45 paid Boreal Druid.
  Island #56 stayed up. The dig found **Frost Augur ({U})**, cast off #56.
* **Engine:** activates #39 and pays `{1}{S}` with **both Islands**, leaving #38 (colourless only)
  up. Frost Augur is uncastable; the recorded plan disappears (`nplans 2->1`). The reference has
  never replayed on any commit (checked at 09-06, 09-08 and HEAD).

Note what the surplus rule does here: two U against one C, so it spends U — it is reasoning
correctly about the *known* demand, which is zero. The demand appears only after the dig.

## Why not a blanket "colourless first" rule

The same game shows it. Had the plan activated *both* Sheets, #38's `{C}` is precisely the source
you must not spend, because tapping it for mana consumes the `{T}` its own ability needs. A source's
colourless output is cheap only if nothing else wants that source. That is a needs question, which
is why the heuristic layer must be needs-based, not ordinal.

## The hard constraints

### 1. It must stay RARE — degeneracy is the main risk

A rollback that fires often means the heuristic layer is wrong and the rollback is doing its job
for it: a hidden second payment solver, unbounded in cost, and invisible in the census. So:

* **The firing rate is the success metric.** Count rollbacks per turn and per game (a census
  counter alongside the existing tap-backtrack counters), by deck. A high or rising rate is a defect
  report against the heuristics, not a sign the rollback is working.
* **Set a tripwire.** Decide a per-deck ceiling (e.g. rollbacks on ≤ a few % of turns) and fail the
  measurement loudly above it, the way the batch heartbeat surfaces starvation. Tune the
  heuristics until every deck is under it *before* adoption.
* **Bound the work.** At most one swap set per short line, a hard cap on ledger length, and no
  nested rollback (a rolled-back payment cannot itself trigger another rollback). The tap
  backtracker's history (`tap-backtrack-blowup.md`: one payment solve ran for 14 hours) is the
  warning: no unbudgeted search inside payment.

### 2. Clairvoyance across a reveal

The breakpoints you would look back across are usually reveals (draws, digs). Rolling back across one
lets a payment act on information it did not have. In the worked example the payment funded the
very dig that created the demand. Two classes:

* **Dominant swaps — not clairvoyant.** Keeping a source that produces `{U}` open instead of one that
  produces only `{C}`, when the `{C}` source had no other planned use, is weakly better *whatever the
  reveal shows*. The heuristic layer should already make these up front; a rollback that does one
  is a heuristic miss to log, not a peek.
* **Non-dominated swaps — clairvoyant.** U vs G when either might have been needed. Across a reveal,
  either forbid the swap or charge it to the clairvoyance account the repo already keeps. Never
  silently allow it.

### 3. The ledger must prove the alternative is still valid

A swap is legal only if, for the alternative source, all of these hold:

* at payment time it was untapped and able to produce that mana (summoning sickness, restrictions
  such as Unclaimed Territory);
* now it is still untapped and in the same state: not tapped, sacrificed, bounced, attacked with,
  or otherwise used since;
* and the freed source is fine to untap (nothing since depended on it being tapped).

Tap side effects must replay: pain lands cost life, and some cards trigger on "whenever you tap a
land for mana". A swap that changes those changes the game.

### 4. Scope and placement

* **Window: one turn.** Everything untaps at the untap step, so the ledger never needs to outlive
  the turn. "1–2 breakpoints back" is in practice "since the turn's first payment".
* **Lockstep.** Executor and rollout must both do it, identically, like batch payment.
* **No-greedy rule.** This lives inside mana payment (a standing exemption) and repairs a payment;
  it must never choose a line. Get the user's confirmation that this placement fits the rule
  before building.

## Viewer: let the user backtrack payments too

The user wants to see what the engine does and do the same by hand:

* When the engine applies a rollback, the event log shows it explicitly, e.g. *"re-tapped: Scrying
  Sheets #38 for {C} instead of Snow-Covered Island #56 (needed {U} for Frost Augur)"*. Never a
  silent change of tap state.
* A human can trigger the same repair: on a main-phase frame where a plan is short of a colour that
  an earlier same-turn generic payment could have supplied, offer it as an option (for example
  "re-pay earlier: …"), subject to the same validity checks. It is recorded in the reference like
  any other decision, so references replay through the same mechanism.
* This also gives references a principled answer for the class Snow `s4_gi3` belongs to: a human
  line that kept a flexible source up, which the engine's payment then failed to reproduce.

## Measurement / adoption

The standard bar applies (CLAUDE.md, "no greedy" section): every deck's aggregate net ≤ 0 vs the
committed tree on a large held-out sample. In addition, report the **per-deck firing rate** and
show it under the tripwire. A quality win that comes with a high firing rate is not adoptable as is:
it means the heuristics are owed work first.

Order of work: (1) build the needs-based generic heuristic and measure it alone; (2) add the ledger
and counters with rollback OFF, to measure how often it *would* fire; (3) only then enable rollback.
