# Colossus Hammer attach folds -- what was fixed, and what is still open

Self-contained follow-up to the 2026-10-17 claude-play sweep fixes on branch `colossus-fold`
(Magnetic Theft / Lizard Blades / Forge Anew / Path to Exile on the Colossus Hammer list). The fixed
part is summarised first so the open items have their context; the open items are NOT being worked
on and are recorded here per the deferred-work rule.

## Fixed (for context)

* **An Equip with no legal target is never activated** (`EquipTargetNotACreatureNow`,
  `src/core/SpellEffects.h`; both trailing passes). A host that is on the battlefield but no longer a
  creature (a Lizard Blades the same pass reconfigured away) is not targetable (CR 702.6a, 602.2b), so
  nothing is paid and Forge Anew's / Kili's first-equip waiver is not spent; the per-plan waiver
  (`StoriedWaivedEquipId`) moves on to the equips still to come.
* **`SubsetHasEquipOntoAttachedPiece`** (`src/ai/TurnSolver.cpp`): no plan equips onto a reconfigure
  piece after the plan's own reconfigure of it (illegal), nor -- from an unattached start -- before it
  (the attach is stripped again, CR 704.5n).
* **`SubsetHasStrandedAttachSpell`** gained the same-plan HAND Equipment cases of its redundant-attach
  folds (identity pinned by "exactly one copy of that name cast, none entered this turn"), and the
  Theft-then-unattach fold (f). Its clause (d) was tightened: it no longer folds a plan in which
  something can take the Theft's new host away first (a Path at it, a trailing sacrifice), which makes
  the piece a creature again and the Equip a real move.
* Path to Exile's human default target ranks by real power (`EquipGatePowerOf`).

## Open items

1. **Equip / reconfigure HOSTS -- DONE 2026-10-17 for the autonomous search (MTG_EQUIP_HOST_FOLD).**
   A canonical relabelling on the subset (a restricted-growth string over the identical hosts, sources
   read in card-number order), so "both on one Speaker" and "one on each" both survive and only their
   mirror images are dropped; see BuildEquipHostFold / SubsetHasNonCanonicalEquipHost in TurnSolver.cpp
   for the identity argument and every refusal. STILL OPEN: (a) the HUMAN menu (viewer / claude-play)
   keeps every host, on the MTG_AURA_HOST_FOLD precedent -- a saved reference replays by card number,
   so folding there would need the reference checker to map a recorded host onto its canonical twin;
   (b) the fold refuses any subset holding a creature cast, a draw / tutor / Forge Anew, a removal or a
   Speaker's own tap (each can pick among the hosts while they are still identical, or re-solve the turn
   mid-plan); a per-card "provably picks no creature" classification would widen it; (c) the Equip
   block's width policy already offers the Hammer onto one Speaker only, so most of the remaining
   doubling it removes is the reconfigure's.
2. **Clause (c) of SubsetHasStrandedAttachSpell, battlefield pieces, is a "never better" fold, not an
   identity.** A Theft of an Equipment that is ATTACHED elsewhere onto a loose Lizard the plan then
   reconfigures ends with the Equipment unattached, where the plan without the Theft leaves it on its
   old host. The new hand-piece form of (c) and the e-first half of SubsetHasEquipOntoAttachedPiece
   require an unattached start for exactly this reason; the original battlefield form was left as it
   shipped. Nothing in the current pool profits from an Equipment falling off (Grafted Wargear's
   re-host sacrifice is guarded), but a proof would add the same start condition.
3. **The dropped-cast Equip class is still paid for.** An Equip whose host cast was dropped as
   unpayable earlier in the plan still pays (and, on a waiver deck, spends the first-equip waiver)
   before ApplyEquip refuses. That is `MTG_EQUIP_PAY_GUARD`'s class
   (docs/design/equip-host-not-on-battlefield.md), default OFF pending the USER, and was deliberately
   not adopted by the back door here.
4. **The waiver is assigned up front.** `StoriedWaivedEquipId` picks the plan's most expensive equip
   before the pass. When that equip is refused AFTER a cheaper equip already paid full, the cheaper one
   does not get the waiver retroactively (by the rules it was the first equip activated and should have
   been free). After the enumeration fold this is reachable only through a human-declared order or a
   stale line.
5. **Human-declared orders.** The enumeration folds read the CANONICAL order (plan order + the
   provider's activation rank). A human who reorders "equip E -> Lizard" before "reconfigure Lizard ->
   X", with E attached elsewhere, could have expressed "move E off its host for nothing"; the menu no
   longer offers that set when its canonical order is the illegal one.
6. **Magnetic Theft's class folds -- FIXED 2026-10-17 for battlefield creatures AND battlefield pieces.**
   BuildAttachSpellPairs kept one creature and one Equipment per identity class, so two Thefts could
   never put one Equipment on EACH of two identical creatures, nor move each of two identical loose
   Hammers (and, with the fungible-copy fold canonicalising the Equips of those Hammers, "equip one
   Hammer, Theft the other" was unreachable too) -- unfindable at any budget. The search now offers
   every pair; the mirror images are folded jointly with the Equips by MTG_EQUIP_HOST_FOLD's canonical
   relabelling (a creature axis and an independent piece axis). Human play keeps the one-per-class menu
   (the person re-picks both targets on the board at resolution).
7. **Still open, the same defect one zone over: a HAND piece is offered one per NAME.** Both Thefts of a
   "cast two Colossus Hammers, Theft each onto a different creature" line name the first hand copy, so
   the second Theft moves the same Hammer again: two freshly cast identical Equipment cannot both be
   Thefted (reachable only as Theft + Equip). Fixing it needs per-COPY hand variants plus a relabelling
   over the hand copies that composes with the hand-cast fold (MTG_FOLD_HAND_CASTS) and with apply_one
   casting an arbitrary copy of a name -- the identity of the copy that materialises is the subtle part.
8. **Two identical Theft CARDS are still walked in both assignments.** {Theft #22: X, Theft #23: Y} and
   {Theft #22: Y, Theft #23: X} are one plan (the plan dedup keys a Theft by its pair) but two odometer
   positions. A card-copy fold must order the restricted-growth strings by something a card swap does
   not move (they read Theft-card order today), or the two forms conflict -- worked through in the
   commit that fixed item 6.
9. **d0 greedy: a self-target Path is priced 0 even when it exiles the plan's only attacker.** Found
   through smoke gi539 (seed 1540) after item 6: the greedy's projection has no win on either plan, so
   {Theft, Theft} and {Path -> Kor Duelist, Theft, Theft} tie on rank (4200) and the smallest mask picks
   the second; the game goes from a T6 win to unwon. The search finds T3 at d6/b100 (stage-1 recovery),
   so this is the depth-0 runner's valuation, not a reachability gap; a heuristic fix needs its own A/B.
