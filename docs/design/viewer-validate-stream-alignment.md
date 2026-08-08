# `viewer_validate_check.js` stream alignment — remaining work

Status: **partially fixed 2026-08-08**; 13 residual failures, all Goblins. Not a blocker — the two
suite-wired viewer checks are green on all 178 references.

## What this check is, and why it was lying

`test/viewer_validate_check.js` is the only check that exercises `--validate-line`
(`TurnSolver::CheckLine`). It rebuilds each reference's played line exactly as the GUI would
(`linebuild.js`), replays the recorded choices as a positional `--choices` prefix, and asserts the
verdict is `accept` / `choose`.

It is **not wired into any suite** (`viewer_checks.sh` runs the decision-type, parity, line-build
and protocol layers, not this one), so it drifted unnoticed.

### Bug 1 — no `--force-mulligan` (FIXED)

The check never reconstructed the recorded opening hand, so a reference recorded after mulliganing
to 5 was validated against a 7-card game. Every positional choice then indexed a different decision
and the verdicts were meaningless.

This hid behind an accident: references that carried mulligan/bottom **decision frames** had those
recorded answers replayed positionally *as* the live mulligan, which re-aligned the stream by luck.
`scripts/ref_regenerate.py` folds those frames into the `mulligan` header, so regenerating 69
references removed the accident and the breakage surfaced all at once.

Measured on `treasure_hunt/claude_s4_gi3` T2 — the same line, same prefix:

| | verdict |
|---|---|
| without `--force-mulligan` | `illegal (can't play land 'Reliquary Tower': land drop unavailable)` |
| with `--force-mulligan 4:41,1,20,16` | **`accept`** |

Fixed by mirroring `viewer_protocol_check.py`'s `force_arg()` + `FORCED_MULLIGAN_TYPES`.

### Bug 2 — no tolerance for inserted breakpoints (PARTIALLY FIXED)

`MTG_PLAY_SEGMENT_ALWAYS` makes Commit Line re-prompt inside a main phase. A reference recorded
before it has no answer for the new segment, so every later choice slips by one.

Proven on `Goblins/claude_s22_gi21`: at prefix `[0,1]` the engine offers **T2/pre_main a second
time**, so the recorded prefix `[0,1,-1]` lands on T2/post_main and the T3 line reports "land drop
unavailable". Insert one pass — prefix `[0,1,-1,-1]` — and the identical line returns **`accept`**.

`alignPrefix` now probes the offered frame and passes extra main-phase segments until the recorded
frame lines up. It does not absorb a **sub-decision** (`lackey_put` / `tutor_etb` / `echo`)
interleaved at the shift point: the probe stops there and the line reports `NO_VALIDATION_BLOCK`.

## Measurements (178 refs)

| arm | accept | REGRESSION |
|---|---|---|
| pre-change tree `337d1b1`, check as-was | 451 | 141 |
| HEAD, check as-was | 301 | 306 |
| HEAD, `--force-mulligan` fix | 628 | 15 |
| HEAD, + `alignPrefix` | **631** | **13** |
| pre-change tree `337d1b1`, both fixes | 639 | 4 |

So 141 of the original failures predate this work entirely; the fixes clear all but 13, and 10 of
those 13 are the A1 stream shift on **Goblins** references (Goblins lines create new play mid-phase,
so it is the deck A1 re-prompts most, and none of its references were regenerated — the protocol
checker already rated them `ok`, so `ref_regenerate.py` skipped them by design).

## Options for the remainder, cheapest first

1. **Regenerate the Goblins references.** They would then record A1's segments literally and the
   positional stream would align with no code change. `ref_regenerate.py` skips `ok` references
   deliberately ("no churn"), so this needs an explicit opt-in flag. Touches user-owned files under
   `references/` — **requires the user's say-so** (see the commit-only rule in CLAUDE.md), and it
   would fold their mulligan frames into the header, as the 69 already regenerated did.
2. **Rebaseline.** `node test/viewer_validate_check.js --update-baseline` records the 13 as known
   fails. Cheap, but it enshrines artifacts as expected and blunts the check.
3. **Finish the driver (the real fix).** Drive the engine decision-by-decision and answer *any*
   unaligned frame — including sub-decisions — from the recorded stream by content rather than by
   position, i.e. what `viewer_protocol_check.py` already does with `frame_ident`. The two checks
   would then share one alignment implementation instead of having two divergent ones. This is the
   durable answer and the reason the residue is Goblins-only.

## Also worth doing

Wire this check into `test/viewer_checks.sh` once it is green. It was written to close a real blind
spot ("nobody validates the reconstructed line against the engine") and then left out of the suite,
which is how it reached 141 stale failures without anyone noticing. Runtime is ~4–5 min after the
alignment probe (roughly double, one extra binary invocation per validated line).
