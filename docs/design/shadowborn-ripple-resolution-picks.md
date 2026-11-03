# Ripple chains: decide free picks at RESOLUTION, not as a pre-built cross product (deferred)

Status: DEFERRED (2026-11-03, Shadowborn Apostle INT-8). Owner: whoever next works on the ripple engine.

## Why this exists

The USER ruled on 2026-11-03 that the ripple "late wave" (INT-3, `MTG_RIPPLE_LATE_WAVE_MS` = 100 virtual ms) and
"deep wave" (INT-5, `MTG_RIPPLE_DEEP_WAVE_MS` = 1000) must go: *"I don't want to add special rules like this."*
Every ripple option should be offered at every budget. If that is too wide, the sanctioned fix is to decide the
free-tutor picks **at resolution** instead of pre-building them as a cross product. A new threshold is not
an acceptable fix.

INT-8 deleted both waves and measured the result. It was far too wide:

- **One-cast probe.** Stone out, a hard Diabolic Intent, three Intents in the library: `EmitRipplePins` emits
  **5,488 variants** of that single cast. That is 8 don't-reveal masks x 8 decline masks x wt^k free-tutor
  ranks x wv^k free-victim ranks, with k up to 3, wt = 3 and wv = 2. The INT-7 shape at b10, sized by the dry
  run, emits 1,304 variants for that already-extreme state; probe arms 2,608 / 2,744 isolate the deep and late
  parts.
- **Real play.** In the d5 b20 held-out cell (seed 7001), individual games ran **past 20 minutes**. Under INT-7
  the whole 200-game cell took ~2,000 game-seconds. The run was stopped. Most of the cost is uncharged
  enumeration and per-variant applies, not budgeted search.

So the waves are back in the tree, exactly as at INT-7, until this lands.

## The shape of the fix

Today a hard cast carries its whole chain's answers up front: `Action::ripple_mask`, `_decline`, `_free_tutor`
and `_free_victim`. These are stamped into `GameState::ripple_script` and read by the shared resolvers in
`SpellEffects.h` ("RIPPLE") at every trigger, in the executor, the rollout and the viewer. Because the pins
are pre-built, each answer multiplies the others.

At resolution, each free cast's pick should instead be a choice point:

1. **Choice point.** When a free tutor (or free sacrifice) resolves inside the decision window with no pin, the
   resolution stops at that point and hands the choice to the search. This can be done in either of two ways:
   - a breakpoint continuation (the plan's remainder re-solved per candidate), or
   - an explicit, resumable chain stack in `GameState`, so the rollout can clone the mid-chain state per
     candidate.

   The C++ recursion in the rollout's inline `ripple_trigger` holds pending triggers in locals. It therefore
   has to become data (a stack carried by the state) before a mid-chain state can be cloned.
2. **Width becomes a sum.** Each choice point branches over its own <= wt candidates (the USER's tutor table
   keeps wt to a few names, with exactly one as the fallback). The cost of a chain becomes the sum of its
   choice points' widths, multiplied only along the lines the search actually explores. Nothing is fixed
   before the library has moved.
3. **Lockstep.** The executor's `ResolveRippleTrigger` / `PushFreeCast` path and the viewer's `free_cast` /
   `dig` panels already ask per resolution. The search side is the only one that pre-builds.
4. **Masks and declines.** The don't-reveal and decline bits have the same shape and can move with the picks.
   The order-inert F1 names (Apostle, Dark Ritual) are cheap as they stand (linear menus) and need not move.

## Acceptance

- Unbudgeted completeness: every pick, victim, mask and decline is findable at `--budget-ms 0`.
- No budget threshold anywhere in the ripple code.
- Width before and after at the probe state above, plus units per game on the Shadowborn d3 b10 seed-1001
  sanity and the held-out seed-7001 d3 b10 and d5 b20 cells, against the tree that still has the waves.
