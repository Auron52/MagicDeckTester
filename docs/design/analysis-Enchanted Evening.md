# Analysis ledger: Enchanted Evening

Worktree `.claude/worktrees/enchanted-evening`, branch `ee-analysis` (from 05e2e0030). Nothing pushed yet.
Decklist: `decks/Enchanted Evening/Enchanted Evening.cod` (60 cards, GW enchantress / lock).

## CHECKPOINT (orchestrator, 2026-11-26 ~01:20 UTC) -- read this first after a compaction

* Done: Stage 1 coverage; lock-cluster DESIGN 84a576461 (`docs/design/enchanted-evening-lock-design.md`, with the
  orchestrator's Grove correction below); four simple cards WIP f986f6e19 (Release build OK; handoff
  `logs/ee/simple-cards-handoff.md`).
* RUNNING: a fresh "finish simple cards" agent (Opus) working the handoff's items: searched O-Ring target, same-turn
  lead-position Grove activation + budget-0 test, units, `audit_card_fields.py --update` re-run (the first ended rc=1,
  232 rate-limited; snapshot uncommitted), offline diff, smoke (expect play-changed=0), coverage. It reports with
  commit hashes + smoke numbers. Its own handoff (if any) = `logs/ee/simple-cards-handoff2.md`.
* NEXT (after it reports, same files so strictly after): launch the LOCK-CLUSTER implementer (Opus) from the design
  doc's §8 order, with USER rulings 1-13 below and the Grove correction in its brief; split into narrow agents
  (type refresh + Opalescence SBA + pre-float first) under the 250K handoff rule. Then: Stage 3 coverage loop, Stage 4
  profile, provider audit, suite addition (all three tiers, 3x Hinata gate), Stage 5 verification incl. 5d sweep.
* No USER question is open (rulings 8-13 answered the design's two questions).
* **LOCK STEP 1 (type statics: Enchanted Evening + Opalescence) DONE (2026-11-26):** code a9d866afe, Scryfall snapshot
  aea0e800e (update rc=0, offline diff rc=0). Units 4191/4191 (13 new, `test/unit/test_enchanted_evening_lock1.cpp`).
  Smoke (Release, `MTG_PGO=0`): 247 passed / 0 failed / 0 new, play-changed=0 (searched and d0), scenarios
  677/677 (EE is not in the suite yet). Sanity run (24 games, seed 7000, d3 b10, no profile): 23 won, avg 6.625; the
  lock formed in 18 of the 23 inspected (both sides 0 lands, opponent 0 permanents, later drops die, no fetch); the unwon game is a
  no-threat flood (Opalescence + Exploration only). SLOW-GAME 34-45 s on 3 games (no keep table yet). Coverage: EE + Opalescence `full`; missing = Calming Verse,
  Force of Vigor, Parallax Wave. Notes for steps 2-8: "Lock-cluster step 1" section below the Cards table. NEXT:
  step 2 (pre-float, §4.2).

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
| simple | Sunpetal Grove, Oblivion Ring, Enchantress's Presence, Sterling Grove | implementer agent A | DONE (f986f6e19, b5b8dd3cb): coverage `full` x4; units 4178/4178; smoke (Release) 247 PASS, play-changed=0; Scryfall snapshot 61e5bc848 (offline diff rc=0) |
| lock cluster | Enchanted Evening, Opalescence, Calming Verse, Parallax Wave, Force of Vigor | design agent B -> `docs/design/enchanted-evening-lock-design.md`, then an implementer | design DONE (84a576461); step 1 (EE + Opalescence type statics) DONE a9d866afe / aea0e800e; steps 2-9 to do |

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
| Oblivion Ring | 2/3 | `etb_exile_nonland_until_leaves` | shared enter cascade (`ResolveEtbExileNonlandUntilLeaves`, every entry path); LTB in `FireLeavesBattlefieldTriggers` (`ReturnExiledUntilLeaves`); link = `kExileUntilLeavesBase` marker + `linked_exile_number` (already folded) | A: `target` (board click, full legal set) | -- (target SEARCHED, D1 below) |
| Sterling Grove | 3 | `grants_other_enchantments_shroud`, `sac_tutor_top_cost` {1}, `sac_tutor_top_types` [Enchantment]; derived `CardDefinition::activated_tutor`; deck stamp `deck_has_enchantment_shroud` | shroud: `EnchantmentShroudFromGrant` inside `LandHasShroud` / `CreatureHasShroud` / new `PermanentHasShroud` (so Aura host selection, equip/aura creature targeting and O-Ring all honour it); hook for EE = `PermanentIsEnchantmentNow`. Activation: new `PermAbilityMode::SacTutorTop` (CollectActions emission one variant per `RankedTutorCandidates` name; pin = `SacNamedPin.target` in BOTH trailing passes; `ApplyPermAbility` -> `ResolveSacTutorTop`; site-9 key + `CardHasPostEntryActivation`); upkeep window: `GameEngine::UpkeepTutorOptions` battlefield half + `AIEngine::CastUpkeepTutor` battlefield dispatch (shared by both worlds) + the BoundaryFan `tutor_` liveness + the human-play upkeep prompt | A: `main_phase` plan variants (tutor target); upkeep = the tutor chooser prompt | -- (lead activation, D2 below) |
| Enchanted Evening | 3 (lock step 1) | `static_all_permanents_enchantments`; deck stamp `deck_has_type_statics` | `RefreshTypeStatics` (layer 4, every battlefield copy, both sides) at every `RefreshDevotionCreatures` chokepoint (enter-cascade top, `OnCreatureDies`, the leave helpers, bounce, Replenish, scenario loader); deaths at `TypeStaticsSbaCheckpoint` (inside `LegendRuleSbaCheckpoint`: post-resolution both worlds, land drop, end step; + `ResolvePassiveOpponentLandTurn`, both spawn sites, both turn starts) | none (static) | -- |
| Opalescence | 3 (lock step 1) | `static_animate_other_nonaura_enchantments_mv` | same refresh (layer 4 + 7b: other non-Aura enchantments = MV/MV creatures; two animate each other); 0/0s die at the checkpoint through `OnCreatureDies` (LTB fires: O-Ring returns its card); orphaned Auras (Sprawl / Wild Growth) swept; fetch on play suppressed both sides (`LandDiesToTypeStatics` in `PlayLandCore` and `PlayLeftmostLandFrom`), opponent Karoo finds no land | A: existing `attackers` | -- |

Provider: `EnchantedEveningProvider` (empty `DeckProvider` derivation, `Certificate()` = NotAssessed), routed FIRST on
`grants_other_enchantments_shroud || draw_on_cast_types || static_all_permanents_enchantments ||
static_animate_other_nonaura_enchantments_mv` (Calming Verse / Parallax Wave / Force of Vigor params to be OR-ed in by
their implementers).

## Lock-cluster step 1 (type statics) -- notes for the later steps

* **Where the state lives.** The refresh writes the permanent's own `Card` copy; what it added is recorded ON THE CARD
  (`Card::m_ts_mask` + `m_ts_saved_power/_tough`, in Card's tail padding: `sizeof(Card)` 136, `sizeof(Permanent)` 336,
  `sizeof(GameState)` 1744 -- all unchanged; `deck_has_type_statics` sits in a pad byte). A card that leaves keeps its
  bits, so an O-Ring / Parallax Wave return (step 4) is undone and re-derived by the first refresh after it re-enters;
  build the Wave's return through the enter cascade (or call `RefreshTypeStatics`) and nothing else is needed.
  Off-battlefield cards are NOT scrubbed (readers there use the definition, the Card.h convention); flagged only.
* **Deaths happen only at `TypeStaticsSbaCheckpoint`** (never inside the refresh: the enter cascade's callers hold slot
  indices). A new effect that can make a 0/0 outside a stack resolution / land drop / end step (Parallax Wave's
  opponent-end-step sweep, step 4; Calming Verse is a resolution, so it is covered) must call
  `TypeStaticsSbaCheckpoint(state)` itself at a point that holds no battlefield index, in both worlds.
* **Step 2 (pre-float) hook:** the lock completes inside `FireEtbWatchers` (the second of EE / Opalescence entering),
  and the lands die at the next checkpoint. `TypeStaticsOnBattlefield(state)` / `LandDiesToTypeStatics` are the
  presence tests to reuse; the pre-float must run before the refresh that makes the lands creatures (or at least before
  the checkpoint) in the shared resolution path. The whole-turn PREPAY (`BatchPrepayMainCasts`) may already have
  tapped lands before the lock-completing cast: check it when building step 2.
* **Wasted search to collapse later (step 8):** under EE + Opalescence our fetch-on-play variants (one per fetch target)
  all collapse to "the fetch enters and dies"; and any land play is dominated by holding it. Not folded in step 1.
* **Audit of the definition-reading battlefield sites (§2.1):** all 23 listed sites plus the new ones a fresh scan found
  (SE.h `PipFreeOutletOnBoard`, TS `AuraPlanHostKeyOn`, `HasteUnlockedManaOf`, the `poolaudit` diagnostic, Shadowborn /
  AntiLifegain / LandfallElementals / Stompy / Merfolk provider overrides) are param-gated, provider-gated (this deck
  routes to `EnchantedEveningProvider`, an empty `DeckProvider`), or read a hand / library / cast card. **No site needs a
  fix.** Note for step 3: keep Calming Verse on its own param, NOT `destroy_all_enchantments` (ManaPayment's
  `ApplyEnablerWipeRecheck` keys on that one; unaudited for this deck).
* **2HG:** a second head's land that dies goes to `players[1].graveyard` (the second head has no tracked graveyard; nothing
  reads it).

Notes for the lock-cluster implementer: (a) Enchanted Evening must extend `PermanentIsEnchantmentNow`
(SpellEffects.h) so the Grove grant covers every permanent; (b) every own-target site it adds (Parallax Wave, Force of
Vigor) should consult `PermanentHasShroud`; (c) any leave path it adds (Wave's exile, FoV's destroy, an Opalescence
creature dying) must reach `FireLeavesBattlefieldTriggers` so O-Ring's linked card returns; (d) under Opalescence a
Grove is a creature: `SacrificeSourceAsCost` already routes it through the creature death chokepoint.

## Done items (formerly provisional Q1 / Q2 -- settled by standing CLAUDE.md rules, not USER questions)

* **D1 (was Q1) -- Oblivion Ring's target is a SEARCHED axis** (settled by the NO-GREEDY rule: a provider may only
  restrict / order, never pick one of several without branching). TurnSolver's Oblivion Ring post-pass (beside
  Archdruid's Charm's) emits, in the decision space, one cast variant per legal target at emission
  (`EtbExileNonlandLegal`: both sides, shroud / hexproof honoured), pinned on `tutor_target` "OR<n>"
  (`ExileUntilLeavesPin`; `#OR` in BOTH plan signatures keeps the dedup lossless). The provider may only
  restrict / order them (`DecisionProvider::EtbExileNonlandTargets`, default every legal target). The unpinned base
  keeps the live-board pick (`EtbExileNonlandTargetPick`, the old ladder) -- the route to a permanent that enters
  earlier in the same plan, every put path, and the rollout leaves beyond the horizon; one legal target = no
  variant. A pin whose permanent is gone falls back to the base pick. Human play board-clicks the full legal set at
  resolution. Unit test: "Oblivion Ring target is a SEARCHED axis".
* **D2 (was Q2) -- Sterling Grove's same-turn "tutor to top, then cast an enchantment so Presence draws it"** (settled
  by the COMPLETENESS rule: it was unfindable at any budget, because the trailing pass runs after every cast). Each
  main-phase SacTutorTop variant now has a LEAD twin (`chosen_x == 1`, `TurnSolver::IsSacTutorTopLead`) in the
  decision space, applied before every cast by ONE body in both worlds (`TurnSolver::ApplySacTutorTopLeadActs`,
  called beside the morph lead in ApplyPlanDirect's and AIEngine::TakeTurn's pre-cast passes). Both twins sit in
  the source's one activation family (a plan holds at most one); chosen_x and the tutor target (`#F`, now tagged
  for SacTutorTop -- before this every candidate folded onto the first) keep them apart in every signature. Unit
  test: "Sterling Grove LEAD activation" (the plan is in the root enumeration, so it is findable at
  `--budget-ms 0`; both worlds draw the tutored card). **Note for the lock-cluster implementer:** the
  opponent-end-step activation (tutor at their end step, draw it in our draw step) is NOT built -- the upkeep window
  (before the draw) is today's nearest timing, but it spends THIS turn's mana where the end-step activation spends
  last turn's untapped mana; build it on the end-step window Force of Vigor's pitch needs.

## Approved deferrals

_none_
