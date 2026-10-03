# Snow at depth 8 / budget 0: where the time goes (2026-10-02)

Status: **DEFERRED** -- measured, not yet worked on.

## Context

The d8 b0 recovery check (every game slower than ground truth must reach the GT turn at
`--depth 8 --budget-ms 0`) has 5 Snow games. One (overnight d5 s7007 gi55) took 9.5 h and
recovered (T6 = GT). The other four (overnight d3 s4004 gi56/gi104, s6006 gi134, s7007 gi16)
were still running after 11 h, on 4 threads of an otherwise-busy box.

## Finding 1 -- gi16 is not playing, it is BOTTOMING

gi16 mulligans to 7 and bottoms 1 of 8. Snow has no exhaustive keep table and no
`bottom_eval_depth` / `bottom_eval_budget_ms`, so `AIEngine::BottomCards` scores each
bottom candidate with a full clairvoyant `RolloutWinTurnFrom` game **at the game's play
settings** -- here d8 b0. Every sample (6 gdb snapshots over 2 min) had that thread in
`HandleMulligan -> BottomCards -> RolloutWinTurnFrom -> ... -> FullSearchLine`. That is one
full d8 b0 game per distinct candidate before turn 1 -- several times the cost of the game itself.

This is an apparatus problem for the recovery check rather than a search cost: the GT game
bottomed with d3 b10 rollouts. Re-run such games with the bottom eval pinned to the cell's
settings (`MTG_BOTTOM_EVAL_DEPTH=<cell depth> MTG_BOTTOM_EVAL_BUDGET=<cell budget>`), which
also makes the d8 b0 game start from the same kept hand as the GT game.

## Finding 2 -- the other three: 75% in the post-breakpoint continuation derivation

`perf record --call-graph dwarf` on the live batch (20 s, 7.7k samples, 4 threads), inclusive:

| frame | incl. |
|---|---|
| `SimulateToEndImpl` (rollouts) | 99% |
| `ApplyPlanDirect` | 87% |
| `BpEnumEntryFor` | 78% |
| `BpDeriveContinuationList` | 75% |
| `EnumeratePlansWithLandUncached` | 73% |
| `EnumeratePlans` | 63% |
| `TapForCostSharedImpl` (payability) | 24% |
| `SubsetPayableWithFilters` | 22% |
| `TurnSolver::SolveUncached` | 8% |

So nearly all d8 b0 Snow time is spent re-enumerating the continuation list at a breakpoint
(a cantrip resolving mid-turn) while APPLYING plans inside rollouts. The thread-local
`BpEnumEntryFor` memo (cap 8192) does not catch them: rollout breakpoint states are almost all
distinct. The flat profile also shows ~15% in allocation and string handling (`operator new`/
`free`, `std::string` append/assign, `unordered_map<string,...>` lookups) on this path.

## Finding 3 -- the rollouts were avoidable: the lazy leaf was OFF

`MTG_LAZY_LEAF` (leafless ladder 1..D before the leafed ladder; commit an in-window win with no
rollout at all) existed, was answer-identical since the ladder fix (Giants 0/400 differ, Pirates
40/40), and refuses to fire under a limited budget -- but was default OFF and armed only on the
value-leaf matrix's H cells. So the d8 b0 recovery runs paid a rollout at every horizon leaf of every
pass, though Snow wins T6-T8, i.e. inside an 8-turn window for nearly every decision.
**Flipped default ON for unlimited budgets (USER, 2026-10-02).** No tier GT can move: every
regression cell with budget 0 is depth 0. The matrix's V arm is pinned `=0` explicitly.

## Leads (unmeasured)

1. Hit rate of the BP enum memo at d8 vs d3 (`MTG_BP_ENUM_PROBE=1`) -- if the miss rate is
   ~100% inside rollouts, a cheaper derivation, not a bigger cache, is the lever.
2. The subset-payability walk under the enumeration (see the Snow "greedy subset walk" cost
   notes) is 22% inclusive by itself.
3. Allocation/string churn in `EnumeratePlans` lambdas.

Any change must stay sound (no truncation) and byte-identical at play settings.

## Finding 4 -- the executor re-searched a committed line at Scrying Sheets (FIXED 2026-10-03)

The long "playing" games were not searching at all for most of their wall time: the T1 decision had
already committed a VERIFIED T6 line, and the executor replayed it until a Scrying Sheets activation
(breakpoint site 8, trailing pass) the plan did not target. That site called `resolve_draw_breakpoint`
with no `fd_plan_committed` guard, so it ran `SolveWithLookahead` at deck depth -- each candidate's
rollout re-deciding every simulated turn at depth-1, five-plus levels deep -- where the apply had
scored the narrow branch (nothing inside the window). The executor now mirrors the scored branch
(`TurnSolver::Site8NarrowIsWindowBase`, shared). gi55 9.5 h -> 70 s, gi134 >11 h -> 39 s, both GT.

The POD trailing twin (site 7, Melira) had the same shape and is mirrored the same way: the apply's
canon-or-EMPTY default now lives in `TurnSolver::BpUnbranchedCanon`, called by both ApplyPlanDirect and
the executor's committed-line pod branch.

Still open (no current deck reaches it): `resolve_draw_breakpoint(bp_depth + 1)` -- a draw engine cast
INSIDE a site-7/8 continuation on a committed line -- still re-solves, because that call does not know
its site number (class_on). Snow and Melira play no such card. Pass the site through before a deck that
does is added.

Remaining Snow cost after the fix is the search itself: gi104 (GT T8) spends its time in the T1
`FullSearchLine` (FSLineWin six deep), which must cover the whole 8-turn window to see a T8 win.
