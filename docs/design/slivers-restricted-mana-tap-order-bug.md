# Slivers restricted-mana tap-order bug (exposed by `e6c1f2c`)

**Status:** ROOT-CAUSED (primary), secondary sub-bug scoped. Fix not yet written. 2026-07-25.

## Symptom

On the slivers_vial deck, a handful of searched-depth games (~7/1000 at d3/d5) win **one turn
later** than before commit `e6c1f2c`. The faster (old) win is a fully **legal, all-creature line**
that the current engine **cannot reach at any depth/budget** (verified d3/d4/d5 up to 300 s, and
`MTG_UNPRUNED` / `MTG_SEARCH_ORDER` exhaustive-ish — all still the slow line). So it is NOT search
budget/depth/pruning — it is a deterministic engine mis-step.

This surfaced during the Hinata R=22 mulligan adoption (overnight GT rebaseline) but is **unrelated
to Hinata** — it is pre-existing since `e6c1f2c` (adopted weeks ago). The adopted GT correctly
reflects current (buggy) behavior; when this is fixed, slivers GT rebaselines faster again.

## Faithful reproduction (single game, no batch needed)

`--game-index` is a **no-op** for single-game replay; the faithful reproduction is `--seed = base+gi`.
The representative game is **gi80 of slivers_overnight_d3_s4004 → seed 4084** (old T4, new T5):

```
MTG_DUMP_WINS=1 build/Release/mtg decks/slivers_vial/slivers_vial.txt \
  --profile decks/slivers_vial/slivers_vial.profile.json \
  --seed 4084 --games 1 --depth 3 --budget-ms 2000 --ignore-play-profile
```

Other affected d3 s4004 games (all old T4 → new T5): gi277, gi278, gi314, gi725, gi726
(seeds 4281, 4282, 4318, 4729, 4730).

## What `e6c1f2c` changed

It applied `colored_creature_only:true` **and** added `"C"` to `produces` on four lands —
in the slivers deck: **Sliver Hive** and **Secluded Courtyard**:

```
"produces": ["W","U","B","R","G"]                       →
"produces": ["W","U","B","R","G","C"], "colored_creature_only": true
```

The **card data is correct MTG** (real Sliver Hive taps for {C} plus Sliver-only colors). The bug
is in how the engine's heuristics consume it.

## Isolation (new binary, `--cards-json` variants of the 4 lands; gi80)

| Sliver Hive / Secluded Courtyard | gi80 |
|---|---|
| `[W,U,B,R,G,C]` + flag (current)  | T5 |
| `[W,U,B,R,G,C]`, no flag (**C-only**) | T5 |
| `[W,U,B,R,G]` + flag (**flag-only**)  | T5 |
| `[W,U,B,R,G]`, no flag (pre-`e6c1f2c`) | **T4** |

→ **two independent bugs**; either sub-change alone regresses the line. `real: fails=0` in
`MTG_AFFORD_AUDIT` for all variants (the *played* line is always payable — the damage is in rollout
line SELECTION/ordering, not legality).

## Primary root cause (the `C`-addition) — CONFIRMED

`GenericProvider::ManaSourceRank` (`src/ai/DecisionProviders.cpp:369-373`) ranks a source's
tap-priority by **color count** (`ncol*10`: mono=10 … rainbow=50; LOWER = tapped earlier, "spend the
least-flexible first"):

```cpp
const std::vector<Color>& prod = EffectiveProduces(s, active, def);   // raw produces — INCLUDES C
const int ncol = static_cast<int>(prod.size());
int rank = ncol <= 1 ? 10 : ncol * 10;                                // C counted as a 6th "color"
```

Adding `"C"` bumped Sliver Hive `ncol` 5→6 → rank **50→60**. Rank **60 is Mutavault's reserve rank**
(line 353-358: a colorless-only manland is ranked *above* rainbow so it is SAVED to attack — the
comment literally warns this reserve exists because tapping Mutavault "was costing slivers a turn of
Mutavault damage"). Sliver Hive colliding into tier 60 breaks that ordering (tie → Mutavault no
longer strictly saved) → Mutavault gets tapped instead of attacking → the deck loses a turn of
Mutavault damage. Exactly the symptom. The **C-only variant (=T5)** confirms it; `flag-only` leaves
`ncol=5`/rank 50 unchanged, so this path does not explain that case.

### Proposed primary fix (to try Monday)

In `ManaSourceRank`, do **not** count `Colorless` toward `ncol` — a {C} mode is not color-fixing
flexibility. e.g. `ncol = count of non-Colorless colors in prod`. This restores Sliver Hive to rank
50 with the {C} present. **Must A/B the full regression suite** — this touches every land that
produces {C} alongside colors (check no other deck regresses), then rebaseline. Consider whether
`colored_creature_only` lands specifically should rank by their *colored* count.

## Secondary bug (the flag alone) — SCOPED, not yet located

`flag-only` (no {C}) also → T5, yet `ManaSourceRank` is byte-identical to pre-`e6c1f2c` there
(`EffectiveProduces` is not stripped by the flag). Both payment paths that DO honor the flag —
`ProducesForPayment` (`SpellEffects.h:3018`) and the affordability strip (`SpellEffects.h:4249`) —
correctly return the full colored list when `for_creature==true`, so a creature cast is unaffected
*through them*. The leak is therefore a **rollout heuristic that evaluates these lands with
`for_creature=false`** (or otherwise reads the flag) during an all-creature line — where, with no
{C}, stripping yields an **empty** produces set, so the source reads as producing nothing and the
rollout mis-values the board. **TODO Monday:** find the call site of `ProducesForPayment` /
`ManaSourceRank` / source-counting that runs with an indeterminate/false `for_creature` during
line evaluation. Candidate source-counting spots: `DecisionProviders.cpp:469,478,492,1675,1692`.

## Verification checklist (Monday)

1. Patch `ManaSourceRank` (don't count {C}); rebuild; confirm gi80 → **T4** with CURRENT cards.
2. Confirm gi277/278/314/725/726 → T4.
3. Full regression A/B (smoke+regression) — ensure no other deck regresses; watch dragonstorm
   (also has `colored_creature_only` lands: Unclaimed Territory / Cavern of Souls).
4. Locate + fix the secondary (flag-only) path; re-verify the flag-only variant → T4.
5. Rebaseline GT (smoke/regression/overnight) once both fixed — slivers should get *faster*.

Read `.claude/skills/mtg-rules.md` (mana abilities / restricted mana) and `.claude/skills/mtg-ai.md`
(tap-order heuristic, `heuristic-optimization`) before changing the ranking.
