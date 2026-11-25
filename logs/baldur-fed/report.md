# Baldur's Gate fed-mode payment fix (agent report, 2026-11-25)

I fixed it, and g1483 is back to a T4 win at both its own settings and at d8 b0, matching base's digest. The A/B nets are Maze's End play -1, Maze's End 2HG 0, Merfolk Turns 0 and Goblins 0. All three slower games recover at d8 b0. The cost is about +1% CPU on Maze's End and nothing on the other decks. The fix is on `wip/baldur-fed` (head **75484034b**, fix commits 3ce0d0345, b7d214fff, a4f87035d), pushed.

**Cause.** This was not a fault in the spend-rank rule itself; it exposed an existing gap in the shared payer. A repair for this case already exists (`MTG_SCALED_LAND_ROUTE`): when a payment leaves Baldur's Gate or Three Tree City unable to fund the rest of the line, it re-makes the payment with one source held out. But it read "the rest of the line" from the line hold, which carries only the line's casts. Charm was the last cast, so the route saw nothing owed after it and never ran (2 checks and no strands found in the whole game). The second Maze's End activation was visible only to the Maze's End source rank, which only orders sources and never re-routes a payment. Base won T4 by luck: its rank happened to spend Golgari early, which forced Charm through the fed mode.

**Fix: `MTG_SCALED_ROUTE_ACTS`** (new per-job flag, default ON, `=0` gives the old payment exactly), in `ScaledLandRouteHold`, `src/ai/ManaPayment.cpp`.
- The route's "rest of the line" now includes the plan's pending activations, priced as generic mana. While an activation is being paid, its cost is no longer subtracted from the casts-only line hold.
- When only activations are left, a scaled land must make X >= 4 (feeder + 2) before the route looks. Below that, fed mode makes no more mana than the plain path, which matches your "unless the amount generated is 2-3 or less".
- An early exit skips the check when untapped plain-mana lands already cover the rest. It skipped 4.6M of 10.75M checks.
- Neither cost cut changed any game: 0 of 22,000 digests in the full re-run, and 0 of 4,200 on the subset.

It is a feasibility repair, not a pure dominance. A payment is replaced only when the greedy one provably strands the planned line and the alternative is proved to keep it payable. Floating the fed surplus is not strictly better than keeping a source untapped, which is why it needed the A/B. The same code covers Three Tree City, and it runs in search, executor and playout at every budget.

**Verification** (d8 b0 unless noted):

| game | flag off (= maze-rank3) | flag on |
|---|---|---|
| g1483 2HG, d8 b0 and play setting | T5 e14e2325 | T4 6f20b863 (= base's digest) |
| g993 / g1149 / g1770 / g2030 and the 14 other recovered games | maze-rank3's recorded digests | identical digests |

- **Unit tests:** two new cases from the g1483 board in `test/unit/test_mazes_end_f6.cpp` (flag on: Charm feeds Baldur's Gate and the second {3} pays; flag off: it strands). The Maze's End and scaled-land tests pass 174/174, and the full suite passes 4177/4177 on the final binary.
- **INT-9 Thrumming Stone** passes on this tree, so the suspected pre-existing failure doesn't show here. I did not check origin itself.

**A/B** (one pooled batch, 22,000 jobs, 32 threads, under the lock; fresh seeds 39000000 / 39200000 / 39400000 / 39600000; play settings; net = slower − faster, negative is better):

| deck | pairs | slower | faster | net | CPU on/off, identical games |
|---|---|---|---|---|---|
| Maze's End play (d5 b20) | 5000 | 2 (g2549, g3226) | 3 | **-1** | 1.011 final (1.017 before the cost cuts) |
| Maze's End 2HG | 2000 | 1 (g546) | 1 | **0** | 1.010 final (1.013 before) |
| Merfolk Turns | 2000 | 0 (all identical) | 0 | 0 | 1.002 |
| Goblins | 2000 | 0 (all identical) | 0 | 0 | ~1.00 |

The final CPU figures come from a 1,500 + 600-pair Maze subset. Search work is unchanged on identical games, so the remaining ~1% is the cost of checking each payment for a strand.

The slower games at d8 b0, off → on:
- **g2549:** T6 → T6, identical digest.
- **g3226:** T5 → T5.
- **g546 (2HG):** T7 → T7, identical digest.

**Not done:**
- I could not write `logs/baldur-fed/report.md`: the harness blocks subagents from writing report files. The mechanism, fix, per-deck nets, CPU and slower-game verdicts above are its content. The A/B and verification outputs and the A/B scripts are committed under `logs/baldur-fed/`.
- The four faster games were not rechecked at d8 b0. I stopped that run because they aren't part of the adoption bar.
- No ground truth was accepted. This sits on the maze-rank3 rank work and should land with it, with smoke and regression GT accepted then.
- Nothing went to `phase-1-2-deck-analyzer`.
- The A/B ran inside the 12:30–20:00 UTC window. I have no timer tool, so if you launch further long runs from here, arm the 2-hour status timer.

Scratch binaries and batch outputs are in `/workspaces/MagicDeckTester/build/scratch/baldur-fed/`; the final binary is `bin3/`.
