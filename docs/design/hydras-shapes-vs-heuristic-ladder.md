# Hydras: why nlfit lost to the heuristic ladder, and a FIT gate that matches the ladder (2026-11-12)

Status: FINDINGS + a local PROTOTYPE. Nothing in this doc changes shipped play. The prototype
(`value_play.fit_gate: "ladder"`, default off) is not pushed, because it is an engine change. Its fate is a USER
decision; see "Open USER questions".

## The question

On Hydras (d5 b20, no `target_depth`, keep table attached), phase E measured the leafless single-FIT shape (`nlfit` =
`{ladder: single, leaf: none, alpha: relaxed}`) as cheaper than the plain rollout ladder (`heur`, no sidecar) but
worse in quality. The USER: *"how does nlfit lose to the ladder in quality? That doesn't make much sense."*
*"nlfit should not search less. It should search approximately the same as the heuristic ladder or even a bit more
because it doesn't pay the early rollouts."* *"we should aim to make them as similar as possible except for nlfit not
running unnecessary leaves."*

## How the two shapes search one main-phase decision

- **heur.** `FullSearchLineHybrid`'s probe is the plain rollout ladder `FullSearchLine` d1..5, with the 1-ply rollout
  leaf.
  - Start gate: pass k runs iff `c_{k-1} x ratio <= 1.10 x Remaining`, where `ratio = c_{k-1}/c_{k-2}` and pass 2 uses
    the bootstrap 6.0 (`kDefaultGrowth`). The priced start gate is also checked, but it is default OFF.
  - Each pass has a 25x overrun ceiling and the anytime rule. The ladder stops at the first verified win.
  - One line cache is shared across the passes, so a pass reuses its predecessor's memo. `MTG_BP_DEV_SEQ` wraps the
    ladder in its two-pass deviation search.
- **nlfit.** The probe is a LEAFLESS ladder d1..5 under the relaxed gate (8.8x). If it proves a win, that win is
  committed.
  - Otherwise R is calibrated (once per game), and ONE rollout pass (`FSLineWinComplete`) runs at
    `dpass` = the deepest d <= the probe depth with `probe_cost[d] + R x probe_leaves[d] <= 1.10 x Remaining`.
  - The probe's depth, not the pass's, is reported as committed.

## Why nlfit lost (P12, freeze commit, r1 = 4 arms x 8 seeds x 1000, all 32 digests equal to phase E)

- nlfit vs heur, paired: **+0.00237 turns (se 0.00102)**. 43 games worse, 24 better, every change one turn.
- **Every worse game diverges where FIT's rollout pass ran SHALLOWER than heur's committed depth** (42 of 42 classified
  among the 43; FIT ran at d1 in 38 of them). 20 of the 24 better games diverge where the leafless probe proved a win
  the ladder had not reached.
- Two mechanisms starve the FIT pass:
  - **(a) The affordability estimate overprices a rollout pass 2.4-20x.** Measured actual/predicted at d>=2: 0.05-0.41.
    R is calibrated on d1, where every rollout plays the whole game out, and the rollout pass's cuts remove leaves the
    leafless probe counts. Meanwhile the probe has already spent ~60% of the budget, and FIT is gated on what is left.
    On 21 of 21 traced FIT decisions the pass ran shallower than heur.
  - **(b) The probe record is zeroed by `MTG_BP_DEV_SEQ`'s second pass.** That pass re-runs the ladder from pass 1's
    line memo, so d1..D-1 record ~0 cost and FIT is forced to d1. This happened on 7 of 21 traced decisions, and on 5
    of the 42 worse games.
- **It is budget spend, not a search hole.** 600000_271, _210 and _100 (all worse, T5 -> T6 under nlfit) each win T5
  under nlfit at `--depth 8 --budget-ms 0`. FIT is bypassed at an unlimited budget.
- So nlfit was cheaper (0.756x units) BECAUSE it searched less: the USER's reading, confirmed game by game.

## The prototype: `value_play.fit_gate: "ladder"` (env `MTG_FIT_GATE_LADDER`; default OFF)

Local commit 6fc82b00d on branch `hydras-shapes` (base origin 80d17fc85; P7b's plan-cache fix had not landed).
1. **Record fix.** Under the flag the probe record keeps the max per depth, so dev-seq pass 2 cannot zero it.
2. **The climb.** After FIT's pass at `dpass` completes, it climbs d+1..depth by the ladder's own start-gate rule.
   - The gate runs on the passes' MEASURED costs: `c_{k-1} x ratio <= 1.10 x (Limit - FIT rollout units this
     decision)`, with ratio from the last two passes and the bootstrap 6.0.
   - It has the same 25x overrun ceiling and anytime rule, and stops at a verified win.
   - The probe and the calibration are not charged against the climb, because the ladder never runs them.
3. **Reported depth = the pass's depth,** so AIEngine's verified-win test means what it means for the ladder.
4. Follow-up (P12b, local commit 1bc20b95d on the same branch):
   - The ladder's priced start gate is factored into ONE function, `PricedStartGateFits`. The ladder and the climb both
     call it, which closes the "priced gate not replicated" gap. It is inert on every shipped deck: the gate is
     default off and no sidecar arms it.
   - `MTG_FIT_CLIMB_CACHE=1` (default off) hands the completed FIT pass's line memo to the climb, as the ladder keeps
     one line cache across passes.

## Held-out A/B (P12b, 2026-11-12)

Setup:
- Binary: `mtg_proto2`, origin 80d17fc85 + 6fc82b00d.
- Hydras d5 b20, keep table attached, `MTG_RSS_CAP_GB=20`.
- ONE pooled batch under the box lock: 3 arms x 480 chunks x 50 games, 24,000 games per arm.
  - Seed 710000+off, game_index off, arms innermost.
  - 32/32 workers busy at every heartbeat. Wall 12:52-13:29 UTC.
- All 900 single-thread traces (300 games x 3 arms) reproduce the batch's win turns.

| arm | avg turn | units/game | core-s (total) | vs heur |
|---|---|---|---|---|
| heur | 5.41396 | 12,801 | 27,917 | - |
| nlfit | 5.41896 | 10,400 | 12,299 | +0.00500 (se 0.00060, t +8.29), 45 better / 165 worse games, blocks b/w/t 0/8/0; 0.812x units, 0.441x core-s |
| nlfitL | 5.41325 | 14,771 | 29,999 | **-0.00071 (se 0.00037, t -1.91)**, 48 better / 31 worse, blocks 5/2/1; **1.154x units, 1.075x core-s** |

"Blocks" are 8 blocks of 3,000 consecutive games.

- nlfitL vs nlfit: -0.00571 (t -11.74), **137 better / 0 worse**, blocks 8/0/0, at 1.420x units and 2.439x core-s.
- Every change between nlfit and nlfitL is one turn faster.
- On origin, nlfit trails heur by twice the margin it did on the freeze commit (+0.0050 vs +0.0024). The climb removes
  all of that loss and lands at parity-or-better.

### Depth parity (300 held-out games, decisions paired at the same played state)

A decision pairs when both arms searched at the same point of the played stream with identical play before it.
Depth is the committed depth: the ladder's committing pass, the climb's last completed pass, the old FIT pass, or the
probe depth when the leafless probe proved the win.

| | paired decisions | same committed depth | same committed line | same play this turn |
|---|---|---|---|---|
| nlfitL vs heur, all | 487 | **75.4%** | **71.9%** | 99.4% |
| - FIT ran (climb) | 191 | **80.1%** | **79.1%** | 100.0% |
| - leafless probe proved the win | 296 | 72.3% | 67.2% | 99.0% |
| old nlfit vs heur, all | 485 | 48.5% | 44.7% | 98.1% |
| - FIT ran | 192 | **9.9%** | **9.4%** | 96.9% |

heur also re-searches 112 times (451k units) where nlfitL replays a verified line.

### The remaining mismatch classes

1. **The leafless probe proves a win the ladder has not verified.** 296 decisions. heur commits an unverified rollout
   line at d2-d4, while nlfitL commits a proven line, often at d4-d5. This is nlfit's own strength, not a defect.
   - **All 21 played divergences in the 300 games trace to a probe-proved line.**
     - In 16, both arms then replay VERIFIED lines with the same proven win turn that differ in a later phase: a
       tie-break among equal proven wins.
     - The other 5 diverge where heur re-searches (unverified) and nlfitL replays its proven line.
   - Results: 3 better, 0 worse, 18 the same.
2. **The climb's depth differs from the ladder's: 38 of 191 FIT decisions, 24 shallower and 14 deeper.**
   - The rule is the same, but its INPUTS are not. The ladder's growth ratio `c_{k-1}/c_{k-2}` comes from its own
     shallow passes, and from T2 on those are memo-warm and nearly free. Traced examples:
     - 710000_179 T2: passes 1-2 cost 8 and 8 units, ratio 1.0. Pass 3 was admitted and cost 23,282.
     - 710000_106 T2: pass 3 cost 81,689 against an 18,000 budget.
   - The climb's ratio comes from its own colder FIT pass (e.g. d1 271 -> d2 6,512), so it declines that pass.
   - Where heur's shallow passes are cold and the climb's base pass is the dearer one, the climb goes deeper:
     - 710000_140 T1: heur's ratio 2561/215 refused d4; the climb's 2561/820 admitted it.
   - **None of the 38 changed the play at that decision** (same play this turn: 191 of 191).
3. **Verified-line replay.** This is structural and the same as mismatch 1's 16 tie-breaks.

### The parity levers (item 3), measured

- **ID-order hints already carry across the climb.** The hybrid installs ONE `IdHintScope` for the probe, the FIT
  pass and every climbed pass, so pass d+1 reads pass d's hints. There is nothing to add.
- **`MTG_FIT_CLIMB_CACHE=1`** (the FIT pass's line memo handed to the climb) is a null: byte-identical traces and
  units on all 300 games. The cross-pass line memo is not where the ladder's warmth comes from.
- **The priced start gate** is shared code now (`PricedStartGateFits`), so the climb is admitted by exactly the
  ladder's priced test.
  - It is inert on every shipped deck: default off, and no sidecar sets `decision_priced_gate`.
  - Lever off, the build is byte-identical on 80 traced games.
- The only way to close class 2 completely is to give the climb the ladder's shallow-pass costs. That means RUNNING
  the shallow rollout passes, which is exactly the work nlfit exists to skip.

### Cost: nlfitL is DEARER than heur on Hydras

1.154x units and 1.075x core-s. The USER expected nlfit to save the shallow rollout passes. On Hydras there is little
to save: from T2 on those passes are memo-cheap (single-digit units). Meanwhile the leafless probe runs first and is,
by design, not charged against the climb, so 120 of 487 nlfitL decisions finish over the 18,000-unit limit (heur: 68).

Charging the probe against the climb is the old nlfit behaviour, the one that searched less. It would trade the quality
back for the cost.

## Fleet

25 decks ship `{ladder: single, leaf: none, alpha: relaxed}`, the FIT route this changes:

- AccomplishedAlchemist, Angels, Auras, Colossus Hammer, Dragons, EldraziDisplacerFlicker, Fluctuator, Fungus,
  Galadriel, Giants, Goblins, Jumbo Cactuar, Kithkin, LifegainHeliodCombo, Merfolk, Midrange Burn, Minotaur,
  Mirrorwing Dragon, Pirates, SelesnyaLifegain, Snow, Soldiers, Unpredictable Cyclone, slivers_vial, treasure_hunt.
- None sets `fit_alpha` or `fit_gate`. The key is per-deck opt-in, so nothing moves until a deck sets it.
- Each adopting deck needs:
  - a held-out paired A/B at its own play setting against its shipped sidecar (quality net <= 0, plus units and wall);
  - then the normal smoke/regression GT accept.
- Hydras itself ships NO sidecar (heur). Adopting nlfitL there is a new sidecar.
- The record fix (part 1) alone changes FIT's depth on any deck where dev-seq's second pass fires. It is a correctness
  fix to the probe record. It could be split out as its own lever and A/B'd fleet-wide, but it is not separable in
  this prototype.

## Open USER questions

1. **Hydras: adopt `fit_gate: "ladder"` (nlfitL), or stay on heur?**
   - nlfitL is -0.00071 turns (t -1.91, 5/2/1 blocks) at 1.154x units / 1.075x core-s.
   - That is a TRADE: better and dearer, short of the t ~2 a clean call would want.
   - Recommendation: do NOT adopt on Hydras for now. heur is nearly as good and cheaper.
   - What this work settles is the USER's question. nlfit lost because its single pass searched less. With the climb it
     searches what the ladder searches (79% same committed line on FIT decisions, against 9% before) and the loss is
     gone.
2. **Fleet: screen nlfitL on the 25 FIT decks?**
   - On Hydras it turned a +0.0050 loss into parity.
   - The decks that already ship nlfit may hold the same hidden loss, since their FIT pass is starved by the same
     estimator.
   - Recommendation: a scoped A/B on the cheapest few first, then the rest.
3. **Should the record fix be default-on everywhere?** It is a correctness fix, but it moves play wherever dev-seq pass
   2 fires, so it needs its own A/B.
4. **nlesc** shares mechanism (b) through the same probe. Its escalation re-runs the full ladder, which is why it costs
   what it does.
