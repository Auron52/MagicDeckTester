# Hinata spasm gate — root-cause (measured)

**Status:** ROOT-CAUSED (opt-in, `MTG_HINATA_SPASM_GATE`, default OFF, committed dormant in `f2ee9d7`).
Measured under the numbered+rankfix binary (commits `b3f0bd5` + `f2ee9d7`). NOT adopted.

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

## Concrete refuting games — FOR USER REVIEW (numbers alone are not convincing)

The user (rightly) wants to SEE the games where casting Reality Spasm without Crackle was good, not just
the aggregate. `logs/audit_fix/gate/refute.py <gi...>` reproduces a gate-worse game OFF vs ON (strict),
prints both full lines, and FLAGS every gate-OFF turn that casts a mana ritual (Reality Spasm / Irencrag
Feat) WITHOUT same-turn Crackle — the exact play the gate forbids (`◄◄ RITUAL W/O CRACKLE`).

Reproduced set (gate-worse at d3 s4004): gi46/48/51/93/113/120 (5→6), gi13/63 (6→7) →
`logs/audit_fix/gate/refute_games.out`.

**HONEST CONFOUND to state up-front when reviewing:** gate on/off are *different physical games* — the
gate perturbs lookahead values, changing an early decision, and because Hinata Ponders that reshuffles
the draws. So it is usually NOT "same board, gate removed one Reality Spasm." The evidence to weigh is
(a) the flagged ritual-without-Crackle plays the gate-OFF line actually made, and whether each looks
reckless or like a sensible mana accelerant / hold-Crackle-for-bigger-X; and (b) that gate-OFF assembles
the combo ~1 turn sooner across these games. If the user judges the flagged plays reckless, the +0.047 is
"correct pruning that costs a hair"; if sensible, the rule is slightly too strict. **NEXT SESSION: walk
the user through `refute_games.out` game-by-game.** If a cleaner isolation is wanted, reproduce the same
game with a forced opening hand (`--force-mulligan`) so both arms share draws and the ONLY difference is
the gate's ritual decision.

## Recommendation

Keep `MTG_HINATA_SPASM_GATE` **opt-in, default off** (as committed). Use **strict (=1)**
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
