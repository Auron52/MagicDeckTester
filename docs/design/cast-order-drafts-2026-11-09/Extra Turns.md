# Extra Turns: proposed cast order for review (2026-11-09)

## Status

This is a **proposal**, not an installed cast order. The current
`ExtraTurnsProvider::CastOrderRank` only promotes Extraplanar Lens to the mana-rock
tier. Everything else uses the generic order. In particular, the current report
puts Ulamog and Emrakul before the draw artifacts and turn spells. Draw-sensitive
plans may bypass those ranks, so the report is not the whole execution policy.

The proposed order below concerns **the order of actions already chosen for this
turn**. Deciding whether to cast a card at all belongs to the plan search and
the extra-turn walk. Horn may straddle the land drop, and the acceleration
selected for the turn may need to precede Horn to pay for it. Lens and Mind
Stone swap places according to Lens's current-turn mana benefit. Temporal
Mastery's miracle cast happens at the draw event, outside the ordinary
main-phase order.

## Proposed full order

Earlier rows execute first. Cards in the same row have the explicit order shown.
All ordinary casts are in the deck's single main phase.

| Slot | Action | Placement and reason |
|---:|---|---|
| 0 | Temporal Mastery **miracle cast** | Resolve its {1}{U} miracle decision when it is drawn as the first card of the turn. This is outside the normal main-phase cast order. |
| 1 | Extraplanar Lens and/or Mind Stone, **if they and Horn are payable before a remaining land drop** | Cast the selected acceleration before Horn. Lens goes before Stone when Lens's imprint gives usable mana this turn; otherwise Stone goes first. If the acceleration plus Horn cannot all fit before the land, evaluate the alternatives below. |
| 2 | Horn of Greed, **when payable before a remaining land drop** | Cast after any pre-land acceleration, then play the land to draw immediately. A Horn that needs the land to be payable uses slot 6. |
| 3 | Land drop: Snow-Covered Island or Myriad Landscape | First when Horn is not cast. Otherwise it follows the pre-land acceleration and Horn when that whole line is payable, or precedes the post-land line. Land choice is a separate searched decision. |
| 4 | Extraplanar Lens and/or Mind Stone **not cast before the land** | Cast the selected acceleration after the land and before Horn. Lens goes first when its imprint gives usable mana this turn; otherwise Stone goes first. A second Lens remains subject to its current-turn payback test; a third is unnecessary. |
| 5 | Howling Mine | Prefer it as the draw-engine investment while fewer than two engines are live. It adds a card on the next own draw step, including an extra turn. |
| 6 | Horn of Greed **after the land** | Use when the pre-land acceleration plus Horn does not fit, but the post-land combination does. It misses this land's draw and can draw from later land drops. |
| 7 | Emrakul, the Aeons Torn | Cast whenever the exact line can pay {15}. Its cast itself grants the attack turn, so it does not need a separate turn spell first. Perform required mana setup before it. |
| 8 | Temporal Manipulation; Time Warp | The two {3}{U}{U} one-turn spells are functionally equivalent here; use name order only to make the sequence total. At least one turn must be secured before ending a turn whose chain would otherwise stop. |
| 9 | Part the Waterveil, normal or awaken cast | Use after the five-mana spells when both are selected. Awaken is a distinct payment and may make a quicker kill; search chooses that mode. |
| 10 | Temporal Mastery, normal {5}{U}{U} cast | Spend the one-shot seven-mana spell before Nexus when both are chosen, preserving the reusable Nexus for a later draw. |
| 11 | Nexus of Fate | Cast after Mastery when both are chosen. Its shuffle changes the library, so the search still evaluates the exact draw continuation. |
| 12 | Expropriate | Its time vote secures at least one extra turn in this goldfish. If it can be paid, search may prefer it over a cheaper turn spell because it can grant more turns; this row only resolves the order when both are in the selected line. |
| 13 | Walk the Aeons, plain or buyback | Last among the ordinary turn spells so that other spells are used first. Buy back only when sacrificing three Islands leaves enough mana for the next necessary turn spell or repeat Walk; otherwise cast it plain. If it is the only way to maintain the chain now, cast it now rather than wait for an imaginary later slot. |
| 14 | Ulamog, the Ceaseless Hunger | Cast after a selected turn spell unless a turn is already queued. It does not grant a turn, so it needs a queued turn or a turn spell in the same line to get an attack. Search must account for its mandatory two-target exile trigger. |
| 15 | Mind Stone sacrifice for a card | This is an **activation**, not a cast. Normally cash in a surplus Stone after its mana is no longer needed. If no turn spell is available and drawing one is the way to keep the chain, move the activation **before any discretionary mana spending** and re-plan from the card drawn. |

## Rules that outrank the nominal slots

1. **Keep the chain.** A proposed setup cast must not consume the mana needed
   for the only available extra-turn spell. If no spell is in hand, use legal
   draws, including a Mind Stone sacrifice, to seek one before giving up.
2. **Take a castable Emrakul.** Its own cast trigger supplies an extra turn.
   Ulamog is permissible only when another own turn is already queued or the
   selected line also casts an extra-turn spell.
3. **Check exact payment.** Lens's imprint and Walk's buyback sacrifices must
   be included. The search chooses only legal, payable lines; the cast order
   does not force every card in hand to be played.
4. **Use draw information.** A Horn cast before a land drop can draw a new
   action. Stop the old cast sequence at that draw and choose the continuation
   from the new hand. A Mine does not draw immediately; its benefit begins at
   the next draw step.
5. **Do not cast every selected turn spell reflexively.** Usually one is enough
   for the next handoff. Extra queued turns can be useful, but spending a
   scarce spell now can shorten the chain later. The plan search owns that
   choice; this table only orders a set it has selected.

## Horn and acceleration decision

1. For the **chosen set of casts**, test the whole line with exact mana payment:
   acceleration, Horn, land; and land, acceleration, Horn. Include every other
   selected cast, especially the spell that maintains the extra-turn chain.
   An order that cannot pay for that line is discarded. Acceleration therefore
   goes first whenever it is required to make the selected line work.
2. If acceleration **and** Horn can both fit before the land while still paying
   for the rest of the selected line, use acceleration, Horn, land. That gets
   the immediate draw and the acceleration; re-plan after the draw.
3. If both cards do **not** fit before the land, compare pre-land Horn for the
   immediate draw with land, acceleration, Horn for the mana development. Test
   what each leaves for the **other casts the line needs**. If only one can pay
   for those casts and maintain the chain, choose it without an order search.
4. If both alternatives still work, payability alone does **not** resolve the
   draw-versus-acceleration choice: they cast different sets of cards. Retain
   that small search, or measure a provider heuristic that favors the immediate
   draw when no known cast needs the extra mana. A newly drawn card can make
   spare mana valuable, so that preference is not a lossless dominance proof.
5. When Horn is not cast, play the land before every other ordinary cast.

**Three Islands in play; Island, Horn, and Mind Stone in hand:** Stone then
Horn cannot both fit before the land. Horn before the land draws immediately
but leaves too little mana for Stone. Land, Stone, Horn casts both without this
land's draw. These are different cast sets, so exact payment alone does not
choose between them; any additional selected spell may make one line uniquely
payable.

**Four Islands in play; a fifth Island, Lens, and Horn in hand:** To cast both,
play the land, cast Lens using three Islands, imprint one of those tapped
Islands, then use the two untapped doubled Islands to cast Horn. Casting Horn
first leaves too little mana for Lens; casting Lens before the land leaves too
little for Horn. The payable two-card line has one order, so no permutation
search is needed.

Temporal Mastery has an ordinary slot at 10 and a separate miracle opportunity
at the draw event. The remaining slots form a straightforward order; their
exact relative positions usually do not change the outcome of a payable plan.

No code or measured results are claimed for this proposal. Implementing it
would require the deck's order to apply consistently through draw breakpoints.
