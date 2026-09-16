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
| edf_ref_s6_t4_drake_chain_before_reducer | claude_s6_gi5 T4, 3rd frame | 4 | 5 | [Drake, Drake, Call -> Displacer, Training Grounds] then the loop from the float left over. NB the REAL board (scripts/ref_handoff.py --turn 4 --frame 2) wins T4 with MTG_EDF_PAYLOAD_FIRST on; this frozen copy still loses a turn -- the difference between the frame and its fixture is BATTLEFIELD INSERTION ORDER (settled 2026-09-15, Session 30): both cast [Drake, Drake, TG, Call], but the payer's tie-break between Adarkar Wastes and Brushland for the Call follows battlefield order -- the real frame leaves Adarkar (the {C} source) up and the Displacer's blink pays, the fixture leaves Brushland up and the Displacer's {W} taps the only {C} source. An order-dependent payment: the autonomous payer does not reserve a {C} source for the line's coming blink (MTG_LINE_C_HOLD is the hard form and lost s10; the human-play HOLD_C_IN_PAYMENT tie-break is the soft form, unmeasured autonomously) |

Closed and promoted to `test/scenarios/`: `edf_ref_s8_t3_bank_dig_wish_drain` (2026-09-15, `MTG_EDF_DRAW_SINK_HONEST`);
`edf_ref_s6_t4_float_keeps_c_for_blink` (2026-09-15, the executor's dead line-hold scope + `MTG_HOLD_C_FOR_LINE` default ON).
