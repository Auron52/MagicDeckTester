# FIT climb on the 24 shipped-FIT decks: nlfitL and nlfitR vs the shipped sidecar (2026-11-12)

Status: MEASUREMENTS ONLY. Nothing shipped changes. Both variants are a local, unpushed engine prototype (branch
`fit-fleet`, see "Build"). Adopting either on any deck is a **USER decision**; see "Open USER questions".

Follows `docs/design/hydras-shapes-vs-heuristic-ladder.md`, which found that nlfit (`{ladder: single, leaf: none,
alpha: relaxed}`) lost to the heuristic ladder on Hydras because its single FIT rollout pass was starved. That doc's
prototype, `value_play.fit_gate: "ladder"`, fixes the probe record and climbs depths by the ladder's own start gate.

## The two variants

- **nlfitL** = `fit_gate: "ladder"` (P12). The record fix, plus a climb from FIT's pass by the ladder's start rule,
  `c_{k-1} x ratio <= 1.10 x (Limit - FIT's own rollout units)`. The leafless probe and the R calibration are **not**
  charged against the climb.
- **nlfitR** = `fit_gate: "ladder_rem"` (new, P14). The same fix and climb, but the climb's gate reads the decision's
  **true** `budget->Remaining()`, with probe, calibration and FIT passes all charged. That is the quantity
  `FullSearchLine`'s own start gate reads.
  - Env `MTG_FIT_GATE_LADDER_REM`, default off.
  - Code: `valuearm::FitGateCode` / `FitGateResolve` / `FitClimbAllowance` in `src/ai/ValueArm.h`; the climb gate in
    `TurnSolver.cpp` calls `FitClimbAllowance`.
  - Unit tests: `test/unit/test_fit_gate.cpp`, 4 cases / 20 assertions (sidecar parse end to end, code mapping, env
    resolution, allowance arithmetic). The full suite passes, 2,859 cases.
  - One-game smoke on Hydras 710001: L and R both reproduce P12's batch win turn (T7) and trace the same climb.

## Setup

- **Build.** Branch `fit-fleet` = P12's two WIP commits rebased onto origin `afd791934`, plus `47457900b` (ladder_rem).
  `HEAD:src` `b4d7a6649`; Release, `MTG_RSS_CAP_GB=20`.
  - The baseline arm is the same binary with no `fit_gate` key.
  - The rebased binary reproduces P12's Hydras A/B: heur and nlfitL job digests are identical on all 160 shared chunks.
- **Arms.** Per deck, three directories that symlink every sibling of the shipped deck (decklist, profile, keep
  table), so each arm plays at the deck's own play setting with its keep table attached:
  - `ship`: the shipped sidecar, unchanged;
  - `L`: the shipped sidecar plus `fit_gate: "ladder"`;
  - `R`: the shipped sidecar plus `fit_gate: "ladder_rem"`.
  Hydras ships no sidecar, so its `ship` arm is heur, and L/R add a bare `{ladder: single, leaf: none, alpha: relaxed}`.
- **Play settings.** From each deck's `value_play` (`target_depth`/`budget_ms`), else the built-in d5 b20; each job's
  `[play]` line confirms them. The provider is identical across arms on every deck.
- **Round 1.** ONE pooled batch: 24 decks + Hydras x 3 arms, arms innermost, 236,700 games, 5,148 jobs.
  - Held-out seeds: 720000+off (Hydras 710000+off, the first 8,000 of P12's seeds).
  - Sizing: 4,000 games per arm on the cheap decks; 500-1,000 on the expensive ones (snow 800, merfolk 600, jumbo 500).
  - 32/32 workers busy at every heartbeat; about 34 min wall.
- **Round 2.** ONE pooled batch re-measuring ship vs L on fresh seeds 740000+off, for the 8 decks whose round-1
  L delta was non-zero and which were cheap enough to grow: 65,200 games, about 27 min wall.
  - Its last ~10 min ran at 1/32 on one slow Alchemist game. That skews round 2's per-job wall (th's L core-s reads
    0.72x against 1.15x units), so the **core-s column below is round 1 only**; units are deterministic and pooled.
- **Cyclone was NOT screened.** USER 2026-11-04: leave Cyclone alone in budget work. It is also the fleet's most
  expensive deck.
- Box time total: about 1 h of the 3 h allowed. More games would not change any verdict below.
- **Metrics.**
  - delta = arm minus ship in mean win turn (unwon = 9), paired per game.
  - se = paired se; t = delta/se. "Better/worse" = games whose win turn moved.
  - Blocks = 8 consecutive equal blocks per round (round 1 + round 2).
  - Units/core-s = arm/ship totals. Units are the budget's own currency and deterministic; core-s is CPU time.
- **Verdicts (standing rule: "almost anything that measures better and is cheaper is a good option").**
  - CLEAN WIN = t <= -2 and cheaper.
  - TRADE = t <= -2 and dearer.
  - NULL = |t| < 2.
  - WORSE = t >= +2.

## Results

| deck | play | games/arm | nlfitL delta (se, t) | L better/worse | L blocks b/w/t | L units | L core-s | **L verdict** | nlfitR delta (se, t) | R better/worse | R units | R core-s | **R verdict** |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hydras (vs heur) | d5 b20 (default) | 8000 | -0.00137 (0.00067, -2.04) | 20/9 | 4/0/4 | 1.161x | 1.072x | TRADE | +0.00387 (0.00105, +3.68) | 20/51 | 0.890x | 0.698x | WORSE (cheaper) |
| fungusv3 | d5 b20 (default) | 4000 + 12000 | -0.00156 (0.00031, -5.00) | 25/0 | 5/0/3 + 8/0/0 | 1.255x | 1.427x | TRADE | +0.00000 (0.00000, +0.00) | 0/0 | 1.032x | 1.051x | NULL, dearer |
| alchemist | d5 b20 | 1000 + 2000 | -0.01267 (0.00270, -4.69) | 36/5 | 7/0/1 + 6/1/1 | 1.269x | 1.564x | TRADE | +0.00000 (0.00000, +0.00) | 0/0 | 1.027x | 1.062x | NULL, dearer |
| midrangeburn | d5 b20 (default) | 1000 + 3000 | -0.00550 (0.00132, -4.17) | 21/1 | 4/0/4 + 8/0/0 | 1.269x | 1.410x | TRADE | +0.00000 (0.00000, +0.00) | 0/0 | 1.031x | 1.049x | NULL, dearer |
| edf | d5 b20 (default) | 1000 + 2000 | -0.00733 (0.00182, -4.03) | 24/3 | 5/2/1 + 7/1/0 | 1.467x | 1.637x | TRADE | +0.00000 (0.00000, +0.00) | 0/0 | 1.052x | 1.076x | NULL, dearer |
| th | d5 b40 | 4000 + 8000 | -0.00167 (0.00044, -3.78) | 22/3 | 5/1/2 + 7/0/1 | 1.153x | 1.171x | TRADE | +0.00000 (0.00000, +0.00) | 0/0 | 1.021x | 1.006x | NULL, dearer |
| snow | d5 b20 (default) | 800 + 1600 | -0.00458 (0.00150, -3.06) | 12/1 | 3/0/5 + 6/0/2 | 1.178x | 1.152x | TRADE | +0.00000 (0.00000, +0.00) | 0/0 | 1.004x | 1.026x | NULL, dearer |
| goblins | d7 b60 | 1000 + 2000 | -0.00167 (0.00074, -2.24) | 5/0 | 2/0/6 + 3/0/5 | 1.074x | 1.309x | TRADE | -0.00200 (0.00141, -1.41) | 2/0 | 1.044x | 1.048x | NULL, dearer |
| merfolk | d5 b20 (default) | 600 + 2000 | -0.00115 (0.00067, -1.73) | 3/0 | 3/0/5 + 0/0/8 | 1.365x | 1.458x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.071x | 1.079x | NULL, dearer |
| mirrorwing | d6 b20 | 4000 | -0.00050 (0.00035, -1.41) | 2/0 | 2/0/6 | 1.238x | 1.241x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.032x | 1.047x | NULL, dearer |
| pirates | d5 b20 (default) | 4000 | -0.00050 (0.00035, -1.41) | 2/0 | 2/0/6 | 1.400x | 1.431x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.047x | 1.037x | NULL, dearer |
| dragons | d6 b20 | 4000 | -0.00025 (0.00025, -1.00) | 1/0 | 1/0/7 | 1.167x | 1.141x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.040x | 1.049x | NULL, dearer |
| jumbo | d6 b20 | 500 | -0.00200 (0.00200, -1.00) | 1/0 | 1/0/7 | 0.970x | 1.214x | NULL, cheaper | +0.00000 (0.00000, +0.00) | 0/0 | 1.035x | 1.013x | NULL, dearer |
| lhc | d6 b20 | 4000 | -0.00025 (0.00025, -1.00) | 1/0 | 1/0/7 | 1.220x | 1.225x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.034x | 1.047x | NULL, dearer |
| soldiers | d6 b30 | 4000 | -0.00025 (0.00025, -1.00) | 1/0 | 1/0/7 | 1.196x | 1.191x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.046x | 1.046x | NULL, dearer |
| fluctuator | d6 b40 | 4000 | -0.00050 (0.00071, -0.71) | 5/3 | 3/1/4 | 1.208x | 1.193x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.008x | 0.992x | NULL, dearer |
| minotaur | d6 b20 | 4000 | -0.00025 (0.00043, -0.58) | 2/1 | 1/0/7 | 1.233x | 1.327x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.021x | 1.031x | NULL, dearer |
| angels | d6 b20 | 4000 | +0.00000 (0.00000, +0.00) | 0/0 | 0/0/8 | 1.109x | 1.069x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.011x | 0.962x | NULL, dearer |
| auras | d5 b20 | 4000 | +0.00000 (0.00000, +0.00) | 0/0 | 0/0/8 | 1.263x | 1.345x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.059x | 1.069x | NULL, dearer |
| colossus | d5 b20 (default) | 4000 | +0.00000 (0.00035, +0.00) | 1/1 | 0/0/8 | 1.205x | 1.234x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.017x | 1.016x | NULL, dearer |
| galadriel | d5 b20 (default) | 4000 | +0.00000 (0.00000, +0.00) | 0/0 | 0/0/8 | 1.069x | 1.068x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.020x | 1.011x | NULL, dearer |
| giants | d6 b20 | 4000 | +0.00000 (0.00000, +0.00) | 0/0 | 0/0/8 | 1.292x | 1.339x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.095x | 1.097x | NULL, dearer |
| kithkin | d6 b30 | 1000 | +0.00000 (0.00000, +0.00) | 0/0 | 0/0/8 | 1.170x | 1.196x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.059x | 1.061x | NULL, dearer |
| selesnyav2 | d6 b20 | 4000 | +0.00000 (0.00000, +0.00) | 0/0 | 0/0/8 | 1.234x | 1.152x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.009x | 0.968x | NULL, dearer |
| slivers | d5 b20 | 4000 | +0.00000 (0.00000, +0.00) | 0/0 | 0/0/8 | 1.187x | 1.221x | NULL, dearer | +0.00000 (0.00000, +0.00) | 0/0 | 1.036x | 1.063x | NULL, dearer |

The Hydras row is vs heur, its shipped shape, on 8,000 games. Those seeds are a subset of P12's 24k, so it is a
replication, not new evidence. P12's 24k gave nlfitL -0.00071 (t -1.91). On the same 8k games, nlfitR vs nlfitL is
**+0.00525 (t +6.50, 0 better / 42 worse, blocks 0/8/0)** at 0.767x units.

### Fleet totals (24 decks, Hydras excluded)

- **nlfitL: 60 games better, 9 worse.** No deck has a positive delta.
  - 7 decks are TRADE at t <= -2 (alchemist, edf, fungusv3, goblins, midrangeburn, snow, th). Every one of the 7
    replicated in direction on fresh seeds.
  - The other 17 are NULL. 7 of those had **no game change at all**: angels, auras, galadriel, giants, kithkin,
    selesnyav2, slivers.
  - Cost on every deck is 1.07-1.47x units and 1.07-1.64x core-s. Jumbo is 0.97x units but 1.21x core-s on only 500
    games.
- **nlfitR: 2 games better, 0 worse (both Goblins).** It changes the PLAY digest on about 0.1% of games and the win
  turn on none of the other 23 decks.
  - Cost 1.00-1.10x units: a null that costs more, on every deck.
- **No CLEAN WIN anywhere**, for either variant.

## Why the two variants come out this way

- **FIT is rare on the fleet, and it runs exactly at the hard decisions.**
  - Fungus trace, 40 games single-threaded: the leafless probe proved the win on 40 of 45 searched decisions. FIT
    ran on only 5.
  - nlfitL's climb on those 5 decisions cost 19,385 units: the whole of its +20% over ship (94,577 -> 113,962 units).
  - nlfitR's climb cost 5,750 units (+6%) on the same 5 decisions. Win turns were identical across all three arms.
  - The climb adds the ladder's deep rollout passes where FIT runs. Those are the decisions the probe could not
    settle, so they are the expensive ones.
- **nlfitR reproduces the old starvation.**
  - It charges the probe, and the probe has typically spent most of the decision before FIT starts (~60% on
    Hydras, per the earlier doc).
  - So the climb rarely gets a pass the old FIT gate would not have given. On the fleet it plays as shipped nlfit
    to within 0.1% of games.
  - On Hydras it climbs less than the ladder and is clearly worse than heur (+0.00387, t +3.68).
  - That is the USER's diagnosis measured from the other side. Once the probe is charged, nlfit searches less than
    the ladder, and the loss returns. **nlfitR is not a usable shape.**
- **nlfitL buys real quality, but it is never free.**
  - On the 7 TRADE decks it finds lines the starved FIT pass missed. Alchemist gains the most: -0.0127 turns, 36/5
    games, t -4.69.
  - It never loses in aggregate on any deck, so the climb's extra depth is not a hazard.
  - The cost is structural. The ladder would also have paid for those deep passes, but the ladder does NOT also pay
    for a leafless probe first.
- **The probe-record fix alone is nearly inert on the fleet.** nlfitR contains the fix and plays like ship to 0.1%
  of games. That answers the earlier doc's question 3: making the fix default-on would move almost nothing outside
  Hydras.

## Recommendations

1. **Drop nlfitR (`ladder_rem`).**
   - The fleet result is NULL and dearer on all 24 decks, and on Hydras it is WORSE than heur.
   - Charging the probe against the climb is the starvation the USER asked to remove.
2. **nlfitL is a TRADE wherever it does anything.** It meets the USER's design intent ("search approximately the
   same as the heuristic ladder or even a bit more"), and the measurement shows what that costs.
   - My recommendation: if the USER accepts the trade, adopt `fit_gate: "ladder"` on the 7 TRADE decks only:
     - Alchemist, EDF, Midrange Burn and Snow (deltas -0.0046 to -0.0127, t 3.1-4.7, 1.18-1.47x units);
     - Fungus v3, Treasure Hunt and Goblins (-0.0016/-0.0017, t 2.2-5.0, 1.07-1.26x units).
   - Leave the other 17 on shipped nlfit, since L buys nothing measurable there and costs 7-40%.
   - Each adoption then needs its normal smoke/regression GT accept.
   - Check the 3x-Hinata suite cost gate before adopting, on Snow and Alchemist in particular. Their L core-s is
     1.15-1.56x on decks already among the suite's dearest.
3. **Hydras** stays as the earlier doc recommends: heur. nlfitL is -0.0007 to -0.0014 at 1.16x units, a TRADE short
   of a clean call.
4. **If the USER wants nlfitL cheaper rather than traded,** the lever is the cost of the climb's deep passes, not
   their admission.
   - Gating the climb on any budget that includes the probe is nlfitR, and it fails.
   - The open direction is to make the climb's passes reuse the probe's work, e.g. seeding the rollout pass from
     the leafless probe's refuted subtrees. That is a search change with its own A/B, not a gate.

## Open USER questions

1. Adopt `fit_gate: "ladder"` on the 7 TRADE decks (better by t 2.2-5.0, 1.07-1.47x units)? Or on none, or on a
   subset (e.g. the four with deltas >= 0.0046)? Until you answer, all decks stay as shipped.
2. Confirm dropping `ladder_rem`. It stays default-off on the local branch and is not pushed.
3. Should Cyclone get the same screen? It was skipped under the 2026-11-04 ruling.

## Artifacts

- Engine branch `fit-fleet` (local, NOT pushed), at `.claude/worktrees/fit-fleet`.
- Scratch `build/scratch/p14/` (regenerable):
  - `fleet.json` (deck list, play settings, suite costs);
  - `v/<key>/{ship,L,R}/` (arm deck dirs);
  - `r1/` and `r2/` (manifest, out/err logs, per-game `.wins`, `analysis.json`);
  - `pooled_L.json`, `table.md`;
  - `tr/` (the Fungus traces).
- Scripts: `mkvariants.py`, `mkmanifest.py`, `run.sh`, `analyze.py RUNDIR`, `pool.py`.
