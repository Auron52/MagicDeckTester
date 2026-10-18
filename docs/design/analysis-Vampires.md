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
   [default: one-option rule, PROVISIONAL]
5. **Convoke tap order** for the attackers it taps: lowest power first vs a provider order keeping Drana /
   Mavren untapped. [default: lowest power first]
6. **Own-death checks on for Vampires** (Asylum Visitor's mandatory loss, Voldaren Estate's pay-1-life).
   [default: on]
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
