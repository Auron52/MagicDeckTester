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

## Re-calibration under the 0.5 escalation floor (2026-10-22)

Every allowance above was fitted when the escalation ran on the probe's **leftover** budget, which was
often close to nothing. Since eb0e7d066 the escalation runs on a fresh 0.5 x budget_ms as a floor
(`search-shape-open-items.md`, the superseded `escalation_fresh_frac` note). That changes both sides of
the trade:

* A doomed escalation is no longer a cheap failure. It spends real budget and is still discarded, so the
  headroom rises. Of the floor's extra work over the leftover, the share that went to discarded
  escalations: slivers_vial 85%, Auras 54%, Prevent Damage 38%, treasure_hunt 26%, Bruna 24%.
* Some escalations that used to be starved are now takeable. An allowance fitted on the starved regime
  then skips escalations that would now be taken, so it can turn lossy without anyone touching it.

The stage was re-run fleet-wide, dry (35 suite decks with a live sidecar, probe seed 7.1M, A/B seed
7.3M; `logs/tgt/fresh05` in the lf-target work tree). The two shipped keys under the 4% gate were then
A/B'd by hand the same way (`curvec.py`), because the stage keeps such keys unmeasured.

| deck | headroom | verdict |
|---|---|---|
| Selesnya | 11.4% | **replace** [110.4, 57.5, 39.0, 18.5, 6.5] with the fit [74.3, 29.3, 16.1, 8.4, 1.0]: the shipped key now loses gi4562 (7 -> 8) in 6000 g; the fit is -5.2% units with 0 moved |
| Prevent Damage | 4.8% | **remove** [200.7, 96.2, 58.6, 36.0, 21.0]: 6 of 6000 games slower, 3 of them losing a turn-8 win; the fit is 2 slower too, so back to R = 0 (+2.7% units vs the shipped key) |
| Fluctuator | 4.6% | **adopt** [137.8, 0, 0, 0, 0]: -1.0% units, 0 moved |
| Giants | 1.0% | under the gate; the shipped key measured by hand: 0 of 6000 moved, -1.9%: **kept** |
| Mirrorwing | 2.4% | under the gate; the shipped 70.5 measured by hand: 3 moved, all the same win turn alone, -2.6%: **kept** |
| Bruna, Vampires, Soldiers, CritterLifegain, Pirates | 0-3.7% | under the gate (no key) |

Applied with the stage's own `install`/`remove` and accepted through smoke + regression.

**The gap this exposed: single-path decks are outside the stage.** The probe gives up on any deck with no
ladder escalations, but the single path discards too, and under the floor three single-path decks clear
the gate: slivers_vial 7.2%, Auras 6.1%, treasure_hunt 4.3%. These are also the three decks whose cost
grew most under the floor. The climb-aware single skip already reads the same `escalation_skip_r`
(`esk_R` in `esk_climb_reach`), so fitting it is plumbing in the stage, not new engine code. One
mechanism was not a prediction at all. A single-path retry follows an overrun and never climbs, so a
retry below T is discarded whatever it finds. That is folded unconditionally (`MTG_ESC_RETRY_FOLD`,
TurnSolver.cpp). On the probe's 12 single-path decks x 1200 games it moved 0 of 14,400 digests. It is
small: slivers_vial -0.83% units, Auras -0.38%, FiveColour / Hinata2 / treasure_hunt about -0.08%. The
overrun pass before the retry is the bulk of a fell-back escalation, and that pass is not foldable.

## Open questions for the user

* **Automatic adoption** is what was built, as E.5 does for the leaf, under the strict bar above.
* **There is no q05 fallback.** q05 changed no game anywhere in the A/B, with about 60% of q10's savings.
  Retrying a failed deck at q05 would cost a second A/B, which works against the "fast" brief, so it was
  left out.
* **The gate is the ceiling, not the projection. DECIDED (USER 2026-10-13): keep it.** Headroom is read as
  the most that could be saved. On the six measured decks only Selesnya (12.3%) and KittyV3 (5.8%) clear 4%.
  KittyV3's achievable saving is about 3.3%, and gating on the projection would admit only Selesnya. The
  user: *"I think that's fine. If we get 2-3% that's not a waste of time."* So a deck that clears the 4%
  ceiling earns its A/B even when the fit projects only 2-3%. **The 4% threshold stands** (same day: *"So 4%
  is okay. If we found it to be really worthwhile we could even go a bit lower, but with what you mentioned
  4% is probably good enough."*). Lowering it is the user's lever, to reach for if the fleet's adoptions
  show the stage paying well.
* **Extend the stage to single-path decks? (OPEN, 2026-10-22.)** See the floor re-calibration above:
  slivers_vial, Auras and treasure_hunt clear 4% on the single path, where the stage does not look.
* **Skip, or price the next pass correctly? (OPEN, 2026-10-22.)** Most discards under the floor stop with
  more than half of the fresh budget unspent, because the next pass is over-priced. Skipping them saves
  the work. Pricing the pass right would instead let many of them reach T and be taken, which spends the
  budget the floor gives. The two compete for the same escalations: see `search-shape-open-items.md` §3d.
