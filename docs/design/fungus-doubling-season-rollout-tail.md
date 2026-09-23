# Fungus: the Doubling Season keep-rollout tail

**Status: OPEN.** Captured 2026-09-23 during the mulligan generation on `49f45dd2` (Mycoloth
second main adopted). USER: *"we should keep these games and try to understand why they are so slow
after."* The games are preserved; this is the record of what is known and what to do with them.

## 1. The artifact

`decks/Fungus/Fungus.keepmodel.exhaustive.raw.json.slow.log` is written by the generator and
**APPENDS across runs**, so it holds both the pre-adoption run (first 42 lines, commit `b9d891eb`,
single main) and the adopted run (commit `49f45dd2`). Snapshots and the derived tables:

* `logs/fungus_slow_tail/slow.log.snapshot_1914` — the raw log
* `logs/fungus_slow_tail/slow_rollouts_m2run.tsv` — every slow rollout of the adopted run, sorted
  (ms, size, mode, r, seed, hand)
* `logs/fungus_slow_tail/like_for_like.txt` — the matched-pair table below

Each line carries `size / mode / r / seed / hand`, which is the full identity of a keep-rollout --
so any of these replays exactly.

## 2. WHAT THE MATCHED PAIRS SAY, and it is not what the mean said

Eight rollouts appear in BOTH runs with identical `size`, `mode`, `r` **and `seed`** -- the same
physical rollout, before and after. This is a clean like-for-like, not a distributional comparison:

| pre (b9d891eb) | post (49f45dd2) | ratio | hand |
|---|---|---|---|
| 36,822 ms | 35,073 ms | 1.0x | DS x1; SGC x1; Thallid x1; Mycon x4 |
| 53,174 ms | 38,773 ms | 0.7x | DS x2; Forest x2; **Mycoloth x1**; Psychotrope x1; Mycon x1 |
| 50,804 ms | 104,108 ms | 2.0x | DS x3; SGC x1; Thallid x2; Mycon x1 |
| 31,480 ms | 171,429 ms | 5.4x | DS x3; Psychotrope x1; SGC x1; Shell-Dweller x1; Mycon x1 |
| 35,698 ms | 495,333 ms | **13.9x** | DS x3; Forest x2; Thallid x1; Wild Growth x1 |
| 32,006 ms | 661,012 ms | **20.7x** | DS x3; Forest x1; Psychotrope x1; SGC x1 |
| 232,494 ms | 2,096,608 ms | 9.0x | DS x2; Forest x3; Psychotrope x1; Wild Growth x1 |
| 379,313 ms | 2,472,687 ms | 6.5x | DS x3; Forest x2; Psychotrope x1; Wild Growth x1 |

**The second main is 1.47x on the mean and up to 20.7x on this deck's tail.** Both numbers are real;
the 1.47x (fungus-second-main-and-devour.md §7e) was measured over 150 ordinary d1/b3 games and it
does not predict this at all. The one pair that got FASTER is the only one holding Mycoloth.

## 3. THE HAND SIGNATURE

Over the adopted run's 233 logged slow rollouts, the worst 20 by card frequency:

```
19  Doubling Season      13  Psychotrope Thallid       3  Mycoloth
17  Forest               11  Wild Growth               2  Thallid
14  Simic Growth Chamber  8  Utopia Mycon              2  Tukatongue Thallid
```

**Doubling Season in 19 of 20; Mycoloth in 3 of 20.** The tail is a Doubling Season + Psychotrope
tail, not a devour tail -- which the token-search ledger already said before the second main existed
(`fungus-token-search-cost.md`: the worst hands all hold Doubling Season, and the worst replayed at
238 s). What changed is the multiplier on it.

## 4. HYPOTHESIS (untested -- this is the work)

`uses_second_main` is a DECK-level stamp, so the post-combat phase exists on **every turn of every
rollout** whether or not a Mycoloth is anywhere near the hand. Its cost per turn is dominated by a
`GameState` copy plus an enumeration, both of which scale with BOARD WIDTH. Doubling Season doubles
both the Saproling tokens and the spore counters that mint them, so width compounds turn over turn,
and Psychotrope converts spore counters into draws, which open breakpoints and re-enumerate. So the
extra phase's cost should be superlinear in exactly the cells that were already the tail -- which is
the shape the table in §2 has (1.0x on narrow boards, 20x on the widest).

**If that is right, the remedy already exists and is measured**: `MTG_FUNGUS_M2_ROOT` (built,
default OFF, §7e) removes the deferral from PROJECTED turns and keeps it at real decision turns. It
cost 1.07x on the mean and gives up the mean quality gain -- but the quality it gives up was measured
on ordinary games, and it targets precisely the rollout turns where this tail lives. It has not been
measured on these hands.

## 5. HOW TO REPLAY ONE

Every field needed is in the log line. The two to start with are the extremes of the table:

* **20.7x** — `size6 draw r=1 seed=4354685634036052585`, hand DS x3; Forest x1; Psychotrope x1; SGC x1
* **6.5x** — `size7 draw r=0 seed=11400714852857299021`, hand DS x3; Forest x2; Psychotrope x1; Wild Growth x1

Replay each under `49f45dd2` and under `MTG_FUNGUS_M2_ROOT=1`, and under `b9d891eb` for the
baseline. Read the per-turn board width alongside the time: the hypothesis in §4 predicts the ratio
tracks width, not turn count.

## 6. THE STRUCTURAL FIX, independent of the cause

**The refine phase has no per-rollout cap.** One cell held a core for **2h 02m** (7,350,039 ms) in
this run. Whatever the multiplier turns out to be, a single degenerate cell should not be able to do
that -- it converts a bounded generation into an unbounded one and it is invisible until the monitor
line has been flat for an hour. Bound it by REACHABLE STATES rather than by a wall clock where
possible (the labeller precedent), and record a truncation when it bites so the cell is not silently
scored as converged.
