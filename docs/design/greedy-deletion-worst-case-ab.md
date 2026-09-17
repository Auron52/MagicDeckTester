# Greedy-deletion worst-case A/B — which regressions are real (2026-09-17)

**Status: MEASURED, follow-up PARKED.** The measurement below is complete and needs no
re-running. The investigation it points at has not been started.

## What this answered

After the greedy `Solve()` continuations were deleted from the searched window
(`docs/design/greedy-continuation-deletion-route.md`), the three regression tiers showed a
handful of decks scoring worse. Those tier cells are small — several "regressions" rested on
under ten divergent games — so it was unclear which were real effects and which were
small-sample artifacts. This run settles that on fresh seeds at ~30x the tier sample.

## Method

Paired A/B, **`63dd9ce3` (pre-deletion) vs `80d0fa3f` (shipped)**, identical seeds in both arms.

- **Deck set:** every deck scoring worse *overall* across the tiers (hinata, th, dragonstorm,
  dragons, kitty, critter, creature_giving, burn, antilife) **or** worse specifically at d5 =
  play settings (dragonstorm, melira, dragons, burn, antilife). Union = 10 decks.
- **Cells:** `d3 b10` (the gate / mulligan-generation setting) and `d5` with the `depth` key
  dropped so each deck's `value_play` block owns it — burn resolves to depth 6, the rest to 5,
  at the block's default 20 ms. That is the shipped **play** configuration, verified in the
  engine's own `[play] ... source=value_play(depth)` lines.
- **Scale:** 1,364,000 games per arm, 259 pooled jobs, one `--batch` per arm (the only barrier
  is the one two binaries force). Seeds **9,000,001–10,623,001**, disjoint from every tier
  (1001 / 2002 / 3003 / 4004–10010) and from the earlier units (5,500,001) and ladder
  (8,800,001) probes.
- **Apparatus validated before launch:** each binary reproduced its own committed ground truth
  **byte-identically** on the same tree (dragonstorm smoke d3: new `a4b4cc8f…`, old
  `56d8fb88…`). `cards.json` is identical across the two commits and only `src/ai/` differs, so
  the binary is the sole variable.

### Why the statistic is a sign test, not just a mean

Both arms are deterministic, so almost every game is byte-identical between them and carries no
information; only games whose outcome *moved* do. Divergence rates span three orders of
magnitude across this deck set (hinata moves 1 game in 16 at d3; burn 1 in 5,350), so each cell
was sized on **expected divergent games**, not on games. Reported per cell: the game-weighted
mean turn delta with its paired standard error, **and** an exact two-sided binomial sign test on
faster-vs-slower, which one freak game cannot swing. A loss scores `max_turns+1 = 9`.

## Caveat: the shipped binary has moved since this measurement

This A/B names two commits: `63dd9ce3` and `80d0fa3f`. Two condemnation commits
(`f98e79c6`, `3547e67a`) landed on the branch afterwards and touch `src/ai/TurnSolver.cpp`
— the same file the deletion lives in. They are gated on `MTG_BP_CONDEMN_ALLPATHS`, read via
`EnvOn("MTG_BP_CONDEMN_ALLPATHS")` with no default argument, i.e. **default OFF** ("DEFAULT OFF
until measured", TurnSolver.cpp:2005), so the default path should be unchanged and the results
below should carry over. That has NOT been confirmed by a byte-identity smoke run — do that
before treating these numbers as current, per the rebase rule in CLAUDE.md.

## Result

Positive = the shipped binary is SLOWER (worse). Full table: `logs/worst_ab_2026-09-17/RESULTS.md`.

### At play settings (d5) — the setting that matters

| deck | games | turns/game | t | faster | slower | wins L/G | verdict |
|---|---:|---:|---:|---:|---:|---:|---|
| dragonstorm | 60,000 | **+0.00197** | +8.4 | 23 | 136 | 1/1 | **REAL, strongly one-directional** |
| th | 40,000 | **+0.00195** | +3.3 | 167 | 206 | **20/4** | **REAL, driven by lost wins** |
| antilife | 160,000 | +0.00025 | +5.8 | 4 | 43 | 1/0 | real, one-directional, tiny |
| kitty | 40,000 | +0.00017 | +2.3 | 1 | 8 | — | marginal (9 divergent games) |
| critter | 120,000 | +0.00002 | +1.4 | 0 | 2 | — | noise |
| dragons | 160,000 | +0.00002 | +0.5 | 15 | 18 | 1/1 | **noise** |
| melira | 8,000 | −0.00025 | −0.3 | 14 | 12 | — | **noise** |
| burn | 160,000 | −0.00011 | −3.3 | 22 | 5 | — | **real IMPROVEMENT** |
| creature_giving | 40,000 | −0.00055 | −2.0 | 70 | 45 | 1/0 | real improvement |
| hinata | 12,000 | **−0.00925** | −3.8 | 400 | 299 | 17/22 | **real improvement** |

Of the five decks that looked worse at d5 in the tiers: **dragonstorm confirmed**, **antilife
confirmed but negligible**, **dragons and melira are noise**, and **burn REVERSED** into a real
improvement (22 faster against 5 slower).

### At d3 b10 — uniformly worse, and about double the tier estimate

Every deck but melira (t=+0.67, noise) is significantly worse. Dragonstorm **+0.00808** against
the tier's +0.00357; th **+0.00980** against +0.00519. Pooled **+0.00211/game, t=+18.9**. This
cell is a useful input (it is a candidate mulligan-generation setting) rather than a shipped
play setting, so the cost is tolerable — but it is systematic, not noise.

### Two cautions on reading the pooled rows

1. **The deck set is selected for being negative.** Every deck the change helped — fluctuator,
   mirrorwing, goblins, auras, fivecolour — is excluded by construction. The pooled
   "+0.00012/game at play settings (t=2.20, sign p=0.076)" is the worst case *within the worst
   cases*, not a suite-level number.
2. **th's d5 sign FLIPPED between seed sets.** The tiers measured th at −0.00193 (better) at d5
   over 4,675 games; these 40,000 fresh games give +0.00195 (worse) at t=+3.3. Both are honest
   measurements on disjoint seeds. The direction here is independently supported by the sign
   test (167 vs 206, p=0.0067), and this sample is ~9x larger — but the flip means th's d5
   behaviour is seed-sensitive, and any single-tier reading of it should be distrusted.

## Detail on the two real regressions

**th @ play settings**, 397 of 40,000 games moved, net +78 turns. The ±1 churn is nearly
symmetric (162 faster / 195 slower, +33). The damage concentrates in **20 lost wins against 4
gained**, contributing +38 of the +78. Those 20 split sharply:

| the old binary won on | count | reading |
|---|---:|---|
| T8 | 11 | slipped one turn past an arbitrary horizon — least alarming |
| T7 | 4 | |
| T6 | 1 | |
| **T5** | **4** | **four-turn regressions — the real concern** |

**dragonstorm @ play settings**, 161 of 60,000 games moved: 23 faster, 136 slower, 1 lost /
1 gained. Broad systematic slowdown rather than a lost-win cliff — the signature of a search
that stopped *finding* something, not one that got unlucky.

Also unexplained: the shipped arm logged 14 games over 30 s, **13 of them dragonstorm d5**; the
pre-deletion arm logged none across the same 1,364,000 games. Wall thresholds are
contention-sensitive on this box, so this needs a controlled repro before it is believed.

## PARKED follow-up

Nothing below has been started.

1. **Budget ladder on the two real regressions.** Re-run th's 20 lost wins and dragonstorm's
   136 slower games on both binaries at budgets 20 → 40 → 80 → 160 → 320. If the shipped binary
   recovers with budget, these are starvation (a resource problem, like hinata at d3) and not a
   quality hole. If th's four T5 games stay lost at 16x budget, that is a genuine refutation
   hole in the searched window and needs a plan-level trace.
   The manifest supports pinning one game — `"seed": base+gi, "game_index": gi, "games": 1` —
   so the whole ladder pools into one queue rather than ~200 single-game invocations. The
   per-game repro lines are already extracted:
   `logs/worst_ab_2026-09-17/divergent_{th,dragonstorm,kitty}_d5.txt`.
2. **Extensions for the two marginal calls:** kitty d5 (9 divergent games, p=0.039) and melira
   d5 (8,000 games only) on fresh seeds at ~3x, seeds from 10,700,001 to stay disjoint.
3. **Dragonstorm's slow-game tail** — controlled repro of the 13 games, uncontended.
4. Still open from the parent doc: the **d0 scope leak** (`MTG_BP_BASE_CANON` fires at depth 0,
   which has no search; gating on `depth > 0` costs 5 turns over 200,000 games) and the
   **empty-arm doctrine gap** ("stop here" is not a scored option at a base-plan slot).

## Reproducing

```
python3 logs/worst_ab_2026-09-17/make_manifest.py     # writes worst_ab.json (259 jobs)
bash    logs/worst_ab_2026-09-17/run_ab.sh            # both arms, ~2 h each on 24 cores
python3 logs/worst_ab_2026-09-17/analyse.py           # the paired table above
python3 logs/worst_ab_2026-09-17/divergent.py <deck> <depth>   # per-game repro lines
```

The old arm needs a `63dd9ce3` build; `run_ab.sh` expects it at `/tmp/pre63/build/Release/mtg`
(`git worktree add /tmp/pre63 63dd9ce3 && cd /tmp/pre63 && ./build.sh`).

## Appendix — repro seeds for the parked ladder

Regenerating these costs 2.7 M games, so they are recorded here. Each line is a complete
single-game repro at PLAY settings (no `depth` key, no `--ignore-play-profile` — the deck's
`value_play` block owns the depth). Add the deck's `--deck`/`--profile`, plus
`--games 1 --max-turns 8`: omitting `--max-turns` plays a different race and will not reproduce.
In a pooled manifest the same game is `"seed": <seed>, "game_index": <gi>, "games": 1`.

### th @ d5 — all 20 lost wins

```
--seed 9110696   --game-index 361     # old won T5, shipped does not win by T8
--seed 9116734   --game-index 6399    # old won T5, shipped does not win by T8
--seed 9129646   --game-index 3977    # old won T5, shipped does not win by T8
--seed 9139878   --game-index 6542    # old won T5, shipped does not win by T8
--seed 9111588   --game-index 1253    # old won T6, shipped does not win by T8
--seed 9095784   --game-index 783     # old won T7, shipped does not win by T8
--seed 9112638   --game-index 2303    # old won T7, shipped does not win by T8
--seed 9118277   --game-index 275     # old won T7, shipped does not win by T8
--seed 9122874   --game-index 4872    # old won T7, shipped does not win by T8
--seed 9096718   --game-index 1717    # old won T8, shipped does not win by T8
--seed 9097063   --game-index 2062    # old won T8, shipped does not win by T8
--seed 9100160   --game-index 5159    # old won T8, shipped does not win by T8
--seed 9100843   --game-index 5842    # old won T8, shipped does not win by T8
--seed 9113549   --game-index 3214    # old won T8, shipped does not win by T8
--seed 9113952   --game-index 3617    # old won T8, shipped does not win by T8
--seed 9118474   --game-index 472     # old won T8, shipped does not win by T8
--seed 9118773   --game-index 771     # old won T8, shipped does not win by T8
--seed 9121452   --game-index 3450    # old won T8, shipped does not win by T8
--seed 9122971   --game-index 4969    # old won T8, shipped does not win by T8
--seed 9131694   --game-index 6025    # old won T8, shipped does not win by T8
```

### dragonstorm @ d5 — the 25 worst slowdowns (of 136) plus its one lost win

```
--seed 9189574   --game-index 1573    # +3  T3 -> T6
--seed 9197207   --game-index 3206    # +2  T4 -> T6
--seed 9201197   --game-index 1196    # +2  T3 -> T5
--seed 9206632   --game-index 631     # +2  T4 -> T6
--seed 9216645   --game-index 4644    # +2  T4 -> T6
--seed 9225867   --game-index 1866    # +2  T4 -> T6
--seed 9232891   --game-index 2890    # +2  T3 -> T5
--seed 9188649   --game-index 648     # +1  T4 -> T5
--seed 9189092   --game-index 1091    # +1  T4 -> T5
--seed 9189377   --game-index 1376    # +1  T4 -> T5
--seed 9190670   --game-index 2669    # +1  T5 -> T6
--seed 9190811   --game-index 2810    # +1  T5 -> T6
--seed 9191222   --game-index 3221    # +1  T4 -> T5
--seed 9191225   --game-index 3224    # +1  T3 -> T4
--seed 9192515   --game-index 4514    # +1  T7 -> T8
--seed 9194339   --game-index 338     # +1  T5 -> T6
--seed 9195299   --game-index 1298    # +1  T4 -> T5
--seed 9195531   --game-index 1530    # +1  T3 -> T4
--seed 9196819   --game-index 2818    # +1  T4 -> T5
--seed 9197039   --game-index 3038    # +1  T5 -> T6
--seed 9197064   --game-index 3063    # +1  T7 -> T8
--seed 9197118   --game-index 3117    # +1  T5 -> T6
--seed 9198026   --game-index 4025    # +1  T5 -> T6
--seed 9201069   --game-index 1068    # +1  T5 -> T6
--seed 9201110   --game-index 1109    # +1  T4 -> T5
--seed 9201707   --game-index 1706    # +1  T4 -> T5
```

### kitty @ d5 — all 9 divergent games (the marginal call, p=0.039)

```
--seed 9680792   --game-index 714     # +1  T6 -> T7
--seed 9708909   --game-index 292     # +1  T5 -> T6
--seed 9708950   --game-index 333     # +1  T5 -> T6
--seed 9711273   --game-index 2656    # +1  T6 -> T7
--seed 9713117   --game-index 423     # +1  T5 -> T6
--seed 9713186   --game-index 492     # +1  T6 -> T7
--seed 9719074   --game-index 2303    # -1  T7 -> T6
--seed 9725714   --game-index 789     # +1  T6 -> T7
--seed 9727915   --game-index 2990    # +1  T7 -> T8
```
