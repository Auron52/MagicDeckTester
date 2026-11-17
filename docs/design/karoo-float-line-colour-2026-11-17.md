# Karoo tap-in-response: a choice land floats the colour its line owes (2026-11-17)

Lever: `MTG_KAROO_FLOAT_LINE_COLOUR` (heurarm slot `KAROO_FLOAT_LINE_COLOUR`), DEFAULT ON; `=0` restores the
first-listed-colour commit byte-for-byte.

## The defect

Minotaur mana-clean gate `claude-pd-karoo-20261117`, seed `30289322` T4, plan
`{no land + cast:Deathbellow Raider + cast:Slaughter-Priest of Mogis}` with Ragemonger in play: APPLY-DROP x2.
Rakdos Carnarium #45 pays the Raider's `{1}` with its `{B}` and floats `{R}`. The breakpoint continuation plays
Rakdos Carnarium #46, whose bounce returns the untapped Blood Crypt. The tap-in-response floats it as `{B}`, so
the continuation's Boros Reckoner (`{R/W}{R/W}` after Ragemonger) meets `{B}{R}` and is dropped. The oracle's line
has the Crypt's `{R}`.

Two causes, both fixed:

1. **The colour.** The float is real mana, so a source with a choice of colours must commit one: a bare wild would
   launder the choice into a colour the land cannot make. `FloatSourceTapCommitted` commits by the hand's pip
   total. That read is off by default (`MTG_PLACEHOLDER_MANA`: hand costs are placeholders), so the land always
   floated its first listed colour.
2. **The line it reads.** A breakpoint continuation plays its land-first drop (`bp_play_searched_land`) BEFORE
   `apply_continuation_plan` binds the continuation's line hold. At the bounce, `g_line_unpaid_cost` still held
   the invoking plan's bill, which was already paid.

## The change

- `FloatLandBeforeSacrifice` asks `FloatLineOwedColour` first. When the land makes ONE mana from 2-4 colours, it
  commits the colour the line still owes, net of what already floats in that colour, with ties in list order.
  When nothing is owed, it keeps the old commit. A pin (Lake of the Dead's victim colour) and the plain-`{C}`
  path still take precedence. The ritual tap-ahead does not go through here and is unchanged.
- The demand is `g_land_float_demand` (`LandFloatDemandScope`, `SpellEffects.h`) when a land drop binds one, and
  otherwise the live hold (`LineOwedAfterOwnPips`). A continuation's land-first drop binds that continuation's
  own cast bill, `LineCastCostTotal(cp.actions)`, in lockstep in both worlds:
  - search: `bp_play_and_record_land` / `bp_play_searched_land` (`TurnSolver.cpp`);
  - executor: every live continuation land play (`TryPlaySpecificLand(state, extra.land_to_play, ...)`, seven
    sites in `AIEngine.cpp`);
  - the committed-line replay: the recorded `PlayLand` carries the bill as `Action::rec_line_hold`, and
    `replay_recorded` binds it around the land.

  A positioned continuation land plays inside its cast section and reads the live hold there, as both worlds
  already bind it.

## Measurement

Paired A/B, one pooled Release batch, both arms per job via the heurarm flag. Games 510,000–511,999 of every
deck with a Karoo or Lake of the Dead (16 decks, 32,000 pairs; `build/scratch/claude_karoo_line_ab/`):

| deck | earlier | later | digest-only | work units |
|---|---|---|---|---|
| Angels, Dragons, Landfall, Landfall Elementals, Martyr Lifegain, Shadowborn Apostle | 0 | 0 | 0 | +0.00% |
| Boars | 0 | 0 | 3 | +0.07% |
| Bruna | 0 | 0 | 1 | +0.05% |
| Creature Giving, Eldrazi Displacer Flicker, Hinata, Spirits | 0 | 0 | 0 | ≤ +0.01% |
| Melira Pod | 0 | 0 | 2 | +0.00% |
| Minotaur | 0 | 0 | 0 | -0.01% |
| Mirrorwing Dragon | 0 | 0 | 1 | +0.17% |
| Morph | 0 | 0 | 5 | -0.08% |

No game is slower or faster. The exact seed replays with zero oracle issues (lever off: APPLY-DROP=2). Unit
suite 3,388/3,388; `test_karoo_tap_in_response.cpp` covers the owed colour, the netting against the float, and
the lever off.

## Known gaps (not changed)

- A TOP-LEVEL plan's own land drop is played before its line hold is bound, in both worlds alike, so a Karoo
  played as the plan's land still floats the first listed colour. Making it read the plan's bill is the same
  shape at more sites; no flag has shown it.
- The commit is still made at the bounce, not at spend (the pool has no "one of these two colours" unit). Reading
  the line's bill is the sequence-aware signal `FloatSourceTapCommitted`'s note asks for, but it is a bill, not
  an order.
