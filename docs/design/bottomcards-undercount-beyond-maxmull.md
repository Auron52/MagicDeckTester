# BottomCards under-bottoms when mulligan_count > table max_mull (latent core bug)

**Status:** confirmed by code inspection, deferred (fix touches core play logic + GT; do NOT change while a
profile regen is in flight). Found 2026-07-12 while adding the claude-play joint `ai_set`.

**Largely mooted 2026-07-28:** `max_mull` is now FIXED at 6 for every gen (the `MTG_KEEP_MAXMULL` knob was
removed; gens used to run at 3). The bug's precondition is `mulligan_count > max_mull`, which at max_mull=6
requires mulliganing to keep-0 (7 mulligans) -- effectively a non-event. So a shipped profile now covers
keep-7..keep-1 exactly and the under-bottoming can only bite at the degenerate keep-0, which never arises.
The `DecideBottom`/`BottomCards` `std::min(count, max_mull)` cap is still worth hardening for robustness, but
the practical exposure is gone. (The rest of this doc is the original analysis, written when tables were mm3.)

## The bug

`ExhaustiveKeepPolicy::DecideBottom` indexes its table with `std::min(count, max_mull)`
([ExhaustiveKeepPolicy.h](../../src/ai/ExhaustiveKeepPolicy.h)). Tables are generated at
`MTG_KEEP_MAXMULL=3`, so for a keep at `mulligan_count > 3` it returns the **capped** keep-target (keep
`7-max_mull` = keep 4), not the true `7-count` target. `AIEngine::BottomCards`
([AIEngine.cpp](../../src/ai/AIEngine.cpp) ~line 933) then bottoms cards toward that target and **stops
when no bucket is over-target** — i.e. it bottoms only `max_mull` cards, not `count`, and `return`s. The
result is an **oversized opening hand** (e.g. mulligan to 3 → should keep 3, but keeps 4).

## Reachability

`KeepHand`'s only hard force-keep floor is `effective_size <= 1` (AIEngine.cpp:356), i.e.
`mulligan_count == 6`. Between the table's depth (3) and that floor, the keep decision falls through to the
`keep_model` / static path (the exhaustive `Decide` returns `present=false` for an untabled depth), which
was trained at hand sizes 7..2 and **can choose to mulligan to keep 3/2/1**. So a tabled deck (TH, slivers,
burn, antilife…) can reach `BottomCards(count > 3)` autonomously. It is **rare** (only terrible hands
mulligan that deep), which is why it has stayed latent — but it is a real rules violation when it fires.

The **claude-play/viewer** hit the same root cause (the joint `ai_set` returned only 3 of 4). That side is
FIXED front-of-engine: `ExhaustiveBottomSet` in `src/main.cpp` now returns an empty set when it cannot
cover all `count` bottoms, so the GUI falls back to the per-step hint / manual selection. The **autonomous**
path is NOT fixed.

## Fix options (when picked up, on a commit where a regen is safe)

In `BottomCards`, when `DecideBottom`'s target keeps more than `7-count` (i.e. `count > max_mull`):
1. **Fall back to lookahead/heuristic bottoming for the FULL `count`** (skip the table branch entirely) —
   simplest and correct; the table just can't answer beyond its depth.
2. **Table for the first `max_mull`, heuristic for the remainder** — bottom the table's set, then bottom
   the extra `count-max_mull` via `HeuristicBottomPick`. Keeps the table's guidance where it exists.
3. **Generate deeper tables** (`MTG_KEEP_MAXMULL` > 3) — expensive; usually not worth it for depths that
   almost never occur.

Option 1 is the safe default. Whichever is chosen: it changes play for deep-mulligan hands → **re-baseline
GT** for affected decks, and note it's a correctness fix, not a heuristic change.

## Related: does the same `max_mull` anchor bias the KEEP decision?

The generation (`ExhaustiveKeep.cpp`, backward induction with a **forced-keep anchor at `m == max_mull`**,
~line 179) can't model mulliganing below keep-`(7-max_mull)` (= keep 4 at max_mull 3). Direction of the
resulting bias, first-order: **under-mulligan (too conservative)**, NOT over-mulligan. The keep rule is
"keep iff `V_keep ≥ V_mull`"; anchoring sets `V_mull ≈ E[value of a random keep-4 hand]`, whereas the true
value is `E[max(keep-4 value, dig further)] ≥` that. The anchor already counts landing on a *bad* keep-4
(it's in the expectation) — it only omits the **escape hatch** of digging below 4, and omitting an option
lowers the mulligan value, so the profile keeps slightly more than the fully-unrolled optimum. At depth 3
itself it is *forced to keep*, so it will hold some unkeepable keep-4 hands (0-landers) it should ship.

Magnitude is small (deep mulligans are rare). This is **distinct** from the clairvoyance bias
(`nc-mulligan-table-generation.md`), which pushes the *other* way (mulligan looks too good) and is the more
likely cause if over-mulliganing is ever observed. The historical "over-mulligan curse" was in the older
keep-*model* optimizer, not this exact backward induction. Fix if it matters: generate at a deeper
`MTG_KEEP_MAXMULL` (everything is already sized `(max_mull+1)*2`), or a smarter terminal anchor — measure
the shift in keep rate before paying the extra generation cost.

## Guard against regressions

Add a state-based sanity check (debug/assert) that after `BottomCards(count)` the hand size is exactly
`7-count` — this class of "bottomed too few" bug should never be silent again.
