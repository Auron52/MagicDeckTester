# Hinata spasm gate — root-cause (measured)

**Status (updated 2026-09-03):** `MTG_HINATA_SPASM_GATE` was DELETED from src on 2026-07-30
(f6e16645, with 17 other spent flags, user-approved) — there is no flag left to keep opt-in;
the recommendation below is historical.

**Status:** ROOT-CAUSED. Committed modes are only `off / strict(=1) / soft(=2)` (opt-in,
`MTG_HINATA_SPASM_GATE`, default OFF, `f2ee9d7`) — both non-off modes were **rejected**. The variant
actually kept (≈0 quality change / ~0.5% perf) — a *full-turn, order-aware "ritual but no Win and no
Crackle"* prune — was stashed, the stash was dropped, then **RECOVERED 2026-07-22** from dangling commit
`eef4575` and preserved (branch `recovered/hinata-spasm-gate-redesign` + `stash@{0}` +
`logs/spasm_remeasure/spasm-gate-redesign.eef4575.patch`). NOT adopted; not in the working tree. See next section.

## 2026-07-22 — the keeper variant: RECOVERED (was a dropped stash, not lost)

The variant actually settled on — measured at **≈0 quality change and a ~0.5% speedup** (within noise) —
is a *full-turn, order-aware* Crackle-payoff-sink prune, NOT either committed mode. It was implemented,
stashed, and the stash was later dropped; **recovered 2026-07-22** from dangling stash commit **`eef4575`**
and preserved three ways: branch **`recovered/hinata-spasm-gate-redesign`** (pushed), re-stashed at
`stash@{0}`, and `logs/spasm_remeasure/spasm-gate-redesign.eef4575.patch`. Touches `TurnSolver.cpp`,
`DecisionProviders.cpp`, `GameEngine.cpp`, `GameState.h` (+ this doc).

**What it does (from the recovered code):**
> Prune a *whole-turn* line iff it cast a mana ritual (Reality Spasm / Irencrag Feat, `IsManaRitual`), cast
> **no Crackle payoff** (`x_damage_multiplier > 1`) after the ritual, **and did not win** — the genuinely-
> wasted case (the ritual's mana must be spent this turn or it evaporates). A ritual that funds a Crackle —
> even a **sub-lethal** one — is KEPT (sub-lethal Crackle is load-bearing chip damage that reaches the same
> optimal win a turn or two later). Applied at the turn boundary in `SimulateEndAndStartNextTurn`; a false
> return in the FSLine line-finder means "this line has no win, try another" (an INVALID line, not a slow
> one). **Order-aware:** a fresh ritual resets `crackle_since_ritual` (it now needs a Crackle *after* it); a
> Crackle sets it; both reset each turn; byte-identical off. **Rollout-safe:** disabled inside
> `SimulateToEnd` ranking rollouts (`g_in_rollout_eval`) — a false "no win" there poisons candidate ranking,
> the exact confound that lost gi10/gi46 in earlier attempts.

**Why the committed modes are NOT it:** `strict (=1)` has no Win escape and fires on subsets (killing
"accelerate → dig → Crackle-later"); `soft (=2)`'s `HinataCrackleInHand` check is inverted. The recovered
redesign is the cleaner whole-turn, order-aware, rollout-safe formulation.

**Status:** RECOVERED + preserved, **not adopted, not in the working tree** (user 2026-07-22: not worth
rebuilding for ~0.5% within noise). To revisit: `git stash apply stash@{0}` (or check out
`recovered/hinata-spasm-gate-redesign`), rebuild, A/B, adopt behind `MTG_HINATA_SPASM_GATE` only on a
measured win. The `strict`/`soft` tables below are stale (2026-07-22 re-check: strict ~1.8× / +0.04, soft ~neutral).

## What the gate is

Two parts, both behind `MTG_HINATA_SPASM_GATE` (byte-identical off):
- **Emission gate** `HinataProvider::ShouldEmitUntapRitual` ([DecisionProviders.cpp](../../src/ai/DecisionProviders.cpp)):
  only emit Reality Spasm when Crackle with Power is in hand.
- **Plan-prune** `SubsetWastesRampRitual` ([TurnSolver.cpp](../../src/ai/TurnSolver.cpp), called in
  `Solve::consider` + `eval_and_push`): drop any single-turn plan subset that contains a mana ritual
  (`IsManaRitual` = Reality Spasm untap / Irencrag Feat) but no Crackle (`x_damage_multiplier > 1`).

Intent (user, Dragonstorm analogy): only spend rituals when casting the payoff (Crackle); "the gate
should still prune Crackle-present hands that do not cast Crackle"; dig-then-Crackle is acceptable.

## Measured (gate OFF vs ON, hinata d3, seed 4004, N=200)

| metric | OFF | ON |
|--------|-----|-----|
| avg turn-to-win (unwon=9) | 5.870 | **5.925** (+0.055 = gate WORSE) |
| games changed | — | 15 (13 gate-worse, 2 gate-better) |
| win→loss | — | **0** |
| wall time | 604 s | **226 s (2.67× FASTER)** |

- **Quality: −0.055** (gate worse), reproducing the earlier ~0.052 magnitude — now confirmed under the
  numbered+rankfix binary (the earlier number was pre-numbering-fix).
- **Perf: 2.67× faster** — the prune shrinks the combo-hand plan powerset. This is the perf win.
- **Tell:** every one of the 13 gate-worse games is **exactly +1 turn slower** (5→6, 6→7, 7→8), and
  **0 games are lost**. Pure tempo cost, not lost wins.

## Mechanism (per-game: gi46, gi13)

The gate does NOT block the winning combo turn — in both arms the win turn casts Reality Spasm +
Crackle together (Crackle present → allowed). The cost is **indirect**:

- The prune removes ritual-without-same-turn-Crackle subsets from the **lookahead** enumeration. This
  forbids casting Reality Spasm as a **mana-accelerant on a setup turn** (untap → cast more cantrips /
  dig harder / develop) when Crackle isn't yet in hand.
- That perturbs the search's plan values, changing even early cantrip choices (gi46: T1 Ponder→Preordain).
- Because Hinata Ponders, a changed early decision **reshuffles the draws** (`ShuffleByKey`), so gate-ON
  becomes a *different physical game* that assembles the combo ~1 turn later. gi13: gate-OFF wins T6 with
  a 3×-Reality-Spasm + Sol Ring + Ponder(→draws Crackle) + Crackle mega-turn; gate-ON wins T7.

So it is **not a correctness bug** — the gate prunes exactly what it says. It is the *intuitive heuristic
being slightly too strict*: using Reality Spasm as a mid-combo mana accelerant on a non-Crackle turn is,
on balance, net-positive tempo (it digs to the combo faster), and forbidding it costs ~1 turn in ~6.5%
of games. The "reckless tempo" the gate removes was mostly *good* tempo.

## Does it flip under a tight budget? (user's hypothesis) — NO

Budget sweep (d3, seed 4004, N=150):

| budget | off avg | strict-gate avg | delta | perf |
|--------|---------|-----------------|-------|------|
| 3  | 5.8400 | 5.8867 | **+0.0467** (worse) | 1.9× faster |
| 10 | 5.8700 | 5.9250 | **+0.0550** (worse) | 2.67× faster |

The gate is quality-negative at BOTH tight (3) and normal (10) budgets — the "quality-neutral under a
tight budget" hypothesis does NOT hold. The freed nodes don't recover the lost tempo; gate-off is not
node-starved enough at these budgets for the pruning to help. (Budget 40 not run; the trend is clear.)

## Attempted fix: SOFT gate (`MTG_HINATA_SPASM_GATE=2`)

Implemented a soft mode (uncommitted, opt-in; off/strict paths byte-identical): only prune the ritual
when Crackle is IN HAND but the plan casts the ritual without it (the user's literal "prune
Crackle-present hands that do not cast Crackle"); when Crackle is ABSENT, ALLOW the ritual as a
mana-accelerant. Emission gate also relaxed to emit always in soft mode.

**Result (d3 s4004 N=150):**

| mode | avg | Δ vs off | worse/better | wall (perf) |
|------|-----|----------|--------------|-------------|
| off | 5.8133 | — | — | 597 s |
| strict (=1) | 5.8600 | +0.0467 | 9 / 2 | 210 s (**2.84× faster**) |
| soft (=2) | 5.8322 | +0.0189 | 3 / 0 | ~600 s (**~1× — no perf win**) |

- **Soft recovers ~60% of the tempo** (fixes 6 of strict's 9 worse games — the accelerant cases where
  Crackle was ABSENT) but is still mildly negative (+0.019). The residual 3 are cases where Crackle IS
  in hand and holding it for a bigger next-turn X beats casting it now — so even the user's literal
  "prune Crackle-present hands that do not cast Crackle" is very slightly too strict.
- **Soft LOSES the perf win.** The expensive combo-hand enumerations usually have Crackle *absent*
  (still digging for it), so soft doesn't prune them — its last d3 s4004 tail game alone ran >9 min.
  Strict prunes everything (2.84× faster); soft ≈ off. So soft does NOT dominate: it trades away the
  gate's entire *raison d'être* (perf) to halve a small quality cost.

## Conclusion

The spasm gate is a **perf ⇄ quality tradeoff, not a free win**, and the tradeoff does not have a
sweet spot:
- **strict** = big perf (2.84×) at −0.047 tempo (removes net-good accelerant lines);
- **soft** = −0.019 tempo but ~no perf;
- **off** = quality-best, slowest.

The gate's core premise (a ritual is only worth casting with same-turn Crackle) is measurably slightly
too strict *in both forms* — Reality Spasm as a mid-combo mana accelerant (to power a bigger cantrip/dig
turn, or to hold Crackle for a bigger X next turn) is, on balance, net-positive tempo. **0 games are ever
lost** (it's pure turn-count), and the effect is small (~0.02–0.05 turn).

**Recommendation:** keep `MTG_HINATA_SPASM_GATE` **opt-in, default off** (as committed). Use **strict (=1)**
only as a fast approximate mode when search cost matters more than ~1 turn of tempo (e.g. bulk
profile-gen). **Soft (=2)** is not worth adopting (no perf payoff). Do NOT adopt either as default. If
the user wants the perf without the tempo cost, the real lever is elsewhere (a cheaper enumerator /
better max-mana bound), not this heuristic prune.

## Recommendation (pending budget sweep)

Keep `MTG_HINATA_SPASM_GATE` **opt-in, default off** (as committed). It is a real 2.67× perf win but a
real −0.055 quality cost at the shipped budget, with 0 lost games. Options for the user:
1. **Drop it** — the tempo cost isn't worth it if perf isn't the bottleneck.
2. **Keep opt-in** — use it as a fast approximate mode when perf matters more than 1 turn of tempo.
3. **Soften it** — only prune the ritual when Crackle is IN HAND but uncast (the user's literal spec),
   NOT when Crackle is absent and the ritual is a legit accelerant. This should recover most of the
   tempo while keeping much of the perf. (Would need a new measurement.)
