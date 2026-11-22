# Rock CPU handoff (agent rock-cpu, 2026-11-22 ~10:30 UTC, stopped at ~251K context)

## USER rulings (verbatim, as relayed by the parent)
- "Sure, we can do your recommendation on Rock Niv." The recommendation: "a fresh per-deck CPU measurement first so you
  have current figures for every deck, not only Niv. Then have an agent look for a cut that gives up some quality."
- "I'm mostly concerned with searched."
- "I don't like leaving gaps" (if d8 b0 cannot settle a game because of the unit cap, replay it at an escalating budget).
- Earlier, from the doc: "Sounds good. Let's turn it on if the unwon looks okay. We should indeed support this."

## Brief rules (from the parent)
- Adopt nothing. A lossy cut is the USER's trade-off. Report a menu: CPU saved per deck vs quality lost per deck, with
  a recommendation. If a cut is exact (byte-identical play), say so; that one may be prepared for a GT accept.
- One big process at a time under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_RSS_CAP_GB=40 ...`.
  Small work in `scripts/box_lane.sh --slots N` with N <= 2. `MTG_PGO=0` on both arms (explicit Release binaries).
- Push wip/rock-cpu only. NEVER push phase-1-2-deck-analyzer and never join the push queue. Rebase, never merge.
- Every slower game in a reported A/B gets a d8 b0 replay (lane, <= 2 slots); escalate the budget if the unit cap stops it.
- Hand off at ~250K context.

## Done
- Worktrees: `.claude/worktrees/rock-cpu` (branch wip/rock-cpu-base = origin 4a7f65983, pristine; built; runs cd here
  for deck paths) and `.claude/worktrees/rock-cpu-dev` (branch **wip/rock-cpu**, pushed).
- Commit **b9013484b** (wip/rock-cpu): the step-1 table in `docs/design/rock-in-plan-2026-11-19.md` ("Current per-deck CPU
  of the rung, 2026-11-22") and three DEFAULT-OFF cuts in `LineReducerKeepHold` (ManaPayment.cpp), heurarm slots
  `LKR_PLAYOUT_NO_TABLE`, `LKR_PLAYOUT_OFF`, `LKR_PRICE` (HeuristicArm.h), unit site `kLkrRung` / `lkr_rung` plus
  `TurnSolver::ChargeLineKeepUnits` (TurnSolver.cpp/.h). Compiled without errors (dev build at 362/381 at handoff).
  Context-split stats: `MTG_LINE_REDUCER_KEEP_STATS=1` prints `[line-keep-ctx]` and `[line-keep-cut]`.
- Base binary: `build/scratch/claude_rock_cpu/mtg.base` (origin 4a7f65983, HEAD:src 104f1245).
- Step-1 batch (m1) DONE: `build/scratch/claude_rock_cpu/m1.out`, `logs_m1/`, compare `m1.cmp.txt` (copy in this dir).

## Step-1 table (searched = play settings, seeds 700000; d0 seeds 710000; one pooled batch, 32 threads)
| deck | s pairs | s faster/slower | s net | s units | s CPU | s instr (12 g) | d0 faster/slower | d0 net | d0 instr (50 g) |
|---|---|---|---|---|---|---|---|---|---|
| angels | 1000 | 0/0 | 0 | 1.0000 | 1.0025 | 1.0082 | 0/0 | 0 | 1.0002 |
| bruna | 1000 | 1/0 | -1 | 0.9981 | 1.0034 | 1.0065 | 1/1 | +1 | 1.0002 |
| dragons | 1000 | 0/0 | 0 | 0.9996 | 1.0089 | 1.0185 | 3/0 | -3 | 0.9995 |
| extraturns | 1000 | 0/0 | 0 | 1.0000 | 1.0329 | 1.0667 | 0/0 | 0 | 1.0081 |
| fivecolour | 1000 | 0/0 | 0 | 1.0001 | 1.0073 | 1.0149 | 2/1 | -1 | 1.0006 |
| fungusv3 | 1000 | 0/0 | 0 | 1.0000 | 1.0015 | 1.0117 | 0/0 | 0 | 1.0003 |
| galadriel | 1000 | 1/0 | -1 | 0.9998 | 0.9991 | 1.0237 | 3/0 | -4 | 1.0031 |
| giants | 1000 | 0/0 | 0 | 1.0000 | 1.0053 | 1.0057 | 0/0 | 0 | 1.0001 |
| hinata | 1000 | 1/0 | -1 | 1.0002 | 1.0285 | 1.0287 | 1/0 | -1 | 1.0012 |
| kittyv3 | 1000 | 0/0 | 0 | 1.0000 | 1.0008 | 1.0016 | 0/0 | 0 | 1.0000 |
| merfolkturns | 1000 | 0/0 | 0 | 1.0000 | 1.0105 | 1.0015 | 0/0 | 0 | 1.0001 |
| niv | 2000 | 10/4 | -6 | 0.9998 | 1.0641 | 1.0652 | 10/3 | -12 | 1.0013 |
| sea | 1000 | 1/0 | -1 | 0.9992 | 1.0004 | 1.0025 | 0/0 | 0 | 1.0004 |
| stompy | 1000 | 0/0 | 0 | 1.0000 | 1.0182 | 1.0074 | 0/0 | 0 | 1.0005 |
| stuffy | 1000 | 0/0 | 0 | 1.0001 | 1.0129 | 1.0226 | 0/0 | 0 | 1.0009 |
| whiteknights | 1000 | 0/0 | 0 | 1.0000 | 1.0062 | 1.0086 | 0/0 | 0 | 1.0002 |
| ALL | 17000 | 14/4 | -10 | 0.9999 | 1.0256 | 1.0372 | 20/5 | -20 | 1.0015 |
d0 CPU ratios are omitted here (noise: ~10 M instructions per game); the full table with them is in the doc.
Slower games: searched niv gi 79, 1250, 1494, 1527 (seed = 700000 + gi); d0 bruna 772, fivecolour 686, niv 74, 201, 401
(seed = 710000 + gi).

## Running jobs at handoff (do NOT kill; markers in build/scratch/claude_rock_cpu/)
| job | PID (lane wrapper) | output | DONE marker |
|---|---|---|---|
| dev (cuts) build, worktree rock-cpu-dev, lane 2 slots | 732572 | `build_dev.log` | `build_dev.done` (holds rc) |
| perf slice base100: base binary, on/off, 100 searched + 300 d0 games/deck, seeds 760000/770000, 1 thread x2 | 816001 (queued) | `perf_base100/*.perf, *.out` | `perf_base100/DONE` |
| startup instruction baseline (1 d0 game) | 816004 (queued) | `perf_startup/st.perf` | the file itself |
| d8 b0 replays of the 9 step-1 slower games, both arms (`d8_m1.json`) | 850695 (queued) | `d8_m1.out`, `logs_d8_m1/` | `d8_m1.done` |
Read perf: `python3 perfsum.py perf_base100 off on` (subtract `perf_startup/st.perf` per process for cheap decks).
Compare d8: `python3 -c` over `logs_d8_m1/*.wins` (on vs off per game: equal turn = recovered).

## Remaining, in order
1. When `build_dev.done` = 0: copy `rock-cpu-dev/build/Release/mtg` to `build/scratch/claude_rock_cpu/mtg.dev`. Check that
   with every new flag off it is byte-identical to `mtg.base` (e.g. `perf_lane.sh` with the dev binary, arm `{"on":{}}`, same
   seeds as perf_base100, compare `.wins`; perfsum's "same games" column does it).
2. Calibrate the price: run the on arm with `MTG_LINE_REDUCER_KEEP_STATS=1` on Niv / Extra Turns / Hinata (lane, 1 thread),
   read `[line-keep-ctx]` (where the rung's ms go: window vs playout) and work events per ms; set MTG_LKR_PRICE_MILLI so that
   the units charged approximate the rung's CPU (ns per unit is about cpu/units from m1.out, per deck). Try ~1x and ~0.5x.
3. Held-out A/B on the box lock, one pooled batch, fresh seeds (e.g. searched 780000, d0 790000): arms def (no flags),
   playout_off, playout_no_table, price (one MILLI per batch, since MILLI is a process env). Use
   `mk.py out.json SB DB N NIV_N ND '{"def":{},"poff":{"MTG_LKR_PLAYOUT_OFF":true},"pnt":{"MTG_LKR_PLAYOUT_NO_TABLE":true},"price":{"MTG_LKR_PRICE":true}}'`
   and `big.sh <tag> mtg.dev out.json` (add `env MTG_LKR_PRICE_MILLI=...` in big.sh if not the default). Size it to the effect:
   Niv 2000+, Extra Turns / Hinata 1000, others 500. Compare each arm vs def with `cmp.py <out> logs_<tag> def <arm> --list`.
   Add perf instruction counts per cut (perf_lane.sh with the dev binary and the same arms).
4. d8 b0 replay every slower game of each cut (`mkd8.py`, lane via `lanebatch.sh`); escalate the budget if a d8 b0 run hits the unit cap.
5. Report the menu (CPU saved per deck vs quality per deck, searched and d0) with a recommendation; adopt nothing. The price
   and the playout cuts are inert at d0 by construction (no decision budget / no playout at d0), so d0 should be byte-identical
   for every cut: verify.
