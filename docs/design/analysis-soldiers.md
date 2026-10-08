# Analysis ledger — Soldiers

Deck: `decks/Soldiers/soldiers.cod` (mono-W Humans/Soldiers + Aether Vial; 60 cards, no wish -> sideboard unscanned).
Branch/worktree: `soldiers-analysis` @ `/tmp/soldiers-wt` (cut from 9380c934).

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells, never blocks unless the engine models it, has 20 life.
  Opponent-facing taxes/triggers (Esper Sentinel's opponent-cast trigger, Thalia's tax on the
  OPPONENT) are inert, but SYMMETRIC effects bind US: Thalia taxes OUR noncreature spells
  (Aether Vial costs 2 with Thalia out). Model the full card; never drop a self-affecting clause.
- No greedy / heuristic substitute inside the search window (CLAUDE.md + memory rule). Choices a
  card introduces (targets, modes, X) are SEARCHED or surfaced to the viewer, never auto-picked
  by a hard-coded heuristic unless it is a deck/archetype heuristic adopted by the user.
- Cast order / range / main-split for the deck are USER-OWNED decisions: author, present, never
  adopt unilaterally.

## Stage 1 — coverage (2026-10-04)
Missing (16): Champion of the Parish, Field Marshal, Thalia Guardian of Thraben, Thalia's
Lieutenant, Rick Steadfast Leader, Esper Sentinel, Coppercoat Vanguard, Recruitment Officer,
Jirina Dauntless General, Harbin Vanguard Aviator, General Kudro of Drannith, Cathar Commando,
Brutal Cathar, King Darien XLVIII, Fortified Beachhead, Silent Clearing.
Present: Aether Vial, Cavern of Souls, Secluded Courtyard, Unclaimed Territory, Ranger-Captain
of Eos, Recruiter of the Guard, Plains.

## Stage 2 — cards (integrated 2026-10-04; drafts in logs/soldiers_drafts/, gitignored)

Verified against Scryfall type lines: EVERY mainboard creature is a **Human Soldier** (Champion,
Field Marshal, Thalia, Thalia's Lieutenant, Rick/Greymond, Esper Sentinel [Artifact], Coppercoat,
Recruitment Officer, Jirina, Harbin, Kudro, Cathar Commando, Brutal Cathar [front; the Moonrage Brute
night face is a Werewolf only], King Darien, Ranger-Captain [+Ranger], Recruiter of the Guard). So the
"chosen type = any creature" simplification of Cavern of Souls / Unclaimed Territory / Secluded
Courtyard is EXACT here (naming Human or Soldier covers every creature). Every Soldier card / token in
cards.json carries the Soldier subtype (King Darien's 1/1 token: pay_token_subtypes [Soldier]).

The deck is NOT mono-W. Off-colour pips and how they are paid (nothing is uncastable):
- Kudro {1}{W}{B}, Jirina {W}{B}: Silent Clearing (W/B, 1 life) or Cavern/Unclaimed/Courtyard (creature
  spells), or Aether Vial.
- Harbin {W}{U}: Fortified Beachhead (W/U) or the three type lands, or Vial.
- King Darien {1}{G}{W}: ONLY the three type lands (10 sources) or Vial. His `{3}{G}{W}` token ability:
  ONLY Secluded Courtyard (4) -- now legal because every creature-source ActivatePermAbility pays under
  CreatureAbilityPayScope (both apply sites + a human-play payment probe). Before this change the
  ability could never be paid (silent no-op).

| Card | Tier | C++ / data | Deferrals (status) |
|---|---|---|---|
| Champion of the Parish | 1 | own_creature_enters_self_counters + enters_watch_subtypes [Human] (Youthful Valkyrie lane) | none |
| Thalia's Lieutenant | 2 | NEW etb_each_other_own_creature_counters + etb_counters_subtypes in FireOwnEtbTriggers (PutPlusCounters, doubler-aware); clause 2 = Valkyrie lane | none |
| Field Marshal | 1 | lord_effect Soldier +1/+1, excludes self | first-strike grant inert; opponent-side lord scope inert (spawns have no subtype) -- PROVISIONAL |
| Coppercoat Vanguard | 1 | lord_effect Human +1/+0, excludes self | ward {1} grant inert -- PROVISIONAL |
| Esper Sentinel | 1 | vanilla 1/1 Artifact Human Soldier | opponent-cast draw trigger inert (standing decision) -- PROVISIONAL (explicit sign-off) |
| Thalia, Guardian of Thraben | 2 | NEW noncreature_spell_tax: EffectiveSpellCost (symmetric, raw cost, also on spectacle), deck stamp GameState::deck_has_spell_tax, SubsetPayableSequential joins_for_mana, CheckLine declared-order walk; VialOrderMatters (item 4, below) | free-cast paths bypass the tax (no instance here) -- PROVISIONAL scope note |
| Rick, Steadfast Leader | 2 + new viewer type | Secret Lair printed name of Greymond; `scryfall_name` honoured by audit_card_costs/audit_card_fields. NEW lord_min_controlled_matching (ComputeLordBonus, continuous), etb_choose_keyword_count/_menu + keyword_grant_subtypes, Permanent::chosen_keyword_mask (padding; sim/dominance/fold-census folds), CreatureHasLifelink/CreatureHasVigilance readers + BoardSources lifelink/vigilance lists, provider hook DecisionProvider::EtbChosenKeywords, viewer `choose_abilities` (all four sites) | first-strike grant has no reader -- PROVISIONAL; keyword-pair = provider one-option choice -- NEEDS USER SIGN-OFF (the human pick is a real `choose_abilities` decision, verified end-to-end 2026-10-04). Usability (USER 2026-10-04): `scryfall_image` sld/143 = Secret Lair art in both viewers; `scryfall_name` is also an import alias (either name in a decklist) |
| Recruitment Officer | 3 | NEW PermAbilityMode::ActivatedDig (no {T}, appended last in all ModeSpec tables), PerformLookTakeDig (PerformEtbDig's body, MV filter), searched pick on the etbdig axis at FULL width, lossless whiff drop (autonomous), site-10 route clause + CardHasPostEntryActivation, K never > 1 (each pick searched; 2nd activation via the put-in-hand re-solve) | instant-speed timing collapsed to main phase (dominated) -- disclosed; "you may" always taken autonomously (weakly dominant) -- disclosed |
| Jirina, Dauntless General | 1 | 2/2 legendary; self-only sac outlet (Ranger-Captain shape) | ETB graveyard exile (opponent gy always empty; ours unread; target-player not surfaced) and the hexproof/indestructible grant -- PROVISIONAL |
| Harbin, Vanguard Aviator | 2 | NEW attack_with_n_* : ApplyAttackThresholdTeamPump (both combat worlds, declared attackers counted before the token block) + CountAttackThresholdTeamPump in PendingAttackDamage; ClassifyMainPhase / DeckFeedsCombat Main1 terms; deck stamp deck_has_attack_threshold | flying grant + own Flying inert -- PROVISIONAL |
| General Kudro of Drannith | 1 | lord_effect Human +1/+1, excludes self | graveyard-exile ETB (no legal target) and `{2}, sac two Humans: destroy power>=4` (dominated; targets = spawns or own) -- PROVISIONAL |
| Cathar Commando | 1 | vanilla 3/1 + Flash keyword | Flash timing (Ice-Fang Coatl precedent) and `{1}, sac: destroy artifact/enchantment` (Deconstruction Hammer precedent) -- PROVISIONAL (the cited 2026-10-04 approvals exist in git -- a038ded3, 81732983 -- but are for THOSE cards, not this one) |
| Brutal Cathar // Moonrage Brute | 3 | Day/night: GameState::day_night (padding; folded nonzero in sim key + dominance), ApplyDayboundOnEnter at the top of FireEtbWatchers/FireOwnEtbTriggers (night entry = back face, no ETB, never a Human), SetDayNight (in-place face swap, transform-into-front trigger), DayNightTurnBoundary at the shared end-of-turn site in BOTH worlds (CR 726.3a); ExileOppCreatureUntilLeaves (loyalty-shape `target` in human play); linked exile parked in GameState::exile, returned by an orphan sweep at the turn boundary; back face = separate cards.json entry (Kaldring precedent) + MdfcBackFaceNames for the viewer image; `day_night` in the decision JSON + `--scenario` key | exile target default = largest opponent creature -- SIGNED OFF (USER 2026-10-04, see Rulings); return-at-leave and front-face-off-battlefield -- FIXED 2026-10-04 (were disclosed gaps); back-face first strike + ward inert -- PROVISIONAL |
| King Darien XLVIII | 2 | lord_effect all-creatures +1/+1; PayToken + NEW pay_token_self_counters; self-only sac outlet; creature-source pay scope | token hexproof/indestructible grant -- PROVISIONAL; instant-speed timing collapsed -- disclosed |
| Fortified Beachhead | 2 | NEW etb_untap_control_subtypes (LandControlUntapMet in LandWouldEnterTapped / LandEntryHasChoice, before the provider reveal hook), team_pump_taps_source + team_pump_tough in ApplyFirebreathing (taps first, own mana excluded, atomic rollback), kActTeamPump tapped guard, land_sig `cu` + land_bonus `tp` | none (+1 toughness applied, inert) |
| Silent Clearing | 1 | basic_land W/B, tap_self_damage 1, sacrifice_draw_cost {1} (Horizon Canopy sibling) | none |

Cross-cutting changes:
- **Routing (item 1).** The Angels signature's `own_creature_enters_self_counters` term now requires an
  ["Angel"] watch filter (byte-identical for Angels: Youthful Valkyrie is its only carrier). Soldiers
  gets its own `soldiers` signature (Harbin / Thalia / Lieutenant / Officer / Rick params) returning
  **VialProvider**, placed above goblin (Ranger-Captain / Jirina / Darien sac outlets) and anti
  (Recruiter / Ranger-Captain tutors). A dedicated SoldiersProvider is Stage 4a. The "(Angel entered)"
  play-event text now names the matched subtype.
- **Payment (item 2).** CreatureAbilityPayScope at TurnSolver apply_one and AIEngine's executor twin for
  every ActivatePermAbility whose source is a creature (+ the K-1 repeat payments), plus a human-play
  real-payment probe in the enumerator. The other four Courtyard decks (Pirates, Knights, Minotaur,
  slivers_vial) hold no creature-source PermAbility card -> unaffected by construction (smoke to confirm).
- **Item 4 -- Thalia vs Aether Vial: IMPLEMENTED (small).** The machinery already existed
  (Plan::vial_after_casts, the Pirates puts-last order). TurnSolver::VialOrderMatters now also returns
  true for an ActivateVial of a noncreature-spell taxer alongside a noncreature hand cast, so
  AppendVialOrderVariants emits the puts-last twin and "cast Vial #2 for {1}, THEN Vial in Thalia" is a
  scored line. The twin is priced soundly (the enumerator prices casts off the pre-plan board = the
  puts-last board); it is the puts-first base that stays optimistic there (apply drops the unpayable
  cast -- honest score, wasted branch). No design doc needed.
- **Inert-param classification** for audit_viewer_decisions.py (19 Soldiers params in INERT_PARAMS) --
  PENDING the user's OK per that script's self-guard.

### Stage 2d review (rules skill, Step 4) -- findings applied
- Night entry: the face swap runs at the TOP of the enter cascade (FireEtbWatchers, and idempotently
  FireOwnEtbTriggers), so a night-cast / Vial-put Brutal Cathar never fires its ETB nor the Human
  enter-watchers (Champion, Lieutenant, Kudro's unmodelled trigger) -- verified in a staged run.
- Transform is not a zone change: m_number, counters, tapped/sick state and the linked exile persist
  (in-place Card swap); the transform-into-front trigger fires at the opponent's notional turn start.
- Thalia: increases join the RAW cost before reductions (CR 601.2f); the alternative (spectacle) cost
  is taxed too (CR 118.9d); creature spells, Vial puts and activated abilities are never taxed.
- Rick: the as-enters choice is a replacement (no stack), chosen before any watcher sees the permanent;
  the anthem is a continuously re-checked condition (CR 611.3) evaluated inside ComputeLordBonus.
- Harbin: counted over DECLARED attackers only (CR 508.1 / 508.4), pump after the attack-token block.
- Fortified Beachhead: the {T} is paid before the mana (the source cannot pay for itself), payment is
  atomic (re-untapped on failure), one activation per untapped copy.
- Recruitment Officer: no {T} (legal while sick, repeatable), MV filter on the PRINTED card.
- Bracket notes corrected where the drafts were wrong: Field Marshal / Kudro / Cathar Commando claimed
  the opponent controls no creatures (it holds PopulateOpponentSpawns bodies in 8 of 10 game indices);
  Thalia's Lieutenant claimed King Darien's ability was unpayable; Cathar Commando cited OTHER cards'
  approvals as its own; Thalia's note claimed an adopted cast-order rank. Shared land notes: Cavern of
  Souls' note wrongly said the colour restriction was unmodelled (fixed); Soldiers exactness added to
  Unclaimed Territory / Secluded Courtyard / Ranger-Captain.

### Verification (integrator, 2026-10-04 -- no suites run, the box belongs to another batch)
- `./build.sh` (Release) clean; `mtg-test` 386/386 (the routing-pin test gained `Soldiers -> Vial` and
  learned the lower-case `decks/Soldiers/soldiers.cod` file name); `bash test/scenarios.sh` 129/129
  before the three new fixtures, which each PASS and each FAIL on a control arm that removes the
  mechanism (Courtyard -> Unclaimed Territory; no Coppercoat in the top four):
  `soldiers_recruitment_officer_dig_same_turn`, `soldiers_king_darien_courtyard_pays_ability`,
  `soldiers_brutal_cathar_day_night` (Brute attacks for 3 from T4; 20-3-3 = 14).
- Item 4 evidence (MTG_PLAN_DUMP, Vial on 2 + one Plains, hand Thalia + Aether Vial): the puts-first
  plan is `DROPPED-CASTS[Aether Vial]`, its puts-last twin is enumerated and pays.
- Night -> day path: a staged night board casting two spells transforms the Brute to Brutal Cathar at
  the opponent's turn start (exiling the opponent's creature) and back to the Brute by our next turn.
- `audit_card_costs.py` (2d-bis): 329 of 385 costed cards compared and matched; the other 56 were
  Scryfall 429s (rc 2, another agent was hitting the API) and were re-fetched one at a time with the
  script's own fetch + face logic -- all 56 match (incl. Rick via scryfall_name). Every cost verified.
- `analyze_deck.py --coverage-only`: missing [], no gaps. `audit_card_fields.py`: rc 0 (snapshot
  refreshed for the 17 new entries + 2 previously-unfetched cards). `audit_viewer_decisions.py
  --no-sweep`: expected types choose_abilities / dig / land_entry / target / vial_charge; the oracle
  cross-check's four hits are a regex false match (Rick 'choose two' = choose_abilities) and three
  disclosed deferrals. Fortified Beachhead's pump count is the existing `firebreathe` decision; the
  auditor files team_pump_cost under Lathliss's DEFERRED_PARAMS entry by param name (its own comment
  warns of exactly this inheritance) -- noted, not changed.

### Open questions for the user (none blocks; defaults taken)
1. **Thalia cast order (USER-OWNED, not adopted):** the draft proposes ranking a noncreature-spell taxer
   AFTER same-turn noncreature spells (generic rank 25) so a hand-cast Vial precedes a hand-cast Thalia.
   Today Thalia ranks as a creature (before Vial); the payable-order fallback rescues the case where that
   order cannot pay, but on 4+ mana it casts Thalia first and pays {2} for the Vial. Adopt?
2. **Provider one-option choices (no-greedy rule, need sign-off):** Rick's keyword pair
   (EtbChosenKeywords: Vigilance+Lifelink, first strike has no reader); Brutal Cathar's exile target
   (largest opponent creature) -- SIGNED OFF 2026-10-04 (Rulings B); Beachhead's pump count rides the existing FirebreatheActivations
   default (already recorded COMPLIANT).
3. **Provisional deferrals** listed in the table (Field Marshal x2, Coppercoat, Esper Sentinel, Thalia
   scope note, Rick first strike, Jirina x2, Harbin, Kudro x2, Cathar Commando x2, Brutal Cathar x1 (back-face first strike + ward; the other two FIXED),
   King Darien) -- approve or reject each.
4. ~~**Slimefoot (Fungus):**~~ RESOLVED 2026-10-04 -- USER: mark it (see Rulings A).
   Follow-up (same-turn Slimefoot -> token -> Utopia Mycon sac, the ping): FIXED 2026-10-04, see
   Rulings A, "Finding -- FIXED". The site-9 "once per apply" diagnosis was wrong; nothing to decide.
5. **Silent Clearing dig:** generic dig gate (VialProvider inherits it). Opt into a searched dig later?
6. **Recruitment Officer:** pick axis = every legal hit (no width cap); one activation per action
   (picks 2..K never a ranked default). Taken as defaults.
7. ~~**Viewer cosmetic (Rick art):**~~ RESOLVED 2026-10-04 (Rulings C).
8. **Day-night display:** the decision JSON carries `day_night`; the GUI does not render it yet.


## Rulings 2026-10-04 (user) and what was done

**A. Slimefoot -- "Yes, if Slimefoot is missing its ability to create a token it should be added."**
- `CardHasPostEntryActivation` (TurnSolver.cpp) now marks EVERY `pay_token_cost` card (keyed on the
  mode's cost, not on King Darien's counter rider) -- the general form of 0f412eeb's Darien clause, so
  a Slimefoot cast this turn gets the site-9 wave fan-out and its {4} Saproling is a searched
  same-phase continuation instead of the one-option canonical base.
- Fixture `test/scenarios/fungusb_slimefoot_token_same_turn.json` (FungusB list): 7 lands, cast
  Slimefoot + {4} token on T5, swing 2+1 = 3 on T6. PASS (T6) with the fix; FAIL (T7) on the 0f412eeb
  control binary.
- **GT WILL MOVE -- FungusB only.** Slimefoot is in `decks/Fungus/candidate-b-2026-09/Fungus.cod`
  (1 copy), NOT in the incumbent `decks/Fungus/Fungus.cod`, so the `fungus` cases are unaffected by
  construction while the `fungusb` cases (all three tiers) will move. Each moved game needs a
  per-game verdict at the next smoke/regression before any `--accept` (no suite was run here: the
  box belongs to another batch).
- Finding -- FIXED 2026-10-04 (`MTG_SAC_FODDER_ACT_MAKER`, default ON, `=0` = the old enumeration).
  The same-turn Slimefoot + {4} Saproling + Utopia Mycon sacrifice (the ping for the last point) was
  inexpressible at any budget. **The earlier diagnosis -- "site 9 fires once per apply" -- was a
  symptom, and the claim that an on-board Slimefoot finds the line was FALSE**: that probe won by
  ATTACKING with the (non-sick) Slimefoot. With Slimefoot on the board but sick (`resume_at main1`)
  the line failed identically (T6 not T5, at budget 2000 / d5 too).
  * ROOT CAUSE: the sac-outlet emitter (CollectActions) resolves its victim against the board the
    plan STARTS from (`CanonicalSacVictim` -> -1 with no Saproling out) and `continue`s, unless a FREE
    counter-costed maker (spore/fade, `SameLineSacFodderSource`) can be fused into the sac. A
    mana-costed maker (`pay_token_cost`) is excluded from fusion on purpose -- its {4} would be paid
    inside a pre-cast SacForMana the enumerator had already credited. So no plan, and no site-9
    continuation (enumerated by the same CollectActions at the post-cast state), could ever hold
    "make the Saproling, then sacrifice it".
  * FIX: when there is no victim and no free maker but an AFFORDABLE pay-token maker of the outlet's
    subtype is on the board, emit the outlet in its TRAILING form -- `Kind::SacCreatureOutlet`,
    victim `kSameLineSacVictim`, no ritual credit (its mana floats after the casts; an any-colour
    outlet is fanned over `ChosenFloatColorCandidates` and `ApplySacCreatureOutlet` floats the chosen
    letter). Both trailing dispatchers (rollout `apply_trailing_activations`, executor
    `exec_trailing_activations`) move such sacs to the END of the pass via the one shared
    `TurnSolver::DeferSameLineSacs`, so the co-selected `{4}` activation has made the token when the
    sac resolves its sentinel. A subset without the maker strands the sac (no victim -> no-op, both
    worlds) and collapses onto its sibling: dedupe work, never a wrong line. The fodder guard
    (`plan_can_add` / `plan_fodder_credit` / the FodderIndex twin) credits a co-selected PayToken
    activation as UNBOUNDED supply (was: zero -> an over-reject). No new breakpoint, no new site,
    no greedy, no truncation; the just-cast case rides the EXISTING site-9 occurrence, whose
    continuation list now contains `[{4} Saproling, sac it]`.
  * Fixtures (both FAIL T6 with `=0`, PASS T5 with the fix): `fungusb_slimefoot_cast_token_sac_ping`
    (cast Slimefoot this turn) and `fungusb_slimefoot_onboard_token_sac_ping` (sick Slimefoot
    already out). mtg-test 389/389; scenarios 136/136.
  * Cost (single games, play settings, `MTG_ROLLOUT_STATS` units ON vs `=0`): FungusB seeds 5005+gi
    gi0-5 units 1,834,533 vs 1,829,857 (+0.26%; 4 of 6 games identical, gi1 +2.9%, gi4 +6.7%), win
    turns identical; Soldiers d3/b20 gi0-3 units identical (no non-self outlet in the list). Wall
    within noise.
  * Blast radius: only a deck holding a `pay_token_cost` card AND a non-self-only sac outlet --
    FungusB alone today (King Darien's Soldiers outlets are all self-only). Incumbent Fungus,
    Soldiers and every other deck are unaffected by construction. **FungusB GT will move** --
    per-game verdicts at the next suite run (no suite run here: the box belongs to another batch).
  * Not logged: the executor's `SacCreatureOutlet` branch writes no LogAbility line (pre-existing,
    every value outlet); the sac shows only in the life totals. Unchanged here because a log line
    folds into every value-outlet deck's digest.

**B. Brutal Cathar -- "There are actually critters for Brutal Cathar sometimes. They just don't do
anything otherwise."**
- That statement is the user's SIGN-OFF for the single-default exile target (largest opponent creature
  by power): the target is immaterial because the opponent's creatures never act. The human still
  board-clicks it (`target`) when more than one is legal.
- In a REAL game: `audit_viewer_decisions.py --verify-card "Brutal Cathar"` -> VERIFIED (seed 1 gi 2):
  the exile surfaced as a `target` decision over the goldfish's spawns.
- The ETB exile genuinely fires: fixture `soldiers_brutal_cathar_exiles_opp_creature.json` (opponent
  Skyshroud Cutter is in exile at game end, Cathar/Brute wins T5) using a NEW scenario assertion
  `expect_exile_contains` (main.cpp RunScenario) -- the only observable, since exiling a passive body
  moves neither life nor win turn. Control arm (no opponent creature) FAILS the assertion.
- The two disclosed gaps were re-checked. Opponent critters make the EXILE live, but nothing in this 60
  can make Brutal Cathar / Moonrage Brute LEAVE (every sac outlet here is self-only; Kudro's
  sacrifice ability is deferred; the opponent never blocks), so both stay unreachable in Soldiers play.
  They were cheap and general, so both are FIXED anyway: `FireLeavesBattlefieldTriggers` (every death
  site via OnCreatureDies, and the flicker exile half) now, for either day/night face only,
  (1) returns the linked card at the leave (CR 610.3; the turn-boundary sweep stays as the backstop for
  leave paths that bypass the hook) and (2) re-faces a Moonrage Brute that left to its Brutal Cathar
  front in the graveyard / exile (CR 711.8 / 712.8a). Keyed on the two day/night params, so every
  other card is byte-identical. Unit tests: `test/unit/test_soldiers_cathar.cpp` (3 cases).

**C. Rick, Steadfast Leader -- "Let's do whatever we can for Rick to make it most usable."**
- Art: cards.json `scryfall_image: "sld/143"` (Scryfall's Secret Lair printing: name Greymond,
  printed_name Rick, Steadfast Leader). The engine loads it into `CardDatabase::ImageRefs()`; the
  decision JSON carries `image_refs` (only when an alias card is in the game, so other decks' JSON is
  byte-identical) and the game log carries `imageRefs` (only for alias cards in the deck). Both
  viewers (`tools/play/index.html` scryImg via IMAGE_REFS; `tools/replay/index.html`) request
  `api.scryfall.com/cards/sld/143?format=image` -- verified 302 to the Rick art. That covers board,
  hand, hover popover, dialogs and the choose_abilities panel (all go through scryImg).
- Text: the viewer shows no oracle text anywhere (hover = the card image), so the SLD image IS the
  tooltip and it prints Rick's text. The cards.json oracle_text already reads "As Rick, Steadfast
  Leader enters..."; its bracket note was updated (it claimed the engine had no alias table).
- Decision: `choose_abilities` verified END-TO-END through the real protocol --
  `audit_viewer_decisions.py --verify-card "Rick, Steadfast Leader"` -> VERIFIED (seed 1 gi 3); the
  frame offers all three pairs (First strike+Vigilance / First strike+Lifelink / Vigilance+Lifelink,
  default 2) with `image_refs`, and answering 0 logs "Rick, Steadfast Leader -- chose first strike
  and vigilance" (a non-default human pick is applied).
- Import: `CardDatabase::CanonicalName` (alias = an entry's `scryfall_name`) is applied by
  DeckLoader (.txt and .cod), and `analyze_deck.py` / `audit_viewer_decisions.py` canonicalise the same
  way, so a decklist may say "Greymond, Avacyn's Stalwart" (Arena "(SLD) 143" suffix stripped too):
  verified -- coverage resolves it to Rick, and an engine game loads it as Rick.
- Audits: audit_card_costs (scryfall_name; Rick/Cathar/Slimefoot re-fetched, all match),
  audit_card_fields rc 0, audit_viewer_decisions --no-sweep: Rick's "choose two" and Cathar's "exile
  target" are now credited to their decisions (were regex advisories); only Kudro / Cathar Commando's
  disclosed deferrals remain.
- Still open for Rick: the provider's autonomous keyword pair (Vigilance+Lifelink) is a one-option
  default awaiting sign-off (open question 2), and first strike has no reader (PROVISIONAL).

## Stage 4 / 5 (2026-10-04, worktree /tmp/soldiers-wt, rebased on bcbf29df; autonomous run)

### Stage 4a -- provider routing
- `provider_audit.py`: once the profile existed Soldiers would SHARE `Vial` with slivers_vial (a
  `--check` failure). NEW `SoldiersProvider : VialProvider` -- an EMPTY derivation of what the deck
  actually rode (VialProvider's `MTG_KNIGHTS_ORDER` clauses key on `cast_trigger_creates_tokens` /
  `etb_dig_requires_subtypes`, which no Soldiers card carries, so its cast order is the generic one).
  Certificate: NotAssessed (Vial instant-speed puts, Commando flash, Beachhead/Harbin pumps, Rick's
  mid-turn anthem must be priced first). Routing pin updated (`Soldiers -> Soldiers`).
  `provider_audit.py --check` rc 0. Commit 8465d838.
- Hooks added since (both per-job overridable via `heurarm`):
  * `MTG_SOLDIERS_ORDER` -- PROPOSED total cast order, **default OFF** (USER question; see
    `docs/design/cast-order-rankings.md` "Soldiers").
  * `MTG_SOLDIERS_BUCKET_DISCARD` -- authored discard buckets, **default ON** per the authoring
    brief's contract (`EnvOn(..., true)`, `=0` -> generic). PROVISIONAL, user review.
    Proposal: `docs/design/soldiers-discard-policy-proposal.md`. Commit f7818b30.

### Stage 4 -- baseline profile
- `analyze_deck.py decks/Soldiers/soldiers.cod --no-rebuild` (5m26s): card-scores-only profile
  (1000 games d5), no hand-score gate, default land window 1..5, stop_at 4, `required_pieces []`.
  Cost diagnostic NO_COST_INTERACTIONS. Discard evidence (400 g d3): **DISCARD_INERT -- no cleanup
  shed reached by either caller**. Top card scores: Champion +0.39, Officer +0.21, Coppercoat +0.12.
- Play settings: no value leaf exists yet, so play = the built-in **d5 / budget 20** (`[play]
  source=default`); `value_play` arrives only with the (out-of-scope) value-leaf generation.

### DEFECT FOUND + FIXED -- the analyzer's DISCARD_INERT was false (commit 9503b7c4)
`analyze_deck.py`'s shed census regex was anchored on the closing `===` of the engine's
`=== SHED STATS ...` line, which has carried two more trailing fields since 4ebb31ac (2026-08-22).
It matched nothing, the census read (0,0,0) and the stage printed DISCARD_INERT. Measured truth for
Soldiers (5b batch below, MTG_SHED_STATS): **real=5, rollout=467,425 sheds per 4,400 games**
(422,705 at <4 lands; hand 8: 457,211, hand 9: 4,877). Fixed (un-anchored, and an unparseable census
now raises). **Fleet note:** the Pirates, CritterLifegain, BreachingDragonstorm, WhiteKnights and KittyEquipment
ledgers cite DISCARD_INERT -- every such verdict produced after 2026-08-22 is suspect and should be
re-run (not done here: out of this deck's scope).

### 5b -- multi-depth sanity (pooled batch, seed 777000, MTG_DUMP_WINS + MTG_FLAG_NONCONV)
| cell | games | win% | avg (unwon=9) | wall core-ms/game |
|---|---|---|---|---|
| d0 | 2000 | 99.95 | 4.6070 | ~0 |
| d3 b10 | 600 | 100 | 4.3733 | 571 |
| d3 b20 | 600 | 100 | 4.3733 | 937 |
| d5 b20 (play) | 600 | 100 | 4.3733 | 1039 |
| d5 b40 | 600 | 100 | 4.3717 | 1490 |
Monotone (no searched game slower than a shallower/cheaper cell; d3 vs d0 on the shared 600:
123 faster, 0 slower). Plausible clock: T4 388 / T5 202 / T6 9 / T8 1 at play settings -- a
1-3-drop lord deck killing on T4 two times in three. d3b20 and d5b20 share a play DIGEST (identical
play on 600 games: depth 4-5 never binds at b20). `[nonconv]`: 0 lines.

### 5c -- budget starvation (seed 777000 outliers, pooled single-game jobs)
- gi33: T5 at d5b20 / T4 at d5b40 and at **d8 b0** and d5 b0 -> **budget starvation at b20** (1 of 600
  games). Threshold: b40 recovers it.
- Every other slow game (T6: gi28,128,162,308,361,376,417,448,480; T8: gi569) plays IDENTICALLY at
  **d8 b0** (full-depth, unbudgeted: same win turn) and at d5 b0 where it finished -> legitimately slow
  (draw/mulligan-limited), not starvation, not a search defect. (gi417 d5b0 was stopped by me after
  >4 min -- my own probe; its d8b0 answer, T6, already settles it.)

### 5a -- mismatch harnesses (pooled batch, MTG_FULL_DEPTH=1 MTG_FD_ORACLE=1 MTG_FLAG_NONCONV=1)
Seeds 1001/2002/3003/4004 x {d3 b10, d5 b20} x 300 games = 2,400 games: **0 `[fd-diverge]`, 0
`[nonconv]`** (plus 0 `[nonconv]` over the 4,400-game 5b batch). Clean. (Full-depth avgs: d5b20
4.360/4.353/4.373/4.373, d3b10 4.360/4.353/4.380/4.377.)

### 5c2 -- horizon-honest tie-break (`leaf_tiebreak_check.py`, PLAY settings d5/b20, 12 blocks)
24,000 games (12,000 paired): **0 changed games, and every one of the 12 blocks has a BYTE-IDENTICAL
play digest across the two arms** while `units` differ (the tie-break IS consulted in rollouts but
never moves a committed decision -- the deck wins inside the horizon). The script prints "NO SIGN
AT THIS SAMPLE / re-run at --blocks 48"; not re-run, because identical digests on 12k paired games
already say the lever cannot change this deck's play at play settings. **Keep the default (ON).**
Play-setting avg over the 12k: 4.358..4.397 per 1k block (~4.38).

### A/B batch ab1 -- cast-order proposal + discard bound (ONE pooled batch, heurarm per-job flags)
Held-out seed block 8,800,000 (disjoint from every suite seed), arms paired per game:
`base` (shipped: order OFF, discard buckets ON) / `order` (MTG_SOLDIERS_ORDER=1) / `gendisc`
(MTG_SOLDIERS_BUCKET_DISCARD=0, the generic max-MV) / `shedworst` (MTG_SHED_WORST=1, the bound).
| cell | games | order vs base | gendisc vs base | shedworst vs base |
|---|---|---|---|---|
| d0 | 4000 | **-0.00350 t=-3.30** (16 better / 2 worse) | 0 changed (digest differs: real sheds move, no win turn) | 0 changed, digest IDENTICAL (no rollouts at d0) |
| d3 b10 | 2000 | **-0.00350 t=-2.65** (7 / 0) | digest IDENTICAL | 0 changed (digest differs) |
| d5 b20 (PLAY) | 2000 | **-0.00300 t=-2.45** (6 / 0) | digest IDENTICAL | digest IDENTICAL |
Units at d5b20: base 115,576,724 / order 115,584,044 (+0.006%) / gendisc +0.38% / shedworst -0.63%.
SHED STATS over the batch: real=8, rollout=2,102,646 (1,894,739 under 4 lands).
- **Discard (5i): the BOUND is zero.** Best-vs-worst ranking (`MTG_SHED_WORST`) changes no win turn
  at any cell and no play at all at play settings, so no ranking can be worth anything measurable on
  this deck -- the axis is closed. The authored policy is non-inferior by construction (identical
  play at d3/d5). Shipped default ON per the brief; adoption remains a USER review.
- **Cast order: a measured improvement at every depth, 0 slower games at d3 and at PLAY settings.**
  d0's two slower games (gi1379, gi3261, 4->5) are greedy d0 churn (no search; acceptable per 5e).
  Mechanism (gi10, d5b20, read from the game logs): with the proposal the search casts Thalia's
  Lieutenant on T2 (base: Coppercoat) and Champion+Coppercoat T3 -> T4 kill vs T5 -- the order drives
  the rollout/leaf policy's sequencing (Champion first, Lieutenant last), which re-scores the root
  choice. Stays DEFAULT OFF: cast order is USER-OWNED (question recorded in cast-order-rankings.md).
- **Gate probe** (live UnprunedGates for this deck): altpayload dig xspell groupcap comboline
  searchorder blinktarget jittemode tapreserve digchain. (`tutor` is dead: Recruiter/Ranger-Captain
  targets are not narrowed by TutorCandidates.)

### 5e / 5f -- heuristic accuracy vs the full-search oracle (MTG_UNPRUNED=1, seed 8.8M, first 1000)
| cell | arm | shipped avg | unpruned-oracle avg | games the oracle wins earlier |
|---|---|---|---|---|
| d5 b20 | base (generic order) | 4.3780 | 4.3740 | gi10, 170, 302, 308 (all 5->4) |
| d5 b20 | proposed order | 4.3750 | 4.3740 | gi170 |
| d3 b10 | base | 4.3790 | 4.3740 | gi10, 170, 302, 308, 607 |
| d3 b10 | proposed order | 4.3760 | 4.3740 | gi170, 607 |
Oracle never slower. Classification (single-game repros, all pooled/short):
- **gi10 / gi302 / gi308 -- CAST ORDER IS A HARD PRUNE.** The shipped search does NOT reach T4 even at
  **d8 b0** (full depth, unbudgeted); `MTG_UNPRUNE=searchorder` ALONE recovers all three at d5b20, and
  the proposed total order recovers all three at d5b20 AND at d8b0. Same class as StompySurprise's
  2026-08-25 finding (`cast-order-rankings.md` "ADOPTION MEASUREMENT"): with the ordering search off,
  `CastOrderRank` fixes the sequence, and today's generic rank ties every creature at 10 (plan order).
  An inexpressible line under the shipped config = a DEFECT; its fixes are (a) the proposed order
  (user-owned) or (b) opening the ordering search for this deck -- measured next (batch `so`).
- **gi170 -- the user-ruled CLAIRVOYANT-DIG exclusion, not a defect.** Attributed by opening each live
  gate alone: only `dig` recovers it. The oracle line plays Silent Clearing and cracks it the same turn
  (`{1}, sac: draw`) while holding castables; the generic provider deliberately declines the searched
  dig axis (docs/design/generic-dig-gate.md, USER 2026-09-29: a clairvoyant rollout digs exactly when
  it can see a better top card). Unreachable at d8b0 under both orders, by that ruling.
- **gi607 (d3 only) -- budget starvation**: T4 at d8b0 under both orders.
- 5f (runtime gate): Soldiers adds **no** pruning heuristic. The existing generic gates' cost on this
  deck: unpruned d5b20 = 65.57M units / 1000 games vs shipped 57.79M (+13%) for the 4 games above.

### 5g -- earliest-win rule miner (ONE pooled batch: MTG_DUMP_EWINS=1 MTG_SEARCH_ORDER=1, every turn,
d5 b3000, seeds 2002/3003/4004 x 300 games; 3,926 decisions; 24/24 workers busy)
- ORDER rules (0 conflicts): Champion before Esper Sentinel (12/0), Champion before Thalia's
  Lieutenant (5/0), Esper Sentinel before Harbin (4/0). **All three agree with the proposed
  `MTG_SOLDIERS_ORDER`** (Champion 1, Sentinel 6, Harbin 11, Lieutenant 17); none conflicts with it.
  Sparse, as expected of a lord/anthem deck.
- INCLUSION: Rick -0.30 (53 help / 2 hurt), Lieutenant -0.11, Champion -0.09, Coppercoat/Kudro -0.08,
  Field Marshal -0.07, Officer -0.06 -> cast them (already cast). Positive deltas -- Brutal Cathar
  +0.24, Thalia +0.17 (her tax hits our own Vial), Jirina +0.16, Ranger-Captain +0.15 -- all carry
  help > 0, i.e. SITUATIONAL: left to the search, no gate (5g table: never gate a +delta with help>0).
- LAND: earliest-win lines favour Silent Clearing then Plains; the land drop is searched -- no rule.
- Verdict: nothing to encode beyond the (already proposed) order; no generic limiter found.

### 5h -- play-viewer decision surface (`audit_viewer_decisions.py`, 40-game sweep from seed 9001)
Expected types from params: choose_abilities, dig, land_entry, target, vial_charge. Observed in the
sweep: choose_abilities, dig, land_entry, vial_charge; `target` (Brutal Cathar) escalated to a
targeted seed-search -> VERIFIED (seed 9001 gi28). **5h PASS** -- no HARD MISS / SELF-GUARD / DRIVER
failure. Oracle cross-check advisories: Kudro's and Cathar Commando's destroy-target abilities, both
carrying their disclosed deferral notes (PROVISIONAL, open question 3). Fortified Beachhead's pump
count rides the existing `firebreathe` decision (auditor files `team_pump_cost` under DEFERRED_PARAMS
by name -- noted in Stage 2, unchanged); it never fired in 60 benchmark games ({5}+{T} is rarely
affordable before the kill), so it is listed in the 5d plan as an item to exercise deliberately.

### 5d -- claude-play sweep: NOT RUN HERE (orchestrator's fan-out). Plan + pooled benchmark manifest:
`logs/soldiers_5d_plan.md` (gitignored; 20 seeds from 9,100,000 chosen for card coverage out of a
60-seed benchmark pass; `logs/soldiers/5d/bench_manifest.json`, traces in `logs/soldiers/5d/traces/`).
Benchmark (60 games, play settings): T4 x33, T5 x26, T7 x1 (s9100037: a 5-land + Vial + Rick flood
keep -- T7 confirmed optimal at d8 b0).

## Claude-play sweep
- commit: `74737c6e`
- seeds: 9100001..9100059 (the 20 in `logs/soldiers_5d_plan.md`) games: 20
- flags: 0 unresolved

Run 2026-10-04 by the orchestrator's fan-out (one Opus agent per seed, results in
`logs/soldiers/5d/results/<seed>.json`, gitignored). Claude tied the AI in 19/20 and beat it in 1
(9100012, T4 vs T5 -- a mulligan call, below). Decision types seen: mulligan, bottom, main_phase,
vial_charge, land_entry, choose_abilities, dig (`target` and `firebreathe` not reached; `target` is
verified separately at seed 9001 gi28, 5h). 1 strong + 11 weak flags, every one resolved:

| # | seeds | sev | flag | resolution |
|---|---|---|---|---|
| 1 | 9100047 (also 9100043, 9100019) | strong | Vial-put vs hand-cast ORDER inexpressible for Champion of the Parish / Thalia's Lieutenant: "cast Champion, THEN Vial-put a Human" offered to neither search nor human (`VialOrderMatters` was a 4-term param list). | **FIXED.** `VialOrderMatters` is now structural (a put + a cast) and the twin is gated on `TurnSolver::VialOrderChangesOutcome` -- both orders applied on copies, compared under the canonical sim key -- so any enters/cost/reveal class qualifies with no card list. Also: base plans marked only for breakpoint site 9 (Recruitment Officer's dig, a post-plan site a base plan never opens) are no longer excluded; continuation lists are twinned (a tutored Champion cast then the Vial puts, 9100019's shape) and BOTH worlds honour a continuation's flag (`apply_continuation_plan` / AIEngine `cont_vial_after`) -- which also fixes a latent lockstep bug: the executor's breakpoint re-solve could pick a twin and then deploy its puts FIRST. Interleavings beyond one split point remain deferred: `docs/design/vial-put-interleaving.md`. |
| 2 | 9100047, 9100043, 9100019 | weak | Plan SUMMARY printed Vial puts in action-vector order (last) while they resolve first. | **FIXED.** `SummarizePlan` now always lists puts where they resolve: first, or right after the last cast for a `vial_after_casts` plan (it used to reorder only inside the old predicate). The viewer shows the same string. |
| 3 | 9100001, 9100004, 9100019, 9100025, 9100042, 9100052, 9100059 | weak | `land_entry` note for Fortified Beachhead said "reveal a matching land"; the reveal is a Soldier card. | **FIXED** generally: the note (main.cpp) and the viewer panel (index.html) name the `reveal_types` with per-type articles ("reveal a Soldier card", "an Island or a Mountain card"). |
| 4 | 9100004 | weak | Payer taps the painful Silent Clearing for {W} while Unclaimed Territory / Secluded Courtyard sat untapped (1 life/tap). | **VERDICT: not changed.** The only pain-aware ordering in the payer (`PreventDamageProvider::ManaSourceRank` +5/pain and `dmgev::PainAwarePay`) is armed solely under `dmg_events_armed` (Prevent Damage). The generic scarcity rank sees Clearing as a W/B dual (20) and the two creature-only any-colour lands as rainbow (50), so it spends the dual first by design (keep flexible sources). No existing generic pain preference missed a case; adding one is a fleet-wide rank change (every painland deck's GT), and life is inert in this goldfish deck (no life payment, no life-total payoff). Recorded, no change. |
| 5 | 9100012 | weak | claude_win 4 < ai_win 5. | **VERDICT: mulligan, not play.** The AI mulled a 6-card 1-lander on the play; Claude's keep relied on `--reveal` draws. Handing Claude's keep to the search (`--choices 0,1,5 --choices-then-auto`) also wins T4 on the same line. A keep-policy data point for the mulligan stage (no keep table yet). |

Measured after fix #1-3 (worktree build vs a scratch-worktree build of 74737c6e, Soldiers d5/b20, one
pooled batch each, 60 bench seeds + 300 cost seeds 9300000..): win turns **identical in all 360 games**
(9100047 stays T5: the twin ties the base line there, and ties keep the base); units_total
**+3.1% (60) / +1.6% (300)**, wall +5-10% (run-to-run wall noise 4-7%). New scenarios FAIL before / PASS
after: `soldiers_vial_after_champion_cast`, `soldiers_vial_after_t3_champion_officer` (the 9100047 T3
line), `soldiers_vial_after_tutor_continuation` (9100019 shape); unit tests
`test/unit/test_soldiers_vial_order.cpp` (3 of 4 fail before). Regression tier vs the same base build
(GT itself is stale on this branch for ~50 non-Vial keys, so attribution is base-vs-new, not vs GT):
digests moved ONLY on Vial decks -- soldiers (d3 s2002, d5 s2002, d5 s3003), minotaur (d3 s2002/s3003,
d5 s3003, 2hg d3 s2002), pirates (d3 s3003). Two searched games slower, both **budget churn**
(recover at 4x and 16x budget): soldiers d5 s3003 gi72 T4->T5, minotaur d3 s2002 gi295 T4->T5. No
game faster. Reference gate: identical to base except `Pirates/claude_s4_gi3` ok -> repaired (2 stale
plan indices; same T5 outcome). GT NOT re-accepted (see open question F).

### 4-bis / 5j -- regression-suite membership (ALL THREE tiers)
Rows added to `test/regression_cases.sh` at pirates' counts (smoke d0x1000 / d3 b10 x150 / d5 b20 x75
@1001 + `soldiers2hg` d3 x50 smoke canary; regression d0 @2002, d3/d5 @2002+3003; overnight d0 x2000 @
4004/6006/8008/10010, d3 b20 x1000 and d5 b40 x500 @4004-7007).
- **smoke** (`--deck=soldiers,soldiers2hg`): 4 NEW -- d0 4.5940, d3 4.3333, d5 4.3600, 2hg d3 5.0400.
  Cross-depth per game: no d5 game differs from d3 on the shared 75; no d3 game slower than d0.
  ACCEPTED (filtered); GT diff = exactly the 4 keys + header; `check_gt_logs.py` consistent.
- **regression** (`--deck=soldiers`): 5 NEW -- d0 4.5990, d3 4.3800/4.3533, d5 4.3733/4.3733; only
  cross-depth difference s3003 gi72 d3 T5 / d5 T4 (deeper better). The tier printed REGRESSION
  DETECTED solely from the reference-reproducibility gate: `Hinata2/claude_s1_gi0.json` ENUM-GAP and
  `Mirrorwing_Dragon/v2-instigator-entrance/claude_s51_gi50.json` play-drift (T5 vs ref T4). **Both
  reproduce identically with the BASE binary bb5bcceb** (scratch-worktree build, `--only` those two
  refs) -> pre-existing, not caused by this branch (whose C++ changes route only Soldiers). Not
  investigated further here (out of this deck's scope) -- reported to the orchestrator. Soldiers
  keys ACCEPTED (filtered); diff = exactly the 5 keys; GT logs consistent.
- **3x cost rule** (`suite_gate.py --cost --no-run`): soldiers 3077.8 ms/game (worst searched case,
  d5 b20 s2002) vs fivecolour 1033.5 = **2.98x -> PASS, but borderline.** Methodology note: in a
  small FILTERED run the per-game ms is ~2-3x a large pooled run's (the same d5 b20 cell read 1.04
  s/game in the 4,400-game 5b batch; units/game are equal, 61-75k, so it is per-unit wall, i.e. cold
  per-worker caches / small pools, not deck cost). A `--measure-all` pooled measurement would be the
  apples-to-apples figure; not run (it re-runs every deck's tier).

### Play settings and headline numbers (Stage 6 item 3)
Play = built-in **d5 / budget 20** (no `value_play` until a value leaf is generated -- out of scope).
Regression-suite numbers at play-depth cells: d5 b20 4.3733 / 4.3733 (s2002/s3003), smoke 4.3600;
overnight d5 b40 4.370-4.400. **Win rate 100% at every searched cell** measured (d0 99.95%). Over
12,000 play-setting games (5c2 base arm): avg 4.36-4.40 per 1k block (~T4.38). Kill turns at play
settings: T4 ~65%, T5 ~34%, T6 ~1.5%, T7-8 rare (flood keeps, optimal at d8 b0).

### Stage 6a -- deck-specific heuristics in force (SoldiersProvider : VialProvider)
| hook | state | classification |
|---|---|---|
| CastOrderRank (`MTG_SOLDIERS_ORDER`) | OFF -> VialProvider -> generic (creatures tied at 10, Vial 20) | the generic tie is a measured HARD PRUNE on ~3/1000 play games (5e) -- USER question |
| CleanupDiscardCandidates (`MTG_SOLDIERS_BUCKET_DISCARD`) | ON (brief contract), PROVISIONAL | shed ranking; bound = 0 at every cell (no measurable effect) |
| GradesNoWinLeaf | inherited default ON | play byte-identical either way (5c2) |
| dig gate (Silent Clearing) | generic: dig only when no castable nonland; no searched dig axis | user-ruled clairvoyance exclusion (gi170) |
| everything else | root defaults | -- |

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-10-04)

`verify_deck.py decks/Soldiers/soldiers.cod --write-ledger --no-network` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 23 cards full (missing=0, partial=0) |
| card_costs | SKIP | yes | skipped (--no-network) |
| card_fields | PASS | yes | 500 cards match snapshot (cost/PT/types/keywords); 14 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | PASS | yes | in all 3 suite tiers with GT (soldiers) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 5 type(s) wired (emitter + GUI): choose_abilities, dig, land_entry, target, vial_charge |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 7002] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/152 decisions: determinism+integrity+progress hold |
| claude_sweep | SKIP | yes | no Claude-play sweep recorded |
| suite | PASS | yes | suite gate: soldiers is regression key `soldiers` |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Champion of the Parish: own_creature_enters_self_counters 1 + enters_watch_subtypes ["Human"
- coverage deferral -- Field Marshal: The +1/+1 Soldier lord is modelled: Knight Exemplar's / Goblin King's shape exactly (lord_effect, subtypes_affected Soldier, +1/+1, lord_excludes_self -- 'Other' is per-instance by address inside ComputeLordBonus, so Field Marshal never pumps itself but a second copy would pump it). Matching reads the creature's LIVE subtypes, so it reaches every Human Soldier in the deck, Soldier TOKENS (Myrel's colorless Soldier artifact tokens, King Darien's and General's Enforcer's Soldier tokens) and a typed-animated Gideon, Battle-Forged (4/4 Human Soldier) via animated_printed_types; it does NOT reach Moonrage Brute (Werewolf only) or Descendant of Storms' Spirit tokens. Field Marshal itself is a Soldier, so it counts for Harbin's five-Soldier attack and Myrel's X. [PARTIAL: the lord scope is 'Other Soldier creatures' on EVERY side, but ComputeLordBonus applies lords to the controller's creatures only. WHY inert: the passive opponent's only creatures are the scheduled GoldFishRunner::PopulateOpponentSpawns bodies (8 of 10 game indices), which carry no subtype at all -- no Soldier for the lord to reach -- and never attack or block; Goblin King carries the same disclosure. PROVISIONAL pending user sign-off.
- coverage deferral -- Field Marshal: PARTIAL: the granted FIRST STRIKE is not modelled. WHY inert: structural -- the passive opponent never blocks, and the engine collapses combat damage into one event, so strike order can never change an outcome (Knight Exemplar, Ocelot Pride, Lyra carry the same disclosure). No card in this deck has double strike or reads 'creatures with first strike', so the keyword has no other reader. PROVISIONAL pending user sign-off.
- coverage deferral -- Thalia, Guardian of Thraben: First strike: real keyword (Keyword::FirstStrike), inert vs the passive non-blocking goldfish opponent but kept faithful. The tax is SYMMETRIC and modelled via noncreature_spell_tax:1 -- each permanent with it, controlled by ANY player, adds {1} GENERIC to every noncreature spell's cost, applied to the RAW cost in EffectiveSpellCost BEFORE reducers (CR 601.2f: cost + increases - reductions). It binds OUR noncreature spells: in Soldiers that is Aether Vial ({1} -> {2} with one Thalia out). Its opponent-facing half is inert (the goldfish opponent never casts). Creature spells (incl. Cavern-cast ones) are untaxed; Vial PUTs and activated abilities are not casts and are untaxed. Legendary: the second copy dies to the engine's legend rule (GameEngine EnforceLegendRule). Same-turn ORDER: a hand-cast Thalia and a hand-cast Vial in one plan are cast in the provider's CastOrderRank order (unchanged -- Thalia ranks as a creature, before the Vial), falling back to the payable order (MTG_PAYABLE_ORDER) when that order cannot pay; ranking a taxer after noncreature spells is a USER-OWNED cast-order question, recorded in analysis-soldiers.md, not adopted here. A VIAL-PUT Thalia alongside a noncreature hand cast is a searched order: TurnSolver::VialOrderMatters emits the puts-last twin (Plan::vial_after_casts), so 'cast the second Vial for {1}, THEN Vial in Thalia' is a line the search scores. Taxed sites: EffectiveSpellCost (all three worlds), the sequenced feasibility walk (joins_for_mana), the viewer's declared-order CheckLine walk. NOT taxed (no instance in this list): free / without-paying-its-mana-cost casts that bypass EffectiveSpellCost -- a cross-card scope note, PROVISIONAL.
- coverage deferral -- Thalia's Lieutenant: BOTH clauses modelled; nothing deferred, nothing inert. CLAUSE 1 (ETB): etb_each_other_own_creature_counters 1 + etb_counters_subtypes ["Human"
- coverage deferral -- Thalia's Lieutenant: "Human"
- coverage deferral -- Rick, Steadfast Leader: Secret Lair printed name of GREYMOND, AVACYN'S STALWART (oracle text/cost/PT verified against that oracle card on Scryfall; the cards.json name is the deck's printed name; scryfall_name is ALSO an import alias -- CardDatabase::CanonicalName, applied by DeckLoader and analyze_deck.py, so a decklist may list either name -- and scryfall_image sld/143 pins the Secret Lair printing's art for both viewers). CLAUSE 1 (as-enters choice, CR 614.12 replacement, no stack): etb_choose_keyword_count 2 from etb_choose_keyword_menu [first strike, vigilance, lifelink
- coverage deferral -- Rick, Steadfast Leader: Human
- coverage deferral -- Rick, Steadfast Leader: PARTIAL: first-strike grant has no reader; PROVISIONAL pending user sign-off.
- coverage deferral -- Esper Sentinel: Modelled as a 1/1 white ARTIFACT CREATURE -- Human Soldier (vanilla_creature, no params; colour W derived from the {W} pip). Every characteristic is live: ARTIFACT + CREATURE types (Card.h/EffectHandler permanent paths; TurnSolver marks the cast adds_durable_artifact -- harmless, nothing in this list reads artifact count), HUMAN (Champion of the Parish 'another Human enters', Thalia's Lieutenant / Coppercoat Vanguard / Kudro Human readers) and SOLDIER (Field Marshal and other Soldier lords, Harbin's Soldier-attack count, Fortified Beachhead's Soldier mana), MV 1 (Aether Vial on 1 counter; Ranger-Captain of Eos's MV<=1 tutor), toughness 1 (Recruiter of the Guard's toughness<=2 tutor). As a CREATURE spell it is NOT taxed by Thalia, Guardian of Thraben. Cavern of Souls' coloured mana casts it under either Human or Soldier. PARTIAL -- goldfish-inert, per the analysis-soldiers standing decision: the cast trigger watches only an OPPONENT's first NONCREATURE spell each turn, and the passive goldfish opponent never casts a spell (the opponent seat has no hand/library/action path) -- the Voice of Resurgence / Dragonlord Kolaghan precedent. Our own spells never trigger it (it says 'an opponent'). The 'unless that player pays {X}' choice and X = this creature's power (so Field Marshal / lord pumps would raise X) belong to that never-firing trigger and are equally dead. Nothing to model; disclosed. Would become live only if an opponent that casts spells is ever modelled. Covered by the analysis-soldiers.md standing decision (opponent-facing triggers inert); listed as PROVISIONAL pending explicit user sign-off.
- coverage deferral -- Aether Vial: AI heuristic: adds counters each upkeep until reaching the most common creature MV in hand; activates to deploy matching creatures for free each main phase.
- coverage deferral -- Coppercoat Vanguard: The +1/+0 is a plain subtype lord: template lord_effect, subtypes_affected ["Human"
- coverage deferral -- Coppercoat Vanguard: PARTIAL: the WARD {1} grant is not modelled -- Keyword::Ward is an inert tag no engine code reads, and the grant would need a granted-keyword path that does not exist for ward. WHY inert, provably: ward triggers ONLY on a spell or ability an OPPONENT controls targeting the Human (CR 702.21a); our own targeting never triggers it, and the passive goldfish opponent never casts spells or activates abilities, so it has no reachable application. Not self-binding: ward taxes the opponent, never us. PROVISIONAL pending user sign-off.
- coverage deferral -- Recruitment Officer: Activated dig = PermAbilityMode::ActivatedDig (activated_dig_cost {3}{W}, activated_dig_count 4, activated_dig_types [Creature
- coverage deferral -- Jirina, Dauntless General: BODY: a 2/2 LEGENDARY Human Soldier for {W}{B} -- legend rule via the existing SBA (2 copies in the list); being a Human it fires Champion of the Parish / Thalia's Lieutenant enter watchers and counts for Rick/Greymond's four-Humans and Kudro/Coppercoat anthems; being a Soldier it gets Field Marshal and counts toward Harbin's five-Soldier attack. The {B} pip is real: payable by Silent Clearing ({T}, pay 1 life: W or B), Cavern of Souls / Unclaimed Territory / Secluded Courtyard (any colour for a creature spell; Jirina is both Human and Soldier so it matches either chosen type), or bypassed by Aether Vial on 2. CLAUSE 1 (ETB 'exile target player's graveyard'): [PARTIAL, PROVISIONAL pending user sign-off: no params -- the trigger's resolution is a no-op. WHY INERT: the only two legal targets are the two players. The opponent's graveyard is ALWAYS empty here (it never casts or mills, and nothing in this 60 kills an opponent creature -- Brutal Cathar exiles, Kudro's destroy is not enumerated), so the dominant target resolves to exiling nothing, i.e. the no-op IS the faithful resolution of the correct target. Targeting OURSELVES is weakly dominated: no card in this 60 (no wish -> no sideboard access) reads our graveyard (no flashback/escape/delirium/threshold/recursion), so it changes no outcome. Re-examine if a graveyard reader ever joins the list.
- coverage deferral -- Jirina, Dauntless General: PARTIAL, PROVISIONAL pending user sign-off: the GRANT is not modelled. WHY INERT: hexproof only stops OPPONENT targeting (the passive opponent never targets); indestructible only stops destruction/lethal-damage SBA, and nothing in a goldfish destroys or damages our Humans (the opponent never blocks/casts; our own only destroy effect, Kudro's 'destroy target creature with power 4 or greater', is not enumerated). The 'target player' choice is therefore not surfaced in the viewer -- a disclosed inert-collapse, PROVISIONAL with the clause.
- coverage deferral -- Harbin, Vanguard Aviator: Cost/type/P-T pasted from Scryfall (WU, Legendary Creature -- Human Soldier, 3/2). Castable in this list off Fortified Beachhead ({W} or {U}) and off Unclaimed Territory / Secluded Courtyard / Cavern of Souls any-colour mana when their chosen type is Human OR Soldier (Harbin is both); Aether Vial on 2 puts it in. A creature spell, so Thalia's noncreature tax never applies. Recruiter of the Guard (toughness <= 2) and Recruitment Officer (MV <= 3) can both find it. THE TRIGGER: attack_with_n_threshold 5 / attack_with_n_subtype Soldier / attack_with_n_team_pump_power 1 / attack_with_n_team_pump_tough 1, applied by ApplyAttackThresholdTeamPump at declare-attackers in BOTH worlds (GameEngine::CombatPhase + TurnSolver::SimulateCombat -- ONE shared helper, lockstep by construction) and projected by PendingAttackDamage via CountAttackThresholdTeamPump so the search prices crossing the threshold. Harbin need NOT attack and need NOT be untapped or free of summoning sickness: the source is any Harbin you control on the battlefield as attackers are declared ('whenever YOU attack', not 'whenever this creature attacks'), so a Harbin cast or Vialed in pre-combat main enables the pump THIS turn -- which is why the param is an attack-helping Main1 term in ClassifyMainPhase and DeckFeedsCombat. The count is over DECLARED attackers only (CR 508.1 / 508.4): a creature put onto the battlefield attacking was never declared and does not count, so the count is taken BEFORE FireAttackCreateTokens widens the list (the Beastmaster Ascension / Inferno Titan position), while the PUMP lands AFTER the token block so such tokens are pumped (CR 603.3b ordering, tokens-first strictly dominant vs a never-blocking opponent -- the ApplyBattleCry precedent). A creature counts if it has the Soldier subtype or AnimatedAllTypes() (changeling-style animated land), the Kragma Warcaller matcher. Recipients: EVERY creature you control at resolution (Bogbeast's Craterhoof loop), not attackers only; temp bonuses, so 'until end of turn' is the CR 514.2 cleanup. Toughness half applied faithfully though inert in effect (the opponent deals no damage). Legendary: the engine's EnforceLegendRule keeps one copy, so at most one trigger per combat in practice; the helper still loops per source so a hypothetical copy effect would be counted faithfully. [PARTIAL (PROVISIONAL, awaiting user sign-off): the 'and gain flying until end of turn' grant is NOT modelled -- provably inert: evasion only matters against blockers and the passive goldfish opponent never blocks (no creatures, no block model); the engine has no temporary keyword-grant field and Combat reads no Flying. Verbatim the accepted Blossoming Bogbeast trample / Craterhoof Behemoth collapse. Harbin's own printed Flying is carried in keywords for fidelity and is inert for the same reason.
- coverage deferral -- General Kudro of Drannith: LORD MODELLED: lord_effect subtypes_affected Human +1/+1 with lord_excludes_self -- the Knight Exemplar / Valiant Knight shape, applied in ComputeLordBonus on every combat/eval/SBA/viewer call site; 'you control' = our battlefield only. It is a Human Soldier itself, so it triggers Champion of the Parish / Thalia's Lieutenant watchers and is buffed by OTHER Human/Soldier lords (Field Marshal, Coppercoat Vanguard, King Darien); King Darien's 1/1 Soldier TOKENS are not Human and get nothing from Kudro. LEGENDARY: 2 copies in the list -- the second copy is a legend-rule duplicate (CR 704.5j, existing SBA). COLOUR: {B} is payable by Cavern of Souls / Unclaimed Territory / Secluded Courtyard (colored_creature_only -- Kudro is a creature spell, so allowed) and Silent Clearing (W/B horizon land, pay 1 life), or bypassed by Aether Vial at 3 counters. [PARTIAL, PROVISIONAL pending user sign-off: (1) the enters trigger 'exile target card from an opponent's graveyard' is NOT modelled -- the passive opponent never casts, discards, mills or loses a creature, so its graveyard is always empty (the same collapse disclosed on Deathrite Shaman and Scavenging Ooze); with no legal target the trigger is removed from the stack (CR 603.3d) and does nothing. (2) The activated ability '{2}, Sacrifice two Humans: Destroy target creature with power 4 or greater' is NOT enumerated -- its legal targets are a scheduled opponent spawn body with power >= 4 (GoldFishRunner::PopulateOpponentSpawns; spawns never attack or block, so destroying one changes no outcome) or one of OUR OWN power>=4 creatures; every activation sacrifices two of our Humans and pays {2}, and nothing in the 60 has a dies/sacrifice/leaves-the-battlefield payoff, so every such line is strictly dominated (fewer attackers, less mana, no compensation) and can never shorten the goldfish clock.
- coverage deferral -- Cathar Commando: Body modelled exactly: a 3/1 Human Soldier for {1}{W} -- both subtypes are load-bearing (Champion of the Parish / Thalia's Lieutenant / General Kudro count Humans; Field Marshal pumps Soldiers; Cavern of Souls naming either type pays for it; Aether Vial on 2 puts it in; Recruiter of the Guard can fetch it, toughness 1). It is a CREATURE spell, so Thalia's noncreature tax never applies to it. PARTIAL, PROVISIONAL (awaiting user sign-off): Flash is inert -- the engine opens no priority window outside our own main phase (the Ice-Fang Coatl precedent, whose Flash deferral the USER approved 2026-10-04 for THAT card), and in a goldfish an end-of-opponent's-turn cast is weakly dominated by a main-phase cast: the mana is the same untapped pool, the creature can attack on the same next turn either way, no information is gained (our next draw is still unseen at the opponent's end step), the opponent never acts so there is nothing to dodge, and a main-phase cast fires Champion/Lieutenant/Kudro enter-triggers SOONER and shrinks the hand before cleanup. The keyword is still listed (TurnSolver's timing_ok honours it for casts with a non-empty stack). PARTIAL, PROVISIONAL (awaiting user sign-off): the activated ability '{1}, Sacrifice this creature: Destroy target artifact or enchantment' is NOT modelled. WHY inert -- the passive opponent never casts, so it controls no artifact or enchantment ever (its library holds Sol Ring, but nothing is ever put onto its battlefield except creature spawns); the opponent's only permanents are the PopulateOpponentSpawns creature bodies, never an artifact or enchantment, so the ONLY legal targets are our own Aether Vial and Esper Sentinel (artifact creature), and every activation is strictly dominated by declining: it spends {1}, loses a 3-power attacker, and destroys a second own permanent, while this deck has no death/sacrifice payoff that could repay it. Optional activation, so declining is always legal (the Deconstruction Hammer precedent -- that card's deferral was USER-approved 2026-10-04; this one is not yet). Viewer cost, disclosed: a human cannot choose this strictly-dominated activation.
- coverage deferral -- Brutal Cathar: TRANSFORMING DFC, FRONT FACE (Brutal Cathar // Moonrage Brute); listed under the front name as in every zone but the battlefield (CR 712.8). DAY/NIGHT (CR 726 + 702.145) is a NEW game designation, GameState::day_night {neither, day, night}, armed only for a deck that runs a daybound/nightbound card (deck stamp). (1) First appearance: if it is neither day nor night when a daybound permanent is on the battlefield, it becomes day. (2) If it is NIGHT as this enters (cast OR put -- Aether Vial), it enters with its BACK face up as Moonrage Brute (ruling: it does NOT enter as Brutal Cathar and then transform), so the enter trigger does not fire and it never enters as a Human/Soldier: Champion of the Parish / Thalia's Lieutenant / Kudro enter-watchers do not see a Human. The face is set at the TOP of the enter cascade, BEFORE the watchers and own-ETB triggers. (3) Turn-based check before untap (CR 726.3a), applied at BOTH turn boundaries in lockstep (executor end-of-turn/UntapStep + rollout SimulateEndAndStartNextTurn): the passive opponent's notional turn falls between ours (the opponentdeck::EndOfTurnDraw site). At the opponent's turn start: day && we cast 0 spells -> night; night && we cast >= 2 spells -> day. At our turn start: day && the opponent cast 0 spells (always, in a goldfish) -> night. So from the turn after it first lands, it is ALWAYS night on our turns and this permanent is Moonrage Brute (3/3 red Werewolf, not a Human or Soldier: it leaves every Human/Soldier lord and count -- Field Marshal, Kudro, Coppercoat Vanguard, Rick's four-Humans clause, Harbin's five Soldiers). A Vial put is not a cast and does not count toward spells. (4) Transforming is not entering and not leaving: counters (e.g. Lieutenant's), m_number, tapped state, summoning-sickness status and the linked exile all persist; no enter watcher fires. (5) The ETB / transforms-into-Brutal-Cathar trigger: exile target creature an opponent controls (spawned opponent creatures, 8 of 10 game indices) until this permanent leaves; with no opponent creature the trigger has no target and is removed. It fires on the cast turn (day) and again at the opponent's notional turn start whenever night -> day (we cast >= 2 spells). Target pick = the shared Chupacabra convention (largest opponent creature by power -- a resolution default with ~0 payoff, PROVISIONAL pending user sign-off under the no-greedy rule), human board-click via the `target` (loyalty-shape) chooser, which fires on both the enter and the transform route. Payoff is ~0 (spawns never attack or block) -- implemented faithfully, carries no eval credit. The exiled creature returns under its owner's control after this permanent (either face) leaves the battlefield -- implemented as an orphan sweep at the day/night turn boundary in both worlds (ReturnOrphanedLinkedExiles), i.e. at the next turn boundary rather than the instant it leaves: disclosed, unobservable (opponent creatures never act) and UNREACHABLE in this 60 (no card here can make it leave: Kudro's sac-two-Humans is not enumerated, it is not legendary, nothing bounces or destroys it). A token would cease to exist (ruling) -- spawns are not tokens. (6) Leaving the battlefield as Moonrage Brute should put BRUTAL CATHAR into the graveyard/hand (CR 712.8); the engine's ~30 graveyard-push sites keep the face name -- disclosed, unreachable here for the reason in (5). PARTIAL: the back face's FIRST STRIKE is parsed but inert (Keyword::FirstStrike has no readers; the passive opponent never blocks) and its WARD -- PAY 3 LIFE is inert (the opponent never targets) -- the established Dwalin / Kitesail Larcenist disclosures.
- coverage deferral -- King Darien XLVIII: Clause 1 (anthem): Benalish Marshal's exact shape -- lord_effect + affects_all_creatures + power/tough_bonus 1 + lord_excludes_self; tokens are creatures you control and get it too. Clause 2: the Slimefoot PayToken mode (pay_token_cost, NO {T}, NO sacrifice -> repeatable within a turn and legal the turn Darien lands, CR 302.6; activation count K is the searched axis, bounded only by mana) creating one 1/1 white Soldier token (subtype load-bearing: Field Marshal's 'Other Soldiers' lord reads it), plus the NEW pay_token_self_counters 1 rider: each activation also puts one +1/+1 counter on Darien via the PutPlusCounters chokepoint (counter first, then token, per oracle order -- unobservable difference). The {G} is payable in this mono-W shell ONLY by Secluded Courtyard (colored_creature_ability_ok: 'activate an ability of a creature source'); Cavern of Souls and Unclaimed Territory are cast-only and yield {C} here, Fortified Beachhead (W/U), Silent Clearing (W/B) and Plains make no green. The perm-ability payment therefore runs under CreatureAbilityPayScope when its source is a creature. Clause 3: the SACRIFICE is real and modelled as a self-only sac outlet (sac_creature_outlet + sac_outlet_self_only, no payload -- the Ranger-Captain of Eos shape): the activation's whole modelled effect is Darien leaving (anthem gone) and the death event via OnCreatureDies. The granted hexproof + indestructible on tokens is INERT in a goldfish -- the passive opponent never targets, destroys, blocks or deals damage to our creatures -- so it is not modelled [PARTIAL: token hexproof/indestructible grant not modelled; WHY inert -- no opponent removal, combat damage to our creatures, or targeting exists in a goldfish; PROVISIONAL pending user sign-off
- coverage deferral -- Unclaimed Territory: Modelled as producing all five colours plus {C}. colored_creature_only enforces the restriction in the payer (ProducesForPayment / TapFlowInfeasible / the backtracking payer): for a noncreature spell, or any activated ability, this land yields only {C}. So it cannot pay Lightning Bolt's {R} or Siren Stormtamer's {U}, but it pays generic costs such as Aether Vial {1} and a Clue's {2}. The ETB creature-type choice is simplified to 'any creature spell'. That is outcome-exact for Pirates: every creature except Metallic Mimic {2} and Adaptive Automaton {3} is a Pirate, and those two cost only generic mana, which the land's unrestricted {C} pays identically. Search-side only: with MTG_CCO_NONCREATURE_POOL off (default) the noncreature enumeration pool counts this land as any-colour, so a Bolt plan relying on it can be offered and is then refused by the payer (no illegal play).
- coverage deferral -- Unclaimed Territory: Soldiers (2026-10-04): EXACT there too -- every creature in that list is BOTH a Human and a Soldier (Scryfall type lines verified), so naming either type pays for every creature it casts; King Darien XLVIII's {G}/{1}{G}{W} and Harbin's {U} are castable ONLY through these lands (Harbin also off Fortified Beachhead).
- coverage deferral -- Secluded Courtyard: Modelled as producing all five colors; the ETB type choice is simplified to "any creature" (exact for the mono-tribal decks that run it). colored_creature_only restricts the coloured mana to creature spells; colored_creature_ability_ok additionally permits it for an ACTIVATED ABILITY whose source is a creature (D12 fix: Burning-Fist / Sethron activations, paid under the CreatureAbilityPayScope guard). Ability sources are simplified to battlefield creatures -- the oracle's any-zone "source" (e.g. cycling a creature card) is not modelled.
- coverage deferral -- Secluded Courtyard: Soldiers (2026-10-04): EXACT there too -- every creature in that list is BOTH a Human and a Soldier (Scryfall type lines verified), so naming either type pays for every creature it casts; King Darien XLVIII's {G}/{1}{G}{W} and Harbin's {U} are castable ONLY through these lands (Harbin also off Fortified Beachhead).
- coverage deferral -- Secluded Courtyard: Soldiers: its coloured mana also pays the abilities of Recruitment Officer ({3}{W}) and King Darien XLVIII ({3}{G}{W} -- its ONLY green source for that ability): every ActivatePermAbility whose source is a creature now pays under CreatureAbilityPayScope at both apply sites, as ActivatePump always did.
- coverage deferral -- Fortified Beachhead: All three clauses modelled. ETB: etb_untap_reveal_subtypes (Soldier) (reveal from hand, the Frostboil Snarl/Game Trail path) OR etb_untap_control_subtypes (Soldier) (a permanent you control with that subtype, read off the LIVE permanent, so a transformed Moonrage Brute is not a Soldier and Soldier tokens are), both evaluated in the shared LandWouldEnterTapped predicate so enumeration pricing and the real drop agree; controlling a Soldier short-circuits to untapped before the provider's reveal hook (no choice exists then). Mana: produces W/U. Pump: team_pump_cost {5} + team_pump_taps_source (the land itself is tapped as part of the cost, so it cannot also pay; one activation per untapped Beachhead per turn) on the combat firebreathing converter (ApplyFirebreathing), team_pump_power 1 to every attacking Soldier. HOW MANY to activate is the combat converter's existing count (FirebreatheActivations, provider-owned, recorded COMPLIANT in docs/design/no-greedy-in-search-window.md) and the human's `firebreathe` decision. The +1 toughness (team_pump_tough 1 -> temp_tough_bonus) is applied for fidelity but is inert in a goldfish (no blockers, no damage to our creatures); +1/+1 to non-attacking Soldiers is likewise inert (no post-combat reader of their power).
- coverage deferral -- Cavern of Souls: Modelled as producing {C} plus all five colours, the colours restricted to CREATURE spells (colored_creature_only, enforced in the payer); the ETB creature-type choice is simplified to 'any creature' -- exact for a tribal list whose creatures share the named type (Soldiers 2026-10-04: every creature there is a Human Soldier). The uncounterable clause is not modelled -- inert: the passive opponent never counters. [Bracket corrected 2026-10-04: it used to say the colour restriction was not modelled, which has been false since colored_creature_only.
- coverage deferral -- Ranger-Captain of Eos: ETB SINGLE-tutor to HAND (Ranger of Eos's params minus etb_tutor_hand_count, which selects the multi path at >1): tutor_to_hand + tutor_types Creature + tutor_max_mv 1 + tutor_shuffle_after. Legal pool in this deck is exactly Soul Warden / Soul's Attendant / Serra Ascendant (every other creature is MV>=2); WHICH one is a searched main_phase plan variant (tutor axis, width 6 >= 3 candidates, so all three are scored; plan-less paths take the first library match, disclosed). 'You may' is ALWAYS taken when a legal target exists -- declining is legal but never right with three castable {W} bodies; the only theoretical cost is the post-search shuffle, disclosed (same treatment as Ranger of Eos). The fetched card lands in HAND and IS castable in the same phase (2026-09-08): the creature-ETB tutor arms the same deferred acquisition re-solve (MTG_ACQ_RESOLVE) a tutor SPELL does, in the rollout as well as the depth-0 executor (only the executor half existed before, so at every searched depth the fetch waited a turn: 0 of 7 spare-mana fetches cast same-turn -> 6 of 7). Guarded by test/scenarios/critter_ranger_captain_fetch_same_turn.json. Clause 2: the effect ('opponents can't cast noncreature spells') is INERT -- the passive goldfish opponent never casts a spell of any kind -- but the SACRIFICE is real and is modelled: sac_creature_outlet + sac_outlet_self_only (the cost is 'Sacrifice this creature', so the source is the ONLY legal victim), with no payload params, so the activation's whole effect is the death event routed through OnCreatureDies. This is the deck's only way to kill its own creature, which matters solely because Daxos, Blessed by the Sun turns it into a lifegain event (-> Pridemate / Voice / Archangel of Thune / Heliod counters). Emitted to the search only when such a payoff is live (SelfSacHasDeathPayoff); with none on board the sac is strictly dominated (loses a 3/3, changes nothing else) so omitting it is lossless. Human play always sees it. A Ranger-Captain cast THIS turn with Daxos out can also sac in the phase it landed: breakpoint site 9 (post-entry activation, TurnSolver::PostEntryActivationPending) re-decides the phase after a permanent with a live, affordable activation enters. Sac timing: the engine plays first main only, so the search sacs pre-combat and forgoes that turn's 3 damage -- a small disclosed under-rating (the Goblins DeferSacOutletPreCombat gap).
- coverage deferral -- Ranger-Captain of Eos: Soldiers (2026-10-04): there the MV<=1 pool is Champion of the Parish / Esper Sentinel / Recruitment Officer; no card in that 60 pays off a death, so the sacrifice is strictly dominated and not emitted to the search (human play still sees it).
- coverage deferral -- Silent Clearing: Exact sibling of Horizon Canopy / Fiery Islet (Horizon land cycle), same two params, nothing deferred. Clause 1: produces (W,B) + tap_self_damage 1 -- the land has NO {C} mode, so every tap (coloured or generic) costs 1 life (PainForTap: has_c_mode false). The life payment is a COST (CR 119.4/602.2), modelled through the shared pain path; on a dmg_events-armed board it is recorded via ArmedManaTap like every pain land -- no damage-prevention or lifegain card in this deck makes the cost-vs-damage distinction observable. Clause 2: sacrifice_draw_cost {1} -- pay {1}, tap and sacrifice the land, draw one (the land cannot tap for mana toward its own activation: {T} is part of the cost). Dig is offered by the GENERIC dig gate (GenericProvider::HasAnyDigSource/ShouldConsiderDig, MTG_GENERIC_DIG default on): VialProvider adds no override, so the deck digs only in the user-ruled 'nothing castable, >=2 lands left after the sac' case, and DigDecisionSearched stays false (clairvoyant-dig objection, USER 2026-09-29). Black matters only for the {B} pips of General Kudro of Drannith and Jirina, Dauntless General; both are Human Soldier creatures, so Unclaimed Territory / Cavern of Souls / Secluded Courtyard can pay them too.
- coverage deferral -- Recruiter of the Guard: ETB tutor to HAND on the Goblin Matron / Stoneforge machinery (tutor_to_hand + tutor_types + tutor_shuffle_after), narrowed by the NEW tutor_max_toughness filter -- PRINTED toughness off the CardDefinition (no continuous effect applies to a card in a library). WHICH creature: on a cast, the searched tutor_target plan axis; on a Birthing Pod / Chord PUT, the provider's front pick with the human tutor_etb chooser override (-1 declines the optional search). 'Reveal it' unobservable (nothing reads reveals).
- card_costs SKIPPED (--no-network) -- Scryfall cost/cmc reality-diff not run
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
- STALE allowlist entry -- Glorybringer [keywords] no longer mismatches; remove from scryfall_divergences.json
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.56); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.20); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.27); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.
{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.71); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.
{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.
{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.
You may choose not to untap this land during your untap step.
At the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.
{T}: Put a storage counter on this land.
{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)
This land enters tapped.
When this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.
{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.
Sacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.41); scryfall='First strike (This creature deals combat damage before creatures without first strike.)
Other Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.
{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.06); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.
{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins
Other Knights you control get +2/+2 and have protection from Assassins.
When Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance
Adeline's power is equal to the number of creatures you control.
Whenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)
When
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)
{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying
Rot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.
Whenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.
Target
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.

- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.43); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.55); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.
Whenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.11); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.
Draw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.58); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.
Ferocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample
Spells you cast cost {1} less to cast for each target.
Spells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —
• Tap X target permanents.
• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying
{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.
Splice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste
Other creatures you control have haste.
Whenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste
When Karrthus enters, gain control of all Dragons, then untap all Dragons.
Other Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying
Whenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying
Whenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.
Storm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.
If this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.
Whenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.60); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample.
When this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.58); scryfall='Enchant creature with another Aura attached to it
Enchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample.
Whenever enchanted creature deals damage, yo
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)
Umbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature
Enchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature
Enchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature
Enchanted creature gets +1/+0 and has flying.
{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)
Whenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)
Enchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)
Other Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.
Goblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.
{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.
{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.
Whenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.08); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.40); scryfall='First strike
When this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach
When this creature enters, it deals 2 damage to any target.
Channel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.
{T}: Add {C}.
{2}, {T}: Choose a color. Add an amount of 
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.
When this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.
Whenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.
Whenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.
{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.28); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.47); scryfall='Flying, first strike, trample, lifelink, haste
At the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste
Whenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything
If Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance
This creature gets +1/+1 for each color among permanents you control.
{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)
{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.24); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)
Equip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.
−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.
−2: Gain control of target creature.
−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")
+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.23); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying
{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.46); scryfall="Choose five. You may choose the same mode more than once.
• Target permanent phases out.
• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying
Whenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.
Draw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.
Draw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.
Choose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.43); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.
Equip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.
Equip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.
{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.
Whenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.
Equipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.
Equip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.
Equip {3} ({3}: Attach to target creature you control. Equip 
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)
Equip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.
Equip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)
Equip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.
Equip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.
Equipped creature gets +1/+0 and has haste.
Equip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.
Equip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.
Jump — During your turn, Cid has flying.
{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.
Whenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike
Whenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone 
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.
Remove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.49); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.
Metalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.37); scryfall='First strike
Balan has double strike as long as two or more Equipment are attached to it.
{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying
Whenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.
{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.12); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.39); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch
When this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample
When this creature enters, if your life total is less than 7, your life total becomes 7.
As long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste
When this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample
When this creature dies, create three 5/5 green Wurm creature tokens with trample.
When Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample
Whenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.
Search your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.24); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.
{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.17); scryfall='First strike
{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.
{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.
Whenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.35); scryfall='First strike
Other Minotaurs you control have first strike.
As long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample
Other Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.
{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.28); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.
{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample
When Atsushi dies, choose one —
• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.
Flying, haste
{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.
Whenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.
Creature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.
{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste
You may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.
{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.
{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.
Whenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.
{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.
{T}: Add {G} or {W}.
{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.
{T}: Add {C}.
{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)
{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.

- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying
When this creature enters, untap up to two lands.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying
When this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land
Enchanted land has shroud. (It can't be the target of spells or abilities.)
Whenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.22); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.06); scryfall="This spell can't be countered.
{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)
{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)
Flash
Flying
{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).
{T}: Add {C}.
{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.
Cascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample
Cascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)
Shuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.
{T}: Add {R}.
{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {U}.
{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.
{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.
Creatures you control can't have -1/-1 counters put on them.
Creatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.
Persist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.
Persist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.
Sacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch
When this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)
{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying
When this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.
Whenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {W}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {B}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {R}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{1}, {T}: Add one mana of any color.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.
Cycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.
Cycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.
Cycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and 
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
As this artifact enters, choose a color.
{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample
Abominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)
When this artifact enters, draw a card.
{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash
Flying
When this creature enters, draw a card.
This creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.
At the beginning of your 
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying
Rimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying
{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.12); scryfall='Protection from black and from red
Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.13); scryfall="First strike, lifelink
Ascend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
As long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.
As long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying
Lifelink (Damage dealt by this creature also causes you to gain that much life.)
Whenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible
As long as your devotion to white is less than five, Heliod isn't a creature.
Whenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.13); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.

- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.21); scryfall="When this creature enters, it fights up to one target creature you don't control.
Enrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)
I — The next red or 
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample
When Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.13); scryfall='Flying
First strike (This creature deals combat damage before creatures without first strike.)
Lifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.11); scryfall='Whenever an Angel you control enters, you gain 4 life.
Whenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying
Whenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.13); scryfall='When this land enters, you gain 1 life.
Whenever an Angel you control enters, you gain 1 life.
{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying
At the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying
When this creature enters, you may reveal a card you own named Legion Angel from outside the game and 
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.
−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.13); scryfall='Flying, vigilance
Each other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample
Whenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.
{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."
At the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.
As long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender
At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters 
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.
Remove three spore counters 
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance
When this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying
Whenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that 
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample
This creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.
Whenever this creature enters or attacks, it deals 3 damage 
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —
• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.18); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste
Whenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying
{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}
When this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.
{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.
{T}: Add {C}.
{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
If you would gain life, you 
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.
{3}{B}{B}: Creatures you control gain lifelink 
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.
{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.
{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying
If noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.
{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)
Search your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
Sacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
When this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.
Eternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
When this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.
Ferocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.04); scryfall='{T}: Add one mana of any color.
{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.
{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.08); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.13); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike
Noncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.
Whenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.
Sacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until 
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.
Whenever General Kudro or another Human you control enters, exile target 
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash
{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.25); scryfall='First strike
Ward—Pay 3 life.
Nightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.
{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.
{1}, {T}, Sacrifice this land: Draw a card.'
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep SKIPPED -- no '## Claude-play sweep' section in docs/design/analysis-soldiers.md. Run the Claude-driven sweep (.claude/skills/claude-play.md, analyze-deck 5d; fan game-indices out with the Workflow engine), verify any flags against cards.json + the rules skill, then record `commit:` / `seeds:` / `games:` / `flags: N unresolved` under that heading. play_invariants (above) already guards the protocol mechanically.

<!-- verify_deck:end -->

## Value leaf -- generated, ADOPTED 2026-10-08

USER 2026-10-08: do the value leaf first ("it fits the profile to be fast under the leaf, potentially
with trust"), then the mulligan profile.

**Generation.** `bash scripts/valueleaf.sh run decks/Soldiers` at `35a7c3ac` (src tree `bf680c55`), on
the PGO+LTO engine, 14:17-15:23Z. Phase A 2,500 games -> 10,937 rows; GBDT held-out RMSE 0.37 turns
(the observed range elsewhere is 0.45-0.55). Phase C matrix (400 games/cell, H1-5 x V1-8) clean through
the C.5 risk gate. Phase D: crossover `1->1 2->1 3->1 4->2 5->6 6->6 7->6 8->6`; V4 not trusted (its
95% upper bound 0.00271 > tol 0.0020), trust candidate d5.

**Phase E (held-out, 8 seeds x 1000 games, d5/b20 play):**

| arm | avg | delta | paired t | seeds better/worse/tied | core-s |
|---|---|---|---|---|---|
| no leaf (incumbent play) | 4.37250 | -- | -- | -- | 12,464 |
| **value leaf** | 4.37137 | **-0.00113** | -2.83 | 5/0/3 | 1,658 (**0.13x**) |

* **Trust d5 ACCEPTED:** ON-OFF +0.00000 (one-sided 95% upper +0.00062 <= tol 0.0020), 8/8 seeds
  byte-identical, 0.88x the cost. Promoted into `value_trust_depth`.
* Depth sweep d4 / d5 / d6 vs the shipped default: all four arms 4.37800 on 2,000 paired games (d5/d6
  byte-identical; d4 differs in digest on 1 of 4 seeds at the same score). Depth beyond 4 does not
  move this deck at b20 -- it mostly wins on T4. No play-depth change proposed.
* Adopted as staged (no `value_play` play block: play stays the built-in d5/b20 with the leaf live).

**Phase F (run with the leaf LIVE, so the setting is measured under it):** `mull_gen_depth=1`,
`mull_gen_budget_ms=3` (rank fidelity 0.9934 >= 0.990 floor vs the d5/b20 play reference, 0.49x the
play cost; d2/b3 0.9965 at 0.68x, d5/b20 = 1.0). **K = 19** recorded in `value_play.expected_buckets`
-- the first recorded value. **USER 2026-10-08: "Let's leave Soldiers at 19 for now. I'll probably want
to change it in a modified finalized list."** (Phase F refuses on a first leaf until the sidecar is live,
which is why adoption came first.)

**Suite (all three tiers, `--deck=soldiers,soldiers2hg`, PGO+LTO):** 0 slower games anywhere.

| tier | cases changed | faster | slower | same score, line differs |
|---|---|---|---|---|
| smoke | 1/4 | 1 | 0 | 0 |
| regression | 2/5 | 2 | 0 | 4 |
| overnight | 8/12 | 6 | 0 | 26 |

d0 cases unchanged (no search, no leaf). Every same-score change spot-checked reads "kept hand + draws
IDENTICAL -> a clean like-for-like LINE change" at the same win turn -- the new evaluator choosing an
equal line. Viewer references: 556, 0 play-drift / 0 board-diverged / 0 enum-gap / 0 mull-drift.
Accepted; `check_gt_logs.py` 659 consistent.

## Mulligan profile (keep table) -- generated, validated, ADOPTED 2026-10-08

**Generation.** Scout (`--gen-mulligan recommend`, 16:20-16:38Z, 739 rollouts/s): K=19, 260,417 size-7
cells; projected `complete` ~11.4 h (upper bound) vs `fast` ~5.7 h -> `fast` (R=30), the recipe that fits
the 8 h window. `scripts/mullgen.sh run decks/Soldiers fast` on PGO+LTO at `60db0d8c`, rollouts d1/b3
from the leaf's `value_play` (phase F, measured under the live leaf), the scout's r=0 chunk carried
(757,782 cell-sides). Generation 16:38-20:29Z (~700 rollouts/s); artifact check OK (K=19, 260,417
entries, `bottoming_enabled=true`, 236,948 sub-cells).

**Validation (`mullgen.sh`, play profile, early stop on):**

| A/B | delta | seeds | paired t |
|---|---|---|---|
| keep: exhaustive vs static (bottoming held identical) | **-0.1768 t** | 4/4 better (stopped early at 4 pairs) | -14.7 |
| bottoming: blind exhaustive vs lookahead (confounded) | **-0.0197 t** | 11/11 better (stopped early at 11 pairs) | -9.1 |

**Suite (all three tiers, `--deck=soldiers,soldiers2hg`, on the committed `.gz` artifacts):** every
case faster -- 21/21.

| tier | cases | mean delta | changed games | different mulligan count | different bottom | bottom ORDER only | unexplained |
|---|---|---|---|---|---|---|---|
| smoke (soldiers) | 3 | -0.0916 | 674 / 1,225 | 560 | 109 | 5 | 0 |
| smoke (soldiers2hg) | 1 | -0.0400 | 30 / 50 | 28 | 2 | 0 | 0 |
| regression | 5 | -0.1307 | 794 / 1,450 | 679 | 110 | 5 | 0 |
| overnight | 12 | -0.1350 | 7,671 / 14,000 | 6,578 | 1,055 | 38 | 0 |

Per-game attribution by `test/keep_adoption_attribution.py` (table off vs on, the run's own `mtg.run`):
the off replay reproduces the committed GT and the on replay reproduces the run on every case, and every
changed game -- including every slower one -- is explained by a different kept hand or bottom, i.e. the
new keep policy, not play. The tiers also ran ~10x faster: with a keep table, mulligan decisions no longer
fall through to lookahead-bottoming rollouts. Viewer references 556: 0 drift. Uncompressed profile/raw
and the journal backups are kept in `logs/durable/soldiers_profile_adopted_2026-10-08/`.

**DEFERRED (USER 2026-10-08) -- a bucket ruling for the lands, after the deck is screened further.**
Discovery merged five lands into ONE 18-card bucket: Plains x4, Fortified Beachhead x4, Unclaimed
Territory x4, Secluded Courtyard x4, Cavern of Souls x2 (Silent Clearing stayed apart). USER: *"That merge
is kind of suspect, particularly Fortified Beachhead and Plains"*; *"The main problem is that Plains and
Fortified cannot tap for B or G and Plains can't tap for U"*; *"I would consider making that 2 buckets if
we do a modified version. Plains + Fortified and the rest in the second."*; then *"Let's stick with this
for this version and perhaps fix it when we have screened the deck more."* and *"I think the manabase is
good enough that it mitigates any issue with getting the colours we need. Hence the risk of the merge is
notably lessened."* -- ten of the eighteen merged lands (and both Silent Clearings, for {B}) cast every
off-colour card, and Aether Vial puts any of them in with no mana at all.
* The off-colour pips are all creature spells: {B} General Kudro of Drannith x2 + Jirina, Dauntless
  General x1, {G} King Darien XLVIII x1, {U} Harbin, Vanguard Aviator x2 -- six cards. The three
  creature-type lands pay all of them; Fortified Beachhead (W/U) pays only Harbin; Plains pays none. Plains
  and Beachhead also have UNRESTRICTED {W} (e.g. Recruitment Officer's activated ability, which Cavern's /
  Unclaimed Territory's coloured mana cannot pay), and Beachhead enters tapped without a Soldier.
* Why discovery cannot see it: it clusters on mean |delta win-turn| per probe at whole-turn granularity
  (0.01 = at most 4 of 400 probes; `src/analyzer/BucketPolicy.h`), and these differences only bite in hands
  holding one of the six cards and lacking another source.
* How to apply it when the time comes: `decks/Soldiers/soldiers.buckets.json` with `merge` groups
  [Plains, Fortified Beachhead] and [Cavern of Souls, Unclaimed Territory, Secluded Courtyard] plus six
  two-card `keep_apart` pairs across them (a single keep_apart group would also split each merge group),
  `value_play.expected_buckets` 19 -> 20, then `mullgen.sh run decks/Soldiers fast` as a regeneration
  (it A/Bs new vs incumbent and keeps the incumbent unless the new table is not worse).
* Cost: K=20 -> 378,892 size-7 cells (1.46x; ~5.5-6 h `fast` at tonight's rate). Splitting Plains off
  too (K=21) -> 538,380 cells (2.07x, ~8 h).

## Stage 4/5 status + OPEN USER QUESTIONS (2026-10-04; none blocked the run -- default taken for each)
Status: Stage 4 + 5a/5b/5c/5c2/5e/5f/5g/5h/5i + 4-bis DONE; 5d claude-play sweep RUN (see
"## Claude-play sweep"; flags resolved 2026-10-05). verify_deck after this run: the only
blocking reds are `claude_sweep` (5d un-run) and `card_costs` (Scryfall HTTP 429 -- a did-not-run;
re-run when the limit clears); `viewer_wiring:choose_abilities` FIXED (DECISIONS.md row's type cell
held prose, so the registry parser could not read it).

Questions (Stage 2's list above still stands; these are new):
A. **Adopt `MTG_SOLDIERS_ORDER`?** (cast-order-rankings.md "Soldiers"). Default taken: OFF. Measured
   -0.003 t/game at play settings, 0 slower games at d3/d5, and it repairs the generic order's hard
   prune (~3/1000 games unreachable at d8 b0). Thalia-after-Vial is part of it.
B. **Discard buckets** (`docs/design/soldiers-discard-policy-proposal.md`): approve/amend? Default
   taken: ON (the brief's contract). Measured bound = 0, so this is doctrine, not a measurable win.
C. **Fleet: re-run `--discard-analysis`** for Pirates / CritterLifegain / BreachingDragonstorm /
   WhiteKnights / KittyEquipment? Their DISCARD_INERT verdicts came from the broken census (fixed
   9503b7c4). Not done here (out of scope).
D. **Pre-existing reference failures** at bb5bcceb (not this branch): Hinata2 `claude_s1_gi0` ENUM-GAP,
   Mirrorwing v2 `claude_s51_gi50` play-drift -- they make every regression-tier run print
   REGRESSION DETECTED. Owner?
E. **3x cost gate at 2.98x** from a filtered run; a pooled `--measure-all` would likely read far lower
   (see 5j note). Accept the filtered verdict as-is? Default taken: yes (it PASSes).
F. **Vial-order twin cost (claude-play flag #1).** The fix is an expressibility repair (a legal line no
   budget reached), but it adds candidates: Soldiers d5/b20 units +1.6-3.1%, and two regression-tier
   games went T4->T5 at their case budget (soldiers d5 s3003 gi72, minotaur d3 s2002 gi295), both
   recovering at 4x budget (churn); 0 of 360 play-setting Soldiers games moved. Default taken: ship it,
   GT for soldiers/minotaur/pirates NOT re-accepted (the branch's GT is already stale on ~50 unrelated
   keys -- whoever rebaselines the tier owns the per-difference verdicts). Want the churn minimised
   further (e.g. a cheaper gate) before accepting?
- (later) The discard hatch reader was renamed `SoldiersBucketDiscardEnabled()` so the pending
  `discard_policy` gate (main checkout's WIP verify_deck: `DISCARD_FLAG_RE` matches
  `_BUCKET_DISCARD` / `BucketDiscardEnabled()` in the method BODY) recognises the authored policy;
  checked with that script's own `_discard_impl_body` against this tree -> flag found, own override.
