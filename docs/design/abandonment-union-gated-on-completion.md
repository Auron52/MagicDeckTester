# The abandonment union should be gated on COMPLETION, not taken over every cell

**Status:** deferred, designed, not implemented. Applies to `scripts/attic/valueleaf_depth_matrix.py`
(`apply_skiplist`). Raised by the user, 2026-08-16, during the Mirrorwing phase-C matrix.

## The rule

> Condemnable games should not discard those from other cells unless we are able to complete the
> condemnable cell. — user, 2026-08-16

- **Binds:** every cell that COMPLETES into the table. Its abandoned games are excluded from every
  other cell of its `(deck, seed)`, exactly as today. This is the whole H ladder *and* any V rung
  that survives.
- **Releases:** the *exclusive* abandonments of cells that end up condemned (`intractable` or
  `qdead`) — i.e. games no surviving cell abandoned on its own account.

**The gate is completion, not arm.** An earlier draft of this rule said "H binds, condemnable V does
not." That is wrong, and the user corrected it: *"a complete V6 can cause games to be condemned."* A
V6 that survives is a table participant exactly like H4, and its abandonments must propagate or the
row means stop being over one game set. That H cells always bind is a *consequence* — they are
structurally exempt from condemnation (`NEVER_CONDEMN` clamps to >=5, `HDEPTHS` tops at 5), so they
always complete — not a special case. Encoding it as a special case would get a surviving V6 wrong.

## Why the current behaviour is a defect

`apply_skiplist` unions abandonments over **every** cell. But the consumer — the comparability guard
in `emit_table` — builds its comparison set from cells that are

```python
not c["intractable"] and not c["qdead"]
```

So the union is taken over a strictly **wider** set than the one that reads it. Games are being
withheld from the table on behalf of cells that are not in the table.

The rung verdict does not need the union either: `dead_rung` pairs on `set(dg) & set(sg)`, a pairwise
intersection. A cell missing a game drops out of its own comparison automatically. So releasing a
condemned cell's exclusive abandonments cannot change how that cell was judged.

## Measured size of the effect (Mirrorwing, 2026-08-16, ~81% of a 400x4 matrix)

| | games |
|---|---|
| union of all abandonments | 261 |
| H5's exclusive contribution (binds — H always completes) | 54 |
| **releasable** (exclusive to condemned V6/V7/V8) | **8** (3%) |

Per seed: 4 / 3 / 1 / 0, against ~340 games per cell.

**Why it is small, and why that likely generalises:** a game that chokes V8 almost always chokes H5
too. Hard games are hard in every cell, so a deep-V cell's abandonments are nearly a subset of the
reference's, and its *exclusive* residue is thin. Expect this fix to be a correctness tidy-up rather
than a recovery of meaningful sample — but the residue is not bounded a priori, and on a deck whose
deep-V cells choke on a *different* population than H5 does, it would not be thin.

An earlier figure of 70 games (27%) in this same run was wrong: it classed H5 as releasable.

## Implementation notes

1. **Attribution must be recorded.** The skip list is stored flat as `{(deck, seed): set(offsets)}`,
   which loses *which cell* abandoned each game. The per-cell `.abandoned` files in the wins dir
   still carry it (`<arm><depth>_s<seed>_off<n>.abandoned`), but the state file should record it
   directly so the release survives a wins-dir cleanup.

2. **Separate the two roles the skip list currently plays.** They are not the same set:
   - *never re-queue* — must stay the full per-cell set (a re-queued game just abandons again at
     full cost);
   - *exclude from the table* — should be the union over COMPLETING cells only.

3. **Status is provisional during the run.** A live V6 may still complete, so honour its
   abandonments while it runs and release them only if it is condemned. This is safe because
   `apply_skiplist` is a filter over data already on disk — the applied set may *shrink*, and the
   released games come back with real results intact in every surviving cell. It is also why the fix
   can be applied **retroactively to a finished table without re-running a single game**.

4. **Latched verdicts do not recompute.** `check_quality` runs per batch against the filtered chunks
   and latches into `quality_dead`. If an early-condemned cell dropped games before it died, verdicts
   computed in that window used the smaller sample. Release only ever *adds* paired games, so later
   verdicts get strictly more power, but the latched ones are not revisited. Accepted.

5. **Target accounting.** Released games can push a cell past `--target`; harmless (more data), and
   it makes `--max-skip-frac` strictly less likely to trip. Release applies uniformly to all
   surviving cells of a `(deck, seed)`, so the equal-game-sets invariant the `UNEQUAL GAME SETS`
   guard checks is preserved.

## Not applied to the Mirrorwing run

That matrix was ~81% complete with ceilings already frozen when this was raised, the residue was 8
games, and none of the verdicts in question were close (V6/V7/V8 were condemned on `improvement
+0.0000` equivalence calls at ~260 paired games). Editing the driver's state format under a live run
on a deadline night was the larger risk. Future runs only.
