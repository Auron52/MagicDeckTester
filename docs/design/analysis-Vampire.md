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
