# Hinata2 artifact refresh (e55b07bd6): what it did in the 2026-11-12 overnight

The refresh landed a new keep table (verdict TRADE: -0.081 t at play settings, 1.064x cost; the USER approved
the trade) and a new value leaf (ADOPTED). The 2026-11-12 overnight (accept `fec16d7d2`) moved every Hinata cell.
This splits that movement by swapping artifacts on the overnight GT binary (`test/logs/overnight/mtg.run`), paired
per game on the overnight Hinata cells, loss = 9, negative = faster. Script: `build/scratch/hinab/{run.sh,cmp.py}`.

| step | Hinata searched (n=1831) | Hinata2HG searched (n=400) | Hinata d0 (n=8000) |
|---|---|---|---|
| control (current artifacts vs committed GT) | 0 diffs | 0 diffs | 0 diffs |
| src commits since 11-10 (old artifacts) | +0.001 | +0.007 | -0.001 |
| new value leaf | -0.034 | -0.040 | 0 (d0 does not read it) |
| new keep table | **-0.103** (paired t -4.9 at d5) | **-0.110** | **+0.021** (t 1.6) |
| total (old GT -> new GT) | -0.137 | -0.142 | +0.020 |

Win rate, total: searched 96.8% -> 98.5% (Hinata), 96.2% -> 98.5% (2HG); d0 81.4% -> 80.5% (keep table: 610
newly unwon, 536 newly won).

Reading: the keep table's searched gain is real and matches the refresh's own measurement. Its d0 cost is small
and not significant, and the refresh never measured d0 (it measures at play settings only), so the TRADE verdict
did not mention it. Hinata plays at d5 b30; d0 is the depth-0 runner tier. Surfaced to the USER 2026-11-13. **USER ruling 2026-11-13: keep the table** ("Okay, we'll keep the Hinata
result.").
