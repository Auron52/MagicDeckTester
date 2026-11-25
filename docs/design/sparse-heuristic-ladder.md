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

- 2026-11-25 (clarifies "I don't want the heuristic ladder for any deck"): *"I don't want the heuristic ladder for
  any deck beyond the value-leaf run."* *"I mean that decks start on the heuristic ladder and upgrade to something
  else after the value-leaf. The idea is that all should upgrade with this option being the alternative option for
  those that seem to currently prefer the heuristic ladder over other alternatives."* *"I want this new mode to be
  capable of beating the heuristic ladder clearly."* *"Even if only by a few percent in some cases. It seems like we
  still have problems where we do more work, though, which makes little sense to me."*
  So: a new deck runs the heur ladder until its value-leaf run; after it EVERY deck ships a non-heur shape (leaf,
  mode 3, ...), and the sparse ladder is the shape for the decks whose best alternative today is heur. The sparse
  ladder must therefore beat heur CLEARLY (a few percent at least) on those decks, and must never do MORE work than
  heur -- an arm that costs more units than heur on any deck is a defect to root-cause, not an outcome.
  *"So what I want to see is all decks moving off the heuristic ladder after the value-leaf with at least a small
  benefit."* *"Ideally a bit bigger than that."*
  *"The heuristic ladder is worth having up front, but most of the reason I want it is calibration, because I
  consider it a problem if we cannot beat it."* So heur stays as the pre-value-leaf shape AND the yardstick every
  post-leaf shape is measured against; a deck where nothing beats it is an open defect.
  *"I mean for certain it is silly to do more work than the rollout. We should find a solution that is slightly better
  than even in the worst examples. As much as I would prefer to see significant benefit for this design over the
  heuristic ladder I may need to set expectations at that level just because on some decks the ladder is a smaller
  portion of the cost."* So the BAR: on every deck whose best alternative is heur, the sparse ladder is never more
  work and at least slightly better than even in the worst case (quality even); significant gains where the ladder's
  superseded rollouts are a large share (Sea, Hydras), small but positive where they are not (Mill, Maze's End).
  *"If there is other headroom I would like us to consider whether that is headroom that we can take benefit of in
  other modes."* So any saving found outside the superseded rollouts (escalation path 2, committing-pass leaves that
  cannot change the decision) is sized for EVERY shape (heur, mode 3, value leaf), not only the sparse ladder.

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
2. **[CORRECTED 2026-11-20, see AB2: the skip share and the tree growth below are TRACE ARTIFACTS.]** **Mode 5 skips 35-79 % of the rollouts, but units and CPU barely move** (units 0.93-1.02, CPU 0.91-1.03): a
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
Measured in AB2 (`sparse` vs `sparse_pwe0`, rebased tree): the fix wins 4 Maze's End games and loses none (sparse_pwe0
+4 (0/4) vs live, sparse 0 vs live), and 1 Mill game (sparse_pwe0 +1); it is digest-identical on every other deck.

### AB2 (2026-11-20, rebased tree, AB1's held-out structure on new seeds)

One pooled `mtg --batch`, 47,200 games, 24 workers (heartbeat 24/24), binary `mtg_d2` (a3de8b3b7 src: the walk-edge fix +
diagnostics), `MTG_P16_TRACE=1 MTG_ESC_P2_MEAS=1`, unit tests rc 0. Bare decks and Sea Creatures / Boars: 6 blocks x 200
games, seeds 17,100,000 + 10,000 x block, arms adjacent. Paired per game, loss = 9, vs `live` (the shipped shape: bare
ladder, Sea = emulated + model, Boars = mode 3 at d6/b40). CPU = the job's `cpu=` field (thread CPU; `abreport.py` read the
wall `ms` field as "cpu" before 2026-11-20 -- the AB1 CPU column is wall). CPU noise between two digest-identical arms is
up to +-6 % per deck (Mill sparse 0.932 vs sparse_pwe0 1.019 at identical units), so only units are a fine signal.

| deck | sparse net (e/l), units / CPU | sg5 net (e/l), units / CPU | heur vs live |
|---|---|---|---|
| Hydras | 0, 0.998 / 1.005 | 0, 0.983 / 0.982 | |
| Martyr Lifegain | 0, 1.011 / 0.994 | +1 (0/1), 1.012 / 1.003 | |
| Maze's End | 0, 1.003 / 0.991 (no-fix twin +4) | +3 (0/3), 1.000 / 0.983 | |
| Merfolk Turns | 0, 0.999 / 0.995 | 0, 0.994 / 0.994 | |
| Mill | 0, 1.006 / 0.932 | 0, 1.000 / 0.986 | |
| Shadowborn Apostle | 0, 1.010 / 0.963 | **+8 (0/7)**, 0.994 / 0.975 | |
| Stuffy Doll | 0, 0.998 / 0.999 | 0, 0.976 / 0.973 | |
| Sea Creatures (emulated + model) | +1 (0/1), 0.943 / 0.951 | +1 (0/1), 0.925 / 0.932 | +1 (0/1), 0.998 / 0.982 |
| Boars (mode 3, d6/b40) | +2 (0/2), 1.205 / 1.300 | +1 (0/1), 1.197 / 1.265 | +7 (0/7), 1.528 / 1.667 |

Sea's one later game (17110151, T6 -> T7) is the same game in heur, sparse and sg5: it is the bare ladder against the
emulated model, not a sparse-ladder defect. Boars: mode 3 stays the best shape (heur +7, sparse +2 at 1.2x units). Sg5's
Shadowborn losses are all mode 2 refusals at T1 (firstdiv: passes 1-3 identical, pass 4 or 5 priced and refused, e.g.
17150038 heur H4 = 11,346 units fits, sg5 refuses at W4 and commits pass 3); its AB1 Hydras refusals did not recur.
**No mode meets the bar on any deck** (quality even AND units/CPU measurably lower): sparse is quality-even everywhere but
saves nothing (units 0.998-1.011, Sea 0.943 at +1); sg5 saves 0-2.4 % units with losses on three decks.

**AB1's mode-5 diagnosis was a trace artifact.** Two recording defects in the [p16] trace, not in play:
1. Mode 5's exact bootstrap passes (mode 2's `_pb1`/`_pb2`/`_pb3`: passes 1, 2 and 3 run on rollouts) were recorded with
   leaf = 0, so `abreport.py` counted their rollouts as skipped and `decstats.py` counted them as tree. The "35-79 %
   skipped" and "leafless trees eat the saving (Mill tree 639 -> 7,976)" in AB1 item 2 are that. Fixed (print-only).
2. Mode 5's priced W records carry the pass's LEAF COUNT in the leaf field (the walk's carry its price).
Measured directly (`tools/wtree.py`, each sg5 leafless pass W_k against the rollout pass H_k of the same depth later in the
same decision): the leafless pass costs 0.15-0.95x the rollout pass's tree on 8 of 9 decks (Merfolk Turns 1.34x), e.g.
Shadowborn 17100014 T1 W4 = 337 units = H4's tree (3,258 - 2,921). **The leafless trees do not grow.** What actually
limits mode 5 is that the superseded rollouts sit in passes 1-3, which it runs exactly. Superseded rollout units by pass
depth, share of all units (`tools/supdepth.py`, live arm):

| deck | pass 1 | pass 2 | pass 3 | pass 4+ | all |
|---|---|---|---|---|---|
| Hydras | 3.1 % | 5.9 % | 8.8 % | 3.8 % | 21.5 % |
| Martyr Lifegain | 3.6 | 14.5 | 10.0 | 1.4 | 29.6 |
| Maze's End | 2.6 | 1.6 | 0.5 | 0.1 | 4.8 |
| Merfolk Turns | 3.6 | 3.6 | 2.1 | 0.5 | 9.8 |
| Mill | 3.4 | 1.4 | 0.6 | 0.2 | 5.8 |
| Shadowborn Apostle | 5.3 | 5.2 | 2.7 | 0.8 | 14.0 |
| Stuffy Doll | 4.4 | 5.0 | 3.4 | 1.7 | 14.4 |
| Sea Creatures (heur arm) | 7.1 | 12.0 | 9.9 | 4.0 | 33.0 |
| Boars (heur arm) | 4.6 | 14.5 | 19.6 | 2.0 | 40.7 |

Mode 5 can bank at most the pass-4+ column (0.1-4 % of units), less its pricing (W + Z = 1-10 % of units). Note also
that superseded rollouts are not all waste: they pre-pay the committing pass's rollouts through the rollout tables (P18:
the committing pass costs ~1.6x more when the pass below ran leafless), so the real ceiling is below the table. P18i's
oracle (perfect K, passes below it leafless, 56 decks x 60) measured 0.925x CPU overall, 0.36-0.60x on rollout-heavy
decks and only 2-4 % on gate-bound decks (Mill, Gods, Hinata, Cyclone, Melira, Shadowborn).

**The early-win cutoff for the rollout-free passes (USER Q1's recommended default) has nothing to cut.** Its premise was
the AB1 tree growth, which is the artifact above. Structurally: (a) the mode-4 walk already cuts every pass at its window
edge, and its deepest pass ends at the root rollout's win W, so an incumbent W prunes nothing inside the window; (b) mode
2's leafless passes already carry the sampled incumbent (`lcut = max(wb_prev - 1, window_end)`); (c) whatever they do,
mode 5's rollout-free passes are only passes 4+, whose superseded rollouts are 0.1-4 % of units. Not built.

#### Escalation PATH 2 (MEASURE ONLY, USER: "only really a good idea if we can drop a lot of lines that we have shown will not win")

16 escalation / learned-hybrid decks x 2 x 50 games, live only, seeds 18,000,000 (+10,000), `MTG_ESC_P2_MEAS=1`
(`tools/escshare.py`). A path-2 leaf is a top-level rollout leaf of the escalation's full heuristic ladder. "No win" = its
rollout returned no win (none in the game, or none by its cutoff: a rollout aborts once past its cutoff); "useless" = no
win by the leaf's cutoff, i.e. it could not change the pass's result.

| deck | path-2 decisions | path-2 units / all units | no-win leaf units / leaf units | useless units / path-2 units | decisions with no win |
|---|---|---|---|---|---|
| Bruna | 58 | 21.6 % | 94.8 % | 88.8 % | 1 |
| Extra Turns | 90 | 1.5 % | 100.0 % | 0.0 % (no cutoff: nothing to beat) | 90 |
| Gods | 88 | 19.7 % | 93.5 % | 53.7 % | 15 |
| Kitty Equipment | 38 | 29.9 % | 82.4 % | 70.0 % | 3 |
| Landfall Elementals | 58 | 21.7 % | 93.7 % | 89.0 % | 1 |
| Prevent Damage | 81 | 16.6 % | 93.4 % | 85.8 % | 4 |
| Vampires | 9 | 7.1 % | 91.7 % | 86.9 % | 0 |
| **all 7** | 422 | 18.4 % (10.4 % of all 16 decks' units) | 92.9 % | 72.9 % | 114 |

The other 9 (Anti-Lifegain, Breaching Dragonstorm, Burn, Creature Giving, Critter Lifegain, Five Colour, Hinata, Knights,
White Knights) never reached path 2 in 100 games. So ~93 % of the fallback's rollout work ends in no win and ~73 % could
not have changed its pass -- but both are known only AFTER the rollout ran; dropping them needs a cheap proof of "will not
win" before rolling out, which nothing here provides. Not built (USER).


### AB3 (2026-11-20): the ORACLE CEILING and mode 3 on the bare decks

One pooled `mtg --batch`, 39,600 games, 24 workers (heartbeat 24/24), binary `mtg_d3` (6d430e300 src), unit tests rc 0, on
AB2's seeds (17,100,000 + 10,000 x block, 6 x 200). Sanity: every baseline job is digest-identical to AB2's, so the oracle
file (`oracle_ab2.txt`, the committed depth K of every AB2 baseline decision, deck-tagged) applies; it covered 98.6-100 % of
the multi-pass decisions. Arms: `oracle` = passes 1..K-1 leafless, only pass K on rollouts, K from the baseline's own
trace (`MTG_P16_ORACLE`, measurement only: it reads another run's answers) -- the ceiling of skipping EVERY superseded
rollout with perfect foresight; `sg3` = mode 3 (leafless lookahead, ladder fallback); `sg3_pwe1` = mode 3 with the
walk-edge roll + window cut in its lookahead (arm key). Baseline = the bare ladder (Sea Creatures: the `heur` arm).

| deck | oracle net, units / CPU | sg3 net (e/l), units / CPU | sg3_pwe1 net (e/l), units / CPU |
|---|---|---|---|
| Hydras | 0, **0.850** / 0.769 | +2 (2/4), 1.023 / 1.021 | +2 (2/4), 1.023 / 1.051 |
| Martyr Lifegain | 0, 0.919 / 0.913 | -5 (6/1), 1.233 / 1.148 | -5 (6/1), 1.233 / 1.073 |
| Maze's End | +1 (0/1), 0.980 / 0.991 | **+118 (55/170)**, 1.432 / 1.806 | **-91 (124/32)**, 1.106 / 1.504 |
| Merfolk Turns | 0, 0.935 / 0.870 | -2 (4/2), 1.076 / 0.941 | -2 (4/2), 1.076 / 0.917 |
| Mill | 0, 0.968 / 0.977 | +30 (29/62), 1.698 / 1.798 | +8 (32/43), 1.677 / 1.722 |
| Shadowborn Apostle | 0, 0.910 / 0.967 | -14 (23/8), 1.335 / 1.224 | -14 (23/8), 1.335 / 1.158 |
| Stuffy Doll | 0, 0.910 / 0.913 | -6 (9/3), 1.157 / 1.069 | -6 (9/3), 1.157 / 1.146 |
| Sea Creatures (vs heur) | 0, **0.721** / 0.728 | -2 (2/0), 1.218 / 1.386 | -2 (2/0), 1.218 / 1.348 |

1. **The ceiling.** Skipping every superseded rollout, with K known, saves 2-28 % of units: Sea 27.9 %, Hydras 15.0 %,
   Shadowborn / Stuffy 9.0 %, Martyr 8.1 %, Merfolk Turns 6.5 %, Mill 3.2 %, Maze's End 2.0 % -- about half of the
   superseded share in AB2's table (the rest is pre-pay the committing pass loses). Any real sparse ladder pays for
   choosing K without foresight out of that; Mill and Maze's End have almost nothing to bank.
2. **Mode 3 is no candidate on any bare deck**: units 1.02-1.70x the ladder (its lookahead misses and then runs the
   ladder anyway). It plays BETTER on Martyr (-5), Shadowborn (-14), Stuffy (-6) and Sea (-2) -- deeper commits through
   its gate credit, at 1.16-1.33x units -- and much worse on Maze's End (+118) without the edge roll.
3. **The walk-edge gap is real in mode 3 too** (USER Q3): on Maze's End the edge roll turns +118 into -91 (124 earlier /
   32 later) and on Mill +30 into +8; it is inert on the other six decks. So the gap costs mode 3 dozens of games where it
   bites. The shipped mode-3 decks (Boars, Morph, Spirits) were not in AB3.
No flips (parent, 2026-11-20: flips and tiers deferred to a later session).
