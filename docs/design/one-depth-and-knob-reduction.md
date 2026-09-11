# ONE DEPTH EVERYWHERE, and the knob reduction (user directive, 2026-09-11)

## The directive

> *"I don't want full ladder anywhere. It suggests a bug. For escalation it should always be 1 depth.
> For the other approach we could ladder with no-leaf into one heuristic depth. Either way, there
> should be 1 depth and we work to address any issues that come up."*
> *"As-in, if skipping work isn't paying off we figure out why and fix it. Not ship the 'do extra
> work' mode."*
> *"We need fewer of these random knobs."* / *"the best would be only a few."*

So: the full 1..D heuristic escalation ladder is not a shippable mode. Where a single pass loses to
it, that is a DEFECT to diagnose, not evidence for the ladder. And the per-deck knob count is itself
a problem to fix, not just a configuration surface.

## The defect this directive immediately exposed (Melira)

`nocap` (full ladder) beat `cap` (single pass) on three decks: melira +0.0208 +/- 0.0027 (8 better /
77 worse), creature_giving -0.0045 +/- 0.0016, hinata2 -0.0037 +/- 0.0022. Diagnosed with
`MTG_ESC_SINGLE_DIAG=1` (3 games, 24 escalation decisions):

**Melira HAS the bug.** `units=0` in 16 of 24 decisions -- the affordability walk is asked to size a
pass against a budget of ZERO, so nothing is affordable and it floors at `max(1, daff)` = depth 1.
Result: a depth-1 heuristic rollout stands in for a depth-5 value line. Cause: melira's probe
exhausts the SHARED budget, and melira ships no `escalation_fresh_frac`, so the escalation never gets
a fresh allowance.
* This also explains a previously-filed puzzle: cap at R 120 / 12 / 16 was byte-identical on melira in
  all 16 blocks. That was NOT "R is irrelevant here" -- with a zero budget the comparison is
  `anything > 0` for every R. The byte-identity was a SYMPTOM of this defect.
* Fix verified at the mechanism level: with a fresh budget the same 24 decisions show `units=9000`
  (0 of 24 at zero) and targets of 1/2/3 instead of 22-of-24 at depth 1.
* CAVEAT, not yet closed: melira's deep passes are genuinely unaffordable at b20 -- a d5 heuristic
  pass estimates 2,486,071 units against 9,000. So the fresh budget removes the pathology but the
  target stays shallow. Whether melira's quality recovers is an open A/B.

**creature_giving and hinata2 do NOT have it.** Both ship cap AND `escalation_fresh_frac: 0.5`; their
predictor is healthy (`units=13500 -> daff=3 target=3`, `units=9000 -> daff=2 target=2`). Also only 2
`[esc1]` lines in 6 games, i.e. the escalation is rarely reached, so their small `nocap` edge cannot
come from the escalation shape. Most likely cause: cap's ACCIDENTAL side effect -- `escalation_cap > 0`
is what arms probe-leaf recording (`TurnSolver.cpp:36084`), which the path-to-trust rescue (`:35286`)
and the ladder's own cost estimates read on EVERY decision. That is a coupling bug, not a reason to
ship extra rollouts.

## Do NOT fix Melira by adding the knob

Setting `escalation_fresh_frac: 0.5` on melira would be one more per-deck knob for a key that already
ships exactly one value on 7 decks. **Delete the knob instead: make the fresh escalation budget
unconditional.** Then melira cannot be sized against a zero budget by construction, on every deck at
once, and the key disappears.

## Knob inventory, 2026-09-11 (20 decks)

| key | decks | distinct values | verdict |
|---|---|---|---|
| `regime` | 10 | heavy/light | **DEAD** -- "Informative only (not read at runtime)", MulliganProfile.h:92. Delete, zero risk |
| `ladder` | 1 | `"escalation"` | that IS the default => no-op key. Delete |
| `alpha` | 8 | `"relaxed"` | always paired with `leaf: none`; not an axis. Fold into leaf |
| `escalation_fresh_frac` | 7 | `0.5` | one value => unconditional. Delete (fixes melira) |
| `beam_width` | 4 | `3` | constant with extra steps. Hardcode |
| `beam_leafdepth` | 4 | `2` | same |
| `commit` | 0 | -- | unused in production |
| `exhaust_mult` | 0 | -- | unused in production |
| `escalation_cap` | 12 | 5, 6 | `= target_depth` on 10 of 12; `target_depth - 1` on exactly 2 (fivecolour 5/6, stompy 5/6). NOT derivable -- goblins and burn have the same 5-entry `heuristic_lp` and target 6 yet cap 6, so the two exceptions are hand-set. But the ceiling appears INERT: across 26 diagnosed escalations `daff` was 1/2/3 against a cap of 5 and never reached it. Test `cap = target_depth` on fivecolour + stompy by digest; if inert, cap becomes the constant "always on, ceiling target_depth" |
| `escalation_r` | 7 | 7 distinct | a genuine per-deck CALIBRATION, but stale and not a fixed point. Should be EMITTED by generation, not hand-set |
| `leaf` | 8 | `"none"` only | never `"model"` => a boolean, not a string enum |
| `target_depth`, `budget_ms` | 13 | 5/6, 20/30/40 | REAL per-deck policy |
| `mull_gen_depth`, `mull_gen_budget_ms` | 11 | 1-4, 1/3 | REAL, derived by measurement |
| `expected_buckets` | 9 | 11-23 | REAL |

**Target: six real knobs** -- `target_depth`, `budget_ms`, `leaf` (bool), `mull_gen_depth`,
`mull_gen_budget_ms`, `expected_buckets`, plus `escalation_r` as an emitted constant. The 2x2
configuration matrix (leaf x cap) collapses to ONE axis once cap is unconditional.

## Sequencing

1. **Free now** (dead or unused, no measurement): delete `regime`, `ladder`, `commit`, `exhaust_mult`.
2. **Single-valued** (hardcode, byte-identical on the decks that ship them, changes the 16 that do not
   -- so verify by digest): `beam_width` 3, `beam_leafdepth` 2.
3. **Needs a fleet measurement FIRST, as sidecar arms (no code change required):**
   * fresh escalation budget unconditional -- changes play on the 13 decks without it; re-tests melira.
   * `escalation_cap` unconditional -- changes play on the 8 without it. Already measured 2026-09-11:
     7 of 8 are neutral-or-better (breaching/critter/minotaur byte-identical, dragons 0 of 4,000 games
     changed at 0.783x, fluctuator -0.0010 at 0.938x wall, kitty/mirrorwing neutral); melira is the
     only loser and is the deck with the defect above.
   * `alpha` folded into `leaf` -- byte-identical on the 8 leafless decks by construction (all ship
     relaxed); verify by digest.
4. Only then delete the keys and re-accept GT.

## THE REAL FOOTGUN: three activation regimes, invisible from the sidecar (2026-09-11)

User: *"The fact that we ended up with bad results because of not setting escalation_fresh_frac is
another big reason I want to drop these settings. Too many ways to shoot ourselves in the foot."*

The count is not the whole problem. The same sidecar has THREE different activation regimes, and
which one a key belongs to cannot be seen in the file:

| regime | keys | applies when |
|---|---|---|
| unconditional | `leaf`, `alpha`, `ladder`, `commit` | at sidecar LOAD, always (`MulliganProfileIO.h` apply_leaf_policy / DeckShapeScope) |
| `drives()` | `beam_width`, `beam_leafdepth` | `target_depth > 0 && enabled` (`AIEngine.cpp:2498`) |
| `drives()` + depth match | `escalation_cap`, `escalation_r`, `escalation_fresh_frac` | also `m_lookahead_depth == target_depth` (`AIEngine.cpp:2489`) |

Three live consequences:

1. **KittyEquipment is split-brained today.** It ships `enabled: false`, so adopting `leaf: none` on it
   WORKS while adopting `escalation_cap` on it silently does NOTHING. Confirmed empirically: the
   2026-09-11 `capctl` arm (enable alone) was byte-identical 16/16, and the `cap` arm only acted
   because the arm also set `enabled: true`.
2. **It explains the Hinata2 / Kitty d3-only regressions.** At d3 the depth match fails, so
   cap/R/fresh_frac switch OFF while `leaf`/`alpha` stay ON -- the deck runs a configuration nobody
   ever chose or measured, half of one setup and half of another. Those regressions are an artifact of
   this split, not a property of the shape.
3. **Keys whose ABSENCE selects a setting nobody ships** (the Melira defect class):
   * `escalation_fresh_frac` absent => off / shared budget. 7 decks ship 0.5, none ship off. Caused
     Melira's zero-budget depth-1 defect.
   * `escalation_cap` absent => the full 1..D ladder, which the user has now ruled out entirely.
   * `alpha` absent WITH `leaf: none` => **strict**, which `TurnSolver.cpp` says "stops it one depth
     short of the win on deep-win decks (finding 11, Melira / Hinata)". All 8 leafless decks ship
     relaxed. Documented as having already bitten -- on Melira.
   * `beam_width` absent => off, while the beam was adopted 2026-07-18 as quality-better AND faster.
     16 decks silently run without it. Milder: a missed win, not a defect.

**Therefore hardcoding is not merely tidier -- it collapses all three regimes.** A hardcoded behaviour
has one activation rule (always), so "did this key apply here?" stops being a question that can be
answered wrong.

## The ONE key the user is willing to keep

For the no-leaf -> heuristic approach (`commit: "model"`), if it wins on Fluctuator or Melira-class
decks. Evidence so far:
* **Fluctuator: LOSES** +0.0068 +/- 0.0016 at 1.417x units -- putting the leaf back at the final depth
  makes it worse, consistent with its model being the rejected 2026-09-06 candidate.
* **treasure_hunt: WINS** -0.0013 +/- 0.0011 at 0.758x / 0.782x.
* **Dragons: near-win** +0.0008 +/- 0.0004 at 0.398x units / 0.502x wall.
* dstorm / goblins / mirrorwing / stompy: lose. Melira and the other 11 model-leaf decks: in flight.

## THE LIST: testably necessary vs constant vs deleted (user framing, 2026-09-11)

> *"we want to come up with a list that is testably necessary, force those to be set and tested as
> part of our processes and delete or replace the rest with constants."*
> *"We also want good defaults for these prior to them being set."*

Both halves are needed and they are complementary: the DEFAULT carries a new deck through the
pipeline; the REQUIREMENT stops a shipped deck from silently riding that default untested.
"Necessary" = some deck measurably wants a different value.

### Necessary -- set + verified before a deck ships

| key | good default | why that default | necessity evidence |
|---|---|---|---|
| `target_depth` | 5 | 9 of 13 ship it; `BuiltinDefaultPlay` already says 5 | fivecolour/goblins/burn/stompy want 6 |
| `budget_ms` | 20 | 11 of 13 ship it | hinata adopted 30, goblins 40 |
| `leaf` | model | 12 of 20 | both directions strongly: fluctuator leafless 0.433x units z+4.0; breaching/critter leafless 2.9-4.0x WORSE |
| `mull_gen_depth` + `mull_gen_budget_ms` | the deck's PLAY settings | derive_mullgen rule 1 (trusted => no override; play settings often the cheapest arm) | deck-shaped: on fivecolour a LOWER depth is 2.4x MORE expensive |
| `expected_buckets` | as discovered, no policy | bucket rulings are the user's alone | per-deck by construction |

### Replace with constants (one value fleet-wide; the alternative is documented-harmful in two cases)

* `alpha` -> relaxed whenever leafless (8/8; strict = "stops one depth short of the win")
* `escalation_fresh_frac` -> 0.5 always (7/7; OFF caused the melira defect). Measurement in flight.
* `escalation_cap` -> unconditional, ceiling `target_depth` (see the corrected row above; test the 2
  exceptions for inertness first)
* `beam_width` 3 / `beam_leafdepth` 2 (4/4) -- needs a digest check on the 16 decks that lack them

### Delete

`regime` (dead), `ladder` (no-op: its only value IS the default), `enabled` (implied by adoption),
plus `commit` / `exhaust_mult` unless the 2026-09-11 batches earn them.

### `escalation_r`: necessity is UNTESTED

7 decks ship 7 distinct values, but the 120,000-game A/B only ever moved **goblins** (74 -> 240).
Whether the other six beat the plain 120 prior has never been tested. Cheap test: R = 120 on all
seven. If neutral, R collapses to "a constant plus goblins" and leaves the necessary list.

### Enforcement -- copy an existing pattern

`test/keepmodel_exhaustive_ab.sh` already FAILS on `bottoming_enabled != true`. The same shape works
here: a sidecar validator walking every deck in `regression_cases.sh`'s `DECK_FILE` that fails if a
necessary key is absent. That is what makes a good default safe -- the default carries a new deck,
the validator guarantees no deck SHIPS on it untested.
