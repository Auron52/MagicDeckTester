# Karoo tap-in-response: a creature-only land floats its plain {C} (2026-11-17)

Lever: `MTG_KAROO_FLOAT_RESTRICTED_C` (heurarm slot `KAROO_FLOAT_RESTRICTED_C`), DEFAULT ON; `=0` restores the
old behaviour byte-for-byte.

## The defect

Minotaur mana-clean gate `codex-clean-refresh-two-20261117`, seed `30284922` T5, plan
`{no land + cast:Slaughter-Priest of Mogis}`: APPLY-DROP x2. Rakdos Carnarium #45 pays the Priest's `{B}{R}`. The
breakpoint continuation plays Rakdos Carnarium #46, whose bounce cannot take the other Karoo and returns the
untapped Unclaimed Territory. `KarooBounceFloatable` refused every `colored_creature_only` land, so nothing was
floated, and the continuation's Deathbellow Raider `{1}{R}` had only Secluded Courtyard left. The oracle's route
taps the Territory first.

Unclaimed Territory, Secluded Courtyard, Cavern of Souls, Haven of the Spirit Dragon and Sliver Hive each have a
plain, unrestricted `{T}: Add {C}` besides their creature-only coloured mode. Holding priority while the bounce
trigger is on the stack, the controller may tap the land for that `{C}` (CR 605.3a, 106.4). Floating a COLOUR
would launder the spend restriction. That is why the old predicate refused these lands, but it refused the `{C}`
as well.

## The change

`FloatLandBeforeSacrifice` (the bounce's own float) asks `KarooBounceFloatable(..., plain_c=true)` for such a land
and floats exactly `{C}`: never a colour, and never a pinned colour. Every other caller keeps the old answer:
- the provider's bounce ranking, where a `{C}` float is not a no-loss bounce of a land whose coloured creature mana
  is still lost;
- the USER-rule candidate filters (Hinata, Landfall);
- the human-play Satyr float, which floats through `FloatSourceTapCommitted` and would pick a colour.

A first version widened `KarooBounceFloatable` itself. Its A/B moved Minotaur's bounce ranking (39 changed digests)
and the Landfall user rule (one slower game). It was replaced before commit.

## Measurement

Paired A/B, one pooled Release batch, both arms per job via the heurarm flag, games 500,000–504,999 of each deck
(`build/scratch/claude_karoo_c_ab/`):

| deck | earlier | later | changed digests | score new / old | work units |
|---|---|---|---|---|---|
| Dragons | 0 | 0 | 0 | 27,758 / 27,758 | +0.05% |
| Landfall Elementals | 1 | 0 | 4 | 21,648 / 21,649 | +0.02% |
| Minotaur | 0 | 0 | 1 | 24,280 / 24,280 | +4.02% |
| Spirits | 0 | 0 | 0 | 23,171 / 23,171 | +0.17% |

No slower game. Minotaur's work rises on 4,999 byte-identical games: rollouts that bounce a creature-type land
now hold its `{C}`, so more simulated lines are payable. That is the corrected mana model, not a new branch.
The exact seed replay has zero oracle issues (lever off: APPLY-DROP=2).

Tests: `test/unit/test_karoo_tap_in_response.cpp` (Territory floats `{C}` and nothing else; lever off floats
nothing). `test_karoo_bounce_search.cpp`'s Dragons s4004 gi47 case now expects Haven's pinned bounce to float
`{C}` (it asserted the old "nothing floats").

## Not changed (open for the USER)

Painlands (`tap_self_damage > 0`) are refused by the same predicate although their `{C}` mode is painless. That is
the same shape, but a wider play change across more decks, so it is not made here.
