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
* **Final claude-play recheck (seeds 2100017 / 2100018), 2026-10-17.**
  * Clause (d)'s Path exemption is ORDER-AWARE: a target-less Path (the human's one entry, its creature
    picked at resolution) frees the Theft's piece only when cast AFTER the Theft. The subset walk has no
    order yet and keeps the subset; the human cast-ORDER menu then drops each ordering that casts the
    Path first (`TurnSolver::AttachOrderStrandsPlan`, realised order = vector order for a searched-order
    variant, else PlanCastOrder + ApplyAttachSpellDependencyOrder).
  * Clause (c) (a Theft onto a loose Lizard Blades the subset attaches) covers a HAND-CAST Lizard Blades
    (`AttachHandHostIsPinnedCopy`; refused while Sigarda's Aid could attach it on entry).
  * CR 601.2c: a Magnetic Theft with no legal (Equipment, creature) pair is not cast, in both worlds
    (`AttachSpellCastLegal`, SpellEffects.h, also read by `FreeCastHasLegalTargets`). The human menu drops
    an ordering whose Path is FORCED onto the Theft's only creature (the attach-order probe on the trial
    apply). The card stays in hand; on a multi-cast line the whole-turn prepay (BatchPrepayMainCasts) has
    already tapped its {R} into the pool, so the mana floats for the phase rather than staying in a land.

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
7. **HAND pieces -- FIXED 2026-10-17: one Theft variant per hand COPY.** A same-plan hand Equipment
   was offered once per name, pinned to the first copy, so "cast two Colossus Hammers, Theft each onto a
   different creature" was unfindable (both Thefts moved the first copy). The search now pins each copy
   (Action::attach_equip_hand_ord); the k-th cast of a name takes the first copy still in hand in both
   worlds, so the copy of ordinal i lands on the (i+1)-th cast -- the Theft is ordered after it
   (ApplyAttachSpellDependencyOrder), a subset casting no more than i copies is refused (the pin would
   fall back onto a copy whose own pin is enumerated), and the copies' mirror images are folded by the
   joint relabelling as a piece class whose rank is the hand ordinal. Human play, and a name with a
   STAGED copy in hand (cast first, so hand order no longer says which copy lands), keep one per name.
8. **Two identical Theft CARDS are still walked in both assignments -- deliberately left (2026-10-17).**
   {Theft #22: X, Theft #23: Y} and {Theft #22: Y, Theft #23: X} are one plan (the plan dedup keys a
   Theft by its pair) but two odometer positions: work, never a lost line. Not small-and-sound today:
   (1) the relabelling strings read Thefts in CARD order, so a card-copy canonical form (the odometer
   copy folds' "digits non-increasing") conflicts with them and can leave a spread assignment with no
   surviving position; a sound version has to be restricted to subsets where neither Theft names a
   class member, or replace both with one lexicographic-minimum-over-the-orbit test; (2) swapping the
   cards also swaps which pair RESOLVES first, which an O-Naginata-style power gate or the hand-piece
   name fallback can observe; (3) the Aura copy fold's precedent also checks the m1_hand
   order-condemnation snapshot, which singles hand cards out by number.
9. **d0 greedy: a self-target Path is priced 0 even when it exiles the plan's only attacker.** Found
   through smoke gi539 (seed 1540) after item 6: the greedy's projection has no win on either plan, so
   {Theft, Theft} and {Path -> Kor Duelist, Theft, Theft} tie on rank (4200) and the smallest mask picks
   the second; the game goes from a T6 win to unwon. The search finds T3 at d6/b100 (stage-1 recovery),
   so this is the depth-0 runner's valuation, not a reachability gap; a heuristic fix needs its own A/B.
10. **An Equip of a hand Equipment copy that never lands -- FIXED 2026-10-17 (MTG_EQUIP_UNLANDED_COPY_FOLD).**
   The Equip block offers one Equip per hand COPY, and SubsetHasStrandedEquip accepts it whenever a
   cast names that copy's hand SLOT -- but the cast materialises the first copy still in hand, so with
   N copies of a name cast, an Equip of the copy of hand ordinal k >= N pays its equip cost (and spends
   the first-equip waiver; MTG_EQUIP_PAY_GUARD is off) and attaches nothing. It is reachable whenever
   the hand-cast fold keeps two copies apart (one copy drawn after this turn's main-1 snapshot, a
   tagging decline), which is when the two cast slots are separate digits. Folded in both subset walkers
   (SubsetHasEquipOfUnlandedCopy, beside SubsetHasStrandedEquip, odometer positions only) with the
   ordinal rule the per-copy Theft pins use: the subset without that Equip is enumerated (an optional
   digit, never forced), costs no more, and reaches the same board. Proven only where nothing else in the
   subset can move a hand copy (every action an Equip, a reconfigure unattach, or a cast of an Equipment
   or attach spell) and nothing can re-solve the phase before the trailing pass (no equipment / cast /
   enters draw watcher, no play-from-top source); a name with a staged copy keeps every plan; human play
   and MTG_UNPRUNED are untouched. Unit cases in test/unit/test_equip_host_fold.cpp ("Unlanded hand
   copy: ...").
