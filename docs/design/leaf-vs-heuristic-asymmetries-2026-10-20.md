# Does the search favour the heuristic leaf over the value leaf? (2026-10-20)

**Question (USER, after Landfall's value-leaf run):** *"There is no reason why default, which I assume is
heuristic rollouts at every depth should win against all alternatives in cost"*, and *"it seems we have been
giving advantages just to the heuristic."* Landfall's phase E verdicts (8,000 held-out games each, against
the shipped play: no sidecar, the greedy horizon leaf, `search_leaf_depth 0`):

| Shape | Cost | Win turn |
|---|---|---|
| staged learned leaf | 1.710x | -0.029 (faster) |
| no-leaf escalation (`nlesc`) | 1.441x | +0.055 |
| no-leaf single pass (`nlfit`) | 1.238x | +0.093 |

This page records what was checked, what was fixed, and what is left. Companion to
`leaf-pick-and-dead-end-memo-2026-10-17.md`.

## Fixed

1. **Dead ends were not memoised once a pass had spent its budget** (`MTG_PROVISIONAL_NOWIN`, default ON,
   commit "search: keep dead ends found after the budget is spent"). A no-win is stored only when nothing
   beneath it was cut short; past the budget every node skips its wave phases and books that as a cut, so
   every transposition of a dead end was searched again at full price. Now kept under a per-pass provisional
   key. Play-identical on smoke and regression; 0 games moved in every Landfall arm below.
2. **The lazy-leaf probe ran where its miss was already known** (unlimited budget only; commit "search: no
   lazy-leaf probe where its miss is already known"): under a no-leaf ladder the probe IS the ladder, and
   the hybrid's heuristic escalation re-proved the value pass's leafless miss. Digest-identical at d5 b0 on
   10 decks; Goblins 0.913x, Minotaur 0.911x, Angels 0.872x units.

## Measured, NOT a heuristic advantage

**Same seeds (820000/821000 x 200), memo fix on vs off, Landfall:**

| Shape | memo fix on/off units | vs live, fix off | vs live, fix on |
|---|---|---|---|
| live (greedy leaf) | 0.933x, 0 moved | -- | -- |
| staged leaf | 0.995x, 0 moved | 1.61x, -0.018 (se 0.018) | 1.72x |
| nlesc | 0.993x, 0 moved | 1.32x, +0.070 | 1.41x |
| nlfit | 1.000x, 0 moved | 1.10x, +0.095 | 1.17x |

The memo defect cost the HEURISTIC most on Landfall; fixing it widens the gap slightly.

**Strict child cut at an O(1) leaf** (`MTG_FS_STRICT_CUT_O1`, default off 2026-10-17..20 on a measurement that
showed it RAISING value-leaf cost). With the memo fix on, held-out over every deck shipping a value sidecar plus
Landfall's staged leaf (31 decks x 400 paired games): **3 of 12,800 games moved, pooled units 1.003x** (0.92-1.28x
per deck). The "raises cost" was the memo defect. *Superseded 2026-10-21 -- it is now DEFAULT ON; see below: the
saving is in the memo, not the leaf.*

**Why Landfall's leaf costs 1.7x: it is never trusted, by its own table.** `value_leaf_table` (400 x 4
seeds): V5 5.7153 vs H5 5.6915 (paired gap 0.0238, upper95 0.0304 against tol 0.002). The leaf only matches
the greedy leaf at V6-V8 (5.6896 / 5.6884 / 5.6878), and trust is granted only at depths <= the play depth
(5): *"a trust_depth > search_depth is equivalent to UNSET in play."* So every unverified decision pays the
value pass AND a full heuristic escalation. For this deck a single greedy playout is a better and nearly as
cheap evaluator than the learned model; the remedy is a better model (or a different play depth), not search
plumbing. On 2026-10-17, simply raising Landfall's play budget bought more for less (40 ms -0.030 at 1.34x,
60 ms -0.053 at 1.53x; before the keep table, to be re-checked).

## Measured, a TRADE (USER's call; nothing changed)

**The leafless exhaustion stop** (`ConstantExhaustMult`, default 1 = a no-leaf pass stops at its budget; the
model pass runs on to the 25x overrun ceiling). Its stated reason was the memo defect, so with the fix on it
was re-measured: every deck shipping `leaf: "none"` (12), seeds 830000-832000 x 200, run-on 1 / 4 / 25:

| Deck | 4x / 25x vs 1x |
|---|---|
| Fluctuator | -0.0050 (3 games), 1.007x |
| Snow | -0.0050 (3 games), 1.34x / 1.46x |
| Mirrorwing Dragon | -0.0034 (2 games), 1.08x |
| Fungus | -0.0017 (1 game), 1.14x |
| treasure_hunt | +0.0017 (1 game), 1.05x |
| the other 7 | identical play, 1.00-1.06x |
| **pooled** | **1.205x / 1.269x** |

Small, mostly-positive quality for ~20% more work: a budget trade, not a free fix. Per-deck
`value_play.exhaust_mult` already exists if the USER wants it on a deck. (An audit read the code as pushing the
six FIT decks to heuristic depth 1 when the probe exhausts the budget; play is identical on all six at 4x/25x,
so that does not bind in practice.)

## Parked / unmeasured

- **The take rule for `leaf: "none"` probes.** `line_constant` is wired to the `nl_esc` route only, so a
  constant-model probe line goes through the model-calibrated crossover table. Extending the guard
  (patches kept in `logs/tgt/leafless_take_rule*.patch.txt` of the work tree, not committed) measured
  quality-neutral on 12 decks and **1.27x / 1.29x units on Dragonstorm / Goblins** (Goblins play identical;
  Dragonstorm one seed's digest moved at the same average): the crossover skip it disables was doing useful,
  safe work there. Needs a better design than extending the
  guard; not adopted.
- **O(1) leaf estimates are stored order-exact** (the rollout's past-cutoff results become bounded no-wins
  under the order-free key, so zone-order twins share them; a model estimate becomes an order-exact WIN).
  The model's features are counts, so the estimate itself is order-invariant. Unmeasured; size it with the
  `MTG_OF_WAVE_PROBE` counters on a model-leaf deck before building anything.
- **The strict start-gate alpha default for no-leaf ladders** (1.10 vs the model's 8.8) is justified in code by
  the memo defect. Every shipped no-leaf sidecar already sets `alpha: "relaxed"`, so flipping the default
  moves no shipped play; left until a new deck needs it.

## 2026-10-21: why the default beat every alternative, and what now beats it

USER: *"At least one of them should outperform the default or we should fix it so that they do."* All numbers
below are on lf-integrate (phase-1-2 + the fixes above), Landfall with its keep table, b20 / d5 / max_turns 8,
2,000 paired games per arm (seeds 840000-843000 x 500 unless marked held-out = 860000-863000 x 500). Scripts and
raw output: `logs/tgt/{deep,curve,blind,sites,emul,emulf,emulh,nlfix}/` of the lf-target work tree.

### 1. Phase E compared shapes at the same NOMINAL budget, and they do not spend it alike

| Shape | b | units x live b20 | win turn vs live b20 |
|---|---|---|---|
| live | 15 / 20 / 30 / 40 / 60 | 0.85 / 1.00 / 1.22 / 1.40 / 1.68 | +0.015 / 0 / **-0.023** / **-0.033** / **-0.042** |
| staged leaf | 8 / 10 / 15 / 20 | 1.21 / 1.34 / 1.67 / 1.86 | +0.029 / +0.011 / -0.014 / -0.029 |
| nlesc | 10 / 20 / 30 | 1.04 / 1.55 / 1.91 | +0.116 / +0.054 / +0.025 |
| nlfit | 10 / 20 / 30 | 0.91 / 1.30 / 1.62 | +0.165 / +0.074 / +0.051 |

Every shape sits 0.03-0.05 turns behind live's own curve at equal cost; raising live's budget is almost purely
better (b30: 46 games faster, 0 slower). A shape is only "better" if it beats that curve, so **phase E must judge
against a live budget curve, not one live point** (open item below). Pushing the trusted leaf past the play depth
(value_play target 6/7/8 with value_trust_depth = target) does not change this at b20: ~1.73-1.78x for -0.033 to
-0.035.

### 2. Where the work goes

At b20 (300 games): live spends 27% of its units in rollouts (`rollout_step` + `greedy_fallback`) and 72% in the
tree. A d0 greedy rollout over at most three turns is cheap here: the emulated ladder measures **2.6-11 units per
leaf** against the engine's 120 prior. So on this deck the most any O(1) leaf can save is about a quarter of the work;
the leaf shapes save most of that (rollouts 4-6%) and then spend more tree: staged's value-ladder committing pass
costs 9.1M units against live's 4.6M, because its relaxed start gate admits passes estimated at 8.8x the remaining
budget (the 1.10x heuristic gate would refuse them).

### 3. FIXED: the O(1) leaf was denied the strict child cut -- `MTG_FS_STRICT_CUT_O1` is now DEFAULT ON

Unbudgeted d5, 200 games, the same games in every arm:

| arm | units | re-searches with a permuted twin's WIN available | play |
|---|---|---|---|
| rollout leaf (strict cut) | 17.76M | 2% | -- |
| value leaf (inclusive cut, as shipped) | 21.23M (1.20x) | 23% (twin agrees 99.8%) | -- |
| value leaf + strict cut | **17.57M (0.99x)** | -- | byte-identical to the value leaf |
| rollout leaf + inclusive cut | 1.29x (s8008) | -- | byte-identical to the rollout leaf |

Under the inclusive cut, a subtree beneath an estimated incumbent at turn T asks for wins <= T; the O(1) leaf
answers with an estimated win at T, stored as a WIN on the ORDER-EXACT key, so every zone-order twin (the same lands
in another order) re-searches it. Under the strict cut the subtree asks for wins < T and returns a bounded no-win on
the canonical key, shared by every twin -- what a rollout search already got. Same answer at every node, so a sound
collapse (USER 2026-10-01). At b20: byte-identical on staged / pure value / both no-leaf shapes at 0.995x units; the
d7 trusted arm moves (-0.004 at 1.08x).

### 4. Not the cause: clairvoyance

The value model reads no library order (its label averages K reshuffles) while the rollout plays the real draws.
`MTG_LEAF_BLIND_K=8` (diagnostic, default 0) replaces the rollout with the mean of 8 rollouts over reshuffled
libraries: unbudgeted d5, seed 8008's 100 games are **byte-identical** to the real-draw rollout and seed 9009 has
the same average. Win turns are integers, so nearly every leaf reads "turn 6" either way.

### 5. Leafless probes have no incumbent

The leafless d5 search alone (no rollout, no model) blew up to 2.5-9.7M units in single games, against ~100k for the
rollout search: no B&B prune fires before an in-window win is found (the emulated ladder's counters show 0 prunes per
leafless or value warm-up pass, 268-283 per rollout pass at d4). On Landfall a d5 window from turns 1-2 rarely holds a
win (average 5.7), so the probe pays for nothing exactly where the budget binds, and the single FIT pass is left the
scraps (509 FIT passes averaging 667 units). The strict probe gate (`value_play.alpha "strict"`) helps a lot but not
enough: nlfit 0.97x +0.053, nlesc 1.33x +0.021; a reserved single pass is cheap but no better (0.62x +0.074).

### 6. FIXED: the emulated ladder never left calibration; it now beats live's curve

`value_play.ladder "emulated"` (USER design 2026-09-09: warm-ups on the cheap leaf, the committing pass on the
rollout at the depth the heuristic ladder would commit) cost **1.11x live at live's play** (1 game moved in 2,000).
Its R calibration runs both leaves at a depth until that depth has 2 samples IN THIS GAME; Landfall has ~2.8 searched
decisions a game, so 1,873 calibration passes in 842 decisions -- the heuristic ladder plus value warm-ups on top.
Two fixes:

* **`value_play.emul_r`** -- a frozen per-deck R by pass depth (measured once on disjoint seeds). Present => the
  calibration never runs; a constant is as deterministic as the per-game R it replaces.
* **No escalation after an emulated heuristic commit** (`g_fsl_emul_heuristic_commit`): the hybrid read the
  committed line like a value line and escalated it to the heuristic -- the same leaf and ladder again. (A plain
  sidecar now reproduces the per-job-suppressed arm byte-for-byte.)

| arm (strict cut on) | A/B seeds | HELD-OUT |
|---|---|---|
| emulated, value warm-ups, R = [11.3, 6.6, 4.6, 2.6], b20 | 0.998x **-0.0065** (16/3, t -3.0) | 1.027x **-0.0105** (23/2, t -4.2) |
| same shape, R measured on value warm-ups [11.4, 6.9, 12.4, 45.9], b20 / b25 / b30 | -- | 1.066x -0.010 / 1.21x -0.0265 / 1.33x -0.035 |
| emulated, leafless warm-ups, b20 / b30 | 0.997x -0.003 | 1.022x -0.005 / 1.26x -0.028 |
| live b20 / b25 / b30 | 1.00 / -- / 1.22x -0.023 | 1.00 / 1.12x -0.0175 / 1.23x -0.025 |

Held-out, live's curve at 1.027x is about -0.004, so the value-warm-up emulated ladder is ~0.006 turns ahead at equal
cost, on two independent seed sets -- the first alternative that beats the default on this deck. The edge is small
because warm-ups are only 22% of live's ladder; 28% of decisions still mispredict the committing depth (the frozen R
cannot reproduce the heuristic tree exactly, the two leaves prune differently).

**But not in instructions.** Callgrind, the same 12 games per arm (s840000, b20): live 99.2k Ir per unit, emulated
(value warm-ups) 106.4k (1.07x), emulated (leafless warm-ups) 106.0k, staged 109.8k (1.11x). The per-function diff
(`logs/tgt/ir/fdiff.py`) finds no unmetered sink -- the model's feature extraction + score is 0.3% of instructions --
the gap is that a unit of TREE work (state keys, enumeration, activation keys) costs more instructions than a unit of
ROLLOUT work, and the emulated ladder trades rollouts for tree. At ~1.10x real cost it is level with live, not ahead.
(Ir per unit also swings by game: one 202k-unit game took a 24-game live sample to 144k Ir/unit.)

### 7. Move ORDER: `MTG_ID_ORDER_HINT` (DEFAULT ON below the pass root) -- and the root tie-break (USER's call)

`MTG_ORDER_ORACLE=1` (diagnostic) replays every completed ladder pass from snapshots of its TT and budget on fresh
memos: as recorded (reproduces the pass exactly: 0 unit and 0 win-turn mismatches in 2,215 passes), best-first by the
recording (the oracle), and in the static order with no hint. Landfall b20, 200 games: the oracle order costs
**0.888x** the units at the same win turn and root plan in every pass, although only 2,907 of 99,476 nodes (3%) were
mis-ordered -- MoveOrderPlans is usually right, and where it is wrong the subtrees are big.

**The hint.** The ladder searches the same positions again one pass deeper, so each completed FSLineWin node records
which plan proved best (its static index, the plan count as a guard, and its win turn) and the same position one pass
deeper -- the node's key at depth - 1, two Folds away now that `BuildSimKey` folds the depth LAST (byte-identical:
every key's value moved, no key's equality) -- tries it first. One map per decision, capped (`MTG_ID_HINT_CAP`).
Every hinted pass returns the static order's win turn (0 mismatches in 2,203 live and 2,369 emulated passes).

**But at a node the saving IS a tie-break.** Under the strict cut the plan tried first wins every tie, and the plans
the static order puts ahead of the hinted one mostly TIE it: a strict cut refutes a tie for free, and proving one
costs the inclusive search the static order already paid. `MTG_ID_HINT_STATIC_TIE=1` keeps the static choice (the
plans moved behind the hinted one are searched with the INCLUSIVE cut and take a tie; an early end the hinted plan
earns is held until they have been tried): **0 root-plan differences in ~4,900 replayed passes (Landfall, Melira,
Melira 2HG, Dragonstorm) -- and 0.99-1.006x the static order's units.** Kept to record that; default OFF.

**So the default runs it BELOW THE PASS ROOT only** (`MTG_ID_HINT_ROOT=0`). A tie under the root picks only which line
a node returns, never its win turn, so the root -- the committed decision -- commits the static order's plan, and play
moves only through the budget it frees (and through which of several equally VERIFIED winning lines the executor
then follows: digests move, outcomes do not). Landfall b20, 200 games, exact replays of 2,211 passes: 0 root-plan and
0 win-turn differences, **0.928x the pass units** (0.947x the game). Held-out, all 35 shipping decks at their own play
settings (seeds 880000/881000, 400 paired games; 200 for the six heaviest; `logs/tgt/oo/int/`): **0 games moved**.
Landfall's held-out curve (seeds 860000-867000, 4,000 paired games per arm, vs live b20 without the hint):

| arm | units | win turn | faster / slower |
|---|---|---|---|
| live b25, no hint | 1.122x | -0.015 | -- |
| live + interior hint b20 / b25 / b30 | 0.937x / 1.049x / 1.154x | -0.0015 / -0.0155 / -0.024 | 8/2, 65/3, 99/3 |
| emulated + interior hint b15 / b20 / b25 | 0.806x / 0.954x / 1.061x | +0.007 / -0.0095 / -0.0235 | 21/49, 47/9, 95/1 |

The interior hint makes budget cheaper (b25's -0.015 now costs 1.05x, not 1.12x) without changing what a pass commits.
With it, the emulated ladder is ~0.006 turns ahead of live at equal units -- level at equal instructions.

**Why the hint stays off the root by default: Bruna.** With the root hinted, Bruna s880019 played NOTHING on turn 1
(Forest + Avacyn's Pilgrim without it) and won a turn later; s880084 lost two. Pass 3's best at the root was the empty
turn because a greedy rollout found a T4 win from it; pass 4 refuted that and read T5 for both plans, and the hint
handed the tie to the refuted plan.

**The root tie-break is the USER's call, and it is where the big gains are.** With the root hinted (`=1`), the tie at
the committed decision goes to the plan that was best one pass shallower -- and in the emulated ladder that shallower
pass runs on the learned model, so the MODEL breaks the ties the rollout cannot separate. Landfall held-out
(`logs/tgt/oo/ab2.*`, same seeds/arms as above):

| arm (root hinted) | units | win turn | faster / slower |
|---|---|---|---|
| live b20 / b25 / b30 | 0.840x / 0.932x / 1.018x | **-0.0250** / -0.0343 / -0.0405 | 134/34, 166/29, 186/24 |
| emulated b15 / b20 / b25 | 0.738x / 0.869x / 0.964x | -0.0375 / **-0.0450** / **-0.0535** | 207/57, 218/38, 236/22 |

In instructions (callgrind, 200 held-out games, `logs/tgt/oo/cg3/`): live + root hint b20 **0.785x** live's, b25
0.897x, emulated + root hint b20 0.945x (the emulated ladder pays ~9% more instructions per unit -- tree is
under-priced against rollout; the model's features + score are 0.4%), so at equal instructions the emulated ladder
with the root hint leads live + root hint by ~0.007-0.013 turns and the old default by ~0.04. All decks with the
root hinted (seeds 880000/881000, 26 decks completed before two VM reboots, `build/scratch/lf-target/alldeck/`):
Landfall -0.030 (8/2), Melira -0.025 (5/0), Kitty -0.0175 (6/0), FiveColour -0.005 (3/1), Creature Giving -0.0025,
Niv-Mizzet -0.005 (partial), Hinata 1/1, **Bruna +0.015 (0/2)**, the rest 0/0; pooled -0.0020 (se 0.0006).

**The amended root rule, `MTG_ID_HINT_ROOT=2` ("stable"):** the hinted plan keeps the root tie only while its win turn
HELD at this depth; if it got worse than one pass shallower (Bruna's T4 -> T5), the root keeps the static tie-break
(the static-tie machinery, armed for that node). Bruna s880019 / s880084: back to T5 / T6.
Held-out, all 35 decks (`logs/tgt/oo/stab/`; 12,800 paired games, pooled -0.0008, se 0.0003): Landfall -0.030
(8/2), Melira -0.020 (4/0), Niv-Mizzet -0.0025 (1/0), **Bruna 0/0**, Kitty 0/0 (its always-root gain is lost), every
other deck 0/0 -- and **FiveColour +0.0025 (0/1)**: s881103 T1, Steam Vents (static) and
Windswept Heath (hinted, T5 at passes 3 and 4) tie on an ESTIMATED T5 -- a rollout past the horizon -- and the Heath
line is refuted the next turn (T6) while the Steam Vents line wins T5. So neither root rule meets the every-deck bar,
for the same reason: between plans tied on an ESTIMATED win turn, any tie-break is a guess, and the static order is
just another guess. The principled form would break root ties on a finer estimate than the integer win turn -- the
learned model's milliturns, which is presumably why the emulated ladder's root tie-break is the strongest arm.
And the stable test does not transfer to the emulated ladder: there the hint's recorded win turn is the MODEL's (a
value warm-up) and the committing pass's is the rollout's, so "got worse" fires on the scale difference. Landfall
held-out: emulated + stable root b15 / b20 / b25 = 0.741x +0.0025 / 0.865x -0.0120 / 0.957x -0.0245, against
-0.0375 / -0.0450 / -0.0535 with the root always hinted. (Live + stable root b20 is identical to live + root b20.)

**`MTG_ID_HINT_ROOT=3` ("model"): the model breaks the rollout's ties, and nothing else does.** A rollout pass's root
takes the hint only when a LEARNED-MODEL pass recorded it (`IdHint.model`); a rollout-recorded hint -- Bruna's and
FiveColour's failures -- and every deck without a model keep the static root. All 35 decks held-out at play settings
(`logs/tgt/oo/mdl/`, 12,800 paired games): **0 games moved** -- the shipped hybrids commit their value probe's own line
almost always, so a model hint rarely meets a rollout root. Landfall's default reproduces the interior-only arm
exactly (no model). Landfall's emulated ladder, held-out (4,000 paired games per arm, vs live b20 without the hint):

| arm | units | win turn | faster / slower |
|---|---|---|---|
| live + interior hint b20 / b25 / b30 | 0.937x / 1.049x / 1.154x | -0.0015 / -0.0155 / -0.024 | 8/2, 65/3, 99/3 |
| **emulated + model root b15 / b20 / b25** | **0.785x / 0.929x / 1.040x** | **-0.0227 / -0.0355 / -0.0465** | 140/49, 161/19, 196/10 |

At equal units the emulated ladder is ~0.031-0.034 turns ahead of the default; its b15 matches the default's b30
at 32% fewer units. (Most of the always-root emulated gain, with half the slower games.)
**In instructions** (callgrind, the same 200 held-out games as above, `logs/tgt/oo/cg4/`, vs live b20 without the
hint): live + interior hint b20 **0.898x**, b25 **1.022x**; **emulated + model root b20 0.996x** (108.3k Ir per unit
against live's 102k). So at 0.975x the instructions of live + interior hint b25, the emulated ladder with the model's
root tie-break is **0.020 turns faster** (-0.0355 vs -0.0155); at 1.108x live + interior hint b20 it is 0.034 faster.
The default no longer wins at equal real cost -- the learned-leaf alternative does.

**The other shapes with the root hint** (2,000 paired games, seeds 860000-863000, vs live b20; `logs/tgt/oo/alt.*`):
staged b10 1.05x -0.047, b15 1.30x -0.0525 (about level with live + root hint's curve, behind the emulated ladder);
leafless FIT (strict gate) b20 0.96x **+0.049**; leafless escalation b10 0.81x +0.050, b20 1.23x +0.004 -- still well
behind: a leafless tree has no incumbent to prune against, and the hint cannot give it one.

## Open USER decisions

1. **`MTG_ID_HINT_ROOT=3` as the default** (the learned model breaks the rollout's ties at the committed decision).
   It moved 0 of 12,800 games across all 35 shipped decks, so it changes nothing that ships today; it is what makes
   the emulated ladder the strongest shape on Landfall. The other root rules (`=1` always, `=2` stable) each fail the
   every-deck bar on one deck (Bruna; FiveColour) -- rollout-on-rollout tie-breaks between ESTIMATED ties.
2. **Landfall: adopt the emulated-ladder sidecar** (model + `value_play {ladder: "emulated", emul_r: [11.3, 6.6, 4.6,
   2.6]}`) together with (1): -0.0355 at b20 for 0.93x the default's units (the default with the interior hint:
   -0.0015 at 0.94x). Without (1) it is ~0.006 ahead in units and level in instructions. (emul_r was measured without
   the hint; re-measuring it is cheap.)
3. Phase E: judge every shape against a live budget CURVE (live at b x {0.75, 1.25, 1.5}) instead of one point,
   and add the emulated shape (with R measured in-pipeline) to its arms.
4. Leafless run-on (`exhaust_mult` 4) per deck: a ~20% cost trade for the small gains above.
