# Analysis ledger — Vampires

Deck: `decks/Vampires/Vampire.cod` (the user's file name; the folder is `Vampires`). B/R/W Vampire tribal on
Aether Vial: 4 Falkenrath Pit Fighter, 4 Falkenrath Gorger, 4 Legion Lieutenant, 4 Stromkirk Captain,
3 Stromkirk Condemned, 2 Captivating Vampire, 2 Bloodghast, 4 Markov Baron, 2 Asylum Visitor, 3 Vampire of
the Dire Moon, 1 Vampire Socialite, 1 Mavren Fein, 1 Drana, 2 Bloodline Keeper, 4 Aether Vial; lands
4 Voldaren Estate, 4 Unclaimed Territory, 4 Secluded Courtyard, 2 Blackcleave Cliffs, 6 Swamp (20).
Sideboard (Anje Falkenrath, 2 Anje's Ravager, Worldfire, Mountain) is unreachable (no wish) and unscanned.

Analysis started 2026-10-18 in worktree `.claude/worktrees/vampires`, branch `vampires-analysis` (from
origin `phase-1-2-deck-analyzer` 2176032dd).

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells, never blocks. Model every clause faithfully; a goldfish-inert
  deferral stays PROVISIONAL until the user signs off.
- No greedy pick inside the search window (user hard rule). Every new decision (which card to discard,
  madness cast-or-not, Bloodghast return, which Vampire to sacrifice, Blood/Keeper activations) is SEARCHED;
  a provider may only order/restrict.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present, never
  adopt unilaterally.
- One heavy job (build or batch) at a time.

## Stage 1 — coverage (2026-10-18)
Missing (16): Falkenrath Pit Fighter, Falkenrath Gorger, Legion Lieutenant, Stromkirk Captain, Stromkirk
Condemned, Captivating Vampire, Bloodghast, Voldaren Estate, Swamp, Markov Baron, Asylum Visitor, Vampire of
the Dire Moon, Vampire Socialite, Mavren Fein, Drana, Bloodline Keeper.
Present (4): Unclaimed Territory, Secluded Courtyard (Pirates/Soldiers), Blackcleave Cliffs, Aether Vial.

## Stage 2 — research clusters (fan-out, 2026-10-18)
- A1 madness: Falkenrath Gorger, Markov Baron (convoke, lifelink, lord), Asylum Visitor (upkeep draw).
- A2 discard/sacrifice outlets + recursion: Falkenrath Pit Fighter, Stromkirk Condemned, Voldaren Estate
  (Vampire-only mana, Blood token), Bloodghast.
- B lords: Legion Lieutenant, Stromkirk Captain, Captivating Vampire, Bloodline Keeper // Lord of Lineage,
  Vampire of the Dire Moon, Swamp.
- C combat/ETB triggers: Drana (first-strike-step combat-damage counters), Mavren Fein, Vampire Socialite.
Design files (committed): `docs/design/vampires/design_{A1_madness,A2_outlets,B_lords,C_combat}.md` —
verbatim Scryfall fields, clause tables, full cards.json entries, C++ plan with file:line, search design,
viewer wiring, unit-test plans. Summary of what they established:
- No madness, no Blood token, no Swamp, no first-strike damage step in the engine today. Combat damage is
  one summed step (`ResolveCombatDamage`, shared by executor and rollout); nothing reads first strike.
- The goldfish opponent HAS creatures in 8 of 10 game slots, so Captivating Vampire's steal is LIVE
  (implemented, not deferred).
- Every non-cleanup discard in the engine today takes the provider's first pick (not searched). Vampires
  gets a searched discard pin (one branch per distinct name) and a searched sacrifice victim.
- Secluded Courtyard cannot pay a sac outlet's coloured cost today (the sac-outlet payment skips
  `CreatureAbilityPayScope`) — a mis-model; fixed generally.
- Routing hazards: Markov Baron's `convoke` (Melira Pod signature), Aether Vial (Vial), a sac outlet
  (goblin). The deck gets `VampiresProvider`, routed above all three.

### Integration plan (serial where coupled)
- Base commit: deck file, ledger, designs, Tier-1 cards Swamp / Legion Lieutenant / Vampire of the Dire Moon.
- Track C (own worktree `.claude/worktrees/vampires-c`, parallel): two-step first-strike combat, Stromkirk
  Captain (lord + `grants_first_strike`), Drana, Mavren Fein, Vampire Socialite, second-main detection.
- Track A (this worktree): A1 + A2 — shared discard hook + madness, outlets, Voldaren Estate / Blood,
  Bloodghast, Gorger, Baron (convoke generalised), Asylum Visitor, VampiresProvider + routing.
- Then Track B (this worktree, after A — shares the activation pass and the pinned Action size):
  Captivating Vampire (lord + steal), Bloodline Keeper // Lord of Lineage (token + transform),
  Unclaimed Territory / Secluded Courtyard note lines.
- Then C is rebased in, smoke/regression byte-identity checked, Stage 3 coverage re-run.

## Status (orchestrator, 2026-10-18)
- Stage 2 integrated in order A2 -> C (cherry-picked from `vampires-combat`; cards.json merged per card, no
  card edited on both sides) -> A1 -> B. Unit 1514/1514. Backup: origin `backup/2026-10-18/vampires-analysis`.
- Stage 3: coverage clean (20/20 full, 0 missing); every remaining bracket note is an implementation note or
  a PROVISIONAL goldfish-inert collapse listed below. Cost audit rc 0 (450/450, B integrator). Field audit:
  all hard fields match; its rc 1 is the pre-existing unfetched Temple of Abandon (not this deck).
- Stage 4: baseline profile `decks/Vampires/Vampire.profile.json` (static keep; no keep table / value leaf).
  Cost diagnostic NO_COST_INTERACTIONS. Discard analysis DISCARD_UNLABELLED: 0 real cleanup sheds in 400 d3
  games, 64,621 rollout sheds (all with < 4 lands out, each taking ranking index 0) -> BOUND it with the
  MTG_SHED_WORST pair (Stage 5i).
- Stage 4a: VampiresProvider (derives VialProvider; empty of judgement hooks except the AGENT-DRAFTED orders,
  `MTG_VAMP_DRAFT_ORDERS`) routed at the top of the chain; `provider_audit.py --check` rc 0.
- Stage 4-bis: rows in all three tiers (colossus shape, overnight ENTRY set, vampires2hg in each tier).
  Smoke accepted (46b709eeb) and regression accepted (ff401f887), both FILTERED accepts over FULL-tier runs in
  which every other deck was byte-identical; critter/angels moved with the two-step combat (smoke: 0 slower,
  19 searched + 65 d0 faster; regression: 0 slower, 45 searched + 79 d0 faster). Their OVERNIGHT GT is left
  to the periodic overnight run (CLAUDE.md).
- Reference gate (regression mode): ONE play-drift, CritterLifegain/claude_s6_gi5 (recorded T5, replays T4).
  BISECTED: origin 2176032dd ok (T5); combat track alone T4; combined T4. Mechanism: two Ocelot Prides'
  first-strike lifelink reach 30+ life before regular damage, so Serra Ascendant connects as a 6/6. Reference
  untouched -- open question 27.

## Status update (orchestrator, 2026-10-18 late)
- Claude-play sweep 1 (20 seeds, `docs/design/vampires/claude_sweep_1.md`): 20/20 tie the search; 12 defects
  A-L, all fixed on `vampires-fix` (see "Sweep fixes" above) and cherry-picked here, with the AI-drafted cast
  order + discard buckets from `vampires-order` (DEFAULT OFF; `docs/design/vampires-cast-order-2026-10-18.md`,
  which now answers questions 11-12's "agent drafts" half -- the USER review is pending). Unit 1553/1553.
- The smoke (46b709eeb) and regression (ff401f887) Vampires GT were accepted while defect A (phantom convoke
  mana) was live -> re-accepted after the fixes; the overnight rows were never run on the defective binary.
- The payer fix (F + L: MTG_PAY_PAIN_LAST, MTG_PAY_NARROW_RESTRICTED_FIRST, default ON) moves payments in other
  decks (smoke: 153 non-Vampires games, all digest-only at searched depths; d0: 4 faster, 2 slower soldiers).
  A payment-ORDER heuristic change across decks earns adoption by a held-out A/B on the decks it moves:
  `test/tools/payer_pain_ab.py` (8 decks, both levers on vs =0, two held-out blocks, one pooled batch).

## Status update (orchestrator, 2026-10-18 night)
- Payer A/B (test/tools/payer_pain_ab.py; 8 decks x 2 held-out blocks, one pooled batch): FIRST run failed the
  bar (vampires +1 -- the levers skipped a T1 land on s13480130 gi9 -- and creature_giving +1). Root-caused and
  fixed on `vampires-payer2` (merged: Pit Fighter's main-2 activation holds an ability-legal {R}; a sac-land
  cast's victim pays first). RE-RUN: every deck net <= 0 (vampires -1, creature_giving -3, minotaur -2,
  pirates -2, th -4, auras / selesnya / soldiers 0), ZERO slower searched games; three d0 slower games are the
  d0 runner's later choice after a better payment (th gi1155 decks itself -- question 38, minotaur gi83,
  soldiers gi1554).
- GT re-accepted on the merged tree: smoke 6c5442e74 and regression 5b4b21c98 (filtered accepts over FULL
  runs; every move attributed in the commit messages: searched slower = 2 vampires games, gi76 an old
  phantom-mana line verified on the pre-fix binary, gi79 budget churn that recovers at d3/d5/d8 b0; d0 slower =
  vampires phantom-mana / d0-greedy classes and pirates gi628 payer churn). Overnight vampires rows: running
  (MTG_PGO=0, byte-identical to PGO).
- Remaining: overnight accept; perf track (slow games, `vampires-perf`); leaf tie-break check (5c2); shed
  bound (5i); verify_deck full (mismatch harnesses, viewer sweep, play_invariants); confirmation claude-play
  sweep (incl. a game-index with opponent creatures for Captivating's steal); cast-order / bucket A/B.

## Status update (orchestrator, 2026-10-18 late night)
- Overnight vampires + vampires2hg ENTRY rows accepted (bcbd21481, filtered, 16 new keys; d0 4.70-4.73, d3
  4.41-4.43, d5 4.40-4.43, 2HG d5 4.93-5.05). Left as accepted through the later play changes below: the periodic
  overnight re-accepts (CLAUDE.md).
- Stage 5f search cost (`docs/design/vampires-search-cost.md`; perf commits cherry-picked as f95a154d6 / 40e4951f2 /
  1feccc32b / 2ed53cf74 = the doc's 6161e387c / 46f82d9e5 / ee3dbd8f6 / 1b34af3eb): three folds, default ON --
  `MTG_VAMP_M2_PUMP_DISCARD_FOLD` (dominance, subset rule 9), `MTG_VAMP_MADNESS_COLOR_FOLD` (identity),
  `MTG_VAMP_OUTLET_NOT_GROWTH` (sound). The slowest single-player games cost 2.9-3.7x fewer units (gi131 d5
  4.86M -> 1.44M); held-out 400 games and a d5 b100 sample byte-identical in win turn. 99.7% of a slow game's units
  are mulligan-bottoming trial games, which the keep table removes. Questions 39-42.
- GT re-accepted on the perf-merged tree (89eddb1f6, filtered over FULL runs; every other deck byte-identical):
  smoke searched 0 slower / 1 faster, d0 0 slower; regression searched 0 slower / 3 faster, d0 1 slower (gi265
  4->5, the d0 greedy's extra T3 pump; the perf doc).
- Claude-play confirmation sweep 2 (`docs/design/vampires/claude_sweep_2.md`): 8 Opus players, seeds 92000-92007 at
  game-index 2..9 (opponent creatures, so Captivating Vampire's steal ran its full cycle in three games): 8/8 tie
  the search; every sweep-1 defect class re-checked clean; ONE new defect, M.
- Defect M (fixed, 4493e2158 = vampires-fixm 4a36fa9cd): Voldaren Estate's Blood cost variants counted only the
  plan's Vampire CASTS, so a Bloodline Keeper token tapped earlier in the same plan never discounted the Blood --
  a completeness hole within one main phase at any budget (a pre-combat Blood affordable only at the true cost was
  unfindable; mostly masked by the main-2 Blood reaching the same end-of-turn board). Rule 3 now counts the
  arrivals that run BEFORE the Blood in the shared trailing order (TurnSolver::TrailingRunsBefore, the same keys as
  OrderTrailingActivations); madness casts / steals that run after it are correctly not counted. 0 of 110 paired
  Vampires games moved; KittyEquipment / Colossus byte-identical. Question 43.
- Static checks: verify_deck --no-sweep GATE PASS; whole-repo audit_card_costs 450/450.

## Stage 5 measurements + the 3x COST GATE (orchestrator, 2026-10-19)
One pooled batch on 89eddb1f6 (117,000 games; `build/scratch/vampires/stage5/combined.py`, report `report.txt`):
- **5c2 horizon tie-break: NO SIGN** -- 0 win turns changed of 12,000 paired games at play settings (d5 b20); the
  lever FIRES (play diverged in 3 of 12 blocks), it never binds a win turn (the deck wins T4-5, inside the horizon).
  The default (ON) costs nothing. The script's 48-block re-run was not run (question 44).
- **5i discard-ranking bound:** rollout cleanup best-vs-worst (`MTG_SHED_WORST`) d3 -1 / 3,000 (WORST faster by
  one), d5 0 / 1,500: the ranking is worth nothing at play settings. Non-cleanup discards: `MTG_NONCLEANUP_SHED_WORST`
  inert (0 digests) BY DESIGN -- every Vampires outlet discard (Condemned, Pit Fighter, Blood crack) is a searched
  pin per distinct name, so no ranking decides one.
- **Cast order / buckets A/B** (`docs/design/vampires-cast-order-2026-10-18.md` section 5): the drafted order is
  win-turn NEUTRAL (0 of 16,000 paired games moved); the drafted buckets net -14 (d0) / -2 (d3) / 0 (d5) / -2 / -3
  (2HG d3 / d5), 0 slower searched games, 16 slower d0 games (being root-caused). USER-owned: both stay OFF.

**COST GATE FAILS: `suite_gate.py --cost decks/Vampires` = 7.90x the reference (Galadriel), limit 3x.** Read with
three corrections, none of which changes the verdict:
- That run was CONTENDED (an agent build alongside): its worst row, vampires2hg d5 s2002, cost 23.4 s/game; the
  full regression run on the same binary had it at 10.6 s/game (std d5 3.1-4.6 s, d3 2.1-2.6 s) -- still ~3.6x the
  cached reference. Vampires is 25.8% of the regression tier's core time (18.0% of smoke).
- The cached REFERENCE is stale: `test/suite_cost.json` holds Galadriel at 2,969 ms/game, measured before its keep
  table; the same regression run had Galadriel at 60-94 ms/game. Refreshed, the reference would be the next deck
  with both artifacts (Snow, 1,319 ms/game cached), and Vampires would be ~8x. (Every deck's gate reads that stale
  number; a `--measure-all` refill from one pooled full-tier run fixes it.)
- **The excess is the pre-keep-table mulligan bottoming** (CLAUDE.md: "before the keep table every mulligan game
  plays out each candidate bottom as a full game"). Measured, `MTG_BOTTOM_ROLLOUTS=1` vs `=0`, units/game
  (deterministic): 2HG d5 s2002 248,662 -> 78,570 (3.2x), std d5 s3003 122,925 -> 30,197 (4.1x), d3 s3003 71,502
  -> 17,365 (4.1x); without them the worst row is ~2.1 s/game, ~1.6x Snow -- INSIDE 3x. A keep table replaces exactly
  those rollouts.
The rows stay on this branch (they carry the GT every check above used); nothing is landed. Whether Vampires enters
the suite over 3x so the mulligan profile can be generated (which brings it into range), or stays out until a perf
route gets it there, is the USER's call (question 45; `MTG_ALLOW_UNTESTED_DECK` is a USER override).

## Stage 6a disclosure defects D1-D3 (fixed 2026-10-19, branch `vampires-fixn`, cherry-picked 3cd49a776 / 1b1d2d0e9 / a537d4e88)
A read-only code audit compiling the Stage 6a disclosure found three in-window defects; each was reproduced on the
pre-fix engine (every new test failed there), then fixed:
- **D1 (completeness):** madness convoke arms were priced on the PRE-PLAN board and pinned by INDEX, so bodies the
  plan adds first (a Keeper token, a same-plan cast) were invisible and an index could resolve as a different arm or
  a decline. A madness convoke cast is now pinned by its COVER (pips / generic its bodies pay), realised at the
  trigger by the cheapest bodies that pay it there; `MadnessFanFor` offers every cover the trigger's board can pay;
  new subset rule 10 (`SubsetMadnessCoverInvalid`) checks each plan's trigger in executed order (exact partition).
- **D2 (completeness):** the madness path never applied convoke contention, so Q16's free-body dominance dropped the
  land-paid arm even when a later steal needed the untapped Vampires; mirrors the hand-cast exception now.
- **D3 (fold soundness):** rule 9's hand-size exemption was off by one (`h > 7` -> `h + 1 > 7`, the twin's hand), and
  a land-after clone counted the held land (hid an emptied hand under Asylum Visitor).
- Aether Vial's stale bracket note now describes `WantVialCharge`. Melira Pod / Knights byte-identical; Vampires
  60 d3 + 30 d5 paired games: 0 moved. Units 1578/1578.

## Open questions for the user (work continues on the stated default)
1. **Main-2 held-land bar vs Bloodghast** (USER 2026-09-30 "Only newly arrived lands in second main").
   "Pit Fighter sacs Bloodghast post-combat, then play the held land" is unfindable at any budget under the
   bar (main 1 cannot express it: Pit Fighter needs the opponent to have lost life). [default: narrow
   exemption when a graveyard-landfall card is or can be put in our graveyard; env-flagged; PROVISIONAL]
2. **First-strike two-step damage applies to EVERY deck** (a rules-correctness fix: Angels' Lyra lifelink
   feeding Thune / Valkyrie before regular damage; Soldiers' Rick may move). [default: everywhere, GT moves
   explained per game before accept]
3. **Searched discards / sac victims for OTHER decks** (Burning-Fist, Neheb, Troyan, Siege-Gang, Pashalik,
   Psychotrope take the provider's first pick today — an in-window greedy). [default: Vampires only now]
4. **Madness at cleanup**: one-option "cast if payable" (dominance argument in design_A1 §3) vs searched.
   [default: one-option rule, PROVISIONAL] -- LANDED (A1) as DecisionProvider::MadnessCastDefault (base rule;
   the dominance argument is on the hook's declaration); the human is still asked through `madness`.
5. **Convoke tap order** for the attackers it taps: lowest power first vs a provider order keeping Drana /
   Mavren untapped. [default: lowest power first] -- LANDED (A1): CreatureConvokeArms adds COSTLY bodies
   lowest power first; every k is a searched arm, so only WHICH k bodies is the order's choice.
6. **Own-death checks on for Vampires** (Asylum Visitor's mandatory loss, Voldaren Estate's pay-1-life).
   [default: on] -- LANDED (A1): GoldFishRunner arms own_death_live on hellbent_upkeep_life_loss or
   tap_blood_cost (both Vampires-only params; no other deck arms).
7. **Bloodghast "may return" = always yes** in autonomous play (the human is still asked). [PROVISIONAL]
8. **Goldfish-inert collapses** (PROVISIONAL until signed off): Bloodghast can't block; flying (Drana,
   Keeper, tokens); Socialite's menace; Dire Moon's deathtouch; instant-speed activations folded into the
   main phases (+ the in-combat transform window); no priority window between the two damage steps (D-C4,
   ~<=1% of games); simultaneous-entry Socialite counter (D-C3); Captivating never targets our own
   creatures; stolen spawns carry no Auras/Equipment; Lord of Lineage reads mana value 0 on the battlefield.
9. **Stromkirk Captain main-1 vs main-2 on a Drana turn** (all Vampires strike first with Captain out, so
   Drana's counters add no damage that combat) — main split is USER-owned. [default: both mains searched]
10. **Secluded Courtyard paying sac-outlet coloured costs** — mis-model fix; may move other decks that
    run Courtyard + a sac outlet. [default: fix, explain moves] -- LANDED (A2, `MTG_SAC_OUTLET_PAY_SCOPE`
    default ON): smoke play-changed=0; Minotaur's and Soldiers' outlets have no coloured pip, so nothing in
    the suite moves today.
11. **VampiresProvider orders** (discard / victim / activation drafts, design_A2 §8) and, later, the
    Vampires cast order and discard buckets — AGENT-DRAFTED, USER reviews. [landed as ordering hooks,
    `MTG_VAMP_DRAFT_ORDERS` default ON; never narrowing]
12. **VampiresProvider inherits slivers_vial's CLEANUP-discard buckets** from VialProvider
    (`MTG_SLIVERS_BUCKET_DISCARD`) -- the rule the deck rode before it had a provider (Aether Vial routed it to
    Vial). Discard buckets are USER-owned. [default: inherited unchanged (play-neutral vs the old routing);
    the Vampires buckets are yours to set]
13. **Searched discard pins grow `sizeof(Action)` 472 -> 480** for every deck (one InternedName; no padding
    hole holds a pointer). Byte-identical play; a small copy-cost increase everywhere. [default: keep]
14. **Madness emission gate is OPTIMISTIC** (A1): an outlet's cast variants are gated on the turn's optimistic
    mana (OptimisticTurnMana), not on an exact payability probe of the pre-plan board, because the plan may
    play a land / add mana before its trailing pass (an exact pre-plan probe would make "land, then Condemned
    discarding Baron, cast it" unfindable at any budget). Cost: a cast variant that cannot pay at the trigger
    resolves as the decline (a duplicate, never phantom mana). [default: optimistic -- the completeness rule]
15. **Madness / creature-convoke in ROLLOUT LEAVES** (A1): a rollout leaf (estimating, beyond the horizon)
    emits only the provider's front madness variant and convoke arm 0 -- the phyrexian precedent; the searched
    collect and the executor keep every variant. [default: on; PROVISIONAL, the permitted playout policy]
16. **Creature convoke drops the no-convoke cast when a free body helps** (A1; Chord's weak dominance: a free
    body has no other use vs the passive opponent and the land mana it saves is at least as flexible).
    [default: on, PROVISIONAL]
17. **Chord of Calling keeps the LEGACY convoke rules** (A1): the creature-spell terms (every body free after
    combat; a live non-mana {T} body is costly) apply to creature spells only, so Melira Pod is byte-identical.
    Adopting them for Chord is a play change (main-2 arm 0 widens). [default: legacy for Chord]
18. **VampiresProvider declines Bloodghast's madness while the land drop is open** (MadnessCastDefault, draft,
    MTG_VAMP_DRAFT_ORDERS): it orders the fan (decline first) and decides beyond the horizon only. [default: on]
19. **Asylum Visitor's loss to 0 at upkeep** is detected at the post-main-1 own-death check in both worlds (no
    SBA checkpoint between upkeep and main 1 exists; nothing in this list gains life in main 1).
    [default: disclose; PROVISIONAL]

## Track A -- cluster A2 integrated (2026-10-18, integrator; branch `vampires-analysis`, NOT pushed)
Cards (cards.json, verbatim Scryfall costs): Falkenrath Pit Fighter, Stromkirk Condemned, Voldaren Estate,
Blood Token (named token def), Bloodghast. Everything in design_A2 is implemented except where noted below.

**Engine (shared resolvers, both worlds):**
- `DiscardCardFromHand` (SpellEffects.h) -- THE discard hook (A1's name), with `[MADNESS SEAM]` markers; today
  only the new outlets call it (every pre-existing discard site stays open-coded until A1 migrates it).
- Falkenrath Pit Fighter: its own apply `ApplyDiscardSacOutlet` + the one legality predicate `SacOutletLive`
  (restriction + pinned discard + baked victim, checked BEFORE any cost in both trailing passes, by the
  enumerator and by the site-9 key). Cost order = mana -> discard -> sacrifice -> [madness seam] -> draw.
- Stromkirk Condemned: `ApplyActivatePump` mode 4 + `DiscardTeamPumpLive`; `Permanent::once_per_turn_used`
  (reset at our untap in both worlds; BuildSimKey bit 1<<19, PermIsPlainForFoldImpl, FungibilityKey,
  dominance::Build, LandfallTargetFoldKey, RemovalSelfTargetFoldKey, the payment parity check).
- Voldaren Estate: Tier-1 mana (Unclaimed Territory + painland reading); `PermAbilityMode::TapBlood` with
  `TapBloodCostNow` (paid at activation in both worlds); the Blood token `PermAbilityMode::BloodDraw`
  (discard pin via `ActivationDiscardPinScope`, consumed once).
- Bloodghast: graveyard landfall in `FireLandfallTriggers` (snapshot at entry; `revive` chooser in human
  play; autonomous YES -- PROVISIONAL dominance); `Permanent::cond_haste` (derived; `RefreshOppLifeHaste` at
  both begin-combat checkpoints + entry); `GyR_GyLandfallNames` folded into BuildSimKey / dominance;
  `deck_has_landfall` + `deck_has_cond_haste` stamps.
- Secluded Courtyard sac-outlet pay scope (design 0.4): both trailing passes pay every creature-source
  sac outlet under `CreatureAbilityPayScope` (`MTG_SAC_OUTLET_PAY_SCOPE`, default ON, =0 = old payment).
- `DeckUsesSecondMain`: `activate_only_if_opp_lost_life`.
- Sizes: `sizeof(Permanent)` stays 304 (the two new flags + Kili's flag are 1-bit fields in one byte);
  `sizeof(GameState)` stays 1136 (the stamp sits in padding); `sizeof(Action)` 472 -> 480 (one
  `InternedName discard_name`; `int8_t vamp_casts_needed` in padding). No Action size pin exists.

**Search (all branched; providers order only):**
- `Action::discard_name` = the searched discard pin (one variant per distinct non-staged hand name), on Pit
  Fighter / Condemned / Blood crack; folded into plan_signature, ActionFoldSig, BpCandFingerprint,
  PlanDomKey, the action diff printer (all gated on non-empty -> other decks' keys unchanged).
- Pit Fighter victims: one per identity class (LandfallTargetFoldKey) on the pre-plan board + one per
  Vampire creature NAME in hand (a same-plan victim: `victim_name` + `needs_cast_mask` bit1). Source pooled
  (first copy emits; `ActivationFamilyKey` group -> one activation per plan; more through the draw's
  hand-entry breakpoint). Human play: ONE unpinned entry (+ a payability probe), asked at resolution.
- Condemned: per-copy family (`ActivationFamilyKey`), one variant per discard name.
- Voldaren: cost variants k = 0..kmax (Vampires the plan adds), exact-k + a top "k or more" variant.
- `SubsetVampOutletInvalid` (both walkers): over-pinned discards, a same-plan victim without its cast, the
  wrong Blood cost variant, a non-canonical identical-Condemned assignment (odometer positions only).
- Site-9 keys: kActSacOutlet uses `SacOutletLive`; kActTeamPump keys mode 4; TapBlood / BloodDraw appended
  LAST to all four ModeSpec tables (TapBlood priced at `TapBloodCostNow`).
- Land: `TurnSolver::PutsGyLandfallFeeder` adds the -1 ("land after the activations") clone for a plan
  whose activation discards / sacrifices a Bloodghast; the main-2 held-land exemption
  (`GyLandfallHeldLandExempt`, `MTG_VAMP_GY_LANDFALL_HELD_LAND` default ON -- USER QUESTION 1).
- `VampiresProvider : VialProvider` (Stage 4a), routed at the TOP of `DetectDecisionProvider` by
  Vampires-only params from four cards; AGENT-DRAFTED orders (`MTG_VAMP_DRAFT_ORDERS`, default ON):
  ActivationDiscardCandidates, SacOutletVictimCandidates, ActivationOrderRank (Blood -> Condemned -> crack
  -> Pit Fighter). New base hooks `DecisionProvider::ActivationDiscardCandidates` /
  `SacOutletVictimCandidates` (defaults: the cleanup ranking / SacExpendabilityRank, no narrowing).
- Viewer: `discard` / `sacrifice` / `revive` reused (DECISIONS.md rows + a "Vampires A2 outlets" section);
  plan JSON `makes_blood`; menu labels for mode 4 / Pit Fighter / Blood crack; auditor MANIFEST / INERT /
  BOARD_ACTIVATIONS rows (`audit_viewer_decisions.py --no-sweep`: clean).

**Deviations from design_A2 (and why):**
1. Cost order is discard -> sacrifice (A1 §2.5 / the integration brief), not sacrifice -> discard: the madness
   card is in exile while the victim is chosen, so it still can never be the victim.
2. Same-plan Pit Fighter victims are emitted at ENUMERATION (named, guarded) instead of the post-dedup
   `AppendSubdecisionAxes` re-point: no hand-index reconstruction, and the subset guard makes a missing cast
   a rejected duplicate. Same reachable set.
3. Condemned: per-copy emission + a canonical-assignment subset rule instead of a k-count multiset action
   (same canonical set, no new Action fields).
4. Blood cost variants keep EXACTLY one variant per subset (exact k, top variant "k or more") -- a ">= k"
   guard alone would enumerate each subset once per k it satisfies.
5. The "unpinned autonomous apply in the window" invariant is a counter (`g_activation_discard_unpinned`,
   asserted 0 by the lockstep unit test), not an abort -- there is no debug build to abort in.
6. Blood-crack pooling = the first untapped Blood (identical untapped Bloods have no distinguishing state).

**PROVISIONAL (user sign-off owed):** Bloodghast "can't block" inert; instant-speed timing of Pit Fighter /
Condemned / Blood (and Blood creation) collapsed to the main phases' trailing passes; Bloodghast's "may"
= YES autonomously; Voldaren "Vampire spell" == "creature spell" (exact for this list); the
main-2 held-land exemption (open question 1); the VampiresProvider draft orders (open question 11).

**Verification (A2, 2026-10-18):** `mtg-test` 1464/1464 cases pass (new `test_vampires_outlets.cpp`: card
data, Voldaren mana contexts incl. Courtyard under the creature-ability scope, Blood cost / creation /
crack, Pit Fighter liveness / apply / self-sac / enumeration folds / same-plan victims, Condemned
once-per-turn + fold keys + identical-copy symmetry, Bloodghast return / revive decline / conditional
haste, the -1 land after a Bloodghast discard, the main-2 held-land exemption, a rollout-vs-executor
lockstep sweep over every main-2 activation plan with `g_activation_discard_unpinned == 0`; the routing
table pins Vampires -> Vampires and survives cutting any one A2 card). Smoke: 128/128 PASS, searched and
d0 `play-changed=0` -- byte-identical for every suite deck, including the two other decks that pair a
Secluded Courtyard with a sac outlet (Minotaur: Slaughter-Priest's {2} is generic, which Courtyard's {C}
already paid; Soldiers: Ranger-Captain / King Darien / Jirina outlets cost no mana) -- so the pay-scope
fix moves nothing in the suite today. `provider_audit.py --check` rc 0 (Vampires has no profile yet:
"NOT AUDITED"; the unit test pins its routing). `audit_card_costs.py` rc 0 (441/441 compared).
`audit_card_fields.py`: every hard field (cost, P/T, types, keywords) of the new cards matches the
snapshot (Voldaren Estate / Bloodghast entries built from the stored Scryfall JSON after a 429; the
audit's rc 1 is the pre-existing unfetched Temple of Abandon). `audit_viewer_decisions.py
decks/Vampires/Vampire.cod --no-sweep`: clean. Sanity
`mtg decks/Vampires/Vampire.cod --games 20 --seed 777 --depth 3 --budget-ms 20 --threads 8`: no crash, avg
6.55 (4 unwon -- 9 of the 20 cards still unimplemented); one SLOW-GAME (seed 790 gi13, 66 s in the batch,
2:42 single-threaded, 4.9M units) -- a flat profile (BuildSimKey 5.9%, operator new 4.3%, CollectActions
2.6%), i.e. broad search over dead hands, not one hot new path. Watch it at the 3x suite-cost gate.
UN-RUN: a hand-built `--claude-play --choices` walk to a Pit Fighter / Condemned / Blood activation (not
cheap to steer; the decision surfaces are the existing `discard` / `sacrifice` / `revive` paths, covered
by the unit tests' revive-decline case) -- owed to Stage 5h.

**Seams for the madness integrator (cluster A1, next):**
- `DiscardCardFromHand(state, controller, hand_idx, cause)` (SpellEffects.h) is the hook; add the
  `std::vector<MadnessPending>* mq` parameter and the `MadnessCostsFor` exile there. Its three callers are
  exactly the `[MADNESS SEAM]` markers: `ApplyDiscardSacOutlet` (after `SacrificePermanentAt`, before
  `TrickDraw` -- the discard is BEFORE the sacrifice, so a Gorger sacrificed to Pit Fighter must not revoke
  the snapshotted grant), `ApplyActivatePump` mode 4 (after the discard, before the pump), and
  `ApplyPermAbility` BloodDraw (after the token sacrifice, before the draw). ApplyPermAbility has no payer
  parameter: thread the `MadnessPayer` the way `ActivationDiscardPinScope` threads the pin (both trailing
  passes install it around the call).
- The madness pin rides the SAME action as `Action::discard_name` (TurnSolver.h, beside it); fold it at
  every site `discard_name` is folded (grep `discard_name`: plan_signature's SAC / APUMP / PABIL keys,
  ActionFoldSig, BpCandFingerprint, PlanDomKey, the action diff printer).
- The discard fan is built in ONE place per outlet: `ActivationDiscardClasses` (TurnSolver.cpp) feeds the
  Pit Fighter block ("FALKENRATH PIT FIGHTER" in the value-outlet loop), the Condemned mode-4 block and the
  Blood-crack block; cross each madness-capable discard class with decline + payable options there.
  Human play emits ONE unpinned action per outlet (the card -- and A1's madness answer -- is asked at
  resolution).
- `TurnSolver::PutsGyLandfallFeeder` must NOT count a discard whose madness pin CASTS the card (it does not
  reach the graveyard). `SubsetVampOutletInvalid` rule 1 counts pinned discards per name -- unchanged by
  madness. A madness-cast Vampire lowers Voldaren's Blood cost only if Blood creation runs after it
  (provider ActivationOrderRank puts Blood creation FIRST in the trailing pass today).
- My outlet sites already increment `cards_cycled_or_discarded_this_turn` themselves (the hook does not).

## Track A -- cluster A1 integrated (2026-10-18, integrator; branch `vampires-analysis`, NOT pushed)
Commits `34ccb465a` (engine + viewer + unit tests) and `51a60b624` (Scryfall snapshot, audit tweak). Cards
(cards.json, verbatim Scryfall costs): **Falkenrath Gorger** {R}, **Markov Baron** {2}{B}, **Asylum Visitor**
{1}{B} -- every clause modelled (design_A1 §1); `Keyword::Madness` (inert tag) maps Scryfall's keyword.

**Engine (shared, both worlds):**
- THE DISCARD HOOK `DiscardCardFromHand(state, ctl, idx, mq*, cause)` now carries EVERY real discard: the cleanup
  (`GameEngine::CleanupStep`, `CleanupDiscardShed` + the legacy loop), Burning-Fist (`ApplyActivatePump` mode 1),
  Neheb (`FireNehebCombatDamageDiscards`), the Troyan loot, `ApplyChannel`, `ApplyTransmute`, Celes's rummage,
  Gamble (`PerformTutor`), retrace (both worlds), cycling (D1/D2/D3), Land's Edge (both worlds, hand order kept),
  the dormant `ChooseDiscard` probe trial, and A2's three outlets. A madness card (own `madness_cost`, or a
  `grants_madness_creature_subtype` granter we control -- snapshotted at the discard) goes to exile and is queued;
  `GameState::deck_has_madness` (padding, sizeof unchanged at 1136) skips the lookup for every other deck.
- `ResolvePendingMadness` (FIFO; pin -> option by (cost, arm), stranded -> decline; unpinned -> provider
  `MadnessCastDefault`; human `madness` chooser) pays through a CALLER-OWNED creature-SPELL payer
  (`MadnessHostScope`: rollout `RolloutMadnessPayer` = TapForCostDirect, executor `AIEngine::MadnessPayer` =
  TapForCost + fresh pool; never inside the outlet's ability pay scope). `CastFromMadness` = the cast bookkeeping
  (storm / MV counters, Irencrag budget, on-cast triggers, prowess) + the shared enter cascade (D-A1-1: no
  stack traversal, provably inert for the pool). Order at the outlets: discard -> sacrifice -> madness -> effect
  (Pit Fighter's lone Gorger still grants; Condemned's pump reaches the madness-cast creature). Cleanup: after
  the CR 514.2 reset, the step's float emptied, the provider rule.
- Convoke generalised (`ConvokeRules`, `ClassifyConvokeBodiesEx`, `ApplyConvokeReduction(.., pip)`,
  `ApplyConvokeTapsForCard`, `CreatureConvokeArms`): a creature spell's pip colour, every body free after
  combat / at cleanup, a live non-mana {T} body costly (`ConvokeBodyTapAbilityLive` -- cluster B adds Bloodline
  Keeper's param there). Chord keeps the legacy rules (open question 17). Every recompute site passes the pip.
- Asylum Visitor: `PerformUpkeepHellbentDraw` (top of `GameEngine::UpkeepStep` / before the rollout's suspend
  arrivals; intervening-if on non-staged cards; one draw for N Visitors; empty library = the draw-step loss) and
  `PerformOpponentUpkeepHellbentDraw` (before `opponentdeck::EndOfTurnDraw` in both worlds; only with a dealt,
  empty opponent hand; each Visitor resolves). Own death armed (question 6).
- `greedywindow` moved verbatim to `src/core/GreedyWindow.h` (the core resolver's in-window counter reads it).

**Search:** `Action::madness_choice / madness_arm / madness_mv` (in padding: sizeof(Action) stays 480).
`MadnessFanFor` crosses each madness-capable outlet discard pin with decline + one cast per (cost x convoke arm),
provider default first, gated on the optimistic turn mana (question 14); a Gorger IN HAND opens the grant and
`SubsetVampOutletInvalid` rule 5 requires its cast. Folded into plan_signature (#M), ActionFoldSig,
BpCandFingerprint, PlanDomKey, the diff printer (all gated on >= 0). `madness_mv` rides `a.cost` as generic;
the six trailing-pass pay sites pay `OutletOwnCost(a)`. `PutsGyLandfallFeeder` ignores a discard whose pin casts
the card. Baron's hard cast: a CollectActions post-pass (arm 0 replaces the base when it taps a body; arms k
searched; rollout leaves keep arm 0); the emit prune exempts convoke creatures and gates each arm on its reduced
cost. Counters (`MTG_ROLLOUT_STATS`): `madness_variants`, `madness_unpinned_in_window`, `madness_unhosted`.

**Viewer:** bucket-B `madness` (GameLogger `MadnessChooser` + RevealLogPause / ComboOffApplyPause nulling;
call site ResolvePendingMadness, asked only for UNPINNED triggers with a payable cast; `WriteMadnessDecisionJson`
+ RunClaudePlay lambda; `madnessPanelHtml` centred card-image modal; SUBDECISIONS; DECISIONS.md row; audit
MANIFEST `madness_cost` / `grants_madness_creature_subtype` -> madness, INERT the three Visitor params --
PENDING USER OK per the self-guard). Baron's convoke arms ride `main_phase` plan variants (the Chord precedent).

**Deviations from design_A1 (and why):**
1. The pin names (cost index, convoke ARM), not raw tap counts -- a plan whose earlier casts add free bodies
   still realises "every free body"; a pinned option that no longer exists resolves as decline.
2. Emission gate optimistic, not an exact copy probe (question 14). The apply-time options ARE probed exactly.
3. A Gorger in hand opens the fan + rule 5 (the design's emission only read the pre-plan board, which would make
   "cast Gorger, then discard a Vampire for madness" unfindable).
4. Rollout leaves keep the front variant / arm 0 (question 15).
5. The human is asked only when a cast is payable (a forced decline is not a decision; the event log says it).
6. Creature-spell convoke rules are keyed on the card being a creature; Chord stays legacy (question 17).
7. Resolution-time discards outside a hosted pass (Neheb in combat, Gamble, Celes outside the trailing pass)
   have no payer -> decline (unreachable today: no current deck pairs them with a madness card).
8. `MTG_DISCARD_SHED_VERIFY` was not extended to madness outcomes (it compares shed NAMES; the madness outcome is
   covered by the unit sweep instead).

**Verification (A1, 2026-10-18):** `mtg-test` **1499/1499** (new `test/unit/test_vampires_madness.cpp`, 18
cases: card data; MadnessCostsFor incl. scope / fold / gate; the hook; decline / cast / unpayable / unpinned;
Territory pays the madness SPELL but not an ability; Pit Fighter sacrificing the lone Gorger still casts;
Condemned pumps the madness-cast Baron; convoke arms pre/post combat + Chord byte-identity; Baron's arm
enumeration; the madness fan (decline AND cast survive the dedup, both realise); a Gorger in hand + rule 5; two
Gorgers = one option; cleanup in both worlds (cast / decline, float emptied); Visitor ours (two Visitors = one
draw, staged-only hand, empty library) and the opponent's; the chooser (asked unpinned, nulled under
RevealLogPause, not asked when pinned); a rollout-vs-executor lockstep sweep over madness / convoke plans in
both mains with `g_madness_unpinned_in_window` unchanged; routing). **Smoke** (`regression.sh --smoke`, NOT
accepted): `120 passed, 8 failed`, scenarios 217/217 -- exactly cluster C's 8 rows (critter d0/d3/d5/2hg,
angels d0/d3/d5/2hg) with C's values and C's per-tier counts (searched faster=19 play-changed=136, d0 faster=65
play-changed=459, 0 slower); **every other deck byte-identical**, including every deck whose discard site moved
(Melira Pod's Chord, Minotaur's Burning-Fist / Neheb, Hinata's Gamble, Treasure Hunt's Land's Edge / retrace /
cycling, Bruna's Troyan, FiveColour's Progenitus / transmute). **Madness decision repro** (the real resolution
paths): cleanup -- `build/Release/mtg decks/Vampires/Vampire.cod --claude-play --seed 101 --game-index 0
--max-turns 9 --choices 1,2,-1,-1,4,-1,-1,11,-1,-1,11,11,0` -> exit 70, `madness` (source `cleanup`, Markov
Baron, option 0 "Cast for {2}{B}"); reply 0 puts Baron onto the battlefield, -1 into the graveyard. Outlet --
`--choices 1,2,-1,-1,4,-1,-1,11,-1,-1,9,-1,-1,77,0` (T5: Stromkirk Condemned's unpinned entry -> `discard` ->
Baron) -> exit 70, `madness` (source `Stromkirk Condemned`, options "Cast for {2}{B}" and "Cast for {2},
tapping 1 creature (convoke)"). **Sanity** `mtg decks/Vampires/Vampire.cod --games 40 --seed 777 --depth 3
--budget-ms 20 --threads 16`: no crash, avg **4.5000** (22 T4 / 16 T5 / 2 T6, **0 unwon**), 4.05 s wall at 16
threads, no game over 10 s (`--batch` SLOW-GAME), `madness_variants=219751 madness_unpinned_in_window=0
madness_unhosted=0`; a crash check at d0 (200 games, seed 4242: avg 4.915) and d5 b20 (20 games, seed 9100: avg 4.600) also kept both counters at 0. `audit_card_costs.py` **rc 0** (448/448 compared). `audit_card_fields.py --update`
(targeted) + offline: every hard field matches (rc 1 = the pre-existing unfetched Temple of Abandon).
`provider_audit.py --check` rc 0 (Vampires NOT AUDITED: no profile yet; the routing test pins Vampires ->
Vampires and Melira Pod -> MeliraPod). `audit_viewer_decisions.py --no-sweep`: expected {discard, madness,
revive, vial_charge}, oracle cross-check clean. `test/viewer_decision_types_check.js`: 37/37 answerable.

**Notes for the next integrator (cluster B: Captivating Vampire, Bloodline Keeper // Lord of Lineage):**
- Bloodline Keeper's "{T}: Create a 2/2 black Vampire token with flying" makes it a COSTLY convoke body: add its
  param to `ConvokeBodyTapAbilityLive` (SpellEffects.h) -- the creature-spell convoke rules then never count it
  free (and the post-combat collapse still leaves it costly). Captivating Vampire's "Tap five untapped Vampires
  you control" competes with convoke for the same bodies -- that is a cost on OTHER creatures, not a {T} on
  itself, so it does not belong in that predicate; the search already sees both (casts / activations in one plan).
- Madness casts enter through the shared enter path (`CastFromMadness` -> FireEtbWatchers / FireOwnEtbTriggers),
  so Keeper's five-Vampire transform count and Captivating's lord bonus see them for free.
- `sizeof(Action)` is 480 with A1's three int8 pins in padding; `sizeof(GameState)` 1136. No Action size pin
  exists. The outlet trailing passes now install `MadnessHostScope` around every ApplyPermAbility /
  ApplyActivatePump / discard-sac apply -- a new activation kind that can discard must do the same (both worlds)
  and fan its discard pin through `PushWithMadnessFan(actions, a, MadnessFanFor(...))`.
- Bloodline Keeper is still a name-only placeholder in the deck (cost "" in the viewer); it is never castable
  until B lands.


## Track B -- cluster B integrated (2026-10-18, integrator; branch `vampires-analysis`, NOT pushed)
Commits `8a0faa4cf` (engine + cards + viewer + unit tests) and `d63ce1889` (Scryfall snapshot, audit rows).
Cards (cards.json, verbatim Scryfall costs): **Captivating Vampire** {1}{B}{B}, **Bloodline Keeper** {2}{B}{B}
(was a name-only placeholder -- never in cards.json), **Lord of Lineage** (back face, `mana_cost ""`, colour
indicator B); the design §7 note lines appended to **Unclaimed Territory** / **Secluded Courtyard**; both
Keeper faces' card-level `Transform` keyword allowlisted in `scryfall_divergences.json`.

**Engine (shared resolvers, both worlds):**
- `PermAbilityMode::TapSubtypeSteal` (appended last; in `PermAbilityTaps`' exclusion list). Params
  `steal_tap_count` / `steal_tap_subtype` / `steal_adds_subtype`. Liveness (`PermAbilitySourceLive`, under a
  `StealPinScope`): the pinned target is still a creature we do not control and the pinned tap classes still
  have their untapped members -- no substitution. The apply (`ApplyPermAbility` case) chooses the TARGET
  first (CR 601.2c), then pays the cost through ONE payer `PayTapSubtypeCost` (never `CanTapNow`: sick bodies
  and the source itself are legal -- the 2010-08-15 ruling), then: `controller_index`, `gained_control_this_
  turn`, `+Vampire` (if absent), tapped status untouched, owner kept. `SacrificePermanentAt` now puts a card in
  its OWNER's graveyard (a stolen spawn sacrificed to Pit Fighter goes to the opponent's; byte-identical for
  every owned permanent).
- `PermAbilityMode::TransformSelf` (appended last; no {T}). Params `transform_back_name` / `transform_front_name`
  / `transform_cost` / `transform_min_controlled_subtype` / `_count` (NOT the daybound params). Liveness counts
  Vampire PERMANENTS (`CountControlledSubtypePermanents`) at activation; the {B} is paid under
  `CreatureAbilityPayScope` (Courtyard yes, Territory / Estate restricted mana no). The swap reuses
  `SwapDayboundFace` (same Permanent: number, counters, tapped, `entered_this_turn`; no enter cascade).
  Leaving: `NormalizeLeftDayboundFace` keys on `transform_front_name` too (Lord of Lineage -> Bloodline Keeper
  card). `CardDatabase::MdfcBackFaceNames` lists Lord of Lineage.
- In-combat window: `Plan::combat_transform_choice` (int8, sizeof(Plan) stays 464) -> `GameState::
  scripted_combat_transform` (int8 in padding, sizeof(GameState) stays 1136) -> `ApplyCombatTransforms`
  (SpellEffects.h) right after `ApplyCombatAuraSwap` in `GameEngine::CombatPhase` and `SimulateCombat`, plus
  the projection twin `CountCombatTransformPump`. Folded in the plan hash, SamePlan, IsApplyEmptyPlan,
  PlanIsAxisVariant, PlanHasCombatPin, the census, the dedup key's combat-pin slots, dominance::Build, the
  executor's realised-pin check; cleared at both turn starts.
- Keeper's live token tap makes it a COSTLY convoke body (`ConvokeBodyTapAbilityLive`, via `TapTokenSourceLive`;
  only creature-spell convoke reads it -- Markov Baron).
- Deck stamps `deck_has_transform` / `deck_has_tap_steal` (padding).

**Search (everything branched; nothing collapsed to one option in the window):**
- STEAL variants (`EmitStealVariants`): target = one per identity class of creature we do not control
  (`LandfallTargetFoldKey`), representative = lowest number; tap set = one per distinct count vector over the
  body classes summing to 5 -- FREE (cannot attack this turn: colour mask + live {T} ability) and ATTACK
  (full identity, `StealAttackBodyKey`) -- counting the bodies the plan can still ADD before its trailing pass
  (hand Vampires cast / Vial-put / madness-cast, Keeper tokens, a graveyard Bloodghast while the drop is open).
  The spec is interned (`StealTapSpec`, `Action::steal_spec` in the hole after `kind` -- sizeof(Action) stays
  480); every key folds its CONTENT (plan_signature `#T<target>#S<spec>`, ActionFoldSig, BpCandFingerprint,
  PlanDomKey, the diff printer). Both mains. A Captivating cast THIS plan is a source (needs_cast_mask bit0,
  re-resolved by name at apply: `ResolveHandSourceActivation`).
- TRANSFORM (`EmitTransformAction`): one per untransformed source (board or same-plan cast), gated on the
  UPPER BOUND of Vampires the plan can add (`SubtypeCountUpperBound`), exact restriction at apply.
- Combat window axis (AppendSubdecisionAxes): one variant per non-empty subset of the transform sources at
  the window (<= 3), pre-combat outer lists only, only while an attack-token source of the subtype is on our
  side or cast by the plan, and only when the plan's pre-combat Vampire count CANNOT already reach 5 (the
  identity fold: then the main-1 transform of the same plan reaches the same board).
- Subset rule 6 (`SubsetVampOutletInvalid`): a hand-source activation needs its source's cast; a variant whose
  arrivals (`vamp_casts_needed`) exceed what the selection can possibly add is a no-op duplicate (a necessary
  condition, counted generously).
- Trailing COUNT BANDS (`OrderTrailingActivations`, both worlds; only when a steal / transform is in the
  pass): everything else in provider order (Keeper token taps, Blood creation, Condemned, Blood crack) ->
  the steal -> the transform -> creature sacrifices (Pit Fighter). Sound: "token, then transform" and
  "Condemned's madness Baron, then steal with it" are both findable.
- Attack projection: a steal's ATTACK-class bodies are discounted (`AttackTapCtx::cand_mask`).
- Rollout leaves emit only the front steal variant (the madness / convoke-arm precedent -- the permitted
  playout policy). Unpinned autonomous steals are counted (`g_steal_unpinned`, must read 0 in the window).
- Routing: VampiresProvider's signature ORs `steal_tap_subtype == "Vampire"` and
  `transform_min_controlled_subtype == "Vampire"`.

**Viewer:** verbs `steal=` / `transform=` (plan JSON `verb`, LineSpec, CheckLine legality + matching + the
pass-check list, GUI labels at the three sites); the steal's target and five bodies are board-click `target`
prompts at resolution (forced picks not prompted); the combat transform is `dig` "(combat transform)"
(emitter note + `digPanelHtml` branch). DECISIONS.md: the verbs table, a `target` reuse row, a `dig` reuse
row, the `taptoken=` row names both Keeper faces. Auditor: MANIFEST `steal_tap_count` -> target,
`transform_back_name` -> dig, BOARD_ACTIVATIONS `verb:steal` / `verb:transform`, MAINPHASE `transform_cost`,
INERT the detail params, two STATE-GATED rows, the oracle cross-check models the steal's target.

**Deviations from design_B (and why):**
1. Tap set = an interned CLASS multiset (FREE colour/{T} codes + ATTACK representatives), not five pinned
   card numbers: bodies the plan itself adds have no number at enumeration, and a convoke cast that taps one
   twin is absorbed by the other. The FREE key drops "is source": tapping the source vs a same-colour body is
   unobservable (nothing reads Captivating being tapped).
2. `PayTapSubtypeCost` is called INSIDE the `ApplyPermAbility` case (which both trailing passes reach), not by
   the two apply sites before it -- still one shared payer, and target-before-cost holds there.
3. Trailing bands: the steal runs AFTER the provider-ordered band-0 activations (not beside the token taps):
   Condemned's / the crack's madness casts can then pay it; Blood creation loses nothing (with five Vampires
   out it already costs {0}).
4. Combat window: also seeds an empty "pass" variant when the drop is spent (the enumeration offers no empty
   plan then, and Solve's own pass never reaches the fan) -- otherwise "attack with the Keeper, transform on
   Mavren's token" was unfindable on drop-spent turns.
5. No site-9 activation keys / BP_NEW_ONLY availability bits for the two modes: a continuation's steal or
   transform therefore always reads as NEW (the safe direction; never drops a line).
6. Lord of Lineage's battlefield MV stays 0 (PROVISIONAL): not cheap -- Card has no MV field separate from
   its mana cost, and copying the front's cost would give the face a mana cost (devotion).
7. The steal variants' ORDER (move ordering + the rollout leaf's front) is a neutral engine eval (target power
   first, then least attack power given up, then fewest arrivals), not a provider hook (open question 22).

**Verification (B, 2026-10-18):** `mtg-test` **1514/1514** (new `test/unit/test_vampires_lords.cpp`, 16 cases:
card data + back-face art list; Keeper token (sick -> none; 2/2 black flying sick Vampire, Lieutenant +1;
live tap = costly convoke body); transform restriction 4 vs 5 (token counts, opponent's Vampire does not;
sick / tapped Keeper legal); swap fidelity (number, counter, tapped, sick; no Socialite counter; others +2/+2,
not itself; one-shot); Lord sacrificed -> Bloodline Keeper card; payment (Swamp / Courtyard yes, Territory
no); reachability (fifth Vampire cast this plan with rule 6b; token-then-transform in both action orders; a
Keeper cast this plan); the combat window (axis fans it; unpinned holds; pinned transforms after Mavren's
token, pays {B}, +2 to the attacker; no fifth Vampire = clean no-op); steal legality (4 no, 5 with sick
source yes, tapped source no, no target no); steal effect (five tapped incl. sick + itself, controller /
owner / sickness / Vampire / three lords, next-turn attack, owner's graveyard); folds (three 1/1s = one
target class; tap classes fold the source with same-colour bodies -- 4 sets of 6 choose 5); stale pin = no-op;
same-plan bodies + self-tap of a Captivating cast this plan; rollout-vs-executor lockstep over every steal /
transform / token plan in both mains with `g_steal_unpinned` unchanged; routing on the B cards alone).
**Smoke** (`regression.sh --smoke`, NOT accepted): `120 passed, 8 failed` -- EVERY case digest identical to the
A1 run, i.e. exactly cluster C's 8 rows (critter d0/d3/d5/2hg, angels d0/d3/d5/2hg), searched faster=19
play-changed=136, d0 faster=65 play-changed=459, 0 slower; nothing else moved. **Sanity**
`--games 40 --seed 777 --depth 3 --budget-ms 20 --threads 16`: avg **4.4250** (24 T4 / 15 T5 / 1 T6, 0 unwon;
was 4.5000), 4.87 s wall (was 4.05 s), 931 core-ms/game; `--games 20 --depth 5 --budget-ms 40`: avg **4.4000**,
5.46 s wall, 1,291 core-ms/game; no game over 10 s (`--batch`, MTG_SLOW_GAME_MS=10000). Well under the 8,907
ms/game suite cap. `audit_card_costs.py` **rc 0** (450/450 compared). `audit_card_fields.py --update`
(targeted) + offline: every hard field matches (rc 1 = the pre-existing unfetched Temple of Abandon).
`audit_viewer_decisions.py --no-sweep` **rc 0**, oracle cross-check clean; `viewer_decision_types_check.js`
37/37; `provider_audit.py --check` rc 0 (Vampires NOT AUDITED: no profile yet).
**Viewer repros** (claude-play, real resolution paths):
- steal TARGET: `build/Release/mtg decks/Vampires/Vampire.cod --claude-play --seed 128 --game-index 8
  --max-turns 9 --choices 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0` -> exit 70 `target` (source Captivating Vampire,
  "taken under your control (it becomes a Vampire)", 6/6 and 1/1); the T5 menu's plan 0 carries
  `"verb": "transform"` + `"verb": "steal"`; reply 0 -> the 6/6 is ours (the five bodies were forced: exactly
  five untapped Vampires).
- steal BODIES: `--choices 1` + eighteen `0` -> `target` "tapped to pay its cost (1 of 5)" (T6; the lone 1/1
  is a forced target), Captivating itself and the stolen 6/6 among the legal bodies.
- combat transform: `--seed 254 --game-index 8 --max-turns 9 --choices 1,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,1`
  -> exit 70 `dig` "Bloodline Keeper (combat transform)", default -1; reply 0 -> "Bloodline Keeper transforms
  into Lord of Lineage", the attack deals 19 (13 with -1).
- `--validate-line "transform=Bloodline Keeper;steal=Captivating Vampire"` (same T5 state) -> accept plan 0;
  `steal=` alone -> plan 2, `transform=` alone -> plan 1.

**PROVISIONAL (user sign-off owed):** own creatures not steal targets; no opponent-turn window (steal, token
tap); attached Auras/Equipment on a stolen creature unmodelled (spawns carry none); Lord of Lineage MV 0;
Flying (Keeper, Lord, tokens) inert; one steal per Captivating per main phase.

## Open questions -- cluster B (work continues on the stated default)
20. **Steal targets exclude our own creatures** (a legal no-op; the Bolas -2 precedent). [default: excluded,
    PROVISIONAL]
21. **Rollout leaves take only the front steal variant** (the playout policy beyond the horizon; the searched
    collect and the executor keep every variant). [default: on, PROVISIONAL]
22. **Steal variant ORDER lives in the enumerator** (target power, then least attack power given up, then
    fewest arrivals) -- ordering only, and the rollout leaf's front. Should it be a VampiresProvider hook
    (USER-owned like the other Vampires draft orders)? [default: engine-neutral order, keep]
23. **Combat-transform fold**: no combat variant for a plan whose pre-combat count can already reach five (its
    main-1 transform reaches the same board from the same mana). [default: on -- an identity fold]
24. **The combat transform's {B} has no main-phase mana hold**: if the plan's casts tap the only black source a
    combat variant resolves as its base plan (a duplicate). Not observed in the repros; a hold (the aura-swap
    shape) is a follow-up if the sweep shows it. [default: no hold]
25. **Lord of Lineage's battlefield mana value reads 0** (CR 712.8e says 4) -- unobservable in this list.
    [default: disclose, PROVISIONAL]
26. **A sacrificed permanent goes to its OWNER's graveyard** (SacrificePermanentAt) -- a rules fix for every
    deck, byte-identical in the suite (no suite deck sacrifices a creature it does not own). [default: keep]

## Open questions -- orchestrator / cluster C (work continues on the stated default)
27. **CritterLifegain/claude_s6_gi5 now replays T4 (recorded T5)** under the faithful first-strike step (see
    Status). The checker has no override mechanism by design, so the regression tier's reference gate stays red
    on this one reference until the USER rules. [default: reference untouched; drift disclosed]
28. **Baseline profile `min_playable: 0`** (the analyzer's output; Soldiers / Pirates / Colossus ship the same;
    the skill's old note asks >= 1 for multi-colour decks). Keep policy is USER-owned and the keep table
    replaces it. [default: as generated]
29. **Attack projection does not count first-step lifelink feeding Thune / Pridemate / life thresholds**
    (integration_C_notes): the search's damage estimate undercounts there (the safe direction); rollouts are
    exact. [default: leave]
30. **Rick, Steadfast Leader keeps choosing vigilance + lifelink over first strike** -- correct for Soldiers,
    which has nothing between the damage steps. [default: unchanged]
31. **New combat params classified as no-viewer-decision** (INERT_PARAMS). [default: as classified]

## Sweep fixes (Stage 5d claude-play convergence, 2026-10-18; branch `vampires-fix`, NOT pushed)
Fix integrator over the sweep's defect list (items A-L; repro CSVs in the sweep scratch DEFECTS.md). Commits
`c2d643522` (A-E, G-K), `6939bd4d3` (F + L, the payer sub-integrator's `6e6497968` cherry-picked) and the F x L
follow-up (the activation-line hold). The old CSVs do not replay index-for-index on the new binary (the convoke
arms and the human filters change the menus), so every repro below was re-driven with a summary-matching
translator (scratch `fix/repro/translate.py`) and the human menus checked by an all-or-nothing probe
(`fix/repro/allornothing.py`: execute every plan holding a Vampires activation, verify each listed activation's
event).

**A -- phantom convoke mana (SERIOUS).** Root cause: the creature-spell convoke bodies were tapped in a PRE-PASS
over the whole plan (executor TakeTurn / ApplyPlanDirect / the continuation replays), each Baron's committed
(pip, other) counts against the pre-plan board, while every payment site reduced the cost by the COMMITTED
counts -- two Markov Barons on the same three bodies tapped three creatures and were both reduced by three (seed
91011 T3: "Territory; Baron, Baron" resolved with all three lands untapped). Fix: creature-spell convoke taps AT
THE CAST (`ConvokeCastTaps`, SpellEffects.h), cheapest first on the board as it stands, and the cost comes down by
the bodies ACTUALLY tapped -- one helper in CastSpellFromHand, apply_one and the sequential-payability walk; a
failed payment untaps them; the pre-pass is a no-op for creature spells (Chord keeps it: Melira Pod
byte-identical). Subset rule 7 (`SubsetConvokeBodiesInvalid`, both walkers) refuses a selection whose arms claim
more bodies than the board plus the plan's own arrivals hold (the last convoke cast serves nobody). Under
contention (a second convoke creature, a steal source) fewer-free arms down to the plain cast are offered, so
"Baron A takes the bodies, Baron B pays lands" stays findable. Tests: `test_vampires_convoke.cpp` "Convoke A" x2
(mana conservation in both worlds over every two-Baron plan; a stale arm reduces only by its real taps and
untaps on failure). Repro: 91011 T3 menu holds no phantom plan (the 9-mana "Baron, Baron, Captain" and the
land-less "Baron, Baron" are gone) and every two-Baron plan executes conserving mana (e.g. plan 8: Bloodghast +
Dire Moon + the first Baron + 3 lands = 6).

**B -- same-plan convoke bodies (reachability).** Root cause: arms came from the pre-plan board and the pre-pass
ran before any cast, so a creature the plan casts first (sick = a free body) could never convoke -- unfindable at
any budget. Fix: arms may count arrivals (`Action::convoke_plan_pip / _other`; every split between arrivals and
would-attack board bodies is its own arm), `ApplyConvokeArrivalDependencyOrder` (end of both
ApplyPayableCastOrder forms -- every apply site in both worlds) casts such an arm after its arrivals, rule 7
requires them, and the human ordering search drops an order that does not (`OrderingPlacesConvokeBeforeArrivals`).
Tests: "Convoke B" x2 (Dire Moon then Baron convoked by it on three Swamps, both worlds; the dependency order).
Repro: 91016 T5 menu offers "Falkenrath Pit Fighter, Markov Baron (convoke: Legion Lieutenant #24, Asylum
Visitor #5, Falkenrath Pit Fighter #21)"; 91005 handed to the search at d5 b200 now plays T3 "Condemned,
Lieutenant, Baron convoked by Condemned + Lieutenant + Gorger" (4 to the face vs 3; both arms win T4, opp -12
vs -6). The sweep's "6 on T3" line needs mana the board does not have; not reproduced.

**C -- arm order ignored lords and the pip colour.** Root cause: `ClassifyConvokeBodiesEx` sorted would-attack
bodies by `EffectivePower()` (no lord term) and only PREFIX arms of that list existed. Fix: creature rules rank by
`CombatPowerOf` (`ConvokeRules::true_power`) and the arms are the full (pip, other) grid, one per (reduced cost,
plan need) keeping the least attack power given up (ties: fewer costly, fewer pip bodies); the USER's question-5
default (lowest power first) now applies to TRUE power. Test: "Convoke C" (both "tap the black Baron, pays {B},
loses 2" and "tap the lord-pumped Gorger, pays {1}, loses 3" are offered; costly order by true power). Repro: 91000
T5 offers "Drana, Markov Baron (convoke: Drana #15, Markov Baron #28)" -- the Gorger stays attacking.

**D -- convoke arms printed identically.** Fix: every creature-convoke cast's summary and JSON (`"convoke": {pip,
other, plan_bodies, taps}`) name the bodies it REALLY taps, off a probe apply with a depth-gated recorder
(`g_convoke_tap_record`; main.cpp `ConvokeLabelsFor`) -- a Bloodghast the plan's land returns and a creature the
plan casts first are named; the convoke event and the madness chooser labels name bodies too; human-menu twins
that realise the same bodies (board arm vs plan-body arm once the arrival is cast first) are hidden in the capped
menu. Test: "Sweep D" (the recorder names "Vampire of the Dire Moon #10"). Repro: the 91011 / 91016 menus above.

**E -- outlets whose discard the plan's own land / casts empty.** Root cause: human play emits the discard outlets
UNPINNED, and rule 1 counted pinned discards only; the executor then skipped the pump silently. Fix: subset rule
8 (`SubsetDiscardOutletsUnpayable`, both walkers): every discard outlet, pinned or not, needs a card at its turn in
the EXECUTED trailing order (OrderTrailingActivations), net of the selection's casts / puts and the earlier
outlets' draws (the land drop is already off the walker's board -- EnumeratePlansWithLand plays it first -- so the
coordinator's land-drop note needs no extra term). The search never credits a dead pump (the trailing pass
checks liveness first). Test: "Sweep E" (pump + crack on one card rejected; a cast that empties the hand
rejected; both accepted with a second card; the executed order). Repro: 91009 / 91011 / 91013 / 91007 -- the
phantom plan has NO match in the new menu at that decision; all-or-nothing on those menus: 6 + 24 + 46 + 2 plans,
0 partial.

**F / L -- the payer (sub-integrator, `6939bd4d3`).** Root cause: Voldaren Estate, Unclaimed Territory and
Secluded Courtyard tie at rank 59, so battlefield order picked; the backtracker returned its first DFS hit, and the
painland {C}-first hoist only covered generic pips. Fix: `MTG_PAY_PAIN_LAST` (a life-costing coloured mode is the
last resort for coloured pips too, in the greedy, the backtracker -- a painless solve first, the old solve as the
fallback -- and the prepay shrink) and `MTG_PAY_NARROW_RESTRICTED_FIRST` (a creature-spell-only colour before a
Courtyard), both default ON, heurarm, lossless; armed Prevent Damage boards keep their pain. Tests:
`test_vampires_payer.cpp` (15 cases, 8 fail with the levers =0). Repros (life before -> after): 91017 19->20,
91012 20->21, 91018 17->18, 91013 18->19, 91007 17/20 -> 18/20, 91004 17/19 -> 18/19, 91016 plans 48/52 19 -> 20;
91001 now taps the Territory for the cast and keeps the Courtyard for Pit Fighter's {1}{R}. **F x L follow-up**
(the sub-integrator's open question 1): pain-last sent a creature SPELL's pip to the Courtyard and Pit Fighter's
own {R} (a creature ABILITY: Territory / Estate colour cannot pay it) no-op'd. `PlanTraits::act_pips_cab` now
carries Pit Fighter's and the transform's pips and `ActLineHoldMask` reserves an ABILITY-LEGAL provider for them;
both trailing passes bracket Pit Fighter's payment in `ActLinePayScope`. Test: "payer F x L (search)".

**G -- duplicate Blood plans for identical Estates.** Fix: TapBlood actions carry the interchangeable-source
`equiv_tag` (the k variant rides the tag), so the canonical-prefix fold drops the twin wherever it applies (the
greedy walk always; the search walk under the deck opt-in, `FoldSearchOdometerOptIn`, which Vampires does not
take -- open question 33); the human menu (never folded at enumeration) hides the twin. Test: "Sweep G" (with the
fold arm pinned on, single-Blood plans use one Estate; before: both).

**H -- transform missing from human plans that reach five.** Root cause: human play emitted the transform only
when the restriction already held on the pre-plan board. Fix: human play takes the search's gate (the plan can
reach five; rule 6b checks arrivals), board Keeper and a same-plan Keeper alike. Repro: 91012 T5 now offers
"Territory; Aether Vial, Legion Lieutenant, Bloodline Keeper: transform it" and it executes (Lord of Lineage).

**I / all-or-nothing -- a transform only Territory could pay; a pump that ran while its transform died.** Fix (human
menu): a plan holding a Vampires activation (steal / transform / Blood / the discard outlets) is probed on a copy
(land fixed as the frame decided it) and dropped when its apply SKIPS any activation (`g_trailing_act_skips`,
counted at the trailing pass's not-live / unpaid branches); the same plan without the dead activation is its own
entry. Repro: 91012 T5 with only Territory untapped offers no transform; 91004 T8 offers the transform only with
the Swamp land drop (all-or-nothing on that menu: 52 plans, 0 partial). The AUTONOMOUS creature-ability pool
still counts a colored_creature_only land as any colour (the USER withdrew MTG_CCO_NONCREATURE_POOL as
enumeration hygiene that must earn its place; the narrow creature-ability analogue is the same class) -- open
question 32.

**J -- listed order vs executed order.** Root cause: the menu listed the plan vector; the trailing pass runs in
OrderTrailingActivations order (VampiresProvider's draft: Blood creation -> Condemned -> crack -> Pit Fighter).
Pricing parity holds: the Blood variant counts only casts / puts (rule 3) and runs first in both worlds, so it is
priced and realised at the same {1}. Fix: a Vampires plan lists its trailing activations in the executed order
(main.cpp `DisplayActionOrder`, summary + JSON). Repro: 91015 T5 now reads "Voldaren Estate: create a Blood
token, Stromkirk Condemned: Vampires +1/+1". The ORDER itself is open question 34.

**K -- the batch prepay ignored convoke.** Root cause: BatchPrepayMainCasts' combined bill recomputed each cast
from EffectiveCost and never applied the convoke reduction (Chord never reached it: X spells decline). Fix: the
bill applies the arm's reduction (the bodies tap at the cast; rule 7 makes the bill match). Test: "Convoke K"
(Pit Fighter + a 3-body Baron: one land, nothing floating, the Estate untapped, in both worlds). Repro: 91016 T5
"Pit Fighter, Baron (3 bodies), Estate: Blood" taps one land for {R} and makes the Blood (life 20).

**Verification.** `mtg-test` 1540/1540 (1514 + convoke 7 + sweep 4 + payer 15). The enumeration-level convoke
cases (A conservation, B found, K one land, C the black-Baron arm) were also built against the PRE-fix tree
(1c4484e7e, a scratch worktree, removed): all four FAIL there (A: 6 mana cast off 3 bodies and 0 lands; B: no such
plan; K: 4 lands tapped, {3} floating, life 19, the Estate tapped; C: no such arm). The rules-7/8 / recorder /
fold cases drive predicates that did not exist before. **Smoke** (`regression.sh
--smoke`, NOT accepted), my commit alone: 128 pass / 4 fail -- exactly the four Vampires rows, every other deck
byte-identical (Melira Pod included). With F/L + the F x L hold: 106 / 26; Vampires
`vampires_smoke_d0` 4.8300 -> 4.7000, `_d3` 4.4667 -> 4.3800, `_d5` 4.4267 -> 4.3467, `vampires2hg_d3` 5.0400 ->
4.8800 (searched: 0 slower, 27 faster; d0: 17 slower, 147 faster -- the slower d0 games checked are the removed
phantom: e.g. gi12 T3 old "Baron, Baron" on two lands + three bodies). Non-Vampires: 153 moved games, every one
reproduced as a payer-lever move from a single-game repro (a pooled batch of the 153 with both levers =0
reproduces the committed GT digest 153/153; mechanism per game from a levers-off vs -on log diff): 117 a coloured
pip paid without pain (life +1), 12 a different (painless / narrower) land tapped, 6 a same-name copy swap, 15 an
extra cast the saved colour funds, 3 a different searched line at the same score; score moves only at d0 --
faster: treasure_hunt gi146 8->7, gi450 6->5, minotaur gi444 6->5 (Territories pay the cast, the Courtyard funds
Burning-Fist's {1}{R}), pirates gi530 5->4 (Fiery Islet's {R} left for Lightning Bolt); slower: soldiers gi49 5->6,
gi817 4->5 (the payment is painless and keeps Silent Clearing; the d0 runner then sacrifice-cycles the Clearing).
Searched non-Vampires moves: 37 games, all at the same score. **Sanity** (`--batch`, MTG_SLOW_GAME_MS=10000):
d3 b20 40 games seed 777 avg **4.3750** (was 4.4250), 5.56 s wall, 1,011 core-ms/game; d5 b40 20 games avg
**4.3500** (was 4.4000), 6.05 s wall, 1,317 core-ms/game; no game over 10 s.

## Open questions -- sweep fixes (work continues on the stated default)
32. **Autonomous creature-ability pool**: a colored_creature_only land still counts as any colour for a creature
    ABILITY's pip in the search (the transform / Pit Fighter variants the payer then cannot fund are rollout
    duplicates, never wrong lines). Same class as the USER-withdrawn MTG_CCO_NONCREATURE_POOL. [default: unchanged;
    the human menu's all-or-nothing probe removes the phantom there]
33. **Vampires FoldSearchOdometerOptIn** (the canonical-prefix fold on the search's own walk -- identical Estates,
    identical hand casts): per-deck, verified-before-adoption. [default: not taken]
34. **The Vampires trailing order** (AGENT-DRAFTED, question 11) runs Blood creation BEFORE the discard outlets, so a
    madness-cast Vampire from Condemned / the crack never discounts the Blood (91015), and "crack first, then pump the
    drawn card" is reachable only through the crack's hand-entry breakpoint. Options: Blood after Condemned / the
    crack, or search the order. [default: unchanged -- the order is yours]
35. **Plan-body convoke arms in rollout leaves**: beyond the horizon only arm 0 (free board bodies) is offered, as
    for every other convoke variant (question 15's playout policy). [default: on]
36. **Payer over-tap with a held activation pip** (observed, not caused here): 91012 T5 "Vial, Lieutenant, transform"
    taps five lands for four mana ({C} floats) -- the prepay ladder under MTG_ACT_LINE_HOLD. Payment quality only.
    [default: disclose; follow-up]
37. **The payer sub-integrator's open questions** (in `6939bd4d3`): MTG_HUMAN_PREPAY_DEMAND still outranks pain-last
    (91016 plan 50 costs a life in human play only); the backtracker's needless first-candidate taps; recorded
    `--tap-pref` taps still win over the new order in reference replays. [defaults: as shipped]

## Payer A/B -- the slower games root-caused (2026-10-18, branch `vampires-payer2`, NOT pushed)
The pooled held-out A/B (`test/tools/payer_pain_ab.py`, 8 decks x {std d0, d3 b10, d5 b20, 2hg d3 b10} x blocks
A/B, both levers on vs =0, 81,600 games) left six slower games and two decks net slower (vampires +1,
creature_giving +1). Per game, from a levers-on vs -off single-game log diff (MTG_TRACE_SOLVE /
MTG_FSW_TRACE / the new MTG_TRACE_SOLVE_BOARD):

- **vampires gi129 block B (d3 b10 and d5 b20, T5 -> T6) -- DEFECT, fixed.** The budgeted root skipped its T1
  land drop. Pass 1's horizon leaf (the 1-ply SimulateToEnd) under "T1 Voldaren Estate" played T4 "Courtyard,
  Pit Fighter" and paid the {R} with Secluded Courtyard (painless) instead of an Estate's pay-1-life colour; main
  2's Pit Fighter draw-two ({1}{R}, a creature ABILITY -- only the Courtyard's colour or an unrestricted {R} may
  pay it) then had no legal {R}, the leaf's kill slipped T5 -> T7, the T1 roots scored Estate 7 / no-land 6, and
  the 8x costlier pass 1 (7,987 units vs 963) left no budget for pass 2. Unbudgeted (d3 b0, d8 b0) both arms win
  T5. Sweep defect L one phase later: the activation-line hold (`act_pips_cab`) sees an activation only inside the
  plan, and Pit Fighter's lost-life restriction makes its activation a main-2 plan. Fix: a main-1 plan that
  attacks with such an outlet on board or among its casts owes the outlet's pips as creature-ability demand
  (`ComputePlanTraits`, NARROW-gated; Pit Fighter is the only card it matches). Now T5 at both budgets.
- **creature_giving gi24 block B (d0, T4 -> T5) -- DEFECT, fixed.** Pain-last tapped the Forbidden Orchards for
  Birds' and Crop Rotation's {G}; the additional cost (tapped-first, Orchard last) then sacrificed the UNTAPPED
  City of Brass and the plan's Sylvan Scrying (payable by the walk, which never models the sacrifice) had one
  land. Fix: `SacLandPayVictim` (SpellEffects.h) publishes the land the cost will take as the sac-fodder victim
  around the cast's payment, both worlds (pain-last-gated). Its first build promoted the ranking's FRONT and
  sacrificed a Stomping Ground over a Forest (gi1565 block A, T7 -> T8, a new slower game in the re-run); it now
  promotes the LEAST flexible best-band land that can pay a pip of the cost -- what the scarcity payer would have
  tapped from that band. Burn (Shard Volley, all Mountains) is byte-identical.
- **th gi1155 block A (d0, T6 -> T9 = a loss) -- legitimate.** The painless order routes both Fiery Islet taps
  through the Cascade Bluffs, so T5 pays TWO Treasure Hunts for the 2 life the old order spent on ONE (both
  Islets for a single {1}{U}); the d0 runner casts the second, it draws the rest of the library (no nonland card
  left), and the T6 draw decks it. Off could not afford the second Hunt, cast it T6 with Land's Edge and won.
  The payment is strictly better; the deck-out is the d0 policy's (open question 38).
- **minotaur gi83 block A (d0, T5 -> T6) -- legitimate.** Narrow-first pays Slaughter-Priest with the two
  Territories and keeps the Courtyard + Blood Crypt, so the d0 runner can now pump Burning-Fist on T4; it
  discards Bloodstained Mire for it, and T5 then has neither the fetch's Slaughter-Priest sacrifice trigger
  (+2/+0) nor a card for the T5 pump -- 2 short of off's T5 kill. Same total pump damage; the discard choice and
  the timing are the d0 policy's.
- **soldiers gi1554 block B (d0, T5 -> T6) -- legitimate** (the probe's gi49/817 class). Champion's {W} comes
  off a Territory (painless) instead of Silent Clearing (1 life); the d0 runner then sacrifice-cycles the
  untapped Clearing with the other Territory, is a land short on T4 and cannot activate Recruitment Officer.

**Re-run** (same manifest, final binary, one pooled batch, 22 threads): per-deck net vampires -1, auras 0,
creature_giving -3, minotaur -2, pirates -2, selesnya 0, soldiers 0 (d0 +1, 2hg -1), th -4; 0 slower in any
searched cell; slower games: th gi1155, minotaur gi83, soldiers gi1554 (the three d0 games above). The =0 arm is
byte-identical to the first run (64/64 wins files, digests included). Tests: `test_vampires_payer.cpp` "payer
A/B ..." (gi129 both worlds + scoping; gi24 both worlds and both arms; the predictor's band / tapped / cost
rules; gi1565's least-flexible pick).

38. **A flood engine that empties the library** (th gi1155): the d0 runner casts a DrawUntilNonland Treasure
    Hunt when the library holds no nonland card and no same-turn kill, and decks itself. A provider prune
    (TreasureHuntProvider::ShouldCastDrawEngine) would be the place. [default: unchanged -- d0 policy, yours]

## Open questions -- Stage 5f search cost + sweep 2 (work continues on the stated default)
39. **Subset rule 9 (`MTG_VAMP_M2_PUMP_DISCARD_FOLD`) is a DOMINANCE fold shipped ON.** Its deck half rests on a
    reading of this list: after combat nothing reads power, and nothing reads the graveyard or a discard except
    Bloodghast and madness (both exempted, as are an emptied hand with Asylum Visitor out, > 7 cards at cleanup,
    and a plan that draws). Sign-off? [default: ON]
40. **Breakpoint CHAIN arm off for Vampires** (the per-deck setting five other decks use): -1.1% to -2.2% units,
    -8.1% on 2HG gi34, win turns identical. [default: unchanged (on)]
41. **A general EMPTY-arm fold** (skip a breakpoint's "cast nothing more" arm when it repeats its base plan; 100% of
    gi34's empty arms did): ~8% on gi34, ~2% on the 2HG cell; touches breakpoint code every deck uses and moves
    every deck's GT. [default: not built]
42. **The search-walk canonical-prefix fold** is still question 33: -0.1% to -0.4% here, byte-identical; needs its
    verification run. [default: not taken]
43. **The Vampires trailing order hides two cheaper-Blood lines** (extends question 34). The AGENT-DRAFTED order runs
    Blood before Condemned's pump and the Blood crack, and the count band runs the steal after them, so "pump
    discarding a madness Vampire, then a cheaper Blood" and "steal, then a cheaper Blood" cannot be expressed
    within one main phase (only the main-2 Blood substitutes). Candidate amendment: token taps -> pump -> crack ->
    steal -> Blood -> transform -> sacrifices (risk: a madness cast paying first could tap the Estate for mana;
    the activation hold reserves it first). [default: unchanged -- the order is yours; not measured]
44. **The 5c2 48-block re-run** (the script's own suggestion after NO SIGN; ~96k d5 games, ~4-5 h). Not run: at
    0 / 12,000 the larger sample could only confirm the lever does not bind. [default: keep ON, not re-run]
45. **COST GATE: Vampires is over 3x** (see "Stage 5 measurements + the 3x COST GATE"). Options: (a) land the rows
    (an over-3x entry, USER override) and generate the mulligan profile, whose keep table removes the 68-76% of units
    that are bottoming rollouts (measured: then ~1.6x Snow); (b) keep the deck out of the suite until a perf route
    gets it under 3x first (no keep table is allowed before suite membership, so (b) needs a non-mulligan route --
    the remaining folds are questions 40-42, each a few %). [default: neither -- rows stay on the branch, nothing
    landed, no generator started; I would recommend (a)]
46. **The cost cache's Galadriel reference is stale** (2,969 vs ~94 ms/game today), so the 3x gate is mis-calibrated
    for every deck. [default: refill with `suite_gate.py --measure-all --no-run` off the next full regression run
    and commit `test/suite_cost.json` with this branch]
47. **Bucket amendments from the d0 root-cause** (cast-order doc 5.1): (1) Keeper's delayed value x0.75 (Keeper 12 ->
    10.5); (2) duplicate copies priced at cumulative cost. Each changes searched play too, so each needs the same A/B.
    [default: buckets as drafted, OFF; amendments not tried]
48. **Depth-0 runner blind spots** (hit both arms equally; they make d0 a noisy test for any discard / order rule):
    the lethal check ignores a lord cast this turn, Condemned's pump and Keeper's transform; convoke tapping attackers
    is not counted as lost attack; the Blood's +1 breaks ties against casting a lord; a colour-infeasible madness
    cast scored as payable (2343, no effect). The d0 runner is the permitted greedy, so this is a quality work item,
    not a no-greedy defect. [default: not taken up]
49. **The trailing order is a fixed sequence per plan, not ordering-only** (the Stage 6a audit; the provider class
    header calls it ordering). Beyond questions 34 / 43, three more lines are inexpressible within one main: Keeper
    token -> a hand-cast Baron convoking it (casts run before activations), steal -> Baron convoking the stolen body,
    steal -> Pit Fighter sacrificing the stolen creature. Main 2 or a breakpoint recovers some. [default: disclosed]
50. **Question 4's cleanup-madness "always cast" argument misses Asylum Visitor**: casting the last card at cleanup
    empties the hand, and the Visitor's upkeep draw costs 1 life, lethal at 1 life under own-death checks.
    Theoretical. [default: unchanged]
51. **Rollout madness pin** (D1 fix): beyond the horizon the single madness variant is now the default cover chosen
    on the pre-plan board and realised with the cheapest bodies at the trigger (it no longer uses free bodies the
    plan adds first). Alternative: compute the rollout's pin on a board including those arrivals. [default: as built]
52. **GUI: Markov Baron's hard-cast convoke arm cannot be picked** (`CheckLine` has no convoke dimension, so arms with
    the same casts collapse to the first payable one; claude-play lists every arm). [default: viewer follow-up]
53. **Viewer auditor coverage**: `sacrifice` is never an expected decision type (Pit Fighter's `sac_creature_outlet`
    is classed INERT), so the dynamic sweep never checks that prompt (the claude-play sweeps did, by hand); and the
    INERT rows for Asylum Visitor and cluster C's combat params await your OK (question 31). [default: as classified]

