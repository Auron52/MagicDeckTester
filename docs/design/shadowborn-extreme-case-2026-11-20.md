# Shadowborn Apostle: the USER's "extreme case" line, 2026-11-20

Status: diagnosis only. No play change. Branch `wip/sb-extreme-case` (fixtures + unit tests).

## The question

The USER (2026-11-20): *"In the worst case you might have just 1 swamp out and Dark ritual x 2, Shadowborn x 2,
Thrumming stone and Culling the Weak in your hand. In theory it should be possible to Dark Ritual x2 Shadowborn +
Culling Thrumming Stone, Shadowborn to go off. I'm not sure if our order handles that, but this is the extreme case.
You cannot cast the Shadowborn first in this example."*

Mana, from `cards.json` (Swamp `{T}: B`; Dark Ritual `{B}` -> `BBB`; Apostle `{B}`; Culling the Weak `{B}` + sacrifice
a creature -> `BBBB`; Thrumming Stone `{5}`; the activation `{B}` + six Apostles):

    Swamp 1 -> Ritual 3 -> Ritual 5 -> Apostle 4 -> Culling (eats it) 7 -> Stone 2 -> Apostle 1 (+ ripple) -> activation 0

Casting the Apostle first spends the only `{B}` and strands both Rituals. Any order with Apostle somewhere before Culling,
the Stone after Culling, and a Ritual before the first Apostle pays (e.g. Ritual, Apostle, Culling, Ritual, Stone,
Apostle also ends on 1).

## Answer: the engine finds it, at every setting tried

Fixtures (`test/scenarios/`): one Swamp, no land in hand, the six cards, T3 `resume_at: main1`, `max_turns` 8.
* `shadowborn_extreme_user_line.json` (d8 b0) and `..._d5b20.json`: GOING-OFF library, six Apostles on top, then Overlord,
  Reaper, Jetmir, Sire, filler Swords to Plowshares.
* `shadowborn_extreme_control_no_apostles.json`: the same, with no Apostle in the library.
* `shadowborn_extreme_user_line_human.json`: the USER's line as a human line. It realises with nothing dropped.

| library | d3 b10 | d5 b20 | d3 b0 | d5 b0 | d8 b0 |
|---|---|---|---|---|---|
| going off | USER line + activation, win T6 | same | same | same | same |
| control | 1 Apostle, Rituals held, win T7 | same | | | same |

The line played in every going-off cell, all in **main 2**:
`Dark Ritual > Dark Ritual > Shadowborn Apostle > Culling the Weak (eats it) > Thrumming Stone > Shadowborn Apostle`.
The ripple chain casts six free Apostles. Then `ABILITY Shadowborn Apostle` fetches Abhorrent Overlord.

How the order arises (`ShadowbornProvider`, `src/ai/DecisionProviders.cpp`):
1. `CastOrderRank`: Ritual 1, Ritual 1, Culling 2, Stone 3, Apostle 4, Apostle 4.
2. `ArrangeCastOrder`, the victim pass: Culling's `trick_hand_target` is the hand Apostle, so the first Apostle cast moves
   to just before Culling. Result: R R A C S A.
3. `SbRitualAfterStoneIfPaid` (MTG_SB_RITUAL_AFTER_STONE) tries R A C S R A. The exact walk fails at the moved Ritual
   (0 mana after the Stone), so nothing changes.
4. The Stone-visible Apostle-before-Ritual moves never apply here, because the Stone is in hand. (0984fe0a5 on
   `wip/sb-ritual-first` deletes them anyway.)

The unit test `SB extreme case: the rank order + ArrangeCastOrder realise ...` (test_shadowborn_mainphase.cpp) pins
the arrangement. It also checks that the USER's order pays under `OrderPaysSequentially` and that Apostle-first does not.
`SB extreme case: the enumerator offers the six-cast line` prints the plan census for both mains and both libraries:

| library | main 1 | main 2 | of which cast all six | arranged order of every six-cast plan |
|---|---|---|---|---|
| going off | 1 (the fold) | 148 | 24: 8 with no activation, 8 with `act -> Overlord`, 8 with `act -> Jetmir` | R R A C S A, pays |
| control | 1 | 64 | 8, no activation | R R A C S A, pays |

Each block of 8 is one cast set repeated across its variants. The going-off library also offers the activation
variants because the enumerator credits the Apostles the ripple chain will reveal.

Main 2 rather than main 1: the main-1 rule (`Main1PlanAdmitted`) folds every main-1 plan to its main-2 twin when no
creature is attack-ready. This is a sound fold, and in this position the outcome is the same.

### Two caveats

* **Fixture horizon artefact.** With `max_turns` 3 (the game ends after T3), the b0 cells cast **nothing**, and the
  budgeted cells still played the line. With no future turn, every T3 line ties at an unlimited budget. This is not a
  play defect. The fixtures use `max_turns` 8.
* **The choice reads the library.** In the control, the same hand casts one Apostle and keeps both Rituals. In the
  going-off case it plays the whole line. So the search decides from the real library order (the ripple outcome),
  not from the hand alone. This is the goldfish search's usual relationship to its own library and nothing specific
  to this line. It does mean the position does not test what a non-clairvoyant player would do.

## Where the order puts an Apostle somewhere OTHER than right before its consumer (rules after 0984fe0a5)

* **A non-victim Apostle sits at rank 4**: after Rituals (1), Culling (2) and the Stone (3), and before the Demons and
  tutors. It is cast after the Stone so that its cast ripples. Its "consumer", the six-Apostle activation, is not a cast.
* **Diabolic Intent's hand victim stays at rank 4.** The victim pass only moves a victim that sits AFTER its spell, and
  Intent (rank 10) is already after it. So Reaper, Sire, Overlord or Demonic Tutor (5 to 9) can come between the Apostle
  and Intent. The reason is the same ripple-after-Stone placement; the pass leaves a spell that already follows its
  victim alone.
* **Two sacrifice spells on hand Apostles (Culling + Intent):** only the FIRST Apostle cast by name moves to just before
  Culling. Intent's body is whatever Apostle is on the battlefield at its cast (the victim is re-picked on the live
  board): the rank-4 copy cast after the Stone, or a ripple copy.
* **Culling with a battlefield victim** (an Apostle or Harpy already out; `SacCostVictimVariant`'s one-variant prune):
  the hand Apostle has no cast consumer and stays at rank 4.
* **Generic passes after ArrangeCastOrder** (range ladder, enabler-wipe recheck, `ApplyPayableCastOrder` /
  MTG_PAYABLE_ORDER, then the attach/sacrifice dependency pass): when the arranged order does not pay, they can
  re-sequence the line, including the Apostle. That is payability, not deck policy.
* **Not exceptions:** Ritual-after-Stone moves a Ritual from ahead of the Apostle-Culling pair to after the Stone, so the
  pair stays adjacent. Ripple free casts are not ordered at all: they are cast from the reveal.
* **Removed by 0984fe0a5:** the Stone-visible Apostle-before-Ritual moves (MTG_SB_APOSTLE_BEFORE_RITUAL_INTENT,
  MTG_SB_APOSTLE_FODDER_CLAIMS). With them gone, a plain Ritual always keeps rank 1 with the Stone out. The unit test
  `SB order: Stone out, Ritual + Apostle with no sacrifice claim -> the Ritual goes first` covers 1 and 2 Swamps.

This matches the USER's rule (*"I would put the shadowborn cast right before the thing that needs it. ... Dark Ritual
... doesn't need a shadowborn."*) for Culling. The Intent case is the one place where an Apostle that a cast consumes
does not sit right before that cast. Rank 4 places it after the Stone and ahead of the Demons and tutors that rank
between them.

## Unrelated pre-existing failure seen while testing

`INT-9 free casts: a revealed Thrumming Stone is cast and the legend rule keeps the copy the cast names`
(test_shadowborn_int9.cpp:184, `REQUIRE(casts.size() == 2)`) fails on origin/phase-1-2-deck-analyzer 8fbbc5023. It also
fails when run alone, and this branch changes no src.

## No fix proposed

The engine reaches the USER's order through the rank table and the victim pass, at play settings and at d8 b0. Nothing
prunes it.
