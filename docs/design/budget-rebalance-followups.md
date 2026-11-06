# Budget rebalance: deferred follow-ups

Status: FOLLOW-UPS CHECKED (2026-11-06; item 1 mitigated, item 2 implemented, item 3 diagnosed).
This records the items left over after the budget-rebalance feature landed on
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

**2026-11-06 progress.** `plan_emit` now prices every raw plan at 10,000 ns, and budgeted
`EnumeratePlans` polls the priced limit through its inline, carry, and two-stage walks. Interrupted
lists are not cached. The priced ledger's deterministic virtual cache declines derivations that
emit over 100,000 raw plans (or return over 100,000 plans); at budget 0 it keeps the original
unlimited admission. The 20,000-plan trial cut Melira further but made one EDF play game run past
24 minutes, so it was rejected. The 100,000-plan trial finished that EDF game in 245 s (baseline
283 s) with the same play digest.

- Melira's worst generation replay at the production 20 GB cap: 644 s -> 46.5 s, win turn 7 in
  both arms. Its largest instrumented decision is 20.0 s, down from 154 s in the priced-emits-only
  trial. The 6.75 s priced allowance remains an estimate, not a literal wall-time bound.
- Paired play corpus: 184 jobs / 31,645 games, no average win-turn changes, one same-turn play
  digest change (LFE gi242, T6 in both arms). Summed job wall fell 12,059 -> 7,029 s; the
  regression-shaped slow-game p99.9 fell 18.2 -> 9.9 s. Evidence:
  `build/scratch/budget/priced_emit_100k_ab/` and
  `build/scratch/budget/priced_emit_ab/melira_shadow100k_census.tsv`.
- Final source gates: 2,612 unit tests; smoke 176/176 with no changed games; regression 282/282
  with no changed games; strict viewer replay 767 references, zero drift/gaps. The one held-out
  LFE digest change is outside these committed tiers and retains its T6 win.

## 2. `dupc_fold_prefix`: a candidate exact skip in the two-stage plan driver

On the Goblins d6/b10 generation line, 36-61M selections per line die at the canonical-prefix clause of
the duplicate check, after the walk has visited them. This is the same shape the sac-dup fold removed.
- It needs a straddle-aware prefix map.
- Notes: `build/scratch/budget/sacfold/HANDOFF.md` (RESOLVED section).
- Exactness bar: a verify mode with 0 passed / 0 brute mismatches, and regression digests equal to GT.
- That cost line is now dominated by 140M `pay_calls`, so expect work removed, not much wall.

**2026-11-06 candidate.** `MTG_TWO_STAGE_FOLD_PREFIX` checks combined mana/payoff ord masks
before admitting pairs when there are at most two classes; larger class sets use the full
choice after pair sorting. A class wholly on one side can reject that side's line before
pairing; a class that straddles the split is checked only after the masks are combined. The
ordinary leaf rule remains in place. `MTG_TWO_STAGE_FOLD_PREFIX_VERIFY=1` preserves flagged
positions and independently checks their ords against the leaf verdict: over seven Goblins
regression jobs (1,715 games), 4,367,408 flagged, **0 passed, 0 brute mismatches**. All seven
on/off job digests match.

On the Goblins d6/b10 generation line, the fold keeps win turn 4 and play digest
`00498288b76987e4`; wall falls 115.0 -> 98.7 s. `dupc_fold_prefix` falls 7.07M -> 0 and
`enum_enter` falls 23.05M -> 16.84M. Evidence: `build/scratch/budget/priced_emit_100k_ab/prefix_gob_*`.
Final gates: 2,612 unit tests; smoke 176/176 and regression 282/282 with no changed games;
strict viewer replay 767 references, zero drift/gaps. No GT acceptance was needed.

## 3. LFE generation replays hit 5-6 GB RSS caps

Replaying LFE's slow generation lines at 4-7 threads aborted at 5-6 GB caps (exit 137), with rollouts
17-31 min in flight. The pending question was whether the tier-2 plan pool or a breakpoint cache grew.

**Diagnosed 2026-11-06:** neither stored pool explains the RSS spike. Replaying the 30-line LFE
tail list at four threads and a 5 GB cap:

- With the breakpoint prefix-resume cache disabled (`MTG_NO_BP_PREFIX_CACHE=1`), RSS reached
  5.43 GB and aborted while the shared plan pool held only 240 MB.
- With the plan pool effectively disabled (`MTG_PLAN_CACHE_KB=1`), RSS reached 5.80 GB and
  aborted with the pool at **0 MB**. Logs: `build/scratch/budget/priced_emit_100k_ab/lfe_no_prefix.err`
  and `lfe_no_plan_pool.err`.

The large allocations are in-flight enumeration/axis expansion, per the earlier source-level
diagnosis in `docs/design/lfe-plan-fan-explosion.md` (sections 3 and 7). These 5 GB replay caps
are below what the four concurrent heavy LFE rollouts need. The stored plan and breakpoint
continuation caches share the accounted plan pool; changing their limits does not fix this case.

**2026-11-06 larger historical-tail sample.** Replayed the first 100 distinct lines of each
historical slow-generation list at the generation settings (depth 1, budget 1), priced-stop ON,
and a 20 GB RSS cap. These are deliberately selected slow cases, not a random game sample.
Logs and input lists: `build/scratch/budget/lfe_followup/`.

| Deck / workers | Finished | Batch wall | Peak RSS | Per-line median / p90 / max |
| --- | ---: | ---: | ---: | ---: |
| EDF / 30 | 100/100 | 1:18 | 4.5 GiB | 6.6 / 18.8 / 40.0 s |
| LFE / 30 | 92/100, RSS abort | 1:18 | 20.8 GiB | 8.7 / 28.1 / 53.6 s (completed lines only) |
| LFE / 8 | 100/100 | 1:47 | 18.0 GiB | 3.8 / 16.2 / 40.0 s |
| LFE / 4 | 100/100 | 2:49 | 11.1 GiB | 3.4 / 15.0 / 33.0 s |

The EDF sample has no remaining multi-minute line. All 100 LFE lines finish in well under a
minute when concurrency is limited, but the 30-worker run still cannot finish under the
production memory cap. Eight workers leave only about 2 GB headroom in this replay and are not
safe for a full generation with its cell-table baseline. The LFE issue remains **open**:
temporary plan fan-out needs a memory bound that preserves the same decisions; changing the
stored plan-pool limit does not provide one. The lazy look-top fan was active in the four-worker
run and avoided materializing 118.8 million variants, so disabling that optimization would
worsen this case. No new engine or budget setting was adopted from this diagnostic.

## 4. Held or decided by the USER (do not adopt held items without them)

USER 2026-11-04: hold these "unless they are shown to help the HEAVY TAIL; not keen on a new per-deck
setting for a few percent."
1. **Per-deck price R** (each deck's own ns/unit). Over 31,082 paired games: 27 better, 10 worse, wall
   +0.9%. It redistributes quality within a deck at the same cost and does not shrink the tail. The
   build-in shape is also open: a fit table in the profile, or derived in-engine.
2. **Lower search budget for three decks — declined 2026-11-06; keep 20 virtual ms.**
   - Snow x0.7: +1 / 1,300, 0.72x wall.
   - Selesnya v2 x0.5: 0 / 1,800, 0.74x wall.
   - Colossus x0.5: -1 / 1,767, 0.77x wall.
   These earlier arms changed the priced-work rate, not `budget_ms`. A direct play-budget A/B on
   600 fresh paired games per deck at shipped depth tested Snow 20 -> 14 and Selesnya v2 / Colossus
   20 -> 10: Snow had 3 slower games, 0 faster, net +3 loss-penalized turns at 0.84x wall;
   Selesnya had 0 win-turn changes at 0.86x wall; Colossus had 1 slower game (T5 -> T7), net +2
   turns at 0.77x wall. In the same pooled batch Hinata at its shipped b30 averaged 3.03 s/game,
   Snow b20 2.99 s/game, Selesnya b20 0.098 s/game, Colossus b20 0.064 s/game. Evidence:
   `build/scratch/budget/direct_budget_ab/`. USER: stick with 20 ms and drop this question.
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
