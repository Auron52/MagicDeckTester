# Mirrorwing: the horizon search underestimates draw-dependent wins (open, fix planned)

**Status: DIAGNOSED with a minimal repro, not yet root-caused. The fix-first item from the
2026-08-14 degenerate-case investigation.** User directive (2026-08-14): *"If search and play do
not match it's worthy of a fix"* — even though the unwon-game audit found no lost games (see §3).

This is the established DIVERGENCE defect class, one level up: the continuation SacForMana fix
(`fda8b07b`) closed a rollout/executor divergence (the rollout could not crack Treasures the
executor cracks), and the repo gates such divergence with fd-diverge checks. Here the divergence is
between the SEARCH'S MODEL of future turns and what play actually does in those turns (mid-turn
draw-breakpoint re-solves). Same principle: the search must model play.

## 1. The phenomenon

On draw-explosion boards (Fists/Gold Rush chains resolving at deferred breakpoints), the in-horizon
search reports win-turn ESTIMATES far worse than what play then achieves through mid-turn re-solves:

- **Minimal repro: seed 8022 / game_index 14 ("gi=14"), matrix H5 config** (unbounded d5,
  `--ignore-play-profile`). The game WINS at T5. The T1 committed pass at depth 5 — whose horizon
  covers T1..T5 exactly — reports `win=8` (`MTG_TRACE=search`: `T1 pass=5 done win=8
  cost=1600619`). The actual T1..T5 winning line is captured in `logs/mwprof/h5_gi14_fd.log`
  (`MTG_FD_TRACE`).

## 2. Why it costs so much (measured 2026-08-14)

The ladder's win-break (`TurnSolver.cpp` ~14775: stop at first VERIFIED in-horizon win) never fires
when verification fails, so every pass runs to full exhaustion:

- H5 T1 committed pass on the repro: 1.60M work units. H6's committed pass on the same decision:
  5.19M (3.24x — depth exhaustion ratio). H6's rung-5 *warm-up* on the same walk: 43,888 units —
  36x cheaper than H5's committed rung-5, proving the walk itself is cheap when leaf-priced; the
  bill is heuristic-priced exhaustion with no incumbent.
- Counter profile of the kept-hand monster (seed 8025/gi17, H5, 786 s): **99.1% of the game's
  140.9M rollout-steps in the single T1 decision**; 20,355 FSLineWin tree nodes fanning to 1.02M
  candidate plans (50-way branching); 68.6M GameState deep copies (0.49/step).

Consequences: the H-cell ladder's sweet spot (win verifies at a leaf-priced warm-up rung) almost
never engages on this deck, H6 is uniformly expensive except where verification lands at exactly
rung 5-6 (measured: 2 games of 25), and every no-win decision pays full-tree exhaustion — the
"Class B" degenerate cost (see the census in `mirrorwing-gen-perf-profile.md`).

## 3. What it is NOT (audited 2026-08-14)

The two unwon phase-A games (seeds 900248, 900369; labels said winnable ~5.3-5.7) are NOT evidence
of missed wins:

- **900248**: wins at T7 when replayed unbounded at d5 — shipped-budget starvation, a priced
  tradeoff, not a defect.
- **900369**: still unwon unbounded; `MTG_DUMP_EWINS` full-horizon on the TRUE library order says
  `earliest=9` — the hand is genuinely dead as drawn. The label's optimism was its K=3 RESHUFFLED
  futures (de-clairvoyance measuring the position, not this ordering). No bug.

So the mismatch costs WALL-CLOCK (proven, large) and possibly line quality on close decisions
(plausible, unproven); it does not cost games in the audited cases.

## 3b. NARROWED (2026-08-15): the search picks the RIGHT play and MIS-VALUES it

Two measurements on the gi=14 repro move this from "the search underestimates" to a located defect.

**(i) The ladder commits the deepest pass unconditionally, and deeper is systematically WORSE.**
`TurnSolver.cpp` ~16535 does `line = attempt; committed_depth = pass_depth;` with no comparison
against the previous pass. On the repro's T1 decision the reported win DEGRADES with depth:

| pass | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| win | 6 | **5** | 6 | 7 | **8** |
| cost | 4 | 38 | 625 | 5,807 | **1,600,619** |

Pass 2 already had the right answer (5, at 38 units). Pass 5 overwrote it with 8, for 42,000x the
cost. Whatever the root cause, a ladder whose deeper rungs report worse values is not merely slow.

**(ii) The committed play IS the winning play — only its VALUATION is wrong.** `MTG_DUMP_EWINS`
(real library order) on the same T1 state:

```
{"ewins":{"seed":8022,"turn":1,"earliest":5,"candidates":[
  {"win":5,"land":"Forest","casts":["Ignoble Hierarch"]},   <-- what the search COMMITTED
  {"win":5,"land":"Forest","casts":[]},
  {"win":5,"land":"Rootbound Crag","casts":[]},
  {"win":6,"land":"","casts":[]}]}}
```

`earliest=5`, and the top candidate is EXACTLY the search's committed T1 line (Forest + Ignoble
Hierarch, per `h5_gi14_fd.log`). So the depth-5 search chose the correct first play and scored it
**8 instead of 5**.

**What this rules in and out.** It is NOT a top-level enumeration hole -- the winning line was
enumerated AND selected. The entire error is in the CONTINUATION VALUE: the recursion's model of
T2..T5 from that exact position reaches T8, while real play from the identical position reaches T5.
That confirms prime suspect (a) (deferred draw-breakpoint re-solves missing inside RECURSED future
turns) and eliminates suspect (b) AT THE TOP LEVEL (it may still apply inside the recursion).

The search's own predicted continuation (`[fd-pred]`, T1 line) is a slow clock that never closes:
opp_life 20 -> 19 -> 15 -> 13 -> 11 -> 9. Real play, from the same T1 play, wins on T5.

## 4. Investigation plan

1. ~~From `h5_gi14_fd.log`, extract the actual winning line~~ — DONE (see 3b): the committed T1 play
   is the winning play; the error is in the continuation value, so the probe below should target
   the RECURSED turns, not the top-level enumeration.
2. Instrument the T1 pass-5 recursion (temporary probe, stripped after diagnosis — the
   MTG_BP_DUP_PROBE precedent) to answer, level by level: is each step of the known winning line
   ENUMERATED at the corresponding recursion depth? First site where it is not (or where its value
   is mis-scored) is the defect site.
3. Prime suspects, in order: (a) deferred draw-breakpoint re-solves inside RECURSED future turns
   (the site-5 machinery made continuations searchable at the current decision — verify the same
   coverage exists at depth: a T2-in-recursion Fists cast must open the same continuation the real
   T2 would); (b) EnumGroupCap / group-wave tranche limits at inner recursion depths dropping the
   explosive groups; (c) strive/target folds pruning the winning variant at depth.
4. Any fix is play-changing (better verification => different committed lines): full standing gate
   (smoke + regression + GT rebaseline if play moves), and re-measure the H ladder afterwards —
   win-break firing should collapse the Class B exhaustion class outright.

## 5. Sequencing

Does NOT block: adoption A/B (done under current engine), keepgen configuration work, or the
low-R keepgen lever A/B. SHOULD land before: the depth matrix (phase C) — the matrix would
grind ~6,400 games against this mismatch, and a post-fix engine both runs it cheaper and measures
the play we would actually ship. Banked phase A rows/model survive any fix per the play-digest
seam (rows are internally consistent; C measures the engine that exists when it runs).
