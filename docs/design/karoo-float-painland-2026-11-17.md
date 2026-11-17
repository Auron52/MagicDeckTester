# Karoo tap-in-response: a painland floats its painless {C}, or the colour the line owes (2026-11-17)

Levers (heurarm slots of the same names), both DEFAULT ON:
- `MTG_KAROO_FLOAT_PAINLAND_C`: `=0` restores the old refusal byte-for-byte.
- `MTG_KAROO_FLOAT_PAINLAND_OWED`: `=0` always floats the `{C}`.

## The defect

`KarooBounceFloatable` refused every `tap_self_damage` land, so a Karoo that returned an untapped Brushland, Llanowar
Wastes or Caves of Koilos floated nothing. A painland's `{T}: Add {C}` is a separate, painless ability (the reading
`DripLandAnyPipColor` and the payer's pain guard already use), so the controller can tap it for that `{C}` while the
bounce trigger is on the stack (CR 605.3a, 106.4). This is the same shape as `karoo-float-restricted-c-2026-11-17.md`.

USER 2026-11-17: *"I certainly see no reason not to float mana. The bigger question is whether to float colourless or
something else?"*

## The change

`FloatLandBeforeSacrifice` (the bounce's own float, `SpellEffects.h`) now handles a painland with a real `{C}` mode
(`KarooFloatsPlainC`: `tap_self_damage > 0`, not `tap_self_damage_any_mode`, one mana per tap, `{C}` in `produces`):
- If the rest of the applying line still owes a colour the land makes (`FloatLineOwedColour`: the land drop's bound
  demand, otherwise the live hold, net of what already floats), it floats that colour and the controller takes the
  land's damage. `KarooBounceFloatable` refuses an armed damage-event board, so the damage is plain life loss.
- Otherwise it floats `{C}`, at no life.

What is unchanged:
- A creature-only pain land (Voldaren Estate) floats `{C}` only and never a colour.
- City of Brass (no `{C}` mode) and Ancient Tomb (pain in every mode) stay refused.
- The provider's bounce ranking and the USER-rule candidate filters still ask the default question, so a painland
  is still not a no-loss bounce for them.

The colour is read from the line's bill, not from its payment order. When another untapped source would have covered
that colour, the 1 life was spent for nothing.

## Measurement

The test is a paired A/B in one pooled Release batch with three arms per seed:
- old: both levers off;
- `cee`: always `{C}`;
- `owed`: both levers on.

It ran games 520,000-524,999 of the only two decks holding both a Karoo and a `{C}`-mode painland
(`build/scratch/claude_painland_ab/`):

| deck | arm | earlier | later | digest-only | work units |
|---|---|---|---|---|---|
| Eldrazi Displacer Flicker | cee | 0 | 0 | 17 | +0.15% |
| Eldrazi Displacer Flicker | owed | 1 (gi 524423, T5 -> T4) | 0 | 18 | +0.18% |
| Melira Pod | cee | 0 | 0 | 7 | +0.16% |
| Melira Pod | owed | 0 | 0 | 7 | +0.16% |

Neither arm has a slower game. The owed arm is the shipped default. The extra work comes from more simulated lines
becoming payable, not from a new branch.

`test/unit/test_karoo_tap_in_response.cpp` covers the cases:
- nothing owed: `{C}` at 20 life;
- `{G}` owed: `{G}` at 19 life;
- owed lever off: `{C}`;
- Voldaren Estate: `{C}` only;
- `_PAINLAND_C` off: nothing floats.

The same commit fixes a unit-test leak. `test_human_line_joint_pay.cpp` called `HumanLineJointPrepay` bare, and the
joint pay's activation hold left `{G}` in `g_line_unpaid_cost` for every later test on the thread. In play, the
apply's own scope restores it.
