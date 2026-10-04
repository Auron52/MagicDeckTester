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
now raises). **Fleet note:** the Pirates, CritterLifegain, BreachingDragonstorm and WhiteKnights
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
