# Viewer interactive mode (`--interactive`): the prefix-cache for the quadratic step slowdown

**Status: implemented 2026-09-09 (overnight session 11), parity-checked; ships with the viewer
fixes batch.** User report it answers (2026-09-09, EDF seed 8): *"the mana spending is so poor and
it becomes progressively slower for some reason."* The mana half was 4784796b; this is the
slowness half.

## The defect shape

The play viewer's `/api/step` is stateless: every human click re-invokes `mtg --claude-play
--choices <every pick so far>` and the engine re-simulates the whole game to reach the new frame.
Step N therefore re-enumerates all N-1 earlier plan fans. On a long manual combo turn (the EDF
blink loop runs 100+ frames, some with 10^2..10^5 plans under `MTG_UNPRUNED`) the per-click cost
grows without bound — measured on the seed-8 game: ~66 ms at step 0 to ~250 ms by step 160 and
still climbing linearly (agent scratch `logs/item_c/goff_s8.json`).

## The fix: a persistent child, same code path

`--interactive` (opt-in, the server's default) changes exactly one thing about `--claude-play`:
when a chooser exhausts the `--choices` stream it still emits its `<<<CLAUDE_DECISION>>>` block,
but instead of `exit(70)` it calls `ClaudePlayHarness::AwaitMoreChoices()` — block for ONE stdin
line of comma-separated picks, append them to the stream, and `goto` back into the same chooser's
consume branch. The game object never restarts, so each step costs only its own frame's
enumeration. Non-interactive behaviour is byte-identical (the helper returns false immediately and
the site exits 70 as always).

Implementation notes:

* All 29 positional-chooser emission sites in `src/main.cpp` got the identical two-line transform
  (a `claude_retry_N:` label before the consume guard, `if (AwaitMoreChoices()) { goto
  claude_retry_N; }` before the exit). Labels are numbered because one lambda (the target chooser)
  holds two sites.
* The three side-channel PROMPT frames — firebreathe, Jitte, storage-hold — keep their
  unconditional `exit(70)`. Their answers are argv-keyed (`--firebreathe "turn:count"`, never a
  `--choices` slot), so a respawn is the only delivery route; they are rare, and the fallback is
  exactly the old behaviour.
* The consume branch after the await also writes the trace entry, so a reference saved from an
  interactive game records the same decisions a stateless chain would have.

`tools/play/server.js` keeps ONE live child (`isession`, single-user tool) keyed on the full argv
minus `--choices`. A step that extends the child's stream writes only the delta picks; anything
else — rewind/undo, changed side-channel args, a validate/save spawn, child death, malformed
output, step timeout — kills the session and falls back to the stateless respawn, which remains
the source of truth. Opt out with `PLAY_INTERACTIVE=0`.

## Parity evidence

`test/interactive_parity_check.py`: drives one game per configured (deck, seed) through a single
interactive child answering with the engine's own defaults, then replays every prefix statelessly
and byte-compares each emitted decision frame and the terminal result. Run it whenever the
decision-emission contract changes.

## What this deliberately does NOT do

* No engine state snapshot/serialisation, no plan-list caching, no skipping of enumeration —
  those all create a second code path whose divergence from the stateless truth would be invisible
  until a reference drifts. The persistent child runs the code the stateless chain runs.
* The regression/reference tooling (`viewer_protocol_check.py`, `ref_line_replay.py`) stays on
  stateless spawns: those are batch sweeps where per-step latency is amortised and process
  isolation is worth more. (If the 306-ref sweep ever needs the speedup, drive it through the same
  stdin protocol — the parity check shows how.)
