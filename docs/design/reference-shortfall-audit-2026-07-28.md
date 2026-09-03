# Reference shortfall audit — all 140 user references (2026-07-28)

Full per-game sweep of `references/<deck>/*.json`: HUMAN vs the shipped **clairvoyant** search
(each deck's committed `value_play`) vs the **non-clairvoyant** reshuffle search
(`MTG_NC_SEARCH` K=8 d=2), on the *identical* opening hand (`--force-mulligan` reconstructs each
reference's exact kept hand, so mulligan differences cannot contaminate the comparison).

> **Status update.** The TH shortfall found here (`claude_s2_gi1`, human 4 / search 6) is
> **fixed** — see [clairvoyant-reference-shortfalls.md §A5](clairvoyant-reference-shortfalls.md).
> With `MTG_TH_HOLD_FOR_DIG=1 MTG_TH_DROP_YIELD=1` the clairvoyant shortfall count is **3 → 2**
> (both survivors Dragonstorm class B). The tables below are the pre-fix baseline.
> *(2026-09-03: neither flag exists under those names now — hold-for-dig ships default ON behind
> the inverted hatch `MTG_NO_TH_HOLD_FOR_DIG`; `MTG_TH_DROP_YIELD` has no reader under any
> polarity. `scripts/ref_line_replay.py` has moved to `scripts/attic/`.)*

Repro:

```bash
python3 scripts/ref_bench.py --deck all --threads 12 --nc-threads 6 --out logs/ref_bench
python3 test/viewer_protocol_check.py          # does each human line still replay?
```

## Two harness fixes this audit required

Both were silently producing wrong answers before this run.

1. **`scripts/ref_bench.py` could not run at all.** Its `DECKS` map still used the pre-move flat
   paths (`decks/Anti-Lifegain.cod`), and it always passed `--depth 5`, which now hard-errors
   against every reference deck's *enabled* `value_play` lock. Fixed: per-deck folder paths,
   Dragonstorm added, `--deck all`, and `--depth` omitted by default so the run measures the policy
   that actually ships (an explicit `--depth` now auto-adds `--ignore-play-profile`). Also: derive
   `max_turns` from the slowest human win (the Hinata2 T9 reference was being scored as a loss),
   handle `"mulligan": null`, and flag CLAIR-vs-human shortfalls (it previously only flagged NC).

2. **`test/viewer_protocol_check.py` was off by one on 131 of 140 references.** It folded the
   recorded *mulligan* and *bottom* picks into the positional `--choices` stream while also passing
   `--force-mulligan`. Under force the engine resolves both internally and never calls the external
   chooser for them, so the keep/mull answer was being consumed by the **turn-1 main phase** and
   every downstream pick then read as "enumeration drift". Fixed (`drop_mulligan` mirrors
   `force_arg` and covers `FORCED_MULLIGAN_TYPES = {mulligan, bottom}`), plus the same stale deck
   paths and a `--max-turns 8` hardcode that made the T9 Hinata reference unreplayable.
   **Drift went 107 → 27; ok went 32 → 112.** Most of the reported "behaviour drift" in this
   harness was never real.

## Result

Loss-penalised avg win turn (LP), lower is better; `n` = references for that deck.

| deck | n | human | clairvoyant | NC | clair short of human | NC short of human |
|------|---|-------|-------------|-----|---------------------|-------------------|
| antilife | 13 | 4.231 | **4.077** | 4.615 | 0 | 3 |
| burn | 16 | 4.625 | **4.375** | 4.438 | 0 | 1 |
| slivers | 10 | 4.200 | 4.200 | 4.200 | 0 | 0 |
| knights | 28 | 4.321 | 4.321 | 4.357 | 0 | 1 |
| TH | 5 | 5.800 | **4.800** | 6.200 | 1 | 2 |
| hinata | 5 | 7.600 | **5.800** | 7.800 | 0 | 1 |
| auras | 24 | 4.292 | **4.208** | 4.292 | 0 | 1 |
| dragonstorm | 39 | 4.538 | **4.513** | 4.718 | 2 | 6 |

The clairvoyant search is at or ahead of the human on every deck in aggregate; the NC search is
behind on every deck except slivers.

## A. Clairvoyant falls short of the human — 3 / 140

| deck | ref | human | clair | human line still replays? | root cause |
|------|-----|-------|-------|---------------------------|------------|
| TH | `claude_s2_gi1` | 4 | 6 | **yes** (`ok`) | prune gate `DrawEngine` |
| dragonstorm | `claude_s1_gi0` | 4 | 6 | **yes** (`ok`) | combo-vs-beatdown misvaluation |
| dragonstorm | `claude_s24_gi23` | 6 | 7 | unconfirmed (index drift) | unknown; invariant to every lever |

### A1. TH `claude_s2_gi1` — a single prune gate costs 2 turns (actionable)

The `MTG_UNPRUNE` gate isolation is unambiguous, and it reproduces at the **default** budget
(not just an inflated one), so this is not budget churn:

```
default (value_play d5/b20)   6
MTG_UNPRUNE=dig               5
MTG_UNPRUNE=drawengine        4     <- matches the human
MTG_UNPRUNE=dig,drawengine    4
budget-ms 4000                6     (budget does not help)
--depth 7 --budget-ms 4000    6     (depth does not help)
```

`DrawEngine` is the "draw-engine (flood) cast gate" — the heuristic that decides whether casting the
draw engine is even offered as a choice. Opening it alone recovers the human's turn-4 win. This is
exactly the failure mode the search-primary bar exists to catch: **a heuristic pruning a line the
search would otherwise find.** Not adopted here — flipping it needs the normal A/B (regression train
seeds, then held-out overnight seeds) because ungating costs breadth everywhere.

### A2. Dragonstorm `claude_s1_gi0` — the search trades the combo for a beater

The human's exact recorded line replays cleanly on the current binary (`ok`, win_turn 4), so the
turn-4 kill is genuinely available today. No lever moves the search off turn 6: every budget
(20 → 4000 virtual-ms), `--depth 7`, and all 15 individual `MTG_UNPRUNE` gates return 6
(`MTG_UNPRUNED` global makes it *worse* at 8, as does `payoffprune` alone).

The two lines diverge on **turn 3**:

| turn | human (wins T4) | search (wins T6) |
|------|-----------------|------------------|
| 1 | Sandstone Needle, cast nothing | Sandstone Needle, cast nothing |
| 2 | Mountain, Ruby Medallion | Unclaimed Territory, Ruby Medallion |
| 3 | Mountain, **cast nothing** (holds Rite of Flame) | Mountain, **Rite of Flame → Scourge of Valkas** |
| 4 | Unclaimed Territory → Rite of Flame, Seething Song, Apex of Power → … → Dragonstorm → 5 dragons, lethal | Mountain, attack for 7 |
| 5–6 | — | attack 7, then Rite of Flame + Seething Song + Utvara Hellkite, attack 5 → win T6 |

The search spends its turn-3 ritual to deploy a 5-drop body and converts to beatdown; the human
banks the ritual and storms off on turn 4. The winning plan **is** enumerated (turn-1 index 6,
"land=Sandstone Needle; cast: (nothing)", is present in the current plan list) — this is a leaf
*evaluation* problem (a body on board outscores a held ritual), not an enumeration problem.

### A3. Dragonstorm `claude_s24_gi23` — 7 vs 6, invariant

Flat at 7 under every budget, `--depth 7`, `MTG_UNPRUNED`, and all 15 individual gates. This
reference's recorded line no longer re-anchors by index (`recorded pick 4 out of range (nplans=2)`),
so the human's turn 6 is **not confirmed reachable** on the current engine. Lowest-confidence of
the three; needs a summary-matched replay (see "Known gap" below) before it is worth chasing.

## B. Non-clairvoyant falls short of the human — 15 / 140

| deck | ref | human | clair | NC |
|------|-----|-------|-------|-----|
| antilife | `claude_s6_gi5` | 4 | 4 | **LOSS** |
| dragonstorm | `claude_s13_gi0` | 3 | 3 | 5 |
| dragonstorm | `claude_s3_gi2` | 4 | 4 | 6 |
| antilife | `claude_s10_gi9`, `claude_s7_gi6` | 4 | 4 | 5 |
| burn | `claude_s13_gi12` | 5 | 5 | 6 |
| knights | `claude_s28_gi27` | 4 | 4 | 5 |
| TH | `claude_s1_gi0` | 3 | 3 | 4 |
| hinata | `claude_s2_gi1` | 5 | 5 | 6 |
| auras | `claude_s16_gi15` | 4 | 4 | 5 |
| dragonstorm | `claude_s21_gi20`, `claude_s30_gi29` | 7 / 4 | 7 / 4 | 8 / 5 |
| TH `claude_s2_gi1`, dragonstorm `claude_s1_gi0`, `claude_s24_gi23` | | | | (also in section A) |

Every one of these except the three section-A games has **clair == human**, i.e. the line is found
by the clairvoyant search and lost only when the future is hidden. That is the expected
information-limited NC gap, with one standout: **antilife `claude_s6_gi5` does not win at all**
(human and clairvoyant both win T4), which is a 5-turn swing and the single worst NC result in the
set.

**The `MTG_NC_TEMPO` land-drop bonus no longer recovers any of these.** Re-swept at t=0.5 and t=1.0
(with `MTG_NC_TEMPO_LANDS=2`) over all 15: 14 unchanged, and TH `claude_s2_gi1` improves 6 → 5 at
t=1.0 but is still 1 short of the human. The games that bonus was built for (antilife gi11/gi10/
gi29/gi5, burn gi12 per `learned-d0-policy.md`, 2026-07-10) have since been fixed by other work.
What remains is the **residual land-selection / on-curve-sequencing family** that doc predicted a
land-drop bonus could not reach. Anyone reading that doc's tempo table should treat it as historical.

## C. Where the human falls short of the search — 17 / 140

Reported for completeness (the user's side question), not as a defect. Notable: hinata
`claude_s12_gi11` (human 9, clair 6), TH `claude_s5_gi4` (human 8, clair 5), and two games the
human did **not** win that the clairvoyant search wins on turn 5 — TH `claude_s4_gi3` and hinata
`claude_s6_gi5`. Some of these are EVPI (the search sees the library), not human error.

Separately, all three `references/suboptimal/` games (user-flagged "winnable earlier") were checked:
the search already beats the recorded line on Dragonstorm `s26_gi25` (5 vs 6) and burn `s18_gi17`
(4 vs 5), but ties Auras `s21_gi20` at 6 — so the faster Auras line the user suspects is also out of
the search's reach.

## Content-matched reachability (`scripts/ref_line_replay.py`)

`viewer_protocol_check.py` re-anchors a saved line by **plan index**, so a change to enumeration
order or breadth reads as "drift" even when the human's line is still perfectly legal. The new
`scripts/ref_line_replay.py` re-anchors by plan CONTENT instead (summary, then land+casts), which
answers the reachability question directly. Over all 140:

```
114 REPRODUCES, 5 DIFFERENT-RESULT, 21 UNREACHABLE
```

**Caveat on the 21.** Fifteen are `decision type changed main_phase -> land_entry / target /
sacrifice / replicate / mulligan` — the current engine emits an extra *sub*-decision the reference
does not carry, and the tool's strictly-sequential matcher cannot skip it. Those are a tool
limitation, not evidence the line is gone. Only six are a genuine "recorded plan is no longer
enumerated". Handling interposed sub-decisions is the obvious next improvement.

All three clairvoyant-shortfall references REPRODUCE, including under shipped pruning — see
[clairvoyant-reference-shortfalls.md](clairvoyant-reference-shortfalls.md).

## Artifacts

`logs/ref_bench/` — `console.log` (per-game table), `summary.json` (machine-readable),
`viewer_check3.log` (post-fix drift report), and per-game engine JSON logs under
`logs/ref_bench/<deck>/{clair,nc}/s<seed>_gi<gi>/`.
