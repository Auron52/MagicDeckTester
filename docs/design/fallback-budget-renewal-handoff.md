# HANDOFF — escalation fallback tuning: budget renewal + single-depth fallback

**Status (updated 2026-09-03): DONE / SUPERSEDED** — both levers were measured and ship as
per-deck `value_play` params: `escalation_cap` (eded828b, 2026-07-19) and
`escalation_fresh_frac=0.5` on antilife/hinata/dragonstorm. `scripts/esc_fallback_ab.py` no
longer exists. Nothing to pick up.

**Status: SET UP, not yet measured on real tables. Pick this up next session.**
Self-contained resume doc (2026-07-18). Everything below is on branch `d0-dynamic-model`, **uncommitted**.

## What this task is

Tune the two escalation-fallback levers on the HEAVY decks (antilife, hinata) — the only decks that actually
escalate. "Escalation" = when a value-leaf line committed *below* its trust depth falls back to a heuristic
re-search. Light decks (burn trusts at d6, th/knights/slivers at d5) barely escalate, so these levers are inert
for them.

1. **Budget renewal — `escalation_fresh_frac`** (`MTG_ESCALATION_FRESH_FRAC`, or per-deck via
   `value_play.escalation_fresh_frac`). The escalation re-search runs on the *shared remaining* budget by
   default (`-1` = legacy). Fresh-budget gives it a *fresh* `frac × budget_ms` instead — fixes the case where
   the value-leaf probe eats most of the budget and starves the heuristic re-search to ~d1. Measured earlier as
   a candidate: antilife/hinata **+~0.01–0.02 LP** but **~+16% wall** on hinata; TH slightly worse. UNADOPTED.
2. **Single-depth fallback — `MTG_ESC_SINGLE`** (+ `MTG_ESC_SINGLE_OFFSET`, default 0; offset=2 targets the
   crossover-min depth). Instead of re-running the whole 1..depth heuristic ladder on escalation, run ONE
   heuristic pass at `committed − offset`. Concentrates the reserved budget, skips the shallow-pass rework; its
   overrun-abort doubles as the crossover-skip. Env-only, UNADOPTED.

Code: both are read in `TurnSolver::FullSearchLineHybrid`, `src/ai/TurnSolver.cpp` ~6716–6736 (fresh_frac,
`eff_fresh_frac`) and ~6717 (`s_esc_single`, `s_esc_single_off`). `escalation_fresh_frac` is a param on that
function, passed from `AIEngine.cpp` as `value_play.drives() ? value_play.escalation_fresh_frac : -2.0`.

## Provisional signal (why this is worth doing)

A 2-seed × 40-game plumbing run on antilife's PROVISIONAL table:
- baseline: LP 4.1374 @ **445 ms/game** (antilife escalates heavily at d5 → slow).
- **`MTG_ESC_SINGLE` (single-depth): ~35% FASTER (−150 to −165 ms/game) at neutral quality.** Big perf lever.
- fresh_frac 0.5/1.0: neutral quality, slightly slower (+3 ms) — it *adds* budget, as expected.

Not conclusive (`dLP=0` on 40 games isn't a quality verdict), but the single-depth speedup is striking.

## Exactly how to resume (next session)

1. **Prereq — real tables.** antilife needs its gap-fill (H6 redo + H7/H8; see the mulligan/value-leaf
   generation notes) done; hinata needs its mulligan profile (generating on the OTHER machine, slow — don't
   block antilife on it). The escalation behaviour depends on table quality, so measure on the REAL table.
2. **Run the A/B** (harness already built): `scripts/esc_fallback_ab.py <deck>[,<deck>] [nseeds] [games]`,
   deck ∈ {antilife, hinata}. Default 6 seeds × 300 games. Runs each deck BARE (its enabled value_play block
   drives d5/b20) with the levers via env; paired per seed; reports per-variant `dLP` (quality) + `dms/game`
   (speed) vs baseline. Variants: baseline, fresh0.5, fresh1.0, single_off1, single_off2, fresh1_single2.
   Logs to `logs/eval/esc_fallback_ab.log`.
   - e.g. `python3 scripts/esc_fallback_ab.py antilife 8 400`  (antilife first, after gap-fill).
3. **Interpret.** Primary objective = avg win turn (LP), loss=max_turns+1; lower better. A lever WINS if it is
   quality-neutral-or-better AND faster (single-depth looks like this) OR clearly better quality at acceptable
   wall (fresh-budget's +0.01–0.02 LP — weigh vs its wall cost). Validate the winner on held-out seeds (the
   harness already uses held-out 4004+; add more seeds for the winner).
4. **Adopt per-deck (on a win).** Budget renewal is ALREADY per-deck wired: set
   `value_play.escalation_fresh_frac` in the deck's `<deck>.value.json` block (e.g. `1.0`). Single-depth is NOT
   yet a value_play field — if it wins, mirror the fresh_frac wiring: add `single_depth`/`single_offset` to
   `ValuePlay` (`MulliganProfile.h`), parse in `MulliganProfileIO.h`, thread through
   `FullSearchLineHybrid` like `escalation_fresh_frac`, and gate on `value_play.drives()`.
5. **GT.** Adopting a lever CHANGES the heavy-deck play → re-run smoke+regression, inspect per-game, `--accept`.

## Enabling change already made (so the A/B works)

`TurnSolver.cpp` ~6711: added `s_fresh_frac_env_set`. An explicitly-set `MTG_ESCALATION_FRESH_FRAC` env now
WINS over the block's `escalation_fresh_frac` (else the block's `-1` would mask the env). Env-unset ==
byte-identical (verified: antilife bare 4.588 vs fresh_frac=1.0 4.577). Precedence: env(explicit) > block > -1.

## Surrounding state (context a fresh agent needs)

- **value_play is ADOPTED + byte-identical + GT green** (smoke 18/18). All 6 decks have enabled value_play
  blocks: burn `{d6,b20}`+`value_trust_depth=6` (~10% faster, identical play); th/knights/slivers/antilife/hinata
  `{d5,b20}` (byte-identical; antilife/hinata marked PROVISIONAL, revise after their tables). Full design in
  `docs/design/value-leaf-fallback-table.md` ("SETTLED DESIGN" + the empirical sections).
- **Depth is a locked policy, budget is a free resource knob**: `--depth` on an enabled block errors (unless
  `--ignore-play-profile`); `--budget-ms` overrides just the budget, keeping profile depth. Heavy-deck A/B uses
  env for the escalation levers, not --depth, so no conflict.
- **Binary**: `build/Release/mtg` (rebuilt with all changes). The regression harness uses it.
- **Uncommitted**: value.json blocks (6), `test/regression.sh` (manifest gen), `test/explain_game.py`,
  `test/classify_turn_later.sh`, `src/ai/{MulliganProfile.h,MulliganProfileIO.h,AIEngine.cpp,TurnSolver.cpp,
  TurnSolver.h}`, `src/main.cpp`, `src/runner/BatchRunner.cpp`, `scripts/esc_fallback_ab.py`, this doc + GT
  (`test/regression_gt.txt`, `test/gt_logs/`). Plus the PRE-EXISTING crossover/escalation-refactor tree (now
  baked into the accepted GT). Nothing committed — the user has not asked to commit.
- **Overnight GT is STALE** (only smoke+regression were re-accepted). Run + accept overnight before relying on
  its numbers.
