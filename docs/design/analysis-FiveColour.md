# Analysis ledger — FiveColour

In-flight ledger for the analyze-deck run on `decks/FiveColour/FiveColour.cod` (started 2026-08-06).
Per the analyze-deck skill: this is the durable state across compaction/handoffs — update it as stages complete.

## Deck (60 cards)

2 Jared Carthalion / 3 Mana Cannons / 1 Lightning Greaves / 2 Nicol Bolas, Planeswalker /
1 Mountain / 1 Stomping Ground / 1 Overgrown Tomb / 1 Breeding Pool / 1 Steam Vents /
2 Windswept Heath / 4 Wooded Foothills / 4 Faeburrow Elder / 4 Birds of Paradise /
4 Maelstrom Archangel / 1 Progenitus / 1 Garth One-Eye / 2 Unite the Coalition /
3 Two-Headed Hellkite / 2 Verdant Catacombs / 2 Misty Rainforest / 1 Jetmir's Garden /
1 Zagoth Triome / 1 Godless Shrine / 1 Oko, Thief of Crowns / 2 Cosmic Spider-Man /
4 Deathrite Shaman / 1 Blood Crypt / 3 Scalding Tarn / 1 Island / 1 Ancient Cornucopia /
2 Bloom Tender

## Stage 1 — Coverage (2026-08-06)

13/32 full. **19 missing**: Jared Carthalion, Mana Cannons, Lightning Greaves,
Nicol Bolas Planeswalker, Faeburrow Elder, Maelstrom Archangel, Progenitus, Garth One-Eye,
Unite the Coalition, Two-Headed Hellkite, Verdant Catacombs, Jetmir's Garden, Zagoth Triome,
Oko, Cosmic Spider-Man, Deathrite Shaman, Scalding Tarn, Ancient Cornucopia, Bloom Tender.

Existing fetch entries (Windswept Heath / Wooded Foothills / Misty Rainforest) give the
fetch pattern for Verdant Catacombs / Scalding Tarn. No triome exists yet. **No planeswalker
support exists in the engine** (planeswalkers appear only as damage-target prose) — the deck
runs 2 Jared Carthalion + 2 Nicol Bolas + 1 Oko, so a loyalty model is a likely Tier 3 task.

## Stage 2 — research fan-out (in flight)

Per-card research agents spawned (Sonnet; compact drafts back, single-integrator writes).
Status table to be filled as drafts return.

| Card | Tier | Draft | Integrated | Notes |
|---|---|---|---|---|
| Zagoth Triome | 2 | done | no | basic_land + subtypes [Swamp,Forest,Island], produces B/G/U, enters_tapped, cycling {3}; zero C++ (all params exist; Lonely Sandbar/Remote Isle pattern; fetchable via subtypes) |
| Jetmir's Garden | 2 | done | no | same pattern; subtypes [Mountain,Forest,Plains], produces R/G/W |
| Verdant Catacombs | 1 | done | no | standard fetch (Swamp/Forest), produces WUBRG-in-hand per Misty pattern; zero C++ |
| Scalding Tarn | 1 | done | no | standard fetch (Island/Mountain), produces WUBRG-in-hand; zero C++ |
| Lightning Greaves | 3 | done | no | NEW equipment subsystem: Permanent::equipped_to (aura_attached_to mirror), CardParams is_equipment/equip_cost_generic/equip_grants_haste, Action::Kind::Equip (sorcery-speed, per-creature enumeration), HasHasteFromEquip beside HasHasteFromLords in CanAttackFull; shroud documented-inert. Cleanup: zero equipped_to when host leaves (falls off, not destroyed). |
| Mana Cannons | 2 | done | no | Extends FireOnCastTriggers (Eidolon site): on_cast_trigger_multicolor_only + damage_per_color; damage = #colors of cast spell (from ManaCost pips — m_color_mask is declared-but-never-populated, don't use), to OPPONENT face (Eidolon hits caster — differs!); any-target collapses to face (etb_damage_any precedent). NOT second-main-relevant despite agent flag (no combat-generated resource) — integrator override. |
| Cosmic Spider-Man | 1 | done | no | vanilla_creature 5/5 legendary {W}{U}{B}{R}{G}, keywords Flying/FS/Trample/Lifelink/Haste (agent claims lifelink modelled — VERIFY at integration). PROPOSED DEFERRAL: begin-of-combat Spider anthem (zero other Spiders in deck + legend rule ⇒ provably no targets). Legend rule engine-enforced automatically. |
| Maelstrom Archangel | 3 | done | no | NEW `combat_damage_free_cast`: bank per-turn `free_casts_available` counter in Combat.cpp ResolveCombatDamage (Lackey precedent), spend via plan-carried free_cast flag in apply_one (cascade_free precedent) + executor mirror; DeckUsesSecondMain wiring REQUIRED; viewer bucket A (main_phase plan variants + decline variant; plan_signature must key the flag). PROPOSED DEFERRAL: free cast resolves in post-combat main, not mid-combat (Scryfall ruling: timing restrictions ignored; goldfish: nothing happens between). X=0 rule if an X spell ever enters the pool. |

| Progenitus | 1+2 | done | no | {W}{W}{U}{U}{B}{B}{R}{R}{G}{G} 10/10 legendary. Protection-from-everything inert (no Keyword::Protection needed). Graveyard-replacement is LIVE (cleanup discard of a 10-drop is reachable) → NEW graveyard_replace_shuffle_library param, wired in GameEngine::CleanupStep + TurnSolver scripted_discard mirror. |
| Two-Headed Hellkite | 3 | done | no | NOT legendary (×3 fine). Flying/menace inert; haste live. NEW attack_draw_cards=2: ApplyAttackDrawTriggers beside ApplyAttackSelfPumps in both GameEngine::CombatPhase + TurnSolver::SimulateCombat; DeckUsesSecondMain wiring (draw-on-attack = combat-generated resource). |
| Unite the Coalition | 3 | done | no | {2}{W}{U}{B}{R}{G} instant "choose five, repeats OK". NEW split machinery: search-chosen S∈[0,5] → 2·S face damage + (5−S) draws (crackle_targets/X-ladder precedent for threading S plan→resolution). Viewer: prefer bucket A via plan variants (re-check before bespoke chooser). PROPOSED DEFERRAL: 3 dead modes (phase out / exile gy / destroy artifact-ench) — provably dead vs passive cardless opponent, self-target neutral-to-harmful. |

| Ancient Cornucopia | 2 | done | no | mana_rock:true + produces WUBRG (Sol Ring/Izzet Signet pattern; no spend-restriction on real card — do NOT set colored_creature_only). NEW colored_cast_lifegain: once-per-turn per-permanent flag (reset in BOTH untap sites), fired in FireOnCastTriggers, life += #colors of cast spell. Lifegain live-ish (6 shocks + 11 fetches self-damage; HasLost checks life≤0). |

| Faeburrow Elder | 3 | done | no | mana_dork + NEW domain_mana (dynamic EffectiveProduces=DomainColors) + domain_self_pump (+1/+1 per color, via ComputeLordBonus extension). ManaPayment tap_source amt must use prod.size() (Karoo gate). **SBA BUG RISK: base 0/0 + lord-unaware EffectiveToughness → dies on ETB unless SBA check adds domain/lord bonus.** Audit all plan-time produces readers for EffectiveProduces. |
| Bloom Tender | 2 | done | no | Same domain_mana infra, 1/1 base (no SBA risk), no self-pump. |
| Deathrite Shaman | 3 | done | no | 3 mutually-exclusive {T} abilities = real searched branching. Ability 1 needs graveyard-land-exile gate+consume (13 fetches = fuel; NEW gy-by-type plumbing, no delve/escape exists). Ability 2 ({B}: exile inst/sorc → opp −2; fuel = 2× Unite only) + 3 ({G}: exile creature → +2 life, marginal). **DECISION: hybrid {B/G} parses as {B}-only today (real fidelity loss for T1 cast off green) — accept disclosed or build hybrid payment.** "a graveyard" → own gy (opponent's always empty). |

| Garth One-Eye | 3 | done | no | {W}{U}{B}{R}{G} 5/5 legendary. {T}: choose un-chosen name (per-PERMANENT tracking, 6-bit mask on Permanent), copy cast AS ABILITY RESOLVES (no "this turn" — Oracle correction). PerformGarthActivate (SpellEffects one-off pattern) + NEW CastConjuredCopy entry point (CastSpellFromHand assumes hand index). 6 sibling cards.json entries (Lookup is name-keyed over all entries): Black Lotus (sac_for_mana_amount=3 + chosen_float_color, Lotus Bloom reuse), Shivan Dragon (firebreathing reuse), Braingeyser (NEW x_draw_multiplier, chosen_x reuse), Regrowth (NEW gy→hand return + gy-zone chooser), Terror (NEW first hard-destroy primitive; targets = spawn tokens, payoff ~0), Disenchant (structurally target-less — stub proposal). Viewer: garth_choice plan variants (plan_signature must key them). |

**CROSS-CUTTING (integration prerequisite): card COLORS are unpopulated dead scaffolding**
(`Card::m_color_mask`/`AddColor` zero call sites; no "colors" keys in cards.json). Needed by
Faeburrow/Bloom Tender (domain), Mana Cannons (#colors damage), Cornucopia (lifegain=#colors),
Jared −3/−6 (color counts). Plan: add a `"colors"` array (Scryfall field) parsed in
CardDatabase.cpp → AddColor, with fallback derivation from mana-cost pips when absent; tokens
(all-colors Kavu) set colors explicitly. The Cornucopia draft's HasColor loop only works after
this lands; Mana Cannons draft's pip-derivation becomes unnecessary (use HasColor uniformly).

**Opponent-spawn check (RESOLVED, from Garth agent): `GoldFishRunner::PopulateOpponentSpawns`
(GoldFishRunner.cpp:116-141), 10-game cycle: 8/10 patterns spawn opponent creature(s); they NEVER
attack or block; opponent never has artifacts/enchantments.** So: flying/menace/vigilance inert
claims hold; Archangel always connects; Garth's Disenchant structurally target-less; Terror has
targets but ~0 payoff; Bolas −2 / Oko −5 have possible spawn-token targets (steal → we gain an
attacker) — marginal, proposed as not-modeled disclosures.

## Planeswalker model (design decision, orchestrator)

Adopting the survey proposal: `Permanent::loyalty` dedicated int (charge/verse-counter pattern)
mirrored into `Counter{Loyalty,…}` for the existing viewer badge; `loyalty_activated_this_turn`
reset in lockstep (GameEngine::UntapStep + TurnSolver per-turn reset); `Action::Kind::ActivateLoyalty`
enumerated in CollectActions (sorcery-speed, stack empty, minus needs loyalty ≥ |delta|), applied in
apply_one + executor mirror; SBA loyalty≤0 → graveyard; legend rule already generic (zero pw code).
Viewer: plan-variant sub-decision (no new decision type; SummarizePlan labels).

Per-ability decisions (pending the opponent-blockers check below):
- **Jared** {W}{U}{B}{R}{G} L5: +1 Kavu 3/3 trample all-colors (token primitive); −3 up-to-2 creatures
  get +1/+1 × its color count (collapse to max-color-count targets = provably optimal for total
  damage; human-play unpruned surfaces all pairs); −6 return multicolored from gy (+draw+2 Treasure
  if all-colors) — live, reachable ultimate.
- **Bolas** {4}{U}{B}{B}{R} L5: +3 destroy noncreature permanent — implement FAITHFULLY (own-permanent
  targets only; it is Bolas's only loyalty ramp — a real, costly line the search prices); −2 gain
  control — no beneficial effect (own-creature no-op) → not enumerated, disclosed; −9: 7 face damage
  (discard-7/sac-7 inert vs empty opponent — re-check vs spawn tokens).
- **Oko** {1}{G}{U} L4: +2 Food token; +1 Elk-transform (NEW primitive: synthetic 3/3 green Elk
  identity, no abilities) — real line = Elk-a-Food; −5 requires opponent creature (check spawn);
  Food's nested sac ability ({2},{T},Sac: gain 3) → PROPOSED DEFERRAL (lifegain inert vs passive
  opponent; Elk line is the value).

**MUST-VERIFY at integration (affects many drafts' "inert" claims): do opponent-spawn tokens
exist for this deck's runs, and can opponent creatures BLOCK?** (Chainwhirler note: "only matters
vs opponent spawn tokens"; Orchard failure note calls opponent Spirits "blockers"; but the skill's
goldfish definition says opponent "never blocks".) If blockers are real: flying/menace NOT inert,
Maelstrom Archangel's combat-damage trigger needs to actually connect, Bolas −2 / Oko −5 may have
targets. Resolve by reading GoldFishRunner spawn logic + Combat.cpp before writing "inert" notes.

**Pre-existing data bug found (fix at integration):** basic **Mountain** entry in cards.json
has no `"subtypes": ["Mountain"]` (Island/Plains/Forest have theirs) → unreachable by any
fetchland's subtype match. Matters here (Scalding Tarn / Wooded Foothills → basic Mountain).

## RESUME STATE (2026-08-06, pre-compaction checkpoint)

**Where we are:** ALL implementation phases A–K are DONE and each was smoke-green
(`bash test/regression.sh --smoke` → 30 passed, 0 failed after every phase; existing decks
byte-identical throughout). Stage 3 coverage is CLEAN (0 missing, 0 partial, 31/31 full).
First-light run works: `./build/Release/mtg decks/FiveColour/FiveColour.cod --games 10
--seed 42 --depth 3 --budget-ms 10` → avg 5.70, 1 unwon@8.

**In flight (background):**
- Stage 4 baseline profile: `python3 scripts/analyze_deck.py decks/FiveColour/FiveColour.cod
  --no-rebuild` → writes decks/FiveColour/FiveColour.profile.json (was still running).
- Stage 2d-bis cost audit: `python3 scripts/audit_card_costs.py` (network, slow/rate-limited;
  output was still empty). Must end "All mana costs match Scryfall"; treat non-zero exit as a
  hard stop. NOTE: Scryfall's Braingeyser/Terror/etc. are real cards — verify my entries match
  (I authored costs from the research agents' Scryfall fetches, but the audit is the gate).

**Remaining pipeline (in order):**
1. Finish 2d-bis: cost audit green + `python3 scripts/audit_card_fields.py --update` (refresh
   snapshot for the ~20 new cards, commit snapshot) then offline diff. Token-only entries
   (Treasure Token, Elk — no Scryfall card) may need audit exemptions — check how the scripts
   handle unknown names (Kavu Token/Food Token have no entries; Treasure Token DOES — watch it).
2. Stage 5: `python3 scripts/verify_deck.py FiveColour` (or per-piece: nonconv + fd-diverge
   harnesses, multi-depth d0/d3/d5 sanity with MTG_DUMP_WINS, budget-starvation check,
   audit_viewer_decisions.py). Root-cause every flag; loop back to fixes as needed.
3. Stage 5d: ~15–20 game claude-play sweep, one SONNET agent per game (user directive), base
   seed disjoint from suite seeds (suite uses 1001/2002/3003/4004..10010 — pick e.g. 7777),
   per .claude/skills/claude-play.md.
4. Stage 5h viewer decision surface; new decision types this deck introduced ride main_phase
   plan variants (bucket A) — confirm SummarizePlan labels them readably; audit script manifest
   may need rows for new params (loyalty_abilities, garth_copy_ability, modal_choose_n,
   is_equipment, gy_land_exile_mana, gy_exile_*).
5. Regression: add FiveColour to test/regression_cases.sh tiers ONLY when user asks (Creature
   Giving precedent: registration was a separate step) — actually per skill, run suite for
   win-turn numbers in the report; adding to tiers = follow the regression-testing skill.
6. Stage 6 report + full 6a disclosure table (deferrals + auto-resolved picks list below).
7. Run smoke AND regression before any commit (regression-cadence memory); commit the ledger,
   cards.json, all C++, profile, and the field-audit snapshot.

**Uncommitted pre-existing changes NOT mine (leave alone):** src/analyzer/ExhaustiveKeep.cpp
(modified), untracked `1`, build_xover/, decks/Creature Giving/*.value.json, Dragonstorm
journal.

**Key disclosure items for 6a (accumulate):** approved deferrals section above; plus
auto-resolved picks: Jared −3 top-2-color-count creatures, Jared −6 highest-MV multicolored,
Bolas +3 first-own-land destroy, Oko +1 Food-only Elk (not offered without Food), Garth
Regrowth highest-MV / Terror largest-spawn / Braingeyser auto-max-X / Disenchant never,
Deathrite fuel-card picks fungible, Unite 'any target'→face + draws→self, equip-haste
attacks-only limitation, hybrid pips invisible to colour-demand heuristics, free-cast
banking caveat (phase-crossing mana), Archangel/Hellkite flip second main on.

## Verification verdicts (Stage 5)

- **2d-bis cost audit GREEN (2026-08-06):** every card verified vs Scryfall across three runs
  (Scryfall throttled full-database re-runs; the tail 8 — Garth conjure suite + Unite/Disenchant
  — verified by a targeted 10s-spaced probe: all match exactly).
- **2d-bis field audit GREEN (2026-08-06):** snapshot extended with the 25 new cards (targeted
  fetch, not a full --update — avoids the throttle); 195 checked, all hard fields match. New
  systematic strip: `food` (token-type keyword, mirrors `treasure`). New per-card allowlist:
  Progenitus `protection` (inert vs passive opponent; shuffle-in IS modeled), Bloom Tender
  `vivid` (Scryfall data quirk). Field auditor now discloses runtime-token skips
  (Treasure Token — no Scryfall card by that name).
- **Stage 4 DEGENERATE ATOM found + TAMED (2026-08-07, user-approved kill of the stuck run):**
  the first analyzer run stalled ~3.4h single-threaded on ONE scoring game (stack: HandleMulligan
  bottoming rollout → FSLine full search → Solve → EnumeratePlanPositions). A 1000-game d5/b20
  straggler sweep (seed 4200000) found ~1.5% of games degenerate (gi 47 + ~12 more). MTG_ENUM_STATS
  (new inert instrument, MTG_ENUM_STATS_MIN watermark) captured the odometer shape on gi 47:
  bound 7.4e7 = free-cast variants DOUBLING all 12 groups (2^12, Unite's group 6→12) × five
  independent SacForMana bits for the conjured Black Lotus's colour variants (2^5). Fix, all in
  the TurnSolver partition passes (PlanGroupKey, both twins):
  (1) activation families → groups keyed by source (GarthActivate/ActivateLoyalty/Equip/
      GraveyardExileAbility): k+1 states instead of 2^k;
  (2) free-cast variants → ONE group per bank slot (they were already mutually exclusive per
      slot); new SubsetHasDuplicateSacSource clause rejects a card's paid+free pair (the only
      pairing the old per-card grouping made impossible);
  (3) SacForMana colour variants → group per source ONLY when a source has ≥2 variants
      (single-variant sources stay independent bits → Dragonstorm byte-identical).
  MTG_NO_ACTIVATION_FAMILY_GROUPS=1 restores the old enumeration (A/B lever). Result: gi 47
  4.5 s single-threaded (was ≥3.4 h, ≥2700×); smoke 30/30 byte-identical after each step.
  Re-sweep CONFIRMED: all 1000 games complete in ~4 min wall (was: 36+ never finished after
  17 min, ~1.5% degenerate); avg win turn 5.8440 (d5/b20/mt20, unwon=21); 980 common games
  vs the pre-fix sweep, only 2 changed win turns (pure enumeration-order tie-breaks — the
  collapse is play-neutral). A GENERIC odometer-product backstop (bound cap for shapes no
  grouping fixes) was considered and DEFERRED: see
  docs/design/enumeration-product-backstop.md. analyze_deck.py gained --analyzer-seed
  (mtg-analyze --seed passthrough) so profile runs are reproducible; Stage 4 relaunched
  with seed 42.
- **5d claude-play sweep (2026-08-07, 18 Sonnet agents @ commit 10f3541) — 2 REAL BUGS FOUND,
  BOTH FIXED:** 14/18 games independently converged on a domain_mana payment bug: the
  backtracker's B&B total-mana gate read a domain source (Faeburrow/Bloom Tender) as 1 mana
  (static ManaProducedPerTap) instead of |domain| (2-5), pruning payable WUBRG costs -> the
  executor silently DROPPED legal committed casts (Cosmic Spider-Man stuck a full turn, T3->T4,
  across many games); the same branch also credited the static WUBRG `produces` hint instead of
  the dynamic domain colours (an over-credit that could pay colours the board lacks). Both fixed
  in SpellEffects.cpp (source_max_net domain branch + EffectiveProduces in the domain tap).
  gi15 also exposed a search-space gap: Equip variants only paired battlefield pieces, so
  "cast Greaves + Archangel, equip {0}, attack with haste" was unreachable -> Equip enumeration
  now draws BOTH sides from battlefield + hand (aura same-turn-target precedent; ApplyEquip is
  already stranded-safe). gi12's Claude-faster game (T4 vs AI T5) is attributed to the payment
  bug taxing the AI's committed lines (re-checked post-fix). NOTE: the sweep's 18 protocol games
  shared one shuffle (seed fixed, only --game-index/spawns varied) -- coverage was 1 opening
  played 18 ways; the confirmation pass uses distinct seeds. Post-fix: smoke 30/30 + regression
  50/50 byte-identical (no existing deck has domain_mana/equipment); 18-shuffle d5/b200
  benchmark avg 5.3889; profile REGENERATED (seed 42) for the play change.
- **Battery GATE PASS (2026-08-07, post-fix rerun):** coverage/card_fields/viewer/viewer_wiring/
  mismatch/play_invariants ALL PASS; mismatch clean across seeds 7001+7002 x 60 games; zero play
  advisories. Only claude_sweep outstanding (5d).
- **Multi-depth + budget sanity (2026-08-07, 200 games @ seed 4200000):** d0 6.6950 > d3 5.5300 =
  d5 5.5300 (monotone, converged by d3 at b20). Budget check: d5 b100 = 5.5150 vs b20 5.5300 —
  not budget-starved. The Unite executor fix alone moved d5 from 5.8440 to 5.5300 on this seed
  set. Profile REGENERATED at seed 42 with the fixed executor (Unite marginal now +0.036; the
  first profile scored it under the payment bug).
- **5a fd-diverge FOUND + FIXED (2026-08-07):** first battery run flagged 6 seeds (7001/7002/
  7003/7025/7040/7060, ~10% of games) [fd-diverge] realized 6-8 vs predicted 5-6. Root cause via
  MTG_BP_TRACE: the executor's CastSpellFromHand charged `chosen_x` as generic {X} mana for ANY
  card (x_pips floor 1) -- but Unite the Coalition carries its MODE SPLIT S on chosen_x with no
  {X} in its cost, so the executor priced it {2+S}{W}{U}{B}{R}{G} while the search's apply
  (correctly) priced S free. The committed T5 "Unite S=5 + attack = exact lethal" line failed at
  payment every turn and the card eventually DISCARDED to hand size. Fix: gate the X charge on
  `has_x` (AIEngine.cpp) -- byte-identical for every real {X} spell. All 6 seeds now realize
  their predicted win (7001: 7 -> 5; 7025: 8 -> 5). A real play-strength bug, not harness noise.
  Also 5h: SummarizePlan now labels the four activation kinds readably (equip X -> Y, PW loyalty#,
  Garth: conjure N, Deathrite exile modes) and the plan JSON splits `activations` from `casts`
  (clears the play_invariants advisory storm; play_invariants PASS 8 games/96 decisions).
- **5h manifest classification DONE (2026-08-06):** all 20 new params classified in
  `scripts/audit_viewer_decisions.py` — 8 choice-bearing → MAINPHASE_PARAMS
  (combat_damage_free_cast #FREE, Deathrite gy_* DRE#, is_equipment EQ#, loyalty_abilities PW#,
  garth_copy_ability GARTH#, modal_choose_n #S), 12 automatic/detail → INERT_PARAMS. Static
  self-guard PASSES. Oracle advisories (6) all map to known 6a items: Mana Cannons face-damage,
  Deathrite fungible picks, Jared −3 auto-pick, Bolas/Oko approved deferrals, Unite collapse.
  Dynamic sweep (needs profile) still pending.
- (other verdicts pending)

## Approved deferrals (user, 2026-08-06)

1. **Maelstrom Archangel free-cast BANKING** (resolve in post-combat main via `free_casts_available`,
   not mid-combat). **USER CAVEAT to honor in code comments + future reviews:** this pattern is only
   safe because nothing the free cast produces crosses a phase boundary it shouldn't — a future card
   that produces MANA mid-combat (or otherwise benefits from resolving in a different phase, e.g.
   keeping floated mana into the second main) must NOT reuse banking blindly.
2. **Unite the Coalition mode collapse**: search-chosen S∈[0,5] → 2·S face damage + (5−S) draws;
   phase-out / exile-graveyard / destroy-artifact-or-enchantment modes dropped (provably dead).
3. **Inert batch**: Cosmic Spider-Man Spider anthem; Oko Food-sac lifegain ability; Bolas −2 + Oko −5
   (spawn-token steals not enumerated); Bolas −9 discard/sac riders (7 damage implemented);
   Garth's Disenchant choose-but-never-cast stub. **Terror IS built for real** (first hard-removal
   primitive).

## User decisions (not deferrals)

- **Deathrite {B/G}: BUILD real hybrid-pip payment** (either color castable), not the {B}-only
  simplification. New ManaCost hybrid representation + payment-site support.

## Integration log

- Phases A–C integrated + built. Smoke: PASS byte-identical after fixes below.
- **Phase D (hybrid) lessons:** (1) the flat pips must keep the first-listed colour BAKED IN
  (heuristic colour-demand readers churned auras/Bogle otherwise); hybrid metadata only ADDS
  payable assignments via ExpandHybrids (bits==0 == old cost, tried first, unsnapshotted;
  total failure replays bits==0 for identical side effects); (2) ManaCost::ToString must keep
  the OLD rendering — the regression play digest folds the manaPaid string, so a {G/U} render
  phantom-churned 255 auras games with provably identical play. MTG_NO_HYBRID=1 = off-switch/
  A/B lever. (3) run the smoke after each shared-code phase — it caught both.
- Phase E (domain mana + SBA base-0/0 guard + self-pump) + entries: smoke PASS.
- Phase F (Deathrite): gy-land-fuel gate at payment/backtracker/pool-builder sites (fuel counter
  in AvailableManaPool/BuildNonCreaturePool/SpareUntappedMana/ScaledManaFeederMana; ≥1 gate in
  boolean color-availability checks); tap exiles a gy land with snapshot/restore on failed
  payments (incl. backtracker exact-slot undo); abilities 2/3 = Action::Kind::GraveyardExileAbility
  (trailing-outlet pattern, both worlds); hybrid {B/G} live. Smoke PASS.
- Phase G (equipment): Permanent::equipped_to, Action::Kind::Equip (per-(equipment,creature)
  variants, sac_source_id-exclusive), ApplyEquip both worlds, HasHasteFromEquip in CanAttackFull,
  SBA fall-off, Keyword::Equip inert tag (loader would THROW on unknown keyword otherwise —
  caught pre-smoke). DISCLOSED: equip haste enables attacks only, not same-turn tap abilities.
  Smoke PASS.

- Phase H (Archangel free cast): GameState::free_casts_available banked in Combat.cpp, spent as
  free-variant CastFromHand actions (slot-exclusive; X=0 rule; signature #FREE); rollout via
  prep_free→cascade_free arm; executor via CastSpellFromHand(free_cast). Smoke PASS.
- Phase I (planeswalkers): loyalty_start + loyalty_abilities params; Permanent::loyalty +
  once-per-turn flag; ActivateLoyalty actions; ApplyLoyaltyAbility (both worlds) with scripted
  primitives (Kavu all-colors token, counters collapse, regrow-multicolored, destroy-own,
  face damage, Food token, Elk transform); executor default-branch now admits Planeswalker
  permanents; Treasure Token entry (live sac-for-mana). Smoke PASS.
- Phase J (Garth): garth_copy_ability + per-permanent chosen mask; GarthActivate variants
  (Disenchant never — approved stub; Braingeyser single max-X variant); ApplyGarthActivate casts
  the conjured copy as the ability resolves (Lotus/Shivan enter play; Regrowth/Terror new
  primitives); 7 entries. Smoke PASS.
- Phase K (Unite): modal_choose_n split machinery — S∈[0..5] CastFromHand variants, resolution in
  both worlds, signature folds #S. Entry added.

## Integration order (orchestrator = integrator, serial)

A. Data-only: Mountain subtypes fix, triomes, fetches, Cosmic Spider-Man (verify lifelink support). Build.
B. Colors-loading pass ("colors" array → AddColor, pip fallback) — prerequisite for C/E/I.
C. Tier-2 params: Mana Cannons, Ancient Cornucopia, Two-Headed Hellkite (+DeckUsesSecondMain),
   Progenitus gy-replacement, Braingeyser's x_draw_multiplier.
D. Hybrid mana payment (Deathrite prerequisite).
E. Domain mana (Faeburrow/Bloom Tender) + SBA effective-toughness fix.
F. Deathrite (gy-type-exile plumbing, 3-way mutually-exclusive activation).
G. Equipment subsystem (Lightning Greaves).
H. Combat-damage free cast (Maelstrom Archangel).
I. Planeswalker loyalty subsystem (Jared/Bolas/Oko + Elk transform + Kavu/Food/Treasure tokens).
J. Garth One-Eye (PerformGarthActivate, CastConjuredCopy, Regrowth gy→hand, Terror destroy, 6 entries).
K. Unite the Coalition split machinery.
Each phase builds cleanly before the next; 2d review + audits after all phases.
