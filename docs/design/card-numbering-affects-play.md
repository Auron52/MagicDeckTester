# Card numbering affects gameplay (reproducibility gap)

Self-contained. Noted 2026-07-02 during the exalted-attack investigation; **not being worked on**
(user: "note it, move on"). Parked here so it's available to anyone.

## Symptom

For the **same deck + seed**, the goldfish result differs depending on **whether card numbering is
assigned**:

- `mtg --batch <manifest> --game-log-dir DIR` (the regression harness path) and
- `mtg deck --games 1 --seed S --log-dir DIR` (single-game, full JSON logging)

give **different win turns** for the same effective game. Concretely, Anti-Lifegain d5:
batch `s4004` game 433 = **win turn 5**; the single run `--seed 4437` (= 4004+433) `--log-dir`
= **win turn 6**. Games 0 and 1 matched (5, 3) between the two paths, so it is not a blanket setup
difference — it's numbering-sensitive tie-breaking that only bites some games.

Root cause is almost certainly that `GoldFishRunner::AssignCardNumbers` runs on **different code
paths** (single `--log-dir` numbers cards; the batch `--game-log-dir` path may number differently or
not at all — see the three `AssignCardNumbers`/`BuildCardNumbering` call sites in `src/main.cpp`),
and a **card number leaks into a gameplay tie-break** (e.g. sorting identical-name copies, or a hash
that feeds a decision). Numbering is supposed to be a **logging identity only** — it must not change
which line the AI plays.

## Why it matters

- **Reproducibility:** you cannot reliably reproduce a specific batch/regression game as a single
  logged run for debugging — the numbering flips the line. This blocked single-game tracing of the
  exalted-attack pump regressions (had to A/B via the batch mechanism instead).
- **Determinism hygiene:** identical `(deck, seed, depth, budget)` should give one game, period.
  A numbering-dependent result means some decision is reading an identity field it shouldn't.

## A within-run A/B is still valid

Two single `--log-dir` runs that differ only in a flag (both number cards the same way) remain a
correct A/B of that flag — the numbering is held constant across the two arms. It's only the
**cross-path** (batch vs single) absolute results that diverge. So flag comparisons are fine; only
"reproduce batch game N as a standalone logged game" is broken.

## Fix sketch (when picked up)

1. Grep every read of `m_number` / card-number in the **AI/decision** code (`src/ai/`, the tap and
   attack ordering, any `std::sort` comparator or hash over cards) — numbers must appear only in
   logging (`GameLogger`) and claude-play emission, never in a decision.
2. Make numbering assignment **unconditional and identical** across all runner paths (assign in
   `SetupGame`/once per game regardless of logging), so it can never be the swing variable. The
   claude-play fix already did this for its path (`AssignCardNumbers` in `RunClaudePlay`); the batch
   and goldfish paths should match.
3. Guard with a test: same `(deck, seed, depth, budget)` → identical win turn with logging on vs off.
