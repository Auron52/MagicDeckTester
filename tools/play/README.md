# Play GUI — human-played reference games

A browser GUI for **playing a simulated game by hand** and **probing the engine's model**.
You assemble a main-phase line — **click, double-click or drag** a hand card into the
**Casting this phase** zone, drag it back or ✕ to undo — then **Commit phase**. On a hand card
every gesture on the card BODY means the same thing, *play this card*; each of the other ways to
play it (**↻ cycle**, **⚡ channel**, **⌛ suspend**, **▣ land** for an MDFC's land face) has its
own badge, and only that badge does it. (On the BATTLEFIELD a single click still means "activate
this permanent's ability" — there is nothing else a click there could mean.) **Committing does not end the phase**: the engine applies the
line, re-enumerates from the resulting board and asks again, for as long as there is anything left
to do. Only **Pass** ends the main phase. That is what makes a resource you generate mid-phase
usable by that phase — tap Krenko for tokens *then* sacrifice them to Skirk Prospector, Vial in a
Mogg War Marshal *then* eat it and its token for the mana to cast Goblin King, Crop Rotation a land
into play *then* cast off it. None of those fit in one atomic line, because the board they need does
not exist while the line is being assembled. (`MTG_PLAY_SEGMENT_ALWAYS=0` restores the old
end-the-phase-on-commit behaviour.)

Each committed segment is reconciled against what the model would do and returns one of:

- **accept** — the line matches a plan the model plays → the game advances.
- **choose** — the line resolves several ways (tutor target / X value / Ponder keep-vs-shuffle /
  Soulfire count); you pick the variant. Human-play mode runs **unrestricted** (`MTG_UNPRUNED` +
  `MTG_PONDER_SEARCH`), so the search offers every legal sub-decision, not the heuristic's pick.
- **legal · not enumerated** — your line is *rules-legal* (a same-turn-ramp-aware affordability
  simulation can execute it) but the search never enumerated it → a real enumeration gap.
- **illegal** — a cast is unaffordable or a land can't be played (with the offending action).
- **unsupported** — an action kind v1 can't yet validate (X spells, alt-cost, tutors).

### Tapping mana yourself (the ⛏ Tap mana fallback)

The engine allocates your mana. When it allocates it **badly** — the classic being a dual tapped for
the wrong half, stranding the one pip a later cast needed — you can override it: press **⛏ Tap
mana** (in the Lands header, next to the floating-mana pips) and click the sources you want tapped.
A source that makes more than one kind of mana asks which face; the mana goes into your floating
pool, and the line's payments spend the float **before** they tap anything, so what you tapped is
what gets spent. Anything you *don't* tap is still the engine's to allocate — this overrides an
allocation, it does not replace the allocator.

Each forced tap is a plan chip (`⛏ tap Conservatory → {W}`) at the position you clicked it, so it
can sit *between* two other queued actions ("crack the Clue, then tap Kitchen, then blink"); ✕
removes it. After the line commits, the history names each one (`manual tap: …`) — every other
tapped permanent on that board was the engine's own choice, which is how the two stay tellable
apart. A tap that can't happen (a source already tapped, a colour it can't make) is **rejected with
a reason** rather than quietly fixed. Filters, feed-cost lands, one-shot sacrifice sources and
restricted mana are not offered: they stay with the engine's payment. Full design:
`docs/design/viewer-manual-tap-pay.md`; `MTG_HUMAN_PRE_TAP=0` removes the feature entirely.

### Grinding a loop without clicking it out (the ⟲ macros)

Some turns are the same two or three activations over and over — on EldraziDisplacerFlicker,
*activate Kitchen (investigate) → blink a Drake to untap the lands → crack the Clue to draw*, twenty
times. Three plan-bar gestures cover that, and all three are **pure queue edits**: they push
ordinary entries into the turn plan, which commit as ordinary consecutive lines, so a macro'd game
is saved as exactly the clicks a human would have made.

| gesture | what it repeats |
|---|---|
| **click an Investigate source** | queues the investigate **and** the crack of the Clue it makes (one click, no dialog — a global option under ⚙ Options; turn it off to crack the Clue yourself) |
| **⟲ Repeat ×N** | the block you have **queued but not committed** — set the loop up once, then stack it |
| **⟲ Loop last lines ×N** | the last **K lines you already committed this turn** — play the loop once by hand, then ask for twenty more |

`⟲ Loop last lines` is the one for a loop you have already discovered. It opens a central dialog
that names the actual lines it is about to repeat, asks how many of them make up one turn of the
loop (K) and how many times to run it (N), and queues K×N lines; press **Commit Line** once and they
commit back to back. Each iteration is validated on the board the previous one left — so the engine
pays every one for real, and at the first iteration it cannot, the macro **stops and says where**:
the lines already committed stay played, the rest are dropped, and the reject panel leads with
*"Loop (2 lines ×10) stopped here. 7 of 20 lines committed (3 complete iterations)"*.

Offered only when it is real: same turn only, an empty queue, and never across a land drop or a
Land's Edge discard (once-per-turn resources — the control is withheld rather than offered and then
rejected). Manual `⛏ tap` entries inside a looped line ride through verbatim, since the loop is
usually the very thing that untaps that land again. Undo is unchanged: every iteration is its own
committed line and steps back one at a time. Full design + the failure modes:
`docs/design/viewer-line-macros.md`.

### "The play server is out of date — restart it"

Your browser re-reads `index.html` from disk on every load; `node server.js` does **not** — it keeps
whatever it was started with. So after any change to `server.js`, a viewer left open is a new client
on an old server: same routes, same payloads, different behaviour underneath. The client now checks
(`serverApi` on `/api/decks`) and shows a red banner when they disagree — including when the server
is too old to report a version at all, which is the case that matters, since such a server cannot
answer the question. The bite: a server started before the per-game engine pin runs
`build/Release/mtg` **unpinned**, so anything that rebuilds the binary mid-session changes the engine
under your game *and* under the save, which re-runs the whole choice stream in a fresh process.
Restart with `./play.sh` when you see it.

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

From the repo root:

```bash
./play.sh          # Linux/macOS
play.cmd           # Windows
```

That builds the engine if needed, starts the server, and opens <http://localhost:8080>.
`--no-open` / `-NoOpen` skips the browser launch.

The two steps by hand, if you'd rather:

```bash
./build.sh                                # build.cmd on Windows -> build/Release/mtg[.exe]
node tools/play/server.js                 # then open http://localhost:8080
```

No dependencies — `server.js` uses only Node built-ins. Env: `PORT` (default 8080),
`MTG_BIN` (default: the first of `build/Release/mtg`, `build/Release/mtg.exe` that exists).

### What the deck picker's labels mean

A deck is only as trustworthy as the apparatus fitted to it, and those pieces arrive late and
independently of the decklist — so a deck can be fully implemented, pass every gate, and still be
measuring something you would not quote. The picker grades every deck:

| label | meaning |
|---|---|
| **(alpha)** | a piece of the apparatus is **missing** — its numbers may simply be wrong |
| **(beta)** | complete, but **unproven** — not yet enough references, or the search does not match the human on them |
| *(none)* | 30+ reference games, and the shipped search matches or beats the human win turn on **every one** |
| **(no profile)** | no `<stem>.profile.json`: never measured at shipped play, so it is disabled outright |

Selecting a labelled deck shows a badge naming the specific reasons (amber for beta, red for alpha).

**Alpha** — any one of these is missing:

- **Fewer than 10 optimal reference games** (`references/<Deck>/claude_s*_gi*.json`). These are the
  only human-played ground truth in the repo — the bound the AI's win turn is judged against, and
  the thing that surfaces engine bugs autonomous play cannot; every viewer bug-bash in
  `docs/design/` started as a reference. A deck with three of them has not been looked at.
  Games under `references/suboptimal/<Deck>/` do **not** count — you flagged those as winnable
  earlier, so they are targets, not standards. Neither do games played on a decklist that has since
  been **archived** (`deck_registry.REFERENCE_DECK`): replaying a recorded human line against cards
  that were never in the deck is a benchmark that means nothing.
- **No value leaf** (`<stem>.value.json`).
- **No completed mulligan profile.** *Completed* means the compiled table exists
  (`<stem>.keepmodel.exhaustive.profile.json[.gz]`, the two names `MulliganProfileIO.h` loads), not
  that generation was started — a lone `.raw.json.journal` is a paused run, not a model.

**Beta → stable** additionally needs both:

- **30 reference games.** Not a statistical threshold: every deck's references run from seed 1
  upward at roughly one game per seed, so "30" reads as *the first 30 seeds have been played*.
  Knights at 28 is not 2 short of a quota — it has two gaps.
- **Green on the bench.** The shipped search, replayed on each reference's exact opening hand
  (`--force-mulligan`, so the comparison isolates play rather than mulligan policy), must match or
  beat the human win turn on **every** reference. This is the criterion that actually matters — 30
  games nobody compared against is not evidence — and the only one that can move a deck *down*
  after it was fine.

The bench is **cached**, never run by the viewer:

```bash
python3 scripts/ref_bench.py --json test/ref_bench.json                # full run (~224 games)
python3 scripts/ref_bench.py --json test/ref_bench.json --stale-only   # the routine call
```

Each deck's entry is stamped with the `git rev-parse HEAD:src` it was measured at, so `--stale-only`
re-benches exactly the decks whose stamp no longer matches and skips the rest in under a second. A
stamp that no longer matches reads **stale**, which is not green — evidence measured against an
engine that no longer exists cannot hold up a top tier.

`node test/viewer_deck_beta_check.js` prints the current alpha/beta/stable split and gates the grading.

Pick a profiled deck, set seed / game # / max-turns, hit **New game**. First you drive the
**mulligan**: each London attempt shows the 7-card hand as art with **Keep** / **Mulligan** buttons
(tagging what the engine's `KeepHand` would do), and on a keep you click which card(s) to put on the
**bottom** — one modal per card. The bottom modal appears instantly showing *AI thinking…* and, in the
background, asks the **depth-5 search** what it would bottom (via `/api/ai-hint`, `HINT_DEPTH` env);
when that returns it tags the deep pick and ✓-marks the removals that keep the earliest win — never
blocking your own choice. Following those picks reproduces the search's exact opening hand. Then each
main phase:
click, double-click or drag hand cards into **Casting this phase** to build a land drop + casts, then
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

### What a click actually costs (the persistent child, and the two things that defeated it)

The stateless protocol re-simulates the WHOLE `--choices` prefix, so step N pays for re-enumerating
all N−1 earlier plan fans and a long combo turn gets progressively slower. `--interactive` fixes
that: the engine blocks on stdin after emitting a decision instead of exiting 70, and continues the
SAME in-process game, so a step costs only its own frame. `server.js` keeps at most one such child
(`isession`) and falls back to a stateless respawn for anything unusual (rewind/undo, a side-channel
PROMPT frame, child death, a timeout). `PLAY_INTERACTIVE=0` opts out.

Keeping the child is the whole game, and two ordinary things used to throw it away every click:

* **A cast-order pin.** The session key is every argv except `--choices`, and committing a
  hand-sequenced line records a `--cast-order` pin — so the key changed on nearly every click of a
  deck whose lines the human orders (EldraziDisplacerFlicker `claude_s12_gi11`: a pin on 31 of 60
  decisions). Pins now ride stdin as `@cast-order <spec>` and `--cast-order` is out of the key.
* **`Commit Line` validates first.** The commit flow is `/api/validate` then `/api/step`, and the
  validation was always a fresh full-prefix spawn. It is now asked of the live child as
  `@validate-line <spec>`, answered against the frame the child is parked on, consuming no pick.

Both directives are **`--interactive` only**; a stateless replay of the same prefix is unaffected,
which is what keeps every saved reference replaying byte-for-byte. Measured 2026-09-11, commit-line
flow, per game: `claude_s12_gi11` 4.5 s / 87 engine spawns → 0.58 s / 1 spawn; `claude_s9_gi8`
7.8 s / 56 spawns → 2.3 s / 1 spawn; worst single click 231 ms → 90 ms. Gated by
`test/interactive_parity_check.py` (layer 1g of `test/viewer_checks.sh`), which byte-compares every
frame AND every in-child validation against the stateless spawn. Measure with
`node test/viewer_click_latency.js <reference.json> --validate` (per-click wall time + engine spawn
count) and `python3 test/viewer_step_latency.py --ref <reference.json>` (engine CPU per prefix;
add `--env MTG_PLAY_STEP_TIMING=1` for the enumerate/combo-off/apply split).

### A saved log IS the game you played (the engine is pinned, the save is audited)

Both save routes RE-RUN the whole accumulated choice stream in a **fresh process** and publish the
result. A plan index only means something against the plan fan it was picked from, so if that fresh
process enumerates differently, every later index lands on a different plan and the file records a
game nobody played — under the right name, with no error. That happened on **2026-09-10**: the
engine was rebuilt two minutes before a save, the replay veered at decision 27 of a turn-4 go-off
(3528 plans on the session's image, 1206 on the new one), and the log came out "won on turn 7" for a
game still in turn 4. Two guards now stand between a session and its file:

* **The engine binary is pinned per game.** The first request of a `(deck, version, seed,
  game-index, max-turns)` copies `build/Release/mtg` to `logs/play/.session/` and every later spawn
  for that game — interactive child, stateless fallback, `/api/validate`, the hints, and both saves
  — runs the copy. **Rebuild whatever you like mid-session; the game in front of you does not
  change.** Starting a new game re-pins, so a rebuild is picked up immediately. `PLAY_PIN_BIN=0`
  disables it.
* **The save is verified before it is published.** The server fingerprints every decision frame it
  serves you, the replay writes to a staging dir, and the two are compared decision-for-decision
  (plus a self-contained check that no recorded pick is outside the fan the replay produced). A save
  that disagrees is **REFUSED** — nothing is written to `logs/play/` or `references/`, and the
  diverged replay is parked in `logs/play/diverged/` for triage. With the pin in place a refusal
  means the pin was not available — the server was **restarted** part-way through the game, so the
  image the earlier decisions were played on is gone. Nothing can recover that game's log; the
  refusal is there so you are told rather than handed a wrong one.

`test/viewer_save_parity_check.js` (wired into `test/viewer_checks.sh`) guards both.

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

## Regression checks

Two standalone checks replay the saved `references/<deck>/` games to guard the GUI across engine
and UI edits — they use the real played lines as exercise cases, so real bugs surface as a
reference that no longer reconstructs. Run both after touching the engine, the decision JSON, or
`index.html`/`linebuild.js`:

```bash
python3 test/viewer_protocol_check.py     # engine↔protocol contract layer
node    test/viewer_linebuild_check.js     # browser line-building layer
```

- **`viewer_protocol_check.py`** — replays each reference by **intent**: recorded picks are
  re-anchored by plan content (recorded index breaks ties between visibly-identical plans, e.g.
  MDFC faces), and decision points a reference predates are answered from the frame's own
  `heuristic_default`/`ai_choice`. Asserts the engine emits well-formed decisions and reaches a
  clean terminal (the `decide(deck,seed,gi,choices)→json` contract). Reports `repaired` /
  `play-drift` / `shuffle-dead` / `ENUM-GAP` as information; `--strict` also fails on play-drift
  and enum-gap. See docs/design/reference-intent-replay.md.
- **`test/play_drive.js`** — the same two routes (`/api/step`, `/api/validate`) driven from a script
  instead of a browser, so a line a user PASTED can be replayed headlessly and each commit timed:

  ```bash
  node test/play_drive.js --deck "Mirrorwing Dragon.cod" --seed 22 --gi 21 --dump \
       --lines "land=Forest;cast=Elvish Mystic" --lines "land=Gruul Turf;cast=Elvish Mystic" ...
  ```

  `--pre a,b,c` pins the opening mulligan/bottom picks, `--prefer <text>` answers each board-target
  prompt with the option whose label contains it, `--dump` prints the board (with tap state) at every
  frame, and every step prints its wall time — which is how "committing is slow" became the measured
  30ms → 2.1s → 21s → 31s → timeout escalation behind viewer issues #4/#8/#11.

- **`viewer_linebuild_check.js`** — drives the **actual** GUI line-building code (`linebuild.js`,
  the same module `index.html` loads) headlessly: for every main-phase decision the user played, it
  rebuilds the chosen plan's land + hand casts via `LineBuild.queueCard()` and asserts the line
  reconstructs. This catches viewer regressions the protocol check is blind to — e.g. a staged
  (exiled-but-playable, Soulfire dig / Light Up the Stage) cast that `queueCard` silently drops:
  the engine still enumerates it, so the contract check is green while the GUI can't build the line.
  That is exactly why the line-building logic lives in the shared `linebuild.js` and not inline.
  It also walks the **choose-variant picker** (`LineBuild.nextDimension`) over a two-tutor payload
  and asserts BOTH dimensions are asked with every choice offered — the shape of viewer issue #13,
  and the same blind spot: the engine emits all 144 variants, so only a client-side check sees it.

## Scope today, and what's next

Per the agreed "start small, build toward the full game" plan, v1 covers what the existing
external-chooser surface exposes:

- **Now:** every **main-phase** decision, assembled **atomically** (double-click/drag land +
  casts), reconciled at the phase breakpoint (accept / **choose** the sub-decision variant /
  legal-not-enumerated / illegal / unsupported, with artifacts), plus Aether Vial charges. No
  clairvoyance; clean games saved to tracked `references/`; a best-effort "what changed" strip
  after each action. Scryfall board reusing `tools/replay/` idioms. Combat, blocks,
  mulligan/bottoming stay on the engine heuristics.
- **Board activations — single-click a permanent already in play.** A permanent whose ability the
  model enumerates this phase carries a ⟳ badge; clicking it queues one activation, clicking again
  removes it. Covered: creature-sac outlets (Skirk Prospector / Siege-Gang / Pashalik — you pick
  which creature dies as each sacrifice resolves), Krenko's token tap, main-phase pumps, Call of the
  Wild, **every planeswalker loyalty ability**, Garth, Wirewood Lodge's untap, Balan's attach-all,
  Stoneforge's put-from-hand, and the Jitte's counter modes — **including its `+2/+2`**, which is now
  activatable in the main phase whenever the Jitte holds counters and is attached, instead of only
  inside combat (spend several at once and the choose dialog asks how many). A source offering
  *several* distinct activations opens a small picker first (which Equipment to put, which Jitte
  mode); everything decided *after* the commit — which loyalty ability, how many times a repeatable
  ability fires — is asked by the ordinary choose-variant dialog. An ability is offered only when it
  can really resolve *now*: the menu must never hold a silent no-op.
- **Equipping is a DRAG, like an Aura — from the battlefield OR straight from hand.** Grab the
  Equipment (it carries a ⚔ drag badge) and drop it on one of your creatures. The legal hosts light up
  while you drag, the queued equip renders stacked behind the creature it will attach to (and only
  there — it leaves the spot it came from, so the same card is never drawn twice on the field), and
  dragging it again moves it rather than queueing a second one. An **attached** Equipment stays
  draggable, which is how you re-host it. Dropping one from **hand** onto a creature queues the two
  operations the play needs — cast it, then attach it — as a single line, matching the combined plan
  the engine enumerates. The drop names the host by its card number, so equipping the *second* of two
  same-named creatures works and no post-commit dialog asks for it.
  See `DECISIONS.md` → *Board activations* for the wiring sites a new activation needs.
- **A card with two playable faces gets a route for each** — an MDFC whose back is a land (Turntimber
  Symbiosis // Turntimber, Serpentine Wood) shows a **▣ land** badge; click it to take the land drop
  instead of casting the front. The badge only appears when the model is actually offering that land
  play this phase.
- **Cycling is a BADGE, not the card.** A hand card the model offers a cycle of carries a **↻ cycle**
  badge; clicking *that* discards it to draw (it resolves on the spot — it is a committed plan, not a
  queued entry you can ✕ back). Clicking the card itself plays it, like every other hand card. The
  badge used to have no handler at all, so a click anywhere on a cycling land cycled it and the land
  could not be played by clicking at all (user report: FiveColour seed 3, Jetmir's Garden).
- **Next:**
  - **Land's Edge discard as a committable line action** — today it is a click MODE entered from the
    Land's Edge permanent rather than a per-card affordance.
  - **Higher-fidelity resolved effects** — the "what changed" strip is a client-side state diff,
    so it can't show *which* card Gamble discarded or Soulfire's per-target flips. Faithful detail
    needs the `claude-play` apply path to emit the `tools/replay/` action log (Gamble discard /
    Soulfire reveals / targets) and the GUI to render that step.
  - Route **mulligan/keep/bottom** and **combat (attackers/blockers)** through the chooser too,
    for a fully human-controlled game.
- **v1 line-check limits:** validates land + plain casts + tutors + every board activation
  (`sacout=` / `equip=` / `attachall=` / `sfput=` / `jittemode=`); only **{X}** casts report
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
