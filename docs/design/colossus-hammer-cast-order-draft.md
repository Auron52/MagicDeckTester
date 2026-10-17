# Colossus Hammer: DRAFT cast order (AI-drafted 2026-10-17 — awaiting the USER's review)

**Status: PROPOSAL.** Cast order is USER-owned (CLAUDE.md; USER 2026-10-14: *"The generic order is never a
good option ... It should be edited by you and then presented to me for additional feedback"*). This
draft ships behind `MTG_COLOSSUS_ORDER` (heurarm `COLOSSUS_ORDER`), **default OFF** until the USER
approves; with it off the deck rides `GenericProvider`'s param tiers. The measurement of ON vs OFF is
recorded at the bottom when it has run.

## The deck's ordering problem in one paragraph

Every line that matters is "get Colossus Hammer (+10/+10) onto a double striker without paying equip
{8}". Three of the five cheats are ORDER-SENSITIVE inside one main phase: **Sigarda's Aid** attaches an
Equipment only as it ENTERS, so the Aid must already be on the battlefield and a HOST must already be
there; **Forge Anew**'s graveyard return enters through the same cascade, so it also wants the Aid and
the hosts first; **Magnetic Theft** needs both its targets on the battlefield (already enforced
structurally by `ApplyAttachSpellDependencyOrder`, whatever the rank). The tutors put a card into hand
mid-phase, and site 10 (the general put-in-hand breakpoint, `MTG_BP_PUT_IN_HAND`) re-solves the rest of
the phase, so a tutor first leaves the most of the phase to spend on what it found.

## Proposed slots (all MAIN 1; the land drop is structural and precedes every cast)

| slot | cards | key (params, never names) | why here |
|---|---|---|---|
| 1 | Steelshaper's Gift, Open the Armory | `tutor_to_hand` with `tutor_types` holding Equipment | the found card enters hand → site-10 re-solve; first = the most phase left to use it |
| 2 | Sigarda's Aid | `attach_equipment_on_etb` | must be on the battlefield BEFORE any Equipment enters this phase |
| 3 | Swiftblade Vindicator, Kor Duelist | creature, printed double strike or `double_strike_while_equipped` | the hosts the Hammer is FOR must exist when the Aid's trigger picks a host |
| 4 | Raubahn, Ardenn, Speaker of the Heavens | any other non-Equipment creature (sub-key mana value) | further hosts; Raubahn / Ardenn re-attach at combat, so their own timing is free |
| 5 | Lizard Blades | Equipment that is a creature (`reconfigure_cost`) | after the hosts (so the Aid can put it ON one), before the Hammer (so, if it stays a creature, the Hammer can land on IT — a 1/1 double striker) |
| 6 | Forge Anew | `etb_return_equipment_from_gy` / `first_equip_free` | after the Aid + hosts (the returned piece is attached as it enters); its {0} equip is used in the trailing equip pass either way |
| 7 | Colossus Hammer, Dragonfire Blade, Loxodon Warhammer | non-creature Equipment (sub-key mana value) | after every host and the Aid |
| 8 | Magnetic Theft | `attach_target_equipment` | after every Equipment and creature (structural anyway) |
| 9 | Path to Exile | Removal template | last: it removes a body; only the self-target ramp line is offered |

TOTAL order, as every agreed order here is (USER 2026-10-14: *"fully order the cards in each list
inasmuch as possible"*): `rank = slot << 16 | sub-key << 12 | name ordinal` — plan / hand order never
decides. Activations (equip, reconfigure, Speaker's token) stay in the engine's trailing pass after the
casts. Speaker's token activation is ALSO offered in the narrow post-combat main (`MTG_SPEAKER_M2`).

## Where order should NOT matter (said so, per the USER's ask)

* Within slots 3, 4 and 7 the order is a tie-break only: no card there reads another's arrival.
* Forge Anew's waiver (slot 6) is order-free: it is credited per plan to the most expensive equip.

## Questions for the USER

1. Slot 5 (Lizard Blades between the hosts and the Hammer) is the one judgement call: it lets the Hammer
   land on a creature Lizard; the alternative is Lizard with the other Equipment (slot 7).
2. Should Forge Anew precede Sigarda's Aid when the graveyard holds no Equipment? (It does not matter
   then — the draft keeps one fixed slot for simplicity.)
