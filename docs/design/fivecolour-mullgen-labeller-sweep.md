# FiveColour mulligan-gen: the labeller-policy sweep (2026-08-18)

Follow-on to `fivecolour-speed-handoff.md` §4 item 1 — "the NUMBER of nodes (depth / beam / horizon
policy) is the one lever nobody has swept for FiveColour." This sweeps it, with the fidelity harness
the deriver doc prescribed (`mullgen-depth-cost-vs-quality.md` "the no-regeneration route") instead of
regenerating anything. Everything below is measured at HEAD `6732199a` on an idle box.

## 0. HEAD re-verification (the handoff's numbers still hold)

* Gen-config rollout cost is unchanged by the mana-reservation / tap-order arc: 120-game proxy
  (profile attached, `--ignore-play-profile --depth 3 --budget-ms 3 --max-turns 8`, `--threads 1`,
  seed 1001) = **23.9 s ≈ 199 ms/game**, consistent with the measured 4.8–5.7 rollouts/s/core.
* The flat profile is still flat (top symbol `SolveUncached` 6.35%): the six kills stand; no
  micro-fix multiple exists.
* Two corrections to the record:
  - **The value leaf is LOAD-BEARING in gen at HEAD**, not "nearly irrelevant" (its 7.9% EVAL share
    measures its *cost*, not its value): detaching the sidecar (`MTG_VALUE_PROFILE=none`) makes the
    same 120-game d3/b3 run **1.69x slower** (40.3 s vs 23.9 s) and slightly worse (5.1750 vs 5.1667).
    Do not consider "gen without the sidecar".
  - **The Finding-0 depth inversion is GONE at HEAD**: d2/b3 = 24.6 s ≈ d3/b3 = 23.9 s (the
    2026-08-15 table had d2 at 2.4x d3). Do not cite the inversion as current.

## 1. The sweep: budget was the unswept knob

`mullgen-depth-cost-vs-quality.md` swept DEPTH at fixed b3 and never touched BUDGET. On the same
120-game proxy:

| config | wall | avg (turns) | vs d3/b3 labels |
|---|---|---|---|
| d3/b3 (current gen) | 23.9 s | 5.1667 | — |
| d3/b2 | 21.4 s | 5.1667 | — |
| d3/b1 | 17.6 s | 5.1667 | 120/120 same win turns, 116/120 byte-identical logs |
| **d2/b1** | **14.8 s** | 5.1667 | **120/120 same win turns**, 115/120 byte-identical logs |
| d1/b1 | 19.3 s | 5.1750 | worse AND slower — d1 still inverts; do not use |

## 2. Fidelity, measured the right way (rank agreement, not avg)

What decides a labeller is whether it RANKS HANDS like the shipped policy, not its in-play avg
(`mullgen-depth-cost-vs-quality.md`: a uniform shift changes nothing; DISPERSION flips keeps —
same lesson as `deck-screening`). Harness: the comp-scorer's hand mode, which exists precisely for
decks with no mulligan artifacts:

```
MTG_SCORE_COMPS=1 MTG_SCORE_HANDS=200 MTG_SCORE_R=30 \
MTG_EQUIV_DEPTH=<d> MTG_SCORE_BUDGET_MS=<b> \
  build/Release/mtg-analyze decks/FiveColour/FiveColour.cod --cards-json src/cards/data/cards.json
```

200 openers from the real opening distribution, FORCED KEPT (the gen shape), paired seeds across
arms, R=30 each side. Reference = the shipped play policy (hybrid d6/b20). `work_units` is the
deterministic cost currency (immune to load, unlike wall). V arms add
`MTG_VALUE_MODEL=1 MTG_VALUE_MIN_DEPTH=0 MTG_VALUE_STARTGATE_ALPHA=8` (the depth-matrix V-cell env).
Analysis: `logs/fc_labeller_ab/analyze.py` (arm outputs in the same dir).

| arm | units/rollout | cost vs CUR | rho (play) | mean shift | dispersion sd | pair-agree (>0.5t pairs) |
|---|---|---|---|---|---|---|
| REF hybrid d6/b20 | 92,610 | 0.19x (5.1x dearer) | 1 | 0 | 0 | — |
| CUR hybrid d3/b3 | 18,056 | 1.00x | 0.9906 | +0.036 | 0.067 | 100.0% |
| **H21 hybrid d2/b1** | **10,804** | **1.67x cheaper** | **0.9891** | +0.056 | **0.081** | **100.0%** |
| V41 pure-V d4/b1 | 9,321 | 1.94x cheaper | 0.9680 | +0.218 | 0.143 | 100.0% |
| V21 pure-V d2/b1 | 4,231 | 4.27x cheaper | 0.9533 | +0.328 | 0.199 | 99.5% |

**H21 is fidelity-equal to the current setting** — same 100% pairwise ordering agreement, rho and
dispersion within a hair of CUR's own distance from the reference — at 1.67x less work. Draw-side
numbers agree (H21 rho 0.9849 vs CUR 0.9850).

**The V arms are NOT uniformly worse — they are biased exactly where this deck lives.** V21's five
worst residuals are all dork-ramp hands (worst: double Faeburrow + Bloom Tender + Jared, +0.97 turns
pessimistic after removing the mean shift). A labeller that systematically under-rates Bloom
Tender/Faeburrow hands shifts keeps AWAY from the deck's engine hands; the mull-side sub-tables
(labelled by the same policy) only partially cancel this, since a 7-card dork hand is more
dork-concentrated than its average 6-card sub-hand. So V21's 4.27x is real but it buys a *biased*
profile; it is an option only with eyes open, not a default.

## 3. The degenerate tail at HEAD

The 253 banked slow reproducers (`test/slow_repro/`) predate the mana-cache §4/§5 fixes, the state
reuse, and the tap-order arc, and are not seed-exact at HEAD (K=31→27 bucketing; a fetch label like
`Misty Rainforest` no longer resolves — use the merged class representative). Structural replay of
the worst fetch-free hands (`logs/fc_labeller_ab/tail_probe.sh`, 20 rollouts each, play side):

| arm | 4-hand total (20 rollouts each) | worst single rollout | labels vs CUR (80 paired) |
|---|---|---|---|
| CUR d3/b3 | 20.8 s | 1.75 s | — |
| H21 d2/b1 | 15.7 s (1.33x) | 2.82 s | 76/80 identical; 4 at +1 turn |
| V21 pure-V d2/b1 | **4.2 s (5.0x)** | 0.37 s | 62/80 identical; 18 differ, **all worse** (two +2), heaviest on the Faeburrow x3 / Bloom hands |

Three conclusions:

1. **The 30-minute atoms are GONE at HEAD.** The 255 s / 104 s capture-era hands label in 0.1–0.6 s
   per rollout under CUR now — the mana-cache §4/§5 + state-reuse + tap-order arcs already tamed the
   tail. The mean rate, not the tail, is the feasibility constraint today, which is why the labeller
   policy is the right lever.
2. H21 is not uniformly faster on the tail (one hand ran 2.3x slower — a shallower policy can play
   longer games), but is cheaper in aggregate and its rare label shifts are +1s indistinguishable
   from the 200-hand dispersion.
3. The V arm's tail numbers repeat the fidelity verdict in miniature: enormous cost win, one-sided
   pessimism on exactly the hands the deck keeps for.

Replay seeds are paired across arms (same `rs = f(seed, r, w, pd)` — same HEAD bucketing in every
copy), so these win-turn comparisons are element-wise, not distributional. One extra V-arm adoption
caveat found here: the V env is process-wide, so it would also change DISCOVERY's probe rollouts and
re-bucket the deck — arm selection must be scoped to label rollouts only (`valuearm::t_arm` per
worker), exactly the mistake the 2026-08-15 discovery/gen split exists to prevent.

## 4. Feasibility, restated

Scaling the recommend probe's own projections (FAST R30-adaptive 220.3 h, COMPLETE R40 440.6 h on
23 cores, `fivecolour-mulligan-and-slow-atom.md` §7) by the deterministic unit ratios:

| labeller | FAST (R30 adaptive) | COMPLETE (R40) | FAST on 2 pooled boxes |
|---|---|---|---|
| CUR d3/b3 | 220 h | 441 h | 110 h |
| **H21 d2/b1** | **~132 h** | ~264 h | **~66 h** |
| V41 d4/b1 | ~114 h | ~228 h | ~57 h |
| V21 d2/b1 | ~52 h | ~103 h | ~26 h |

Notes that condition these numbers:

* Changing `mull_gen_*` changes the `RolloutCfg` stamp (d/b/t comparability gate), so the banked
  d3/b3 floor probe (`.raw.json.probe`, 10.9 h) is **forfeited** by any labeller change — small
  against the saving, but it is a real write-off.
* Any V-arm adoption needs a mechanism first: gen reads depth/budget from `value_play.mull_gen_*`,
  but the value-arm selection is env-only today (`valuearm::t_arm` / process env). Wiring it as a
  profile field must ALSO stamp it into `RolloutCfg`, or two sidecars generated under different arms
  would read `Match` and pool.
* Discovery re-derives at today's HEAD (the committed `gencache` is commit-keyed): measured ~10 min
  wall on 24 cores, once per gen start. Not material.

## 5. Recommendation (decision is the user's)

Adopt **`mull_gen_depth: 2` / `mull_gen_budget_ms: 1`** for FiveColour (two fields in
`decks/FiveColour/FiveColour.value.json` + note). Measured fidelity-equal to the current d3/b3 by
every metric the deriver framework defines, 1.67x cheaper on the exact gen-shaped workload. This puts
FAST at ~132 h single-box / ~66 h split across two — still not an overnight, so the remaining choice
is scheduling (a week-long single-box run, the two-machine pool per
`.claude/skills/mulligan-profile.md`, or a lower tier), which is a user call, not an engineering one.

V21 (pure value-leaf d2/b1, 4.27x, ~26 h on two boxes) is documented above as the aggressive option;
its measured dork-hand bias is the argument against. A future design that generates the floor under
V21 and re-labels only near-margin cells under H21 would capture most of the 4.27x without the bias,
but cross-config pooling is exactly what the RolloutCfg gate exists to refuse — that is a real
design, not a config change.

## 6. Follow-on (2026-08-18, same day): the per-game structural levers, measured

The user asked why per-game cost is high. Decomposition at the gen config (d2/b1, 120 games,
`MTG_ENUM_STATS` + `MTG_ROLLOUT_STATS`, FiveColour vs Goblins): to play one ~5-turn game the search
simulates **~4,700 hypothetical turns** (Goblins: 235) — 14,347 enumeration calls, 529k odometer
positions, 2,495 leaf evals per game. Work volume is ~20x Goblins while wall is ~4.3x (FiveColour is
CHEAPER per unit of work); the whole gap is volume: 1.4x longer games × more decisions/turn × more
plans/decision (land fan + affordability barely pruning) × horizon amplification. None of it is
redundant (the 1.00x dedup collapse).

Three candidate levers were then sized/measured (user-directed). Hatches added for the sweep, both
value-carrying ints, default 0 = off = byte-identical: `MTG_LAND_FAN_CAP` (truncate the
land-candidate fan, greedy pick first) and `MTG_FETCH_FAN_CAP` (cap fetch targets below the provider
cap and the collapsed-main override).

| lever | measured (120-game proxy, d2/b1 base 15.5 s / 5.1667) | verdict |
|---|---|---|
| land-name fan cap=1 | 25.0 s, avg 5.3583 — **slower AND worse** | DEAD. Greedy land choice plays out badly enough that longer games swamp the enumeration saving — the same inversion shape as d1. The land fan is load-bearing for this deck. |
| fetch-target fan cap=1 | 10.7 s (1.45x), avg 5.3500; fidelity rho 0.9567/0.9289, dispersion 0.163, pair-agree **99.1%** at 6,824 units/rollout | DEAD — **dominated by V21** (4,231 units, 99.5% pair-agree, rho 0.9533/0.9490). Anyone accepting this fidelity class should take V21 instead. Consistent with the in-play warning already in the code (5c d3 s2002: cap 5.0150 vs no-cap 4.9250). |
| two-stage odometer digit-gating | sized from the enum stats at gen config: saves 5.6M of 63.5M odometer positions (8.8%); the odometer walk is ~10–15% of runtime → **~1–2% net** | NOT BUILT — below the user's stated headroom bar ("digging into 4% isn't worth our time", 2026-08-15). The 1.82x "two-stage visits" line applies only to the 8% of calls with a mana side. |
| cheaper policy for later rollout turns | not built | user expects little vs d2/b1; no existing knob; escalation re-traversal is only 6.8% of interior nodes at d2/b1, bounding the adjacent "skip escalation" idea below the bar too. |

Both hatches lost; per coding-conventions rule 5 they stay until this record + user sign-off, then
the losing branches get deleted. Net of the whole arc: **the per-game levers that survive are the
labeller policy itself (H21, 1.67x) and V21 (4.27x, biased)** — the deck's per-game cost is
genuinely its option volume, not an inefficiency with a switch, which is the same conclusion the
six kills reached about per-node cost.
