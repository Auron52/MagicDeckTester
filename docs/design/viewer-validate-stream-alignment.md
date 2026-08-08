# `viewer_validate_check.js` — how it broke, and the fix

Status: **RESOLVED 2026-08-08.** 713 accept / 72 choose / **0 known-fail / 0 REGRESSION** over all
178 references, and the check is now wired into `test/viewer_checks.sh` (full mode).

## Why this happened at all

`test/viewer_validate_check.js` is the only check that exercises `--validate-line`
(`TurnSolver::CheckLine`). It rebuilds each reference's played line exactly as the GUI would
(`linebuild.js`), replays a choices prefix, and asserts the verdict is `accept` / `choose`.

**It was wired into no suite.** `viewer_checks.sh` ran the decision-type, parity, line-build and
protocol layers — not this one. So it drifted to **141 failures on an untouched tree** with a
116-line "known-fail" baseline that had absorbed the drift. An unrun check is not a check; that is
the root cause behind all four bugs below.

## The four bugs

### 1. No `--force-mulligan`

The check never reconstructed the recorded opening hand, so a reference recorded after mulliganing
to 5 was validated against a 7-card game and every positional choice indexed a different decision.

Hidden by an accident: references carrying mulligan/bottom **frames** had those recorded answers
replayed positionally *as* the live mulligan, which re-aligned the stream by luck.
`scripts/ref_regenerate.py` folds those frames into the `mulligan` header, so regenerating 69
references removed the accident and a long-latent bug surfaced all at once.

`treasure_hunt/claude_s4_gi3` T2, same line and prefix: `illegal (land drop unavailable)` →
**`accept`** once forced.

### 2. Positional replay could not absorb an inserted breakpoint

`MTG_PLAY_SEGMENT_ALWAYS` re-prompts inside a main phase, so a pre-segment recording has no answer
for the new frame and every later choice slips by one.

`Goblins/claude_s22_gi21`: at prefix `[0,1]` the engine offers **T2/pre_main a second time**, so the
recorded prefix `[0,1,-1]` lands on T2/post_main and the T3 line reports "land drop unavailable".
Prefix `[0,1,-1,-1]` → **`accept`**.

Fixed by **sharing one alignment implementation** instead of growing a second: the check now calls
`viewer_protocol_check.py --emit-resolved`, which walks each reference against the current engine
and returns the content-resolved pick stream plus every recorded main-phase frame's offset into it.
The protocol checker already did this correctly (`frame_ident` alignment: pass an unaligned
main-phase frame, answer any other unaligned frame as the unattended engine would, re-anchor stale
indices by plan content). Two divergent copies of this logic is exactly how the check drifted.

### 3. 40 references were never replayed — by either check

`viewer_protocol_check.py` resolves a reference's deck through a hardcoded `DECKS` map and returned
**`ok`, "skip (unknown deck dir)"** for anything missing from it. Missing: `Goblins` (30 refs) and
`Creature_Giving` (10) — the latter because the reference dir is `Creature_Giving` while the deck
folder is `decks/Creature Giving` (a space). So 40 references, including every one of the Goblins
games a 13-issue viewer batch was built against, were reported as verified without being run.

Both decks are now in the map, and an unresolvable deck dir is a **contract failure**, not an `ok`.
The validate check no longer keeps its own name→path guess either; it takes the deck and profile
from the resolver.

### 4. Aether Vial deploys were encoded as casts

A vialled card is in hand but is never cast and pays no mana. Encoding it as `cast=` asks CheckLine
to pay a cost that never existed — `Goblins/claude_s18_gi17` T4 read `illegal (can't pay {1}{R}{R}
for 'Goblin Chieftain')` on a line played by putting it onto the battlefield with a vial.

The plan JSON carries no structured flag for this (unlike `activate` / `sacout`), only the emitter's
`" (vial)"` summary tag, so the check cursor-matches cast names through the summary in plan order.
A plan can hold two copies of one card with only the second vialled ("cast: Marshal of Zhalfir,
Marshal of Zhalfir (vial)"), so a naive `summary.includes(name + " (vial)")` marks both — that
produced four bogus `vial=X;vial=X` failures on Knights before the cursor walk replaced it.

**Worth doing later:** emit a structured `"vial": true` on the action, as `activate`/`sacout`
already do, and drop the summary parsing.

## Measurements (178 refs)

| arm | accept | REGRESSION |
|---|---|---|
| pre-change tree `337d1b1`, check as-was | 451 | 141 |
| HEAD, check as-was | 301 | 306 |
| + `--force-mulligan` | 628 | 15 |
| + shared resolver alignment | 676 | 2 |
| + vial encoding | **713** | **0** |

The baseline file is now empty: nothing is a known fail. `test/viewer_protocol_check.py --strict`
over the same 178 (now including the 40 previously skipped) reports 156 ok / 22 repaired /
0 play-drift / 0 shuffle-dead / 0 enum-gap / 0 mull-drift / 0 contract-fail.

## Lesson

Every bug here was invisible for the same reason: **the check was not in the suite, and its failure
mode was to report `ok`.** Prefer loud failure over a silent skip — bug 3 in particular passed 40
unverified references for as long as the map was stale.
