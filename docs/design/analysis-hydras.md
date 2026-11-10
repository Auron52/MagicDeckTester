# Hydras deck analysis

Per-deck ledger for the analyze-deck workflow (`.claude/skills/analyze-deck.md`). It is the durable
state for this analysis across compaction and handoffs; keep it current after every milestone.

## Deck and scope

- Source: `decks/Hydras/Hydras.cod` (committed as-is 8c2a7aac3, 2026-10-08; routing pinned to Generic in
  `test/unit/test_pirates_provider.cpp`).
- Main deck: 60 cards, 15 names, mono-green, 23 Forest. No sideboard, no wish, so nothing outside the main is reachable.
- Intended line (working hypothesis): ramp on basic Forests (Search for Tomorrow, Nature's Lore, Three Visits),
  double Forest mana (Vernal Bloom, Gauntlet of Power, Nissa), cheapen Hydras (Gargos), then cast large X
  Hydras; Kalonian Hydra doubles counters on attack. Lightning Greaves gives haste.
- Earlier provisional cast-order draft (written before any card existed):
  `docs/design/cast-order-drafts-2026-10-14/Hydras.md`. Cast order is a USER decision; that draft is input.
- Analysis worktree: `.claude/worktrees/hydras`, branch `hydras-analysis`, based on
  `origin/phase-1-2-deck-analyzer` at c79006605 (2026-11-10).

## Stage 1 — coverage (2026-11-10)

- Full: Forest, Search for Tomorrow, Nature's Lore, Lightning Greaves.
- Missing (11): Kalonian Hydra, Neverwinter Hydra, Primordial Hydra, Steelbane Hydra, Voracious Hydra,
  Gargos, Vicious Watcher, Vernal Bloom, Nissa, Who Shakes the World, Gauntlet of Power, Three Visits,
  Garruk, Primal Hunter.
- Existing precedents in cards.json: Mossborn Hydra / Scythecat Cub (counter doubling), Walking Ballista
  (enters with X counters), Utopia Sprawl / Overgrowth / Wild Growth (extra mana on a land tap), Ruby
  Medallion / Goblin Warchief (cost reduction), Apex Altisaur (fight), Gideon / Oko / Chandra (loyalty).
- Routing hazard noted in the cast-order draft: params `tutor_to_hand` / `fetch_land_types` set the `anti`
  archetype signature and would route the deck to AntiLifegainProvider. Stage 4a gives Hydras its own provider.

## Stage 2 — research and implementation

Research fan-out (one Opus agent per card group, read-only, compact drafts):
A Kalonian + Primordial · B Voracious + Neverwinter + Steelbane · C Gargos · D Vernal Bloom + Gauntlet +
Three Visits + Nissa's static · E Nissa's loyalty abilities + Garruk.

| Card | Tier | Status | Viewer bucket | Notes |
|---|---|---|---|---|
| Kalonian Hydra | 3 | researched (A-counter-doublers.md) | none | enters_with_plus_counters 4 (Mossborn route). New `attack_double_team_counters` via shared DoublePlusCounters helper at the same point in GameEngine::CombatPhase and SimulateOneCombatAfterTitan (declared attackers only, doubles every own creature). REQUIRED projection addend in PendingAttackDamageImpl (else a lone swing reads 4 not 8). Add param to DeckFeedsCombat. Scryfall 'Double' keyword must be omitted (loader throws). |
| Primordial Hydra | 2 | researched (A-counter-doublers.md) | A (chosen_x plan variants) | enters_with_x_plus_counters; new `upkeep_double_self_counters` in GameEngine::UpkeepTail + SimulateEndAndStartNextTurn after spore step. X=0 prune proposed sound/default-on; 'max X' narrowing is LOSSY -> USER + A/B. Viewer plan-space limit risk with many X variants (Stroke of Genius precedent). |
| Voracious Hydra | 2-3 | researched (B-x-hydras.md) | B? modal ETB (mode choice must reach search + viewer) | enters_with_x_plus_counters; ETB choose one: double counters (DoublePlusCounters) OR fight target opp creature. Agent B claimed fight never legal — WRONG (opp spawns exist 8/10 games). Fight is DOMINATED in goldfish (opp creatures never block; fight can only damage our Hydra) -> provider prune to 'double' with stated proof is permitted; human play must surface both modes. Trufflesnout mode channel unusable (it carries mode in chosen_x). |
| Neverwinter Hydra | 2-3 | researched (B-x-hydras.md) | A (chosen_x) | Live Oracle {X}{X}{G}{G}; enters with counters = roll X d6 (no dies trigger). Proposed `enters_with_x_dice_sides: 6` from a seeded per-game dice sequence + Player::dice_rolled in state keys. Ward/trample inert. |
| Steelbane Hydra | 3 | researched (B-x-hydras.md) | A (target) | enters_with_x_plus_counters; activated {2}{G}, remove a counter: destroy target artifact/enchantment — only legal targets here are OUR Gauntlet/Greaves/Bloom (opp spawns are creatures only). Proposed new activated mode + provider target filter (default excludes own unless it pays off); human play sees all targets. |
| Nissa, Who Shakes the World (loyalty) | 3 | researched (E-walkers.md) | A (target, plan variants) | Loyalty 5; +1 permanent animation (0/0 Elemental, 3 counters, vigilance, haste, untap) as new `untap_land_animate` in shared ApplyLoyaltyAbility (Part the Waterveil awaken code); untap mana via new breakpoint key (Chandra +RR precedent). Two proposed target shortcuts (tap untapped target for mana first; prefer tapped Forest) — VET as exact dominance folds before adopting. -8: Forest count is searched (chosen_x into plan signature, guarded). Mana-ceiling prune must not prune while an unactivated Nissa is on board. Gauntlet does NOT pump the animated land (colourless); Kalonian DOES double it. |
| Garruk, Primal Hunter | 3 | researched (E-walkers.md) | A | Loyalty 3. +1 Beast, -3 draw = greatest power (counters+Gauntlet), -6 Wurm per land. REQUIRED: flag -3 draws in PlanOpensBreakpoint (bit 10) or drawn cards get one fixed follow-up (Jared -6 same hole). |
| Three Visits | 1 | researched (D-mana.md) | A (tutor_etb) | Same params as Nature's Lore; untapped; no routing signature. |
| Vernal Bloom | 3 | researched (D-mana.md) | none | New `land_tap_bonus*` params fed into the land-aura bonus machinery (LandAuraBonus / LandAuraAddToPool / LandAuraColorMask / fold) + deck flag, so ~60 land-mana counting sites see it without per-site edits. |
| Nissa, Who Shakes the World (static) | 2 | researched (D-mana.md) | none | Same machinery, Forests her controller taps. Same-turn credit needed (coordinate with loyalty work). |
| Gauntlet of Power | 3 | researched (D-mana.md) | A (choose_color) | Colour via Utopia Sprawl searched choice; provider may prune to green WITH PROOF (all lands basic Forest, all creatures green, opp spawns colourless). +1/+1 = new ComputeLordBonus pass. Must be excluded from EffectiveProducesFor like Sprawl. |
| Gargos, Vicious Watcher | 3 | researched (C-gargos.md) | none (fight trigger inert) | Live Oracle {3}{G}{G}{G} Legendary Hydra 8/7 vigilance; trigger is spell-only, no optional cost. Engine bug: `reduces_spell_subtype` reduction is floored on PRINTED generic before X is added (EffectiveSpellCostImpl ~4053), violating 601.2f, so X-Hydras get no discount; X range in CollectActions under-enumerated. Fix at executor/apply/CollectActions/SeqWalk/StampM1 sites (Hinata X-discount precedent). Needs inert Keyword::Fight. Routing: reduces_spell_subtype alone -> GoblinsProvider; Hydras needs own signature above fungus/goblin. |

### Mana-core defects the integration must fix (D-mana.md)
1. PaymentManaCovers returns unpayable with an empty aura list on a Bloom board — CollectLandAuraFold must include the new bonus.
2. ManaCacheKey cannot distinguish pre/post-doubler boards (stale cached verdicts; Extraplanar Lens possibly exposed too).
3. LandSearchPickCredit credits a fetched Forest {G} from its definition, not 2-8 with doublers.
4. Gauntlet would read as a {G} producer in EffectiveProducesFor unless excluded.
5. Same-turn doubler credit (Brightcap Badger precedent): count Forests still untapped after paying.
6. X max is sized from the pre-cast board: 'doubler then max-X hydra' unfindable at any budget — fix with the mint-credit 'potential' pattern.
Lower priority: OptimisticTurnMana assumes a land drop adds <=3 mana; dominance key lacks Gauntlet colour (MTG_DOM_PRUNE only).

## Integration plan (serial, one fresh agent per group, each builds + unit-tests + commits)
- I1 mana core: Vernal Bloom, Gauntlet, Three Visits, Nissa static (card entry w/ static only if loyalty not yet in), defects 1-5, HydrasProvider skeleton + signature.
- I2 X + Gargos: 601.2f reduction-after-X fix at every X-cost site, defect 6 (X potential), Primordial, Voracious (modal), Neverwinter (dice), Steelbane, Gargos, inert keywords Fight/Double.
- I3 combat + walkers: Kalonian attack trigger + PendingAttackDamage addend, Nissa loyalty, Garruk + PlanOpensBreakpoint bit.

## Provisional deferrals (need USER sign-off)

- PROVISIONAL — Gargos fight trigger ("Whenever a creature you control becomes the target of a spell, Gargos fights up to one target creature you don't control"): inert because NO SPELL in this deck targets a creature we control (Greaves' equip, Nissa, Garruk, Voracious are abilities, not spells). CORRECTION 2026-11-10: the opponent DOES have creatures — GoldFishRunner::PopulateOpponentSpawns gives 1/1..6/6 non-blocking creatures in 8 of 10 games (gi%10 patterns 2-9), unconditionally — so 'no opponent creatures' is NOT a valid reason here. (Apex Altisaur's committed bracket note makes that false claim; flagged to the USER.)

- PROVISIONAL — Trample on Kalonian and Primordial (conditional): inert — opponent creatures exist in 8/10 spawn patterns but never block; combat has no blocker path; nothing reads trample.
- PROVISIONAL — Neverwinter Hydra dice: rolled from a seeded per-game sequence that the search can see before choosing X (clairvoyant, like the known library order). Alternative: expected value (non-clairvoyant). Recommended: seeded.
- PROVISIONAL — Steelbane Hydra activation timing collapsed to main phase (instant-speed is inert vs a passive opponent).
- PROVISIONAL — Nissa -8 emblem ("lands you control have indestructible"): no-op; nothing in this game destroys our lands.
- USER DECISION (open) — main 1 vs main 2 for Garruk -3: after a Kalonian attack, -3 draws more. DeckUsesSecondMain has no rule today. To be measured and reported; the user decides.
- PROVISIONAL — Gauntlet +1/+1 applied to its controller's creatures only (opp spawns are colourless and the opponent casts nothing).
- PROVISIONAL — Three Visits never fails to find (same simplification Nature's Lore already makes).
- USER DECISION (open) — cast order: doublers before creatures (draft recommendation) vs generic; to be A/B'd and reported.
- Noted (no deferral): the horizon leaf does not see next turn's counter doublings; this under-credits safely.

## Stage 5 verification

(not started)

## Approved deferrals

USER 2026-11-10: *"All of the deferrals are okay."* — covers every PROVISIONAL item listed above
(Gargos fight trigger, trample on Kalonian/Primordial, Steelbane sorcery-speed timing, Nissa -8 emblem,
Gauntlet +1/+1 controller-only, Three Visits never fails to find). Neverwinter's dice model is superseded
by the X ruling below.

## USER rulings (verbatim, 2026-11-10)

- Cast order: *"Doublers and Gargos should be before the Hydras as you mentioned."*
- Max X: *"I think we should play around with the maximum X and it is probably not going to lose lines,
  since a huge creature will usually kill before we deck out."* -> build a max-X narrowing in
  HydrasProvider and measure it (A/B vs full X range) rather than refusing it outright.
- Second main: *"Second main -3 makes sense."* -> enable the second main for Garruk's -3 after combat.
- Neverwinter Hydra: *"For Neverwinter I would rather not have it figuring out how to game X. Trying the
  maximum is a good way to minimize this."* -> Neverwinter casts at maximum affordable X so the search
  cannot pick X against the known dice results.
