# Per-deck search shape (`value_play.ladder` / `leaf` / `commit`)

Standalone record of the 2026-09-09/10 shape work: what each shape is, how a deck selects one, what the
screen measured, and what is adopted. The per-session narrative is in `analysis-Melira Pod.md` (09i-10a).

## The shapes

A deck's value sidecar (`decks/<Deck>/<Deck>.value.json`) carries, under `value_play`, three keys that are
read whether or not the block is `enabled`:

| key | values | meaning |
|---|---|---|
| `ladder` | `escalation` (default) / `emulated` | the hybrid's value ladder with trust escalation, or the emulated-gate ladder |
| `leaf` | `model` (default) / `none` | the leaf on the ladder's passes: the learned model, or NO leaf (the search commits only proven in-horizon wins) |
| `commit` | `heuristic` (default) / `model` | the leaf of the pass that commits: the rollout, or the model (the model stays attached even with `leaf: none`) |

Combinations that exist and were measured (arm names from the screen):

| shape | ladder | leaf | commit | what happens |
|---|---|---|---|---|
| ship | escalation | model | - | value passes 1..D, trust escalation on an unverified line below the trust depth |
| escnl | escalation | none | - | leafless passes 1..D, proven wins banked, everything else escalates to the heuristic. Needs no model. |
| escnlv | escalation | none | model | as escnl, but the gate reserves one pass's cost and an unverified line at a trusted depth pays ONE value pass there (partial kept under the anytime rule) |
| emulnl | emulated | none | heuristic | leafless warm-ups, heuristic committing pass at the heuristic ladder's depth, verified warm-up wins committed directly. Needs no model. |
| emul | emulated | model | heuristic | model warm-ups, heuristic committing pass |
| emulnlv | emulated | none | model | leafless warm-ups, model committing pass at the value ladder's depth, then trust escalation |
| emulv | emulated | model | model | the value ladder emulated (fidelity control) |

"No sidecar" is no longer a state a deck should be in: a new deck ships `{ladder: escalation, leaf: none}`
from day one (no model needed), which removes the presence-gating traps (hybrid activation by file
existence; the H-cell ladder's 1.35-84.8x cliff on a missing model).

> **Why each cell reads what it reads: `search-shape-mechanisms.md`.** Four mechanisms account for the whole
> table (the rollout ladder trading depth for simulation; a model leaf widening the tree 1.1-1.8x at equal
> depth; how often the probe PROVES its win; and one asymmetry this work introduced). Read it before
> explaining any single row.

## THE MENU (SETTLED 2026-09-10, batches 3+4 on the fixed binary: 19 modelled decks + Fluctuator, both configurations, 8 x 500 games)

User requirement: *"It would be preferable to not have more than a few options for the leaf, so 3-4 should be
the maximum. Some of these approaches are likely to outperform others in general which means the dominated
ideas can be dropped."* Four candidates were measured -- escalation and final-depth, each with and without the
value leaf -- plus the full rollout ladder as the control. **Three survive.** The decision is reproducible:
`scripts/shape_menu.py` reads a screen's per-game logs and prints exactly the table below (per-arm detail
in the screen's own `decide2_batch4_all.txt`).

| # | shape | `value_play` | when |
|---|---|---|---|
| 1 | escalation, model leaf | `{ladder: escalation, leaf: model}` (the default; no keys needed) | any deck with a TRUSTED value leaf. Wins or ties on 17 of the 18 modelled decks. |
| 2 | escalation, leafless probe | `{ladder: escalation, leaf: none, alpha: relaxed}` | a deck with NO model, a rejected one, or day one of a new deck. Fluctuator: **0.23x / 0.45x the rollout ladder's units** with better quality (`shape_probe`, fresh seeds), and 0.43x / 0.79x the same shape without `alpha`. |
| 3 | final-depth single pass | `{ladder: single, leaf: none, alpha: relaxed}` | the low-budget dial: where the ladder's escalation is wasted. Dragonstorm 0.74x at d3b10 at neutral quality; Fluctuator 0.42x the rollout ladder at d3b10. |

**DROPPED -- the full rollout ladder (`heur`), on the evidence the user predicted.** *"Likely the full
heuristic ladder is such an idea, since skipping those early escalations should be pretty much free."* It is
dominated on all 19 decks at both configurations: 1.17x to 18.76x the shipped shape's units, and never better
on quality anywhere. It survives only as the control arm in a screen.

**DROPPED -- the value probe + final-depth pass (`fit_v`).** Adoptable on no deck at both configurations.
Wherever it looked good at d5b20 (FiveColour 0.91x with 58 net games better, z +6.8; Hinata z +2.5 at equal
cost) it lost at d3b10 (z -2.4, -3.8). Its leafless twin (#3 above) is the one that earns a place, so the
"value leaf on or off" pair collapses to ONE entry here -- which is the user's own prediction that the menu
might come to three.

### The one mechanism worth carrying forward: the single pass is a DIAL, not a free win

The final-depth shapes behave oppositely at the two budgets, and consistently across decks:

| | d5b20 (18,000 units) | d3b10 (9,000 units) |
|---|---|---|
| units vs ship | 0.80x - 1.8x (usually MORE) | 0.61x - 0.97x (always LESS) |
| quality | neutral to better (Hinata z +2.5, FiveColour z +6.8) | worse on 10 of 18 decks (z -2 to -5) |

The probe runs unreserved and eats the budget; the single rollout pass then runs at the deepest depth whose
predicted cost still FITS what is left. At a large budget that is deep enough and the shape is a real saving.
At a small budget "what is left" buys only a d1 or d2 rollout, where the ladder's escalation would have gone
deeper -- so it is cheaper precisely BECAUSE it is doing less, and the quality goes with it. The reserved
variant fails the mirror-image way: reserving room for the pass at every depth stops the PROBE early (Melira
+0.22 turns, finding 8). Neither is a free lunch; the single pass belongs on the menu as the option for decks
whose escalation is wasted, not as a general replacement.

### Two ways the screen's numbers are not the shipped deck's numbers

Both were found by cross-checking a screen row against a fresh measurement, and both leave the screen's
COMPARISONS intact (every arm in a batch shared the same binary and environment) while invalidating any
ABSOLUTE reading of a row. An ADOPTION must therefore be re-measured the way the deck actually runs.

**`MTG_NO_BP_PREFIX_CACHE=1` CHANGES PLAY -- it is not a pure memo under a budget.** Every screen batch ran
with it (the breakpoint prefix cache costs ~730 MB per game on Dragonstorm and was OOMing the box). Isolated
on Fluctuator, 500 games at d5b20: with the cache off the search spends **+2.9% units and returns a DIFFERENT
play digest** (`e390ddbd` vs `885342e9`); the three memo caps beside it (`MTG_TT_CAP`, `MTG_FSL_CAP`,
`MTG_FSL_POOL`) are byte-identical exactly as documented. The mechanism is the budget: a cache miss is not
free, it is recomputation, so under a FIXED unit budget the same decision truncates at a different point and
can commit a different line. "Result-neutral memo" holds only at an unlimited budget. The earlier reading
that the cache is play-neutral came from Dragonstorm alone and does not generalise.

**Finding 12 -- the screen's leafless arm is NOT the play a deck ships, AND IT CAN INVERT A VERDICT**

`value_profile: "noleaf"` (how every leafless arm in the screen was run) substitutes a constant stand-in and
**never loads the model file**, so the sidecar's escalation parameters -- trust depth, the fallback crossover,
`escalation_cap` / `escalation_r`, the beam -- are absent. A deck that SHIPS `leaf: "none"` keeps its sidecar
and those parameters stay live. The two are near-equivalent but not identical: on Fluctuator, 8 x 500 games,
the stand-in route costs 0.967x the shipped route and 5 games in 2,000 end differently. Small enough that the
menu's conclusions hold, but it means **an adoption must be re-measured on the sidecar route** -- which is
what Fluctuator's was (see its `value_play.note`), and what `chain_fix5.sh`'s route check exists to catch.

**On Creature Giving the two routes disagree about the SIGN.** Stand-in route (the screen): 0.92x units with
17 net games better, z +2.3 -- a clean win, flagged BETTER. Sidecar route (what the deck would ship): 0.752x
units but **14 better / 35 worse, z -3.0**, mean +0.0053 -- a rejection. The cause is visible in the deck's
own block: `escalation_cap` 5, `beam_width` 3, `escalation_r` 21, `escalation_fresh_frac` 0.5, every one of
them tuned FOR the model leaf. `leaf: "none"` keeps them live under a leafless probe they were not fitted to;
`value_profile: "noleaf"` silently discards them, and that is why the stand-in looked good. So a deck with a
TUNED value_play block cannot take shape #2 or #3 by reading a screen row -- the row is measuring a
configuration the deck cannot ship. Re-measure, or retune the block for the leafless probe.

**Consequence for reading the table below:** a `<== BETTER` row is a CANDIDATE, not a decision. Of the three
the screen flagged, Fluctuator's held on the sidecar route and was adopted, Dragonstorm's became a trade
(0.958x / z +0.9 at d5b20, 0.740x / z -1.3 at d3b10) and is staged, and Creature Giving's inverted and was
rejected.

**A LABEL to distrust, for the same reason.** A screen arm named `heur` means "the plain rollout ladder" only
if the job actually pins `value_model: false`. For Fluctuator -- the one deck in the screen with no trusted
model -- the generator emitted `heur` as an EMPTY job spec, so the deck's own sidecar drove it and that row is
Fluctuator's SHIPPED leafless escalation, not the rollout ladder. Every "vs heur" ratio for Fluctuator in
batches 1-4 is therefore against its shipped shape (which is why `escnl_rx` at 0.438x agrees to 1% with the
sidecar-route measurement of 0.433x). The true rollout-ladder comparison comes from `shape_probe.py`, whose
control pins every shape key explicitly: **0.23x at d5b20, 0.45x at d3b10.**

## What the screen measured (2026-09-10, 19 modelled decks + Fluctuator, d5b20 and d3b10, 8 x 500 games)

**SUPERSEDED IN PART -- read "Why the shapes measured what they did" below first.** The first screen ran on a
binary with two defects (the emulated model-commit ladder's escalation hijack; the constant-leaf ladder's
relaxed start gate), so its emulv / emulnlv / escnl / escnlv rows measure those defects, not the shapes. The
re-screen on the fixed binary is `logs/emul_screen/decide_fix.txt`. Fluctuator's and emulnl's rows stand.

Full table: `analysis-Melira Pod.md` 2026-09-10a. Summary as first read:

- **Fluctuator (no trusted model): escnl is a clean win on every axis** -- units 0.38x / 0.42x, wall 0.28x /
  0.37x (same-batch A/B, 12 workers), play -0.005 / -0.011 t at d5b20 / d3b10. ADOPTED 2026-09-10 (f714046c).
- **Modelled decks: ship stays.** escnl costs 1.0-1.15x ship at d3b10 (the model buys nothing at d3: every
  trust depth is >= 4) and 1.1-2.5x at d5b20 (up to 19x on the decks whose escalation is the cost).
- **emulv / emulnlv are broken at d3b10** (0.1-0.5x units, +0.01 to +0.57 t): the committing pass overruns
  at a 10 ms budget and the fallback leaves the decision with the warm-up line and no budget. Not adoptable
  until that fallback is fixed; at d5b20 they track ship (1.0-1.06x) and are nominally clean on Breaching
  and Burn (0.95x / 0.97x).
- **escnlv is never a units win** (1.0-1.1x at d3b10, 1.06-2.6x at d5b20) at ship's quality; its value is
  wall time only (skipped leaf evaluations are unmetered) -- see the CPU probe section.

## Adoption rule

Both axes, per configuration, against the deck's shipped shape: quality AND run time (deterministic search
units; wall only from a same-batch pair or the single-worker probe -- never across batches). A shape is
adopted only when it is no worse on either axis at every configuration the deck is played at (d5b20 and
d3b10 here -- but see the coverage caveat below: NEITHER is most decks' own configuration).

**WHAT THE TWO SCREEN CONFIGURATIONS ACTUALLY ARE (checked 2026-09-10, do not assume).** Every screen job
ran `--ignore-play-profile` at an explicit d5/20ms and d3/10ms, which OVERRIDES the depth and budget a
deck's own `value_play` locks. Against the committed sidecars:

| | decks |
|---|---|
| d5b20 IS their locked configuration | slivers, th, knights, antilife, dragonstorm, auras, creature_giving (7) |
| lock something the screen never ran | burn d6b20, fivecolour d6b20, stompy d6b20, hinata d5b30, goblins d6b40 (5) |
| lock nothing (`enabled: false`) | mirrorwing, minotaur, kitty, dragons, breaching, critter, melira, fluct (8) |

And **d3b10 is no deck's generation configuration**: the mulligan generators run at `mull_gen` d1b3, d2b1,
d2b3, d3b3 or d4b3 -- depth 1 to 4 at a 1-3 ms budget, never 10 ms. (That is what this sentence used to
claim, and it was wrong in the budget.) So d3b10 is a generic low-budget probe, not a stand-in for
generation, and for the 5 locked-elsewhere decks NEITHER column is a configuration they are played at.

Nothing adopted rests on this -- Fluctuator locks nothing and was re-measured at its real settings with no
flags -- but the two shapes this screen DROPPED were dropped at settings 5 of 20 decks do not use, and the
margins are what carry that conclusion (the rollout ladder loses by 1.17x-18.76x, not by a hair), not the
configuration coverage. A deck that locks d6 should be re-screened at d6 before its row is trusted.

**The quality axis is the PAIRED SIGN TEST, not the average win turn (2026-09-10).** Arms are compared on
identical games (same seed, same game index, same opening hand), so `better` and `worse` game counts are
paired and `z = net / sqrt(better + worse)` is a sign test on the discordant pairs. Use it as the decision
statistic; keep `d_avg` only as the magnitude (a shape that loses a fifth of a turn per game is rejected
whatever its z). **Why the change:** `d_avg`'s spread ACROSS SEEDS, measured on batch 2's completed cells at
500 games per seed, is

| deck | sd of d_avg per seed | se over 8 seeds |
|---|---|---|
| Melira | 0.033 | 0.011 |
| Hinata | 0.011 | 0.004 |
| FiveColour | 0.008 | 0.003 |
| Knights / Critter | 0.001 | 0.000 |

-- so on the heavy decks the standard error of `d_avg` over the whole 8 x 500-game screen is 8-22x the
0.0005 tolerance this rule was originally written with. A "+0.0010 worse" reading there is noise. The sign
test uses the pairing and resolves what the mean cannot: Melira's rejections come back at z = -8 to -27
(unambiguous), Hinata's leafless escalation at z = -3.1 (real), while the sub-0.002 readings that decorated
the light decks turn out to be |z| <= 1. It also sizes the CHEAP EARLY TEST for a new deck: a light deck
separates at one or two seeds, a heavy one needs the full eight.

## Leaf-usefulness predictor

`probeonly` (the leafless ladder with escalation off) gives the constant ladder's own units; `ceiling =
probeonly / escnl` is what a leaf trusted at every depth would leave of the no-leaf shape's cost. Compared
with the realized `ship / escnl` per deck below. A new deck's leaf decision is then one cheap no-leaf batch.

**Outcome: the experiment as constructed is INVALID, not merely negative.** `probeonly` (arm
`value_profile noleaf` + `value_min_depth 0`) plays IDENTICALLY to `escnl` and to `ship` (same average win
turn to two decimals on every deck checked: antilife 4.18, auras 4.12, knights 4.33, slivers 4.21, melira
4.80, burn 4.33) and costs 0.9-1.07x `escnl`. Switching the hybrid's trust escalation off does not leave the
decision with the leafless line: an unverified empty line is re-searched by the engine's own continuation
path, which is the heuristic again. So `probeonly / escnl` is ~1 by construction and says nothing.

The first reading of the realized `ship / escnl` (0.4-0.9 at d5b20) as "the value leaf's estimates give the
search an ORDERING and B&B cuts, so its tree is smaller" is REFUTED by the replays below: at an unexhausted
budget the leafless tree costs exactly what the model's does at every depth. The gap was the overrun waste of
the constant-leaf ladder under the relaxed alpha. A predictor still needs per-decision instrumentation
(verified fraction, escalation-unit share, exhausted-pass share) recorded per job; future work.

## CPU probe

Single worker, 150 games at d5b20, two reps each (`logs/emul_screen/run_cpu.out`), wall ms per job:

| deck | ship | emulv | emulnlv | escnlv | escnl |
|---|---|---|---|---|---|
| melira | 97,869 / 97,772 | 88,664 | 88,509 | 212,188 | 168,153 |
| knights | 748 / 728 | 752 / 747 | 637 / 648 | 590 / 595 | 750 / 748 |
| slivers | 759 / 689 | 759 / 720 | 604 / 614 | 619 / 633 | 600 / 605 |
| breaching | 4 / 2 | 4 / 5 | 1 / 1 | 1 / 1 | 9 / 9 |

**The value leaf's evaluation cost is negligible in wall time.** On Melira -- the one deck whose search is
heavy enough to time (64M units per 500 games; Breaching is 29k) -- leafless warm-ups save 0.2% over
model warm-ups (emulnlv vs emulv), and both are 9% under ship only because the emulated ladder commits
differently (a different digest, not a saving at equal play). The leafless ladders are far SLOWER on Melira
in wall exactly as in units (escnl +72%, escnlv +117%). On Knights the leafless shapes read 13-20% faster,
but the whole job is 0.7 s for 150 games -- nothing to bank. Breaching is unmeasurable (single-digit ms).

So the leaf's evaluation costs ~nothing in play; what it costs is its generation. The "emulv 9% under
ship" wall reading was the escalation hijack (no rollouts ran), not a saving. The no-leaf shapes are for
decks WITHOUT a trusted model (Fluctuator: 0.38x) and for day one of a new deck.

## Why the shapes measured what they did (2026-09-10; per-game replays, `analysis-Melira Pod.md` 2026-09-10c)

Method: `logs/emul_screen/gamediff.py <armA> <armB>` lists the games whose win turn or units diverge; each
was replayed single-process (`--seed <base+gi> --games 1`, `MTG_ROLLOUT_STATS=1 MTG_TRACE=search`) and
reproduced its batch result exactly; the ladder was then read pass by pass.

| shape | screen reading | mechanism | status |
|---|---|---|---|
| emulv / emulnlv | cheap but bad at d3b10; ~1.0x, slightly worse at d5b20 | **BUG: escalation hijacked.** The emulated block re-entered on the hybrid's escalation call and `run_pass` overwrote the forced rollout leaf, so the "escalation" replayed the same model passes; the rollout leaf never ran in any game (Dragonstorm 800676: T4 -> T8 at the same 600 units) | fixed: block skipped under `g_force_heuristic_leaf`; re-screened |
| escnl on modelled decks | 1.1-2.5x ship at d5b20 | **Overrun waste.** Leafless pass costs == model pass costs at every depth (Melira T1: 14/118/1236/7411/80679 both). The stand-in inherits the value ladder's alpha 8.8, so the deepest pass is admitted with est 44k vs 9.2k remaining; after exhaustion no no-win is memoised (truncation watermark) and every result of a constant leaf IS a no-win, so the pass runs to the 25x ceiling: 14 aborts = 80% of the game's units. The model's exhausted pass winds down (WIN entries keep the memo alive) | fixed (a): strict alpha 1.10 for a constant-leaf ladder (0 aborts, 1.83M vs ship 2.04M on that game); (b): a constant-leaf pass stops at exhaustion (edited, measured after (a)'s batch) |
| escnlv | never a units win | same waste as escnl, plus on every deck WITHOUT `value_trust_depth` (`escalate_below` = depth+1) the reserved value pass can never run: 30 decisions, 0 value passes on Melira | re-screened after (a); judge it on trust-depth decks |
| emulnl | 0.75x / +0.02 t on Melira; 3-19x on cheap decks | **It is the heuristic ladder** (16 of 4000 Melira games differ from heur): the replayed rollout gate sees 46x growth d2->d3 and commits the heuristic d2 line where ship's relaxed alpha admits a rated value d3 line. On cheap decks the rollout leaf (~145 units/leaf) is the whole cost | not a candidate: no advantage over heur |

Two facts that reshape the picture: (1) in deterministic units the tree does not depend on the leaf, so a
leafless pass can only save units by being SKIPPED (fewer passes) or by replacing rollouts; (2) the model's
benefit on a deck without a trust depth is the rated line of an exhausted deepest pass (kept by crossover),
plus the cancelled escalation on trust-depth decks.

## Determinism

The emulated ladder's learned R(k)/G(k) are per GAME (keyed on `game_seed`, prior = the deck's frozen
`escalation_r` else 120). Before 2026-09-09 they persisted across games on a batch worker, so a job's play
depended on thread history (caught by a re-run: different digest). Same-job-twice checks now match.

## Memory (and why screens ran at 12 of 32 workers until 2026-09-10)

Per-GAME peaks are small (Melira ship 50 MB, Melira escnl 131 MB, FiveColour ship 78 MB, FiveColour escnl
monster game 259 MB), but a single DECISION's memo can balloon: the TT comment records a ~6 GB antilife
escalation, the line-cache comment a ~28 GB Mirrorwing decision. A 12-worker batch sat at ~10 GB RSS with a
16.5 GB high-water mark and was killed by its own watchdog when MemAvailable fell to 1.46 GB (batch 1,
04:18). Both memos have RESULT-NEUTRAL caps that were OFF by default: `MTG_TT_CAP` (entries per table),
`MTG_FSL_CAP` (entries per decision cache) -- deterministic, a refused insert just recomputes -- and
`MTG_FSL_POOL` (a global KB bound across workers; schedule-dependent recompute, so a backstop only).
Batches now run at 32 workers with `MTG_TT_CAP=3000000 MTG_FSL_CAP=500000 MTG_FSL_POOL=10000000`
(`logs/emul_screen/launch_fix2.sh`), a pid watchdog (`memwatch.sh`, 2.5 GB floor) and a 30 s RSS trend
that names the in-flight games (`memtrend.sh`). Verified inert on normal games: identical units and play
for ship, escnl and nl_sres on Melira 802768 with and without the caps.

## THE ON-POLICY RE-SCREEN (2026-09-11): the 5 decks the menu measured at the wrong configuration

The coverage caveat above ("a deck that locks d6 should be re-screened at d6 before its row is trusted")
is now discharged. The five decks whose locked configuration the 2026-09-10 menu never ran were re-screened
**on-policy** -- no `--depth`, no `--budget-ms`, no `--ignore-play-profile`, so each deck's own `value_play`
drives -- with three arms in ONE pooled batch (480 jobs, 32 x 250 games per cell, fresh seeds 7,000,000+):

* `ship`   -- the deck exactly as committed.
* `escnl`  -- shape #2: `leaf: "none"` + relaxed alpha, **on the sidecar route** (the deck's tuned
  `escalation_cap` / `escalation_r` / `escalation_fresh_frac` / `beam_width` stay LIVE, which is what the
  deck would actually ship). NOT `value_profile: "noleaf"` -- see finding 12.
* `single` -- shape #3: the same leafless probe plus the FIT final-depth pass.

| deck | on-policy config | `escnl` quality | `escnl` units / wall | `single` quality | `single` units / wall |
|---|---|---|---|---|---|
| StompySurprise | d6b20 | 28/5, **z +4.00**, d_avg −0.0034 | **0.753x / 0.730x** | 25/16, z +1.41 | 1.010x / 0.939x |
| Goblins | d6b40 | 0/0, d_avg **0.0000** | **0.914x / 0.939x** | 0/0 | 1.290x / 1.525x |
| Hinata2 | d5b30 | 70/120, z −3.63 | 0.804x / 0.798x | 143/138, **z +0.30**, d_avg −0.0028 | **0.799x / 0.777x** |
| burn | d6b20 | 0/3, z −1.73 | 1.087x / 0.969x | 1/4, z −1.34 | 1.169x / 1.003x |
| FiveColour | d6b20 | 56/103, z −3.73 | 1.298x / 1.751x | 54/134, z −5.83 | 0.734x / 0.722x |

**Three candidates, and each is the shape the mechanisms doc predicts for that deck.** The predictor is
`search-shape-mechanisms.md` mechanism 3 -- *how often the probe PROVES its win* (single passes per ladder
decision at d5b20) -- and it calls all five rows correctly:

| probe-failure rate | decks | what wins | measured |
|---|---|---|---|
| LOW (leaf not load-bearing) | goblins 5/65, fluct 6/66 | **shape #2**: drop the leaf, nothing to replace | Goblins 0/0 at 0.914x; Fluctuator adopted 2026-09-10 |
| HIGH (escalation runs constantly) | hinata 77/136, fivecolour 311/646 | **shape #3**: one pass at the end beats escalating at every depth | Hinata2 0.799x at z +0.30 |
| HIGH **and** the leaf is load-bearing | fivecolour 311/646 | neither -- ship stays | escnl z −3.73, single z −5.83 |

So the menu's three shapes are not interchangeable options to screen blindly; which one fits is PREDICTABLE
from one counter, and the screen confirms the prediction rather than discovering it.

**StompySurprise is the sharpest result: its value leaf is a NET NEGATIVE.** Removing it is 25% cheaper on
both axes AND better on quality (z +4.00 on 33 discordant pairs). A model leaf that loses on quality is not a
tuning problem, it is a miscalibrated model actively misleading the search: with `leaf: "none"` only PROVEN
in-horizon wins are banked and everything else escalates to the heuristic, so the trust escalation can no
longer commit an unverified line the crossover table then keeps. This is the first deck measured where the
leaf costs quality as well as time.

**FiveColour is the control that proves the leaf can earn its keep** -- and it retires a scare number. Its
leafless arm costs 1.298x units / 1.751x wall here, NOT the ~49x once read off the `value_profile: "noleaf"`
route; that cliff was the stand-in discarding the deck's tuned cap/beam/R, not the leafless probe itself.

**burn rejects both shapes on the UNITS axis while looking fine on wall** (escnl 1.087x units but 0.969x
wall). Units is the deterministic axis and the decision axis; wall falls because skipped leaf evaluations are
unmetered. Do not read a wall-only improvement as a saving.

## THE FLEET LEAF-NECESSITY SCREEN (2026-09-11): is each deck's value leaf worth its cost?

One pooled batch, **1,222 jobs / 462,000 games**, fresh seeds 9,000,000+. For twelve modelled decks, `ship`
against `{leaf: "none", alpha: "relaxed"}` **at every configuration that deck is actually played at** (its own
locked config plus every `regression_cases.sh` tier), on the sidecar route via a `value_profile` override --
which shares the deck's real keep table between arms, so only the sidecar differs. 4,000 games per cell;
`d0b0` run as a 1,000-game DIGEST-EQUALITY proof rather than a statistic.

### Two structural facts that shrink the question

**At depth 0 the leaf cannot participate, and at depth 3 it does not matter.** Every `d0b0` cell came back
byte-identical on all twelve decks (greedy, no lookahead). And at d3 the leaf is inert or nearly so --
`auras_d3b80`, `breach_d3b10/d3b20`, `critter_d3b10/d3b20`, `gob_d3b20`, `knights_d3b20`, `thl_d3b80`
BYTE-IDENTICAL, the rest within 1-3% of 1.000x. Every trust depth is >= 4, so below that the model is never
consulted. **Consequence: a leaf change can only move the suite's d5 cases.**

### Per deck, at every configuration it is played at

| deck | verdict | quality (worst cell) | units | wall |
|---|---|---|---|---|
| StompySurprise | **ADOPTED** | +1.41 (d3b20), +3.90 d5b20, +3.74 own | 0.750x - 0.990x | 0.640x - 0.950x |
| treasure_hunt | **ADOPTED** | 0.00 (d3b10), +1.51 d5b20 | 0.866x - 0.991x | 0.772x - 0.983x |
| Dragonstorm | **ADOPTED** | 0.00 (d3b10), +1.29 d5b20 | 0.916x - 0.995x | 0.814x - 0.971x |
| Goblins | **ADOPTED** | −1.00 (d3b10, 1 game in 4,000) | 0.840x - 0.976x | 0.808x - 0.970x |
| slivers_vial | **STAGED -- a TRADE** | **+3.46** (12/0) at d3b10, 0/0 d5b20 | **1.023x** d3b10, 0.878x d5b20 | 0.674x d5b20 |
| Knights | rejected | neutral everywhere | **1.096x** d5b40 | 0.714x - 0.991x |
| Auras | rejected | **−2.00** d5b20 | 0.945x - 0.997x | |
| Anti-Lifegain | rejected | **−2.50** d5b20 | **1.212x** d5b20 | |
| Creature Giving | rejected | **−2.59** d5b20, −2.24 d5b40 | 0.747x | (finding 12, reproduced) |
| CritterLifegain | rejected | +1.73 | **2.069x - 3.298x** | 1.698x - 2.567x |
| BreachingDragonstorm | rejected | 0/0 | **2.947x - 4.019x** | **6.080x - 9.281x** |
| Hinata2 (shape #3) | rejected | **−4.33** d3b10 (+0.17 own) | 0.788x - 0.852x | 0.737x - 0.793x |
| burn / FiveColour | rejected | −1.73 / −3.73 | 1.087x / 1.298x | |
| Melira Pod | rejected (all shapes) | best −1.25 | 0.892x at **1.398x wall** | |

**Do NOT generalise "drop the leaf".** The spread is 0.75x to 4.02x units on the SAME one-line change.
BreachingDragonstorm leafless costs 9.3x wall; Creature Giving buys 0.747x units for z −2.59. Per-deck
measurement at per-deck configurations is the only thing that separates these.

### Hinata2 is the cautionary row, and it vindicates an existing warning

Shape #3 measured **+0.17 at Hinata2's own d5b30 for 0.788x units / 0.737x wall** -- a 21% saving at dead-
neutral quality, confirmed on two disjoint seed blocks (+0.30 on 7,000,000+, +0.17 on 9,000,000+). Adopting on
that row alone would have shipped a **z −4.33 regression at d3b10** (18 better / 55 worse), which is a tier
the suite runs. That is exactly what "the single pass is a DIAL, not a free win" already predicted for a small
budget, and it is the whole reason the adoption rule says *every configuration the deck is played at*.

This leaves an open DESIGN question (not actioned): the shape keys apply unconditionally at sidecar-load,
while the cap and `escalation_fresh_frac` are gated on-policy (`vp_here`). If shape were likewise gated,
Hinata2 could take its 21% saving and the d3 tiers would be untouched. It cannot simply reuse `vp_here`,
because that requires `value_play.drives()` and Fluctuator -- the deck whose adopted shape this would
protect -- locks no depth at all, so the gate would silently switch its shape off.

### Two suspected config defects, both closed as NON-defects

`escalation_cap` on five decks runs the built-in 120 prior with no `escalation_fresh_frac`, which makes the
escalation budget the shared remainder and was measured starving it (StompySurprise 21.8% of escalations at
`units==0`, treasure_hunt 20.8%). Measured directly, on-policy, 4,000 games:

* **StompySurprise + `escalation_fresh_frac: 0.5` -- byte-identical play in all 16 blocks** at 1.011x units.
* treasure_hunt + frac: 1.083x units at z +0.71. Dropping `escalation_cap` entirely: 1.004x at z −0.58
  (StompySurprise 15/16 blocks identical).

So the starvation is real in the counters and inert in the play. Neither deck needed repair, and the
`units==0` counter is not by itself evidence of a defect -- a conclusion only digest equality could deliver.

### The last four decks (2026-09-11, second pooled screen: 264 jobs / 136,000 games, seeds 9,500,000+)

Mirrorwing, Minotaur, Dragons and KittyEquipment were the modelled decks the first screen missed. All four
turned out CHEAP at d5b20 (36-72 ms/game; the `TIMINGS.md` figures for them predate the perf work), so they
were screened at the same 4,000 games per cell.

| deck | d3b10 | d3b20 | d5b20 | d5b40 | verdict |
|---|---|---|---|---|---|
| Mirrorwing Dragon | +1.41, 0.987x/0.964x | — | **+5.60** (37/2), 0.914x/0.848x | — | **ADOPTED** |
| Dragons | 0/0, 0.995x/0.977x | 0/0, 0.995x/0.993x | +0.58, 0.944x/0.863x | −0.58, 0.942x/0.873x | **ADOPTED** |
| Minotaur | 0/0, 0.994x | 0/0, 0.996x | **+3.71** (19/2), 0.971x/0.959x | **+3.21** (13/1), 0.999x/1.003x | **ADOPTED** (re-measured at 8,000 games) |
| KittyEquipment | **−3.46** (0/12), 0.964x | −1.34, 0.975x | 0/0, 0.874x | +1.51, 0.833x | rejected |

**Mirrorwing Dragon is the strongest leaf-is-a-net-negative result measured** -- 37 better against 2 worse at
d5b20 (d_avg −0.0135) while also 0.914x units / 0.848x wall. Like StompySurprise, its model was not merely
unhelpful but actively misleading.

**KittyEquipment is the second Hinata2.** Good at depth 5 (0/0 at 0.874x, +1.51 at 0.833x), **bad at depth 3**
(z −3.46). Two of the fourteen decks screened now show this exact depth-split signature, which is what makes
the unconditional application of shape keys a real design question rather than a one-off.

**Minotaur's apparent unit regression was SAMPLE SIZE, and re-measuring flipped it to an adoption.** At 4,000
games d5b40 read 1.009x units; at 8,000 games (16 x 500, seeds 9,800,000+) it reads **0.999x** with quality
z **+3.21**, and d5b20 firmed to z **+3.71** at 0.971x/0.959x. ADOPTED. The lesson is narrow but useful: a
~1% units ratio is within sampling noise even though units are *deterministic per game* -- determinism makes a
given game's cost exact, it does not make the game SET representative.

**slivers_vial remains a TRADE, deliberately not adopted -- the user's call.** Its d5b20 side is a pure win
(0.914x units, **0.709x wall**, z +1.00, 9 of 16 blocks byte-identical, 8,000 games), but d3b10 buys z **+4.58**
(21 better / 0 worse over 8,000 games) for **1.020x units** -- reproduced at two sample sizes, so unlike
Minotaur's this one is real. Wall is cheaper at both configurations (0.988x / 0.709x). By the standing rule
(a clean win regresses on NO axis) this is not an agent's call; the recommendation is to take it, since the
adverse axis is the proxy and real time moved the other way.

### Dragonstorm's staged `nl_sres2` candidate is now REJECTED, and that is a lesson about additive reading

The 2026-09-10 record staged shape #3 for Dragonstorm at 0.958x (d5b20) against the MODEL-leaf baseline, with a
warning that it must be re-measured jointly rather than added to another change. Re-measured against the
leafless baseline now shipped (16 x 500 = 8,000 games per cell, seeds 9,800,000+): **d5b20 1.091x units** --
it has become more EXPENSIVE than the baseline it was meant to improve -- and **d3b10 z -3.05** (17 better /
40 worse) despite 0.763x units. The two changes overlap in what they remove, so their savings were never
additive. Staged candidate closed.

## FLEET STATE after 2026-09-11 (8 of 20 decks now run leafless)

| shape | decks |
|---|---|
| `{leaf: none, alpha: relaxed}` (shape #2) | Fluctuator, **StompySurprise, Goblins, treasure_hunt, Dragonstorm, Mirrorwing Dragon, Dragons, Minotaur** (8) |
| default (escalation + model leaf, shape #1) | Anti-Lifegain, Auras, BreachingDragonstorm, Creature Giving, CritterLifegain, FiveColour, Hinata2, KittyEquipment, Knights, Melira Pod, burn, slivers_vial (12) |

**AN OPERATIONAL CONSEQUENCE WORTH ACTING ON: those 8 decks no longer need their value leaf REGENERATED.**
`leaf: "none"` is applied in `AttachValueSidecar`'s `apply_leaf_policy()`, which parses the sidecar and then
replaces `value_model` with `MidGameEvaluator::Constant()` and sets `value_trust_depth = 0`. The 120-tree
model is therefore loaded and immediately discarded -- never consulted for any decision. So when a play-logic
change invalidates the fleet's value sidecars (the usual trigger for a regeneration sweep), these 8 decks can
be SKIPPED: only their `value_play` block and the keep table matter.

**But the FILE must stay.** Sidecar presence is what activates the hybrid at all, and the H-cell ladder is
guarded on the sidecar EXISTING (a missing model silently costs 1.35-84.8x, per `value-leaf.md`). This is
exactly why these adoptions set `leaf: "none"` instead of renaming to `.value.DISABLED.json`.

**One caveat on the adopted decks, and a possible further gain.** Each deck's exhaustive keep table and its
`mull_gen` settings were fitted under the MODEL-leaf play, and the shape keys apply during mulligan generation
too. Every measurement above was taken WITH the shipped keep table live, so the results are valid for what
ships; but a keep table re-fitted to the leafless play could add further gain on these 8 decks. Untested.
