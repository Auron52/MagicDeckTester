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
- RULED (USER 2026-11-10, *"Doublers and Gargos should be before the Hydras as you mentioned."*) — cast order: implemented by P1 (f4bf4ba72, `MTG_HYDRAS_CAST_ORDER`, default ON) and A/B'd below (P1 section). Keeping it ON is the USER's call; the measurement favours it.
- Noted (no deferral): the horizon leaf does not see next turn's counter doublings; this under-credits safely.

## P1 — cast order, baseline profile, cost, A/B (2026-11-11)

Commits: **f4bf4ba72** (cast order + per-job levers + tests), **3223ac6c7** (Stage 4 baseline profile).

**Cast order** (`HydrasProvider::CastOrderRank`, `MTG_HYDRAS_CAST_ORDER`, default ON, heurarm slot
`HYDRAS_CAST_ORDER`; TOTAL by name): Vernal Bloom 1; Gauntlet of Power, Nissa 2; Nature's Lore, Search for
Tomorrow, Three Visits 3; Gargos 4; Lightning Greaves 5; Garruk 6; Kalonian 7; Neverwinter / Primordial /
Steelbane / Voracious 8. The land drop keeps its default slot (first). `=0` gives the generic tiers
byte-for-byte (Gargos 8, creatures 10, the rest 20), which a unit test pins. Draft section 3 was checked
against the real params and needed no change. `MTG_HYDRAS_MAX_X` became a heurarm slot (still default OFF).
`MTG_HYDRAS_SECOND_MAIN` is new: default ON, and `=0` is only a measurement arm that prices Garruk's main 2;
the USER ruling stands. `mtg --cast-order-report` shows Gargos as "m2". That column is the informational
classifier only. HydrasProvider does not opt into ClassifiesMainPhases, so nothing is moved between mains.
Verification: mtg-test 2803/2803 plus 3 new cases (test_hydras_cast_order.cpp); smoke (MTG_PGO=0, not
accepted) shows exactly the 3 known pre-existing same-score changes and nothing else.

**Stage 4 profile**: `analyze_deck.py --no-rebuild` exit 0. Card scores come from 1000 games at d5 b20. There
is no value_play block, so play is the built-in d5 / b20. Stage-7 cost diagnostic: NO_COST_INTERACTIONS (no
game run). Discard analysis: STATUS_QUO_OK (5 decisions in 400 games). `provider_audit.py --check` exit 0
(Hydras -> Hydras, NotAssessed). Report: logs/hydras-p1/analyze.json.

**ONE pooled batch** (Release binary, MTG_PGO not used, 32 threads, heartbeat 32/32 at 10 min; ~33 min plus
one tail game). Contents: 4 arms x 2400 games, arms innermost in 50-game chunks, seed = 7007 + off,
game_index = off. Also Hydras cost shapes at s2002 (100 games each) and Hinata's 4 regression rows. Scratch
is on build/scratch/hydras-p1/; the readers are logs/hydras-p1/{mkmanifest,analyze_ab}.py. Unwon games score
9. Arm deltas are paired, arm minus base. base = cast order ON, max X OFF, m2 ON (the shipped shape).

| arm | avg | wins/2400 | paired delta vs base (se) | slower / faster | ms/game x base | units x base |
|---|---|---|---|---|---|---|
| base | 5.5287 | 2393 | - | - | 1 (5957 ms) | 1 |
| cast order OFF | 5.5308 | 2393 | +0.0021 (0.0009) | 5 / 0 | 0.962 | 1.010 |
| MAX_X ON | 5.5204 | 2393 | **-0.0083 (0.0019)** | 1 / 21 | 0.796 | 0.631 |
| m2 OFF (2399 games) | 5.5361 | 2392 / 2399 | +0.0075 (0.0037) | 48 / 30 | 0.482 | 0.526 |

(The ms and units columns are taken over the 47 chunks every arm finished. P1 killed m2-off gi 25, its own
measurement arm, after about 31 min unfinished; base takes about 11 min on that game.)
- Cast order: ON is better. No game is slower with ON. OFF is slower on gi 25 (7 v 6), 252, 891, 1032
  (8 v 7) and 1724 (6 v 5). So there is no slower game of the adopted arm to re-check at b0.
- MAX_X ON: better on average and cheaper (0.80x ms, 0.63x units). Its one slower game is gi 358 (seed 7365,
  T6 v T5). The b20 repro matches the batch. At d5 b0 it RECOVERS to T5 with the same digest as the base
  arm's d5 b0 line, and it takes 8.2x fewer units (337K v 2.77M). So the loss is budget-driven, not a
  deleted line, and it meets the CLAUDE.md adoption bar on this sample. Adopting it is the USER's decision.
- Second main: it costs **2.07x ms (1.90x units) per game** and buys 0.0075 turns (48 slower and 30 faster
  without it). It is more than half of Hydras' d5 cost.

**3x-Hinata cost** (core-ms per game, same run, same binary). `suite_gate.py --cost` cannot time a deck
that has no suite cases, so these come from the pooled batch:

| shape | Hydras | Hinata |
|---|---|---|
| d3 | 3,098 (b20) | 3,067 (b10) |
| d5 | 4,809 (b20, s2002, 100 games); 6,191 (b20, s7007, 2400 games) | 5,017 (b30, s2002 + s3003 pooled) |
| 2HG d5 | 7,110 (b20) | 7,215 (b30) = the gate's reference shape |

Worst Hydras shape against the Hinata reference: about **0.99x** (2HG), or 0.86x on the large d5 sample.
That is well under 3x. With m2 off, d5 would be about 0.40x. The cached Hinata figure (4,864) is from
another run and binary; today's numbers are Release with one extra analyzer thread running alongside.
**Heavy tail**: 176 games took over 30 s. gi 25 (seed 7032) takes about 11 min in every arm at under 1M
units. Typical games run 0.087 ms per unit; the slow ones run 1.3-3.4 ms per unit (30-40x). So the tail is
per-node cost, not units. perf on gi 1782 (seed 8789): plan enumeration under SolveSecondMainInSearch /
BpDeriveContinuationList, plus mana payment (TapForCostSharedOnceImpl, OrderPaysSequentially,
LandHasSubtypeNow, LandAuraBonus, PermanentManaYield; memset 8.8%). That is the Forest/doubler mana model,
re-evaluated per node.

**Open USER decisions from P1** (no default was changed):
1. MTG_HYDRAS_MAX_X: on this sample ON is better (-0.0083 +/- 0.0019 turns, 21 faster, 1 slower, and that
   game recovers at d5 b0) and cheaper (0.80x ms). It stays OFF until the USER rules.
2. Cast order: ON (the USER's ruling) is +0.0021 +/- 0.0009 better than generic, with 0 slower games. It
   ships ON.
3. Second main: it doubles Hydras' per-game cost (2.07x) for 0.0075 turns. The USER ruling keeps it on. The
   deck fits the 3x gate either way.

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

## P2/P3: max X adopted; the budget-vs-wall outliers (2026-11-11)

**Max X ADOPTED, default ON (f3e68c6fa).** USER 2026-11-11: *"Max x sounds good. Honestly, almost anything
that measures better and is cheaper is a good option."* The ruling answers P1's open decision 1 (-0.0083 +/-
0.0019 turns, 0.80x ms, 0.63x units over 2400 paired games; the one slower game, gi 358, recovers at d5 b0).
`EnvOn("MTG_HYDRAS_MAX_X", true)`; `=0` gives back the full X range. The heurarm slot, the comments and
test_x_hydras.cpp now name the ruling. The unit test checks both the default and the `=0` slot.

**The problem.** At d5 b20, the slow Hydras games ran at 1.3-3.4 ms per unit, against 0.087 for a typical
game. Units did not charge the work that took the time.

**Root cause.** Gargos (`reduces_spell_subtype` Hydra, 4) sets `block` in the selection-exact odometer gate.
So every selection that held him passed the gate. The flat gate then rejected them, and the ef rescue walk
ran on every one. That walk is `SubsetPayableSequential`: a GameState copy plus a real payment per cast.
- gi 1782: 2.39M walks, all but 4 false, about 74 G cycles.
- Units charged none of that work: 26.6M odometer visits and 9.1M payments came to 70k units.

A second leak survived the reducer fix. gi 1979 still walked 10.9M times, all false.
`LandTapCrossBoundCredit` gave a bonus across all candidates, which admitted Gargos + Nature's Lore / Search /
Three Visits selections. The bonus came from Vernal Bloom / Nissa / Gauntlet in hand, even when they were not
selected.

**The commits.**
- e1220eb61: payment builds the Islandized-land face on demand (`unique_ptr`). It used to be an empty
  `std::optional<CardDefinition>`, which GCC zero-filled at about 10.8 KB per source per pip; that memset was
  7.5-14% of the outliers' time. Byte-identical: 11 games, digests and units equal. About 3-5% wall.
- 09268788b: `MTG_SEL_GATE_EXACT`, default ON, heurarm slot `SEL_GATE_EXACT`. `SelGateExactRefuses` in
  TurnSolver.cpp runs at the EnumeratePlans odometer's two emission sites. It changes two terms:
  1. a generic same-turn reducer's discount is bounded at min(generic, the selected reducers' steps) per
     cast, instead of passing every selection outright;
  2. the land-tap x land-put cross term counts only the SELECTED bonus sources.

  It is SOUND: `MTG_SEL_GATE_EXACT_VERIFY=1` refused 23.9M (gi 1782), 26.9M (gi 1979) and 75.7M (gi 25)
  selections with 0 VIOLATIONS. It moves play only through the work-priced decision ceilings, which now trip
  later.

**Outlier numbers (P2, 11 games, d5 b20; the digests were identical in all three arms: the pre-P2 binary, gate
off, gate on).** ms per unit, base -> gate on:

| gi | base | gate on |
|---|---|---|
| 1774 | 1.80 | 0.62 |
| 1782 | 1.33 | 0.63 |
| 2062 | 1.25 | 0.46 |
| 1979 | 1.53 | 1.01 |
| 1034 | 0.50 | 0.27 |
| 132 | 0.71 | 0.36 |
| 313 | 0.18 | 0.10 |
| 25 | 0.36 | 0.21 |
| 252 | 0.77 | 0.62 |
| 28 (typical) | 0.052 | 0.048 |

- Total wall: 1559 s -> 1057 s.
- gi 1782: odometer visits 26.6M -> 3.5M, payments 9.1M -> 4.4M.
- gi 1774: odometer visits 28.8M -> 2.8M, payments 42.3M -> 9.2M.
- The gate-off slot moved units (not digests) on gi 28 / 313 by +50. Adding a heurarm slot changes the
  BP-key arm-hash constant.

**Held-out A/B (P3).** One pooled batch on the f3e68c6fa binary (Release, MTG_PGO not used, 32 threads):
- 3 arms x 2400 games, arms innermost in 50-game chunks; seed = 7007 + off, game_index = off.
- d5 b20, profile attached. Max X is ON in every arm (the default).
- The heartbeat was 32/32 at 10 min. The run took about 15 min.
- Unwon games score 9. Deltas are paired, arm minus base. Scripts: logs/hydras-p3/run_ab.sh and
  logs/hydras-p2/mkab.py. Summary: logs/hydras-p3/ab_summary.txt.

| arm | avg | wins/2400 | paired delta vs base (se) | slower / faster | ms/game (x base) | units x base |
|---|---|---|---|---|---|---|
| base (gate OFF) | 5.5204 | 2393 | - | - | 4436 (1) | 1 |
| gate (ON, shipped) | 5.5204 | 2392 | +0.0000 (0.0006) | 1 / 1 | 3450 (0.778) | 1.022 |
| gatechg (gate + MTG_ENUM_CHARGE w4) | 5.5263 | 2393 | +0.0058 (0.0016) | 14 / 0 | 2107 (0.475) | 1.779 * |

\* gatechg's units include the charged enumeration work, so they are not comparable with the other two arms.
Its ms figure is the real cost.
- The base arm reproduces P1's MAX_X ON arm exactly (5.5204). The cost fell from P1's 4742 ms/game to 4436,
  which is e1220eb61.
- Games over 30 s: 50 in base, 38 in gate, 20 in gatechg. The slowest game in base and gate is now gi 799
  (seed 7806): 724 s in base, 673 s in gate. The slowest in gatechg is 212 s.
- Gate's slower game is gi 1979 (seed 8986): unwon in the gate arm, T8 in base. Gate's faster game is gi 1782
  (T5 v T6).
- **Recovery of gi 1979 at `--depth 8 --budget-ms 0`, profile attached:** gate wins on T7, which is earlier
  than base's T8 at b20. The base arm at d8 b0 plays the identical game (same digest c8b8efa08c9df3a6, same
  249,844 units). So the loss is the d5 b20 budget, not a deleted line. The gate meets the adoption bar on
  this sample: net 0.0000, and every slower game recovers.
  The single-game b20 repros match the batch. Base wins on T8 at 152,646 units; gate is unwon at 333,353
  units. Those are the A/B's units, so the batch result is a faithful repro.
  (logs/hydras-p3/rec_manifest.json, rec.out, rec_wins.txt.)

**Not addressed.**
- **Breakpoint-continuation re-enumeration.** In gi 1782, 63% of the odometer visits (16.7M of 26.6M) are
  whole re-enumerations of the same board. They happen inside BP continuation derivations under different
  BP keys: 1262 of 12563 EnumeratePlans calls.
- The pay census still shows about 72% content-repeat walks across enumerations (gi 1979).

The remaining tail is these two plus genuine sequencing walks: gi 1979 still runs about 1.0 ms/unit, gi 25
about 0.21, and gi 799 is the new worst game. A collapse or a cache of the per-board enumeration across BP
keys is the next lever. It is a sound identity fold if the BP key does not change the plan list.

**OPEN USER DECISION: `MTG_ENUM_CHARGE` for Hydras.** It charges enumeration work to the budget. It is
default OFF, and turning it on is the USER's decision. There is no per-deck opt-in hook yet. The gatechg arm
measures it at w4:
- cost: 0.475x ms per game (2107 v 4436), and the 30 s tail falls from 50 games to 20;
- quality: +0.0058 +/- 0.0016 turns, 14 slower and 0 faster;
- **all 14 slower games recover at d8 b0** to base's turn: gi 252, 278, 841, 1419, 1614, 1748, 1857, 2062,
  2068, 2210 and 2320 on T5; gi 559, 694 and 1811 on T6.

So the loss is pure re-pricing (the charge is inert at unlimited budget). But on Hydras' own aggregate it is
+0.0058, and the CLAUDE.md bar is net <= 0. It does not meet the bar as it stands. A smaller weight, or a
charge applied only past the decision ceiling, is the obvious next measurement if the USER wants it.

## Claude-play sweep
- commit: `b7a4d6eb8`
- seeds: 50000-50015 (gi 0-15) games: 16
- driver: Claude-play sweep (Claude, Opus agents, per the USER's same-type rule)
- result: 16/16 matched the search's win turn (no game won faster or slower than `--depth 5 --budget-ms 20`)
- flags (each verified against cards.json and the rules; resolutions in F1 below):
  - g0 T5, BUG: same-plan Nissa x fetched-Forest under-credit. "Nissa, Nature's Lore, Three Visits, Gargos,
    Kalonian Hydra" was refused by the menu and `--validate-line`, and Voracious maxed at X=4 (6 real 8).
    FIXED, F1-A 79703da7d.
  - g4 T4, SUSPECT: "Vernal Bloom, Nature's Lore, Steelbane" offered X=1 only (X=2 pays). FIXED, F1-A 79703da7d.
  - ledger gap: Neverwinter's max-X counts no same-plan producer mana. FIXED, F1-A 79703da7d.
  - g4 T2, SUSPECT: "suspend Search for Tomorrow, Three Visits" drops Visits but read `not_verified`.
    Detector FIXED, F1-B 36b9798ad; the realised-order engine half is deferred to the USER
    (docs/design/costed-suspend-realised-order.md). Not lossy for Hydras (searched second main).
  - g2 T4, cosmetic: fight death labelled "0 toughness"; fight prompt `per_target_damage: 0`. FIXED, F1-C a31495872.
  - g6 T3, cosmetic: prompt typo "It is fights it". FIXED, F1-C a31495872.
  - g4, cosmetic: a Forest under Vernal Bloom showed `taps: G` only; `mdfc_backs` named cards not in the deck.
    FIXED, F1-C a31495872.
  - g6 T3 / g12 T3-T4, cosmetic: "X=0 Hydra + equip Greaves to it" offered with no Gauntlet. HARMLESS, kept
    (see F1).
  - g1 T6: the viewer plan cap dropped the Greaves group (documented `plans_truncated`). Not a bug; census in F1.
- flags: 0 unresolved

## F1 — the Claude-play sweep fixes (2026-11-11)

Worktree `.claude/worktrees/hydras-fix`, branch `hydras-sweep-fixes`. Commits: 79703da7d (A), 36b9798ad (B),
a31495872 (C).

**A. The bonuses a plan establishes now reach the lands it fetches** (`MTG_LANDPUT_CROSS_CREDIT`, heurarm
slot, default ON; `=0` gives the pre-F1 play).

*Root cause.* A plan's same-plan credits were each stamped off the board the plan was enumerated on:
- a land-tap bonus source's credit (`SamePlanLandTapBonusCredit`) counts only lands already on the board;
- a land-put's credit (`LandSearchPickCredit` -> `Action::rock_mana`) counts only bonus sources already on
  the board.

So the Nissa (or Bloom / Gauntlet) bonus on each fetched Forest was in no credit. Only the odometer's scalar
bound (`LandTapCrossBoundCredit`) had that cross term, as the TurnSolver comment said. Solve's `consider()`
and `EnumeratePlans` then priced the plan without it, and on a Forest-only board there is no real-payment
rescue. The plan was refused: one {G} short per fetched Forest. A second defect sat underneath:
`LandSearchCreditFilter` matched picks to the stamp EXACTLY, so once Nissa was live every pick out-credited
the stamp. The filter came back empty, the real-payment walk then put no land at all, and the resolution lost
its pin restriction.

*Fix.*
- `LandPutCrossCredit`: the flat pools add each selected (source, land-put) pair's bonus on the put's picks,
  when the realised order casts the source first.
- `LandSearchCreditFilter`: if the exact match is empty, it falls back to the picks whose bonus-free credit is
  within the stamp and whose live credit covers it.
- `XMaxSamePlanExactCredit`: the always-max-X test (Neverwinter; `MTG_HYDRAS_MAX_X`) counts only the
  same-plan producer mana that is exact in the realised order, so it still never drops the true maximum. This
  closes the ledger's "Neverwinter's max-X counts no same-plan producer credit".

*Tests.* Four `F1:*` cases in test_land_tap_bonus.cpp, each FAILING with `=0`:
- seed 50004: Steelbane X=2;
- seed 50000: Nissa, Lore, Visits, Gargos, Kalonian;
- the filter fallback;
- Neverwinter offered only at X=3 after a same-plan Bloom.

*Repros (fixed binary).* g0 decision 18: `--validate-line` for that line is now **accept** (plan 497).
Executing it puts Gargos and Kalonian on the battlefield, and Voracious now reaches X=6 (8 mana). g4 T4:
"Vernal Bloom, Nature's Lore, Steelbane Hydra (X=2)" is offered and `not_verified` (no drops).

*Was the line findable before?* As a main-1 plan, no, at any budget. The flat pool refused it, and no
rescue walk runs on a Forest-only board. A land search opens no breakpoint (the site-9 Expedition clause
aside), so the autonomous search has no mid-main continuation that could re-solve the leftover {G}{G}. The
human reached it only through the viewer's segment re-prompt. The search could reach the same END STATE only
by splitting the turn across its searched second main: Kalonian, or Steelbane at X=2, cast post-combat. That
loses pre-combat timing (a Greaves-hasted Hydra could not attack). From the g0 T5 board, both binaries hand
off to T6 at d5 b20 and d5 b0 (no T5 kill exists there, as the sweep found), so the game's win turn did not
move.

**B. "suspend Search for Tomorrow, Three Visits" (seed 50004 T2).**
- *Engine.* Every apply world pays a plan's suspend in its pre-cast pass. A mana-costed suspend therefore
  goes first, while the flat pools price the plan as a bag. The payable order (Visits, then the suspend off
  the fetched Forest) cannot be emitted as its own plan: plans have no position for a suspend.
- *Hydras.* Not lossy: "Visits in main 1, suspend in main 2" (the searched second main) reaches the same
  state. The `{suspend, Visits}` plan is a phantom, scored by what it realises.
- *Decks without a second main* (Landfall, Shadowborn, Rift Bolt lists) lose that line for the turn.
- Deferred to the USER: docs/design/costed-suspend-realised-order.md. It is a lockstep change at the five
  executor sites and the rollout pre-pass, and it moves those decks' GT.
- *Detector.* The ordering expansion probes drops only when a plan has two or more reorderable hand casts.
  It treats a suspend as fixed, so a one-cast plan was never applied and read `not_verified`. Human-play
  plans with a costed suspend and one cast are now probed. g4 T2 plan 1 now reads `known_drops:
  ["Three Visits"]`.

**C. Cosmetic / viewer** (all display-only; the event text feeds no digest: play events go to
`g_play_event_sink`, not the GameLogger fold).
1. `DrainPendingZeroToughness` names the real SBA. g2 T4 now reads "1/1 Creature dies (lethal damage)". A
   real 0/0 still reads "0 toughness" (the LHC Ballista scenario).
2. Loyalty-style prompts, the Voracious fight among them, no longer carry `per_target_damage` (it read 0).
   The viewer words them from `loyalty`.
3. A land raised by a land Aura or a global land-tap bonus publishes `yield` (a Forest under Bloom + Nissa
   shows 3).
4. `mdfc_backs` lists only back faces of cards in this game (empty for Hydras).
5. The fight prompt reads "It is fought by Voracious Hydra (each deals damage equal to its power to the
   other)".
6. "X=0 Hydra + equip Greaves to it" (no Gauntlet): KEPT, because it is harmless. The 0/0 dies to the SBA as
   it enters, the equip finds no host and is skipped, and Greaves' equip is {0}. The plan's outcome is
   therefore exactly that of the standalone "Greaves, Hydra X=0", which is also on the menu. Folding it would
   need a toughness-pump-aware host test at the equip-plan emission, to remove a duplicate the human can
   already see is dead. Not worth a second rule beside HydrasProvider's X=0 prune.
7. The viewer plan cap is `MTG_VIEWER_PLAN_CAP` (default ON), 65,536 odometer positions per enumeration
   (`MTG_VIEWER_PLAN_CAP_POSITIONS`). Over all 251 main-phase frames of the 16 sweep games it truncated 2
   (0.8%): g0 T5 (Voracious group, 1.06M positions in full) and g1 T6 (Greaves group, 221k). Census:
   logs/hydras-f1/viewer_cap_census.txt. The cap is unchanged.

**Verification (fixed binary, Release, `MTG_PGO=0`).**
- `mtg-test`: 2819/2819 PASS. The 4 `F1:*` cases FAIL under `MTG_LANDPUT_CROSS_CREDIT=0`.
- Smoke (not accepted at first): 188 configs unchanged; only `hydras_smoke_d0/d5` and `hydras2hg_smoke_d5`
  moved, at the same score (searched: 0 slower, 0 faster, 5 play-changed). No other deck moved: Hydras is the
  only list with a land-tap bonus source, so Landfall, Fungus and the other land-search decks are
  byte-identical, and the new heurarm slot moved nothing.
- **Held-out paired A/B.** One pooled batch, 2 arms x 2400 games, arms innermost in 50-game chunks,
  seed = 7007 + off, game_index = off, d5 b20, profile attached, 32 threads. The heartbeat read 32/32 at
  10 min, and the run took about 20 min (the gi 25 tail). Scripts: logs/hydras-f1/{mkab.py,run_ab.sh};
  summary: logs/hydras-f1/ab_summary.txt.

| arm | avg | wins/2400 | paired delta (se) | slower / faster | ms/game (x base) | units x base |
|---|---|---|---|---|---|---|
| base (`=0`) | 5.5204 | 2392 | - | - | 6752 (1) | 1 |
| fix (shipped) | 5.5171 | 2393 | **-0.0033 (0.0012)** | **0 / 8** | 5908 (0.875) | 0.889 |

- A/B, continued. Faster games: gi 448, 472, 497, 1109, 1446, 1919, 1979 (unwon -> T8) and 2358, each one turn.
  There is no slower game, so nothing needs a d8 b0 recovery check. The base arm reproduces P3's shipped
  5.5204. The fix is better and cheaper, so it meets the USER's bar (*"almost anything that measures better
  and is cheaper is a good option"*) on top of being a correctness fix.
- Regression tier, `--deck=hydras,hydras2hg`:
  - `hydras_regression_d0/d3/d5_s2002`, `_d5_s3003` and `hydras2hg_regression_d5_s2002` moved at the SAME
    score (0 slower, 0 faster; 30 searched and 9 d0 play-changed);
  - `_s30001` unchanged;
  - the viewer-protocol reference check: 817 refs, 0 play-drift, 0 board-diverged, 0 enum-gap.
- GT accepted deck-filtered for smoke and regression: 4 smoke keys and 5 regression keys, every one at an
  UNCHANGED aggregate (only the digest moved). `check_gt_logs.py`: 1097 consistent, 0 stale. The overnight
  `hydras*` keys are left to the periodic overnight run (CLAUDE.md).
- `ref_bench.py --deck Hydras`: search 5.300 vs the USER's 5.600, 0/10 short. Unchanged.
- **After the rebase onto 496b4ae35** (Shadowborn's `SB_RITUAL_AFTER_STONE` slot):
  - rebuilt; `mtg-test` 2824/2824;
  - full smoke: every `hydras*` key matched the accepted GT;
  - ONE other key moved: `alchemist2hg_smoke_d5_s1001`, gi 8, T6 -> T6 (same score, line changed). Its
    cause is the second new heurarm slot changing the BP-key arm-hash constant (the P2 note): both slots
    alone passed it, and the combination moves it;
  - regression `--deck=alchemist,alchemist2hg`: unchanged (0 changed);
  - accepted deck-filtered (`--smoke --deck=alchemist2hg`), sign 0.
