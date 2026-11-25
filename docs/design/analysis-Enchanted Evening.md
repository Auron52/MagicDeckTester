# Analysis ledger: Enchanted Evening

Worktree `.claude/worktrees/enchanted-evening`, branch `ee-analysis` (from 05e2e0030). Nothing pushed yet.
Decklist: `decks/Enchanted Evening/Enchanted Evening.cod` (60 cards, GW enchantress / lock).

## USER rulings (verbatim, 2026-11-25)

1. *"Note that the deck locks down the opponent so, like Worldfire, we will need to take that properly into account. In
   particular Calming Verse with Enchanted Evening out removes everything and temporarily locks their lands. Enchanted
   Evening and Opalescence permanently locks the lands, though we should consider cards they can play prior to being
   locked to see if it is a true lock. One Parallax Wave is also this is considered a full lock because any creature can
   be exiled using the Parallax Wave + Opalescence combo and all permanents except Opalescence are creatures."*
2. *"(the lock idea is all about getting free turns in particular)"*
3. *"But with the full combo out here, we can attack with Enchanted Evening and Parallax Wave, ending the game pretty
   quickly."* / *"(while the opponent can do nothing)"*
4. *"It should be one 5/5 and one 4/4."* / *"(only enchanted evening costs 5, parallax wave is 4)"*

5. *"Yes, Calming Verse + Enchanted Evening is not a full lock, but if they play no land then it gives free turns. Also,
   when Opalescence drops it becomes a full lock as long as there is not a major gap between them."* -- so Verse+EE
   needs no rule change (an opponent turn ending with no land and no creature is already free), and Verse+EE then
   Opalescence is a full lock; the fairness check counts only what the opponent could cast in the gap between them.
6. *"I think "could they actually have cast a permanent with the lands they had in the gap" probably works, but we'll
   discuss if anything else comes up."* -- that is the gap test; no further threshold.
7. *"The default for Parallax Wave and Opalescence in this deck should be to exile all opponent creatures. Then when
   Enchanted Evening drops all of the opponent permanents are creatures. This means that they should all be exiled.
   Fortunately new lands die immediately so that doesn't need to enter the equation."* -- Wave + Opalescence exile
   every opponent creature (the self-exile refresh loop makes it unlimited); with EE that is every opponent permanent.
   A deck rule: it lives in this deck's provider.
8. On whether Force of Vigor / Oblivion Ring in our hand answer a permanent the opponent could have cast (fairness
   check): *"I guess it could count for that in theory, though it is a bit tricky to do so given that it requires us to
   either pay the mana or the extra card. It doesn't count for removing a follow-up land for sure."* -- default stays
   NO credit for held removal in the fairness check (only removal we actually cast counts); never assume a follow-up
   opponent land is removed.
9. On a hexproof creature the opponent could have cast vs the Wave + Opalescence lock: *"It does technically, but would
   need to be able to grow to at least 5 power with trample to be an actual threat to the full lock. The reason is that
   Enchanted Eveining can block a creature with less power creatures without trample can be infinitely chump blocked
   with their blocker exiled and replaced."* -- so in the fairness check an untargetable (hexproof) castable creature
   breaks the full lock ONLY if it has trample and can reach power >= 5; otherwise it is contained (chump-blocked
   forever, blockers exiled and replaced by the Wave) and the lock holds.
10. *"But it is true that Auras is a threat to this setup if they get the right stuff on board."* -- the "can reach
   power >= 5 with trample" test must count what the opponent could have put ON the hexproof creature in the gap, not
   just the creature's printed stats: castable Auras / equipment from its hand (Rancor-style +power and trample) within
   the same land budget. Rules detail: under EE + Opalescence the creature is also a non-Aura enchantment, so its BASE
   P/T becomes its mana value (layer 7b), and Aura bonuses apply on top (7c); Auras themselves are never animated and
   the Wave cannot target them.
11. *"But, if they can prevent attacks from the Auras deck in the way I mentioned, Enchanted Evening is guaranteed to
   win with the lock because, in the worst case, they can stall until Calming Verse."* -- Calming Verse does not
   target, so hexproof is no defence: under EE it destroys the creature AND its Auras (all enchantments we don't
   control). Orchestrator's reading for the fairness check, consistent with rulings 5, 6 and 8: (a) a hexproof
   castable creature that cannot reach power >= 5 with trample never breaks the lock (contained by chump blocks until
   a Verse); (b) one that CAN vetoes free turns only until we actually resolve a Calming Verse with EE out after it;
   that Verse is a new wipe point, so the "what could they cast in the gap" window restarts from it (ruling 6), and
   with Opalescence also out the opponent cannot rebuild lands after it.
12. *"(and to be clear, if it only hits 3 power or less you can actually attack freely with Enchanted Evening, holding
   back Parallax Wave to block."* -- context for (a): against a contained threat of power <= 3 the real clock is EE
   attacking (5/turn) with the Wave (4/4) held back as a blocker. The engine's passive opponent never attacks and a
   virtual (could-have-cast) creature is never on the board, so the engine lets every attacker swing; in those rare
   games our clock is slightly optimistic. Disclose in Stage 6a; no modelling change unless the USER asks.
   USER follow-up: *"Yes, we won't play that out, I'm just clearing up why and why not something might be a full
   lock."*
13. *"Note that not having a full lock because of a hexproof creature with enchantments is a real use case for Force of
   Vigor. In that situation the Force will often be able to reduce the size or remove trample, leading to an actual
   lock."* -- Force of Vigor targets the AURAS (not the hexproof creature), so it can take the threat below 5 power or
   strip trample and restore the full lock. Consistent with ruling 8 (only removal we actually cast counts), the
   orchestrator's DEFAULT design: the fairness check keeps the could-have-cast threat as a structured object (the
   creature + the Auras/equipment it could carry), and while such a threat vetoes the lock the deck may actually cast
   (hard-cast or end-step pitch) a Force of Vigor against up to two of those virtual Auras -- paying its real cost --
   after which the reach test is recomputed. A searched action, offered only while a virtual threat exists.
   USER: *"That sounds good."* (approved, 2026-11-25)

Reading: the lock is credited through the EXISTING free-turn rule (`MTG_FREE_TURNS`, default ON,
`src/core/FreeTurns.h`, `docs/design/opponent-lockdown-free-turns.md`): a locked opponent turn does not advance the
round. The deck's lock cards must join `GoldFishRunner::DeckMayLockOpponent`. Under EE + Opalescence the attackers are
Enchanted Evening 5/5 (MV 5) and Parallax Wave 4/4 (MV 4), plus the other non-Aura enchantments at P/T = MV.

## Stage 1 coverage (2026-11-25)

Missing (9): Enchanted Evening, Calming Verse, Sterling Grove, Enchantress's Presence, Opalescence, Force of Vigor,
Parallax Wave, Sunpetal Grove, Oblivion Ring. Implemented already: Utopia Sprawl, Wild Growth, Forest, Temple Garden,
Exploration, Windswept Heath, Plains, Idyllic Tutor, Wooded Foothills. Fetch-hold review owed (Heath, Foothills).

## Stage 2 work split

| group | cards | owner | status |
|---|---|---|---|
| simple | Sunpetal Grove, Oblivion Ring, Enchantress's Presence, Sterling Grove | implementer agent A | in progress |
| lock cluster | Enchanted Evening, Opalescence, Calming Verse, Parallax Wave, Force of Vigor | design agent B -> `docs/design/enchanted-evening-lock-design.md`, then an implementer | design in progress |

## Lock-cluster design (84a576461, `docs/design/enchanted-evening-lock-design.md`)

Tiers: EE + Opalescence Tier 3 (type refresh + explicit 0-toughness SBA sweep in both worlds; pre-float our lands
when the lock completes), Calming Verse Tier 3 (two variants: tap-all / not), Parallax Wave Tier 3 (exile ability,
linked exile list, self-exile loop, USER ruling-7 sweep at the opponent end step), Force of Vigor Tier 2 hard cast +
Tier 3 end-step pitch. Lock trait: EE plus one of Opalescence / Verse / FoV. Reach certificate OFF for this deck.

**Orchestrator correction to §4.4/§8 (Sterling Grove vs the Wave loop):** the doc's "`1 + G` counters, nets `4 - G`"
holds only for G = 1. With G >= 2 Groves each Grove shrouds the OTHER Groves (and the Wave), so the Wave can target
none of them and the self-exile loop is impossible (a Grove's own {1}-sacrifice needs mana, which the EE + Opalescence
lock has removed unless floated). The implementer must model the targeting legality, not the formula.

## Cards (tier, viewer bucket)

| card | tier | params | lockstep sites | viewer bucket | open |
|---|---|---|---|---|---|
| Sunpetal Grove | 1 | `produces` G/W, `checkland_subtypes` Forest/Plains | shared `LandWouldEnterTapped` | none | -- |
| Enchantress's Presence | 2 | `draw_on_cast_types` ["Enchantment"] | `FireOnCastTriggers` (both cast paths) draws via `TrickDraw`; site-10 put-in-hand fan reads the same predicate (`CastTypeDrawWatcherMatches`) | none (mandatory draw) | -- |
| Oblivion Ring | 2/3 | `etb_exile_nonland_until_leaves` | shared enter cascade (`ResolveEtbExileNonlandUntilLeaves`, every entry path); LTB in `FireLeavesBattlefieldTriggers` (`ReturnExiledUntilLeaves`); link = `kExileUntilLeavesBase` marker + `linked_exile_number` (already folded) | A: `target` (board click, full legal set) | **PROVISIONAL Q1** below |
| Sterling Grove | 3 | `grants_other_enchantments_shroud`, `sac_tutor_top_cost` {1}, `sac_tutor_top_types` [Enchantment]; derived `CardDefinition::activated_tutor`; deck stamp `deck_has_enchantment_shroud` | shroud: `EnchantmentShroudFromGrant` inside `LandHasShroud` / `CreatureHasShroud` / new `PermanentHasShroud` (so Aura host selection, equip/aura creature targeting and O-Ring all honour it); hook for EE = `PermanentIsEnchantmentNow`. Activation: new `PermAbilityMode::SacTutorTop` (CollectActions emission one variant per `RankedTutorCandidates` name; pin = `SacNamedPin.target` in BOTH trailing passes; `ApplyPermAbility` -> `ResolveSacTutorTop`; site-9 key + `CardHasPostEntryActivation`); upkeep window: `GameEngine::UpkeepTutorOptions` battlefield half + `AIEngine::CastUpkeepTutor` battlefield dispatch (shared by both worlds) + the BoundaryFan `tutor_` liveness + the human-play upkeep prompt | A: `main_phase` plan variants (tutor target); upkeep = the tutor chooser prompt | **PROVISIONAL Q2** below |

Provider: `EnchantedEveningProvider` (empty `DeckProvider` derivation, `Certificate()` = NotAssessed), routed FIRST on
`grants_other_enchantments_shroud || draw_on_cast_types` (lock-cluster params to be OR-ed in by its implementer).

Notes for the lock-cluster implementer: (a) Enchanted Evening must extend `PermanentIsEnchantmentNow`
(SpellEffects.h) so the Grove grant covers every permanent; (b) every own-target site it adds (Parallax Wave, Force of
Vigor) should consult `PermanentHasShroud`; (c) any leave path it adds (Wave's exile, FoV's destroy, an Opalescence
creature dying) must reach `FireLeavesBattlefieldTriggers` so O-Ring's linked card returns; (d) under Opalescence a
Grove is a creature: `SacrificeSourceAsCost` already routes it through the creature death chokepoint.

## Open USER questions

* **Q1 (PROVISIONAL, Oblivion Ring own-target pick).** The ETB is mandatory; with no opponent nonland permanent (2 of 10
  game indices schedule no spawn) it must exile one of OURS. Which one is a resolution pick
  (`DecisionProvider::EtbExileNonlandTargetPick`, default = the Acidic Slime ladder: creature first, tapped first,
  lowest MV), not a searched axis; whether to cast O-Ring at all IS searched. Default taken: the ladder. Sign-off, or
  ask for a searched target axis.
* **Q2 (PROVISIONAL, Sterling Grove timing).** Modelled timings: a main-phase activation (searched, one variant per
  candidate) and the upkeep window before the draw (searched). An activation at the opponent's end step is folded
  into the main-phase one; the only difference against the goldfish is that a Grove animated by Opalescence and
  activated in main 1 does not attack that turn. Also not offered: activating it in main 1 BEFORE the plan's casts
  (the trailing-activation pass runs after casts), so "tutor to top, then cast an Aura so Presence draws it" needs
  the next turn or a breakpoint continuation. Sign-off, or ask for a lead-position activation.

## Approved deferrals

_none_
