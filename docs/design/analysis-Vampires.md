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
    run Courtyard + a sac outlet. [default: fix, explain moves]
11. **VampiresProvider orders** (discard / victim / activation drafts, design_A2 §8) and, later, the
    Vampires cast order and discard buckets — AGENT-DRAFTED, USER reviews.

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
