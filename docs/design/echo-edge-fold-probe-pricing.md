# The echo edge fold inside a recording probe (possible FIT mispricing) -- DEFERRED

Status: **open question, nothing changed.** Raised 2026-11-20 by the Maze's End P7 work (P7-8 / P7-9), while
fixing Snow gi961. Self-contained; no agent notes needed.

## What the edge fold is

`EchoEdgeFoldHere(s)` (`src/ai/TurnSolver.cpp`, defined just above the echo cover census, ~line 82248) is true
at a full-line node (`g_fsline_nest > 0`, not inside a cast-solver or rollout, `greedywindow::t_depth == 1`,
no extra-turn deck) whose NEXT turn is scored by a CONSTANT leaf (`FsNextLeafConstant`: a forced constant leaf
`g_force_constant_leaf`, or an attached value model that is the no-leaf stand-in). A constant leaf answers
`max_turns + 1` for every state, so a decision taken in the next upkeep / turn boundary cannot change any value
the node computes. Branching it is duplicate work, so every boundary axis skips it there:

- the searched echo axis (`EchoAxisLiveHere`),
- Defense of the Heart's put odometer (`DothAxisOn` callers),
- the suspend-tutor pin and the upkeep land search (`SuspendTutorUncoveredPending`, `UpkeepLandUncoveredPending`),
- the morph / end-of-turn face-up and tap-put axis (`FaceUpUncoveredPending`),
- Maze's End's crossing land puts, i.e. Manabond's end-step yes/no and Burgeoning's which-land
  (`CrossingLandUncoveredPending`),
- the upkeep Horn and upkeep legend-keep axes (`HornUncoveredPending`, `UpkeepLegendUncoveredPending`),
- the boundary dredge fan (`BoundaryFan`, `dredge_ = ... && !EchoEdgeFoldHere(pre)`),
- and the matching "edge" buckets of each axis census.

As a fold of the search's OWN answer it is sound: at a constant leaf every clone scores the same, and a deeper
pass (or a pass with a real leaf) searches that boundary in full, so no line is unfindable at any budget.

## Why it may skew the estimate

Several escalation shapes run a leafless PROBE ladder first and then price a later pass with a real leaf off
what the probe recorded. While `g_probe_recording` is set (`s_esc_predict`, the single-pass reserve / FIT
shapes, `single_mode >= 1`), `FullSearchLine` snapshots each depth's tree cost and leaf count into
`g_probe_cost[d]` / `g_probe_leaves[d]`. FIT then prices a rollout pass at depth d as
`tree(d) + R x leaves(d)`; the reserve, the K-predictor and path-to-trust read the same numbers.

The probe's passes run under `ForceConstantLeafGuard(true)`, so `FsNextLeafConstant` is true in them and the edge
fold fires: each boundary fan above is collapsed to one clone at the last ply, and the probe reaches ONE depth-0
leaf where the priced pass will reach one leaf per clone. The priced pass has a non-constant leaf, so the edge fold
does NOT fire there, and the pass branches the full fan. So, on any deck with a live boundary axis at the last
ply, the recorded `leaves(d)` (and `tree(d)`) under-count what the priced pass will face, by about the mean fan
width at the horizon boundary.

This is the same shape as the defect fixed in a650abea4 / 3ca5e9f1e (`MTG_LASTPLY_PROBE_MEASURE`): the last-ply
no-win fold fired inside recording probes, Snow gi961's recorded d3 leaves fell 184 -> 0 (tree 390 -> 111), FIT
admitted a d3 rollout pass priced at 111 units that ran to the 25x overrun ceiling (225k units), and the
overrun broke the breakpoint loops so the anytime rescue committed T7 instead of T6. The fix: that fold does not
fire while `g_probe_recording` (the same reason and gate as `MTG_PROBE_INCLUSIVE_CUT`). The edge fold has no such
exclusion.

Two differences from the last-ply case, which is why this is a question and not a known bug:

1. The edge fold under-counts by a FACTOR (the fan width), not to zero. A width-2 echo or Manabond yes/no
   halves the leaves. That is a smaller mispricing, but FIT's overrun ceiling is 25x, so a wide odometer
   (Defense of the Heart, a Burgeoning menu, dredge) could still matter.
2. It is OLD: every committed ground truth already contains it. Excluding it from recording probes is a play
   change for every affected deck, not a bug fix with net-0 elsewhere.

Under the budget rules (CLAUDE.md, "A BUDGET MAY RE-PRICE WORK ...") either version is legitimate: the fold is
sound and a price only changes how a finite budget is spent. The question is purely which price is more
accurate and whether that buys anything.

## How to test it

1. **Census first (cheap, no play change).** Add a counter of `EchoEdgeFoldHere` hits taken while
   `g_probe_recording` is set, per axis, printed beside the existing `[echo-axis]` / `[crossing-land]` /
   `[doth-axis]` edge buckets. Run it on one pooled regression-tier batch. A deck whose count is 0 is
   unaffected (its probes never meet a boundary axis, or it never records). If every deck reads 0, close this
   note.
2. **Measure the price error on the decks that hit.** For a hit decision, compare the probe's recorded
   `leaves(d)` with the leaves the priced pass actually reached (`MTG_P12_FIT_TRACE=1` prints FIT's estimate and
   the pass's real cost; `MTG_P16_TRACE=1` the pass ladder). A ratio near 1 means the factor is harmless.
3. **If the error is material, build the exclusion as a lever** -- `!(g_probe_recording && <Lever>On())` in
   `EchoEdgeFoldHere`, a heurarm slot so a manifest can flip it per job, `EnvOn` with `=0` meaning the current
   behaviour -- exactly the `MTG_LASTPLY_PROBE_MEASURE` pattern. Check it is inert where it should be: a
   non-recording leafless pass (the ladders, P16/P18, Maze's End's play) and every rollout pass must stay
   byte-identical.
4. **A/B on held-out seeds, pooled in ONE `mtg --batch`**, arms innermost, only on the decks step 1 flagged, std
   and 2HG both, sized to the effect. Report per deck: digest-identical games, games faster / slower, units
   ratio, net turns. The adoption bar is the usual one: every slower game recovers at `--depth 8 --budget-ms 0`
   and every deck's net is <= 0 ("+1 is not neutral. 0 is neutral."). Whether to adopt is the USER's call.

Not done here: none of the above has been run. The Maze's End deck itself plays a non-recording leafless shape,
so it is not expected to be affected.
