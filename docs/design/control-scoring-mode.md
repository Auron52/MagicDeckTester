# Control-scoring mode: scoring decks that win by stopping the opponent

Status: DESIGN (2026-11-26), not built. A default-off injection prototype was built on branch `control-mode`
to measure the size of the effects and how they stack (section 8). It is evidence for this design, not a
deliverable. Every number below is from the suite's ground truth or from that probe, as stated.

## 1. The problem, in the USER's words

The simulator measures a deck by GOLDFISHING: the deck plays alone against a passive opponent, and the metric
is the average turn it kills on (an unwon game counts as `max_turns + 1` = 9). That metric cannot see the
main value of a deck that wins by stopping the opponent. The USER, 2026-11-26:

> "I'm starting to have second thoughts about introducing Enchanted Evening for goldfishing, so we might want
> to park it in some form. The deck is very good in real play, but pretty poor even with attempts to model the
> lock. It would be a better as a deck using a new mode I have been thinking of that assigns scores to controls
> effects used on a certain turn based on an average of how they impact real decks. I'm not sure whether this
> new mode would require 1v1 play to be built first, but it seems like a cheaper alternative for controlish
> decks once the scores are available."

> "Much of the work we did is not wasted, in my estimation. The deck just needs a better home than goldfishing.
> It has similar issues to a lot of other decks I and others own, like counterspell decks, discard decks,
> standard control decks with sweepers and a mix of counterspells and discard. land destruction etc. None of
> these win in a reasonable amount of time in goldfish, but can be very powerful. Hence we'll need to make a
> mode that they can work with. Originally I was just thinking about 1v1 and would still like to do that at
> some point, but with our current approach 1v1 is looking very computationally expensive, particularly to run
> large numbers of simulations. So I want this other mode to be its replacement for high volume."

The objective, USER 2026-11-26:

> "The idea for the mode is that we would optimize for <win turn - average control impact> where we'll need to
> come up with some ways of quantifying the average control impact via either real 1v1 games or other options
> like having a goldfish play through them and figuring out how much they get delayed. Part of the difficulty
> is that different turns have different effects and control cards also stack. If you cast more in a game you
> would expect a different result for their value in terms of delay. Sometimes more will be less effective
> since you have exhausted all of the targets, but with things like counterspells casting more may delay the
> opponent even further since they can't land anything important."

The hard invariant, USER 2026-11-26:

> "One thing that should definitely be true is that a spell cast on Turn 5 on its own should never reduce <win
> turn - average control impact> below Turn 5. Best case scenario you locked them on Turn 5 and that is
> equivalent to a Turn 5 win, but it cannot do better than that."

Its refinement for earlier control, USER 2026-11-26:

> "Now, if you cast a whole bunch of other control spells earlier that would change things. In Enchanted
> Evening that might be Parallax Waving the opponent's creatures or even Opalescence with Parallax Wave. That
> would stop a lot of decks in their tracks even without Enchanted Evening."

> "Or even somewhere between Turn 3 and 4 if the early Parallax is effective enough. In my experience it can be
> pretty devastating just exiling a few creatures and then following up with Opalescence."

What happens to goldfish, USER 2026-11-26:

> "Maybe just add a way to mark something as disabled for goldfish rather than moving it into another
> worktree? Realistically we'll need this anyway as decks like this one don't want to be doing goldfishing."

> "We might want to drop the weird logic with the fake permanent stuff from goldfish arguably. The stuff we did
> for Worldfire is good, though." / "Some free turn machinery does come from worldfire." / "Essentially
> Worldfire is easy to consider because it wipes everything and leaves no trace. The other lock stuff we did
> here was super complex and becoming more than golfishing really should be." / "So, Worldfire can remain in
> goldfish because even though it doesn't fit 100% perfectly, it is close enough. Enchanted Evening should only
> use the new mode and 1v1 once they is built."

So this mode is to be the high-volume home for control decks: counterspells, discard, sweepers, land
destruction, removal and locks. Real 1v1 stays a later, lower-volume mode.

## 2. Which mode a deck belongs in

**Test: does crediting the deck require knowing what the opponent WOULD have done?**

* **No: goldfish.** The deck's plan is to kill. Any interaction it has can be scored from the passive
  opponent's actual state, with no model of the opponent's denied plays. Worldfire is the boundary case
  that stays: it wipes every zone, so the turn after it is decided by what the opponent actually draws and
  plays. The free-turn rule credits that from real state, with no model of what the opponent would have had.
  The USER: "Essentially Worldfire is easy to consider because it wipes everything and leaves no trace." and
  "So, Worldfire can remain in goldfish because even though it doesn't fit 100% perfectly, it is close enough."
* **Yes: control mode.** A large part of the deck's value is the opponent's plays that never happen: the
  land that is destroyed, the threat that is countered or discarded, the creature that is exiled, and the
  turns the opponent spends locked. Crediting any of that against the passive opponent needs a counterfactual
  of the opponent's line. Enchanted Evening needed one: its virtual opponent board ("could they actually have
  cast a permanent with the lands they had") grew into veto clauses and virtual Oblivion Ring / Force of Vigor
  targets. That machinery is being deleted from goldfish (prior art: branch `backup/2026-11-26/ee-analysis-lock`,
  `docs/design/enchanted-evening-lock-design.md` section 3.4). The deck goes to this mode only.
* A secondary signal: the deck's goldfish kill turn is far behind the gauntlet's (section 3.1: the suite's decks
  kill on turn 4.6 on average). Its real strength is the race it wins by slowing the other side.

**The per-deck switch.** A separate change adds `decks/<Name>/<Name>.modes.json`, for example
`{"goldfish": false}`. The viewers, `deck_registry.py`, `suite_gate.py`, the generators and the deck walkers
will honour it, and Enchanted Evening is the first deck marked. This mode adds one key there:
`"control_score": true`. Its consumers:

* The control-mode harness rows (section 7, stage 2) are built for exactly the decks with `control_score: true`.
  A goldfish-only deck has no rows. A deck may carry both keys, for example a tempo deck with a real clock and
  some disruption.
* **The gauntlet** (the opposing decks a control deck is scored against) is drawn only from decks with
  `goldfish: true`. A `goldfish: false` deck has no meaningful goldfish line to disrupt.
* A later control-mode mulligan table or value leaf would be gated on `control_score: true`. It would be fitted
  to the control score, not the kill turn (section 9, Q6).

**Property worth keeping: the mode is a superset of goldfish.** For a deck that casts no control effect, the
score in section 4 is exactly its goldfish win turn W. So a control score and a goldfish score are in the same
unit (turns) and the same direction (lower is better). Section 6 covers how far they can be compared across
modes.

## 3. What already exists

### 3.1 The gauntlet's kill-turn distributions (suite ground truth)

`test/regression_cases.sh` defines 63 decks (each also has a 2HG key). `test/regression_gt.txt` holds each
case's mean; `test/gt_logs/<case>.wins` holds one line per game: `<game index> <win turn | -1> <digest>`.
Pooling the searched (d3 / d5) overnight `.wins` gives the kill-turn distribution of 61 decks, each with at
least 500 games (several have 6,000+), at their shipped settings.

* Mean kill turn per deck ranges from 3.10 (Breaching Dragonstorm, 92% on turn 3) to 5.67 (Landfall). The
  unweighted mean over decks is **4.58**.
* P(the deck has NOT killed by the end of turn L), averaged over the 61 decks:

  | L | 3 | 4 | 5 | 6 | 7 | 8 |
  |---|---|---|---|---|---|---|
  | mean over decks | 0.899 | 0.483 | 0.148 | 0.033 | 0.011 | 0.004 |
  | median deck | 0.999 | 0.469 | 0.075 | 0.018 | 0.005 | 0.001 |

  So against an undisrupted goldfish gauntlet, a pure lock that lands on turn 5 is in time in about 15% of
  games, and one that lands on turn 6 in about 3%. Enchanted Evening's goldfish score with every lock credit
  in place was 5.80 (d3 b10, 224 games, its ledger's step-8c A/B). **This is why the deck looks poor, and the
  same table shows the calibration problem of section 6.1.** Goldfish kill turns are what a deck does to a
  player who never blocks, never removes anything and never counters. A real opponent kills later than this,
  and a lock on turn 5 wins real games far more often than 15%.

What it gives: a ready, large, per-game sample of "when would this opponent have killed us", for 61 real
decks, already at their shipped settings, with per-game digests to replay any game exactly. It is sufficient
input for design B (section 5.2) with no new runs at all.
What it lacks: anything about what happens when the deck is disrupted. A kill turn alone cannot say how much a
land destroyed on turn 3 costs. That needs new runs (the injection probe, section 8).

### 3.2 The passive opponent already IS a real gauntlet deck

`src/core/OpponentDeck.h` / `OpponentDeckPool.h` (generated by `scripts/generate_opponent_deck_pool.py`)
deal every deck's passive opponent a seed-selected list from a 50-deck pool. The pool is 50 of the suite's own
decklists, frozen, selected by `seed % 50`. The opponent gets a shuffled library, a 7-card hand, one draw per
turn and the leftmost land from hand each turn. It never casts a nonland. Screw/flood hurdles adjust its
opening hand.

What it gives: in a control deck's own game, the opponent's hand and library are already the actual cards of a
real gauntlet deck, in the order that deck's own goldfish game with the same shuffle would draw them. That
pairing is the key to the sequence-replay design (section 5.4). A discard effect in the control game names a
real card, and the same card can be removed from the paired gauntlet game. Hand and library effects already
have real targets.
What it lacks: the opponent's board. It never casts anything, so removal, sweepers, counters and Parallax Wave
have no targets in the control game. Its creatures are the scheduled spawns
(`GoldFishRunner::PopulateOpponentSpawns`): ten fixed P/T patterns, no definitions, all arriving on turns 1-3.

### 3.3 The free-turn rule (from Worldfire; stays in goldfish)

`src/core/FreeTurns.h`, `docs/design/opponent-lockdown-free-turns.md`. An opponent turn that ends with (no lands
OR no nonland in hand) AND no creature is a FREE TURN: the round counter (`GameState::turn_number`, the unit of
every reported win turn) does not advance across it. Extra-turn machinery (`AdvanceTurnCounter`,
`InSameRoundTurn`, `pending_free_turn`) carries it. Depth counts ROUNDS, so a free turn costs no ply (USER
2026-11-22). Every search window check is `win_turn <= turn + depth - 1` in rounds.

What it gives the control mode is more than an ancestor: **it is the integer form of the objective in section
4, already wired through the executor, the rollout's lockstep twin, the depth accounting and the reported
metric.** One credited turn of delay is one round that does not advance. The round counter is monotone, so an
effect on round t can at best freeze the count at t. That is the USER's invariant (section 4.3) holding by
construction.
What it lacks: a free turn is earned by the opponent's actual state, so it is all-or-nothing per opponent
turn. A control mode needs credits from an estimated delay (fractional, averaged over a gauntlet), and credits
banked by one effect that are consumed later.

### 3.4 Enchanted Evening's virtual opponent board (prior art only; being deleted)

`docs/design/enchanted-evening-lock-design.md` section 3.4, in the `enchanted-evening` worktree.
`GameState::opp_virtual_board[2]` records, turn by turn, the nonland permanents the passive opponent could
have cast with the lands it had. Our sweepers and Wave exiles clear them, and the lock credit is vetoed while
one survives.

Lessons it gives this design:
1. A one-sided model of what the opponent "could have done" turns into a rules engine of its own, built one
   card at a time (veto clauses, virtual targets, Wave counter accounting, hexproof, 2HG heads). Section 5.3
   avoids building it again by letting a real goldfisher play the opponent's side.
2. The fairness question ("was the lock real, or did they already have a creature out") is the right one. In
   the control mode it is answered by the opponent's own replayed game, not by a mask.
3. The free-turn integration (search depth in rounds, a lock = rounds frozen) worked and is reused.

What it lacks: the opponent's deployments are decided by a cheap castability rule, not by the opponent's play.
It also never measures delay: it only answers locked or not locked.

### 3.5 2HG, life totals, the end-step window

* 2HG (`opponent_heads`, team life 30, `<deck>2hg` suite keys): two passive heads with their own zones. The
  mode could score a 2HG gauntlet the same way, with one paired goldfisher per head. Out of scope for the
  first milestones (Q7).
* The **opponent end-step window** (`PerformOpponentEndStepFaceUp`, `docs/design/opponent-end-step-window.md`)
  is a searched decision made where the search crosses into the opponent's turn: morph flips, Soulstoke puts,
  Force of Vigor's pitch. It is the existing precedent for us acting on the opponent's turn. An instant-speed
  counter or flash removal in the co-simulation (section 5.3) would be a decision of the same kind.
* `EffectHandler::ResolveCounterSpell` is a stub: "TODO: Phase 2 — counter the top non-counter spell on the
  stack". No counterspell has a target in goldfish.

### 3.6 The mtg-ai skill's Phase 2 notes

`.claude/skills/mtg-ai.md` "[Phase 2] Opponent AI Considerations": blocks, reactive casting, holding mana,
information hiding. It flags 1v1 as "explicitly flagged for discussion before implementation". The engine
assumes one active player throughout (`active_player_index` stays ours even through the simulated opponent
turn). That assumption, not the search, is the big cost of real 1v1 (section 6.3).

### 3.7 Learned per-deck evaluators

55 decks ship a value sidecar (`<deck>.value.json`: an O(1) gradient-boosted evaluator of a state's
turns-to-win, used as the search's horizon leaf). For a gauntlet deck, that is already a cheap estimate of
how far from killing a disrupted state is. It is fitted on undisrupted goldfish states, so it is
out-of-distribution after a land is destroyed (section 5.5).

## 4. The objective

### 4.1 Definition

Units are ROUNDS, the unit of every reported win turn. Lower is better.

For ONE control game paired with ONE gauntlet game g:

* `W` = our own kill turn in our game (raw rounds, no free-turn credit; unwon = the horizon + 1).
* Our control effects in that game, ordered by round: `T_1 <= T_2 <= ... <= T_n`. An effect is credited to the
  round on which it resolves. Sorcery-speed effects resolve before the opponent's turn of that round, and an
  instant during the opponent's turn of round t counts as round t.
* `D_k(g)` = the delay of g's kill turn when ONLY the prefix of effects 1..k is applied to it, measured jointly
  (stacking included). `D_0 = 0`. A lock is `D = infinity`: g never kills.
* **The score of the pair:**

      E(g) = min over k = 0..n of  max(T_k, W - D_k(g)),   with T_0 = -infinity (so the k = 0 term is W)

* **The deck's score:** `E = mean over (control game, paired gauntlet game) of E(g)`. Each control game is
  paired with one gauntlet game (section 5.4), so this is a mean over games. It is not computed per deck from
  averaged delays.

The USER asked for "<win turn - average control impact>", with the floor quoted in section 1. This exact form
(the min over prefixes of `max(T_k, W - D_k)`, taken per pair) is a CANDIDATE proposed for this design, not a
USER ruling (Q9). It is not just a convenient formula: it is the race, rewritten as an equivalent goldfish
turn. The derivation shows why the floor is where it is.

### 4.2 Derivation: it is the goldfish turn that wins exactly the same races

Count rounds, with us acting first in a round. g kills us on its turn `K` if undisrupted.

* A goldfish deck that kills on round W wins the race iff `K >= W`.
* A control deck wins iff its kill comes first in the DISRUPTED game: `K' >= W`. An effect on round `T_k` only
  happens if we are still alive then (`K >= T_k`). So the race is won iff, for some prefix k, we survive to
  `T_k` and the disruption that prefix causes is enough: `K >= T_k` and `K + D_k >= W`. That is
  `K >= max(T_k, W - D_k)`.
* So the control game wins exactly the races a goldfish deck killing on round
  `min_k max(T_k, W - D_k)` would win. That number is `E(g)`.

The floor `T_k` is not an extra rule: it is "you must be alive to cast it". A lock on round 5 wins exactly when
a round-5 kill would, so it scores 5.

### 4.3 The invariant, and the properties that keep it from being gamed

* **A lone effect on round T never scores below T.** The USER: "One thing that should definitely be true is
  that a spell cast on Turn 5 on its own should never reduce <win turn - average control impact> below Turn 5.
  Best case scenario you locked them on Turn 5 and that is equivalent to a Turn 5 win, but it cannot do better
  than that." With one effect, `E = min(W, max(T, W - D_1)) >= min(W, T)`.
  If `W < T`, we won before casting it and the effect is irrelevant. A lock scores exactly T.
* **Earlier control moves the floor, legitimately.** The USER: "Now, if you cast a whole bunch of other
  control spells earlier that would change things. In Enchanted Evening that might be Parallax Waving the
  opponent's creatures or even Opalescence with Parallax Wave. That would stop a lot of decks in their tracks
  even without Enchanted Evening." If the prefix up to round 4 (Wave
  on round 3 plus Opalescence on round 4) already stops g, then `D_2(g) = infinity` and `E(g) = 4`, even
  though Enchanted Evening lands on round 5. If the round-3 Wave alone stops g, `E(g) = 3`. The floor is the
  round of the last effect the credit actually depends on, not of the last effect cast.
* **Fractional scores come from averaging per pair.** If an early Wave alone stops 40% of the gauntlet games
  and Opalescence stops the rest, `E = 0.4 * 3 + 0.6 * 4 = 3.4`: the USER's "somewhere between Turn 3 and 4"
  (section 4.3a works it through).
* **Monotone in our speed:** E never increases when W decreases.
* **Monotone in delay:** E never increases when any `D_k` increases.
* **Later effects can only help, never hurt, the score itself:** adding effect n+1 adds one more term to the
  `min`. Its real cost (cards and mana not spent on the clock) shows up as a later W, which is where it belongs.
* **Never better than winning:** `E <= W`.
* **An early cheap spell cannot score better than the lock or win it enables.** Its own term is
  `max(T_1, W - D_1)`. That is below the lock's round L only if its own delay already wins the race before L,
  and then the lock is not what won.
* **Gaming risk lives in the delay model, not the formula.** The formula is sound if `D_k` is right. Any
  positive error in an estimated `D_k` is what a search will exploit (the optimizer's curse; the repo saw
  it with keep tables). Hence section 5.5: model delays for the search, but MEASURE them for the score.

### 4.3a Worked example: Parallax Wave on round 3, then Opalescence on round 4

The USER, 2026-11-26: "Or even somewhere between Turn 3 and 4 if the early Parallax is effective enough. In
my experience it can be pretty devastating just exiling a few creatures and then following up with
Opalescence." This example shows the formula producing exactly that, and what the measurement has to model
for it to be true.

**The cards** (`src/cards/data/cards.json`, as implemented for Enchanted Evening):
* **Parallax Wave** {2}{W}{W}, enchantment. Fading 5: it enters with five fade counters, and at the beginning
  of our upkeep one is removed; if none can be, it is sacrificed. "Remove a fade counter: exile target
  creature" (no mana, no tap, instant speed, repeatable). When it leaves the battlefield, every card exiled
  with it returns to the battlefield under its owner. A token exiled with it ceases to exist.
* **Opalescence** {2}{W}{W}, enchantment. Every OTHER non-Aura enchantment is a creature with base P/T equal
  to its mana value. The Wave becomes a 4/4 creature, so the Wave can target itself.
* A round-3 Wave needs one mana accelerant on turn 1 or 2 (the list runs Utopia Sprawl and Wild Growth).

**The schedule:** `T_1 = 3` (the Wave, exiling `j` of the opponent's creatures), `T_2 = 4` (Opalescence).
`W` is our own raw kill turn.

**Prefix k = 1, the Wave alone: a temporary, bounded delay.**
* Each exile spends a fade counter, and fading spends one more per upkeep. After `j` exiles on round 3 the
  Wave has `5 - j` counters. It is sacrificed at our upkeep of round `3 + (5 - j) + 1 = 9 - j`. With `j = 3`
  that is round 6.
* The exiled cards then return as new objects during our upkeep. On the play, the opponent's turn of that
  round starts after they return, so they can attack in it. With `j = 3` they miss the opponent's turns of
  rounds 3, 4 and 5.
* A counter kept back can exile a creature cast later, at instant speed, but every counter spent shortens the
  Wave's life by a round, and every exile comes back together. When to spend counters is a target-policy choice
  (Q3) in the replay and a searched choice in the co-simulation (section 5.3).
* Creatures that arrive after the counters are gone are untouched. Damage that does not come from a creature
  (burn, a storm or spell-combo kill) is not slowed at all: there `D_1 = 0`.
* So `D_1(g)` is finite. It is at most what losing those creatures for three of the opponent's turns costs g.
  The round-3 term `max(3, W - D_1)` equals 3 only when `D_1 >= W - 3`. The slower our own kill, the more the
  Wave alone has to buy.

**Prefix k = 2, Wave plus Opalescence: a lock against creature decks.**
* With Opalescence out, the loop is: activate the Wave targeting each opposing creature, and last, on top of
  the stack, activate it targeting itself. The self-exile resolves first. The Wave leaves; its leave trigger
  returns every card linked to it (so the round-3 exiles come back), and the Wave returns as a new object with
  five fade counters. The activations still on the stack then resolve with their source gone, so those
  creatures are exiled for good (CR 607.2a).
* The round-3 exiles that just came back are caught by the next loop of the new Wave before the opponent
  untaps with them. Repeated each turn (for example at each opponent end step), every creature
  the opponent deploys is exiled for good, and the Wave never fades.
* So `D_2(g) = infinity` for a creature deck, until the lock breaks: enchantment removal, a hexproof creature,
  two Sterling Groves shrouding the Wave, or a kill that needs no creature. In this high-volume mode the
  gauntlet never casts anything at us (section 6.2), so only the last two can break it.
* Opalescence changes nothing before round 4, so the prefix replay stays causal (section 5.4). What it changes
  is the round-3 Wave's exiles, from "back on round 6" to "gone". **This is the sharpest non-additivity in the
  design:** Opalescence alone does nothing to an opponent (a per-effect table would give it 0), yet with the
  Wave it is a lock. Design A cannot represent it; A+ can only with a measured pair term; D and C measure it
  directly.

**Per-pair scores, for illustration with `W = 9`:**

| gauntlet game g | `D_1` (Wave r3) | `D_2` (+ Opalescence r4) | k = 0: `W` | k = 1: `max(3, W - D_1)` | k = 2: `max(4, W - D_2)` | `E(g)` |
|---|---|---|---|---|---|---|
| creature deck, the Wave takes its whole early board, its kill moves from round 5 to 11 | 6 | inf | 9 | 3 | 4 | **3** |
| creature deck that rebuilds quickly, its kill moves from 5 to 7 | 2 | inf | 9 | 7 | 4 | **4** |
| creatures plus a burn finish: the lock stops the creatures, not the burn | 1 | 3 | 9 | 8 | 6 | **6** |
| burn, or a spell-combo kill | 0 | 0 | 9 | 9 | 9 | **9** |

* Against a creature-heavy gauntlet where half the games are the first row and half the second:
  `E = 0.5 * 3 + 0.5 * 4 = 3.5`, the USER's "somewhere between Turn 3 and 4".
* Against a mixed gauntlet of 40% / 30% / 15% / 15% of the four rows:
  `E = 0.4 * 3 + 0.3 * 4 + 0.15 * 6 + 0.15 * 9 = 4.65`. The burn and combo decks are where the deck needs its
  own clock (W), and the score says so.
* Enchanted Evening on round 5 adds a third term, `max(5, W - D_3)`. It only lowers E in games the first two
  effects did not already stop, which is the USER's "That would stop a lot of decks in their tracks even
  without Enchanted Evening."

**What the measurement must model for this example (stage 1's vocabulary, section 7):** exile-until-leaves with
the return round computed from the fade counters; the return as new objects, with summoning sickness judged
against the opponent's turn; tokens never returning; the self-exile loop, legal only with Opalescence out and
at most one Sterling Grove; hexproof creatures as illegal targets. The probe's `bigcrt` effect (section 8) is a
permanent destroy. That is the Wave-plus-Opalescence loop applied to one creature, not the Wave alone.

### 4.4 Per pair first, then average (not the other way round)

The floor and the min are non-linear, so applying them to averaged delays gives a different and wrong number.
Example: W = 10, effect on round 4. Against deck A it is a full lock (`D = infinity`); against deck B it does
nothing (`D = 0`).

| order | A | B | score |
|---|---|---|---|
| per pair, then average | max(4, 10 - inf) = 4 | min(10, max(4, 10)) = 10 | **7.0** |
| average D first, then floor (D capped at the 12-round horizon: mean D = 6) | | | max(4, 10 - 6) = **6.0** |
| average D first, uncapped | | | **4.0** |

Averaging first credits the lock against A as half a lock against B. It also makes the result depend on an
arbitrary cap. Per-pair is also what makes "stop a lot of decks in their tracks" mean what the USER means: a
partial lock fully stops some decks and not others.

### 4.5 What the control deck's search maximises

At a search node, the search compares lines by their estimated E. That is a single number per line, the same
kind of thing as the turn-to-win it compares now:

* The effects the line casts inside the window are concrete: their rounds `T_k` are known.
* The effects the playout casts beyond the horizon are concrete too (the playout is a policy that casts things).
* `W` comes from the existing machinery: a verified win inside the window, or the value leaf / rollout beyond it.
* `D_k` must come from a model that costs O(1) per node (section 5.5). A replay cannot be run at a node.

**Integer form for the search: credited delay is banked free turns.** The existing search is built around
integer win turns in rounds (window checks, win-turn prunes, folds whose soundness proofs assume the reward is a
win turn). Rather than give TurnSolver a fractional objective, the first build credits delay the way the
free-turn rule already does: an effect on round t raises the banked credit by its modelled delay increment, and whole free turns
(the floor of the cumulative credit, Q4) are consumed one per following round. The round counter does not advance on a consumed free turn. This gives exactly
`max(T, W - D)` for a single effect, because banked turns cannot be consumed before round T, and it keeps
every window check and every free-turn depth rule (USER 2026-11-22: no ply cost) as they are. A lock banks
"until it breaks". The exact fractional E is computed for the REPORT, from the measured delays (section 5.4).
In control mode the free-turn rule's own lock test is replaced by this credit, so a lock is never credited
twice.

**Soundness work this implies:** every TurnSolver prune or fold whose proof says "this line cannot win by
round X, so cut it" must be re-checked. Under credits, a line that does not kill can still score better.
This is the largest engine cost of stage 3 (section 7), and it is why the first milestones need no search
change at all.

### 4.6 Play and draw

The derivation in section 4.2 assumes we act first in each round (on the play). Goldfish games alternate:
`GoldFishRunner` sets `on_the_play = (seed % 2 == 0)`, so half of every run is on the draw.

* **On the draw** the opponent's turn of round t comes before ours. A goldfish kill on round W wins iff
  `K > W`. A sorcery-speed effect on our round `T_k` needs us alive after their turn `T_k` (`K > T_k`), and it
  first affects their turn `T_k + 1`, so the disrupted race is won iff `K + D_k > W`. Together: won iff
  `K > max(T_k, W - D_k)` for some k.
* So **both comparisons become strict and the formula's form is unchanged**: `E(g)` is the same expression, and
  it is still the equivalent goldfish turn, now for a deck on the draw. The deck's E averages over both
  positions, as its goldfish W already does.
* One timing detail: an instant cast during the opponent's turn of round t is credited to round t on the play
  (section 4.1) but to round `t - 1` on the draw, because there that turn comes before our turn t.
* **The pairing must give the gauntlet game the opposite position.** If we are on the play, the paired gauntlet
  deck is on the draw (it draws on its turn 1), and vice versa. A replay that reuses our seed unchanged would
  give both sides the same position. Stage 2 must decouple the gauntlet game's position from its seed's
  parity, or pick the paired seed with the opposite parity.

## 5. Candidate designs

### 5.1 A: a per-(effect, turn) delay table, credited additively

Measure, once per gauntlet, `S[e, t]` = the mean delay of a gauntlet deck's kill turn when effect e is applied
before its round t. This is the USER's original idea: a mode "that assigns scores to controls effects used on
a certain turn based on an average of how they impact real decks". This is the injection harness (section 8). The control deck
then goldfishes alone, and each effect it casts on round t is credited `S[e, t]`.

* **Targets:** fixed by the table's policy, since the control game has no opponent board: "the largest
  creature", "the first land", "the highest-MV nonland in hand", "the first spell of MV >= m". The table
  averages over the times the policy had no target.
* **Cost:** the table is shared by every control deck. Effects x rounds x gauntlet decks x ~200 games each.
  At the suite's cheap settings (gauntlet decks of 23-250 ms/game at d3 b10), 8 effects x 5 rounds x 10
  decks x 200 games = 80,000 games, about 3-4 core-hours, regenerated occasionally. Per control deck: its own
  goldfish cost, nothing more.
* **What it gets wrong:**
  1. **Stacking.** It is additive by construction, and section 8 measures how far from additive the real
     effects are. Diminishing returns (the second removal finds no creature) and increasing returns (repeated
     counters, land destruction into mana screw) are both real.
  2. **State blindness.** The control deck casts removal with the same credit whether or not the opponent
     would have had a creature. The table is unbiased only on average, and the search will learn to time
     casts to the table's best cells regardless of board.
  3. **Averaging before the floor** (section 4.4) if the floor is applied to table values. It must use
     per-deck tables at least: `S[e, t, g]`.
* **Search consistency:** fine. It is an evaluation, O(1), and every action is still branched.
* **Toward 1v1:** the harness is reusable (a cheap baseline to check 1v1 against). The table itself is a
  stepping stone only as a prior.

**Variant A+: interaction terms.** Add measured pair terms `S2[e1, t1, e2, t2]`. The cell count is quadratic:
40 (effect, round) cells give ~800 ordered pairs x 10 decks x 200 games = 1.6M games, ~70 core-hours. That is
before triples, and repeated counters are a triple-and-beyond effect. It is only workable as sparse samples
feeding a fitted model, which is design E.

**Variant A-state: a state-conditioned table.** Delay as a function of what the opponent has left: e.g. for
removal, the number of creatures it has and holds; for land destruction, its lands in play and in hand; for a
counter, its castable spells. This fixes "exhausted targets" (a second removal against a deck with no second
creature is worth ~0) and captures much of the counter compounding (its hand of threats shrinks). But the
control game has no opponent board to condition on. Only the hand and library counts are real (section 3.2),
and they are hidden information. So it only becomes usable with design C or with the paired replay of
design D supplying the state.

### 5.2 B: a lock as survival, from existing distributions

For a pure lock deck: score = the round L the lock completes, read against the gauntlet's `P(K > L)`
(section 3.1). Zero new runs. It is the `D = infinity` special case of section 4: `E = L` per game.

* **Gives:** an immediate, honest reading. Against undisrupted goldfish opponents, a round-5 lock is in time
  ~15% of the time, round-6 ~3%.
* **Gets wrong:** it gives no credit to the partial control before the lock (the Wave on round 3 that is
  "pretty devastating"). Its baseline K is goldfish-fast (section 6.1), and it is binary.
* **Verdict:** a useful sanity line and a reporting column (P(lock in time)). It is not a mode. The free-turn
  rule already credits locks this way in goldfish. Refuted as the main design.

### 5.3 C: co-simulation, two goldfish games joined by an effect channel

Run the control deck's game and a gauntlet goldfisher's game side by side, round by round, on two separate
`GameState`s, each a normal single-player goldfish. Between them is a narrow channel:

* At our turn, the control side sees the gauntlet game's PUBLIC state: its permanents, its land count, its
  hand size, and its graveyard. It does not see the hand or library, except what a reveal effect shows.
* Our control effects are applied to the gauntlet game as engine operations on ITS active player: destroy that
  permanent, discard that card, counter that spell, exile until our Wave leaves. The prototype's hook is
  exactly this kind of operation.
* The gauntlet goldfisher then plays its turn with its own search, unaware of us. The prototype drops its
  committed line after a disruption and re-searches. Its kill of "the passive opponent" in its game is its kill
  of us.
* A counter is decided at the gauntlet's cast: the channel pauses its executor at the stack. The decision is a
  searched choice on our side, in the shape of the opponent end-step window (section 3.5).
* Lifegain on our side raises the passive opponent's life in their game. Their burn to our face is already
  their damage to it.

**Targets:** real and searchable. The search branches over the actual permanents (which creature to exile, which
spell to counter). Each branch is evaluated by an O(1) estimate of the gauntlet's remaining turns-to-kill
(section 5.5) plus our own W estimate.

**Metric:** per game, the race outcome and E exactly as in section 4. The delays are no longer modelled; they
are what happened.

**Cost:** per game, the control game plus the gauntlet game, and the gauntlet's re-searches after disruption.
That is about 2x a goldfish game, with no multiplicative opponent search inside our lookahead, because our
search never simulates their turns, only the channel's estimate. A cheap gauntlet (median 177-683 ms/game,
section 7.4) keeps it within the suite's 3x Hinata gate for most control decks.

**What it gets wrong:** the opponent never interacts with us (no removal, no counters of its own), never blocks
our attackers, never plays around a counter, and never changes plan to race. Our creatures do not block it
either, unless "a blocker" becomes a channel effect. These are real 1v1 effects. They are second-order for "how
much does this control slow a proactive deck", which is what the USER asks the mode to score.

**Search rules:** the opponent's play is the environment, played by the opponent's OWN searched goldfish policy
(not a greedy pick of ours). Our search branches every option we have. Completeness at large budget holds over
our action space. The opponent model is fixed and not ours to optimise. One rule to surface: our lookahead
must not read the gauntlet's hidden zones. The default search is clairvoyant about its own library
(deck-screening skill); it must not become clairvoyant about the opponent's.

**Toward 1v1:** the strongest stepping stone of the four. It builds the paired runner, opponent-state
targeting, effects applied to the opponent's objects, the response window on the opponent's turn, and a
per-game race metric. 1v1 then replaces the channel with one shared state and adds the opponent's reasoning
about us.

**Engineering:** two engines in one worker, a round-synchronous scheduler, the channel's effect vocabulary, and
the counter pause in the gauntlet's executor. No change to single-player rules.

### 5.4 D: sequence replay (the USER's "having a goldfish play through them and figuring out how much they get delayed")

Two passes per control game, no lockstep:

1. The control deck plays its normal goldfish game, with its search crediting effects through a delay model
   (section 5.5) as banked free turns (section 4.5), and records its effect SCHEDULE: (round, effect, target
   policy or named card).
2. The paired gauntlet game is replayed with that schedule injected. The pairing is the gauntlet deck that is
   ALREADY the passive opponent in the control game: `pool[seed % 50]`, same seed and shuffle. Every prefix
   `k` is replayed, so each `D_k(g)` is measured jointly, with stacking included. Since effects are causal,
   prefix k's game equals the full schedule's game up to round `T_{k+1}`, so n effects cost n replays that
   share a prefix, not n full games (a resume point after `T_k` would share the work).
3. `E(g) = min_k max(T_k, W - D_k(g))`, averaged over games.

* **Targets:** hand and library effects name real cards (the passive opponent already holds g's real hand).
  Board effects use a target policy applied in the replay ("its largest creature"). The control deck commits
  to the effect without seeing whether a target will exist, which is the same blind spot as design A. The
  REPLAY then measures what that blind cast was really worth (zero when there was no target).
* **Cost:** one control game plus about n gauntlet replays sharing a prefix. With n ~ 2-4 control effects per
  game and a cheap gauntlet, about 2-3x a goldfish game.
* **Variance:** one gauntlet game per control game, so E carries the gauntlet's spread on top of ours. The
  per-pair E has a standard deviation of ~1-1.5 rounds, so ~1,000 games give a standard error of ~0.04. Two
  variants of a control deck played on the same seeds face the same gauntlet games (common random numbers),
  which is what keeps a card A/B affordable (the deck-screening skill's pairing contract: `seed = BASE + off`
  and `game_index = off`).
* **What it gets wrong:** open loop. The control deck never reacts to the opponent's actual state, so it cannot
  hold a counter for the right threat or decline removal when there is nothing to hit. It is pessimistic for
  skilled control play and exact for the schedule it chose.
* **Search rules:** as A. The search sees a modelled credit, the report a measured one.
* **Toward 1v1:** a stepping stone for the runner and the metric, not for interaction.

### 5.5 E: a learned delay model (the search's O(1) evaluator)

Any design whose search credits control needs `D_k` at every node in O(1): a table lookup or a model.
Train a small model offline, the way the value leaf is trained, on injection runs:

* **Input:** the effect schedule so far (counts by kind and round), the round, and whatever opponent state the
  design exposes. In D: public counts only, plus the gauntlet deck's identity, or "unknown deck" for a
  deck-agnostic credit. In C: the real public board.
* **Target:** the joint delay `D` of the gauntlet game. It is per gauntlet deck, so per-pair flooring
  (section 4.4) can be applied to the model's output too.
* **Training data:** random schedules drawn from what control decks actually do (on-policy: replay schedules
  recorded by the control decks' own games, section 5.4), plus a designed sample of 1-3 effects per kind for
  coverage. The section 8 probe is the first such sample.
* **Cost at a node:** O(1), like the value leaf (the repo's gradient-boosted `eval_model` format already exists).
* **Risk:** the optimizer's curse. The search seeks out the model's over-estimates. Mitigations: measure, never
  model, the reported score (D's replay or C's co-simulation). Flag any deck whose modelled and measured E
  differ by more than the bias floor. Retrain with on-policy schedules from the decks that showed the gap.
* **Is it worth it over the table?** Only where stacking matters. Section 8 says whether it does, and by how
  much.

## 6. What none of these designs fixes

### 6.1 The gauntlet is goldfish-fast

All four designs read the gauntlet's kill turn from goldfish play: an opponent that never blocks, removes or
counters. A real deck in a real game kills later. So a control deck's score is pessimistic in absolute terms,
and a lock on round 5 that wins most real games scores as winning ~15% of races (section 3.1). This is fine
for the repo's main use: comparing card choices and lists WITHIN a control deck, and comparing control decks
with each other. All of these face the same gauntlet. It is not fine for comparing a control deck with an aggro
deck's goldfish score. Calibration options: (a) none, and say so; (b) score the aggro decks against the same
gauntlet with their own interaction (blocks, removal) as channel effects, which is design C extended; (c) a
global shift fitted to a small set of real 1v1 games once 1v1 exists. Q1 asks the USER.

### 6.2 The gauntlet does not adapt

No gauntlet deck sandbagging threats into a counter, holding back attackers, or racing harder when locked.
Against counterspells in particular this is an over-estimate of delay: real opponents bait. Acceptable for a
high-volume mode if stated.

### 6.3 Does it need 1v1 first? No.

* A, B, D and E need only the existing single-player engine, plus an effect-injection hook into the goldfisher.
  The prototype hook is about 250 lines in three files, default off, with no change to any existing game.
* C needs a two-game scheduler and an effect channel. It does not need a two-player rules engine: each game
  stays single-player, and the other side is reached only through the channel.
* What real 1v1 adds is the opponent acting AGAINST us: removal on our threats, its own counters, combat with
  blocks on both sides, and adaptation. The engine's single-active-player assumption is the large refactor,
  and a search that must model the opponent's turns inside its lookahead is the multiplicative compute cost.
  Neither is needed to measure how much a control effect slows a proactive deck.
* The order is the other way round: C is a stepping stone toward 1v1, and the injection harness gives 1v1 a
  cheap baseline to validate against.

## 7. Recommendation and staged build plan

**Recommended route: the objective of section 4, scored by MEASUREMENT (D, then C), searched with a MODEL (A,
then E), integrated through the free-turn credit.** Concretely, smallest useful first:

### Stage 1: the injection harness as a feature (smallest milestone; no search change)

Make the prototype a proper default-off feature: the `ctrl_inject` batch field, a fuller effect vocabulary
(exile-until with a return round for the Wave, a land "tapped down", a tax, and a skip / lock), and correct
leave-the-battlefield triggers. Build `S[e, t, g]` on a gauntlet of ~10 cheap, varied suite decks, with the
stacking probes of section 8 widened.
* **Deliverable:** the per-deck delay table and a stacking report: how far from additive each effect family is.
* **Validation:** off = byte-identical (smoke play-changed = 0 with no job carrying the field). Spot-check
  injected games in the viewer's trace for the three families (land, creature, counter). The `skip` arm must
  give `+1` for every game not yet won by that round (analytic check). Paired seeds.
* **Cost:** ~80,000 gauntlet games, about 3-4 core-hours with the cheap decks; one pooled batch.

### Stage 2: the control score as a REPORT (sequence replay, D, with no search change)

The control deck plays its normal goldfish game and records its effect schedule (an executor-side log, so the
search does not change). A second pooled batch replays the paired gauntlet games with each schedule prefix, and
the report computes E per section 4, per pair. This gives the USER the score of section 4 for real lists, and
it shows how much a card swap moves it, before the search is taught anything.
* **Validation:** a deck with no control effect gives `E == W` exactly, per game. A hand-built schedule with a
  known lock gives `E == L`. Monotonicity checks on synthetic schedules. Enchanted Evening as the first deck:
  its E against its raw W, and how much of the difference comes from the round-3 Wave alone.
* **Cost per deck:** its own goldfish cost (EE: 224 games at d3 b10 took ~4.5 min of batch wall in its ledger)
  plus ~n replays of a cheap gauntlet game per game. For a 1,000-game report, roughly 1,000 x (own cost +
  n x 0.2-0.7 s).
* **Limitation it will show:** the control deck plays as a goldfisher that does not value its control. Its
  schedule is whatever its kill-seeking search chose. That makes the stage-3 gap measurable.

### Stage 3: the search credits control (model + banked free turns)

The control deck's search optimises E: delay credited as banked free turns from the stage-1 table (A), then
from the learned model (E). Reported E stays measured (stage 2's replay). The prune/fold soundness audit of
section 4.5 is part of this stage.
* **Validation:** every deck without `control_score` is byte-identical. Each control deck's measured E must
  improve over stage 2. The adoption bar for search changes still holds (net <= 0 per deck on held-out seeds;
  slower games recover at d8 b0). The modelled-vs-measured gap per deck is reported.

### Stage 4: co-simulation (C) for reactive control

Counters held for the right threat, removal aimed at the real board, the Wave's actual exiles. Build when stage
3 shows the open-loop schedules are the binding error (likely for counterspell decks first).

### Stage 5: real 1v1, at low volume, using stage 4's runner

### 7.4 Compute reference (from `test/suite_cost.json`)

Per-game cost of the 63 suite decks at their most expensive regression shape: quartiles 177 / 683 / 1,753 ms,
90th percentile 4,092 ms, cheapest Breaching Dragonstorm 23 ms, most expensive Beasts 33,683 ms. A gauntlet
built from the cheapest quartile (Breaching, White Knights, Slivers, Burn, Kithkin, Dragons, Critter, Auras,
Soldiers, Stompy, Angels, ... all <= ~200 ms at d3 b10) adds about 0.1-0.2 s per replayed game.

## 8. Prototype probe: one effect vs the same effect twice and three times

**The hook (branch `control-mode`, commit in section 10).** `src/core/ControlInject.h` + `GameEngine.cpp` +
`BatchRunner.cpp`: a batch job field `"ctrl_inject": "<kind>:<round>[:<arg>];..."` applies a schedule of
effects to the GOLDFISHING deck. Board and hand effects apply at the start of its round, before untap. The
counter applies at the executor's stack resolution: the first spell of mana value >= arg it casts that round.
Only the real game is disrupted, never a mulligan trial. The goldfisher's lookahead is NOT told in advance
(its earlier search never sees the effect). After the effect, its committed line is dropped and the turn is
searched afresh. With no job carrying the field nothing changes. The simplifications: "dies" and leave
triggers do not fire; Indestructible is honoured but no other protection; a countered spell's cast triggers
have already fired.

**The run.** 5 suite decks: Burn, White Knights, Stompy Surprise, Dragons, Breaching Dragonstorm. 26 arms each,
200 paired games per arm (seeds 7900001.., held out from every tier). d3 b10 with the shipped profiles,
`max_turns 12` so a delayed kill is not cut at the goldfish horizon (unwon = 13). One pooled batch, 2 threads
in the small-job lane. `scripts/control_inject_probe.py make|report`.

PROBE_RESULTS_PLACEHOLDER

## 9. Open questions for the USER

Each has the default this design takes and the reason.

**Q1. Is the control score only for comparing control decks (and card choices within one), or must it also be
comparable with aggro decks' goldfish scores?** Default: within-mode comparison only. Every report shows E beside
the deck's raw goldfish W, and no calibration shift is applied. Reason: the gauntlet's kill turns are
goldfish-fast (section 6.1), so a cross-mode comparison needs a calibration that only real 1v1 games (or C with
the aggro decks' own interaction) can supply.

**Q2. Which gauntlet, weighted how?** Default: about 10 of the cheapest suite decks that span the archetypes
(burn, white weenie, elf ramp, big-mana ramp, a combo deck, a tribal midrange deck), equally weighted, all with
`goldfish: true`. It would widen to every `goldfish: true` deck at d3 b10 once the cost is known. Reason: cost
(section 7.4) and the opponent pool's existing equal weights. A real-meta weighting is your call.

**Q3. Which target does a board effect hit in the measured replay (stages 1-2), when the control deck cannot see
the opponent's board?** Options: (a) a fixed policy (largest power creature; for land destruction, the land of the
colour the opponent's hand needs most); (b) the target that delays the opponent most, found by replaying every
target (exact, but clairvoyant: the control player "knows" the opponent's future); (c) random. Default: (a) for
the score, with (b) reported beside it in the stage-1 table as the upper bound. Reason: (b) over-credits and (c)
under-credits a competent control player. Stage 4 (co-simulation) replaces all three with a searched choice on
the real board.

**Q4. How should the search's integer credit be rounded (stage 3)?** The search credits modelled delay as banked
free turns, which must be whole rounds. Default: bank the FLOOR of the cumulative modelled delay (credits of 0.4
+ 0.4 + 0.4 bank one free turn after the third effect). The report still uses the exact measured fractional E.
Reason: flooring never over-credits, which blunts the optimizer's curse (section 5.5). The cost is that a single
small effect is invisible to the search until others join it. The alternative is a fractional objective in
TurnSolver, which reopens the soundness proof of every win-turn prune.

**Q5. What horizon and loss score does the control mode use?** Goldfish uses `max_turns 8`, with an unwon game
scored 9. Control decks kill late, and delays push the gauntlet's kills past round 8. Default: `max_turns 12`,
unwon = 13, for the control rows and the injection runs (the probe used this). Reason: a delay that pushes a
kill to round 10 is otherwise invisible. Cost: longer games for slow decks.

**Q6. Mulligan tables and value leaves for control decks.** Default: none until stage 3 ships. Enchanted Evening
keeps no generated artifacts in the meantime. When they come, they are fitted to E, not to W, and gated on
`control_score: true`. Reason: generation binds to the play it is fitted to (the generation-serial rule).

**Q7. 2HG.** Default: out of scope until stage 2 works one-headed. Then one paired goldfisher per head, with the
lock needing both heads (as the free-turn rule does). Reason: it doubles the replay cost and none of the probe's
questions need it.

**Q8. Which decks should follow Enchanted Evening into the mode?** You named counterspell, discard, sweeper,
land-destruction and mixed control decks. Default: Enchanted Evening first (its cards are implemented). Then the
first list you supply of a counterspell deck, because counters are the effect family that stacks upward (section
8) and the one designs A and D handle worst. Reason: only you know which of your lists matter.

**Q9. Is the per-pair form of section 4.1 what you mean by "<win turn - average control impact>"?** The design
scores each (control game, gauntlet game) pair as `min over prefixes k of max(T_k, W - D_k)` and only then
averages. Default: yes. Reason: it is derived, not chosen (section 4.2: it is the goldfish turn that wins exactly
the same races), it satisfies your turn-5 floor by construction, it gives the "somewhere between Turn 3 and 4"
of section 4.3a, and the plain average-first reading gives a different and cap-dependent number (section 4.4).
The difference from a literal "W minus the average delay" is the floor and the per-pair min, and that is your
decision.

## 10. Provenance

Everything in section 8 can be reproduced from these. Nothing on branch `control-mode` is pushed or landed;
the prototype is evidence, not a deliverable.

* **Base:** `34bd5960c` (the shared branch's tip when the branch was cut; `HEAD:src` tree `8d0e5cf5b737`).
* **Prototype:** `d02db29a7` "PROTOTYPE (not for landing): ctrl_inject batch field". `HEAD:src` tree
  `32f8c15f4ff8`. Files: `src/core/ControlInject.h` (the spec parser and per-game schedule),
  `src/core/GameEngine.cpp` (`CtrlInjectTurnStart`, `CtrlInjectDestroyAt`, `CtrlInjectCounter`; the hook in
  `RunTurnFrom` after `AdvanceTurnCounter`, and the counter hook in `ResolveStack`), `src/ai/AIEngine.h`
  (`InRolloutForCtrlInject`, `DropCommittedLineForCtrlInject`), `src/runner/BatchRunner.cpp` (the job field,
  installed and cleared around `RunGame`), and `scripts/control_inject_probe.py` (`make` writes the manifest,
  `report` prints the paired table). 397 lines added, none removed.
* **Effect vocabulary of the prototype** (`kind:round[:arg]`, `;`-separated, all aimed at the GOLDFISHING
  deck): `skip` (it takes no part of that round: a one-round Time Walk for its opponent, used as an analytic
  control), `land:T:i` (destroy its i-th land in battlefield order), `creature:T:i`, `nonland:T:i`, `bigcrt`
  (destroy its creature with the largest effective power), `sweep` (destroy every creature it controls),
  `discard:T:i`, `bigdisc` (discard its highest-mana-value nonland), `counter:T:m` (counter the first spell of
  mana value >= m it casts that round; the mana stays spent). An index past the candidate count is a no-op,
  logged `applied=0`. Board and hand effects apply at the start of the round, after the round counter
  advances and before untap: an opponent's sorcery cast on its preceding turn.
* **Binary:** `build/Release/mtg` in the worktree (a symlink onto the build volume, `build/wt/control-mode`),
  built with `./build.sh` (Release, `--target mtg` only) at 14:05 UTC on 2026-11-26; sha256
  `5018ac0f7ffab6077a1df36f79008744e8b221efbeda2ffae3ccca3e90378dd0`. Unit tests were not built or run: the
  prototype will not land.
* **Hook off = byte-identical.** With no job carrying `ctrl_inject`, the smoke cases `burn_smoke_d3_s1001`
  (100 games) and `whiteknights_smoke_d3_s1001` (75 games) gave `4.4400/66921a2e4aaff4ad` and
  `4.1467/13fe195aa47a42c7`, both equal to `test/regression_gt.txt`.
* **Probe manifest:** `python3 scripts/control_inject_probe.py make <manifest> --games 200 --seed 7900001`:
  130 jobs (5 decks x 26 arms), 26,000 games, every arm of a deck on the same seeds (`game_index 0`), d3 b10,
  `ignore_play_profile`, `max_turns 12`.
* **Probe command** (run from the worktree, in the small-job lane):
  `scripts/box_lane.sh --slots 2 -- build/Release/mtg --batch <manifest> --threads 2 --game-log-dir <dir> --resume`.
  PROBE_WALL_PLACEHOLDER
* **Report:** `python3 scripts/control_inject_probe.py report <game_log_dir> <batch_stderr>`, plus the
  additivity and analytic checks of section 8 computed from the same per-game `.wins` files. Scratch output
  lives on the build volume (`build/scratch/control-mode/`); the summary is copied to
  `logs/control-mode/probe-summary.txt`.
