# Budget rebalance: deferred follow-ups

Status: DEFERRED (2026-11-05). This is everything left over after the budget-rebalance feature landed on
`phase-1-2-deck-analyzer`. Each item stands alone; work them in the order given. Full history of the
measurements is in `docs/design/budget-rebalance-checkpoint.md`.

## What the feature shipped (for context)

The aim (USER): expensive cases must be priced so that a LOWER budget means LOWER cost ("far too many
examples of us using budget 1-3 and it taking hours"), and no game should run for an hour or more.

- **Priced per-decision ceiling.** `decisionwork` (`src/ai/DecisionWorkMeter.h`) bounds the wall-clock
  cost of one decision.
  - The limit is `PricedLimitNs = limit_units x MTG_DECISION_PRICED_REF_NS (25,000) x MTG_DECISION_PRICED_X (100)`.
  - It trips through `SearchBudget::Overrun()` and through the breakpoint-derivation stop.
- **`MTG_BP_DERIVE_PRICED_STOP`** (default ON). The breakpoint-continuation derivation polls the priced
  ceiling.
  - EDF's depth-1 generation tail went from 47,146 s to 236 s.
  - 0 win turns moved over 4,936 paired generation lines and 31,588 held-out play games.
- **`MTG_SAC_DUP_WALK_FOLD`** (default ON, exact). The plan walk never visits selections that use one
  sac outlet twice for mana.
  - That waste dominated the Goblins regression from `MTG_SUBSET_SAC_OUTLETS`.
- **`MTG_DUP_ACTION_FOLD`** (default ON, exact). Independent actions that are equal except for `eval` are
  walked in one canonical order only.
  - On the worst Melira d3/b3 generation line, emitted plans went from 302M to 127M.

Every switch has a per-job manifest arm (`src/ai/ValueArm.h`, parser in `src/runner/BatchRunner.cpp`),
so an A/B is a single pooled batch.

## 1. The Melira d3/b3 heavy decision is still not bounded by the priced ceiling

**Where it is.** Melira Pod mulligan generation (d3/b3), turn 5-6, main-2 enumerations.
- The board: 3x Carrion Feeder, 3x Kitchen Finks, Bloodthrone Vampire, Celes.
- Each call emits 20K-110K plans.
- The worst replayed line (seed 10372713029942101962, list `build/scratch/budget/hist/mel/l_<seed>.list`)
  takes about 1,500 s with the dup-action fold, and its heaviest decision about 550 s.
- The priced limit for that decision is 6.75 s.

**Why the ceiling does not trip.** The ceiling reads `workctr::t_priced`, and two things keep that count
low:
1. **The plan-cache priced ledger mutes recomputes** (`MTG_PLAN_PRICED_LEDGER`, the `pledger`
   namespace in `TurnSolver.cpp`).
   - It bills only what an unlimited plan cache would recompute. That rule exists so that play does not
     depend on cache residency.
   - The real cache refuses the 110K-plan lists and recomputes them. The ledger runs those recomputes
     muted, so only 4.9% of odometer visits were billed.
   - With `MTG_PLAN_PRICED_LEDGER=0` the decision stops at 2,896 units / 182 s.
2. **Emitted plans are unpriced.** Building, sorting and deduplicating each emitted plan costs about 20x
   the 511 ns an odometer visit is priced at.

**Check this first.** Those replays ran at `MTG_RSS_CAP_GB=3`, and the plan-cache byte pool derives from
the RSS cap, while real generation runs at 20 GB. `build/scratch/budget/melfold/run_cap.sh` replays the
line at cap 20, and at cap 20 with `MTG_PLAN_CACHE_KB=0` (unbounded). See "Probe result" below.
- If the line is fast at cap 20 / unbounded, the tail is a small-pool artifact. Look at entry-size
  admission or the pool size, not at pricing.
- If it stays slow, it is a pricing gap.

**Candidate fix if it is a pricing gap.** Add a priced work kind for emitted plans, charged per plan
emitted (with a measured ns price, like the other `workctr` kinds). Also add a priced poll in
EnumeratePlans' walks, so a tripped ceiling actually cuts the walk:
- the carry walk's imask loop;
- `PlainOdometerDfs` (its emit already supports stop-by-returning-true);
- `EnumeratePlanPositions`.

A truncated list must be marked interrupted so that no memo stores it, as the derivation stop already
does.

**Constraints on that fix:**
- It is inert at budget 0 by construction, because the ceiling is only armed at a budgeted decision
  root. That makes it a re-pricing, which is allowed: "a budget may re-price work, it may not make a
  line unfindable".
- It moves budgeted play. It therefore needs a held-out play A/B (every deck net <= 0) plus the
  generation-tail replay (`build/scratch/budget/hist/stop/pair.py` pattern), then a smoke/regression GT
  accept.
- Re-check the X=100 calibration afterwards, since a new priced kind raises every decision's priced
  total.

**What is NOT exactly foldable.** One 110,591-plan call reaches only 3,363 distinct end states. But which
body dies changes Celes' counters and the persist return. So only a state-level dedup
(`MTG_CAND_DEDUP`, default off, a lossy question) could collapse it.

**Not covered by the dup-action fold.** The two-stage (mana-side) walk and Solve's rollout walk. The
heavy rows put no positions in either.

Data: `build/scratch/budget/melfold/`.
- `cen_offc.tsv` / `cen_onc.tsv`: census with the fold off / on.
- `cen_noledger.tsv`: census with the ledger off.
- `dbg2.err`: heavy-call dumps.
- `dbg3.err`: state census.

## 2. `dupc_fold_prefix`: a candidate exact skip in the two-stage plan driver

On the Goblins d6/b10 generation line, 36-61M selections per line die at the canonical-prefix clause of
the duplicate check, after the walk has visited them. This is the same shape the sac-dup fold removed.
- It needs a straddle-aware prefix map.
- Notes: `build/scratch/budget/sacfold/HANDOFF.md` (RESOLVED section).
- Exactness bar: a verify mode with 0 passed / 0 brute mismatches, and regression digests equal to GT.
- That cost line is now dominated by 140M `pay_calls`, so expect work removed, not much wall.

## 3. LFE generation replays hit 5-6 GB RSS caps

Replaying LFE's slow generation lines at 4-7 threads aborted at 5-6 GB caps (exit 137), with rollouts
17-31 min in flight. Nobody has looked at it yet. Find which pool grows: tier-2 plan pools, or the
breakpoint cache.

## 4. Held for the USER (do not adopt without them)

USER 2026-11-04: hold these "unless they are shown to help the HEAVY TAIL; not keen on a new per-deck
setting for a few percent."
1. **Per-deck price R** (each deck's own ns/unit). Over 31,082 paired games: 27 better, 10 worse, wall
   +0.9%. It redistributes quality within a deck at the same cost and does not shrink the tail. The
   build-in shape is also open: a fit table in the profile, or derived in-engine.
2. **Lower search budget for three decks.**
   - Snow x0.7: +1 / 1,300, 0.72x wall.
   - Selesnya v2 x0.5: 0 / 1,800, 0.74x wall.
   - Colossus x0.5: -1 / 1,767, 0.77x wall.
3. **A tighter priced ceiling at low budgets (X=30 at b1/b3).** On all 40 complete decks (200 games
   each):
   - b1: 0.93x wall, p99.9 7.9 -> 5.8 s.
   - b3: 0.95x wall, max 26.4 -> 20.6 s.
   - dq +3 at each budget.

   At play budgets X30 was lossy: kittyv2arch gi1723 went from a turn-4 win to turn 6.

## Quality-vs-tail curve (the feature's deliverable): data on hand

| Run (`build/scratch/budget/`) | What it covers | Result |
|---|---|---|
| `scale.*` | 40 complete decks, d3, budgets 1/3/10/30 | Total wall 690 / 964 / 1,531 / 2,332 s; max game 15 s at b1, 28 s at b3 |
| `ceil_ab.json` | 8 expensive decks, X sweep | X30 at b3 costs about nothing |
| `c40.*` | All 40 complete decks, X30 vs X100 | See item 4.3 |

On the scaling sweep, budget 1 costs 30% of budget 30: a floor. It is dominated by EDF / Alchemist /
Hinata at about 0.8-1 s/game.

## Probe result

2026-11-06, worst line (seed 10372713029942101962), dup-action fold build, unit ceiling `MTG_DECISION_WORK_X=1000`,
single core, run alone on the box:

| Arm | Line wall | Win turn |
|---|---|---|
| `MTG_RSS_CAP_GB=3` (earlier replays) | ~1,473 s | 7 |
| `MTG_RSS_CAP_GB=20` (the generation cap) | **644 s** (logged 1,993 s) | 7 |
| cap 20 + `MTG_PLAN_CACHE_KB=0` (unbounded) | killed at the 20 GB RSS cap (exit 137) | -- |

Reading:
- The small pool explained about half the cost.
- At the generation cap the line is still about 100x over its heaviest decision's priced limit, so it is
  a real pricing gap (item 1's fix).
- An unbounded plan cache does not fit 20 GB on this one line, so `MTG_PLAN_CACHE_KB=0` is not a
  usable control on this box.
