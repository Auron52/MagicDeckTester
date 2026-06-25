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

## Generating logs

Any run with `--log-dir` emits one JSON per game (deterministic by seed + game index, so a
flagged game is exactly reproducible):

```
./build/Release/mtg decks/Hinata2.cod --games 30 --seed 7000 --depth 5 \
    --budget-ms 20 --max-turns 8 --threads 0 --log-dir logs/hinata_games
```

## Planned (needs log-side fields)

The viewer already has latent support for these; they light up once the engine logs the data:

- **Tapped state** — any permanent the log marks `tapped` renders rotated 90° (lands tapped for
  mana, dorks/rocks whose ability was used, attackers after combat).
- **Chosen X** — the real X value for `{X}` spells (Crackle / Reality Spasm) on cast actions.
- **Scry/dig reveals** — what Ponder/Preordain/Scry looked at, kept, and bottomed.

A human-play / compare mode is a later phase.
