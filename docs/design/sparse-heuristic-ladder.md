# The sparse heuristic ladder (2026-11-19)

The **sparse heuristic ladder** is the rollout ladder with the rollouts it does not need skipped. It is selected per
deck by `value_play.ladder = "sparse"` in `<deck>.value.json` and is, in code, sampled-gate mode 4
(`valuearm::t_deck_sampled_gate == 4`, `p18::Knobs()` in `src/ai/TurnSolver.cpp`). Its research history is P18
(`docs/design/sampled-gate-ladder.md`, iterations 1-11 and P18i) and P18j (`docs/design/p18-claude-handoff.md`).
This document is the canonical description; those two are the measurement logs.

## USER rulings (verbatim)

- 2026-10-24: *"It should be possible to replace an expensive rollout ladder with a leafless shape or equivalent up
  to the final rollout. Doing early rollouts for little to no benefit makes no sense."*
- 2026-11-13: *"My preferred vision would be that it uses less rollouts when more certain and more when less
  certain. So, it would be closer to the heuristic rollout, but would still bank savings."* *"There shouldn't be
  cases where it is so uncertain it just runs the ladder."* *"It should at least cut out a significant chunk of it."*
  Uncertainty drives more or fewer SAMPLED rollouts under a hard fractional ceiling; it never falls back to the
  whole heuristic ladder.
- 2026-11-17: *"We shouldn't leave any of these open. If there are cases where the mode loses to the heuristic
  ladder we should keep investigating."* Ruling 3: valueleaf.sh gains a stage that measures the mode against the
  heur ladder when the ladder is the shape that would ship, and turns it on when it wins.
- 2026-11-19: *"I don't want the heuristic ladder for any deck."* *"Yes, I think we should finish P18 (or whatever
  it is called) on the branch and probably use a better name like sparse heuristic ladder or something like
  that."* *"The idea is that I want to actually skip some of the rollouts we don't need. I don't see an advantage
  to doing all of them."* *"Delaying the rollouts to the end is also beneficial, but does not encompass my full
  design."*
- 2026-11-19, scope: *"I was primarily just thinking about 1 here. 2 is possible if you can come up with lossless
  ideas for it, though."* -- (1) = the rollouts of SUPERSEDED passes (a deeper pass replaces them), (2) = leaves
  inside the committing pass that cannot change the decision. Bar for (2): *"I guess being no worse on average is
  okay for the bar here, since we aren't impacting the search. This is just the rollouts which already make
  mistakes. However, this rule should apply to just your prunes in 2 in isolation."* *"And, to be clear, it does
  need to be better on some axis. In this case we expect to improve performance. If the quality is even and
  performance better, that is sufficient."* So each (2) prune is A/B'd alone ((1) vs (1) + prune, held out):
  net <= 0 turns AND units/CPU measurably lower.

## The keys

| sidecar `value_play` | engine | status |
|---|---|---|
| `"ladder": "sparse"` | sampled gate 4; any learned model in the file is set aside (`AttachValueSidecar`); `leaf: "none"` overrides it | the shape |
| `"sampled_gate": 4` | the same mode, the research spelling | kept |
| `"sampled_gate": 3` | P18i leafless lookahead, ladder fallback (Boars, Morph, Spirits) | shipped; explicit key wins over `ladder` |
| `"sampled_gate": 2` / `1` | P18 priced sampled gate / raw sampler | research |
| `"sampled_gate": 5` | RESEARCH (2026-11-19): mode 4, then mode 2's priced sampled gate on a miss instead of the ladder | screen arm |

The gate acts only when no value model is attached (`p18_on` requires `!vl_active`), at the top-level decision
(`p16::t_hyb_nest == 1`, not inside rollouts), at every budget.

## What mode 4 does

One rollout of the root (`FSLineWin` depth 0, cut at the window edge, capped at 1% of the decision budget in units
and 10% in priced work, skipped for the rest of a game after two consecutive cuts). If it wins inside the window at
turn W, leafless passes 1..W-T+1 are walked, each admitted only where the ladder's own start gate would admit the
rollout pass it stands in for (priced `l_k + n_k * q * max(0, W-T-k)`); the target pass and cheap leafless passes may
run past that reach under an allowance. The first proven in-window win commits, for the current turn only (no
cross-turn replay). Without a commit, every unit and every priced nanosecond the lookahead spent is refunded and the
ordinary ladder runs on exactly its own budget. The whole lookahead is isolated: its own memo epoch, a scratch table
for the root rollout, no stores into the game-level caches.

At budget 0 nothing becomes unfindable: a lookahead commit is a proven in-window win (the earliest, since every pass
below it was refuted), and a miss leaves the ladder untouched.

## Which shapes run the heuristic ladder

Census 2026-11-20: every shipping deck, 30 games at its shipped settings (seeds 17,500,000 + 1000 x deck index), binary
`mtg_c0` = wip/p18j-int 02c7044e2, `MTG_P16_TRACE=1`. Per top-level decision the trace lists each pass (kind, units,
rollout units). "Superseded rollouts" = rollout units in every rollout pass of a decision except its last (the passes a
deeper rollout pass replaced), as a share of all decision units. Tools: `build/scratch/p18j-int/tools/census.py`.

| shape (value_play) | how it searches (code) | decks | superseded rollouts / units |
|---|---|---|---|
| no sidecar, or play-only sidecar (bare) | `FullSearchLine` rollout ladder 1..K, start gate `c_k^2/c_{k-1} <= 1.1 x rem` | Hydras, Martyr Lifegain, Maze's End, Merfolk Turns, Mill, Sea Creatures, Shadowborn Apostle, Stuffy Doll | 14.6, 9.9, 2.5, 6.2, 5.1, 29.2, 13.8, 17.2 % |
| `sampled_gate: 3` | leafless passes 1..depth; a proven in-window win commits; else the bare ladder | Boars, Morph, Spirits | 3.8, 1.2, 4.3 % |
| `ladder: emulated` | warm-up passes on the model (or leafless with `leaf: none`), committing rollout pass(es) at the emulated depth, stepping down on overrun | Landfall, Melira Pod, Modern RW Burn, NivMizzetCombo | 0.9, 2.3, 7.9, 4.7 % |
| learned model, no ladder key (escalation hybrid) | value-leaf probe; an unverified line escalates to PATH 2 = `FullSearchLine` under `ForceHeuristicLeafGuard(true)`: a full rollout ladder from pass 1 (TurnSolver.cpp ~124288) | Gods, Landfall Elementals, Prevent Damage, Vampires (the others escalated on no rollout in 30 games: Anti-Lifegain, Breaching Dragonstorm, Burn, Critter Lifegain, Five Colour, Hinata, Knights, White Knights) | 1.1, 0.8, 1.0, 0.3 % |
| `ladder: escalation, leaf: none` | leafless probe ladder; an unverified line escalates exactly as above (path 2 heur ladder) | Bruna, Extra Turns, Kitty Equipment (Creature Giving: no rollout) | 2.2, 0.4, 5.3 % |
| `ladder: single, leaf: none` (FIT) | leafless probe as deep as its gate allows, then ONE rollout pass at the fitted depth (shallower only on overrun) | Accomplished Alchemist, Angels, Auras, Colossus Hammer, Dragons, Eldrazi Displacer Flicker, Fungus, Galadriel, Giants, Goblins, Jumbo Cactuar, Kithkin, Lifegain Heliod Combo, Merfolk, Midrange Burn, Minotaur, Mirrorwing Dragon, Pirates, Red Elementals, Selesnya Lifegain, Slivers, Snow, Soldiers, Treasure Hunt, Unpredictable Cyclone; `leaf: none` without ladder (Dragonstorm, Fluctuator, Stompy Surprise): leafless only | 0.0 % on every deck |

So the leafless FIT single pass is the one shape that never runs a superseded rollout. The two escalation shapes run
the heuristic ladder inside their escalation, where no sampled gate acts (`p18_on` requires `!g_force_heuristic_leaf`).
The sparse ladder composes only with the bare and mode-3 shapes as they stand; for an emulated deck the sparse arm
sets the deck's model aside (a shape change, the model is then unused), and for the escalation shapes it would need
the gate admitted inside the escalation (not done; a USER question).

## Measurements

### AB1 (2026-11-20, held out)

One pooled `mtg --batch`, 102,370 games, 24 workers (heartbeat 24/24 throughout), binary `mtg_c0` (= 02c7044e2 src),
shipped settings, `MTG_P16_TRACE=1`. 15 decks x 6 blocks x 200 games, seeds 17,000,000 + 10,000 x block, arms adjacent
per (block, deck). Arms: `live` (the shipped sidecar), `sparse` (`ladder: sparse` = mode 4), `sg5` (mode 5, research),
and on the mode-3 and emulated decks `heur` (the bare ladder, deck play block kept). Paired per game; loss = 9.
"roll" = rollout units (leaf part of every pass + the root rollout) vs the baseline; "skip" = share of the baseline's
rollout units not spent. Tools: `build/scratch/p18j-int/tools/abreport.py`, `decstats.py`, `firstdiv.py`.

Bare decks (live = the heuristic ladder):

| deck | sparse net (e/l), t | sparse units / CPU | sparse skip | sg5 net (e/l), t | sg5 units / CPU | sg5 skip |
|---|---|---|---|---|---|---|
| Hydras | 0 (0/0) | 0.991 / 0.931 | 4.2 % | +4 (0/4), +2.00 | 0.984 / 1.030 | 36.5 % |
| Martyr Lifegain | -1 (1/0), -1.00 | 1.012 / 0.949 | 3.6 % | 0 (1/1) | 1.018 / 0.970 | 37.5 % |
| Maze's End | **+10 (0/10), +3.17** | 1.005 / 0.905 | -0.2 % | **+12 (0/12), +3.48** | 1.002 / 0.948 | 57.1 % |
| Merfolk Turns (pre-adoption) | 0 | 1.003 / 0.992 | 0.0 % | 0 | 0.991 / 0.990 | 34.5 % |
| Mill | 0 | 1.004 / 1.003 | 0.5 % | +1 (0/1) | 1.004 / 0.991 | 78.6 % |
| Sea Creatures (pre-adoption) | 0 | 0.984 / 0.971 | 3.8 % | 0 | 0.930 / 0.908 | 51.0 % |
| Shadowborn Apostle | 0 | 1.013 / 0.988 | 0.5 % | 0 | 1.016 / 0.995 | 56.9 % |
| Stuffy Doll | 0 | 0.999 / 1.006 | 0.8 % | 0 | 0.985 / 0.983 | 39.8 % |

Mode-3 decks (live = `sampled_gate: 3`) and emulated decks (live = `ladder: emulated`), each vs live and vs heur:

| deck | live vs heur: net (e/l), units, skip | sparse vs live: net (e/l), units | sparse vs heur: net, units, skip | sg5 vs live: net, units |
|---|---|---|---|---|
| Boars (pre-adoption) | -12 (13/1), 0.746, 75.8 % | **+9 (1/10)**, 1.076 | -3 (3/0), 0.803, 35.6 % | +9 (1/10), 1.079 |
| Morph | -8 (9/1), 0.800, 76.5 % | **+8 (1/9)**, 1.238 | 0, 0.991, 3.4 % | +8 (1/9), 1.251 |
| Spirits | -1 (1/0), 0.704, 81.0 % | +1 (0/1), 1.092 | 0, 0.769, 39.7 % | +1 (0/1), 1.089 |
| Landfall | -33 (37/4), 1.093 | **+33 (4/37)**, 0.920 | 0, 1.006, -0.3 % | +33 (4/37), 0.920 |
| Melira Pod | -1 (1/0), 1.017 | +1 (0/1), 1.008 | 0, 1.025, 0.7 % | +1 (0/1), 1.008 |
| Modern RW Burn | +1 (0/1), 1.027 | -1 (1/0), 0.948 | 0, 0.974, 16.6 % | -1 (1/0), 0.951 |
| NivMizzetCombo | -12 (12/0), 1.112 | **+12 (0/12)**, 0.905 | 0, 1.006, -0.4 % | +12 (0/12), 0.904 |

What AB1 says:

1. **Mode 4 skips almost none of the ladder's rollouts on the decks that run it**: 0-4 % on every bare deck, against
   2.5-29 % of units that the census measured as superseded rollouts there. It acts only when the root rollout wins
   inside the window AND a leafless pass proves that win; every other decision runs the whole ladder. That is the
   case the USER ruled out (*"There shouldn't be cases where it is so uncertain it just runs the ladder."*), so
   mode 4 alone is not the USER's design; mode 5 (a miss goes to the priced sampled gate) is.
2. **Mode 5 skips 35-79 % of the rollouts, but units and CPU barely move** (units 0.93-1.02, CPU 0.91-1.03): a
   leafless pass has no incumbent, so its tree is far larger than the rollout pass's tree it replaces. Per decision
   (decstats): Mill live = 639 tree + 9,362 rollout units, sg5 = 7,976 tree + 1,968 rollout units; Maze's End 440 +
   11,588 vs 7,023 + 4,927. The rollouts saved are spent again on incumbent-free trees.
3. **Maze's End loses 10 games under mode 4 (12 under mode 5), all the same shape** (firstdiv, 7 of the 10): the ladder's
   pass 2 commits a win at T+1, the walk's leafless pass 2 completes WITHOUT one, and leafless pass 3 commits the
   root rollout's T+2 win. The walk's premise (every pass below the commit refuted the earlier win) fails: the
   leafless pass refutes a T+1 win the rollout pass finds. Root cause and fix: below.
4. **Mode 3 beats both the bare ladder and the sparse ladder on Boars and Morph** (live vs heur -12 and -8, at 0.75x and
   0.80x units): the approved fold of the mode-3 decks into sparse does NOT meet its condition ("at least as good as
   their mode 3"): sparse vs mode 3 is +9 (Boars), +8 (Morph), +1 (Spirits). They stay on mode 3.
5. **On the emulated decks sparse = the bare ladder** (it sets the model aside), which is +33 (Landfall) and +12 (Niv) vs
   the shipped emulated ladder. Not flipped.
6. Mode 5's own losses (Hydras +4) are priced-gate refusals: it commits a shallower pass than the ladder (Hydras
   17010116 T1: ladder pass 4 wins T6, sg5 commits pass 3, T7).

### The walk-edge gap (Maze's End, AB1) and its fix

Maze's End 17000084, T4 decision (MTG_FSW_TRACE=1 MTG_FSW_TURN=5, binary mtg_c0): the same breakpoint node child at T5
d1 -- `land=Maze's End; Manabond, Maze's End` with the continuation `[play Maze's End, activate Maze's End]` -- scores
tail=5 under the ladder's rollout leaf and tail=9 under the walk's constant leaf. The tail of a window turn that a
continuation (or an edge node) hands to the horizon is played out by the rollout leaf, which finds the win in that turn;
the constant leaf answers "no win" for it. So leafless pass 2 "refuted" a T5 win that rollout pass 2 has, and leafless
pass 3 committed the root rollout's T6 win. Every one of the 7 traced Maze losers has this shape.

Fix (walk only; modes 4 and 5): `p18::t_walk_edge` = the walk pass's last window turn; a leaf at or before it is rolled
out (cut at the window edge) instead of answering constant, so the walk's leafless pass sees every in-window win the
rollout pass sees, and its refutations are refutations again. Leaves past the window stay constant. Default on
(`MTG_P18_WALK_EDGE_ROLL=0` / arm key `_pwe0` turn it off for the A/B); no deck ships mode 4 or 5, so no GT moves.
The same gap exists in every other leafless shape (FIT and escalation probes, emulated warm-ups, mode 3's lookahead);
not touched here (a USER question: it would move GT on ~30 decks and needs its own A/B).
Measured in AB2 (`sparse` vs `sparse_pwe0`, rebased tree): pending.
