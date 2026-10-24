# Game Replay Viewer

A zero-dependency, client-side viewer for the simulator's per-game JSON logs. Step through a
simulated game like a real board so you (or anyone) can flag plays that look like
card-implementation or AI-decision bugs.

## Use

1. Open `tools/replay/index.html` in a browser (no server needed — `file://` works; it's pure
   client-side, file-input + drag-and-drop).
2. Drag a game-log `.json` onto the page, or click **Open game log**.

Sample logs are in `logs/replay_samples/` (a clean turn-4 combo win, a slow turn-7 grind, and a
no-win brick — from `decks/Hinata2.cod`, seed 7000).

## What you get

- **Board playback** — a board picture per step with transport controls (⏮ ◀ ▶/⏸ ▶▏ ⏭, a
  timeline slider, and a speed selector) plus keyboard (←/→ step, Space play/pause, Home/End).
- **Board laid out like a real game** — non-land **Permanents** on top, **Lands** underneath,
  **Hand** below, with both life totals and the step's non-cast actions in the lifebar.
- **"Cast this step" zone** (right) — each spell cast at the current step as a card image with
  the mana paid.
- **Card images via Scryfall** — opening hand and every mulligan attempt render as thumbnails;
  **hover any card** anywhere to see the full card (read the real oracle text vs what the sim did).
  No API key; images are lazy-loaded and browser-cached.
- **Tapped permanents render rotated 90°** — lands tapped for mana, mana rocks/dorks whose
  ability fired, and attackers all show sideways (from the log's per-permanent `tapped` state).
- **Chosen X** on `{X}` spells — Crackle with Power / Reality Spasm show `X=N` next to the mana
  paid (and the mana now reads as the resolved cost, e.g. `{8}{R}{R}`, not a stray `{X}`).
- **Scry/dig reveals** — Ponder/Preordain/Scry etc. show the cards they looked at in the
  "Revealed" panel (right), each marked **▲ top** (kept on top) or **▼ away** (bottomed/binned).
  Ponder is reorder-or-shuffle (never bottoms); a shuffled Ponder is labelled "Ponder (shuffle)".
- **Opponent's side** — the opponent's permanents (Forbidden Orchard tokens, scheduled spawns,
  modelled opponent lands) render across the table above your board, so spell targets are visible.
- **Spell targets** — each cast shows `→ opponent` / `→ you` / `→ CardName (opp)` so you can see
  exactly what Crackle with Power (etc.) is pointed at.
- **Playback speed** — Very slow → Very fast in the transport bar (the slow end is good for
  stepping through a combo turn).

## Generating logs

Any run with `--log-dir` emits one JSON per game (deterministic by seed + game index, so a
flagged game is exactly reproducible):

```
./build/Release/mtg decks/Hinata2.cod --games 30 --seed 7000 --depth 5 \
    --budget-ms 20 --max-turns 8 --threads 0 --log-dir logs/hinata_games
```

## Log schema notes

- `boardAfter.battlefield` / `boardAfter.opponentBattlefield` are arrays of `{card, cardName,
  tapped}` objects (cardName lets the viewer label tokens, which have no deck card number).
- `CAST_SPELL` actions carry `manaPaid` (resolved cost string), `chosenX` (for `{X}` spells),
  and `targets` (`[{kind:'player'|'permanent', who:'you'|'opponent', card, cardName}]`).
- `REVEAL` actions carry `source` (the spell), `lookedAt` (`[{card, cardName}]` in look order),
  `kept` (card numbers kept on top) and `bottomed` (numbers sent to bottom / graveyard).
- `ABILITY` actions carry `card`/`cardName` (the source) and `ability` (description). Logged for
  audit; the non-tap visual is not drawn yet (mana taps are already shown via `tapped`).

## Review mode — references where the search beat your line

```
python3 scripts/ref_review.py build     # find + localise + lay out the games (logs/ref_review/)
./review.sh                             # http://localhost:8090
python3 scripts/ref_review.py status    # the same table in the terminal
```

`build` plays every saved reference in `references/` with the shipped search from your exact opening
hand, in one pooled batch, and keeps each game the search wins **earlier**. `references/suboptimal/`
is not reviewed: a game there is your note that the viewer would not let you reach the optimal turn,
so the search beating it is expected (`ref_bench.py --suboptimal --require-beat` checks that). For each one it then hands the search *your* board at the start of each of your turns:
that **hand-off ladder** shows the turn(s) on which your line lost ground (the search's win turn from
your board gets worse across them). The ladder is run a second time under the engine's
**non-clairvoyant** search (`MTG_NC_SEARCH=1`). If a blind search also gains on that turn, the hint
is `likely-misplay`; if it doesn't, `likely-clairvoyance`. It is only a hint.

The review page lists the games. For each one it shows the ladder and **your game and the search's
side by side, turn by turn**: identical turns fold away, the lost turn is highlighted, and cards one
side holds and the other does not are outlined. The right column can be the search from turn 1, or
"your turns 1..k-1, then the search", which is the cleanest comparison because both columns share
the same board up to turn k. Each log also opens in the ordinary step viewer (`index.html?log=…`).

Settle each game one of four ways (keys `1`–`4`, `j`/`k` to move between games):

- **Promote search line**: the line in the right-hand column (the search from turn 1, or your
  turns 1..k-1 then the search) becomes the reference for this game. `scripts/ref_promote.py`
  replays the game in the play engine and picks, at each main-phase frame, the plan that does what
  the search did. It writes the result only if the engine-written file wins on the search's turn and
  replays as a literal reference. Otherwise it refuses, says why and at which turn, and you can
  replay that game yourself. Your own line is kept byte for byte at
  `references/review/replaced/<deck>/human_s<S>_gi<G>.json`. That is outside every `claude_s*_gi*.json`
  glob, so nothing replays or benches it, but the reference safety scan still covers it.
- **Clairvoyance**: the search's line needs to know the library.
- **Bug**: the search's line relies on an engine defect (name it in the note).
- **Replay manually**: your line is beatable but not this way. This is just a marker; re-save the
  game yourself in the play viewer.

Verdicts (and promoted references) are saved to the tracked `references/review/verdicts.json` /
`references/` and protected like reference games: a backup outside the repo, a commit to
`references-autosave`, and a push. So `review.sh`, like `play.sh`, refuses to start from a temp
directory or a linked worktree. Each verdict records the search line it judged; after an engine
change, a game whose search line moved shows its verdict as **stale** for re-checking.

`logs/ref_review/<ref dir>/<game>/` holds `human.json` (your game as a replay log: its boards are exact,
but replaying a choice stream does not log the plays, so the page reads your plays from the reference's
decisions), `search.json`,
`handoff_T<k>.json`, a copy of the reference, and `review.json` (the ladder, hints and pasteable
repro commands). Re-run `build` after an engine change; verdicts carry over, and finished engine games
are cached (keyed by the command and the engine), so a rerun only plays what changed. Games become
reviewable as soon as their own ladder is in; the page shows a build in progress and fills in the
non-clairvoyant hints as they land (on the combo decks those take the longest).

## Planned

- **Non-tap ability visuals** — sac / pay-life / discard-cost abilities are logged (`ABILITY`)
  but not yet drawn distinctly; add when a deck uses them.
- A human-play mode is a later phase (compare mode: see "Review mode" above).
