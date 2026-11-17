# Creature Giving "job-mix / CardParams-layout" nondeterminism: NOT an engine defect

Investigation 2026-11-17, worktree `.claude/worktrees/cg-jobmix` (base `8cd0bbf23`). Scratch and
binaries are in `build/scratch/cg-jobmix/` (build volume). The integrator's original runs are in
`build/scratch/sea/i1/bi/`.

## What was reported (Sea Creatures I1 integration, 2026-11-16)

1. With ONE unchanged binary, Creature Giving (CG) at d3 b10 (40 games, seed 7710001) gave different
   results depending on which other jobs shared the pooled `mtg --batch` run.
2. Adding ONE unused `bool` to `CardParams` changed CG's per-game `units_total` and the play digest of
   1/40 games (gi21: T2 Misty Rainforest fetches Breeding Pool instead of Overgrown Tomb), at b10 and at
   b0. No other deck moved.

## Verdict

**Neither effect is real once the data is held fixed.** Every arm in the original comparison ran an
engine binary against the sea-creatures worktree's LIVE `src/cards/data/cards.json`, which was edited
between runs. The only thing that moves CG is **Misty Rainforest's cards.json parameters** in
combination with **whether the binary knows the key `fetch_hold_human_only`**:

* Sea Creatures I1 (`4cf605b4f`) gave Misty `land_search_count` / `land_search_activation_*` (a real
  battlefield fetchland, the Windswept Heath shape) plus `fetch_hold_human_only: true`. That last key
  keeps AUTONOMOUS play on the legacy immediate route (`PerformFetch` at the land drop).
* `CardDatabase` reads params with `params.value(key, default)`, so a binary built BEFORE that key
  existed (`8cd0bbf23`, `4eb5a3e10`, `4eb5a3e10`+bool) **silently ignores it**. Given the new cards.json,
  such a binary plays Misty as a real battlefield fetchland in autonomous play too. That is a genuine
  play change, not nondeterminism. It moves CG (units in ~7 games, play in gi21) and leaves Five Colour,
  the only other Misty deck, identical.

## Evidence (all solo CG d3 b10 40 g, seed 7710001, run from an extract of `4eb5a3e10`'s `src/cards/data`
+ `decks`, `--cards-json` pinned)

| binary | Misty v0 (no battlefield params) | Misty v1 (I1 params + `fetch_hold_human_only`) |
|---|---|---|
| I1-base (`8cd0bbf23`-era) | `16009bd3` / 338192 u | `d7c4b48b` / 334395 u |
| I1a (`4eb5a3e10`) | `16009bd3` / 338192 u | `d7c4b48b` / 334395 u |
| I1ctl (`4eb5a3e10` + unused bool) | `16009bd3` / 338192 u | `d7c4b48b` / 334395 u |
| I1b (`4cf605b4f`, knows `fetch_hold_human_only`) | `16009bd3` / 338192 u | `16009bd3` / 338192 u |

* **The bool is inert:** I1ctl matches I1a in both data states, digest for digest.
* **The Misty change is inert where it should be:** I1b gives v0 == v1.
* **Job mix is inert:** with data pinned, each binary gives the same CG result solo, CG+Mill, Mill+CG,
  CG first/last of 10 other decks, the integrator's `manifest.json` (10 decks) and `manifest_b.json`
  (+ Sea Creatures). I1a gives `d7c4b48b` in all of them and I1b gives `16009bd3` in all of them. The original
  "job-mix" pair was `I1a.out` (00:15, cards.json still v0, `16009bd3`) against `I1a.b.out` (00:26,
  cards.json already v1, `d7c4b48b`). Different data, not different job mix.
* The current tree (`8cd0bbf23` Release) is also invariant under: job mix (solo, CG+Mill, CG + 10
  decks either order), `--threads 1` / `3` / 24 (a deliberate thread-invariance check), heap-content
  perturbation (`MALLOC_PERTURB_=165`, `=90`), heap-layout perturbation (`tcache_count=0`,
  `MALLOC_TOP_PAD_`, and an `LD_PRELOAD` shim shifting every later heap address by 24..8184 bytes),
  and an unused `bool` placed so that `sizeof(CardParams)` grows 11512 -> 11520. All give
  `16009bd3` / 338192.
* valgrind memcheck on CG gi21 at d3 b0 (relwithdebinfo, 1 thread): see "valgrind" below.

## The real hazard, and the rule that avoids it

**A binary from commit X silently ignores any cards.json key added after X.** So an A/B of binaries
built from different commits, run from one checkout's cards.json, does not compare the commits. It
compares "new data on a binary that half-understands it" against the real thing. That can look exactly
like a layout or job-mix sensitivity, because the result follows whichever binary/data pair happened to
run.

* When A/B-ing binaries from different commits, give EACH arm its own commit's data:
  `git show <commit>:src/cards/data/cards.json > <scratch>/cards_<commit>.json` and pass
  `--cards-json <that file>` (batch mode accepts it), or run each arm from its own extracted tree.
* Before attributing a move to "layout" or "job mix", re-run both arms with the data pinned. Here the
  matrix above took about two minutes.
* A possible engine-side guard (NOT implemented; no USER ruling): have `CardDatabase` warn on parameter
  keys it does not read. There is no central key registry today (every reader is a `params.value`
  call), so that needs a registry first.

## valgrind

`valgrind --tool=memcheck --track-origins=yes` on `./build.sh relwithdebinfo` (`8cd0bbf23`), CG gi21
alone (`skip_games` = every other index) at d3 b0, `--threads 1`: **0 errors from 0 contexts**. The game
plays 6165 units, the same as the Release binary, so the -O2 build plays it identically.

## Suite impact

None. No engine change was made, so no tier's GT moves.
