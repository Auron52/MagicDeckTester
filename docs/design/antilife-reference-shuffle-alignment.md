# Anti-Lifegain reference alignment after the shuffle changes

**Date:** 2026-07-22. **Outcome:** of 30 `references/Anti-Lifegain/claude_s*_gi*.json`
games, **6 kept, 24 deleted.** This note records the rule, the method used to decide,
and why the coarse prior classification was refined.

## Why references drift on a shuffle change

Across all the shuffle work — stable-shuffle default (`MTG_LEGACY_SHUFFLE` reproduces
legacy) and the numbering fix `b3f0bd5` ("number cards ALWAYS so CRN reshuffle is real")
— **the opening Fisher-Yates shuffle is unchanged.** `b3f0bd5`'s own message: *"Numbering
is post-shuffle-order based and independent of the opening Fisher-Yates shuffle, so opening
hands are unchanged; only mid-game reshuffles move."*

So a saved reference only misaligns when a **mid-game reshuffle** (fetchland crack, tutor,
Ponder) reorders the library **and the recorded line then plays a card the new order no
longer supplies.**

## The rule (refined)

A reference is **usable** iff **it never *plays* a card that was drawn (randomly) after a
shuffle.** Three buckets:

- **safe** — no reshuffle at all, or a reshuffle with no subsequent random draw. Deal is
  byte-identical; use as-is.
- **recreatable** — reshuffled *and* drew, but the payoff was a **tutored/fetched (chosen)
  card**, not a random draw, so no post-shuffle *random* draw was played. The decision line
  is unchanged; only the recorded *deal* is stale → **regenerate, no re-play.**
- **dead** — the line cast a card that came off the top *after* a shuffle (e.g. a fetch on
  T1, then cast the drawn Birds of Paradise on T2). Under the new order that card isn't
  there → the recorded line cannot occur → must be re-played by hand, or deleted.

This refines the earlier `references/stable_shuffle_recoverability.json`, which was a safe
*over-approximation* (it marked **every** ref that draws after a reshuffle as "affected",
lumping recreatable in with dead — 26 of 30). It also predated `b3f0bd5`.

## Method: semantic replay (not index replay)

The refs were hand-played in early July; ~100+ `src/` commits since then changed the *plan
enumeration* (Magma modeling, situational-rank, value model, mana-prune, escalation, …). So
replaying the **recorded plan indices** (`test/viewer_protocol_check.py`) reports drift for
reasons unrelated to shuffle — 29/30 "drift", useless for isolating the shuffle.

Instead, drive `--claude-play` and at each decision pick the plan whose **`land` + `casts`
matches the recorded choice** (semantic, index-independent). A recorded *cast* that the
current engine no longer offers ⇒ the card it needed is no longer drawn ⇒ the line played a
post-shuffle draw ⇒ **dead**.

**Control that validates it:** the 4 fully-safe refs (deal byte-identical) produced **zero**
missing-play divergences — so a missing-play verdict is genuinely shuffle-caused, not
enumeration noise. Two "affected" refs (s24, s30) replayed clean because their payoff was a
**tutored** Aria of Flame / an Enlightened-Tutor line, not a random draw.

## Verdict

**Kept (6):**

| Ref | Bucket | Note |
|-----|--------|------|
| `claude_s1_gi0`, `claude_s5_gi4`, `claude_s16_gi15`, `claude_s26_gi25` | safe | deal identical, as-is |
| `claude_s24_gi23`, `claude_s30_gi29` | recreatable | **regenerated** 2026-07-22 (see below) |

**Deleted (24):** `claude_s{2,3,4,6,7,8,9,10,11,12,13,14,15,18,19,20,21,22,23,25,27,28,29,31}`
(gi `{1,2,3,5,6,7,8,9,10,11,12,13,14,17,18,19,20,21,22,24,26,27,28,30}`). Each casts a
post-shuffle random draw; can't be reproduced without re-playing. Deleted (recoverable from
git history) so they stop confounding "engine-vs-human on the same game" comparisons
(`scripts/ref_bench.py`, `scripts/nc_tempo_sweep.py`).

## How s24/s30 were regenerated (no re-play)

Semantic-replay produced the choice stream that reproduces the recorded land+casts under the
current engine, then the engine wrote a schema-faithful trace via `--log-dir`:

```
mtg decks/Anti-Lifegain/Anti-Lifegain.cod --claude-play --seed <S> --game-index <GI> \
    --max-turns 8 --depth 0 --profile decks/Anti-Lifegain/Anti-Lifegain.profile.json \
    --force-mulligan "<count>:<bottom>" --choices "<semantic indices>" --log-dir <dir>
```

Verified before overwrite: same `won`/`win_turn` (T5), same `mulligan`, and **byte-identical
semantic line** (same land+casts every turn) — only the underlying deal changed.

## Side-finding (orthogonal, not fixed here)

`test/viewer_protocol_check.py`'s `DECKS` map still points at the **pre-folder-move flat
paths** (`decks/Anti-Lifegain.cod`, etc.) — all six are gone after the per-deck folder move,
so that check would contract-fail on *every* deck regardless of shuffle. Fix the map to the
`decks/<name>/<name>.*` layout before relying on it to catch reference drift.
