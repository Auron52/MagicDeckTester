# `references/suboptimal/` — known-slow saved games (aspirational targets)

This subtree mirrors the deck layout of the verified set
(`references/suboptimal/<deck>/claude_s<seed>_gi<gi>.json`) but holds games whose recorded win is
**suboptimal**: you believe the same game is winnable **earlier** than the turn the run ended on, so
the saved line is a *target* documenting that the engine (or a played line) is leaving turns on the
table — not a verified benchmark.

## How these games are checked (USER 2026-10-22)

*"Suboptimal games could be replayed as well with the expectation that we strictly beat them."* Every
reference game bounds the engine, so these are checked in the regression tier like the verified set,
with a stricter expectation:

* **The recorded line is replayed** by `test/viewer_protocol_check.py` (the same `--strict` sweep as
  `references/<deck>/`). A replay that reaches an EARLIER win than the recorded one is reported as
  `beaten` and passes; a later win or a loss is `play-drift` and fails.
* **The search must strictly beat it**: `scripts/ref_bench.py --suboptimal --require-beat` plays each
  game from its shuffle at the deck's committed play setting and fails unless the search wins earlier
  than the recorded line (or wins where the line did not).
* **Superseded games are skipped by both**: a suboptimal game with a proper reference for the same game
  (`references/<deck>/` holding the same `claude_s<seed>_gi<gi>.json`) is already bounded at least as
  tightly by that reference (USER: *"it is arguable whether we need to do this for games that we have a
  proper reference for"*). Both tools print the superseded games rather than dropping them silently.

They stay out of the verified set because a verified reference is a line the engine must MATCH (the
viewer's maturity badges and the reference bench read `references/<deck>/`), while a suboptimal one is a
line it must BEAT.

An archived list's suboptimal games live one level deeper, mirroring the deck layout:
`references/suboptimal/<deck>/v<N>-<slug>/`.

## Workflow

1. While playing, if you reach a win but think a **faster** line exists, save the game here.
2. Once the underlying issue is fixed (or you conclude the recorded turn *is* optimal), either
   re-save the improved line, or **promote** the game to `references/<deck>/` as a normal verified
   reference (if the current turn turns out to be the best after all).

There is intentionally no `optimal/` folder — the verified set already lives directly under
`references/<deck>/`, so "optimal" is just the normal reference location. (Add an `optimal/` mirror
later if symmetry is ever wanted; nothing depends on its absence.)

