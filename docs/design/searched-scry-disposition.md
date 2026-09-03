# Scry / surveil disposition should be a SEARCH BRANCH (not a policy hook)

2026-07-29. Direction from the user, correcting an assumption in
`treasure-hunt-open-findings.md`:

> "scry should not be a no-branch case. It should be handled the same as any other branch, the only
> difference being that we will usually implement a heuristic."

That is the general rule for this engine: a heuristic is the **default/prune** for a branch, not a
substitute for having one. The scry keep/bottom call was treated as a pure policy hook, which is why
two separate Treasure Hunt investigations dead-ended on "no fixed rule can express this".

## Current state

`DecisionProvider::ScryKeepOnTop` picks ONE disposition and the search never alternatives over it.
The standing `MTG_UNPRUNED` comment in `DecisionProviders.cpp` records this honestly as a gap, not a
design:

> Pure DECISION/POLICY hooks that pick ONE option the search never alternatives over (cast-ORDER,
> vial-charge, **scry-keep**, discard-order, combat) are NOT yet opened here: making the search branch
> on them needs new enumeration ... not just a wider gate.

## Most of the machinery already exists

The look-at-top path was already refactored into a decision object, and it is the right shape:

| piece | where | what it does |
|-------|-------|--------------|
| `TopDisposition` | `SpellEffects.h` | which looked-at indices go on top, in what order; rest away |
| `ApplyTopDisposition` | `SpellEffects.h` | the SINGLE placement routine (bottom / graveyard / shuffle) |
| `HeuristicTopDisposition` | `SpellEffects.h` | reproduces today's provider choice byte-identically |
| `EnumerateTopDispositions` | `SpellEffects.h` | **every legal option**, already written |

`EnumerateTopDispositions` is currently consumed only by `main.cpp` for the claude-play human menu.
Wiring it into the autonomous search is the work; enumerating the options is not.

**The branch factor is tiny.** For Scry/Surveil it is every ordered subset kept on top, and every
modelled card looks at 1-3 cards. Treasure Hunt's only two sources (Temple of Epiphany `etb_scry 1`,
Thundering Falls `etb_surveil 1`) look at exactly ONE card, so the branch is binary: keep or away.
This is nothing like the fetch-target blowup that needed a search cap.

## What is actually missing

The disposition resolves INLINE inside the effect, mid-turn, during a land's ETB. Branching over it
needs a **re-solve breakpoint** at that point -- the same mechanism the draw/staging/cascade
breakpoints already use (see the `CastOrderRank` note on "cast sets with NO re-solve breakpoint",
and `resolve_draw_breakpoint`). In Treasure Hunt every scry is a land ETB, so the breakpoint lands
immediately after a land drop, before the turn's casts are re-solved.

The adoption shape should mirror `TutorCandidates` / `FetchCandidates`: the provider returns the
heuristic's single pick under normal search (byte-identical, zero cost) and the full legal set when
the branch is opened. That keeps every existing baseline intact until the branch is deliberately
switched on and measured.

## What this resolves for free

Three Treasure Hunt decisions are already parked as "leave it to the search", and all three are
board-state dependent in a way a fixed rule provably cannot express:

1. **The first Land's Edge on top** -- the shipped rule deliberately declines to handle it (whether to
   scry it away before a Treasure Hunt depends on land count and spare copies).
2. **A depletion land on top** -- worth its tapped turn only when that turn was going to be spent
   anyway. Measured as an unconditional rule: +0.0200, 14 games slower, 0 faster
   (`treasure-hunt-open-findings.md` section 3b). The T1-only narrowing is just the crudest proxy for
   the real condition.
3. **Colour-vs-tempo trades** -- the residual slower games in section 4 are exactly this.

Same argument as `searched-cleanup-discard.md`: the three static scopes there bracketed the whole
design space and none reached the right answer, because the correct choice depended on board state.

## Order of work

This is a prerequisite for the two open Treasure Hunt items, so it should come BEFORE any further
scry-heuristic tuning -- that avenue is measured out (section 3b: the last four corrections moved 3
games in 9,000). Cleanup discard (`MTG_SEARCHED_DISCARD`) needs the same breakpoint treatment and has
a reproduced win waiting on it, so the two should be scoped together.

---

## 2026-07-31 — the Ponder branch already exists, and it is expensive

`cast_reorder` (Ponder) is the one look-at-top decision that **already has a search branch**:
`TurnSolver.cpp` emits a keep variant and a shuffle variant per castable Ponder, carried on
`Action::ponder_keep` and honoured by both the rollout and the executor. It ships **off**
(`MTG_PONDER_SEARCH` / `MTG_UNPRUNED`) because it was measured as the #1 branching source —
`MTG_BRANCH_STATS` put it at ~47% of all enumeration. *(2026-09-03: no longer — the post-dedup
`MTG_PONDER_AXIS` shipped default-ON in 712342ee, 2026-08-02, and `MTG_PONDER_SEARCH` is itself
default ON; the keep-vs-shuffle call IS searched today.)*

A/B with the branch on (hinata is the only suite deck with Ponder):

| case | heuristic | branch on | delta |
|---|---|---|---|
| hinata smoke d3 s1001 | 5.9000 | 6.1333 | +0.2333 |
| hinata smoke d5 s1001 | 6.0267 | 6.2800 | +0.2533 |
| hinata regression d3 s2002 | 5.9000 | 6.1050 | +0.2050 |
| hinata regression d3 s3003 | 5.8500 | 6.0000 | +0.1500 |
| hinata regression d5 s2002 | 5.8400 | 6.1100 | +0.2700 |
| hinata regression d5 s3003 | 5.8000 | 5.9900 | +0.1900 |

Two separate effects are folded into those numbers, and they need separating before this is read as
"searching the disposition is bad":

1. **d0 is not a search at all.** `hinata_smoke_d0` moved +0.0300 and `hinata_regression_d0_s2002`
   +0.0420 — but at depth 0 there is no rollout to score the two variants, so the branch degenerates
   into a *different fixed rule*: both variants carry the same plan `value`, the sort is stable, and
   the keep variant is pushed first, so d0 simply always keeps. That is "always keep" vs "heuristic
   at resolution", which says nothing about searching.
2. **At searched depths it is the familiar dilution.** Doubling the plan count on ~47% of
   enumerations at a fixed per-decision budget starves the search — the same shape as the cantrip
   breakpoint class in `cantrip-first-collapse.md`.

Whether the branch is *correct but unaffordable* (like the cantrip class) or actually *wrong* is a
budget-sweep question, not an opinion: at unlimited budget a searched decision must be at least as
good as the heuristic it replaces, unless the win is outside the searched window.

### It was a TIE-BREAK defect, not dilution and not a search result

The budget sweep (hinata d3, 40 games, seed 1001) answers the question, and the answer is neither
of the two expected ones:

| budget | heuristic | branch on (keep variant first) |
|---|---|---|
| 10 ms | 6.2750 | 6.6500 |
| 80 ms | 6.2750 | 6.6000 |
| 640 ms | 6.2750 | 6.6000 |
| 2560 ms | 6.2750 | 6.6000 |

640 ms and 2560 ms return **identical digests**, so the search has converged — this is not budget
starvation. A searched decision that is still 0.325 worse than the heuristic it replaced with a
converged search is a defect, and the play logs name it: over 48 Hinata Ponder resolutions the
heuristic shuffled **13** times and the searched branch shuffled **0**.

The search accepts a candidate only on a STRICT improvement (`win_turn < best`, then
`value > best.value`), so **whichever variant is enumerated first owns every tie** — and this
decision ties constantly. Keeping three dead cards on top costs draws on turns 4-6; from a depth-3
horizon those turns are invisible, so both variants look win-equal and the tie-break silently
becomes the decision. The old enumeration pushed the pinned *keep* variant first, so "searching" the
disposition actually meant "always keep".

Note what this is NOT: it is not a horizon problem to be fixed with depth, and not a budget problem.
The heuristic's rule ("shuffle iff nothing in the top N is wanted") encodes knowledge from beyond the
horizon, which is exactly what a heuristic is for. The bug is that the branch threw that knowledge
away on ties instead of falling back to it.

**Fix: enumerate the heuristic FIRST** (`ponder_keep = -1`, decided at resolution), then the two
pinned alternatives. The search can then only override the heuristic on a difference it can actually
see. Re-measured:

| budget | heuristic | branch on, heuristic-first |
|---|---|---|
| 10 ms | 6.2750 | 6.2750 |
| 80 ms | 6.2750 | 6.2750 |
| 640 ms | 6.2750 | 6.2750 (digest identical to the heuristic arm) |
| 2560 ms | 6.2750 | 6.2750 (identical) |

The branch is now **free** rather than harmful. It does not yet win anything on this sample — the
search finds no strictly-better disposition — so what it buys is the *ability* to find one, at the
cost of the extra variants.

### The general rule this establishes

Every plan-level sub-decision must enumerate **the heuristic's answer first**, because the search's
strict-improvement rule makes enumeration order the tie-break, and ties are the common case for any
decision whose consequences fall outside the horizon. Audited the others: `fetch_target` emits
`cands[0]` (the heuristic's top pick) first, `land_face` emits `front` first, `bp_choice` leaves the
greedy base plan ahead of its variants, and `scry_choice` puts `TopDispositionCandidates[0]` (the
heuristic) first. Ponder was the only one inverted.

---

## 2026-07-31 — the scry/surveil branch is BUILT and ADOPTED

Built as this doc specified, using the plan-level sub-decision pattern rather than a new breakpoint:

| piece | what |
|---|---|
| `Plan::scry_choice` | which candidate the land's ETB look takes; -1 = provider heuristic at resolution |
| `ScriptedTopChoice` | scoped RAII pin on `g_scripted_top_choice`, consumed by the first look, restored on exit so a nested apply cannot leak its script |
| `ChooseTopDisposition` | one entry point for all three look sites (`ScryTop` / `SurveilTop` / `ReorderTopOrShuffle`): script, then human chooser, then heuristic |
| `TopDispositionCandidates` | heuristic at index 0, then every other legal disposition -- so k=1 is byte-identical and ties go to the heuristic |

The disposition resolves INLINE inside a land's ETB, so it cannot be an `Action`; the plan pins it
for the apply instead, exactly as `bp_choice` pins a breakpoint continuation. Both the rollout
(`ApplyPlanDirect`) and the executor (`AIEngine::TakeTurn`) set the same pin around the land drop, so
they stay in lockstep by construction.

**Cost is one AXIS, not a cross product.** The fan-out runs *after* `AppendBreakpointVariants` and
skips plans that already carry a `bp_choice`, so cost is L+S rather than L*S — the same trade the
`bp_at` axis makes. A line needing a non-heuristic scry *and* a non-greedy breakpoint continuation
at once is deliberately out of reach.

### Measured

| mode | result |
|---|---|
| smoke | 14 games play-changed, **0 slower, 0 faster** (score-neutral) |
| regression | **21 faster, 9 slower**; th d3 s2002 −0.0120, d5 s2002 −0.0200, d3 s3003 −0.0020, d5 s3003 +0.0067 |
| overnight, HELD OUT | **72 faster, 5 slower**, all 8 Treasure Hunt cases improved, **−0.0690** summed |

All **14** slower games across both modes are budget churn: every one recovers at 4x its case budget
and stays recovered at 16x. Zero persistent slowdowns.

Depth 0 is byte-identical (the branch is depth-gated, and at d0 an extra variant would not be a
search — just enumeration order picking a different fixed rule). Wall time is unchanged: the
overnight arm's makespan was 8m27s against 8m39s for the baseline.

Note the audit's inline `explain_game` diff is **not usable for this A/B**: it re-runs both arms as
subprocesses that inherit `MTG_SCRY_SEARCH` from the environment, so both sides get the branch and it
reports "kept hand + draws IDENTICAL" for games that in fact diverge. The classification above was
done by hand, toggling the flag per arm.

### What this unblocks

The three Treasure Hunt decisions parked in `treasure-hunt-open-findings.md` as "no fixed rule can
express this" (the first Land's Edge on top, a depletion land on top, colour-vs-tempo trades) are now
reachable by search rather than by another round of scry-heuristic tuning — which was measured out at
3 games moved in 9,000.

### The heuristic variant is autonomous-only

Enumerating the heuristic first is a claim about the SEARCH's tie-break, and it does not transfer to
the human-play menu: there the heuristic variant is a duplicate of whichever pinned option it
resolves to, so the player would see a redundant third entry. It also shifted the recorded plan
indices of two saved Hinata references (`main_phase` 26 -> 35), which the viewer protocol check
reported as `repaired` -- informational rather than gating, since both reproduced their recorded line
exactly, but a needless churn of user-owned files.

`HumanPlayActive()` gates it: autonomous search gets heuristic-first, the human menu keeps the
explicit two. References return to 138 ok / 0 repaired.

---

## CORRECTION 2026-07-31 — the Ponder result was misdiagnosed

The section above attributes the Ponder keep-vs-shuffle result (0.325 worse with the pinned-keep
variant enumerated first; free once the heuristic was enumerated first) to the search's
strict-improvement **tie-break**. That reasoning is sound in general and the fix that shipped is
still the right one, but it is not what was happening.

`ponder_keep` is **cost-neutral**, so all three variants carry the same cast-NAME set — and
`EnumeratePlans`' autonomous `plan_signature` keys only on names. The dedup therefore keeps the
first-enumerated variant and discards the other two, every time. Enumerating the pinned keep first
made the engine *always keep* (which is exactly the 0-shuffles-vs-13 observation); enumerating the
heuristic first made the branch **not exist** rather than free.

Verified directly: `MTG_PONDER_SEARCH=0` and `MTG_PONDER_SEARCH=1` produce identical play (Hinata,
200 games, seed 4004, d3, budget 10 → 5.8950 both).

So the Ponder keep-vs-shuffle call is ~~**still unsearched**~~ *(2026-09-03: searched since
712342ee — see the correction above)*. The real fix is the post-dedup axis the
tutor target now uses — see `searched-action-subdecisions.md`. The land-ETB scry disposition in this
document is unaffected: `Plan::scry_choice` variants are emitted in `EnumeratePlansWithLand` *after*
`EnumeratePlans` returns, so they never meet the dedup.
