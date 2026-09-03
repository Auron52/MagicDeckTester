# Scenario harness (`mtg --scenario`) + pass/fail fixtures

Self-contained. Shipped 2026-07-02.

## What it is

A way to run **one hand-built board** through the real AI turn engine and assert the outcome —
deterministic, no seed/shuffle. The seed-driven regression suite can't target a specific interaction
("with these exact permanents in play, does the AI find the pump-and-swing lethal?"); a scenario can.

```
mtg --scenario test/scenarios/<name>.json
```

Exit code: **0** = PASS (or no assertion), **1** = FAIL (`expect_win_turn` not met), **2** = harness
error (bad JSON / unknown card). *(2026-09-03: `expect_win_turn` is no longer the only assertion —
`RunScenario` also supports `expect_no_win`, `expect_opponent_life`, `expect_active_life`,
`validate_line`+`expect_verdict`, and `expect_variants` (src/main.cpp ~3985); `validate_line`
exists precisely because a win-turn-only fixture can miss a play bug — see
scaling-source-widening.md. Note `library_filler` defaults to `"Forest"`, the very filler the
dead-draw advice below warns against — set it explicitly.)* Implemented as `RunScenario` in `src/main.cpp` (early `--scenario`
dispatch, before the normal arg parse).

## Fixture format

```jsonc
{
  "deck": "decks/Anti-Lifegain.cod",   // provider selection + profile auto-detect ONLY (not shuffled in)
  "profile": "decks/X.profile.json",    // optional; auto-detected from the deck path if omitted
  "turn": 4, "on_the_play": true,       // state runs THROUGH this turn (turn_number = turn-1)
  "active_life": 20, "opponent_life": 16,
  "battlefield": [                       // controller 0 = you, 1 = opponent
    { "name": "Mountain" },
    { "name": "Birds of Paradise", "controller": 0, "tapped": false, "sick": false }
  ],
  "hand": [ "Aria of Flame", "Invigorate" ],
  "library_filler": "Swords to Plowshares",  // library = N copies of this (draws / rollouts don't run dry)
  "library_size": 40,
  "depth": 5, "budget_ms": 100, "max_turns": 4,
  "expect_win_turn": 4,                  // optional: FAIL (exit 1) if the actual win is later / absent
  "log_out": "logs/play/foo.json"        // optional: write the per-turn trace for inspection
}
```

- Each card is instantiated from its **CardDatabase definition** (`Lookup(name)->card`) — full P/T,
  cost, types, keywords — the same object play/cast copies onto a `Permanent`. `sick:false` (default)
  means it can attack this turn.
- **Filler matters.** The draw step (and lookahead rollouts) pull from `library_filler`. Pick a card
  that won't perturb the interaction: a **dead draw** (e.g. `Swords to Plowshares` when there's no
  creature to target → never cast) keeps the board fixed. A basic land would be drawn and PLAYED,
  handing the AI extra mana and dissolving a "must tap a scarce source" setup.

## Pass/fail suite + regression gate

`test/scenarios.sh` runs every `test/scenarios/*.json` and reports pass/fail from each fixture's own
`expect_win_turn`. `test/regression.sh` runs it as a **sanity gate** before the batch run (a scenario
FAIL aborts early). Fixtures are cheap (seconds), deck-agnostic, and committed alongside ground truth.

## Worked fixture — `dork_pump_target.json`

Guards the pump-target / mana-tap coordination (the "don't waste Invigorate on the dork that gets
tapped" concern). Board: `Mountain, Forest, Tainted Remedy, Birds of Paradise, Ignoble Hierarch`;
hand: `Aria of Flame, Invigorate`; opponent at 16.

The turn-4 kill: Tainted Remedy flips Aria's ETB "opponent gains 10" to **−10** (16→6); casting
Invigorate (free — we control a Forest) adds a verse counter so Aria pings **1** (→5) and pumps a dork
**+4/+4** to swing **alone** for 4 + Ignoble's exalted **+1** = **5** → lethal. Aria's `{2}{R}` forces
tapping one of the two dorks, so the kill needs the **other** dork pumped.

It PASSES today with no special code: the scarcity tap order leaves the most-flexible dork (rainbow
**Birds**, tapped last) untapped, and `FindBestOwnAttacker` — resolved *after* payment, filtering
tapped creatures via `CanAttackFull` — picks that untapped dork as the pump target. So the AI's
auto-target is inherently tap-aware. (The human-play path, where the player fixes the target and has no
tap visibility, is the open gap — see the mana-source-reservation doc's "reserve the pumped creature".)
