# The same-turn attack projection ignores haste granted by Equipment

**Status: DIAGNOSED, NOT FIXED. Needs a user decision, because the fix moves ground truth for five
shipping decks.**

Found 2026-09-26 by the WhiteKnights Stage-5d claude-play sweep (seed 7867), confirmed directly.

## The defect

`CollectActions` stamps `Action::haste_attack_power` — "the power this creature adds to THIS turn's
attack, because it can attack the turn it arrives" — under this condition
(`src/ai/TurnSolver.cpp:16588`):

```cpp
if (def.card.IsCreature()
    && (def.card.HasKeyword(Keyword::Haste)
        || HasHasteFromLords(def.card, state.battlefield, state.active_player_index)))
```

Printed haste, or haste from a lord. **Haste granted by an Equipment attached in the same plan is not
considered.** So a plan of the shape *cast a creature + cast Lightning Greaves + equip it to that
creature* never has the creature's power added to `projected_atk`, and the search cannot see that the
plan is lethal.

`PendingAttackDamage` consumes the stamp (`haste_cast_atk`), so the miss is in the projection input,
not in the arithmetic.

## Proof

Deck `decks/WhiteKnights/WhiteKnights.cod`, profile attached, **seed 7867**:

```bash
# The line a guided human found -- wins TURN 4 (18 damage into 12 life, opponent to -6):
./build/Release/mtg decks/WhiteKnights/WhiteKnights.cod \
  --profile decks/WhiteKnights/WhiteKnights.profile.json \
  --claude-play --seed 7867 --max-turns 12 --reveal 8 \
  --choices "1,0,-1,-1,1,-1,-1,1,0,-1,-1,0,-1"
# -> "win_turn": 4, combat: Venerable Knight (6), Dauntless Bodyguard (3),
#    Silverblade Paladin (6), Accorder Paladin (3) -- 18 to opponent (12->-6)

# The fully-clairvoyant search on the same game, at a depth and budget far above production:
./build/Release/mtg decks/WhiteKnights/WhiteKnights.cod \
  --profile decks/WhiteKnights/WhiteKnights.profile.json \
  --games 1 --seed 7867 --depth 8 --budget-ms 3000
# -> avg (turns): 5.0000
```

The winning play is Lightning Greaves equipped to a **freshly cast** Accorder Paladin, which gives it
haste; battle cry plus a live soulbond pair then make exactly 18.

## Why the usual explanations do not apply

* **Not a budget or depth limit.** The sweep agent handed the search the identical turn-4 board via
  `--choices-then-auto` and it chose turn 5 at d5/20 ms, d6/200 ms **and** d6/2000 ms; the full
  benchmark takes turn 5 at d5/200, d6/1000 and d8/3000. More search effort cannot fix a projection
  that never reports the damage.
* **Not a missing plan.** The plan is enumerated — a human selected it by index. `equip_host` on a
  just-cast creature is offered for a hard cast.
* **Not "the search does not know about equip-for-haste".** It does use Greaves on turn 5 of this very
  game. That is the tell, not a counterexample: by turn 5 the creature is no longer summoning-sick, so
  the haste grant is irrelevant and the equip is chosen for other reasons. The projection is only
  consulted for *this* turn's damage, which is exactly the case it gets wrong.

## Blast radius — why this is not a quiet fix

`equip_grants_haste` is true for exactly one card today, **Lightning Greaves**, which appears in **six**
shipping decks:

| deck | in ground truth? |
|---|---|
| Angels | yes |
| Dragons | yes |
| FiveColour | yes |
| Giants | yes |
| KittyEquipment | yes |
| WhiteKnights | no (new deck, not yet in the suite) |

Fixing the projection makes the search **strictly better informed**, so it will find earlier kills and
win turns will improve — which means **play changes and per-game digests move for five GT tiers**. That
is a quality improvement with a real cost (a five-tier rebaseline), not a no-drawback win, so by the
repo's own rule it is the user's call rather than an agent's.

## Suggested shape of the fix, when it is taken

1. Extend the stamp condition to include an equipment-granted haste that **this plan** attaches. The
   plan already knows its `equip_host`, so the information is present at `CollectActions` time; the
   condition needs the equip pairing, not a new lookup.
2. While there, note the adjacent narrowing on the **next two lines**: the `ds` flag beside it uses
   printed double strike + `HasDoubleStrikeFromLords`, and not the shared `CreatureHasDoubleStrike`
   oracle, so an equipment-granted double strike is missed in the same projection. Worth fixing in the
   same change for the same reason. (Soulbond is not affected here: the creature is not on the
   battlefield yet when the stamp is written, so no pairing exists to read.)
3. Measure as an A/B on the five affected tiers, expect **improved** win turns, and rebaseline
   deliberately per `.claude/skills/regression-testing.md`.

## Related sweep finding (same area, already cleared)

A separate agent (seed 7811) observed that plans which **Aether-Vial put** a creature offer
`equip Lightning Greaves` only to *pre-existing* creatures, while the hard-cast plan offers the
just-arrived one. They branched and confirmed the following `pre_main` frame does offer the equip, so
**no legal line is lost** for a human — it is a single-plan enumeration narrowing. It is recorded here
only because it sits next to this defect and could be mistaken for it: that one costs the human
nothing, this one costs the search a whole turn.
