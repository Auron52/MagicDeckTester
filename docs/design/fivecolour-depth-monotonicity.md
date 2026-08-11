# FiveColour: does a fully-searched arm ever lose? (TABLED, resume overnight)

## The question

The pre-merge FiveColour depth matrix reports `V8=5.0204` as WORSE than `H5=5.0165`.

That should be impossible. FiveColour runs at `max_turns=8`, so a depth-8 search from turn 1 covers
the whole game; the matrix is UNBOUNDED (no budget cutoff); and the search is CLAIRVOYANT by default
--- `GoldFishRunner.cpp:418` sets

```cpp
state.shuffle_salt_search = s_have_salt_search ? s_shuffle_salt_search : s_shuffle_salt;
```

so unless `MTG_SHUFFLE_SALT_SEARCH` is set the search's mid-game reshuffles deal exactly the order
the real game will. (This matters here: FiveColour runs 13 fetchlands plus Progenitus / Nicol Bolas /
Unite the Coalition, so mid-game shuffles are frequent. They are still clairvoyant by default.)

A clairvoyant search with a horizon covering the entire game and no budget should never be beaten by
a shallower one. **So either the measurement is wrong, or there is a search bug** (user, 2026-08-11).

## What the stored matrix can and cannot answer

It cannot answer it. Two independent reasons:

1. **The cells are engine-mixed, asymmetrically.** `matrix.txt.cells.json` records a `src` (tree hash
   of `src/`) per chunk. Three engines appear. `e8c1cce2` (117 games) is symmetric --- every cell got
   it, so it cancels. But `982717d8` (63 games, 3 chunks) appears in **only** `H5 s10010` (19 games),
   `H4 s11011` (22) and `H5 s11011` (22) --- i.e. only in the deep heuristic cells whose advantage is
   the thing in question. The mechanism is structural: the most expensive cells are the least complete
   when the source moves, so contamination lands on them preferentially.

   NOTE: this does NOT by itself prove the engines played differently. Those 19 games are DIFFERENT
   GAMES from the cell's other 375, so comparing their means (4.9048 vs 5.0427) says nothing about
   engine behaviour. The user notes the change was well-tested and should not change play. The
   contamination is a *methodology* defect that makes the comparison unsound, not evidence of a
   play difference.

2. **Only chunk MEANS were ever stored, over non-aligned boundaries.** A monotonicity counterexample
   is ONE GAME where the deeper arm is worse. No mean can show one. Worse, "healing" the mixed cells
   requires carrying a chunk mean into a sub-range (`_split_chunk` / `_chunk_minus`), an approximation
   of the same magnitude as the effect being measured. Both the imputation and the deletion estimates
   below are therefore indicative only:

   | cell | reported | imputed | healed-by-deletion |
   |------|---------:|--------:|-------------------:|
   | H5   |   5.0165 |  5.0194 |             5.0193 |
   | V8   |   5.0204 |  5.0204 |             5.0204 |
   | V6   |   5.0217 |  5.0217 |             5.0216 |
   | H4   |   5.0236 |  5.0249 |             5.0248 |

   Healed `V8 - H5 = +0.0010 +- 0.0061` (paired, 4 seeds) --- noise. But this is NOT a measurement.

## Partial re-measurement (2026-08-11, tabled mid-run)

Re-ran on ONE engine (today's HEAD), seed 10010, UNBOUNDED, per-game win turns via `MTG_DUMP_WINS`.
Cancelled partway at the user's request (shared box during the day). Saved:
`logs/vlq_fc_depthbudget/partial_wins.json`.

| comparison | common games | result |
|---|---:|---|
| V8 vs H5 | 92 | **identical on every game** (0 worse, 0 better) |
| V8 vs V7 | 222 | **identical on every game** |

**No counterexample found --- but the coverage does not yet reach the interesting games.** H5 only got
to games 0..111; it never reached `[375,394)`, which is exactly the contaminated range. V8 covers
`[0,400)` almost fully.

## Confound to control when resuming

`V8 vs H5` differs in BOTH depth and evaluator: the value model is attached to rollout states
(`AIEngine.cpp:1009`), and `BottomCards` (`AIEngine.cpp:1060`) scores bottoming choices by rollout ---
so the V arm also *bottoms differently*, before turn 1. The clean test of depth alone is **H8 vs H5**
(same evaluator). Efficient shape: get V8/H5 per game first, then run H6/H7/H8 only on the games that
diverge (expected to be few), rather than 400 games of H8 at ~2800 s/game.

## Resume recipe

The working files live under `logs/vlq_fc_depthbudget/` (gitignored), so the manifest is inlined
here to keep this doc standalone. Write it to `logs/vlq_fc_depthbudget/monotone.json`:

```json
{"jobs": [
 {"name":"H5_s10010","deck":"decks/FiveColour/FiveColour.cod","games":400,"seed":10010,
  "depth":5,"budget_ms":0,"value_profile":"none","cell":"H5","weight":139200},
 {"name":"V8_s10010","deck":"decks/FiveColour/FiveColour.cod","games":400,"seed":10010,
  "depth":8,"budget_ms":0,"value_profile":"logs/eval/FiveColour.value.STAGED.pre-merge.json",
  "value_min_depth":0,"cell":"V8","weight":4000},
 {"name":"H8_s10010","deck":"decks/FiveColour/FiveColour.cod","games":8,"seed":10010,
  "depth":8,"budget_ms":0,"value_profile":"none","cell":"H8","weight":22400}
]}
```

`budget_ms: 0` = UNBOUNDED (matching the matrix); `value_min_depth: 0` = PURE value leaf, no
escalation; `value_profile: "none"` = explicitly no sidecar (required --- sidecar PRESENCE is what
activates the hybrid, so merely omitting the key is NOT the same thing).

```bash
MTG_DUMP_WINS=1 ./build/Release/mtg --batch logs/vlq_fc_depthbudget/monotone.json --threads 24
```

Then diff per game: parse `[win] job=<j> gi=<i> wt=<w>` from stderr (`wt=-1` means no win, scored
`max_turns+1 = 9`), group by job, and report any game where the DEEPER arm scores worse. One such
game is the counterexample; the mean is not the test.

Run it when the box is free --- a second agent shares this machine, and every `ms/game` figure taken
under that contention is WALL-inflated and unusable for cost comparison (the fault that once
manufactured a false 1.6-3.8x regression).

## Fixed as a result

`scripts/attic/valueleaf_depth_matrix.py` now enforces per-OFFSET engine agreement across cells
(`enforce_offset_src_agreement`), aligns chunk boundaries to a fixed grid so a chunk is a comparable
unit, and prints an `!! ENGINE-MIXED` banner into `matrix.txt` so a contaminated table cannot be read
silently. Validated against the real contaminated state: flags exactly the 41 offsets
(`s10010 [375,394)`, `s11011 [375,397)`), heals to zero, idempotent.
