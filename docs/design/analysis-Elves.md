# Analysis ledger -- Elves

Deck: `decks/Elves/Elves.cod` (60 cards, mono-G). Started 2026-11-26.
Work tree: `.claude/worktrees/elves` on branch `wip/elves-analysis` (base a825e81f4).

Plan of the deck: elf mana (Llanowar / Mystic / Priest of Titania / Elvish Archdruid / Marwyn)
into a wide board of stacked lords (Clancaller, Galadhrim Brigade + squad copies, Leaf-Crowned
Visionary, Joraga Warcaller multikicked), Concordant Crossroads for haste (dorks tap the turn they
land), Genesis Wave as the big-mana refill, Ezuri / Allosaurus Shepherd / Immaculate Magistrate as
the mana-sink finishers.

## Stage 1 -- coverage (2026-11-26)

Missing (8): Immaculate Magistrate, Joraga Warcaller, Allosaurus Shepherd, Taunting Elf,
Elvish Clancaller, Galadhrim Brigade, Leaf-Crowned Visionary, Marwyn, the Nurturer.
Already implemented (full): Llanowar Elves, Elvish Mystic, Elvish Archdruid, Priest of Titania,
Genesis Wave, Concordant Crossroads, Ezuri, Wirewood Lodge, Cavern of Souls, Forest.
No fetchlands (fetch-hold review not required). No sideboard.

Existing-card issues found at Stage 1:
* **Concordant Crossroads -- World rule (CR 704.5m) NOT modelled**, and its own bracket says the
  invariant "GOES LIVE AT TWO OR MORE COPIES". This list runs **2**. Tier 2/3 gap: implement the
  world SBA (all but the most recently entered World permanent go to the graveyard).
* Cavern of Souls' "choose a creature type" is simplified to "any creature"; exact here, since
  every creature in the list is an Elf.

| card | Scryfall cost | type | proposed tier | notes |
|---|---|---|---|---|
| Immaculate Magistrate | {3}{G} | Elf Shaman 2/2 | 2/3 | {T}: +1/+1 counter on TARGET creature for each Elf you control (target = viewer `target` decision) |
| Joraga Warcaller | {G} | Elf Warrior 1/1 | 3 | MULTIKICKER {1}{G}: enters with N +1/+1 counters; other Elf creatures +1/+1 per counter on it. Kick count = cast axis (like X) |
| Allosaurus Shepherd | {G} | Elf Shaman 1/1 | 2/3 | uncounterable x2 (inert: passive opp); {4}{G}{G}: Elf creatures you control base P/T 5/5 (layer 7b) + Dinosaur until EOT |
| Taunting Elf | {G} | Elf 0/1 | 1 | lure: "all creatures able to block it do so" -- inert (opponent never blocks); PROVISIONAL deferral |
| Elvish Clancaller | {G}{G} | Elf Druid 1/1 | 2/3 | lord (other Elves +1/+1); {4}{G}{G},{T}: search for a card NAMED Elvish Clancaller -> battlefield, shuffle |
| Galadhrim Brigade | {2}{G} | Elf Soldier 2/2 | 3 | SQUAD {1}{G}: N token copies on ETB, each a lord (other Elves +1/+1) |
| Leaf-Crowned Visionary | {G}{G} | Elf Druid 1/1 | 2/3 | lord; whenever you cast an Elf spell, MAY pay {G}: draw (optional payment = decision) |
| Marwyn, the Nurturer | {2}{G} | Legendary Elf Druid 1/1 | 2/3 | +1/+1 counter when another Elf you control enters; {T}: add {G} x Marwyn's power |

## Stage 2 -- implementation

(integrator fills in)

## Viewer classification (2c-ter)

(integrator fills in)

## Stage 5 verification verdicts

(pending)

## Approved deferrals

(none yet -- Taunting Elf lure is PROVISIONAL pending user sign-off)
