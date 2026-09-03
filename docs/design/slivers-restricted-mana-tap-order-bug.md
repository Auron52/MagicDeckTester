# Slivers restricted-mana tap-order bug (exposed by `e6c1f2c`)

**Status (updated 2026-09-03): BOTH FIXED AND ADOPTED** — primary clamp-59 a8ea4369, secondary
7b47650d (both 2026-07-26), GT rebaselined. The "fix not yet written" header and the
"UNCOMMITTED" adoption-checklist tail are history.

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

## Secondary bug (the flag alone) — FIXED 2026-07-26 (`7b47650`)

**Both bugs are now fixed and adopted** (primary clamp-59 `a8ea436`; secondary `7b47650`), GT
rebaselined across smoke/regression/overnight — only slivers moves (faster/neutral), every other
deck incl dragonstorm byte-identical. All four repro games gi80/277/278/314 → T4.

### Known limitation (mixed batches) — not a regression

The secondary fix keys `for_creature` on `all_creatures`, so a MIXED batch (a noncreature present)
stays conservative (`false`) — a `colored_creature_only` land contributes only `{C}` in the combined
prepay there. This is BYTE-IDENTICAL to the pre-fix behaviour (it was hardcoded `false`), so it
regresses nothing; it just means the batch-prepay anti-stranding optimization stays OFF for a mixed
batch on these lands. Safe because: (a) the solver already declines on any ritual/rock producer, so
Dragonstorm's ritual go-off turns never use it; (b) on decline it falls back to GREEDY per-cast
solving where `for_creature = def->card.IsCreature()` is correct per spell (`TurnSolver.cpp:526`), so
Unclaimed's colour IS usable for a dragon — never illegal, at worst slightly worse sequencing.
Dragonstorm (2 Unclaimed Territory) is byte-identical across all modes. The GENERAL fix (thread
per-spell creature-awareness into `TapForCostBacktrack` so a mixed batch pays each spell's pips with
the right source) would close it; unmotivated until a deck measurably needs it.

## Secondary bug (the flag alone) — LOCATED 2026-07-26

**Root cause:** the multi-spell (combined) mana solver pays the whole turn's batch at once and calls
`TapForCostBacktrack(state, combined, /*for_creature=*/false, ...)` with `for_creature` HARDCODED
false (`src/ai/TurnSolver.cpp:3762` and `3779`). For a `colored_creature_only` land that strips its
coloured mana, so an ALL-CREATURE batch needing a coloured pip off Sliver Hive / Secluded Courtyard
reads as unaffordable -> planner declines the batch -> casts FEWER creatures (gi277: two 1-drops on
T3 -> one -> T5 instead of T4). Confirmed: stripping the flag (keeping {C}) recovers gi277 to T4;
clamp-59 does NOT (different path). Byte-identical for decks without these lands, which is why the
hardcoded false was never revisited.

**Fix:** `combined` is a mixed batch so one bool is wrong in general, but the failing case is an
all-creature batch. Pass `for_creature = (every spell in the batch IsCreature())` instead of false.
A general fix threads per-spell creature-awareness into the backtracker (only needed for MIXED
batches on colored_creature_only lands). Needs the same adopt cycle (A/B + GT rebaseline).

## Secondary bug (the flag alone) — earlier scoping notes

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

## Fix attempt #1 (2026-07-25) — PROMISING but a TRADEOFF, NOT adopted

Tried the primary fix: in `ManaSourceRank` count only non-`Colorless` colours toward `ncol`:

```cpp
// was: const int ncol = static_cast<int>(prod.size());
int ncol = 0;
for (Color c : prod) { if (c != Color::Colorless) { ++ncol; } }
int rank = ncol <= 1 ? 10 : ncol * 10;
```

Rebuilt + tested (CURRENT correct cards):
- **Recovers 5/6** target d3 s4004 games to T4 (gi80/278/314/725/726). gi277 stays T5 → that one is
  the **secondary (flag-only) bug**, not this path.
- **Blast radius = slivers-only.** Full smoke vs GT: burn / th / knights / antilife / hinata /
  **dragonstorm all byte-identical** (incl. dragonstorm's own colored_creature_only lands). The 3
  `unclaimed_*` scenario tests still PASS.
- **BUT it's a heuristic tradeoff, not a clean win.** At the smoke seed s1001 slivers gets slightly
  *worse*: d0 4.6500→4.6570 (+0.007), d5 4.1933→4.2000 (+0.007), d3 unchanged. i.e. "spend Sliver
  Hive earlier" (rank 50) helps the s4004 Mutavault-collision games but "save it" (rank 60) happens
  to help some s1001 games. So `e6c1f2c`'s accidental rerank is a MIXED heuristic change, not a pure
  correctness regression — EXCEPT that at s4004 the faster line is genuinely *unreachable at deep
  search* (the tap-order is baked into rollouts, a real search-completeness concern), which the
  s1001 "just slightly worse line" is not.

**Conclusion:** treat this as a heuristic-optimization problem, not a one-line bug fix. Run the full
regression + overnight A/B (train vs held-out seeds, avg9) per `.claude/skills/heuristic-optimization.md`
before adopting; consider a MORE SURGICAL rank change (e.g. only prevent a coloured-producing land
from crossing into the colourless-manland reserve tier, without otherwise moving it), and possibly
gate it in the archetype provider rather than the root. The exact one-line patch above was **reverted**
from the working tree (unvalidated); reapply from here on Monday.

Still open: the **secondary (flag-only) bug** — the affordability/source path at `SpellEffects.h:4198,
4235,4249` keys on `creature_mana_only`/`colored_creature_only && !for_creature`; find the CALLER that
passes `for_creature=false` while evaluating an all-creature line (gi277, seed 4281, is the repro).

## Fix attempt #2 (2026-07-26) — CLAMP-59, A/B CLEAN WIN (recommend adopt)

The blunt "exclude {C} from ncol" (attempt #1) helped d3/d5 but regressed greedy d0 (Sliver Hive
50 gets spent where greedy wants it saved). A `{C}`-for-generic-pip tweak (`DripLandAnyPipColor`)
was a **no-op**. The winning variant **keeps {C} in ncol but clamps a colour land out of the
colourless-manland RESERVE tier** — one line after `int rank = ncol<=1?10:ncol*10;` in
`ManaSourceRank` (`DecisionProviders.cpp`):

```cpp
if (rank >= 60) { rank = 59; }   // a colour land never enters the manland-reserve tier (60)
```

Sliver Hive 60→59 still sits *below* Mutavault's reserve (60), so the search spends it first and
frees Mutavault to attack; but 59 is near-reserve so greedy still saves it. A/B (slivers, 6 seeds
2002/3003 train + 4004-7007 held, 1000 games, avg9):

| depth | attempt#1 (rank=50) held | **clamp-59 held** |
|---|---|---|
| d0 (greedy) | +0.0067 (worse) | **+0.0007 (neutral)** |
| d3 (searched) | −0.0048 | **−0.0053** |
| d5 (searched) | −0.0043 | **−0.0043** |
| overall | +0.0002 | **−0.0023 (faster)** |

Net faster on BOTH train (−0.0012) and held-out (−0.0029); d0 regression gone. Recovers target
games gi80/278/314 → T4 (gi277 still T5 = the secondary bug). Blast radius: **dragonstorm d0
byte-identical** (its Unclaimed/Cavern are also 6-mode but it has no Mutavault collision); d3/d5 not
yet A/B'd (heavy — needs the suite).

**Adoption checklist (not yet done):** full regression+overnight A/B incl dragonstorm d3/d5;
rebaseline slivers (+ any dragonstorm) GT across smoke/regression/overnight; commit code + GT
together. The clamp-59 patch is currently applied to the working tree but UNCOMMITTED.
