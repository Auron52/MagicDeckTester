# Fluctuator cast order + cycling placement + drawn-land drop -- RESUME note (2026-10-14)

Paused for the night on the coordinator's instruction (the box is needed for a value-leaf generation on
the other container). Nothing was running when paused.

## Base

- Worktree branch `worktree-agent-a162d1093980f7af0`, reset to `16dfb7e4` (tip of
  `work/integ-2026-10-14`). This note is the only commit on top of it.
- No binary built yet. `build/scratch/fluct-order/` does not exist yet.

## Status

| part | status |
|---|---|
| 0. Reading | DONE: CLAUDE.md, the draft `docs/design/cast-order-drafts-2026-10-14/Fluctuator.md` (all of it), ledger `docs/design/analysis-Fluctuator.md` §6-§9, `.claude/skills/coding-conventions.md`, `.claude/skills/regression-testing.md` §1-§6. |
| 0. Code reading | NOT STARTED. Next: `FluctuatorProvider` (`src/ai/DecisionProviders.h` ~2018, `.cpp` SelectDigSource / DigResolveEvenOnLand / DigResolveOnlyWhenCastable / HoldFuelWhileComboing), the rollout dig loop (`TurnSolver.cpp`, "Dig when stuck", `resolve_now`, `AnyHandCastableNow`), the executor (`AIEngine.cpp` recorded-dig replay after the casts, `PerformDig`), `Plan::dig_choice`, the breakpoint DigDraw replay, `AppendHumanPlayDigPlans`, `bp_play_searched_land`. |
| 1. Order (`MTG_FLUCT_ORDER`, heurarm slot at enum END) | NOT STARTED |
| 1. Cycling placed last at every site + unit test | NOT STARTED |
| 2. Census (`MTG_FLUCT_LANDDRAW_CENSUS`-style, diagnostic) | NOT STARTED -- run BEFORE the fix |
| 2. Drawn-land-as-the-drop fix + §4.3 scenario test | NOT STARTED |
| 3. Free Hollow One root causes (seeds 5007 T2, 5009 T3, 5026 T3, 5019 T3) | NOT STARTED |
| 4. Other problems | NOT STARTED |
| Measure (one pooled batch) | NOT STARTED -- no numbers, partial or otherwise |

## Plan to resume (in order)

1. Code reading (list above). Verify the four claims in the brief: (1) a land draw never stops the chain
   except Fluctuator's pending-Unearth rebuy; (2) `AnyHandCastableNow` ignores the open drop + a land in
   hand; (3) no later land play in the executor for this provider (single main) -> the drop is lost;
   (4) `SelectDigSource` ranks every land as fodder ahead of Hollow One.
2. Part 1: `FluctuatorProvider::CastOrderRank` / `CastOrderTierName` / `MainPhaseOverride` (Main1),
   params per draft §7 table (Unearth 11, Tutor 12, Stinger 13, Fluctuator 14, Hollow One 16), plus the
   SECOND-Fluctuator slot after Hollow One (a copy on the battlefield or earlier in the same plan), names
   as final tie-break, never hand order. `EnvOn("MTG_FLUCT_ORDER", true)` + heurarm slot appended at the
   END of the enum and `kNames`. Unit test: permuting the hand does not change the realised order.
3. Census (diagnostic flag, default off): per cell, turns where a cycle draws a land with the drop open
   and the turn ends with the drop unused; of those, how many held a card that land would have paid for.
4. Part 2 design (preferred, to be justified after code reading): stop the chain for a searched re-solve
   only while the drop is open AND (the drawn land enters untapped and changes what is castable, OR the
   chain is about to end with the drop unused); make `AnyHandCastableNow` drop-aware under the provider
   hook; the USER's "occasionally an untapped land" protection in `SelectDigSource` only where that land
   is the drop that pays a held card. Keep `HoldFuelWhileComboing`. Prefer a provider hook (base false)
   so other dig decks stay byte-identical; otherwise a full paired A/B on every dig deck.
5. Part 3 repros at `--depth 8 --budget-ms 0`, `--threads 1`, niced, `env -C /workspaces/MagicDeckTester`.
6. Measure: old = `16dfb7e4` binary kept as `build/scratch/fluct-order/bin/mtg.base`; arms part1,
   part1+2(+3/4), part1+2+`MTG_NO_REDUNDANT_REDUCER=1` (report only). Decks fluctuator (>=4000 shipped,
   >=6000 d3 b10, >=20000 d0) and fluctuator2hg (half). Held-out seeds from 11500001, `base = S0 + i*N`.
   `MTG_DUMP_WINS=1`. Identity: every other suite deck at d0 / d3 b10 / shipped, 20+ games each. ONE
   pooled `mtg --batch`, <= 8 threads, niced, only with >= 8 GB free.

## Findings so far for the USER

None yet (no code read, nothing measured).
