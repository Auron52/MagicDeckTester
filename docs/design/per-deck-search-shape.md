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

Full table: `analysis-Melira Pod.md` 2026-09-10a. Summary:

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

What the screen DOES show about where a leaf's benefit comes from: the realized `ship / escnl` is 0.4-0.9 at
d5b20 while the heuristic escalation is NOT the dominant cost of `escnl` -- the value leaf's estimates give
the search an ORDERING and B&B cuts that a leafless pass cannot have, so its tree is smaller. A leafless run
therefore cannot predict the leaf's benefit; the predictor needs per-decision instrumentation (verified
fraction, escalation-unit share, tree size by depth) recorded per job, which is future work.

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

So the answer to "are we wasting unnecessary time with the value leaf" is no: what the leaf costs is its
generation, not its play; in play it PAYS (0.4-0.9x units) through ordering and cuts. The no-leaf shapes
are for decks WITHOUT a trusted model (Fluctuator: 0.38x) and for day one of a new deck.

## Determinism

The emulated ladder's learned R(k)/G(k) are per GAME (keyed on `game_seed`, prior = the deck's frozen
`escalation_r` else 120). Before 2026-09-09 they persisted across games on a batch worker, so a job's play
depended on thread history (caught by a re-run: different digest). Same-job-twice checks now match.

## Memory

The leafless ladders build large per-decision memos: a 32-worker screen batch reached ~20 GB on the 23 GB
box, 16 workers 14.6 GB, 12 workers ~10 GB. Run screens of these shapes at <= 12 workers with a
`MemAvailable` watchdog (`logs/emul_screen/memwatch.sh <pid>`).
