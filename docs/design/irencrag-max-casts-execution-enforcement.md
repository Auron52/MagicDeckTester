# Irencrag Feat "one more spell" — execution-time enforcement (correctness fix)

2026-07-21. Fixing a confirmed **rules violation**: the engine let a turn cast **7+ spells after
Irencrag Feat**, whose card model (`CardParams::max_casts_after = 1`, oracle "You can cast only one more
spell this turn") allows exactly ONE. Surfaced while implementing the user's Dragonstorm cast-ordering
guidance ("Irencrag before Apex doesn't work — you can't cast your spells or use your mana").

## The bug (confirmed, faithful gi73 d5, seed 1074 game-index 73)

Executed T4 line: `Rite → Irencrag → Apex → Scourge → Seething → Desperate → Seething → Rite →
Dragonstorm` — **seven** spells after Irencrag. It "won" T4 illegally.

Why the old guards missed it — the only enforcement was two **static, subset-level approximations**
(`TurnSolver.cpp` `consider()` ~L2191 and `EnumeratePlans` ~L5552). Both count "spells after the
restrictor" by **`CastOrderRank`** over the **hand-cast subset only**:

- **Rank-blind to actual order:** cards ranked *below* Irencrag (rituals @15, creatures @10) are assumed
  cast *before* it, so they never count as "after" — even when the searched/opaque order casts them after.
- **Blind to staged casts:** Apex of Power exiles 7 cards castable this turn. Those are cast in a
  post-Apex **draw-breakpoint re-solve**, not in the hand-subset — so an Apex-exiled Dragonstorm cast
  after Irencrag is never counted at all. The subset `{rituals, Apex, Irencrag}` shows "1 after" (just
  Apex) and passes, then the exile dump casts Dragonstorm + more, unrestricted.

So the restriction was never enforced at **execution** time; the search happily found (and the executor
happily replayed) the illegal go-off.

## The fix — a turn-scoped budget enforced at every cast site

`GameState::casts_remaining_this_turn` (`-1` = no restrictor active / unlimited). At every cast:

1. **Before** any mutation: if the budget is `0`, the cast is illegal → skip it.
2. **After** the cast (same site as the `spells_cast_this_turn` storm increment): a non-restrictor spends
   one (when a budget is active); the restrictor **installs** its budget — decrement first (its own cast
   is governed by any *prior* budget), then `min` with `max_casts_after` (so two Irencrags compose).
3. Reset to `-1` at every turn start (alongside `spells_cast_this_turn`).

Enforced in **all three cast paths** so search prediction and real execution stay in lockstep:

- `TurnSolver::apply_one` — the search rollout + ordering-search re-scoring + commit-the-line recording.
- `AIEngine::CastSpellFromHand` — the **real executor** (replayed main line AND its post-Apex staged
  re-solve). This path was the reason the *first* pass (apply_one only) still left 3/399 real-game
  violations — the executor re-derives staged casts independently.
- (Off-suspend / retrace: off-suspend fires at upkeep before any main-phase Irencrag, budget still `-1`;
  retrace-after-Irencrag did not appear in either deck. Add there too if a violation ever surfaces.)

## Containment & measurement

**Fully inert for any deck without a `max_casts_after` card** (budget stays `-1`, guard and update are
no-ops, folded into no state key). Verified: all 5 non-Irencrag suite decks are **byte-identical** on
smoke (d0/d3/d5).

Legality (fresh sample, `--log-dir` scan for >1 CAST_SPELL after the last Irencrag in a turn):
`dragonstorm 0 violations / 93 Irencrag-turns / 499 games` (was 3 before the executor-path fix).

Metric impact — only the two Irencrag decks move, all **slightly slower because now legal** (removed
illegal fast wins), smoke seed 1001. The "+heuristic" column adds the Ruby-rank + Medallion-cap follow-up
(see below), which recovers ~half the Dragonstorm slowdown:

| case | old GT | enforce-only | **final (+heuristic)** |
|---|---|---|---|
| dragonstorm d0 | 8.1240 | 8.1620 | **8.1570 (+0.033)** |
| dragonstorm d3 | 5.0333 | 5.0733 | **5.0533 (+0.020)** |
| dragonstorm d5 | 5.0000 | 5.0267 | **5.0133 (+0.013)** |
| hinata d0 | 7.4760 | 7.4810 | **7.4810 (+0.005)** |
| hinata d3 | 6.0133 | 6.0267 | **6.0267 (+0.013)** |
| hinata d5 | 6.0400 | 6.0533 | **6.0533 (+0.013)** |

(Hinata is unaffected by the Dragonstorm-scoped heuristic → identical to enforce-only.)

This is a **correctness** change (per CLAUDE.md: a mis-modeled rule is a bug to fix, not a heuristic to
tune), so the small slowdown is the *right* baseline — the deck was banking illegal Irencrag→dump wins.

## Heuristic follow-up — never waste the "one more spell" (game analysis)

Analyzing every changed game (old-vs-new, both binaries) confirmed **every** slowdown was an illegal old
line replaced by a legal one, and surfaced a quality issue the user asked to fix: the single spell cast
*after* Irencrag was sometimes a **mana ritual or Ruby Medallion** — pure waste (you can't use the mana,
and a cost reducer discounts nothing when nothing follows). It came from two paths the ordering search
doesn't own:

- **Ruby Medallion via the post-Apex STAGED re-solve** (Ruby is exiled by Apex, cast in the re-solve, not
  a hand cast the generator orders) — ordered by `CastOrderRank`, where Ruby was 20 > Irencrag 18.
- The generator's own Medallion-position insertion, which tried *every* slot including after Irencrag.

Two targeted fixes (Dragonstorm-provider-scoped → Hinata/others still byte-identical):
1. `DragonstormProvider::CastOrderRank`: **Ruby Medallion → 16** (after rituals @15, before Irencrag @18).
   Governs the fixed-order / d0 / staged-re-solve paths.
2. `DragonstormCastOrderings`: **cap Medallion insertion to before the restrictor** (never after Irencrag).

Result (fresh 400-game d5 scan): **0 illegal casts AND 0 ritual/Ruby-after-Irencrag casts**, and the fix
recovered ~half the slowdown — Dragonstorm d3 +0.040 → **+0.020**, d5 +0.027 → **+0.013**, d0 +0.038 →
**+0.033** (vs the original GT). The residual d0 ritual-after-Irencrag (e.g. gi40 `Apex → Irencrag →
Desperate`) comes from the OrderingOpaque **plan-action** path (Apex makes the set opaque; d0 has no
ordering search to reorder it) and lands only in already-losing greedy lines — left as a known d0-baseline
limitation.

## Relationship to the cast-ordering search (`ccec4e8`, this session)

`DragonstormCastOrderings` now splits the old single "finisher" bucket into an **Apex enabler** (before
Irencrag) and a **closer** (Dragonstorm / a Dragon, after Irencrag) so the search *offers* the legal
`Apex → Irencrag → Dragonstorm` line. But ordering guidance alone could never fix this: without execution
enforcement the search still found the illegal Irencrag→dump line via the identity floor / subset / opaque
paths. Enforcement is what makes those lines **non-winning** (their post-budget casts are dropped), so the
legal ordering is the one that actually goes off.

## Open follow-up (not blocking)

The static subset check (L2191/L5552) can now *over-reject* a legal `Apex + Irencrag + Dragonstorm(in
HAND)` subset (it counts 2 "after", order-blind), before the ordering search can legalise it by ordering.
In the measured go-offs Dragonstorm comes from Apex's **exile** (staged, not in the subset), so that case
passes and the +0.04 movement is small — but relaxing/skipping the static check for
`WantsCastOrderingSearch` decks (deferring fully to the ordering search + execution enforcement) could
recover the rarer hand-Dragonstorm legal lines. Measure before adopting.
