# Deferred: rollout-ladder shapes for the bare decks, and the window-edge leaf (2026-11-21)

Status: DEFERRED to a separate session (USER 2026-11-21: *"I think it makes sense to address 6-8 in a separate session
next week."*). Nothing here is built on the shared branch. Every adoption is a USER decision.

This doc names everything involved so the work can be picked up cold. It collects three questions that were numbered
6, 7 and 8 in the 2026-11-21 session. Two of them (6 and 7) collapsed into one proposal; 8 stands on its own.

## Background: the search shapes involved

During a main-phase decision with no value model, the engine runs `FullSearchLine` as a ladder of passes at depth
1, 2, ... K, and commits the deepest one that finished. A pass either rolls out its horizon leaves (the permitted
greedy playout past the search window) or is *leafless*: every horizon leaf is scored as the constant "no win"
(tail 9) without being played out. The per-deck `value_play` block in the deck's sidecar chooses the shape:

| shape | `value_play` keys | how it searches | decks (census 2026-11-20) |
|---|---|---|---|
| bare ladder | no sidecar, or play-only sidecar | rollout passes 1..K; each pass's rollouts are thrown away once a deeper pass finishes | Hydras, Martyr Lifegain, Maze's End, Merfolk Turns, Mill, Shadowborn Apostle, Stuffy Doll (Sea Creatures was bare in the census; it has shipped the emulated ladder since 2026-11-20) |
| FIT single pass | `"ladder": "single", "leaf": "none"` (+ `alpha: relaxed`) | leafless probe as deep as its gate allows, then ONE rollout pass at the fitted depth `dpass` (shallower only on overrun); a win the probe proves commits directly | ~25 decks (Alchemist, Angels, Auras, Colossus Hammer, Dragons, EDF, Fungus, Galadriel, Giants, Goblins, Jumbo Cactuar, Kithkin, Lifegain Heliod Combo, Merfolk, Midrange Burn, Minotaur, Mirrorwing Dragon, Pirates, Red Elementals, Selesnya Lifegain, Slivers, Snow, Soldiers, Treasure Hunt, Unpredictable Cyclone) |
| leafless only | `"leaf": "none"`, no ladder key | leafless passes only | Dragonstorm, Fluctuator, Stompy Surprise |
| escalation, leafless probe | `"ladder": "escalation", "leaf": "none"` | leafless probe ladder; an unverified line escalates to the full rollout ladder (PATH 2) | Bruna, Extra Turns, Kitty Equipment, Creature Giving |
| emulated ladder | `"ladder": "emulated"` | warm-up passes on the model (or leafless), committing rollout pass(es) at the emulated depth | Landfall, Melira Pod, Modern RW Burn, Niv-Mizzet Combo, Sea Creatures (interim until a ladder replacement; adopted 2026-11-20) |
| mode 3 | `"sampled_gate": 3` | leafless lookahead 1..depth; a proven in-window win commits, else the bare ladder runs | Boars, Morph, Spirits |
| sparse (mode 4) / sg5 (mode 5) | `"ladder": "sparse"` = `"sampled_gate": 4`; `"sampled_gate": 5` | research shapes on `wip/p18j-int`; ship on no deck | none |

Engine anchors: `valuearm::t_deck_single` (src/ai/AIEngine.cpp, set from `value_play.ladder == "single"`); the
sampled-gate modes and `p16`/`p18` thread-locals in src/ai/TurnSolver.cpp. Superseded-rollout census and the
measurements AB1-AB3: `docs/design/sparse-heuristic-ladder.md` **on branch `wip/p18j-int` (tip 8704050f7, parked, CI
green; not on the shared branch)**, with its resume file `logs/p18j-int/handoff.md` on the same branch, and scratch
tools under `build/scratch/p18j-int/tools/` (census.py; the scratch volume, regenerable).

## Item A (was questions 6 and 7): move the bare-ladder decks to a shape that skips superseded rollouts

### What the two original proposals were, and why both were dropped

- **Q6 "depth chooser" (sparse Q1).** Build a per-decision predictor of the ladder's final depth K, run passes 1..K-1
  leafless and only pass K on rollouts. Ceiling, measured in AB3 with perfect foresight (`MTG_P16_ORACLE`, which reads
  another run's committed K; measurement only): saves Sea Creatures 27.9% of units, Hydras 15.0%, Shadowborn / Stuffy
  9.0%, Martyr 8.1%, Merfolk Turns 6.5%, Mill 3.2%, Maze's End 2.0%, at equal play. A real chooser pays for its lack
  of foresight out of that ceiling.
- **Q7 mode 3 on the bare decks (sparse Q2).** Mode 3 plays better on Martyr (-5 games), Shadowborn (-14), Stuffy
  (-6), Sea (-2) but at 1.16-1.33x units (AB3), because its lookahead usually misses and the ladder then runs anyway.
- USER 2026-11-21 on Q6: *"I don't know what this is. Obviously we do need to estimate and guess to a degree to get
  savings, but I don't understand what your proposal actually is."* On Q7: *"Isn't this idea similar to escalation? I
  don't get what it buys over that idea. I do think that fully searching a depth before rollouts is good, though."*
  Agent reply (accepted as the restatement): mode 3 is essentially escalation with a leafless probe plus a change to
  how the probe's spend is credited (`p18_credit`), so both proposals drop. Mode 3 stays shipped on Boars/Morph/Spirits.

### The surviving proposal

"Fully search a depth before rollouts" is what FIT already does. So: one pooled held-out A/B moving the bare decks
(Hydras, Martyr Lifegain, Maze's End, Merfolk Turns, Mill, Sea Creatures, Shadowborn Apostle, Stuffy Doll) to FIT,
with three arms per deck:

1. today's shape (bare ladder; Sea Creatures: its emulated ladder, and the `heur` arm);
2. FIT, `{ladder: single, leaf: none, alpha: relaxed}`;
3. escalation with a leafless probe, `{ladder: escalation, leaf: none}`, for comparison.

**Known risk, measured before (read first):** `docs/design/hydras-shapes-vs-heuristic-ladder.md`. Plain FIT
(`nlfit`) LOST to the ladder on Hydras, +0.00237 turns (se 0.00102), because its one rollout pass ran shallower than
the ladder's committed depth (42 of 42 classified worse games; d1 in 38). The cause was the affordability estimate
overpricing a rollout pass 2.4-20x. The prototype climb `value_play.fit_gate: "ladder"` (`MTG_FIT_GATE_LADDER`,
`nlfitL`, local branch `hydras-shapes`, commits 6fc82b00d / 1bc20b95d, never pushed) reached parity (-0.00071 turns) but at
1.154x units on Hydras, where the shallow rollout passes are memo-cheap. So the A/B should include `nlfitL` as a fourth
arm, and the doc's own open questions (fleet screen of nlfitL, the probe-record fix) belong to this session too.
Related: `docs/design/fit-climb-fleet-screen.md`, `docs/design/single-ladder-skip-leaves.md`.

Bar: per deck, net <= 0 turns on held-out seeds, every slower game recovering at `--depth 8 --budget-ms 0`, and
units measurably lower (or quality better). Then smoke/regression GT accept for the decks that switch.

## Item B (was question 8): the window-edge ("walk-edge") leaf

USER 2026-11-21: *"I don't follow what the suggestion is."*, then *"I do get the issue but not entirely the solution
you are recommending. That said, I guess it is fine to try it out. However, if we are deferring this work I will need
to know what it is all called."*

### The issue

A leafless pass at depth k searches the turns T .. T+k-1 (its *window*). Where it stops expanding, it scores the
position "no win". That is right for a position past the window: the pass only claims to find wins inside it. But a
line can reach the end of the search tree while it is still INSIDE the window. Typically a draw/tutor breakpoint
continuation uses up the remaining depth in the middle of a window turn. Then "no win" is wrong: the rest of that turn
was never played. The rollout pass at the same depth plays that turn out and finds the win. So the leafless pass
falsely refutes an in-window win, and the ladder commits a later one.

Worked example: Maze's End seed 17000084, T4 decision. The breakpoint child `land=Maze's End; Manabond, Maze's End`
with continuation `[play Maze's End, activate Maze's End]` scores tail 5 (a T5 win) under the rollout leaf and tail 9
("no win") under the constant leaf. Leafless pass 2 therefore refuted the T5 win, and the engine took a T6 line. All 7
traced Maze's End losers in AB1 had this shape.

### The fix, concretely

In a leafless pass, look at each leaf's turn when it is reached:
- **leaf turn <= the window's last turn** (T+k-1, capped at the root's cut): run the ordinary greedy rollout from that
  position, **cut at the window's last turn**. It costs at most the rest of the window, never the whole game, and it
  reports a win only if one lands inside the window.
- **leaf turn past the window:** unchanged, constant "no win".

After the fix, a leafless pass sees every in-window win the rollout pass at that depth sees. Its refutations are true
again, and a leafless commit stays a proven earliest in-window win. The only extra cost is the rollouts at in-window
leaves. It is not a heuristic in the search window: the rollout is the permitted beyond-horizon greedy, and nothing is
pruned.

### Names

| name | what |
|---|---|
| `p18::t_walk_edge` | thread-local in src/ai/TurnSolver.cpp (on `wip/p18j-int`): the last turn of the running leafless pass's window; 0 = off. A leaf with `state.turn_number <= t_walk_edge` (outside a rollout) is rolled out instead of answering constant. `p18::t_walk_edge_rolls` counts them. |
| `MTG_P18_WALK_EDGE_ROLL` | env flag, default ON on `wip/p18j-int`; `=0` turns the in-window roll off. |
| arm keys `_pwe0` / `_pwe1` | A/B-only arm suffixes: `_pwe0` = fix OFF in mode 4/5's walk; `_pwe1` = fix ON (roll + window cut) in mode 3's lookahead. |
| commits (all `wip/p18j-int`) | a3de8b3b7 (the fix in the sparse walk), 6d430e300 (`_pwe1` arm for mode 3), be35a6a6a (AB3 doc). |

### Where it is built today, and where it would go

- **Built:** only the sparse walk (modes 4 and 5), which no deck ships, so no GT moved.
- **Proposed:** every shipped leafless pass, i.e. FIT's probe (~25 decks), leafless-only (Dragonstorm, Fluctuator,
  Stompy Surprise), the escalation probe (Bruna, Extra Turns, Kitty Equipment, Creature Giving), emulated warm-ups
  (Landfall, Melira Pod, Modern RW Burn, Niv-Mizzet Combo, Sea Creatures) and mode 3's lookahead (Boars, Morph,
  Spirits). About 30 decks' GT would move.

### Measurements so far

- Mode 4 (AB2, `sparse` vs `sparse_pwe0`): the fix wins 4 Maze's End games and loses none, plus 1 Mill game;
  digest-identical on every other deck.
- Mode 3 (AB3, `sg3` vs `sg3_pwe1`, vs the bare ladder): Maze's End +118 (55 earlier / 170 later) becomes -91
  (124 / 32); Mill +30 becomes +8; inert on the other six bare decks. Units 1.432x -> 1.106x on Maze's End.
- NOT yet measured: how often FIT's probe hits an in-window leaf, and the fix's effect on any shipped shape.

### Proposed A/B

Port the in-window roll to the shared branch's leafless leaf site (the same constant-leaf branch the `wip/p18j-int`
change edits), behind a new default-OFF flag (name to be chosen, e.g. `MTG_LEAFLESS_WINDOW_EDGE_ROLL`). Then run one
pooled held-out A/B over the ~30 leafless-shape decks at their shipped settings, on vs off. Same bar as item A. If item
A runs in the same session, put both in one pooled batch.

## Order for the session

1. Read this doc, `hydras-shapes-vs-heuristic-ladder.md`, and `sparse-heuristic-ladder.md` on `wip/p18j-int`.
2. Item B first: it is a soundness fix to every leafless pass, and item A's FIT arm runs on such a pass.
3. Item A with the item-B result in the FIT arm (and nlfitL as an arm).
4. Report per deck with numbers; the USER decides each flip.
