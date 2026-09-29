# Introduced-card activity probe — detecting a screen arm that is measuring a blank

**Status: DESIGNED, NOT BUILT.** Deferred 2026-09-29, the night the WhiteKnights Vial defect was
found and fixed. The engine-level cause of that defect is closed for good (`c4da77cd`); this
document is about the *methodology* layer that failed to notice, and would fail to notice the next
instance of a different cause.

## The gap

`scripts/deck_compare.py --preflight` already refuses a screen that introduces a card the engine
does not implement, a card with oracle-text coverage gaps, or a card with no `card_scores` entry.
All three ask: **is this card known?** None asks the question that actually matters:

> **Once the games have run, did the introduced card ever do anything?**

For three rounds of the WhiteKnights campaign the answer was *no*, and nothing said so. Every arm
holding an Aether Vial ran it as a 1-mana artifact that entered the battlefield, never gained a
charge counter, and never deployed a creature. The screen dutifully reported that the card cost
about +0.02 win turns. It did — as a blank. The full account is in
`docs/design/knights-copy-count-screen.md`.

**Why none of the existing guards saw it.** The preflight's own error text already cites the
`slivers_vial` version of this incident, and it still missed this one: WhiteKnights *had* a profile,
it simply did not carry the field. The floor bracket did not see it either — an arm's own R=40 table
fits itself perfectly well to a deck that is effectively playing 59 cards, so the bias reads as
"unresolved", not "broken". The identical-play fraction cannot see it, because a blank card still
occupies a slot and so still re-permutes every draw. **A card doing nothing looks exactly like a
card doing very little**, which is precisely the hypothesis these screens exist to test.

## What to build

A post-batch (or small pre-batch) probe that, for each **introduced** card, plays a short trace run
of the arm carrying the most copies and reports how often the card was actually *used*.

`--game-trace-dir` writes the real `GameLogger` JSON per game (`src/runner/BatchRunner.h`). Note
that `--game-log-dir` is **not** this — under `--batch` it writes per-game `.wins` outcome records,
and grepping those for a card name matches nothing and reads as a clean negative. That mistake has
already cost one false finding.

Each trace is `turns[].actions[]` with `type` and `cardName`, plus `boardAfter.battlefield`. Report,
per introduced card, across ~40 games:

| signal | reads |
|---|---|
| drawn / in opening hand | the card reaches the hand at all |
| `CAST` / `PLAY` | it gets deployed |
| appears in `boardAfter.battlefield` | it resolves and stays |
| **any subsequent action naming it** | **it does something after entering** |

The last row is the one that matters, and it is the one no current check approximates.

## The hard part, stated honestly

**"Did it do anything" is not generic, and pretending otherwise is how this gets built wrong.**

A cast-count check alone would NOT have caught the Vial: the Vial was cast, in most games. It
entered, sat there, and never charged. So the probe must detect an **inert permanent** — one that
enters and whose state never changes afterwards — and that needs the trace to expose per-permanent
state (counters especially; `boardAfter.battlefield` currently carries `card`, `cardName`,
`isLand`, `tapped`). Extending the trace to carry `charge_counters` / `+1/+1` counters is probably a
prerequisite, and should be checked before anything else is written.

Two shapes worth separating:

1. **Never deployed** — the card is drawn but never cast. Generic, easy, catches cost/colour bugs.
2. **Deployed but inert** — it resolves and then nothing about it ever changes. This is the Vial
   shape, and the valuable one. It needs per-permanent state in the trace, and it will have false
   positives that must be allowed for by name (a Sol Ring taps for mana without changing its own
   recorded state; an anthem lord does its whole job by existing and never acts again).

That false-positive list is the reason this is a design note and not a patch. A probe that cries
wolf on every lord in the repo will be switched off within a week, and then the next inert card
ships exactly as this one did.

## Where it should live

In `deck_compare.py`, as a **report, not a refusal**, printed in the same block as the floor
bracket. It should name the arm and the card and state the counts plainly:

```
introduced-card activity (40 games, arm w_av3_vk1):
  Aether Vial      cast 37/40   entered 37   state changed after entering  0/37   <-- INERT?
```

A refusal is wrong here: some cards genuinely are near-inert in a goldfish and the campaign knows
it (Swords to Plowshares and Unexpectedly Absent are deliberately never screened for exactly that
reason). The point is not to block the run; it is to make **"this card did nothing"** impossible to
confuse with **"this card was not worth much"** — which is the confusion that cost three rounds.

## Related

* `docs/design/knights-copy-count-screen.md` — the defect, the fix, and what it invalidated.
* `docs/design/deck-combination-screening.md` — the slivers instance (a 2.6x error) and Rule 0 on
  shared apparatus.
* `docs/design/analysis-Pirates.md` — the Pirates instance.
* `.claude/skills/deck-screening.md` — where the preflight's contract is documented.
