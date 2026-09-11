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
| `escalation_cap` | 12 | 5, 6 | always target_depth or one below, and DERIVABLE from `value_leaf_table.heuristic_lp` (the convergence depth). Under "always 1 depth" it stops being a choice: compute at load |
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
