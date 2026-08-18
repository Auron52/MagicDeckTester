# An {X} cast variant the search enumerated, pushed, and never once evaluated

**Status:** FIXED 2026-08-18. `EvalCard` + the `{X}`-trick emitter in `src/ai/TurnSolver.cpp`.
Guarded by `test/scenarios/libation_x_lands_not_dorks.json`.

## The bug

`Action::eval` is the static ranking key that decides which plans the search actually expands. For
the solo-target trick branch, `EvalCard` estimated Luxurious Libation's `+X/+X` with a **constant**:

```cpp
pump += tp.pump_per_x_power * 2;      // "mid" magnitude; X not yet chosen
```

Each candidate X is its own plan variant sharing one `hand_index`, so every variant received the
**same** eval — while the larger-X variants cost more mana. Plan ordering therefore could not tell
X=4 from X=0, and always preferred the cheap one.

The constant was not obviously wrong. Its comment argued, correctly in the abstract, that the
ordering heuristic runs *before* X is chosen and that "the SEARCH owns the real valuation". That is
true of the FIRST call (enumeration order) and false of the per-variant call: by the time an Action
exists, X *is* fixed, and refusing to score it makes the variants indistinguishable.

## Why it was invisible

The variant was not missing at any stage a normal audit checks. Instrumenting the pipeline on the
fixture board showed it present at every step and absent only at the last:

| stage | X=0 | X=4 |
|---|---|---|
| emitted by the enumerator | yes | **yes** (cost `{4}{G}`, correct) |
| reached `EnumeratePlans` candidates | yes | **yes** (same group key) |
| survived every subset filter (`eval_and_push`) | yes | **yes** |
| pushed onto the plan list | yes | **yes** |
| ever APPLIED / scored | yes | **never** |

So the line was legal, affordable, enumerated and stored — and no rollout ever ran it. A win-turn
fixture alone cannot distinguish that from "the option does not exist"; only probing application
separates them.

`MTG_UNPRUNED` does NOT rescue it. Opening the full `1..max` range adds more variants that share the
same blind eval, so the fixture fails unpruned too — a useful negative result, because "unpruned
still loses" reads like a genuine evaluation preference rather than a bug.

## The fix

`EvalCard` takes `int chosen_x = -1`; the term becomes

```cpp
pump += tp.pump_per_x_power * (chosen_x >= 0 ? chosen_x : 2);
```

and the `{X}`-trick emitter passes `xv`. `-1` preserves the old constant for every other caller, and
a non-`{X}` trick passes 0 into a term whose coefficient is 0 — smoke 36/36 byte-identical.

The `{X}`-burn path never had this bug: it sets `a.eval = x * mult * 100` directly. **That is the
convention — an {X} variant's eval must scale with its X.**

## Measured effect

`Mirrorwing Trick Suite`, 2,000 games, seed base 940000, before → after:

- X distribution: **100% X=0** (1,104 casts) → 55.6% X=0, 44.4% spread over X=1..8 (1,050 casts).
- X now scales with the turn: X=0 at mean turn 3.75, X=3 at 4.38, X=5 at 5.23, X=8 at 7.00 — the
  two roles the user described (early "make bodies", late "final pump to close") both appear.
- Mean win turn **5.0600 → 5.0275**.

## Follow-up: X must be "max board power GIVEN THE REST OF THE PLAN"

Even with the eval fixed, the provider's candidate set was `{0, max_affordable}`, where
`max_affordable` assumes the spell gets ALL the non-dork mana. An X>0 variant could therefore only
ever appear in a plan that casts nothing else, so a `{G}` Fortifying Draught and a large-X Libation
could not co-occur: the odometer's only pairing was Draught + X=0.

The fix reuses machinery that already existed rather than adding candidates (user, 2026-08-18):
`FillScaledCastFace` pours a subset's LEFTOVER mana into a cast that scales, *after* the subset's
cost is known. `FillScaledXTrick` is its sibling for `{X}` pump tricks, with two differences:

- **dorks are exempt** — the surplus is reduced by the untapped mana-dork count before any of it
  goes into X, because a dork tapped for X cannot attack and wastes its own +X/+X;
- **it returns an EVAL delta, never face damage** — Magma's fill feeds `direct_dmg` because face
  burn really hits the opponent, whereas +X/+X is board power that still has to connect. Folding it
  into `direct_dmg` would fabricate lethal.

The provider's all-in candidate stays, so both ends are searched: the fill supplies the middle.

### ...but NOT on a plan that draws

Filling is a commitment made BEFORE the information arrives. A cantrip trick under a magnet
(Impolite Entrance / Fists of Flame, `cast_draw > 0`) mass-draws mid-turn and the engine re-solves
at that draw breakpoint — mana already sunk into X cannot cast what the draw reveals (user,
2026-08-18: "when there are draw spells involved, we do not want to spend it all willy-nilly").

So the fill is skipped when any cast in the subset draws. That still leaves the branch **two**
options, which is all this decision needs: X=0 (hold the mana for what we draw) and the provider's
all-in candidate, chosen by the search rather than pre-committed.

Measured, 2,000 games, same seeds:

| | before fill | fill, unguarded | fill + draw guard |
|---|---|---|---|
| X=0 share | 55.6% | 45.4% | 49.8% |
| Fortifying Draught cast in the same turn as X>0 | 74 | 129 | **107** |
| X>0 rate on turns that ALSO cantrip | 23.6% | — | 26.3% |
| mean win turn | 5.0275 | 5.0270 | **5.0255** |

The guard is not a tax: it improves the mean. Its effect is to concentrate the fill's gain on
non-drawing turns while leaving drawing turns' X>0 rate almost unchanged (23.6% → 26.3%) — i.e. on
those turns X>0 now happens only when the search actively wants it.

The win turn barely moves — this is a FAITHFULNESS fix, not a strength fix, and that is the expected
shape: the lines it unlocks mostly win on the same turn by a different route. Smoke 36/36
byte-identical (no shipped deck has `pump_per_x_power`).

## The general lesson

Three separate defects had to be cleared before this card worked, and each one *fully masked* the
next:

1. `{X}` non-damage spells were dropped from enumeration entirely
   ([[x-spell-tricks-dropped-from-enumeration]]) — the card was never cast.
2. The only X>0 candidate offered was `max_affordable`, which taps mana dorks
   ([[mirrorwing-trick-suite-usage-findings]]) — the offered X was always the wrong one.
3. This bug — the right X was offered and never evaluated.

After (1) and (2) the measurement still read "100% X=0 across 1,104 casts", which looks exactly like
a settled search preference. **A stable, plausible distribution is not evidence that a decision is
being made.** What distinguished them was asserting on a constructed board where exactly one option
wins (only X=3 is lethal there), then probing *where* the option disappeared.
