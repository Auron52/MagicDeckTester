# Depth-matrix interrupt resilience: the packed last quarter

**Status:** diagnosed, fix NOT yet applied (a run was in flight when this was written).
**Owner:** apply before the next `valueleaf.sh run`.
**Trigger:** Fungus value-leaf run, 2026-09-21. A cancel-and-restart at **99.5% complete**
discarded **1,450 finished games** and left the box at **4 of 24 cores for ~30 hours** beforehand.

The user's framing, which is the right one: *"Either we didn't run games in parallel or we
restarted too many. Either way, the result is wrong."* Both happened. They are **one root cause**.

---

## 1. What was measured

Fungus phase C: 52 cells = 13 per seed (H1-H5 + V1-V8) x 4 seeds (8008/9009/10010/11011),
target 400 games/cell.

| quantity | value | how derived |
|---|---|---|
| nominal slots | 20,800 | 52 x 400 |
| condemned slots | 1,066 | **82 condemned games x 13 cells** -- `apply_skiplist` drops a condemned offset from EVERY cell of its seed |
| runnable slots | 19,734 | 20,800 - 1,066 |
| had results at cancel | 19,637 | = **99.5%** |
| never run | **97** | ~4 chunks' worth -- matches the observed in-flight depth |
| **discarded by resync** | **1,450** | **7.3%**, all of them FINISHED results |

Per seed: s8008 445, s9009 247, s10010 563, s11011 195. Per-cell drop sizes were
**15, 19, 23, 40, 45 offsets** -- all inside the last quarter of the target.

> **Counting trap that cost an hour here.** A condemned game costs **13 slots, not 1**. Subtracting
> 82 instead of 1,066 invents ~1,000 phantom "never-run" games and makes the tail look like a
> scheduling failure rather than a genuine 4-game long tail. Sanity-check any derived backlog against
> observed queue depth: a queue holding 1,081 pending games cannot show 4 games in flight on 24
> workers.

---

## 2. Root cause: divergence in the packed quarter, amplified 13x

### 2a. The queue deliberately stops keeping cells in lockstep at 75%

`scripts/attic/valueleaf_depth_matrix.py:913-917`:

```python
def zone(ch):
    c = ch["c"]
    if c["intractable"] and ch["off"] == first_out[id(c)]:
        return 0                                          # CONDEMNED, next chunk: up front
    return 1 if ch["off"] < 0.75*target(c) else 2         # level-order, then packed
```

Zone 1 (offsets 0-299) is **level-ordered**: every cell clears level *k* before any cell starts
*k+1*, so the banked prefix climbs with the run. Zone 2 (offsets 300-400) is **packed** -- pure LPT,
cost-descending, no lockstep. The rationale is recorded at `:865` ("LEVEL-ORDER THE FIRST 3/4, PACK
THE LAST", user 2026-08-12): packing protects **makespan** at the end.

Cells therefore fan out freely over the last 100 offsets. At cancel they sat between ~355 and 400.

### 2b. The banking rule then multiplies that lag by cells-per-seed

`resync_engine_change` (`:511`, "KEEP EVERY FULL SET, CONTIGUOUS OR NOT") keeps an offset only if
**every incomplete cell of the seed already holds it** -- otherwise some cell would re-run that
offset on the NEW engine while another kept it on the OLD one, and the per-game paired comparison
would be mixed across cells. **This rule is correct and must not be weakened.** A cell already at
target gets no vote but is still subject to the drop, which is also correct for the same reason.

The consequence is arithmetic:

```
games discarded per seed  ~=  (lag in offsets)  x  (cells in that seed)
```

With 13 cells per seed, **one cell lagging 15 offsets costs 195 finished games**; 45 offsets costs
585. That is exactly the observed 15/19/23/40/45 -> 195/247/.../563 pattern. Zone 2 permits a lag of
up to 100 offsets, i.e. **up to ~1,300 discarded games per seed**.

### 2c. The same divergence produces the idle tail

`BatchRunner`'s pool is per-**game** (`WorkItem{job, game}`, `Take` at
`src/runner/BatchRunner.cpp:1135`), so there is no scheduling cap to blame for low utilisation --
and `classify` (`:1229`) did not gate these cells either: H3/H4/H5 are at or below
`never_condemn_depth`=5 so they return `kProtected`, and their `CellCeiling`s froze at offset 25.

The box was at 4/24 simply because **the queue had drained**. Packing finishes the cheap cells early,
so by the end only a few expensive cells have work left; when those reduce to their handful of
degenerate games, 20 cores have nothing to do. Measured: ~4 games in flight, **4/24 = 17%**, which is
the ~20% the user read off Windows Process Explorer. (My own `ps -o %cpu` reading of "~15 of 24
cores" was a 40-hour *lifetime average* and hid this completely -- see the CPU-measurement note in
`docs/design/per-game-wall-clock-backstop.md`.)

Under level-ordering the opposite holds: every cell still owes its last level, so ~1,300 games are
available right up to the end and the pool stays fed.

**So one property -- cell divergence -- produces both symptoms.** Reduce divergence and the discard
shrinks *and* the tail stays saturated.

---

## 3. Why the trade that justified packing has flipped

Packing exists to bound makespan when a catastrophic game is discovered late. Under LPT the
expensive cells run first, so a 30-hour game starts early and overlaps the bulk of the run. Under
level-ordering, a catastrophic game at offset 390 starts ~97% of the way through and adds its **full
duration** to the makespan.

That was a sound trade **when a single game's duration was unbounded**. It no longer is. The
wall-clock backstop (`b37e0d5c`, `docs/design/per-game-wall-clock-backstop.md`) caps a game at
`--max-game-predict-sec` / `--max-game-wall-sec`; `valueleaf.sh:261-262` sets 7200 s / 12600 s.

| | before the wall cap | now |
|---|---|---|
| worst-case makespan cost of level-ordering to the end | **unbounded** (observed: 30.9 h) | **<= 210 min**, by construction |
| worst-case banking loss from packing | up to ~1,300 games/seed | unchanged |

The exposure packing protects against is now bounded; the exposure it *creates* is not. **The wall
cap is precisely what makes level-ordering-to-the-end safe**, and it landed after this zone split was
designed.

---

## 4. The fix

Gate zone 2 on the wall cap being **absent**. One condition, in `zone()`:

```python
# Packing the last quarter trades banking for makespan. That trade was sound only while a single
# game could run unbounded (observed 30.9 h, 2026-09-20). With a wall cap armed the makespan
# exposure is bounded by --max-game-wall-sec, while the banking exposure it creates is not:
# a lagging cell costs (lag x cells-in-seed) FINISHED games on any engine change. So level-order
# all the way whenever the cap is armed. See docs/design/matrix-interrupt-resilience.md.
packed_ok = args.max_game_wall_sec <= 0 and args.max_game_predict_sec <= 0
return 1 if (not packed_ok or ch["off"] < 0.75*target(c)) else 2
```

Zone 0 (condemned cells to the front) is unchanged -- those must still be judged early to free the
queue, and they are capped at the reference target, so they cannot lag a seed.

Nothing else moves. In particular the packed weight encoding at `:932-936` still works: with zone 2
unused, every non-condemned chunk carries `lvl = (target-off)*10_000`, which `BatchRunner`'s
`sched_weight` sort (`src/runner/BatchRunner.cpp:1079`) honours ahead of depth. That path is already
exercised by zone 1 today.

**Expected effect.** Divergence falls to roughly the pool's in-flight window (~24 games) instead of
100 offsets, so an interruption at any point costs about **one level**, not a quarter. On the
2026-09-21 numbers that is ~1,450 discarded games -> order ~100.

---

## 5. Considered and rejected

- **Weaken the resync's full-set rule.** No. It is what keeps each offset single-engine across every
  cell, which is what makes the per-game paired comparison valid. Unequal game sets have already
  flipped the sign of a depth-matrix verdict once.
- **Re-run whole cells instead of offsets.** Already rejected upstream (`:495-509`): 140x more
  expensive for the same guarantee.
- **Truncate the seed to the banked intersection instead of re-running.** Genuinely attractive -- it
  makes the redo *zero* at the cost of ~4% of the sample -- but it silently shrinks the matrix, and
  the operator cannot see that it happened. Worth revisiting as an explicit
  `--allow-target-shortfall <frac>` if interruptions become routine; it is not a substitute for
  fixing the divergence that made the intersection small.
- **Seed LPT from measured per-game cost history** (`slow_games.log`, per-chunk `ms`) so known-slow
  games start first on a resume. Complementary, not a substitute, and higher effort. Note
  `cells.json`'s per-chunk `ms` is **not** wall time -- summed across the matrix it gives 0.39 core-h
  against ~626 core-h actually spent -- so any such work needs a real cost column first.

---

## 6. Verification

The change alters only *order*, never results: every game is seeded by its job seed + game index, so
reordering is lossless (stated at `BatchRunner.cpp:1067`). So:

1. `python3 test/check_gt_logs.py` and a smoke regression are sufficient for correctness.
2. The real check is operational -- on the next matrix run, watch
   `[batch] heartbeat: N/M workers busy` through the final 25% (it should stay near M/M, where
   packing previously let it decay), and confirm the per-cell `games` counts in
   `matrix.txt.cells.json` stay within ~one chunk of each other.
3. Confirm makespan did not regress by more than the cap: compare phase C wall against the Fungus
   baseline.
