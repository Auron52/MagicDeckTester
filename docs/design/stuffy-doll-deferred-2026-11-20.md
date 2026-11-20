# Stuffy Doll: deferred items (2026-11-20)

Status: DEFERRED. The Stuffy Doll analysis checkout was freed for other work. The USER (2026-11-20): *"drop any work
that doesn't seem promising for the time being"*, and *"Things focused on improving d0 or arguing about small
differences don't seem worth a second look. We're looking for high impact."* Both items below have USER rulings, so
whoever picks one up needs no further decision. The analysis ledger is `docs/design/analysis-Stuffy Doll.md`; the
mechanics are in `docs/design/damage-reflection-pipeline.md`.

## 1. Pip-aware Karoo bounce float colour in human play (claude-play sweep flag E)

**Problem.** A Karoo land (Boros Garrison, Azorius Chancery, ...) returns a land to hand on entering. If the returned
land is untapped, the engine taps it "in response" and floats its mana (`FloatKarooBouncedLand` ->
`FloatLandBeforeSacrifice`, `src/core/SpellEffects.h`). The colour comes from, in order:
1. a pin (`t_sac_victim_float_pin`);
2. the colour the applying line still owes (`FloatLineOwedColour`, `MTG_KAROO_FLOAT_LINE_COLOUR`);
3. otherwise `FloatSourceTapCommitted`'s fixed commit.

Human play is never asked. When the committed line owes nothing more, case 3 picks the colour blind to the hand.
Sweep seed 930004 (decision 6): Garrison returned Sacred Foundry, which floated {R}. {W} would have cast Guilty
Conscience on Nomads en-Kor in that same phase.

**USER ruling (verbatim):** "(a) is the way to go and should be sufficient when combined with manual tap." So:
- Human play only: when the line owes no colour, float the colour that makes something in hand payable this phase.
  Try each producible colour and ask the payer whether any hand spell or ability becomes payable; the
  shock-land default in the VSH change (analysis ledger row VSH) does the same "any payable use" test.
- On a tie, keep today's commit, so most games replay unchanged.
- Manual tap (`tap=` tokens) stays the override. Do not add a new viewer prompt.
- Autonomous play is unchanged.

**Replay.** Changing the default changes how existing references replay. Stamp new traces `"karoo_float": 1`. Replay
a reference without the stamp under a legacy flag that keeps today's colour, following the
`opponent_shuffle_ordinal` / `--legacy-shared-shuffle-ordinal` pattern (`viewer_protocol_check.recording_rule_args`).
Validate with `test/viewer_protocol_check.py --strict`: 0 play-drift, 0 enum-gap. The USER never re-saves a reference.

**When done:** set the analysis ledger's `## Claude-play sweep` line to `flags: 0 unresolved`.

## 2. MTG_CCO_NONCREATURE_POOL in human menus

**Problem.** Some lands' coloured mana pays only for creature spells (Unclaimed Territory, Cavern-type lands,
`colored_creature_only`). The noncreature enumeration pool counts such a land as any colour. Human menus therefore
offer plans the payer cannot pay, labelled `known_drops`. Example: Spirits reference s9, where the flashback is
offered after the Grave is spent. In the search, the flag measured exactly neutral and stays OFF
(`src/ai/TurnSolver.cpp`, `s_cco_noncreature_pool`).

**USER ruling (verbatim):** "This is fine to turn on as long as we make sure to fix any issues we might find. I am not
okay with needing to replay references I already did, so any that are broken need to be fixed." So:
- Turn it ON for human menus only.
- Any reference that then fails to replay is a defect to fix in the engine or the checker. Plan indices that shift are
  already repaired by VPC's stale-index repair; anything else must be fixed.
- Validate with VPC `--strict` over every reference, and start with Spirits s9.

## Dropped (not deferred)

`MTG_RFX_FAN_CHARGE` option (b), billing fan probes only against the Overrun ceiling, was approved to try ("Let's try
(b)") and then dropped as a small difference (+0.002 turns at stake). The charge stays as shipped (option a).
