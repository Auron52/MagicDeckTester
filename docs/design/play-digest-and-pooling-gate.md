# Per-deck play digest: regression tripwire + mulligan-pooling gate

**Status (updated 2026-07-03):** the **regression-tripwire half is BUILT**; the
**mulligan-pooling-gate half is still deferred**. See "Implementation status" below.

## Implementation status

**Built (regression tripwire + per-deck runner + analysis integration):**
- **Play digest, engine-side.** `GameLogger` maintains an FNV-1a fold of the ordered REAL
  decision stream (mulligan keeps/bottoms, opening hand, lands, casts + targets/mana/X, draws,
  discards, attacks, per-turn phase markers) — `GameLogger::Digest()`, and a cheap `digest_only`
  ctor that folds without building structures or writing logs. `BatchRunner` attaches a
  digest-only logger to every game (behaviour-neutral: `m_logger` is nulled in search rollouts,
  so win turns stay byte-identical — verified across all decks), collects a per-game digest, and
  folds a per-case digest (`BatchJobResult::digests` / `case_digest`).
- **Storage.** `.wins` gained a 3rd column `<gi> <win_turn> <play_digest>` (optional to every
  reader); `regression_gt.txt` fingerprints became `won/avg/play_digest`. Both promoted by
  `--accept` (legacy 2-field entries match on won/avg until re-accepted). Full logs are still
  never routinely stored — the digest is a few KB/case.
- **Per-deck runner.** `regression.sh --deck=<name>` filters to one deck (safe with `--accept`;
  the aggregate-GT rebuild sources existing GT first, so only that deck's keys change).
  `test/regression_deck.sh <deck> [modes...]` runs one deck across modes.
- **Analysis integration** (ties into `audit_changed_games.py` + `explain_game.py`): the audit
  now diffs the per-game digest column and reports a **`play-changed`** category — games whose
  play moved at the SAME win turn — and prints an inline `explain_game` per-turn diff for each.
  Not a hard gate, but flagged "ANALYZE each" before `--accept`.

**Still deferred (mulligan-pooling gate):** the `ExhaustiveKeep` soft-gate merge, the
rollout-config digest **battery** (depth 5 / budget 20 emittable "mode" for the single-deck
runner), the `meta.play_digest` field + merge-acceptance change in `src/analyzer/ExhaustiveKeep.*`
and `src/ai/MulliganProfileIO.h`. The engine digest built above is the prerequisite those reuse.
The "Config scoping", "Soft-gate merge procedure", and "Sidecar / merge code touch points"
sections below specify that remaining work.

---

**Original design follows (self-contained).**

Deferred until the in-flight Slivers R=100 mulligan
profile is pooled (that repo copy is frozen — see the "Freeze interaction" note at the
bottom). This doc is self-contained; an implementing agent can build from it directly.

## Problem

Two separate needs converge on the same artifact — a compact, deterministic fingerprint of
how a deck *plays*:

1. **Regression sensitivity.** The regression suite's ground truth today is a coarse
   per-case fingerprint (`games_won` / `avg_win_turn`). Two genuinely different play lines can
   land the same win-turn, so a behavior change can slip through. We want a signal that
   detects *any* change to a deck's play, and does so **without re-running the old build** —
   the "before" should already be frozen in ground truth so a single fresh run tells us what
   moved.

2. **Mulligan-profile pooling gate.** The exhaustive bucketed keep/bottom profile
   (`src/analyzer/ExhaustiveKeep.*`, `MTG_KEEP_EXHAUSTIVE`) writes a poolable raw sidecar
   (`decks/<deck>.keepmodel.exhaustive.raw.json`) whose `meta` block carries a `commit`
   fingerprint. Cross-machine / cross-run pooling (`MTG_KEEP_MERGE`) currently **rejects any
   sidecar whose commit ≠ the current commit**, because a play-logic change would make two
   runs' rollouts incomparable. But the commit hash is a *conservative over-approximation*: a
   doc-only, test-only, analyzer-only, GUI-only, or other-deck-only commit changes the hash
   yet leaves a given deck's rollouts byte-identical. Those pools are rejected needlessly
   ("false invalidation").

The right fingerprint answers the real question — *"does this deck's play produce
byte-identical decisions?"* — not the proxy question *"is the source revision identical?"*.

## The play digest

Define a per-deck, per-config **play digest**:

- For each game, the engine already produces an ordered decision/event stream (see
  `src/core/GameLogger.h` and the `--log-dir` trace path in `src/main.cpp`: one trace entry
  per *resolved* decision — cards played, targets, attackers/blockers, mulligan keeps/bottoms).
- **Canonically serialize** that stream (stable field order; exclude any non-reproducible
  fields — wall-clock, thread ids, node/visit counters that depend on scheduling rather than
  seed) and hash it (e.g. FNV-1a, matching the sidecar fingerprints) to a per-game digest.
- **Fold** all games in a case into one **per-case digest** (order-independent combine, e.g.
  XOR/sum of per-game hashes keyed by `gi`, or a hash of the sorted `(gi -> game_digest)`
  list). One hash string per case.

Determinism prerequisite: a case must be byte-deterministic given its seed (same seed →
same play). The suite already relies on this (ground truth is byte-identical across runs; the
only observed overnight deltas are CPU-oversubscription *timing*, not play divergence), so the
digest is stable in all three modes.

### Why this scopes per-deck for free

Because the digest is computed **on the deck being profiled/tested**, a commit that only
touches other decks' cards or non-gameplay code leaves that deck's digest unchanged. The
digest invalidates a deck's pooled sidecars **iff** the change actually moves that deck's
play — exactly the invariant we want. No separate "which decks does this commit affect?"
analysis or allowlist is needed; the per-deck digest *is* that analysis, computed empirically.

Worked example that motivated this: an integration merged ~23 commits focused on the replay
viewer and the Anti-Lifegain deck. Post-integration smoke was 18/18 byte-identical to the
pre-integration ground truth, including the Slivers and Knights cases — evidence those decks'
play did not move, even though every commit changed the commit hash. Under a digest gate,
Slivers/Knights sidecars from before the integration would remain poolable; under the commit
gate they are rejected.

## Storage policy

- **Commit only the digest to ground truth**, across **all three modes** (smoke, regression,
  overnight). A hash-per-case is a few KB total regardless of game count, so overnight's large
  game counts add negligible size while broadening seed coverage (catches changes that only
  manifest on library orders the smoke/regression seeds miss). Sits alongside the existing
  `games_won`/`avg_win_turn` fingerprint; diffing two ground-truth files shows at a glance
  which decks moved.
- **Never routinely store full logs.** Measured per-game JSON log ≈ 20 KB (min 12, median 19,
  max 33 on d5 burn — the long end). Full-log capture would be ≈ 160 MB (smoke, ~8.2k games)
  + ≈ 275 MB (regression, ~13.8k games) ≈ **440 MB/run** — untenable to keep routinely.
- **Full logs are on-demand only.** When a digest mismatches, regenerate full `--log-dir`
  logs for *just the diverging case* (~20 MB for a 1000-game case, or narrow to the one
  seed/depth that moved) and inspect. That is exactly when you want logs anyway — you are
  solving a specific issue. Kept locally under `logs/`, never committed.

## Config scoping (important)

The digest depends on the play config (depth, budget, seeds). Two distinct digests are needed:

- **Mode digests** — computed under each regression mode's own configs. These are the general
  regression tripwire, committed to ground truth per mode.
- **Rollout-config digest** — computed under the **mulligan rollout config** (depth 5,
  budget 20, the continuation setup the sidecars average, per `ExhaustiveKeepConfig`). *This*
  is the digest the pooling gate must use, because it certifies the exact play the rollouts
  depend on. A broad mode-digest match is strong evidence but is not literally the rollout
  config.

Implement the rollout config as an additional **digest battery** the single-deck runner can
emit (a fixed set of that deck's goldfish games at depth 5 / budget 20). Then:
- **Regression** gates on the mode digests.
- **Pooling** gates on the rollout-config digest.

## Regression harness changes

1. **`--deck=<name>` filter** for `test/regression.sh`. None exists today (the arg loop only
   handles `--smoke` / `--overnight` / `--accept*`; it then iterates the whole mode array from
   `test/regression_cases.sh`). Add a flag that, when set, skips case specs whose `deck` field
   ≠ the requested name. ~5 lines: parse in the `for arg in "$@"` loop, guard in the case
   iteration. Useful independently of the digest.

2. **Digest computation + storage.** In the per-case run path, capture each game's decision
   stream, fold to a per-case digest, and write it into the mode's results/ground-truth
   fingerprint next to the existing counters. Add the digest to the `--accept` promotion so an
   inspected run's digests become the new baseline (never hand-edit).

3. **Rollout-config digest battery** as an emittable "mode" for the single-deck runner
   (depth 5 / budget 20 fixed), for the pooling gate.

## Soft-gate merge procedure

With the digests in place, the commit-hash gate becomes *soft* — a mismatch triggers cheap
re-verification instead of a hard reject:

1. A sidecar's `meta.commit` ≠ current commit.
2. Run just that deck at the current commit: `regression.sh --deck=<name>` for each mode
   (mode digests) **and** the rollout-config battery (rollout digest).
3. Compare against the ground-truth digests baselined at the *old* commit (the "before" is
   already frozen — no need to rebuild/re-run the old binary).
4. If the rollout digest matches (and mode digests as corroboration) ⇒ the deck's play is
   byte-identical across the two commits ⇒ the old sidecar is poolable. Merge, and record
   **both** commits in the merged sidecar's provenance as an equivalence class.
5. Gate future merges on the **play digest**; demote `commit` to an advisory provenance field.

## Sidecar / merge code touch points

- Sidecar `meta` fingerprints and the merge gate live in `src/analyzer/ExhaustiveKeep.cpp`
  (`RunExhaustiveKeep`, `RunKeepMerge`) and `ExhaustiveKeep.h`. Add a `play_digest`
  (rollout-config) field to `meta`; keep `commit`, `bucket_fp`, `deck_fp`, `equiv_seed`,
  `seed_base` as-is. Change the merge acceptance test from "commit must match" to "play_digest
  must match (commit advisory); seed_base must be distinct; bucket_fp/deck_fp/equiv_seed must
  match".
- Serialization of the digest goes through `src/ai/MulliganProfileIO.h` alongside the existing
  meta fields.

## Freeze interaction (read before building)

At the time of writing, the primary repo copy is **frozen at commit `9c11ae5`** while an
overnight Slivers R=100 mulligan profile is generated and pooled (R=20 done, R=80 pending).
Building this feature there — editing source, rebuilding, committing — would bump HEAD and
invalidate the in-flight sidecars' commit stamp (the very false-invalidation this design
removes, but the gate isn't built yet, so it would bite). An implementing agent should work in
a **separate branch/worktree/clone**, not on the frozen copy, and the frozen copy must not
pull those changes until R=100 is pooled. Once R=100 is done and adopted, land normally.
