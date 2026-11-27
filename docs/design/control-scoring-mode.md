# Control-scoring mode: scoring decks that win by stopping the opponent

Status: DESIGN (2026-11-26), not built. A default-off injection prototype was built on branch `control-mode`
to measure the size of the effects and how they stack (section 8). It is evidence for this design, not a
deliverable. Every number below is from the suite's ground truth or from that probe, as stated.

**Summary (read this first).**
* **Does this mode need 1v1 play built first? No** (section 6.3). Every design here uses the existing
  single-player engine plus a narrow effect-injection hook into a gauntlet deck's goldfish game (about 275
  lines of engine code in the prototype, default off, byte-identical with it off). The one design that couples
  two games (C, co-simulation) still keeps each game single-player. It is a stepping stone TOWARD 1v1, not something that
  needs it.
* **The objective** (section 4): per (control game, gauntlet game) pair, `E = min over prefixes k of
  max(T_k, W - D_k)`, then averaged. It is exactly the goldfish turn that wins the same races, so the USER's
  turn-5 floor is derived rather than imposed, and "somewhere between Turn 3 and 4" for an early Parallax Wave
  followed by Opalescence falls out of it (section 4.3a).
* **Recommendation** (section 7): score by MEASUREMENT (sequence replay D, then co-simulation C), search with a
  MODEL (a delay table A as a prior, then a learned state-aware model E), credited to the search as a
  fractional PARTIAL SCORE PER GAUNTLET DECK, with a bound that cuts a line only when it can no longer beat the
  best verified line (USER 2026-11-26, Q4; section 4.5). Stage 1 (the injection harness
  and delay table) and stage 2 (the control score as a report) change no search code.
* **The probe** (section 8; 5 suite decks x 26 effect schedules x 200 paired games, 45.8 core-minutes):
  single effects cost the opponent 0.24-1.76 rounds on average over the five decks, and from 0 to 3.9 rounds
  for one deck. They do NOT add up, and the direction depends on the effect and the deck:
  * Land destruction: increasing returns. Two cost 0.45 rounds (39%) more than the sum of the singles, and
    1.36 more against a combo deck.
  * Removal: target exhaustion shows in the hit rate (Burn: 98% for the first, 80% for the third copy), but the
    delay stays about additive per copy (+0.13 on a pair).
  * Counters: NOT the increasing returns the USER expected, except against the combo deck. On different rounds
    they are additive (+0.03). A second counter in the same round is worth 0.38 rounds LESS than two singles,
    because there is often no second spell that round, but 0.76 MORE against Breaching Dragonstorm's combo turn.
    The probe's counter takes the first spell it sees. Which spell a counter takes changes its value more than
    the number cast does, so the USER's compounding needs a counter aimed at the important spell (Q3).
  * The third copy of every family adds less than the second.
  So an additive table is wrong by about as much as the effects themselves. The stacking is carried by the
  opponent's state, which favours a measured per-pair score and a state-aware model.
* **Open USER questions:** section 9 (Q1-Q9).

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
* A control-mode ("scored") mulligan table is gated on `control_score: true`, fitted to the control score
  rather than the kill turn, and selected by mode beside the goldfish table (USER 2026-11-26, section 9, Q6).
* `"control_max_turns"` (default 12, at most 15) sets the deck's control-mode horizon (USER 2026-11-26, Q5).

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
  in place was 5.80 (d3 b10, 224 games: the step-8c A/B in `docs/design/analysis-Enchanted Evening.md` on
  branch `ee-analysis`). **This is why the deck looks poor, and the same table shows the calibration problem of
  section 6.1.** Goldfish kill turns are what a deck does to a
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
banked by one effect that are consumed later. (USER 2026-11-26, Q4: the control mode does NOT turn delay into
banked integer free turns (kept only as the fallback if partial scores prove prohibitively expensive). It carries fractional per-gauntlet-deck partial scores instead; section 4.5. The
free-turn rule itself stays as it is in goldfish.)

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

* **The deck's score, partial at every level (USER 2026-11-26):** "Yeah, ideally it should be partial at every
  level. We average the effect on each deck and then average those averages together." So:
  1. per (control game, gauntlet game) pair, `E(g)` as above (floor and min applied per pair, never to averages;
     section 4.4);
  2. per gauntlet DECK d, `E_d = mean of E(g) over that deck's games`;
  3. `E = mean over gauntlet decks d of E_d`, each deck weighted EQUALLY (USER, section 7.5).
  Every level stays fractional. A single mean over all pairs equals this only when every deck has the same number
  of games, so the two-level form is the definition. It is never computed from averaged delays.

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

**Partial scores, carried per gauntlet deck (USER 2026-11-26, Q4).** The first draft turned the modelled delay
into banked integer free turns, so the search could keep its integer win-turn machinery. The USER chose partial scores instead, keeping banking as the fallback:
"This mode might have to be more complex and expensive in that you cannot stop immediately on finding a win.
However, there are still limits to what we need to search, since only hands with a high control score that
could outperform the known win need to be carried forward." / "We should be able to carry partial scores,
because you may win at different turns vs different decks. Or rather you may delay deck 1 more than deck 2. The
scores should be partial to reflect that." So the search objective is the fractional E itself, and a line carries
one partial score per gauntlet deck, not one scalar credit.

* **What a line carries.** Its control effects so far, with rounds `T_1..T_m`, and from the mapping (section 7.5)
  each gauntlet deck's prefix delays `D_1(g)..D_m(g)`. Together they define, per gauntlet deck g, the partial
  score as a function of the line's eventual kill round W:

      P_g(W) = min( W,  min over k = 1..m of  max(T_k, W - D_k(g)) )

  `P_g` is non-decreasing in W, and `P_g(W) <= W`. A line that delays deck 1 by three rounds and deck 2 by none
  has `P_1` well below `P_2`. That difference is the partial-ness the USER asked for, and averaging only at the
  end keeps it. Section 4.4's rule (floor and min per pair, then average) applies: the mapping supplies delays
  per gauntlet GAME (at least 20 per deck), and `P_g` is the mean over that deck's games. The line's score is then
  the equal-weight mean of the per-deck `P_g` (section 4.1: partial at every level). A per-deck histogram over
  (base kill round, delay) is an equivalent compact form if the per-game vector is too large to carry at a node.
* **A finished line's score** is `E = mean over g of P_g(W)`, with W its verified kill round, or the leaf or
  rollout estimate beyond the window.
* **The bound.** A line that is now at round t can only add effects on rounds `>= t`. Each such effect adds a
  term `max(T_k, ...) >= t` to the min, and its kill round satisfies `W >= t`. Since `P_g` is non-decreasing:

      LB(line, t) = mean over g of P_g(t)

  This is the score the line would get if it won right now. No continuation of the line can score below it.
  The bound holds for any mapping values, including ones where a longer prefix delays less, because it uses only
  `W >= t` and `T_k >= t` for future effects.
* **The cut.** Let `E*` be the best score of a VERIFIED line so far: a win inside the window, scored with its
  known effects. A line with `LB >= E*` is cut, and everything else is carried forward. This is the USER's
  "only hands with a high control score that could outperform the known win need to be carried forward". Two
  consequences:
  * Goldfish's rule is the special case. A line with no effects has `LB = t`, so once a win at `W*` is
    verified, every effect-free line at round `>= W*` is cut, exactly as now. The extra work in control mode is
    only the lines whose effects have already pushed some gauntlet decks' partial scores below `E*`.
  * A win found early does not end the search. A line that wins on round 6 is beaten by one that wins on round 7
    but already locked most of the gauntlet on round 4, and the second line is still carried when the first is
    found.
* **Only a verified incumbent cuts.** A leaf or rollout estimate beyond the window may ORDER lines, but it never
  sets `E*`. This is the completeness invariant (everything findable at large budget): a cut has to be sound
  against the exact objective, never against an estimate.
* **No integer banking, and no rounding, unless the cost forces it.** The 0.4 + 0.4 + 0.4 case is credited as
  1.2 rounds (averaged per pair), not as one free turn after the third effect. The USER keeps the banked-free-turn
  form as the FALLBACK: "At least, I think that makes the most sense. Your turn banking idea is an option if this
  is prohibitively expensive." So stage 3 measures the cost of partial scores (units and wall per control game,
  against the same deck's goldfish game) before committing to it. Falling back is the USER's call, made on those
  numbers. The optimizer's-curse protection that flooring would have
  given now comes from the measured report and the modelled-vs-measured gap flag (section 5.5).
* **The free-turn rule in control mode.** The mapping's credit replaces the free-turn rule's own lock test, so a
  lock is never credited twice. Its round-counter machinery is not used to carry control credit.

**Soundness work this implies (now larger):** the search's objective becomes a fractional score that is not a
win turn. Every TurnSolver prune or fold whose proof says "this line cannot win by round X, so cut it", every
window check (`win_turn <= turn + depth - 1`) and every win-turn memo has to be restated against `LB` and `E*`
for control-mode decks. Goldfish decks (no `control_score`) keep the current integer objective and stay
byte-identical. This is the largest engine cost of stage 3 (section 7), and it is why the first milestones need
no search change at all. The cost the USER accepts: no stop at the first win, bounded by the cut above.

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
     effects are. Both kinds are real (section 8.2): increasing returns for land destruction into mana screw
     (+0.45 rounds on a pair, +1.36 against a combo deck) and two counters on a combo turn; diminishing returns
     for a second counter in the same round (-0.38) and for the third copy of every family.
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
before triples, and the probe's third copies already depart from the pair terms (section 8.2). It is only workable as sparse samples
feeding a fitted model, which is design E.

**Variant A-state: a state-conditioned table.** Delay as a function of what the opponent has left: e.g. for
removal, the number of creatures it has and holds; for land destruction, its lands in play and in hand; for a
counter, its castable spells. This fixes "exhausted targets" (a second removal against a deck with no second
creature is worth ~0) and captures much of the counter stacking (whether a second spell exists that round, and whether the key one
is still in hand). But the
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
   (section 5.5) as per-gauntlet-deck partial scores (section 4.5), and records its effect SCHEDULE: (round, effect, target
   policy or named card).
2. The paired gauntlet game is replayed with that schedule injected. The pairing is the gauntlet deck that is
   ALREADY the passive opponent in the control game: `pool[seed % 50]`, same seed and shuffle. Every prefix
   `k` is replayed, so each `D_k(g)` is measured jointly, with stacking included. Since effects are causal,
   prefix k's game equals the full schedule's game up to round `T_{k+1}`, so n effects cost n replays that
   share a prefix, not n full games (a resume point after `T_k` would share the work).
3. `E(g) = min_k max(T_k, W - D_k(g))`, averaged within each gauntlet deck, then over decks with equal weight
   (section 4.1). The `pool[seed % 50]` pairing must therefore be replaced or extended so that every
   goldfish-enabled gauntlet deck gets at least 20 games.

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
  differ by more than the bias floor. Retrain with on-policy schedules from the decks that showed the gap. (The USER chose fractional partial
  scores over the floored integer credit, Q4, so these measured checks are the protection, not rounding.)
* **Is it worth it over the table?** Only where stacking matters. Section 8 says whether it does, and by how
  much.

### 5.6 Blocking as a control effect (USER idea, 2026-11-27; proposed, not built)

USER, verbatim: "It's a bit of a mess, but I wonder whether this design should finally implement blocking. Control
decks in particular rarely are the aggressors early, but they do judiciously block attackers, particularly from
aggressive decks, so it might make sense to include the effectiveness of dropping a blocker."

**Where the engine stands.** There is no blocking anywhere. `Step::DeclareBlockers` (`src/core/GameState.h`) is an
unused enum value, `src/ai/Combat.cpp` has no declare-blockers step, and the attack policy is "attack with
everything that can attack (no blockers)" (`src/ai/DecisionProviders.cpp`). Every attacker's power reaches the
player. `docs/design/fungus-blocker-exposure-2026-10-15.md` measured what that costs on the goldfish side.

**Why it fits this design.** A blocker is one more effect in the schedule: "creature X, from round t". In the
gauntlet replay the gauntlet deck attacks into it, and the replay measures the delay to its kill exactly as for
a removal spell. The objective, the floor, the per-deck partial scores, the mapping and the fixed-seed baselines
(sections 4 and 7.5) need no change. It matters most against the creature decks: a burn deck is barely slowed
by a wall. It also matters for Enchanted Evening: under Opalescence its enchantments are creatures and so are
blockers, which is part of why the USER finds Parallax Wave followed by Opalescence so strong.

**What it needs, in order:**
1. **A rules-correct declare-blockers step** in `Combat.cpp`, from the mtg-rules skill, never from memory:
   evasion (flying/reach, menace, can't be blocked), multiple blockers, first and double strike, trample and
   deathtouch damage assignment, lifelink, and the combat-damage triggers already in the card pool. The 2HG and
   viewer paths must keep their current behaviour while blocking is off for them.
2. **The defender's block choice** (our creature in the gauntlet replay). By the Q3/Q8 rulings, the scored
   variant is clairvoyant: try the legal block assignments and keep the one that delays the gauntlet deck most.
   Boards are small, so that is a bounded enumeration. The reported non-clairvoyant variant is the mtg-ai skill's
   basic rule: chump only to prevent lethal, otherwise block when the trade is even or better.
   **Soaking damage (USER 2026-11-27, verbatim):** "Similarily for blocking, if you happen to have a smaller creature
   that can't kill anything you would want to soak up as much (preferably non-trample) damage as possible." /
   "Particularly on the turn where you would die otherwise." So a blocker that cannot kill any attacker is
   assigned to the attacker whose damage it stops the most:
   * the largest attacker WITHOUT trample, since it stops all of that damage; a trampler only loses damage equal
     to the blocker's toughness (one point if the trampler has deathtouch);
   * a blocker that survives the block (a wall) does this every turn, since it costs nothing;
   * a blocker that dies doing it (a chump) does it above all on the turn we would otherwise die. On other turns
     the body is usually worth more as a future blocker, and the clairvoyant scored variant measures exactly
     that trade.
   Several blockers are assigned together to stop the most damage in total (killing blockers to the attackers
   worth killing, by Q8 trouble value; soakers to the biggest remaining non-tramplers). The same rule is how the
   attacker's lookahead predicts our blocks.

   **When a body is worth keeping, and when it is a liability (USER 2026-11-27, verbatim):** "Since in goldfish the
   creatures tend to get bigger holding it back makes some sense, except perhaps against a deck like Creature
   Giving, Hinata, Stuffy etc. where the creatures are a liability."
   * **Against most gauntlet decks, hold the chump.** Their attackers grow, so a chump blocker is worth more later,
     on the lethal turn or against a bigger threat. That is the default of the rule above.
   * **Against decks that use our creatures, spend them.** Creature Giving, Hinata and Stuffy Doll all run
     Forbidden Orchard, and each uses the creatures on our side (USER 2026-11-27, verbatim: "In Hinata it provides
     targets, in Stuffy it reduces the cost of Blasphemous act an in Creature giving it makes Massacre Wurm do more
     life loss."):
     * **Hinata:** our creatures are targets for its spells, and Hinata, Dawn-Crowned makes each extra target
       cheaper.
     * **Stuffy Doll:** each creature on the battlefield makes Blasphemous Act cost 1 less, and Chain Reaction
       deals damage equal to the number of creatures, so our little creatures scale it too (USER 2026-11-27,
       verbatim: "stuffy also has Chain reaction which scales damage including little critters"). Either one
       hits Stuffy Doll and its reflectors, which pass the damage on to us.
     * **Creature Giving:** each of our creatures that dies to Massacre Wurm costs us 2 life. It also hands us
       tokens itself (Hunted Phantasm, Varchild's War-Riders).
     Against these, a body on our side is a liability, so trading or chumping it away early costs nothing or
     gains. Those gifted creatures are on OUR side in the
     replay: today the passive opponent receives them.
   * **Priced by measurement, not by a deck list.** The clairvoyant scored block sees the drain or the bigger
     Chain Reaction in the replay. The non-clairvoyant rule uses a per-gauntlet-deck BODY VALUE, measured offline
     the same way as the Q8 trouble value: the delay from removing one of our creatures. Where that value is
     negative, the rule chumps and trades freely.
   * **Damage reflectors on the attacker's side.** Stuffy Doll, Mogg Maniac, Boros Reckoner, Screaming Nemesis and a
     Spiteful Sliver send the damage they are dealt on to us. The amount is our blocker's FULL power, even beyond
     the reflector's toughness. Blocking one can still be a net good (USER 2026-11-27, verbatim): "True but
     blocking those creatures could be a net good. Particularly if you can kill them with something around their
     size. Even blocking with a 1/1 could be a decent play for the ones that have 3 power, since you would rather
     take 1 than 3." So the block rule counts the reflected damage as damage to us and compares:
     * **Soak:** block when the attacker's damage we stop is more than the power we hit it with. A 1/1 blocking a
       3-power reflector means taking 1 instead of 3.
     * **Kill:** block with a blocker whose power is just enough to kill it ("around their size"), so the
       reflected damage is as small as it can be, when removing the reflector (its Q8 trouble value) is worth
       that hit. A bigger blocker only adds reflected damage. An indestructible reflector (Stuffy Doll) can only
       be soaked, never killed.
     * **Screaming Nemesis also stops our life gain (USER 2026-11-27, verbatim):** "Hmm... Although Nemesis does
       indeed bring up the question of whether we need the lifegain in our control deck. If we do, allowing the
       'no lifegain' to kick in could cost us." A player dealt damage by its trigger can't gain life for the rest
       of the game, so a block (or our own sweeper or burn hitting it) can switch off every later life-gain effect
       of ours. Our life gain is an effect in the replayed schedule (see "Related control effects" below), so:
       * the replay applies the lockout and credits nothing for life gain after it. The clairvoyant scored block
         therefore sees the full cost, and blocks Nemesis only when that is still worth it;
       * the non-clairvoyant rule adds the lockout to the price of dealing damage to Nemesis: the delay the
         mapping credits to the life gain our deck would still cast this game. A deck with no life gain pays
         nothing and blocks Nemesis on damage alone. A life-gain control deck mostly lets it through or soaks it
         with a 0-power blocker, which deals no damage and so triggers nothing.
3. **The attacker's choice against blockers** (the gauntlet deck). This is the messy part. "Attack with
   everything" sends a 1/1 into a 4/4 wall every turn, which over-credits the blocker. Options: (a) keep attack-all,
   as an upper bound only; (b) a provider rule that holds back an attacker that would die to a block without
   killing or dealing damage; (c) search over attack subsets. Option (c) is a new search capability, and it is
   the one real 1v1 needs anyway. USER ruling (Q10): heuristics for the obvious attacks plus search for the at-risk ones;
   see section 9.
4. **Mapping keys** gain creature effects (power, toughness, the keywords that matter in combat, entry round,
   and leaving play). Blockers interact (two walls are not two times one wall), so this falls under the
   interaction terms of section 7.5 levels 2-3, not the pure sum.

**Heuristic where obvious, searched where not (USER 2026-11-27).** USER, verbatim: "Blocking is pretty easy except
perhaps deciding what is a priority to kill due to abilities and what is a threat to our blocker at instant speed.
I suspect we could heuristically implement a chunk of it and search parts that are less obvious." This is the
repo's standing split: a provider may RESTRICT or ORDER the block options, and the rest is branched on.
* **The heuristic chunk (provider prunes).** Rule out blocks that are dominated on combat facts alone, for
  example a block where our creature dies, deals no damage that matters, and prevents no damage that matters.
  A one-option prune is allowed only where it is provably no worse. Each prune gets a counterexample test, and a
  counterexample means fixing the ranking, never dropping the prune silently.
* **"What is a priority to kill" is the trouble value of Q8.** The value of killing a given attacker is the
  measured delay from removing that card from the gauntlet deck: the same per-card number, measured offline per
  gauntlet deck, that ranks counter targets. Abilities (a lord, a combat-damage trigger, a mana creature) are
  priced by measurement rather than by a hand list.
* **"What is a threat to our blocker at instant speed"** is two pieces:
  * The attacker needs a window to answer a block: a pump spell or removal after blockers are declared. Today no
    goldfish deck has one, because nothing ever blocks. This is a new mid-combat search root for the attacker,
    and by the hard rule it must offer "do nothing" there like every other root.
  * Our block choice must allow for it. In the clairvoyant scored variant the replay simply shows the trick
    happening, so a block that loses to it measures worse. The non-clairvoyant variant discounts a block by the
    risk: open mana and cards in hand, priced from how often that gauntlet deck holds a trick at that point.
* **The searched remainder.** Whatever the provider does not rule out is tried in the replay, and the block
  that delays the gauntlet deck most is kept. The provider's prunes keep that enumeration small.

**What stays wrong (open loop, section 5.4).** A blocker that dies in the gauntlet replay is still alive in our
own game, so our own W can count on it attacking or blocking later. That is mild for a control deck that rarely
attacks, and only co-simulation (design C) removes it.

**The same step serves 1v1 later; goldfish never gets it (USER 2026-11-27, Q11).** Once the step
exists, goldfish could let the passive opponent's spawned creatures block our attackers (the Fungus doc's case).
The USER ruled it out: normal goldfish games stay blocker-free, and their ground truth does not move.

**Related control effects that take the same route:** life gain, damage prevention (fogs), and removal of an
attacker mid-combat. Each one changes how much of the gauntlet deck's damage lands, which the replay measures.

**Life gain as a control effect (USER 2026-11-27, verbatim):** "Lifegain itself naturally is another control aspect,
though not a particularly strong one on its own." In the replay it is an effect like any other: life gained on
round t raises the life the gauntlet deck has to get through, and the replay measures how far that pushes its kill.
Expected size: gaining L life against a deck that deals about R damage a round buys at most about L / R rounds,
and only in the games where it carries the kill past a round boundary. Which R matters depends on how the deck
deals its damage (USER 2026-11-27, verbatim: "But for delaying the win turn it is very good vs burn and not so
amazing vs creature decks."):
* **Against burn it is strong.** Burn's damage is spent card by card, about 3 a spell, and does not repeat. Every
  ~3 life gained is one more burn spell it must draw, which is close to one more draw step.
* **Against creature decks it is weak.** A board deals its damage again every round, and it grows, so R rises and
  the same life is gone in a fraction of a round.
It is largest on top of other delays (a blocker, a removal spell), which is where the mapping's interaction terms
matter. The per-gauntlet-deck partial scores (section 4.5) keep the burn-vs-creature difference visible instead of
averaging it away.

**Life gain is the effect most likely to be OVERRATED (USER 2026-11-27, verbatim):** "But it still isn't too difficult
for a bit of lifegain to delay things by a turn. This might honestly be one of the cases that the model will
overrate the most. Pure lifegain rarely actually allows us to win the game, but here it would often give us a turn
delay." Why the design inflates it:
* **Kill thresholds are whole turns.** A gauntlet deck's kill is usually its last attack or burn spell finishing us
  off, often with little to spare, so 2-3 life frequently pushes the kill back a whole round. The measured delay is
  real, but it is a threshold effect, not lasting pressure.
* **E credits delay in races we still lose.** E treats a round of delay like a round of speed, the same way goldfish
  treats a turn-8 kill as better than turn 9 even against a turn-5 opponent. Life gain buys exactly that kind of
  delay without changing the board, so it is where E and "does this win more games" part ways the most.
* **The gauntlet does not adapt.** A real opponent facing life gain keeps developing and counts its burn
  differently. The replay plays its goldfish line unchanged.
What the design does about it (no new rule, a measurement):
* **Race-win share beside E.** Every control report also gives, per gauntlet deck, the share of pairs where we win
  the race (our kill comes before the gauntlet deck's delayed kill), with and without each effect kind. An effect
  that moves E but not the race-win share is the overrating made visible. Life gain is the first effect checked.
* **Stage 1 measures it directly:** life-gain singles (for example 3 and 5 life on rounds 2-5) against the
  gauntlet, reporting delay and race-flip share separately for burn and creature decks.
* **Calibrated against 1v1 when it exists** (stage 5): a per-effect-kind correction fitted on real games, so the
  mapping stops crediting delay that does not win. Whether E itself should be adjusted before then is the USER's
  call on stage 1's numbers.
* **The overrating is bounded (USER 2026-11-27, verbatim):** "Though not against combo I suppose, many of those decks
  don't care about the total at all, but I could still see this being a bit of an issue. That said, it should
  usually only shift some of the decks a turn. It shouldn't allow the deck to get away with much, if any, more."
  Against a combo deck whose kill ignores our life total (infinite damage, an alternate win), life gain measures
  close to zero delay. The per-deck partial scores keep it at zero there rather than averaging in the burn-deck
  gain. Elsewhere it is mostly a single round on some of the gauntlet decks, so averaged over all of them it moves
  E by a fraction of a round. The race-win check above stays as the guard. No correction is planned unless stage 1
  shows a larger effect.
* **Why it is bounded: early kills are close, turn-5 kills are not (USER 2026-11-27, verbatim):** "Because, in my
  experience, turn 3 or 4 are often a fight to hit. Turn 5, for most decks, is really easy to hit and you usually
  win by a lot on that turn." So a few life pushes a turn-3 or turn-4 kill back one round, and the delayed kill
  then lands with damage to spare. The delay stops at about one round.
* **What the stored damage curve can and cannot do.** The fixed-seed baseline games (section 7.5) also record each
  gauntlet game's cumulative damage to us at the end of every round. It does NOT price life gain by itself (USER
  2026-11-27, verbatim): "True, if we only gained life and did nothing else, you could just force the engine to hit
  a higher life total. However, I don't think you can just reuse the existing result. After all, the search
  doesn't card about finding the top damage line. It finds a line that wins, but with a different life total, that
  line might not win anymore, but a better line may." The gauntlet deck's search stops at A winning line, not the
  most damaging one. What the curve gives, for a gain of L life on round t:
  * **An exact zero.** If the recorded line already deals 20 + L by its kill round, that same line still wins on
    that round, so the delay is 0. No replay is needed.
  * **Otherwise, only an upper bound.** The first round where the recorded damage reaches 20 + L bounds the delay
    from above. The real number needs a replay in which the gauntlet deck's search knows our higher life total from
    round t on, so it can find a different line that still wins. That is the "force the engine to hit a higher
    life total" replay, one goldfish game per (seed, L, t).
  So the curve screens out the replays that cannot matter, and the replay measures the rest. A kill that does not
  go through our life total (an alternate win) is marked as such and gets no delay. The curve also shows the
  USER's margin point directly: the recorded margin over lethal, by kill turn, for each gauntlet deck (a lower bound
  on the true margin, for the same reason). It also has a hard counter in the gauntlet: Screaming Nemesis's lockout, above. The suite's life-gain
decks (Selesnya Lifegain, Critter Lifegain, Martyr Lifegain, Lifegain Heliod Combo) are goldfish decks today. If
the USER ever wants their life gain credited, they are candidates to carry both modes.

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
  The prototype hook is about 275 lines of engine code in four files, default off, with no change to any
  existing game.
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
then E), integrated as fractional per-gauntlet-deck partial scores with the bound of section 4.5.** Concretely, smallest useful first:

### Stage 1: the injection harness as a feature (smallest milestone; no search change)

Make the prototype a proper default-off feature: the `ctrl_inject` batch field, a fuller effect vocabulary
(exile-until with a return round for the Wave, a land "tapped down", a tax, and a skip / lock), and correct
leave-the-battlefield triggers. Two prototype defects to fix (section 8.3): a skipped round must not end the
goldfisher's "until the end of your next turn" windows, and a counter needs a target policy beyond "the first
spell". Record the opponent's state (lands, creatures, spells in hand and cast that round) beside every
measured delay, so the table can feed the model of design E. Build `S[e, t, g]` on a gauntlet of ~10 cheap,
varied suite decks, with the stacking probes of section 8 widened.
* **Deliverable:** the per-deck delay table and a stacking report: how far from additive each effect family is.
* **Validation:** off = byte-identical (smoke play-changed = 0 with no job carrying the field). Spot-check
  injected games in the viewer's trace for the three families (land, creature, counter). The `skip` arm must
  give `+1` for every game not yet won by that round (analytic check). Paired seeds.
* **Cost:** ~80,000 gauntlet games, about 2.5-4 core-hours with the cheap decks (the probe measured 0.11 s per
  game over five of them, section 8.3); one pooled batch.

### Stage 2: the control score as a REPORT (sequence replay, D, with no search change)

The control deck plays its normal goldfish game and records its effect schedule (an executor-side log, so the
search does not change). A second pooled batch replays the paired gauntlet games with each schedule prefix, and
the report computes E per section 4, per pair. This gives the USER the score of section 4 for real lists, and
it shows how much a card swap moves it, before the search is taught anything.
* **Validation:** a deck with no control effect gives `E == W` exactly, per game. A hand-built schedule with a
  known lock gives `E == L`. Monotonicity checks on synthetic schedules. Enchanted Evening as the first deck:
  its E against its raw W, and how much of the difference comes from the round-3 Wave alone.
* **Cost per deck:** its own goldfish cost (EE: 224 games at d3 b10 took ~4.5 min of batch wall in the
  same A/B) plus ~n replays of a cheap gauntlet game per game. For a 1,000-game report, roughly 1,000 x (own cost +
  n x 0.2-0.7 s).
* **Limitation it will show:** the control deck plays as a goldfisher that does not value its control. Its
  schedule is whatever its kill-seeking search chose. That makes the stage-3 gap measurable.

### Stage 3: the search credits control (model + per-deck partial scores)

The control deck's search optimises E: delay credited as per-gauntlet-deck partial scores from the mapping
(section 7.5: the stage-1 table first, then the learned model), with the verified-incumbent cut of section 4.5. Reported E stays measured (stage 2's replay). The prune/fold soundness audit of
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

### 7.5 The score is CONSULTED from a precomputed mapping, never replayed per game (USER 2026-11-26)

USER, verbatim: "To be clear, I don't want to redo the gauntlet every playthrough. That seems like a disaster
because it costs likely as much as 1v1 if not more. Instead I want to build some sort of mapping to decide the
value that we can consult. There are more and less complex mappings. The most complex would state all of the
control spells played and on which turns and play that out through the gauntlet. Less complex, but likely less
accurate would make the value equal to a combination of individual values of the spells cast."

This supersedes any reading of designs A/C as a per-game replay. The gauntlet is played ONLY to build the mapping.
Scoring a control game (and every search node) is a lookup. Why: one game against every one of the 63 gauntlet
decks costs about 148 core-s at the suite's play settings (sum of `cost_per_game_ms` in `test/suite_cost.json`;
mean 2.4 s, median 0.7 s per deck), so a stable average (~50 games per deck) is about 2 core-hours per evaluated
control sequence.

USER, verbatim (2026-11-26): "I don't know if we need as many as 50 games per deck, but yes it should be at least
20 games per deck I suppose. Equally is fine." -> Every mapping measurement uses AT LEAST 20 games per gauntlet
deck (about 0.8 core-hours per full-joint key at play settings), and the gauntlet decks are weighted EQUALLY
(this settles the balance question left open under Q2).

The mapping spectrum, most to least exact:
1. **Full joint table.** Key = the whole sequence of control effects with their turns (and targets as chosen by
   the Q3 rule). Value = per-gauntlet-deck delays, measured by playing the sequence through the gauntlet. Exact,
   including stacking, but each new key costs ~2 core-hours, and the key space grows combinatorially. Usable as a
   CACHE for the sequences a deck actually produces most often. It is also the ground truth the cheaper mappings
   are validated against.
2. **Learned joint model** (value-leaf style). Features = the effect sequence (types, turns, counts) plus a coarse
   opponent state per gauntlet deck. Trained offline on a sampled set of full-joint measurements. O(1) to consult;
   accuracy is measured against held-out full-joint keys.
3. **Individual values plus interaction terms.** A per-(effect type, turn) single-effect table, plus measured
   corrections for the pairs the probe shows are far from additive: repeated land destruction (increasing),
   repeated same-turn counters (diminishing), removal after targets run out.
4. **Pure combination of individual values.** Sum (or another fixed combination) of the per-(effect, turn)
   singles. Cheapest to build (the stage-1 table, ~2.5 core-hours on the probe's cheap decks), and wrong exactly
   where section 8 measured non-additivity (+39% for paired land destruction; -0.38 rounds for a same-turn second
   counter).

**The no-control baselines are computed ONCE (USER 2026-11-27).** USER, verbatim: "The other advantage of doing each
gauntlet deck alone is that, if we choose set seeds, we don't need to recalculate them every time. We can
potentially calculate it once and only calculate the other numbers multiple times." So:
* **Fixed seeds per gauntlet deck.** The mapping uses one fixed, held-out seed set per gauntlet deck (at least 20
  games each), never reused by a tier.
* **Baseline table, built once.** Each gauntlet deck's kill turn with no control, per seed, at the mapping horizon
  of 15 (Q5), together with its cumulative damage to us at the end of each round (which screens life-gain replays,
  section 5.6), is stored and reused by every mapping key and every control deck. It is NOT the suite's ground
  truth: the tiers stop at turn 8 and use other seeds. The suite's turn 4.6 average kill says this is a small
  run, about 63 x 20 games once.
* **Only the controlled games are played per key.** The delay of a key is its controlled game's kill turn minus
  the stored baseline for the same seed. The paired seed is also what keeps the difference low-variance: the
  two games share their draws until the first effect.
* **Prefix sharing, as a further saving.** With a fixed seed, a controlled game is identical to its baseline up
  to the round of the first effect. If the baseline run snapshots the game state at each round, a controlled
  replay can start from the snapshot instead of turn 1. This needs the engine to resume a game from a saved
  state across processes; within one process a state copy is enough. It is an option to measure, not yet a
  claim.
* **Invalidation, like any generated artifact.** The baseline is keyed on each gauntlet deck's decklist hash, its
  play digest (an engine change that moves its play) and the seed set. A key that no longer matches is recomputed
  for that deck only, and the controlled games for that deck are then stale too.

Recommended route: build 4 first (it is the stage-1 table and gives the baseline), measure its error against a
few hundred full-joint keys (1), and then add 3 or 2 only as far as that error demands. Every level records the
gauntlet it was built on, so a changed gauntlet triggers a rebuild of the mapping rather than of any game.

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

The run finished cleanly (`=== BATCH done (130 jobs, 26000 games) ===`): 26 min of wall at 2 threads, 45.8
core-minutes of engine CPU, i.e. 20 (Breaching) to 219 (Burn) ms per game. The heartbeat showed 2/2 workers busy.
The full table, with a standard error for every cell, is `logs/control-mode/probe-summary.txt`.

### 8.1 Delay per effect (rounds; paired per game against the base arm; 200 games per cell)

Base kill turn (avg, unwon = 13): Burn 4.31, White Knights 4.16, Stompy 4.05, Dragons 5.61, Breaching 3.14. A
per-deck cell's standard error is 0.02-0.16 rounds; the mean over decks has a standard error of 0.02-0.05.

| arm | Burn | W.Knights | Stompy | Dragons | Breaching | mean of 5 |
|---|---|---|---|---|---|---|
| skip3 (analytic control) | +1.12 | +1.00 | +1.00 | +1.00 | +1.00 | +1.02 |
| land2 / land3 / land4 | .80 / .52 / .30 | 1.10 / .69 / .18 | .60 / .35 / .08 | 1.46 / 1.31 / .89 | 1.34 / 1.34 / .10 | **1.06 / .84 / .31** |
| land 3+4 / 3+4+5 | .98 / 1.14 | 1.00 / 1.06 | .55 / .66 | 2.66 / 3.83 | 2.81 / 4.34 | **1.60 / 2.20** |
| bigcrt 2 / 3 / 4 | 1.37 / 1.07 / .69 | .38 / .89 / .75 | .85 / .88 / .68 | 0 / .06 / .54 | 0 / 0 / 0 | **.52 / .58 / .53** |
| bigcrt 3+4 / 3+4+5 | 2.00 / 2.56 | 1.67 / 2.38 | 1.93 / 2.96 | .60 / 1.28 | 0 / 0 | **1.24 / 1.84** |
| sweep3 / sweep4 | 2.35 / 1.95 | 1.34 / 2.44 | 2.54 / 3.86 | .06 / .56 | 0 / 0 | **1.26 / 1.76** |
| counter 2 / 3 / 4 | .87 / .67 / .58 | .86 / .92 / .38 | .45 / .81 / .53 | .54 / .90 / .99 | 0 / .79 / .12 | **.54 / .82 / .52** |
| counter 3+4 / 3+4+5 | 1.09 / 1.42 | 1.35 / 1.56 | 1.64 / 2.15 | 1.77 / 2.60 | .99 / 1.19 | **1.37 / 1.78** |
| two counters in round 3 | .91 | 1.06 | 1.05 | .93 | 2.33 | **1.25** |
| counter MV >= 3, round 3 | .08 | .73 | 1.39 | .81 | .79 | **.76** |
| bigdisc 2 / 3 / 4 | .48 / .40 / .28 | .36 / .30 / .11 | .49 / .64 / .23 | .70 / .58 / .49 | .52 / .49 / .08 | **.51 / .48 / .24** |
| bigdisc 2+3 / 2+3+4 | .88 / 1.12 | .70 / .77 | 1.19 / 1.49 | 1.36 / 1.98 | 1.27 / 1.62 | **1.08 / 1.40** |

Share of casts that found a target (from the `[ctrl-inject]` lines; for counters, applications divided by the
games that reached that round): removal on round 3 hits 98-100% against the three creature decks, 6% against
Dragons and 0% against Breaching; the third removal of `3+4+5` hits 80% against Burn. The second counter in
round 3 finds a second spell in only 48-82% of games (Dragons 48%), against 90-98% for the first. Land
destruction and discard hit 83-100% throughout. Unwon games stay rare (at most 12%, Dragons `land 3+4+5`), so
the 12-round horizon barely truncates the delays.

### 8.2 Is it additive? (interaction = joint delay minus the sum of the parts, paired per game)

| test | Burn | W.Knights | Stompy | Dragons | Breaching | mean of 5 (se) |
|---|---|---|---|---|---|---|
| land: (3+4) - 3 - 4 | +0.16 | +0.13 | +0.12 | +0.47 | +1.36 | **+0.45 (0.03)** |
| bigcrt: (3+4) - 3 - 4 | +0.24 | +0.03 | +0.38 | +0.01 | 0 | **+0.13 (0.03)** |
| counter: (3+4) - 3 - 4 | -0.16 | +0.06 | +0.30 | -0.12 | +0.09 | **+0.03 (0.03)** |
| discard: (2+3) - 2 - 3 | -0.01 | +0.04 | +0.06 | +0.08 | +0.26 | **+0.09 (0.04)** |
| counter: two in round 3 - 2 x one | -0.44 | -0.77 | -0.57 | -0.88 | +0.76 | **-0.38 (0.05)** |
| land: 3rd increment - 2nd | -0.31 | -0.26 | -0.10 | -0.19 | +0.07 | **-0.16 (0.04)** |
| bigcrt: 3rd increment - 2nd | -0.37 | -0.06 | -0.02 | +0.14 | 0 | **-0.06 (0.04)** |
| counter: 3rd increment - 2nd | -0.11 | -0.24 | -0.31 | -0.06 | -0.02 | **-0.14 (0.04)** |
| discard: 3rd increment - 2nd | -0.14 | -0.27 | -0.40 | -0.06 | -0.40 | **-0.25 (0.04)** |
| sweep3 - bigcrt3 | +1.29 | +0.46 | +1.66 | 0 | 0 | **+0.68 (0.03)** |

(A positive interaction is increasing returns; a negative one is diminishing returns. Mean increments per copy:
land 0.84 / 0.76 / 0.60, removal 0.58 / 0.66 / 0.60, counter 0.82 / 0.56 / 0.41, discard 0.51 / 0.57 / 0.32.)

### 8.3 What it shows

* **The harness works.** `skip3` gives exactly +1.000 in every game that was not won by round 2 for four of five
  decks. Burn's +0.12 excess is a prototype artefact, traced in one game (seed 7900005, game index 4: base wins
  on round 4, skip on round 6). Light Up the Stage cast on round 2 lets Burn play two exiled cards "until the end
  of your next turn". The prototype's skip consumes round 3 as that turn, so the cards are lost. In real Magic an
  opponent's extra turn is not ours, so a stage-1 `skip` must not end "your next turn" windows.
* **Stacking is real, and its SIGN depends on the effect AND on the deck.** The additive table of design A is
  wrong by up to 1.4 rounds on a two-effect schedule (land destruction against Breaching), and the error is about
  as large as the effects themselves.
  * **Land destruction: increasing returns.** A land destroyed on round 4 alone costs 0.31 rounds, but 0.76
    after one destroyed on round 3: the pair is 0.45 rounds (39%) more than the sum, and 1.36 more against the
    combo deck. This is mana screw compounding. The third copy then adds less than the second (-0.16), so the
    returns rise at first and then fall.
  * **Removal: about additive per copy, with target exhaustion visible.** The pair is +0.13 over the sum (Stompy
    +0.38), and the third increment is flat. The fall in hit rate (Burn 98% to 80%) is the "exhausted all of the
    targets" effect the USER expected. It shows mostly in the hit rate, because a removal that does hit costs a
    creature deck more once it is already short. A sweeper on round 3 is worth 0.68 rounds more than a spot
    removal, and its value RISES from round 3 to 4 against Stompy (2.54 to 3.86) and White Knights, because
    there is more to sweep. Against Dragons and Breaching, removal is worth nothing until a creature exists.
  * **Counters: NOT increasing returns in this probe, except against the combo deck.** On different rounds the
    pair is additive (+0.03), each extra copy adds less (0.82, then 0.56, then 0.41), and a second counter in the
    same round is strongly diminishing (-0.38; Dragons -0.88), because there is often no second spell to counter
    that round. The exception is Breaching Dragonstorm (+0.76): two counters on its combo turn break it, the
    USER's "they can't land anything important". **This is not the result the USER's intuition predicted, and the
    reason is worth stating:** the probe's counter takes the FIRST spell of mana value >= 1, not the one that
    matters. Counting only mana value 3 or more changes Stompy's value from 0.81 to 1.39 and Burn's from 0.67 to
    0.08 (Burn's key spells are cheap). So a counter's value depends on WHICH spell it takes much more than on how
    many counters were cast. The compounding the USER describes needs a counter aimed at the important spell, plus
    an opponent with few of them. That is a target policy (Q3) or a searched choice (design C), not a property of
    the count. Real opponents also bait counters (section 6.2), which pulls the other way.
  * **Discard: about additive for two, then diminishing** (the third adds 0.25 less than the second), as the
    hand's best card is gone.
  * **Timing matters as much as kind.** Land destruction is worth 1.06 rounds on round 2 and 0.31 on round 4;
    discard 0.51 and 0.24. Removal and sweepers go the other way against slow-to-deploy decks.
* **What it favours among the designs:**
  * **A (the additive table) is refuted as a scoring method** and is only a usable first search prior, which is
    what section 7 already restricts it to. Its over-credits are caught by the measured report and the gap flag
    (section 5.5). The flooring that would have limited them was the old Q4 default, which the USER replaced
    with fractional partial scores.
  * **A+ (pair terms) would fit the two-copy numbers but not the three-copy ones**, and the interactions change
    sign by deck. The cell count is quadratic before triples.
  * **The interactions are carried by STATE**: how many lands the opponent already lacks, whether a creature or
    a second spell exists that round, and whether the spell taken is the key one. That favours **A-state and E**
    (a model whose inputs are the opponent's lands, creatures and spells), and confirms that the score itself must
    be **measured jointly per pair (D, then C)**, which needs no model of stacking at all.
  * The section 7 recommendation therefore stands, with one change of emphasis: the stage-1 table should record
    the state features above beside every delay, so it can feed E directly.
* **Cost.** 26,000 games of the five cheap decks took 45.8 core-minutes (0.11 s/game on average). The stage-1
  table of section 7 (~80,000 games) is therefore about 2.5 core-hours at these decks' cost, at the low end of
  the 3-4 core-hour estimate.
* **Limits of the probe.** Five decks, all fast and proactive, so the delays are for the goldfish-fast gauntlet
  of section 6.1. Target policies are fixed (largest power, highest mana value, first spell), so these are
  delays for a blind control player, not a skilled one. Dies and leave triggers do not fire. The goldfisher
  re-plans after each effect, but never anticipates one (it plays no differently into an open counter).

## 9. Open questions for the USER

Each has the default this design takes and the reason.

**Q1. Is the control score only for comparing control decks (and card choices within one), or must it also be
comparable with aggro decks' goldfish scores?** Default: within-mode comparison only. Every report shows E beside
the deck's raw goldfish W, and no calibration shift is applied. Reason: the gauntlet's kill turns are
goldfish-fast (section 6.1), so a cross-mode comparison needs a calibration that only real 1v1 games (or C with
the aggro decks' own interaction) can supply.

*USER answer (2026-11-26/27), verbatim:* "I don't understand this question. My idea is to compare each gauntlet deck
without any control vs that gauntlet deck fighting through the control and use the average win-turn difference."
-> That comparison is exactly how the delay `D` is measured: each gauntlet deck's kill turn with no control,
against the same game played through the control effects. It enters the score through section 4.1 (floor and
min per pair, averaged per gauntlet deck, then over decks). What Q1 was asking is whether a control deck's E
can be set beside an aggro deck's goldfish W. By the derivation in section 4.2 it can: E is the goldfish turn
that wins the same races against the same gauntlet, so both are turns on one scale, and no calibration shift
is applied. The caveat is stated in reports, not corrected: neither number sees the gauntlet decks interact
(they do not attack the aggro deck's blockers or counter the control deck's spells) or adapt (section 6).
Every control-mode report shows E beside the deck's raw goldfish W. Q1 is closed.

**Q2. Which gauntlet, weighted how?** Default: about 10 of the cheapest suite decks that span the archetypes
(burn, white weenie, elf ramp, big-mana ramp, a combo deck, a tribal midrange deck), equally weighted, all with
`goldfish: true`. It would widen to every `goldfish: true` deck at d3 b10 once the cost is known. Reason: cost
(section 7.4) and the opponent pool's existing equal weights. A real-meta weighting is your call.

*USER answer (2026-11-26), verbatim:* "All of the decks we have been adding should be part of it. The only question is
whether they are balanced enough as-is. I suppose we may need to redo the gauntlet to recalculate numbers in the
future." -> The gauntlet is EVERY `goldfish: true` deck (63 suite decks on 2026-11-26), not a cheap subset. Two
consequences. (1) Every delay table / fitted model records the gauntlet it was measured on (deck list + each
deck's decklist hash), so a changed gauntlet is detected and the numbers are regenerated, like any other
generated artifact. (2) Cost: the suite's measured play-setting cost per game spans 23 ms (Breaching) to 33.7 s
(Beasts) (`test/suite_cost.json`), so replaying the whole gauntlet inside every scored control game is not
affordable at play settings. The full gauntlet is therefore used ONLY OFFLINE, to build the mapping that scoring
consults (section 7.5; the USER ruled out per-game replay). Still open: whether the
decks as they stand are balanced enough, i.e. equal weights per deck or weights per archetype cluster (so that
many similar aggro decks do not dominate the average).

**Q3. Which target does a board effect hit in the measured replay (stages 1-2), when the control deck cannot see
the opponent's board?** Options: (a) a fixed policy (largest power creature; for land destruction, the land of the
colour the opponent's hand needs most); (b) the target that delays the opponent most, found by replaying every
target (exact, but clairvoyant: the control player "knows" the opponent's future); (c) random. Default: (a) for
the score, with (b) reported beside it in the stage-1 table as the upper bound. Reason: (b) over-credits and (c)
under-credits a competent control player. Stage 4 (co-simulation) replaces all three with a searched choice on
the real board. The same question applies to counters, where the probe shows it matters MORE than the number of
counters cast (section 8.3). Default for (a) there: the first spell of the round whose mana value is at least the
gauntlet deck's own median nonland mana value, rather than the first spell of all. (Superseded for counters by the USER's Q8 answer: a rising trouble threshold.)

*USER answer (2026-11-26), verbatim:* "The goal is to hit the highest value card the opponent has when we cast it.
That is a little tricky to determine. It also is somewhat clairvoyant for anything cast on the opponents' turn
since a normal player doesn't know what they will cast later, but perhaps that is okay when using our clairvoyant
mode." -> Target = the highest-value card the opponent has AT CAST TIME (on the board, or the spell on the stack
for a counter). "Value" is defined operationally as option (b): the candidate whose removal delays that opponent
most, found by trying each candidate in the replay. That is exact and needs no hand-written value heuristic. It
is clairvoyant (it uses the opponent's future draws to score a target, and for an off-turn counter it knows
what comes later), which the USER accepts under the clairvoyant mode. The fixed policy (a) stays as a reported
non-clairvoyant variant. Cost: one extra replay per candidate target, at each control decision only.

**Q4. How should the search's integer credit be rounded (stage 3)?** The search credits modelled delay as banked
free turns, which must be whole rounds. Default: bank the FLOOR of the cumulative modelled delay (credits of 0.4
+ 0.4 + 0.4 bank one free turn after the third effect). The report still uses the exact measured fractional E.
Reason: flooring never over-credits, which blunts the optimizer's curse (section 5.5). The cost is that a single
small effect is invisible to the search until others join it. The alternative is a fractional objective in
TurnSolver, which reopens the soundness proof of every win-turn prune.

*USER answer (2026-11-26), verbatim:* "This mode might have to be more complex and expensive in that you cannot stop
immediately on finding a win. However, there are still limits to what we need to search, since only hands with a
high control score that could outperform the known win need to be carried forward." / "We should be able to carry
partial scores, because you may win at different turns vs different decks." / "Or rather you may delay deck 1 more
than deck 2. The scores should be partial to reflect that." / "At least, I think that makes the most sense. Your turn
banking idea is an option if this is prohibitively expensive." -> Partial scores are the design, with no integer
banking and no rounding. The floored banked-free-turn form stays as the fallback if stage 3 measures partial scores
as prohibitively expensive (the USER decides, on the numbers). The search
optimises the fractional E. Each line carries a partial score per gauntlet deck, `P_g(W)`, and is cut only when
`LB = mean_g P_g(t)` (the score if it won on the current round) is no better than the best VERIFIED line's score.
Full form in section 4.5.

**Q5. What horizon and loss score does the control mode use?** Goldfish uses `max_turns 8`, with an unwon game
scored 9. Control decks kill late, and delays push the gauntlet's kills past round 8. Default: `max_turns 12`,
unwon = 13, for the control rows and the injection runs (the probe used this). Reason: a delay that pushes a
kill to round 10 is otherwise invisible. Cost: longer games for slow decks.

*USER answer (2026-11-26), verbatim:* "Yes, we need to move the max turn for sure. We can try 12 like you mentioned for
now, but we might need to go as far as something like 15 for certain decks. I doubt we'll have to go further than
that. I realize this has cost implications, but we'll have to do the best we can." -> The control-mode horizon is
12 (unwon = 13) by default, and is set PER DECK up to 15 (unwon = 16). It is a per-deck key in the modes marker,
`"control_max_turns"`, default 12. What follows from it:
* **The mapping is built at 15**, the largest horizon any deck may use, so a delay is never cut short by the
  mapping's own horizon and a deck raised from 12 to 15 needs no rebuild. A deck at a horizon H reads the same
  table and censors at H. Cost: an undelayed gauntlet game still ends when it kills (turn 4.6 on average,
  section 3.1). Only the delayed and locked replays run longer, and a locked replay can stop as soon as the lock
  is proven to hold to the horizon.
* **Comparing two control decks at different horizons** (open Q1 territory): each deck's own number is scored at
  its own H. A comparison between decks rescores both at the larger H, so the two loss scores are the same
  number. An A/B of card choices inside one deck always uses that deck's H.
* **Which decks need more than 12** is measured, not guessed: the share of a deck's games unwon at 12 in the
  stage-2 report. A deck that leaves many games unwon at 12 is a candidate for 15, and raising it is the USER's
  call on those numbers.

**Q6. Mulligan tables and value leaves for control decks.** Default: none until stage 3 ships. Enchanted Evening
keeps no generated artifacts in the meantime. When they come, they are fitted to E, not to W, and gated on
`control_score: true`. Reason: generation binds to the play it is fitted to (the generation-serial rule).

*USER answer (2026-11-26), verbatim:* "Yes, we would have a mulligan table that is built using rollouts with the
control score active. Technically we may even build entries like this for existing decks going forward, since many
do include control cards. The idea is that we would use the goldfish profile for goldfishing and the scored profile
for control-score search and maybe even 1v1 in the future, since generating a 1v1 profile seems pretty
impractical." -> A deck can have TWO keep tables side by side:
* **the goldfish table** (the existing artifact, fitted to W), used for goldfish play and every current tier;
* **the scored table** (new), generated by `mullgen` with the control score active in the rollouts (each
  candidate hand is scored by E, not by W). It is used for control-score search, and possibly later for 1v1,
  since a real 1v1 keep table is impractical to generate.
What this needs:
* **Explicit selection by mode, never by presence.** Keep tables are presence-gated today: the file existing IS
  adoption. The scored table therefore gets its own file name (proposed:
  `<Name>.control.keepmodel.exhaustive.profile.json.gz` + `.raw.json.gz`), and the engine loads it only in
  control-score play. Goldfish play never reads it, so existing goldfish ground truth does not move when one
  appears.
* **When:** after stage 3, because the rollouts need a search that already optimises E. Until then Enchanted
  Evening has no generated artifacts.
* **Existing decks with control cards** may get a scored table "going forward". Such a deck carries both
  `goldfish: true` and `control_score: true`, and needs control-mode suite rows first, by the same rule that puts
  a deck in the suite before its artifacts. `mullgen.sh` today refuses a `goldfish: false` deck. A scored-table
  run is a separate mode of the generator, gated on `control_score: true` rather than on `goldfish`.
* **The generation-serial rule applies per artifact:** a scored-table run is one more generation stage, alone on
  the box like every other.
* **Value leaves** were not part of this answer. The design assumes the same pattern (a scored leaf fitted to E,
  selected by mode) if one is ever wanted.

**Q7. 2HG.** Default: out of scope until stage 2 works one-headed. Then one paired goldfisher per head, with the
lock needing both heads (as the free-turn rule does). Reason: it doubles the replay cost and none of the probe's
questions need it.

*USER answer (2026-11-26/27), verbatim:* "Yeah, let's leave this out-of-scope." -> 2HG is out of scope for the control
mode.

**Q8. Which decks should follow Enchanted Evening into the mode?** You named counterspell, discard, sweeper,
land-destruction and mixed control decks. Default: Enchanted Evening first (its cards are implemented). Then the
first list you supply of a counterspell deck, because a counter's value depends most on WHICH spell it takes
(section 8.3: countering only spells of mana value >= 3 moves Stompy's value from 0.81 to 1.39 rounds and Burn's
from 0.67 to 0.08), so counters are the family designs A and D handle worst and the first that needs design C.
Reason: only you know which of your lists matter.

*USER answer (2026-11-26/27), verbatim:* "That makes sense. If we want the clairvoyant answer which spell is countered
should be the most troublesome. The non-clairvoyant answer would be to attempt to hit the most troublesome
spells with counters. Early this means you would hit almost anything, but later you might have to let something
less serious pass to save mana or counters for a bigger threat." -> Order: Enchanted Evening first, then the
first counterspell list the USER supplies. Counter targeting, both variants:
* **Clairvoyant (the scored answer, as in Q3):** counter the spell whose countering delays that gauntlet deck
  most, found by replaying each candidate.
* **Non-clairvoyant (the reported variant):** a threat threshold. Each gauntlet deck's spells get an offline
  "trouble" value: the average delay from countering that card, measured over the deck's games, which is an
  average and so knows nothing about the current game's draws. A counter is cast on a spell whose trouble value
  is at least the threshold. The threshold starts at zero (early, counter almost anything) and rises as the game
  goes on and as counters and mana run short, so a lesser spell is let through to keep a counter for a bigger
  threat. The threshold schedule is fitted offline per control deck, not hand-set. It replaces the earlier
  draft rule (the first spell at or above the gauntlet deck's median mana value).

**Q10 (2026-11-27). The gauntlet deck's attacks once blockers exist (section 5.6, step 3).** (a) keep attack-all,
which over-credits blockers; (b) a provider rule that holds back an attacker that would die to a block for
nothing; (c) a searched choice of attackers, a new search capability that real 1v1 also needs. Default: (b) for
the first build, with (a) reported beside it as the upper bound, and (c) when 1v1 work starts. Reason: (b) is
small and removes the worst over-credit, and (c) is most of a 1v1 attack search.

*USER answer (2026-11-27), verbatim:* "Good point on the attacking, though. That is where things get a lot trickier.
We would want some heuristics and some search. Typically if only a little damage would get through and you would
lose creatures you would hold back until your board is larger or you have evasion or creature combat is more
favourable." -> Neither (a), (b) nor (c) alone: a provider decides the obvious attacks and the search branches on
the rest.
* **Why the obvious part is large in the replay.** Our creatures never attack the gauntlet deck in the replay
  (our damage lives in our own game, W). So a gauntlet creature held back has no defensive value. Holding back
  only avoids losing it.
* **Provider (sound prunes):**
  * A creature that cannot die in this combat attacks. That covers evasion no untapped blocker can meet, and
    no blocker able to kill it (counting deathtouch and first strike). The existing tap-ability exception still
    applies (a mana creature or a tap ability can be worth more untapped).
  * Interchangeable attackers (same power, toughness and combat keywords) are folded, so "send 2 of the 4 1/1s"
    is one option, not six.
* **Search (the less obvious part):** which of the creatures that COULD die in this combat attack. The hold-back
  the USER describes ("until your board is larger or you have evasion or creature combat is more favourable")
  comes out of the search, not a rule: depth counts rounds and the gauntlet deck's objective is its kill turn,
  so waiting a turn for a lethal alpha strike is found whenever it kills sooner. The branching is the subsets of
  at-risk attackers after the folds, which is small on most boards. "Attack with none of them" is always one
  option.
* **Our blocks inside the attacker's lookahead.** The clairvoyant block choice tries each block by replaying the
  game, which is far too costly at every node of the attacker's search. Inside its lookahead the attacker
  therefore predicts our blocks with the non-clairvoyant block rule (section 5.6). The replay itself then uses
  the scored block choice. That mismatch is stated, not hidden. This is the opponent's behaviour as seen from
  the attacker's search, not a greedy pick for the searching side.
* **What the search weighs (USER 2026-11-27, verbatim):** "Essentially, it is the question of what you lose vs the
  opponent in terms of resources or health." The gauntlet deck's kill-turn objective prices all three: damage
  through brings its kill closer, a lost attacker pushes it back, and our blocker dying removes future blocks,
  which also brings it closer. Inside the search window that is exact. Beyond it, two things need care:
  * **The playout must block.** The rollout beyond the horizon plays combat with the same provider attack rule and
    our non-clairvoyant block rule, or it will value every attack as free again.
  * **Gauntlet value leaves are fitted to blocker-free play.** In a replay with blockers they misprice exactly this
    exchange (cf. "turn-to-win does not price resources"). Until they are refitted with blockers, a gauntlet deck
    facing a blocker runs leafless (rollout beyond the horizon), and the cost of that is measured in stage 1.
* **The USER's attack rule for the at-risk attackers (2026-11-27, verbatim):** "If you can attack and lose your
  attacker, but the blocker goes down it might be worth it. Attacking in when only you lose it worth it only when
  you can get through enough damage to either kill the opponent or put them on the ropes for next turn." As a
  provider rule on the attackers the search would otherwise branch on:
  * **A trade** (our block kills the attacker AND the blocker dies) stays a searched option.
  * **A one-sided loss** (the attacker dies to the block and the blocker survives) is pruned UNLESS the attack's
    damage through is lethal this turn or leaves the defender dead to next turn's attack ("on the ropes").
  * **Mixed boards (USER 2026-11-27, verbatim):** "Naturally, if you have one creature that is safe and another that
    would just die uselessly you would only attack with the safe creature." The rules apply per attacker: the
    safe one attacks, the useless one stays home. "Useless" is judged against the WHOLE attack, though. With more
    attackers than our untapped blockers, someone gets through unblocked, so an attacker is a one-sided loss only
    if our block rule would actually block it, with a blocker that survives, given everything else attacking.
  * **A swarm decides as a group (USER 2026-11-27, verbatim):** "If you have a big board of small creatures they
    cannot all say 'oh there is a blocker that would kill me' and not attack, even though that gives each the best
    chance at survival. The question is whether they outnumber the blockers that would kill them and by how much."
    So the rule is applied to the attack as a whole, by count:
    * Our blockers that can kill an attacker can each take at most one attacker (more, only by giving up other
      blocks, as in a double block on a big attacker). With n at-risk attackers and b killing blockers, at most
      b die, and the rest connect or meet blockers that cannot kill them.
    * The searched options are counts, not subsets: with interchangeable attackers folded, "send k of the n" for
      k = 0..n. A k whose losses are one-sided is pruned unless the damage of the k - b that get through is
      lethal or sets up next-turn lethal. Trades stay searched. How far n outnumbers b is what makes a large k
      worth it.
  * **A blocker that kills nothing threatens nothing (USER 2026-11-27, verbatim):** "Also note that a blocker that
    kills nothing (a 0-power wall for example) doesn't prevent anything from attacking." An attacker that no
    blocker can kill is in the can't-die group above and attacks. Such a wall still stops one attacker's damage,
    so it reduces the damage that gets through, which the search and the replay count, but it never makes an
    attacker stay home.
  This is a heuristic prune, unlike the can't-die and interchangeable-attacker rules above, which are sound. It
  is therefore checked against the unpruned search at a large budget on a held-out sample. A game it makes worse
  is a counterexample, and the fix is to the rule's ranking. Its default is the USER's call on that measurement.
* Attack-all stays as a reported upper bound on the blocker's value.

**Q11 (2026-11-27). Blocking in goldfish.** Should the new block step also let the passive opponent's spawned
creatures block our attackers in ordinary goldfish games? Default: no, control-mode replays only. Reason: it
changes every deck's ground truth and the meaning of every goldfish number so far, which is a separate decision.

*USER answer (2026-11-27), verbatim:* "No, we aren't changing normal goldfish games, but we may set up other decks to use
this new control scored mode as well. I imagine in screens we could use a combination of both and in the future
1v1 and the other two modes. For decks that are control heavy we would only use 1v1 and this new mode." ->
Blocking is never switched on in goldfish games. Goldfish ground truth is untouched by this design. The modes per
deck:
* **Goldfish** (`goldfish`, default true): as today.
* **Control-scored** (`control_score`): this design. Other decks may opt in beside goldfish, for example decks that
  run some control cards.
* **1v1** (a future key): when it exists.
* **Control-heavy decks** (Enchanted Evening first) use only control-scored and 1v1, with `goldfish: false`. They
  get no goldfish rows, keep tables or value leaves at all (USER 2026-11-27, verbatim: "This is also why decks that
  are heavy in control will probably not even have a goldfishing mode. It is just not worth the time to work with
  it.").
* **Screens** (`deck_compare.py`) on a deck with more than one mode run each mode it has and report each mode's
  result side by side. The USER said "a combination"; until the USER gives a rule for combining them (weights, or
  which mode decides), a screen's verdict lists every mode's number and does not blend them.
* **A control card needs control evidence (USER 2026-11-27, verbatim):** "When screening out a card that is
  control-based we would need the evidence of this mode and 1v1 most likely. It isn't possible to do with
  goldfishing only." Goldfish sees only a control card's cost (the mana and the slot), never what it does to the
  opponent, so a goldfish-only screen would always cut it. Consequences:
  * **A guard in `deck_compare.py` (to build with stage 2):** when an arm adds or removes a control-based card, the
    screen refuses a goldfish-only verdict and names the missing mode. That matches its existing guard for an
    unscored card.
  * **"Control-based" is derived from the card data, not a hand list:** a card with an effect the injection
    harness and the mapping model (removal, counter, discard, land destruction, sweeper, life gain, damage
    prevention, a blocker the deck casts mainly to block, a lock piece). The classification is listed in the
    screen report so the USER can correct it.
  * **In such a screen only the control-scored mode runs,** with 1v1 joining when it exists. Goldfish is not run at
    all (USER 2026-11-27, verbatim): "Realistically we don't need the goldfish for the control card tests because we
    know it will be totally unreliable." The card's cost to the deck's own clock is already inside the control
    score, through W. The compute goldfish would have used goes to a larger control-mode (and later 1v1) sample
    instead (USER 2026-11-27, verbatim: "So we would rather put its compute toward giving the other modes a larger
    sample.").
* **A non-control swap: goldfish decides, the other modes sanity-check (USER 2026-11-27, verbatim):** "Regarding the
  open question, the control modes are probably more accurate, but also clearly more expensive. Goldfishing on the
  other hand allows us to run a lot of games which is very helpful when we are looking at cards that are just
  about winning faster. We would probably use Goldfishing with the others as sanity checks for such changes. Then
  do more focused effort on the other modes when a control card is involved." So, in a deck with more than one
  mode:
  * **No control card in the swap:** the goldfish screen, at its usual large sample, gives the verdict. The deck's
    other modes run a smaller sample as a sanity check. A sanity check that points the other way, beyond its own
    noise, is reported to the USER rather than overriding the verdict.
  * **A control card in the swap:** the control-scored mode (and 1v1 later) gets the focused effort and decides,
    as above. Goldfish is not run.
  * The screen report states which case it applied and why, from the card classification.

**Q9. Is the per-pair form of section 4.1 what you mean by "<win turn - average control impact>"?** The design
scores each (control game, gauntlet game) pair as `min over prefixes k of max(T_k, W - D_k)` and only then
averages. Default: yes. Reason: it is derived, not chosen (section 4.2: it is the goldfish turn that wins exactly
the same races), it satisfies your turn-5 floor by construction, it gives the "somewhere between Turn 3 and 4"
of section 4.3a, and the plain average-first reading gives a different and cap-dependent number (section 4.4).
The difference from a literal "W minus the average delay" is the floor and the per-pair min, and that is your
decision.

*USER answer (2026-11-26), verbatim:* "I don't know what you mean by per-deck formula, but the value of average
control impact does need to be calculated in the context of the deck." -> Consistent with section 4.1 as built:
"average control impact" is not a fixed number per card. It is measured from what THIS control deck actually did
in THIS game (which effects, on which turns, all applied together), against each gauntlet deck in turn, and
combined with this deck's own win turn W before averaging over the gauntlet. To confirm with the USER in plain
terms (see the chat explanation of 2026-11-26).

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
  Started 14:06:45 UTC, finished 14:32:45 UTC on 2026-11-26 (`=== BATCH done (130 jobs, 26000 games) ===`).
  Scratch: `build/scratch/control-mode/probe.json`, `probe.out`, `probe.err`, `probe_logs/<deck>__<arm>.wins`.
* **Skip diagnosis:** the two-job manifest `build/scratch/control-mode/diag.json` (Burn base and `skip:3`, seed
  7900005, game index 4, `--game-trace-dir`). The traces show Light Up the Stage's two staged cards on round 2
  and an empty staged list on round 4 in the skip arm.
* **Report:** `python3 scripts/control_inject_probe.py report <game_log_dir> <batch_stderr>` (per-arm delays),
  and `python3 scripts/control_inject_additivity.py <game_log_dir> <batch_stderr>` (every table of section 8:
  delays, hit rates, unwon shares, the interaction terms and the `skip` analytic check), both reading the same
  per-game `.wins` files. Scratch output
  lives on the build volume (`build/scratch/control-mode/`); the summary is copied to
  `logs/control-mode/probe-summary.txt`.
