# MTG_STABLE_SHUFFLE — common-random-numbers reshuffle for clean policy A/B

**Status on `phase-1-2-deck-analyzer`:** mechanism applied opt-in via the `MTG_STABLE_SHUFFLE`
env var, **off by default** (verified byte-identical to the historical shuffle when unset — full
smoke ALL PASS). Ported from branch `d0-dynamic-model` (origin commit `60080b1`). **Promotion to
default is pending** a per-game inspection of the shifted decks + a ground-truth rebaseline (see
[Promoting to default](#promoting-to-default)).

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

## Open wrinkle — machine-vs-human

The saved human reference games were recorded under the **old** shuffle. STABLE cleans
**machine-vs-machine** A/B, but a fair **machine-vs-human** comparison would need the human's
**decisions** replayed under STABLE. The `references/` games are commit-only ground truth and cannot be
regenerated, so STABLE does not retroactively clean the existing human reference draws.

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
