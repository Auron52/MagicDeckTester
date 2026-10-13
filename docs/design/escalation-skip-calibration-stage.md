# Escalation-skip allowance: a per-deck calibration stage in the value-leaf pipeline

**Status (2026-10-13): BUILT.** Phase **E.6** of `scripts/valueleaf.sh`, between E.5 (adopt the leaf)
and F (the mulligan-generation contract), implemented in `scripts/esc_skip_stage.py`;
`bash scripts/valueleaf.sh skipcal [decks/<Deck> ...]` runs it alone (backfill, or a leaf installed by
hand). The user asked for it to be *"relatively fast for most decks ... give up if there is not a notable
amount of headroom. So maybe 4%+"*, so a cheap probe gates the expensive A/B.

## What is being calibrated

The value-leaf hybrid commits a line with the cheap value-leaf probe and, when that line is unverified
and shallower than the deck's trust depth, escalates to a heuristic-rollout search on the remaining
budget. The take rule then keeps the heuristic line only if it reached the deck's take depth `T`
(`value_fallback_take_at`). An escalation that cannot reach `T` is computed and thrown away.

The **up-front skip** (`MTG_ESC_SKIP_PREDICT`, default ON, `TurnSolver.cpp`) predicts, before any
heuristic pass runs, how deep the ladder could get, and skips the escalation when that is short of `T`.
Each pass is predicted from the probe's own record at that depth as

    h[d] = pc[d] + R[d] * pl[d]        (probe units + allowance x probe leaves)

and the ladder's start gate is walked on those predictions. The fleet runs `R = 0`: the bare tree walk,
optimistic, measured lossless (16 decks x 480 held-out games, zero play changes). A deck may opt in to
`R > 0` with the sidecar key `escalation_skip_r`, which skips more of the escalations its take rule would
discard. It is opt-in because a larger allowance can skip an escalation that would have been taken.

**`R` must be per deck and per depth.**
* **Per deck:** rollout cost per leaf differs by about 30x across the fleet. Melira's 10th percentile is 2-5
  units per leaf against 60-200 elsewhere, and a fleet-wide `R = 50` skipped takeable escalations there.
* **Per depth:** the cost per leaf falls with depth, because a deeper leaf has fewer turns left to roll
  out. Selesnya's median is ~200 at d1, 119, 77, 48 and ~23 at d5. One constant overprices the deep passes.
  The start gate extrapolates the next pass from the ratio of the last two, so the skip then refuses
  escalations the ladder would have completed and taken. Selesnya at a constant 74 had three real play
  changes in ~22k held-out games, and one of them turned a T8 win into no win.

## The fit

`scripts/esc_skip_calibrate.py <batch.err>` works from a batch run at `R = 0` with `MTG_ESC_SKIP_LOG=1`,
where each `[esk]` line is one escalation decision.
* It uses every ladder escalation that ran (`path=2 skip=0`).
* `R[d]` is the **10th percentile** of `(hc[d] - pc[d]) / pl[d]`, the real heuristic pass's cost beyond
  the probe's tree, per probe leaf.
* Depths with fewer than 20 samples are priced at 0, and the vector is padded with 0 to 5 depths, because
  past the end of the list its last entry applies.
* Single-path decks (`escalation_cap > 0`) record no per-depth heuristic cost, so they get no fit and
  stay at `R = 0`.

The low quantile is deliberate. The prediction stays below most real passes, so the skip leans towards
running the escalation.

## Evidence that this shape works (2026-10-13)

The vectors were fitted on older seeds, then tested in a held-out paired A/B against `R = 0`
(`logs/esk/manifest_rv.json`, seeds 35xxxxx). Every changed game was replayed alone, as a one-game batch
chunk, to separate real play changes from batch thread-history flicker
(`batch-run-to-run-nondeterminism.md`).

| deck | games | units | play changes |
|---|---|---|---|
| Selesnya | 6000 | -7.8% | 0 |
| Mirrorwing | 6000 | -1.0% | 0 |
| KittyV3 | 2400 | -3.2% | flicker only |
| PD | 2400 | -1.3% | gi1608: no win -> T8 win |
| FungusB | 2400 | -1.9% | 0 |
| Giants | 4000 | -0.8% | 0 |

0 games slower in 23,200. Adopted: Selesnya (vector replaces 74), PD, FungusB and Giants. Mirrorwing
(70.5) and KittyV3 (43.3) keep their earlier constants, which also passed and save a little more.

## The stage (phase E.6)

It runs after E.5 because the allowance is a measurement of the probe, which is the value leaf. A
regenerated leaf changes `pc` and `pl`, and before this stage a regeneration carried the old key over
unchanged. It runs before F, and before any mulligan generation, because the allowance can change play and
the keep table is fitted to play. That keeps the per-deck pipeline strictly serial: profile, then value
leaf, then skip allowance, then mulligan.

There are three pooled batches, each covering every deck in the run. Each step needs the previous one's
data; nothing else is a barrier.

1. **Probe.** 1200 games per deck at play settings, `esc_skip_r = 0`, `MTG_ESC_SKIP_LOG=1`, seed
   7,100,000. It gives up at once when:
   * the deck has no ladder escalations (single-path, or trust-dominated); or
   * the **headroom** is under **4%**. Headroom is the work of every ladder escalation that ran and was
     thrown away, as a share of all the games' work. It is the most any allowance could save.

   Otherwise it fits the q10 vector from these games and **projects** its saving by walking the start
   gate offline. The projection is reported, and the deck also gives up if it is zero.
2. **A/B.** Surviving decks only, R = 0 vs the vector, paired, on fresh seeds (7,300,000; the fit never
   sees them). Each deck gets about 15 minutes of the box: 2400 to 6000 games, from the probe's cost per
   game.
3. **Isolation.** Every game whose digest moved is replayed alone at both arms as one-game batch chunks
   (`seed + gi`, `game_index = gi`). Identical alone means flicker
   (`batch-run-to-run-nondeterminism.md`). Different alone means a real play change, scored by win turn,
   with no win counted as `max_turns + 1`, as the suite does. More than 100 moved games is not flicker,
   and the deck is not adopted.

**Verdict.** An arm passes with no real slower game, a net change of 0 or better (*"+1 is not neutral.
0 is neutral."*), and a unit saving. A deck that already carries an allowance gets a **third A/B arm**
with it. That key may be a user-approved constant that saves more than the fit: KittyV3's 43.3 measured
-4.2% against its vector's -3.0%. Or it may have been fitted to an earlier leaf. The bar picks between
them:

| fitted vector | existing key | outcome |
|---|---|---|
| passes | none, or fails | **adopt** the vector |
| passes | passes | whichever saves more units (a tie keeps the existing key) |
| fails | passes | **keep** the existing key |
| fails | fails | **remove** the key (measured lossy on the live leaf), back to R = 0 |

How the outcome is applied:
* **Writing** installs or removes the key in the live sidecar in this tree, uncommitted, like E.5. The
  deck's smoke and regression GT then have to be re-accepted.
* **A deck that gives up at the probe** is not A/B'd, so its existing key is kept unmeasured, and the
  decision line flags it.
* **Every outcome** is appended to `<queue>/DECISIONS.txt`, and nothing stops to ask.

**Cost.** A deck that gives up costs only its share of the probe: seconds for most decks, about 4 minutes
for the most expensive (KittyV3 at ~5 s a game). A deck that passes adds about 15 minutes of A/B, or about 22 with an existing key's arm.

**Validation (2026-10-13, dry run, `logs/esk_stage/validate`).** Four decks, one pooled run:

| deck | headroom | outcome |
|---|---|---|
| Giants | 1.4% | gave up at the probe (fit would project -0.6%) |
| Mirrorwing | 2.5% | gave up at the probe (-0.8%) |
| Selesnya | 12.6% | both pass on 6000 g with 0 moved; fit -7.5%, existing vector -7.7%: **kept existing** |
| KittyV3 | 5.9% | both pass on 2459 g, 4 moved, all flicker; fit -3.1%, existing 43.3 -4.3%: **kept existing** |

It reproduced every hand-made decision of the same day.

## Open questions for the user

* **Automatic adoption** is what was built, as E.5 does for the leaf, under the strict bar above.
* **There is no q05 fallback.** q05 changed no game anywhere in the A/B, with about 60% of q10's savings.
  Retrying a failed deck at q05 would cost a second A/B, which works against the "fast" brief, so it was
  left out.
* **The gate is the ceiling, not the projection. DECIDED (USER 2026-10-13): keep it.** Headroom is read as
  the most that could be saved. On the six measured decks only Selesnya (12.3%) and KittyV3 (5.8%) clear 4%.
  KittyV3's achievable saving is about 3.3%, and gating on the projection would admit only Selesnya. The
  user: *"I think that's fine. If we get 2-3% that's not a waste of time."* So a deck that clears the 4%
  ceiling earns its A/B even when the fit projects only 2-3%.
