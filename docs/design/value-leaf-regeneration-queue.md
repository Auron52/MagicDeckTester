# Value-leaf regeneration: the serialized queue

**Status: RUN 2026-08-02..04 on frozen `27d76b4`. Result: regeneration is NEUTRAL — the staleness
this document was written to fix turned out to cost nothing measurable.** 4 decks completed
(Anti-Lifegain, Dragonstorm, burn, Auras), 1 thin (Hinata), 3 could not be labelled at all
(slivers_vial, treasure_hunt, Knights — **zero rows in 34 h**; see §8). Nothing adopted; staged
artifacts sit in `logs/eval/<stem>.value.STAGED.json` with the live sidecars untouched.
Driver: `scripts/valueleaf_regen_queue.sh` (a pooled rewrite of the serial queue below —
see §8 for why the serial design in §4 was abandoned).

**Update 2026-08-06 — treasure_hunt COMPLETED on frozen `ba5f1b1`.** The TH cleanup-discard
redesign (heuristic return = trial set, `e6beb73`..`6fdf180`) removed the probe-rollout blowup
that made TH unlabellable and its deep cells intractable. Fresh full matrix
(`logs/eval/valueleaf_depth_th_redesign.txt`): all 52 cells at 400 games, **0 intractable** —
V6 went from 413 s/game (50-game reference cell) to 254 ms/game. Ladder clean and monotone;
h_conv 4.0687 (H converges at d4); V8 = h_conv exactly. Derivation → STAGED sidecar: trust
stays UNSET (V5 gap 0.0044 > tol), deep crossover `take@hc` for c=6/7/8 moved [6,9,9] → [4,4,6]
(the old entries rested on starved pre-redesign cells). Staged-vs-live A/B at shipped play
(8 × 1000 games, `logs/vlq_th_ab/`): **−0.00063, t=−0.89, 4/3/1** — neutral, consistent with
every other deck in this queue. Depth sweep re-confirms shipped d5 (d6 identical on all 4
seeds, d4 ~+0.005 worse). Note the pre-redesign TH cells
in `valueleaf_depth_regen.txt(.cells.json)` are stale for TH and must not be resumed or pooled.

**Resolution 2026-08-06 (user-approved): SEVEN of eight adopted; Knights declined.** The full
`94c917f` regen (all 8 models retrained on ~10–12k rows each at shipped play; matrix at 400 g ×
4 seeds, TH's fresh at `ba5f1b1`) was derived and A/B'd staged-vs-live at shipped play (8 ×
1000 g paired per deck): Hinata2 **−0.00525 t=−4.52 8/0/0** (the thin-model deck — a real win);
antilife −0.00087, TH −0.00063, dragonstorm −0.00038, auras −0.00025, slivers −0.00012 (all
neutral); burn 0.00000 exact 0/0/8 with **trust 6→5** (callgrind paired 240 games: −0.07% Ir —
instruction-clean). **Knights DECLINED**: its 400-g derivation flipped trust 5→UNSET on 4
boundary games (noise — a 2000 g/seed top-up re-derived trust=4, V4 gap 0.0019), but at-scale
verification on 16 FRESH seeds × 2500 g showed the staged artifact itself costs quality
one-sidedly (**+0.00020 t=+2.74, 0/16 seeds better**; trust-4-vs-5 = 1 game in 40k, 15/16
byte-identical — the scalar was innocent, the retrained model/crossover is the cost). Live
Knights kept; its staged sidecar remains in `logs/eval/` for diagnosis. Gates: smoke 24/27,
regression 38/45, overnight 89/108 — every move an installed deck, d5-only (one digest-only
slivers d3), burn byte-identical at all 180k suite games, net −0.0067 summed; all three tiers
accepted. LESSONS: (1) an 8-seed "inert" read can hide a one-sided +0.0002 — verify trust/model
moves on MORE, FRESH seeds before adopting; (2) the trust derivation's hard 0.002 threshold on
a noisy cell flip-flops near the boundary — a noise-aware margin (clear tol by > cell SE) is a
deferred improvement; (3) re-deriving trust for the LIVE Knights table at higher sample might
recover the ~0.7% Ir saving without the model swap (open follow-up).

Owner: whichever agent takes the queue — it is designed to be run
by **ONE agent, one job at a time**, because every job wants the whole box.

This is the self-contained runbook for regenerating the value-leaf artifacts across all nine suite
decks. It exists because there was no single document for this: `docs/design/learned-d0-policy.md`
covers the broader learned-d0 program (and lists Hinata only as a deferred loose end), and the real
operational knowledge lived in the header comments of `scripts/hinata_valueleaf_pipeline.sh`. That
made the work impossible to hand to a fresh agent without reading two thousand lines.

---

## 1. Why regenerate at all

Two independent invalidations were claimed. **The first one is wrong** — corrected below, since it
was the headline reason and acting on it alone would waste days.

**~~(a) Every table was measured profile-less.~~ FALSE — do not regenerate on this rationale.**
The original claim was that `scripts/attic/valueleaf_depth_matrix.py` never passed `--profile`, so
every cell was measured against a deck with no mulligan/keep model and no card_scores. In fact the
**profile auto-resolves from the deck path** and has since `e71f51f`, so the matrix has been picking
up each deck's profile (and, directory-relative, its keep model) all along. The script does not even
accept `--profile` — passing it exits 2. `c910b06` did not fix a live defect of this shape.

**(b) Play has drifted since.** *This* is the real invalidator. The tables are win-turn
measurements, so any change to play invalidates them. Several landed after the tables were built,
including the duplicate-legend prune (`dd8d93c`, 2026-08-01 22:40), which moves any deck running a
legend — and, larger, the 65-commit searched-decisions merge.

**What the regeneration actually measured (2026-08-04), 8 seeds × 1000 games/arm, staged vs live:**

| deck | Δ LP (staged − live) | paired t | seeds better/worse/tied | trust: live → staged |
|---|---|---|---|---|
| Anti-Lifegain | −0.00138 | −1.14 | 5/3/0 | UNSET → UNSET |
| Dragonstorm | −0.00075 | −1.03 | 3/1/4 | UNSET → UNSET |
| burn | 0.00000 | +0.00 | 1/1/6 | **6 → 5** |
| Auras | 0.00000 | n/a | 0/0/8 | 5 → 5 |

Final matrix: 208 cells, all at the full 400 games × 4 seeds, one consistent pass on `27d76b4`.

Every shipped `target_depth` stands (the play-profile sweeps put the shipped depth at or inside
noise of the best arm on all four). burn is the only derived scalar that moves, and its A/B is
*exactly* zero — the two trust depths are behaviourally indistinguishable over 8,000 games. So the
honest summary is: **(b) was real but its effect was below measurement, and regeneration on a
neutral result is not worth its cost.** Regenerate a deck when its *profile* changes, not on a
schedule.

Coverage is a third, lesser problem: several tables are thin (Hinata 200 games × 2 seeds, H ∈ {1,2,3}).

### DO NOT START THIS QUEUE UNTIL PROFILE GENERATION HAS LANDED

**Hard ordering constraint — still correct, but not for the reason originally given.** The matrix is
always measured *with* the deck profile (it auto-resolves; §1a), and the keep/mulligan model is a
*sibling of the profile*, resolved directory-relative
(`decks/<d>/<d>.keepmodel.exhaustive.profile.json.gz` next to `decks/<d>/<d>.profile.json`). So a
matrix run silently picks up whatever profile and keep model are on disk at that moment.

That means **regenerating a deck's play or mulligan profile invalidates any value-leaf table measured
before it.** This is the one invalidation that survived scrutiny (§1b), and it is the trigger to
regenerate a deck — not elapsed time, and not the withdrawn "profile-less" claim.

As of 2026-08-02, play-profile and mulligan-profile generation are being run on other machines. This
queue is therefore **downstream of that work and must not start until it lands**. Order:

1. play + mulligan profile generation completes and is committed
2. freeze
3. this queue

If a deck's profile is regenerated later, that deck's value-leaf artifacts go stale again and it
returns to the queue. Only that deck, not the whole set.

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

**SUPERSEDED as an execution plan — kept for the per-deck cost estimates.** The serial "one job at a
time" design below was replaced by `scripts/valueleaf_regen_queue.sh`, which pools *every* game of
*every* deck into three batches (rows → matrix → measurement). The reason is the repo's standing
batching rule: a serial queue pays a load-imbalance tail **per job**, and the slow decks here all
have long-tail games, so a nine-job queue meant nine tails plus an idle box during every
single-threaded step between them. Pooling collapses that to one tail per phase. The ordering column
is still useful for deciding what to sacrifice if the run is stopped early.

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

**Matrix, ALL decks in ONE pool** (not one run per deck — a per-deck loop pays a load-imbalance tail
*per deck*, and the intractability cut-off is wall-clock based, so the idle-then-busy pattern of
serial runs misclassifies cells):

```bash
python3 scripts/attic/valueleaf_depth_matrix.py --incremental \
  --decks <deck1>_staged <deck2>_staged ... \
  --hdepths 1 2 3 4 5 --vdepths 1 2 3 4 5 6 7 8 \
  --seeds 8008 9009 10010 11011 \
  --target 400 --reference-target 50 --batch 25 --workers 20 \
  --value-min-depth 0 --intractable-sec-per-game 60 \
  --out logs/eval/valueleaf_depth_regen.txt
```

**Do NOT pass `--profile`** — the script has no such option and exits 2. The profile resolves
automatically from the deck path (§1a). Use the `<deck>_staged` keys so the derivation writes to
`logs/eval/<stem>.value.STAGED.json` instead of straight to the live sidecar.

**`--intractable-sec-per-game` must be generous — 60, not 3.** See §8: at 3.0 this guard truncated
cells that were 11× *under* budget and silently corrupted a shipped play parameter.

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

---

## 8. What the 2026-08-02..04 run learned

### 8.1 A cost guard keyed on one sample faked a −4.25σ regression

This is the run's main finding and it generalises well beyond this queue.

`--intractable-sec-per-game` decided whether a cell was too slow to be worth finishing. It keyed on
**the current batch's** rate and, once tripped, set a **sticky** flag that was never re-examined.
One 25-game batch holding a single pathological game therefore condemned a cell permanently.

The damage was not "a slow cell got skipped". It ran all the way through to a shipped play parameter:

```
one unlucky batch  ->  cell capped at reference-target  ->  thin H2..H5
                   ->  wrong h_conv (min over a truncated H row)
                   ->  table derives trust=5 instead of UNSET
                   ->  trust IS the escalate-below threshold, so play changes in EVERY game
                   ->  +0.0035 LP, t = +4.25, 0/8 seeds better
```

Read as a model comparison, that is an unambiguous regression with a clean significance story — and
it was entirely an artifact of the measurement harness. Filling the cells restored `UNSET` and the
regression vanished (−0.00075, t = −1.03). **Every one of the seven tripped cells was far under the
threshold on its cumulative average**; the worst was 20.0 s/game and one was 5.5 s/game against a
60 s/game bar — 11× under. The entire cost of the cells the old 3.0 cutoff condemned was **4
core-hours**, i.e. the guard saved nothing and cost a wrong answer.

Fixed in `1249872`: the rate is **cumulative and recomputed every batch**, so it is robust to one
bad game and self-corrects as the average recovers, while still capping a genuinely hopeless cell.

**Lessons, in order of generality:**
1. **Never let a cost guard key on a single sample, and never make its verdict sticky.** Use a
   cumulative statistic and re-evaluate it.
2. **A truncation guard must not be able to change a derived value silently.** The derivation read a
   thin row as if it were a complete one. Either propagate "this cell is partial" into the
   derivation (the `completeness_error` path already exists — it just did not cover this shape), or
   refuse to derive.
   **A partial cell is not merely noisy — it is BIASED, and `h_conv` is a `min`, which makes the
   bias one-sided.** Measured on the refill: dragonstorm's `H5` read **4.3438 at 300 games and
   4.3550 at 400** — the partial cell was optimistic by 0.0112. Taking a min over the H row means
   any one optimistic cell pulls `h_conv` down, which raises the bar the value leaf must clear, which
   pushes `trust` deeper or to UNSET. So under-training a *single* H cell perturbs the gate for the
   whole deck, in a predictable direction, with no warning anywhere in the output.
3. **When an A/B says a regenerated model is worse, suspect the harness before the model.** The
   tell here was that the regression was *large and clean* for a change that should have been
   near-neutral — the same shape as the seed-overlap trap (rule 7).

The guard still cannot preempt: it only checks *between* batches, so a 25-game batch ran ~90 min at
>200 s/game before being caught. It bounds throughput, not latency.

### 8.2 The label path contains no rollouts and never reaches the value leaf

Worth recording because two plausible-sounding concerns about label quality are both false, and
because it redirects all optimisation effort to the one thing that actually costs.

Rows are byte-identical with `MTG_VALUE_MODEL=1` and `=0`. The reason is structural:
`EnumerateEarliestWins` sets `depth = max_turns - turn + 1` exactly, and `FSLineWin`'s **first**
guard (`turn_number > max_turns`) therefore always fires before the `depth <= 0` value-leaf branch.
So the label is search-verified wins only — an old or bad value leaf cannot contaminate it, and
there is nothing wasteful to remove there.

The cost is entirely:

```
|candidate plans|  ×  unpruned depth-8 search  ×  K
```

with `cutoff = max_turns + 1` (cross-candidate B&B deliberately off) and
`SearchBudget::FromVirtualMs(1000000)` (~9×10⁸ nodes — it never binds). Play's 20 ms budget does
**not** bound the labeller.

**Two cost intuitions that measurement refuted.** The H/V ratio is not a large constant: it is ~1.0×
at d1 rising to only 4–16× at d5 (antilife 8.1, burn 15.9, auras 3.8, dragonstorm 8.1). V is
roughly *flat* in depth while H grows steeply — which is why extending the V ladder to 8 was cheap.
And ladder growth is ~1.6×/level, not ~6× (games end around turn 4–5), so intermediate passes are
~60% of total cost rather than ~20%.

### 8.3 Three decks cannot be labelled at all, and why

slivers_vial, treasure_hunt and Knights produced **zero rows in 34 hours**. No mechanism has been
verified for why — in particular the "wide board" explanation is wrong for treasure_hunt, which is
not a wide-board deck. Do not restart these decks on the current engine; they will not finish.

Three **offline-only** changes unblock them. All three are gated to the label path, so the in-play
path stays byte-identical and **no ground-truth rebaseline is needed**:

1. **`earliest_only` branch-and-bound (the big one).** **RESOLVED 2026-08-05 — ADOPTED,
   `MTG_VALUE_LABEL_BNB` now default ON.** The 2026-08-04 refutation above was measuring a bug in
   the arm it was being compared *against*; both halves of it are now explained.

   The **label difference was the real finding**, exactly as this section said, and the suspect named
   here (shared-cache order-dependence) was **wrong**: `MTG_LABEL_COLD_CACHE=1` gives every candidate
   a fresh `tt`/`lc` and leaves the disagreement completely intact. The actual cause is one layer up.
   `FSLineWin` returns the FIRST in-horizon win it finds, and its own comment states that this is
   sound only under `FullSearchLine`'s ladder — "it is not a standalone earliest-win finder". The
   labeller used it as one, via a single `FSLineTail` call at a whole-game depth, so it returned *a*
   win rather than *the earliest* and **every label was pessimistic**. B&B's tight cutoff narrows the
   horizon, which accidentally restores the premise — the "pruned" arm was the *less broken* one, so
   its earlier labels were the correct ones. Fixed by `MTG_LABEL_LADDER` (default ON); measured
   69 of 240 rows moved earlier and **none later** across 7 decks. See
   `docs/design/label-horizon-ladder.md`.

   The **slowdown** had two causes and both are gone. The reasoning quoted above — "a cutoff-aborted
   no-win is deliberately never cached, so the shared memo stops paying off" — was correct, and is
   now fixed rather than worked around: `FSLineCache` stores a no-win **qualified by the cutoff it was
   proved under** and reuses it for any query asking no more (`MTG_FS_NOWIN_CACHE`). And the ladder
   supplies cheap shallow refutations before the expensive deep pass, so B&B no longer converts cheap
   "find a win" queries into cold "prove nothing beats it" ones.

   Re-measured under the ladder, 6 games/deck, seed 666000 — B&B is now **lossless on every deck**:

   | deck | B&B off | B&B on | labels |
   |---|---|---|---|
   | Goblins | 3.41 s | 1.98 s (**1.72×**) | identical |
   | treasure_hunt | 5.60 s | 4.49 s (1.25×) | identical |
   | Dragonstorm | 6.39 s | 5.34 s (1.20×) | identical |
   | burn | 1.24 s | 1.06 s (1.17×) | identical |
   | slivers_vial | 0.85 s | 0.73 s (1.16×) | identical |
   | Knights | 1.42 s | 1.39 s (1.02×) | identical |
   | Anti-Lifegain | 23.27 s | 23.48 s (0.99×) | identical |

   **The lesson worth keeping: a comparison has two arms, and "the new arm disagrees" is not evidence
   about which arm is wrong.** This one was quarantined for a day on the assumption that the
   established path was the reference.
2. **Fix the fabricated loss.** On budget overrun `FSLineWin` returns `{max_turns+1}` — which is
   indistinguishable from a real LOSS and would poison labels. It must instead fall through to the
   1-ply heuristic leaf below it. This is a prerequisite for *any* budgeting of the labeller.

   **Correction to an earlier claim in this file's history:** the labeller's budget was described as
   "~9×10⁸ nodes, never binds". The overrun guard is indeed never armed, so no pass is aborted
   mid-flight — but `m_limit` still gates whether iterative deepening *starts* a further pass, and
   on an expensive deck it does bind, silently degrading the label to whatever depth was reached.
   `MTG_VALUE_LABEL_BUDGET_MS` (default 1000000, `0` = unlimited) now exposes it so a label run can
   be checked for budget degradation. Note this was NOT the cause of the B&B disagreement above.
3. **Ladder-on-value-leaf as an offline matrix mode.** **BUILT AND VERIFIED 2026-08-05 —
   `MTG_LADDER_VALUE_LEAF`, default OFF (it is a matrix-generation mode, not a play change).**
   Use the value leaf for passes 1..d−1 and the heuristic only for pass d. Measured saving 49–71% at d5, and it makes H6/H7 cost less than
   today's H5. Pure-H timings stay reconstructible as `H(d) = H(d−1) + ladder(d) − V(d−1)` with
   H(1) exact, so the timing column survives in approximate form.

   **Identity is by construction, then checked.** Three facts make the committed line independent of
   the warm-up passes' leaf: every node of pass *k* satisfies `turn + depth == turn0 + k` and the
   `FSLineCache` key folds both, so two passes can never share an entry; a value-leaf pass returns
   *before* `SimulateToEnd`, the only writer of the leaf table, so it cannot contaminate that either;
   and matrix cells run unbounded, so there is no budget coupling. Measured: **21 of 21 cells
   byte-identical on avg AND play digest** (7 decks x d3/d4/d5, 40 games), plus 7 more at d2.
   Harness: `test/ladder_value_leaf_check.sh`.

   **Cost: strictly less work, growing with depth.** `-DMTG_PROFILE=ON` search nodes, off -> on,
   12 games, single thread, unbounded; `ApplyPlanDirect` / `EnumeratePlans` / `GameState` copies all
   agree within a few percent:

   | deck | d4 | d5 |
   |---|---|---|
   | Knights | 1.35x fewer | **84.8x fewer** |
   | Anti-Lifegain | 1.85x | **39.5x** |
   | slivers_vial | 3.21x | **35.7x** |
   | burn | 1.37x | **15.3x** |
   | Dragonstorm | 1.90x | 2.91x |
   | Goblins | 1.42x | 2.19x |
   | treasure_hunt | 1.36x | 1.51x |

   All 14 cells `avg`-identical. The d5 spread (1.5x to 85x) is wide enough that no single figure
   should be quoted as "the" saving — it depends on how much of each deck's ladder is warm-up.

   **A retraction worth keeping, because it nearly shipped a false mechanism.** The first pass at this
   used WALL-CLOCK over 40 games and reported the mode as *slower* at d4 on Goblins and Dragonstorm
   (0.57x, 0.67x) and 16-26x faster at d5. I explained the regression with a plausible story: the
   heuristic warm-ups had been *pre-warming* the shared leaf table for the committed pass. There is no
   such effect. The tell sat in the same table — Goblins ladder-on measured 3.4 s at d5 and 61.4 s at
   d4, i.e. more depth for 18x less time, which is not a cost model — and it took the user's scepticism
   to make me look. Both the "regression" and the headline speedups were the same artifact in opposite
   directions. **On a heavy-tailed deck, wall-clock is not evidence; use the counters.**

   **Under a BOUNDED budget this stops being free** — cheaper warm-ups leave more budget for the deep
   pass, a real (and probably favourable) play change needing its own A/B. It ships OFF for that reason.

   **Remaining caveats, none of them correctness.** (a) It needs the deck's `.value.json`; without one
   the mode is inert and the cell silently reverts to the slow path — a perf cliff, not a wrong number,
   but silent. (b) The counters do not include the value model's own evaluation cost, so "fewer nodes"
   is strong evidence for, not proof of, less wall-clock; both the node count and the per-leaf cost move
   the same way, so the direction is safe. (c) `scripts/attic/valueleaf_depth_matrix.py` sets it on H
   cells as of 2026-08-05 — before that wiring the flag existed but delivered nothing.

**Acceptance bar for all three (user, explicit):** digest drift is *expected* and fine; **win-turn
drift is a bug and blocks adoption until fixed.** Gate on per-game win turns via `MTG_DUMP_WINS`,
not on aggregate LP. The rationale: search returns a *minimum* (order-independent) and the engine
commits to and follows that line, so the realised win must equal the searched win. Drift therefore
means either unsound pruning (a search bug) or broken follow-through (an executor bug — this has
happened, `23d7b9a`). The gate matters most for (1), which is a pruning change.

**Rollout labels stay OFF for value dumps.** Value dumps want the searched label; the
rollout-vs-searched comparison in `learned-d0-policy.md` is about **d0 EVAL sidecars**, a different
artifact with a different purpose.

### 8.4 Operational gotchas

- **The filesystem is case-insensitive.** `logs/eval/Auras_value.rows` collided with a pre-merge
  `auras_value.rows`. Rows now live in queue-owned `logs/vlq/rows/`.
- **Never edit a running `.sh`.** Bash reads a script lazily and will execute garbage from the new
  byte offset. Use `cp` → edit → `mv` (atomic rename), or a separate sidecar process — which is why
  the supervisor and heartbeat are two scripts rather than one. Editing a running **`.py`** is safe
  (compiled at import).
- **Killing the driver does not kill its descendants.** `pkill -f <script>` left the Python matrix
  pool and 14 `mtg` workers running and burning the box, twice. Kill by explicit PID list; note
  `pgrep -cf` also matches its own wrapper.
- **A counter incremented inside `{ ...; } | consumer` is lost** — that is a subshell. It made a
  populated manifest report "0 games".
- **Never rebuild while a matrix pool is running.** The pool spawns `build/Release/mtg` per batch,
  so a rebuild swaps the binary mid-run and silently violates the freeze.

### 8.5 burn `trust` 6 → 5: the only moving scalar, and its history

This is the one derived value the regeneration changes, so it is the one adoption decision on the
table. It has been attempted before and it failed at scale — the context matters.

**Why it was 6.** `docs/design/learned-d0-policy.md` records the matched-depth residual `V5 − H5`,
described there as "the worse at generous budget" case:

| deck | knights | slivers | **burn** | antilife | TH |
|---|---|---|---|---|---|
| `V5 − H5` | 0.000 | +0.0003 | **+0.0033** | +0.009 | +0.0165 |

burn's `+0.0033` exceeded `tol = 0.002`, so the derivation pushed trust to 6. That was not a
formality: at depth 5 the leaf really was ~0.0033 turns worse than the converged heuristic —
invisible in smoke, but enough to show up in a large run, which is what happened.

**Why it is now 5.** On the regenerated table the residual is **0.0000** (`h_conv = V5 = 4.3438`).
burn has moved into knights' category, and knights ships trust=5 without trouble.

**Caveats before adopting.**
- The new table is 400 games/cell against the old 1000, so the LP quantum is 0.000625 vs 0.000250.
  "0.0000" means *under one quantum*, not identically zero. It is still a real narrowing — the old
  0.0033 would be ~5 quanta at the new resolution and would be plainly visible.
- The trust derivation is thin either way: `V4`'s gap is 0.0031, missing the 0.002 bar by 0.0011.
  trust=5 rests on one cell clearing a tolerance by about a thousandth of a turn.
- **trust=5 is NOT inert.** The staged-vs-live A/B differs in digest on 4 of 8 seeds, with 1 seed
  better, 1 worse, 6 tied — play changes and the effects cancel. Cancellation at 8 seeds is weaker
  evidence than it looks.
- **The matrix does not measure burn as it ships.** Cells are run `--ignore-play-profile --depth N`
  at the default budget; burn ships **d6 / budget-20** with its profile live. Trust interacts with
  the budget (trusting at 5 means one fewer escalation, handing the freed budget back to the
  search), and the matrix cannot see that interaction at all.

**Recommended gate:** because the previous failure surfaced at scale rather than in smoke, carry
burn trust=5 through the **overnight** tier, not just regression, before adopting.
