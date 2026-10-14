# Snow's heavy matrix games are the beyond-horizon LEAF, not the search — measured 2026-10-13

Self-contained. Written while the Snow value-leaf run (queue `logs/vlq_snow`, frozen at `1eb4ad7f`)
was STOPPED in phase C at the user's direction:

> *"If we are skipping that many games I think we may need to stop and optimize."*
> *"The cap is not intended to get a deck this slow through the value-leaf. It is intended to take out
> a few outliers."*

At the stop, 110 of ~600 game indices (18%) were on the skip list (abandoned at the 40M-unit
ceiling), projecting to ~290 of 1,600 — about twice the 10% skip cap. Almost all abandoned in the
**H4/H5** cells (43 at H4+H5 only, 21 at H5 only, 19 at H3–H5; one game, s11011 gi66, in every H cell
and V4–V8).

## 1. Where the units go

An H cell is the HEURISTIC reference: `value_model` off, budget 0, the ladder's warm-up passes on the
value leaf, and the committed pass scored by the rollout leaf. That leaf is `SimulateToEnd` at
`EffectiveFdLeafDepth()` — by default **1**, a 1-ply lookahead on EVERY simulated turn of EVERY leaf
(`TurnSolver.cpp`, `s_fd_leaf_depth`). Each of those plies enumerates the turn's candidates and, on
Snow, opens the tap-draw breakpoints (site 8) inside it; every candidate then rolls out greedily.

Same binary, same games, one pooled batch, per-game units (`MTG_DUMP_UNITS=1`), H-cell fields copied
from the run's own manifest. `leaf0` = the deck profile with `search_leaf_depth: 0` (a greedy
playout beyond the horizon; Melira Pod ships this), siblings symlinked so the keep table and sidecar
are the shipped ones.

| game (cell seed, gi) | cell | 1-ply leaf (units) | greedy leaf (units) | ratio | win turn |
|---|---|---|---|---|---|
| 8008, 107 | H3 | 6,190,266 | 13,421 | 461x | 6 / 6 |
| 8008, 32 | H3 | 15,122,689 | 229,040 | 66x | 6 / 6 |
| 10010, 4 | H3 | 8,262,732 | 87,870 | 94x | 6 / 6 |
| 11011, 13 | H3 | 2,833,833 | 61,384 | 46x | 7 / 7 |
| 9009, 41 | H3 | 35,239,416 | 699,028 | 50x | 7 / 7 |

Every one of these abandoned at 40M in H4 and H5 under the 1-ply leaf. Under the greedy leaf, H4/H5
on the same games (s8008 gi107/gi32, s9009 gi41/gi5, s10010 gi4/gi37, s11011 gi13/gi43):
**0.11M–14.3M units, all inside the ceiling** (worst: s10010 gi37 H5 14.28M, s9009 gi41 H5 12.99M).

Ordinary games, paired, H3 (4 seeds x 25, 10M ceiling): **83 paired, 0 better / 0 worse**, units
133.2M -> 3.77M (**0.028x**).

V cells do not move: V4, 25 games, byte-identical digests AND identical units — the value leaf
replaces the rollout, so the rollout's fidelity is never read there.

## 2. Why it is NOT a profile change

The same `search_leaf_depth: 0` in SHIPPED (budgeted) play is worse, on held-out seeds 830000+1000·i:

| cell | paired | Δ turns/game | better / worse | units ratio |
|---|---|---|---|---|
| d3 / b10 | 480 | +0.0208 | 7 / 17 | 0.38 |
| d5 / b20 | 240 | +0.0208 | 2 / 7 | 0.47 |

A budgeted search finishes its configured depth early on the cheap leaf and leaves budget unspent,
while each leaf is a weaker estimate. `search_leaf_first_turn_depth: 1` (one searched ply at the
horizon, greedy after) was worse still (+0.042 / +0.033) AND 1.3x the cost, so it is dead.

## 2b. But NOT at shallow depth -- the reason the override carries a threshold

Paired ordinary games, greedy leaf vs the 1-ply one, same pooled batch:

| cell | paired | better / worse | Δ turns/game | units ratio |
|---|---|---|---|---|
| H1 | 50 | 0 / 11 | +0.22 | 0.052 |
| H2 | 25 | 0 / 2 | +0.08 | 0.034 |
| H3 | 83 | 0 / 0 | 0 | 0.028 |

A shallow search leans on its leaf; a deep one barely reads it (at d>=3 from turn 3 onward the window
already spans most of an 8-turn goldfish, and the lazy leaf proves many decisions leafless). So a
blanket matrix override would WEAKEN the H1/H2 reference, and a weaker H makes the value leaf look
better than it is -- the bias runs toward adopting the leaf, the same direction as the lazy-leaf H5
finding. H1/H2 are cheap at the 1-ply leaf anyway (~0.1M / ~0.4M units per ordinary game), so there
is nothing to buy there.

## 3. The matrix-only override, and the route bug it exposed

`scripts/attic/valueleaf_depth_matrix.py` already had the right knob — `MTG_MATRIX_FD_LEAF_DEPTH`,
a per-RUN override scoped to the matrix (phase A's label games and phase E never see it), used by
EDF on 2026-09-09. **It was a no-op on the pooled route**: only `run_batch` (the retired per-cell
path) translated it. Rebuilt on branch `snow-opt` (`4a60ec66`) as a PER-JOB override with a depth
threshold: `MTG_MATRIX_FD_LEAF_DEPTH=0 MTG_MATRIX_FD_LEAF_FROM=3` makes H cells of depth >= 3 read a
variant profile carrying `search_leaf_depth: 0` (siblings symlinked, valueleaf.sh's
`make_variant_deck` pattern) -- one pool cannot vary an env var per cell. H1/H2, V cells, phase A and
phase E are untouched; an exported `MTG_FD_LEAF_DEPTH` is refused (it would outrank every cell's
profile). The setting is stamped beside the state (`<out>.fdleaf`) and a resume across a change
REFUSES, because neither the src nor the smoke-digest play fingerprint can see it. Unset, the
manifest is byte-identical to the previous driver's (checked with a stub engine).

## 4. What is the user's decision

The H cells are the reference every V cell is judged against, and H5 is the escalation cap. Changing
their leaf is therefore a methodology choice, not an optimization to slip in:

* **Evidence it is harmless to the reference:** H3 0/0 over 83 paired ordinary games, every heavy game
  the same win turn, V cells untouched. (H1/H2 in §2b, H4/H5 in §5.)
* **Evidence it is not free elsewhere:** shipped budgeted play loses 0.02 t/game, so it must stay a
  MATRIX setting, never a profile one.
* **Cost of using it:** the banked phase-C chunks (~5,900 games at the 1-ply leaf) cannot be mixed in;
  phase C restarts fresh. Phases A (rows) and B (model) are unaffected and are kept.

## 5. The deep cells, paired on ordinary games (seeds 601000-604000, 631000-633000; 10M ceiling)

| cell | paired | better / worse | digests moved | units ratio |
|---|---|---|---|---|
| H3 | 83 | 0 / 0 | 6 | 0.028 |
| H5 | 41 | 0 / 0 | **0** | 0.070 |
| H4 | 62 | 0 / 0 | 3 | 0.049 |

So at depth >= 3 the greedy leaf gives the SAME win turn on every paired game measured (186 of 186),
and at H5 the same game on every one. The one game that still abandons under it is s11011 gi66 (H4
and H5 both at 40M) -- the kind of outlier the ceiling exists for.

## 6. The 1-ply leaf cannot be made cheap without paying the same quality (branch `snow-rollout`)

A separate investigation (docs/design/snow-leaf-lookahead-cost.md on branch `snow-rollout`) found
each simulated turn's 1-ply lookahead scores ~116 candidates on Snow (~45 base plans + ~70 tap-draw
breakpoint variants), each with a greedy rollout. Two leaf-only, default-OFF levers
(`MTG_LEAF_NO_BP`, `MTG_LEAF_CAND_CAP` with K) cut the heavy games 9-38x, but on 75 paired H2 games
every arm was worse than base (+0.027 .. +0.067 t/game, 0 better), approaching leaf0's own +0.067 as
the cut deepens. No lossless lever was found. That is the evidence for keeping H1/H2 on the 1-ply
leaf and changing only H>=3, where the leaf measurably does not decide anything.

## 7. In-window cuts are percent-sized next to this (branch `snow-window`)

`MTG_SNOW_LOOK_CAP1` (at most one look per card name offered per plan list; a hit still opens the
breakpoint and the re-plan re-offers the untapped copies) is the best of three default-OFF provider
restrictions: 0.876x units over five heavy H3 games (0.21x on gi107, **1.000x on gi41**, the heaviest)
and 70/70 paired H2 games at the same win turn. It completes s10010 gi37 at H3 (36M) where base
abandoned. Real, but it does not move the tail that drove the stop -- the leaf does. Full record:
docs/design/snow-look-cap-and-defer-levers.md on that branch.

## 8. (superseded) The matrix-only override

The first route was a per-run env override (`MTG_MATRIX_FD_LEAF_DEPTH=0 MTG_MATRIX_FD_LEAF_FROM=3`)
applied to every deck in the run. Superseded by the per-deck calibration in section 10 and removed from
the pooled driver; kept here only so the history reads.

## 9. A GENERAL rule instead of a matrix override (USER 2026-10-14)

> *"My recommendation is to figure out whether we can come up with a generally adoptable rule like this."*

*(The per-job heurarm flags named here were later replaced by one numeric job field; section 10.)*

**The rule: leaf fidelity by PASS depth.** A commit-the-line pass of nominal depth >= K scores its
horizon with a greedy playout; shallower passes keep the 1-ply leaf. The pass depth is set by the
root FSLineWin node and read by `EffectiveFdLeafDepth` (below an explicit `MTG_FD_LEAF_DEPTH` and a
deck's own `search_leaf_depth`). Per-job flags `MTG_LEAF_GREEDY_D2/_D3/_D4` (+ `_D3_UNL`, the
depth-3 rule only in an UNLIMITED-budget pass; built, measured dead, deleted -- see below). Flags off is
byte-identical.

**Shipped (budgeted) play, every suite deck** -- smoke-tier cell shapes at depth >= 3 on held-out
seeds (+900000), plus Snow's 720 extra d3/b10 + d5/b20 games, one pooled batch, paired
(`test/leaf_greedy_ab.sh`, run `fleet1`):

| arm | paired games | win turns moved | better / worse |
|---|---|---|---|
| K=3 | 11,030 | 39 | 1 / 38 |
| **K=4** | 11,030 | **0** (0 digests) | 0 / 0 |

K=3 is NOT adoptable fleet-wide under a budget, and the mechanism is specific: at d3/b10 the 1-ply leaf
often cannot finish the depth-3 pass, so play commits the depth-2 pass; the cheap leaf lets the depth-3
pass finish and its greedy-scored commit replaces the depth-2 one scored by the 1-ply leaf -- and the
1-ply leaf wins that trade. **Snow is the exception: K=3 moved 0 of its ~1,140 budgeted games.**

**Unbudgeted H cells, every suite deck** (run `hcells`, 15 games x deck x cell, 10M ceiling; the arm
is greedy from depth 3, so at H4/H5 it IS the K=4 / K=5 rule). Every moved game is WORSE:

| cell | paired | moved (all worse) | decks |
|---|---|---|---|
| H3 | ~490 | 9 | Creature Giving, FiveColour, Hinata2 x2, Kitty, Minotaur, Mirrorwing, Prevent Damage, Treasure Hunt |
| H4 | 470 | 3 | Hinata2 x2 (T1 Ponder), Treasure Hunt (T2 dig before land) |
| H5 | 472 | 0 | -- |

The leaf's influence decays with depth (1.8% -> 0.6% -> 0 of games), which is the premise of the rule;
but at H3/H4 the combo decks still lean on it. The H4 losses are early-turn decisions whose window ends
before the kill, so a leaf carrying 3-4 turns decides the tie and the greedy playout misses the combo
the 1-ply leaf finds.

**Two deck-agnostic refinements, both DEAD (built, measured, deleted):**

* *Bind only SHORT leaves* (covering <= 2 turns before max_turns). Recovers every combo game (0 moved
  of 382 unbudgeted, 0 of 3,670 budgeted), but gives back Snow's savings: Snow's ordinary H5 games
  abandon at 10M exactly as at base (gi0/3/11), gi6 9.6M -> 1.34M instead of 0.17M, gi4 1.34M =
  base. Snow's cost sits in the SAME long early-turn leaves the combo decks need.
* *Key on leaf width.* Snow's leaf lookahead averages 77-96 candidates per turn (mostly 64-127),
  Hinata2's 3.7, but Treasure Hunt's losing H4 game has 2,355 leaf turns in the 64-127 band too
  (`MTG_LEAF_STATS`, branch `snow-rollout`). No threshold separates them.

## 10. The rule as a per-deck MEASUREMENT in the value-leaf pipeline (USER 2026-10-14)

> *"Can we apply this more generally rather than as another flag?"* -- then, of the three options
> offered (one engine default at K=4; the pipeline measures K per deck; a lossless cheaper 1-ply leaf):
> *"let's try option 2."*

A per-deck profile key was built first (`search_leaf_greedy_from_depth`) and withdrawn in favour of
this: no hand-set setting, the pipeline decides each deck's threshold from data every time it runs.

**Phase C0, leaf calibration** (`scripts/valueleaf.sh`, between B and C; `run_leafcal` in the matrix
driver). For every deck in the queue, one pooled batch plays each H cell of depth >= 3 twice on the same
50 held-out games (seed 12012) -- 1-ply leaf and greedy leaf -- using the matrix's own H-cell job shape
(one shared helper, `_h_arm_fields`). K = the shallowest depth such that every cell at or above it moved
no win turn over >= 30 completed pairs; none = every H cell keeps the 1-ply leaf. The verdict goes to
`<queue>/leafcal.json` and `DECISIONS.txt`, and `status` prints it. The matrix then sets the per-job
field `leaf_greedy_from_depth: K` on that deck's H cells of depth >= K.

* **Engine:** one per-job numeric arm (`valuearm::Arm::leaf_greedy_from`, manifest
  `leaf_greedy_from_depth`), read by `LeafGreedyFromDepth`. Nothing else sets it: no env, no profile
  key, no default. Unset is byte-identical (smoke + regression all PASS).
* **Why only H >= 3, and why only the matrix:** H1/H2 are scored by their leaf (Snow H1 0 better / 11
  worse of 50) and cheap; under a budget the rule is a play lever, not a cost one (K=3 moved 1 better /
  38 worse of 11,030 budgeted fleet games), and shipped play is budgeted.
* **Ceiling:** the calibration's games run under a 10M-unit ceiling so the 1-ply arm's heavy games cost
  minutes; games either arm abandons are unpaired. Same population restriction the matrix's skip list
  makes. Snow's heavy games were identical in win turn under the greedy leaf anyway (section 1).
* **Power:** 50 games detect a per-game loss rate of ~6% at 95%. The fleet's measured rates were 7-13%
  per affected deck and cell (Hinata2 H4 2 of 15), so the decks known to need the 1-ply leaf are caught;
  a rarer loss can pass.
* **Resume:** the matrix stamps the thresholds (`<matrix>.fdleaf`) and refuses to resume a table banked
  under different ones. A queue that predates C0 (Snow's) runs it on the next `run`; its banked matrix
  must be moved aside first.
* **Phase A is unaffected:** rows never see the job field; and with a greedy leaf in the label search
  anyway, 24 Snow games gave 139 rows identical as sets.

**Measured for Snow (first real run, 2026-10-14, 22 min wall / ~8 core-h, nearly all of it the 1-ply
arm's games hitting the 10M ceiling): K=4.** H5 0 of 36 moved (0.08x units), H4 0 of 33 (0.04x), H3 **2
of 40 worse** (games 25 and 39 at seed 12012, 5->6 and 6->7, both reproduced in solo replays). The
earlier hand reads (H3 0 of 98) were themselves underpowered. So Snow's matrix keeps the 1-ply leaf at
H3 and the H3 heavy games: the skip list is projected at ~43 of ~600 (~7%) rather than ~21.

**What 50 games can and cannot certify.** 0 moved in n pairs still admits a per-game loss rate up to
1 - 0.05^(1/n): ~9% at n=33, 2% at ~150, 0.2% at ~1,500. The matrix derives the trust candidate at
tol = 0.002 turns against the best H cell -- exactly the H4/H5 cells the greedy leaf changes -- so
certifying the rule at the derivation's own precision needs ~1,500 pairs per cell, i.e. playing the
1-ply cells the rule exists to skip. The screen catches gross damage (Snow H3 at 5%, combo decks at
7-13%); it cannot certify a 1% effect.

**So the FINAL product is A/B'd with the rule on and off** (USER: *"we should still A/B test the final
result with it off and on"*, *"separately from the rest of the leaf"*, *"as an additional test if it was
adopted"*). The rule never touches the model; it can only move the settings phase D derives from the H
rows (fallback crossover, no_fallback, trust candidate). After phase D, for every deck with a K,
`leafrule_counterfactual` re-derives those settings with each greedy H row shifted DOWN by the worst
case C0's own pairs allow (`--h-shift`, `--write-to` in the deriver). Same settings -> ON and OFF are the
same product, recorded, nothing played. Different -> phase E adds ONE arm, `leafOFF`, on the A/B seeds,
paired against the staged arm (which is the ON product): same model, only those settings differ, at
PLAY settings -- which is the right yardstick, since a setting play's budget never reaches cannot cost
anything (USER: *"if a setting is fully unreachable due to cost it also will not come up so much because
the budget will cut it off"*). ON ships unless it is notably worse than OFF (delta > 0.002 at paired
t > 2), in which case OFF's settings -- which lean toward falling back to the heuristic, the safe side --
replace them in the staged model. Every outcome goes to `DECISIONS.txt`. Tested on Prevent Damage's
completed matrix: the worst-case shift (H4 -0.087, H5 -0.080) moves its crossover at committed depths 7
and 8 (take at H5 -> H4); a negligible shift moves nothing.

**Expected for Snow** (from sections 1, 5 and 9; superseded by the measurement above at H3): K=3; ordinary H3-H5 games 14-36x fewer units, heavy
games 46-460x; skip list ~21 of ~600 instead of 110 (the rest are V-cell or H1/H2 abandons no leaf
reaches); the matrix phase on the order of an hour instead of the ~390 core-h the stopped run projected,
plus the calibration's own ~3-4 core-h.

**The profile guard stays:** `scripts/valueleaf.sh` stamps the profiles' `search_leaf_*` keys
(`<queue>/freeze.leaf`, backfilled from `freeze.commit` for older queues) and hard-stops on a change,
because the budgeted play fingerprint cannot see a leaf change that only bites unbudgeted passes.

Section 8's matrix-only env override (`MTG_MATRIX_FD_LEAF_DEPTH` / `_FROM` with a variant profile) is
superseded by C0 and removed from the pooled driver.
