# Snow plan-payment shortfalls (2026-11-13)

Follow-up to Morph INT-14 (`docs/design/analysis-Morph.md`, "plan-wide payment shortfalls"): the
`MTG_LINE_PAY_AUDIT` found 9 Snow committed plans (payer2/t2 OFF arm, seeds 61000+, d5 b20) whose
payment came up short although the oracle said the whole bill was jointly payable, plus 3 cases on
SelesnyaLifegain / EldraziDisplacerFlicker. Worktree `.claude/worktrees/snow-contpay`, branch
`snow-contpay` (local).

USER rulings (verbatim, 2026-11-13):
- "The payment order doesn't matter that much to me but our mana handling should be smart enough to
  address shortfalls in part of the plan. Maybe that means we need adjustments."
- "Let's pull and then attempt fixing the snow shortfall."
- "we have an agent that has been working on mana for a while" (build on the prepay / wild-mana work).
- Snow's cast/activation order is USER-owned: the fix must not change how the provider ranks the Owl
  against the Augur / Sheets activations.

## 1. The audit was not honest (fix (c), measurement only)

Two classification errors in the INT-14 oracle:

1. **A K-repeat sink was billed once.** `LineJointPayOracle` priced Rimefeather Owl `x3` as one
   `{1}{S}`; the apply pays all three. Now the oracle bills `chosen_x` repeats of every {T}-less
   ActivatePermAbility (`joint=`); the old verdict prints as `joint1=`.
2. **A continuation's spend was blamed on the plan.** When a breakpoint continuation applied anything
   inside the plan's apply (`resolve_draw_breakpoint`: the searched/carried continuation or the
   executor's re-solve), the line changed mid-apply; the oracle prices only the top-level plan. Such
   plans are now class `CONT` (counter `cont=`), not YES/NO. `LINE_PAY_SHORT` prints `class=` and
   `cont=[carried:... | resolve:...]`.

Re-classification of the 12 cases (single-game repros, `build/scratch/snowcp/tools/{nine,others}.sh`):

| case | old | honest | mechanism |
|---|---|---|---|
| Snow g57 T6, g66 T5, g42 T6, g87 T7, g27 T6, g188 T6 | DEFECT | unpayable | Owl xK extras eat the Augur/Sheets mana (fix (b)) |
| Snow g42 T3 | DEFECT | CONT | re-solve offered phantom [Astrolabe, Sheets(act)] (fix (a)) |
| Snow g30 T4 | DEFECT | CONT | carried continuation cast Jorn |
| Snow g120 T5 | DEFECT | CONT | carried continuation cast Abominable Treefolk |
| Selesnya g68 T3, g156 T4; EDF g36 T3 | DEFECT | DEFECT | cast-only lines with a producer (prepay PRODUCER decline; the mana agent's area) |

So on Snow the TRUE jointly-payable count is **0** of 9; the 6 Owl cases are real shortfalls of a
different kind (the line as committed was never payable), and 3 are re-plans.

## 2. Fix (b): K-repeat extras keep what the plan still owes (`MTG_REPEAT_EXTRAS_RESERVE`, default ON)

A repeatable {T}-less sink (Owl `{1}{S}: ice counter`) asked for K activations carries ONE activation
on the subset's books; `SpendRepeatActivations` pays the K-1 extras at apply "out of what is left".
Snow's activation order (USER-owned) runs the Owl first, so its extras spent the mana and the {T}
sources of the plan's later Augur / Sheets activations. Fix: `TurnSolver::RepeatExtrasBesideOwed`
caps the extras at the most that leaves the trailing pass's LATER owed activations payable (probe:
the real repeat payer on a scratch board, then the owed {T} sources set aside and the owed bill paid
as one cost). Both apply worlds call it at the same point (lockstep). The order is unchanged.

Why this shape and not "bill K x cost on the subset's books": billing all K up front would refuse a
self-funding sink line (EDF's Drain / ExileTop extras paid from mana the turn makes that the subset
math does not credit) at every budget, which is lossy. The cap is lossless: every line the old apply
realised for an over-admitted plan is the realisation of an honest plan the enumeration still offers
(K is still branched over 1..3, and [Owl xK] alone stays on the menu).

Scenario: `test/scenarios/snow_owl_repeat_extras_keep_owed_sheets.json` (g66 T5 main 2): before,
[Sheets(act), Owl x3] fires Owl x3 and the look is unpaid (1 short plan); after, 0.

## 3. Fix (a): the phantom [Astrolabe, Sheets(act)] is the rescue's self-funding hole

Root cause: an Arcum's Astrolabe in HAND arms the filter real-payment rescue
(`SubsetPayableWithFilters`, `PendingFilterInHand`), and that rescue never applies an activation's own
{T}, so Scrying Sheets pays for its own `{1}{S}, {T}` and the unpayable subset is admitted. The fix
already exists as `MTG_RESCUE_TAP_SOURCE` (default OFF since 2026-09-25 as a USER decision; see
`snow-intractable-games.md` "The rescue lets a source pay for its own tap"). With it, the g42 T3
re-solve offers [Astrolabe] (no phantom activation). Measured below as its own arm; NOT flipped.

## 4. Measurement

(pending -- see below)
