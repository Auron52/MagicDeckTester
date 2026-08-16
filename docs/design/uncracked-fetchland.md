# Deferring the fetchland crack

**Status:** designed, not built.
**Origin:** USER, 2026-08-15 — "With a fetch they might defer cracking it until the
second main. Fetchlands are the most flexible lands, so deferring the crack is
rarely a bad play, unless you already know what you want to do with it (or need the
mana now)."

## The gap

`PlayLandByName` (`src/ai/LandPlay.cpp:21-31`) resolves a fetchland **atomically**:

```cpp
if (!def.params.fetch_land_types.empty())
{
    Card fetchland = *it;
    ap.hand.erase(it);            // hand ...
    ++ap.lands_played_this_turn;
    ap.graveyard.push_back(fetchland);   // ... straight to the graveyard
    PerformFetch(state, state.active_player_index, def.params, opts.fetch_target);
    return true;
}
```

Hand -> graveyard -> fetched land onto the battlefield, in one step. The fetchland
never exists as a permanent, so **"play the fetch in main 1, decide the crack in
main 2" is not a line the search can consider.** The *target* is already searched
(`FetchCandidates` + `FetchSearchCap` + `opts.fetch_target`); only the *timing* is
unrepresentable.

This strictly dominates the defer-the-drop rule shipped in `67bd91d`. Deferring the
whole **drop** risks stranding main-1 mana, which is the safety concern that rule
has to reason around. Deferring only the **crack** keeps the land drop for the turn
and cannot strand anything — it just moves the colour decision after combat, when
more is known.

## Why this is cheaper than it first appears

The obvious objection is that a fetch shuffles (CR 701.19), so deferring it re-rolls
every downstream draw and churns ground truth at every tier with no byte-identical
safety net.

That objection is **only true across turns**. `ShuffleAfterSearch` keys the shuffle
on `(game_seed, search_count)` and search-shuffle is ON by default (the opt-out is
`MTG_NO_SEARCH_SHUFFLE`; five comments claiming otherwise were corrected in the
commit that added this doc). So for a crack deferred from main 1 to main 2 **of the
same turn**:

* library **contents** at crack time are identical (nothing was drawn in between),
* `search_count` is identical (same single search, just later),
* therefore `ShuffleByKey` yields the **identical** order, and the next draw step
  draws the identical card.

**Same-turn crack deferral is draw-neutral.** It changes ground truth only in games
where the deferred decision actually changes the play — which is precisely the
signal we want, with no churn floor underneath it.

It stops being neutral if something searches or draws between the two mains (another
fetch, a tutor, a combat-damage draw trigger), and it is definitely not neutral
across turns, where the draw step intervenes.

## Plan

### Phase 1 — same-turn deferral (do this one)

Model an uncracked fetch as a battlefield permanent carrying a pending-crack marker,
**forced to crack by the end of the turn**. This captures the whole of the USER's
line, keeps every game single-turn, and inherits the draw-neutrality above.

Forcing the crack at end of turn is an approximation, but a defensible one for a
goldfish: with no opponent to play around, the only reason to hold a fetch across
turns is to use the reshuffle as a dig, which is marginal. Disclose it in the
bracket note.

1. `Permanent` gains a pending-fetch marker (the fetchland's `CardParams` is already
   reachable via `LookupCached`).
2. `PlayLandByName` gains a "defer" path that puts the fetchland onto the
   battlefield with the marker instead of running `PerformFetch`.
3. Main-2 plan enumeration emits crack actions — one per `FetchCandidates` target,
   under the existing `FetchSearchCap` — plus the no-crack continuation.
4. End-of-turn: any still-pending fetch cracks with the provider's top pick.
5. Plan signature folds the pending-crack state and the chosen target (same shape as
   `elk_target` in `67bd91d`/`c63f969`), so two plans that differ only in crack
   timing are not collapsed.
6. Executor and rollout must apply this identically — `PerformFetch` is already
   shared by both, so keep the new path shared too.

### Phase 2 — cross-turn holding (defer; probably not worth it)

Needs the forced end-of-turn crack lifted, which re-rolls draws and churns GT
everywhere. Only worth attempting if Phase 1 shows the deferral mattering a lot.

## THE TRAP — a battlefield fetchland would tap for mana

Every fetchland carries a **deliberately false** `produces`:

| card | `produces` | fetches |
|---|---|---|
| Windswept Heath | W B R G | Forest, Plains |
| Misty Rainforest | W U B R G | Forest, Island |
| Scalding Tarn | W U B R G | Island, Mountain |
| Verdant Catacombs | W U B R G | Swamp, Forest |

Its own bracket note says why: *"Keeps a W/U/B/R/G `produces` ONLY so a copy in HAND
counts as a flexible colour source for fixing heuristics; **it never taps for mana
(never reaches the battlefield)**."*

Phase 1 breaks that premise by design. An uncracked fetch on the battlefield would
become a **perfect five-colour dual** — silently, with no assertion tripped and no
test failing. Every measurement taken after that point would be worthless.

Required before anything else: split the field. Real `produces` becomes empty, and
the hand-fixing hint moves to a separate key (`hand_fixing_produces` or similar).
Call sites that already special-case `!fetch_land_types.empty()` and must be
re-checked: `DecisionProviders.cpp` lines 227, 1360, 1397, 1428, 1493, 1902, 6521.

Do this as its own commit and prove it byte-identical *before* the permanent exists —
that way the risky change lands on a base whose mana is known good.

## Related

* `67bd91d` — defer the land **drop** to main 2 (the weaker cousin of this).
* [[auto-resolved-target-picks]] — same "can the engine express the line?" lens.
* The land tie-break (`f21b046`) — a fetch produces nothing directly, so any
  colour-aware land ranker must look *through* `FetchCandidates`.
