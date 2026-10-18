# Colossus Hammer: the cast order (USER-ADOPTED 2026-10-18)

**Status: ADOPTED, default ON** (`MTG_COLOSSUS_ORDER`, heurarm `COLOSSUS_ORDER`; =0 restores the generic
order). AI-drafted 2026-10-17 under the USER's standing ask (*"The generic order is never a good option ...
It should be edited by you and then presented to me for additional feedback"*); the USER on 2026-10-18:
*"That seems reasonable, but we should make a full order, also, we should have equip abilities listed in
the order. Path to Exile can be excluded entirely via a heuristic for goldfishing."* So the draft's nine
classes became one position per card, the board activations are positions of the order too, and Path to
Exile is never cast (`MTG_COLOSSUS_NO_PATH`, default ON). Code: `ColossusHammerProvider::CastOrderRank`,
`ActivationOrderRankFor`, `NeverCast` (src/ai/DecisionProviders.cpp, "Cast order").

## The deck's ordering problem in one paragraph

Every line that matters is "get Colossus Hammer (+10/+10) onto a double striker without paying equip
{8}". Two of the five cheats are ORDER-SENSITIVE inside one main phase: **Sigarda's Aid** attaches an
Equipment only as it ENTERS, so the Aid must already be on the battlefield and a HOST must already be
there; **Forge Anew**'s graveyard return enters through the same trigger, so it also wants the Aid and the
hosts first. **Magnetic Theft** needs both its targets on the battlefield (enforced structurally by
`ApplyAttachSpellDependencyOrder` anyway). The tutors put a card into hand mid-phase, and site 10 (the
put-in-hand breakpoint) re-solves the rest of the phase, so a tutor first leaves the most of the phase to
spend on what it found.

## The full order (all main 1)

| # | step | why here |
|---|---|---|
| 0 | the land drop | precedes every cast: holding it never pays in this list (no landfall, Karoo or storage land) |
| 1 | cast Steelshaper's Gift | the found card enters hand and the rest of the phase re-solves; first = the most phase left |
| 2 | cast Open the Armory | the same; second only because it costs more |
| 3 | cast Sigarda's Aid | must be on the battlefield BEFORE any Equipment enters |
| 4 | cast Kor Duelist | a double-strike host, out when the Aid's trigger picks one |
| 5 | cast Swiftblade Vindicator | the same |
| 6 | cast Speaker of the Heavens | a further (lifelink) body |
| 7 | cast Raubahn, Bull of Ala Mhigo | attaches at its own attack, so its timing is free |
| 8 | cast Ardenn, Intrepid Archaeologist | attaches at begin combat, so its timing is free |
| 9 | cast Lizard Blades | after the hosts (the Aid can put it ON one), before the Hammer (a Lizard left a creature -- a 1/1 double striker -- can carry it) |
| 10 | cast Forge Anew | after the Aid and the hosts: the Equipment it returns attaches as it enters |
| 11 | cast Colossus Hammer | after every host and the Aid; first of the Equipment, the kill piece |
| 12 | cast Loxodon Warhammer | |
| 13 | cast Dragonfire Blade | |
| 14 | cast Magnetic Theft | after every Equipment and creature |
| -- | Path to Exile | **never cast** (below) |
| 15 | Lizard Blades: reconfigure, unattach | a creature again before anything is equipped |
| 16 | Colossus Hammer: equip | the FIRST equip ability, so the one Forge Anew's {0} pays for |
| 17 | Loxodon Warhammer: equip | |
| 18 | Dragonfire Blade: equip | |
| 19 | Lizard Blades: reconfigure, attach | not an equip ability, so never waived |
| 20 | Speaker of the Heavens: create the Angel | last: the token is summoning-sick, nothing above wants it first (also offered in the narrow post-combat main, `MTG_SPEAKER_M2`) |

Positions 15-20 run after every cast -- the shape the USER set for KittyEquipment (*"the equip usages
should all go at the end of main 1"*): an equip is pure generic mana, so paying it after the casts lets
every coloured source go to a spell first, the hosts and Forge Anew are on the battlefield by then, and
nothing in the list reads an equipped creature while a spell is being cast. Every position is keyed on
the card's parameters, never its name; an unlisted card falls back on its class, then mana value, then
name, so plan order never decides.

**Where order does not change the result** (said so, as the USER asked to know): within 4-5, 6-8 and
11-13 nothing reads another's arrival (the Aid's host pick stacks every piece onto the best host
whichever enters first), and 17-18 commute (generic mana, different pieces). **Forge Anew's waiver**: the
engine gives it to the plan's MOST EXPENSIVE equip (`StoriedWaivedEquipId`), which position 16 makes the
first equip activated whenever the Hammer is in the plan; when it is not and Warhammer and Dragonfire
Blade both cost {3}, which one is "first" changes no mana and no board.

## Path to Exile: never cast

`ColossusHammerProvider::NeverCast` drops it at the enumeration choke point (the KittyEquipment Swords
shape), so the search, the breakpoint continuations, the rollouts and the d0 runner never see it, and
the discard reads it as dead. Its only goldfish use was the self-target ramp line (exile our own
creature, fetch a TAPPED basic), which trades a body -- usually a host -- for a land that taps next turn;
the opponent target never had any effect. Human play and `MTG_UNPRUNE=uacast` still offer it.

## Measurement of the DRAFT (2026-10-17, PGO binary of `8c1de4465`, ONE pooled batch, heurarm arms per job)

Held-out seeds (13100001+, disjoint from every suite seed), every arm the same games, loss = 9, paired per
game, diff = draft - generic (negative = faster). Script: `build/scratch/colossus/order_ab/make_ab.py`.

| cell | n | generic | draft | diff (se) | faster / slower | search units |
|---|---|---|---|---|---|---|
| play settings (d5 b20) | 3000 | 3.7917 | 3.7387 | **-0.0530** (.0043) | 160 / 2 | 0.944x |
| d3 b10 | 4000 | 3.8185 | 3.7630 | **-0.0555** (.0038) | 219 / 3 | 0.948x |
| d0 | 20000 | 5.0282 | 4.9804 | **-0.0478** (.0025) | 512 / 16 | -- |
| 2HG play settings | 1000 | 4.4500 | 4.4040 | **-0.0460** (.0071) | 49 / 3 | 0.965x |

**Every searched slower game recovers** (the adoption bar): all at stage 1 (same depth, budget 100) except
s13102419 (play cell, T5 -> T7), which recovers at stage 2 (d8, unlimited budget: T5).

**WHY it wins (the mechanism, from the logs and claude-play seed 2100014):** under the generic order Sigarda's
Aid and every Equipment tie at rank 20 and fall to PLAN order, which casts the Equipment first -- so the Aid
is not on the battlefield when the Hammer enters and its attach trigger never fires. "Aid, then Hammer
onto a host" is the deck's cheapest kill (T2 off Kor Duelist + {W} + {1}) and under the generic order it is
**unfindable at any depth or budget** (seed 2100014: generic T3 at d1..d5 unlimited; draft T2). Slot 2 (Aid
before every Equipment) is the slot that matters; the rest of the draft is tie-break.

The USER adopted it on 2026-10-18 with the amendments above; the adopted order is measured in the next section.
