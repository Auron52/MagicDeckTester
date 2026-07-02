# `references/suboptimal/` — known-slow saved games (aspirational targets)

This subtree mirrors the deck layout of the verified set
(`references/suboptimal/<deck>/claude_s<seed>_gi<gi>.json`) but holds games whose recorded win is
**suboptimal**: you believe the same game is winnable **earlier** than the turn the run ended on, so
the saved line is a *target* documenting that the engine (or a played line) is leaving turns on the
table — not a verified benchmark.

## Why it's separate from `references/<deck>/`

`references/<deck>/` is the **verified** benchmark set: games you saved because the line is correct
and the win turn is the best you can find. `test/viewer_protocol_check.py` gates on that set and the
tools/play GUI badges from it. A game whose win turn is knowingly beatable would otherwise show up as
permanent "drift" and pollute the benchmark. Keeping it here means **nothing is lost** — you can save
every interesting game — while the verified set stays clean.

The one-level glob `references/*/claude_s*_gi*.json` used by the checker does **not** reach files one
level deeper here (`references/suboptimal/<deck>/…`), so these are excluded automatically.

## Workflow

1. While playing, if you reach a win but think a **faster** line exists, save the game here.
2. Once the underlying issue is fixed (or you conclude the recorded turn *is* optimal), either
   re-save the improved line, or **promote** the game to `references/<deck>/` as a normal verified
   reference (if the current turn turns out to be the best after all).

There is intentionally no `optimal/` folder — the verified set already lives directly under
`references/<deck>/`, so "optimal" is just the normal reference location. (Add an `optimal/` mirror
later if symmetry is ever wanted; nothing depends on its absence.)

*Currently empty — populate as you play.*
