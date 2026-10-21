# Vampires: which decisions cost the search, and the USER's play rules (2026-10-21)

Branch `vampires-axis` (from `1a1cf9b9e`). Scratch `build/scratch/vampires/axis/`. Part of the round-2 search-cost
work (`docs/design/vampires-search-cost-2.md`); the USER asked: *"Can you work out which decisions are slowing us
down? Is it cards like Stromkirk Condemned choosing which to discard?"*

## 1. The instrument: `MTG_VAMP_AXIS_CENSUS` (diagnostic, default off, byte-identical)

`vampaxis` in `src/ai/TurnSolver.cpp`, hooked at the three plan loops that charge a unit per candidate
(`SolveWithLookahead`'s candidate loop, `FullSearchLine`'s pre-combat loop and its second-main loop). Every
candidate is keyed once per AXIS with that axis's choice masked out. A candidate whose masked key an earlier sibling
of the same pass already produced exists only because that axis branches; its INCLUSIVE units (`gamework::t_used`
over its whole subtree) are charged to the axis, once, at the outermost such variant. So each row is an upper bound
on what folding that one axis to a single option could save. Rows overlap and do not add.

Axes: the discarded card's name (all outlets, and per outlet), the madness answer, Falkenrath Pit Fighter's victim,
the breakpoint arm, the land (which land / its timing / play vs hold), the action order, Aether Vial vs a hand cast,
and the whole Stromkirk Condemned / Pit Fighter / Blood activation. Split by real game vs mulligan-bottoming trial
game (`turncensus::t_probe`) and by searched vs inside a playout (`g_rollout_nest`). Plus a per-(outlet, discarded
card, madness answer) table. Digests and units equal the baseline with it on (checked on all four rows).

## 2. The answer (60-card list, the USER's rules OFF; % of ALL units)

| decision | 2HG (d3 x100 + d5 x50) | std (d5 s2002 x75 + d5 s3003 x75) |
|---|---|---|
| **land drop, all of it** | **63.7%** | **76.8%** |
| -- which land | 30.7% | 52.0% |
| -- before / after the activations (Bloodghast) | 1.4% | 1.0% |
| Pit Fighter: which card it discards | 23.7% | 10.1% |
| Pit Fighter: which Vampire it sacrifices | 18.9% | 6.5% |
| Aether Vial vs a hand cast | 19.4% | 15.5% |
| Stromkirk Condemned: which card it discards | 10.2% | 11.1% |
| breakpoint arm | 6.4% | 2.4% |
| madness answer (cast / decline) | 3.3% | 2.4% |
| Blood: which card it discards | 1.7% | 1.5% |
| action order, exact duplicates | < 1% | < 1% |

- 64-71% of units are mulligan-bottoming trial games (no keep table), the rest the real game; the shape is the same
  in both. Most variants sit at SEARCHED nodes; their subtrees hold the playouts.
- So Condemned's discard choice is a real but secondary cost (~10%). Pit Fighter's discard + victim choice is larger
  in 2HG. The land drop is the dominant multiplier, and over half of it is play-vs-hold (or defer to main 2), not
  which land. (A direct measurement of the hold class is pending; the split above is by subtraction.)

## 3. The USER's rules (2026-10-21) and their A/B

All three are restrictions in `VampiresProvider` (through the new generic hook
`DecisionProvider::RestrictsCollectedActions` / `CollectedActionAllowed`, applied at the tail of `CollectActions`,
every world but human play, and `ActivationDiscardCandidates`). Default ON; each `=0` restores the old menu.

- `MTG_VAMP_FREE_DISCARD` -- *"when discarding the cards with madness that we can afford to cast for that cost are
  the ideal. If the cost is the same or less than the casting cost there is no reason not to discard them"* and
  *"Bloodghast is another very good choice for discards, since it comes back from the graveyard whenever a land
  drops."* With a free discard in hand, the outlet discard pins are only the free names. (Socialite before combat
  is not free: its madness cast would be a main-1 cast.)
- `MTG_VAMP_NO_STEAL` -- *"Heuristically it isn't worthwhile to use Captivating Vampire's ability."*
- `MTG_VAMP_SOCIALITE_M2` -- *"Vampire Socialite should always be cast in main 2, so it can benefit from the damage
  dealt."* No pre-combat hand cast or Vial put.
- The list is 60 cards: *"is this list 61 cards? If so, let's drop Mavren Fein."*

A/B 1 (one pooled batch, 11,300 games, scratch `ab1/`): the seven regression rows plus held-out blocks A = 16,100,001
and B = 16,400,001 (std d3 b10 x400, std d5 b20 x200, 2HG d3 b10 x150, 2HG d5 b20 x80 per block), every arm paired
per game against all-off. Net = sum of per-game win-turn differences (positive = slower).

| arm | held-out units | faster / slower | net |
|---|---|---|---|
| all three | 0.856x | 4 / 8 | +4 |
| discard rule | 0.888x | 1 / 8 | +7 |
| no steal | 0.994x | 0 / 0 | 0 |
| Socialite main 2 | 0.969x | 3 / 0 | -3 |

### 3.1 The discard rule's slower games, root-caused (scratch `rc/`, `rc/deals.py`)

Eight games, six deals, every one T4 -> T5:

1. **Bloodghast with no land to return it** (std A gi88). The T4 kill casts Bloodghast ({B}{B}) and has Condemned
   discard an uncastable Bloodline Keeper. Under the rule Bloodghast is free, so Condemned may only discard
   Bloodghast; no land drops that turn, so it stays in the graveyard and an attacker is lost.
2. **Two outlets, one free card** (std B gi75). The kill pumps with BOTH Condemneds, discarding Vampire of the Dire
   Moon and Asylum Visitor. Under the rule Asylum Visitor (madness {1}{B}, payable) is the only free card, and each
   Condemned's pins are computed from the same pre-plan hand, so both are restricted to it and the double pump is gone.
3. The rest diverge on turns 1-2, through the search's evaluation: the restriction also applies inside the
   lookahead's playouts, so the value of an early line moves. Their T4 lines themselves have an equal-or-better twin
   (discard the madness card and cast it with madness instead of hard-casting it and discarding something else).

Proposed amendments (DEFAULT OFF, for the USER), measured in A/B 2:
- `MTG_VAMP_FREE_DISCARD_V2`: Bloodghast is free only when a land drop is open and a land is in hand; and no
  restriction when the discard outlets on board and in hand outnumber the free discards.
- `MTG_VAMP_FREE_DISCARD_DEAD`: a nonland card none of whose costs fits this turn's mana is a free discard too.

A/B 2: pending.
