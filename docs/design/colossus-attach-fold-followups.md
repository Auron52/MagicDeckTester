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

1. **Equip / reconfigure HOSTS are not identity-folded.** Battlefield equip hosts are deliberately not
   deduped by NAME (two Kor Duelists can differ), but nothing dedupes them by an identity CLASS either,
   so two class-identical Speakers of the Heavens double every "equip X -> Speaker" and "reconfigure
   Lizard Blades -> Speaker" plan (seed 2100003 T5 shows them in pairs). Magnetic Theft's creature side
   already folds exactly these with `AttachHostClassKey` (SpellEffects.h); the same key on the Equip
   host loop would be a sound identity fold. It interacts with the Equip block's host rankings and
   width policies, so it needs its own enumeration-width measurement.
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
