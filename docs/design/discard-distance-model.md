# A shared distance-to-castable model for the cleanup-discard buckets

Status: IN PROGRESS (2026-10-10). Lever: `MTG_DISCARD_DIST_MODEL` (heurarm `DISCARD_DIST_MODEL`),
default ON; `=0` restores each deck's previous distance proxy so the model is A/B-measured, not assumed.

## Why

The USER's bucket doctrine (2026-08-21) orders an over-full bucket by distance to playable: *"Dropping
a Progenitus for example may still make sense in this case, if it is nowhere near playable."* Minotaur
adopted that as **EV = P(play) x value** with a fixed table of P(play) by how many mana sources a card is
short: 1.0 / 0.70 / 0.35 / 0.10 / 0.03 for 0..4 short. The 17 designs the USER settled on 2026-10-09 say
the same ("within a bucket, distance to castable orders the cards"), and their implementations used
Minotaur's table (Giants, Goblins), a binary "castable within 2 turns" (SelesnyaLifegain), "5-drops last"
(Angels, CritterLifegain), or nothing (Auras). Reviewing them, the USER said (2026-10-10): *"For Distance
to castable I think it makes sense to have a smarter model if we can."*

What the fixed table cannot see:

1. **Land drops already in hand.** A hand holding three lands is three mana closer in three turns with
   certainty; the table prices "three short" at 0.10 regardless.
2. **The library.** With no land in hand the next land comes off the top at the library's land density,
   which differs by deck (17 vs 24 lands) and drifts over a game.
3. **Colours.** A card with enough mana but one white source short of WW reads as "0 short".
4. **Time.** Castable next turn and castable in three turns are both "reachable"; a goldfish deck that
   closes on turn 4-6 values them very differently.

## The model

At the cleanup discard it is the end of our turn. For each of our next `H = 4` turns `t = 1..H`, we draw
one card and may play one land.

* **Board mana `B`** (next turn): every mana source we control -- lands (their `produces_amount`),
  mana rocks, mana creatures (summoning sickness is gone by next turn) -- and, per colour, the number of
  sources that can make it (an any-colour source counts for every colour).
* **Kept hand lands**: the bucket rule passes the lands it keeps. They are played one per turn, best
  first for the query (lands that supply a missing colour, then the most mana).
* **Library census** (counts only, never order -- the same non-clairvoyance standard as
  `DiscardProtectScope::LastInDeck`): `N` cards, `L` lands, `L_c` lands that can make colour `c`.
* **Drawn land drops** by turn `t`: once the kept hand lands run out, a land drop needs a drawn land.
  `D_t ~ Hypergeometric(N, L, t)` lands are drawn by turn `t`; the drops they fund are
  `min(t - h_t, D_t)` (one drop per turn).
* **Mana by turn `t`** = `B` + kept-land mana played by `t` + drawn drops + any per-turn extra mana the
  caller supplies (a dork it will cast, a cost reducer already folded into the cost, ...).
* **Colour by turn `t`**: for each colour the card needs `p_c` pips of, sources by `t` = board sources +
  kept colour lands played by `t` + drawn colour lands (`Hypergeometric(N, L_c, t)`) + any per-turn
  coloured sources the caller supplies with its extra mana (`ColourCredit`; Angels' Giada makes {W}).
* **Restricted mana** (a source that pays only some spells -- Giada: Angel spells) is the caller's to
  net out: the census counts every mana permanent, so a caller whose hand mixes payable and unpayable
  cards removes the restricted board sources from the census for the cards they cannot pay.
* **`P_by(t)`** = P(mana by `t` >= cost) x product over colours of P(colour sources by `t` >= `p_c`),
  forced monotone in `t`. (Mana and colour share land drops; treating them as independent is the
  stated approximation.)
* **Castability factor** = sum over `t` of (`P_by(t)` - `P_by(t-1)`) x `gamma^(t-1)`, `gamma = 0.75` per
  turn of delay; a card not castable within `H` turns keeps a floor of `0.02 x gamma^H` so the stranded
  still order by value. The deck combines it with its own value: **EV = factor x value**. Where the
  deck's value is an authored ORDER (Angels, CritterLifegain, Giants), value = 0.85^position, so one
  place in the order is worth about half a turn of delay. USER 2026-10-10: *"That is an empirical value.
  I don't know the answer myself ... testing it also won't give us an easy value because discards are
  so rare. I guess we can stick with it if you think the number seems reasonable."* -- kept.
* **Duplicates**: the k-th copy in hand pays for the copies ahead of it in hand (Minotaur's cumulative
  rule); a copy already in play is paid for and adds nothing.

Determinism and cost: the hypergeometric terms are products of small integer ratios evaluated in a
fixed order (no `std::pow` / `lgamma`, which are not bit-identical across libms -- the Windows parity
rule). The census is one pass over the library per call; each card query is `O(H x colours)`.

For calibration against the old table: a card one source short with no land in hand, in a 40% land
library, gets roughly 0.40 + 0.24 x 0.75 + 0.14 x 0.56 + 0.09 x 0.42 = 0.70 (table: 0.70); with a land in hand it
gets 1.0 (table: 0.70); three short with three lands in hand gets 0.56 (table: 0.10).

## Where it applies

Only where a deck's design orders a bucket by distance and its implementation used a proxy, and never
over a USER rule that fixes an order explicitly:

| Deck | Previous proxy (`=0`) | With the model |
|---|---|---|
| Angels | value order after the USER tiers (variety, cheap/payoff mix, 5-drops first) | EV within those tiers |
| CritterLifegain | value order after variety / 4-5 drops last | EV within those tiers |
| Auras | live power per mana | rate x castability factor |
| Giants | Minotaur's table x value | model x value |
| Goblins | Minotaur's table x value | model x value |
| SelesnyaLifegain | "reachable within 2 turns" + distance capped at 3 | model factor (cheapest effective payoffs first when mana is poor, USER 2026-10-10) |

Not changed: decks whose distance notion is already exact over the kept hand (Knights / WhiteKnights /
slivers_vial curve fill, Fluctuator reach, Melira Pod and Creature Giving castable-next-turn tests,
Fungus's 5-mana threshold, Dragonstorm's 9-mana projection, burn). Minotaur keeps its adopted table;
the model can be offered to it later as its own A/B arm.
