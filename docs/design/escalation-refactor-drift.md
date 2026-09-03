# Escalation-refactor drift vs regression GT (ROOT CAUSE FOUND + FIXED)

**Status (2026-07-17, RESOLVED):** The uncommitted escalation-logic refactor in `src/ai/` changed default
play for the value-leaf decks (TH, antilife, hinata) vs the committed regression GT. **Root cause pinned and
fixed:** it was NOT a subtle refactor bug — it was a single **non-gated default flip**. The escalation budget
knob `s_fresh_frac` (`MTG_ESCALATION_FRESH_FRAC`) defaulted to `1.0` (a FRESH FULL budget for the heuristic
escalation) instead of `-1.0` (the legacy shared-REMAINING budget == committed GT). One line at
`TurnSolver.cpp` ~6708 gated on its *default*, not on an env var being set, so every default run drifted.

**Fix applied:** default flipped `1.0 -> -1.0` (escalation budget OFF = legacy shared-remaining). The fresh-
full budget is now OPT-IN via `MTG_ESCALATION_FRESH_FRAC=1.0`, preserved as an UNADOPTED improvement candidate.

**Confirmation (all three decks snap to EXACT GT):**

| deck (d5 s2002)     | drift (fresh on) | GT       | fixed default | env `-1` |
|---------------------|------------------|----------|---------------|----------|
| TH 300g             | 4.11371          | 4.11037  | **4.11037**   | 4.11037  |
| antilife 250g       | 4.61885          | 4.63115  | **4.63115**   | 4.63115  |
| hinata 100g         | 5.85556          | 5.87778  | (load-skipped)| 5.87778  |

`build_xover/Release/mtg` = the fixed-default binary (incremental rebuild, only `TurnSolver.cpp` changed).
The crossover/value-leaf-table work (Feature A) is DONE and separate (see below); this drift was 100% the
escalation refactor (Feature B) and is the only non-gated behavior change in the whole uncommitted tree.

## What is DONE (not this problem)
- **slivers c=3 crossover override** shipped and validated. The value model undervalues Aether Vial (a no-P/T
  enabler), so the value-leaf's committed-3 line is worse in play than the aggregate table LP implies, making
  the derived `hc*[3]=2` one notch too high (+0.0004 LP vs uniform, on trained AND held-out seeds; 5/5 flip
  games show the same T1 Vial-vs-creature pattern). Fix = manual override `c=3: 2->1` (== validated uniform,
  zero downside; isolation `scripts/xover_cell_isolate.py` proved c=4/c=5 need NO override). Mechanism: the
  writer (`valueleaf_table_to_metadata.py`) preserves `value_fallback_crossover.manual_overrides` across
  re-finalization, keeping `value_leaf_table` a pure measurement. Depth-5 keep-leaf is retained via
  `value_trust_depth=5` (committed>=trust never escalates), untouched by the override.

## The drift (this problem) — DEFINITIVELY isolated
Same TH deck + metadata, two binaries:

| binary            | metadata      | TH d5 s2002 | verdict |
|-------------------|---------------|-------------|---------|
| **HEAD-clean**    | HEAD (no xo)  | **4.11037** | == GT   |
| working (dirty)   | HEAD (no xo)  | 4.11371     | drift   |
| working (dirty)   | working (+xo) | 4.11371     | drift   |

So: **the drift is 100% the uncommitted `src/ai/` refactor** — NOT the crossover metadata (inert for TH; forcing
`MTG_VALUE_TRUST_OFFSET=3` still gives 4.11371), NOT committed code (HEAD-clean reproduces GT exactly on TH
4.11037, antilife 4.63115, hinata 5.87778). Direction is mixed: TH worse, antilife/hinata slightly better.

## What was ruled OUT by reading the diff (all byte-identical with flags off)
- `MTG_LEAF_CACHE` (AIEngine `fd_tt` -> `m_shared_tt` when off), `MTG_ESC_SPLIT` (`probe_budget`/`esc_budget`
  == `budget`), `MTG_ESC_PREDICT`, `MTG_ESC_SINGLE`, `MTG_ESC_JUMP`, `MTG_ESCALATION_GATE`, `MTG_LEAF_VERIFY`,
  `MTG_TT_STATS`, `s_rollout_stats` — all env-gated, default off.
- Start-gate (`kStartGateAlpha=1.10`, `kDefaultGrowth=6.0`, estimate condition) — identical to HEAD.
- `FullSearchLine` pass loop, `FSLineWin` — identical except gated counters (`g_fs_leaf_evals`,
  `g_interior_nodes`).
- `SimulateToEnd` -> `SimulateToEndImpl` split — wrapper stores `tt->Store(key,result)` same as HEAD.
- `TranspositionTable.h`, `MulliganProfile.h` — additive only.

## ROOT CAUSE (found by reading the non-additive `-` lines, confirmed by a one-env-var A/B)
The bisect was NOT needed. Filtering the diff to REMOVED/CHANGED lines (`git diff | grep '^-'`) — pure
additions can't drift — surfaced exactly one behavior-changing edit in `FullSearchLineHybrid`: the escalation
budget source. HEAD gave the heuristic escalation the shared REMAINING budget (`esc_budget = budget`); the
working tree defaults `s_fresh_frac = 1.0` and so allocates a FRESH FULL `budget_ms` (`TurnSolver.cpp` ~6708).
This is the "deferred fresh-budget fix" (overnight-audit-2026-07-11) that got wired in as the DEFAULT but was
never gated/regression-passed. Confirmed: `MTG_ESCALATION_FRESH_FRAC=-1` on the working binary restores GT to
5 decimals on all three decks (TH 4.11037, antilife 4.63115, hinata 5.87778) — see the table at the top.

Why reading it the first time missed it: the earlier "ruled OUT" pass treated `MTG_ESCALATION_FRESH_FRAC` as
"env-gated, default off" like the others — but its default (`1.0`) is the ON path, so it was NOT off. The tell
was that it gates on the *default value*, not on `getenv(...) != nullptr`.

## Resolution (applied)
Flipped the default `1.0 -> -1.0` (legacy shared-remaining == GT). Escalation-budget shaping is now OFF by
default; the fresh-full budget is opt-in (`MTG_ESCALATION_FRESH_FRAC=1.0`). This is decision option **1**
below (make the refactor byte-identical to HEAD with flags off). The fresh-budget stays as decision option 2's
candidate for a later, properly-validated adoption.

Reproducer + binaries (kept):
- `build_xover/Release/mtg` = fixed-default working-tree binary (Feature A + Feature B-off).
- HEAD-clean worktree binary: `/tmp/mtg-headclean/build/Release/mtg` (== GT).
- `<bin> decks/treasure_hunt/treasure_hunt.txt --depth 5 --seed 2002 --games 300 --max-turns 8 --budget-ms 20`
  -> 4.11037 (GT/fixed) vs 4.11371 (drift, fresh-budget on).

## Decision options (option 1 taken; option 2 is a future candidate)
1. **[DONE]** Make the refactor byte-identical to HEAD with flags off (the fresh-budget default is now OFF).
2. **[CANDIDATE, needs approval]** Adopt the fresh-full escalation budget as default: single-seed A/B shows
   antilife/hinata better (+~0.01-0.02 LP) but TH slightly worse (4.11037->4.11371) and ~+16% wall on hinata.
   Adopting requires a full-regression A/B (train + held-out) + PERFORMANCE measurement (quality-neutral can
   still be a perf regression) + user approval + a deliberate GT rebaseline. Turn on with
   `MTG_ESCALATION_FRESH_FRAC=1.0` for that A/B.

## SEPARATE THE TWO (post-compaction task — the uncommitted tree intermingles two features)
Context: a prior (pre-compaction) session of THIS agent wrote both features into the same dirty tree. They must
be split so the clean one can commit and the drifting one can be finished independently.

**Feature A — value-leaf per-depth crossover (CLEAN, keep, small).** Replaces the uniform "committed-3"
fallback with the measured per-committed-depth `hc*[c]`. Understood + validated: inert for burn/knights/TH/
antilife, HELPS hinata (weak leaf), and slivers is handled by the `c=3` manual override. Files/hunks:
- `MulliganProfile.h`: `value_fallback_take_at`, `value_no_fallback` fields + `FallbackTakeAt()`.
- `MulliganProfileIO.h`: the crossover loader (committed_depths/take_heuristic_at_hdepth -> 1-indexed lookup).
- `TurnSolver.h`: the 2 extra `FullSearchLineHybrid` params (`value_no_fallback`, `value_fallback_take_at`).
- `TurnSolver.cpp`: ONLY the take-decision block (the `if (!value_fallback_take_at.empty() ...) taken =
  hcommitted >= take_at[c]` branch, ~lines 7013-7033) — apply it onto HEAD's SIMPLE escalation, NOT the +617
  refactor.
- `AIEngine.cpp`: ONLY the two args passed to `FullSearchLineHybrid` (drop the `fd_tt`/leaf-cache line).
- metadata: `decks/*/*.value.json` `value_leaf_table` + `value_fallback_crossover` (+ slivers `manual_overrides`).
Expected result: byte-identical to GT except hinata (better -> deliberate GT rebaseline) + slivers (override -> GT).

**Feature B — escalation/rollout refactor (HAS THE DRIFT, quarantine + finish).** Files/hunks:
- `TurnSolver.cpp`: the +617 lines — K-predictor (`MTG_ESC_PREDICT`), `MTG_ESC_SPLIT`/`esc_budget`/`probe_budget`,
  `MTG_ESC_SINGLE`, `MTG_ESC_JUMP`, `MTG_LEAF_VERIFY`, `s_rollout_stats` counters, the `SimulateToEnd`/
  `SimulateToEndImpl` split, the probe/ladder restructuring. **The non-gated drift lives here.**
- `AIEngine.cpp`/`AIEngine.h`: the `m_leaf_cache`/`fd_tt`/`Clear()` game-persistent leaf cache (`MTG_LEAF_CACHE`).
- `TranspositionTable.h`: `Clear()`, `StatsOn()`, leaf-cache/stats additions.

**How to execute the split** (fresh worktree, HEAD-clean == GT already exists at `/tmp/mtg-headclean`):
1. Start from HEAD-clean. Apply ONLY Feature A files/hunks. Rebuild; run regression -> expect PASS for
   burn/knights/TH/antilife/slivers(with override), hinata changed-for-better (rebaseline decision). This
   proves Feature A is clean and isolates it for commit.
2. Feature B stays in the working tree (or a branch) until the non-gated drift is bisected/fixed per the section
   above. Do NOT commit B until it passes regression byte-identical-with-flags-off (or its new behavior is
   validated + GT rebaselined).
