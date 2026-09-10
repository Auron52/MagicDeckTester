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

Both axes, per configuration, against the deck's shipped shape: quality (average win turn and per-game
better/worse counts over >= 8 x 500 games) AND run time (deterministic search units; wall only from a
same-batch pair or the single-worker probe -- never across batches). A shape is adopted only when it is no
worse on either axis at every configuration the deck is played at (d5b20 and d3b10 here, since the mulligan
generator runs the deck at d3).

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
