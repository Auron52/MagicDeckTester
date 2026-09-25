# A mana rock cast by a line cannot pay for that line's later casts

**Status: DIAGNOSED, NOT FIXED.** Reported by the user 2026-09-25 from the play viewer; root-caused
the same day; no fix attempted yet because the change is in the committed-payment path and needs
measurement, not a patch.

## The report

> "The mana usage is just bad in this line. It fails to keep the Peat Bog despite the fact that it
> isn't difficult to do so."

candidate-B Fungus, **seed 8, game-index 0, turn 3**. Reproduce with:

```
build/Release/mtg decks/Fungus/candidate-b-2026-09/Fungus.cod \
  --profile decks/Fungus/candidate-b-2026-09/Fungus.profile.json \
  --cards-json src/cards/data/cards.json --claude-play \
  --seed 8 --game-index 0 --max-turns 8 --depth 0 \
  --choices "1,4,-1,-1,12,-1,-1,<plan>"
```

(`logs/wild2/replay8.py` walks to that frame and dumps it; `logs/wild2/s8ab.py` commits a plan
matched by CONTENT rather than index — see the pitfall at the bottom.)

## The board and the line

Turn 3, pre-combat main, before the land drop:

| permanent | produces |
|---|---|
| Forest | `{G}` |
| Peat Bog | `{B}{B}` — **one depletion counter left**, so the next tap sacrifices it |
| Undercellar Myconid | one mana of any colour (cast T2, not summoning-sick) |
| 1/1 Saproling Token | — |

Hand holds `Secluded Courtyard` (the land drop; `{C}`, or any colour restricted to creature
spells), `Sol Ring` `{1}`, `Shroofus Sproutsire` `{2}{G}`, `Wild Growth` `{G}`, and two more.

The committed line plays Secluded Courtyard and casts **Sol Ring + Shroofus Sproutsire** (4 mana).
The plan's own `cast_order_canonical` is `["Sol Ring", "Shroofus Sproutsire"]` — Sol Ring first.

## What happens

Forest, Secluded Courtyard and **Peat Bog** are tapped; Peat Bog's last counter goes and the land is
sacrificed. **Sol Ring is left untapped, and so is the Undercellar Myconid.**

```
Forest               tapped
Secluded Courtyard   tapped
Sol Ring             UNTAPPED   <-- cast by this very line, makes {C}{C}
Undercellar Myconid  UNTAPPED   <-- free, any colour
graveyard: ['Peat Bog']
```

A Peat-Bog-sparing assignment plainly exists: Forest `{G}` pays Sol Ring's `{1}`; Secluded Courtyard
pays Shroofus's `{G}` (it is a creature spell, so the restricted colour is legal); Sol Ring's own
`{C}{C}` pays Shroofus's `{2}`. Nothing is destroyed and the Myconid is still up.

## Root cause — it is NOT tap order

The first hypothesis was that the source ranking preferred the depletion land. It does not. Tracing
`ManaSourceRank` on this exact payment gives:

| source | rank (lower = tapped earlier) |
|---|---|
| Sol Ring | **5** |
| Forest | 10 |
| Secluded Courtyard | 59 |
| Peat Bog | 11 |
| Undercellar Myconid | 64–65 (dork attack reserve) |

Sol Ring is already the FIRST source the greedy would reach. It is not chosen because **it is not on
the battlefield when the payment runs**: the subset's whole cost is paid before any of its casts
resolve, so a permanent the line itself creates is never a source for that same line.

A reserve lever that pushed Peat Bog from 11 to 63 was built and measured on this game: **it does not
change the outcome** (Peat Bog still dies, because without Sol Ring the only remaining source is the
Myconid at 64, which ranks even later). It was reverted rather than shipped — ranking is the wrong
axis. Keep that negative result: it rules out the cheap fix.

## Why this is surprising — the engine already models it once

`TurnSolver.cpp` has a deliberate two-pass payment simulation, `want_rock`, that pays rocks FIRST and
then treats them as supply for the rest of the subset; `Action::rock_mana` carries the credit and
`joins_for_mana` pushes a resolved rock onto the copied battlefield so later casts in the ordered
walk can see it. Its comment is explicit that a same-line producer "resolves before the casts it
funds, so its bonus is genuine supply for the rest of the subset".

So the **payability** question is answered correctly and the **committed payment** is answered
without it. The two disagree, and the committed side is the one that taps real permanents. That is
the gap: the plan is judged affordable in a world where Sol Ring's mana exists, then paid in a world
where it does not — and the shortfall is made up out of whatever the ladder offers next, here a land
that dies for it.

## Consequences

* A one-shot resource (a depletion land's last counter, a Treasure, a storage battery) can be
  consumed to cover a shortfall that a rock cast in the same line already covers.
* It is not Fungus-specific. Any deck that casts a rock and then a spell in one main phase is
  exposed; Sol Ring is in candidate B precisely to be cast and used the same turn.
* It is invisible in aggregate metrics — the line still resolves and still wins — so nothing in the
  regression suite would have caught it. It took a human reading their own board.

## What a fix has to do

Make the committed payment pay in the plan's canonical cast order, crediting each resolved
mana producer before the casts that follow it — i.e. bring the apply path into line with the
`want_rock` simulation that already exists. Two cautions:

1. This is the hottest code in the engine and it moves tap order for every deck that casts a rock
   mid-line, so it is a byte-identity change and needs the full gate plus a measured A/B, not a
   patch.
2. The ordered payment must not be allowed to FAIL where the lump payment succeeded — a line that
   is affordable as a lump but not in order would become unplayable. The `want_rock` pass's retry
   shape (try reserved, fall back unreserved) is the precedent.

## A pitfall worth recording

`--choices` selects a plan by its **`index` field**, not by its position in the `plans` array. On
this frame there are 500+ plans and position 169 has index 340; committing "169" silently runs a
different line. Any A/B over plans must match on CONTENT and read the index off the matched plan —
the first comparison run for this report was invalid for exactly this reason.
