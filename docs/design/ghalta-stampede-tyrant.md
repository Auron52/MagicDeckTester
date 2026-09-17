# Ghalta, Stampede Tyrant — implementation, the "put ALL" simplification, and what it costs

Implemented 2026-09-17 at the user's request: *"I think we need Ghalta here. It solves my Worldspine
Wurm cut concerns as well."*

`{5}{G}{G}{G}` Legendary Creature — Elder Dinosaur, 12/12, Trample.
**When Ghalta enters, put any number of creature cards from your hand onto the battlefield.**

## Why it was added

The whole StompySurprise screening line ran into **stranding**: a fatty drawn into hand stops being a
library-tutor target, and at 8–11 mana is often uncastable. The user's own framing of the cost of
cutting Worldspine to 1: *"the risk is that it ends up in hand with no hulk and lacking the 11 mana to
play it."*

Ghalta is the general answer. It does not merely insure against a stranded fatty the way a second copy
does — it **converts the stranded copy into the payoff**. Measured in a 40-game probe on the first
build: one game deployed a stranded Craterhoof, another deployed Terastodon + Vaultborn Tyrant +
Worldspine Wurm in a single ETB.

## Two-phase resolution (load-bearing, not tidiness)

Every chosen card enters SIMULTANEOUSLY; the resulting ETB triggers only then go on the stack
(CR 603.3b). The implementation therefore pushes all permanents first and fires their triggers in a
second pass. One-at-a-time would make a deployed **Craterhoof's X depend on hand order** — the
difference between a lethal alpha strike and a damp one.

## THE SIMPLIFICATION, AND IT IS NOT FREE

"Any number" is modelled as **ALL creature cards in hand**.

*Why it is defensible here:* against a passive opponent that never blocks, removes or sweeps, an extra
body is never a liability. The one card that could punish a free deploy — Terastodon, whose live mode
in this sim destroys OUR OWN noncreature permanents — picks its K at resolution and may pick 0. So in
the goldfish, "all" is weakly dominant and the arithmetic is not distorted.

*What it costs, and the USER found this independently from the strategy side.* Discussing whether
Ghalta should displace a World War Hulk, the user argued a creature is the better card to hold in that
slot: *"you could choose not to deploy it and hold it in case there is a sweeper"*, whereas
*"the Hulk would have nothing to drop."* **That decision does not exist in this model.** A creature you
mean to hold back is force-deployed by your own Ghalta. Consequences:

1. **The sim OVERSTATES Ghalta.** Emptying your hand is exactly what a sweeper punishes, and no
   sweeper exists here. Any measured Ghalta win is an UPPER bound on its real-game value.
2. **It narrows a legal choice for HUMAN play**, which this repo forbids outright (the rule that keeps
   the Jitte's pruned modes and the Turntimber put open to a human). **This is a known, unpaid debt.**
3. It is the mirror of the Terastodon problem: there the sim cannot see a card's upside; here it
   cannot see a card's downside.

**Deliberately NOT a searched subset axis.** `2^|creatures in hand|` plan variants is precisely the
multiplicative explosion removed from Turntimber the same night
(`turntimber-multi-copy-plan-explosion.md`). The fix is a human chooser, not a search fan.

### The outstanding fix

Offer the subset at the HUMAN-play path only (the `g_play_dig_chooser` / Turntimber put precedent:
autonomous keeps the dominant "all", the viewer gets the real decision). Held back on 2026-09-17
because two keep tables (lists G and H) were mid-generation and **rebuilding between them would fit
the two tables under different play logic** — the exact skew Rule 0 of `mulligan-profile.md` exists to
prevent.

## Ranking: the card had to be taught, not just implemented

USER: *"we should also update our heuristics for this deck. We may be undercounting the effectiveness
of the new cards like Apex Altisaur and now Ghalta."* Correct, and specifically at the Natural Order
fetch. The deck's NO doctrine classified targets by `etb_team_pump_per_creature` / token count /
POWER, so Ghalta (power 12) landed in the `>= 10` class — **indistinguishable from Worldspine Wurm**,
fetched as a plain big body.

That is blindest exactly where Ghalta is strongest: a team pump stranded in hand. Fetch Ghalta, it
deploys the Hoof, and the Hoof's X then counts Ghalta *and* everything else deployed — strictly bigger
than fetching the Hoof directly, which only pumps the board already present. Now ranked by what is
stranded in hand (`MTG_STOMPY_GHALTA_RANK`, default ON): pump in hand → above Craterhoof; other
creatures in hand → with the biggest bodies; empty hand → just a 12/12.

Verified blast radius: on a list WITHOUT Ghalta both flag settings are byte-identical; on one WITH it
they differ.

## The Hulk interaction — asymmetric, and the sim sees only half of it

World War Hulk is a Saga whose chapter I reads *"The next red or green creature spell you cast this
turn can be cast without paying its mana cost."*

* **Hulk → Ghalta is a strong line.** Ghalta is a green creature spell at 8 mana; chapter I casts it
  free, and it then deploys the rest of the hand. Hulk's own chapter-I candidate scoring performs the
  put **on a copy of the state**, so the search finds this by measurement rather than needing a hint.
* **Ghalta → Hulk wastes chapter I.** Ghalta already emptied the hand of creatures, so there is
  nothing to free-cast (II and III still pump). The user: the bad case is a Natural-Ordered Ghalta,
  where *"Hulk doesn't have a follow up"* — though *"you take the opponent down easily at that point"*,
  so the goldfish impact may be near zero.
* **The real argument is the one the sim cannot price:** Ghalta is ALL-IN, Hulk is INCREMENTAL. Only
  *"in a real game, where you could get swept, would Hulk be clearly worse"* (user).

## Open

* The human subset chooser (above) — the only known rules-narrowing this card introduces.
* Whether Ghalta's slot comes from the 2nd Craterhoof (list G) or the 3rd Hulk (list H): both tables
  generating 2026-09-17, to be compared each under its own table.
* Whether Ghalta wants a 2nd copy (untested; it is Legendary, so the second is insurance against
  stranding-of-the-stranding-fix, not a second effect).
