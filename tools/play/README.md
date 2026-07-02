# Play GUI — human-played reference games

A browser GUI for **playing a simulated game by hand** and **probing the engine's model**.
You assemble a main-phase line atomically — **double-click or drag** a hand card into the
**Casting this phase** zone (single-click is reserved for ability activation), drag it back or
✕ to undo — then **Commit phase**. The engine reconciles your whole line against what its model
would do and returns one of:

- **accept** — the line matches a plan the model plays → the game advances.
- **choose** — the line resolves several ways (tutor target / X value / Ponder keep-vs-shuffle /
  Soulfire count); you pick the variant. Human-play mode runs **unrestricted** (`MTG_UNPRUNED` +
  `MTG_PONDER_SEARCH`), so the search offers every legal sub-decision, not the heuristic's pick.
- **legal · not enumerated** — your line is *rules-legal* (a same-turn-ramp-aware affordability
  simulation can execute it) but the search never enumerated it → a real enumeration gap.
- **illegal** — a cast is unaffordable or a land can't be played (with the offending action).
- **unsupported** — an action kind v1 can't yet validate (X spells, alt-cost, tutors).

No clairvoyance: play is deliberately run **without** `--reveal`, so your win-turn is an honest
no-foresight ground-truth bound a good AI should be able to match. On any reject you can **Store
as artifact** (`logs/play/rejections/`). A **clean** completed game (no rejects) saves to the
**tracked** `references/<deck>/` set; a game that had rejects saves only to gitignored
`logs/play/`. After each action a **what-changed** strip shows the resolved board delta (cards to
the battlefield/graveyard, draws, opponent life). Everything is deterministic and exactly
reproducible from `(deck, seed, game #, choices)`.

> The canonical example: on a hand with Mountain + Sol Ring + Ornithopter of Paradise, the line
> `Mountain → Sol Ring → Ornithopter` is **legal · not enumerated** — the enumerator's mana model
> doesn't credit a rock cast *this* turn toward a later same-turn cast (see `BuildPool`). The
> artifact points straight at that gap.

## Run

```bash
cmake --build build --config Release      # ensure ./build/Release/mtg exists
node tools/play/server.js                 # then open http://localhost:8080
```

No dependencies — `server.js` uses only Node built-ins. Env: `PORT` (default 8080),
`MTG_BIN` (default `./build/Release/mtg`).

Pick a profiled deck, set seed / game # / max-turns, hit **New game**. Each main phase:
double-click or drag hand cards into **Casting this phase** to build a land drop + casts, then
**Commit phase** (empty = **Pass**). On accept the board advances; on **choose** you pick the
sub-decision variant; on reject you get the classified verdict, the lines the model *would*
play, and **Store as artifact**. **Undo** steps back (free — replayed from choices). Aether Vial
upkeep charges are answered with Add/Hold. At game end, **Save as reference** writes the
deterministic per-game trace — to `references/<deck>/` for a clean game, else `logs/play/`. On a
clean **win** a second button, **Save as suboptimal (should be faster)**, writes the same trace to
`references/suboptimal/<deck>/` instead — use it when you won but believe an earlier win exists, so
the game is kept as a target without polluting the verified set (see
`references/suboptimal/README.md`). The top-bar badge reports whether a verified (`✓`) or suboptimal
(`⚠`) save already exists for the selected game. The board uses Scryfall card art (hover any card for
the full image).

## How it works (and the architecture seam)

The simulator already exposes a clean injection point: `AIEngine::SetExternalChooser`. With
`--claude-play`, a game is fully determined by `(deck, seed, game-index, choices-CSV)`. Each
binary invocation replays the deterministic game applying the prior choices and emits the
**next single decision** as JSON between `<<<CLAUDE_DECISION>>>`/`<<<END_DECISION>>>` markers
(exit 70), or the final `<<<CLAUDE_RESULT>>>` (exit 0). See `src/main.cpp:RunClaudePlay` and
`WriteDecisionJson`.

```
 browser GUI  ── decision JSON / choice index ──►  transport  ──►  engine
 (index.html)        (the stable contract)         (server.js)     (mtg --claude-play)
```

**The browser↔engine contract is the decision-JSON protocol — and that is the same contract
a future WebAssembly build would expose** (`decide(deck, seed, gi, choices) -> json`).
Today the transport is this thin Node bridge that shells out to the native binary (works now,
no extra toolchain). To move to WASM later, compile the core engine with Emscripten exposing
`decide(...)`, and have `index.html` call it directly instead of `fetch('/api/step')` — **the
UI does not change.** That is why the GUI is built against the protocol, not the subprocess.

`server.js` routes (binary always runs at `--depth 0`, **no `--reveal`**):
- `GET /api/decks` — profiled decks under `decks/`.
- `POST /api/step` — `{deck, seed, gameIndex, maxTurns, choices[]}` → next decision or result.
- `POST /api/validate` — same body + `line` (encoded `"land=X;cast=Y;..."` or `"pass"`); runs
  `--validate-line` to reconcile the hand-assembled line at the current main phase →
  `{verdict, plan_index, matched_summary, reason, failed_action, variants[], decision}`
  (`<<<CLAUDE_VALIDATION>>>`, exit 71). On **accept** the GUI appends `plan_index` to `choices`
  and steps; on **choose** it lists `variants` (each a `plan_index` + label) for the human to
  pick (the pick is just that index); otherwise it shows the classified reject.
- `POST /api/reject-artifact` — persists a rejected line to `logs/play/rejections/<deck>_s<seed>_gi<gi>_t<turn>.json`.
- `POST /api/save-reference` — clean game → tracked `references/<deck>/claude_s<seed>_gi<gi>.json`;
  with `suboptimal:true` → `references/suboptimal/<deck>/…` (a "should-be-faster" target).
- `GET /api/reference-exists` — reports `{exists, path, suboptimal, suboptimalPath}` for a game.
- `POST /api/save` — re-runs with `--log-dir logs/play` (used for a game that had rejects).

### The line-reconciliation seam (engine)

`--validate-line "<spec>"` (with the same `--choices` prefix) replays to the first un-chosen
main phase and calls `TurnSolver::CheckLine(state, is_pre, spec)`, which (1) matches the line
against `EnumerateMainPlans` (→ accept + the plan index for the stateless replay), else (2) runs
an affordability simulation **independent of the enumerator's `BuildPool`** — it credits mana
from a rock cast *this* turn — to distinguish *legal-but-not-enumerated* from *illegal*. Only
lines that match an enumerated plan advance the game (so the `--choices` index stream stays
intact); rejected lines are classified and saved, not played. v1 validates land + plain hand
casts; X / alt-cost / tutor casts report **unsupported** rather than guess.

When several enumerated plans share the same land + cast names but differ in a per-spell
sub-decision (tutor target / X / Ponder keep-or-shuffle / Soulfire own-target count), `CheckLine`
returns them all as **variants** (deduped by an order-independent sub-decision signature, so pure
cast-ORDER duplicates collapse). One variant → accept; several → **choose**. Because human-play
mode forces `MTG_UNPRUNED` + `MTG_PONDER_SEARCH` (set in `main.cpp`'s `--claude-play` branch),
the search enumerates every legal sub-decision rather than the heuristic-narrowed one — so the
human is choosing from the full set. (Plan indices are stable because the mode always enumerates
unpruned, keeping the `--choices` replay valid.)

## Scope today, and what's next

Per the agreed "start small, build toward the full game" plan, v1 covers what the existing
external-chooser surface exposes:

- **Now:** every **main-phase** decision, assembled **atomically** (double-click/drag land +
  casts), reconciled at the phase breakpoint (accept / **choose** the sub-decision variant /
  legal-not-enumerated / illegal / unsupported, with artifacts), plus Aether Vial charges. No
  clairvoyance; clean games saved to tracked `references/`; a best-effort "what changed" strip
  after each action. Scryfall board reusing `tools/replay/` idioms. Combat, blocks,
  mulligan/bottoming stay on the engine heuristics.
- **Next:**
  - **Ability activation by single-click** — cycle/dig and Land's Edge discard as committable
    line actions (new `LineSpec` kinds in `ParseLineSpec` + `CheckLine` matching/affordability).
    Today single-click just hints; cast/play is double-click/drag.
  - **Higher-fidelity resolved effects** — the "what changed" strip is a client-side state diff,
    so it can't show *which* card Gamble discarded or Soulfire's per-target flips. Faithful detail
    needs the `claude-play` apply path to emit the `tools/replay/` action log (Gamble discard /
    Soulfire reveals / targets) and the GUI to render that step.
  - Route **mulligan/keep/bottom** and **combat (attackers/blockers)** through the chooser too,
    for a fully human-controlled game.
- **v1 line-check limits:** validates land + plain casts; **X / alt-cost / tutor** casts report
  *unsupported*. The affordability sim models same-turn rock ramp + colour availability but uses the
  enumerator's over-approximate multi-colour "wild" mana, so a rare colour-contention line could
  read *legal* when the real payment can't make it — caught on artifact review. Reconciliation is
  **end-of-main-phase** (not end-of-turn).
- **Later:** swap the transport to in-browser WASM (seam above).

## Notes

- Local single-user dev tool: binds `127.0.0.1`, shells out to a local binary — do not expose.
- **`depth` is fixed at 0**, and must be: at depth > 0 the engine turns on lookahead-bottoming and
  its mulligan/lookahead rollouts replay whole games through the *same* external chooser, so the
  human would be asked to play hypothetical rollout games instead of one real game. Depth 0 means
  the human drives every real main phase exactly once; combat/blocks/mulligan use engine
  heuristics. Results are work-budget deterministic, independent of CPU/threads.
