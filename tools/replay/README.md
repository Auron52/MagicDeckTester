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

## Planned

- **Non-tap ability visuals** — sac / pay-life / discard-cost abilities are logged (`ABILITY`)
  but not yet drawn distinctly; add when a deck uses them.
- A human-play / compare mode is a later phase.
