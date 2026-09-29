# WhiteKnights lost its perfect reference match — 2 of 10 games now trail the human

**Status: OPEN, unattributed, and NOT yet measured at HEAD.** Filed 2026-09-29. Found incidentally:
`test/ref_bench.json` was sitting modified in the working tree during unrelated work on the Fungus
mulligan run, and the modification records a regression nobody had reported.

## The finding

`decks/whiteknights` in the `ref_bench` cache, old value vs the working-tree measurement:

| field | was | now |
|---|---|---|
| `n` | 10 | 10 |
| `human` | 4.4 | **4.4** |
| `search` | 4.4 | **4.6** |
| `short` | 0 | **2** |
| `shortfalls` | `[]` | `claude_s3_gi2.json`, `claude_s5_gi4.json` |
| `hand_mismatch` | 0 | 0 |
| `src` | `565f5a73…` | `b4d6a2fe…` (but see the mis-stamp below) |

Read it carefully, because every confounder is ruled out by the row next to it:

- **The human bar did not move.** `human` is 4.4 both times, so this is not a consequence of
  `970ebe36` ("user re-played the 3 games the search beat — all now win T4"). The references are the
  same and their turns are the same.
- **The game set did not change.** `n` is 10 both times.
- **The opening hands still reproduce.** `hand_mismatch` 0, so this is not a decklist drift
  invalidating the saved hands (the `deck-revision-silently-repoints-a-viewer-check` failure mode).
- **So `search` 4.4 → 4.6 over 10 games is exactly 2 games one turn slower**, and those are precisely
  the 2 that now show as shortfalls.

**WhiteKnights previously matched the human line on all ten references** — `search` 4.4 *equalled*
`human` 4.4 at `short: 0`. That is the strongest reference result any deck in this repo has, and it is
now gone. A `SHORTFALL` is `ref_bench.py:294`: the search's win turn is later than the human's on the
same forced opening hand.

## What it is NOT

**It is not the hand-entry adoption**, and this is the trap worth recording. The obvious suspect is
`6b9f0cb2` ("`MTG_BP_HAND_ENTRY` + `MTG_BP_NODE_S10` default ON", 2026-09-27 12:01), but the binary
that produced this measurement is `build/Release/mtg` dated **2026-09-27 10:07** — built roughly two
hours *before* that commit landed. The measurement cannot contain those flags.

Note also that `hand-entry-adoption-deck-followups.md` lists whiteknights as **"faster" (0 slower, 2
faster, net −3 turns)** and therefore as a deck owing no Monday verdict. That verdict came from the
**overnight regression tier**; the **references** say the opposite. Two yardsticks, two answers — the
suite measures seeded games against GT, the references measure against a human who played the deck.
A deck can improve on one and regress on the other, so "faster in the tier" does not discharge a
reference shortfall.

## The mis-stamp, and why the cache entry must not be committed as it stands

`ref_bench.py` stamps each deck with `git rev-parse HEAD:src` (`ref_bench.py:211`) but **measures with
whatever binary is on disk, and nothing checks that the two agree.** Here they did not: the entry
claims `b4d6a2fe…` (HEAD's src, i.e. including `6b9f0cb2`) while the binary predates it. Consequences:

- `--stale-only` — "the routine call" — would then consider whiteknights **fresh** and skip it, so the
  wrong number would persist indefinitely.
- The doc for `--json` says *"the play viewer reads this to decide whether a deck's play is GREEN on
  its references"*, so the viewer would show a verdict derived from a binary that no commit describes.

This is the `gate-must-judge-the-consumed-object` failure: the stamp describes the *tree*, but the
thing consumed is the *binary*. **Do not commit this entry.** Rebuild at HEAD and re-bench, so the
stamp and the measurement refer to the same engine.

**Suggested hardening (not done here):** have `ref_bench.py` refuse to write, or write a
`binary_mtime`/`binary_md5` alongside the `src` stamp, when the binary is older than the newest file
under `src/`. A cache whose key can disagree with what it measured is worse than no cache, for the
same reason a rate-limited audit that prints "all match" is worse than no audit
(`card-costs-audit-adventure-and-false-green.md`).

## What to do next

1. **`./build.sh`, then `python3 scripts/ref_bench.py --deck whiteknights --json test/ref_bench.json`.**
   That is ~10 single games; use a low `--threads` if a generation is running. This answers whether
   HEAD regresses, and whether the hand-entry flags help or hurt on top.
2. If it persists, bisect the interval. It is wide — `565f5a73…` is **not in the last 120 commits** of
   `phase-1-2-deck-analyzer`, so the window spans the Gideon/Basri card additions and EXERT, the
   breakpoint site-10 and hand-entry-window fixes (`25628ecc`, `560e08cf`, `c84ac41f`), and the
   60-commit rebase plus GT rebaseline `a21b790f`. `ref_bench --deck whiteknights` is cheap enough to
   bisect directly.
3. Isolate the arming rule with `MTG_BP_HAND_ENTRY=0` / `MTG_BP_NODE_S10=0` on the HEAD binary —
   a diagnostic only, never a shipped per-deck exemption (that doc's own rule).
4. Explain the two games: `scripts/wk_ref_line.py` and `test/explain_game.py` on
   `claude_s3_gi2.json` / `claude_s5_gi4.json`. The question is whether the human's T4 line is still
   **inside the search's menu** (a valuation regression) or has fallen **out of it** (a pruning
   regression). Those need different fixes, and `MTG_UNPRUNED=1` separates them
   (`checkline-verdicts-are-the-viewers-enumeration`).

Related: `reference-win-turn-invariant`, `references-must-be-matched-by-the-search`,
`hand-entry-adoption-deck-followups.md`, `analysis-WhiteKnights.md`.
