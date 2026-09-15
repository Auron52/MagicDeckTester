# Open construct fixtures (NOT run by test/scenarios.sh)

Each fixture here is a real board from a hand-played reference game on which the shipped search
loses a turn to the human, reproduced at true fidelity with `scripts/ref_handoff.py` (the
recorded picks up to the frame, then the autonomous engine) and then frozen as a `--scenario`
board. They FAIL on purpose: each documents a construct the engine cannot yet execute, with the
exact expected turn. When one passes, move it up into `test/scenarios/` so the gate keeps it.

Run one by hand:

    build/Release/mtg --scenario test/scenarios/open/<fixture>.json

`resume_at: main1` starts the playout inside the fixture's turn (no untap, no draw, the
`floating_mana` pool intact) -- the form a mid-turn frame needs.

| fixture | reference | expected | today | construct |
|---|---|---|---|---|
| edf_ref_s6_t4_drake_chain_before_reducer | claude_s6_gi5 T4, 3rd frame | 4 | 5 | [Drake, Drake, Call -> Displacer, Training Grounds] then the loop from the float left over. NB the REAL board (scripts/ref_handoff.py --turn 4 --frame 2) wins T4 with MTG_EDF_PAYLOAD_FIRST on; this frozen copy still loses a turn -- the difference between the frame and its fixture (sick flags / the used land drop / the opponent library) is itself the open question |
| edf_ref_s6_t4_float_keeps_c_for_blink | claude_s6_gi5 T4, 5th frame | 4 | 5 | cast the outlet from a mixed float keeping {C} (MTG_HOLD_C_FOR_LINE does that), THEN the loop's draw sink must not spend the next crank's {C} |

Closed and promoted to `test/scenarios/`: `edf_ref_s8_t3_bank_dig_wish_drain` (2026-09-15, `MTG_EDF_DRAW_SINK_HONEST`).
