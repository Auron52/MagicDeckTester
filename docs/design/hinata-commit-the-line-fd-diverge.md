# Hinata: three SEVERE commit-the-line divergences (CLOSED -- lockstep #5 and #14)

**Status (2026-10-17): CLOSED.** None of the three reproduces on current HEAD, at the gate budget or
unbounded. Each was a real executor/rollout lockstep defect, and each was fixed at its source on
2026-10-01/02. Lockstep **#5** is `fef4a54b`, which fixed gi107. Lockstep **#14** is `60c168f8`, which
fixed gi160 and gi291. Both are in `rollout-executor-lockstep.md`. Before those fixes landed, unrelated
play changes had already moved each game off its divergent line and *masked* it. See "2026-10-17
re-check" below. The original status (2026-08-25) read "root-caused, not yet fixed". It had named the
class (commit-the-line), but not the mechanism.

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

*(2026-10-17: a manifest job now carries `"game_index"` (BatchRunner `Job::game_index`, the chunk
offset), so a one-game job with `seed = base + gi` and `game_index = gi` IS the same game.
`test/fd_pinned_games.sh` reproduces these games exactly that way.)*

## 2026-10-17 re-check: the three mechanisms, the fixing commits, and what masked them

**Method.** The doc's own repro commands were run first on HEAD (`aecc265d`). All three games win on
T5 with no `[fd-diverge]`, at the gate budget and at `--budget-ms 0`, and T5 matches committed GT.
The doc's commit `f96be01f` was then rebuilt clean, and it still reproduces exactly: 7/7/7 against a
predicted 5. Each game was bisected with single-game predicates between the overnight accepts at
which its GT row moved, and the first difference was read off `MTG_BP_TRACE` / `MTG_FD_TRACE` /
`MTG_DISCARD_TRACE` on the ORIGINAL binary.

| game | first difference (original binary) | lockstep | real fix | first masked by |
|---|---|---|---|---|
| d5 gi107 (s4111) | T5 Gamble: both worlds draw victim **index 8**, but the hands are stored in different orders (Forbidden Orchard#10 is at index 3 in the executor, index 10 in the rollout). The search sheds Ornithopter#37, the executor sheds Mountain#30, keeps the Ornithopter, casts it, and Reality Spasm's {U}{U} is unpayable. | **#5** | `fef4a54b` (rollout lays staged cards last, as the executor does) | `e5e2eb16` (Irencrag Feat / Reality Spasm float true colours, not WILD) changed T1-T4: `MTG_LEGACY_RITUAL_WILD=1` on that binary restores the divergence |
| d3 gi160 (s4164) | T3: the search casts Gamble, then Expressive Iteration, then the DEFERRED continuation (Preordain, then Ponder). The executor replays the continuation INLINE at Gamble and casts Expressive Iteration afterwards. The library is dug in a different order, the T4 hand lacks Forbidden Orchard and Reflecting Pool, and Hinata is unpayable on T4. | **#14** | `60c168f8` (deferred acquisition continuations replay at the deferred point) | between `db280397` and `f1037a32` (2026-09-11..17); it was already 7 -> 6 at `e5e2eb16` |
| d5 gi291 (s4295) | T4: the same shape. The search casts Ponder, Gamble, Expressive Iteration, then [Reflecting Pool, Preordain]. The executor replays [Reflecting Pool, Preordain] at Gamble, paying Preordain from the float, and then Expressive Iteration FAILS. The T5 hand differs and the T5 kill (Hinata, then Spasm/Crackle/Spasm) has no mana. | **#14** | `60c168f8` | `e30e4f5f` step 33 (main 2's deferred wave phase made unconditional: `MTG_M2_WAVES=1` on `fef4a54b` recovers T5) changed the T1 line one commit earlier |

**The gi291 attribution is proven directly.** `fef4a54b` gives T7, diverged. The same tree with ONLY
`60c168f8` cherry-picked gives **T5, no divergence**. gi107's mechanism is #5 by the discard trace;
the #5 fix's own measurement (executor Gamble discards with a same-order, same-victim search twin:
313/321 -> 322/322 on hinata smoke + regression) covers it.

**Why the doc's "unbounded" column split as it did.** A lockstep defect is invisible to depth and
budget: the search was right and the game played something else. gi160 and gi291 "still diverged at
unlimited budget" for exactly that reason. gi107 "recovered" at unlimited budget only because the
larger search chose a T5 line that did not route through the mis-ordered Gamble discard. It was not
budget starvation.

**Bisect hazard met on the way (now in `repo-mount-failures.md`).** The 9p host clock runs ~195 s
behind the container. A file rewritten by `git checkout` can look older than an object built
minutes earlier, so ninja silently links a stale mix. An old-commit binary built that way aborted
at startup (a HeuristicArm slot/name mismatch from two commits' worth of objects). Every bisect
build here future-stamped the files the checkout changed.

**Pins.**
* `test/unit/test_staged_lockstep.cpp`: #5 at the mechanism level. A card drawn while staged
  cards are out sits before them in both worlds (`MergeStagedIntoHand` vs
  `TurnSolver::LayStagedCardsLast`), so Gamble sheds the same physical card. It also covers the #11
  fix that landed with this re-check (see the lockstep doc).
* `test/fd_pinned_games.sh`: replays these three games plus the original #5/#14 repros (reg d5
  s2002 gi25, ov d5 s5005 gi13, reg d3 s3003 gi24) in ONE pooled batch under `MTG_FD_ORACLE=1`. It
  fails on any `[fd-diverge]` or any mismatch with the committed per-game GT. All six are clean
  on 2026-10-17.
* The `[fd-diverge]` line now carries `job=<batch job> gi=<game index>` (the "worth fixing" note
  above), so a pooled run attributes its own divergences.
