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
| Rick, Steadfast Leader | 2 + new viewer type | Secret Lair printed name of Greymond; `scryfall_name` honoured by audit_card_costs/audit_card_fields. NEW lord_min_controlled_matching (ComputeLordBonus, continuous), etb_choose_keyword_count/_menu + keyword_grant_subtypes, Permanent::chosen_keyword_mask (padding; sim/dominance/fold-census folds), CreatureHasLifelink/CreatureHasVigilance readers + BoardSources lifelink/vigilance lists, provider hook DecisionProvider::EtbChosenKeywords, viewer `choose_abilities` (all four sites) | first-strike grant has no reader -- PROVISIONAL; keyword-pair = provider one-option choice -- NEEDS USER SIGN-OFF |
| Recruitment Officer | 3 | NEW PermAbilityMode::ActivatedDig (no {T}, appended last in all ModeSpec tables), PerformLookTakeDig (PerformEtbDig's body, MV filter), searched pick on the etbdig axis at FULL width, lossless whiff drop (autonomous), site-10 route clause + CardHasPostEntryActivation, K never > 1 (each pick searched; 2nd activation via the put-in-hand re-solve) | instant-speed timing collapsed to main phase (dominated) -- disclosed; "you may" always taken autonomously (weakly dominant) -- disclosed |
| Jirina, Dauntless General | 1 | 2/2 legendary; self-only sac outlet (Ranger-Captain shape) | ETB graveyard exile (opponent gy always empty; ours unread; target-player not surfaced) and the hexproof/indestructible grant -- PROVISIONAL |
| Harbin, Vanguard Aviator | 2 | NEW attack_with_n_* : ApplyAttackThresholdTeamPump (both combat worlds, declared attackers counted before the token block) + CountAttackThresholdTeamPump in PendingAttackDamage; ClassifyMainPhase / DeckFeedsCombat Main1 terms; deck stamp deck_has_attack_threshold | flying grant + own Flying inert -- PROVISIONAL |
| General Kudro of Drannith | 1 | lord_effect Human +1/+1, excludes self | graveyard-exile ETB (no legal target) and `{2}, sac two Humans: destroy power>=4` (dominated; targets = spawns or own) -- PROVISIONAL |
| Cathar Commando | 1 | vanilla 3/1 + Flash keyword | Flash timing (Ice-Fang Coatl precedent) and `{1}, sac: destroy artifact/enchantment` (Deconstruction Hammer precedent) -- PROVISIONAL (the cited 2026-10-04 approvals exist in git -- a038ded3, 81732983 -- but are for THOSE cards, not this one) |
| Brutal Cathar // Moonrage Brute | 3 | Day/night: GameState::day_night (padding; folded nonzero in sim key + dominance), ApplyDayboundOnEnter at the top of FireEtbWatchers/FireOwnEtbTriggers (night entry = back face, no ETB, never a Human), SetDayNight (in-place face swap, transform-into-front trigger), DayNightTurnBoundary at the shared end-of-turn site in BOTH worlds (CR 726.3a); ExileOppCreatureUntilLeaves (loyalty-shape `target` in human play); linked exile parked in GameState::exile, returned by an orphan sweep at the turn boundary; back face = separate cards.json entry (Kaldring precedent) + MdfcBackFaceNames for the viewer image; `day_night` in the decision JSON + `--scenario` key | exile target default = largest opponent creature (payoff ~0) -- NEEDS USER SIGN-OFF; return deferred to the turn boundary and zone-change face name not restored -- disclosed, UNREACHABLE in this 60; back-face first strike + ward inert -- PROVISIONAL |
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
   (largest opponent creature, payoff ~0); Beachhead's pump count rides the existing FirebreatheActivations
   default (already recorded COMPLIANT).
3. **Provisional deferrals** listed in the table (Field Marshal x2, Coppercoat, Esper Sentinel, Thalia
   scope note, Rick first strike, Jirina x2, Harbin, Kudro x2, Cathar Commando x2, Brutal Cathar x3,
   King Darien) -- approve or reject each.
4. **Slimefoot (Fungus):** pay_token_cost is not in CardHasPostEntryActivation, so a Slimefoot cast this
   turn gets an un-fanned site-9 continuation. King Darien is keyed on his counter rider to leave Fungus
   untouched; should Slimefoot be marked too (Fungus-affecting)?
5. **Silent Clearing dig:** generic dig gate (VialProvider inherits it). Opt into a searched dig later?
6. **Recruitment Officer:** pick axis = every legal hit (no width cap); one activation per action
   (picks 2..K never a ranked default). Taken as defaults.
7. **Viewer cosmetic:** Scryfall image lookup by the printed name "Rick, Steadfast Leader" 404s, so the
   viewer shows no art for Rick (data/audits use scryfall_name). Add an image alias?
8. **Day-night display:** the decision JSON carries `day_night`; the GUI does not render it yet.

