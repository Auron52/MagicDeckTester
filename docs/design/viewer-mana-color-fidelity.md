# Mana-color fidelity: multi-color sources must not pay off-color pips (viewer issue #6)

Status: **DEFERRED — needs GT rebaseline + MTG-rules review (discuss before implementing).**
Owner decision required: this changes ground truth across every dual-land deck.

## Problem (as reported)

In the Play viewer, casting **Marshal of Zhalfir** (`{W}{U}`) off a board of 3× **Tournament Grounds**
(+ a Plains) was graded **`legal_not_enumerated`** — i.e. "rules-legal, the search just didn't
enumerate it." The user correctly notes that cast is actually a *mistake*: Tournament Grounds makes
only **W, R, or B**, never **U**, so the line is genuinely **unpayable** and should be rejected
(`illegal`), not shown as legal-but-unenumerated.

Saved reject-artifact: `logs/play/rejections/Knights_cod_s29_gi28_t4.json` (turn-4 pre_main).

## Root cause

The accounting mana pool collapses **every** multi-color source to a single wild (any-color) mana:

- [src/core/SpellEffects.h:2830](../../src/core/SpellEffects.h#L2830) — `AddSourceToPool`:
  `else if (!prod.empty()) { pool.wild += amt; }` (a source with >1 produced color → `pool.wild`).
- Payment then drains `wild` for **any** colored pip:
  [SpellEffects.h:2901-2905](../../src/core/SpellEffects.h#L2901) — `drain(cost.white, reserve.wild)`,
  `drain(cost.blue, reserve.wild)`, … So one `wild` from Tournament Grounds pays `{U}`.

Card data already carries the *correct* color set — Tournament Grounds `produces:["W","R","B"]` — but
the flat `ManaPool` (fields: `colorless, wild, white, blue, black, red, green`) has no way to represent
"1 mana usable as W/R/B but not U/G," so the model uses full `wild`. This is the documented
"wild dual over-fixes blue" approximation (see the Tournament Grounds oracle note in `cards.json`, and
memory `mm6-adoption`/`rollout-ramp-and-fetch-layers`). It affects the search's affordability sim, the
executor, and the viewer's `--validate-line` (`CheckLine`) uniformly — hence the wrong verdict.

## Fix options

1. **Color-masked "restricted wild" in `ManaPool` (recommended).** Add a small fixed set of
   restricted-wild buckets keyed by color-mask (e.g. a `std::array` or a `{mask,count}` list) so a
   dual/tri land contributes 1 mana bearing its exact color mask. Update the payment drains so a
   colored pip may consume: its own color → a restricted-wild whose mask includes that color → full
   wild. This is the faithful model and fixes search, executor, and viewer together.
   - Touch points (keep search↔executor lockstep, per memory `mana-float-fix`): `ManaPool`
     (`src/core/ManaPool.h`), `AddSourceToPool` + `DrainCombined`/`TapForCost*`
     ([SpellEffects.h:2809,2884](../../src/core/SpellEffects.h#L2809)), `BuildPool`/`BuildAvailableMana`
     ([TurnSolver.cpp:241](../../src/ai/TurnSolver.cpp#L241), [AIEngine.cpp:2578](../../src/ai/AIEngine.cpp#L2578)),
     `CanPay`.
2. **Matching-based `CanPay` for colored pips.** Keep the flat pool for totals, but add a bipartite
   feasibility check (sources→colored-pips honoring each source's color set) gating affordability.
   More localized to `CanPay`, but the flat pool still mis-accounts during greedy payment, so it can
   diverge from the feasibility gate; trickier to keep lockstep.
3. **CheckLine-only color gate (viewer-scoped stopgap).** Add a strict color-coverage check *only* in
   `TurnSolver::CheckLine` so the viewer verdict for Marshal-off-Tournament-Grounds becomes `illegal`,
   leaving the core model (and all batch GT) unchanged. Lowest risk / no GT churn, but creates an
   inconsistency (the autonomous search would still "play" the over-fixed blue in batch), so it's a
   band-aid on the verdict, not a real fix. Acceptable interim if a full model change is out of scope.

**Recommendation:** Option 1 for correctness; ship behind an escape hatch (`MTG_LEGACY_WILD_DUAL`,
default = new correct behavior, `=1` restores today's wild model) so the change is auditable and
byte-identical when off. If we want to unblock the viewer verdict *without* GT churn first, land
Option 3 now and schedule Option 1.

## GT / risk

Heaviest item in the viewer-issues batch. Option 1/2 change payability for **every deck with
restricted-color duals** → the full regression suite will diverge. Sequence per
`.claude/skills/regression-testing.md`: implement behind the flag, run the suite once, per-game audit
the diffs (never `--accept` on aggregate — memory `dont-rerun-suite-to-accept-gt`), regenerate
affected deck profiles/mulligan tables (Knights, and any dual-heavy deck) on a frozen commit, then
`--accept`. Read `.claude/skills/mtg-rules.md` (mana abilities, mana restrictions) before/after.

## Verification

- Re-drive Seed 29 Game 28 turn-4 Marshal line in the viewer → verdict now `illegal` (unpayable
  `{U}`), not `legal_not_enumerated`.
- Add a unit/scenario: Tournament Grounds ×3 + Plains cannot pay `{W}{U}`, but can pay `{W}{R}`/`{W}{B}`.
- Confirm 5-color lands (Secluded Courtyard, Unclaimed Territory `produces` all five) still pay any
  color (they're genuinely wild). Run smoke; audit the diff before `--accept`.
