# SHIPPED: auto-run the per-game audit from the regression harness

Landed 2026-07-02 (user's idea). Steps 1, 2, and 3 are all implemented — see
`test/regression.sh` (auto-audit block + the `--accept` gate) and
`test/classify_turn_later.sh` (the churn auto-classifier). This doc is kept as the rationale
record. What shipped:

- **Step 1 — auto-run on every compare run.** After the fingerprint compare, `regression.sh`
  runs `audit_changed_games.py <mode>` and logs the split-by-depth breakdown + the list of every
  searched-depth flip. No separate step to remember.
- **Step 2 — `--accept` hard-gate.** `--accept` re-runs the audit first and aborts if any
  searched-depth win->loss exists, unless every one is acknowledged via
  `--accept-with-regressions="gi<N>:<reason>; ..."`. The acknowledgement string is written into
  the ground-truth provenance header (`# accepted-with-regressions (...)`).
- **Step 3 — churn auto-classifier.** `test/classify_turn_later.sh <mode>` re-runs each
  searched-depth turn-later game at 4x and 16x its case budget: recovers to the old turn -> `churn`
  (benign search-truncation); persists -> `PERSISTS` (draw-divergence variance if the deck shuffles,
  else a real same-draws slowdown — diff the two lines to decide). Validated on the overnight
  rebaseline's three turn-later games: hinata gi234 and slivers gi638 classified `churn` (recover at
  16x), th gi627 classified `PERSISTS` (Treasure-Hunt/Throes-cascade draw divergence — benign).

Original plan below, kept for context.

---

Self-contained deferred task (2026-07-02, user's idea). **Do after the next compaction.**

## Why

The per-game audit before `--accept` is the step agents keep skipping (a *plausible narrative* for an
aggregate shift substituted for measuring it per game). We just added
`test/audit_changed_games.py` (win→loss / turn-later breakdown split by depth, non-zero exit on a
searched-depth win→loss) and made the skill mandate running it. But a script an agent must *remember*
to run is still skippable. Wiring it into the harness makes it unmissable.

## What to build

1. **`regression.sh` runs the audit automatically at the end of a compare run** (any non-`--accept`
   run of a mode whose GT exists): after the batch + fingerprint compare, invoke
   `audit_changed_games.py <mode>` against this run's `logs/<mode>/wins` and print its output as part
   of the run summary. So every ordinary run already surfaces the per-game flip breakdown — no
   separate step to forget.
2. **`--accept` refuses to promote while a searched-depth `win→loss` is unexplained.** Options:
   - Hard block: `--accept` re-runs the audit and aborts (non-zero) if any searched-depth win→loss
     exists, unless an explicit `--accept-with-regressions "gi306:churn-verified; ..."` acknowledges
     each one (the acknowledgement string is recorded in the commit/GT provenance).
   - Softer: `--accept` prints the audit and requires a `--audited` confirmation flag, so accepting is
     a two-step "I read the audit" act rather than a reflex.
3. **(Stretch) auto-classify turn-later.** For each searched-depth turn-later game, the harness could
   itself run the two cheap checks the skill describes and label the game:
   - *budget churn* — re-run that ONE game at a higher budget; if it recovers to the old win turn,
     label `churn`.
   - *fetch-shuffle variance* — compare the two lines' DRAW sequences (needs the per-game draw log,
     which today we don't commit — would piggyback on the "expand what the harness captures" rule);
     if draws diverge, label `variance`.
   - anything left (draws identical AND doesn't recover at higher budget) → `REAL-SLOWDOWN`, block.
   This turns "classify each searched turn-later" from manual work into a generated table. It costs
   one extra single-game run per turn-later game (cheap: dozens, not thousands).

## Notes / touch points

- `audit_changed_games.py` already prints the split-by-depth breakdown and lists each searched flip;
  step 1 is mostly calling it from `regression.sh` and threading the mode/paths.
- Keep the d0 bar light (sample, don't gate) — that's already how the script behaves and matches the
  user's "less worried about d0" guidance.
- The auto-classify (step 3) is the part that needs the per-game draw log; scope it only if/when we
  decide to commit or regenerate that. The churn re-run half needs no new data.
- See `.claude/skills/regression-testing.md` ("MANDATORY before `--accept`") for the policy this
  automates, and `test/audit_changed_games.py` for the existing gate.
