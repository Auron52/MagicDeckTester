# MTG_STABLE_SHUFFLE — common-random-numbers reshuffle for clean policy A/B

**Status:** implemented, opt-in via the `MTG_STABLE_SHUFFLE` env var, **off by default**
(byte-identical to the historical shuffle when unset). Committed on branch `d0-dynamic-model`
(commits `60080b1` code, `8a72765` original notes). Not yet promoted to default — that requires a
ground-truth rebaseline (see [Open decision](#open-decision)).

## Problem

A fetchland's post-search reshuffle (`ShuffleAfterSearch`) was a Fisher–Yates keyed on
`(game_seed, search_count)` applied to the **current** library contents. Two policies run on the
**same seed** that fetch differently — a different land, or a different number of times — end up with a
completely different post-fetch draw order. Their win-turn differences are then partly **shuffle luck,
not play quality**, which contaminates any A/B comparison between policies (human vs teacher, model vs
heuristic, clairvoyant vs non-clairvoyant, etc.).

Concrete example: antilife seed 15, game 14 — the human fetched Overgrown Tomb, the teacher fetched
Stomping Ground → different remaining library → divergent turn-2+ draws → different win turns that reflect
the shuffle, not the decisions. On antilife, **most** human≠teacher games are this fetchland-shuffle
divergence, not misplays ("neither side misplays, the draws just differ").

## Fix — common random numbers (CRN)

`Library::ShuffleByKey(seed)` (`src/core/Library.h`) orders the live library by a per-copy priority
`splitmix64(seed, m_number)`:

- `m_number` is a **stable deck-setup ID**, identical across two same-seed games.
- A card's rank therefore depends only on its own `m_number` — **not** on the current multiset or the
  `search_count`.
- Removing a card (a fetch) leaves **every other card's order unchanged**.

So two policies on the same seed draw the **same future modulo the one card each removed**; only genuine
play differences (a different number of cards drawn/removed) move the realized draws. It remains a real,
non-game-start, **non-clairvoyant** reshuffle — the search still plans against a decoupled reshuffle via
`shuffle_salt_search`, so this does not leak draw-order clairvoyance into planning.

Wired (gated) in the `ShuffleAfterSearch` path at `src/core/SpellEffects.h` (~L417–441); the CRN routine
itself is `src/core/Library.h` (~L180).

## Verification

- **Off = byte-identical** to the historical Fisher–Yates (antilife NC-d0 LP 4.838 unchanged with the
  flag unset).
- seed 22 game 21: **diverges under default, aligns under STABLE** → pure shuffle luck, correctly removed.
- seed 10 game 9: **diverges under both** → genuine play difference, correctly preserved.
- NC and clairvoyant policies draw identically under STABLE on game 14.
- `ref_bench` passes the flag through to every policy column, so machine-vs-machine A/B
  (teacher / model / clairvoyant / NC-d0 / NC-d1) is shuffle-clean.

## Open wrinkle — machine-vs-human

The saved human reference games were recorded under the **old** shuffle. STABLE cleans
**machine-vs-machine** A/B, but a fair **machine-vs-human** comparison would need the human's
**decisions** replayed under STABLE. The references are commit-only ground truth and cannot be
regenerated, so this alignment is not available directly — STABLE does not retroactively clean the
existing human reference draws.

## Open decision

Make `MTG_STABLE_SHUFFLE` the **default** vs. keep it a **comparison-only flag**. Promoting it to default
changes the realized draws for every fetch deck, so it requires a ground-truth **rebaseline** across smoke
+ regression + overnight (`test/regression.sh <mode> --accept`). Until that rebaseline is done and
accepted, it ships **off by default** and is used only to isolate play from shuffle luck in A/B runs.

## References

- Commit `60080b1` — the code change (`Library::ShuffleByKey`, gated `ShuffleAfterSearch`).
- Commit `8a72765` — original rationale/wrinkle notes (a section in `docs/design/learned-d0-policy.md`).
- Code: `src/core/Library.h` (`ShuffleByKey`), `src/core/SpellEffects.h` (gated call).
- Related but distinct instruments (do **not** confuse): `MTG_SHUFFLE_SALT` shuffle-variance tool
  (`docs/design/shuffle-variance-instrument.md`) and `MTG_SHUFFLE_SALT_SEARCH` clairvoyance-decoupling
  probe. STABLE_SHUFFLE changes *how* the reshuffle is ordered; those two vary/decouple the reshuffle for
  measurement.
