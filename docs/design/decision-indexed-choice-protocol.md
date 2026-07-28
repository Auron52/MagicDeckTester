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
3. **#6 Dwarven Hold — REOPENED as a HUMAN decision (user-corrected 2026-07-28).** An earlier note here
   argued #6 needs no decision because the burst is definitionally `storage_burn = min(storage_counters,
   cost.ManaValue() − floating.Total())` (`SpellEffects.h:4348`), "search-driven via plan choice, tapped
   LAST, reserved when unneeded", and charge is automatic (idle → +1). **That reasoning holds ONLY for the
   CLAIRVOYANT SEARCH** — its "reserve when unneeded / tap last" is a foresight-tuned heuristic, and the
   user confirms the clairvoyant/search side needs NO change. **But the HUMAN plays WITHOUT clairvoyance**,
   so that heuristic is not the human's judgment. **User-refined scope (2026-07-28): the decision is the
   TAP-vs-CHARGE binary, NOT the burst count.** The user is "less worried about choosing the number of
   counters to burst rather than 'is it untapped or tapped this turn'. That is an actively difficult decision
   to make for the non-clairvoyant, since you don't know your full hand when you make it." A storage land
   charges +1 automatically at the end of an *idle* turn; tapping it (for its {C} and/or to burst its counters)
   forfeits that turn's charge. So the human decision is per-land-per-turn: **hold it untapped to charge the
   battery, or tap/burst it now** — genuinely hard without clairvoyance because the payoff depends on a future
   hand you can't see. Surface THAT binary, wired like #4 via a keyed side-channel keyed by (permanent#, turn)
   or just turn if one storage land. Default = the current heuristic (tap/burst when the search would) →
   byte-identical when the human accepts the default. This sidesteps the messy `TapForCost` mid-cast burst
   count: we only gate whether the land is *available to tap at all this turn* (a clean pre-turn/per-land veto),
   not the burst arithmetic. Find the cleanest "is this storage land tappable this turn?" hook during
   implementation.
4. **#10 cast-order side-channel — ENGINE/CLI/SERVER DONE (2026-07-28); GUI pending.** The committed cast
   order rides a MAIN-PHASE-ORDINAL-keyed side-channel `--cast-order "<ord>:A|B|C;<ord>:X|Y"` (names
   pipe-separated — MTG names carry ',' but never '|'; ordinal = the Nth main-phase decision, 0-based,
   tracked by `AIEngine::m_ext_main_ordinal` incremented once per external-chooser call). `ReorderPlanCasts`
   (AIEngine.cpp) reorders the chosen plan's non-sacrifice hand casts to the human's names (greedy match,
   unnamed casts keep relative order) and sets `searched_order` so ApplyPlanDirect executes vector order.
   `CastOrderChooser` hook (GameLogger.h, RevealLogPause-nulled) is consulted ONLY at the top-level
   external-chooser site (never a rollout). No permutation enumeration, no `--choices` index churn →
   existing references (absent `--cast-order`) replay byte-identically in canonical order. VERIFIED: smoke
   21/21 byte-identical, 0 play-drift, 0 validate-regression; trace-confirmed the ordinal keying and exact
   order honouring on Dragonstorm s24 (ord 2 → turn-2 [Rite of Flame, Pyretic Ritual] reversed to
   [Pyretic Ritual, Rite of Flame]). This is the option-(b) that succeeds where the reverted post-sort
   append (stash `wip-10-postsort-append`) failed. `server.js buildArgs` emits `--cast-order` from a
   `p.castOrder` `{ mainOrdinal: [names] }` map (empty ⇒ omitted). **REMAINING:** the GUI (drag-reorder a
   committed plan's casts → populate `S.castOrder`, key by client-side main-phase ordinal), the saved-
   reference format (store the reorder so a re-played reference carries it), and the reference-check
   extraction of `--cast-order` from such a reference (no-op until one is saved). The viewer must emit
   `--cast-order` ONLY for decisions the human actually reordered — passing even the canonical order flips
   on `searched_order`, which SKIPS the CastOrderRank sort / Spectacle-hoist path and could differ.

## Verification gates (every increment)

- `./build.sh` clean.
- Autonomous byte-identical: `test/regression.sh smoke` (GT digest unchanged) — the choosers are
  human-play only, so autonomous must not move.
- `python3 test/viewer_protocol_check.py` — 0 play-drift.
- `node test/viewer_validate_check.js` — 0 regression (rebaseline only for intended CheckLine shifts).
- `node test/viewer_client_check.js` — undo-property holds (guards the #2 bookkeeping under keyed choices).
