# The confounded bottoming gate failed on Dragons and Mirrorwing — ROOT-CAUSED

**Status: SOLVED 2026-09-01. The gate was right; the generator was broken.** Both profiles shipped a
bottoming table in which **every sub-table cell held a single rollout**, so `DecideBottom`'s argmin
over 6–26 candidates selected on noise. The confounded A/B detected it and quarantined both. Nothing
bad shipped.

The user's objection was the thing that cracked it:

> *"I understand that it would fail if we tested bottoming on vs off without the confound, but with
> it the lookahead that assumes a certain library should lose out."*

That reasoning is correct, and it is why the result had to be a defect rather than a tuning outcome.
The confound is *designed* to put the blind table in exactly the world it was fit for (below), so a
0/16 loss at mean/se +18 could not be the table merely being a bit coarse.

## Root cause: `feed_sub()` is unreachable on a journal resume

`src/analyzer/ExhaustiveKeep.cpp`, producer loop:

* The fused sub-table batches are drained by **one** call site, `feed_sub()`, and it sat **inside the
  `if (!refine)` branch** — the floor phase.
* A journal resume that restores refs publishes `refs_ready = true` **before the producer loop
  starts** (`"continuous: refs restored from journal -> resuming refine"`). So `refine` is true on the
  very first iteration, the floor branch never executes, and `feed_sub()` is **never called once**.
* The refine-phase exit gated on size-7 alone (`refine && !any_live && in_flight == 0`), and the
  matching `sub_remaining == 0` test lives inside the same unreachable `!refine` branch. So the run
  terminated **normally**, reported success, and wrote the profile with all sub-table work undone.

The generation logs say it outright, on every monitor line, start to finish:

```
[keepgen] RESUME(journal): reloaded 454420 cell-sides + fixed refs -> continuing
[keepgen]   continuous: refs restored from journal -> resuming refine (2s)
[keepgen]   monitor: 300s  phase=refine  roll7=120414 (401/s)  rollsub=0 (0/s)  sub=0/142464 (0.0%, 0.0/s)
...
[keepgen]   monitor: ...                                       rollsub=0 (0/s)  sub=0/142464 (0.0%, 0.0/s)
```

`454420` reloaded cell-sides is `(155978 + 71232) × 2` — *every* cell. `sub=0/142464` never moved.

The residual count of **1** is the probe-carry (`LoadProbeCarry` banks the `recommend` scout's r=0
sample, by design, so the real run rolls only `r >= 1`). With the sub tasks never fed, that single
sample was the entire bottoming table. Both failing decks ran a scout probe first, which is why both
had a journal to resume from and a probe to carry.

## The evidence is a clean split

| deck | sub-cell rollouts (H6…H1) | bottoming gate |
|---|---|---|
| Minotaur | **40** (= cap) | pass |
| slivers_vial | **100** (= cap) | pass |
| burn | **60** (= cap) | pass |
| Goblins | **2** (= adaptive floor, legitimate) | pass |
| **Dragons** | **1** — 100% of 156506 cell-sides | **fail +0.0641t, 0/16** |
| **Mirrorwing** | **1** — 100% of 142464 cell-sides | **fail +0.1006t, 0/16** |

The two decks that failed are exactly the two whose bottoming half was never sampled. No deck with a
properly-sampled sub-table has ever failed this gate.

## Why one rollout per candidate is worse than useless

`DecideBottom` returns the argmin over the (7−m)-subcompositions of the kept hand. Measured on the
Mirrorwing table: **6.1 candidates at mull 1, 16.9 at mull 2, 25.9 at mull 3**. A per-rollout win-turn
sd of ~1.2–1.7 turns means each candidate's estimate carries that much noise, and the *minimum* of
many such estimates is systematically the luckiest one, not the best one — a textbook winner's curse.
The generator's own `RunAdaptiveBottomRegretSim` header already names this failure ("a true-argmin
cell noisily-high at the floor would never be marked"), which is precisely why bottoming-full is
supposed to drive sub-tables **straight to the cap** (`init = sub_floor ? r0 : r_max`).

**This also explains why keep passed while bottoming failed on both decks** — the pattern that looked
so strange. They are different estimator shapes reading different parts of the table:

* **Keep** is a single threshold comparison against size-7 cells, which *were* correctly refined
  (adaptively, 2 → 40). Errors only matter near the boundary; the gen's own noise report put the cost
  at ~0.017 turns. Keep measured **−0.0587t** (Mirrorwing) and **−0.0472t** (Dragons) — real wins.
* **Bottoming** is an argmin over many sub-cells, which were at R=1. Maximally exposed.

## Why the confound made it *more* visible, not less

The generator evaluates a cell by dealing the composition and then **reshuffling the remaining
library** (`ExhaustiveKeep.cpp`: `ap.library.Shuffle(SaltSeed(rs, 0x5EED5ULL))`) before
`RolloutKeepWinTurn(s, 0, ...)` — note `mulligan_count = 0`, so no bottoming happens inside the label.
So V(subcomp) assumes *bottomed cards are shuffled back into circulation*.

`MTG_CONFOUND_BOTTOM` reshuffles the library after the bottoming decision, which reproduces exactly
that distribution. **The confound therefore puts the blind table in the world it was fit for, and
strips the lookahead of its peek at the same time.** It is the table's most favourable condition. A
decisive loss there is a very strong signal — which is what made "the table is broken" the only
reading left once the harness itself was cleared.

## Ruled out along the way

* **The confound not reaching the test.** It does: `mullgen.sh` passes it through `run_ab`'s
  `env "$@"`, the harness logs `confound=1` on the bottoming arm and `confound=0` on keep, and the
  engine reshuffles after `BottomCards` under `mulligan_count > 0` with the same seed in both arms.
* **"The table is fit shallower than it plays."** Minotaur generates at `d1/b3`, *shallower* than
  Mirrorwing's `d2/b3`, and passes. Depth mismatch is not the mechanism.
* **Bucket coarseness.** Mirrorwing's 16 buckets are **all singletons** — the table is exact per card
  name, and the within-bucket tie-break is trivially correct.
* **Deck shape.** Never needed; the split above is explained entirely by sampling.

## The fixes (this commit)

1. **`ExhaustiveKeep.cpp` — drain the sub tasks in the refine phase too**: `if (refine) { feed_sub(); }`
   at the top of the producer loop. The floor-phase call stays where it was, so a normal
   (non-resume) run enqueues in the identical order and stays byte-identical; feed order cannot move
   a value anyway (`run_one` is pure in `(seed_base, r, w, pd)`).
2. **`ExhaustiveKeep.cpp` — the refine exit now requires `sub_remaining == 0`.** The old exit could
   not even announce the defect.
3. **`ExhaustiveKeep.cpp` — the raw sidecar records `meta.sub_target`**: the count every sub cell-side
   was meant to reach (cap for bottoming-full, floor when adaptive). Without it a starved table is
   indistinguishable from a legitimately adaptive one.
4. **`scripts/mullgen.sh` — the artifact check reads the RAW and fails on under-sampled sub-tables.**
   It is free and runs *before* the A/Bs. Verified: Minotaur (min 40) and Goblins (min 2, adaptive)
   pass; Dragons and Mirrorwing fail with an actionable message. The profile alone can never show
   this — it records decisions, not the rollouts behind them.

## What still has to happen

**Both tables must be regenerated; neither is salvageable.** The bottoming half is noise and the
journals are gone (deleted on completion), so a resume-in-place is not available — though the raw
does carry correct size-7 data, so a raw-resume should need only the sub-table deficit
(~142k cell-sides × 39 rollouts for Mirrorwing, order of hours, alone on the box per the
serial-generation rule).

Worth doing: the quarantine currently discards Mirrorwing's **−0.0587t** and Dragons' **−0.0472t**
keep wins along with the broken bottoming half, because the gate is all-or-nothing and there is
deliberately no way to ship a table with bottoming off. A correct regeneration should recover those
*and* whatever the bottoming half is actually worth.

**Do not rebaseline over a failing bottoming gate.** On this evidence the gate has never produced a
false positive.
