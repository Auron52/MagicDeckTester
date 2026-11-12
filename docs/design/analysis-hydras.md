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

V1 (2026-11-11), worktree `.claude/worktrees/hydras`, binary built at dac7cf590 (src tree 587dcce67), snapshot
`build/scratch/hydras-v1/bin/mtg`. Scripts and summaries: `logs/hydras-v1/`.

### 5h — play-viewer decision surface

**Auditor.** `audit_viewer_decisions.py decks/Hydras/Hydras.cod decks/Hydras/Hydras.profile.json` (exit 0):
15 definitions; expected types `attack_mode`, `choose_color`, `target`, `tutor_etb`; the 40-game sweep from
seed 9001 surfaced all four. **5h PASS, 0 HARD MISS, 0 UNVERIFIED**, so no `--verify-card` run was owed.
(`logs/hydras-v1/audit5h.txt`.)

**Oracle-text advisories, triaged.**
- Voracious Hydra `modal`: a blind spot in the cross-check, not a dropped choice. The mode IS modelled
  (`etb_modal_double_self_counters` -> `attack_mode`, trigger etb) and surfaces with both modes (repro below).
  `modeled_tokens()` now maps that param to `modal`, so the advisory is gone.
- Gargos `target` ("fights up to one target creature you don't control"): the approved inert deferral (no
  spell in the list targets a creature we control). Disclosed in 6a.

**Human-play repros** (`--claude-play`, `logs/hydras-v1/drive5h.py` drives a goal-seeking line and stops at the
prompt; every list below is the FULL option list, checked against the board):

| prompt | seed / gi / CSV | what the human is offered |
|---|---|---|
| Gauntlet of Power colour | s4 gi5, `1,1,0,0,0,-1,-1,0,5,0,-1,-1,6` | `choose_color`: White, Blue, Black, Red, Green (default Green) |
| Voracious Hydra mode | s4 gi5, `1,1,0,0` then plan 1 | `attack_mode` (trigger etb): 0 double the counters, 1 fight |
| Voracious Hydra fight target | s4, `1,1,0,0,1,1`, gi 2 / 3 / 9 / 4 / 6 | `target`: every opponent creature -- 4 x 1/1, 2 x 2/2, 2 x 1/1, 1, 1. On gi 7 / 8 the opponent has no creature on T2, so the fight mode has no legal target and the mode is not asked (CR 700.2b): correct |
| Nissa +1 | s9 gi5, the `drv_nissa8_9` CSV (T7) | two plans: the target plan -> `target` listing the 5 NONCREATURE Forests (the 3 Forests her earlier +1s animated are correctly excluded), and the zero-target plan ("up to one") |
| Nissa -8 count | same frame | every count 0..15 = every Forest left in the library (15), alone and inside larger plans. 6,100 plans; the JSON shows the top 200 (`plans_total`, `MTG_PLAY_PLANS_CAP`), the engine-side list is complete |
| Garruk +1 / -3 | s18 gi5, T4 | both offered (loyalty 4; -6 correctly absent) |
| Garruk -6 | s22 gi5, T6 (loyalty 6) | +1, -3 and -6 all offered (11 plans each) |
| Steelbane destroy target | s2 gi5, T4, `1,1,0,0,1,0,0,0,2,0,0,0` then plan 1 (Vernal Bloom) | after the Bloom resolves, the continuation offers one activation per legal target: Lightning Greaves AND Vernal Bloom, both ours. The human sees every target; HydrasProvider's own-permanent prune is search-only |

**`land_search_subtype` -- why it is INERT as a decision (needs the USER's sign-off).** The param is Nature's
Lore's and Three Visits' "search your library for a **Forest** card": a LEGALITY filter
(`LandSearchSpec::subtype`, applied in LandSearch.h's candidate test: a library land without the Forest subtype
is not a legal find). It creates no choice of its own. The choice -- WHICH card, or none -- is the `tutor_etb`
prompt that `land_search_on_resolve` already raises, and that prompt is built from the filtered list, so the
filter decides what the prompt lists, not whether there is one. The references show it: 11 `tutor_etb` prompts
from Nature's Lore / Three Visits across the USER's 10 games. For **Hydras** the filter is moreover non-binding:
the only land in the 60 is the basic Forest (23), so every candidate is a Forest either way and the prompt
offers one identity class (`Forest`) or the decline. It would bind only in a list holding non-Forest lands
(it excludes them) or nonbasic Forests (it admits them where `land_search_basic_only` would not): Skyshroud
Claim and Karametra carry it too; no other suite deck plays one of the four cards except Gods (Karametra,
alongside a single Forest). The audit row stays INERT, marked PENDING the USER.

**Observations, not defects.**
- Steelbane's menu offers the activation only against permanents on the board when the frame is
  enumerated: in "Vernal Bloom, Steelbane: destroy ... -> Lightning Greaves" the Bloom cast earlier in the same
  plan is not offered as a target (the human reaches it through the segment re-prompt). Destroying our own
  doubler is never wanted; not worth a change.
- `HydrasProvider::XCandidates` keeps X = 0 for Primordial / Neverwinter / Steelbane whenever a Gauntlet is in
  HAND, even when the Gauntlet cannot be cast in the same plan. Regression s2002 gi 8 (d5 b20, T7) casts
  Neverwinter at X = 0 on T2 beside a hand Gauntlet: a 0/0 that dies and wastes the card. The game still wins
  on T7, and d5 b200 and d5 b0 (unlimited) play the same T2 cast and the same T7 win, so it cost nothing
  there and the search rates it a tie. A tighter exception (a Gauntlet on the
  battlefield, or castable in the plan before the Hydra) is a possible follow-up; it would move play, so it is
  the USER's call.

### 5b — multi-depth sanity

Accepted GT (`test/regression_gt.txt`), `hydras*` keys:

| tier | d0 | d3 (b10) | d5 (b20) | 2HG d5 |
|---|---|---|---|---|
| smoke s1001 | 6.0600 | 5.5800 | 5.5500 | 5.9600 |
| regression s2002 / s3003 | 6.0320 | 5.4267 | 5.4133 / 5.5600 | 5.7467 |
| overnight s4004-s10010 | 6.02-6.07 | 5.6080 | 5.47-5.59 | 5.9000 |

**Monotone** at every tier: d0 > d3 >= d5. **Plausible**: the deck ramps on T1-T3 and kills with one or two big
X Hydras; 59% of d5 wins land on T5, 36% on T6, 3% on T7 (s2002, 150 games), and the USER's 10 references play
5.60 against the search's 5.30. 2HG's 30 team life costs ~0.3 turns.

**Per-game, current binary** (`logs/hydras-v1/mk5b.py`, ONE pooled batch, `MTG_DUMP_WINS=1`; it reproduces the
accepted GT digests exactly: d0 773d5978, d3 b10 597af3b8, d5 b20 f3d66977). Summary: `logs/hydras-v1/b5_summary.txt`.

| pair (s2002) | paired | d5 slower | d5 faster |
|---|---|---|---|
| d5 b20 vs d3 b10 (the suite rows) | 75 | **0** | 1 (gi 40, T6 -> T5) |
| d5 b20 vs d3 **b20** (same budget) | 150 | **0** | **0** (identical win turns, different lines) |
| d3 b10 vs d0 | 75 | 0 | 26 |

The accepted GT agrees on the other seed sets: smoke s1001 (50 paired) 0 slower / 1 faster; overnight s4004
(125) 0 slower / 2 faster, and the two games unwon at d5 there (gi 86, 94) are unwon at d3 with the same digest
(no Hydra reachable). **There is no game slower at d5 than at d3, so nothing needs a 5c classification.**
The one moved game, gi 40, is a BUDGET effect: d3 also wins it on T5 at b20.
Finding worth stating plainly: **at equal budget, d5 buys nothing over d3 on this sample** (150/150 identical
win turns), at 1.50x the core time (312 s v 208 s for 150 games). The deck's turn is decided by "ramp, then
the largest X Hydra" and two plies see it. Play depth is the USER's setting (the depth floor of 5 is a USER
rule), so this is information, not a proposal to drop depth.

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
    FIXED, F1-A 7b621f2f7.
  - g4 T4, SUSPECT: "Vernal Bloom, Nature's Lore, Steelbane" offered X=1 only (X=2 pays). FIXED, F1-A 7b621f2f7.
  - ledger gap: Neverwinter's max-X counts no same-plan producer mana. FIXED, F1-A 7b621f2f7.
  - g4 T2, SUSPECT: "suspend Search for Tomorrow, Three Visits" drops Visits but read `not_verified`.
    Detector FIXED, F1-B a8f27828c; the realised-order engine half is deferred to the USER
    (docs/design/costed-suspend-realised-order.md). Not lossy for Hydras (searched second main).
  - g2 T4, cosmetic: fight death labelled "0 toughness"; fight prompt `per_target_damage: 0`. FIXED, F1-C 5bf4a7b5d.
  - g6 T3, cosmetic: prompt typo "It is fights it". FIXED, F1-C 5bf4a7b5d.
  - g4, cosmetic: a Forest under Vernal Bloom showed `taps: G` only; `mdfc_backs` named cards not in the deck.
    FIXED, F1-C 5bf4a7b5d.
  - g6 T3 / g12 T3-T4, cosmetic: "X=0 Hydra + equip Greaves to it" offered with no Gauntlet. HARMLESS, kept
    (see F1).
  - g1 T6: the viewer plan cap dropped the Greaves group (documented `plans_truncated`). Not a bug; census in F1.
- flags: 0 unresolved

## F1 — the Claude-play sweep fixes (2026-11-11)

Worktree `.claude/worktrees/hydras-fix`, branch `hydras-sweep-fixes`. Commits: 7b621f2f7 (A), a8f27828c (B),
5bf4a7b5d (C).

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

### 5c2 — horizon-honest tie-break

`leaf_tiebreak_check.py decks/Hydras/Hydras.cod` at PLAY settings (d5 b20), both arms in one pooled batch per
run, binary 66050a983, `MTG_PGO=0`, 32 workers (heartbeat 32/32). The first run (12 blocks x 1000, seed base
1,000,000) read NO SIGN (5 changed games) and suggested ~25 blocks, so the re-run added 13 DISJOINT blocks (seed
base 2,200,000) instead of replaying the first 12; the counts are additive. `logs/hydras-v1/leaf_summary.txt`,
`leafb_summary.txt`.

| run | paired | net turns | worse | better | changed |
|---|---|---|---|---|---|
| blocks 1-12 (half A / half B: -1 / -2) | 12,000 | -3 | 1 | 4 | 5 |
| blocks 13-25 (half A / half B: +1 / 0) | 13,000 | +1 | 1 | 0 | 1 |
| **all 25 blocks** | **25,000** | **-2** | **2** | **4** | **6 (0.024%)** |

**Verdict: the tie-break is UNBINDABLE on Hydras; KEEP THE DEFAULT (ON), no provider override.** It still read
NO SIGN at 25,000 paired games (the script now asks for ~131 blocks, about 5 h of the box, to reach 20 changed
games). That run was not made: at 6 changed games in 25,000 the lever moves the average by at most
~0.0002 turns per game in either direction, and the script's own rule applies ("a lever that never fires costs the
deck nothing"). Why it cannot fire: the deck wins inside the horizon (T5-T6, d5), so a rollout rarely reaches the
horizon without a win, and when it does, Hydras' damage is combat that opponent life already measures (its stored
value -- counters on a Hydra -- turns into damage within a turn). Recorded in
`docs/design/horizon-honest-leaf.md` §4e.

### 5i — discard: the BUCKET policy (PROPOSAL for the USER; built DEFAULT OFF)

**Does the deck reach the cleanup site?** Barely. Hydras discards ONLY at cleanup: no card in the list has a
discard cost or a discard trigger, so the non-cleanup sites (`MTG_NONCLEANUP_SHED_WORST`'s axis) do not exist
here. The evidence pass (`analyze_deck.py --discard-analysis --no-rebuild`: 400 games, d3 b10, the searched
labeller) found **6 real cleanup sheds in 400 games (0.015 per game)**, every one a TIE (multi-optimal 100%:
whichever card goes, the searched result is the same), generic regret 0; verdict STATUS_QUO_OK, no derived order,
so no order A/B was owed. The ON re-run (same config, `MTG_HYDRAS_BUCKET_DISCARD=1`) found 5 sheds, all ties,
**regret 0, optimal 100%**: the rule-vs-searched gate is clean (nothing to classify as churn or clairvoyance).
The deck dumps its hand: 23 Forests and X sinks mean it is rarely holding eight cards at end of turn.

**The policy** (`HydrasProvider::CleanupDiscardCandidates`, `MTG_HYDRAS_BUCKET_DISCARD`, heurarm slot, DEFAULT
ON since 2026-11-11 (see "Adopted" below); `=0` is the generic max-MV ranking byte-for-byte, pinned by
`test_hydras_discard.cpp`). Roles are read
off each card's PARAMS, never its name:

| bucket | cards | quota (net of board) |
|---|---|---|
| LANDS | Forest | 2 while fewer than 5 lands are out, else 1 (the land drop) |
| RAMP | Nature's Lore, Three Visits, Search for Tomorrow | 1 while fewer than 6 lands are out; Lore / Visits ({1}{G}, untapped) before Search |
| DOUBLERS | Vernal Bloom, Gauntlet of Power, Nissa | 2 minus the doublers already out; castable-next-turn first, then cheapest (Bloom) |
| GARGOS | Gargos | 1 unless one is out; a second copy (or one beside Gargos) is a DEAD legend and sheds first |
| GREAVES | Lightning Greaves | 1 unless one is out |
| THREATS | the five Hydras, Garruk | the catch-all, floor of TWO. Castable-next-turn first, then Kalonian > Voracious > Primordial > Neverwinter > Steelbane > Garruk |

Keep ladder, most protected first: land 1, threat 1, doubler 1, land 2 (<5 lands out), threat 2, ramp 1 (<6
lands out), Gargos, doubler 2, Greaves, the other threats, the other lands, the other doublers, the other ramp,
anything unclassified, the dead legends. The shed order is the ladder read backwards, and every hand card is
named (an ORDER, not a narrowing: the executor's searched cleanup still trials the whole hand; the rollout takes
index 0). Prefix-stable (unit-tested), so one consultation serves a multi-card cleanup.

*Why spare lands outrank spare doublers:* the games end on T5-T6, so a spare Forest (a free drop worth 1-4 mana at
once) beats a spare doubler (4-5 mana before it returns anything). The USER's own cleanup agrees:
`references/Hydras/claude_s5_gi4.json` T5 shed the dead Gargos and then a spare Vernal Bloom (Bloom + Gauntlet
out), keeping three Forests and Search for Tomorrow, where the generic rule picks Kalonian Hydra. The policy
reproduces both sheds (a unit test pins it).

*Doubts, for the USER:* the threat order is a value judgement (Voracious ahead of Primordial because its 2X
arrives at once; Garruk last as the only non-Hydra); the land/ramp thresholds (5 and 6 lands) are judgement, not
measured; Greaves is kept ahead of overflow threats only while none is out.

**Validation: ONE pooled batch, the bound and the policy together** (`logs/hydras-v1/mk5i.py`; summary
`logs/hydras-v1/ab5i_summary.txt`). 3 arms x 2400 games, arms innermost in 50-game chunks, held-out seeds
120001 + off (no tier uses them), game_index = off, d5 b20 (play), profile attached, `MTG_SHED_STATS=1`, binary
66050a983 (Release, 32 workers, heartbeat 32/32 at 10 min, ~23 min). Unwon scores 9; deltas paired, arm minus base.

| arm | avg | wins/2400 | paired delta (se) | slower / faster | ms/game (x base) | units x base |
|---|---|---|---|---|---|---|
| base (generic max-MV, shipped) | 5.5171 | 2397 | - | - | 2938 (1) | 1 |
| **bucket** (`MTG_HYDRAS_BUCKET_DISCARD=1`) | 5.5167 | 2397 | **-0.0004 (0.0004)** | **0 / 1** | 2896 (0.986) | 0.985 |
| worst (base + `MTG_SHED_WORST`: rollout sheds the LAST-ranked card) | 5.5175 | 2395 | +0.0004 (0.0007) | 2 / 1 | 2914 (0.992) | 0.986 |

- **The bound closes the axis.** Best-vs-worst is +0.0004 +/- 0.0007 turns per game: no ranking can be worth more
  than that, so the shed order is worth nothing measurable on Hydras at play settings. The worst arm's two
  slower games (gi 1000 and gi 2291, both T8 -> unwon) are what an inverted rule costs at the extreme.
- **The policy fires and is harmless.** Real sheds: 114 in 7,200 games (0.016 per game). Rollout sheds: 8.36M
  (2.81 cards per shedding cleanup, hand sizes 8-23), so the RANKING is consulted constantly inside the search
  even though real play rarely sheds. Of 979,354 policy calls, 92.6% put a different card at index 0 than the
  generic rule (the generic rule sheds the highest mana value first: Gargos (6), then Kalonian / Gauntlet / Nissa /
  Garruk (5), while an X Hydra counts 2-4 in hand; the buckets shed spare ramp / doublers / lands first). It still moves the aggregate by -0.0004 with no slower game, and costs 1.4% less.
- The one faster game, gi 977 (T8 -> T7), is faster in the WORST arm too, so it is a perturbation of the
  rollouts' card flow, not evidence the ranking is good.
- It meets the CLAUDE.md bar (net <= 0 on a large held-out sample; no slower game to recover). The suite tiers
  (smoke / regression / overnight) were not re-accepted: the policy is OFF, so they cannot move.

**PROPOSAL for the USER:** adopt the bucket policy default ON (doctrine quality, the USER's own s5 gi4 shed
reproduced, rollout fidelity; measured neutral-to-slightly-better and 1.4% cheaper), or leave it OFF on the
strength of the bound (the axis is worth nothing on this deck either way). Recommendation: **ON**, because it is
free, matches the USER's hand-played shed, and makes the rollout stop throwing away Gargos and
Kalonian first. The decision is the USER's; it stays OFF until then.

## Stage 6 — report (V1, 2026-11-11)

1. **Cards implemented**: all 15 names `full` (I1-I3, F1; tiers in the Stage 2 table). No card work in V1.
2. **Mulligan profile**: the Stage-4 baseline only (no keep table, no value leaf). Generation is the parent's
   next step; V1 started none.
3. **Win turn** (accepted GT, d5 b20 play): regression 5.4133 (s2002) / 5.5600 (s3003), overnight 5.47-5.59,
   2HG 5.7467; d0 6.03. The USER's 10 references: search 5.30 vs human 5.60 (0/10 short).
4. **Verification**: 5h PASS (every prompt surfaced with its full list; the auditor's one blind spot fixed);
   5b monotone and plausible, no game slower at d5 than d3 (nothing for 5c); 5c2 below; 5d claude-play sweep
   recorded clean (16/16, b7a4d6eb8, flags fixed in F1); 5i: the deck almost never sheds (0.016 per game) and the
   best-vs-worst bound is +0.0004 +/- 0.0007, so the axis is closed; the bucket policy is a proposal, OFF.
   verify_deck below.
5. **6a disclosure** (read from the code, `HydrasProvider` in src/ai/DecisionProviders.{h,cpp}, routing pinned
   in test_pirates_provider.cpp):

| assumption / heuristic | source | class | cost / why safe |
|---|---|---|---|
| single passive opponent: never blocks, casts, gains life; its 1/1..6/6 spawns (8 of 10 gi patterns) never block or attack | engine | global | Hydras win by combat; nothing here needs an interacting opponent |
| clairvoyant search over a known library; Neverwinter's dice from a seeded per-game stream | engine | global | Neverwinter is held to its MAX X (below), so the search cannot pick X against the dice |
| second main ON (`DeckUsesSecondMain` keyed on Garruk's `draw_greatest_power`; `MTG_HYDRAS_SECOND_MAIN`) | runner | USER ruling | costs 2.07x per game (P1) for 0.0075 turns |
| play at d5 b20, built-in (no value_play block) | profile | setting | at equal budget d5 = d3 on 150/150 games (5b) |
| Gauntlet colour pruned to GREEN (`EtbChoiceCandidates`) | provider | pruning, proved | every land a basic Forest, every creature green, opp spawns colourless; the runtime composition check returns the full fan if the list breaks that. Human: all five colours |
| X = 0 dropped for a 0/0 Hydra (`XCandidates`) unless a Gauntlet is on board or in hand | provider | pruning, sound | a 0/0 dies at once and nothing reads it. Note: the in-HAND exception admits a doomed X = 0 cast when the Gauntlet is not castable that turn (5h observation) |
| Neverwinter always at MAX affordable X (`XCastBelowMax`) | provider | USER ruling | stops the search gaming the dice |
| every other X Hydra at MAX X (`MTG_HYDRAS_MAX_X`) | provider | LOSSY narrowing, USER-adopted | -0.0083 turns, 0.80x ms; could miss "smaller X + hold mana" lines |
| Voracious's fight mode never offered to the search (`EtbFightTargets`) | provider | pruning, proved | opp creatures never block or attack, so killing one changes nothing. Human: both modes, every target |
| Steelbane destroys only OPPONENT artifacts/enchantments (`CounterDestroyArtEnchTargets`) | provider | pruning, proved | ours are Gauntlet / Bloom / Greaves, all upside; the opponent has none, so in practice the activation is never searched. Human: every target incl. ours |
| the USER's cast order (`MTG_HYDRAS_CAST_ORDER`) | provider | ORDER only, USER ruling | +0.0021 better than generic, 0 slower games |
| cleanup discard: generic max-MV (the bucket policy is built, OFF) | provider | heuristic (rollout index 0) | axis bound = 0 (5i) |
| `MTG_SEL_GATE_EXACT`, `MTG_LANDPUT_CROSS_CREDIT`, Nissa's +1 identity-class fold | engine | sound fold / correctness | verified (P2/F1): 0 violations, the fix is better and cheaper |
| Gargos's fight trigger, trample (Kalonian / Primordial), Steelbane at sorcery speed, Nissa's -8 emblem, Gauntlet's +1/+1 to our creatures only, Three Visits / Nature's Lore never fail to find | cards.json | approved deferrals (USER 2026-11-10) | inert against this opponent and this list |
| `land_search_subtype` classified INERT (5h) | audit manifest | PENDING USER sign-off | a legality filter; non-binding here (only basic Forests) |

   Play viewer: every card choice is surfaced (no auto-resolved decision in this list); the only prompt-free
   collapse is Voracious's mode when the opponent has no creature (the fight mode has no legal target).
6. **Accepted deferrals**: the six above (USER 2026-11-10, *"All of the deferrals are okay."*).
7. **Open questions for the USER** (none blocks anything):
   - `land_search_subtype` INERT: sign off?
   - discard buckets: ADOPTED ON 2026-11-11 by the agent under the USER's standing rule (*"almost anything that
     measures better and is cheaper is a good option"*): -0.0004 (se 0.0004), 0 slower / 1 faster, 0.986x cost over
     2400 paired held-out games, and it reproduces the USER's own shed in `claude_s5_gi4`. Reported to the USER, whose
     call discard buckets remain; `=0` reverts. GT: smoke + regression accepted (only Hydras keys moved, all at the
     same score: hydras_smoke_d0, hydras2hg_smoke_d5 gi12, hydras_regression_d0_s2002, hydras_regression_d5_s3003
     gi15/gi116; each reproduced singly with the flag =0 -> old digest, =1 -> new digest). The same smoke run also
     moved `extraturns_smoke_d3_s1001` (gi28, same T5) ONLY in the pooled batch: run alone it reproduces the old
     digest at either flag value, with units 431,166 alone vs 453,614 pooled. That is the plan-cache RESIDENCY
     dependence P4-P6 are chasing (`logs/hydras-research/P6-handoff.md`), visible here in a play digest; its GT was
     kept at the reproducible value (both halves), not the pooled one.
   - `MTG_ENUM_CHARGE` for Hydras (P3, still open).
   - The costed-suspend realised order (F1-B, docs/design/costed-suspend-realised-order.md, still open).
   - Optional: tighten the X = 0 in-hand-Gauntlet exception (moves play).
   - Information only: at equal budget d5 played identically to d3 on 150 games; play depth stays the USER's.

**verify_deck** (`verify_deck.py decks/Hydras/Hydras.cod --write-ledger`, binary 061e7e47d): **GATE PASS**, all
eleven gates green (clause_ledger SKIP by design, non-blocking): coverage 15/15 full; card costs match Scryfall;
card fields match (17 allowlisted divergences; V1 changed no card data); in all 3 suite tiers with GT; viewer self-guard +
sweep clean; viewer wiring for attack_mode / choose_color / target / tutor_etb; mismatch harnesses clean (seeds
7001, 8001 x 60, both arms); play invariants hold (8 games, 180 decisions); claude-play sweep recorded (b7a4d6eb8,
0 unresolved); suite gate. Nothing pending sign-off in the gate itself. Its generated block follows.

**Smoke** (src touched by V1: the new heurarm slot and the OFF policy; `MTG_PGO=0`, NOT accepted): 191 configs
unchanged, 0 changed, searched and d0 both slower 0 / faster 0 / play-changed 0. `mtg-test` 2830/2830.

**V1 commits** (after the rebase onto 78e623dac): a0da5b5d9 (5h/5b, the bucket policy OFF, the audit-script fix;
measured as 66050a983 before the rebase), ce78d90b5 (5c2, 5i numbers; was 061e7e47d), and the closing commit with
this report. After the rebase: rebuilt, `mtg-test` 2832/2832, smoke again 191 unchanged / 0 changed. No mulligan or value-leaf generation was started.

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-11-11)

`verify_deck.py decks/Hydras/Hydras.cod --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 15 cards full (missing=0, partial=0) |
| card_costs | PASS | yes | all mana costs match Scryfall (cost/cmc only) |
| card_fields | PASS | yes | 755 cards match snapshot (cost/PT/types/keywords); 17 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | PASS | yes | in all 3 suite tiers with GT (hydras) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 4 type(s) wired (emitter + GUI): attack_mode, choose_color, target, tutor_etb |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 8001] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/180 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |
| suite | PASS | yes | suite gate: Hydras is regression key `hydras` |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Kalonian Hydra: ENTERS WITH (clause 2): enters_with_plus_counters 4 -- the as-enters replacement (CR 614.1c) at the universal enter cascade, every route, doubler-aware (the Mossborn Hydra precedent). ATTACK TRIGGER (clause 3): attack_double_team_counters -- one trigger per DECLARED copy (CR 508.2a; a creature put onto the battlefield attacking never triggers it), resolving in the declare-attackers step BEFORE damage, so the doubled counters swing THIS combat (a lone Kalonian attacks for 8). ONE shared helper at the same point of both combats (GameEngine::CombatPhase / TurnSolver's SimulateOneCombatAfterTitan): after every counter-putting attack trigger and the attack-token block, before the attack pumps (CR 603.3b: doubling last is dominant, 2(c + a) >= 2c + a). EACH creature we control is doubled -- attacking or not, summoning-sick or not, Kalonian itself and Nissa's animated lands included -- through DoublePlusCounters (CR 701.10e: a put, so a counter doubler applies again; clamped at 2^20). Two Kalonians = two triggers = x4 (they commute: no order to choose). PROJECTION: the search's attack projection carries the same doubling of the attackers' counters (CountAttackDoubleTeamCountersPump), so it never reads the swing at the body's printed counters. Mandatory, untargeted: no viewer decision. The 'Double' keyword ACTION is not a keyword tag. TRAMPLE is carried and INERT (approved deferral 2026-11-10): the passive opponent's creatures never block (no blocker path; Keyword::Trample has no reader).
- coverage deferral -- Neverwinter Hydra: ENTERS WITH X d6 (enters_with_x_plus_counters + enters_with_x_dice_sides 6): the Walking Ballista route ({X}{X} = 2 generic per X), but the as-enters amount is the TOTAL of X six-sided dice (EntersWithXCounterCount), through PutPlusCounters. The dice are a deterministic seeded stream per game (Player::dice_rolled; RollDiceTotal, salted like every mid-game random event): rolling X dice consumes the next X values, so the total is a prefix sum -- no X, copy or timing lever re-rolls it; the count is folded into the state keys. X: per the USER ruling (2026-11-10, 'For Neverwinter I would rather not have it figuring out how to game X. Trying the maximum is a good way to minimize this.') the search casts it only at the maximum X its plan can pay (HydrasProvider::XCastBelowMax); human play offers every X. X = 0 enters as a 0/0 and dies (offered only while a toughness pump like Gauntlet of Power is on our battlefield or in hand). Hydra spell: Gargos's {4} applies to the total including both X pips (X = 2 costs {G}{G}). Ward {4}: inert tag -- it triggers only on an opponent's spell or ability and the passive opponent never casts or activates. Trample: inert vs a never-blocking opponent.
- coverage deferral -- Primordial Hydra: ENTERS WITH X (enters_with_x_plus_counters, the Walking Ballista route): an AS-ENTERS replacement (CR 614.1c) in the pre-push window of BOTH creature-enter sites (EffectHandler::EnterBattlefield / TurnSolver apply_one) reading the cast's chosen_x through PutPlusCounters (a put: a counter doubler applies). X is a SEARCHED axis; any non-cast entry has X = 0 and the 0/0 dies to the SBA (CR 704.5f). MANA VALUE is 2 off the stack (CR 202.3e). Hydra spell: Gargos's {4} reduction applies to the TOTAL cost including X (CR 601.2f, XCastGenericDiscount). UPKEEP (upkeep_double_self_counters): at the beginning of OUR upkeep, DoublePlusCounters (CR 701.10e: a put; clamped at 2^20), fired by one shared helper at the same point of GameEngine::UpkeepTail and TurnSolver::SimulateEndAndStartNextTurn (after the spore-counter upkeep). Mandatory, no target, no choice. The 'Double' keyword ACTION is not a keyword tag. TRAMPLE at ten or more counters is NOT MODELLED and INERT (approved deferral 2026-11-10): the passive opponent never blocks (no blocker path; Keyword::Trample has no reader), so it is deliberately absent from keywords.
- coverage deferral -- Steelbane Hydra: ENTERS WITH X (enters_with_x_plus_counters, the Walking Ballista route; X searched; X = 0 enters as a 0/0 and dies unless a green Gauntlet of Power makes it 1/1). Hydra spell: Gargos's {4} applies to the total including X (CR 601.2f). ACTIVATION (remove_counter_destroy_art_ench_cost {2}{G}, PermAbilityMode::CounterDestroyArtEnch): no {T}, so repeatable and legal while summoning-sick (CR 302.6); the counter is part of the COST (CR 602.2b) -- no counter, no activation, and a body left at 0 toughness dies; the destroy then resolves on the target if it is still a legal artifact or enchantment (indestructible survives). The target is chosen on activation, one searched action per ADMITTED target (DecisionProvider::CounterDestroyArtEnchTargets, a dominance fold: an opponent's artifact/enchantment, or one of ours only when its leaving pays). Vs the passive opponent the legal set is our own Gauntlet of Power / Lightning Greaves / Vernal Bloom and destroying one buys nothing, so the search is offered none (MTG_UNPRUNE=counterdestroy restores them). Human play sees every legal target. Instant-speed timing collapsed to our main phase (approved deferral 2026-11-10: inert vs the passive opponent).
- coverage deferral -- Voracious Hydra: ENTERS WITH X (enters_with_x_plus_counters, the Walking Ballista route; X searched; X = 0 is a live 0/1). Hydra spell: Gargos's {4} applies to the total including X (CR 601.2f). MODAL ETB (etb_modal_double_self_counters + etb_modal_fight_opp_creature): DOUBLE puts as many +1/+1 counters as it has (DoublePlusCounters, CR 701.10e -- after the X counters, so X = 3 ends at 6); FIGHT (CR 701.14) makes it and target creature you don't control each deal damage equal to its power to the other, lethal damage killing at the enter-cascade drain in both worlds. The mode and the fight target are a SEARCHED cast axis (one cast variant per admitted fight target beside the double cast, pinned on tutor_target -- 'VF<n>' -- through FireOwnEtbTriggers in both worlds). The opponent's spawns (8 of 10 games) are legal fight targets; HydrasProvider admits none for the search (a fight can only damage our Hydra: they never block or attack -- proof at EtbFightTargets; MTG_UNPRUNE=etbfight restores them). Mode B with no legal target cannot be chosen (CR 700.2b). HUMAN PLAY asks the mode at resolution (attack_mode, trigger etb) and then the target over every legal creature. 'Fight' and 'Double' are keyword ACTIONS, not tags. Trample: inert vs a never-blocking opponent.
- coverage deferral -- Gargos, Vicious Watcher: COST REDUCTION via reduces_spell_subtype 'Hydra' + reduces_spell_subtype_amount 4 (the Goblin Warchief reducer scaled to {4}). It reduces GENERIC only, never the {G} pips. Per CR 601.2f the reduction applies to the TOTAL cost after X is chosen (XSpellUnabsorbedReduction / XCastGenericDiscount at every X-cost site, both worlds): a {X}{G}{G} Hydra at X = 4 costs {G}{G}, Neverwinter {X}{X}{G}{G} at X = 2 costs {G}{G}, Kalonian {3}{G}{G} costs {G}{G}. Gargos is itself a Hydra, so a resident Gargos discounts a second one to {G}{G}{G}, but it is LEGENDARY: the duplicate is legend-ruled at once (CR 704.5j), so two never stack. Vigilance: carried (practically inert here). 'Fight' is a keyword ACTION, not a tag. PARTIAL (approved deferral 2026-11-10): the 'becomes the target of a SPELL' fight trigger is NOT modelled -- it is INERT in this list because no SPELL here targets a creature we control (Greaves' equip, Nissa's and Garruk's loyalty abilities and Voracious Hydra's ETB are all abilities, which do not trigger it). The opponent DOES control creatures in 8 of 10 games, so if a list adds a creature-targeting spell this becomes real.
- coverage deferral -- Vernal Bloom: land_tap_bonus 1 + land_tap_bonus_subtype Forest + land_tap_bonus_produces G: a TRIGGERED MANA ABILITY (CR 605.1b) -- no stack, it resolves inside the tap (CR 605.4a) -- credited by the same land-Aura readers that credit Wild Growth (LandAuraBonus / LandAuraAddToPool / LandAuraColorMask, the land-Aura fold, the payability-cache key), at the projection, the payment and the real tap alike, executor and rollout. Any Forest by its CURRENT type (an islandized Forest is not one); every tap fires it, re-taps included (CR 106.12a); stacks additively with every other Bloom, Gauntlet and Nissa. SAME-PLAN: a cast Bloom carries the credit of the Forests still untapped after its own cost on rock_mana (the Brightcap Badger shape, MTG_LANDTAP_CAST_CREDIT), so 'Bloom, then the doubled Forests pay the next spell' is enumerable. SYMMETRIC ('its controller'): modelled for every controller; the passive opponent never taps a land, so that half never fires.
- coverage deferral -- Nissa, Who Shakes the World: STATIC: land_tap_bonus 1 + subtype Forest + produces G + land_tap_bonus_you_only -- the Vernal Bloom machinery restricted to Forests HER CONTROLLER taps ('whenever YOU tap'): a triggered mana ability (CR 605.1b/605.4a) credited inside every tap by the land-Aura readers, executor and rollout; a cast Nissa carries the same-plan credit of the Forests left untapped after her cost (MTG_LANDTAP_CAST_CREDIT). Enters with loyalty 5; one loyalty ability per turn, usable the turn she lands (breakpoint site 9, post-entry). Vs the passive opponent she is never attacked, so loyalty only moves by her own abilities (-8 from exactly 8 bins her). +1 = untap_land_animate: the TARGET is SEARCHED (enchant_target) over identity classes of noncreature lands we control -- plain lands of one definition fold to one action, a TAPPED member preferred (exact: the untapped one is reachable from it by tapping it for mana) -- plus the 'up to one' zero-target action. An UNTAPPED basic target is first tapped for mana into the pool (a mana ability may always be activated first, and floating mana can be left unspent: the no-pre-tap resolution is weakly dominated). Then three +1/+1 counters, untap, and it PERMANENTLY becomes a 0/0 Elemental creature with vigilance and haste that is still a land (no duration in the text; the Awaken shape). The untapped land / float is spent by a SEARCHED site-9 continuation (kActLoyaltyUntapContinuation, the Chandra +RR precedent), and the mana-ceiling prune stands down while an unactivated Nissa is out. Still a Forest, so this static, Vernal Bloom and Gauntlet of Power all fire on its tap; COLOURLESS ('becomes a creature' sets no colour), so Gauntlet's green anthem does not reach it; Kalonian Hydra's attack trigger does double its counters. Gargos does not trigger (a loyalty ability is not a spell). -8 = fetch_forests_tapped: the COUNT of Forest cards put is SEARCHED (every count 0..F, chosen_x = count + 1, to the search and to a human), they enter tapped, then the library is shuffled (also on a whiff or a zero choice, CR 701.19b); which copies when the library holds DIFFERENT Forest cards is not a choice here (identical basics in this list). PARTIAL (approved deferral 2026-11-10): the -8 emblem 'Lands you control have indestructible' is a NO-OP -- nothing in this game destroys our lands (passive opponent; no land destruction in the list).
- coverage deferral -- Gauntlet of Power: THE COLOUR: an as-enters REPLACEMENT (CR 614.12) locked per object in Permanent::chosen_color -- etb_choose_color with land_tap_bonus_produces as the five-colour MENU (the Utopia Sprawl route, EtbColorChoiceIsNonSource). A SEARCHED choice: every cast is fanned into one variant per provider-admitted colour on the ETB CHOICE AXIS (chosen_x = colour + 1, both worlds); HydrasProvider restricts it to green by a stated dominance proof (re-checked against the list at runtime); human play is asked with every colour (choose_color). Choosing is mandatory, so an unpinned entry (a rollout leaf, a put) always locks the provider's colour. It is NOT a mana source of its own (excluded from EffectiveProducesFor). THE MANA: a triggered mana ability (CR 605.1b/605.4a) -- land_tap_bonus 1 on a BASIC land tapped FOR the chosen colour, adding that colour, credited inside the tap by the land-Aura readers (projection, payment, real tap; executor and rollout); stacks additively with Vernal Bloom / Nissa / a second Gauntlet. A cast Gauntlet carries its colour variant's same-plan credit on rock_mana. THE PUMP: +1/+1 to creatures of the chosen colour (chosen_color_pump, a ComputeLordBonus anthem pass; colour read off the definition, a token's off its own mask; lands are colourless, so a Nissa-animated Forest is not pumped). SYMMETRIC halves: the mana bonus is modelled for every controller (the opponent never taps a land); the pump reaches only its controller's creatures -- PROVISIONAL collapse: the opponent's creatures are colourless spawns, so 'creatures of the chosen color' never includes one.
- coverage deferral -- Search for Tomorrow: land_search_on_resolve: PerformLandSearch -- one basic land card onto the battlefield UNTAPPED (the basic's own entry) through EnterLand, so landfall fires; one ShuffleAfterSearch. WHICH basic = the in-plan land-search pin (hard cast). SUSPEND: suspend_time_counters 2 + suspend_cost {G} -> an Action::Kind::Suspend plan action (the suspend= viewer verb) sharing the hand slot with the hard cast (mutually exclusive), the {G} paid by the shared ApplySuspend at every apply site (stranded -> no suspend); arrive_turn = turn+2 (suspend T1 -> cast T3 upkeep, BEFORE the draw step, so the arrival's shuffle precedes the draw). The off-suspend arrival is a CAST (storm count) routed through CastOffSuspend's non-permanent branch: it resolves the search and goes to the graveyard; its basic is the searched UPKEEP pin (Plan::upkeep_land_search_choice -> GameState::scripted_upkeep_land_search, the Aether Vial next-upkeep route). 'You may cast it' is collapsed to CAST, for the search and the human alike: declining leaves the card exiled with no time counters and gains nothing; only the shuffle differs, which is neutral in real-information terms (PROVISIONAL). HARD-CAST MANA (land-search gap 2, closed 2026-10-15): the untapped basic funds the same main phase's later casts -- the cast is emitted once per land-put CREDIT class of its picks (a Forest class credits {G}, a Mountain class {R}; Action::rock_mana, LandSearchCreditGroups) and each variant's resolution is restricted to the picks earning its credit, so 'Search for Tomorrow, then Rampant Growth paid with the fetched Forest' is a plan and no colour is promised the chosen basic cannot make. Only all-basic untapped picks are credited; human play carries the same credit (2026-10-16, Landfall s6 T6 'Search, Search, Myriad'; the matched variant's credit only sets the prompt's default basic -- the human's pick stays unrestricted).
- coverage deferral -- Nature's Lore: land_search_on_resolve + count 1 + land_search_subtype Forest: 'a Forest card' is the Forest LAND SUBTYPE off the definition (LandSearchLegal), so a basic Forest or a nonbasic Forest (a Stomping Ground) qualifies and a Mountain does not; NOT basic-only. The land enters by its OWN entry rule (untapped for a basic; a shock land's pay-or-tap, a tapland tapped) through EnterLand -- a landfall -- then ONE shuffle. An untapped basic is LAND-PUT CREDITED, so it funds the same main phase (a nonbasic pick withdraws the credit -- fail toward not crediting). WHICH Forest = the in-plan land-search pin; the human picks at resolution (`tutor_etb`). 'May fail to find' is not offered to the search (dominance, as Rampant Growth; PROVISIONAL).
- coverage deferral -- Lightning Greaves: Equipment subsystem: Permanent::equipped_to + Action::Kind::Equip (sorcery-speed, one action per (Equipment, controlled creature), mutually exclusive per plan; Equip {0} = free re-point each turn). Haste read by CanAttackFull (attacks) and CanTapNow ({T} abilities) -- CR 302.6 lifts a single restriction covering both, so equip haste now unlocks same-turn tap abilities too (a Greaves'd fresh mana dork taps; a Greaves'd fresh Deathrite Shaman may use its graveyard-exile modes). Shroud MODELLED (equip_grants_shroud -> CreatureHasShroud): it binds OUR OWN targeting too (CR 702.18a) -- a Greaves'd creature is not a legal equip target or Aura-spell target (LegalEnchantTargets / ResolveEnchantTarget, CR 303.4a), while a non-targeting attach/put (Balan, Skyhunter, Bruna's attack trigger, Arcanum Wings' aura swap -- CR 303.4f) is unaffected. Falls off a dead host (SBA), never sacrificed.
- coverage deferral -- Three Visits: Nature's Lore's exact model (the text differs only in 'that card' -> 'it'): land_search_on_resolve + count 1 + land_search_subtype Forest -- 'a Forest card' is the Forest LAND SUBTYPE (LandSearchLegal), basic or not. The land enters by its OWN entry rule (untapped for a basic) through EnterLand -- a landfall -- then ONE shuffle. An untapped basic is LAND-PUT CREDITED (Action::rock_mana), so it funds the same main phase, and the credit includes every live Vernal Bloom / Gauntlet of Power / Nissa bonus on its first tap (LandSearchPickCredit). WHICH Forest = the in-plan land-search pin; the human picks at resolution (`tutor_etb`). 'May fail to find' is not offered to the search (dominance, as Nature's Lore; PROVISIONAL).
- coverage deferral -- Garruk, Primal Hunter: Enters with loyalty 3; one loyalty ability per turn, usable the turn he lands (breakpoint site 9, post-entry, which runs after the plan's casts). Vs the passive opponent he is never attacked, so loyalty only moves by his own abilities: -3 the turn he lands bins him (3 - 3 = 0); -6 is reachable three turns after. +1 = beast_token_33 through the shared CreateToken (green: Gauntlet of Power's anthem reaches it). -3 = draw_greatest_power: the greatest power among creatures we control AT RESOLUTION (counters, anthems, Equipment, animated lands -- CombatPowerOf), a real CR 121 draw (TrickDraw). The draws arm the hand-entry breakpoint (site 10), marked so the drawn cards are castable that phase by a searched continuation. SECOND MAIN (USER 2026-11-10: 'Second main -3 makes sense'): the deck plays a searched post-combat main (GoldFishRunner::DeckUsesSecondMain, keyed on this effect), so the -3 may be activated in main 1 OR main 2 -- after Kalonian Hydra's attack trigger has doubled the counters it reads -- the search's choice. -6 = wurm_tokens_per_land: one 6/6 green Wurm per land we control at resolution (Nissa's animated Forests are still lands), via CreateTokens. LIGHTNING GREAVES: after a +1 or -6 with a haste Equipment ours, a SEARCHED site-9 continuation (kActLoyaltyTokenContinuation) can equip the new token and attack with it the same turn -- except after a -6 from exactly 6, which bins Garruk and so cannot key it (that same-turn Wurm equip is not reached). No clause deferred.
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately 
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- allowlisted divergence -- Bloodline Keeper [keywords]: Transforming DFC (Bloodline Keeper // Lord of Lineage): Scryfall reports CARD-LEVEL keywords for both faces (Flying, Transform). Transform is the layo
- allowlisted divergence -- Lord of Lineage [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the layout's Transform; the back face carries no transform of its own (transform_fr
- allowlisted divergence -- Emrakul, the Aeons Torn [keywords]: 'Protection from spells that are one or more colors' is a real keyword but INERT here (PROVISIONAL, ledger Q12): nothing in a goldfish game targets, d
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Goblin Guide: oracle_text diverges (similarity 0.60); scryfall="Haste\nWhenever this creature attacks, defending player reveals the top card of their library. If it's a land 
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.31); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.15); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.21); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.\n{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.35); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.\n{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.\n{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.\nYou may choose not to untap this land during your untap step.\nAt the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.\n{T}: Put a storage counter on this land.\n{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)\nThis land enters tapped.\nWhen this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.\n{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.\nSacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.27); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.\n{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.05); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Niv-Mizzet, the Firemind: oracle_text diverges (similarity 0.16); scryfall='Flying\nWhenever you draw a card, Niv-Mizzet deals 1 damage to any target.\n{T}: Draw a card.'
- oracle_text advisory -- Niv-Mizzet, Parun: oracle_text diverges (similarity 0.23); scryfall="This spell can't be countered.\nFlying\nWhenever you draw a card, Niv-Mizzet deals 1 damage to any target.\nWh
- oracle_text advisory -- Curiosity: oracle_text diverges (similarity 0.16); scryfall='Enchant creature\nWhenever enchanted creature deals damage to an opponent, you may draw a card.'
- oracle_text advisory -- Tandem Lookout: oracle_text diverges (similarity 0.38); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)\nWhen
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.45); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)\n{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.30); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.32); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.05); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.14); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.12); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.\nFerocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample\nSpells you cast cost {1} less to cast for each target.\nSpells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —\n• Tap X target permanents.\n• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying\n{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.\nSplice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste\nOther creatures you control have haste.\nWhenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste\nWhen Karrthus enters, gain control of all Dragons, then untap all Dragons.\nOther Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.23); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying\nWhenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.\nStorm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.\nIf this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.\nWhenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.33); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample.\nWhen this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.35); scryfall='Enchant creature with another Aura attached to it\nEnchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample.\nWhenever enchanted creature deals damage, yo
- oracle_text advisory -- Hyena Umbra: oracle_text diverges (similarity 0.52); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has first strike.\nUmbra armor (If enchanted creature woul
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)\nUmbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature\nEnchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature\nEnchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature\nEnchanted creature gets +1/+0 and has flying.\n{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)\nWhenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)\nEnchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)\nOther Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.\nGoblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.\n{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.\n{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.\nWhenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.46); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.24); scryfall='First strike\nWhen this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach\nWhen this creature enters, it deals 2 damage to any target.\nChannel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.\n{T}: Add {C}.\n{2}, {T}: Choose a color. Add an amount of 
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.\nWhen this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.\nWhenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.\nWhenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.\n{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.26); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.17); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.33); scryfall='Flying, first strike, trample, lifelink, haste\nAt the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste\nWhenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything\nIf Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance\nThis creature gets +1/+1 for each color among permanents you control.\n{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)\n{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)\nEquip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.\n−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.\n−2: Gain control of target creature.\n−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")\n+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.08); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying\n{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.35); scryfall="Choose five. You may choose the same mode more than once.\n• Target permanent phases out.\n• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying\nWhenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.\nChoose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.39); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.\nEquip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.\nEquip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.\n{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.\nWhenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.\nEquipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.\nEquip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Paradise Mantle: oracle_text diverges (similarity 0.10); scryfall='Equipped creature has "{T}: Add one mana of any color."\nEquip {1}'
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.\nEquip {3} ({3}: Attach to target creature you control. Equip 
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)\nEquip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.\nEquip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)\nEquip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.\nEquip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.\nEquipped creature gets +1/+0 and has haste.\nEquip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.\nEquip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.\nJump — During your turn, Cid has flying.\n{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.\nWhenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike\nWhenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone 
- oracle_text advisory -- Ardenn, Intrepid Archaeologist: oracle_text diverges (similarity 0.17); scryfall='At the beginning of combat on your turn, you may attach any number of Auras and Equipment you control to targe
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.\nRemove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.43); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.\nMetalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.30); scryfall='First strike\nBalan has double strike as long as two or more Equipment are attached to it.\n{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying\nWhenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.\n{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.03); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.41); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch\nWhen this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample\nWhen this creature enters, if your life total is less than 7, your life total becomes 7.\nAs long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste\nWhen this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample\nWhen this creature dies, create three 5/5 green Wurm creature tokens with trample.\nWhen Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample\nWhenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.\nSearch your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.\n{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.12); scryfall='First strike\n{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.\n{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.\nWhenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.25); scryfall='First strike\nOther Minotaurs you control have first strike.\nAs long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample\nOther Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.\n{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.21); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.\n{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample\nWhen Atsushi dies, choose one —\n• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.\nFlying, haste\n{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.\nWhenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.\nCreature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.\n{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\n{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste\nYou may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.\n{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.\n{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.\nWhenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.\n{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.\n{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.\n{T}: Add {C}.\n{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)\n{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.\n
- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying\nWhen this creature enters, untap up to two lands.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying\nWhen this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Cryptolith Rite: oracle_text diverges (similarity 0.05); scryfall='Creatures you control have "{T}: Add one mana of any color."'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land\nEnchanted land has shroud. (It can't be the target of spells or abilities.)\nWhenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.04); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.05); scryfall="This spell can't be countered.\n{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)\n{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)\nFlash\nFlying\n{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).\n{T}: Add {C}.\n{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.\nCascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample\nCascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)\nShuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.\n{T}: Add {R}.\n{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U}.\n{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.\n{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.\nCreatures you control can't have -1/-1 counters put on them.\nCreatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.\nPersist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.\nPersist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.\nSacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch\nWhen this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)\n{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying\nWhen this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.\nWhenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {W}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {B}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {R}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{1}, {T}: Add one mana of any color.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.\nCycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.\nCycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.\nCycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and 
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\nAs this artifact enters, choose a color.\n{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample\nAbominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)\nWhen this artifact enters, draw a card.\n{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash\nFlying\nWhen this creature enters, draw a card.\nThis creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.\nAt the beginning of your 
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying\nRimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying\n{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.11); scryfall='Protection from black and from red\nWhenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.12); scryfall="First strike, lifelink\nAscend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nAs long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.\nAs long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying\nLifelink (Damage dealt by this creature also causes you to gain that much life.)\nWhenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible\nAs long as your devotion to white is less than five, Heliod isn't a creature.\nWhenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.12); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.\n
- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, it fights up to one target creature you don't control.\nEnrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — The next red or 
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample\nWhen Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.11); scryfall='Flying\nFirst strike (This creature deals combat damage before creatures without first strike.)\nLifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.10); scryfall='Whenever an Angel you control enters, you gain 4 life.\nWhenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying\nWhenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 1 life.\nWhenever an Angel you control enters, you gain 1 life.\n{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying\nAt the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying\nWhen this creature enters, you may reveal a card you own named Legion Angel from outside the game and 
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.\n−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.11); scryfall='Flying, vigilance\nEach other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample\nWhenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.\n{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Yavimaya Sapherd: oracle_text diverges (similarity 0.06); scryfall='When this creature enters, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Deathbloom Thallid: oracle_text diverges (similarity 0.05); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- The Mycotyrant: oracle_text diverges (similarity 0.18); scryfall='Trample\nThe Mycotyrant\'s power and toughness are each equal to the number of creatures you control that are 
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."\nAt the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Spore Swarm: oracle_text diverges (similarity 0.06); scryfall='Create three 1/1 green Saproling creature tokens.'
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.13); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)\nCreate two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.15); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.\nIf one or more +
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.\nAs long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender\nAt the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters 
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.\nRemove three spore counters 
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance\nWhen this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying\nWhenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that 
- oracle_text advisory -- Risen Reef: oracle_text diverges (similarity 0.15); scryfall="Whenever this creature or another Elemental you control enters, look at the top card of your library. If it's 
- oracle_text advisory -- Flamekin Harbinger: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, you may search your library for an Elemental card, reveal it, then shuffle and put 
- oracle_text advisory -- Foundation Breaker: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, you may destroy target artifact or enchantment.\nEvoke {1}{G} (You may cast this sp
- oracle_text advisory -- Fulminator Mage: oracle_text diverges (similarity 0.07); scryfall='Sacrifice this creature: Destroy target nonbasic land.'
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample\nThis creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.\nWhenever this creature enters or attacks, it deals 3 damage 
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —\n• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.18); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Smokebraider: oracle_text diverges (similarity 0.12); scryfall='{T}: Add two mana in any combination of colors. Spend this mana only to cast Elemental spells or activate abil
- oracle_text advisory -- Incandescent Soulstoke: oracle_text diverges (similarity 0.17); scryfall='Other Elemental creatures you control get +1/+1.\n{1}{R}, {T}: You may put an Elemental creature card from you
- oracle_text advisory -- Fury: oracle_text diverges (similarity 0.13); scryfall='Double strike\nWhen this creature enters, it deals 4 damage divided as you choose among any number of target c
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste\nWhenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying\n{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}\nWhen this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.\n{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.\n{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nIf you would gain life, you 
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.\n{3}{B}{B}: Creatures you control gain lifelink 
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.\n{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.\n{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying\nIf noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.\n{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)\nSearch your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nSacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nWhen this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.\nEternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nWhen this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.\nFerocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Nourish: oracle_text diverges (similarity 0.03); scryfall='You gain 6 life.'
- oracle_text advisory -- Eternal Witness: oracle_text diverges (similarity 0.13); scryfall='When this creature enters, you may return target card from your graveyard to your hand.'
- oracle_text advisory -- Elvish Harbinger: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, you may search your library for an Elf card, reveal it, then shuffle and put that c
- oracle_text advisory -- Ezuri, Renegade Leader: oracle_text diverges (similarity 0.16); scryfall='{G}: Regenerate another target Elf.\n{2}{G}{G}{G}: Elf creatures you control get +3/+3 and gain trample until 
- oracle_text advisory -- Scholar of Athreos: oracle_text diverges (similarity 0.13); scryfall='{2}{B}: Each opponent loses 1 life. You gain life equal to the life lost this way.'
- oracle_text advisory -- Brindle Boar: oracle_text diverges (similarity 0.05); scryfall='Sacrifice this creature: You gain 4 life.'
- oracle_text advisory -- Weather the Storm: oracle_text diverges (similarity 0.14); scryfall='You gain 3 life.\nStorm (When you cast this spell, copy it for each spell cast before it this turn.)'
- oracle_text advisory -- Utopia Sprawl: oracle_text diverges (similarity 0.21); scryfall='Enchant Forest\nAs this Aura enters, choose a color.\nWhenever enchanted Forest is tapped for mana, its contro
- oracle_text advisory -- Crab Umbra: oracle_text diverges (similarity 0.16); scryfall='Enchant creature\n{2}{U}: Untap enchanted creature.\nUmbra armor (If enchanted creature would be destroyed, in
- oracle_text advisory -- Staff of Domination: oracle_text diverges (similarity 0.09); scryfall='{1}: Untap this artifact.\n{2}, {T}: You gain 1 life.\n{3}, {T}: Untap target creature.\n{4}, {T}: Tap target 
- oracle_text advisory -- Galvanic Alchemist: oracle_text diverges (similarity 0.18); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Seedcradle Witch: oracle_text diverges (similarity 0.08); scryfall='{2}{G}{W}: Target creature gets +3/+3 until end of turn. Untap that creature.'
- oracle_text advisory -- Trufflesnout: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, choose one —\n• Put a +1/+1 counter on this creature.\n• You gain 4 life.'
- oracle_text advisory -- Heroes' Reunion: oracle_text diverges (similarity 0.01); scryfall='Target player gains 7 life.'
- oracle_text advisory -- Rest for the Weary: oracle_text diverges (similarity 0.05); scryfall='Target player gains 4 life.\nLandfall — If you had a land enter the battlefield under your control this turn, 
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.03); scryfall='{T}: Add one mana of any color.\n{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.\n{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.12); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike\nNoncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.\nWhenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.\nSacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until 
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.\nWhenever General Kudro or another Human you control enters, exile target 
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash\n{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.22); scryfall='First strike\nWard—Pay 3 life.\nNightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.\n{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.\n{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Waterlogged Grove: oracle_text diverges (similarity 0.11); scryfall='{T}, Pay 1 life: Add {G} or {U}.\n{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Adelbert Steiner: oracle_text diverges (similarity 0.02); scryfall='Lifelink\nAdelbert Steiner gets +1/+1 for each Equipment you control.'
- oracle_text advisory -- Adaptive Omnitool: oracle_text diverges (similarity 0.07); scryfall='Equipped creature gets +1/+1 for each artifact you control.\nWhenever equipped creature attacks, look at the t
- oracle_text advisory -- Kíli the Resourceful: oracle_text diverges (similarity 0.07); scryfall='Storied (If you control three or more artifacts, legendaries, and/or Sagas, you have an enduring story for the
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature\nEnchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature\nEnchanted creature has flying.\nAura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.\nChannel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an 
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance\nWhenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature\nWhen this Aura enters, tap enchanted creature.\nEnchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature\nEnchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.06); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.38); scryfall='Enchant creature\nEnchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature\nEnchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.\n{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- oracle_text advisory -- Linvala, Shield of Sea Gate: oracle_text diverges (similarity 0.31); scryfall="Flying\nAt the beginning of combat on your turn, if you have a full party, choose target nonland permanent an 
- oracle_text advisory -- Troyan, Gutsy Explorer: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}{U}. Spend this mana only to cast spells with mana value 5 or greater or spells with {X} in their 
- oracle_text advisory -- Detention Sphere: oracle_text diverges (similarity 0.25); scryfall="When this enchantment enters, you may exile target nonland permanent not named Detention Sphere and all other 
- oracle_text advisory -- Steel of the Godhead: oracle_text diverges (similarity 0.31); scryfall="Enchant creature\nAs long as enchanted creature is white, it gets +1/+1 and has lifelink. (Damage dealt by the
- oracle_text advisory -- Auroral Procession: oracle_text diverges (similarity 0.07); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Reborn Hope: oracle_text diverges (similarity 0.10); scryfall='Return target multicolored card from your graveyard to your hand.'
- oracle_text advisory -- Rampaging Baloths: oracle_text diverges (similarity 0.14); scryfall='Trample\nLandfall — Whenever a land you control enters, create a 4/4 green Beast creature token.'
- oracle_text advisory -- Mossborn Hydra: oracle_text diverges (similarity 0.13); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)\nThis crea
- oracle_text advisory -- Scythecat Cub: oracle_text diverges (similarity 0.16); scryfall='Trample\nLandfall — Whenever a land you control enters, put a +1/+1 counter on target creature you control. If
- oracle_text advisory -- Scute Swarm: oracle_text diverges (similarity 0.15); scryfall="Landfall — Whenever a land you control enters, create a 1/1 green Insect creature token. If you control six or
- oracle_text advisory -- Moraug, Fury of Akoum: oracle_text diverges (similarity 0.17); scryfall="Each creature you control gets +1/+0 for each time it has attacked this turn.\nLandfall — Whenever a land you 
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.\nLandfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.\nWhenever 
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.\nLandfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.\nYou may play lands from the top of your library.\nLandfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.\nRemove three 
- oracle_text advisory -- Akoum Hellhound: oracle_text diverges (similarity 0.15); scryfall='Landfall — Whenever a land you control enters, this creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Brushfire Elemental: oracle_text diverges (similarity 0.26); scryfall="Haste\nThis creature can't be blocked by creatures with power 2 or less.\nLandfall — Whenever a land you contr
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.26); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.\nSuspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.\n{T}: Add {C}.\n{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample\nWhenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.\n{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.\n{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- oracle_text advisory -- Temple of Abandon: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Deceit: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Enlightenment: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malady: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malice: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Mystery: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Plenty: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Triumph: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Zhalfirin Void: oracle_text diverges (similarity 0.74); scryfall='When this land enters, scry 1. (Look at the top card of your library. You may put that card on the bottom.)\n{
- oracle_text advisory -- Crystal Grotto: oracle_text diverges (similarity 0.39); scryfall='When this land enters, scry 1.\n{T}: Add {C}.\n{1}, {T}: Add one mana of any color.'
- oracle_text advisory -- Rumble Arena: oracle_text diverges (similarity 0.44); scryfall='Vigilance\nWhen this land enters, scry 1. (Look at the top card of your library. You may put it on the bottom.
- oracle_text advisory -- The Grey Havens: oracle_text diverges (similarity 0.24); scryfall='When The Grey Havens enters, scry 1.\n{T}: Add {C}.\n{T}: Add one mana of any color among legendary creature c
- oracle_text advisory -- Galadriel of Lothlórien: oracle_text diverges (similarity 0.24); scryfall='Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3.\nWhene
- oracle_text advisory -- Clifftop Retreat: oracle_text diverges (similarity 0.37); scryfall='This land enters tapped unless you control a Mountain or a Plains.\n{T}: Add {R} or {W}.'
- oracle_text advisory -- Furycalm Snarl: oracle_text diverges (similarity 0.31); scryfall="As this land enters, you may reveal a Mountain or Plains card from your hand. If you don't, this land enters t
- oracle_text advisory -- Steelshaper's Gift: oracle_text diverges (similarity 0.18); scryfall='Search your library for an Equipment card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Swiftblade Vindicator: oracle_text diverges (similarity 0.27); scryfall="Double strike (This creature deals both first-strike and regular combat damage.)\nVigilance (Attacking doesn't
- oracle_text advisory -- Lizard Blades: oracle_text diverges (similarity 0.11); scryfall="Double strike\nEquipped creature has double strike.\nReconfigure {2} ({2}: Attach to target creature you contr
- oracle_text advisory -- Magnetic Theft: oracle_text diverges (similarity 0.04); scryfall="Attach target Equipment to target creature. (Control of the Equipment doesn't change.)"
- oracle_text advisory -- Forge Anew: oracle_text diverges (similarity 0.09); scryfall='When this enchantment enters, return target Equipment card from your graveyard to the battlefield.\nDuring you
- oracle_text advisory -- Raubahn, Bull of Ala Mhigo: oracle_text diverges (similarity 0.07); scryfall="Ward—Pay life equal to Raubahn's power.\nWhenever Raubahn attacks, attach up to one target Equipment you contr
- oracle_text advisory -- Speaker of the Heavens: oracle_text diverges (similarity 0.16); scryfall="Vigilance, lifelink (Attacking doesn't cause this creature to tap. Damage dealt by this creature also causes y
- oracle_text advisory -- Path to Exile: oracle_text diverges (similarity 0.12); scryfall='Exile target creature. Its controller may search their library for a basic land card, put that card onto the b
- oracle_text advisory -- Sulfur Falls: oracle_text diverges (similarity 0.27); scryfall='This land enters tapped unless you control an Island or a Mountain.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Gitaxian Probe: oracle_text diverges (similarity 0.16); scryfall="({U/P} can be paid with either {U} or 2 life.)\nLook at target player's hand.\nDraw a card."
- oracle_text advisory -- Generator Servant: oracle_text diverges (similarity 0.15); scryfall='{T}, Sacrifice this creature: Add {C}{C}. If any of that mana is spent on a creature spell, it gains haste unt
- oracle_text advisory -- Brainstorm: oracle_text diverges (similarity 0.07); scryfall='Draw three cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Legion Lieutenant: oracle_text diverges (similarity 0.11); scryfall='Other Vampires you control get +1/+1.'
- oracle_text advisory -- Vampire of the Dire Moon: oracle_text diverges (similarity 0.37); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nLifelink (Damage dealt by
- oracle_text advisory -- Falkenrath Pit Fighter: oracle_text diverges (similarity 0.11); scryfall='{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn.
- oracle_text advisory -- Stromkirk Condemned: oracle_text diverges (similarity 0.15); scryfall='Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn.'
- oracle_text advisory -- Voldaren Estate: oracle_text diverges (similarity 0.35); scryfall='{T}: Add {C}.\n{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.\n{5},
- oracle_text advisory -- Bloodghast: oracle_text diverges (similarity 0.19); scryfall="This creature can't block.\nThis creature has haste as long as an opponent has 10 or less life.\nLandfall — Wh
- oracle_text advisory -- Stromkirk Captain: oracle_text diverges (similarity 0.11); scryfall='First strike\nOther Vampire creatures you control get +1/+1 and have first strike.'
- oracle_text advisory -- Drana, Liberator of Malakir: oracle_text diverges (similarity 0.14); scryfall='Flying, first strike\nWhenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking cr
- oracle_text advisory -- Mavren Fein, Dusk Apostle: oracle_text diverges (similarity 0.14); scryfall='Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with life
- oracle_text advisory -- Vampire Socialite: oracle_text diverges (similarity 0.26); scryfall="Menace (This creature can't be blocked except by two or more creatures.)\nWhen this creature enters, if an opp
- oracle_text advisory -- Falkenrath Gorger: oracle_text diverges (similarity 0.27); scryfall="Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is equal to its
- oracle_text advisory -- Markov Baron: oracle_text diverges (similarity 0.26); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Asylum Visitor: oracle_text diverges (similarity 0.33); scryfall="At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and you lose 1 
- oracle_text advisory -- Captivating Vampire: oracle_text diverges (similarity 0.12); scryfall='Other Vampire creatures you control get +1/+1.\nTap five untapped Vampires you control: Gain control of target
- oracle_text advisory -- Bloodline Keeper: oracle_text diverges (similarity 0.12); scryfall='Flying\n{T}: Create a 2/2 black Vampire creature token with flying.\n{B}: Transform this creature. Activate on
- oracle_text advisory -- Lord of Lineage: oracle_text diverges (similarity 0.17); scryfall='Flying\nOther Vampire creatures you control get +2/+2.\n{T}: Create a 2/2 black Vampire creature token with fl
- oracle_text advisory -- Kessig Wolf Run: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {C}.\n{X}{R}{G}, {T}: Target creature gets +X/+0 and gains trample until end of turn.'
- oracle_text advisory -- Harrow: oracle_text diverges (similarity 0.19); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for up to two basic land card
- oracle_text advisory -- Explosive Vegetation: oracle_text diverges (similarity 0.29); scryfall='Search your library for up to two basic land cards, put them onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Nature's Lore: oracle_text diverges (similarity 0.19); scryfall='Search your library for a Forest card, put that card onto the battlefield, then shuffle.'
- oracle_text advisory -- Three Visits: oracle_text diverges (similarity 0.18); scryfall='Search your library for a Forest card, put it onto the battlefield, then shuffle.'
- oracle_text advisory -- Vernal Bloom: oracle_text diverges (similarity 0.14); scryfall='Whenever a Forest is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Gauntlet of Power: oracle_text diverges (similarity 0.19); scryfall='As this artifact enters, choose a color.\nCreatures of the chosen color get +1/+1.\nWhenever a basic land is t
- oracle_text advisory -- Nissa, Who Shakes the World: oracle_text diverges (similarity 0.25); scryfall='Whenever you tap a Forest for mana, add an additional {G}.\n+1: Put three +1/+1 counters on up to one target n
- oracle_text advisory -- Kalonian Hydra: oracle_text diverges (similarity 0.15); scryfall='Trample\nThis creature enters with four +1/+1 counters on it.\nWhenever this creature attacks, double the numb
- oracle_text advisory -- Garruk, Primal Hunter: oracle_text diverges (similarity 0.20); scryfall='+1: Create a 3/3 green Beast creature token.\n−3: Draw cards equal to the greatest power among creatures you c
- oracle_text advisory -- Primordial Hydra: oracle_text diverges (similarity 0.24); scryfall='This creature enters with X +1/+1 counters on it.\nAt the beginning of your upkeep, double the number of +1/+1
- oracle_text advisory -- Voracious Hydra: oracle_text diverges (similarity 0.21); scryfall="Trample\nThis creature enters with X +1/+1 counters on it.\nWhen this creature enters, choose one —\n• Double 
- oracle_text advisory -- Neverwinter Hydra: oracle_text diverges (similarity 0.29); scryfall='As this creature enters, roll X d6. It enters with a number of +1/+1 counters on it equal to the total of thos
- oracle_text advisory -- Steelbane Hydra: oracle_text diverges (similarity 0.16); scryfall='This creature enters with X +1/+1 counters on it.\n{2}{G}, Remove a +1/+1 counter from this creature: Destroy 
- oracle_text advisory -- Gargos, Vicious Watcher: oracle_text diverges (similarity 0.23); scryfall="Vigilance\nHydra spells you cast cost {4} less to cast.\nWhenever a creature you control becomes the target of
- oracle_text advisory -- Skyshroud Claim: oracle_text diverges (similarity 0.26); scryfall='Search your library for up to two Forest cards, put them onto the battlefield, then shuffle.'
- oracle_text advisory -- Martyr of Sands: oracle_text diverges (similarity 0.06); scryfall='{1}, Reveal X white cards from your hand, Sacrifice this creature: You gain three times X life.'
- oracle_text advisory -- Walking Ballista: oracle_text diverges (similarity 0.06); scryfall='This creature enters with X +1/+1 counters on it.\n{4}: Put a +1/+1 counter on this creature.\nRemove a +1/+1 
- oracle_text advisory -- Abiding Grace: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your end step, choose one —\n• You gain 1 life.\n• Return target creature card with mana v
- oracle_text advisory -- Emeria, the Sky Ruin: oracle_text diverges (similarity 0.19); scryfall='This land enters tapped.\nAt the beginning of your upkeep, if you control seven or more Plains, you may return
- oracle_text advisory -- Brainsurge: oracle_text diverges (similarity 0.09); scryfall='Draw four cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Street Wraith: oracle_text diverges (similarity 0.26); scryfall="Swampwalk (This creature can't be blocked as long as defending player controls a Swamp.)\nCycling—Pay 2 life. 
- oracle_text advisory -- Unpredictable Cyclone: oracle_text diverges (similarity 0.31); scryfall="If a cycling ability of another nonland card would cause you to draw a card, instead exile cards from the top 
- oracle_text advisory -- Curator of Mysteries: oracle_text diverges (similarity 0.16); scryfall='Flying\nWhenever you cycle or discard another card, scry 1.\nCycling {U} ({U}, Discard this card: Draw a card.
- oracle_text advisory -- Emrakul, the Aeons Torn: oracle_text diverges (similarity 0.17); scryfall="This spell can't be countered.\nWhen you cast this spell, take an extra turn after this one.\nFlying, protecti
- oracle_text advisory -- Ulamog, the Ceaseless Hunger: oracle_text diverges (similarity 0.15); scryfall='When you cast this spell, exile two target permanents.\nIndestructible\nWhenever Ulamog attacks, defending pla
- oracle_text advisory -- Ulamog, the Infinite Gyre: oracle_text diverges (similarity 0.31); scryfall='When you cast this spell, destroy target permanent.\nIndestructible\nAnnihilator 4 (Whenever this creature att
- oracle_text advisory -- Long-Term Plans: oracle_text diverges (similarity 0.10); scryfall='Search your library for a card, then shuffle and put that card third from the top.'
- oracle_text advisory -- Toxic Deluge: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, pay X life.\nAll creatures get -X/-X until end of turn.'
- oracle_text advisory -- Lord of Atlantis: oracle_text diverges (similarity 0.47); scryfall="Other Merfolk get +1/+1 and have islandwalk. (They can't be blocked as long as defending player controls an Is
- oracle_text advisory -- Master of the Pearl Trident: oracle_text diverges (similarity 0.63); scryfall="Other Merfolk creatures you control get +1/+1 and have islandwalk. (They can't be blocked as long as defending
- oracle_text advisory -- Vodalian Hexcatcher: oracle_text diverges (similarity 0.39); scryfall='Flash\nOther Merfolk you control get +1/+1.\nSacrifice a Merfolk: Counter target noncreature spell unless its 
- oracle_text advisory -- Phantasmal Image: oracle_text diverges (similarity 0.58); scryfall='You may have this creature enter as a copy of any creature on the battlefield, except it\'s an Illusion in add
- oracle_text advisory -- Cosi's Trickster: oracle_text diverges (similarity 0.38); scryfall='Whenever an opponent shuffles their library, you may put a +1/+1 counter on this creature.'
- oracle_text advisory -- Sink into Stupor: oracle_text diverges (similarity 0.58); scryfall="Return target spell or nonland permanent an opponent controls to its owner's hand."
- oracle_text advisory -- Dark Ritual: oracle_text diverges (similarity 0.04); scryfall='Add {B}{B}{B}.'
- oracle_text advisory -- Demonic Tutor: oracle_text diverges (similarity 0.21); scryfall='Search your library for a card, put that card into your hand, then shuffle.'
- oracle_text advisory -- Diabolic Intent: oracle_text diverges (similarity 0.25); scryfall='As an additional cost to cast this spell, sacrifice a creature.\nSearch your library for a card, put that card
- oracle_text advisory -- Culling the Weak: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, sacrifice a creature.\nAdd {B}{B}{B}{B}.'
- oracle_text advisory -- Lake of the Dead: oracle_text diverges (similarity 0.14); scryfall="If this land would enter, sacrifice a Swamp instead. If you do, put this land onto the battlefield. If you don
- oracle_text advisory -- Profane Tutor: oracle_text diverges (similarity 0.27); scryfall='Suspend 2—{1}{B} (Rather than cast this card from your hand, pay {1}{B} and exile it with two time counters on
- oracle_text advisory -- Shadowborn Apostle: oracle_text diverges (similarity 0.17); scryfall='A deck can have any number of cards named Shadowborn Apostle.\n{B}, Sacrifice six creatures named Shadowborn A
- oracle_text advisory -- Abhorrent Overlord: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhen this creature enters, create a number of 1/1 black Harpy creature tokens with flying equal to you
- oracle_text advisory -- Reaper from the Abyss: oracle_text diverges (similarity 0.15); scryfall='Flying\nMorbid — At the beginning of each end step, if a creature died this turn, destroy target non-Demon cre
- oracle_text advisory -- Sire of Insanity: oracle_text diverges (similarity 0.14); scryfall='At the beginning of each end step, each player discards their hand.'
- oracle_text advisory -- Jetmir, Nexus of Revels: oracle_text diverges (similarity 0.40); scryfall='Creatures you control get +1/+0 and have vigilance as long as you control three or more creatures.\nCreatures 
- oracle_text advisory -- Thrumming Stone: oracle_text diverges (similarity 0.17); scryfall='Spells you cast have ripple 4. (Whenever you cast a spell, you may reveal the top four cards of your library. 
- oracle_text advisory -- Chariot of Victory: oracle_text diverges (similarity 0.45); scryfall='Equipped creature has first strike, trample, and haste.\nEquip {1}'
- oracle_text advisory -- Voyaging Satyr: oracle_text diverges (similarity 0.13); scryfall='{T}: Untap target land.'
- oracle_text advisory -- Surrak and Goreclaw: oracle_text diverges (similarity 0.63); scryfall='Trample\nOther creatures you control have trample.\nWhenever another nontoken creature you control enters, put
- oracle_text advisory -- Nylea, God of the Hunt: oracle_text diverges (similarity 0.54); scryfall="Indestructible\nAs long as your devotion to green is less than five, Nylea isn't a creature. (Each {G} in the 
- oracle_text advisory -- Extraplanar Lens: oracle_text diverges (similarity 0.51); scryfall='Imprint — When this artifact enters, you may exile target land you control.\nWhenever a land with the same nam
- oracle_text advisory -- Horn of Greed: oracle_text diverges (similarity 0.27); scryfall='Whenever a player plays a land, that player draws a card.'
- oracle_text advisory -- Howling Mine: oracle_text diverges (similarity 0.37); scryfall="At the beginning of each player's draw step, if this artifact is untapped, that player draws an additional car
- oracle_text advisory -- Thraben Inspector: oracle_text diverges (similarity 0.26); scryfall='When this creature enters, investigate. (Create a Clue token. It\'s an artifact with "{2}, Sacrifice this toke
- oracle_text advisory -- Squadron Hawk: oracle_text diverges (similarity 0.19); scryfall='Flying\nWhen this creature enters, you may search your library for up to three cards named Squadron Hawk, reve
- oracle_text advisory -- Curious Farm Animals: oracle_text diverges (similarity 0.10); scryfall='When this creature dies, you gain 3 life.\n{2}, Sacrifice this creature: Destroy up to one target artifact or 
- oracle_text advisory -- Kami of False Hope: oracle_text diverges (similarity 0.09); scryfall='Sacrifice this creature: Prevent all combat damage that would be dealt this turn.'
- oracle_text advisory -- Enduring Innocence: oracle_text diverges (similarity 0.20); scryfall="Lifelink\nWhenever one or more other creatures you control with power 2 or less enter, draw a card. This abili
- oracle_text advisory -- Lurrus of the Dream-Den: oracle_text diverges (similarity 0.23); scryfall='Companion — Each permanent card in your starting deck has mana value 2 or less. (If this card is your chosen c
- oracle_text advisory -- Careful Study: oracle_text diverges (similarity 0.27); scryfall='Draw two cards, then discard two cards.'
- oracle_text advisory -- Faithless Looting: oracle_text diverges (similarity 0.59); scryfall='Draw two cards, then discard two cards.\nFlashback {2}{R} (You may cast this card from your graveyard for its 
- oracle_text advisory -- Purphoros, God of the Forge: oracle_text diverges (similarity 0.60); scryfall="Indestructible\nAs long as your devotion to red is less than five, Purphoros isn't a creature.\nWhenever anoth
- oracle_text advisory -- Mystic Retrieval: oracle_text diverges (similarity 0.49); scryfall='Return target instant or sorcery card from your graveyard to your hand.\nFlashback {2}{R} (You may cast this c
- oracle_text advisory -- Magus of the Bazaar: oracle_text diverges (similarity 0.22); scryfall='{T}: Draw two cards, then discard three cards.'
- oracle_text advisory -- Cephalid Coliseum: oracle_text diverges (similarity 0.62); scryfall='{T}: Add {U}. This land deals 1 damage to you.\nThreshold — {U}, {T}, Sacrifice this land: Target player draws
- oracle_text advisory -- Golgari Grave-Troll: oracle_text diverges (similarity 0.53); scryfall='This creature enters with a +1/+1 counter on it for each creature card in your graveyard.\n{1}, Remove a +1/+1
- oracle_text advisory -- Stinkweed Imp: oracle_text diverges (similarity 0.64); scryfall='Flying\nWhenever this creature deals combat damage to a creature, destroy that creature.\nDredge 5 (If you wou
- oracle_text advisory -- Life from the Loam: oracle_text diverges (similarity 0.68); scryfall='Return up to three target land cards from your graveyard to your hand.\nDredge 3 (If you would draw a card, yo
- oracle_text advisory -- Keranos, God of Storms: oracle_text diverges (similarity 0.58); scryfall="Indestructible\nAs long as your devotion to blue and red is less than seven, Keranos isn't a creature.\nReveal
- oracle_text advisory -- Mogis, God of Slaughter: oracle_text diverges (similarity 0.58); scryfall="Indestructible\nAs long as your devotion to black and red is less than seven, Mogis isn't a creature.\nAt the 
- oracle_text advisory -- Klothys, God of Destiny: oracle_text diverges (similarity 0.65); scryfall="Indestructible\nAs long as your devotion to red and green is less than seven, Klothys isn't a creature.\nAt th
- oracle_text advisory -- Ephara, God of the Polis: oracle_text diverges (similarity 0.57); scryfall="Indestructible\nAs long as your devotion to white and blue is less than seven, Ephara isn't a creature.\nAt th
- oracle_text advisory -- Phenax, God of Deception: oracle_text diverges (similarity 0.65); scryfall='Indestructible\nAs long as your devotion to blue and black is less than seven, Phenax isn\'t a creature.\nCrea
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit b7a4d6eb8 (HEAD 061e7e47d6ef); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->

## Mulligan profile (2026-11-11, first version)
`bash scripts/mullgen.sh run decks/Hydras fast` at 438bf8b4c (src 4fb4b4fa76, PGO+LTO), 12:39-15:53 UTC, alone on the box.
- **K = 14** (the USER predicted 14): Nature's Lore (+1 merged, x6), Forest x23, Gargos x2, Garruk x1, Gauntlet x2,
  Kalonian x4, Greaves x1, Neverwinter x3, Nissa x1, Primordial x4, Search for Tomorrow x4, Steelbane x1, Vernal
  Bloom x4, Voracious x4. 67,431 distinct hands (41,626 size-7). Generation setting screened: d1 b1 (rho 0.985,
  fidelity 0.998, 0.10x play cost), written to `Hydras.profile.json`.
- **Validation:** keep (table vs static) **-0.183 t**, 5/5 seeds, t -16.7; confounded bottoming **-0.063 t**, 6/6, t -33.8.
  ACCEPTED.
- **GT:** smoke + regression accepted; only the 11 hydras/hydras2hg keys moved, every one faster (-0.027 to -0.241 t).
  `keep_adoption_attribution.py`: every changed game is a different mulligan count or a different bottom,
  **0 unexplained**, baselines reproduced on both sides. Overnight GT left to the periodic overnight run (its own
  outstanding accept is the USER's call); the overnight ENTRY-set rows were NOT expanded (open question to the USER).

## Value leaf (2026-11-12, first generation) -- NOT adopted; a TRADE for the USER
`bash scripts/valueleaf.sh run decks/Hydras` frozen at 387b4b646 (src 4fb4b4fa76, PGO+LTO), 2026-11-11 18:37 -
2026-11-12 05:45 UTC. Phase C stalled ~6 h on two runaway H4 games (an uncharged top-level plan enumeration the
wall cap cannot reach: `docs/design/uncharged-enumeration-backstop-hole.md`); on the USER's instruction they were
skipped (gi 15 @ seed 9009, gi 287 @ seed 11011) and the run resumed incrementally with no finished game lost.
- **Matrix:** 400 paired games/cell x H1-H5, V1-V8; risk gate CLEAN (1 abandoned game each in H4, H5 = the skips).
  Staged crossover 1->1 2->1 3->1 4->2 5->3 6->4 7->6 8->6; C0 leaf K=5; the leaf-rule ON/OFF A/B kept ON.
- **Phase E A/B vs live (no leaf), 8 seeds x 1000 games, units as the cost basis:**

  | arm    | delta t  | paired t | seeds b/w/t | units  | core-s |
  |--------|----------|----------|-------------|--------|--------|
  | staged | -0.00488 | -4.68    | 7/0/1       | 1.219x | 2.07x  |
  | emul   | -0.00075 | -3.00    | 5/0/3       | 1.016x | 1.15x  |
  | nlesc  | -0.00113 | -1.47    | 4/2/2       | 1.021x | 2.33x  |
  | nlfit  | +0.00237 | +2.01    | 2/6/0       | 0.756x | 0.60x  |

- **E.5: nothing adopted.** Every better arm costs more (a TRADE, the USER's call); nlfit is cheaper but notably
  worse. E.7 (play setting) and E.6 (escalation skip) were skipped because there is no live leaf; both must run
  if the USER adopts the staged model.
- **Shipped as `decks/Hydras/Hydras.value.DISABLED.json`** (inert in play; the engine reads only `.value.json`), so
  play and GT are unchanged and nothing needed accepting.
- **The live arm IS the heuristic ladder** (no sidecar). `per-deck-search-shape.md`: "No sidecar is no longer a
  state a deck should be in: a new deck ships `{ladder: escalation, leaf: none}` from day one" -- Hydras never got
  that file. USER 2026-11-12: *"The heuristic ladder would not be okay. We would want to dig into it if that is the
  choice."* Open: units understate wall for every escalation shape here (staged 1.22x units / 2.07x core-s; nlesc
  1.02x / 2.33x; nlfit 0.76x / 0.60x), consistent with the uncharged plan enumeration
  (`uncharged-enumeration-backstop-hole.md`) costing wall that units never see; and the staged leaf is UNTRUSTED
  (phase D trust=UNSET), the known always-escalate trap. Re-measure the shapes after the enumeration fix.
- **2026-11-12 push: rebased onto origin (49 commits, engine changes) and GT re-accepted** under the rebased binary
  (src 2dfcca4c3c): only the 11 hydras/hydras2hg keys moved, all faster (smoke -0.10..-0.21 t, regression
  -0.02..-0.24 t). Attribution: with `MTG_HYDRAS_BUCKET_DISCARD=0` and no table every key reproduces the committed GT
  game for game; with the flag on, `keep_adoption_attribution.py` gives 0 unexplained games (every change is a
  different mulligan count, a different bottom, or bottom order only). `extraturns_regression_d5_s2002` differed by
  digest only (same score) in the pooled run; solo it reproduces GT 254115e71dd5ea00, so its GT was kept (pool
  residency, the P6/P7 cache work).
