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

## THE SIMPLIFICATION — RULED CORRECT BY THE USER, 2026-09-17

"Any number" is modelled as **ALL creature cards in hand**.

**USER RULING (2026-09-17):** *"The Ghalta simplification is fine. Put all is correct in this
environment."* This closes what had been written up here as an unpaid debt. The subset chooser
described below is **not** outstanding work; it is a Phase-2 item, live only if this engine ever grows
an opponent that can punish an empty hand.

*Why it is correct here:* against a passive opponent that never blocks, removes or sweeps, an extra
body is never a liability. The one card that could punish a free deploy — Terastodon, whose live mode
in this sim destroys OUR OWN noncreature permanents — picks its K at resolution and may pick 0. So
"all" is not an approximation of the choice, it **is** the choice: every other subset is weakly
dominated, and a chooser offered to a human here would be a menu with one correct entry.

### Keep these two statements apart — they look contradictory and are not

1. **Within this environment, put-all is optimal play** (the ruling above). There is no decision being
   narrowed, because there is no live alternative.
2. **The environment itself prices all-in deployment too generously.** No sweeper exists, so emptying
   your hand costs nothing here and something real in a constructed game.

(2) is not a defect in Ghalta's implementation — it is the same passive-opponent property that makes
(1) true, seen from the other side. It is the mirror of the Terastodon problem: there the sim cannot
see a card's upside; here it cannot see an archetype's downside. **It therefore does not license a
correction to Ghalta's code, but it does bear on how far to trust a Ghalta-vs-Hulk margin** — see the
Hulk section below, and treat any measured Ghalta win as an upper bound on its constructed value.

The user reached (2) independently from the strategy side, arguing a creature is the better card to
hold in a Hulk slot — *"you could choose not to deploy it and hold it in case there is a sweeper"*,
whereas *"the Hulk would have nothing to drop"* — and then ruled, correctly, that the sim should not
try to model a risk its opponent cannot pose.

**Deliberately NOT a searched subset axis, and this part stands regardless of the ruling.**
`2^|creatures in hand|` plan variants is precisely the multiplicative explosion removed from Turntimber
the same night (`turntimber-multi-copy-plan-explosion.md`). Searching a set of weakly-dominated
options would buy nothing and cost the plan space.

### If a real opponent ever lands (Phase 2 only)

Offer the subset at the HUMAN-play path (the `g_play_dig_chooser` / Turntimber put precedent:
autonomous keeps the dominant "all", the viewer gets the real decision). Nothing to do until then —
against an opponent that cannot sweep, that chooser has no second option worth showing.

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

* Whether Ghalta's slot comes from the 2nd Craterhoof (list G) or the 3rd Hulk (list H): both tables
  generating 2026-09-17, to be compared each under its own table.
* Whether Ghalta wants a 2nd copy (untested; it is Legendary, so the second is insurance against
  stranding-of-the-stranding-fix, not a second effect).

## Audit note: G and H record DIFFERENT commit strings and are still comparable

The two keep tables fingerprint themselves differently:

| list | recorded `commit` | gen window |
|---|---|---|
| H | `7d1057f6` | 01:08:48 → 05:06:46 UTC |
| G | `eb29dfc4+dirty` | 05:06:50 → … |

**This is cosmetic. Proof the play logic is identical across both:**

* `git rev-parse eb29dfc4:src` == `git rev-parse 7d1057f6:src` == `a7d6a2930c0f63b8448c54d3b08c28f998839f52`.
  `eb29dfc4` is a DOCS-ONLY commit, so the src tree never moved.
* `git status --porcelain -- src/` was empty at G's launch; the `+dirty` came solely from an
  uncommitted edit to this very file.
* `build/Release/mtg-analyze` mtime `01:04`, predating BOTH runs — one binary, never rebuilt.
* `src/cards/data/cards.json` mtime `00:57`, predating both. Ghalta's bracket-note rewrite was
  deliberately HELD until G finished precisely so this stayed true.

Do not read the differing commit strings as an engine skew. The freeze keys on `HEAD:src`, and that
is what held.
