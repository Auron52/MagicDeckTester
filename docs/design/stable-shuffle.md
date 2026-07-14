# MTG_STABLE_SHUFFLE — common-random-numbers reshuffle for clean policy A/B

**Status on `phase-1-2-deck-analyzer`:** the CRN mid-game reshuffle is the **DEFAULT**;
`MTG_LEGACY_SHUFFLE` forces the historical Fisher–Yates (a byte-identical escape hatch). Ported from
branch `d0-dynamic-model` (origin commit `60080b1`), then **two corrections over the original port**:
1. **Re-randomize each reshuffle (bug fix).** The original keyed every reshuffle on a pinned ordinal
   `0`, so `ShuffleByKey` (a deterministic sort) produced the *same order every time* -> re-shuffling
   was a no-op (same top) and shuffle-to-dig (Ponder, repeated fetch) was broken. Fixed by keying each
   reshuffle on `search_count` (the per-game shuffle ORDINAL): the Nth reshuffle re-randomizes vs the
   (N-1)th, while the Nth reshuffle still ALIGNS across two same-seed playthroughs (CRN benefit from
   `ShuffleByKey` itself, not from pinning the seed). Verified with standalone proofs.
2. **Promoted to default** (was opt-in). Rationale below.

**Why default (not opt-in):** the goal is that *any* game be comparable to *any other* by construction,
without remembering a flag. Keying the reshuffle on (shuffle-ordinal, card-identity) rather than on
turn/timing **confounds a clairvoyant/searching agent's fetch-timing exploit**: under legacy, fetching
turn 1 vs turn 2 significantly re-permutes the library, and the search can prefer the luckier line; under
CRN the first reshuffle is the same order regardless of *when* you fetch, so that edge is gone by
construction. Measured: hinata's shift grows with depth (d0 −2.5% -> d5 −8%), consistent with the search
losing exactly that shuffle-divergence advantage.

**Determination — no bug (checked):** ShuffleByKey is a statistically uniform shuffle (top-card and
per-card-position distributions match Fisher–Yates; no trend). The residual d0 shift (hinata ~−4%,
antilife ~neutral over 5 seeds) is benign per-deck shuffle-luck from using a different valid shuffle; it
becomes the new consistent baseline after the one-time rebaseline.

## Problem

A fetchland's / tutor's post-search reshuffle (`ShuffleAfterSearch`) was a Fisher–Yates keyed on
`(game_seed, search_count)` applied to the **current** library contents. Two policies run on the
**same seed** that fetch differently — a different land, or a different number of times — end up with a
completely different post-fetch draw order. Their win-turn differences are then partly **shuffle luck,
not play quality**, which contaminates any A/B comparison between similar games: human vs teacher,
model vs heuristic, clairvoyant vs non-clairvoyant, or slightly different card sets.

Concrete example: antilife seed 15, game 14 — the human fetched Overgrown Tomb, the teacher fetched
Stomping Ground → different remaining library → divergent turn-2+ draws → different win turns that reflect
the shuffle, not the decisions. On antilife, **most** human≠teacher games are this fetch-shuffle
divergence, not misplays ("neither side misplays, the draws just differ").

## Fix — common random numbers (CRN)

`Library::ShuffleByKey(seed)` (`src/core/Library.h`) orders the live library by a per-copy priority
`splitmix64(seed, m_number)`:

- `m_number` is a **stable deck-setup ID** (per-copy, assigned at setup; shared cards get the same
  number across decklists via the union-numbering scheme — see `.claude/skills/mtg-ai.md`), identical
  across two same-seed games.
- A card's rank therefore depends only on its own `m_number` — **not** on the current multiset or the
  `search_count`.
- Removing a card (a fetch) leaves **every other card's order unchanged**.

So two policies on the same seed draw the **same future modulo the one card each removed**; only genuine
play differences (a different number of cards drawn/removed) move the realized draws. It remains a real,
**non-game-start** reshuffle (the opening shuffle is untouched — this is strictly the mid-game
post-search reshuffle) and **non-clairvoyant** (the search still plans against a decoupled reshuffle via
`shuffle_salt_search`).

`search_count` is deliberately excluded from the CRN key so every reshuffle in a game yields the same
canonical order (max consistency across policies that fetch a different number of times).

Wired (gated) in `ShuffleAfterSearch` at `src/core/SpellEffects.h`; the CRN routine is
`Library::ShuffleByKey` in `src/core/Library.h`.

## Verification on this branch

- **Off = byte-identical:** full smoke (18 jobs, 6 decks × d0/d3/d5) ALL PASS with the flag unset.
- **On shifts exactly the library-search decks:** with `MTG_STABLE_SHUFFLE=1`, smoke is **byte-identical
  for slivers / burn / th / knights** (they never call `ShuffleAfterSearch`) and shifts **antilife**
  (12 fetchlands) and **hinata** (tutor/search). Those two are the entire rebaseline scope.
- Win-rate read on the smoke seed (s1001): antilife d0 903→920 (+17, not at ceiling); d3 250→239 and
  d5 150→144 are **ceiling artifacts** (those cells were at 100%, so any reshuffle change can only show
  as a loss). Avg win-turn ticks up slightly. Confirm per-game that the shifts are shuffle-realignment,
  not play regressions, before rebaselining.

## References — the motivating use case

The whole point: compare a **human** reference game to the **ideal non-clairvoyant search** on the same
seed, with shuffle luck removed, so the measured gap is play quality (not who got the luckier reshuffle).

- **New references (played under the stable default) are clean by construction:** the human game and the
  AI run on that seed reshuffle identically (aligned modulo the specifically-different fetch), so the gap
  reflects decisions, and the search -- with its fetch-timing shuffle-exploit confounded -- is closer to
  an ideal non-clairvoyant agent.
- **Existing references were recorded under the OLD (legacy) shuffle.** They reproduce EXACTLY under
  `MTG_LEGACY_SHUFFLE` (commit-only, never regenerate them). Only the **33** games on the two reshuffle
  decks are potentially "off-shuffle" vs the new default (Anti-Lifegain 30, Hinata2 3); the other **29**
  (Knights / burn / slivers / treasure_hunt) never reshuffle, so they already match the stable default
  byte-for-byte.

### Recoverability of the 33 — which need re-play

After the opening shuffle (untouched), legacy and stable run **identical code** until the first mid-game
reshuffle (`ShuffleAfterSearch`). So a reference is **recoverable as-is** (byte-identical under the stable
default -- no re-play, no flag) **iff no reshuffle precedes a later library draw** in its recorded line.
Classifying each reference's RECORDED (legacy) decision line -- drift-proof, since it reads the saved game
rather than re-running it -- gives:

| deck | recoverable as-is | affected (need legacy flag or re-play) |
|------|-------------------|----------------------------------------|
| Anti-Lifegain | 4 (`s5_gi4`, `s16_gi15`, `s26_gi25`, `s1_gi0`) | 26 |
| Hinata2 | 1 (`s1_gi0`) | 2 (`s2_gi1`, `s12_gi11`) |
| **total** | **5** | **28** |

- The 3 antilife no-trigger games play no fetch/tutor; `s1_gi0` fetches only on its win turn with lethal
  cast after (no post-shuffle draw). Hinata `s1_gi0`'s only Ponder chose a kept ordering, **not** "shuffle
  them away", so no reshuffle fired. All five verified from their recorded lines; `s5_gi4` also confirmed
  byte-identical by a direct legacy-vs-stable replay diff.
- The full per-game verdict + first-reshuffle turn/trigger is the machine-readable manifest
  `references/stable_shuffle_recoverability.json` (a **new sidecar** -- reference game files are never
  edited). This is the "marking" of which refs need the legacy flag; it lives beside the refs, not inside
  them.
- **Handling the 28 affected.** Cheapest (zero re-play): compare them against an AI run under
  `MTG_LEGACY_SHUFFLE=1` -- a blanket, correct rule since every existing reference predates the stable
  default. Downside: a legacy comparison re-permits the fetch-timing shuffle-exploit this change confounds,
  so the "ideal search" arm is the clairvoyant-exploiting one. To get a clairvoyance-clean comparison for a
  specific affected game, **re-play it under the stable default** (recorded choices can't be auto-converted
  -- their indices are tied to the old draw order, and naive `--choices` replay desyncs on engine drift).

**Caveat on empirical replay (do not trust it for this):** replaying a reference's recorded `--choices`
under the current engine desyncs badly (the saved indices no longer map to the same decisions), so the
fallback plays an entirely different line -- often one that fetches when the human didn't -- and a
legacy-vs-stable diff of *that* line is meaningless. Win-turn "matches" are frequently collisions, not
faithful replays (verified: `s16_gi15` replay reproduced win T4 but played Wooded Foothills + Marsh Flats,
not the recorded Stomping Ground + Temple Garden). Classify from the recorded line, never from a replay.

## Promoting to default

Goal: make the CRN reshuffle the **default** so every run is shuffle-clean without the env var. Deferred
from the initial port because flipping it changes realized draws for fetch/search decks, moving their
ground-truth fingerprints → an expensive, commit-bound rebaseline.

**Steps:**
1. Read `.claude/skills/regression-testing.md` (authoritative for the harness + accept flow).
2. Flip the default in `ShuffleAfterSearch` (`src/core/SpellEffects.h`): default to
   `Library::ShuffleByKey`, keeping an inverse `MTG_LEGACY_SHUFFLE` escape hatch (byte-identical
   fallback — the repo's "always keep a byte-identical A/B toggle" bar). Rebuild Release.
3. Run harness smoke → regression → overnight. Expect **antilife + hinata to shift, the other four
   byte-identical**.
4. **Inspect** the shifted games: confirm same-seed policies now draw aligned futures (shuffle
   realignment), not play regressions. Root-cause every searched (d>0) win→loss per the accept gate.
5. Rebaseline via `test/regression.sh <mode> --accept` (never hand-edit GT), for all three modes.

**Do NOT:**
- Touch anything under `references/` — commit-only; STABLE does not clean machine-vs-human.
- Hand-edit ground-truth files (use `--accept`).

**Acceptance:** default-on; smoke + regression + overnight rebaselined; the four non-search decks
byte-identical; every antilife/hinata shift verified as shuffle-clean.

## References

- Origin branch `d0-dynamic-model`, commit `60080b1` — the original code change + rationale.
- Code: `src/core/Library.h` (`ShuffleByKey`), `src/core/SpellEffects.h` (gated call).
- Related but distinct instruments (do **not** confuse): `MTG_SHUFFLE_SALT` shuffle-variance tool
  (`docs/design/shuffle-variance-instrument.md`) and `MTG_SHUFFLE_SALT_SEARCH` clairvoyance-decoupling
  probe. STABLE_SHUFFLE changes *how* the reshuffle is ordered; those two vary/decouple it for measurement.
