# Value-leaf regeneration: the serialized queue

**Status:** planned, not started. Owner: whichever agent takes the queue — it is designed to be run
by **ONE agent, one job at a time**, because every job wants the whole box.

This is the self-contained runbook for regenerating the value-leaf artifacts across all nine suite
decks. It exists because there was no single document for this: `docs/design/learned-d0-policy.md`
covers the broader learned-d0 program (and lists Hinata only as a deferred loose end), and the real
operational knowledge lived in the header comments of `scripts/hinata_valueleaf_pipeline.sh`. That
made the work impossible to hand to a fresh agent without reading two thousand lines.

---

## 1. Why regenerate at all

Two independent invalidations, either of which alone would justify it.

**(a) Every table was measured profile-less.** `scripts/attic/valueleaf_depth_matrix.py` never passed
`--profile`, so every cell in every deck's `value_leaf_table` was measured against a deck with **no
mulligan/keep model and no card_scores** — a materially different engine from the one that ships.
Fixed in `c910b06` (2026-07-31); the tables built before it were not. The precedent for how badly
this misleads is `docs/design/antilife-valueleaf-deep-cells-overnight.md`.

**(b) Play has drifted since.** The tables are win-turn measurements, so any change to play
invalidates them. Several landed after the tables were built, including the duplicate-legend prune
(`dd8d93c`, 2026-08-01 22:40), which moves any deck running a legend.

Coverage is a third, lesser problem: several tables are thin (Hinata 200 games × 2 seeds, H ∈ {1,2,3}).

---

## 2. THE FREEZE RULE — read this before starting

**Generate the whole queue on ONE frozen commit, and record it.** This is the same Rule 0 the
mulligan-profile skill enforces, for the same reason: the artifacts are engine-state fingerprints, so
a play change midway through silently produces a table whose rows disagree with each other. If a play
change lands mid-queue, everything measured before it is void — not "slightly stale".

Freeze candidate: **the current tip of `phase-1-2-deck-analyzer`.** Do not copy a SHA out of this
document — look it up when you start, for the reason immediately below.

Write the frozen commit into each artifact's provenance as you go. If you must take a play change
mid-queue, restart the queue; do not mix.

**This hazard is not hypothetical, and it fired while this document was being written.** The first
draft named `c89a7c2` as the freeze point (integration complete, all three tiers re-accepted with 0
regressions). Within the hour the other machine pushed `6bc04b8` — a Goblins tutor-axis ranking with
the width narrowed 12 → 4 — which is a play change, and the named freeze point was stale before
anyone had run a single job against it. Two machines work this branch. **Announce the freeze, or take
the branch quiet for the duration.** Coordination is cheaper than discovering mid-queue that half the
tables describe a different engine.

That particular collision was harmless: `6bc04b8` only overrides `GoblinsProvider::TutorCandidates`,
and Goblins is the one deck the queue skips. Confirm the same is true of anything that lands next —
"it's another deck's provider" is a claim to verify against the diff, not to assume.

### The rule already has one victim — do not repeat it

The 1,628 Hinata rows in `logs/eval/hinata_rows_B.all.rows` **must be discarded, not resumed.** Every
one was written 2026-08-01 21:02 or earlier; the duplicate-legend prune landed at 22:40 that night and
changed Hinata's smoke play digest (`7ef9b9cd…` → `00c7dfbd…`, stable since). Hinata, Dawn-Crowned is
legendary, so the prune bears directly on this deck. Any note saying "1,628 rows recovered, ~9,200 to
go" is superseded: **usable rows are 0, and the job is the full ~11k.**

Delete or archive these before starting so they cannot be folded in by `collect`, which globs them:

```
logs/eval/hinata_rows_B.all.rows          1628 rows   2026-08-01 21:02
logs/eval/hinata_value_pooled.rows         127 rows   2026-08-01 21:05
logs/eval/hinata_value_v2b.rows           1016 rows   2026-08-01 01:12
logs/eval/rows_B_rescue_chunk215_564rows.rows  564    2026-08-01 17:59
logs/eval/rows_B/chunk_*.rows               48 rows   2026-08-01 01:55
```

---

## 3. Inventory: what each deck currently has

| deck | games | seeds | H | V | trust | needs |
|---|---|---|---|---|---|---|
| Goblins | 3000 | 2 | 1–5 | 5–8 | 6 | **probably none** — regenerated post-fix (`70515df`, 08-01). Verify only. |
| Anti-Lifegain | 1000 | 4 | 1–6 | 1–7 | – | matrix |
| Knights | 1000 | 4 | 1–8 | 1–8 | 5 | matrix |
| burn | 1000 | 4 | 1–8 | 1–8 | 6 | matrix |
| slivers_vial | 1000 | 4 | 1–8 | 1–8 | 5 | matrix |
| treasure_hunt | 1000 | 4 | 1–8 | 1–8 | – | matrix |
| Auras | 400 | 2 | 1–3 | 1–5 | 5 | matrix + widen H |
| Dragonstorm | 400 | 2 | 1–3 | 1–5 | – | matrix + widen H |
| **Hinata2** | **200** | **2** | **1–3** | **1–5** | – | **rows → train → matrix** (the long pole) |

Decks with a `trust` value are the ones whose table gates a live play decision, so they carry the
most risk from a wrong table. Decks showing `–` still use the table via
`value_fallback_crossover` / `value_no_fallback`.

---

## 4. The queue, in order

Ordered by value-per-hour, so that stopping early still banks the most useful results. Total is
additive — serial execution means order does not change the sum, only what is finished if you stop.

| # | job | rough cost | why here |
|---|---|---|---|
| 0 | verify Goblins needs nothing | minutes | avoid redoing the other machine's work |
| 1 | matrix: Auras | hours | thin table (400g/2 seeds) AND gates play (trust 5) |
| 2 | matrix: burn | hours | trust 6, wide table already — a re-measure may move it |
| 3 | matrix: slivers_vial | hours | trust 5; slivers is slow, budget accordingly |
| 4 | matrix: Knights | hours | trust 5 |
| 5 | matrix: treasure_hunt | hours | no trust; crossover only |
| 6 | matrix: Anti-Lifegain | hours | no trust; deep cells already known expensive |
| 7 | matrix: Dragonstorm | hours | thin table; deck is slow |
| 8 | **Hinata rows** | **~24 h** | the long pole; ~11k rows at ~450/hr sustained |
| 9 | Hinata train + matrix | hours | depends on 8 |

Deep H cells are the cost risk, not the V cells: a single seed at H5+H6 took **~5.8 h** on antilife,
and that run died at ~6.4 h having written almost nothing before the incremental machinery existed.
The matrix is now incremental and resumable (`<out>.cells.json`), so a killed run costs one batch.

---

## 5. Commands

Rebuild first — a stale binary invalidates everything downstream:

```bash
bash build.sh                      # NEVER raw cmake; see CLAUDE.md
git rev-parse --short HEAD         # record this in every artifact's provenance
```

**Matrix, per deck** (run with the box to *itself* — the intractability cut-off is wall-clock based,
so a loaded box misclassifies slow cells as intractable and silently truncates the table):

```bash
python3 scripts/attic/valueleaf_depth_matrix.py --incremental --decks <deck> \
  --hdepths 1 2 3 4 5 --vdepths 1 2 3 4 5 \
  --seeds 8008 9009 10010 11011 \
  --profile <the deck's profile>            \
  --target 400 --reference-target 50 --batch 25 --workers 20 \
  --value-min-depth 0 --intractable-sec-per-game 3.0 \
  --out logs/eval/valueleaf_depth_<deck>.txt
```

`--profile` is the whole point of this exercise — if it is missing the run reproduces the original bug.

**Hinata rows** (pooled; see the batching gotcha below):

```bash
bash scripts/hinata_valueleaf_batch_dump.sh 2000        # repeat until ~11k rows
bash scripts/hinata_valueleaf_batch_dump.sh collect     # dedupe on (seed, turn)
bash scripts/hinata_valueleaf_pipeline.sh train         # -> STAGED, not installed
bash scripts/hinata_valueleaf_pipeline.sh matrix
```

---

## 6. Gotchas that have already cost real time

**Use the pooled dump, never `dumpB`/`dumpA`.** `run_chunked_dump` is a `while` loop of sequential
`mtg` invocations, each with its own `--threads 24`. Every invocation pays its own load-imbalance
tail, and a chunk commits only on clean exit. Measured 2026-08-01: chunk 215 finished 98 of 100 games
in ~5 h, then TWO games ran ~3 h more with ~20 of 24 cores idle and not one row committed. Use
`scripts/hinata_valueleaf_batch_dump.sh`, which pools every game into ONE queue so stragglers occupy
their own threads instead of blocking everyone. This is the repo's standing batching rule.

**Resume is by dedupe, not bookkeeping.** The row writer is mutex-guarded and flushes every row, so
rows are durable the instant they are produced. Every row carries `seed` and `turn`, which identify a
position exactly — so re-running and deduplicating on those two columns is correct, and the chunk
machinery's "resume point" was never needed for durability.

**Seed spacing.** Per-game seed is `base_seed + gi`, with `gi` restarting at 0 each invocation. Jobs
whose ranges touch replay the same games. The pooled dump uses `BASE=30030` with 250-game blocks
spaced exactly 250 — correct, but with **zero margin**: raising the block size without raising the
spacing silently duplicates positions into the training set. See
`docs/design/searched-design-audit-blind-spots.md` and rule 7 of the regression-testing skill.

**Judge a dump by its SUSTAINED rate, never its first minute.** Turn-1/2 positions are cheap and every
run starts fast, then collapses once games reach mid-game. Reference rates on a 24-core box: K=8 ≈ 360
rows/hr, K=3 with `--max-turns 8` ≈ 450 rows/hr.

**Do not kill a run past ~10 minutes** — that is the user's call alone, and a question about progress
is not a cancel. See CLAUDE.md.

**Rows are machine-local.** `logs/` is gitignored, so partial rows do NOT transfer between machines;
only the committed `decks/*/*.value.json` outputs do. This is the main argument for running the whole
queue in one place rather than splitting it.

---

## 7. Acceptance and what gets committed

- Never install a retrained model straight to the live sidecar. `train` writes
  `logs/eval/Hinata2.value.STAGED.json` and merges **only** `eval_model` into a copy, so `value_play`,
  the old table and the crossover survive until the new table is measured and the adoption A/B runs.
- A regenerated table changes play, so it needs the standing gate: **smoke + regression** before
  commit, and an overnight tier for anything that moves a `value_trust_depth`.
- Check the truncation guard (`72f87cb`, `c0b4498`): a table must be rejected if a depth's coverage is
  out of range rather than merely "descending". The Goblins case that motivated it is instructive —
  its crossover read "trust the leaf at 6" when V6 had **never been measured**; the sentinel came from
  absence of data, not from evidence.
- Commit `decks/<deck>/<deck>.value.json` with the frozen commit in provenance. Raw rows stay
  gitignored.
