# d0 mana-realization strand: greedy wastes accelerants on go-offs it can't pay (CONFIRMED)

**Status: root-caused + proof-of-concept 2026-07-23; accurate fix is open.** Found while investigating
Ruby Medallion timing with the deck's pilot. This is a *general* d0/rollout-quality bug, larger than any
of the heuristics tuned this session (go-off recognizer, storm-hold, etc.) — those shaved hundredths;
this wastes whole turns of accelerant and loses games.

## The bug

Greedy (`TurnSolver::Solve` / `consider`) picks a cast subset using a **flat-pool** affordability check
(`mana_ok = pool.CanPay(combined)`) that only verifies NET totals. It accepts a go-off subset
(`rituals + payoff`) whose **payoff cannot be paid in cast order** — the accelerants bootstrap enough
*net* mana on paper, but at execution the rituals are cast first (`CastOrderRank`), their float lands,
and when the executor reaches the payoff (Dragonstorm/Apex, cast last at rank 20) it **can't pay it and
drops it**. The rituals already resolved, so their mana evaporates end of turn — **wasted**, opponent
untouched.

## Evidence (blind d0, credit OFF, 120 games / seed 4004)

**28 fizzle turns** (≥2 accelerants cast, no payoff, opp life unchanged) — this is the strand, in normal
play, not a Medallion-credit artifact:

- **game 3, T2**: hand has Scourge (a payoff), greedy commits `2× Rite of Flame + Scourge`. T2 mana
  (Unclaimed + Mountain + 2 Rites ≈ 4) can't pay Scourge's 5 → Scourge dropped, **both Rites wasted**
  (stumbles to a T7 win anyway).
- **game 22, T5**: commits `3× Pyretic + Dragonstorm` on 2 lands. Can't reach Dragonstorm's 9 →
  Dragonstorm dropped, **3 Pyretics wasted**, never recovers → **LOSS**.

The same defect is what made the same-turn Ruby Medallion credit strand historically (`gi523`): the
credit just pushes more subsets over the flat-pool line into the unrealizable zone. See the Medallion
discussion in `dragonstorm-d0-divergence-digest.md`.

## Proof-of-concept fix (works on the symptom, not yet lockstep)

`SubsetRealizableInCastOrder` (behind `MTG_REALIZE_CHECK`): walk the subset in `CastOrderRank` order with
a scalar running pool (`start = pool.Total()`; pay `a.cost.ManaValue()`, add `a.ritual_float` per cast);
reject if any cast can't be paid. Each `Action` already carries `a.cost` (`EffectiveCost`, in-play
reducers applied) and `a.ritual_float` (`RitualFloatAmount`, the gross add — verified: Pyretic
`ritual_floating_mana=3` for `{1}{R}`), so no recomputation is needed. Gated on ritual-bearing subsets →
inert on normal turns.

**Measured (seed 4004/5005, 300 g):**
- **Fizzles: 28 → 2** (kills the waste — the concept is correct).
- **But blind d0 got WORSE: 6.45 → 6.69 / 6.15 → 6.44 (+0.25/+0.29).**

## Why the PoC over-rejects (the real work remaining)

The scalar check is **not lockstep** with the executor's true mana realization, so it rejects go-offs
that ARE realizable (false rejections dominate the fizzle savings → d0 worse).

**Confirmed false rejection (gi=10, seed 4004):** OFF casts `Rite of Flame + Desperate Ritual +
Dragonstorm` on T4 and wins (opp −34); ON **idles T4** — the check rejected that go-off — and wins T6
instead (−2 turns). So the PoC is strictly under-counting the T4 mana.

**Root cause of the undercount: `start_mana = pool.Total()` is the wrong baseline.** `mana_ok` uses
`credited ? eff : pool`, where `eff` is the *credited* pool (base `pool` + the subset's ritual floats as
wild + rock/other credits). The PoC starts from raw `pool.Total()` and re-adds `a.ritual_float` as it
walks — but if the accepted line only cleared `mana_ok` via the `credited`/`eff` path, `pool.Total()`
alone is below what the executor really has, so a payable line reads as unpayable. The fix is to seed the
walk from the **same effective mana `mana_ok` used** (the base *before* the subset's own ritual floats,
i.e. `eff` minus this subset's floats — or `pool` only when `!credited`), not raw `pool.Total()`. The
sequential *order* check is the part worth keeping; the *baseline* must match `mana_ok` exactly.

Other lockstep gaps to reconcile before a scalar realizer is trustworthy:
- the pending land drop (if `consider` ever runs pre-land) and which land / its yield;

- the pending land drop (which land? its yield);
- storage-land **partial burst** (`PermanentManaYield` = current counters; the executor bursts only the
  shortfall and banks the rest — `ManaSourceRank` taps storage last);
- the turn-scoped **floating reserve** (`state.floating_mana`) and retained over-production;
- same-turn **reducer** discount (Medallion) applied as reducers land — deliberately omitted in the PoC;
- colour (fine for this mono-red deck; a scalar total suffices here, but not in general).

## The right fix (options)

1. **Reuse the executor's real payment on a state copy** for go-off subsets only (guaranteed lockstep;
   cost = one copy + trial-cast per go-off subset — go-offs are rare, but a rollout enumerates the
   ritual powerset, so measure the added cost). This is the safe, accurate route.
2. **Make the scalar realizer lockstep** by feeding it the same inputs the executor sees (post-land
   state / pending-land yield, storage partial-burst, floating reserve). Cheaper but must be verified
   byte-for-byte against execution or it will fd-diverge.

Either way this belongs behind a flag with the **full regression** run (a payment-model change is exactly
the kind that causes `[fd-diverge]` between rollout and executor). The payoff is real: eliminating the
strand should recover a chunk of the ~6.9-vs-4.6 d0-vs-search gap AND make the same-turn Medallion credit
safe to enable (closing the ~24% Medallion slice of the residual).

## Uncommitted scaffolding (this session)

`src/ai/TurnSolver.cpp` (default-OFF flags, inert): `MTG_REALIZE_CHECK`
(`SubsetRealizableInCastOrder` + the `consider` call) and `MTG_SOLVE_MEDALLION_CREDIT` (the strand
repro). Keep or revert per the next session; the approach above is self-contained.
