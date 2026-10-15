# KittyEquipment v3 becomes the shipped list (2026-10-14)

v3-adelbert-gauntlets was promoted to `decks/KittyEquipment/`, v1 was archived as
`v1-kemba-balan`, and the v1 and v2 regression rows were dropped. This doc records the
measurement that authorized it and the three things the promotion nearly broke, because each
one is a trap the next list promotion will meet in the same place.

## The gate

USER 2026-10-14: *"before any regression test rows are dropped I would like to see that KittyV3
beats KittyV2"*, *"I believe it will, but we need actual numbers"*, *"the higher depth is the
number that matters"*, *"I'm not so worried about d3"*, and on the shape of the run: *"Maybe we
should also try a few games at high depth and budget like d7 b1000."*

So the decision cell was **d5 b40**, sized for power, with d7 b1000 as a deliberately small
direction check at the top of the range. d3 was dropped at the user's direction.

## The result

100,000 games, 80 jobs, one pooled batch (`test/tools/kitty_ab/gen_v2_vs_v3_manifest.py`). Each arm ran its
OWN shipped artifacts — its own keep table and value sidecar — because this is a comparison of two
shipping configurations, not a card screen. (That is why `deck_compare.py` is the wrong tool here:
its Rule 0 shares ONE apparatus across arms, and the differing apparatus is half the point.)

| cell | games/arm | v2 | v3 | delta | 95% CI | t unpaired | t clustered |
|---|---|---|---|---|---|---|---|
| **d5 b40** | 48,000 | 3.8820 | **3.8265** | **−0.0555** | [−0.0705, −0.0405] | −7.27 | **−14.85** |
| d7 b1000 | 2,000 | 3.8145 | **3.7465** | −0.0680 | [−0.1358, −0.0002] | −1.97 | −4.25 |

Unwon games 1.44% → 1.25% at d5. Pre-registered bar was t ≤ −2 on d5; it cleared by 3.6x on the
conservative estimate.

**v3 is also cheaper**, which is not a trade-off anyone had to price: 719 vs 1312 ms/game at d5
(1.82x), 1609 vs 3392 at d7 (2.11x), and 1.26x fewer search units at d5. The wall gap exceeds the
work gap because v2 carries the heavier tail — one v2 d5 game in this run took 54 minutes
(`--seed 115119 --game-index 1119`), which is a lead worth pulling on its own.

The lists differ by four main-deck cards (`6c655b97`): Adelbert Steiner in / Cid, Freeflier Pilot
out, Golem-Skin Gauntlets 3→4, Skateboard 2→1. Both 60 cards; Cid moves to the sideboard.

### Why the clustered column is the honest one, and why that is the opposite of the last run

The per-cell delta spread was about HALF what independent sampling predicts (d5 sd 0.0211 against
0.0432; d7 0.0453 against 0.0978). That is the same canary signature that caught an invalid run
hours earlier, so it was checked rather than quoted:

* **The invalid run's cause was shared games.** Seed bases 20001..20016 with 1200 games each, and a
  batch job's per-game seed is `seed_base + game_index`, so consecutive bases replayed one game set
  sixteen times (verified: `s20001[gi+1] == s20002[gi]` for 1199 of 1199). Per-cell delta sd was
  0.0017 against ~0.057 predicted, the distinct sample was ~1,215/arm rather than 19,200, and t was
  inflated ~4x. `gen_v2_vs_v3_manifest.py` now asserts stride > game count and that no two ranges overlap.
* **This run's cause is correlated ARMS.** Seeds are structurally disjoint (asserted), and
  `corr(v2 cell mean, v3 cell mean) = +0.761` at d5. The arms share 56 of 60 main-deck cards and the
  same seed bases, so a base opens similar games on both. Positive covariance SHRINKS the delta's
  variance, which makes the unpaired se **conservative** — the headline understates the effect
  rather than inflating it.

The two look identical in direction and are distinguished by the arm correlation, not by the spread
alone. **Compute `corr(arm_a_cell, arm_b_cell)` before concluding either way.** A correlation near
1.0 with a near-zero spread is shared games; a moderate correlation with disjoint seeds is genuine
pairing and is good news.

## Three things the promotion nearly broke

**1. `buckets.json` would have silently changed v3's play.** `BucketPolicy.h:130` resolves
`<stem>.buckets.json` in the deck's OWN directory and never walks up, so v3 was generated and
ground-truthed with NO bucket ruling in scope. Had v1's ruling stayed at the top level, promoted v3
would have inherited it and all 20 of its GT keys would have moved — reading as an engine
regression that did not exist. The file moved with v1. Bucket rulings are USER-only; nothing was
amended.

**2. The 2HG row resolved to v1's key.** `regression.sh:326` strips the suffix
(`base=${deck%2hg}`), so `kitty2hg` was a case on v1's decklist, and it was the archetype's ONLY
2HG coverage — std and 2hg are co-equal, so dropping v1 would have deleted half the archetype's
format coverage silently. It is now `kittyv32hg`, baselined fresh at 4.4000 (v1's was 4.8000).
**A `<deck>2hg` row is invisible to a grep for the deck key.**

**3. Thirty-eight files pointed at the top-level path and meant v1.** Nine scenario tests, a
bucket-policy unit test asserting v1's ruling, five generation gates, the perf probes and sixteen
`test/tools/kitty_ab/` manifest generators all named `decks/KittyEquipment/KittyEquipment.cod`.
Promotion re-aimed every one of them at a different deck. `test/scenarios/kitty_sfput_line.json` is
a Stoneforge Mystic line and v3 plays no Stoneforge. All 38 were repointed at
`v1-kemba-balan/`, which is **provably inert** — identical file content at a new path, so identical
card numbering — whereas leaving them would have been a silent semantic change.

## What the key is called, and why it is not `kitty`

The shipped list's suite key is **`kittyv3`**, not `kitty`. ONE KEY PER DECKLIST: a key never moves
to a different list. Renaming `kittyv3` → `kitty` would have had to reuse the names v1's GT already
occupied, which is exactly the 2026-10-08 repoint that made a stale baseline read as a **+2.2620**
aggregate regression on an equip change — an engine bug that did not exist, distinguished from a
real one only by a deterministic paired A/B. So v1's rows were deleted outright and v3 kept its own
key. This is a deliberate departure from the mirrorwing/angels/critter cosmetic pattern, where the
shipped list happens to hold the plain name; the convention that actually matters is one key per
decklist, and renaming is only safe for a list whose name was never used by another.

## What was kept

v1's and v2's hand-played references are retained and still replayed. USER: *"The only games we
will continue to play from the old lists should be the references. We'll hold on to those."*
`test/viewer_protocol_check.py` globs `references/**` recursively and resolves a versioned folder to
its versioned list, so an archived list keeps its bench without carrying GT rows. v1's 16 top-level
references moved to `references/KittyEquipment/v1-kemba-balan/`; leaving them at top level would
have credited v1's hand-played games to v3 and lent it a green bench it never earned.

**The shipped list therefore has zero hand-played references until someone plays some.** That is the
honest state and it matches what Mirrorwing and CritterLifegain did at their promotions.

## Open

* `test/suite_cost.json` still holds `kitty` and `kittyv2` entries and has none for `kittyv3`. It is
  a measurement table refilled wholesale by `suite_gate.py --measure-all` from one pooled tier run,
  so it was NOT hand-edited. It wants a refill at the next overnight tier. Until then the 3x cost
  rule's denominator is computed from a table naming two decks that are no longer in the suite.
* The 54-minute v2 d5 game is unexplained. v3 is 1.82x cheaper overall but the tail is where the
  difference concentrates, and a single game at 0.90 h against a 40 ms budget is a budget-escape
  worth isolating (`--seed 115119 --game-index 1119` on `v2-puresteel-hammer`).
* Four docs still name `v3-adelbert-gauntlets` (`adaptive-bottoming-cross-deck-test.md`,
  `kitty-v3-mulligan-resume.md`, `kitty-v3-what-fits-the-window.md`,
  `slow-rollout-tail-and-the-uncharged-greedy-walk.md`). They are dated records and were left as
  written; the path they name was correct on the day.
