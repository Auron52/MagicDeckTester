# Do low-depth / low-budget mulligan generation settings produce the same LABELS?

**Status:** DEFERRED to 2026-09-19 by the user. Angels is the test case, and it is a good one.
**User, 2026-09-18:** *"I would actually be curious to test the play settings profile against this
one. It's a good test for our low-depth low-budget profiles."*

## The question

A mulligan generation fits a keep/bottom table by rolling each cell out `R` times and recording
which choice won. The rollouts run at `value_play.mull_gen_depth` / `mull_gen_budget_ms` — for
Angels **d1 / 3 ms** — which is far shallower and far cheaper than the depth the deck actually
plays at (d3/d5, 10–20 ms).

The table is a set of **labels** (keep or mull; which cards to bottom). The open question is not
whether d1 rollouts are *accurate* — they obviously are not — but whether they are accurate enough
to **rank the two choices the same way** a play-depth rollout would. A cell only needs the sign,
not the magnitude. If the sign agrees, d1 generation is free accuracy; if it does not, every
low-depth table in the repo is fitted to a different game than the one it ships into.

**Nothing on record answers this.** There is no provenance for why Angels' `mull_gen_depth` is 1,
and no experiment anywhere in the repo compares generated labels across depths. The settings were
simply what the value leaf wrote.

## Why Angels is the right deck to settle it on

* **Generation is now known-cheap here.** The `complete` recipe at d1/b3 took **~16 minutes** wall
  on 32 cores (2026-09-18, 11:26:49 → 11:43:09), not the ~0.7 h projected. A play-settings run was
  previously estimated at ~3x, i.e. **well under an hour** — so both arms fit comfortably in one
  session. This is the cheapest deck we have ever been able to ask this question on.
* **The d1 table is already validated and live**, so the comparison has a real, accepted baseline
  rather than a straw man:

  ```
  K=14  entries=48232  sub_cells=56924  min_rollouts=40  bottoming_enabled=True
  keep       exhaustive vs static (bottoming held identical)   -0.222750t
  bottoming  blind exhaustive vs lookahead, CONFOUNDED         -0.089313t
  ```
* Angels' keep decision is **not** degenerate — the deck has a real curve, a real mana requirement
  ({W}{W} on two- and three-drops off 20-odd white sources with Seraph Sanctuary tapping {C}), and
  Giada's restricted mana, so keep/mull is a genuine judgement rather than "keep any 7".

## Method

The thing to compare is **labels**, not play. That distinction is the whole point, and it is easy
to get wrong by reaching for the usual A/B.

1. Generate a second table at play settings, into a **separate path** — do NOT overwrite the live
   one. Keep tables are presence-gated (the file existing IS adoption), so the second arm must not
   be written next to the decklist under its canonical name while the first is still shipping.
2. **Diff the tables directly.** Per cell: does the keep/mull label agree? Where bottoming applies,
   does the bottom set agree? Report
   * % of cells whose label agrees,
   * the disagreement rate weighted by cell REACH (a cell that never comes up is not a defect), and
   * whether disagreements cluster (one bucket, one card count) or scatter.
   A scattered low-rate disagreement is noise; a clustered one is a systematic depth artifact.
3. Only then run the play A/B (`mullgen.sh`'s own keep + confounded-bottoming pair) on the deeper
   table, so the two tables' `-0.22t / -0.09t` numbers are comparable head to head.

**Do not settle this on the A/B deltas alone.** Two tables can post similar aggregate deltas while
disagreeing on many individual cells — the deltas are dominated by the cells that come up most, and
the interesting question is whether the shallow rollout mislabels the rare ones. Agreement rate is
the measurement; the delta is the sanity check.

## What the answer changes

* **Labels agree (say >95% by reach):** d1/b3 is vindicated and every existing low-depth table in
  the repo gets a provenance it currently lacks. Generation stays cheap, and we can stop wondering.
* **Labels disagree systematically:** `mull_gen_depth` becomes a real knob that has to be justified
  per deck, and every shipped table generated at d1 is suspect and wants re-deriving. That is a
  large finding, which is exactly why it is worth an hour.

## Related

* `.claude/skills/mulligan-profile.md` — the generation/validation process. **Bottoming is always
  on; it is not a decision and nothing here proposes changing it.**
* `docs/design/value-leaf.md` — where `value_play.mull_gen_*` is written.
* The stale partial run at `logs/mullgen_angels_stale_e6f7ac07/` is from the pre-equip-fix engine
  and is NOT a usable arm for this comparison — it was fitted to play we no longer ship.
