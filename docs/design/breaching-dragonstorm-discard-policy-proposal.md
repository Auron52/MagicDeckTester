# BreachingDragonstorm — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored from `docs/design/discard-bucket-authoring-brief.md` (the 2026-09-23 per-deck sweep).
**This document is the deliverable; no `.cpp`/`.h` was touched.** Integration is central, as
`BreachingDragonstormProvider::CleanupDiscardCandidates` behind a default-on
`EnvOn("MTG_BREACHING_BUCKET_DISCARD", true)`, `=0` restoring
`GenericProvider::CleanupDiscardCandidates`, returning a **shed order (most expendable first)**
routed through `CleanupDiscardRankingWithOrder(s, required_pieces, shed)`.

The provider already exists (`src/ai/DecisionProviders.h:290`) and this deck is already routed to it
(`src/ai/DecisionProviders.cpp:9900` on `etb_exile_until_nonland || demonstrate ||
self_bounce_on_etb_subtype=="Dragon"`, dispatched at `:10212`). So unlike Dragons and Minotaur this
policy does **not** promote a deck off `GenericProvider` — the class is there and empty, and this
would be its first behavioural override.

Every card fact below was read from `src/cards/data/cards.json` today (Rule 0); no card was recalled.

---

## 1. The deck, in one paragraph — and therefore its shape

37 lands / 23 spells, **no card draw at all**, and **nothing castable below mana value 5**. What it
needs to function is three things in order: (i) **five mana including exactly one red** by turn 3 —
which the deck engineers with twelve two-mana depletion lands and eight sac lands, so three land
cards can be five mana; (ii) an **MV-5 free-cast enabler** to cast with it — Breaching Dragonstorm
(`etb_exile_until_nonland`, free cast at `etb_exile_free_cast_max_mv` 8, which covers every card in
the deck) or Creative Technique (`shuffle_reveal_freecast` + `demonstrate`, i.e. **two** free casts,
uncapped); (iii) **payoffs in the LIBRARY** for the enabler to cheat in — six-to-eight-mana cascade
spells that each flip another free spell, so a single enabler turn chains into a board. Cascade
cannot fizzle here: the only nonlands under MV 6 are the eight enablers, so a `cascade_max_mv` 6
walk digs until it finds one and free-casts it, and *that* one flips anything. Haste (Boarding
Party's own, Maelstrom Wanderer's `grants_haste` + `affects_all_creatures`) converts the chain into a
kill the same turn. It works: regression GT is **3.09–3.12 avg win turn at d3/d5** (4.83 at greedy
d0) — a turn-3 deck.

That is a **COMBO** shape by the brief's table, so: **one bucket per combo part, similar effects
grouped** — MANA / ENABLER / PAYOFF — plus the mana sub-split. There is **no dig/cantrip bucket**,
because this deck's dig *is* its enabler (the walk is inside Dragonstorm and Creative Technique);
there is no separate cantrip to keep.

## 2. What the generic fallback does here — the honest version

`CleanupDiscardRanking` tier B (`src/core/SpellEffects.h:460`) is descending mana value over the
**whole** hand, lands included at MV 0, so they sort last. On this decklist:

    Maelstrom Wanderer (8) / Call Forth the Tempest (8) -> Annoyed Altisaur (7)
      -> Boarding Party (6) / Sakashima's Protege (6)
      -> Breaching Dragonstorm (5) / Creative Technique (5) -> every land (0)

**The brief warns that max-MV is backwards on a payoff deck. On THIS deck it is mostly not, and
saying otherwise would be the easy wrong answer.** The reason is structural and it is the single
most important fact in this document:

> **Every free cast in this deck comes out of the LIBRARY, never out of the hand.** Breaching
> Dragonstorm exiles from the top; Creative Technique reveals from the top; cascade exiles from the
> top. Nothing — no cost reducer, no "free-cast engine online" state — ever lowers the cost of a
> card sitting **in hand**. So a payoff in hand is worth exactly its **hard-cast** value, and an
> 8-mana three-colour payoff really is closer to dead than a 6-mana mono-red one.

Two corollaries worth stating because both are intuitive traps:

* **Holding a payoff does not "protect" it, and shedding one does not thin the library.** The card
  left the library when it was drawn; hand → graveyard changes no density. There is no
  keep-it-for-the-chain argument.
* **Nothing in this deck rewards an empty hand** — no `hand_size_anthem_max`, no madness, no discard
  outlet, no `discard_land_damage`. So unlike Minotaur (the Neheb inversion) a shed here is pure
  loss, and index 0 is simply "the least valuable card".

So the policy's job is **not** to invert max-MV. It is to fix the four places max-MV is wrong here:

| # | defect in the fallback | what it costs |
|---|---|---|
| (a) | **colour-blind.** This deck's scarce axis is colour, not count. Green exists on **4 cards** (Mossfire Valley) and they are `ramp_filter`s that need a feeder, so Annoyed Altisaur's `{G}{G}` needs **two of those four**; Call Forth's `{R}{R}{R}` at MV 8 is *easier* to satisfy than Altisaur's `{G}{G}` at MV 7. | max-MV sheds CFT/Wanderer before Altisaur; with no green in sight the right order is the reverse |
| (b) | **cannot see an engine-PRUNED card.** A second Maelstrom Wanderer while one is on the battlefield **cannot be cast at all**: `OfferDuplicateLegendCast` (`DecisionProviders.cpp:499`) returns false for a Legendary `VanillaCreature` with no enter/death watcher and no hand-size payoff — and this deck has neither. Its cascades never happen because the cast is never offered. | a provably-zero card ranked 1st-of-8 by accident of MV, and only by accident |
| (c) | **protects every land over every spell.** Reachable: "no land drop" is a real plan variant and condemns the lands in hand (`TurnSolver.cpp:3599`). | sheds Creative Technique — the deck's best card, learned marginal **+0.33** — before a surplus Mountain (learned **-0.13**) |
| (d) | **ties the two MV-6 payoffs by hand order.** Boarding Party is a 6/3 with HASTE whose cascade is a guaranteed enabler flip; Protege is a 3/1. | a coin-flip on a real difference |

That is the whole scope of the claim. It is (a)–(d), not a wholesale inversion, and section 9 says so
again when it sizes the expected effect.

## 3. Card-by-card role table

Every row's bucket is derived from the `params` named in it (Rule: classify by params, not names).

| Card | n | cost (MV) | `params` keyed on | Bucket |
|---|---|---|---|---|
| **Creative Technique** | 4 | `{4}{R}` (5) | `shuffle_reveal_freecast`, `demonstrate` | **ENABLER** (2 free casts: your demonstrate copy resolves first and walks its own reveal) |
| **Breaching Dragonstorm** | 4 | `{4}{R}` (5) | `etb_exile_until_nonland`, `etb_exile_free_cast_max_mv` 8, `self_bounce_on_etb_subtype` "Dragon" | **ENABLER** (1 free cast; the walked LANDS stay exiled = permanent thinning, ~1.6/trigger) |
| **Boarding Party** | 4 | `{5}{R}` (6) | `cascade_max_mv` 6, keyword Haste, 6/3 | **PAYOFF** |
| **Sakashima's Protege** | 2 | `{4}{U}{U}` (6) | `cascade_max_mv` 6, `enter_as_copy_of_entrant`, 3/1 (Flash inert) | **PAYOFF** |
| **Annoyed Altisaur** | 4 | `{5}{G}{G}` (7) | `cascade_max_mv` 7, 6/5 (Reach/Trample inert) | **PAYOFF** |
| **Maelstrom Wanderer** | 4 | `{5}{G}{U}{R}` (8) | `cascade_max_mv` 8, `cascade_count` 2, `grants_haste` + `affects_all_creatures`, 7/5, **Legendary** | **PAYOFF** |
| **Call Forth the Tempest** | 1 | `{5}{R}{R}{R}` (8) | `cascade_max_mv` 8, `cascade_count` 2, `damage_opp_creatures_mv_cast` | **PAYOFF** (no body) |
| **Mountain** | 9 | land | `produces` [R] | MANA — plain drop, red |
| **Dwarven Ruins** | 4 | land | `enters_tapped`, `produces` [R], `sac_for_mana_amount` 2 / `_color` R | MANA — red + one-shot burst |
| **Svyelunite Temple** | 4 | land | `enters_tapped`, `produces` [U], `sac_for_mana_amount` 2 / `_color` U | MANA — blue + one-shot burst |
| **Saprazzan Skerry** | 4 | land | `enters_tapped_with_depletion` 2, `produces` [U], `produces_amount` 2 | MANA — burst, blue (both of Protege's pips off one land) |
| **Peat Bog** | 4 | land | `enters_tapped_with_depletion` 2, `produces` [B], `produces_amount` 2 | MANA — burst, **generic-only** (no black pip in the deck) |
| **Remote Farm** | 4 | land | `enters_tapped_with_depletion` 2, `produces` [W], `produces_amount` 2 | MANA — burst, **generic-only** |
| **Ferrous Lake** | 4 | land | `ramp_filter`, `produces` [U,R] | MANA — filter (net +1, **needs a `{1}` feeder**), U **and** R at once |
| **Mossfire Valley** | 4 | land | `ramp_filter`, `produces` [R,G] | MANA — filter, **the deck's only green** |

**Ambiguous roles — flagged.** Only two:

* **Call Forth the Tempest** is a payoff with **no clock**. Its `damage_opp_creatures_mv_cast` clause
  hits the opponent's *creatures* only, and the card's own bracket note says plainly it "cannot
  change the CLOCK here". So it is a pure chain card (two cascades) that contributes zero damage
  itself — real value, at 8 mana, but not the reach card its text suggests. Kept in the payoff
  bucket, ranked low.
* **Dwarven Ruins / Svyelunite Temple** are lands *and* a one-shot two-mana burst
  (`sac_for_mana_amount` 2, colour-pinned). They are counted as standing 1-mana sources for the
  quota and as **+1 for one turn** in the distance test — see §5.

**No bucket for haste, and this is deliberate.** Haste is what turns the cheated-in bodies into a
turn-3 kill, so it looks like a combo part. It is not a *hand-side* one: the only grant is Maelstrom
Wanderer's, and a Wanderer in hand grants nothing until hard-cast at 8 mana in three colours. Haste
arrives from the **library**, like the rest of the payoffs, so a "keep 1 haste source" quota would
protect the least castable card in the deck for a benefit it cannot deliver. No reach bucket either
(see Call Forth above), and no dig bucket (§1).

## 4. The buckets, with quotas

### Bucket 1 — MANA. Quota = 5 usable mana, counting the battlefield first; colour before count.

Sub-split, in the brief's sense ("it splits up the ramp"): this deck has no dorks and no rocks, so
its acceleration lives *inside* the land bucket.

* **1a — COLOUR COVERAGE, and it comes first.** The quota's first unit is **one red source**. Both
  enablers and Boarding Party are `{4}{R}`/`{5}{R}`; **eight** of the 37 lands (4 Peat Bog, 4 Remote
  Farm) produce **B/W, which no card in this deck can spend on a pip**, and a further 8 (Skerry,
  Svyelunite) make only blue — so a hand of burst lands can hold six mana and cast nothing. A land
  that is the hand's only red source is the most protected card in the hand.
* **1b — BURST / RAMP** (`produces_amount >= 2`, or `sac_for_mana_amount >= 2`): Peat Bog, Remote
  Farm, Saprazzan Skerry, and the crack on Dwarven Ruins / Svyelunite Temple. These are how three
  land cards become five mana on turn 3, which is the whole clock. Their `enters_tapped` costs
  nothing here **because turns 1–2 are blank by construction** — the deck has no spell under MV 5.
  (This is why Treasure Hunt's measured rejection of "keep depletion lands as burst"
  (`DecisionProviders.cpp:3907`) does **not** transfer: that deck had 1–2 mana spells and a tapped
  land cost it a casting turn. Here there is no turn to lose.) The deck's own learned `card_scores`
  agree emphatically: Peat Bog **+0.243**, Remote Farm **+0.204**, Dwarven Ruins **+0.202** are the
  best lands, Mountain **-0.127** the worst card in the deck.
* **1c — PLAIN DROPS and LIVE FILTERS**: Mountain, and Ferrous Lake / Mossfire Valley **once a
  feeder exists**.

**FILTER-FEEDER CAVEAT — this deck's Karoo.** `ramp_filter` lands make **no mana alone**: Ferrous
Lake and Mossfire Valley are `{1}, {T}: Add {U}{R}` / `{R}{G}`, i.e. net +1 *given* another source.
A hand whose only red source is a Ferrous Lake, with no other land on board or in hand, has **no red
source at all**. The engine already has this exact predicate —
`ramp_filter_live = (lands_in_play + lands_in_hand) >= 2` (`DecisionProviders.cpp:4028`, with the
"FILTER LANDS ARE NOT UNCONDITIONAL COLOUR SOURCES" note above it) — so this is a reuse, not a new
rule. It is the same class of finding as Minotaur's Rakdos Carnarium (a Karoo with nothing to bounce
is not a land), and the same failure mode: a hand that reads as covered and is dead.

**What fills this quota from the battlefield:** every land we control, at its *yield*, not as a card
— "COUNT MANA, NOT CARDS" (the same Treasure Hunt note): a Peat Bog with a depletion counter left is
**2**, a Mountain is **1**, a live filter is **1**, a Peat Bog with no counters left has already
sacrificed itself and is not there.

**DEPLETING-BOARD CAVEAT (deck-specific, and I have not seen it stated elsewhere in the repo).**
Board mana in this deck is **not monotone**: twelve depletion lands die after two activations and
eight sac lands die when cracked, so a board census taken this turn can overstate next turn. I
propose the mild form — **the mana quota keeps one extra land in hand while every board source is a
depleting one** — and flag it in §10 as the quota I am least sure of.

**Extended target.** 5 is the enabler target. Raise it to `min(8, MV of the best colour-satisfiable
payoff in hand)` when the hand holds one, because a payoff is the **only** card in the deck that can
spend a sixth, seventh or eighth mana (two enablers cost 10 and are a two-turn plan).

### Bucket 2 — FREE-CAST ENABLERS. Quota 1 hard, 2 soft. **Net of board is EMPTY, by construction.**

`etb_exile_until_nonland || shuffle_reveal_freecast`. This bucket is the deck's engine: 5 mana for
one (Dragonstorm) or two (Creative Technique, via `demonstrate`) free casts of anything.

**The brief's rule 2 (NET OF BOARD) inverts here, and this is the deck's real exception.** A
Breaching Dragonstorm **on the battlefield is a spent shell**, not a standing enabler: its trigger is
"when this enchantment enters", one-shot, and the only way it returns is
`self_bounce_on_etb_subtype: "Dragon"` — for which this decklist has **zero Dragon-subtype
permanents and no Dragon tokens** (the card's own note: "NEVER FIRES in this 60"). So the
battlefield can never fill a single unit of this quota, and a census that counted a resolved
Dragonstorm would shed the hand's last real enabler while looking at an enchantment that does
nothing. Creative Technique is a sorcery, so the same is trivially true for it.

### Bucket 3 — CHEAT PAYOFFS. Quota 1, plus a 2nd only if colour-satisfiable. **One grouped bucket.**

`cascade_max_mv > 0`. **Decision on the brief's question — ONE bucket, not several**, and the reason
is the brief's own counterweight ("similar effects grouped"): all five do the *same* job from hand —
pay 6–8, flip a free spell, usually add a body. They are not different combo parts; the parts are
mana / enabler / payoff, and inside the payoff part the cards are interchangeable chain-starters. The
one functional difference that matters (Call Forth has no body) is expressed as a **rank inside** the
bucket, not as a bucket of its own. Splitting them would create quotas like "keep one double-cascade
and one hasty body", which on a hand of stranded 8-drops would protect two uncastable cards.

**Net of board:** a payoff already on the battlefield does not fill the quota either (it is a body
that has already done its cascade) — but it *does* matter for **one** thing: a Legendary payoff on
board makes a held copy uncastable (§8, promotion 2).

## 5. Within-bucket order, and the distance term

**The distance term this deck needs is COLOUR-EXACT, not a count.** In the states where a shed
happens, *nothing* in hand is castable (§9), so ordering by count-deficit is just re-deriving mana
value — which is what the fallback already does. Colour is different: it is the axis the deck is
actually short on, and it is not fixable by "another land drop".

Source inventory (pips available, filters feeder-gated, per activation):

| colour | sources | notes |
|---|---|---|
| **R** | Mountain ×9, Dwarven Ruins ×4 (crack = 2 R), Ferrous Lake ×4*, Mossfire Valley ×4* | 21 cards — the easy colour |
| **U** | Saprazzan Skerry ×4 (**2 U at once** → Protege's `{U}{U}` off one land), Svyelunite Temple ×4 (crack = 2 U), Ferrous Lake ×4* | 12 cards |
| **G** | **Mossfire Valley ×4 only**, `ramp_filter`, 1 G each | so `{G}{G}` needs **two of four specific cards** plus two feeders |
| — | Peat Bog ×4 (B), Remote Farm ×4 (W) | 8 cards that can **never** pay a pip in this deck |

`colour_ok(card)` = for each colour C, `pips(card, C) <= available(C)` across board **and** hand
(pips, not cards — a Skerry supplies two blue). That is the Hall's-condition form the repo already
uses for affordability (`colour-blind-affordability-2026-08-18`), and `colour_mana()` at
`DecisionProviders.cpp:4038` is a ready implementation of the per-land half.

**MANA order** (best kept first): the hand's only red source, then burst lands (highest yield first),
then red 1-yield lands (Mountain, Dwarven Ruins), then live filters, and last a **filter with no
feeder** — which is a blank and is therefore the first land shed.

**ENABLER order:** Creative Technique > Breaching Dragonstorm. Mechanism, not preference:
`demonstrate` gives CT a second reveal-and-free-cast, its free cast is uncapped, and it shuffles
(re-randomising after the walk). Learned `card_scores` put them 1st and 2nd in the deck (+0.330 /
+0.290). Dragonstorm's compensation is real but smaller: the lands its walk exiles **stay exiled**,
thinning the library permanently.

**PAYOFF order** — authored, best kept first. This is a deck-specific value order among cards of the
same role, which the brief permits; the justification is per card mechanical, not a preference:

1. **Boarding Party** (6, `{5}{R}`) — cheapest, easiest colour, **6/3 haste = 6 damage the turn it
   lands**, and its `cascade_max_mv` 6 can only hit an MV-5 nonland, i.e. it flips an **enabler with
   certainty**, which then free-casts anything. The best hard-cast in the deck.
2. **Sakashima's Protege** (6, `{4}{U}{U}`) — same guaranteed enabler flip, `{U}{U}` off a single
   Skerry, and `enter_as_copy_of_entrant` lets it enter as a copy of whatever its own chain just put
   down (it enters *after* its cascade resolves). Only a 3/1 by itself.
3. **Maelstrom Wanderer** (8, `{5}{G}{U}{R}`) — the highest ceiling in the deck (7/5, two cascades,
   `grants_haste` for the whole board) and the hardest hand card to cast: three colours including the
   4-card green, plus a Legendary duplicate problem.
4. **Call Forth the Tempest** (8, `{5}{R}{R}{R}`) — two cascades, colour-reachable, **no body and no
   clock**.
5. **Annoyed Altisaur** (7, `{5}{G}{G}`) — one cascade, 6/5, **no haste**, and `{G}{G}` off four
   feeder-gated filters. Cheaper than the 8s and still the least castable card in the deck.

Note the order deliberately does **not** track mana value (it ranks the 8-drop Wanderer above the
7-drop Altisaur) and does not track raw power either. It tracks *hard-cast reachability × what the
cast does*.

**The one distance modifier:** a payoff that fails `colour_ok` ranks **below every payoff that
passes**, inside the bucket. That is bounded on purpose — it is a single re-partition, not a
playability-first sort, because Minotaur measured lexicographic "playability, then effectiveness" as
**worse** (rounds 1–3 of `minotaur-discard-policy-proposal.md`; the winning shape was value-led).
A **count**-deficit term is deliberately left out and proposed as a measured lever only (§10).

## 6. The total order over a hand

Two ladders, the second being the first reversed. **Omission = keep**, and the shed list **names
every card in the hand** — the Mirrorwing gi295 lesson: anything unnamed falls back to tier B's
descending MV, which is the ranking this provider exists to correct.

**KEEP ladder (protected first; only overflow beyond it is sheddable):**

    R1  the hand's only red source            (colour coverage; a dead filter does not count)
    E1  best enabler                          (CT > BD)
    M   mana up to 5 usable                   (burst-first, board netted)
    P1  best COLOUR-SATISFIABLE payoff        (the only sink for a 6th-8th mana)
    E2  second enabler                        (prefer a different name; see §10)
    P2  second payoff, only if colour-satisfiable
    M+  mana up to the extended target (<=8)   only while a hard-castable payoff is held

If **no** payoff is colour-satisfiable, P1 protects the best payoff by authored order but drops
**below** E2 (an uncastable payoff is not a mana sink).

**SHED ladder (index 0 first):**

    D1  a payoff whose cast is ENGINE-PRUNED  (Legendary + a copy on the battlefield) -> worth 0
    D2  surplus copies (2nd+ in hand) of a COLOUR-STRANDED payoff, authored-worst first
    D3  colour-stranded payoffs, authored-worst first
        (i.e. Altisaur, then CFT, then Wanderer, then Protege, then Boarding Party)
    D4  lands beyond the extended mana target: dead filter, then lowest yield, then burst
    D5  surplus copies of colour-satisfiable payoffs, authored-worst first
    D6  colour-satisfiable payoffs beyond the payoff quota, authored-worst first
    D7  enablers beyond the enabler quota (BD copies before CT copies)
    D8  the quota-protected cards, in reverse KEEP-ladder order (never reached unless forced)

Worked hands, to show index 0 is determined:

* **Board** Mountain + Peat Bog + Dwarven Ruins (4 mana, R yes, no U, no G). **Hand** CT, CT, BD,
  Wanderer, Wanderer, Altisaur, Boarding Party, Protege. Colour: Protege (`UU`), Altisaur (`GG`),
  Wanderer (`G`) all stranded. Keep = CT#1 (E1), Boarding Party (P1), BD (E2, the different-name
  default); P2 goes unfilled because no second payoff is colour-satisfiable. Shed order =
  **Wanderer#2** (D2), Altisaur, Wanderer#1, Protege (D3), CT#2 (D7). Index 0 = Maelstrom Wanderer —
  *the same card the fallback picks*, which is the honest common case.
* **Board** Peat Bog + Remote Farm + Peat Bog (6 mana; no R, no U, no G — the colour-screw shape).
  **Hand** CT, BD, Boarding Party, Protege, CFT, Wanderer, Altisaur, Altisaur. Everything is
  colour-stranded, so the gate is uniform, P2 goes unfilled and the authored order decides everything
  below the protected prefix (CT = E1, BD = E2, Boarding Party = P1). Shed order = **Altisaur#2**
  (D2), Altisaur#1, CFT, Wanderer, Protege (D3). Index 0 = **Annoyed Altisaur**, where the fallback
  sheds an MV-8 double-cascade first. This is the differing case.
* **Board** one Wanderer resolved, 5 mana. **Hand** anything containing a Wanderer → that Wanderer
  is index 0 by D1, ahead of even a stranded Altisaur, because the engine will not offer its cast.

## 7. Param-level classification predicates (so integration is mechanical)

    is_land(i)        -> CleanupDiscardIsLand(hand[i])
    def_of(i)         -> CardDatabase::Instance().LookupCached(hand[i])   // hand cards are placeholders
    is_enabler(i)     -> p.etb_exile_until_nonland || p.shuffle_reveal_freecast
    free_casts(i)     -> p.shuffle_reveal_freecast ? (p.demonstrate ? 2 : 1)
                       : p.etb_exile_until_nonland ? 1
                       : max(0, p.cascade_count)                          // cascade_count defaults 1
    is_payoff(i)      -> p.cascade_max_mv > 0
    has_haste(i)      -> def->card.HasKeyword(Haste) || (p.grants_haste && p.affects_all_creatures)
    yield(i)          -> p.ramp_filter ? (filter_feeder_live ? 1 : 0) : max(1, p.produces_amount)
    burst(i)          -> p.produces_amount >= 2 || p.sac_for_mana_amount >= 2
    crack_bonus(i)    -> max(0, p.sac_for_mana_amount - max(1, p.produces_amount))   // one-shot +1
    filter_feeder_live-> (board_lands + other_lands_in_hand) >= 1          // cf. DecisionProviders.cpp:4028
    makes(i, C)       -> contains(p.produces, C) && (!p.ramp_filter || filter_feeder_live)
    colour_ok(i)      -> for every C: pips(def, C) <= available(C)         // pips, not cards
    legend_dead(i)    -> def->card.HasSupertype(Legendary)
                         && a same-name permanent is on OUR battlefield    // OfferDuplicateLegendCast
    copies_ahead(i)   -> earlier non-staged hand copies of the same name (+ battlefield copies)

Note the two `params` traps this repo has already paid for: **check a default before a `>0` test**
(`cascade_count` and `reduces_spell_subtype_amount` both default to 1, so `cascade_count > 0` is true
for *every* card — use `cascade_max_mv > 0` for "is a cascade card", which is what the engine's own
routing does), and **read characteristics off the definition, never the hand card** (the Dragons
`CardHasSubtype(ap.hand[i], ...)` bug). Nothing above is keyed on a card name except the authored
payoff/enabler value order of §5, which the brief permits and which §5 justifies per card.

## 8. State promotions

**Proposed (a cleanup ranking can actually establish these):**

1. **Colour-exact stranding** (§5). The deck's only real distance signal.
2. **The engine-pruned Legendary duplicate → index 0.** With a Maelstrom Wanderer on the
   battlefield, `OfferDuplicateLegendCast` never offers a second one (Legendary +
   `VanillaCreature` + no `DuplicateEntryOrDeathHasUpside` watcher in this deck + no
   `HandShedIsPayoff`), so the held copy's two cascades can never fire. This is *provable* from the
   engine, not a judgement, which is what makes it worth a rung of its own. The prune is at the
   HAND-cast enumeration only (`TurnSolver.cpp:13494`), so a duplicate Wanderer **flipped free off
   the library** is unaffected — which is correct, and is what the card's "a duplicate dies but its
   cascades still fire" note describes. Stated dependency: the premise is `MTG_PRUNE_DUP_LEGEND`
   (default ON). With `=0` the held copy becomes castable for 8 mana as two cascades with no body,
   which is still the worst payoff in the hand — so D1's placement survives, just for a weaker
   reason. If the integration wants to be exact, gate the rung on the same flag.
3. **The spent-shell exception to the board census** (§4, bucket 2): a resolved Breaching
   Dragonstorm fills nothing.
4. **The filter-feeder and depleting-board caveats** (§4, bucket 1).
5. **The crack bonus in the distance test only.** A Dwarven Ruins / Svyelunite Temple in play adds
   **+1 mana for one turn** if cracked, which is exactly the shape a single hard-cast needs; it does
   not raise the standing per-turn quota. (The engine already prices this exactly —
   `CreditFixedColorSac`, `TurnSolver.cpp:19954`, credits the *delta* — so the ranking must not
   double-count it either.)

**Deliberately REJECTED as search-owned or unreachable:**

* **Protege's copy value.** `enter_as_copy_of_entrant` is scoped to permanents that entered **this
  turn** and are still on the battlefield. At cleanup we cannot cast anything, and a Dragonstorm that
  entered on an earlier turn is not a legal source — so the promotion's real content is "what will
  next turn's chain put on the battlefield before Protege enters", a cast-sequencing question for
  the search. Left out entirely.
* **Call Forth's damage clause as reach.** It damages opponent *creatures* and cannot change the
  clock (card note). No promotion; it just ranks low.
* **"Keep a payoff to keep library density."** Not a mechanism: discarding does not touch the
  library (§2).
* **Any shed-is-a-benefit inversion.** Nothing here pays for an empty hand (§2). Stated only because
  Minotaur has one and its absence is what makes "index 0 = least valuable card" the whole rule.
* **A count-deficit (mana-distance) term, and an EV product.** Both are plausible and both are
  §10 levers rather than proposals, because at a real shed *everything* is uncastable and a count
  term degenerates toward the max-MV ranking this policy is replacing.

## 9. Honest assessment — read this before approving

**How often can this fire, structurally?** This deck has **no card draw**, one land drop a turn, and
a 7-card limit. So the hand can only reach 8 at cleanup by drawing while holding no playable land —
which means **a cleanup shed in this deck happens with an all-nonland hand**, unless the turn's land
drop was *declined* (a genuine plan variant: `TurnSolver.cpp:3599`, "no land drop, a play the game
can genuinely make", which condemns the lands in hand). Two consequences, both honest:

* The MANA bucket is **insurance**, not the hot path: at most sheds there are no lands in hand to
  rank. It still has to be right for the declined-drop and rollout states, and it costs nothing.
* The load-bearing part is the **within-payoff order plus the enabler floor** — i.e. defects (a),
  (b), (d) of §2. And because the MV-5 enablers are the *cheapest* nonlands, the fallback's
  descending-MV rule already protects them most of the time, so the enabler floor rarely binds
  either. **Expect a small behavioural delta.** I would be surprised by a metric win and would not
  claim one.

**Frequency numbers: UN-RUN, and why.** The `MTG_SHED_STATS=1` census that anchored the Dragons and
Minotaur proposals (66 and 99 real-play sheds against 661k and 250k inside the search) is **not run
here**. The box is at load average 30 on 24 cores with another run in flight, and this repo's rule is
one batch/test at a time; a census is not worth perturbing someone else's measurement, and this is a
document, not a measured adoption. It should be run **at integration**, together with
`MTG_TRACE=discard` — whose `landsinhand` and `dropopen` fields are exactly the instrumentation for
the structural claim above. Given a turn-3 clock the real-play count will be *small*; per the brief
and the Dragons/Minotaur record, **that is the wrong denominator** — the rollout cleanup calls this
same hook, with no search above index 0, and this deck ships a value leaf (`BreachingDragonstorm.value.json`,
trust depth 4, 0.48x cost) whose table was fitted on rollouts that used the fallback ranking.

**Corroborating evidence I did not have to run: the deck's own learned `card_scores`.** First-copy
marginals from `BreachingDragonstorm.profile.json`:

    Creative Technique +0.330 > Breaching Dragonstorm +0.290        <- the ENABLERS
      > Peat Bog +0.243 > Remote Farm +0.204 > Dwarven Ruins +0.202 <- the BURST lands
      > Skerry +0.098 > Svyelunite +0.076
      > Altisaur -0.008 > Wanderer -0.018 > Ferrous Lake -0.053     <- PAYOFFS (and the two
                                                                       filter lands) interleave,
                                                                       spread only 0.085 wide
      > Boarding Party -0.071 > Mossfire -0.082 > CFT -0.092 > Protege -0.093
      > Mountain -0.127                                             <- the worst card in the deck
    second copies: Creative Technique -0.348, Altisaur -0.565, Boarding Party -0.138,
                   Breaching Dragonstorm -0.146, Dwarven Ruins +0.062

Read with the units caveat the Minotaur round-4 work documents (these are *opening-hand* group-mean
differences, confounded with castability, and this profile predates 20 days of `src` commits): they
are **decisive between groups and useless within the payoff group**. Between groups they back this
proposal's structure exactly — enablers ≫ burst lands ≫ payoffs ≳ plain Mountain. Within the payoff
group the whole spread is 0.085 and the order disagrees with §5 (it ranks Altisaur first), so the
authored payoff order is **mine, not the data's**, and §10 flags it.

**Bar.** Non-inferiority plus doctrine quality, per the brief. The case for shipping is (a)+(b)+(d)
being right for stated mechanical reasons, plus rollout fidelity; I am not promising a turn.

## 10. Doubts — flagged for the user to settle

1. **The authored payoff order is my judgement and the learned scores disagree with it.** I rank
   Boarding Party > Protege > Wanderer > Call Forth > Altisaur on hard-cast reachability × effect;
   the deck's `card_scores` rank Altisaur and Wanderer *highest* among payoffs. I trust the mechanism
   over a 0.085-wide confounded spread, but this is the single most swappable thing in the document —
   and it is cheap to A/B as an order lever.
2. **E2 vs P1 (the 5th and 4th rungs).** I put a colour-satisfiable payoff **above** the second
   enabler, on the mechanism that a payoff is the only sink for a 6th–8th mana and that Boarding
   Party's cascade is a guaranteed enabler flip *plus* six hasty damage. The learned second-copy
   marginal for Creative Technique (-0.348, the worst in the deck) weakly agrees. A measurement could
   flip this rung and nothing else.
3. **Enabler quota: 2, or 1?** Two enablers cost 10 mana — a two-turn plan on a deck that wins on
   turn 3. Dragons' analogous ruling was "keep FEWER payoffs, 2 at minimum"; I chose 1 hard + 1 soft.
   Related: should E2 prefer a **different name** (BD when E1 is CT)? I default to yes, on weak
   evidence (CT's second-copy marginal is the deck's worst) and a weak mechanism (a BD cast *in* a
   chain turn can be re-fired by a Protege copy). Low confidence.
4. **The depleting-board +1 land.** Board mana here genuinely shrinks (12 depletion + 8 sac lands),
   which I have not seen priced anywhere in the repo. My "+1 land in hand while every board source is
   depleting" may be over-engineering a quota that rarely binds. Drop it if it complicates the
   ladder.
5. **Should a surplus LAND shed before a stranded payoff?** I put dead/stranded payoffs first (D1–D3)
   and surplus lands at D4, on the grounds that a stranded payoff is worth 0 while a land past the
   target still builds toward the 8-mana hard-cast. With 37 lands (62%) an argument runs the other
   way. It only binds in a declined-drop hand, so it is cheap either way.
6. **No count-deficit term at all.** Deliberate (§8), but it means two colour-satisfiable payoffs at
   MV 6 and MV 8 are ordered purely by the authored value order. If a measured EV-style product is
   wanted, the Minotaur precedent (`EV2` + hard decay + cumulative-mana dupes) is the shape that
   worked there — value-led, not playability-led.
7. **`required_pieces` is empty for this deck**, so `CleanupDiscardProtected` protects nothing today.
   If the user ever wants a hard engine-level protection, the natural declaration is the *pair*
   {Creative Technique, Breaching Dragonstorm} as an **interchangeable** group via
   `InterchangeableRequiredGroup` — the Anti-Lifegain lesson (`SpellEffects.h:300`) is that
   name-only required-piece counting made a provider shed a payoff to protect a redundant enabler.
8. **The census is UN-RUN** (§9). It is recorded as un-run, not as passed or deferred, and the box
   contention is the reason.
