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
  *"I don't want to restrict us to just upgrading this mode."* *"i.e. I don't want to do a bunch of upgrades and then
  find out that these could have been applied to everything else and we didn't do that. I'm happier to take any
  upgrades that apply everywhere, just a slight advantage over the heuristic ladder in tough cases and a bigger
  advantage in easier cases."* So PREFER general savings (they apply to every shape: heur, mode 3, leaf, sparse) over
  sparse-only ones; every saving found is checked for whether it also applies to the other shapes before it is built
  as sparse-only. The sparse ladder's bar vs heur stays: slight advantage on tough decks, bigger on easy ones.

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

### K1 (2026-11-25, rebased tree wip/p18k-claude, 8 bare decks x 200, seeds 18,000,000, `mtg_k1`, trace on)

`build/scratch/p18g/K1/` (tools `summ.py`, `ana6.py`, `cen_k.py`, `dvs_k.py`). Quality 0/0 on every deck.
Sparse (mode 4) vs heur, units: Sea 0.916, Hydras 1.009, Martyr 1.004, Maze's End 1.004, Merfolk Turns 1.000, Mill
1.011, Shadowborn 1.009, Stuffy 1.000. Mode-4 waste: a cut root rollout costs ~0.5 % of units on EVERY deck (Maze's End
gets 0.01 % of hits for it); walk misses 0.8-1.2 % on Mill / Shadowborn.

**General headroom found: MTG_BP_DEV_SEQ's pass 2.** When the one-deviation ladder (pass 1) met a latent occurrence,
a second whole ladder searches the full tree to pass 1's depth with root cut = pass 1's win - 1. `MTG_DEV_SEQ_STATS`
on the heur arm (K1 seeds): pass 2 better in 1 of 803 runs; its units, as a share of the deck's decision units:
Martyr 9.9 %, Hydras 9.8 %, Shadowborn 3.1 %, Maze's End 2.3 %, Mill 1.9 %. The existing lossless skip condition
(`p2_skippable`, every latent occurrence past the cut) covers ~0 of it. H2 traces (56 decks, heur arm, older binary):
pass 2's committing pass = Melira 20.7 %, Goblins 15.0 %, Hydras 10.1 %, Landfall 9.4 %, Vampires 6.4 %, Accomplished
Alchemist 5.9 % (pass 2 better in 7 of 103 there), Martyr 5.5 %, Fungus 4.8 %. Pass 2 is INTERIOR work (0-3 % of its
committing pass is rollout leaves) costing 0.34-1.9x pass 1's committing pass: the compound waves themselves under
the cut. It runs inside every shape that runs FullSearchLine, so a saving here is general (USER: prefer those).

### K2-K4 (2026-11-25): the sparse ladder across dev-seq's two passes (`P18DvsWalk`)

Same 8 decks x 200 games and seeds as K1 (`build/scratch/p18g/K2..K4`, tools `acct.py`, `acct2.py`, `p2acct.py`,
`p2cmp.py`, `zdist.py`, `zback.py`). Heur is byte-identical across K1/K2 (the change acts only under mode 4).

**What was wrong.** Pass 1 and pass 2 of a two-pass search are two FullSearchLine calls, and each ran its own sparse
lookahead. The walk's line memo was a local of the call, so after a pass-1 walk commit pass 2 re-ran the root rollout
(under a cut it could not win by) and the whole rollout ladder cold. Worse, the walk commits deeper than the heuristic
ladder reaches (Martyr 18350002 T1: heur commits pass 3 on a rollout T5 and meets no latent, so it runs NO pass 2; the
walk proves T5 at pass 5, meets a latent there, and pays a depth-5 pass 2). K1: mode 4's pass 2 = 15.7 % of Martyr's
units vs heur's 9.9 %; on Martyr's walk-hit decisions heur's pass 2 totals 18k units, mode 4's 269k.

**K2** (memo + epoch carried, pass 2 walks, pass-2 root rollout skipped when pass 1 proved it cannot win): passes 1-3
of the pass-2 walk cost 0, but its passes 4-5 stayed expensive and a capped walk fell through to the cold ladder:
units Hydras 1.025, Martyr 1.014 (worse). **K4** adds the root-cut clamp (pass 2 walks only to cut - T + 1), the
ladder carry's dvs bookkeeping on the walk memo (tail skip / node keep / apply skip), and no cold-ladder fallback
after a budget-cut pass-2 walk. Quality 0/0 on every deck; units vs heur:

| deck | K1 | K4 |
|---|---|---|
| Hydras | 1.009 | **0.990** |
| Martyr Lifegain | 1.004 | **0.966** |
| Maze's End | 1.004 | 1.004 |
| Merfolk Turns | 1.000 | 1.000 |
| Mill | 1.011 | 1.012 |
| Sea Creatures | 0.916 | 0.915 |
| Shadowborn Apostle | 1.009 | 1.009 |
| Stuffy Doll | 1.000 | 1.000 |

Mode 4's pass 2 after a walk hit: Martyr 7.5 % (K2) -> 2.7 % of units, Hydras 4.8 % -> 1.3 %.

**K3: `_ptp1` (arm key, research) -- the trigger is the ladder's own pass 1** (no root rollout; walk 2..W-T+1 when pass
1's line wins in the window): 9 games earlier / 0 later, but units 1.07-1.34x heur (K3) / 0.97-1.30x (K4 code): the
trigger fires on most decisions and most walks miss (Mill waste 37 % of units). Its hits are real (Stuffy 18580003
T1: 3.2k vs heur 6.2k, a hit mode 4 misses because its root rollout is cut). Not adopted; kept default off.

**What is left on the five even decks: the cut root rollout at T1.** `zdist.py` (K4): cut root rollouts cost 0.42 %
(Maze's End), 0.54 % (Mill), 0.60 % (Merfolk Turns), 0.75 % (Shadowborn), 0.46 % (Stuffy) of units, and 90-99 % of
them are T1 decisions, where the per-game back-off (`_pzb`, two consecutive cuts) cannot act yet. The T1 hits mostly
win at the window edge (Hydras 46 of 48 at W = T+4), so a tighter rollout cut kills them; a one-cut back-off saves
0.16 % on Maze's End and loses 2.6 % on Stuffy (`zback.py`). Walk misses are small (<= 0.08 %).

#### Checkpoint 2026-11-25 ~02:40 UTC (Claude, worktree `.claude/worktrees/p18j-int-note`, branch `wip/p18k-claude`)
State: wip/p18j-int rebased onto origin phase-1-2-deck-analyzer (1 conflict, ClearPerGameCaches, both lines kept);
Release built (`build/scratch/p18g/mtg_k1`), units 4168/4168. Nothing running. Next, in order:
1. Dev-seq pass 2 (general): find what its committing pass re-walks (compound waves under the cut) and cut what
   provably cannot win by cut2 -- LOSSLESS first. A budget-wave re-price (pass 2 only on spare budget, inert at b0)
   is the lossy-at-budget fallback and is the USER's call (surfaced 2026-11-25).
2. Sparse mode's own waste: the cut root rollout (~0.5 % everywhere) and walk misses -- back off per game when the
   lookahead does not pay; let a missed walk's work feed the ladder without moving its start gate.
3. Re-measure every heur-preferring deck (bar: never more work, slightly better than even in the worst case).

#### Checkpoint 2026-11-25 ~03:30 UTC (Claude)
Done: P18DvsWalk (K4 above), unit tests 4168/4168 on the K4 binary (`build/scratch/p18g/mtg_k4`). Nothing running.
Next: (1) the T1 trigger cost on Maze's End / Mill / Merfolk Turns / Shadowborn / Stuffy -- a trigger that costs ~0 where
it does not pay (candidates: `_ptp1`'s free trigger with a miss bound; reusing a cut root rollout's work); (2) dev-seq
pass 2 general (heur too): Martyr / Hydras ~9.8 % of units, still the biggest general headroom; (3) re-measure on
held-out seeds, then the valueleaf.sh stage and per-deck flips.

### E1-E8 (2026-11-25): where mode 4's cost and saving live; the cached-trigger walk (`_ptc`)

Same 8 decks x 200 games and seeds as K1-K4 (`build/scratch/p18g/E1..E8`, merged with K2's heur arm in `ME*`; tools
`lacct.py`, `zcls_heur.py`, `reachk.py`, `conf.py`, `cpot.py`, `hookacct.py`, `simt1.py`). Units / heur; quality 0/0 on
every arm below unless noted.

1. **Every walk that misses is one the ladder would not follow** (`reachk.py`, K4): no walk-miss has heur's committed
   K >= its target pass. Walks that HIT past heur's reach (K < kto) are still a saving on Martyr / Sea (deep leafless
   cheaper than heur's shallower rollout passes) and a coin flip on Mill / Shadowborn.
2. **The root-rollout cap is not the lever** (E1, `_pzc20/30/50`): Hydras 0.970, Maze's End 0.984-0.995 better; Mill
   1.031-1.062, Shadowborn 1.019-1.038, Martyr 0.986 worse.
3. **ALL of mode 4's effect is on turn 1** (E4 `_pnt1` = no lookahead on turn 1: 1.000 +- 0.001 on all 8 decks; `simt1.py`).
   Mill's +1.2 % and Shadowborn's +0.9 % are turn 1; so are Sea's -8.5 %, Martyr's -3.4 %, Hydras' -1.0 %. Later turns
   never walked: their root rollout is answered from the rollout game cache (0 units), which prices nothing.
4. **`rgc::E::cost`** (data only, byte-identical: 160/160 m4 jobs equal): every game-cache entry now stores the units its
   rollout spent from that key's state, so a cached root rollout can be priced. Walking on it at once (`_pzh`) is
   1.03-1.16x: past-reach walks (Mill +17 %); with the priced reach and no extension (`_pcx`, `_pcr` chain) still
   1.00-1.02x, because in cached decisions the ladder's rollouts are cached too and the walk's q-prices overestimate
   them (`conf.py`: the chain refuses decisions heur did reach).
5. **`_ptc` (cached trigger walks AFTER the ladder's pass 1)**: a cached in-window root rollout arms the pass-1 hook;
   the ladder's rollout pass 1 runs as heur's does, then the walk from pass 2 is priced on pass 1's REAL cost and
   per-leaf rollout cost (cache hits included), with the chain reach and no extension, and only when pass 1 spent
   >= 50 % of its cost on rollouts (`_pts`; `cpot.py`: below that the deeper passes are cache-cheap and a walk only
   duplicates their tree -- Hydras 15 % of units there, 0.02 % to save). A refusal costs nothing (`hookacct.py`).
   Without the share rule `_ptc` was 1.03-1.15x (E5).

| deck | m4 | m4_ptc | m4_ptc_pnt1 | m4_ptc_pnt1_pnc (no chain) | m4_pzh_pcr_pnt1 |
|---|---|---|---|---|---|
| Hydras | **0.990** | **0.989** | 1.000 | 1.001 | 1.011 |
| Martyr Lifegain | 0.966 | **0.964** | 0.998 | 0.998 | 0.998 |
| Sea Creatures | **0.915** | **0.915** | 1.000 | 1.001 | 1.001 |
| Mill | 1.012 | 1.008 | **0.997** | 1.010 | 1.001 |
| Shadowborn Apostle | 1.009 | 1.005 | **0.995** | 1.006 | 0.991 |
| Maze's End | 1.004 | 1.004 | 1.000 | 0.996 | 0.995 |
| Merfolk Turns | 1.000 | 1.003 | 1.003 | 1.007 | 1.012 |
| Stuffy Doll | 1.000 | 0.999 | 1.000 | 1.000 | 1.002 |

Rejected: the guarded pass-1 trigger on turn 1 (`_pt1g`, 1.000 on every deck: the turn-1 saving IS pass 1's rollouts,
which only a trigger before pass 1 can skip); `_ptp1` guarded everywhere (`_ptgs` 0.994-1.003, no turn-1 saving);
the walk without the chain reach (`_pnc`).

**So:** turn 1's root-rollout walk is worth -1 to -8.5 % on Hydras / Martyr / Sea and +1 % on Mill / Shadowborn, and no
per-decision signal we have separates them (win-turn means, Z0 cost, leafless growth all overlap). Proposal: `_ptc` in
the default mode 4 (never worse than m4 by more than 0.003 here), and the turn-1 lookahead a PER-DECK choice made by
measurement in the value-leaf run's sparse stage (on for Hydras / Martyr / Sea, off for Mill / Shadowborn here). Held-out
check HO1 (600 games x 8 decks, seeds 19,000,000; heur, m4, m4_ptc, m4_ptc_pnt1) running.

#### HO1 (held out, 8 decks x 600 games, seeds 19,000,000, binary `mtg_k11`; quality 0/0 on every arm)

| deck | m4 | m4_ptc | m4_ptc_pnt1 |
|---|---|---|---|
| Hydras | 0.975 | **0.974** | 1.000 |
| Martyr Lifegain | **0.967** | **0.967** | 1.001 |
| Sea Creatures | 0.923 | **0.922** | 1.000 |
| Mill | 1.005 | 1.003 | **0.999** |
| Shadowborn Apostle | 1.001 | **0.997** | **0.997** |
| Maze's End | 1.003 | 1.003 | 0.999 |
| Merfolk Turns | 1.002 | 1.003 | 1.001 |
| Stuffy Doll | 1.002 | 1.001 | 1.000 |

Units / heur (CPU agrees to ~0.5 %). The screen's -0.3 to -0.5 % on Mill / Shadowborn shrinks to -0.1 / -0.3 % held out.
**Why Mill cannot move much under this design** (heur arm, M24, rollout vs tree units by pass): the COMMITTING pass's
rollouts are 86.5 % of Mill's units (T1 30.9 + T2+ 55.6), its superseded passes 4.8 % (rollouts 2.3 + 2.5, tree 0.2 +
1.6); Shadowborn 74.2 % / 13.5 %; Sea 57.0 % / 36.4 %. Skipping superseded work can only reach that second number, and a
walk has to prove its win leafless to skip any of it. What is left on Mill is the committing pass's own rollouts (class
(2): leaves that cannot change the decision), which the USER's 2026-11-19 scope allows only with lossless ideas.

#### OR1: the ceilings on K1's seeds (oracle K from K2's heur trace, `MTG_P16_ORACLE`; 200 games x 8; measurement only)

`oracle` = passes below heur's own committing K leafless, K on rollouts; `oracleasp` = plus the committing pass cut at its
recorded final win (P17's aspiration oracle). Units / heur, quality 0/0 (an oracle file's tags must not start with a
digit -- `p16::Oracle()` reads a numeric first field as the seed; the per-deck seeds are disjoint, so no tag is needed).

| deck | m4 | oracle | oracleasp |
|---|---|---|---|
| Hydras | 0.990 | 0.886 | 0.887 |
| Martyr Lifegain | 0.966 | 0.943 | 0.943 |
| Maze's End | 1.004 | 0.982 | 0.980 |
| Merfolk Turns | 1.000 | 0.927 | 0.927 |
| Mill | 1.012 | **0.971** | 0.961 |
| Sea Creatures | 0.915 | 0.753 | 0.760 |
| Shadowborn Apostle | 1.009 | **0.940** | 0.889 |
| Stuffy Doll | 1.000 | 0.922 | 0.899 |

So with K known, Mill has 2.9 % and Shadowborn 6.0 % to bank (11 % with the final bound too). Mode 4 banks only the
decisions whose win a leafless walk can PROVE in the window; the rest of the ceiling is choosing K without foresight
(P17-handoff's open problem, mode 2 / 5's priced gate).

#### E9-E10: mode 5 and the skip-leaves ladder on today's code (K1's seeds, 200 x 8; units / heur)

- Mode 5 (`m5` = mode 4 + mode 2's priced gate on a miss): Hydras 0.977, Sea 0.893, Merfolk Turns 0.973, Martyr 0.964 (better
  than m4 on three), Mill 1.005, Shadowborn 1.008, Maze's End 1.050, Stuffy 1.023; quality 0/0. `m5_ptc_pnt1` = 1.000 on every
  deck: mode 2's bootstrap runs passes 1-3 exactly, and at T2+ the committing K is <= 3, so its gate never acts after turn 1.
- `rollout_from: 2` (P16 skip-leaves; pass 1 leafless on every decision): Mill 4.31x, Maze's End 2.64x, Shadowborn 1.35x
  (pass 2 forced on rollouts where heur commits pass 1; 48 earlier / 7 later), Hydras 0.929 / Martyr 0.949. Not a candidate.

#### Checkpoint 2026-11-25 ~07:30 UTC (Claude)

State: `wip/p18k-claude` 00c8f7e24 + this note; nothing running. Shipped default mode 4 is unchanged (all new levers are
arm keys). Proposal awaiting the USER: (a) `_ptc` into default mode 4; (b) a per-deck turn-1 switch (`_pnt1`) chosen by the
value-leaf run's sparse stage -- on for Hydras / Martyr / Sea (HO1 0.974 / 0.967 / 0.922), off for Mill / Shadowborn (HO1
0.999 / 0.997). Mill and Shadowborn are EVEN, not yet "slightly better": their remaining ceiling (OR1: 2.9 % / 6.0 %, of
which T2+ 1.7 % / 2.0 %) needs the committing depth K chosen without foresight at T2+, where mode 2's priced gate does not
act (its exact bootstrap covers K <= 3). Next candidates: a guarded priced-K at T2+ after pass 1 (pass k leafless when the
priced gate admits k+1 by a margin, pass-1 q from real rollouts, the `_ptc` share rule), A/B'd for quality.

#### E11: `_pgk`, the guarded priced skip at T2+ (K1's seeds, 200 x 8; units / heur; REJECTED)

`_pgk`: after a real pass 1 that spent >= half its cost on rollouts, pass k (2 <= k < depth) first runs leafless; an
in-window win commits, else it is priced `l_k + n_k * q * turns` (q = pass 1's rollout units per leaf per turn) and, if heur's
start gate would still admit pass k+1 on that price, pass k's rollouts are skipped. Control `m4_ptc` on the same binary
reproduced E6 160/160 jobs (digest-identical): the arm is inert when off.

| deck | m4_ptc | m4_ptc_pgk | m4_ptc_pnt1 | m4_ptc_pnt1_pgk |
|---|---|---|---|---|
| Hydras | 0.989 | 0.990 | 1.000 | 1.000 |
| Martyr Lifegain | 0.964 | 0.975 | 0.998 | 1.011 |
| Maze's End | 1.004 | 1.015 | 1.000 | 1.011 |
| Merfolk Turns | 1.003 | 0.991 | 1.003 | 0.991 |
| Mill | 1.008 | **1.031** | 0.997 | **1.020** |
| Sea Creatures | 0.915 | 0.898 | 1.000 | 0.969 |
| Shadowborn Apostle | 1.005 | 1.018 | 0.995 | 1.006 |
| Stuffy Doll | 0.999 | 0.995 | 1.000 | 0.995 |

Quality 0/0 everywhere, but the cost goes the wrong way on the target decks. Paired with heur (`gpair.py`): the loss is in
the decisions with a skip, which commit DEEPER than heur (Mill 8 of 11, +2.7 % of the deck's units; shallower ~never),
because the price is low. The price is not calibratable (`gerr.py`, heur's real pass-k units / the price on the skipped
pass): median 0.43 (Stuffy) .. 1.71 (Mill) .. 5.9 (Maze's End), tails 100-9000x. A rollout's cost per leaf in pass 1 says
little about pass k's leaves (rollout-cache hits vary leaf to leaf). Fall-throughs (leafless pass run, then the rollout pass
anyway) are only 0.2-2 % of units. Not pursued further.

#### E12 setup: `_pab`, aspiration from the pass below (any mode, plain heur included)

K2 heur (`wprev.py`): the committing pass K's win equals pass K-1's in most T2+ decisions (Mill 288 eq / 16 better / 21
worse; Shadowborn 274 / 15 / 29). `_pab` starts pass k >= 2 at the completed rollout pass k-1's win turn (inclusive); a pass
that finds nothing by it is searched again from the root cut (the existing aspiration-fail path). Sound at every budget
(same best win turn; tie order may differ). Arms: `heur` (control), `hx_pab` (plain heur + `_pab`; no sidecar, the arm
name reaches the deck key), `m4_ptc_pab`, `m4_ptc_pnt1_pab`.

#### E12 result: `_pab` (K1's seeds, 200 x 8; units / heur; quality 0/0 on every arm; heur control = K2 160/160)

| deck | hx_pab | m4_ptc | m4_ptc_pab | m4_ptc_pnt1 | m4_ptc_pnt1_pab |
|---|---|---|---|---|---|
| Hydras | 0.999 | 0.989 | 0.994 | 1.000 | 1.004 |
| Martyr Lifegain | 1.008 | 0.964 | 0.971 | 0.998 | 1.005 |
| Maze's End | 0.993 | 1.004 | 0.996 | 1.000 | 0.992 |
| Merfolk Turns | 1.017 | 1.003 | 1.020 | 1.003 | 1.020 |
| Mill | 0.996 | 1.008 | 1.007 | 0.997 | 0.995 |
| Sea Creatures | 1.008 | 0.915 | 0.927 | 1.000 | 1.013 |
| Shadowborn Apostle | **0.973** | 1.005 | 0.996 | 0.995 | **0.983** |
| Stuffy Doll | 0.979 | 0.999 | 0.978 | 1.000 | 0.979 |

No aspiration failure was recorded (`apair.py`). The saving sits in same-depth decisions (Shadowborn T1 -0.79 % / T2+
-2.69 %, Stuffy -0.96 / -1.15 %, Mill T2+ -0.47 %). The cost is DEEPER commits through heur's gate (Merfolk 2 decisions
+1.76 %, Martyr 10 +0.83 %, Shadowborn 12 +0.93 %). In each (`adeep.py`) heur's pass 1 was answered from the rollout
game cache (3-50 units, leaf 0), so its c2/c1 ratio refused pass 3; under `_pab` the earlier decisions' bounded rollouts
were cut at W (stored as no-win at that bound, not as wins), pass 1 paid 100-1000 units, and the gate admitted pass 3
(Merfolk 18390157 T2: 39k on an 18k limit). Heur's K there is a by-product of what the previous decision cached.

#### E13: `_pab1` (pass 1 bounded at the previous top-level decision's committed win in the same game) and `_pabx` (a
failed bounded attempt kept out of the gate's pass cost). K1's seeds, 200 x 8, mtg_k14, units / heur (the CPU column of
this run is contended by a concurrent reference scan: ignore it).

| deck | hx_pab | hx_pab1 | hx_pab1_pabx | m4_ptc_pab1_pabx | m4_ptc_pnt1_pab1_pabx |
|---|---|---|---|---|---|
| Hydras | 0.999 | 0.998 | 0.998 | 0.988 | 0.999 |
| Martyr Lifegain | 1.008 | **1.000** | 0.998 | 0.956 | 0.992 |
| Maze's End | 0.993 | 0.998 | 1.031 | 1.038 | 1.051 |
| Merfolk Turns | 1.017 | **0.999** | 0.998 | 1.000 | 1.002 |
| Mill | 0.996 | **0.988** | 0.993 (1 earlier) | 1.034 (1 earlier) | 0.994 (1 earlier) |
| Sea Creatures | 1.008 | 1.006 | 1.005 | 0.920 | 1.007 |
| Shadowborn Apostle | 0.973 | **0.959** | 0.968 | 0.976 | 0.992 (1 earlier) |
| Stuffy Doll | 0.979 | 0.994 | 0.959 (1 LATER) | 0.962 | 0.969 |

`hx_pab_pabx` = `hx_pab` exactly (no pass-k fail to exclude). `_pab1` removes the deeper-commit cost (Merfolk 1.017 ->
0.999, Martyr 1.008 -> 1.000) and deepens the saving on Mill / Shadowborn, quality 0/0. `_pabx` is the arm that moves win
turns (1 later on Stuffy): dropped. E14 (done, below): m4_ptc_pab1, m4_ptc_pnt1_pab1, m4_pnt1_pab1 on mtg_k14 (`run_e14.sh`).

#### E14: `_pab1` combined with mode 4 (same seeds, mtg_k14; heur = E12's, hx_pab1 = E13's; units / heur)

| deck | hx_pab1 | m4_ptc | m4_ptc_pab1 | m4_ptc_pnt1 | m4_ptc_pnt1_pab1 | m4_pnt1_pab1 |
|---|---|---|---|---|---|---|
| Hydras | 0.998 | 0.989 | **0.988** | 1.000 | 0.999 | 0.999 |
| Martyr Lifegain | 1.000 | **0.964** | **0.964** | 0.998 | 0.999 | 1.000 |
| Maze's End | 0.998 | 1.004 | 1.002 | 1.000 | 0.999 | **0.998** |
| Merfolk Turns | 0.999 | 1.003 | 1.002 | 1.003 | 1.015 | **0.999** |
| Mill | **0.988** | 1.008 | 1.000 | 0.997 | 0.987 (1 LATER) | **0.989** |
| Sea Creatures | 1.006 | **0.915** | 0.922 | 1.000 | 1.006 | 1.007 |
| Shadowborn Apostle | **0.959** | 1.005 | 0.976 | 0.995 | 0.962 | **0.960** |
| Stuffy Doll | 0.994 | 0.999 | **0.993** | 1.000 | 0.997 | 0.997 |

The two savings add: m4_ptc_pab1 keeps the turn-1 walk's gain (Sea / Martyr / Hydras) and adds `_pab1`'s (Shadowborn
1.005 -> 0.976, Mill 1.008 -> 1.000). With the turn-1 lookahead chosen per deck (m4_ptc_pab1 on Hydras / Martyr / Sea /
Stuffy, m4_pnt1_pab1 elsewhere) every one of the 8 decks is below heur: Hydras 0.988, Martyr 0.964, Maze 0.998, Merfolk
0.999, Mill 0.989, Sea 0.922, Shadowborn 0.960, Stuffy 0.993. Sea's `_pab1` cost (+0.6 %) is ONE turn-1 decision
(18500017): the bounded passes 2-3 were cheaper (953 / 3582 vs 983 / 3778), the gate's estimate c3^2/c2 fell from 14.5k to
13.5k against ~12.6k remaining x alpha, and it admitted a 17.9k pass 4. HO2 (held out: m4_ptc_pab1, m4_pnt1_pab1 on HO1's
seeds, paired with HO1's heur / m4_ptc) running.

**Where m4_ptc's turn-1 cost is on the decks it does not help** (`gdiv.py`, `uncharged.py`, `zcost.py`, HO1): every
paired decision class is cheaper than heur, also in the games whose turn-1 play diverged. The loss is UNCHARGED work:
the root rollout's misses are refunded from the budget, so they show in the batch's units but not in the decisions'
`used`. Turn-1 root rollouts that miss cost (% of heur's units) Hydras 0.86, Martyr 0.56, Maze 0.35, Merfolk 0.56, Mill
0.45, Sea 0.84, Shadowborn 0.74, Stuffy 0.68, and almost all of them are CUT at the 1 % cap (Mill 542 of 550, Maze 1313
of 1325), not completed no-wins. The lookahead is epoch-isolated, so a miss's turn solves cannot warm pass 1's memos.

**E15 (queued behind HO2): `_pzr`** -- no epoch isolation and no refund in the lookahead; a miss's spend (root rollout
+ missed walk, `gamework::t_used`) goes on pass 1's gate cost through the old `p18_z_add` hook, so the gate reads pass 1
at about heur's cost while pass 1 reuses the turn solves. `_pzrt` also runs the root rollout on the decision's rollout
table. Arms m4_ptc_pzr, m4_ptc_pzrt, m4_ptc_pab1_pzr on mtg_k15 (`build_k15.sh`).

**E15 result: `_pzr` REJECTED (refuted premise).** Units / heur (K1 seeds): m4_ptc_pzr Hydras 0.979, Martyr 0.980, Maze
0.983, Merfolk 0.956, Mill 1.016, Sea 0.892, Shadowborn 0.966, Stuffy 0.993 -- but **3 games LATER** (Maze 137, Mill 41,
Mill 177; 1 earlier, Martyr), same on `_pzrt` / `_pab1_pzr`. Pass 1 reuses NOTHING of a missed root rollout: over the
T1 misses (paired with m4_ptc) pass 1 is cheaper by 0 % of the root rollout's units on 6 decks, 2 % Maze, 4 % Mill. So
every saving is the gate made more conservative by the charged units: Mill 41 T1 (cut Z0 181 units, pass 1 2779 both
arms) -> the bootstrap refused pass 2, win T6 instead of T5; Mill 177 T1 (a missed walk's 3.6k charged) -> pass 1
only, T5 instead of T4. A missed root rollout's work is not recoverable through the memos; the knobs stay, inert.

**HO2 (held out, 600 x 8, seeds 19M paired with HO1's heur / m4_ptc; units / heur):**

| deck | m4_ptc | m4_ptc_pab1 | m4_pnt1_pab1 |
|---|---|---|---|
| Hydras | **0.974** | 0.978 | 1.003 |
| Martyr Lifegain | **0.967** | 0.981 | 1.015 |
| Maze's End | 1.003 | 1.004 (1 earlier) | 1.000 (1 earlier) |
| Merfolk Turns | 1.003 | 1.005 (1 earlier) | 1.002 (1 earlier) |
| Mill | 1.003 | 1.005 | 1.001 |
| Sea Creatures | 0.922 | **0.916** | 0.992 |
| Shadowborn Apostle | 0.997 | 0.966 | **0.962** |
| Stuffy Doll | 1.001 | **0.966** | **0.966** |

`_pab1`'s held-out costs (`apair.py`, `bootx.py`): aspiration fails (Martyr +0.73 %, Hydras +0.35 %) and deeper commits
through heur's gate, of two kinds. (a) BOOTSTRAP: a bounded pass 1 answered from the game cache (Mill 19410160 T2: 110
units, heur's 4825) admits a pass 2 of 62k (19410210: 131 -> 153k): Mill +0.93 % in 2 decisions -> `_pabb` (D16). (b)
the mirror (Martyr +1.5 %, 23 decisions): heur's pass 1 was the cache-cheap one (4-26 units vs our 112-196), so heur's
c2/c1 refused pass 3 and ours admits it (E12's mechanism; same win). The HO seeds are now the DEV set; the final
per-deck choice is confirmed on fresh seeds (HO3, 20M).

**D16 (dev seeds, mtg_k16): `_pabb` REJECTED.** Pricing EVERY game-cache hit of a bounded pass 1 at its stored cost makes
the bootstrap refuse pass 2 almost everywhere: units 0.69-0.99 but **41 games later** (Maze 12, Shadowborn 12, Stuffy 6).
heur on k16 = heur on k11 byte for byte (digests and units). CPU per unit (same-run): heur 0.043 in HO1 and D16; the
`_pab1` arms 0.044 in HO2 -- not yet measured beside heur in one batch (owed in HO3).
**D17 (running): `_pabc`** -- price only the no-win hits the BOUND made usable: taken before pass 1 has an incumbent (the
leaf's cutoff is still the bound) with a stored bound below the root cut, which an unbounded pass 1 cannot read.


#### USER rulings 2026-11-25 (verbatim)
- On `_ptc` in the default mode 4: *"If it is better overall then sure."* Held-out HO1: m4_ptc 0.9951 vs m4 0.9957 pooled,
  quality 0/0 both -- so `_ptc` goes into the default mode 4 when the sparse ladder lands.
- On a per-deck turn-1 lookahead setting: *"I mentioned before that I prefer no deck-specific settings, but I think it is
  okay if we are running into tough problems here."* So a per-deck on/off is allowed as the fallback, chosen by
  measurement (value-leaf run), not as the first resort.

#### D17 / D18: `_pabc` (dev seeds 19M x 600, units / heur)

D17 (mtg_k17): units 0.919-1.012 but 5 games LATER (Maze 395, Merfolk 413, Shadowborn 26 / 536 / 566). Four of them
were a pass 1 whose BOUND FAILED (`A1`): it was searched again from the root cut -- an unbounded pass 1, as heur's --
and the bounded attempt's priced hits still went on the bootstrap, which refused pass 2 (Shadowborn 19520026 T3:
pass 1 152 units vs heur's 147, heur's pass 2 found T6 instead of T7). Fixed in k18 (nothing priced after a failed bound).

| deck | m4_ptc | m4_ptc_pab1 | **m4_ptc_pab1_pabc** (k18) | m4_pnt1_pab1_pabc (k18) |
|---|---|---|---|---|
| Hydras | 0.974 | 0.978 | **0.976** | 1.001 |
| Martyr Lifegain | 0.967 | 0.981 | **0.979** | 1.012 |
| Maze's End | 1.003 | 1.004 | **0.995** (1 earlier / 1 later) | 0.992 |
| Merfolk Turns | 1.003 | 1.005 | **0.979** (1 earlier) | 0.976 |
| Mill | 1.003 | 1.005 | **0.979** | 0.976 |
| Sea Creatures | 0.922 | 0.916 | **0.916** | 0.992 |
| Shadowborn Apostle | 0.997 | 0.966 | **0.927** | 0.923 |
| Stuffy Doll | 1.001 | 0.966 | **0.962** | 0.963 |

ONE setting below heur on all 8 decks, no per-deck switch: m4_ptc_pab1_pabc (pooled 0.976, 2 earlier / 1 later). The
later game (Maze 395, T5 -> T6) is being checked at d8 b0 (`R395/`). Confirmation on FRESH seeds: HO3 (20M x 600; heur,
m4_ptc, m4_ptc_pab1_pabc in ONE batch for a same-run CPU ratio; mtg_k18; `run_ho3.sh`) running.

- On `_pab1` as a general change: *"Let's see how things go. If the results are good then perhaps it could be worth
  adopting. But yes, you are right that the new mode is intended to do less work for the same depth compared to the
  heuristic ladder. The reason is that the decks in which the heuristic ladder is winning out are exactly those that
  punish us for searching further. I would like to avoid that doing so, so that the setting is actually good on those
  decks."* So in the sparse mode a saving must NOT be spent on extra depth (the gate drift `_pabc` removes); the general
  (all-modes) `_pab1` question stays open pending results.
- Maze 395 (the one D18 later game): reproduced at d5 b20 (arm T6, heur T5); at d8 b0 the arm wins T5 with heur's exact
  digest -- recovers.

#### HO3 (FRESH seeds 20M x 600 x 8, mtg_k18, heur in the SAME batch; cpu = same-run CPU / heur, u = units / heur)

| deck | m4_ptc | **m4_ptc_pab1_pabc** |
|---|---|---|
| Hydras | 0.971c 0.973u | 0.976c 0.973u |
| Martyr Lifegain | 0.968c 0.979u | 0.974c 0.978u |
| Maze's End | 0.998c 1.003u | 0.962c 0.983u (0 / **1 later**, 227) |
| Merfolk Turns | 0.993c 1.002u | 0.982c 0.990u |
| Mill | 1.009c 1.004u | 0.987c 0.988u |
| Sea Creatures | 0.898c 0.895u | 0.895c 0.892u |
| Shadowborn Apostle | 0.992c 1.001u | 0.897c 0.910u (1 earlier / 1 later, 454) |
| Stuffy Doll | 1.002c 0.998u | 0.984c 0.980u (1 earlier) |
| pooled | 0.988c 0.995u | **0.959c 0.973u**, 2 / 2 |

Both later games recover at d8 b0 with heur's digest (`RH3/`: Maze 227 T6 = T6, Shadowborn 454 T5 = T5). Maze over
D18 + HO3 is 1 earlier / 2 later -- net +1, which fails the per-deck bar. Both have ONE mechanism (`RH3/L`, `RH3/S`):
pass 1 costs exactly heur's (28 / 39 units, cache-answered, same win), `_pabc`'s price refuses pass 2, heur's pass 2
(56k / 39k on an 18k limit) finds a turn earlier. The priced entries are the arm's own previous-decision bounded rollouts
(no-win at bound W); heur's are unbounded (wins or no-win at the root cut) and answer its pass 1 free.

**Mode 4 default = the candidate** (k19, commit 7f2831b70): `_ptc` in `p18::Knobs` mode 4, `_pab` `_pab1` `_pabc` at the
pass loop when the sampled gate is 4; `_xptc` / `_xab` opt out. C19 (k19, arm `m4`, HO3's seeds) = HO3's
m4_ptc_pab1_pabc job for job (480/480 digests and units). Unit tests 4168/4168. A `B` trace record carries the price.

**Calibration (C19 vs HO3 heur, `pabx.py`, `gatex3.py`, `cdx.py`, `uncharged.py`):**
- c1 + price is 4-11x heur's own c1 (geometric mean) -- but as a predictor of the NEXT pass's cost it is far better than
  heur's c1 x 6: the refused heur passes ran 20k-162k on an 18k limit. Most of `_pabc`'s saving on Maze / Mill / Merfolk is
  the gate refusing passes that heur's gate admits on a cache-answered pass 1 and that then overrun.
- Pricing the hits at any fraction f does not fix the opposite drift (deeper than heur: 29 decisions at f = 0, 21 at
  f = 1) while the shallower ones grow 17 -> 69: a bounded pass 1 is cheaper than heur's for a real reason (it skips the
  unbounded pass's chain of improving incumbents), which no cache price recovers.
- The refunded lookahead (untraced units) costs over heur: Hydras +1.3 %, Martyr +2.8 %, Maze +0.4 %, Merfolk +0.7 %,
  Mill +0.85 %, Sea +2.6 %, Shadowborn +1.3 %, Stuffy +0.7 % -- the turn-1 root rollout, CUT at the 1 % cap in 79-91 % of
  T1 decisions on Maze / Merfolk / Mill / Shadowborn; wins spread right up to the cap (q90 166-178 of 180), so a tighter
  cap loses them (as E1 found).
- **When a deeper pass helps** (HO3 heur, decisions with >= 2 passes): a deeper pass improves pass 1's win 0 % (W-T=1,
  n=1569), 0.6 % (2, n=2966), 5.1 % (3, n=3674), 16.2 % (4 = the window edge, n=8124). Among C19's priced refusals that
  heur went deeper on (104), heur's deeper pass improved 4 -- all at the edge; the edge holds -0.35 % Maze, -0.29 % Mill,
  -0.12 % Merfolk, -1.25 % Shadowborn of the saving.

D20 (k20, HO3's seeds): `_pabk` (consistent aspiration: pass k >= 2 bounded only when pass 1 was) changes little
(Maze 227 still later); `_pabk_xpc` (no price) is Maze 0.997u 0/0 but Mill 1.005u, Merfolk 1.000u, Shadowborn 1/1 (511).

**`_pce` (k21, commit b59c18710): no price when pass 1's win is at the window edge (W >= T + depth - 1).** HO4 (FRESH
seeds 21M x 600 x 8; heur, m4, m4_pce in one batch; `build_k21.sh`) running.
