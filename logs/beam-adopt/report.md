# Beam adopt: SG50 only form, admission arm, K verify (agent report, 2026-11-25)

I finished tasks 1, 4, 5 and 6. I stopped Task 2 and Task 3 because their conditions failed. Without K, the new beam form is not cost-neutral, which changes the cost picture from the earlier report. Branch `wip/beam-staged` is pushed at **713376bb4**. Nothing of mine is still running.

**Commits**
- **2fcc18e92**: the engine change and the admission stage.
- **1b50ff9fa**: the data, scripts and a comment fix.
- **713376bb4**: the slower-game verdicts.

## Per task
1. **SG50 is the only beam form.** The k-th plan beyond W is admitted while Used < 0.5^k × Limit, and nothing is cut at unlimited budget. Deleted:
   - the fixed-width cut and the flat-share stagings;
   - `MTG_ESC_BEAM_STAGED`, `_STAGE_FRAC`, `_STAGE_GEO`, `_NONE`, `_STATIC_ALL`, `_D5_ALL`, and the `MTG_ESC_BEAM` / `_LEAFDEPTH` / `_STATIC` env overrides;
   - the `esc_beam_stage_*` manifest keys.

   No flag selects a beam shape now. Cache keys fold the same values the SG50 arm folded. Six reproduction jobs (1,500 games) match the earlier report's SG50 arm game for game, win turn and decision digest.
2. **The d3 W20 regime is kept.** Without K, removing it keeps every win turn on 1,200 d3 b10 games. It is not byte-identical, though: 21 games take a different line to the same turn (CG 4, Knights 4, Vampires 13, Giants 0). The regime is now staged like everything else. On CG/AL/DS d3 rows the final binary plays identically to today (net 0, units ×0.99–1.00).
3. **K stays a Cyclone opt-in.** I found no USER ruling keeping it opt-in. QC2 is open with "ON" recommended, and `cyclone-completion-review.md` lists NOM2 among the "user-owned" choices. VERIFY at unlimited budget did not come back clean:
   - The census found 31 decks where K arms.
   - The pooled VERIFY batch had 9 mismatches in 22,856 verified rollouts. In each one the kill-only run missed a win at the cutoff that the full run found.
   - Per-deck runs put all of them on **Maze's End**: 6 of 1,581 with K on, 0 with K off. The other 30 decks had 0.
   - I have not root-caused it. The default-ON patch is saved as `logs/beam-adopt/k_default_on.patch`.
4. **Play-depth gate.** AIEngine now passes the deck's `beam_width` only when the search depth equals `target_depth`. Off its play depth, a deck with a driving block passes 0, which keeps the d3 regime. The `depth == 5` switch is gone.
5. **Admission arm in `play_setting_stage.py`.** It adds one arm in the same batch: live with its beam flipped. The baseline is no beam and the picking rule is `vlq_adopt_pick`'s. `--write` writes `beam_width 3` / `beam_leafdepth 2` when the beam is admitted and removes both keys otherwise. The beam is measured at the live setting and applied to whichever setting the grid installs. The new `test/play_setting_beam_selftest.py` passes 8/8, including an end-to-end check through `vlq_adopt_pick`. I did not run the stage on any deck.
6. **Build and unit suite.** Release build is clean and the unit suite passes 4163/4163. The tested binary and the committed tree differ only by one comment fix.

## A/B: final binary vs the earlier report's baseline (fresh seeds 49.1M / 49.9M)
Net = slower games minus faster games; negative is better.

| row | n | net | faster / slower | t | units F/B | cpu F/B |
|---|---|---|---|---|---|---|
| CG play | 5000 | **-13** | 22 / 9 | -2.06 | ×1.033 | ×1.007 |
| CG 2HG | 2000 | -1 | 9 / 8 | -0.24 | ×1.048 | ×0.939 |
| AL play | 5000 | +1 | 0 / 1 | +1.00 | ×1.019 | ×1.009 |
| AL 2HG | 2000 | 0 | 1 / 1 | 0 | ×1.023 | ×1.006 |
| DS play | 5000 | -3 | 5 / 2 | -1.13 | ×1.056 | ×1.036 |
| DS 2HG | 2000 | 0 | 0 / 0 | 0 | ×1.063 | ×1.099 |
| CG / AL / DS d3 b10 | 2000 each | 0 | 0 / 0 | 0 | ×0.99–1.00 | ×0.99–1.00 |

- **Totals:** -15 play, -1 2HG. That is weaker than the first seeds (-34 / -14).
- **Cost:** units rise by 2–6%. The earlier report's "×1.00 / ×0.98" was the SG50 arm *with* K, whose savings offset SG50's extra cost. Its own SG50-without-K rows also show ×1.01–1.05. With K still opt-in, adopting SG50 is a small quality gain at a small CPU cost, which runs against your "cut CPU" rule.
- **Cross-run timing:** the two arms ran back to back on separate binaries, so the cpu column is only a rough guide; units are deterministic.

**Slower games:** all 21 recover at `--depth 8 --budget-ms 0`. Under `MTG_NC_SEARCH=1` both binaries also give the same turn on all 21, so none is real.

## Questions for the user
- **Adopt SG50 without K?** It is -15 / -1 net at +2–6% units on CG/AL/DS. My recommendation: adopt it together with K once Maze's End is fixed, since K was what made it cost-neutral.
- **Maze's End breaks K.** Its NOM2 parity check passes but the results differ, so either K is fixed for Maze's End or it stays opt-in. The likely line of attack is a same-round win path the kill-only skip does not see; this is not confirmed.
- **Delete the d3 regime anyway?** It never changes a win turn in these samples, but it is not byte-identical.
- **Creature Giving's beam is still open with you.** No profiles were changed.

Other notes:
- `gh` is not authenticated here, so I could not watch the Windows CI run triggered by the `src/` push.
- The d3 code comment cites `logs/beam-adopt/report.md`; it needs to be committed from this message.

Files are in `/workspaces/MagicDeckTester/.claude/worktrees/beam-staged/`:
- `logs/beam-adopt/ab_table.txt`
- `logs/beam-adopt/slower_verdicts.txt`
- `logs/beam-adopt/kverify.txt`
- `logs/beam-adopt/kcensus.txt`
- `logs/beam-adopt/d3chk.txt`
- `logs/beam-adopt/k_default_on.patch`

Raw run output is under `/workspaces/MagicDeckTester/build/scratch/beam-adopt/`.
