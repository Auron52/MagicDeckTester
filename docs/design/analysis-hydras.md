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
| Kalonian Hydra | 3 | implemented (I3 31d299879) | none | enters_with_plus_counters 4 + new `attack_double_team_counters`: `ApplyAttackDoubleTeamCounters` (SpellEffects.h) at the same point of GameEngine::CombatPhase and SimulateOneCombatAfterTitan (declared copies only; every own creature incl. sick bodies and animated lands; DoublePlusCounters). Projection addend `CountAttackDoubleTeamCountersPump` in PendingAttackDamageImpl (closed form, exact vs the realised combat; tested 8 / 14 / 32 / Primal Vigor). DeckFeedsCombat term. Trample inert (approved, bracket-noted); Scryfall 'Double' not tagged. |
| Primordial Hydra | 2 | implemented (I2 30dda4a48; verified + fixed I2b c6ba8603b) | A (X plan variants) | enters_with_x_plus_counters + new upkeep_double_self_counters (PerformUpkeepDoubleSelfCounters in GameEngine::UpkeepTail and SimulateEndAndStartNextTurn, after the spore upkeep; DoublePlusCounters). Conditional trample INERT (approved). X = 0 pruned by HydrasProvider (0/0 dies; kept beside a Gauntlet). Trample/Double not tagged. |
| Voracious Hydra | 2-3 | implemented (I2 30dda4a48; verified + fixed I2b c6ba8603b) | attack_mode (etb) + target (fight) | enters_with_x + etb_modal_double_self_counters / etb_modal_fight_opp_creature. Fight modelled (CR 701.14: mutual damage, lethal at the enter-cascade drain). Mode+target searched on tutor_target (`VF<n>`, #VM in both plan signatures); HydrasProvider::EtbFightTargets admits none (proof: opp spawns never block/attack; USER Apex ruling). Human: attack_mode (trigger etb) then the loyalty target chooser. |
| Neverwinter Hydra | 2-3 | implemented (I2 30dda4a48; verified + fixed I2b c6ba8603b) | A (X plan variants); dice = roll event | enters_with_x_dice_sides 6: RollDiceTotal from a seeded per-game prefix stream (Player::dice_rolled, folded into sim + dominance keys when > 0; salted like Fengraf). Both enter sites call EntersWithXCounterCount once. USER ruling: always max affordable X (HydrasProvider::XCastBelowMax, regardless of MTG_HYDRAS_MAX_X); human play offers every X. Ward/Trample inert tags. |
| Steelbane Hydra | 3 | implemented (I2 30dda4a48; verified + fixed I2b c6ba8603b) | main_phase (activation per target) | remove_counter_destroy_art_ench_cost -> PermAbilityMode::CounterDestroyArtEnch (no {T}; counter is the cost; target on sac_victim_id via the TapTargetPumpPin at both apply sites; ResolveCounterDestroyArtEnch). CounterDestroyArtEnchTargets hook: base = all legal; HydrasProvider drops our own Gauntlet/Bloom/Greaves (proof in code; any other own art/ench returns the full set). Sorcery-speed timing (approved deferral). |
| Nissa, Who Shakes the World (loyalty) | 3 | implemented (I3 31d299879) | main_phase + target (+1), main_phase (-8 count) | +1 `untap_land_animate` in ApplyLoyaltyAbility -> ResolveUntapLandAnimate (pre-tap fold for an untapped BASIC target, 3 counters, untap, permanent 0/0 Elemental w/ vigilance+haste). Target = one action per identity class of plain lands (tapped member preferred; proof in CollectActions) + the zero-target action (always emitted -- 'no target is dominated' is a list argument, not an identity, so not folded). Site-9 key kActLoyaltyUntapContinuation + bit-9 mark. OptimisticTurnMana declines to prune while an unactivated Nissa is out. -8 `fetch_forests_tapped`: every count 0..F (chosen_x = count+1, folded into both PW# signatures), tapped entry, shuffle. Emblem no-op (approved). |
| Garruk, Primal Hunter | 3 | implemented (I3 31d299879) | main_phase | +1 `beast_token_33` (CreateToken, green), -3 `draw_greatest_power` (CombatPowerOf max at resolution, TrickDraw; never gated on the pre-plan board), -6 `wurm_tokens_per_land` (CreateTokens). -3 marked bit 10 in PlanOpensBreakpoint; Jared's -6 (`regrow_multicolored`) marked too. Second main enabled via DeckUsesSecondMain keyed on `draw_greatest_power` (USER ruling): -3 offered in main 1 and main 2, search's choice. Greaves onto a fresh Beast/Wurm: site-9 key kActLoyaltyTokenContinuation while a haste Equipment is ours. |
| Three Visits | 1 | implemented (294e1317e; verified I1b) | A (tutor_etb) | Same params as Nature's Lore; untapped; no routing signature. |
| Vernal Bloom | 3 | implemented (294e1317e; verified I1b) | none | New `land_tap_bonus*` params fed into the land-aura bonus machinery (LandAuraBonus / LandAuraAddToPool / LandAuraColorMask / fold) + deck flag, so ~60 land-mana counting sites see it without per-site edits. |
| Nissa, Who Shakes the World (static) | 2 | implemented (294e1317e; verified I1b; loyalty abilities I3 31d299879) | none | Same machinery, Forests her controller taps. Same-turn credit needed (coordinate with loyalty work). |
| Gauntlet of Power | 3 | implemented (294e1317e; verified I1b) | A (choose_color) | Colour via Utopia Sprawl searched choice; provider may prune to green WITH PROOF (all lands basic Forest, all creatures green, opp spawns colourless). +1/+1 = new ComputeLordBonus pass. Must be excluded from EffectiveProducesFor like Sprawl. |
| Gargos, Vicious Watcher | 3 | implemented (I2 30dda4a48; verified + fixed I2b c6ba8603b) | none (fight trigger inert) | reduces_spell_subtype Hydra / amount 4. 601.2f fix: XSpellUnabsorbedReduction + XCastGenericDiscount at every X-cost site (executor, ApplyPlanDirect, SeqWalkFixedOrder, StampM1Hand, every CollectActions X branch). Fight trigger = approved inert deferral (bracket note). Legendary; Fight/Double not tagged (audit MODELED_ELSEWHERE). |

### Mana-core defects the integration must fix (D-mana.md)
1. PaymentManaCovers returns unpayable with an empty aura list on a Bloom board — CollectLandAuraFold must include the new bonus.
2. ManaCacheKey cannot distinguish pre/post-doubler boards (stale cached verdicts; Extraplanar Lens possibly exposed too).
3. LandSearchPickCredit credits a fetched Forest {G} from its definition, not 2-8 with doublers.
4. Gauntlet would read as a {G} producer in EffectiveProducesFor unless excluded.
5. Same-turn doubler credit (Brightcap Badger precedent): count Forests still untapped after paying.
6. X max is sized from the pre-cast board: 'doubler then max-X hydra' unfindable at any budget — fix with the mint-credit 'potential' pattern.
Lower priority: OptimisticTurnMana assumes a land drop adds <=3 mana; dominance key lacks Gauntlet colour (MTG_DOM_PRUNE only).

I1 status (294e1317e, verified by I1b): defects 1-5 fixed; OptimisticTurnMana's land-drop yield now includes
the bonuses (a bonus source in hand, or a land-put while one is live, removes the ceiling). Defect 6 is I2's.
Still open, all LATENT for this list (default-off or not on a Hydras path): `chosen_color` is not in the
Dominance.h key (MTG_DOM_PRUNE only); the SpellEffects.h discount-repay walk adds a source with no Permanent
(no land-tap bonus); Arbor Elf's `UntapLandHostsOfUncached` sees Aura hosts only, not a global bonus (an
Arbor + Bloom list would under-count); Extraplanar Lens is absent from ManaCacheKey (Extra Turns' owner).
HydrasProvider's Gauntlet green-only prune re-checked by I1b: every land is a basic Forest (Search for
Tomorrow / Nature's Lore / Three Visits find only Forests in this library), every creature card is green,
Garruk's tokens are green, Nissa's animated lands are colourless, the opponent's spawns
(GameEngine.cpp spawn site) carry no colour and its lands never tap -- so a non-green colour changes no
board, and the runtime composition check returns the full fan if the list ever breaks that. Human play is
asked at resolution with all five colours (FireOwnEtbTriggers' `g_play_etb_color_chooser`, menu =
land_tap_bonus_produces WUBRG), independent of the cast variant.

I1b verification (2026-11-11): `mtg-test` full suite PASS (incl. the 12 new cases in
test/unit/test_land_tap_bonus.cpp, which passed unchanged); `audit_card_costs.py` all 591 match;
`audit_card_fields.py`: the four I1 cards match every hard field (snapshot refreshed; the full --update hit
198 Scryfall 429s, so Gauntlet and Nissa were fetched in a second, targeted pass). Six PRE-EXISTING hard
keyword mismatches remain, all Gods cards (Faithless Looting, Mystic Retrieval, Golgari Grave-Troll,
Stinkweed Imp, Life from the Loam, Phenax): identical against the HEAD snapshot, the Gods owner's;
coverage: Three Visits / Vernal Bloom / Nissa / Gauntlet report `full`; `provider_audit.py --check` exit 0
(Hydras listed NOT AUDITED: no profile yet). Smoke (`MTG_PGO=0`, NOT accepted): 185 configs unchanged,
slower=0, faster=0; 2 configs play-changed at the same score (bruna2hg_smoke_d5_s1001 gi10,
landfall_smoke_d5_s1001 gi83 + gi98). PRE-EXISTING, not I1: a binary built from the parent fff726745
(src tree 1deeef4ed, pre-I1) reproduces the same three digests, and parent vs 294e1317e is byte-identical on
both keys. The drift is already in the parent tree (most likely the Gods merge f90930c9f, which combined two
engine sides); its owner should root-cause or accept it. Not an I1 leak.

I2 status (30dda4a48, verified and reviewed by I2b c6ba8603b, 2026-11-11): Primordial, Voracious, Neverwinter,
Steelbane and Gargos implemented; CR 601.2f (a reduction applies to the TOTAL cost after X) at every X-cost site;
defect 6 (same-plan X potential) fixed. I2b verification: `./build.sh` exit 0; `mtg-test` full suite PASS
(2790 cases, incl. 13 in test/unit/test_x_hydras.cpp and the 12 in test_land_tap_bonus.cpp);
`audit_card_costs.py` all 596 match; `audit_card_fields.py`: the five I2 cards match every hard field (oracle
advisories only; the 6 pre-existing Gods keyword mismatches unchanged); `provider_audit.py --check` exit 0;
`audit_viewer_decisions.py --no-sweep` exit 0; coverage: every name `full` except Kalonian and Garruk (I3).
Smoke (`MTG_PGO=0`, NOT accepted): 185 configs unchanged, slower=0, faster=0; only the 3 known pre-existing
same-score changes (bruna2hg_smoke_d5_s1001 gi10, landfall_smoke_d5_s1001 gi83 + gi98), digests identical to
I1b's run, so the shared X-cost changes left every existing X-spell deck byte-identical.
I2b review fixes and additions:
- Neverwinter max X under Gargos: `HydrasProvider::XCastBelowMax` tested "left >= x_pips", but under Gargos the
  {4} absorbs the first X steps, so X = 1 and X = 2 both cost {G}{G} and X = 1 survived as a non-maximal X
  (against the USER ruling). It now prices the NEXT X step's marginal generic exactly as CollectActions prices
  X (printed + X * pips - XCastGenericDiscount); with no reducer that is x_pips, as before.
- New focused test: the largest X is an enumerated, applied plan after a same-turn Vernal Bloom, Gauntlet of
  Power, Nissa, Nature's Lore under live doublers, Gargos, and Bloom -> Neverwinter (unlimited enumeration).
- New lockstep test: Voracious's rollout fight variant resolves exactly as the executor's fight pin.
- I1b's "the 12 land-tap cases passed" was a STALE binary: its unit log shows 2765 cases (none from
  test_land_tap_bonus.cpp), because line 120 named a field that never existed (`land_search_subtype`; the
  CardParams member has been `land_search_subtype_id` since 743586a37), so mtg-test failed to compile and the
  previous binary ran. I2 did NOT rename anything; its one-line fix is correct, and I2b added a `!= 0` check so
  the Three Visits / Nature's Lore comparison cannot pass vacuously. All 12 cases now run and pass.
- `land_search_subtype` (Nature's Lore / Three Visits) was an unclassified param in the viewer self-guard:
  classified INERT (a search filter, like `land_search_basic_only`). PENDING USER sign-off.
Open / for later: Neverwinter's max-X rule counts no same-plan producer credit, so in a plan that also casts a
Bloom / Gauntlet / Nissa a below-maximum Neverwinter X can survive (it can only under-drop, never remove the
true maximum); human-play check of the Voracious mode/target prompts and Steelbane's `destroy_target` label is
owed; MTG_HYDRAS_MAX_X A/B; cast order (USER-ruled, after all cards).

I3 status (31d299879, 2026-11-11): Kalonian Hydra, Nissa's loyalty abilities and Garruk implemented; every
name `full` in coverage (15/15, none missing). `./build.sh` exit 0; `mtg-test` 2803 cases PASS (13 new in
test/unit/test_hydras_walkers.cpp, confirmed running); 3 new scenarios PASS (test/scenarios/hydras_*.json);
`audit_card_costs.py` 598 match; `audit_card_fields.py` subset: hard fields match; `provider_audit.py --check`
exit 0; `audit_viewer_decisions.py --no-sweep` exit 0. Smoke (`MTG_PGO=0`, NOT accepted): 185 unchanged,
slower 0, faster 0; only the 3 known pre-existing same-score changes. Second main ON for Hydras (USER ruling),
keyed on Garruk's `draw_greatest_power`; its search-cost impact on Hydras is NOT measured yet. Details and
open items: logs/hydras-research/I3-handoff.md.

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
- RULED (USER 2026-11-10, *"Second main -3 makes sense."*) — main 1 vs main 2 for Garruk -3: implemented by I3 (DeckUsesSecondMain keyed on `draw_greatest_power`; nothing moved out of main 1; the search picks the main).
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
