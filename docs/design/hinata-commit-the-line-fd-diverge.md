# Hinata: three SEVERE commit-the-line divergences (root-caused, pre-existing)

**Status: root-caused, not yet fixed.** Found while auditing the 2026-08-25 overnight tier before
`--accept`. These are **pre-existing** — they are not in the changed-game set of the rebaseline
under review, so they do not block that accept, but they are a real fidelity gap.

## The finding

`MTG_FD_ORACLE=1` over the whole overnight matrix (197,600 games) reports 30 `[fd-diverge]` lines
(commit-the-line predicted a win earlier than the game realized). Categorised by
`realized - predicted`:

- **27 off-by-one** — the known minor rollout optimism. This run establishes the baseline count;
  per the regression skill it must not grow run-over-run.
- **4 severe (delta ≥ 2)** — 1 Dragonstorm, **3 Hinata**.

`nonconv = 0` on every deck.

The oracle line prints only `seed=`, and every deck has an `s4004` job, so attribution is impossible
from the log alone — it needed a per-deck oracle run. **Worth fixing: add the job/deck label to the
`[fd-diverge]` emit in `AIEngine.cpp:404`.**

## The three Hinata games

All reproduce at gate settings. Identity requires BOTH `--seed base+gi` and `--game-index gi`
(see below).

| cell | gi | seed | job | default @gate | legacy @gate | default UNBOUNDED |
|---|---|---|---|---|---|---|
| `d3_s4004` | 160 | 4164 | 7 | 7 (predicted 5) | **6** | 7 — still diverges |
| `d5_s4004` | 107 | 4111 | 7 | 7 (predicted 5) | **6** | **5** — recovers |
| `d5_s4004` | 291 | 4295 | 7 | 7 (predicted 5) | **6** | 7 — still diverges |

Reading, per the regression skill's Pass 2c:

- **Legacy per-turn re-deciding beats commit-the-line by a full turn in all three** (T6 vs T7).
  By the skill's own criterion — *legacy wins, default loses ⇒ commit-the-line regression* — these
  are commit-the-line regressions, not horizon-edge noise.
- **gi107 is budget starvation**: unlimited budget finds T5, which is exactly what the search
  predicted. More budget recovers the predicted line.
- **gi160 and gi291 are NOT budget**: they still realize T7 and still diverge at unlimited budget.
  The committed line genuinely misfires on the realized board — a fidelity gap, not truncation.

Scale: 3 games in 10,800 (0.03%). Small, but it is the deck's deep-search home and the divergence
is 2 full turns.

## Reproduction

```
# d3 gi160
MTG_DUMP_WINS=1 MTG_FD_ORACLE=1 ./build/Release/mtg decks/Hinata2/Hinata2.cod \
  --profile decks/Hinata2/Hinata2.profile.json --games 1 --seed 4164 --game-index 160 \
  --depth 3 --budget-ms 10 --ignore-play-profile --threads 1
# d5 gi107 / gi291 -- NO --depth and NO --ignore-play-profile (value_play owns the depth)
MTG_DUMP_WINS=1 MTG_FD_ORACLE=1 ./build/Release/mtg decks/Hinata2/Hinata2.cod \
  --profile decks/Hinata2/Hinata2.profile.json --games 1 --seed 4111 --game-index 107 \
  --budget-ms 20 --threads 1
# add MTG_LEGACY_SEARCH=1 for the legacy arm; --budget-ms 0 for unbounded (0 = unlimited)
```

## TRAP: game identity needs BOTH seed and --game-index

A game's identity is **not** just `base + gi`. Measured 2026-08-25 on Hinata d5 gi107:

```
--seed 4111 --game-index 107  -> wt=7   <- matches the job
--seed 4111                   -> wt=5   <- WRONG, silently a different game
--seed 4004 --game-index 107  -> wt=5   <- WRONG
```

Omitting `--game-index` does not error; it silently plays a different game. It only matters for
*some* games, which is what makes it dangerous: a spot-check can pass 5/5 and still be using the
wrong method. **A `--batch` manifest job with `games: 1` cannot express this** (its index is 0), so
single-game reproduction must go through the CLI with an explicit `--game-index`, not through a
one-game manifest job.
