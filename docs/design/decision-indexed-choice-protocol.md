# Decision-indexed choice protocol

**Status:** design + staged implementation (started 2026-07-28). Chosen by the user over
"ship #4/#6 on + re-baseline" because it makes existing references forward-compatible and
unblocks BOTH the deferred #4/#6 (combat decisions) and #10 (cast-order channel). See
`docs/design/viewer-fixes-2026-07-27.md` for the viewer-fixes backlog this serves.

## The problem it solves

The play viewer replays a game statelessly from `(deck, seed, game-index, --choices)`. Today
`--choices` is a **positional CSV of ints**: a shared `cursor` walks it, and every decision's
`decision_index` IS that cursor value (its firing ordinal). `main.cpp` has ~15 chooser lambdas
plus the `main_phase` external chooser (`AIEngine::SetExternalChooser`), all consuming
`choices[cursor++]`.

Because the index is the *firing ordinal*, **inserting a new decision shifts every later
decision's index**. So adding a new decision type (e.g. the #4/#6 combat firebreathe-amount
decision) desyncs the replay of every existing reference that reaches it: the old recording has
no choice for the new decision, so the chooser consumes the *next* decision's int and the whole
stream slides by one → `viewer_protocol_check` play-drift, `viewer_validate_check` illegal for the
affected games. The references are user-owned (`references/`, commit-only), so they cannot be
re-recorded by an agent — the desync would be permanent until the user re-plays each one.

## The fix: a STABLE per-decision key

Key each decision by a coordinate that does **not** renumber when a *different* decision type is
inserted:

    key = "<type>#<ordinal-within-type>"      e.g. main_phase#0, main_phase#1, target#0, firebreathe#0

Per-type ordinals: the Kth `main_phase` decision is `main_phase#K` regardless of how many
`firebreathe`/`target`/etc. decisions fire around it. Adding a NEW type (firebreathe) only adds
`firebreathe#*` keys; it never touches `main_phase#*` / `target#*` numbering. So:

- **Existing references replay byte-identically.** They carry `main_phase#*` / `target#*` / … keys
  (derivable from the decision `type` already stored in each trace entry). A newly-added
  `firebreathe#K` key is simply ABSENT → the engine falls back to that decision's
  `heuristic_default` (= the current greedy `ApplyFirebreathing`), which is exactly what the old
  game did → same result, no desync, **no re-baseline of the user's references**.
- **New decisions insert cleanly.** A new type adds its own key namespace.
- **#10 becomes cheap.** Once choices are keyed, the `main_phase` choice value can be extended to
  carry the committed cast ORDER alongside the plan index without a positional shift (the
  order-channel), instead of enumerating all permutations.

## Ask-vs-default (the subtle part)

Two replay contexts need different behaviour when a decision's key is absent from the stream:

- **Live viewer step** (`/api/step`): the human has answered decisions `0..k-1`; the engine is at
  the next decision, whose key is absent → **emit the decision (exit 70)** so the viewer prompts.
- **Complete replay** (the reference checks, "load a saved game"): the stream is the FULL set of
  choices the game made; a decision whose key is absent is a NEW-type decision the old recording
  predates → **use `heuristic_default`, never exit 70**.

Rule: a new `--choices-complete` flag selects complete-replay mode (absent key → default). Default
(unset) is live mode (absent key → exit 70), matching today's cursor-exhaustion behaviour. The
viewer's per-step calls stay live; the checks and reference-load pass `--choices-complete`.

`--validate-line` is orthogonal: it still fires at the first main_phase decision whose key is absent
(live mode), reconciling the hand-built line — unchanged.

## Backward compatibility

`--choices` accepts BOTH formats, auto-detected:

- **Legacy positional** (`"0,1,4"` — no `=`): consumed in firing order via the cursor, exactly as
  today (byte-identical). Every current caller keeps working with zero changes until migrated.
- **Keyed** (`"main_phase#0=0,target#0=4,firebreathe#0=2"` — contains `=`): matched by key.

Existing reference JSON stores `chosen` + the full `decision` (with `type`) per entry, so the checks
can DERIVE keyed choices from an old reference without the reference changing on disk (walk the
decisions in order, count per type, emit `<type>#<ordinal>=<chosen>`). No reference file is rewritten.

## Implementation: keyed SIDE-CHANNELS per new decision class (chosen over a uniform rewrite)

The uniform-keying refactor above (make ALL ~15 choosers key by `<type>#<ordinal>`) is the "pure"
design, but rewriting the working positional stream is high-risk (per-chooser clamp/sink-clear/
validate-line logic each has to stay byte-identical) for little marginal benefit. The insight that
makes it unnecessary: **a NEW decision class that fires at a naturally-unique coordinate needs no slot
in the positional stream at all — give it its own keyed side-channel arg and leave `--choices`
untouched.** This IS decision-indexing (the new decision is keyed by a stable coordinate), realized
incrementally without disturbing the proven positional replay.

- **Firebreathing fires at most once per combat = once per turn**, so its key is the TURN number. New
  arg `--firebreathe "5:2,6:0"` (turn → activation count). At combat of turn T the chooser looks up T;
  absent → the greedy `heuristic_default` (current `ApplyFirebreathing`). `--choices` is not touched.
  → **every existing reference replays byte-identically** (no `--firebreathe` ⇒ all greedy), zero
  re-baseline; **all three checks stay green** (they pass no `--firebreathe`); new viewer games pass
  `--firebreathe` with the human's per-turn picks. This is the whole payoff of the protocol choice, at
  a fraction of the risk.
- **#6 Dwarven Hold storage-burst** rides the same side-channel pattern (its natural key is the
  permanent + turn, or the tap event) — a sibling arg or a shared `--combat-amount`-style channel.
- **#10 cast-order** rides a main-phase-ordinal-keyed side-channel (`--cast-order "main#3:Lathliss,
  Apex,…"`); the executor reorders the accepted plan's non-sac casts to it — no permutation
  enumeration, no index churn, fixes the 6+-cast go-offs option (a) couldn't.

A future uniform `ChoiceStream` keying (the section above) remains available if the side-channel count
ever grows unwieldy, but per-class channels are the pragmatic path and keep each change small and
independently verifiable.

## Staged work

1. **#4 firebreathe side-channel (engine, CLI-verified).** `--firebreathe` arg + parse (turn→count);
   a `FirebreatheChooser` hook in GameLogger.h (nulled in RevealLogPause) consulted from
   `GameEngine.cpp:361`; refactor `ApplyFirebreathing`→`int(..., int max_activations=INT_MAX)` (returns
   activations; default INT_MAX ⇒ autonomous byte-identical), probe greedy-max on a copy, apply k;
   `WriteFirebreatheDecisionJson` (exit-70 when the viewer hasn't supplied the turn's pick) + trace.
   Verify: smoke GT unchanged, 0 play-drift, 0 validate-regression, client-check green — all WITHOUT
   passing `--firebreathe`; then a CLI test WITH `--firebreathe` confirms k is honoured + replays.
2. **#4 GUI modal.** A combat-phase surfacing in index.html (new context — the viewer auto-runs combat
   today): +/- amount modal defaulting to greedy max, `firebreathe` in `AUTO_RESOLVABLE`. jsdom
   `viewer_client_check` asserts the modal + the `--firebreathe` the viewer emits.
3. **#6 Dwarven Hold** storage-burst amount via the same pattern.
4. **#10 cast-order side-channel** (optional follow-on).

## Verification gates (every increment)

- `./build.sh` clean.
- Autonomous byte-identical: `test/regression.sh smoke` (GT digest unchanged) — the choosers are
  human-play only, so autonomous must not move.
- `python3 test/viewer_protocol_check.py` — 0 play-drift.
- `node test/viewer_validate_check.js` — 0 regression (rebaseline only for intended CheckLine shifts).
- `node test/viewer_client_check.js` — undo-property holds (guards the #2 bookkeeping under keyed choices).
